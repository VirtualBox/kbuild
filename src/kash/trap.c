/*	$NetBSD: trap.c,v 1.31 2005/01/11 19:38:57 christos Exp $	*/

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
static char sccsid[] = "@(#)trap.c	8.5 (Berkeley) 6/5/95";
#else
__RCSID("$NetBSD: trap.c,v 1.31 2005/01/11 19:38:57 christos Exp $");
#endif /* not lint */
#endif

#include <stdlib.h>

#include "shell.h"
#include "main.h"
#include "nodes.h"	/* for other headers */
#include "eval.h"
#include "jobs.h"
#include "show.h"
#include "options.h"
#include "syntax.h"
#include "output.h"
#include "memalloc.h"
#include "error.h"
#include "trap.h"
#include "mystring.h"
#include "var.h"
#include "shinstance.h"

#ifndef HAVE_SYS_SIGNAME
extern void init_sys_signame(void);
extern char sys_signame[NSIG][16];
#endif

/*
 * Sigmode records the current value of the signal handlers for the various
 * modes.  A value of zero means that the current handler is not known.
 * S_HARD_IGN indicates that the signal was ignored on entry to the shell,
 */

#define S_DFL 1			/* default signal handling (SIG_DFL) */
#define S_CATCH 2		/* signal is caught */
#define S_IGN 3			/* signal is ignored (SIG_IGN) */
#define S_HARD_IGN 4		/* signal is ignored permenantly */
#define S_RESET 5		/* temporary - to reset a hard ignored sig */


//char *trap[NSIG+1];		/* trap handler commands */
//MKINIT char sigmode[NSIG];	/* current value of signal */
//char gotsig[NSIG];		/* indicates specified signal received */
//int pendingsigs;		/* indicates some signal received */

static int getsigaction(shinstance *, int, shsig_t *);

/*
 * return the signal number described by `p' (as a number or a name)
 * or -1 if it isn't one
 */

static int
signame_to_signum(shinstance *psh, const char *p)
{
	int i;

	if (is_number(p))
		return number(psh, p);

	if (strcasecmp(p, "exit") == 0 )
		return 0;

	if (strncasecmp(p, "sig", 3) == 0)
		p += 3;

#ifndef HAVE_SYS_SIGNAME
	init_sys_signame();
#endif
	for (i = 0; i < NSIG; ++i)
		if (strcasecmp(p, sys_signame[i]) == 0)
			return i;
	return -1;
}

/*
 * Print a list of valid signal names
 */
static void
printsignals(shinstance *psh)
{
	int n;

	out1str(psh, "EXIT ");
#ifndef HAVE_SYS_SIGNAME
	init_sys_signame();
#endif

	for (n = 1; n < NSIG; n++) {
		out1fmt(psh, "%s", sys_signame[n]);
		if ((n == NSIG/2) ||  n == (NSIG - 1))
			out1str(psh, "\n");
		else
			out1c(psh, ' ');
	}
}

/*
 * The trap builtin.
 */

int
trapcmd(shinstance *psh, int argc, char **argv)
{
	char *action;
	char **ap;
	int signo;
#ifndef HAVE_SYS_SIGNAME
	init_sys_signame();
#endif

	if (argc <= 1) {
		for (signo = 0 ; signo <= NSIG ; signo++)
			if (psh->trap[signo] != NULL) {
				out1fmt(psh, "trap -- ");
				print_quoted(psh, psh->trap[signo]);
				out1fmt(psh, " %s\n",
				    (signo) ? sys_signame[signo] : "EXIT");
			}
		return 0;
	}
	ap = argv + 1;

	action = NULL;

	if (strcmp(*ap, "--") == 0)
		if (*++ap == NULL)
			return 0;

	if (signame_to_signum(psh, *ap) == -1) {
		if ((*ap)[0] == '-') {
			if ((*ap)[1] == '\0')
				ap++;
			else if ((*ap)[1] == 'l' && (*ap)[2] == '\0') {
				printsignals(psh);
				return 0;
			}
			else
				error(psh, "bad option %s\n", *ap);
		}
		else
			action = *ap++;
	}

	while (*ap) {
		if (is_number(*ap))
			signo = number(psh, *ap);
		else
			signo = signame_to_signum(psh, *ap);

		if (signo < 0 || signo > NSIG)
			error(psh, "%s: bad trap", *ap);

		INTOFF;
		if (action)
			action = savestr(psh, action);

		if (psh->trap[signo])
			ckfree(psh, psh->trap[signo]);

		psh->trap[signo] = action;

		if (signo != 0)
			setsignal(psh, signo, 0);
		INTON;
		ap++;
	}
	return 0;
}



/*
 * Clear traps on a fork or vfork.
 * Takes one arg vfork, to tell it to not be destructive of
 * the parents variables.
 */

void
clear_traps(shinstance *psh, int vforked)
{
	char **tp;

	for (tp = psh->trap ; tp <= &psh->trap[NSIG] ; tp++) {
		if (*tp && **tp) {	/* trap not NULL or SIG_IGN */
			INTOFF;
			if (!vforked) {
				ckfree(psh, *tp);
				*tp = NULL;
			}
			if (tp != &psh->trap[0])
				setsignal(psh, (int)(tp - psh->trap), vforked);
			INTON;
		}
	}
}



/*
 * Set the signal handler for the specified signal.  The routine figures
 * out what it should be set to.
 */

