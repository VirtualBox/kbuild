/*	$NetBSD: error.h,v 1.16 2003/08/07 09:05:30 agc Exp $	*/

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
 *
 *	@(#)error.h	8.2 (Berkeley) 5/4/95
 */

#ifndef ___error_h
#define ___error_h

#include <stdarg.h>

/*
 * Types of operations (passed to the errmsg routine).
 */

#define E_OPEN 01	/* opening a file */
#define E_CREAT 02	/* creating a file */
#define E_EXEC 04	/* executing a program */


/*
 * We enclose jmp_buf in a structure so that we can declare pointers to
 * jump locations.  The global variable handler contains the location to
 * jump to when an exception occurs, and the global variable exception
 * contains a code identifying the exeception.  To implement nested
 * exception handlers, the user should save the value of handler on entry
 * to an inner scope, set handler to point to a jmploc structure for the
 * inner scope, and restore handler on exit from the scope.
 */

#include <setjmp.h>

struct jmploc {
	jmp_buf loc;
};

/*
extern struct jmploc *handler;
extern int exception;
extern int exerrno;*/	/* error for EXEXEC */

/* exceptions */
#define EXINT 0		/* SIGINT received */
#define EXERROR 1	/* a generic error */
#define EXSHELLPROC 2	/* execute a shell procedure */
#define EXEXEC 3	/* command execution failed */


/*
 * These macros allow the user to suspend the handling of interrupt signals
 * over a period of time.  This is similar to SIGHOLD to or sigblock, but
 * much more efficient and portable.  (But hacking the kernel is so much
 * more fun than worrying about efficiency and portability. :-))
 */

/*extern volatile int suppressint;
extern volatile int intpending;*/

#define INTOFF psh->suppressint++
#define INTON { if (--psh->suppressint == 0 && psh->intpending) onint(psh); }
#define FORCEINTON {psh->suppressint = 0; if (psh->intpending) onint(psh);}
#define CLEAR_PENDING_INT psh->intpending = 0
#define int_pending() psh->intpending

#if !defined(__GNUC__) && !defined(__attribute__)
# define __attribute__(a)
#endif

void exraise(struct shinstance *, int) __attribute__((__noreturn__));
void onint(struct shinstance *);
void error(struct shinstance *, const char *, ...) __attribute__((__noreturn__));
void exerror(struct shinstance *, int, const char *, ...) __attribute__((__noreturn__));
const char *errmsg(struct shinstance *, int, int);

void sh_err(struct shinstance *, int, const char *, ...) __attribute__((__noreturn__));
void sh_verr(struct shinstance *, int, const char *, va_list) __attribute__((__noreturn__));
void sh_errx(struct shinstance *, int, const char *, ...) __attribute__((__noreturn__));
void sh_verrx(struct shinstance *, int, const char *, va_list) __attribute__((__noreturn__));
void sh_warn(struct shinstance *, const char *, ...);
void sh_vwarn(struct shinstance *, const char *, va_list);
void sh_warnx(struct shinstance *, const char *, ...);
void sh_vwarnx(struct shinstance *, const char *, va_list);

void sh_exit(struct shinstance *, int) __attribute__((__noreturn__));


/*
 * BSD setjmp saves the signal mask, which violates ANSI C and takes time,
 * so we use _setjmp instead.
 */

#if defined(BSD) && !defined(__SVR4) && !defined(__GLIBC__) && !defined(__KLIBC__) && !defined(_MSC_VER)
#define setjmp(jmploc)	_setjmp(jmploc)
#define longjmp(jmploc, val)	_longjmp(jmploc, val)
#endif

#endif
