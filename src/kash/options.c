/*	$NetBSD: options.c,v 1.38 2005/03/20 21:38:17 dsl Exp $	*/

/*-
 * Copyright (c) 1991, 1993
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
static char sccsid[] = "@(#)options.c	8.2 (Berkeley) 5/4/95";
#else
__RCSID("$NetBSD: options.c,v 1.38 2005/03/20 21:38:17 dsl Exp $");
#endif /* not lint */
#endif

#include <stdlib.h>

#include "shell.h"
#define DEFINE_OPTIONS
#include "options.h"
#undef DEFINE_OPTIONS
#include "nodes.h"	/* for other header files */
#include "eval.h"
#include "jobs.h"
#include "input.h"
#include "output.h"
#include "trap.h"
#include "var.h"
#include "memalloc.h"
#include "error.h"
#include "mystring.h"
#ifndef SMALL
# include "myhistedit.h"
#endif
#include "show.h"
#include "shinstance.h"

//char *arg0;			/* value of $0 */
//struct shparam shellparam;	/* current positional parameters */
//char **argptr;			/* argument list for builtin commands */
//char *optionarg;		/* set by nextopt (like getopt) */
//char *optptr;			/* used by nextopt */

//char *minusc;			/* argument to -c option */


STATIC void options(shinstance *, int);
STATIC void minus_o(shinstance *, char *, int);
STATIC void setoption(shinstance *, int, int);
STATIC int getopts(shinstance *, char *, char *, char **, char ***, char **);


/*
 * Process the shell command line arguments.
 */

void
procargs(shinstance *psh, int argc, char **argv)
{
	int i;

	psh->argptr = argv;
	if (argc > 0)
		psh->argptr++;
	for (i = 0; i < NOPTS; i++)
		psh->optlist[i].val = 2;
	options(psh, 1);
	if (*psh->argptr == NULL && psh->minusc == NULL)
		sflag(psh) = 1;
	if (iflag(psh) == 2 && sflag(psh) == 1 && shfile_isatty(&psh->fdtab, 0) && shfile_isatty(&psh->fdtab, 1))
		iflag(psh) = 1;
	if (mflag(psh) == 2)
		mflag(psh) = iflag(psh);
	for (i = 0; i < NOPTS; i++)
		if (psh->optlist[i].val == 2)
			psh->optlist[i].val = 0;
#if DEBUG == 2
	debug(psh) = 1;
#endif
	psh->arg0 = argv[0];
	if (sflag(psh) == 0 && psh->minusc == NULL) {
		psh->commandname = argv[0];
		psh->arg0 = *psh->argptr++;
		setinputfile(psh, psh->arg0, 0);
		psh->commandname = psh->arg0;
	}
	/* POSIX 1003.2: first arg after -c cmd is $0, remainder $1... */
	if (psh->minusc != NULL) {
		if (psh->argptr == NULL || *psh->argptr == NULL)
			error(psh, "Bad -c option");
		psh->minusc = *psh->argptr++;
		if (*psh->argptr != 0)
			psh->arg0 = *psh->argptr++;
	}

	psh->shellparam.p = psh->argptr;
	psh->shellparam.reset = 1;
	/* assert(shellparam.malloc == 0 && shellparam.nparam == 0); */
	while (*psh->argptr) {
		psh->shellparam.nparam++;
		psh->argptr++;
	}
	optschanged(psh);
}


void
optschanged(shinstance *psh)
{
	setinteractive(psh, iflag(psh));
#ifndef SMALL
	histedit(psh);
#endif
	setjobctl(psh, mflag(psh));
}

/*
 * Process shell options.  The global variable argptr contains a pointer
 * to the argument list; we advance it past the options.
 */

STATIC void
options(shinstance *psh, int cmdline)
{
	static char empty[] = "";
	char *p;
	int val;
	int c;

	if (cmdline)
		psh->minusc = NULL;
	while ((p = *psh->argptr) != NULL) {
		psh->argptr++;
		if ((c = *p++) == '-') {
			val = 1;
                        if (p[0] == '\0' || (p[0] == '-' && p[1] == '\0')) {
                                if (!cmdline) {
                                        /* "-" means turn off -x and -v */
                                        if (p[0] == '\0')
                                                xflag(psh) = vflag(psh) = 0;
                                        /* "--" means reset params */
                                        else if (*psh->argptr == NULL)
						setparam(psh, psh->argptr);
                                }
				break;	  /* "-" or  "--" terminates options */
			}
		} else if (c == '+') {
			val = 0;
		} else {
			psh->argptr--;
			break;
		}
		while ((c = *p++) != '\0') {
			if (c == 'c' && cmdline) {
				/* command is after shell args*/
				psh->minusc = empty;
			} else if (c == 'o') {
				minus_o(psh, *psh->argptr, val);
				if (*psh->argptr)
					psh->argptr++;
			} else {
				setoption(psh, c, val);
			}
		}
	}
}

