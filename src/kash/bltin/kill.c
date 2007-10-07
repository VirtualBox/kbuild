/* $NetBSD: kill.c,v 1.23 2003/08/07 09:05:13 agc Exp $ */

/*
 * Copyright (c) 1988, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
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
#if !defined(lint) && !defined(SHELL)
__COPYRIGHT("@(#) Copyright (c) 1988, 1993, 1994\n\
	The Regents of the University of California.  All rights reserved.\n");
#endif /* not lint */

#ifndef lint
#if 0
static char sccsid[] = "@(#)kill.c	8.4 (Berkeley) 4/28/95";
#else
__RCSID("$NetBSD: kill.c,v 1.23 2003/08/07 09:05:13 agc Exp $");
#endif
#endif /* not lint */

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "jobs.h"
#include "error.h"
#include "shinstance.h"

#ifndef HAVE_SYS_SIGNAME
extern void init_sys_signame(void);
extern char sys_signame[NSIG][16];
#endif

static int nosig(shinstance *, char *);
static void printsignals(shinstance *, struct output *);
static int signame_to_signum(char *);
static int usage(shinstance *psh);

int
killcmd(shinstance *psh, int argc, char *argv[])
{
	int errors, numsig, pid;
	char *ep;

	if (argc < 2)
		return usage(psh);

	numsig = SIGTERM;
#ifndef HAVE_SYS_SIGNAME
	init_sys_signame();
#endif

	argc--, argv++;
	if (strcmp(*argv, "-l") == 0) {
		argc--, argv++;
		if (argc > 1)
			return usage(psh);
		if (argc == 1) {
			if (isdigit((unsigned char)**argv) == 0)
				return usage(psh);
			numsig = strtol(*argv, &ep, 10);
			if (*ep != '\0') {
				sh_errx(psh, EXIT_FAILURE, "illegal signal number: %s",
						*argv);
				/* NOTREACHED */
			}
			if (numsig >= 128)
				numsig -= 128;
			if (numsig <= 0 || numsig >= NSIG)
				return nosig(psh, *argv);
			outfmt(psh->out1, "%s\n", sys_signame[numsig]);
			//sh_exit(psh, 0);
			return 0;
		}
		printsignals(psh, psh->out1);
		//sh_exit(psh, 0);
		return 0;
	}

	if (!strcmp(*argv, "-s")) {
		argc--, argv++;
		if (argc < 1) {
			sh_warnx(psh, "option requires an argument -- s");
			return usage(psh);
		}
		if (strcmp(*argv, "0")) {
			if ((numsig = signame_to_signum(*argv)) < 0)
				return nosig(psh, *argv);
		} else
			numsig = 0;
		argc--, argv++;
	} else if (**argv == '-') {
		++*argv;
		if (isalpha((unsigned char)**argv)) {
			if ((numsig = signame_to_signum(*argv)) < 0)
				return nosig(psh, *argv);
		} else if (isdigit((unsigned char)**argv)) {
			numsig = strtol(*argv, &ep, 10);
			if (!*argv || *ep) {
				sh_errx(psh, EXIT_FAILURE, "illegal signal number: %s",
						*argv);
				/* NOTREACHED */
			}
			if (numsig < 0 || numsig >= NSIG)
				return nosig(psh, *argv);
		} else
			return nosig(psh, *argv);
		argc--, argv++;
	}

	if (argc == 0)
		return usage(psh);

	for (errors = 0; argc; argc--, argv++) {
		if (*argv[0] == '%') {
			pid = getjobpgrp(psh, *argv);
			if (pid == 0) {
				sh_warnx(psh, "illegal job id: %s", *argv);
				errors = 1;
				continue;
			}
		} else {
			pid = strtol(*argv, &ep, 10);
			if (!**argv || *ep) {
				sh_warnx(psh, "illegal process id: %s", *argv);
				errors = 1;
				continue;
			}
		}
		if (sh_kill(psh, pid, numsig) == -1) {
			sh_warn(psh, "%s", *argv);
			errors = 1;
		}
		/* Wakeup the process if it was suspended, so it can
		   exit without an explicit 'fg'. */
		if (numsig == SIGTERM || numsig == SIGHUP)
			sh_kill(psh, pid, SIGCONT);
	}

	//sh_exit(psh, errors);
	///* NOTREACHED */
	return errors;
}

static int
signame_to_signum(char *sig)
{
	int n;
	if (strncasecmp(sig, "sig", 3) == 0)
		sig += 3;
	for (n = 1; n < NSIG; n++) {
		if (!strcasecmp(sys_signame[n], sig))
			return (n);
	}
	return (-1);
}

static int
nosig(shinstance *psh, char *name)
{
	sh_warnx(psh, "unknown signal %s; valid signals:", name);
	printsignals(psh, psh->out2);
	//sh_exit(psh, 1);
	///* NOTREACHED */
	return 1;
}

static void
printsignals(shinstance *psh, struct output *out)
{
	int sig;
	size_t len, nl;
	const char *name;
	int termwidth = 80;

	if (shfile_isatty(&psh->fdtab, out->fd)) {
		sh_winsize win;
		if (shfile_ioctl(&psh->fdtab, out->fd, TIOCGWINSZ, &win) == 0 && win.ws_col > 0)
			termwidth = win.ws_col;
	}

	for (len = 0, sig = 1; sig < NSIG; sig++) {
		name = sys_signame[sig];
		nl = 1 + strlen(name);

		if (len + nl >= termwidth) {
			outfmt(out, "\n");
			len = 0;
		} else if (len != 0)
			outfmt(out, " ");
		len += nl;
		outfmt(out, "%s", name);
	}
	if (len != 0)
		outfmt(out, "\n");
}

static int
usage(shinstance *psh)
{
	outfmt(psh->out2,
	    "usage: %s [-s signal_name] pid ...\n"
	    "       %s -l [exit_status]\n"
	    "       %s -signal_name pid ...\n"
	    "       %s -signal_number pid ...\n",
	    psh->commandname, psh->commandname, psh->commandname, psh->commandname);
	//sh_exit(psh, 1);
	///* NOTREACHED */
	return 1;
}
