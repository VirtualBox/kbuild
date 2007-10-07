/*	$NetBSD: jobs.c,v 1.63 2005/06/01 15:41:19 lukem Exp $	*/

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
static char sccsid[] = "@(#)jobs.c	8.5 (Berkeley) 5/4/95";
#else
__RCSID("$NetBSD: jobs.c,v 1.63 2005/06/01 15:41:19 lukem Exp $");
#endif /* not lint */
#endif

#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/types.h>

#include "shell.h"
#if JOBS
# include <termios.h>
#endif
#include "redir.h"
#include "show.h"
#include "main.h"
#include "parser.h"
#include "nodes.h"
#include "jobs.h"
#include "options.h"
#include "trap.h"
#include "syntax.h"
#include "input.h"
#include "output.h"
#include "memalloc.h"
#include "error.h"
#include "mystring.h"
#include "shinstance.h"

//static struct job *jobtab;		/* array of jobs */
//static int njobs;			/* size of array */
//static int jobs_invalid;		/* set in child */
//MKINIT pid_t backgndpid = -1;	/* pid of last background process */
#if JOBS
//int initialpgrp;		/* pgrp of shell on invocation */
//static int curjob = -1;		/* current job */
#endif
//static int ttyfd = -1;

STATIC void restartjob(shinstance *, struct job *);
STATIC void freejob(shinstance *, struct job *);
STATIC struct job *getjob(shinstance *, const char *, int);
STATIC int dowait(shinstance *, int, struct job *);
STATIC int waitproc(shinstance *, int, struct job *, int *);
STATIC void cmdtxt(shinstance *, union node *);
STATIC void cmdlist(shinstance *, union node *, int);
STATIC void cmdputs(shinstance *, const char *);


/*
 * Turn job control on and off.
 *
 * Note:  This code assumes that the third arg to ioctl is a character
 * pointer, which is true on Berkeley systems but not System V.  Since
 * System V doesn't have job control yet, this isn't a problem now.
 */

//MKINIT int jobctl;

void
setjobctl(shinstance *psh, int on)
{
	if (on == psh->jobctl || psh->rootshell == 0)
		return;
	if (on) {
#if defined(FIOCLEX) || defined(FD_CLOEXEC)
		int err;
		int i;
		if (psh->ttyfd != -1)
			shfile_close(&psh->fdtab, psh->ttyfd);
		if ((psh->ttyfd = shfile_open(&psh->fdtab, "/dev/tty", O_RDWR)) == -1) {
			for (i = 0; i < 3; i++) {
				if (shfile_isatty(&psh->fdtab, i)
				 && (psh->ttyfd = shfile_dup(&psh->fdtab, i)) != -1)
					break;
			}
			if (i == 3)
				goto out;
		}
		/* Move to a high fd */
		for (i = 10; i > 2; i--) {
			if ((err = shfile_fcntl(&psh->fdtab, psh->ttyfd, F_DUPFD, (1 << i) - 1)) != -1)
				break;
		}
		if (err != -1) {
			shfile_close(&psh->fdtab, psh->ttyfd);
			psh->ttyfd = err;
		}
#ifdef FIOCLEX
		err = shfile_ioctl(&psh->fdtab, psh->ttyfd, FIOCLEX, 0);
#elif FD_CLOEXEC
		err = shfile_fcntl(&psh->fdtab, psh->ttyfd, F_SETFD,
		    shfile_fcntl(&psh->fdtab, psh->ttyfd, F_GETFD, 0) | FD_CLOEXEC);
#endif
		if (err == -1) {
			shfile_close(&psh->fdtab, psh->ttyfd);
			psh->ttyfd = -1;
			goto out;
		}
#else
		out2str(psh, "sh: Need FIOCLEX or FD_CLOEXEC to support job control");
		goto out;
#endif
		do { /* while we are in the background */
			if ((psh->initialpgrp = sh_tcgetpgrp(psh, psh->ttyfd)) < 0) {
out:
				out2str(psh, "sh: can't access tty; job control turned off\n");
				mflag(psh) = 0;
				return;
			}
			if (psh->initialpgrp == -1)
				psh->initialpgrp = sh_getpgrp(psh);
			else if (psh->initialpgrp != sh_getpgrp(psh)) {
				sh_killpg(psh, 0, SIGTTIN);
				continue;
			}
		} while (0);

		setsignal(psh, SIGTSTP, 0);
		setsignal(psh, SIGTTOU, 0);
		setsignal(psh, SIGTTIN, 0);
		if (sh_getpgid(psh, 0) != psh->rootpid && sh_setpgid(psh, 0, psh->rootpid) == -1)
			error(psh, "Cannot set process group (%s) at %d",
			    strerror(errno), __LINE__);
		if (sh_tcsetpgrp(psh, psh->ttyfd, psh->rootpid) == -1)
			error(psh, "Cannot set tty process group (%s) at %d",
			    strerror(errno), __LINE__);
	} else { /* turning job control off */
		if (sh_getpgid(psh, 0) != psh->initialpgrp && sh_setpgid(psh, 0, psh->initialpgrp) == -1)
			error(psh, "Cannot set process group (%s) at %d",
			    strerror(errno), __LINE__);
		if (sh_tcsetpgrp(psh, psh->ttyfd, psh->initialpgrp) == -1)
			error(psh, "Cannot set tty process group (%s) at %d",
			    strerror(errno), __LINE__);
		shfile_close(&psh->fdtab, psh->ttyfd);
		psh->ttyfd = -1;
		setsignal(psh, SIGTSTP, 0);
		setsignal(psh, SIGTTOU, 0);
		setsignal(psh, SIGTTIN, 0);
	}
	psh->jobctl = on;
}


