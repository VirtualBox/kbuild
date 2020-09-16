/*	$NetBSD: main.c,v 1.48 2003/09/14 12:09:29 jmmv Exp $	*/

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
__COPYRIGHT("@(#) Copyright (c) 1991, 1993\n\
	The Regents of the University of California.  All rights reserved.\n");
#endif /* not lint */
#ifndef lint
static char sccsid[] = "@(#)main.c	8.7 (Berkeley) 7/19/95";
#else
__RCSID("$NetBSD: main.c,v 1.48 2003/09/14 12:09:29 jmmv Exp $");
#endif /* not lint */
#endif

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <sys/stat.h>
#include <locale.h>


#include "shell.h"
#include "main.h"
#include "mail.h"
#include "options.h"
#include "output.h"
#include "parser.h"
#include "nodes.h"
#include "expand.h"
#include "eval.h"
#include "jobs.h"
#include "input.h"
#include "trap.h"
#include "var.h"
#include "show.h"
#include "memalloc.h"
#include "error.h"
#include "init.h"
#include "mystring.h"
#include "exec.h"
#include "cd.h"
#include "shinstance.h"

#define PROFILE 0

/*int rootpid;
int rootshell;*/
#ifdef unused_variables
STATIC union node *curcmd;
STATIC union node *prevcmd;
#endif

STATIC void read_profile(struct shinstance *, const char *);
STATIC char *find_dot_file(struct shinstance *, char *);
int main(int, char **, char **);
SH_NORETURN_1 void shell_main(shinstance *, int, char **) SH_NORETURN_2;
#ifdef _MSC_VER
extern void init_syntax(void);
#endif
STATIC int usage(const char *argv0);
STATIC int version(const char *argv0);

/*
 * Main routine.  We initialize things, parse the arguments, execute
 * profiles if we're a login shell, and then call cmdloop to execute
 * commands.  The setjmp call sets up the location to jump to when an
 * exception occurs.  When an exception occurs the variable "state"
 * is used to figure out how far we had gotten.
 */

int
#if K_OS == K_OS_WINDOWS && defined(SH_FORKED_MODE)
real_main(int argc, char **argv, char **envp)
#else
main(int argc, char **argv, char **envp)
#endif
{
	shinstance *psh;

	/*
	 * Global initializations.
	 */
	setlocale(LC_ALL, "");
#ifdef _MSC_VER
	init_syntax();
#endif

	/*
	 * Check for --version and --help.
	 */
	if (argc > 1 && argv[1][0] == '-' && argv[1][1] == '-') {
		if (!strcmp(argv[1], "--help"))
			return usage(argv[0]);
		if (!strcmp(argv[1], "--version"))
			return version(argv[0]);
	}

	/*
	 * Create the root shell instance.
	 */
	psh = sh_create_root_shell(argv, envp);
	if (!psh)
		return 2;
	shthread_set_shell(psh);
	shell_main(psh, argc, psh->orgargv);
	/* Not reached. */
	return 89;
}

