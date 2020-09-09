/*	$NetBSD: alias.c,v 1.12 2003/08/07 09:05:29 agc Exp $	*/

/*-
 * Copyright (c) 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Kenneth Almquist.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#if 0
#ifndef lint
static char sccsid[] = "@(#)alias.c	8.3 (Berkeley) 5/4/95";
#else
__RCSID("$NetBSD: alias.c,v 1.12 2003/08/07 09:05:29 agc Exp $");
#endif /* not lint */
#endif

#include <stdlib.h>
#include "shell.h"
#include "input.h"
#include "output.h"
#include "error.h"
#include "memalloc.h"
#include "mystring.h"
#include "alias.h"
#include "options.h"	/* XXX for argptr (should remove?) */
#include "var.h"
#include "shinstance.h"

/*#define ATABSIZE 39

struct alias *atab[ATABSIZE];*/

STATIC void setalias(shinstance *, char *, char *);
STATIC int unalias(shinstance *, char *);
STATIC struct alias **hashalias(shinstance *, char *);

#ifndef SH_FORKED_MODE
void
subshellinitalias(shinstance *psh, shinstance *inherit)
{
    unsigned i;
    unsigned left = inherit->aliases;
    if (left == 0)
	return;
    for (i = 0; i < K_ELEMENTS(inherit->atab); i++)
    {
	struct alias const *asrc = inherit->atab[i];
	if (asrc)
	{
	    struct alias **ppdst = &psh->atab[i];
	    do
	    {
		if (*asrc->name)
		{
		    struct alias *dst = (struct alias *)ckmalloc(psh, sizeof(*dst));
		    dst->name = savestr(psh, asrc->name);
		    dst->val = savestr(psh, asrc->val);
		    dst->flag = asrc->flag;
		    *ppdst = dst;
		    ppdst = &dst->next;
		}
		left--;
		asrc = asrc->next;
	    } while (asrc);
	    *ppdst = NULL;
	    if (left == 0)
		break;
	}
    }
}
#endif /* !SH_FORKED_MODE */

STATIC
void
setalias(shinstance *psh, char *name, char *val)
{
	struct alias *ap, **app;

	app = hashalias(psh, name);
	for (ap = *app; ap; ap = ap->next) {
		if (equal(name, ap->name)) {
			INTOFF;
			ckfree(psh, ap->val);
			ap->val	= savestr(psh, val);
			INTON;
			return;
		}
	}
	/* not found */
	INTOFF;
	ap = ckmalloc(psh, sizeof (struct alias));
	ap->name = savestr(psh, name);
	/*
	 * XXX - HACK: in order that the parser will not finish reading the
	 * alias value off the input before processing the next alias, we
	 * dummy up an extra space at the end of the alias.  This is a crock
	 * and should be re-thought.  The idea (if you feel inclined to help)
	 * is to avoid alias recursions.  The mechanism used is: when
	 * expanding an alias, the value of the alias is pushed back on the
	 * input as a string and a pointer to the alias is stored with the
	 * string.  The alias is marked as being in use.  When the input
	 * routine finishes reading the string, it markes the alias not
	 * in use.  The problem is synchronization with the parser.  Since
	 * it reads ahead, the alias is marked not in use before the
	 * resulting token(s) is next checked for further alias sub.  The
	 * H A C K is that we add a little fluff after the alias value
	 * so that the string will not be exhausted.  This is a good
	 * idea ------- ***NOT***
	 */
#ifdef notyet
	ap->val = savestr(psh, val);
#else /* hack */
	{
	size_t len = strlen(val);
	ap->val = ckmalloc(psh, len + 2);
	memcpy(ap->val, val, len);
	ap->val[len] = ' ';	/* fluff */
	ap->val[len+1] = '\0';
	}
#endif
	ap->next = *app;
	*app = ap;
	psh->aliases++;
	INTON;
}

STATIC int
unalias(shinstance *psh, char *name)
{
	struct alias *ap, **app;

	app = hashalias(psh, name);

	for (ap = *app; ap; app = &(ap->next), ap = ap->next) {
		if (equal(name, ap->name)) {
			/*
			 * if the alias is currently in use (i.e. its
			 * buffer is being used by the input routine) we
			 * just null out the name instead of freeing it.
			 * We could clear it out later, but this situation
			 * is so rare that it hardly seems worth it.
			 */
			if (ap->flag & ALIASINUSE)
				*ap->name = '\0';
			else {
				INTOFF;
				*app = ap->next;
				ckfree(psh, ap->name);
				ckfree(psh, ap->val);
				ckfree(psh, ap);
				psh->aliases--;
				INTON;
			}
			return (0);
		}
	}

	return (1);
}

#ifdef mkinit
MKINIT void rmaliases(shinstance *psh);

SHELLPROC {
	rmaliases(psh);
}
#endif

void
rmaliases(shinstance *psh)
{
	struct alias *ap, *tmp;
	int i;

	INTOFF;
	for (i = 0; i < ATABSIZE; i++) {
		ap = psh->atab[i];
		psh->atab[i] = NULL;
		while (ap) {
			ckfree(psh, ap->name);
			ckfree(psh, ap->val);
			tmp = ap;
			ap = ap->next;
			ckfree(psh, tmp);
		}
	}
	INTON;
}

struct alias *
lookupalias(shinstance *psh, char *name, int check)
{
	struct alias *ap = *hashalias(psh, name);

	for (; ap; ap = ap->next) {
		if (equal(name, ap->name)) {
			if (check && (ap->flag & ALIASINUSE))
				return (NULL);
			return (ap);
		}
	}

	return (NULL);
}

char *
get_alias_text(shinstance *psh, char *name)
{
	struct alias *ap;

	ap = lookupalias(psh, name, 0);
	if (ap == NULL)
		return NULL;
	return ap->val;
}

/*
 * TODO - sort output
 */
int
aliascmd(shinstance *psh, int argc, char **argv)
{
	char *n, *v;
	int ret = 0;
	struct alias *ap;

	if (argc == 1) {
		int i;

		for (i = 0; i < ATABSIZE; i++)
			for (ap = psh->atab[i]; ap; ap = ap->next) {
				if (*ap->name != '\0') {
					out1fmt(psh, "alias %s=", ap->name);
					print_quoted(psh, ap->val);
					out1c(psh, '\n');
				}
			}
		return (0);
	}
	while ((n = *++argv) != NULL) {
		if ((v = strchr(n+1, '=')) == NULL) { /* n+1: funny ksh stuff */
			if ((ap = lookupalias(psh, n, 0)) == NULL) {
				outfmt(psh->out2, "alias: %s not found\n", n);
				ret = 1;
			} else {
				out1fmt(psh, "alias %s=", n);
				print_quoted(psh, ap->val);
				out1c(psh, '\n');
			}
		} else {
			*v++ = '\0';
			setalias(psh, n, v);
		}
	}

	return (ret);
}

int
unaliascmd(shinstance *psh, int argc, char **argv)
{
	int i;

	while ((i = nextopt(psh, "a")) != '\0') {
		if (i == 'a') {
			rmaliases(psh);
			return (0);
		}
	}
	for (i = 0; *psh->argptr; psh->argptr++)
		i = unalias(psh, *psh->argptr);

	return (i);
}

STATIC struct alias **
hashalias(shinstance *psh, char *p)
{
	unsigned int hashval;

	hashval = *p << 4;
	while (*p)
		hashval+= *p++;
	return &psh->atab[hashval % ATABSIZE];
}