#ifdef mkinit
INCLUDE <stdlib.h>

SHELLPROC {
	psh->backgndpid = -1;
#if JOBS
	psh->jobctl = 0;
#endif
}

#endif



#if JOBS
int
fgcmd(shinstance *psh, int argc, char **argv)
{
	struct job *jp;
	int i;
	int status;

	nextopt(psh, "");
	jp = getjob(psh, *psh->argptr, 0);
	if (jp->jobctl == 0)
		error(psh, "job not created under job control");
	out1fmt(psh, "%s", jp->ps[0].cmd);
	for (i = 1; i < jp->nprocs; i++)
		out1fmt(psh, " | %s", jp->ps[i].cmd );
	out1c(psh, '\n');
	output_flushall(psh);

	for (i = 0; i < jp->nprocs; i++)
	    if (sh_tcsetpgrp(psh, psh->ttyfd, jp->ps[i].pid) != -1)
		    break;

	if (i >= jp->nprocs) {
		error(psh, "Cannot set tty process group (%s) at %d",
		    strerror(errno), __LINE__);
	}
	restartjob(psh, jp);
	INTOFF;
	status = waitforjob(psh, jp);
	INTON;
	return status;
}

static void
set_curjob(shinstance *psh, struct job *jp, int mode)
{
	struct job *jp1, *jp2;
	int i, ji;

	ji = (int)(jp - psh->jobtab);

	/* first remove from list */
	if (ji == psh->curjob)
		psh->curjob = jp->prev_job;
	else {
		for (i = 0; i < psh->njobs; i++) {
			if (psh->jobtab[i].prev_job != ji)
				continue;
			psh->jobtab[i].prev_job = jp->prev_job;
			break;
		}
	}

	/* Then re-insert in correct position */
	switch (mode) {
	case 0:	/* job being deleted */
		jp->prev_job = -1;
		break;
	case 1:	/* newly created job or backgrounded job,
		   put after all stopped jobs. */
		if (psh->curjob != -1 && psh->jobtab[psh->curjob].state == JOBSTOPPED) {
			for (jp1 = psh->jobtab + psh->curjob; ; jp1 = jp2) {
				if (jp1->prev_job == -1)
					break;
				jp2 = psh->jobtab + jp1->prev_job;
				if (jp2->state != JOBSTOPPED)
					break;
			}
			jp->prev_job = jp1->prev_job;
			jp1->prev_job = ji;
			break;
		}
		/* FALLTHROUGH */
	case 2:	/* newly stopped job - becomes psh->curjob */
		jp->prev_job = psh->curjob;
		psh->curjob = ji;
		break;
	}
}

int
bgcmd(shinstance *psh, int argc, char **argv)
{
	struct job *jp;
	int i;

	nextopt(psh, "");
	do {
		jp = getjob(psh, *psh->argptr, 0);
		if (jp->jobctl == 0)
			error(psh, "job not created under job control");
		set_curjob(psh, jp, 1);
		out1fmt(psh, "[%ld] %s", (long)(jp - psh->jobtab + 1), jp->ps[0].cmd);
		for (i = 1; i < jp->nprocs; i++)
			out1fmt(psh, " | %s", jp->ps[i].cmd );
		out1c(psh, '\n');
		output_flushall(psh);
		restartjob(psh, jp);
	} while (*psh->argptr && *++psh->argptr);
	return 0;
}


STATIC void
restartjob(shinstance *psh, struct job *jp)
{
	struct procstat *ps;
	int i;

	if (jp->state == JOBDONE)
		return;
	INTOFF;
	for (i = 0; i < jp->nprocs; i++)
		if (sh_killpg(psh, jp->ps[i].pid, SIGCONT) != -1)
			break;
	if (i >= jp->nprocs)
		error(psh, "Cannot continue job (%s)", strerror(errno));
	for (ps = jp->ps, i = jp->nprocs ; --i >= 0 ; ps++) {
		if (WIFSTOPPED(ps->status)) {
			ps->status = -1;
			jp->state = JOBRUNNING;
		}
	}
	INTON;
}
#endif

