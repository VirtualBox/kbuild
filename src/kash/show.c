/*	$NetBSD: show.c,v 1.26 2003/11/14 10:46:13 dsl Exp $	*/

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
static char sccsid[] = "@(#)show.c	8.3 (Berkeley) 5/4/95";
#else
__RCSID("$NetBSD: show.c,v 1.26 2003/11/14 10:46:13 dsl Exp $");
#endif /* not lint */
#endif

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <assert.h>

#include "shell.h"
#include "parser.h"
#include "nodes.h"
#include "mystring.h"
#include "show.h"
#include "options.h"
#include "shinstance.h"


#ifdef DEBUG
static void shtree(union node *, int, char *, FILE*);
static void shcmd(union node *, FILE *);
static void sharg(union node *, FILE *);
static void indent(int, char *, FILE *);
static void trstring(shinstance *, char *);


void
showtree(shinstance *psh, union node *n)
{
	trputs(psh, "showtree called\n");
	shtree(n, 1, NULL, stdout);
}


static void
shtree(union node *n, int ind, char *pfx, FILE *fp)
{
	struct nodelist *lp;
	const char *s;

	if (n == NULL)
		return;

	indent(ind, pfx, fp);
	switch(n->type) {
	case NSEMI:
		s = "; ";
		goto binop;
	case NAND:
		s = " && ";
		goto binop;
	case NOR:
		s = " || ";
binop:
		shtree(n->nbinary.ch1, ind, NULL, fp);
	   /*    if (ind < 0) */
			fputs(s, fp);
		shtree(n->nbinary.ch2, ind, NULL, fp);
		break;
	case NCMD:
		shcmd(n, fp);
		if (ind >= 0)
			putc('\n', fp);
		break;
	case NPIPE:
		for (lp = n->npipe.cmdlist ; lp ; lp = lp->next) {
			shcmd(lp->n, fp);
			if (lp->next)
				fputs(" | ", fp);
		}
		if (n->npipe.backgnd)
			fputs(" &", fp);
		if (ind >= 0)
			putc('\n', fp);
		break;
	default:
		fprintf(fp, "<node type %d>", n->type);
		if (ind >= 0)
			putc('\n', fp);
		break;
	}
}



static void
shcmd(union node *cmd, FILE *fp)
{
	union node *np;
	int first;
	const char *s;
	int dftfd;

	first = 1;
	for (np = cmd->ncmd.args ; np ; np = np->narg.next) {
		if (! first)
			putchar(' ');
		sharg(np, fp);
		first = 0;
	}
	for (np = cmd->ncmd.redirect ; np ; np = np->nfile.next) {
		if (! first)
			putchar(' ');
		switch (np->nfile.type) {
			case NTO:	s = ">";  dftfd = 1; break;
			case NCLOBBER:	s = ">|"; dftfd = 1; break;
			case NAPPEND:	s = ">>"; dftfd = 1; break;
			case NTOFD:	s = ">&"; dftfd = 1; break;
			case NFROM:	s = "<";  dftfd = 0; break;
			case NFROMFD:	s = "<&"; dftfd = 0; break;
			case NFROMTO:	s = "<>"; dftfd = 0; break;
			default:  	s = "*error*"; dftfd = 0; break;
		}
		if (np->nfile.fd != dftfd)
			fprintf(fp, "%d", np->nfile.fd);
		fputs(s, fp);
		if (np->nfile.type == NTOFD || np->nfile.type == NFROMFD) {
			fprintf(fp, "%d", np->ndup.dupfd);
		} else {
			sharg(np->nfile.fname, fp);
		}
		first = 0;
	}
}



