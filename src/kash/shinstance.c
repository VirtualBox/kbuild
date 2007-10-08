/* $Id: $ */
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

#include <string.h>
#include "shinstance.h"


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

    psh = calloc(sizeof(*psh), 1);
    if (psh)
    {
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
#endif
        psh->ttyfd = -1;

    }
    return psh;
}


char *sh_getenv(shinstance *psh, const char *var)
{
#ifdef SH_PURE_STUB_MODE
    return NULL;
#elif defined(SH_STUB_MODE)
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
    return environ;
#else
#endif
}

const char *sh_gethomedir(shinstance *psh, const char *user)
{
#ifdef SH_PURE_STUB_MODE
    return NULL;
#elif defined(SH_STUB_MODE)
# ifdef _MSC_VER
    return NULL;
# else
    struct passwd *pwd = getpwnam(user);
    return pwd ? pwd->pw_dir;
# endif
#else
#endif
}

int sh_sigaction(int signo, const struct sh_sigaction *newp, struct sh_sigaction *oldp)
{
#ifdef SH_PURE_STUB_MODE
    return -1;
#elif defined(SH_STUB_MODE)
# ifdef _MSC_VER
    return -1;
# else
    struct sigaction old;
    if (newp)
        return -1;
    if (sigaction(signo, NULL, &old))
        return -1;
    oldp->sh_flags = old.sa_flags;
    oldp->sh_handler = old.sa_handler;
    oldp->sh_mask = old.sa_mask;
    return 0;
# endif
#else
#endif
}

sh_sig_t sh_signal(shinstance *psh, int signo, sh_sig_t handler)
{
    return (sh_sig_t)-1;
}

int sh_siginterrupt(shinstance *psh, int signo, int interrupt)
{
    return -1;
}

void sh_sigemptyset(sh_sigset_t *setp)
{
    memset(setp, 0, sizeof(*setp));
}

int sh_sigprocmask(shinstance *psh, int operation, sh_sigset_t const *newp, sh_sigset_t *oldp)
{
    return -1;
}

void sh_abort(shinstance *psh)
{
#ifdef SH_PURE_STUB_MODE
#elif defined(SH_STUB_MODE)
    abort();
#else
#endif
}

void sh_raise_sigint(shinstance *psh)
{
#ifdef SH_PURE_STUB_MODE
#elif defined(SH_STUB_MODE)
    raise(SIGINT);
#else
#endif
}

int sh_kill(shinstance *psh, pid_t pid, int signo)
{
#ifdef SH_PURE_STUB_MODE
    return -1;
#elif defined(SH_STUB_MODE)
# ifdef _MSC_VER
    return -1;
# else
    return kill(pid, signo);
# endif
#else
#endif
}

int sh_killpg(shinstance *psh, pid_t pgid, int signo)
{
#ifdef SH_PURE_STUB_MODE
    return -1;
#elif defined(SH_STUB_MODE)
# ifdef _MSC_VER
    return -1;
# else
    return killpg(pgid, signo);
# endif
#else
#endif
}

clock_t sh_times(shinstance *psh, shtms *tmsp)
{
#ifdef SH_PURE_STUB_MODE
    return 0;
#elif defined(SH_STUB_MODE)
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
    return sysconf(_SC_CLK_TCK)
# endif
#endif
}

pid_t sh_fork(shinstance *psh)
{
#ifdef SH_PURE_STUB_MODE
    return -1;
#elif defined(SH_STUB_MODE)
# ifdef _MSC_VER
    return -1;
# else
    return fork();
# endif
#else
#endif
}

pid_t sh_waitpid(shinstance *psh, pid_t pid, int *statusp, int flags)
{
#ifdef SH_PURE_STUB_MODE
    return -1;
#elif defined(SH_STUB_MODE)
# ifdef _MSC_VER
    return -1;
# else
    return waitpid(pid, statusp, flags);
# endif
#else
#endif
}

void sh__exit(shinstance *psh, int rc)
{
#ifdef SH_PURE_STUB_MODE
    return -1;
#elif defined(SH_STUB_MODE)
    _exit(rc);
#else
#endif
}