static void
set_opt_val(shinstance *psh, int i, int val)
{
	int j;
	int flag;

	if (val && (flag = psh->optlist[i].opt_set)) {
		/* some options (eg vi/emacs) are mutually exclusive */
		for (j = 0; j < NOPTS; j++)
		    if (psh->optlist[j].opt_set == flag)
			psh->optlist[j].val = 0;
	}
	psh->optlist[i].val = val;
#ifdef DEBUG
	if (&psh->optlist[i].val == &debug(psh))
		opentrace(psh);
#endif
}

STATIC void
minus_o(shinstance *psh, char *name, int val)
{
	int i;

	if (name == NULL) {
		out1str(psh, "Current option settings\n");
		for (i = 0; i < NOPTS; i++)
			out1fmt(psh, "%-16s%s\n", psh->optlist[i].name,
				psh->optlist[i].val ? "on" : "off");
	} else {
		for (i = 0; i < NOPTS; i++)
			if (equal(name, psh->optlist[i].name)) {
				set_opt_val(psh, i, val);
				return;
			}
		error(psh, "Illegal option -o %s", name);
	}
}


STATIC void
setoption(shinstance *psh, int flag, int val)
{
	int i;

	for (i = 0; i < NOPTS; i++)
		if (psh->optlist[i].letter == flag) {
			set_opt_val(psh, i, val);
			return;
		}
	error(psh, "Illegal option -%c", flag);
	/* NOTREACHED */
}



#ifdef mkinit
INCLUDE "options.h"

INIT {
	memcpy(&psh->optlist[0], &ro_optlist[0], sizeof(psh->optlist));
}

SHELLPROC {
	int i;

	for (i = 0; psh->optlist[i].name; i++)
		psh->optlist[i].val = 0;
	optschanged(psh);

}
#endif


/*
 * Set the shell parameters.
 */

void
setparam(shinstance *psh, char **argv)
{
	char **newparam;
	char **ap;
	int nparam;

	for (nparam = 0 ; argv[nparam] ; nparam++)
		continue;
	ap = newparam = ckmalloc(psh, (nparam + 1) * sizeof *ap);
	while (*argv) {
		*ap++ = savestr(psh, *argv++);
	}
	*ap = NULL;
	freeparam(psh, &psh->shellparam);
	psh->shellparam.malloc = 1;
	psh->shellparam.nparam = nparam;
	psh->shellparam.p = newparam;
	psh->shellparam.optnext = NULL;
}


/*
 * Free the list of positional parameters.
 */

void
freeparam(shinstance *psh, volatile struct shparam *param)
{
	char **ap;

	if (param->malloc) {
		for (ap = param->p ; *ap ; ap++)
			ckfree(psh, *ap);
		ckfree(psh, param->p);
	}
}



/*
 * The shift builtin command.
 */

int
shiftcmd(shinstance *psh, int argc, char **argv)
{
	int n;
	char **ap1, **ap2;

	n = 1;
	if (argc > 1)
		n = number(psh, argv[1]);
	if (n > psh->shellparam.nparam)
		error(psh, "can't shift that many");
	INTOFF;
	psh->shellparam.nparam -= n;
	for (ap1 = psh->shellparam.p ; --n >= 0 ; ap1++) {
		if (psh->shellparam.malloc)
			ckfree(psh, *ap1);
	}
	ap2 = psh->shellparam.p;
	while ((*ap2++ = *ap1++) != NULL);
	psh->shellparam.optnext = NULL;
	INTON;
	return 0;
}



/*
 * The set command builtin.
 */

int
setcmd(shinstance *psh, int argc, char **argv)
{
	if (argc == 1)
		return showvars(psh, 0, 0, 1);
	INTOFF;
	options(psh, 0);
	optschanged(psh);
	if (*psh->argptr != NULL) {
		setparam(psh, psh->argptr);
	}
	INTON;
	return 0;
}


void
getoptsreset(shinstance *psh, const char *value)
{
	if (number(psh, value) == 1) {
		psh->shellparam.optnext = NULL;
		psh->shellparam.reset = 1;
	}
}

