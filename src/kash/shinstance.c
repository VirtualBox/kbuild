/* $Id: shinstance.c 3439 2020-09-10 00:47:29Z knut.osmundsen@oracle.com $ */
/** @file
 * The shell instance methods.
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#ifdef _MSC_VER
# include <process.h>
#else
# include <unistd.h>
# include <pwd.h>
#endif
#include "shinstance.h"

#include "alias.h"
#include "error.h"
#include "memalloc.h"
#include "shell.h"
#include "trap.h"

#if K_OS == K_OS_WINDOWS
# include <Windows.h>
# ifdef SH_FORKED_MODE
extern pid_t shfork_do(shinstance *psh); /* shforkA-win.asm */
# endif
#endif


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#ifndef SH_FORKED_MODE
/** Used by sh__exit/sh_thread_wrapper for passing zero via longjmp.  */
# define SH_EXIT_ZERO    0x0d15ea5e
#endif


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** The mutex protecting the the globals and some shell instance members (sigs). */
static shmtx        g_sh_mtx;
/** The root shell instance. */
static shinstance  *g_sh_root;
/** The first shell instance. */
static shinstance  *g_sh_head;
/** The last shell instance. */
static shinstance  *g_sh_tail;
/** The number of shells. */
static int volatile g_num_shells;
/** Per signal state for determining a common denominator.
 * @remarks defaults and unmasked actions aren't counted. */
struct shsigstate
{
    /** The current signal action. */
#ifndef _MSC_VER
    struct sigaction sa;
#else
    struct
    {
        void      (*sa_handler)(int);
        int         sa_flags;
        shsigset_t  sa_mask;
    } sa;
#endif
    /** The number of restarts (siginterrupt / SA_RESTART). */
    int num_restart;
    /** The number of ignore handlers. */
    int num_ignore;
    /** The number of specific handlers. */
    int num_specific;
    /** The number of threads masking it. */
    int num_masked;
}                   g_sig_state[NSIG];



/** Magic mutex value (final u64).
 * This is used to detect whether the mutex has been initialized or not,
 * allowing shmtx_delete to be called more than once without doing harm.
 * @internal */
#define SHMTX_MAGIC        KU64_C(0x8888000019641018) /**< Charles Stross */
/** Index into shmtx::au64 of the SHMTX_MAGIC value.
 * @internal */
#define SHMTX_MAGIC_IDX    (sizeof(shmtx) / sizeof(KU64) - 1)

int shmtx_init(shmtx *pmtx)
{
#if K_OS == K_OS_WINDOWS
    typedef int mtxsizecheck[sizeof(CRITICAL_SECTION) + sizeof(KU64) <= sizeof(*pmtx) ? 2 : 0];
    InitializeCriticalSection((CRITICAL_SECTION *)pmtx);
#else
    pmtx->b[0] = 0;
#endif
    pmtx->au64[SHMTX_MAGIC_IDX] = SHMTX_MAGIC;
    return 0;
}

/**
 * Safe to call more than once.
 */
void shmtx_delete(shmtx *pmtx)
{
    if (pmtx->au64[SHMTX_MAGIC_IDX] != SHMTX_MAGIC)
    {
#if K_OS == K_OS_WINDOWS
        DeleteCriticalSection((CRITICAL_SECTION *)pmtx);
#else
        pmtx->b[0] = 0;
#endif
        pmtx->au64[SHMTX_MAGIC_IDX] = ~SHMTX_MAGIC;
    }
}

void shmtx_enter(shmtx *pmtx, shmtxtmp *ptmp)
{
#if K_OS == K_OS_WINDOWS
    EnterCriticalSection((CRITICAL_SECTION *)pmtx);
    ptmp->i = 0x42;
#else
    pmtx->b[0] = 0;
    ptmp->i = 0;
#endif
}

void shmtx_leave(shmtx *pmtx, shmtxtmp *ptmp)
{
#if K_OS == K_OS_WINDOWS
    assert(ptmp->i == 0x42);
    LeaveCriticalSection((CRITICAL_SECTION *)pmtx);
    ptmp->i = 0x21;
#else
    pmtx->b[0] = 0;
    ptmp->i = 432;
#endif
}

/**
 * Links the shell instance.
 *
 * @param   psh     The shell.
 */
static void sh_int_link(shinstance *psh)
{
    shmtxtmp tmp;
    shmtx_enter(&g_sh_mtx, &tmp);

    if (psh->rootshell)
        g_sh_root = psh;

    psh->next = NULL;
    psh->prev = g_sh_tail;
    if (g_sh_tail)
        g_sh_tail->next = psh;
    else
        g_sh_tail = g_sh_head = psh;
    g_sh_tail = psh;

    g_num_shells++;

    psh->linked = 1;

    shmtx_leave(&g_sh_mtx, &tmp);
}

/**
 * Unlink the shell instance.
 *
 * @param   psh     The shell.
 */
static void sh_int_unlink(shinstance *psh)
{
    if (psh->linked)
    {
        shinstance *pshcur;
        shmtxtmp tmp;
        shmtx_enter(&g_sh_mtx, &tmp);

        g_num_shells--;

        if (g_sh_tail == psh)
            g_sh_tail = psh->prev;
        else
            psh->next->prev = psh->prev;

        if (g_sh_head == psh)
            g_sh_head = psh->next;
        else
            psh->prev->next = psh->next;

        if (g_sh_root == psh)
            g_sh_root = NULL;

        /* Orphan children: */
        for (pshcur = g_sh_head; pshcur; pshcur = pshcur->next)
            if (pshcur->parent == psh)
                pshcur->parent = NULL;

        shmtx_leave(&g_sh_mtx, &tmp);
    }
}

/**
 * Frees a string vector like environ or argv.
 *
 * @param   psh     The shell to associate the deallocations with.
 * @param   vecp    Pointer to the vector pointer.
 */
static void sh_free_string_vector(shinstance *psh, char ***vecp)
{
    char **vec = *vecp;
    if (vec)
    {
        char *str;
        size_t i = 0;
        while ((str = vec[i]) != NULL)
        {
            sh_free(psh, str);
            vec[i] = NULL;
            i++;
        }

        sh_free(psh, vec);
        *vecp = NULL;
    }
}


/**
 * Destroys the shell instance.
 *
 * This will work on partially initialized instances (because I'm lazy).
 *
 * @param   psh     The shell instance to be destroyed.
 * @note    invalidate thread arguments.
 */
