/*	$NetBSD: eval.c,v 1.84 2005/06/23 23:05:29 christos Exp $	*/

/*-
 * Copyright (c) 1993
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
static char sccsid[] = "@(#)eval.c	8.9 (Berkeley) 6/8/95";
#else
__RCSID("$NetBSD: eval.c,v 1.84 2005/06/23 23:05:29 christos Exp $");
#endif /* not lint */
#endif

#include <assert.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#ifdef HAVE_SYSCTL_H
# ifdef __OpenBSD__ /* joyful crap */
#  include <sys/param.h>
#  undef psh
# endif
# include <sys/sysctl.h>
#endif

/*
 * Evaluate a command.
 */

#include "shell.h"
#include "nodes.h"
#include "syntax.h"
#include "expand.h"
#include "parser.h"
#include "jobs.h"
#include "eval.h"
#include "builtins.h"
#include "options.h"
#include "exec.h"
#include "redir.h"
#include "input.h"
#include "output.h"
#include "trap.h"
#include "var.h"
#include "memalloc.h"
#include "error.h"
#include "show.h"
#include "mystring.h"
#include "main.h"
#ifndef SMALL
# include "myhistedit.h"
#endif
#include "shinstance.h"


/* flags in argument to evaltree */
#define EV_EXIT 01		/* exit after evaluating tree */
#define EV_TESTED 02		/* exit status is checked; ignore -e flag */
#define EV_BACKCMD 04		/* command executing within back quotes */

/*int evalskip;*/			/* set if we are skipping commands */
/*STATIC int skipcount;*/		/* number of levels to skip */
/*MKINIT int loopnest;*/		/* current loop nesting level */
/*int funcnest;*/			/* depth of function calls */


/*char *commandname;*/
/*struct strlist *cmdenviron;*/
/*int exitstatus;*/			/* exit status of last command */
/*int back_exitstatus;*/		/* exit status of backquoted command */


STATIC void evalloop(shinstance *, union node *, int);
STATIC void evalfor(shinstance *, union node *, int);
STATIC void evalcase(shinstance *, union node *, int);
STATIC void evalsubshell(shinstance *, union node *, int);
STATIC unsigned expredir(shinstance *, union node *);
STATIC void evalpipe(shinstance *, union node *);
STATIC void evalcommand(shinstance *, union node *, int, struct backcmd *);
STATIC void prehash(shinstance *, union node *);


/*
 * Called to reset things after an exception.
 */

#ifdef mkinit
INCLUDE "eval.h"

RESET {
	psh->evalskip = 0;
	psh->loopnest = 0;
	psh->funcnest = 0;
}

SHELLPROC {
	psh->exitstatus = 0;
}
#endif

static int
sh_pipe(shinstance *psh, int fds[2])
{
	int nfd;

	if (shfile_pipe(&psh->fdtab, fds))
		return -1;

	if (fds[0] < 3) {
		nfd = shfile_fcntl(&psh->fdtab, fds[0], F_DUPFD, 3);
		if (nfd != -1) {
			shfile_close(&psh->fdtab, fds[0]);
			fds[0] = nfd;
		}
	}

	if (fds[1] < 3) {
		nfd = shfile_fcntl(&psh->fdtab, fds[1], F_DUPFD, 3);
		if (nfd != -1) {
			shfile_close(&psh->fdtab, fds[1]);
			fds[1] = nfd;
		}
	}
	return 0;
}


/*
 * The eval commmand.
 */

int
evalcmd(shinstance *psh, int argc, char **argv)
{
	char *p;
	char *concat;
	char **ap;

	if (argc > 1) {
		p = argv[1];
		if (argc > 2) {
			STARTSTACKSTR(psh, concat);
			ap = argv + 2;
			for (;;) {
				while (*p)
					STPUTC(psh, *p++, concat);
				if ((p = *ap++) == NULL)
					break;
				STPUTC(psh, ' ', concat);
			}
			STPUTC(psh, '\0', concat);
			p = grabstackstr(psh, concat);
		}
		evalstring(psh, p, EV_TESTED);
	}
	return psh->exitstatus;
}


/*
 * Execute a command or commands contained in a string.
 */

void
evalstring(shinstance *psh, char *s, int flag)
{
	union node *n;
	struct stackmark smark;

	setstackmark(psh, &smark);
	setinputstring(psh, s, 1);

	while ((n = parsecmd(psh, 0)) != NEOF) {
		evaltree(psh, n, flag);
		popstackmark(psh, &smark);
	}
	popfile(psh);
	popstackmark(psh, &smark);
}



/*
 * Evaluate a parse tree.  The value is left in the global variable
 * exitstatus.
 */