/*
 * The getopts builtin.  Shellparam.optnext points to the next argument
 * to be processed.  Shellparam.optptr points to the next character to
 * be processed in the current argument.  If shellparam.optnext is NULL,
 * then it's the first time getopts has been called.
 */

int
getoptscmd(shinstance *psh, int argc, char **argv)
{
	char **optbase;

	if (argc < 3)
		error(psh, "usage: getopts optstring var [arg]");
	else if (argc == 3)
		optbase = psh->shellparam.p;
	else
		optbase = &argv[3];

	if (psh->shellparam.reset == 1) {
		psh->shellparam.optnext = optbase;
		psh->shellparam.optptr = NULL;
		psh->shellparam.reset = 0;
	}

	return getopts(psh, argv[1], argv[2], optbase, &psh->shellparam.optnext,
		       &psh->shellparam.optptr);
}

STATIC int
getopts(shinstance *psh, char *optstr, char *optvar, char **optfirst, char ***optnext, char **optpptr)
{
	char *p, *q;
	char c = '?';
	int done = 0;
	int ind = 0;
	int err = 0;
	char s[12];

	if ((p = *optpptr) == NULL || *p == '\0') {
		/* Current word is done, advance */
		if (*optnext == NULL)
			return 1;
		p = **optnext;
		if (p == NULL || *p != '-' || *++p == '\0') {
atend:
			ind = (int)(*optnext - optfirst + 1);
			*optnext = NULL;
			p = NULL;
			done = 1;
			goto out;
		}
		(*optnext)++;
		if (p[0] == '-' && p[1] == '\0')	/* check for "--" */
			goto atend;
	}

	c = *p++;
	for (q = optstr; *q != c; ) {
		if (*q == '\0') {
			if (optstr[0] == ':') {
				s[0] = c;
				s[1] = '\0';
				err |= setvarsafe(psh, "OPTARG", s, 0);
			} else {
				outfmt(&psh->errout, "Illegal option -%c\n", c);
				(void) unsetvar(psh, "OPTARG", 0);
			}
			c = '?';
			goto bad;
		}
		if (*++q == ':')
			q++;
	}

	if (*++q == ':') {
		if (*p == '\0' && (p = **optnext) == NULL) {
			if (optstr[0] == ':') {
				s[0] = c;
				s[1] = '\0';
				err |= setvarsafe(psh, "OPTARG", s, 0);
				c = ':';
			} else {
				outfmt(&psh->errout, "No arg for -%c option\n", c);
				(void) unsetvar(psh, "OPTARG", 0);
				c = '?';
			}
			goto bad;
		}

		if (p == **optnext)
			(*optnext)++;
		err |= setvarsafe(psh, "OPTARG", p, 0);
		p = NULL;
	} else
		err |= setvarsafe(psh, "OPTARG", "", 0);
	ind = (int)(*optnext - optfirst + 1);
	goto out;

bad:
	ind = 1;
	*optnext = NULL;
	p = NULL;
out:
	*optpptr = p;
	fmtstr(s, sizeof(s), "%d", ind);
	err |= setvarsafe(psh, "OPTIND", s, VNOFUNC);
	s[0] = c;
	s[1] = '\0';
	err |= setvarsafe(psh, optvar, s, 0);
	if (err) {
		*optnext = NULL;
		*optpptr = NULL;
		output_flushall(psh);
		exraise(psh, EXERROR);
	}
	return done;
}

/*
 * XXX - should get rid of.  have all builtins use getopt(3).  the
 * library getopt must have the BSD extension static variable "optreset"
 * otherwise it can't be used within the shell safely.
 *
 * Standard option processing (a la getopt) for builtin routines.  The
 * only argument that is passed to nextopt is the option string; the
 * other arguments are unnecessary.  It return the character, or '\0' on
 * end of input.
 */

int
nextopt(shinstance *psh, const char *optstring)
{
	char *p;
	const char *q;
	char c;

	if ((p = psh->optptr) == NULL || *p == '\0') {
		p = *psh->argptr;
		if (p == NULL || *p != '-' || *++p == '\0')
			return '\0';
		psh->argptr++;
		if (p[0] == '-' && p[1] == '\0')	/* check for "--" */
			return '\0';
	}
	c = *p++;
	for (q = optstring ; *q != c ; ) {
		if (*q == '\0')
			error(psh, "Illegal option -%c", c);
		if (*++q == ':')
			q++;
	}
	if (*++q == ':') {
		if (*p == '\0' && (p = *psh->argptr++) == NULL)
			error(psh, "No arg for -%c option", c);
		psh->optionarg = p;
		p = NULL;
	}
	psh->optptr = p;
	return c;
}
