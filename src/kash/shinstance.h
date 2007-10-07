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

#ifndef ___shinstance_h___
#define ___shinstance_h___

#include <stdio.h> /* BUFSIZ */
#include <signal.h> /* NSIG */

#include "shtypes.h"
#include "shthread.h"
#include "shfile.h"
#include "output.h"
#include "options.h"

#include "expand.h"
#include "exec.h"
#include "var.h"

/* memalloc.c */
#define MINSIZE 504		/* minimum size of a block */
struct stack_block {
	struct stack_block *prev;
	char space[MINSIZE];
};

/* input.c */
struct strpush {
	struct strpush *prev;	/* preceding string on stack */
	char *prevstring;
	int prevnleft;
	int prevlleft;
	struct alias *ap;	/* if push was associated with an alias */
};

/*
 * The parsefile structure pointed to by the global variable parsefile
 * contains information about the current file being read.
 */
struct parsefile {
	struct parsefile *prev;	/* preceding file on stack */
	int linno;		/* current line */
	int fd;			/* file descriptor (or -1 if string) */
	int nleft;		/* number of chars left in this line */
	int lleft;		/* number of chars left in this buffer */
	char *nextc;		/* next char in buffer */
	char *buf;		/* input buffer */
	struct strpush *strpush; /* for pushing strings at this level */
	struct strpush basestrpush; /* so pushing one is fast */
};

/* exec.c */
#define CMDTABLESIZE 31		/* should be prime */
#define ARB 1			/* actual size determined at run time */

struct tblentry {
	struct tblentry *next;	/* next entry in hash chain */
	union param param;	/* definition of builtin function */
	short cmdtype;		/* index identifying command */
	char rehash;		/* if set, cd done since entry created */
	char cmdname[ARB];	/* name of command */
};

/* expand.c */
/*
 * Structure specifying which parts of the string should be searched
 * for IFS characters.
 */
struct ifsregion {
	struct ifsregion *next;	/* next region in list */
	int begoff;		/* offset of start of region */
	int endoff;		/* offset of end of region */
	int inquotes;		/* search for nul bytes only */
};


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
    int                 exerrno/* = 0 */; /**< Last exec error */
    int volatile        suppressint;
    int volatile        intpending;

    /* main.h */
    int                 rootpid;        /**< pid of main shell. */
    int                 rootshell;      /**< true if we aren't a child of the main shell. */
    struct shinstance  *psh_rootshell;  /**< The root shell pointer. (!rootshell) */

    /* trap.h */
    int                 pendingsigs;    /**< indicates some signal received */

    /* trap.c */
    char                gotsig[NSIG];   /**< indicates specified signal received */
    char               *trap[NSIG+1];   /**< trap handler commands */
    char                sigmode[NSIG];  /**< current value of signal */

    /* parse.h */
    int                 tokpushback;
    int                 whichprompt;    /**< 1 == PS1, 2 == PS2 */

    /* output.h */
    struct output       output;
    struct output       errout;
    struct output       memout;
    struct output      *out1;
    struct output      *out2;
    /* output.c */
#define OUTBUFSIZ BUFSIZ
#define MEM_OUT -3                      /**< output to dynamically allocated memory */

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
    struct var          vhistsize;
#endif
    struct var          voptind;
#ifdef PC_OS2_LIBPATHS
    struct var          libpath_vars[4];
#endif
#ifdef SMALL
# define VTABSIZE 39
#else
# define VTABSIZE 517
#endif
    struct var         *vartab[VTABSIZE];

    /* myhistedit.h */
    int                 displayhist;
#ifndef SMALL
    History            *hist;
    EditLine           *el;
#endif

    /* memalloc.h */
    char               *stacknxt/* = stackbase.space*/;
    int                 stacknleft/* = MINSIZE*/;
    int                 sstrnleft;
    int                 herefd/* = -1 */;

    /* memalloc.c */
    struct stack_block  stackbase;
    struct stack_block *stackp/* = &stackbase*/;
    struct stackmark   *markp;

    /* jobs.h */
    pid_t               backgndpid/* = -1 */;   /**< pid of last background process */
    int                 job_warning;    /**< user was warned about stopped jobs */

    /* jobs.c */
    struct job         *jobtab;         /**< array of jobs */
    int                 njobs;          /**< size of array */
    int                 jobs_invalid;   /**< set in child */
#if JOBS
    int                 initialpgrp;    /**< pgrp of shell on invocation */
    int                 curjob/* = -1*/;/**< current job */
#endif
    int                 ttyfd/* = -1*/;
    int                 jobctl;         /**< job control enabled / disabled */
    char               *cmdnextc;
    int                 cmdnleft;

    /* input.h */
    int                 plinno/* = 1 */;/**< input line number */
    int                 parsenleft;     /**< number of characters left in input buffer */
    char               *parsenextc;     /**< next character in input buffer */
    int                 init_editline/* = 0 */;     /**< 0 == not setup, 1 == OK, -1 == failed */

    /* input.c */
    int                 parselleft;     /**< copy of parsefile->lleft */
    struct parsefile    basepf;         /**< top level input file */
    char                basebuf[BUFSIZ];/**< buffer for top level input file */
    struct parsefile   *parsefile/* = &basepf*/;    /**< current input file */
#ifndef SMALL
    EditLine           *el;             /**< cookie for editline package */