static void
showjob(shinstance *psh, struct output *out, struct job *jp, int mode)
{
	int procno;
	int st;
	struct procstat *ps;
	size_t col;
	char s[64];

#if JOBS
	if (mode & SHOW_PGID) {
		/* just output process (group) id of pipeline */
		outfmt(out, "%ld\n", (long)jp->ps->pid);
		return;
	}
#endif

	procno = jp->nprocs;
	if (!procno)
		return;

	if (mode & SHOW_PID)
		mode |= SHOW_MULTILINE;

	if ((procno > 1 && !(mode & SHOW_MULTILINE))
	    || (mode & SHOW_SIGNALLED)) {
		/* See if we have more than one status to report */
		ps = jp->ps;
		st = ps->status;
		do {
			int st1 = ps->status;
			if (st1 != st)
				/* yes - need multi-line output */
				mode |= SHOW_MULTILINE;
			if (st1 == -1 || !(mode & SHOW_SIGNALLED) || WIFEXITED(st1))
				continue;
			if (WIFSTOPPED(st1) || ((st1 = WTERMSIG(st1) & 0x7f)
			    && st1 != SIGINT && st1 != SIGPIPE))
				mode |= SHOW_ISSIG;

		} while (ps++, --procno);
		procno = jp->nprocs;
	}

	if (mode & SHOW_SIGNALLED && !(mode & SHOW_ISSIG)) {
		if (jp->state == JOBDONE && !(mode & SHOW_NO_FREE)) {
			TRACE((psh, "showjob: freeing job %d\n", jp - psh->jobtab + 1));
			freejob(psh, jp);
		}
		return;
	}

	for (ps = jp->ps; --procno >= 0; ps++) {	/* for each process */
		if (ps == jp->ps)
			fmtstr(s, 16, "[%ld] %c ",
				(long)(jp - psh->jobtab + 1),
#if JOBS
				jp == psh->jobtab + psh->curjob ? '+' :
				psh->curjob != -1 && jp == psh->jobtab +
					    psh->jobtab[psh->curjob].prev_job ? '-' :
#endif
				' ');
		else
			fmtstr(s, 16, "      " );
		col = strlen(s);
		if (mode & SHOW_PID) {
			fmtstr(s + col, 16, "%ld ", (long)ps->pid);
			     col += strlen(s + col);
		}
		if (ps->status == -1) {
			scopy("Running", s + col);
		} else if (WIFEXITED(ps->status)) {
			st = WEXITSTATUS(ps->status);
			if (st)
				fmtstr(s + col, 16, "Done(%d)", st);
			else
				fmtstr(s + col, 16, "Done");
		} else {
#if JOBS
			if (WIFSTOPPED(ps->status))
				st = WSTOPSIG(ps->status);
			else /* WIFSIGNALED(ps->status) */
#endif
				st = WTERMSIG(ps->status);
			st &= 0x7f;
			if (st < NSIG && sys_siglist[st])
				scopyn(sys_siglist[st], s + col, 32);
			else
				fmtstr(s + col, 16, "Signal %d", st);
			if (WCOREDUMP(ps->status)) {
				col += strlen(s + col);
				scopyn(" (core dumped)", s + col,  64 - col);
			}
		}
		col += strlen(s + col);
		outstr(s, out);
		do {
			outc(' ', out);
			col++;
		} while (col < 30);
		outstr(ps->cmd, out);
		if (mode & SHOW_MULTILINE) {
			if (procno > 0) {
				outc(' ', out);
				outc('|', out);
			}
		} else {
			while (--procno >= 0)
				outfmt(out, " | %s", (++ps)->cmd );
		}
		outc('\n', out);
	}
	flushout(out);
	jp->changed = 0;
	if (jp->state == JOBDONE && !(mode & SHOW_NO_FREE))
		freejob(psh, jp);
}


int
jobscmd(shinstance *psh, int argc, char **argv)
{
	int mode, m;
	int sv = psh->jobs_invalid;

	psh->jobs_invalid = 0;
	mode = 0;
	while ((m = nextopt(psh, "lp")))
		if (m == 'l')
			mode = SHOW_PID;
		else
			mode = SHOW_PGID;
	if (*psh->argptr)
		do
			showjob(psh, psh->out1, getjob(psh, *psh->argptr,0), mode);
		while (*++psh->argptr);
	else
		showjobs(psh, psh->out1, mode);
	psh->jobs_invalid = sv;
	return 0;
}


/*
 * Print a list of jobs.  If "change" is nonzero, only print jobs whose
 * statuses have changed since the last call to showjobs.
 *
 * If the shell is interrupted in the process of creating a job, the
 * result may be a job structure containing zero processes.  Such structures
 * will be freed here.
 */

void
showjobs(shinstance *psh, struct output *out, int mode)
{
	int jobno;
	struct job *jp;
	int silent = 0, gotpid;

	TRACE((psh, "showjobs(%x) called\n", mode));

	/* If not even one one job changed, there is nothing to do */
	gotpid = dowait(psh, 0, NULL);
	while (dowait(psh, 0, NULL) > 0)
		continue;
#ifdef JOBS
	/*
	 * Check if we are not in our foreground group, and if not
	 * put us in it.
	 */
	if (mflag(psh) && gotpid != -1 && sh_tcgetpgrp(psh, psh->ttyfd) != sh_getpid(psh)) {
		if (sh_tcsetpgrp(psh, psh->ttyfd, sh_getpid(psh)) == -1)
			error(psh, "Cannot set tty process group (%s) at %d",
			    strerror(errno), __LINE__);
		TRACE((psh, "repaired tty process group\n"));
		silent = 1;
	}
#endif
	if (psh->jobs_invalid)
		return;

	for (jobno = 1, jp = psh->jobtab ; jobno <= psh->njobs ; jobno++, jp++) {
		if (!jp->used)
			continue;
		if (jp->nprocs == 0) {
			freejob(psh, jp);
			continue;
		}
		if ((mode & SHOW_CHANGED) && !jp->changed)
			continue;
		if (silent && jp->changed) {
			jp->changed = 0;
			continue;
		}
		showjob(psh, out, jp, mode);
	}
}