static void sh_destroy(shinstance *psh)
{
    unsigned left, i;

    sh_int_unlink(psh);
    shfile_uninit(&psh->fdtab);
    sh_free_string_vector(psh, &psh->shenviron);

    /** @todo children. */
    sh_free(psh, psh->threadarg);
    psh->threadarg = NULL;

    /* alias.c */
    left = psh->aliases;
    if (left > 0)
        for (i = 0; i < K_ELEMENTS(psh->atab); i++)
        {
            struct alias *cur = psh->atab[i];
            if (cur)
            {
                do
                {
                    struct alias *next = cur->next;
                    sh_free(psh, cur->val);
                    sh_free(psh, cur->name);
                    sh_free(psh, cur);
                    cur = next;
                    left--;
                } while (cur);
                psh->atab[i] = NULL;
                if (!left)
                    break;
            }
        }

    /* cd.c */
    sh_free(psh, psh->curdir);
    psh->curdir = NULL;
    sh_free(psh, psh->prevdir);
    psh->prevdir = NULL;
    psh->cdcomppath = NULL; /* stalloc */

    /* eval.h */
    if (psh->commandnamemalloc)
        sh_free(psh, psh->commandname);
    psh->commandname = NULL;
    psh->cmdenviron = NULL;

#if 0
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
    shpid               backgndpid/* = -1 */;   /**< pid of last background process */
    int                 job_warning;    /**< user was warned about stopped jobs */

    /* jobs.c */
    struct job         *jobtab;         /**< array of jobs */
    int                 njobs;          /**< size of array */
    int                 jobs_invalid;   /**< set in child */
    shpid               initialpgrp;    /**< pgrp of shell on invocation */
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
    shpid               rootpid;        /**< pid of main shell. */
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
    char              **orgargv;        /**< The original argument vector (for cleanup). */
    int                 arg0malloc;     /**< Indicates whether arg0 was allocated or is part of orgargv. */

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
#endif

/** @todo finish this...   */
    memset(psh, 0, sizeof(*psh));
    sh_free(NULL, psh);
}

/**
 * Clones a string vector like environ or argv.
 *
 * @returns 0 on success, -1 and errno on failure.
 * @param   psh     The shell to associate the allocations with.
 * @param   dstp    Where to store the clone.
 * @param   src     The vector to be cloned.
 */
static int sh_clone_string_vector(shinstance *psh, char ***dstp, char **src)
{
    char **dst;
    size_t items;

    /* count first */
    items = 0;
    while (src[items])
        items++;

    /* alloc clone array. */
    *dstp = dst = sh_malloc(psh, sizeof(*dst) * (items + 1));
    if (!dst)
        return -1;

    /* copy the items */
    dst[items] = NULL;
    while (items-- > 0)
    {
        dst[items] = sh_strdup(psh, src[items]);
        if (!dst[items])
        {
            /* allocation error, clean up. */
            while (dst[++items])
                sh_free(psh, dst[items]);
            sh_free(psh, dst);
            errno = ENOMEM;
            return -1;
        }
    }

    return 0;
}

/**
 * Creates a shell instance, caller must link it.
 *
 * @param   inherit     The shell to inherit from, or NULL if root.
 * @param   argv        The argument vector.
 * @param   envp        The environment vector.
 * @param   parentfdtab File table to inherit from, NULL if root.
 *
 * @returns pointer to root shell on success, NULL on failure.
 */
static shinstance *sh_create_shell_common(char **argv, char **envp, shfdtab *parentfdtab)
{
    shinstance *psh;

    /*
     * The allocations.
     */
    psh = sh_calloc(NULL, sizeof(*psh), 1);
    if (psh)
    {
        /* Init it enough for sh_destroy() to not get upset: */
        /* ... */

        /* Call the basic initializers. */
        if (    !sh_clone_string_vector(psh, &psh->shenviron, envp)
            &&  !sh_clone_string_vector(psh, &psh->orgargv, argv)
            &&  !shfile_init(&psh->fdtab, parentfdtab))
        {
            unsigned i;

            /*
             * The special stuff.
             */
#ifdef _MSC_VER
            psh->pgid = psh->pid = _getpid();
#else
            psh->pid = getpid();
            psh->pgid = getpgid();
#endif

            /*sh_sigemptyset(&psh->sigrestartset);*/
            for (i = 0; i < K_ELEMENTS(psh->sigactions); i++)
                psh->sigactions[i].sh_handler = SH_SIG_UNK;
#if defined(_MSC_VER)
            sh_sigemptyset(&psh->sigmask);
#else
            sigprocmask(SIG_SETMASK, NULL, &psh->sigmask);
#endif

            /*
             * State initialization.
             */
            /* cd.c */
            psh->getpwd_first = 1;

            /* exec */
            psh->builtinloc = -1;

            /* memalloc.c */
            psh->stacknleft = MINSIZE;
            psh->herefd = -1;
            psh->stackp = &psh->stackbase;
            psh->stacknxt = psh->stackbase.space;

            /* input.c */
            psh->plinno = 1;
            psh->init_editline = 0;
            psh->parsefile = &psh->basepf;

            /* output.c */
            psh->output.bufsize = OUTBUFSIZ;
            psh->output.fd = 1;
            psh->output.psh = psh;
            psh->errout.bufsize = 100;
            psh->errout.fd = 2;
            psh->errout.psh = psh;
            psh->memout.fd = MEM_OUT;
            psh->memout.psh = psh;
            psh->out1 = &psh->output;
            psh->out2 = &psh->errout;

            /* jobs.c */
            psh->backgndpid = -1;
#if JOBS
            psh->curjob = -1;
#else
# error asdf
#endif
            psh->ttyfd = -1;

            /* show.c */
            psh->tracefd = -1;
            return psh;
        }

        sh_destroy(psh);
    }
    return NULL;
}

/**
 * Creates the root shell instance.
 *
 * @param   argv        The argument vector.
 * @param   envp        The environment vector.
 *
 * @returns pointer to root shell on success, NULL on failure.
 */
shinstance *sh_create_root_shell(char **argv, char **envp)
{
    shinstance *psh;

    assert(g_sh_mtx.au64[SHMTX_MAGIC_IDX] != SHMTX_MAGIC);
    shmtx_init(&g_sh_mtx);

    psh = sh_create_shell_common(argv, envp, NULL /*parentfdtab*/);
    if (psh)
    {
        sh_int_link(psh);
        return psh;
    }
    return NULL;
}

#ifndef SH_FORKED_MODE

/**
 * Does the inherting from the parent shell instance.
 */
