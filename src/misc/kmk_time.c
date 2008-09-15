/* $Id: redirect.c 1674 2008-07-11 00:49:29Z bird $ */
/** @file
 * kmk_time - Time program execution.
 *
 * This is based on kmk/kmkbuiltin/redirect.c.
 */

/*
 * Copyright (c) 2007-2008 knut st. osmundsen <bird-kBuild-spam@anduin.net>
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
# include <sys/times.h>
#endif

#ifdef __OS2__
# define INCL_BASE
# include <os2.h>
# ifndef LIBPATHSTRICT
#  define LIBPATHSTRICT 3
# endif
#endif


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
    int i;
#if defined(_MSC_VER)
    FILETIME ftStart, ft;
    intptr_t rc;
#else
    struct timeval tvStart, tv;
    pid_t pid;
    int rc;
#endif

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
        }

        switch (*psz)
        {
            case 'h':
                usage(stdout, name(argv[0]));
                return 0;

            case 'V':
                printf("kmk_time - kBuild version %d.%d.%d (r%u)\n"
                       "Copyright (C) 2007-2008 Knut St. Osmundsen\n",
                       KBUILD_VERSION_MAJOR, KBUILD_VERSION_MINOR, KBUILD_VERSION_PATCH,
                       KBUILD_SVN_REV);
                return 0;

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
    if (rc != -1)
    {
        unsigned _int64 iStart, iElapsed;
        GetSystemTimeAsFileTime(&ft);

        iStart = ftStart.dwLowDateTime | ((unsigned _int64)ftStart.dwHighDateTime << 32);
        iElapsed = ft.dwLowDateTime | ((unsigned _int64)ft.dwHighDateTime << 32);
        iElapsed -= iStart;
        iElapsed /= 10; /* to usecs */

        printf("%s: %um%d.%06ds\n", name(argv[0]),
               (unsigned)(iElapsed / (60 * 1000000)),
               (unsigned)(iElapsed % (60 * 1000000)) / 1000000,
               (unsigned)(iElapsed % 1000000));
    }
    else
    {
        fprintf(stderr, "%s: error: _spawnvp(_P_WAIT, \"%s\", ...) failed: %s\n", name(argv[0]), argv[i], strerror(errno));
        rc = 1;
    }
    return rc;

#else
    rc = 1;
    gettimeofday(&tvStart, NULL);
    pid = fork();
    if (pid > 0)
    {
        waitpid(pid, &rc, 0);
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

        printf("%s: %um%d.%06ds\n", name(argv[0]),
               tv.tv_sec / 60,
               tv.tv_sec % 60,
               tv.tv_usec);
    }
    else if (!pid)
    {
        execvp(argv[i], &argv[i]);
        fprintf(stderr, "%s: error: _execvp(\"%s\", ...) failed: %s\n", name(argv[0]), argv[i], strerror(errno));
    }
    else
        fprintf(stderr, "%s: error: fork() failed: %s\n", name(argv[0]), strerror(errno));

    return rc;
#endif
}