/*
 * Mark a job structure as unused.
 */

STATIC void
freejob(shinstance *psh, struct job *jp)
{
	INTOFF;
	if (jp->ps != &jp->ps0) {
		ckfree(jp->ps);
		jp->ps = &jp->ps0;
	}
	jp->nprocs = 0;
	jp->used = 0;
#if JOBS
	set_curjob(psh, jp, 0);
#endif
	INTON;
}



int
waitcmd(shinstance *psh, int argc, char **argv)
{
	struct job *job;
	int status, retval;
	struct job *jp;

	nextopt(psh, "");

	if (!*psh->argptr) {
		/* wait for all jobs */
		jp = psh->jobtab;
		if (psh->jobs_invalid)
			return 0;
		for (;;) {
			if (jp >= psh->jobtab + psh->njobs) {
				/* no running procs */
				return 0;
			}
			if (!jp->used || jp->state != JOBRUNNING) {
				jp++;
				continue;
			}
			if (dowait(psh, 1, (struct job *)NULL) == -1)
			       return 128 + SIGINT;
			jp = psh->jobtab;
		}
	}

	retval = 127;		/* XXXGCC: -Wuninitialized */
	for (; *psh->argptr; psh->argptr++) {
		job = getjob(psh, *psh->argptr, 1);
		if (!job) {
			retval = 127;
			continue;
		}
		/* loop until process terminated or stopped */
		while (job->state == JOBRUNNING) {
			if (dowait(psh, 1, (struct job *)NULL) == -1)
			       return 128 + SIGINT;
		}
		status = job->ps[job->nprocs].status;
		if (WIFEXITED(status))
			retval = WEXITSTATUS(status);
#if JOBS
		else if (WIFSTOPPED(status))
			retval = WSTOPSIG(status) + 128;
#endif
		else {
			/* XXX: limits number of signals */
			retval = WTERMSIG(status) + 128;
		}
		if (!iflag(psh))
			freejob(psh, job);
	}
	return retval;
}



int
jobidcmd(shinstance *psh, int argc, char **argv)
{
	struct job *jp;
	int i;

	nextopt(psh, "");
	jp = getjob(psh, *psh->argptr, 0);
	for (i = 0 ; i < jp->nprocs ; ) {
		out1fmt(psh, "%ld", (long)jp->ps[i].pid);
		out1c(psh, ++i < jp->nprocs ? ' ' : '\n');
	}
	return 0;
}

int
getjobpgrp(shinstance *psh, const char *name)
{
	struct job *jp;

	jp = getjob(psh, name, 1);
	if (jp == 0)
		return 0;
	return -jp->ps[0].pid;
}

/*
 * Convert a job name to a job structure.
 */

STATIC struct job *
getjob(shinstance *psh, const char *name, int noerror)
{
	int jobno = -1;
	struct job *jp;
	int pid;
	int i;
	const char *err_msg = "No such job: %s";

	if (name == NULL) {
#if JOBS
		jobno = psh->curjob;
#endif
		err_msg = "No current job";
	} else if (name[0] == '%') {
		if (is_number(name + 1)) {
			jobno = number(psh, name + 1) - 1;
		} else if (!name[2]) {
			switch (name[1]) {
#if JOBS
			case 0:
			case '+':
			case '%':
				jobno = psh->curjob;
				err_msg = "No current job";
				break;
			case '-':
				jobno = psh->curjob;
				if (jobno != -1)
					jobno = psh->jobtab[jobno].prev_job;
				err_msg = "No previous job";
				break;
#endif
			default:
				goto check_pattern;
			}
		} else {
			struct job *found;
    check_pattern:
			found = NULL;
			for (jp = psh->jobtab, i = psh->njobs ; --i >= 0 ; jp++) {
				if (!jp->used || jp->nprocs <= 0)
					continue;
				if ((name[1] == '?'
					&& strstr(jp->ps[0].cmd, name + 2))
				    || prefix(name + 1, jp->ps[0].cmd)) {
					if (found) {
						err_msg = "%s: ambiguous";
						found = 0;
						break;
					}
					found = jp;
				}
			}
			if (found)
				return found;
		}

	} else if (is_number(name)) {
		pid = number(psh, name);
		for (jp = psh->jobtab, i = psh->njobs ; --i >= 0 ; jp++) {
			if (jp->used && jp->nprocs > 0
			 && jp->ps[jp->nprocs - 1].pid == pid)
				return jp;
		}
	}

	if (!psh->jobs_invalid && jobno >= 0 && jobno < psh->njobs) {
		jp = psh->jobtab + jobno;
		if (jp->used)
			return jp;
	}
	if (!noerror)
		error(psh, err_msg, name);
	return 0;
}