static void sh_inherit_from_parent(shinstance *psh, shinstance *inherit)
{
    /*
     * Do the rest of the inheriting.
     */
    psh->parent = inherit;
    psh->pgid = inherit->pgid;

    psh->sigmask = psh->sigmask;
    /** @todo sigactions?   */
    /// @todo suppressint?

    /* alises: */
    subshellinitalias(psh, inherit);

    /* cd.c */
    psh->getpwd_first = inherit->getpwd_first;
    if (inherit->curdir)
        psh->curdir = savestr(psh, inherit->curdir);
    if (inherit->prevdir)
        psh->prevdir = savestr(psh, inherit->prevdir);

    /* eval.h */
    /* psh->commandname - see subshellinitoptions */
    psh->exitstatus  = inherit->exitstatus;          /// @todo ??
    psh->back_exitstatus = inherit->back_exitstatus; /// @todo ??
    psh->funcnest = inherit->funcnest;
    psh->evalskip = inherit->evalskip;               /// @todo ??
    psh->skipcount = inherit->skipcount;             /// @todo ??

    /* exec.c */
    subshellinitexec(psh, inherit);

    /* input.h/input.c - only for the parser and anyway forkchild calls closescript(). */

    /* jobs.h - should backgndpid be -1 in subshells? */

    /* jobs.c -    */
    psh->jobctl = inherit->jobctl;  /// @todo ??
    psh->initialpgrp = inherit->initialpgrp;
    psh->ttyfd = inherit->ttyfd;
    /** @todo copy jobtab so the 'jobs' command can be run in a subshell.
     *  Better, make it follow the parent chain and skip the copying.  Will
     *  require some kind of job locking. */

    /* mail.c - nothing (for now at least) */

    /* main.h */
    psh->rootpid = inherit->rootpid;
    psh->psh_rootshell = inherit->psh_rootshell;

    /* memalloc.h / memalloc.c - nothing. */

    /* myhistedit.h  */ /** @todo copy history? Do we need to care? */

    /* output.h */ /** @todo not sure this is possible/relevant for subshells */
    psh->output.fd = inherit->output.fd;
    psh->errout.fd = inherit->errout.fd;
    if (inherit->out1 == &inherit->memout)
        psh->out1 = &psh->memout;
    if (inherit->out2 == &inherit->memout)
        psh->out2 = &psh->memout;

    /* options.h */
    subshellinitoptions(psh, inherit);

    /* parse.h/parse.c */
    psh->whichprompt = inherit->whichprompt;
    /* tokpushback, doprompt and needprompt shouldn't really matter, parsecmd resets thems. */
    /* The rest are internal to the parser, as I see them, and can be ignored. */

    /* redir.c - we shouldn't sub-shell with redirlist as non-NULL, I think.  */
    assert(inherit->redirlist == NULL);
    assert(inherit->fd0_redirected == 0); /* (follows from redirlist == NULL) */

    /* show.c */
    psh->tracefd = inherit->tracefd;

    /* trap.h / trap.c */ /** @todo we don't carry pendingsigs to the subshell, right? */
    subshellinittrap(psh, inherit);

    /* var.h */
    subshellinitvar(psh, inherit);
}

/**
 * Creates a child shell instance.
 *
 * @param   inherit     The shell to inherit from.
 *
 * @returns pointer to root shell on success, NULL on failure.
 */
shinstance *sh_create_child_shell(shinstance *inherit)
{
    shinstance *psh = sh_create_shell_common(inherit->orgargv, inherit->shenviron, &inherit->fdtab);
    if (psh)
    {
        /* Fake a pid for the child: */
        static unsigned volatile s_cShells = 0;
        int const iSubShell = ++s_cShells;
        psh->pid = SHPID_MAKE(SHPID_GET_PID(inherit->pid), iSubShell);

        sh_inherit_from_parent(psh, inherit);

        /* link it */
        sh_int_link(psh);
        return psh;
    }
    return NULL;
}

#endif /* !SH_FORKED_MODE */

/** getenv() */
char *sh_getenv(shinstance *psh, const char *var)
{
    size_t  len;
    int     i = 0;

    if (!var)
        return NULL;

    len = strlen(var);
    i = 0;
    while (psh->shenviron[i])
    {
        const char *item = psh->shenviron[i];
        if (    !strncmp(item, var, len)
            &&  item[len] == '=')
            return (char *)item + len + 1;
    }

    return NULL;
}

char **sh_environ(shinstance *psh)
{
    return psh->shenviron;
}

const char *sh_gethomedir(shinstance *psh, const char *user)
{
    const char *ret = NULL;

#ifdef _MSC_VER
    ret = sh_getenv(psh, "HOME");
    if (!ret)
        ret = sh_getenv(psh, "USERPROFILE");
#else
    struct passwd *pwd = getpwnam(user); /** @todo use getpwdnam_r */
    (void)psh;
    ret = pwd ? pwd->pw_dir : NULL;
#endif

    return ret;
}

/**
 * Lazy initialization of a signal state, globally.
 *
 * @param   psh         The shell doing the lazy work.
 * @param   signo       The signal (valid).
 */
static void sh_int_lazy_init_sigaction(shinstance *psh, int signo)
{
    if (psh->sigactions[signo].sh_handler == SH_SIG_UNK)
    {
        shmtxtmp tmp;
        shmtx_enter(&g_sh_mtx, &tmp);

        if (psh->sigactions[signo].sh_handler == SH_SIG_UNK)
        {
            shsigaction_t shold;
            shinstance *cur;
#ifndef _MSC_VER
            struct sigaction old;
            if (!sigaction(signo, NULL, &old))
            {
                /* convert */
                shold.sh_flags = old.sa_flags;
                shold.sh_mask = old.sa_mask;
                if (old.sa_handler == SIG_DFL)
                    shold.sh_handler = SH_SIG_DFL;
                else
                {
                    assert(old.sa_handler == SIG_IGN);
                    shold.sh_handler = SH_SIG_IGN;
                }
            }
            else
#endif
            {
                /* fake */
#ifndef _MSC_VER
                assert(0);
                old.sa_handler = SIG_DFL;
                old.sa_flags = 0;
                sigemptyset(&shold.sh_mask);
                sigaddset(&shold.sh_mask, signo);
#endif
                shold.sh_flags = 0;
                sh_sigemptyset(&shold.sh_mask);
                sh_sigaddset(&shold.sh_mask, signo);
                shold.sh_handler = SH_SIG_DFL;
            }

            /* update globals */
#ifndef _MSC_VER
            g_sig_state[signo].sa = old;
#else
            g_sig_state[signo].sa.sa_handler = SIG_DFL;
            g_sig_state[signo].sa.sa_flags = 0;
            g_sig_state[signo].sa.sa_mask = shold.sh_mask;
#endif
            TRACE2((psh, "sh_int_lazy_init_sigaction: signo=%d:%s sa_handler=%p sa_flags=%#x\n",
                    signo, sys_signame[signo], g_sig_state[signo].sa.sa_handler, g_sig_state[signo].sa.sa_flags));

            /* update all shells */
            for (cur = g_sh_head; cur; cur = cur->next)
            {
                assert(cur->sigactions[signo].sh_handler == SH_SIG_UNK);
                cur->sigactions[signo] = shold;
            }
        }

        shmtx_leave(&g_sh_mtx, &tmp);
    }
}

/**
 * Perform the default signal action on the shell.
 *
 * @param   psh         The shell instance.
 * @param   signo       The signal.
 */
static void sh_sig_do_default(shinstance *psh, int signo)
{
    /** @todo */
}

/**
 * Deliver a signal to a shell.
 *
 * @param   psh         The shell instance.
 * @param   pshDst      The shell instance to signal.
 * @param   signo       The signal.
 * @param   locked      Whether we're owning the lock or not.
 */