#endif


    /* exec.h */
    const char         *pathopt;        /**< set by padvance */

    /* exec.c */
    struct tblentry    *cmdtable[CMDTABLESIZE];
    int                 builtinloc/* = -1*/;    /**< index in path of %builtin, or -1 */


    /* eval.h */
    char               *commandname;    /**< currently executing command */
    int                 exitstatus;     /**< exit status of last command */
    int                 back_exitstatus;/**< exit status of backquoted command */
    struct strlist     *cmdenviron;     /**< environment for builtin command */
    int                 funcnest;       /**< depth of function calls */
    int                 evalskip;       /**< set if we are skipping commands */
    int                 skipcount;      /**< number of levels to skip */
    int                 loopnest;       /**< current loop nesting level */

    /* builtins.h */

    /* alias.c */
#define ATABSIZE 39
    struct alias       *atab[ATABSIZE];

    /* cd.c */
    char               *curdir;         /**< current working directory */
    char               *prevdir;        /**< previous working directory */
    char               *cdcomppath;
    int                 getpwd_first;   /**< static in getpwd. (initialized to 1!) */

    /* error.c */
    char                errmsg_buf[16]; /**< static in errmsg. (bss) */

    /* eval.c */
    int                 vforked;

    /* expand.c */
    char               *expdest;        /**< output of current string */
    struct nodelist    *argbackq;       /**< list of back quote expressions */
    struct ifsregion    ifsfirst;       /**< first struct in list of ifs regions */
    struct ifsregion   *ifslastp;       /**< last struct in list */
    struct arglist      exparg;         /**< holds expanded arg list */
    char               *expdir;         /**< Used by expandmeta. */

    /* mail.c */
#define MAXMBOXES 10
    int                 nmboxes;        /**< number of mailboxes */
    time_t              mailtime[MAXMBOXES]; /**< times of mailboxes */

    /* bltin/test.c */
    char              **t_wp;
    struct t_op const  *t_wp_op;

} shinstance;


extern shinstance *sh_create_root_shell(shinstance *, int, char **);
char *sh_getenv(shinstance *, const char *);

/* signals */
typedef void (*sh_sig_t)(shinstance *, int);
#ifdef _MSC_VER
 typedef uint32_t sh_sigset_t;
#else
 typedef sigset_t sh_sigset_t;
#endif
struct sh_sigaction
{
    sh_sig_t    sh_handler;
    sh_sigset_t sh_mask;
    int         sh_flags;
};
#define SH_SIG_DFL ((sh_sig_t)SIG_DFL)
#define SH_SIG_IGN ((sh_sig_t)SIG_IGN)

int sh_sigaction(int, const struct sh_sigaction *, struct sh_sigaction *);
sh_sig_t sh_signal(shinstance *, int, sh_sig_t);
void sh_raise_sigint(shinstance *);
void sh_sigemptyset(sh_sigset_t *);
int sh_sigprocmask(shinstance *, int, sh_sigset_t const *, sh_sigset_t *);
void sh_abort(shinstance *);

/* times */
#include <time.h>
#ifdef _MSC_VER
    typedef struct sh_tms
    {
        clock_t tms_utime;
        clock_t tms_stime;
        clock_t tms_cutime;
        clock_t tms_cstime;
    } sh_tms;
#else
#   include <times.h>
    typedef struct tms sh_tms;
#endif
clock_t sh_times(sh_tms *);
int sh_sysconf_clk_tck(void);

/* wait / process */
#ifdef _MSC_VER
#   include <process.h>
#   define WNOHANG         1       /* Don't hang in wait. */
#   define WUNTRACED       2       /* Tell about stopped, untraced children. */
#   define WCONTINUED      4       /* Report a job control continued process. */
#   define _W_INT(w)       (*(int *)&(w))  /* Convert union wait to int. */
#   define WCOREFLAG       0200
#   define _WSTATUS(x)     (_W_INT(x) & 0177)
#   define _WSTOPPED       0177            /* _WSTATUS if process is stopped */
#   define WIFSTOPPED(x)   (_WSTATUS(x) == _WSTOPPED)
#   define WSTOPSIG(x)     (_W_INT(x) >> 8)
#   define WIFSIGNALED(x)  (_WSTATUS(x) != 0 && !WIFSTOPPED(x) && !WIFCONTINUED(x)) /* bird: made GLIBC tests happy. */
#   define WTERMSIG(x)     (_WSTATUS(x))
#   define WIFEXITED(x)    (_WSTATUS(x) == 0)
#   define WEXITSTATUS(x)  (_W_INT(x) >> 8)
#   define WIFCONTINUED(x) (x == 0x13)     /* 0x13 == SIGCONT */
#   define WCOREDUMP(x)    (_W_INT(x) & WCOREFLAG)
#   define W_EXITCODE(ret, sig)    ((ret) << 8 | (sig))
#   define W_STOPCODE(sig)         ((sig) << 8 | _WSTOPPED)
#else
#   include <sys/wait.h>
#endif
pid_t sh_waitpid(shinstance *, pid_t, int *, int);
void sh__exit(shinstance *, int);
int sh_execve(shinstance *, const char *, const char * const*, const char * const *);
uid_t sh_getuid(shinstance *);
uid_t sh_geteuid(shinstance *);
gid_t sh_getgid(shinstance *);
gid_t sh_getegid(shinstance *);
pid_t sh_getpid(shinstance *);
pid_t sh_getpgrp(shinstance *);
pid_t sh_getpgid(shinstance *, pid_t);
int sh_setpgid(shinstance *, pid_t, pid_t);
int sh_killpg(shinstance *, pid_t, int);

/* tc* */
pid_t sh_tcgetpgrp(shinstance *, int);
int sh_tcsetpgrp(shinstance *, int, pid_t);

#endif