void
evaltree(shinstance *psh, union node *n, int flags)
{
	if (n == NULL) {
		TRACE((psh, "evaltree(NULL) called\n"));
		psh->exitstatus = 0;
		goto out;
	}
#ifndef SMALL
	psh->displayhist = 1;	/* show history substitutions done with fc */
#endif
	TRACE((psh, "pid %" SHPID_PRI ", evaltree(%p: %d, %d) called\n",
	       sh_getpid(psh), n, n->type, flags));
	switch (n->type) {
	case NSEMI:
		evaltree(psh, n->nbinary.ch1, flags & EV_TESTED);
		if (psh->evalskip)
			goto out;
		evaltree(psh, n->nbinary.ch2, flags);
		break;
	case NAND:
		evaltree(psh, n->nbinary.ch1, EV_TESTED);
		if (psh->evalskip || psh->exitstatus != 0)
			goto out;
		evaltree(psh, n->nbinary.ch2, flags);
		break;
	case NOR:
		evaltree(psh, n->nbinary.ch1, EV_TESTED);
		if (psh->evalskip || psh->exitstatus == 0)
			goto out;
		evaltree(psh, n->nbinary.ch2, flags);
		break;
	case NREDIR: {
		unsigned const oldfnames = expredir(psh, n->nredir.redirect);
		redirect(psh, n->nredir.redirect, REDIR_PUSH);
		evaltree(psh, n->nredir.n, flags);
		popredir(psh);
		expredircleanup(psh, oldfnames);
		break;
	}
	case NSUBSHELL:
		evalsubshell(psh, n, flags);
		break;
	case NBACKGND:
		evalsubshell(psh, n, flags);
		break;
	case NIF: {
		evaltree(psh, n->nif.test, EV_TESTED);
		if (psh->evalskip)
			goto out;
		if (psh->exitstatus == 0)
			evaltree(psh, n->nif.ifpart, flags);
		else if (n->nif.elsepart)
			evaltree(psh, n->nif.elsepart, flags);
		else
			psh->exitstatus = 0;
		break;
	}
	case NWHILE:
	case NUNTIL:
		evalloop(psh, n, flags);
		break;
	case NFOR:
		evalfor(psh, n, flags);
		break;
	case NCASE:
		evalcase(psh, n, flags);
		break;
	case NDEFUN:
		defun(psh, n->narg.text, n->narg.next);
		psh->exitstatus = 0;
		break;
	case NNOT:
		evaltree(psh, n->nnot.com, EV_TESTED);
		psh->exitstatus = !psh->exitstatus;
		break;
	case NPIPE:
		evalpipe(psh, n);
		break;
	case NCMD:
		evalcommand(psh, n, flags, (struct backcmd *)NULL);
		break;
	default:
		out1fmt(psh, "Node type = %d\n", n->type);
		flushout(&psh->output);
		break;
	}
out:
	if (psh->pendingsigs)
		dotrap(psh);
	if ((flags & EV_EXIT) != 0)
		exitshell(psh, psh->exitstatus);
}


STATIC void
evalloop(shinstance *psh, union node *n, int flags)
{
	int status;

	psh->loopnest++;
	status = 0;
	for (;;) {
		evaltree(psh, n->nbinary.ch1, EV_TESTED);
		if (psh->evalskip) {
skipping:	  if (psh->evalskip == SKIPCONT && --psh->skipcount <= 0) {
				psh->evalskip = 0;
				continue;
			}
			if (psh->evalskip == SKIPBREAK && --psh->skipcount <= 0)
				psh->evalskip = 0;
			break;
		}
		if (n->type == NWHILE) {
			if (psh->exitstatus != 0)
				break;
		} else {
			if (psh->exitstatus == 0)
				break;
		}
		evaltree(psh, n->nbinary.ch2, flags & EV_TESTED);
		status = psh->exitstatus;
		if (psh->evalskip)
			goto skipping;
	}
	psh->loopnest--;
	psh->exitstatus = status;
}



STATIC void
evalfor(shinstance *psh, union node *n, int flags)
{
	struct arglist arglist;
	union node *argp;
	struct strlist *sp;
	struct stackmark smark;
	int status = 0;

	setstackmark(psh, &smark);
	arglist.lastp = &arglist.list;
	for (argp = n->nfor.args ; argp ; argp = argp->narg.next) {
		expandarg(psh, argp, &arglist, EXP_FULL | EXP_TILDE);
		if (psh->evalskip)
			goto out;
	}
	*arglist.lastp = NULL;

	psh->loopnest++;
	for (sp = arglist.list ; sp ; sp = sp->next) {
		setvar(psh, n->nfor.var, sp->text, 0);
		evaltree(psh, n->nfor.body, flags & EV_TESTED);
		status = psh->exitstatus;
		if (psh->evalskip) {
			if (psh->evalskip == SKIPCONT && --psh->skipcount <= 0) {
				psh->evalskip = 0;
				continue;
			}
			if (psh->evalskip == SKIPBREAK && --psh->skipcount <= 0)
				psh->evalskip = 0;
			break;
		}
	}
	psh->loopnest--;
	psh->exitstatus = status;
out:
	popstackmark(psh, &smark);
}



STATIC void
evalcase(shinstance *psh, union node *n, int flags)
{
	union node *cp;
	union node *patp;
	struct arglist arglist;
	struct stackmark smark;
	int status = 0;

	setstackmark(psh, &smark);
	arglist.lastp = &arglist.list;
	expandarg(psh, n->ncase.expr, &arglist, EXP_TILDE);
	for (cp = n->ncase.cases ; cp && psh->evalskip == 0 ; cp = cp->nclist.next) {
		for (patp = cp->nclist.pattern ; patp ; patp = patp->narg.next) {
			if (casematch(psh, patp, arglist.list->text)) {
				if (psh->evalskip == 0) {
					evaltree(psh, cp->nclist.body, flags);
					status = psh->exitstatus;
				}
				goto out;
			}
		}
	}
out:
	psh->exitstatus = status;
	popstackmark(psh, &smark);
}


#ifdef KASH_USE_FORKSHELL2
/*
 * Child of evalsubshell.
 */
struct evalsubshellchild
{
    int flags;
    int backgnd;
};

static int evalsubshell_child(shinstance *psh, union node *n, void *argp)
{
    struct evalsubshellchild args = *(struct evalsubshellchild *)argp;

    INTON;
    if (args.backgnd)
	    args.flags &=~ EV_TESTED;
    redirect(psh, n->nredir.redirect, 0);
    /* never returns */
    evaltree(psh, n->nredir.n, args.flags | EV_EXIT);
    /** @todo make us return here. */
    return 0;
}
#endif /* KASH_USE_FORKSHELL2 */


