/* $Id: kmk_time.c 2413 2010-09-11 17:43:04Z knut.osmundsen@oracle.com $ */
/** @file
 * kmk_time - Time program execution.
 *
 * This is based on kmk/kmkbuiltin/redirect.c.
 */

/*
 * Copyright (c) 2007-2010 knut st. osmundsen <bird-kBuild-spamx@anduin.net>
 *
 * This file is part of kBuild.
 *
 * kBuild is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * kBuild is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with kBuild.  If not, see <http://www.gnu.org/licenses/>
 *
 */

/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#if defined(_MSC_VER)
# include <io.h>
# include <direct.h>
# include <process.h>
# include <Windows.h>
#else
# include <unistd.h>
# include <sys/time.h>
# include <sys/wait.h>
# include <signal.h>
#endif

#ifdef __OS2__
# define INCL_BASE
# include <os2.h>
# ifndef LIBPATHSTRICT
#  define LIBPATHSTRICT 3
# endif
#endif

#ifndef _MSC_VER
static const char *my_strsignal(int signo)
{
#define CASE_SIG_RET_STR(sig) if (signo == SIG##sig) return #sig
#ifdef SIGHUP
    CASE_SIG_RET_STR(HUP);
#endif
#ifdef SIGINT
    CASE_SIG_RET_STR(INT);
#endif
#ifdef SIGQUIT
    CASE_SIG_RET_STR(QUIT);
#endif
#ifdef SIGILL
    CASE_SIG_RET_STR(ILL);
#endif
#ifdef SIGTRAP
    CASE_SIG_RET_STR(TRAP);
#endif
#ifdef SIGABRT
    CASE_SIG_RET_STR(ABRT);
#endif
#ifdef SIGIOT
    CASE_SIG_RET_STR(IOT);
#endif
#ifdef SIGBUS
    CASE_SIG_RET_STR(BUS);
#endif
#ifdef SIGFPE
    CASE_SIG_RET_STR(FPE);
#endif
#ifdef SIGKILL
    CASE_SIG_RET_STR(KILL);
#endif
#ifdef SIGUSR1
    CASE_SIG_RET_STR(USR1);
#endif
#ifdef SIGSEGV
    CASE_SIG_RET_STR(SEGV);
#endif
#ifdef SIGUSR2
    CASE_SIG_RET_STR(USR2);
#endif
#ifdef SIGPIPE
    CASE_SIG_RET_STR(PIPE);
#endif
#ifdef SIGALRM
    CASE_SIG_RET_STR(ALRM);
#endif
#ifdef SIGTERM
    CASE_SIG_RET_STR(TERM);
#endif
#ifdef SIGSTKFLT
    CASE_SIG_RET_STR(STKFLT);
#endif
#ifdef SIGCHLD
    CASE_SIG_RET_STR(CHLD);
#endif
#ifdef SIGCONT
    CASE_SIG_RET_STR(CONT);
#endif
#ifdef SIGSTOP
    CASE_SIG_RET_STR(STOP);
#endif
#ifdef SIGTSTP
    CASE_SIG_RET_STR(TSTP);
#endif
#ifdef SIGTTIN
    CASE_SIG_RET_STR(TTIN);
#endif
#ifdef SIGTTOU
    CASE_SIG_RET_STR(TTOU);
#endif
#ifdef SIGURG
    CASE_SIG_RET_STR(URG);
#endif
#ifdef SIGXCPU
    CASE_SIG_RET_STR(XCPU);
#endif
#ifdef SIGXFSZ
    CASE_SIG_RET_STR(XFSZ);
#endif
#ifdef SIGVTALRM
    CASE_SIG_RET_STR(VTALRM);
#endif
#ifdef SIGPROF
    CASE_SIG_RET_STR(PROF);
#endif
#ifdef SIGWINCH
    CASE_SIG_RET_STR(WINCH);
#endif
#ifdef SIGIO
    CASE_SIG_RET_STR(IO);
#endif
#ifdef SIGPWR
    CASE_SIG_RET_STR(PWR);
#endif
#ifdef SIGSYS
    CASE_SIG_RET_STR(SYS);
#endif
#ifdef SIGBREAK
    CASE_SIG_RET_STR(BREAK);
#endif
#undef CASE_SIG_RET_STR
    return "???";
}
#endif /* unix */