/*
 * Return a new job structure,
 */

struct job *
makejob(shinstance *psh, union node *node, int nprocs)
{
	int i;
	struct job *jp;

	if (psh->jobs_invalid) {
		for (i = psh->njobs, jp = psh->jobtab ; --i >= 0 ; jp++) {
			if (jp->used)
				freejob(psh, jp);
		}
		psh->jobs_invalid = 0;
	}

	for (i = psh->njobs, jp = psh->jobtab ; ; jp++) {
		if (--i < 0) {
			INTOFF;
			if (psh->njobs == 0) {
				psh->jobtab = ckmalloc(4 * sizeof psh->jobtab[0]);
			} else {
				jp = ckmalloc((psh->njobs + 4) * sizeof psh->jobtab[0]);
				memcpy(jp, psh->jobtab, psh->njobs * sizeof jp[0]);
				/* Relocate `ps' pointers */
				for (i = 0; i < psh->njobs; i++)
					if (jp[i].ps == &psh->jobtab[i].ps0)
						jp[i].ps = &jp[i].ps0;
				ckfree(psh->jobtab);
				psh->jobtab = jp;
			}
			jp = psh->jobtab + psh->njobs;
			for (i = 4 ; --i >= 0 ; psh->jobtab[psh->njobs++].used = 0);
			INTON;
			break;
		}
		if (jp->used == 0)
			break;
	}
	INTOFF;
	jp->state = JOBRUNNING;
	jp->used = 1;
	jp->changed = 0;
	jp->nprocs = 0;
#if JOBS
	jp->jobctl = psh->jobctl;
	set_curjob(psh, jp, 1);
#endif
	if (nprocs > 1) {
		jp->ps = ckmalloc(nprocs * sizeof (struct procstat));
	} else {
		jp->ps = &jp->ps0;
	}
	INTON;
	TRACE((psh, "makejob(0x%lx, %d) returns %%%d\n", (long)node, nprocs,
	       jp - psh->jobtab + 1));
	return jp;
}


/*
 * Fork off a subshell.  If we are doing job control, give the subshell its
 * own process group.  Jp is a job structure that the job is to be added to.
 * N is the command that will be evaluated by the child.  Both jp and n may
 * be NULL.  The mode parameter can be one of the following:
 *	FORK_FG - Fork off a foreground process.
 *	FORK_BG - Fork off a background process.
 *	FORK_NOJOB - Like FORK_FG, but don't give the process its own
 *		     process group even if job control is on.
 *
 * When job control is turned off, background processes have their standard
 * input redirected to /dev/null (except for the second and later processes
 * in a pipeline).
 */

int
forkshell(shinstance *psh, struct job *jp, union node *n, int mode)
{
	int pid;

	TRACE((psh, "forkshell(%%%d, %p, %d) called\n", jp - psh->jobtab, n, mode));
	switch ((pid = sh_fork(psh))) {
	case -1:
		TRACE((psh, "Fork failed, errno=%d\n", errno));
		INTON;
		error(psh, "Cannot fork");
		return -1; /* won't get here */
	case 0:
		forkchild(psh, jp, n, mode, 0);
		return 0;
	default:
		return forkparent(psh, jp, n, mode, pid);
	}
}

int
forkparent(shinstance *psh, struct job *jp, union node *n, int mode, pid_t pid)
{
	int pgrp;

	if (psh->rootshell && mode != FORK_NOJOB && mflag(psh)) {
		if (jp == NULL || jp->nprocs == 0)
			pgrp = pid;
		else
			pgrp = jp->ps[0].pid;
		/* This can fail because we are doing it in the child also */
		(void)sh_setpgid(psh, pid, pgrp);
	}
	if (mode == FORK_BG)
		psh->backgndpid = pid;		/* set $! */
	if (jp) {
		struct procstat *ps = &jp->ps[jp->nprocs++];
		ps->pid = pid;
		ps->status = -1;
		ps->cmd[0] = 0;
		if (/* iflag && rootshell && */ n)
			commandtext(psh, ps, n);
	}
	TRACE((psh, "In parent shell:  child = %d\n", pid));
	return pid;
}

