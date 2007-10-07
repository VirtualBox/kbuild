/*	$NetBSD: input.c,v 1.39 2003/08/07 09:05:32 agc Exp $	*/

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

#ifdef HAVE_SYS_CDEFS_H
#include <sys/cdefs.h>
#endif
#ifndef lint
#if 0
static char sccsid[] = "@(#)input.c	8.3 (Berkeley) 6/9/95";
#else
__RCSID("$NetBSD: input.c,v 1.39 2003/08/07 09:05:32 agc Exp $");
#endif
#endif /* not lint */

#include <stdio.h>	/* defines BUFSIZ */
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#ifdef __sun__
#include <iso/limits_iso.h>
#endif

/*
 * This file implements the input routines used by the parser.
 */

#include "shell.h"
#include "redir.h"
#include "syntax.h"
#include "input.h"
#include "output.h"
#include "options.h"
#include "memalloc.h"
#include "error.h"
#include "alias.h"
#include "parser.h"
#include "myhistedit.h"
#include "shinstance.h"

#define EOF_NLEFT -99		/* value of parsenleft when EOF pushed back */

//MKINIT
//struct strpush {
//	struct strpush *prev;	/* preceding string on stack */
//	char *prevstring;
//	int prevnleft;
//	int prevlleft;
//	struct alias *ap;	/* if push was associated with an alias */
//};
//
///*
// * The parsefile structure pointed to by the global variable parsefile
// * contains information about the current file being read.
// */
//
//MKINIT
//struct parsefile {
//	struct parsefile *prev;	/* preceding file on stack */
//	int linno;		/* current line */
//	int fd;			/* file descriptor (or -1 if string) */
//	int nleft;		/* number of chars left in this line */
//	int lleft;		/* number of chars left in this buffer */
//	char *nextc;		/* next char in buffer */
//	char *buf;		/* input buffer */
//	struct strpush *strpush; /* for pushing strings at this level */
//	struct strpush basestrpush; /* so pushing one is fast */
//};
//
//
//int plinno = 1;			/* input line number */
//int parsenleft;			/* copy of parsefile->nleft */
//MKINIT int parselleft;		/* copy of parsefile->lleft */
//char *parsenextc;		/* copy of parsefile->nextc */
//MKINIT struct parsefile basepf;	/* top level input file */
//MKINIT char basebuf[BUFSIZ];	/* buffer for top level input file */
//struct parsefile *parsefile = &basepf;	/* current input file */
//int init_editline = 0;		/* editline library initialized? */
//int whichprompt;		/* 1 == PS1, 2 == PS2 */
//
//#ifndef SMALL
//EditLine *el;			/* cookie for editline package */
//#endif

STATIC void pushfile(shinstance *psh);
static int preadfd(shinstance *psh);

#ifdef mkinit
INCLUDE <stdio.h>
INCLUDE "input.h"
INCLUDE "error.h"

INIT {
	psh->basepf.nextc = psh->basepf.buf = psh->basebuf;
}

RESET {
	if (psh->exception != EXSHELLPROC)
		psh->parselleft = psh->parsenleft = 0;	/* clear input buffer */
	popallfiles(psh);
}

SHELLPROC {
	popallfiles(psh);
}
#endif


/*
 * Read a line from the script.
 */

char *
pfgets(shinstance *psh, char *line, int len)
{
	char *p = line;
	int nleft = len;
	int c;

	while (--nleft > 0) {
		c = pgetc_macro(psh);
		if (c == PEOF) {
			if (p == line)
				return NULL;
			break;
		}
		*p++ = c;
		if (c == '\n')
			break;
	}
	*p = '\0';
	return line;
}



/*
 * Read a character from the script, returning PEOF on end of file.
 * Nul characters in the input are silently discarded.
 */

int
pgetc(shinstance *psh)
{
	return pgetc_macro(psh);
}