static void sh_sig_do_signal(shinstance *psh, shinstance *pshDst, int signo, int locked)
{
    shsig_t pfn = pshDst->sigactions[signo].sh_handler;
    if (pfn == SH_SIG_UNK)
    {
        sh_int_lazy_init_sigaction(pshDst, signo);
        pfn = pshDst->sigactions[signo].sh_handler;
    }

    if (pfn == SH_SIG_DFL)
        sh_sig_do_default(pshDst, signo);
    else if (pfn == SH_SIG_IGN)
        /* ignore it */;
    else
    {
        assert(pfn != SH_SIG_ERR);
        pfn(pshDst, signo);
    }
    (void)locked;
}

/**
 * Handler for external signals.
 *
 * @param   signo       The signal.
 */
static void sh_sig_common_handler(int signo)
{
    shinstance *psh;

/*    fprintf(stderr, "sh_sig_common_handler: signo=%d:%s\n", signo, sys_signame[signo]); */

    /*
     * No need to take locks if there is only one shell.
     * Since this will be the initial case, just avoid the deadlock
     * hell for a litte while...
     */
    if (g_num_shells <= 1)
    {
        psh = g_sh_head;
        if (psh)
            sh_sig_do_signal(NULL, psh, signo, 0 /* no lock */);
    }
    else
    {
        shmtxtmp tmp;
        shmtx_enter(&g_sh_mtx, &tmp);

        /** @todo signal focus chain or something? Atm there will only be one shell,
         *        so it's not really important until we go threaded for real... */
        psh = g_sh_tail;
        while (psh != NULL)
        {
            sh_sig_do_signal(NULL, psh, signo, 1 /* locked */);
            psh = psh->prev;
        }

        shmtx_leave(&g_sh_mtx, &tmp);
    }
}

int sh_sigaction(shinstance *psh, int signo, const struct shsigaction *newp, struct shsigaction *oldp)
{
    if (newp)
        TRACE2((psh, "sh_sigaction: signo=%d:%s newp=%p:{.sh_handler=%p, .sh_flags=%#x} oldp=%p\n",
                signo, sys_signame[signo], newp, newp->sh_handler, newp->sh_flags, oldp));
    else
        TRACE2((psh, "sh_sigaction: signo=%d:%s newp=NULL oldp=%p\n", signo, sys_signame[signo], oldp));

    /*
     * Input validation.
     */
    if (signo >= NSIG || signo <= 0)
    {
        errno = EINVAL;
        return -1;
    }

    /*
     * Make sure our data is correct.
     */
    sh_int_lazy_init_sigaction(psh, signo);

    /*
     * Get the old one if requested.
     */
    if (oldp)
        *oldp = psh->sigactions[signo];

    /*
     * Set the new one if it has changed.
     *
     * This will be attempted coordinated with the other signal handlers so
     * that we can arrive at a common denominator.
     */
    if (    newp
        &&  memcmp(&psh->sigactions[signo], newp, sizeof(*newp)))
    {
        shmtxtmp tmp;
        shmtx_enter(&g_sh_mtx, &tmp);

        /* Undo the accounting for the current entry. */
        if (psh->sigactions[signo].sh_handler == SH_SIG_IGN)
            g_sig_state[signo].num_ignore--;
        else if (psh->sigactions[signo].sh_handler != SH_SIG_DFL)
            g_sig_state[signo].num_specific--;
        if (psh->sigactions[signo].sh_flags & SA_RESTART)
            g_sig_state[signo].num_restart--;

        /* Set the new entry. */
        psh->sigactions[signo] = *newp;

        /* Add the bits for the new action entry. */
        if (psh->sigactions[signo].sh_handler == SH_SIG_IGN)
            g_sig_state[signo].num_ignore++;
        else if (psh->sigactions[signo].sh_handler != SH_SIG_DFL)
            g_sig_state[signo].num_specific++;
        if (psh->sigactions[signo].sh_flags & SA_RESTART)
            g_sig_state[signo].num_restart++;

        /*
         * Calc new common action.
         *
         * This is quit a bit ASSUMPTIVE about the limited use. We will not
         * bother synching the mask, and we pretend to care about SA_RESTART.
         * The only thing we really actually care about is the sh_handler.
         *
         * On second though, it's possible we should just tie this to the root
         * shell since it only really applies to external signal ...
         */
        if (    g_sig_state[signo].num_specific
            ||  g_sig_state[signo].num_ignore != g_num_shells)
            g_sig_state[signo].sa.sa_handler = sh_sig_common_handler;
        else if (g_sig_state[signo].num_ignore)
            g_sig_state[signo].sa.sa_handler = SIG_IGN;
        else
            g_sig_state[signo].sa.sa_handler = SIG_DFL;
        g_sig_state[signo].sa.sa_flags = psh->sigactions[signo].sh_flags & SA_RESTART;

        TRACE2((psh, "sh_sigaction: setting signo=%d:%s to {.sa_handler=%p, .sa_flags=%#x}\n",
                signo, sys_signame[signo], g_sig_state[signo].sa.sa_handler, g_sig_state[signo].sa.sa_flags));
#ifdef _MSC_VER
        if (signal(signo, g_sig_state[signo].sa.sa_handler) == SIG_ERR)
        {
            TRACE2((psh, "sh_sigaction: SIG_ERR, errno=%d signo=%d\n", errno, signo));
            if (   signo != SIGHUP   /* whatever */
                && signo != SIGQUIT
                && signo != SIGPIPE
                && signo != SIGTTIN
                && signo != SIGTSTP
                && signo != SIGTTOU
                && signo != SIGCONT)
                assert(0);
        }
#else
        if (sigaction(signo, &g_sig_state[signo].sa, NULL))
            assert(0);
#endif

        shmtx_leave(&g_sh_mtx, &tmp);
    }

    return 0;
}

shsig_t sh_signal(shinstance *psh, int signo, shsig_t handler)
{
    shsigaction_t sa;
    shsig_t ret;

    /*
     * Implementation using sh_sigaction.
     */
    if (sh_sigaction(psh, signo, NULL, &sa))
        return SH_SIG_ERR;

    ret = sa.sh_handler;
    sa.sh_flags &= SA_RESTART;
    sa.sh_handler = handler;
    sh_sigemptyset(&sa.sh_mask);
    sh_sigaddset(&sa.sh_mask, signo); /* ?? */
    if (sh_sigaction(psh, signo, &sa, NULL))
        return SH_SIG_ERR;

    return ret;
}

int sh_siginterrupt(shinstance *psh, int signo, int interrupt)
{
    shsigaction_t sa;
    int oldflags = 0;

    /*
     * Implementation using sh_sigaction.
     */
    if (sh_sigaction(psh, signo, NULL, &sa))
        return -1;
    oldflags = sa.sh_flags;
    if (interrupt)
        sa.sh_flags &= ~SA_RESTART;
    else
        sa.sh_flags |= ~SA_RESTART;
    if (!((oldflags ^ sa.sh_flags) & SA_RESTART))
        return 0; /* unchanged. */

    return sh_sigaction(psh, signo, &sa, NULL);
}

void sh_sigemptyset(shsigset_t *setp)
{
    memset(setp, 0, sizeof(*setp));
}

void sh_sigfillset(shsigset_t *setp)
{
    memset(setp, 0xff, sizeof(*setp));
}

