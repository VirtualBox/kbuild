/* $Id: shinstance.c 2286 2009-02-25 05:25:38Z knut.osmundsen@oracle.com $ */
/** @file
 *
 * The shell instance methods.
 *
 * Copyright (c) 2007-2009  knut st. osmundsen <bird-kBuild-spamix@anduin.net>
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

/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
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


/*******************************************************************************
*   Global Variables                                                           *
*******************************************************************************/
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



typedef struct shmtxtmp { int i; } shmtxtmp;

int shmtx_init(shmtx *pmtx)
{
    pmtx->b[0] = 0;
    return 0;
}

void shmtx_delete(shmtx *pmtx)
{
    pmtx->b[0] = 0;
}

void shmtx_enter(shmtx *pmtx, shmtxtmp *ptmp)
{
    pmtx->b[0] = 0;
    ptmp->i = 0;
}

void shmtx_leave(shmtx *pmtx, shmtxtmp *ptmp)
{
    pmtx->b[0] = 0;
    ptmp->i = 432;
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

    shmtx_leave(&g_sh_mtx, &tmp);
}

#if 0
/**
 * Unlink the shell instance.
 *
 * @param   psh     The shell.
 */
static void sh_int_unlink(shinstance *psh)
{
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
        g_sh_root = 0;

    shmtx_leave(&g_sh_mtx, &tmp);
}
#endif

/**
 * Clones an environment vector.
 *
 * @returns 0 on success, -1 and errno on failure.
 * @param   dstp    Where to store the clone.
 * @param   src     The vector to be cloned.
 */
static int sh_env_clone(char ***dstp, char **src)
{
   char **dst;
   size_t items;

   /* count first */
   items = 0;
   while (src[items])
       items++;

   /* alloc clone array. */
   *dstp = dst = malloc(sizeof(*dst) * items + 1);
   if (!dst)
       return -1;

   /* copy the items */
   dst[items] = NULL;
   while (items-- > 0)
   {
       dst[items] = strdup(src[items]);
       if (!dst[items])
       {
           /* allocation error, clean up. */
           while (dst[++items])
               free(dst[items]);
           free(dst);
           errno = ENOMEM;
           return -1;
       }
   }

   return 0;
}

/**
 * Creates a root shell instance.
 *
 * @param   inherit     The shell to inherit from. If NULL inherit from environment and such.
 * @param   argc        The argument count.
 * @param   argv        The argument vector.
 * @param   envp        The environment vector.
 *
 * @returns pointer to root shell on success, NULL on failure.
 */
shinstance *sh_create_root_shell(shinstance *inherit, int argc, char **argv, char **envp)
{
    shinstance *psh;
    int i;

    /*
     * The allocations.
     */
    psh = calloc(sizeof(*psh), 1);
    if (psh)
    {
        if (!sh_env_clone(&psh->shenviron, envp))
        {
            /* the special stuff. */
#ifdef _MSC_VER
            psh->pid = _getpid();
#else
            psh->pid = getpid();
#endif
            /*sh_sigemptyset(&psh->sigrestartset);*/
            for (i = 0; i < NSIG; i++)
                psh->sigactions[i].sh_handler = SH_SIG_UNK;
            if (inherit)
                psh->sigmask = psh->sigmask;
            else
            {
#if defined(SH_PURE_STUB_MODE) || defined(_MSC_VER)
                sh_sigemptyset(&psh->sigmask);
#else
                sigprocmask(SIG_SETMASK, NULL, &psh->sigmask);
#endif
            }

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

            /* link it. */
            sh_int_link(psh);
            return psh;
        }

        free(psh);
    }
    return NULL;
}

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

    fprintf(stderr, "sh_sig_common_handler: signo=%d:%s\n", signo, sys_signame[signo]);

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

#ifdef SH_PURE_STUB_MODE
    return -1;
#else

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
# ifdef _MSC_VER
        if (signal(signo, g_sig_state[signo].sa.sa_handler) == SIG_ERR)
