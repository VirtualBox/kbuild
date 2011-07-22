/* $Id: shinstance.h 2498 2011-07-22 12:05:57Z knut.osmundsen@oracle.com $ */
/** @file
 * The shell instance and it's methods.
 */

/*
 * Copyright (c) 2007-2010 knut st. osmundsen <bird-kBuild-spamx@anduin.net>
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

#ifndef ___shinstance_h
#define ___shinstance_h

#include <stdio.h> /* BUFSIZ */
#include <signal.h> /* NSIG */
#ifndef _MSC_VER
# include <termios.h>
# include <sys/types.h>
# include <sys/ioctl.h>
# include <sys/resource.h>
#endif
#include <errno.h>
#ifdef _MSC_VER
# define EWOULDBLOCK    512
#endif

#include "shtypes.h"
#include "shthread.h"
#include "shfile.h"
#include "shheap.h"
#include "shell.h"
#include "output.h"
#include "options.h"

#include "expand.h"
#include "exec.h"
#include "var.h"
#include "show.h"

#ifdef _MSC_VER
# define strcasecmp stricmp
# define strncasecmp strnicmp
#endif

/**
 * A child process.
 */
typedef struct shchild
{
    pid_t       pid;                    /**< The pid. */
#if K_OS == K_OS_WINDOWS
    void       *hChild;                 /**< The process handle. */
#endif
} shchild;

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
struct shinstance
{
    struct shinstance  *next;           /**< The next shell instance. */
    struct shinstance  *prev;           /**< The previous shell instance. */
    struct shinstance  *parent;         /**< The parent shell instance. */
    pid_t               pid;            /**< The (fake) process id of this shell instance. */
    shtid               tid;            /**< The thread identifier of the thread for this shell. */
    shfdtab             fdtab;          /**< The file descriptor table. */
    shsigaction_t       sigactions[NSIG]; /**< The signal actions registered with this shell instance. */
    shsigset_t          sigmask;        /**< Our signal mask. */
    char              **shenviron;      /**< The environment vector. */
    int                 num_children;   /**< Number of children in the array. */
    shchild            *children;       /**< The child array. */

    /* alias.c */
#define ATABSIZE 39
    struct alias       *atab[ATABSIZE];

    /* cd.c */
    char               *curdir;         /**< current working directory */
    char               *prevdir;        /**< previous working directory */
    char               *cdcomppath;
    int                 getpwd_first;   /**< static in getpwd. (initialized to 1!) */

    /* error.h */
    struct jmploc      *handler;
    int                 exception;
    int                 exerrno/* = 0 */; /**< Last exec error */
    int volatile        suppressint;
    int volatile        intpending;

    /* error.c */
    char                errmsg_buf[16]; /**< static in errmsg. (bss) */

    /* eval.h */
    char               *commandname;    /**< currently executing command */
    int                 exitstatus;     /**< exit status of last command */
    int                 back_exitstatus;/**< exit status of backquoted command */
    struct strlist     *cmdenviron;     /**< environment for builtin command */
    int                 funcnest;       /**< depth of function calls */
    int                 evalskip;       /**< set if we are skipping commands */
    int                 skipcount;      /**< number of levels to skip */
    int                 loopnest;       /**< current loop nesting level */

    /* eval.c */
    int                 vforked;

    /* expand.c */
    char               *expdest;        /**< output of current string */
    struct nodelist    *argbackq;       /**< list of back quote expressions */
    struct ifsregion    ifsfirst;       /**< first struct in list of ifs regions */
    struct ifsregion   *ifslastp;       /**< last struct in list */
    struct arglist      exparg;         /**< holds expanded arg list */
    char               *expdir;         /**< Used by expandmeta. */

    /* exec.h */
    const char         *pathopt;        /**< set by padvance */

    /* exec.c */
    struct tblentry    *cmdtable[CMDTABLESIZE];
    int                 builtinloc/* = -1*/;    /**< index in path of %builtin, or -1 */

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

    /* jobs.h */
    pid_t               backgndpid/* = -1 */;   /**< pid of last background process */
    int                 job_warning;    /**< user was warned about stopped jobs */