void sh_sigaddset(shsigset_t *setp, int signo)
{
#ifdef _MSC_VER
    *setp |= 1U << signo;
#else
    sigaddset(setp, signo);
#endif
}

void sh_sigdelset(shsigset_t *setp, int signo)
{
#ifdef _MSC_VER
    *setp &= ~(1U << signo);
#else
    sigdelset(setp, signo);
#endif
}

int sh_sigismember(shsigset_t const *setp, int signo)
{
#ifdef _MSC_VER
    return !!(*setp & (1U << signo));
#else
    return !!sigismember(setp, signo);
#endif
}

int sh_sigprocmask(shinstance *psh, int operation, shsigset_t const *newp, shsigset_t *oldp)
{
    int rc;

    if (    operation != SIG_BLOCK
        &&  operation != SIG_UNBLOCK
        &&  operation != SIG_SETMASK)
    {
        errno = EINVAL;
        return -1;
    }

#if defined(SH_FORKED_MODE) && !defined(_MSC_VER)
    rc = sigprocmask(operation, newp, oldp);
    if (!rc && newp)
        psh->sigmask = *newp;

#else
    if (oldp)
        *oldp = psh->sigmask;
    if (newp)
    {
        /* calc the new mask */
        shsigset_t mask = psh->sigmask;
        switch (operation)
        {
            case SIG_BLOCK:
                for (rc = 0; rc < NSIG; rc++)
                    if (sh_sigismember(newp, rc))
                        sh_sigaddset(&mask, rc);
                break;
            case SIG_UNBLOCK:
                for (rc = 0; rc < NSIG; rc++)
                    if (sh_sigismember(newp, rc))
                        sh_sigdelset(&mask, rc);
                break;
            case SIG_SETMASK:
                mask = *newp;
                break;
        }

# if defined(_MSC_VER)
        rc = 0;
# else
        rc = sigprocmask(operation, &mask, NULL);
        if (!rc)
# endif
            psh->sigmask = mask;
    }

#endif
    return rc;
}

SH_NORETURN_1 void sh_abort(shinstance *psh)
{
    shsigset_t set;
    TRACE2((psh, "sh_abort\n"));

    /* block other async signals */
    sh_sigfillset(&set);
    sh_sigdelset(&set, SIGABRT);
    sh_sigprocmask(psh, SIG_SETMASK, &set, NULL);

    sh_sig_do_signal(psh, psh, SIGABRT, 0 /* no lock */);

    /** @todo die in a nicer manner. */
    *(char *)1 = 3;

    TRACE2((psh, "sh_abort returns!\n"));
    (void)psh;
    abort();
}

void sh_raise_sigint(shinstance *psh)
{
    TRACE2((psh, "sh_raise(SIGINT)\n"));

    sh_sig_do_signal(psh, psh, SIGINT, 0 /* no lock */);

    TRACE2((psh, "sh_raise(SIGINT) returns\n"));
}

int sh_kill(shinstance *psh, shpid pid, int signo)
{
    shinstance *pshDst;
    shmtxtmp tmp;
    int rc;

    /*
     * Self or any of the subshells?
     */
    shmtx_enter(&g_sh_mtx, &tmp);

    pshDst = g_sh_tail;
    while (pshDst != NULL)
    {
        if (pshDst->pid == pid)
        {
            TRACE2((psh, "sh_kill(%" SHPID_PRI ", %d): pshDst=%p\n", pid, signo, pshDst));
            sh_sig_do_signal(psh, pshDst, signo, 1 /* locked */);

            shmtx_leave(&g_sh_mtx, &tmp);
            return 0;
        }
        pshDst = pshDst->prev;
    }

    shmtx_leave(&g_sh_mtx, &tmp);

    /*
     * Some other process, call kill where possible
     */
#ifdef _MSC_VER
    errno = ENOSYS;
    rc = -1;
#elif defined(SH_FORKED_MODE)
/*    fprintf(stderr, "kill(%d, %d)\n", pid, signo);*/
    rc = kill(pid, signo);
#else
# error "PORT ME?"
#endif

    TRACE2((psh, "sh_kill(%d, %d) -> %d [%d]\n", pid, signo, rc, errno));
    return rc;
}

int sh_killpg(shinstance *psh, shpid pgid, int signo)
{
    shinstance *pshDst;
    shmtxtmp tmp;
    int rc;

    /*
     * Self or any of the subshells?
     */
    shmtx_enter(&g_sh_mtx, &tmp);

    pshDst = g_sh_tail;
    while (pshDst != NULL)
    {
        if (pshDst->pgid == pgid)
        {
            TRACE2((psh, "sh_killpg(%" SHPID_PRI ", %d): pshDst=%p\n", pgid, signo, pshDst));
            sh_sig_do_signal(psh, pshDst, signo, 1 /* locked */);

            shmtx_leave(&g_sh_mtx, &tmp);
            return 0;
        }
        pshDst = pshDst->prev;
    }

    shmtx_leave(&g_sh_mtx, &tmp);

#ifdef _MSC_VER
    errno = ENOSYS;
    rc = -1;
#elif defined(SH_FORKED_MODE)
    //fprintf(stderr, "killpg(%d, %d)\n", pgid, signo);
    rc = killpg(pgid, signo);
#else
# error "PORTME?"
#endif

    TRACE2((psh, "sh_killpg(%" SHPID_PRI ", %d) -> %d [%d]\n", pgid, signo, rc, errno));
    (void)psh;
    return rc;
}

clock_t sh_times(shinstance *psh, shtms *tmsp)
{
#ifdef _MSC_VER
    errno = ENOSYS;
    return (clock_t)-1;
#elif defined(SH_FORKED_MODE)
    (void)psh;
    return times(tmsp);
#else
# error "PORTME"
#endif
}

int sh_sysconf_clk_tck(void)
{
#ifdef _MSC_VER
    return CLK_TCK;
#else
    return sysconf(_SC_CLK_TCK);
#endif
}

/**
 * Adds a child to the shell
 *
 * @returns 0 on success, on failure -1 and errno set to ENOMEM.
 *
 * @param   psh         The shell instance.
 * @param   pid         The child pid.
 * @param   hChild      Windows child handle.
 * @param   fProcess    Set if process, clear if thread.
 */
int sh_add_child(shinstance *psh, shpid pid, void *hChild, KBOOL fProcess)
{
    /* get a free table entry. */
    unsigned i = psh->num_children++;
    if (!(i % 32))
    {
        void *ptr = sh_realloc(psh, psh->children, sizeof(*psh->children) * (i + 32));
        if (!ptr)
        {
            psh->num_children--;
            errno = ENOMEM;
            return -1;
        }
        psh->children = ptr;
    }

    /* add it */
    psh->children[i].pid = pid;
#if K_OS == K_OS_WINDOWS
    psh->children[i].hChild = hChild;
#endif
#ifndef SH_FORKED_MODE
    psh->children[i].fProcess = fProcess;
#endif
    (void)hChild; (void)fProcess;
    return 0;
}

#ifdef SH_FORKED_MODE