SH_NORETURN_1 void
shell_main(shinstance *psh, int argc, char **argv)
{
	struct jmploc jmploc;
	struct stackmark smark;
	volatile int state;
	char *shinit;

	state = 0;
	if (setjmp(jmploc.loc)) {
		/*
		 * When a shell procedure is executed, we raise the
		 * exception EXSHELLPROC to clean up before executing
		 * the shell procedure.
		 */
		switch (psh->exception) {
		case EXSHELLPROC:
			psh->rootpid = /*getpid()*/ psh->pid;
			psh->rootshell = 1;
			psh->minusc = NULL;
			state = 3;
			break;

		case EXEXEC:
			psh->exitstatus = psh->exerrno;
			break;

		case EXERROR:
			psh->exitstatus = 2;
			break;

		default:
			break;
		}

		if (psh->exception != EXSHELLPROC) {
			if (state == 0 || iflag(psh) == 0 || ! psh->rootshell)
				exitshell(psh, psh->exitstatus);
		}
		reset(psh);
		if (psh->exception == EXINT
#if ATTY
		 && (! attyset(psh) || equal(termval(psh), "emacs"))
#endif
		 ) {
			out2c(psh, '\n');
			flushout(&psh->errout);
		}
		popstackmark(psh, &smark);
		FORCEINTON;				/* enable interrupts */
		if (state == 1)
			goto state1;
		else if (state == 2)
			goto state2;
		else if (state == 3)
			goto state3;
		else
			goto state4;
	}
	psh->handler = &jmploc;
	psh->rootpid = /*getpid()*/ psh->pid;
	psh->rootshell = 1;
#ifdef DEBUG
#if DEBUG == 2
	debug(psh) = 1;
#endif
	opentrace(psh);
	trputs(psh, "Shell args:  ");  trargs(psh, argv);
#endif

	init(psh);
	setstackmark(psh, &smark);
	procargs(psh, argc, argv);
	if (argv[0] && argv[0][0] == '-') {
		state = 1;
		read_profile(psh, "/etc/profile");
state1:
		state = 2;
		read_profile(psh, ".profile");
	}
state2:
	state = 3;
	if (sh_getuid(psh) == sh_geteuid(psh) && sh_getgid(psh) == sh_getegid(psh)) {
		if ((shinit = lookupvar(psh, "ENV")) != NULL && *shinit != '\0') {
			state = 3;
			read_profile(psh, shinit);
		}
	}
state3:
	state = 4;
	if (sflag(psh) == 0 || psh->minusc) {
		static int sigs[] =  {
		    SIGINT, SIGQUIT, SIGHUP,
#ifdef SIGTSTP
		    SIGTSTP,
#endif
		    SIGPIPE
		};
#define SIGSSIZE (sizeof(sigs)/sizeof(sigs[0]))
		unsigned i;

		for (i = 0; i < SIGSSIZE; i++)
		    setsignal(psh, sigs[i]);
	}

	if (psh->minusc)
		evalstring(psh, psh->minusc, 0);

	if (sflag(psh) || psh->minusc == NULL) {
state4:	/* XXX ??? - why isn't this before the "if" statement */
		cmdloop(psh, 1);
	}
	exitshell(psh, psh->exitstatus);
	/* NOTREACHED */
}


/*
 * Read and execute commands.  "Top" is nonzero for the top level command
 * loop; it turns on prompting if the shell is interactive.
 */

void
cmdloop(struct shinstance *psh, int top)
{
	union node *n;
	struct stackmark smark;
	int inter;
	int numeof = 0;

	TRACE((psh, "cmdloop(%d) called\n", top));
	setstackmark(psh, &smark);
	for (;;) {
		if (psh->pendingsigs)
			dotrap(psh);
		inter = 0;
		if (iflag(psh) && top) {
			inter = 1;
			showjobs(psh, psh->out2, SHOW_CHANGED);
			chkmail(psh, 0);
			flushout(&psh->errout);
		}
		n = parsecmd(psh, inter);
		/* showtree(n); DEBUG */
		if (n == NEOF) {
			if (!top || numeof >= 50)
				break;
			if (!stoppedjobs(psh)) {
				if (!Iflag(psh))
					break;
				out2str(psh, "\nUse \"exit\" to leave shell.\n");
			}
			numeof++;
		} else if (n != NULL && nflag(psh) == 0) {
			psh->job_warning = (psh->job_warning == 2) ? 1 : 0;
			numeof = 0;
			evaltree(psh, n, 0);
		}
		popstackmark(psh, &smark);
		setstackmark(psh, &smark);
		if (psh->evalskip == SKIPFILE) {
			psh->evalskip = 0;
			break;
		}
	}
	popstackmark(psh, &smark);
}



/*
 * Read /etc/profile or .profile.  Return on error.
 */

STATIC void
read_profile(struct shinstance *psh, const char *name)
{
	int fd;
	int xflag_set = 0;
	int vflag_set = 0;

	INTOFF;
	if ((fd = shfile_open(&psh->fdtab, name, O_RDONLY, 0)) >= 0)
		setinputfd(psh, fd, 1);
	INTON;
	if (fd < 0)
		return;
	/* -q turns off -x and -v just when executing init files */
	if (qflag(psh))  {
	    if (xflag(psh))
		    xflag(psh) = 0, xflag_set = 1;
	    if (vflag(psh))
		    vflag(psh) = 0, vflag_set = 1;
	}
	cmdloop(psh, 0);
	if (qflag(psh))  {
	    if (xflag_set)
		    xflag(psh) = 1;
	    if (vflag_set)
		    vflag(psh) = 1;
	}
	popfile(psh);
}



