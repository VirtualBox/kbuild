/*	$NetBSD: error.c,v 1.31 2003/08/07 09:05:30 agc Exp $	*/

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
static char sccsid[] = "@(#)error.c	8.2 (Berkeley) 5/4/95";
#else
__RCSID("$NetBSD: error.c,v 1.31 2003/08/07 09:05:30 agc Exp $");
#endif /* not lint */
#endif

/*
 * Errors and exceptions.
 */

#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "shell.h"
#include "main.h"
#include "options.h"
#include "output.h"
#include "error.h"
#include "show.h"
#include "shinstance.h"


/*
 * Code to handle exceptions in C.
 */

/*struct jmploc *handler;
int exception;
volatile int suppressint;
volatile int intpending;
char *commandname;*/

SH_NORETURN_1
static void exverror(shinstance *psh, int, const char *, va_list) SH_NORETURN_2;

/*
 * Called to raise an exception.  Since C doesn't include exceptions, we
 * just do a longjmp to the exception handler.  The type of exception is
 * stored in the global variable "exception".
 */

SH_NORETURN_1 void
exraise(shinstance *psh, int e)
{
	if (psh->handler == NULL)
		sh_abort(psh);
	psh->exception = e;
	longjmp(psh->handler->loc, 1);
}


/*
 * Called from trap.c when a SIGINT is received.  (If the user specifies
 * that SIGINT is to be trapped or ignored using the trap builtin, then
 * this routine is not called.)  Suppressint is nonzero when interrupts
 * are held using the INTOFF macro.  The call to _exit is necessary because
 * there is a short period after a fork before the signal handlers are
 * set to the appropriate value for the child.  (The test for iflag is
 * just defensive programming.)
 */

void
onint(shinstance *psh)
{
	shsigset_t nsigset;

	if (psh->suppressint) {
		psh->intpending = 1;
		return;
	}
	psh->intpending = 0;
	sh_sigemptyset(&nsigset);
	sh_sigprocmask(psh, SIG_SETMASK, &nsigset, NULL);
	if (psh->rootshell && iflag(psh))
		exraise(psh, EXINT);
	else {
		sh_signal(psh, SIGINT, SH_SIG_DFL);
		sh_raise_sigint(psh);/*raise(psh, SIGINT);*/
	}
	/* NOTREACHED */
}

static void
exvwarning(shinstance *psh, int sv_errno, const char *msg, va_list ap)
{
	/* Partially emulate line buffered output so that:
	 *	printf '%d\n' 1 a 2
	 * and
	 *	printf '%d %d %d\n' 1 a 2
	 * both generate sensible text when stdout and stderr are merged.
	 */
	if (psh->output.nextc != psh->output.buf && psh->output.nextc[-1] == '\n')
		flushout(&psh->output);
	if (psh->commandname)
		outfmt(&psh->errout, "%s: ", psh->commandname);
	if (msg != NULL) {
		doformat(&psh->errout, msg, ap);
		if (sv_errno >= 0)
			outfmt(&psh->errout, ": ");
	}
	if (sv_errno >= 0)
		outfmt(&psh->errout, "%s", sh_strerror(psh, sv_errno));
	out2c(psh, '\n');
	flushout(&psh->errout);
}

/*
 * Exverror is called to raise the error exception.  If the second argument
 * is not NULL then error prints an error message using printf style
 * formatting.  It then raises the error exception.
 */
static void
exverror(shinstance *psh, int cond, const char *msg, va_list ap)
{
	CLEAR_PENDING_INT;
	INTOFF;

#ifdef DEBUG
	if (msg) {
		va_list va2;
		TRACE((psh, "exverror(%d, \"", cond));
# ifdef va_copy /* MSC 2010 still doesn't have va_copy. sigh. */
		va_copy(va2, ap);
# else
		va2 = ap;
# endif
		TRACEV((psh, msg, va2));
		va_end(va2);
		TRACE((psh, "\") pid=%" SHPID_PRI "\n", sh_getpid(psh)));
	} else
		TRACE((psh, "exverror(%d, NULL) pid=%" SHPID_PRI "\n", cond, sh_getpid(psh)));
#endif
	if (msg)
		exvwarning(psh, -1, msg, ap);

	output_flushall(psh);
	exraise(psh, cond);
	/* NOTREACHED */
}


SH_NORETURN_1 void
error(shinstance *psh, const char *msg, ...)
{
	va_list ap;

	va_start(ap, msg);
	exverror(psh, EXERROR, msg, ap);
	/* NOTREACHED */
	va_end(ap);
}


SH_NORETURN_1 void
exerror(shinstance *psh, int cond, const char *msg, ...)
{
	va_list ap;

	va_start(ap, msg);
	exverror(psh, cond, msg, ap);
	/* NOTREACHED */
	va_end(ap);
}