void
forkchild(shinstance *psh, struct job *jp, union node *n, int mode, int vforked)
{
	int wasroot;
	int pgrp;
	const char *devnull = _PATH_DEVNULL;
	const char *nullerr = "Can't open %s";

	wasroot = psh->rootshell;
	TRACE((psh, "Child shell %d\n", sh_getpid(psh)));
	if (!vforked)
		psh->rootshell = 0;

	closescript(psh, vforked);
	clear_traps(psh, vforked);
#if JOBS
	if (!vforked)
		psh->jobctl = 0;		/* do job control only in root shell */
	if (wasroot && mode != FORK_NOJOB && mflag(psh)) {
		if (jp == NULL || jp->nprocs == 0)
			pgrp = sh_getpid(psh);
		else
			pgrp = jp->ps[0].pid;
		/* This can fail because we are doing it in the parent also */
		(void)sh_setpgid(psh, 0, pgrp);
		if (mode == FORK_FG) {
			if (sh_tcsetpgrp(psh, psh->ttyfd, pgrp) == -1)
				error(psh, "Cannot set tty process group (%s) at %d",
				    strerror(errno), __LINE__);
		}
		setsignal(psh, SIGTSTP, vforked);
		setsignal(psh, SIGTTOU, vforked);
	} else if (mode == FORK_BG) {
		ignoresig(psh, SIGINT, vforked);
		ignoresig(psh, SIGQUIT, vforked);
		if ((jp == NULL || jp->nprocs == 0) &&
		    ! fd0_redirected_p(psh)) {
			shfile_close(&psh->fdtab, 0);
			if (open(devnull, O_RDONLY) != 0)
				error(psh, nullerr, devnull);
		}
	}
#else
	if (mode == FORK_BG) {
		ignoresig(psh, SIGINT, vforked);
		ignoresig(psh, SIGQUIT, vforked);
		if ((jp == NULL || jp->nprocs == 0) &&
		    ! fd0_redirected_p(psh)) {
			shfile_close(&psh->fdtab, 0);
			if (open(devnull, O_RDONLY) != 0)
				error(psh, nullerr, devnull);
		}
	}
#endif
	if (wasroot && iflag(psh)) {
		setsignal(psh, SIGINT, vforked);
		setsignal(psh, SIGQUIT, vforked);
		setsignal(psh, SIGTERM, vforked);
	}

	if (!vforked)
		psh->jobs_invalid = 1;
}

/*
 * Wait for job to finish.
 *
 * Under job control we have the problem that while a child process is
 * running interrupts generated by the user are sent to the child but not
 * to the shell.  This means that an infinite loop started by an inter-
 * active user may be hard to kill.  With job control turned off, an
 * interactive user may place an interactive program inside a loop.  If
 * the interactive program catches interrupts, the user doesn't want
 * these interrupts to also abort the loop.  The approach we take here
 * is to have the shell ignore interrupt signals while waiting for a
 * forground process to terminate, and then send itself an interrupt
 * signal if the child process was terminated by an interrupt signal.
 * Unfortunately, some programs want to do a bit of cleanup and then
 * exit on interrupt; unless these processes terminate themselves by
 * sending a signal to themselves (instead of calling exit) they will
 * confuse this approach.
 */

int
waitforjob(shinstance *psh, struct job *jp)
{
#if JOBS
	int mypgrp = sh_getpgrp(psh);
#endif
	int status;
	int st;

	INTOFF;
	TRACE((psh, "waitforjob(%%%d) called\n", jp - psh->jobtab + 1));
	while (jp->state == JOBRUNNING) {
		dowait(psh, 1, jp);
	}
#if JOBS
	if (jp->jobctl) {
		if (sh_tcsetpgrp(psh, psh->ttyfd, mypgrp) == -1)
			error(psh, "Cannot set tty process group (%s) at %d",
			    strerror(errno), __LINE__);
	}
	if (jp->state == JOBSTOPPED && psh->curjob != jp - psh->jobtab)
		set_curjob(psh, jp, 2);
#endif
	status = jp->ps[jp->nprocs - 1].status;
	/* convert to 8 bits */
	if (WIFEXITED(status))
		st = WEXITSTATUS(status);
#if JOBS
	else if (WIFSTOPPED(status))
		st = WSTOPSIG(status) + 128;
#endif
	else
		st = WTERMSIG(status) + 128;
	TRACE((psh, "waitforjob: job %d, nproc %d, status %x, st %x\n",
		jp - psh->jobtab + 1, jp->nprocs, status, st ));
#if JOBS
	if (jp->jobctl) {
		/*
		 * This is truly gross.
		 * If we're doing job control, then we did a TIOCSPGRP which
		 * caused us (the shell) to no longer be in the controlling
		 * session -- so we wouldn't have seen any ^C/SIGINT.  So, we
		 * intuit from the subprocess exit status whether a SIGINT
		 * occurred, and if so interrupt ourselves.  Yuck.  - mycroft
		 */
		if (WIFSIGNALED(status) && WTERMSIG(status) == SIGINT)
			sh_raise_sigint(psh);/*raise(SIGINT);*/
	}
#endif
	if (! JOBS || jp->state == JOBDONE)
		freejob(psh, jp);
	INTON;
	return st;
}



/*
 * Wait for a process to terminate.
 */