/*
 * Kick off a subshell to evaluate a tree.
 */

STATIC void
evalsubshell(shinstance *psh, union node *n, int flags)
{
	struct job *jp;
	int backgnd = (n->type == NBACKGND);
	unsigned expfnamedepth;

	expfnamedepth = expredir(psh, n->nredir.redirect);
	INTOFF;
	jp = makejob(psh, n, 1);
#ifdef KASH_USE_FORKSHELL2
	{
		struct evalsubshellchild args;
		args.flags = flags;
		args.backgnd = backgnd;
		forkshell2(psh, jp, n, backgnd ? FORK_BG : FORK_FG,
		           evalsubshell_child, n, &args, sizeof(args), NULL);
	}
#else
	if (forkshell(psh, jp, n, backgnd ? FORK_BG : FORK_FG) == 0) {
		INTON;
		if (backgnd)
			flags &=~ EV_TESTED;
		redirect(psh, n->nredir.redirect, 0);
		/* never returns */
		evaltree(psh, n->nredir.n, flags | EV_EXIT);
	}
#endif
	if (! backgnd)
		psh->exitstatus = waitforjob(psh, jp);
	expredircleanup(psh, expfnamedepth);
	INTON;
}



/*
 * Compute the names of the files in a redirection list.
 */

STATIC unsigned
expredir(shinstance *psh, union node *n)
{
	union node *redir;
	redirexpfnames *expfnames;
	unsigned i;

	/* We typically end up here w/o redirections. */
	if (!n)
	    return !(expfnames = psh->expfnames) ? 0 : expfnames->depth + 1;

	/* Prepare a table for the expanded names. */
	i = 0;
	for (redir = n; redir ; redir = redir->nfile.next)
	    i++;
	expfnames = stalloc(psh, offsetof(redirexpfnames, names) +  sizeof(expfnames->names[0]) * i);
	expfnames->count = i;
	TRACE2((psh, "expredir: %p: count=%u\n", expfnames, i));

	/* Do the expansion. */
	for (redir = n, i = 0 ; redir ; redir = redir->nfile.next, i++) {
		struct arglist fn;
		fn.lastp = &fn.list;
		switch (redir->type) {
		case NFROMTO:
		case NFROM:
		case NTO:
		case NCLOBBER:
		case NAPPEND:
			expandarg(psh, redir->nfile.fname, &fn, EXP_TILDE | EXP_REDIR);
			expfnames->names[i] = fn.list->text;
			break;
		case NFROMFD:
		case NTOFD:
			if (redir->ndup.vname) {
				expandarg(psh, redir->ndup.vname, &fn, EXP_FULL | EXP_TILDE);
				fixredir(psh, redir, fn.list->text, 1);
			}
			expfnames->names[i] = NULL;
			break;
		default:
			assert(redir->type == NHERE || redir->type == NXHERE);
			expfnames->names[i] = NULL;
			break;
		}
	}
	assert(i == expfnames->count);

	/* Do the linking at the end, as nesting happens when we expand backtick arguments. */
	expfnames->prev = psh->expfnames;
	psh->expfnames = expfnames;
	return expfnames->depth = psh->expfnames ? psh->expfnames->depth + 1 : 1;
}

STATIC void
expredircleanup(shinstance *psh, unsigned depth)
{
	redirexpfnames *expfnames = psh->expfnames;
	assert(expfnames == NULL ? depth == 0 : expfnames->depth == depth || expfnames->depth + 1 == depth);
	while (expfnames && expfnames->depth >= depth)
	    expfnames = psh->expfnames = expfnames->prev;
}


#ifdef KASH_USE_FORKSHELL2
/*
 * Child of evalpipe.
 */
struct evalpipechild
{
	int prevfd;
	int pip[2];
};

static int evalpipe_child(shinstance *psh, union node *n, void *argp)
{
	struct evalpipechild args = *(struct evalpipechild *)argp;

	if (args.prevfd > 0) {
		movefd(psh, args.prevfd, 0);
	}
	if (args.pip[1] >= 0) {
		shfile_close(&psh->fdtab, args.pip[0]);
		if (args.pip[1] != 1) {
			movefd(psh, args.pip[1], 1);
		}
	}
	evaltree(psh, n, EV_EXIT);
	/** @todo make it return thru here. */
	return 0;
}
#endif /* KASH_USE_FORKSHELL2 */

/*
 * Evaluate a pipeline.  All the processes in the pipeline are children
 * of the process creating the pipeline.  (This differs from some versions
 * of the shell, which make the last process in a pipeline the parent
 * of all the rest.)
 */