/*
 * error/warning routines for external builtins
 */

SH_NORETURN_1 void
sh_exit(shinstance *psh, int rval)
{
	psh->exerrno = rval & 255;
	exraise(psh, EXEXEC);
}

SH_NORETURN_1 void
sh_err(shinstance *psh, int status, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	exvwarning(psh, errno, fmt, ap);
	va_end(ap);
	sh_exit(psh, status);
}

SH_NORETURN_1 void
sh_verr(shinstance *psh, int status, const char *fmt, va_list ap)
{
	exvwarning(psh, errno, fmt, ap);
	sh_exit(psh, status);
}

SH_NORETURN_1 void
sh_errx(shinstance *psh, int status, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	exvwarning(psh, -1, fmt, ap);
	va_end(ap);
	sh_exit(psh, status);
}

SH_NORETURN_1 void
sh_verrx(shinstance *psh, int status, const char *fmt, va_list ap)
{
	exvwarning(psh, -1, fmt, ap);
	sh_exit(psh, status);
}

void
sh_warn(shinstance *psh, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	exvwarning(psh, errno, fmt, ap);
	va_end(ap);
}

void
sh_vwarn(shinstance *psh, const char *fmt, va_list ap)
{
	exvwarning(psh, errno, fmt, ap);
}

void
sh_warnx(shinstance *psh, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	exvwarning(psh, -1, fmt, ap);
	va_end(ap);
}

void
sh_vwarnx(shinstance *psh, const char *fmt, va_list ap)
{
	exvwarning(psh, -1, fmt, ap);
}


/*
 * Table of error messages.
 */

struct errname {
	short errcode;		/* error number */
	short action;		/* operation which encountered the error */
	const char *msg;	/* text describing the error */
};


#define ALL (E_OPEN|E_CREAT|E_EXEC)

STATIC const struct errname errormsg[] = {
	{ EINTR,	ALL,	"interrupted" },
	{ EACCES,	ALL,	"permission denied" },
	{ EIO,		ALL,	"I/O error" },
	{ EEXIST,	ALL,	"file exists" },
	{ ENOENT,	E_OPEN,	"no such file" },
	{ ENOENT,	E_CREAT,"directory nonexistent" },
	{ ENOENT,	E_EXEC,	"not found" },
	{ ENOTDIR,	E_OPEN,	"no such file" },
	{ ENOTDIR,	E_CREAT,"directory nonexistent" },
	{ ENOTDIR,	E_EXEC,	"not found" },
	{ EISDIR,	ALL,	"is a directory" },
#ifdef EMFILE
	{ EMFILE,	ALL,	"too many open files" },
#endif
	{ ENFILE,	ALL,	"file table overflow" },
	{ ENOSPC,	ALL,	"file system full" },
#ifdef EDQUOT
	{ EDQUOT,	ALL,	"disk quota exceeded" },
#endif
#ifdef ENOSR
	{ ENOSR,	ALL,	"no streams resources" },
#endif
	{ ENXIO,	ALL,	"no such device or address" },
	{ EROFS,	ALL,	"read-only file system" },
#ifdef ETXTBSY
	{ ETXTBSY,	ALL,	"text busy" },
#endif
#ifdef EAGAIN
	{ EAGAIN,	E_EXEC,	"not enough memory" },
#endif
	{ ENOMEM,	ALL,	"not enough memory" },
#ifdef ENOLINK
	{ ENOLINK,	ALL,	"remote access failed" },
#endif
#ifdef EMULTIHOP
	{ EMULTIHOP,	ALL,	"remote access failed" },
#endif
#ifdef ECOMM
	{ ECOMM,	ALL,	"remote access failed" },
#endif
#ifdef ESTALE
	{ ESTALE,	ALL,	"remote access failed" },
#endif
#ifdef ETIMEDOUT
	{ ETIMEDOUT,	ALL,	"remote access failed" },
#endif
#ifdef ELOOP
	{ ELOOP,	ALL,	"symbolic link loop" },
#endif
	{ E2BIG,	E_EXEC,	"argument list too long" },
#ifdef ELIBACC
	{ ELIBACC,	E_EXEC,	"shared library missing" },
#endif
	{ 0,		0,	NULL },
};


/*
 * Return a string describing an error.  The returned string may be a
 * pointer to a static buffer that will be overwritten on the next call.
 * Action describes the operation that got the error.
 */

const char *
errmsg(shinstance *psh, int e, int action)
{
	struct errname const *ep;
	/*static char buf[12];*/

	for (ep = errormsg ; ep->errcode ; ep++) {
		if (ep->errcode == e && (ep->action & action) != 0)
			return ep->msg;
	}
	fmtstr(psh->errmsg_buf, sizeof psh->errmsg_buf, "error %d", e);
	return psh->errmsg_buf;
}