pid_t sh_fork(shinstance *psh)
{
    pid_t pid;
    TRACE2((psh, "sh_fork\n"));

#if K_OS == K_OS_WINDOWS //&& defined(SH_FORKED_MODE)
    pid = shfork_do(psh);

#elif defined(SH_FORKED_MODE)
# ifdef _MSC_VER
    pid = -1;
    errno = ENOSYS;
# else
    pid = fork();
# endif

#else

#endif

    /* child: update the pid and zap the children array */
    if (!pid)
    {
# ifdef _MSC_VER
        psh->pid = _getpid();
# else
        psh->pid = getpid();
# endif
        psh->num_children = 0;
    }

    TRACE2((psh, "sh_fork -> %d [%d]\n", pid, errno));
    (void)psh;
    return pid;
}

#else  /* !SH_FORKED_MODE */

# ifdef _MSC_VER
/** Thread wrapper procedure. */
static unsigned __stdcall sh_thread_wrapper(void *user)
{
    shinstance * volatile volpsh = (shinstance *)user;
    shinstance *psh = (shinstance *)user;
    struct jmploc exitjmp;
    int iExit;

    /* Update the TID and PID (racing sh_thread_start) */
    DWORD tid = GetCurrentThreadId();
    shpid pid = GetCurrentProcessId();

    pid = SHPID_MAKE(pid, tid);
    psh->pid = pid;
    psh->tid = tid;

    /* Set the TLS entry before we try TRACE or TRACE2. */
    shthread_set_shell(psh);

    TRACE2((psh, "sh_thread_wrapper: enter\n"));
    if ((iExit = setjmp(exitjmp.loc)) == 0)
    {
        psh->exitjmp = &exitjmp;
        iExit = psh->thread(psh, psh->threadarg);
        TRACE2((psh, "sh_thread_wrapper: thread proc returns %d (%#x)\n", iExit, iExit));
    }
    else
    {
        psh = volpsh; /* paranoia */
        psh->exitjmp = NULL;
        TRACE2((psh, "sh_thread_wrapper: longjmp: iExit=%d (%#x)\n", iExit, iExit));
        if (iExit == SH_EXIT_ZERO)
            iExit = 0;
    }

    /* destroy the shell instance and exit the thread. */
    TRACE2((psh, "sh_thread_wrapper: quits - iExit=%d\n", iExit));
    sh_destroy(psh);
    shthread_set_shell(NULL);
    _endthreadex(iExit);
    return iExit;
}
# else
#  error "PORTME"
# endif

/**
 * Starts a sub-shell thread.
 */
shpid sh_thread_start(shinstance *pshparent, shinstance *pshchild, int (*thread)(shinstance *, void *), void *arg)
{
# ifdef _MSC_VER
    unsigned tid = 0;
    uintptr_t hThread;
    shpid pid;

    pshchild->thread    = thread;
    pshchild->threadarg = arg;
    hThread = _beginthreadex(NULL /*security*/, 0 /*stack_size*/, sh_thread_wrapper, pshchild, 0 /*initflags*/, &tid);
    if (hThread == -1)
        return -errno;

    pid = SHPID_MAKE(SHPID_GET_PID(pshparent->pid), tid);
    pshchild->pid = pid;
    pshchild->tid = tid;

    if (sh_add_child(pshparent, pid, (void *)hThread, K_FALSE) != 0) {
        return -ENOMEM;
    }
    return pid;

# else
#  error "PORTME"
# endif
}

#endif /* !SH_FORKED_MODE */

/** waitpid() */
shpid sh_waitpid(shinstance *psh, shpid pid, int *statusp, int flags)
{
    shpid       pidret;
#if K_OS == K_OS_WINDOWS //&& defined(SH_FORKED_MODE)
    DWORD       dwRet;
    HANDLE      hChild = INVALID_HANDLE_VALUE;
    unsigned    i;

    *statusp = 0;
    pidret = -1;
    if (pid != -1)
    {
        /*
         * A specific child, try look it up in the child process table
         * and wait for it.
         */
        for (i = 0; i < psh->num_children; i++)
            if (psh->children[i].pid == pid)
                break;
        if (i < psh->num_children)
        {
            dwRet = WaitForSingleObject(psh->children[i].hChild,
                                        flags & WNOHANG ? 0 : INFINITE);
            if (dwRet == WAIT_OBJECT_0)
                hChild = psh->children[i].hChild;
            else if (dwRet == WAIT_TIMEOUT)
            {
                i = ~0; /* don't try close anything */
                pidret = 0;
            }
            else
                errno = ECHILD;
        }
        else
            errno = ECHILD;
    }
    else if (psh->num_children <= MAXIMUM_WAIT_OBJECTS)
    {
        HANDLE ahChildren[MAXIMUM_WAIT_OBJECTS];
        for (i = 0; i < psh->num_children; i++)
            ahChildren[i] = psh->children[i].hChild;
        dwRet = WaitForMultipleObjects(psh->num_children, &ahChildren[0],
                                       FALSE,
                                       flags & WNOHANG ? 0 : INFINITE);
        i = dwRet - WAIT_OBJECT_0;
        if (i < psh->num_children)
        {
            hChild = psh->children[i].hChild;
        }
        else if (dwRet == WAIT_TIMEOUT)
        {
            i = ~0; /* don't try close anything */
            pidret = 0;
        }
        else
        {
            i = ~0; /* don't try close anything */
            errno = EINVAL;
        }
    }
    else
    {
        fprintf(stderr, "panic! too many children!\n");
        i = ~0;
        *(char *)1 = '\0'; /** @todo implement this! */
    }

    /*
     * Close the handle, and if we succeeded collect the exit code first.
     */
    if (i < psh->num_children)
    {
        if (hChild != INVALID_HANDLE_VALUE)
        {
            DWORD dwExitCode = 127;
#ifndef SH_FORKED_MODE
            if (psh->children[i].fProcess ? GetExitCodeProcess(hChild, &dwExitCode) : GetExitCodeThread(hChild, &dwExitCode))
#else
            if (GetExitCodeProcess(hChild, &dwExitCode))
#endif
            {
                pidret = psh->children[i].pid;
                if (dwExitCode && !W_EXITCODE(dwExitCode, 0))
                    dwExitCode |= 16;
                *statusp = W_EXITCODE(dwExitCode, 0);
            }
            else
                errno = EINVAL;
        }

        /* remove and close */
        hChild = psh->children[i].hChild;
        psh->num_children--;
        if (i < psh->num_children)
            psh->children[i] = psh->children[psh->num_children];
        i = CloseHandle(hChild); assert(i);
    }

#elif defined(SH_FORKED_MODE)
    *statusp = 0;
# ifdef _MSC_VER
    pidret = -1;
    errno = ENOSYS;
# else
    pidret = waitpid(pid, statusp, flags);
# endif

#else
#endif

    TRACE2((psh, "waitpid(%" SHPID_PRI ", %p, %#x) -> %" SHPID_PRI " [%d] *statusp=%#x (rc=%d)\n", pid, statusp, flags,
            pidret, errno, *statusp, WEXITSTATUS(*statusp)));
    (void)psh;
    return pidret;
}

