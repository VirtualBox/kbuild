/*	$NetBSD: cd.c,v 1.34 2003/11/14 20:00:28 dsl Exp $	*/

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
static char sccsid[] = "@(#)cd.c	8.2 (Berkeley) 5/4/95";
#else
__RCSID("$NetBSD: cd.c,v 1.34 2003/11/14 20:00:28 dsl Exp $");
#endif
#endif /* not lint */

#include <sys/types.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

/*
 * The cd and pwd commands.
 */

#include "shell.h"
#include "var.h"
#include "nodes.h"	/* for jobs.h */
#include "jobs.h"
#include "options.h"
#include "output.h"
#include "memalloc.h"
#include "error.h"
#include "exec.h"
#include "redir.h"
#include "mystring.h"
#include "show.h"
#include "cd.h"
#include "shinstance.h"

STATIC int docd(shinstance *psh, char *, int);
STATIC char *getcomponent(shinstance *psh);
STATIC void updatepwd(shinstance *psh, char *);
STATIC void find_curdir(shinstance *psh, int noerror);

/*char *curdir = NULL;*/		/* current working directory */
/*char *prevdir;*/			/* previous working directory */
/*STATIC char *cdcomppath;*/

int
cdcmd(shinstance *psh, int argc, char **argv)
{
	const char *dest;
	const char *path;
	char *p, *d;
	struct stat statb;
	int print = psh->cdprint;	/* set -cdprint to enable */

	nextopt(psh, nullstr);

	/*
	 * Try (quite hard) to have 'curdir' defined, nothing has set
	 * it on entry to the shell, but we want 'cd fred; cd -' to work.
	 */
	getpwd(psh, 1);
	dest = *psh->argptr;
	if (dest == NULL) {
		dest = bltinlookup(psh, "HOME", 1);
		if (dest == NULL)
			error(psh, "HOME not set");
	} else {
		if (psh->argptr[1]) {
			/* Do 'ksh' style substitution */
			if (!psh->curdir)
				error(psh, "PWD not set");
			p = strstr(psh->curdir, dest);
			if (!p)
				error(psh, "bad substitution");
			d = stalloc(psh, strlen(psh->curdir) + strlen(psh->argptr[1]) + 1);
			memcpy(d, psh->curdir, p - psh->curdir);
			strcpy(d + (p - psh->curdir), psh->argptr[1]);
			strcat(d, p + strlen(dest));
			dest = d;
			print = 1;
		}
	}

	if (dest[0] == '-' && dest[1] == '\0') {
		dest = psh->prevdir ? psh->prevdir : psh->curdir;
		print = 1;
	}
	if (*dest == '\0')
	        dest = ".";
	if (IS_ROOT(dest) || (path = bltinlookup(psh, "CDPATH", 1)) == NULL)
		path = nullstr;
	while ((p = padvance(psh, &path, dest)) != NULL) {
		if (shfile_stat(&psh->fdtab, p, &statb) >= 0 && S_ISDIR(statb.st_mode)) {
			if (!print) {
				/*
				 * XXX - rethink
				 */
				if (p[0] == '.' && p[1] == '/' && p[2] != '\0')
					p += 2;
				print = strcmp(p, dest);
			}
			if (docd(psh, p, print) >= 0)
				return 0;

		}
	}
	error(psh, "can't cd to %s", dest);
	/* NOTREACHED */
        return 1;
}


/*
 * Actually do the chdir.  In an interactive shell, print the
 * directory name if "print" is nonzero.
 */

