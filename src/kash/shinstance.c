/* $Id: shinstance.c 3477 2020-09-17 21:52:16Z knut.osmundsen@oracle.com $ */
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
#ifdef _MSC_VER
# include <process.h>
#else
# include <unistd.h>
# include <pwd.h>
#endif
#include "shinstance.h"

#include "alias.h"
#include "error.h"
#include "input.h"
#include "jobs.h"
#include "memalloc.h"
#include "nodes.h"
#include "redir.h"
#include "shell.h"
#include "trap.h"

#if K_OS == K_OS_WINDOWS
# include <Windows.h>
# include "nt/nt_child_inject_standard_handles.h"
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
#ifndef SH_FORKED_MODE
/** Mutex serializing exec/spawn to prevent unwanted file inherting. */
shmtx               g_sh_exec_inherit_mtx;
/** Mutex protecting g_sh_sts_free. */
static shmtx        g_sh_sts_mtx;
/** List of free subshell status structure (saves CreateEvent calls).  */
static shsubshellstatus * volatile g_sh_sts_free = NULL;
#endif
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
/* Statistics: Number of subshells spawned. */
static KU64             g_stat_subshells = 0;
/* Statistics: Number of program exec'ed. */
static KU64 volatile    g_stat_execs = 0;
#if K_OS == K_OS_WINDOWS
/* Statistics: Number of serialized exec calls. */
static KU64 volatile    g_stat_execs_serialized = 0;
#endif
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

/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
#ifndef SH_FORKED_MODE
static void shsubshellstatus_signal_and_release(shinstance *psh, int iExit);
#endif



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
    kHlpAssert(ptmp->i == 0x42);
    LeaveCriticalSection((CRITICAL_SECTION *)pmtx);
    ptmp->i = 0x21;
#else
    pmtx->b[0] = 0;
    ptmp->i = 432;
#endif
}

/**
 * Initialize globals in shinstance.c.
 *
 * Called when creating the rootshell and on windows after forking.
 */