/*
 * Read a file containing shell functions.
 */

void
readcmdfile(struct shinstance *psh, char *name)
{
	int fd;

	INTOFF;
	if ((fd = shfile_open(&psh->fdtab, name, O_RDONLY, 0)) >= 0)
		setinputfd(psh, fd, 1);
	else
		error(psh, "Can't open %s", name);
	INTON;
	cmdloop(psh, 0);
	popfile(psh);
}



/*
 * Take commands from a file.  To be compatible we should do a path
 * search for the file, which is necessary to find sub-commands.
 */


STATIC char *
find_dot_file(struct shinstance *psh, char *basename)
{
	char *fullname;
	const char *path = pathval(psh);

	/* don't try this for absolute or relative paths */
	if (strchr(basename, '/'))
		return basename;

	while ((fullname = padvance(psh, &path, basename)) != NULL) {
		if (shfile_stat_isreg(&psh->fdtab, fullname) > 0) {
			/*
			 * Don't bother freeing here, since it will
			 * be freed by the caller.
			 */
			return fullname;
		}
		stunalloc(psh, fullname);
	}

	/* not found in the PATH */
	error(psh, "%s: not found", basename);
	/* NOTREACHED */
	return NULL;
}

int
dotcmd(struct shinstance *psh, int argc, char **argv)
{
	psh->exitstatus = 0;

	if (argc >= 2) {		/* That's what SVR2 does */
		char * const savedcommandname = psh->commandname;
		int const savedcommandnamemalloc = psh->commandnamemalloc;
		char *fullname;
		struct stackmark smark;

		setstackmark(psh, &smark);
		fullname = find_dot_file(psh, argv[1]);
		setinputfile(psh, fullname, 1);
		psh->commandname = fullname;
		psh->commandnamemalloc = 0;
		cmdloop(psh, 0);
		popfile(psh);
		psh->commandname = savedcommandname;
		psh->commandnamemalloc = savedcommandnamemalloc;
		popstackmark(psh, &smark);
	}
	return psh->exitstatus;
}


int
exitcmd(struct shinstance *psh, int argc, char **argv)
{
	if (stoppedjobs(psh))
		return 0;
	if (argc > 1)
		psh->exitstatus = number(psh, argv[1]);
	exitshell(psh, psh->exitstatus);
	/* NOTREACHED */
	return 1;
}


STATIC const char *
strip_argv0(const char *argv0, unsigned *lenp)
{
	const char *tmp;

	/* skip the path */
	for (tmp = strpbrk(argv0, "\\/:"); tmp; tmp = strpbrk(argv0, "\\/:"))
		argv0 = tmp + 1;

	/* find the end, ignoring extenions */
	tmp = strrchr(argv0, '.');
	if (!tmp)
		tmp = strchr(argv0, '\0');
	*lenp = (unsigned)(tmp - argv0);
	return argv0;
}

STATIC int
usage(const char *argv0)
{
	unsigned len;
	argv0 = strip_argv0(argv0, &len);

	fprintf(stdout,
			"usage: %.*s [-aCefnuvxIimqVEb] [+aCefnuvxIimqVEb] [-o option_name]\n"
		    "               [+o option_name] [command_file [argument ...]]\n"
		    "   or: %.*s -c [-aCefnuvxIimqVEb] [+aCefnuvxIimqVEb] [-o option_name]\n"
		    "               [+o option_name] command_string [command_name [argument ...]]\n"
		    "   or: %.*s -s [-aCefnuvxIimqVEb] [+aCefnuvxIimqVEb] [-o option_name]\n"
		    "               [+o option_name] [argument ...]\n"
		    "   or: %.*s --help\n"
		    "   or: %.*s --version\n",
		    len, argv0, len, argv0, len, argv0, len, argv0, len, argv0);
	return 0;
}

STATIC int
version(const char *argv0)
{
	unsigned len;
	strip_argv0(argv0, &len);

	fprintf(stdout,
			"%.*s - kBuild version %d.%d.%d (r%u)\n",
		    len, argv0,
		    KBUILD_VERSION_MAJOR, KBUILD_VERSION_MINOR, KBUILD_VERSION_PATCH, KBUILD_SVN_REV);
	return 0;
}


/*
 * Local Variables:
 *   c-file-style: bsd
 * End:
 */