    /* jobs.c */
    struct job         *jobtab;         /**< array of jobs */
    int                 njobs;          /**< size of array */
    int                 jobs_invalid;   /**< set in child */
    int                 initialpgrp;    /**< pgrp of shell on invocation */
    int                 curjob/* = -1*/;/**< current job */
    int                 ttyfd/* = -1*/;
    int                 jobctl;         /**< job control enabled / disabled */
    char               *cmdnextc;
    int                 cmdnleft;


    /* mail.c */
#define MAXMBOXES 10
    int                 nmboxes;        /**< number of mailboxes */
    time_t              mailtime[MAXMBOXES]; /**< times of mailboxes */

    /* main.h */
    int                 rootpid;        /**< pid of main shell. */
    int                 rootshell;      /**< true if we aren't a child of the main shell. */
    struct shinstance  *psh_rootshell;  /**< The root shell pointer. (!rootshell) */

    /* memalloc.h */
    char               *stacknxt/* = stackbase.space*/;
    int                 stacknleft/* = MINSIZE*/;
    int                 sstrnleft;
    int                 herefd/* = -1 */;

    /* memalloc.c */
    struct stack_block  stackbase;
    struct stack_block *stackp/* = &stackbase*/;
    struct stackmark   *markp;

    /* myhistedit.h */
    int                 displayhist;
#ifndef SMALL
    History            *hist;
    EditLine           *el;
#endif

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

    /* parse.h */
    int                 tokpushback;
    int                 whichprompt;    /**< 1 == PS1, 2 == PS2 */

    /* parser.c */
    int                 noalias/* = 0*/;/**< when set, don't handle aliases */
    struct heredoc     *heredoclist;    /**< list of here documents to read */
    int                 parsebackquote; /**< nonzero if we are inside backquotes */
    int                 doprompt;       /**< if set, prompt the user */
    int                 needprompt;     /**< true if interactive and at start of line */
    int                 lasttoken;      /**< last token read */
    char               *wordtext;       /**< text of last word returned by readtoken */
    int                 checkkwd;       /**< 1 == check for kwds, 2 == also eat newlines */
    struct nodelist    *backquotelist;
    union node         *redirnode;
    struct heredoc     *heredoc;
    int                 quoteflag;      /**< set if (part of) last token was quoted */
    int                 startlinno;     /**< line # where last token started */

    /* redir.c */
    struct redirtab    *redirlist;
    int                 fd0_redirected/* = 0*/;

    /* show.c */
    char                tracebuf[1024];
    size_t              tracepos;
    int                 tracefd;

    /* trap.h */
    int                 pendingsigs;    /**< indicates some signal received */

    /* trap.c */
    char                gotsig[NSIG];   /**< indicates specified signal received */
    char               *trap[NSIG+1];   /**< trap handler commands */
    char                sigmode[NSIG];  /**< current value of signal */

    /* var.h */
    struct localvar    *localvars;
    struct var          vatty;
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

    /* builtins.h */

    /* bltin/test.c */
    char              **t_wp;
    struct t_op const  *t_wp_op;

};


extern shinstance *sh_create_root_shell(shinstance *, int, char **, char **);

/* environment & pwd.h */
char *sh_getenv(shinstance *, const char *);
char **sh_environ(shinstance *);
const char *sh_gethomedir(shinstance *, const char *);

/* signals */
#define SH_SIG_UNK ((shsig_t)(intptr_t)-199)
#define SH_SIG_DFL ((shsig_t)SIG_DFL)
#define SH_SIG_IGN ((shsig_t)SIG_IGN)
#define SH_SIG_ERR ((shsig_t)SIG_ERR)
#ifdef _MSC_VER
#   define SA_RESTART       0x02
#   define SIG_BLOCK         1
#   define SIG_UNBLOCK       2
#   define SIG_SETMASK       3