static int
preadfd(shinstance *psh)
{
	int nr;
	char *buf = psh->parsefile->buf;
	psh->parsenextc = buf;

retry:
#ifndef SMALL
	if (psh->parsefile->fd == 0 && psh->el) {
		static const char *rl_cp;
		static int el_len;

		if (rl_cp == NULL)
			rl_cp = el_gets(psh->el, &el_len);
		if (rl_cp == NULL)
			nr = 0;
		else {
			nr = el_len;
			if (nr > BUFSIZ - 8)
				nr = BUFSIZ - 8;
			memcpy(buf, rl_cp, nr);
			if (nr != el_len) {
				el_len -= nr;
				rl_cp += nr;
			} else
				rl_cp = 0;
		}

	} else
#endif
		nr = shfile_read(&psh->fdtab, psh->parsefile->fd, buf, BUFSIZ - 8);


	if (nr <= 0) {
                if (nr < 0) {
                        if (errno == EINTR)
                                goto retry;
                        if (psh->parsefile->fd == 0 && errno == EWOULDBLOCK) {
                                int flags = shfile_fcntl(&psh->fdtab, 0, F_GETFL, 0);
                                if (flags >= 0 && flags & O_NONBLOCK) {
                                        flags &=~ O_NONBLOCK;
                                        if (shfile_fcntl(&psh->fdtab, 0, F_SETFL, flags) >= 0) {
						out2str(psh, "sh: turning off NDELAY mode\n");
                                                goto retry;
                                        }
                                }
                        }
                }
                nr = -1;
	}
	return nr;
}

/*
 * Refill the input buffer and return the next input character:
 *
 * 1) If a string was pushed back on the input, pop it;
 * 2) If an EOF was pushed back (parsenleft == EOF_NLEFT) or we are reading
 *    from a string so we can't refill the buffer, return EOF.
 * 3) If the is more stuff in this buffer, use it else call read to fill it.
 * 4) Process input up to the next newline, deleting nul characters.
 */

int
preadbuffer(shinstance *psh)
{
	char *p, *q;
	int more;
	int something;
	char savec;

	if (psh->parsefile->strpush) {
		popstring(psh);
		if (--psh->parsenleft >= 0)
			return (*psh->parsenextc++);
	}
	if (psh->parsenleft == EOF_NLEFT || psh->parsefile->buf == NULL)
		return PEOF;
	flushout(&psh->output);
	flushout(&psh->errout);

again:
	if (psh->parselleft <= 0) {
		if ((psh->parselleft = preadfd(psh)) == -1) {
			psh->parselleft = psh->parsenleft = EOF_NLEFT;
			return PEOF;
		}
	}

	q = p = psh->parsenextc;

	/* delete nul characters */
	something = 0;
	for (more = 1; more;) {
		switch (*p) {
		case '\0':
			p++;	/* Skip nul */
			goto check;

		case '\t':
		case ' ':
			break;

		case '\n':
			psh->parsenleft = (int)(q - psh->parsenextc);
			more = 0; /* Stop processing here */
			break;

		default:
			something = 1;
			break;
		}

		*q++ = *p++;
check:
		if (--psh->parselleft <= 0) {
			psh->parsenleft = (int)(q - psh->parsenextc - 1);
			if (psh->parsenleft < 0)
				goto again;
			*q = '\0';
			more = 0;
		}
	}

	savec = *q;
	*q = '\0';

#ifndef SMALL
	if (psh->parsefile->fd == 0 && hist && something) {
		HistEvent he;
		INTOFF;
		history(hist, &he, psh->whichprompt == 1? H_ENTER : H_APPEND,
		    psh->parsenextc);
		INTON;
	}
#endif

	if (vflag(psh)) {
		out2str(psh, psh->parsenextc);
		flushout(psh->out2);
	}

	*q = savec;

	return *psh->parsenextc++;
}

/*
 * Undo the last call to pgetc.  Only one character may be pushed back.
 * PEOF may be pushed back.
 */

void
pungetc(shinstance *psh)
{
	psh->parsenleft++;
	psh->parsenextc--;
}

/*
 * Push a string back onto the input at this current parsefile level.
 * We handle aliases this way.
 */
void
pushstring(shinstance *psh, char *s, size_t len, void *ap)
{
	struct strpush *sp;

	INTOFF;
/*dprintf("*** calling pushstring: %s, %d\n", s, len);*/
	if (psh->parsefile->strpush) {
		sp = ckmalloc(sizeof (struct strpush));
		sp->prev = psh->parsefile->strpush;
		psh->parsefile->strpush = sp;
	} else
		sp = psh->parsefile->strpush = &(psh->parsefile->basestrpush);
	sp->prevstring = psh->parsenextc;
	sp->prevnleft = psh->parsenleft;
	sp->prevlleft = psh->parselleft;
	sp->ap = (struct alias *)ap;
	if (ap)
		((struct alias *)ap)->flag |= ALIASINUSE;
	psh->parsenextc = s;
	psh->parsenleft = (int)len;
	INTON;
}