STATIC void
evalpipe(shinstance *psh, union node *n)
{
	struct job *jp;
	struct nodelist *lp;
	int pipelen;
	int prevfd;
	int pip[2];

	TRACE((psh, "evalpipe(0x%lx) called\n", (long)n));
	pipelen = 0;
	for (lp = n->npipe.cmdlist ; lp ; lp = lp->next)
		pipelen++;
	INTOFF;
	jp = makejob(psh, n, pipelen);
	prevfd = -1;
	for (lp = n->npipe.cmdlist ; lp ; lp = lp->next) {
		prehash(psh, lp->n);
		pip[1] = -1;
		if (lp->next) {
			if (sh_pipe(psh, pip) < 0) {
				shfile_close(&psh->fdtab, prevfd);
				error(psh, "Pipe call failed");
			}
		}
#ifdef KASH_USE_FORKSHELL2
		{
			struct evalpipechild args;
			args.prevfd = prevfd;
			args.pip[0] = pip[0];
			args.pip[1] = pip[1];
			forkshell2(psh, jp, lp->n, n->npipe.backgnd ? FORK_BG : FORK_FG,
			           evalpipe_child, lp->n, &args, sizeof(args), NULL);
		}
#else
		if (forkshell(psh, jp, lp->n, n->npipe.backgnd ? FORK_BG : FORK_FG) == 0) {
			INTON;
			if (prevfd > 0) {
				movefd(psh, prevfd, 0);
			}
			if (pip[1] >= 0) {
				shfile_close(&psh->fdtab, pip[0]);
				if (pip[1] != 1) {
					movefd(psh, pip[1], 1);
				}
			}
			evaltree(psh, lp->n, EV_EXIT);
		}
#endif
		if (prevfd >= 0)
			shfile_close(&psh->fdtab, prevfd);
		prevfd = pip[0];
		shfile_close(&psh->fdtab, pip[1]);
	}
	if (n->npipe.backgnd == 0) {
		psh->exitstatus = waitforjob(psh, jp);
		TRACE((psh, "evalpipe:  job done exit status %d\n", psh->exitstatus));
	}
	INTON;
}

#ifdef KASH_USE_FORKSHELL2
/*
 * evalbackcmd child.
 */
struct evalbackcmdchild
{
	int pip[2];
};

static int evalbackcmd_child(shinstance *psh, union node *n, void *argp)
{
	struct evalbackcmdchild args = *(struct evalbackcmdchild *)argp;

	FORCEINTON;
	shfile_close(&psh->fdtab, args.pip[0]);
	if (args.pip[1] != 1) {
		movefd(psh, args.pip[1], 1);
	}
	eflag(psh) = 0;
	evaltree(psh, n, EV_EXIT);
	/* NOTREACHED */ /** @todo make it return here to simplify thread handling (no need for setjmp). */
	return 0;
}
#endif /* KASH_USE_FORKSHELL2 */

/*
 * Execute a command inside back quotes.  If it's a builtin command, we
 * want to save its output in a block obtained from malloc.  Otherwise
 * we fork off a subprocess and get the output of the command via a pipe.
 * Should be called with interrupts off.
 */

void
evalbackcmd(shinstance *psh, union node *n, struct backcmd *result)
{
	int pip[2];
	struct job *jp;
	struct stackmark smark;		/* unnecessary */

	setstackmark(psh, &smark);
	result->fd = -1;
	result->buf = NULL;
	result->nleft = 0;
	result->jp = NULL;
	if (n == NULL) {
		goto out;
	}
#ifdef notyet
	/*
	 * For now we disable executing builtins in the same
	 * context as the shell, because we are not keeping
	 * enough state to recover from changes that are
	 * supposed only to affect subshells. eg. echo "`cd /`"
	 */
	if (n->type == NCMD) {
		psh->exitstatus = opsh->exitstatus;
		evalcommand(psh, n, EV_BACKCMD, result);
	} else
#endif
	{
		INTOFF;
		if (sh_pipe(psh, pip) < 0)
			error(psh, "Pipe call failed");
		jp = makejob(psh, n, 1);
#ifdef KASH_USE_FORKSHELL2
		{
			struct evalbackcmdchild args;
			args.pip[0] = pip[0];
			args.pip[1] = pip[1];
			forkshell2(psh, jp, n, FORK_NOJOB,
			           evalbackcmd_child, n, &args, sizeof(args), NULL);
		}
#else
		if (forkshell(psh, jp, n, FORK_NOJOB) == 0) {
			FORCEINTON;
			shfile_close(&psh->fdtab, pip[0]);
			if (pip[1] != 1) {
				movefd(psh, pip[1], 1);
			}
			eflag(psh) = 0;
			evaltree(psh, n, EV_EXIT);
			/* NOTREACHED */
		}
#endif
		shfile_close(&psh->fdtab, pip[1]);
		result->fd = pip[0];
		result->jp = jp;
		INTON;
	}
out:
	popstackmark(psh, &smark);
	TRACE((psh, "evalbackcmd done: fd=%d buf=0x%x nleft=%d jp=0x%x\n",
		result->fd, result->buf, result->nleft, result->jp));
}

static const char *
syspath(shinstance *psh)
{
#ifdef CTL_USER
	static char *sys_path = NULL;
	static int mib[] = {CTL_USER, USER_CS_PATH};
#endif
#ifdef PC_PATH_SEP
	static char def_path[] = "PATH=/usr/bin;/bin;/usr/sbin;/sbin";
#else
	static char def_path[] = "PATH=/usr/bin:/bin:/usr/sbin:/sbin";
#endif
#ifdef CTL_USER
	size_t len;

	if (sys_path == NULL) {
		if (sysctl(mib, 2, 0, &len, 0, 0) != -1 &&
		    (sys_path = ckmalloc(psh, len + 5)) != NULL &&
		    sysctl(mib, 2, sys_path + 5, &len, 0, 0) != -1) {
			memcpy(sys_path, "PATH=", 5);
		} else {
			ckfree(psh, sys_path);
			/* something to keep things happy */
			sys_path = def_path;
		}
	}
	return sys_path;
#else
	return def_path;
#endif
}

static int
parse_command_args(shinstance *psh, int argc, char **argv, int *use_syspath)
{
	int sv_argc = argc;
	char *cp, c;

	*use_syspath = 0;

	for (;;) {
		argv++;
		if (--argc == 0)
			break;
		cp = *argv;
		if (*cp++ != '-')
			break;
		if (*cp == '-' && cp[1] == 0) {
			argv++;
			argc--;
			break;
		}
		while ((c = *cp++)) {
			switch (c) {
			case 'p':
				*use_syspath = 1;
				break;
			default:
				/* run 'typecmd' for other options */
				return 0;
			}
		}
	}
	return sv_argc - argc;
}