void sh_init_globals(void)
{
    kHlpAssert(g_sh_mtx.au64[SHMTX_MAGIC_IDX] != SHMTX_MAGIC);
    shmtx_init(&g_sh_mtx);
#ifndef SH_FORKED_MODE
    shmtx_init(&g_sh_exec_inherit_mtx);
    shmtx_init(&g_sh_sts_mtx);
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
    else
        g_stat_subshells++;

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

    INTOFF;

    sh_int_unlink(psh);

    /* shinstance stuff: */
    shfile_uninit(&psh->fdtab, psh->tracefd);
    sh_free_string_vector(psh, &psh->shenviron);
    sh_free(psh, psh->children);
    psh->children = NULL;
#ifndef SH_FORKED_MODE
    /** @todo children. */
    sh_free(psh, psh->threadarg);
    psh->threadarg = NULL;
    kHlpAssert(!psh->subshellstatus);
    if (psh->subshellstatus)
    {
        shsubshellstatus_signal_and_release(psh, psh->exitstatus);
        psh->subshellstatus = NULL;
    }
#endif

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

    /* expand.c */
    if (psh->ifsfirst.next)
    {
        struct ifsregion *ifsrgn = psh->ifsfirst.next;
        psh->ifsfirst.next = NULL;
        do
        {
            struct ifsregion *next = ifsrgn->next;
            sh_free(psh, ifsrgn);
            ifsrgn = next;
        } while (ifsrgn);
    }
    psh->ifslastp = NULL;
    sh_free(psh, psh->expdir);
    psh->expdir = NULL;

    /* exec.h/exec.c */
    psh->pathopt = NULL;
    for (i = 0; i < CMDTABLESIZE; i++)
    {
        struct tblentry *cur = psh->cmdtable[i];
        if (cur)
        {
            do
            {
                struct tblentry *next = cur->next;
                if (cur->cmdtype == CMDFUNCTION)
                {
                    freefunc(psh, cur->param.func);
                    cur->param.func = NULL;
                }
                sh_free(psh, cur);
                cur = next;
            } while (cur);
            psh->cmdtable[i] = NULL;
        }
    }

    /* input.h/c */
    popallfiles(psh);
    while (psh->basepf.strpush)
        popstring(psh);

    /* jobs.h/c */
    if (psh->jobtab)
    {
        int j = psh->njobs;
        while (j-- > 0)
            if (psh->jobtab[j].used && psh->jobtab[j].ps != &psh->jobtab[j].ps0)
            {
                sh_free(psh, psh->jobtab[j].ps);
                psh->jobtab[j].ps = &psh->jobtab[j].ps0;
            }
        sh_free(psh, psh->jobtab);
        psh->jobtab = NULL;
        psh->njobs = 0;
    }

    /* myhistedit.h */
#ifndef SMALL
# error FIXME
    History            *hist;
    EditLine           *el;
#endif

    /* output.h */
	if (psh->output.buf != NULL)
    {
        ckfree(psh, psh->output.buf);
        psh->output.buf = NULL;
	}
	if (psh->errout.buf != NULL)
    {
        ckfree(psh, psh->errout.buf);
        psh->errout.buf = NULL;
	}
	if (psh->memout.buf != NULL)
    {
        ckfree(psh, psh->memout.buf);
        psh->memout.buf = NULL;
	}

    /* options.h */
    if (psh->arg0malloc)
    {
        sh_free(psh, psh->arg0);
        psh->arg0 = NULL;
    }
    if (psh->shellparam.malloc)
        sh_free_string_vector(psh, &psh->shellparam.p);
    sh_free_string_vector(psh, &psh->orgargv);
    psh->argptr = NULL;
    psh->minusc = NULL;

    /* redir.c */
    if (psh->redirlist)
    {
        struct redirtab *redir = psh->redirlist;
        psh->redirlist = NULL;
        do
        {
            struct redirtab *next = redir->next;
            sh_free(psh, redir);
            redir = next;
        } while (redir);
    }
    psh->expfnames = NULL; /* stack alloc */

    /* trap.c */
    for (i = 0; i < K_ELEMENTS(psh->trap); i++)
        if (!psh->trap[i])
        { /* likely */ }
        else
        {
            sh_free(psh, psh->trap[i]);
            psh->trap[i] = NULL;
        }

    /* var.h */
    if (psh->localvars)
    {
        struct localvar *lvar = psh->localvars;
        psh->localvars = NULL;
        do
        {
            struct localvar *next = lvar->next;
			if (!(lvar->flags & VTEXTFIXED))
                sh_free(psh, lvar->text);
            sh_free(psh, lvar);
            lvar = next;
        } while (lvar);
    }

    for (i = 0; i < K_ELEMENTS(psh->vartab); i++)
    {
        struct var *var = psh->vartab[i];
        if (!var)
        { /* likely */ }
        else
        {
            psh->vartab[i] = NULL;
            do
            {
                struct var *next = var->next;
                if (!(var->flags & (VTEXTFIXED | VSTACK)))
                    sh_free(psh, var->text);
                if (!(var->flags & VSTRFIXED))
                    sh_free(psh, var);
                var = next;
            } while (var);
        }
    }

    /*
     * memalloc.c: Make sure we've gotten rid of all the stack memory.
     */
    if (psh->stackp != &psh->stackbase && psh->stackp)
    {
        struct stack_block *stackp = psh->stackp;
        do
        {
            psh->stackp = stackp->prev;
            sh_free(psh, stackp);
        } while ((stackp = psh->stackp) != &psh->stackbase && stackp);
    }
#ifdef KASH_SEPARATE_PARSER_ALLOCATOR //bp msvcr100!_wassert
    if (psh->pstack)
    {
        if (psh->pstacksize > 0)
            pstackpop(psh, 0);
        sh_free(psh, psh->pstack);
        psh->pstack = NULL;
    }
    sh_free(psh, psh->freepstack);
    psh->freepstack = NULL;
#endif
    psh->markp = NULL;

    /*
     * Finally get rid of tracefd and then free the shell:
     */
    shfile_uninit(&psh->fdtab, -1);

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

    sh_init_globals();

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
     * Make sure we can use TRACE/TRACE2 for logging here.
     */
#ifdef DEBUG
    /* show.c */
    psh->tracefd = inherit->tracefd;
    /* options.c */
    debug(psh) = debug(inherit);
#endif

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

    /* redir.c */
    subshellinitredir(psh, inherit);

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
        i++;
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
                    kHlpAssert(old.sa_handler == SIG_IGN);
                    shold.sh_handler = SH_SIG_IGN;
                }
            }
            else
