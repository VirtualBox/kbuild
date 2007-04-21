/* $Id: $ */
/** @file
 *
 * The shell instance and it's methods.
 *
 * Copyright (c) 2007 knut st. osmundsen <bird-src-spam@anduin.net>
 *
 *
 * This file is part of kBuild.
 *
 * kBuild is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * kBuild is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with kBuild; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include "shtypes.h"
#include "shthread.h"
#include "shfile.h"

/**
 * A shell instance.
 *
 * This is the core structure of the shell, it contains all
 * the data associated with a shell process except that it's
 * running in a thread and not a separate process.
 */
typedef struct shinstance
{
    struct shinstance  *next;           /**< The next shell instance. */
    struct shinstance  *prev;           /**< The previous shell instance. */
    struct shinstance  *parent;         /**< The parent shell instance. */
    pid_t               pid;            /**< The (fake) process id of this shell instance. */
    shtid               tid;            /**< The thread identifier of the thread for this shell. */
    shfdtab             fdtab;          /**< The file descriptor table. */

    /* error.h */
    struct jmploc      *handler;
    int                 exception;
    int                 exerrno;
    int volatile        suppressint;
    int volatile        intpending;

    /* main.h */
    int                 rootpid;        /**< pid of main shell. */
    int                 rootshell;      /**< true if we aren't a child of the main shell. */
    struct shinstance  *psh_rootshell;  /**< The root shell pointer. (!rootshell) */

    /* trap.h */
    int                 pendingsigs;

    /* parse.h */
    int                 tokpushback;
    int                 whichprompt;    /**< 1 == PS1, 2 == PS2 */

    /* output.h */
    struct output       output;
    struct output       errout;
    struct output       memout;
    struct output      *out1;
    struct output      *out2;

    /* options.h */
    struct optent       optlist[NOPTS];
    char               *minusc;         /**< argument to -c option */
    char               *arg0;           /**< $0 */
    struct shparam      shellparam;     /**< $@ */
    char              **argptr;         /**< argument list for builtin commands */
    char               *optionarg;      /**< set by nextopt */
    char               *optptr;         /**< used by nextopt */

    /* var.h */
    struct localvar    *localvars;
#if ATTY
    struct var          vatty;
#endif
    struct var          vifs;
    struct var          vmail;
    struct var          vmpath;
    struct var          vpath;
#ifdef _MSC_VER
    struct var          vpath2;
#endif 
    struct var          vps1;
    struct var          vps2;
    struct var          vps4;
#ifndef SMALL
    struct var          vterm;
    struct var          vtermcap;
    struct var          vhistsize;
#endif

    /* myhistedit.h */
    int                 displayhist;
#ifndef SMALL
    History            *hist;
    EditLine           *el;
#endif

    /* memalloc.h */
    char               *stacknxt;
    int                 stacknleft;
    int                 sstrnleft;
    int                 herefd;

    /* jobs.h */
    pid_t               backgndpid;     /**< pid of last background process */
    int                 job_warning;    /**< user was warned about stopped jobs */

    /* input.h */
    int                 plinno;
    int                 parsenleft;     /**< number of characters left in input buffer */
    char               *parsenextc;     /**< next character in input buffer */
    int                 init_editline;  /**< 0 == not setup, 1 == OK, -1 == failed */

    /* exec.h */
    const char         *pathopt;        /**< set by padvance */

    /* eval.h */
    char               *commandname;    /**< currently executing command */
    int                 exitstatus;     /**< exit status of last command */
    int                 back_exitstatus;/**< exit status of backquoted command */
    struct strlist     *cmdenviron;     /**< environment for builtin command */
    int                 funcnest;
    int                 evalskip;

    /* builtins.h */

    /* alias.c */
#define ATABSIZE 39
    struct alias       *atab[ATABSIZE];

} shinstance;


extern shinstance *create_root_shell(shinstance *inherit, int argc, char **argv);