STATIC int
dowait(shinstance *psh, int block, struct job *job)
{
	int pid;
	int status;
	struct procstat *sp;
	struct job *jp;
	struct job *thisjob;
	int done;
	int stopped;

	TRACE((psh, "dowait(%d) called\n", block));
	do {
		pid = waitproc(psh, block, job, &status);
		TRACE((psh, "wait returns pid %d, status %d\n", pid, status));
	} while (pid == -1 && errno == EINTR && psh->gotsig[SIGINT - 1] == 0);
	if (pid <= 0)
		return pid;
	INTOFF;
	thisjob = NULL;
	for (jp = psh->jobtab ; jp < psh->jobtab + psh->njobs ; jp++) {
		if (jp->used) {
			done = 1;
			stopped = 1;
			for (sp = jp->ps ; sp < jp->ps + jp->nprocs ; sp++) {
				if (sp->pid == -1)
					continue;
				if (sp->pid == pid) {
					TRACE((psh, "Job %d: changing status of proc %d from 0x%x to 0x%x\n", jp - psh->jobtab + 1, pid, sp->status, status));
					sp->status = status;
					thisjob = jp;
				}
				if (sp->status == -1)
					stopped = 0;
				else if (WIFSTOPPED(sp->status))
					done = 0;
			}
			if (stopped) {		/* stopped or done */
				int state = done ? JOBDONE : JOBSTOPPED;
				if (jp->state != state) {
					TRACE((psh, "Job %d: changing state from %d to %d\n", jp - psh->jobtab + 1, jp->state, state));
					jp->state = state;
#if JOBS
					if (done)
						set_curjob(psh, jp, 0);
#endif
				}
			}
		}
	}

	if (thisjob && thisjob->state != JOBRUNNING) {
		int mode = 0;
		if (!psh->rootshell || !iflag(psh))
			mode = SHOW_SIGNALLED;
		if (job == thisjob)
			mode = SHOW_SIGNALLED | SHOW_NO_FREE;
		if (mode)
			showjob(psh, psh->out2, thisjob, mode);
		else {
			TRACE((psh, "Not printing status, rootshell=%d, job=%p\n",
				psh->rootshell, job));
			thisjob->changed = 1;
		}
	}

	INTON;
	return pid;
}



/*
 * Do a wait system call.  If job control is compiled in, we accept
 * stopped processes.  If block is zero, we return a value of zero
 * rather than blocking.
 */
STATIC int
waitproc(shinstance *psh, int block, struct job *jp, int *status)
{
	int flags = 0;

#if JOBS
	if (jp != NULL && jp->jobctl)
		flags |= WUNTRACED;
#endif
	if (block == 0)
		flags |= WNOHANG;
	return sh_waitpid(psh, -1, status, flags);
}

/*
 * return 1 if there are stopped jobs, otherwise 0
 */
//int job_warning = 0;
int
stoppedjobs(shinstance *psh)
{
	int jobno;
	struct job *jp;

	if (psh->job_warning || psh->jobs_invalid)
		return (0);
	for (jobno = 1, jp = psh->jobtab; jobno <= psh->njobs; jobno++, jp++) {
		if (jp->used == 0)
			continue;
		if (jp->state == JOBSTOPPED) {
			out2str(psh, "You have stopped jobs.\n");
			psh->job_warning = 2;
			return (1);
		}
	}

	return (0);
}

/*
 * Return a string identifying a command (to be printed by the
 * jobs command).
 */

//STATIC char *cmdnextc;
//STATIC int cmdnleft;

void
commandtext(shinstance *psh, struct procstat *ps, union node *n)
{
	int len;

	psh->cmdnextc = ps->cmd;
	if (iflag(psh) || mflag(psh) || sizeof(ps->cmd) < 100)
		len = sizeof(ps->cmd);
	else
		len = sizeof(ps->cmd) / 10;
	psh->cmdnleft = len;
	cmdtxt(psh, n);
	if (psh->cmdnleft <= 0) {
		char *p = ps->cmd + len - 4;
		p[0] = '.';
		p[1] = '.';
		p[2] = '.';
		p[3] = 0;
	} else
		*psh->cmdnextc = '\0';
	TRACE((psh, "commandtext: ps->cmd %x, end %x, left %d\n\t\"%s\"\n",
		ps->cmd, psh->cmdnextc, psh->cmdnleft, ps->cmd));
}