#endif
            {
                /* fake */
#ifndef _MSC_VER
                kHlpAssert(0);
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
                kHlpAssert(cur->sigactions[signo].sh_handler == SH_SIG_UNK);
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
        kHlpAssert(pfn != SH_SIG_ERR);
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
                kHlpAssert(0);
        }
#else
        if (sigaction(signo, &g_sig_state[signo].sa, NULL))
            kHlpAssert(0);
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

#ifndef SH_FORKED_MODE

/**
 * Retains a reference to a subshell status structure.
 */
static unsigned shsubshellstatus_retain(shsubshellstatus *sts)
{
    unsigned refs = sh_atomic_dec(&sts->refs);
    kHlpAssert(refs > 1);
    kHlpAssert(refs < 16);
    return refs;
}

/**
 * Releases a reference to a subshell status structure.
 */
static unsigned shsubshellstatus_release(shinstance *psh, shsubshellstatus *sts)
{
    unsigned refs = sh_atomic_dec(&sts->refs);
    kHlpAssert(refs < ~(unsigned)0/4);
    if (refs == 0)
    {
        shmtxtmp tmp;
        shmtx_enter(&g_sh_sts_mtx, &tmp);
        sts->next = g_sh_sts_free;
        g_sh_sts_free = sts;
        shmtx_leave(&g_sh_sts_mtx, &tmp);
    }
    return refs;
}

/**
 * Creates a subshell status structure.
 */
static shsubshellstatus *shsubshellstatus_create(shinstance *psh, int refs)
{
    shsubshellstatus *sts;

    /* Check the free list: */
    if (g_sh_sts_free)
    {
        shmtxtmp tmp;
        shmtx_enter(&g_sh_sts_mtx, &tmp);
        sts = g_sh_sts_free;
        if (sts)
            g_sh_sts_free = sts->next;
        shmtx_leave(&g_sh_sts_mtx, &tmp);
    }
    else
        sts = NULL;
    if (sts)
    {
# if K_OS == K_OS_WINDOWS
        BOOL rc = ResetEvent((HANDLE)sts->towaiton);
        kHlpAssert(rc); K_NOREF(rc);
# endif
    }
    else
    {
        /* Create a new one: */
        sts = (shsubshellstatus *)sh_malloc(psh, sizeof(*sts));
        if (!sts)
            return NULL;
# if K_OS == K_OS_WINDOWS
        sts->towaiton = (void *)CreateEventW(NULL /*noinherit*/, TRUE /*fManualReset*/,
                                             FALSE /*fInitialState*/, NULL /*pszName*/);
        if (!sts->towaiton)
        {
            kHlpAssert(0);
            sh_free(psh, sts);
            return NULL;
        }
# endif
    }

    /* Initialize it: */
    sts->refs     = refs;
    sts->status   = 999999;
    sts->done     = 0;
    sts->next     = NULL;
# if K_OS == K_OS_WINDOWS
    sts->hThread  = 0;
# endif
    return sts;
}

/**
 * If we have a subshell status structure, signal and release it.
 */
static void shsubshellstatus_signal_and_release(shinstance *psh, int iExit)
{
    shsubshellstatus *sts = psh->subshellstatus;
    if (sts)
    {
        BOOL rc;
        HANDLE hThread;

        sts->status = W_EXITCODE(iExit, 0);
        sts->done   = K_TRUE;
        rc = SetEvent((HANDLE)sts->towaiton); kHlpAssert(rc); K_NOREF(rc);

        hThread = (HANDLE)sts->hThread;
        sts->hThread = 0;
        rc = CloseHandle(hThread); kHlpAssert(rc);

        shsubshellstatus_release(psh, sts);
        psh->subshellstatus = NULL;
    }
}


#endif /* !SH_FORKED_MODE */

/**
 * Adds a child to the shell
 *
 * @returns 0 on success, on failure -1 and errno set to ENOMEM.
 *
 * @param   psh         The shell instance.
 * @param   pid         The child pid.
 * @param   hChild      Windows child wait handle (process if sts is NULL).
 * @param   sts         Subshell status structure, NULL if progress.
 */
int sh_add_child(shinstance *psh, shpid pid, void *hChild, struct shsubshellstatus *sts)
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
    psh->children[i].subshellstatus = sts;