void
setsignal(shinstance *psh, int signo, int vforked)
{
	int action;
	shsig_t sigact = SH_SIG_DFL;
	char *t, tsig;

	if ((t = psh->trap[signo]) == NULL)
		action = S_DFL;
	else if (*t != '\0')
		action = S_CATCH;
	else
		action = S_IGN;
	if (psh->rootshell && !vforked && action == S_DFL) {
		switch (signo) {
		case SIGINT:
			if (iflag(psh) || psh->minusc || sflag(psh) == 0)
				action = S_CATCH;
			break;
		case SIGQUIT:
#ifdef DEBUG
			if (debug(psh))
				break;
#endif
			/* FALLTHROUGH */
		case SIGTERM:
			if (iflag(psh))
				action = S_IGN;
			break;
#if JOBS
		case SIGTSTP:
		case SIGTTOU:
			if (mflag(psh))
				action = S_IGN;
			break;
#endif
		}
	}

	t = &psh->sigmode[signo - 1];
	tsig = *t;
	if (tsig == 0) {
		/*
		 * current setting unknown
		 */
		if (!getsigaction(psh, signo, &sigact)) {
			/*
			 * Pretend it worked; maybe we should give a warning
			 * here, but other shells don't. We don't alter
			 * sigmode, so that we retry every time.
			 */
			return;
		}
		if (sigact == SH_SIG_IGN) {
			if (mflag(psh) && (signo == SIGTSTP ||
			     signo == SIGTTIN || signo == SIGTTOU)) {
				tsig = S_IGN;	/* don't hard ignore these */
			} else
				tsig = S_HARD_IGN;
		} else {
			tsig = S_RESET;	/* force to be set */
		}
	}
	if (tsig == S_HARD_IGN || tsig == action)
		return;
	switch (action) {
		case S_DFL:	sigact = SH_SIG_DFL;	break;
		case S_CATCH:  	sigact = onsig;		break;
		case S_IGN:	sigact = SH_SIG_IGN;	break;
	}
	if (!vforked)
		*t = action;
	sh_siginterrupt(psh, signo, 1);
	sh_signal(psh, signo, sigact);
}

/*
 * Return the current setting for sig w/o changing it.
 */
static int
getsigaction(shinstance *psh, int signo, shsig_t *sigact)
{
	struct shsigaction sa;

	if (sh_sigaction(psh, signo, NULL, &sa) == -1)
		return 0;
	*sigact = (shsig_t)sa.sh_handler;
	return 1;
}

/*
 * Ignore a signal.
 */

void
ignoresig(shinstance *psh, int signo, int vforked)
{
	if (psh->sigmode[signo - 1] != S_IGN && psh->sigmode[signo - 1] != S_HARD_IGN) {
		sh_signal(psh, signo, SH_SIG_IGN);
	}
	if (!vforked)
		psh->sigmode[signo - 1] = S_HARD_IGN;
}


#ifdef mkinit
INCLUDE <signal.h>
INCLUDE "trap.h"

SHELLPROC {
	char *sm;

	clear_traps(psh, 0);
	for (sm = psh->sigmode ; sm < psh->sigmode + NSIG ; sm++) {
		if (*sm == S_IGN)
			*sm = S_HARD_IGN;
	}
}
#endif



/*
 * Signal handler.
 */

void
onsig(shinstance *psh, int signo)
{
	sh_signal(psh, signo, onsig);
	if (signo == SIGINT && psh->trap[SIGINT] == NULL) {
		onint(psh);
		return;
	}
	psh->gotsig[signo - 1] = 1;
	psh->pendingsigs++;
}



/*
 * Called to execute a trap.  Perhaps we should avoid entering new trap
 * handlers while we are executing a trap handler.
 */

void
dotrap(shinstance *psh)
{
	int i;
	int savestatus;

	for (;;) {
		for (i = 1 ; ; i++) {
			if (psh->gotsig[i - 1])
				break;
			if (i >= NSIG)
				goto done;
		}
		psh->gotsig[i - 1] = 0;
		savestatus=psh->exitstatus;
		evalstring(psh, psh->trap[i], 0);
		psh->exitstatus=savestatus;
	}
done:
	psh->pendingsigs = 0;
}



/*
 * Controls whether the shell is interactive or not.
 */


void
setinteractive(shinstance *psh, int on)
{
	static int is_interactive;

	if (on == is_interactive)
		return;
	setsignal(psh, SIGINT, 0);
	setsignal(psh, SIGQUIT, 0);
	setsignal(psh, SIGTERM, 0);
	is_interactive = on;
}



/*
 * Called to exit the shell.
 */

void
exitshell(shinstance *psh, int status)
{
	struct jmploc loc1, loc2;
	char *p;

	TRACE((psh, "pid %d, exitshell(%d)\n", sh_getpid(psh), status));
	if (setjmp(loc1.loc)) {
		goto l1;
	}
	if (setjmp(loc2.loc)) {
		goto l2;
	}
	psh->handler = &loc1;
	if ((p = psh->trap[0]) != NULL && *p != '\0') {
		psh->trap[0] = NULL;
		evalstring(psh, p, 0);
	}
l1:   psh->handler = &loc2;			/* probably unnecessary */
	output_flushall(psh);
#if JOBS
	setjobctl(psh, 0);
#endif
l2: sh__exit(psh, status);
	/* NOTREACHED */
}