STATIC void
cmdtxt(shinstance *psh, union node *n)
{
	union node *np;
	struct nodelist *lp;
	const char *p;
	int i;
	char s[2];

	if (n == NULL || psh->cmdnleft <= 0)
		return;
	switch (n->type) {
	case NSEMI:
		cmdtxt(psh, n->nbinary.ch1);
		cmdputs(psh, "; ");
		cmdtxt(psh, n->nbinary.ch2);
		break;
	case NAND:
		cmdtxt(psh, n->nbinary.ch1);
		cmdputs(psh, " && ");
		cmdtxt(psh, n->nbinary.ch2);
		break;
	case NOR:
		cmdtxt(psh, n->nbinary.ch1);
		cmdputs(psh, " || ");
		cmdtxt(psh, n->nbinary.ch2);
		break;
	case NPIPE:
		for (lp = n->npipe.cmdlist ; lp ; lp = lp->next) {
			cmdtxt(psh, lp->n);
			if (lp->next)
				cmdputs(psh, " | ");
		}
		break;
	case NSUBSHELL:
		cmdputs(psh, "(");
		cmdtxt(psh, n->nredir.n);
		cmdputs(psh, ")");
		break;
	case NREDIR:
	case NBACKGND:
		cmdtxt(psh, n->nredir.n);
		break;
	case NIF:
		cmdputs(psh, "if ");
		cmdtxt(psh, n->nif.test);
		cmdputs(psh, "; then ");
		cmdtxt(psh, n->nif.ifpart);
		if (n->nif.elsepart) {
			cmdputs(psh, "; else ");
			cmdtxt(psh, n->nif.elsepart);
		}
		cmdputs(psh, "; fi");
		break;
	case NWHILE:
		cmdputs(psh, "while ");
		goto until;
	case NUNTIL:
		cmdputs(psh, "until ");
until:
		cmdtxt(psh, n->nbinary.ch1);
		cmdputs(psh, "; do ");
		cmdtxt(psh, n->nbinary.ch2);
		cmdputs(psh, "; done");
		break;
	case NFOR:
		cmdputs(psh, "for ");
		cmdputs(psh, n->nfor.var);
		cmdputs(psh, " in ");
		cmdlist(psh, n->nfor.args, 1);
		cmdputs(psh, "; do ");
		cmdtxt(psh, n->nfor.body);
		cmdputs(psh, "; done");
		break;
	case NCASE:
		cmdputs(psh, "case ");
		cmdputs(psh, n->ncase.expr->narg.text);
		cmdputs(psh, " in ");
		for (np = n->ncase.cases; np; np = np->nclist.next) {
			cmdtxt(psh, np->nclist.pattern);
			cmdputs(psh, ") ");
			cmdtxt(psh, np->nclist.body);
			cmdputs(psh, ";; ");
		}
		cmdputs(psh, "esac");
		break;
	case NDEFUN:
		cmdputs(psh, n->narg.text);
		cmdputs(psh, "() { ... }");
		break;
	case NCMD:
		cmdlist(psh, n->ncmd.args, 1);
		cmdlist(psh, n->ncmd.redirect, 0);
		break;
	case NARG:
		cmdputs(psh, n->narg.text);
		break;
	case NTO:
		p = ">";  i = 1;  goto redir;
	case NCLOBBER:
		p = ">|";  i = 1;  goto redir;
	case NAPPEND:
		p = ">>";  i = 1;  goto redir;
	case NTOFD:
		p = ">&";  i = 1;  goto redir;
	case NFROM:
		p = "<";  i = 0;  goto redir;
	case NFROMFD:
		p = "<&";  i = 0;  goto redir;
	case NFROMTO:
		p = "<>";  i = 0;  goto redir;
redir:
		if (n->nfile.fd != i) {
			s[0] = n->nfile.fd + '0';
			s[1] = '\0';
			cmdputs(psh, s);
		}
		cmdputs(psh, p);
		if (n->type == NTOFD || n->type == NFROMFD) {
			s[0] = n->ndup.dupfd + '0';
			s[1] = '\0';
			cmdputs(psh, s);
		} else {
			cmdtxt(psh, n->nfile.fname);
		}
		break;
	case NHERE:
	case NXHERE:
		cmdputs(psh, "<<...");
		break;
	default:
		cmdputs(psh, "???");
		break;
	}
}

STATIC void
cmdlist(shinstance *psh, union node *np, int sep)
{
	for (; np; np = np->narg.next) {
		if (!sep)
			cmdputs(psh, " ");
		cmdtxt(psh, np);
		if (sep && np->narg.next)
			cmdputs(psh, " ");
	}
}


STATIC void
cmdputs(shinstance *psh, const char *s)
{
	const char *p, *str = 0;
	char c, cc[2] = " ";
	char *nextc;
	int nleft;
	int subtype = 0;
	int quoted = 0;
	static char vstype[16][4] = { "", "}", "-", "+", "?", "=",
					"#", "##", "%", "%%" };

	p = s;
	nextc = psh->cmdnextc;
	nleft = psh->cmdnleft;
	while (nleft > 0 && (c = *p++) != 0) {
		switch (c) {
		case CTLESC:
			c = *p++;
			break;
		case CTLVAR:
			subtype = *p++;
			if ((subtype & VSTYPE) == VSLENGTH)
				str = "${#";
			else
				str = "${";
			if (!(subtype & VSQUOTE) != !(quoted & 1)) {
				quoted ^= 1;
				c = '"';
			} else
				c = *str++;
			break;
		case CTLENDVAR:
			if (quoted & 1) {
				c = '"';
				str = "}";
			} else
				c = '}';
			quoted >>= 1;
			subtype = 0;
			break;
		case CTLBACKQ:
			c = '$';
			str = "(...)";
			break;
		case CTLBACKQ+CTLQUOTE:
			c = '"';
			str = "$(...)\"";
			break;
		case CTLARI:
			c = '$';
			str = "((";
			break;
		case CTLENDARI:
			c = ')';
			str = ")";
			break;
		case CTLQUOTEMARK:
			quoted ^= 1;
			c = '"';
			break;
		case '=':
			if (subtype == 0)
				break;
			str = vstype[subtype & VSTYPE];
			if (subtype & VSNUL)
				c = ':';
			else
				c = *str++;
			if (c != '}')
				quoted <<= 1;
			break;
		case '\'':
		case '\\':
		case '"':
		case '$':
			/* These can only happen inside quotes */
			cc[0] = c;
			str = cc;
			c = '\\';
			break;
		default:
			break;
		}
		do {
			*nextc++ = c;
		} while (--nleft > 0 && str && (c = *str++));
		str = 0;
	}
	if ((quoted & 1) && nleft) {
		*nextc++ = '"';
		nleft--;
	}
	psh->cmdnleft = nleft;
	psh->cmdnextc = nextc;
}
