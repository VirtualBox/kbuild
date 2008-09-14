/* $Id: shinstance.c 1785 2008-09-14 17:31:16Z knut.osmundsen@oracle.com $ */
/** @file
 *
 * The shell instance methods.
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

/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#ifndef _MSC_VER
# include <unistd.h>
# include <pwd.h>
extern char **environ;
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
static int          g_num_shells;
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


/**
 * Creates a root shell instance.
 *
 * @param   inherit     The shell to inherit from. If NULL inherit from environment and such.
 * @param   argc        The argument count.
 * @param   argv        The argument vector.
 *
 * @returns pointer to root shell on success, NULL on failure.
 */
shinstance *sh_create_root_shell(shinstance *inherit, int argc, char **argv)
{
    shinstance *psh;
    int i;

    psh = calloc(sizeof(*psh), 1);
    if (psh)
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

    }
    return psh;
}


char *sh_getenv(shinstance *psh, const char *var)
{
#ifdef SH_PURE_STUB_MODE
    return NULL;
#elif defined(SH_STUB_MODE)
    (void)psh;
    return getenv(var);
#else
#endif
}

char **sh_environ(shinstance *psh)
{
#ifdef SH_PURE_STUB_MODE
    static char *s_null[2] = {0,0};
    return &s_null[0];
#elif defined(SH_STUB_MODE)
    (void)psh;
    return environ;
#else
#endif
}

const char *sh_gethomedir(shinstance *psh, const char *user)
{
#ifdef SH_PURE_STUB_MODE
    return NULL;
#elif defined(SH_STUB_MODE)
    (void)psh;
# ifdef _MSC_VER
    return NULL;
# else
    struct passwd *pwd = getpwnam(user);
    return pwd ? pwd->pw_dir : NULL;
# endif
#else
#endif
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
 * Handler for external signals.
 *
 * @param   signo       The signal.
 */
static void sh_sig_common_handler(int signo)
{
    fprintf(stderr, "sh_sig_common_handler: signo=%d:%s\n", signo, sys_signame[signo]);
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
#ifdef SH_PURE_STUB_MODE
    return -1;
#elif defined(SH_STUB_MODE)
    (void)psh;
# ifdef _MSC_VER
    return -1;
# else
    return sigprocmask(operation, newp, oldp);
# endif
#else
#endif
}

void sh_abort(shinstance *psh)
{
    TRACE2((psh, "sh_abort\n"));

#ifdef SH_PURE_STUB_MODE
#elif defined(SH_STUB_MODE)
    abort();
#else
#endif

    TRACE2((psh, "sh_abort returns!\n"));
    (void)psh;
}

void sh_raise_sigint(shinstance *psh)
{
    TRACE2((psh, "sh_raise(SIGINT)\n"));

#ifdef SH_PURE_STUB_MODE
#elif defined(SH_STUB_MODE)
    (void)psh;
    raise(SIGINT);
#else
#endif

    TRACE2((psh, "sh_raise(SIGINT) returns\n"));
    (void)psh;
}

int sh_kill(shinstance *psh, pid_t pid, int signo)
{
    int rc;

#ifdef SH_PURE_STUB_MODE
    rc = -1;
#elif defined(SH_STUB_MODE)
# ifdef _MSC_VER
    rc = -1;
# else
    //fprintf(stderr, "kill(%d, %d)\n", pid, signo);
    rc = kill(pid, signo);
# endif
#else
#endif

    TRACE2((psh, "sh_kill(%d, %d) -> %d [%d]\n", pid, signo, rc, errno));
    (void)psh;
    return rc;
}

int sh_killpg(shinstance *psh, pid_t pgid, int signo)
{
    int rc;

#ifdef SH_PURE_STUB_MODE
    rc = -1;
#elif defined(SH_STUB_MODE)
# ifdef _MSC_VER
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
#elif defined(SH_STUB_MODE)
    (void)psh;
# ifdef _MSC_VER
    return 0;
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
#elif defined(SH_STUB_MODE)
# ifdef _MSC_VER
    pid = -1;
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
#elif defined(SH_STUB_MODE)
# ifdef _MSC_VER
    pidret = -1;
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
#elif defined(SH_STUB_MODE)
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

#ifdef SH_PURE_STUB_MODE
    rc = -1;
#elif defined(SH_STUB_MODE)
# ifdef _MSC_VER
    rc = -1;
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
#elif defined(SH_STUB_MODE)
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
#elif defined(SH_STUB_MODE)
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
#elif defined(SH_STUB_MODE)
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
#elif defined(SH_STUB_MODE)
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
#elif defined(SH_STUB_MODE)
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
#elif defined(SH_STUB_MODE)
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
#elif defined(SH_STUB_MODE)
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
#elif defined(SH_STUB_MODE)
# ifdef _MSC_VER
    int rc = -1;
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
#elif defined(SH_STUB_MODE)
# ifdef _MSC_VER
    pgrp = -1;
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
#elif defined(SH_STUB_MODE)
# ifdef _MSC_VER
    rc = -1;
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
#elif defined(SH_STUB_MODE)
# ifdef _MSC_VER
    int rc = -1;
# else
    int rc = getrlimit(resid, limp);
# endif
#else
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
#elif defined(SH_STUB_MODE)
# ifdef _MSC_VER
    int rc = -1;
# else
    int rc = setrlimit(resid, limp);
# endif
#else
#endif

    TRACE2((psh, "sh_setrlimit(%d, %p:{%ld,%ld}) -> %d [%d]\n",
            resid, limp, (long)limp->rlim_cur, (long)limp->rlim_max, rc, errno));
    (void)psh;
    return rc;
}