int sh_execve(shinstance *psh, const char *exe, const char * const *argv, const char * const *envp)
{
#ifdef SH_PURE_STUB_MODE
    return -1;
#elif defined(SH_STUB_MODE)
# ifdef _MSC_VER
    return -1;
# else
    return execve(exe, (char **)argv, (char **)envp);
# endif
#else
#endif
}

uid_t sh_getuid(shinstance *psh)
{
#ifdef SH_PURE_STUB_MODE
    return 0;
#elif defined(SH_STUB_MODE)
# ifdef _MSC_VER
    return 0;
# else
    return getuid();
# endif
#else
#endif
}

uid_t sh_geteuid(shinstance *psh)
{
#ifdef SH_PURE_STUB_MODE
    return 0;
#elif defined(SH_STUB_MODE)
# ifdef _MSC_VER
    return 0;
# else
    return geteuid();
# endif
#else
#endif
}

gid_t sh_getgid(shinstance *psh)
{
#ifdef SH_PURE_STUB_MODE
    return 0;
#elif defined(SH_STUB_MODE)
# ifdef _MSC_VER
    return 0;
# else
    return getgid();
# endif
#else
#endif
}

gid_t sh_getegid(shinstance *psh)
{
#ifdef SH_PURE_STUB_MODE
    return 0;
#elif defined(SH_STUB_MODE)
# ifdef _MSC_VER
    return 0;
# else
    return getegid();
# endif
#else
#endif
}

pid_t sh_getpid(shinstance *psh)
{
#ifdef SH_PURE_STUB_MODE
    return 0;
#elif defined(SH_STUB_MODE)
# ifdef _MSC_VER
    return _getpid();
# else
    return getpid();
# endif
#else
#endif
}

pid_t sh_getpgrp(shinstance *psh)
{
#ifdef SH_PURE_STUB_MODE
    return 0;
#elif defined(SH_STUB_MODE)
# ifdef _MSC_VER
    return _getpid();
# else
    return getpgrp();
# endif
#else
#endif
}

pid_t sh_getpgid(shinstance *psh, pid_t pid)
{
#ifdef SH_PURE_STUB_MODE
    return pid;
#elif defined(SH_STUB_MODE)
# ifdef _MSC_VER
    return pid;
# else
    return getpgid(pid);
# endif
#else
#endif
}

int sh_setpgid(shinstance *psh, pid_t pid, pid_t pgid)
{
#ifdef SH_PURE_STUB_MODE
    return -1;
#elif defined(SH_STUB_MODE)
# ifdef _MSC_VER
    return -1;
# else
    return setpgid(pid, pgid);
# endif
#else
#endif
}

pid_t sh_tcgetpgrp(shinstance *psh, int fd)
{
#ifdef SH_PURE_STUB_MODE
    return -1;
#elif defined(SH_STUB_MODE)
# ifdef _MSC_VER
    return -1;
# else
    return tcgetpgrp(fd);
# endif
#else
#endif
}

int sh_tcsetpgrp(shinstance *psh, int fd, pid_t pgrp)
{
#ifdef SH_PURE_STUB_MODE
    return -1;
#elif defined(SH_STUB_MODE)
# ifdef _MSC_VER
    return -1;
# else
    return tcsetpgrp(fd, pgrp);
# endif
#else
#endif
}

int sh_getrlimit(shinstance *psh, int resid, shrlimit *limp)
{
#ifdef SH_PURE_STUB_MODE
    return -1;
#elif defined(SH_STUB_MODE)
# ifdef _MSC_VER
    return -1;
# else
    return getrlimit(resid, limp);
# endif
#else
#endif
}

int sh_setrlimit(shinstance *psh, int resid, const shrlimit *limp)
{
#ifdef SH_PURE_STUB_MODE
    return -1;
#elif defined(SH_STUB_MODE)
# ifdef _MSC_VER
    return -1;
# else
    return setrlimit(resid, limp);
# endif
#else
#endif
}