STATIC int
docd(shinstance *psh, char *dest, int print)
{
	char *p;
	char *q;
	char *component;
	struct stat statb;
	int first;
	int badstat;

	TRACE(("docd(\"%s\", %d) called\n", dest, print));

	/*
	 *  Check each component of the path. If we find a symlink or
	 *  something we can't stat, clear curdir to force a getcwd()
	 *  next time we get the value of the current directory.
	 */
	badstat = 0;
	psh->cdcomppath = stalloc(psh, strlen(dest) + 1);
	scopy(dest, psh->cdcomppath);
	STARTSTACKSTR(p);
	if (IS_ROOT(dest)) {
		STPUTC('/', p);
		psh->cdcomppath++;
	}
	first = 1;
	while ((q = getcomponent(psh)) != NULL) {
		if (q[0] == '\0' || (q[0] == '.' && q[1] == '\0'))
			continue;
		if (! first)
			STPUTC('/', p);
		first = 0;
		component = q;
		while (*q)
			STPUTC(*q++, p);
		if (equal(component, ".."))
			continue;
		STACKSTRNUL(p);
		if ((shfile_lstat(&psh->fdtab, stackblock(), &statb) < 0)
		    || (S_ISLNK(statb.st_mode)))  {
			/* print = 1; */
			badstat = 1;
			break;
		}
	}

	INTOFF;
	if (shfile_chdir(&psh->fdtab, dest) < 0) {
		INTON;
		return -1;
	}
	updatepwd(psh, badstat ? NULL : dest);
	INTON;
	if (print && psh->iflag && psh->curdir)
		out1fmt(psh, "%s\n", psh->curdir);
	return 0;
}


/*
 * Get the next component of the path name pointed to by psh->cdcomppath.
 * This routine overwrites the string pointed to by psh->cdcomppath.
 */

STATIC char *
getcomponent(shinstance *psh)
{
	char *p;
	char *start;

	if ((p = psh->cdcomppath) == NULL)
		return NULL;
	start = psh->cdcomppath;
	while (*p != '/' && *p != '\0')
		p++;
	if (*p == '\0') {
		psh->cdcomppath = NULL;
	} else {
		*p++ = '\0';
		psh->cdcomppath = p;
	}
	return start;
}



/*
 * Update curdir (the name of the current directory) in response to a
 * cd command.  We also call hashcd to let the routines in exec.c know
 * that the current directory has changed.
 */

STATIC void
updatepwd(shinstance *psh, char *dir)
{
	char *new;
	char *p;

	hashcd(psh);				/* update command hash table */

	/*
	 * If our argument is NULL, we don't know the current directory
	 * any more because we traversed a symbolic link or something
	 * we couldn't stat().
	 */
	if (dir == NULL || psh->curdir == NULL)  {
		if (psh->prevdir)
			ckfree(psh->prevdir);
		INTOFF;
		psh->prevdir = psh->curdir;
		psh->curdir = NULL;
		getpwd(psh, 1);
		INTON;
		if (psh->curdir)
			setvar(psh, "PWD", psh->curdir, VEXPORT);
		else
			unsetvar(psh, "PWD", 0);
		return;
	}
	psh->cdcomppath = stalloc(psh, strlen(dir) + 1);
	scopy(dir, psh->cdcomppath);
	STARTSTACKSTR(new);
	if (!IS_ROOT(dir)) {
		p = psh->curdir;
		while (*p)
			STPUTC(*p++, new);
		if (p[-1] == '/')
			STUNPUTC(new);
	}
	while ((p = getcomponent(psh)) != NULL) {
		if (equal(p, "..")) {
			while (new > stackblock() && (STUNPUTC(new), *new) != '/');
		} else if (*p != '\0' && ! equal(p, ".")) {
			STPUTC('/', new);
			while (*p)
				STPUTC(*p++, new);
		}
	}
	if (new == stackblock())
		STPUTC('/', new);
	STACKSTRNUL(new);
	INTOFF;
	if (psh->prevdir)
		ckfree(psh->prevdir);
	psh->prevdir = psh->curdir;
	psh->curdir = savestr(stackblock());
	setvar(psh, "PWD", psh->curdir, VEXPORT);
	INTON;
}

/*
 * Posix says the default should be 'pwd -L' (as below), however
 * the 'cd' command (above) does something much nearer to the
 * posix 'cd -P' (not the posix default of 'cd -L').
 * If 'cd' is changed to support -P/L then the default here
 * needs to be revisited if the historic behaviour is to be kept.
 */