/*
 * The split up evalcommand code:
 *      evalcommand_out, evalcommand_parent, evalcommand_doit, evalcommand_child
 */
/*int vforked = 0; - obsolete */

/* Both child and parent exits thru here. */
STATIC void
evalcommand_out(shinstance *psh, int flags, char *lastarg, unsigned expfnamedepth, struct stackmark *smarkp)
{
	if (lastarg)
		/* dsl: I think this is intended to be used to support
		 * '_' in 'vi' command mode during line editing...
		 * However I implemented that within libedit itself.
		 */
		setvar(psh, "_", lastarg, 0);
	expredircleanup(psh, expfnamedepth);
	popstackmark(psh, smarkp);

	if (eflag(psh) && psh->exitstatus && !(flags & EV_TESTED))
		exitshell(psh, psh->exitstatus);
}


/* Called if we forkshell(). */
STATIC void
evalcommand_parent(shinstance *psh, int flags, char *lastarg, unsigned expfnamedepth, struct stackmark *smarkp,
		   int mode, struct job *jp, int pip[2], struct backcmd *backcmd)
{
	if (mode == FORK_FG) {	/* argument to fork */
		psh->exitstatus = waitforjob(psh, jp);
	} else if (mode == FORK_NOJOB) {
		backcmd->fd = pip[0];
		shfile_close(&psh->fdtab, pip[1]);
		backcmd->jp = jp;
	}
	FORCEINTON;

	evalcommand_out(psh, flags, lastarg, expfnamedepth, smarkp);
}

struct evalcommanddoit
{
	struct stackmark smark;
	unsigned expfnamedepth;

	struct backcmd *backcmd;
	int flags;
	int argc;
	char **argv;
	char *lastarg;
	struct arglist varlist;
	const char *path;
	struct cmdentry cmdentry;

	/* for child stuff only: */
	int pip[2];
};

STATIC void
evalcommand_doit(shinstance *psh, union node *cmd, struct evalcommanddoit *args)
{
	struct jmploc jmploc;
	struct jmploc *volatile savehandler;
	struct localvar *volatile savelocalvars;

	/* This is the child process if a fork occurred. */
	/* Execute the command. */
	switch (args->cmdentry.cmdtype) {
		case CMDFUNCTION: {
			volatile struct shparam saveparam;
#ifdef DEBUG
			trputs(psh, "Shell function:  ");  trargs(psh, args->argv);
#endif
			redirect(psh, cmd->ncmd.redirect, REDIR_PUSH);
			saveparam = psh->shellparam;
			psh->shellparam.malloc = 0;
			psh->shellparam.reset = 1;
			psh->shellparam.nparam = args->argc - 1;
			psh->shellparam.p = args->argv + 1;
			psh->shellparam.optnext = NULL;
			INTOFF;
			savelocalvars = psh->localvars;
			psh->localvars = NULL;
			INTON;
			if (setjmp(jmploc.loc)) {
				if (psh->exception == EXSHELLPROC) {
					freeparam(psh, (volatile struct shparam *)
						&saveparam);
				} else {
					freeparam(psh, &psh->shellparam);
					psh->shellparam = saveparam;
				}
				poplocalvars(psh);
				psh->localvars = savelocalvars;
				psh->handler = savehandler;
				longjmp(psh->handler->loc, 1);
			}
			savehandler = psh->handler;
			psh->handler = &jmploc;
			listmklocal(psh, args->varlist.list, 0);
			/* stop shell blowing its stack */
			if (++psh->funcnest > 1000)
				error(psh, "too many nested function calls");
			evaltree(psh, args->cmdentry.u.func, args->flags & EV_TESTED);
			psh->funcnest--;
			INTOFF;
			poplocalvars(psh);
			psh->localvars = savelocalvars;
			freeparam(psh, &psh->shellparam);
			psh->shellparam = saveparam;
			psh->handler = savehandler;
			popredir(psh);
			INTON;
			if (psh->evalskip == SKIPFUNC) {
				psh->evalskip = 0;
				psh->skipcount = 0;
			}
			if (args->flags & EV_EXIT)
				exitshell(psh, psh->exitstatus);
			break;
		}

		case CMDBUILTIN:
		case CMDSPLBLTIN: {
			volatile int temp_path = 0;
			char *volatile savecmdname;
			int volatile savecmdnamemalloc;
			volatile int e;
			int mode;
#ifdef DEBUG
			trputs(psh, "builtin command:  ");  trargs(psh, args->argv);
#endif
			mode = (args->cmdentry.u.bltin == execcmd) ? 0 : REDIR_PUSH;
			if (args->flags == EV_BACKCMD) {
				psh->memout.nleft = 0;
				psh->memout.nextc = psh->memout.buf;
				psh->memout.bufsize = 64;
				mode |= REDIR_BACKQ;
			}
			e = -1;
			savehandler = psh->handler;
			savecmdname = psh->commandname;
			savecmdnamemalloc = psh->commandnamemalloc;
			psh->handler = &jmploc;
			if (!setjmp(jmploc.loc)) {
				/* We need to ensure the command hash table isn't
				 * corruped by temporary PATH assignments.
				 * However we must ensure the 'local' command works!
				 */
				if (args->path != pathval(psh) && (args->cmdentry.u.bltin == hashcmd ||
					args->cmdentry.u.bltin == typecmd)) {
					savelocalvars = psh->localvars;
					psh->localvars = 0;
					mklocal(psh, args->path - 5 /* PATH= */, 0);
					temp_path = 1;
				} else
					temp_path = 0;
				redirect(psh, cmd->ncmd.redirect, mode);

				/* exec is a special builtin, but needs this list... */
				psh->cmdenviron = args->varlist.list;
				/* we must check 'readonly' flag for all builtins */
				listsetvar(psh, args->varlist.list,
					args->cmdentry.cmdtype == CMDSPLBLTIN ? 0 : VNOSET);
				psh->commandnamemalloc = 0;
				psh->commandname = args->argv[0];
				/* initialize nextopt */
				psh->argptr = args->argv + 1;
				psh->optptr = NULL;
				/* and getopt */
#if 0 /** @todo fix getop usage! */
#if defined(__FreeBSD__) || defined(__EMX__) || defined(__APPLE__)
				optreset = 1;
				optind = 1;
#else
				optind = 0; /* init */
#endif
#endif

				psh->exitstatus = args->cmdentry.u.bltin(psh, args->argc, args->argv);
			} else {
				e = psh->exception;
				psh->exitstatus = e == EXINT ? SIGINT + 128 :
						e == EXEXEC ? psh->exerrno : 2;
			}
			psh->handler = savehandler;
			output_flushall(psh);
			psh->out1 = &psh->output;
			psh->out2 = &psh->errout;
			freestdout(psh);
			if (temp_path) {
				poplocalvars(psh);
				psh->localvars = savelocalvars;
			}
			psh->cmdenviron = NULL;
			if (e != EXSHELLPROC) {
				psh->commandname = savecmdname;
				psh->commandnamemalloc = savecmdnamemalloc;
				if (args->flags & EV_EXIT)
					exitshell(psh, psh->exitstatus);
			}
			else if (savecmdnamemalloc)
				sh_free(psh, savecmdname);
			if (e != -1) {
				if ((e != EXERROR && e != EXEXEC)
					|| args->cmdentry.cmdtype == CMDSPLBLTIN)
					exraise(psh, e);
				FORCEINTON;
			}
			if (args->cmdentry.u.bltin != execcmd)
				popredir(psh);
			if (args->flags == EV_BACKCMD) {
				args->backcmd->buf = psh->memout.buf;
				args->backcmd->nleft = (int)(psh->memout.nextc - psh->memout.buf);
				psh->memout.buf = NULL;
			}
			break;
		}

		default: {
			struct strlist *sp;
			char **envp;
#ifdef DEBUG
			trputs(psh, "normal command:  ");  trargs(psh, args->argv);
#endif
			clearredir(psh);
			redirect(psh, cmd->ncmd.redirect, 0);
			for (sp = args->varlist.list ; sp ; sp = sp->next)
                                setvareq(psh, sp->text, VEXPORT|VSTACK);
			envp = environment(psh);
			shellexec(psh, args->argv, envp, args->path,
			          args->cmdentry.u.n.index, args->cmdentry.u.n.suffix);
			break;
		}
	}

	evalcommand_out(psh, args->flags, args->lastarg, args->expfnamedepth, &args->smark);
}