static const char *name(const char *pszName)
{
    const char *psz = strrchr(pszName, '/');
#if defined(_MSC_VER) || defined(__OS2__)
    const char *psz2 = strrchr(pszName, '\\');
    if (!psz2)
        psz2 = strrchr(pszName, ':');
    if (psz2 && (!psz || psz2 > psz))
        psz = psz2;
#endif
    return psz ? psz + 1 : pszName;
}


static int usage(FILE *pOut,  const char *argv0)
{
    fprintf(pOut,
            "usage: %s <program> [args]\n"
            "   or: %s --help\n"
            "   or: %s --version\n"
            ,
            argv0, argv0, argv0);
    return 1;
}


int main(int argc, char **argv)
{
    int                 i, j;
    int                 cTimes = 1;
#if defined(_MSC_VER)
    FILETIME ftStart,   ft;
    unsigned _int64     usMin, usMax, usAvg, usTotal, usCur;
    unsigned _int64     iStart;
    intptr_t            rc;
#else
    struct timeval      tvStart, tv;
    unsigned long long  usMin, usMax, usAvg, usTotal, usCur;
    pid_t               pid;
    int                 rc;
#endif
    int                 rcExit = 0;

    /*
     * Parse arguments.
     */
    if (argc <= 1)
        return usage(stderr, name(argv[0]));
    for (i = 1; i < argc; i++)
    {
        char *psz = &argv[i][0];
        if (*psz++ != '-')
            break;

        if (*psz == '-')
        {
            /* '--' ? */
            if (!psz[1])
            {
                i++;
                break;
            }

            /* convert to short. */
            if (!strcmp(psz, "-help"))
                psz = "h";
            else if (!strcmp(psz, "-version"))
                psz = "V";
            else if (!strcmp(psz, "-iterations"))
                psz = "i";
        }

        switch (*psz)
        {
            case 'h':
                usage(stdout, name(argv[0]));
                return 0;

            case 'V':
                printf("kmk_time - kBuild version %d.%d.%d (r%u)\n"
                       "Copyright (C) 2007-2009 knut st. osmundsen\n",
                       KBUILD_VERSION_MAJOR, KBUILD_VERSION_MINOR, KBUILD_VERSION_PATCH,
                       KBUILD_SVN_REV);
                return 0;

            case 'i':
                if (i + 1 >= argc)
                {
                    fprintf(stderr, "%s: syntax error: missing iteration count\n", name(argv[0]));
                    return 1;
                }
                cTimes = atoi(argv[++i]);
                if (cTimes <= 0)
                {
                    fprintf(stderr, "%s: error: invalid interation count '%s'.\n", name(argv[0]), argv[i]);
                    return 1;
                }
                break;

            default:
                fprintf(stderr, "%s: error: syntax error '%s'\n", name(argv[0]), argv[i]);
                return 1;
        }
    }

    /*
     * Make sure there's something to execute.
     */
    if (i >= argc)
    {
        fprintf(stderr, "%s: syntax error: nothing to execute!\n", name(argv[0]));
        return usage(stderr, name(argv[0]));
    }

    /*
     * Execute the program the specified number of times.
     */
    usMax = usMin = usTotal = 0;
    usMin--; /* wraps to max value */
    for (j = 0; j < cTimes; j++)
    {
        /*
         * Execute the program (it's actually supposed to be a command I think, but wtf).
         */

#if defined(_MSC_VER)
        /** @todo
         * We'll have to find the '--' in the commandline and pass that
         * on to CreateProcess or spawn. Otherwise, the argument qouting
         * is gonna be messed up.
         */
        GetSystemTimeAsFileTime(&ftStart);
        rc = _spawnvp(_P_WAIT, argv[i], &argv[i]);
        if (rc == -1)
        {
            fprintf(stderr, "%s: error: _spawnvp(_P_WAIT, \"%s\", ...) failed: %s\n", name(argv[0]), argv[i], strerror(errno));
            return 8;
        }

        GetSystemTimeAsFileTime(&ft);

        iStart = ftStart.dwLowDateTime | ((unsigned _int64)ftStart.dwHighDateTime << 32);
        usCur = ft.dwLowDateTime | ((unsigned _int64)ft.dwHighDateTime << 32);
        usCur -= iStart;
        usCur /= 10; /* to usecs */

        printf("%s: ", name(argv[0]));
        if (cTimes != 1)
            printf("#%02u ", j + 1);
        printf("%um%u.%06us - exit code: %d\n",
               (unsigned)(usCur / (60 * 1000000)),
               (unsigned)(usCur % (60 * 1000000)) / 1000000,
               (unsigned)(usCur % 1000000),
               rc);

#else /* unix: */
        gettimeofday(&tvStart, NULL);
        pid = fork();
        if (!pid)
        {
            /* child */
            execvp(argv[i], &argv[i]);
            fprintf(stderr, "%s: error: _execvp(\"%s\", ...) failed: %s\n", name(argv[0]), argv[i], strerror(errno));
            return 8;
        }
        if (pid < 0)
        {
            fprintf(stderr, "%s: error: fork() failed: %s\n", name(argv[0]), strerror(errno));
            return 9;
        }

        /* parent, wait for child. */
        rc = 9;
        while (waitpid(pid, &rc, 0) == -1 && errno == EINTR)
            /* nothing */;
        gettimeofday(&tv, NULL);

        /* calc elapsed time */
        tv.tv_sec -= tvStart.tv_sec;
        if (tv.tv_usec > tvStart.tv_usec)
            tv.tv_usec -= tvStart.tv_usec;
        else
        {
            tv.tv_sec--;
            tv.tv_usec = tv.tv_usec + 1000000 - tvStart.tv_usec;
        }
        usCur = tv.tv_sec * 1000000ULL
              + tv.tv_usec;

        printf("%s: ", name(argv[0]));
        if (cTimes != 1)
            printf("#%02u ", j + 1);
        printf("%um%u.%06us",
               (unsigned)(tv.tv_sec / 60),
               (unsigned)(tv.tv_sec % 60),
               (unsigned)tv.tv_usec);
        if (WIFEXITED(rc))
        {
            printf(" - normal exit: %d\n", WEXITSTATUS(rc));
            rc = WEXITSTATUS(rc);
        }
        else if (WIFSIGNALED(rc) && WCOREDUMP(rc))
        {
            printf(" - dumped core: %s (%d)\n", my_strsignal(WTERMSIG(rc)), WTERMSIG(rc));
            rc = 10;
        }
        else if (WIFSIGNALED(rc))
        {
            printf(" -   killed by: %s (%d)\n", my_strsignal(WTERMSIG(rc)), WTERMSIG(rc));
            rc = 11;
        }
        else if (WIFSTOPPED(rc))
        {
            printf(" -  stopped by: %s (%d)\n", my_strsignal(WSTOPSIG(rc)), WSTOPSIG(rc));
            rc = 12;
        }
        else
        {
            printf(" unknown exit status %#x (%d)\n", rc, rc);
            rc = 13;
        }
#endif /* unix */
        if (rc && !rcExit)
            rcExit = (int)rc;

        /* calc min/max/avg */
        usTotal += usCur;
        if (usMax < usCur)
            usMax = usCur;
        if (usMin > usCur)
            usMin = usCur;
    }

    /*
     * Summary if more than one run.
     */
    if (cTimes != 1)
    {
        usAvg = usTotal / cTimes;

        printf("%s: avg %um%u.%06us\n", name(argv[0]), (unsigned)(usAvg / 60000000), (unsigned)(usAvg % 60000000) / 1000000, (unsigned)(usAvg % 1000000));
        printf("%s: min %um%u.%06us\n", name(argv[0]), (unsigned)(usMin / 60000000), (unsigned)(usMin % 60000000) / 1000000, (unsigned)(usMin % 1000000));
        printf("%s: max %um%u.%06us\n", name(argv[0]), (unsigned)(usMax / 60000000), (unsigned)(usMax % 60000000) / 1000000, (unsigned)(usMax % 1000000));
    }

    return rcExit;
}