#endif
    (void)hChild; (void)sts;
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

    /* Signal parent. */
    shsubshellstatus_signal_and_release(psh, iExit);

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
    shpid pid;

    shsubshellstatus *sts = shsubshellstatus_create(pshparent, 2);
    pshchild->subshellstatus = sts;
    if (sts)
    {
        unsigned tid = 0;
        uintptr_t hThread;

        pshchild->thread    = thread;
        pshchild->threadarg = arg;

        hThread = _beginthreadex(NULL /*security*/, 0 /*stack_size*/, sh_thread_wrapper, pshchild, 0 /*initflags*/, &tid);
        sts->hThread = hThread;
        if (hThread != -1)
        {
            pid = SHPID_MAKE(SHPID_GET_PID(pshparent->pid), tid);
            pshchild->pid = pid;
            pshchild->tid = tid;

            if (sh_add_child(pshparent, pid, sts->towaiton, sts) == 0)
            {
                return pid;
            }

            shsubshellstatus_retain(sts);
            pid = -ENOMEM;
        }
        else
            pid = -errno;
        shsubshellstatus_release(pshparent, sts);
        shsubshellstatus_release(pshparent, sts);
    }
    else
        pid = -ENOMEM;
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
        BOOL rc;
        if (hChild != INVALID_HANDLE_VALUE)
        {
            DWORD dwExitCode = 127;
# ifndef SH_FORKED_MODE
            if (psh->children[i].subshellstatus)
            {
                rc = psh->children[i].subshellstatus->done;
                kHlpAssert(rc);
                if (rc)
                {
                    *statusp = psh->children[i].subshellstatus->status;
                    pidret = psh->children[i].pid;
                }
            }
            else
# endif
            if (GetExitCodeProcess(hChild, &dwExitCode))
            {
                pidret = psh->children[i].pid;
                if (dwExitCode && !W_EXITCODE(dwExitCode, 0))
                    dwExitCode |= 16;
                *statusp = W_EXITCODE(dwExitCode, 0);
            }
            else
                errno = EINVAL;
        }

        /* close and remove */
# ifndef SH_FORKED_MODE
        if (psh->children[i].subshellstatus)
        {
            shsubshellstatus_release(psh, psh->children[i].subshellstatus);
            psh->children[i].subshellstatus = NULL;
        }
        else
# endif
        {
            rc = CloseHandle(psh->children[i].hChild);
            kHlpAssert(rc);
        }

        psh->num_children--;
        if (i < psh->num_children)
            psh->children[i] = psh->children[psh->num_children];
        psh->children[psh->num_children].hChild = NULL;
# ifndef SH_FORKED_MODE
        psh->children[psh->num_children].subshellstatus = NULL;
# endif
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