SH_NORETURN_1 void sh__exit(shinstance *psh, int rc)
{
    TRACE2((psh, "sh__exit(%d)\n", rc));

#if defined(SH_FORKED_MODE)
    _exit(rc);
    (void)psh;

#else
    psh->exitstatus = rc;

    /*
     * If we're a thread, jump to the sh_thread_wrapper and make a clean exit.
     */
    if (psh->thread)
    {
        if (psh->exitjmp)
            longjmp(psh->exitjmp->loc, !rc ? SH_EXIT_ZERO : rc);
        else
        {
            static char const s_msg[] = "fatal error in sh__exit: exitjmp is NULL!\n";
            shfile_write(&psh->fdtab, 2, s_msg, sizeof(s_msg) - 1);
            _exit(rc);
        }
    }

    /*
     * The main thread will typically have to stick around till all subshell
     * threads have been stopped.  We must tear down this shell instance as
     * much as possible before doing this, though, as subshells could be
     * waiting for pipes and such to be closed before they're willing to exit.
     */
    if (g_num_shells > 1)
    {
        TRACE2((psh, "sh__exit: %u shells around, must wait...\n", g_num_shells));
        shfile_uninit(&psh->fdtab);
        sh_int_unlink(psh);
    }

    _exit(rc);
#endif
}

int sh_execve(shinstance *psh, const char *exe, const char * const *argv, const char * const *envp)
{
    int rc;

#ifdef DEBUG
    /* log it all */
    TRACE2((psh, "sh_execve(%p:{%s}, %p, %p}\n", exe, exe, argv, envp));
    for (rc = 0; argv[rc]; rc++)
        TRACE2((psh, "  argv[%d]=%p:{%s}\n", rc, argv[rc], argv[rc]));
#endif

    if (!envp)
        envp = (const char * const *)sh_environ(psh);

#if defined(SH_FORKED_MODE) && K_OS != K_OS_WINDOWS
# ifdef _MSC_VER
    errno = 0;
    {
        intptr_t rc2 = _spawnve(_P_WAIT, exe, (char **)argv, (char **)envp);
        if (rc2 != -1)
        {
            TRACE2((psh, "sh_execve: child exited, rc=%d. (errno=%d)\n", rc, errno));
            rc = (int)rc2;
            if (!rc && rc2)
                rc = 16;
            exit(rc);
        }
    }
    rc = -1;

# else
    rc = shfile_exec_unix(&psh->fdtab);
    if (!rc)
        rc = execve(exe, (char **)argv, (char **)envp);
# endif

#else
# if K_OS == K_OS_WINDOWS
    {
        /*
         * This ain't quite straight forward on Windows...
         */
        PROCESS_INFORMATION ProcInfo;
        STARTUPINFO StrtInfo;
        intptr_t hndls[3];
        char *cwd = shfile_getcwd(&psh->fdtab, NULL, 0);
        char *cmdline;
        size_t cmdline_size;
        char *envblock;
        size_t env_size;
        char *p;
        int i;

        /* Create the environment block. */
        if (!envp)
            envp = sh_environ(psh);
        env_size = 2;
        for (i = 0; envp[i]; i++)
            env_size += strlen(envp[i]) + 1;
        envblock = p = sh_malloc(psh, env_size);
        for (i = 0; envp[i]; i++)
        {
            size_t len = strlen(envp[i]) + 1;
            memcpy(p, envp[i], len);
            p += len;
        }
        *p = '\0';

        /* Figure the size of the command line. Double quotes makes this
           tedious and we overestimate to simplify. */
        cmdline_size = 2;
        for (i = 0; argv[i]; i++)
        {
            const char *arg = argv[i];
            cmdline_size += strlen(arg) + 3;
            arg = strchr(arg, '"');
            if (arg)
            {
                do
                    cmdline_size++;
                while ((arg = strchr(arg + 1, '"')) != NULL);
                arg = argv[i] - 1;
                while ((arg = strchr(arg + 1, '\\')) != NULL);
                    cmdline_size++;
            }
        }

        /* Create the command line. */
        cmdline = p = sh_malloc(psh, cmdline_size);
        for (i = 0; argv[i]; i++)
        {
            const char *arg = argv[i];
            const char *cur = arg;
            size_t len = strlen(arg);
            int quoted = 0;
            char ch;
            while ((ch = *cur++) != '\0')
                if (ch <= 0x20 || strchr("&><|%", ch) != NULL)
                {
                    quoted = 1;
                    break;
                }

            if (i != 0)
                *(p++) = ' ';
            if (quoted)
                *(p++) = '"';
            if (memchr(arg, '"', len) == NULL)
            {
                memcpy(p, arg, len);
                p += len;
            }
            else
            {   /* MS CRT style: double quotes must be escaped; backslashes
                   must be escaped if followed by double quotes. */
                while ((ch = *arg++) != '\0')
                    if (ch != '\\' && ch != '"')
                        *p++ = ch;
                    else if (ch == '"')
                    {
                        *p++ = '\\';
                        *p++ = '"';
                    }
                    else
                    {
                        unsigned slashes = 1;
                        *p++ = '\\';
                        while (*arg == '\\')
                        {
                            *p++ = '\\';
                            slashes++;
                            arg++;
                        }
                        if (*arg == '"')
                        {
                            while (slashes-- > 0)
                                *p++ = '\\';
                            *p++ = '\\';
                            *p++ = '"';
                            arg++;
                        }
                    }
            }
            if (quoted)
                *(p++) = '"';
        }
        p[0] = p[1] = '\0';

        /* Init the info structure */
        memset(&StrtInfo, '\0', sizeof(StrtInfo));
        StrtInfo.cb = sizeof(StrtInfo);

        /* File handles. */
        StrtInfo.dwFlags   |= STARTF_USESTDHANDLES;
        StrtInfo.lpReserved2 = shfile_exec_win(&psh->fdtab, 1 /* prepare */, &StrtInfo.cbReserved2, hndls);
        StrtInfo.hStdInput  = (HANDLE)hndls[0];
        StrtInfo.hStdOutput = (HANDLE)hndls[1];
        StrtInfo.hStdError  = (HANDLE)hndls[2];

        /* Get going... */
        if (CreateProcess(exe,
                          cmdline,
                          NULL,         /* pProcessAttributes */
                          NULL,         /* pThreadAttributes */
                          TRUE,         /* bInheritHandles */
                          0,            /* dwCreationFlags */
                          envblock,
                          cwd,
                          &StrtInfo,
                          &ProcInfo))
        {
            DWORD dwErr;
            DWORD dwExitCode;

            CloseHandle(ProcInfo.hThread);
            dwErr = WaitForSingleObject(ProcInfo.hProcess, INFINITE);
            assert(dwErr == WAIT_OBJECT_0);

            if (GetExitCodeProcess(ProcInfo.hProcess, &dwExitCode))
            {
                CloseHandle(ProcInfo.hProcess);
                _exit(dwExitCode);
            }
            errno = EINVAL;
        }
        else
        {
            DWORD dwErr = GetLastError();
            switch (dwErr)
            {
                case ERROR_FILE_NOT_FOUND:          errno = ENOENT; break;
                case ERROR_PATH_NOT_FOUND:          errno = ENOENT; break;
                case ERROR_BAD_EXE_FORMAT:          errno = ENOEXEC; break;
                case ERROR_INVALID_EXE_SIGNATURE:   errno = ENOEXEC; break;
                default:
                    errno = EINVAL;
                    break;
            }
            TRACE2((psh, "sh_execve: dwErr=%d -> errno=%d\n", dwErr, errno));
        }

        shfile_exec_win(&psh->fdtab, 0 /* done */, NULL, NULL);
    }
    rc = -1;

# else
    errno = ENOSYS;
    rc = -1;
# endif
#endif

    TRACE2((psh, "sh_execve -> %d [%d]\n", rc, errno));
    (void)psh;
    return (int)rc;
}