/* child callback. */
static int evalcommand_child(shinstance *psh, union node *cmd, void *argp)
{
	struct evalcommanddoit *args = (struct evalcommanddoit *)argp;

	if (args->flags & EV_BACKCMD) {
		FORCEINTON;
		shfile_close(&psh->fdtab, args->pip[0]);
		if (args->pip[1] != 1) {
			movefd(psh, args->pip[1], 1);
		}
	}
	args->flags |= EV_EXIT;

	evalcommand_doit(psh, cmd, args);
	/* not reached */  /** @todo make it return here */
	return 0;
}

/* Copies data in the argument structure from parent to child. */
static void evalcommand_setup_child(shinstance *pshchild, shinstance *pshparent, void *argp)
{
	struct evalcommanddoit *args = (struct evalcommanddoit *)argp;
	char **argv;
	char **srcargv;
	struct strlist *sp;
	int argc, i;

	setstackmark(pshchild, &args->smark);

	/* copy arguments. */
	srcargv = args->argv;
	argc = args->argc;
	args->argv = argv = stalloc(pshchild, sizeof(char *) * (argc + 1));
	for (i = 0; i < argc; i++)
		argv[i] = stsavestr(pshchild, srcargv[i]);
	argv[argc] = NULL;
	if (args->lastarg)
		args->lastarg = argv[argc - 1];

	/* copy variable list, checking for the 'path'. */
	sp = args->varlist.list;
	args->varlist.list = NULL;
	args->varlist.lastp = &args->varlist.list;
	for (; sp; sp = sp->next) {
		struct strlist *snew = (struct strlist *)stalloc(pshchild, sizeof(*snew));
		char *text;
		snew->next = NULL;
		snew->text = text = stsavestr(pshchild, sp->text);

		if (&text[5] == args->path)
			args->path = &text[sizeof("PATH=") - 1];

		*args->varlist.lastp = snew;
		args->varlist.lastp = &snew->next;
	}

	if (args->path == pathval(pshparent))
		args->path = pathval(pshchild);

	/* back tick command should be ignored in this codepath
	   (flags != EV_BACKCMD as EV_EXIT is ORed in). */

	/* If cmdentry references an internal function, we must duplicates (reference) it's nodes. */
	if (args->cmdentry.cmdtype == CMDFUNCTION)
		args->cmdentry.u.func = copyparsetree(pshchild, args->cmdentry.u.func); /** @todo isn't this duplicated already? */
}

/*
 * Execute a simple command.
 */