void
popstring(shinstance *psh)
{
	struct strpush *sp = psh->parsefile->strpush;

	INTOFF;
	psh->parsenextc = sp->prevstring;
	psh->parsenleft = sp->prevnleft;
	psh->parselleft = sp->prevlleft;
/*dprintf("*** calling popstring: restoring to '%s'\n", psh->parsenextc);*/
	if (sp->ap)
		sp->ap->flag &= ~ALIASINUSE;
	psh->parsefile->strpush = sp->prev;
	if (sp != &(psh->parsefile->basestrpush))
		ckfree(sp);
	INTON;
}

/*
 * Set the input to take input from a file.  If push is set, push the
 * old input onto the stack first.
 */

void
setinputfile(shinstance *psh, const char *fname, int push)
{
	int fd;
	int fd2;

	INTOFF;
/** @todo shfile fixme */
	if ((fd = shfile_open(&psh->fdtab, fname, O_RDONLY)) < 0)
		error(psh, "Can't open %s", fname);
	if (fd < 10) {
		fd2 = copyfd(psh, fd, 10);
		shfile_close(&psh->fdtab, fd);
		if (fd2 < 0)
			error(psh, "Out of file descriptors");
		fd = fd2;
	}
	setinputfd(psh, fd, push);
	INTON;
}


/*
 * Like setinputfile, but takes an open file descriptor.  Call this with
 * interrupts off.
 */

void
setinputfd(shinstance *psh, int fd, int push)
{
	(void) shfile_fcntl(&psh->fdtab, fd, F_SETFD, FD_CLOEXEC);
	if (push) {
		pushfile(psh);
		psh->parsefile->buf = ckmalloc(BUFSIZ);
	}
	if (psh->parsefile->fd > 0)
		shfile_close(&psh->fdtab, psh->parsefile->fd);
	psh->parsefile->fd = fd;
	if (psh->parsefile->buf == NULL)
		psh->parsefile->buf = ckmalloc(BUFSIZ);
	psh->parselleft = psh->parsenleft = 0;
	psh->plinno = 1;
}


/*
 * Like setinputfile, but takes input from a string.
 */

void
setinputstring(shinstance *psh, char *string, int push)
{
	INTOFF;
	if (push)
		pushfile(psh);
	psh->parsenextc = string;
	psh->parselleft = psh->parsenleft = (int)strlen(string);
	psh->parsefile->buf = NULL;
	psh->plinno = 1;
	INTON;
}



/*
 * To handle the "." command, a stack of input files is used.  Pushfile
 * adds a new entry to the stack and popfile restores the previous level.
 */

STATIC void
pushfile(shinstance *psh)
{
	struct parsefile *pf;

	psh->parsefile->nleft = psh->parsenleft;
	psh->parsefile->lleft = psh->parselleft;
	psh->parsefile->nextc = psh->parsenextc;
	psh->parsefile->linno = psh->plinno;
	pf = (struct parsefile *)ckmalloc(sizeof (struct parsefile));
	pf->prev = psh->parsefile;
	pf->fd = -1;
	pf->strpush = NULL;
	pf->basestrpush.prev = NULL;
	psh->parsefile = pf;
}


void
popfile(shinstance *psh)
{
	struct parsefile *pf = psh->parsefile;

	INTOFF;
	if (pf->fd >= 0)
		shfile_close(&psh->fdtab, pf->fd);
	if (pf->buf)
		ckfree(pf->buf);
	while (pf->strpush)
		popstring(psh);
	psh->parsefile = pf->prev;
	ckfree(pf);
	psh->parsenleft = psh->parsefile->nleft;
	psh->parselleft = psh->parsefile->lleft;
	psh->parsenextc = psh->parsefile->nextc;
	psh->plinno = psh->parsefile->linno;
	INTON;
}


/*
 * Return to top level.
 */

void
popallfiles(shinstance *psh)
{
	while (psh->parsefile != &psh->basepf)
		popfile(psh);
}



/*
 * Close the file(s) that the shell is reading commands from.  Called
 * after a fork is done.
 *
 * Takes one arg, vfork, which tells it to not modify its global vars
 * as it is still running in the parent.
 *
 * This code is (probably) unnecessary as the 'close on exec' flag is
 * set and should be enough.  In the vfork case it is definitely wrong
 * to close the fds as another fork() may be done later to feed data
 * from a 'here' document into a pipe and we don't want to close the
 * pipe!
 */

void
closescript(shinstance *psh, int vforked)
{
	if (vforked)
		return;
	popallfiles(psh);
	if (psh->parsefile->fd > 0) {
		shfile_close(&psh->fdtab, psh->parsefile->fd);
		psh->parsefile->fd = 0;
	}
}