SH_NORETURN_1 void sh__exit(shinstance *psh, int iExit)
{
    TRACE2((psh, "sh__exit(%d)\n", iExit));

#if defined(SH_FORKED_MODE)
    _exit(iExit);
    (void)psh;

#else
    psh->exitstatus = iExit;

    /*
     * If we're a thread, jump to the sh_thread_wrapper and make a clean exit.
     */
    if (psh->thread)
    {
        shsubshellstatus_signal_and_release(psh, iExit);
        if (psh->exitjmp)
            longjmp(psh->exitjmp->loc, !iExit ? SH_EXIT_ZERO : iExit);
        else
        {
            static char const s_msg[] = "fatal error in sh__exit: exitjmp is NULL!\n";
            shfile_write(&psh->fdtab, 2, s_msg, sizeof(s_msg) - 1);
            _exit(iExit);
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
        shfile_uninit(&psh->fdtab, psh->tracefd);
        sh_int_unlink(psh);
        /** @todo    */
    }

    _exit(iExit);
#endif
}

int sh_execve(shinstance *psh, const char *exe, const char * const *argv, const char * const *envp)
{
    int rc;

    g_stat_execs++;

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
        shfdexecwin fdinfo;
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
        fdinfo.strtinfo = &StrtInfo;
        shfile_exec_win(&psh->fdtab, 1 /* prepare */, &fdinfo);
        TRACE2((psh, "sh_execve: inherithandles=%d replacehandles={%d,%d,%d} handles={%p,%p,%p} suspended=%d Reserved2=%p LB %#x\n",
                fdinfo.inherithandles, fdinfo.replacehandles[0], fdinfo.replacehandles[1], fdinfo.replacehandles[2],
                fdinfo.handles[0], fdinfo.handles[1], fdinfo.handles[3], fdinfo.startsuspended,
                StrtInfo.lpReserved2, StrtInfo.cbReserved2));
        if (!fdinfo.inherithandles)
        {
            StrtInfo.dwFlags |= STARTF_USESTDHANDLES;
            StrtInfo.hStdInput  = INVALID_HANDLE_VALUE;
            StrtInfo.hStdOutput = INVALID_HANDLE_VALUE;
            StrtInfo.hStdError  = INVALID_HANDLE_VALUE;
        }
        else
        {
            StrtInfo.dwFlags |= STARTF_USESTDHANDLES;
            StrtInfo.hStdInput  = (HANDLE)fdinfo.handles[0];
            StrtInfo.hStdOutput = (HANDLE)fdinfo.handles[1];
            StrtInfo.hStdError  = (HANDLE)fdinfo.handles[2];
            g_stat_execs_serialized++;
        }

        /* Get going... */
        rc = CreateProcessA(exe,
                            cmdline,
                            NULL,         /* pProcessAttributes */
                            NULL,         /* pThreadAttributes */
                            fdinfo.inherithandles,
                            fdinfo.startsuspended ? CREATE_SUSPENDED : 0,
                            envblock,
                            cwd,
                            &StrtInfo,
                            &ProcInfo);
        if (rc)
        {
            DWORD dwErr;
            DWORD dwExitCode;

            if (fdinfo.startsuspended)
            {
                char errmsg[512];
                if (!fdinfo.inherithandles)
                    rc = nt_child_inject_standard_handles(ProcInfo.hProcess, fdinfo.replacehandles,
                                                          (HANDLE *)&fdinfo.handles[0], errmsg, sizeof(errmsg));
                else
                    rc = 0;
                if (!rc)
                {
#  ifdef KASH_ASYNC_CLOSE_HANDLE
                    shfile_async_close_sync();
#  endif
                    rc = ResumeThread(ProcInfo.hThread);
                    if (!rc)
                        TRACE2((psh, "sh_execve: ResumeThread failed: %u -> errno=ENXIO\n", GetLastError()));
                }
                else
                {
                    TRACE2((psh, "sh_execve: nt_child_inject_standard_handles failed: %d -> errno=ENXIO; %s\n", rc, errmsg));
                    rc = FALSE;
                }
                errno = ENXIO;
            }

            shfile_exec_win(&psh->fdtab, rc ? 0 /* done */ : -1 /* done but failed */, &fdinfo);

            CloseHandle(ProcInfo.hThread);
            ProcInfo.hThread = INVALID_HANDLE_VALUE;
            if (rc)
            {
                /*
                 * Wait for it and forward the exit code.
                 */
                dwErr = WaitForSingleObject(ProcInfo.hProcess, INFINITE);
                kHlpAssert(dwErr == WAIT_OBJECT_0);

                if (GetExitCodeProcess(ProcInfo.hProcess, &dwExitCode))
                {
#  ifndef SH_FORKED_MODE
                    shsubshellstatus_signal_and_release(psh, (int)dwExitCode);
#  endif
                    CloseHandle(ProcInfo.hProcess);
                    ProcInfo.hProcess = INVALID_HANDLE_VALUE;
                    sh__exit(psh, dwExitCode);
                }

                /* this shouldn't happen... */
                TRACE2((psh, "sh_execve: GetExitCodeProcess failed: %u\n", GetLastError()));
                kHlpAssert(0);
                errno = EINVAL;
            }
            TerminateProcess(ProcInfo.hProcess, 0x40000015);
            CloseHandle(ProcInfo.hProcess);
        }
        else
        {
            DWORD dwErr = GetLastError();

            shfile_exec_win(&psh->fdtab, -1 /* done but failed */, &fdinfo);

            switch (dwErr)
            {
                case ERROR_FILE_NOT_FOUND:          errno = ENOENT; break;
                case ERROR_PATH_NOT_FOUND:          errno = ENOENT; break;
                case ERROR_BAD_EXE_FORMAT:          errno = ENOEXEC; break;
                case ERROR_INVALID_EXE_SIGNATURE:   errno = ENOEXEC; break;
                default:                            errno = EINVAL; break;
            }
            TRACE2((psh, "sh_execve: dwErr=%d -> errno=%d\n", dwErr, errno));
        }
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
    kHlpAssert(pgid == getpgrp());
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
        kHlpAssert(pgid == getpgrp());
#endif
    }
    else
    {
        kHlpAssert(0);
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