static void
sharg(union node *arg, FILE *fp)
{
	char *p;
	struct nodelist *bqlist;
	int subtype;

	if (arg->type != NARG) {
		printf("<node type %d>\n", arg->type);
		abort();
	}
	bqlist = arg->narg.backquote;
	for (p = arg->narg.text ; *p ; p++) {
		switch (*p) {
		case CTLESC:
			putc(*++p, fp);
			break;
		case CTLVAR:
			putc('$', fp);
			putc('{', fp);
			subtype = *++p;
			if (subtype == VSLENGTH)
				putc('#', fp);

			while (*p != '=')
				putc(*p++, fp);

			if (subtype & VSNUL)
				putc(':', fp);

			switch (subtype & VSTYPE) {
			case VSNORMAL:
				putc('}', fp);
				break;
			case VSMINUS:
				putc('-', fp);
				break;
			case VSPLUS:
				putc('+', fp);
				break;
			case VSQUESTION:
				putc('?', fp);
				break;
			case VSASSIGN:
				putc('=', fp);
				break;
			case VSTRIMLEFT:
				putc('#', fp);
				break;
			case VSTRIMLEFTMAX:
				putc('#', fp);
				putc('#', fp);
				break;
			case VSTRIMRIGHT:
				putc('%', fp);
				break;
			case VSTRIMRIGHTMAX:
				putc('%', fp);
				putc('%', fp);
				break;
			case VSLENGTH:
				break;
			default:
				printf("<subtype %d>", subtype);
			}
			break;
		case CTLENDVAR:
		     putc('}', fp);
		     break;
		case CTLBACKQ:
		case CTLBACKQ|CTLQUOTE:
			putc('$', fp);
			putc('(', fp);
			shtree(bqlist->n, -1, NULL, fp);
			putc(')', fp);
			break;
		default:
			putc(*p, fp);
			break;
		}
	}
}


static void
indent(int amount, char *pfx, FILE *fp)
{
	int i;

	for (i = 0 ; i < amount ; i++) {
		if (pfx && i == amount - 1)
			fputs(pfx, fp);
		putc('\t', fp);
	}
}
#endif



#ifdef DEBUG
/*
 * Debugging stuff.
 */

/** @def TRY_GET_PSH_OR_RETURN
 * Make sure @a psh is valid, trying to fetch it from TLS
 * if it's NULL and returning (void) if that fails. */
# define TRY_GET_PSH_OR_RETURN(psh)  \
	if (!(psh)) { \
		(psh) = shthread_get_shell(); \
		if (!(psh)) \
			return; \
	} else do { } while (0)

/** @def RETURN_IF_NOT_TRACING
 * Return if we're not tracing. */
# define RETURN_IF_NOT_TRACING(psh) \
   if (debug(psh) != 1 || psh->tracefd == -1) \
   	return; \
   else do	{} while (0)

/* Flushes the tracebuf. */
static void
trace_flush(shinstance *psh)
{
	size_t pos = psh->tracepos;

	if (pos > sizeof(psh->tracebuf)) {
		char *end;
		assert(0);
		end = memchr(psh->tracebuf, '\0', sizeof(psh->tracebuf));
		pos = end ? end - &psh->tracebuf[0] : 0;
	}

	if (pos) {
		int     s = errno;
		char 	prefix[40];
		size_t 	len;

#ifdef SH_FORKED_MODE
		len = sprintf(prefix, "[%" SHPID_PRI "] ", sh_getpid(psh));
#else
		shpid pid = sh_getpid(psh);
		len = sprintf(prefix, "[%d/%d] ", SHPID_GET_PID(pid), SHPID_GET_TID(pid));
#endif
		shfile_write(&psh->fdtab, psh->tracefd, prefix, len);
		shfile_write(&psh->fdtab, psh->tracefd, psh->tracebuf, pos);

		psh->tracepos = 0;
		psh->tracebuf[0] = '\0';

		errno = s;
	}
}

/* Adds a char to the trace buffer. */
static void
trace_char(shinstance *psh, int c)
{
	size_t pos = psh->tracepos;
	if (pos >= sizeof(psh->tracebuf) - 1) {
		trace_flush(psh);
		pos = psh->tracepos;
	}
	psh->tracebuf[pos] = c;
	psh->tracepos = pos + 1;
	if (c == '\n')
		trace_flush(psh);
	else
		psh->tracebuf[pos + 1] = '\0';
}

/* Add a string to the trace buffer. */
static void
trace_string(shinstance *psh, const char *str)
{
	/* push it out line by line. */
	while (*str) {
		/* find line/string length. */
		size_t		pos;
		size_t 		len;
		const char *end = str;
		int 		flush_it = 0;
		while (*end) {
			if (*end++ == '\n') {
				flush_it = 1;
				break;
			}
		}
		len = end - str;

		/* copy to the buffer */
		pos = psh->tracepos;
		if (pos + len <= sizeof(psh->tracebuf)) {
			memcpy(&psh->tracebuf[pos], str, len);
			psh->tracepos = pos + len;
			if (flush_it)
				trace_flush(psh);
		} else {
			/* it's too big for some reason... */
			int s = errno;
			trace_flush(psh);
			shfile_write(&psh->fdtab, psh->tracefd, str, len);
			if (!flush_it)
				shfile_write(&psh->fdtab, psh->tracefd, "[too long]\n", sizeof( "[too long]\n") - 1);
			errno = s;
		}

		/* advance */
		str = end;
	}
}

