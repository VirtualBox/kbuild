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

    psh = malloc(sizeof(*psh));
    if (psh)
    {
        memset(psh, 0, sizeof(*psh));

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
        psh->errout.bufsize = 100;
        psh->errout.fd = 2;
        psh->memout.fd = MEM_OUT;
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
    return NULL;
}

char **sh_environ(shinstance *psh)
{
    static char *s_null[2] = {0,0};
    return &s_null[0];
}

const char *sh_gethomedir(shinstance *psh, const char *user)
{
    return NULL;
}

int sh_sigaction(int signo, const struct sh_sigaction *newp, struct sh_sigaction *oldp)
{
    return -1;
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

}

int sh_sigprocmask(shinstance *psh, int operation, sh_sigset_t const *newp, sh_sigset_t *oldp)
{
    return -1;
}

void sh_abort(shinstance *psh)
{
}

void sh_raise_sigint(shinstance *psh)
{

}

int sh_kill(shinstance *psh, pid_t pid, int signo)
{
    return -1;
}

int sh_killpg(shinstance *psh, pid_t pgid, int signo)
{
    return -1;
}

clock_t sh_times(shinstance *psh, shtms *tmsp)
{
    return 0;
}

int sh_sysconf_clk_tck(void)
{
    return 1;
}

pid_t sh_fork(shinstance *psh)
{
    return -1;
}

pid_t sh_waitpid(shinstance *psh, pid_t pid, int *statusp, int flags)
{
    return -1;
}

void sh__exit(shinstance *psh, int rc)
{
}

int sh_execve(shinstance *psh, const char *exe, const char * const *argv, const char * const *envp)
{
    return -1;
}

uid_t sh_getuid(shinstance *psh)
{
    return 0;
}

uid_t sh_geteuid(shinstance *psh)
{
    return 0;
}

gid_t sh_getgid(shinstance *psh)
{
    return 0;
}

gid_t sh_getegid(shinstance *psh)
{
    return 0;
}

pid_t sh_getpid(shinstance *psh)
{
    return 0;
}

pid_t sh_getpgrp(shinstance *psh)
{
    return 0;
}

pid_t sh_getpgid(shinstance *psh, pid_t pid)
{
    return pid;
}

int sh_setpgid(shinstance *psh, pid_t pid, pid_t pgid)
{
    return -1;
}

pid_t sh_tcgetpgrp(shinstance *psh, int fd)
{
    return -1;
}

int sh_tcsetpgrp(shinstance *psh, int fd, pid_t pgrp)
{
    return -1;
}

int sh_getrlimit(shinstance *psh, int resid, shrlimit *limp)
{
    return -1;
}

int sh_setrlimit(shinstance *psh, int resid, const shrlimit *limp)
{
    return -1;
}