# else
        if (sigaction(signo, &g_sig_state[signo].sa, NULL))
# endif
            assert(0);

        shmtx_leave(&g_sh_mtx, &tmp);
    }

    return 0;
#endif
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

int sh_sigismember(shsigset_t *setp, int signo)
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

#if (defined(SH_STUB_MODE) || defined(SH_FORKED_MODE)) && !defined(_MSC_VER)
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
                        sh_sigaddset(&mask);
                break;
            case SIG_UNBLOCK:
                for (rc = 0; rc < NSIG; rc++)
                    if (sh_sigismember(newp, rc))
                        sh_sigdelset(&mask);
                break;
            case SIG_SETMASK:
                mask = *newp;
                break;
        }

# if defined(SH_STUB_MODE) || defined(_MSC_VER)
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

void sh_abort(shinstance *psh)
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

int sh_kill(shinstance *psh, pid_t pid, int signo)
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
            TRACE2((psh, "sh_kill(%d, %d): pshDst=%p\n", pid, signo, pshDst));
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
#ifdef SH_PURE_STUB_MODE
    rc = -1;

#elif defined(SH_STUB_MODE) || defined(SH_FORKED_MODE)
# ifdef _MSC_VER
    errno = ENOSYS;
    rc = -1;
# else
    fprintf(stderr, "kill(%d, %d)\n", pid, signo);
    rc = kill(pid, signo);
# endif

#else
#endif

    TRACE2((psh, "sh_kill(%d, %d) -> %d [%d]\n", pid, signo, rc, errno));
    return rc;
}

int sh_killpg(shinstance *psh, pid_t pgid, int signo)
{
    int rc;

#ifdef SH_PURE_STUB_MODE
    rc = -1;

#elif defined(SH_STUB_MODE)
# ifdef _MSC_VER
    errno = ENOSYS;
    rc = -1;
# else
    //fprintf(stderr, "killpg(%d, %d)\n", pgid, signo);
    rc = killpg(pgid, signo);
# endif

#else
#endif

    TRACE2((psh, "sh_killpg(%d, %d) -> %d [%d]\n", pgid, signo, rc, errno));
    (void)psh;
    return rc;
}

clock_t sh_times(shinstance *psh, shtms *tmsp)
{
#ifdef SH_PURE_STUB_MODE
    return 0;

#elif defined(SH_STUB_MODE) || defined(SH_FORKED_MODE)
    (void)psh;
# ifdef _MSC_VER
    errno = ENOSYS;
    return (clock_t)-1;
# else
    return times(tmsp);
# endif

#else
#endif
}

int sh_sysconf_clk_tck(void)
{
#ifdef SH_PURE_STUB_MODE
    return 1;
#else
# ifdef _MSC_VER
    return CLK_TCK;
# else
    return sysconf(_SC_CLK_TCK);
# endif
#endif
}

pid_t sh_fork(shinstance *psh)
{
    pid_t pid;
    TRACE2((psh, "sh_fork\n"));

#ifdef SH_PURE_STUB_MODE
    pid = -1;

#elif defined(SH_STUB_MODE) || defined(SH_FORKED_MODE)
# ifdef _MSC_VER
#  ifdef SH_FORKED_MODE
    /** @todo */
    *(char *)1 = 0x1;
#  else
    pid = -1;
    error = ENOSYS;
#  endif
# else
    pid = fork();
# endif

#else

#endif

    TRACE2((psh, "sh_fork -> %d [%d]\n", pid, errno));
    (void)psh;
    return pid;
}

pid_t sh_waitpid(shinstance *psh, pid_t pid, int *statusp, int flags)
{
    pid_t pidret;

    *statusp = 0;
#ifdef SH_PURE_STUB_MODE
    pidret = -1;

#elif defined(SH_STUB_MODE) || defined(SH_FORKED_MODE)
# ifdef _MSC_VER
    pidret = -1;
    error = ENOSYS;
# else
    pidret = waitpid(pid, statusp, flags);
# endif

#else
#endif

    TRACE2((psh, "waitpid(%d, %p, %#x) -> %d [%d] *statusp=%#x (rc=%d)\n", pid, statusp, flags,
            pidret, errno, *statusp, WEXITSTATUS(*statusp)));
    (void)psh;
    return pidret;
}