STATIC void
evalcommand(shinstance *psh, union node *cmd, int flags, struct backcmd *backcmd)
{
	struct evalcommanddoit args;
	char **argv;
	int argc;

	union node *argp;
	int numvars;
	struct arglist arglist;
	struct strlist *sp;
	const char *path = pathval(psh);

	/* First expand the arguments. */
	TRACE((psh, "evalcommand(0x%lx, %d) called\n", (long)cmd, flags));
	setstackmark(psh, &args.smark);
	psh->back_exitstatus = 0;

	arglist.lastp = &arglist.list;
	/* Expand arguments, ignoring the initial 'name=value' ones */
	for (argp = cmd->ncmd.args, numvars = 0 ; argp ; argp = argp->narg.next, numvars++) {
	    char *p = argp->narg.text;
	    char ch = *p;
	    if (is_name(ch)) {
		    do	ch = *++p;
		    while (is_in_name(ch));
		    if (ch == '=')
			    continue;
	    }
	    break;
	}
	for (/*continue on argp from above. */ ; argp ; argp = argp->narg.next)
		expandarg(psh, argp, &arglist, EXP_FULL | EXP_TILDE);
	*arglist.lastp = NULL;

	args.expfnamedepth = expredir(psh, cmd->ncmd.redirect);

	/* Now do the initial 'name=value' ones we skipped above */
	args.varlist.lastp = &args.varlist.list;
	for (argp = cmd->ncmd.args ; numvars > 0 && argp ; argp = argp->narg.next, numvars--)
		expandarg(psh, argp, &args.varlist, EXP_VARTILDE);
	*args.varlist.lastp = NULL;

	argc = 0;
	for (sp = arglist.list ; sp ; sp = sp->next)
		argc++;
	args.argc = argc;
	args.argv = argv = stalloc(psh, sizeof (char *) * (argc + 1));

	for (sp = arglist.list ; sp ; sp = sp->next) {
		TRACE((psh, "evalcommand arg: %s\n", sp->text));
		*argv++ = sp->text;
	}
	*argv = NULL;
	args.lastarg = NULL;
	if (iflag(psh) && psh->funcnest == 0 && argc > 0)
		args.lastarg = argv[-1];
	argv -= argc;

	/* Print the command if xflag is set. */
	if (xflag(psh)) {
		char sep = 0;
		out2str(psh, ps4val(psh));
		for (sp = args.varlist.list ; sp ; sp = sp->next) {
			if (sep != 0)
				outc(sep, &psh->errout);
			out2str(psh, sp->text);
			sep = ' ';
		}
		for (sp = arglist.list ; sp ; sp = sp->next) {
			if (sep != 0)
				outc(sep, &psh->errout);
			out2str(psh, sp->text);
			sep = ' ';
		}
		outc('\n', &psh->errout);
		flushout(&psh->errout);
	}

	/* Now locate the command. */
	if (argc == 0) {
		args.cmdentry.cmdtype = CMDSPLBLTIN;
		args.cmdentry.u.bltin = bltincmd;
	} else {
		static const char PATH[] = "PATH=";
		int cmd_flags = DO_ERR;

		/*
		 * Modify the command lookup path, if a PATH= assignment
		 * is present
		 */
		for (sp = args.varlist.list; sp; sp = sp->next)
			if (strncmp(sp->text, PATH, sizeof(PATH) - 1) == 0)
				path = sp->text + sizeof(PATH) - 1;

		do {
			int argsused, use_syspath;
			find_command(psh, argv[0], &args.cmdentry, cmd_flags, path);
			if (args.cmdentry.cmdtype == CMDUNKNOWN) {
				psh->exitstatus = 127;
				flushout(&psh->errout);
				evalcommand_out(psh, flags, args.lastarg, args.expfnamedepth, &args.smark);
				return;
			}

			/* implement the 'command' builtin here */
			if (args.cmdentry.cmdtype != CMDBUILTIN ||
			    args.cmdentry.u.bltin != bltincmd)
				break;
			cmd_flags |= DO_NOFUNC;
			argsused = parse_command_args(psh, argc, argv, &use_syspath);
			if (argsused == 0) {
				/* use 'type' builting to display info */
				args.cmdentry.u.bltin = typecmd;
				break;
			}
			argc -= argsused;
			argv += argsused;
			if (use_syspath)
				path = syspath(psh) + 5;
		} while (argc != 0);
		if (args.cmdentry.cmdtype == CMDSPLBLTIN && cmd_flags & DO_NOFUNC)
			/* posix mandates that 'command <splbltin>' act as if
			   <splbltin> was a normal builtin */
			args.cmdentry.cmdtype = CMDBUILTIN;
	}

	/* Fork off a child process if necessary. */
	if (cmd->ncmd.backgnd
	 || (args.cmdentry.cmdtype == CMDNORMAL && (flags & EV_EXIT) == 0)
	 || (  (flags & EV_BACKCMD) != 0
	    && (  (args.cmdentry.cmdtype != CMDBUILTIN && args.cmdentry.cmdtype != CMDSPLBLTIN)
		   || args.cmdentry.u.bltin == dotcmd
		   || args.cmdentry.u.bltin == evalcmd))) {
		struct job *jp;
		int mode;

		INTOFF;
		jp = makejob(psh, cmd, 1);
		mode = cmd->ncmd.backgnd;
		args.pip[0] = -1;
		args.pip[1] = -1;
		if (flags & EV_BACKCMD) {
			mode = FORK_NOJOB;
			if (sh_pipe(psh, args.pip) < 0)
				error(psh, "Pipe call failed");
		}

		args.backcmd = backcmd;
		args.flags = flags;
		args.path = path;
#ifdef KASH_USE_FORKSHELL2
		forkshell2(psh, jp, cmd, mode, evalcommand_child, cmd,
		           &args, sizeof(args), evalcommand_setup_child);
		evalcommand_parent(psh, flags, args.lastarg, args.expfnamedepth,
				   &args.smark, mode, jp, args.pip, backcmd);
#else
		if (forkshell(psh, jp, cmd, mode) != 0) {
			evalcommand_parent(psh, flags, args.lastarg, args.expfnamedepth,
			                   &args.smark, mode, jp, args.pip, backcmd);
			return;	/* at end of routine */
		}
		evalcommand_child(psh, cmd, &args);
#endif
	} else {
		args.backcmd = backcmd;
		args.flags = flags;
		args.path = path;
		evalcommand_doit(psh, cmd, &args);
	}
}