void
trputc(shinstance *psh, int c)
{
	TRY_GET_PSH_OR_RETURN(psh);
	RETURN_IF_NOT_TRACING(psh);

	trace_char(psh, c);
}

void
trace(shinstance *psh, const char *fmt, ...)
{
	va_list va;
	char buf[2048];

	TRY_GET_PSH_OR_RETURN(psh);
	RETURN_IF_NOT_TRACING(psh);

	va_start(va, fmt);
#  ifdef _MSC_VER
	_vsnprintf(buf, sizeof(buf), fmt, va);
#  else
	vsnprintf(buf, sizeof(buf), fmt, va);
#  endif
	va_end(va);
	trace_string(psh, buf);
}

void
tracev(shinstance *psh, const char *fmt, va_list va)
{
	char buf[2048];

	TRY_GET_PSH_OR_RETURN(psh);
	RETURN_IF_NOT_TRACING(psh);

#  ifdef _MSC_VER
	_vsnprintf(buf, sizeof(buf), fmt, va);
#  else
	vsnprintf(buf, sizeof(buf), fmt, va);
#  endif
	trace_string(psh, buf);
}

void
trputs(shinstance *psh, const char *s)
{
	TRY_GET_PSH_OR_RETURN(psh);
	RETURN_IF_NOT_TRACING(psh);

	trace_string(psh, s);
    trace_char(psh, '\n');
}


static void
trstring(shinstance *psh, char *s)
{
	char *p;
	char c;

	TRY_GET_PSH_OR_RETURN(psh);
	RETURN_IF_NOT_TRACING(psh);

	trace_char(psh, '"');
	for (p = s ; *p ; p++) {
		switch (*p) {
		case '\n':  c = 'n';  goto backslash;
		case '\t':  c = 't';  goto backslash;
		case '\r':  c = 'r';  goto backslash;
		case '"':  c = '"';  goto backslash;
		case '\\':  c = '\\';  goto backslash;
		case CTLESC:  c = 'e';  goto backslash;
		case CTLVAR:  c = 'v';  goto backslash;
		case CTLVAR+CTLQUOTE:  c = 'V';  goto backslash;
		case CTLBACKQ:  c = 'q';  goto backslash;
		case CTLBACKQ+CTLQUOTE:  c = 'Q';  goto backslash;
backslash:	  trace_char(psh, '\\');
			trace_char(psh, c);
			break;
		default:
			if (*p >= ' ' && *p <= '~')
				trace_char(psh, *p);
			else {
				trace_char(psh, '\\');
				trace_char(psh, *p >> 6 & 03);
				trace_char(psh, *p >> 3 & 07);
				trace_char(psh, *p & 07);
			}
			break;
		}
	}
	trace_char(psh, '"');
}

void
trargs(shinstance *psh, char **ap)
{
	TRY_GET_PSH_OR_RETURN(psh);
	RETURN_IF_NOT_TRACING(psh);

	while (*ap) {
		trstring(psh, *ap++);
		if (*ap)
			trace_char(psh, ' ');
		else
			trace_char(psh, '\n');
	}
}

void
opentrace(shinstance *psh)
{
    static const char s[] = "./trace";

	TRY_GET_PSH_OR_RETURN(psh);
	if (debug(psh) != 1) {
        /* disabled */
		if (psh->tracefd != -1) {
			trace_flush(psh);
			shfile_close(&psh->fdtab, psh->tracefd);
			psh->tracefd = -1;
		}
		return;
	}
    /* else: (re-)enabled */

	if (psh->tracefd != -1)
        return;

	psh->tracefd = shfile_open(&psh->fdtab, s, O_APPEND | O_RDWR | O_CREAT, 0600);
	if (psh->tracefd != -1) {
		/* relocate it */
		int want_fd = 199;
		while (want_fd > 10)
		{
			int fd2 = shfile_fcntl(&psh->fdtab, psh->tracefd, F_DUPFD, want_fd);
			if (fd2 != -1) {
				shfile_close(&psh->fdtab, psh->tracefd);
				psh->tracefd = fd2;
				break;
			}
			want_fd = ((want_fd + 1) / 2) - 1;
		}
		shfile_cloexec(&psh->fdtab, psh->tracefd, 1 /* close it */);
		shfile_set_trace(&psh->fdtab, psh->tracefd);
	}
	if (psh->tracefd == -1) {
		fprintf(stderr, "Can't open %s\n", s);
		debug(psh) = 0;
		return;
	}
	trace_string(psh, "Tracing started.\n");
}

#endif /* DEBUG */