void sh__exit(shinstance *psh, int rc)
{
    TRACE2((psh, "sh__exit(%d)\n", rc));
    (void)psh;

#ifdef SH_PURE_STUB_MODE
    return -1;

#elif defined(SH_STUB_MODE) || defined(SH_FORKED_MODE)
    _exit(rc);

#else
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
        envp = sh_environ(psh);

#ifdef SH_PURE_STUB_MODE
    rc = -1;

#elif defined(SH_STUB_MODE) || defined(SH_FORKED_MODE)
# ifdef _MSC_VER
    rc = _spawnve(_P_WAIT, exe, (char **)argv, (char **)envp);
# else
    rc = execve(exe, (char **)argv, (char **)envp);
# endif

#else
#endif

    TRACE2((psh, "sh_execve -> %d [%d]\n", rc, errno));
    (void)psh;
    return rc;
}

uid_t sh_getuid(shinstance *psh)
{
#ifdef SH_PURE_STUB_MODE
    uid_t uid = 0;

#elif defined(SH_STUB_MODE) || defined(SH_FORKED_MODE)
# ifdef _MSC_VER
    uid_t uid = 0;
# else
    uid_t uid = getuid();
# endif

#else
#endif

    TRACE2((psh, "sh_getuid() -> %d [%d]\n", uid, errno));
    (void)psh;
    return uid;
}

uid_t sh_geteuid(shinstance *psh)
{
#ifdef SH_PURE_STUB_MODE
    uid_t euid = 0;

#elif defined(SH_STUB_MODE) || defined(SH_FORKED_MODE)
# ifdef _MSC_VER
    uid_t euid = 0;
# else
    uid_t euid = geteuid();
# endif

#else
#endif

    TRACE2((psh, "sh_geteuid() -> %d [%d]\n", euid, errno));
    (void)psh;
    return euid;
}

gid_t sh_getgid(shinstance *psh)
{
#ifdef SH_PURE_STUB_MODE
    gid_t gid = 0;

#elif defined(SH_STUB_MODE) || defined(SH_FORKED_MODE)
# ifdef _MSC_VER
    gid_t gid = 0;
# else
    gid_t gid = getgid();
# endif

#else
#endif

    TRACE2((psh, "sh_getgid() -> %d [%d]\n", gid, errno));
    (void)psh;
    return gid;
}

gid_t sh_getegid(shinstance *psh)
{
#ifdef SH_PURE_STUB_MODE
    gid_t egid = 0;

#elif defined(SH_STUB_MODE) || defined(SH_FORKED_MODE)
# ifdef _MSC_VER
    gid_t egid = 0;
# else
    gid_t egid = getegid();
# endif

#else
#endif

    TRACE2((psh, "sh_getegid() -> %d [%d]\n", egid, errno));
    (void)psh;
    return egid;
}

pid_t sh_getpid(shinstance *psh)
{
    pid_t pid;

#ifdef SH_PURE_STUB_MODE
    pid = 0;

#elif defined(SH_STUB_MODE) || defined(SH_FORKED_MODE)
# ifdef _MSC_VER
    pid = _getpid();
# else
    pid = getpid();
# endif
#else
#endif

    (void)psh;
    return pid;
}

pid_t sh_getpgrp(shinstance *psh)
{
#ifdef SH_PURE_STUB_MODE
    pid_t pgrp = 0;

#elif defined(SH_STUB_MODE) || defined(SH_FORKED_MODE)
# ifdef _MSC_VER
    pid_t pgrp _getpid();
# else
    pid_t pgrp = getpgrp();
# endif

#else
#endif

    TRACE2((psh, "sh_getpgrp() -> %d [%d]\n", pgrp, errno));
    (void)psh;
    return pgrp;
}