/*
 * Search for a command.  This is called before we fork so that the
 * location of the command will be available in the parent as well as
 * the child.  The check for "goodname" is an overly conservative
 * check that the name will not be subject to expansion.
 */

STATIC void
prehash(shinstance *psh, union node *n)
{
	struct cmdentry entry;

	if (n->type == NCMD && n->ncmd.args)
		if (goodname(n->ncmd.args->narg.text))
			find_command(psh, n->ncmd.args->narg.text, &entry, 0,
				         pathval(psh));
}



/*
 * Builtin commands.  Builtin commands whose functions are closely
 * tied to evaluation are implemented here.
 */

/*
 * No command given.
 */

int
bltincmd(shinstance *psh, int argc, char **argv)
{
	/*
	 * Preserve psh->exitstatus of a previous possible redirection
	 * as POSIX mandates
	 */
	return psh->back_exitstatus;
}


/*
 * Handle break and continue commands.  Break, continue, and return are
 * all handled by setting the psh->evalskip flag.  The evaluation routines
 * above all check this flag, and if it is set they start skipping
 * commands rather than executing them.  The variable skipcount is
 * the number of loops to break/continue, or the number of function
 * levels to return.  (The latter is always 1.)  It should probably
 * be an error to break out of more loops than exist, but it isn't
 * in the standard shell so we don't make it one here.
 */

int
breakcmd(shinstance *psh, int argc, char **argv)
{
	int n = argc > 1 ? number(psh, argv[1]) : 1;

	if (n > psh->loopnest)
		n = psh->loopnest;
	if (n > 0) {
		psh->evalskip = (**argv == 'c')? SKIPCONT : SKIPBREAK;
		psh->skipcount = n;
	}
	return 0;
}


/*
 * The return command.
 */

int
returncmd(shinstance *psh, int argc, char **argv)
{
#if 0
	int ret = argc > 1 ? number(psh, argv[1]) : psh->exitstatus;
#else
	int ret;
	if (argc > 1)  {
		/* make return -1 and VSC lite work ... */
    		if (argv[1][0] != '-' || !is_number(&argv[1][1]))
			ret = number(psh, argv[1]);
		else
			ret = -number(psh, &argv[1][1]) & 255; /* take the bash approach */
	} else {
    		ret = psh->exitstatus;
	}
#endif

	if (psh->funcnest) {
		psh->evalskip = SKIPFUNC;
		psh->skipcount = 1;
		return ret;
	}
	else {
		/* Do what ksh does; skip the rest of the file */
		psh->evalskip = SKIPFILE;
		psh->skipcount = 1;
		return ret;
	}
}


int
falsecmd(shinstance *psh, int argc, char **argv)
{
	return 1;
}


int
truecmd(shinstance *psh, int argc, char **argv)
{
	return 0;
}


int
execcmd(shinstance *psh, int argc, char **argv)
{
	if (argc > 1) {
		struct strlist *sp;

		iflag(psh) = 0;		/* exit on error */
		mflag(psh) = 0;
		optschanged(psh);
		for (sp = psh->cmdenviron; sp; sp = sp->next)
			setvareq(psh, sp->text, VEXPORT|VSTACK);
		shellexec(psh, argv + 1, environment(psh), pathval(psh), 0, -1);
	}
	return 0;
}

static int
conv_time(clock_t ticks, char *seconds, size_t l)
{
	static clock_t tpm = 0;
	clock_t mins;
	size_t i;

	if (!tpm)
		tpm = /*sysconf(_SC_CLK_TCK)*/sh_sysconf_clk_tck() * 60;

	mins = ticks / tpm;
#ifdef _MSC_VER
	{
		char tmp[64];
		sprintf(tmp, "%.4f", (ticks - mins * tpm) * 60.0 / tpm);
		strlcpy(seconds, tmp, l);
	}
#else
	snprintf(seconds, l, "%.4f", (ticks - mins * tpm) * 60.0 / tpm );
#endif

	if (seconds[0] == '6' && seconds[1] == '0') {
		/* 59.99995 got rounded up... */
		mins++;
		strlcpy(seconds, "0.0", l);
		return mins;
	}

	/* suppress trailing zeros */
	i = strlen(seconds) - 1;
	for (; seconds[i] == '0' && seconds[i - 1] != '.'; i--)
		seconds[i] = 0;
	return mins;
}

int
timescmd(shinstance *psh, int argc, char **argv)
{
	shtms tms;
	int u, s, cu, cs;
	char us[8], ss[8], cus[8], css[8];

	nextopt(psh, "");

	sh_times(psh, &tms);

	u = conv_time(tms.tms_utime, us, sizeof(us));
	s = conv_time(tms.tms_stime, ss, sizeof(ss));
	cu = conv_time(tms.tms_cutime, cus, sizeof(cus));
	cs = conv_time(tms.tms_cstime, css, sizeof(css));

	outfmt(psh->out1, "%dm%ss %dm%ss\n%dm%ss %dm%ss\n",
		u, us, s, ss, cu, cus, cs, css);

	return 0;
}