#   define SIGHUP            1          /* _SIGHUP_IGNORE */
/*# define SIGINT            2 */
#   define SIGQUIT           3          /* _SIGQUIT_IGNORE */
/*# define SIGILL            4 */
/*# define SIGFPE            8 */
/*# define SIGSEGV          11 */
#   define SIGPIPE          13          /* _SIGPIPE_IGNORE */
/*# define SIGTERM          15 */
#   define SIGTTIN          16          /* _SIGIOINT_IGNORE */
#   define SIGTSTP          17          /* _SIGSTOP_IGNORE */
#   define SIGTTOU          18
#   define SIGCONT          20
/*# define SIGBREAK         21 */
/*# define SIGABRT          22 */

#   define sys_siglist      sys_signame
#endif /* _MSC_VER */
#ifdef __sun__
#   define sys_siglist      _sys_siglist
#endif
#ifndef HAVE_SYS_SIGNAME
extern char sys_signame[NSIG][16];
#endif

int sh_sigaction(shinstance *, int, const struct shsigaction *, struct shsigaction *);
shsig_t sh_signal(shinstance *, int, shsig_t);
int sh_siginterrupt(shinstance *, int, int);
void sh_sigemptyset(shsigset_t *);
void sh_sigfillset(shsigset_t *);
void sh_sigaddset(shsigset_t *, int);
void sh_sigdelset(shsigset_t *, int);
int sh_sigismember(shsigset_t const *, int);
int sh_sigprocmask(shinstance *, int, shsigset_t const *, shsigset_t *);
SH_NORETURN_1 void sh_abort(shinstance *) SH_NORETURN_2;
void sh_raise_sigint(shinstance *);
int sh_kill(shinstance *, pid_t, int);
int sh_killpg(shinstance *, pid_t, int);

/* times */
#include <time.h>
#ifdef _MSC_VER
    typedef struct shtms
    {
        clock_t tms_utime;
        clock_t tms_stime;
        clock_t tms_cutime;
        clock_t tms_cstime;
    } shtms;
#else
#   include <sys/times.h>
    typedef struct tms shtms;
#endif
clock_t sh_times(shinstance *, shtms *);
int sh_sysconf_clk_tck(void);

/* wait / process */
int sh_add_child(shinstance *psh, pid_t pid, void *hChild);
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
pid_t sh_fork(shinstance *);
pid_t sh_waitpid(shinstance *, pid_t, int *, int);
SH_NORETURN_1 void sh__exit(shinstance *, int) SH_NORETURN_2;
int sh_execve(shinstance *, const char *, const char * const*, const char * const *);
uid_t sh_getuid(shinstance *);
uid_t sh_geteuid(shinstance *);
gid_t sh_getgid(shinstance *);
gid_t sh_getegid(shinstance *);
pid_t sh_getpid(shinstance *);
pid_t sh_getpgrp(shinstance *);
pid_t sh_getpgid(shinstance *, pid_t);
int sh_setpgid(shinstance *, pid_t, pid_t);

/* tc* */
pid_t sh_tcgetpgrp(shinstance *, int);
int sh_tcsetpgrp(shinstance *, int, pid_t);

/* sys/resource.h */
#ifdef _MSC_VER
    typedef int64_t shrlim_t;
    typedef struct shrlimit
    {
        shrlim_t   rlim_cur;
        shrlim_t   rlim_max;
    } shrlimit;
#   define RLIMIT_CPU     0
#   define RLIMIT_FSIZE   1
#   define RLIMIT_DATA    2
#   define RLIMIT_STACK   3
#   define RLIMIT_CORE    4
#   define RLIMIT_RSS     5
#   define RLIMIT_MEMLOCK 6
#   define RLIMIT_NPROC   7
#   define RLIMIT_NOFILE  8
#   define RLIMIT_SBSIZE  9
#   define RLIMIT_VMEM    10
#   define RLIM_NLIMITS   11
#   define RLIM_INFINITY  (0x7fffffffffffffffLL)
#else
    typedef rlim_t          shrlim_t;
    typedef struct rlimit   shrlimit;
#endif
int sh_getrlimit(shinstance *, int, shrlimit *);
int sh_setrlimit(shinstance *, int, const shrlimit *);


#ifdef DEBUG
# define TRACE2(param)	trace param
# define TRACE2V(param)	tracev param
#else
# define TRACE2(param)
# define TRACE2V(param)
#endif

#endif