pid_t sh_getpgid(shinstance *psh, pid_t pid)
{
#ifdef SH_PURE_STUB_MODE
    pid_t pgid = pid;

#elif defined(SH_STUB_MODE) || defined(SH_FORKED_MODE)
# ifdef _MSC_VER
    pid_t pgid = pid;
# else
    pid_t pgid = getpgid(pid);
# endif

#else
#endif

    TRACE2((psh, "sh_getpgid(%d) -> %d [%d]\n", pid, pgid, errno));
    (void)psh;
    return pgid;
}

int sh_setpgid(shinstance *psh, pid_t pid, pid_t pgid)
{
#ifdef SH_PURE_STUB_MODE
    int rc = -1;

#elif defined(SH_STUB_MODE) || defined(SH_FORKED_MODE)
# ifdef _MSC_VER
    int rc = -1;
    error = ENOSYS;
# else
    int rc = setpgid(pid, pgid);
# endif

#else
#endif

    TRACE2((psh, "sh_setpgid(%d, %d) -> %d [%d]\n", pid, pgid, rc, errno));
    (void)psh;
    return rc;
}

pid_t sh_tcgetpgrp(shinstance *psh, int fd)
{
    pid_t pgrp;

#ifdef SH_PURE_STUB_MODE
    pgrp = -1;

#elif defined(SH_STUB_MODE) || defined(SH_FORKED_MODE)
# ifdef _MSC_VER
    pgrp = -1;
    error = ENOSYS;
# else
    pgrp = tcgetpgrp(fd);
# endif

#else
#endif

    TRACE2((psh, "sh_tcgetpgrp(%d) -> %d [%d]\n", fd, pgrp, errno));
    (void)psh;
    return pgrp;
}

int sh_tcsetpgrp(shinstance *psh, int fd, pid_t pgrp)
{
    int rc;
    TRACE2((psh, "sh_tcsetpgrp(%d, %d)\n", fd, pgrp));

#ifdef SH_PURE_STUB_MODE
    rc = -1;

#elif defined(SH_STUB_MODE) || defined(SH_FORKED_MODE)
# ifdef _MSC_VER
    rc = -1;
    error = ENOSYS;
# else
    rc = tcsetpgrp(fd, pgrp);
# endif

#else
#endif

    TRACE2((psh, "sh_tcsetpgrp(%d, %d) -> %d [%d]\n", fd, pgrp, rc, errno));
    (void)psh;
    return rc;
}

int sh_getrlimit(shinstance *psh, int resid, shrlimit *limp)
{
#ifdef SH_PURE_STUB_MODE
    int rc = -1;

#elif defined(SH_STUB_MODE) || defined(SH_FORKED_MODE)
# ifdef _MSC_VER
    int rc = -1;
    error = ENOSYS;
# else
    int rc = getrlimit(resid, limp);
# endif

#else
    /* returned the stored limit */
#endif

    TRACE2((psh, "sh_getrlimit(%d, %p) -> %d [%d] {%ld,%ld}\n",
            resid, limp, rc, errno, (long)limp->rlim_cur, (long)limp->rlim_max));
    (void)psh;
    return rc;
}

int sh_setrlimit(shinstance *psh, int resid, const shrlimit *limp)
{
#ifdef SH_PURE_STUB_MODE
    int rc = -1;

#elif defined(SH_STUB_MODE) || defined(SH_FORKED_MODE)
# ifdef _MSC_VER
    int rc = -1;
    error = ENOSYS;
# else
    int rc = setrlimit(resid, limp);
# endif

#else
    /* if max(shell) < limp; then setrlimit; fi
       if success; then store limit for later retrival and maxing. */

#endif

    TRACE2((psh, "sh_setrlimit(%d, %p:{%ld,%ld}) -> %d [%d]\n",
            resid, limp, (long)limp->rlim_cur, (long)limp->rlim_max, rc, errno));
    (void)psh;
    return rc;
}