uid_t sh_getuid(shinstance *psh)
{
#ifdef _MSC_VER
    uid_t uid = 0;
#else
    uid_t uid = getuid();
#endif

    TRACE2((psh, "sh_getuid() -> %d [%d]\n", uid, errno));
    (void)psh;
    return uid;
}

uid_t sh_geteuid(shinstance *psh)
{
#ifdef _MSC_VER
    uid_t euid = 0;
#else
    uid_t euid = geteuid();
#endif

    TRACE2((psh, "sh_geteuid() -> %d [%d]\n", euid, errno));
    (void)psh;
    return euid;
}

gid_t sh_getgid(shinstance *psh)
{
#ifdef _MSC_VER
    gid_t gid = 0;
#else
    gid_t gid = getgid();
#endif

    TRACE2((psh, "sh_getgid() -> %d [%d]\n", gid, errno));
    (void)psh;
    return gid;
}

gid_t sh_getegid(shinstance *psh)
{
#ifdef _MSC_VER
    gid_t egid = 0;
#else
    gid_t egid = getegid();
#endif

    TRACE2((psh, "sh_getegid() -> %d [%d]\n", egid, errno));
    (void)psh;
    return egid;
}

shpid sh_getpid(shinstance *psh)
{
    return psh->pid;
}

shpid sh_getpgrp(shinstance *psh)
{
    shpid pgid = psh->pgid;
#ifndef _MSC_VER
    assert(pgid == getpgrp());
#endif

    TRACE2((psh, "sh_getpgrp() -> %" SHPID_PRI " [%d]\n", pgid, errno));
    return pgid;
}

/**
 * @param   pid     Should always be zero, i.e. referring to the current shell
 *                  process.
 */
shpid sh_getpgid(shinstance *psh, shpid pid)
{
    shpid pgid;
    if (pid == 0 || psh->pid == pid)
    {
        shpid pgid = psh->pgid;
#ifndef _MSC_VER
        assert(pgid == getpgrp());
#endif
    }
    else
    {
        assert(0);
        errno = ESRCH;
        pgid = -1;
    }

    TRACE2((psh, "sh_getpgid(%" SHPID_PRI ") -> %" SHPID_PRI " [%d]\n", pid, pgid, errno));
    return pgid;
}

/**
 *
 * @param   pid     The pid to modify.  This is always 0, except when forkparent
 *                  calls to group a newly created child.  Though, we might
 *                  almost safely ignore it in that case as the child will also
 *                  perform the operation.
 * @param   pgid    The process group to assign @a pid to.
 */
int sh_setpgid(shinstance *psh, shpid pid, shpid pgid)
{
#if defined(SH_FORKED_MODE) && !defined(_MSC_VER)
    int rc = setpgid(pid, pgid);
    TRACE2((psh, "sh_setpgid(%" SHPID_PRI ", %" SHPID_PRI ") -> %d [%d]\n", pid, pgid, rc, errno));
    (void)psh;
#else
    int rc = 0;
    if (pid == 0 || psh->pid == pid)
    {
        TRACE2((psh, "sh_setpgid(self,): %" SHPID_PRI " -> %" SHPID_PRI "\n", psh->pgid, pgid));
        psh->pgid = pgid;
    }
    else
    {
        /** @todo fixme   */
        rc = -1;
        errno = ENOSYS;
    }
#endif
    return rc;
}

shpid sh_tcgetpgrp(shinstance *psh, int fd)
{
    shpid pgrp;

#ifdef _MSC_VER
    pgrp = -1;
    errno = ENOSYS;
#elif defined(SH_FORKED_MODE)
    pgrp = tcgetpgrp(fd);
#else
# error "PORT ME"
#endif

    TRACE2((psh, "sh_tcgetpgrp(%d) -> %" SHPID_PRI " [%d]\n", fd, pgrp, errno));
    (void)psh;
    return pgrp;
}

int sh_tcsetpgrp(shinstance *psh, int fd, shpid pgrp)
{
    int rc;
    TRACE2((psh, "sh_tcsetpgrp(%d, %" SHPID_PRI ")\n", fd, pgrp));

#ifdef _MSC_VER
    rc = -1;
    errno = ENOSYS;
#elif defined(SH_FORKED_MODE)
    rc = tcsetpgrp(fd, pgrp);
#else
# error "PORT ME"
#endif

    TRACE2((psh, "sh_tcsetpgrp(%d, %" SHPID_PRI ") -> %d [%d]\n", fd, pgrp, rc, errno));
    (void)psh;
    return rc;
}

int sh_getrlimit(shinstance *psh, int resid, shrlimit *limp)
{
#ifdef _MSC_VER
    int rc = -1;
    errno = ENOSYS;
#elif defined(SH_FORKED_MODE)
    int rc = getrlimit(resid, limp);
#else
# error "PORT ME"
    /* returned the stored limit */
#endif

    TRACE2((psh, "sh_getrlimit(%d, %p) -> %d [%d] {%ld,%ld}\n",
            resid, limp, rc, errno, (long)limp->rlim_cur, (long)limp->rlim_max));
    (void)psh;
    return rc;
}

int sh_setrlimit(shinstance *psh, int resid, const shrlimit *limp)
{
#ifdef _MSC_VER
    int rc = -1;
    errno = ENOSYS;
#elif defined(SH_FORKED_MODE)
    int rc = setrlimit(resid, limp);
#else
# error "PORT ME"
    /* if max(shell) < limp; then setrlimit; fi
       if success; then store limit for later retrival and maxing. */

#endif

    TRACE2((psh, "sh_setrlimit(%d, %p:{%ld,%ld}) -> %d [%d]\n",
            resid, limp, (long)limp->rlim_cur, (long)limp->rlim_max, rc, errno));
    (void)psh;
    return rc;
}


/* Wrapper for strerror that makes sure it doesn't return NULL and causes the
   caller or fprintf routines to crash. */
const char *sh_strerror(shinstance *psh, int error)
{
    char *err = strerror(error);
    if (!err)
        return "strerror return NULL!";
    (void)psh;
    return err;
}