int
pwdcmd(shinstance *psh, int argc, char **argv)
{
	int i;
	char opt = 'L';

	while ((i = nextopt(psh, "LP")) != '\0')
		opt = i;
	if (*psh->argptr)
		error(psh, "unexpected argument");

	if (opt == 'L')
		getpwd(psh, 0);
	else
		find_curdir(psh, 0);

	setvar(psh, "PWD", psh->curdir, VEXPORT);
	out1str(psh, psh->curdir);
	out1c(psh, '\n');
	return 0;
}




#define MAXPWD 256

/*
 * Find out what the current directory is. If we already know the current
 * directory, this routine returns immediately.
 */
void
getpwd(shinstance *psh, int noerror)
{
	char *pwd;
	struct stat stdot, stpwd;
	/*static int first = 1;*/

	if (psh->curdir)
		return;

	if (psh->getpwd_first) {
		psh->getpwd_first = 0;
		pwd = sh_getenv(psh, "PWD");
		if (pwd && IS_ROOT(pwd) && shfile_stat(&psh->fdtab, ".", &stdot) != -1 &&
		    shfile_stat(&psh->fdtab, pwd, &stpwd) != -1 &&
		    stdot.st_dev == stpwd.st_dev &&
		    stdot.st_ino == stpwd.st_ino) {
			psh->curdir = savestr(pwd);
			return;
		}
	}

	find_curdir(psh, noerror);

	return;
}

STATIC void
find_curdir(shinstance *psh, int noerror)
{
	int i;
	char *pwd;

	/*
	 * Things are a bit complicated here; we could have just used
	 * getcwd, but traditionally getcwd is implemented using popen
	 * to /bin/pwd. This creates a problem for us, since we cannot
	 * keep track of the job if it is being ran behind our backs.
	 * So we re-implement getcwd(), and we suppress interrupts
	 * throughout the process. This is not completely safe, since
	 * the user can still break out of it by killing the pwd program.
	 * We still try to use getcwd for systems that we know have a
	 * c implementation of getcwd, that does not open a pipe to
	 * /bin/pwd.
	 */
#if 1 //defined(__NetBSD__) || defined(__SVR4) || defined(__INNOTEK_LIBC__)

	for (i = MAXPWD;; i *= 2) {
		pwd = stalloc(psh, i);
		if (shfile_getcwd(&psh->fdtab, pwd, i) != NULL) {
			psh->curdir = savestr(pwd);
			return;
		}
		stunalloc(psh, pwd);
		if (errno == ERANGE)
			continue;
		if (!noerror)
			error(psh, "getcwd() failed: %s", strerror(errno));
		return;
	}
#else
	{
		char *p;
		int status;
		struct job *jp;
		int pip[2];

		pwd = stalloc(psh, MAXPWD);
		INTOFF;
		if (pipe(pip) < 0)
			error("Pipe call failed");
		jp = makejob((union node *)NULL, 1);
		if (forkshell(jp, (union node *)NULL, FORK_NOJOB) == 0) {
			(void) close(pip[0]);
			if (pip[1] != 1) {
				close(1);
				copyfd(pip[1], 1);
				close(pip[1]);
			}
			(void) execl("/bin/pwd", "pwd", (char *)0);
			error("Cannot exec /bin/pwd");
		}
		(void) close(pip[1]);
		pip[1] = -1;
		p = pwd;
		while ((i = read(pip[0], p, pwd + MAXPWD - p)) > 0
		     || (i == -1 && errno == EINTR)) {
			if (i > 0)
				p += i;
		}
		(void) close(pip[0]);
		pip[0] = -1;
		status = waitforjob(jp);
		if (status != 0)
			error((char *)0);
		if (i < 0 || p == pwd || p[-1] != '\n') {
			if (noerror) {
				INTON;
				return;
			}
			error("pwd command failed");
		}
		p[-1] = '\0';
		INTON;
		psh->curdir = savestr(pwd);
		return;
	}
#endif
}
