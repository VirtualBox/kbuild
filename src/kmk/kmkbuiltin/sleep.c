/* $Id: sleep.c 3192 2018-03-26 20:25:56Z knut.osmundsen@oracle.com $ */
/** @file
 * kmk_sleep - suspend execution for an interval of time.
 */

/*
 * Copyright (c) 2008-2010 knut st. osmundsen <bird-kBuild-spamx@anduin.net>
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
#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#if defined(_MSC_VER)
# include <Windows.h>
#else
# include <unistd.h>
# include <time.h>
#endif

#include "err.h"
#include "../kmkbuiltin.h"


static int kmk_builtin_sleep_usage(PKMKBUILTINCTX pCtx, int fIsErr)
{
    kmk_builtin_ctx_printf(pCtx, fIsErr,
                           "usage: %s <seconds>[s]\n"
                           "   or: %s <milliseconds>ms\n"
                           "   or: %s <minutes>m\n"
                           "   or: %s <hours>h\n"
                           "   or: %s <days>d\n"
                           "   or: %s --help\n"
                           "   or: %s --version\n"
                           "\n"
                           "Only integer values are accepted.\n"
                           ,
                           pCtx->pszProgName, pCtx->pszProgName, pCtx->pszProgName, pCtx->pszProgName,
                           pCtx->pszProgName, pCtx->pszProgName, pCtx->pszProgName);
    return 1;
}


int kmk_builtin_sleep(int argc, char **argv, char **envp, PKMKBUILTINCTX pCtx)
{
    long cMsToSleep;
    long lTmp;
    unsigned long ulFactor;
    char *pszInterval;
    char *pszSuff;

    /*
     * Parse arguments.
     */
    if (argc != 2)
        return kmk_builtin_sleep_usage(pCtx, 1);

    /* help request */
    if (   !strcmp(argv[1], "-h")
        || !strcmp(argv[1], "-?")
        || !strcmp(argv[1], "-H")
        || !strcmp(argv[1], "--help"))
    {
        kmk_builtin_sleep_usage(pCtx, 0);
        return 0;
    }

    /* version request */
    if (   !strcmp(argv[1], "-V")
        || !strcmp(argv[1], "--version"))
    {
        printf("kmk_sleep - kBuild version %d.%d.%d (r%u)\n"
               "Copyright (c) 2008-2009 knut st. osmundsen\n",
               KBUILD_VERSION_MAJOR, KBUILD_VERSION_MINOR, KBUILD_VERSION_PATCH,
               KBUILD_SVN_REV);
        return 0;
    }

    /*
     * Try convert the argument to a time period.
     * Allow spaces before, between and after the different parts.
     */
    pszInterval = argv[1];
    while (isspace(*pszInterval))
        pszInterval++;

    cMsToSleep = strtol(pszInterval, &pszSuff, 0);
    if (pszSuff == pszInterval)
        return errx(pCtx, 1, "malformed interval '%s'!\n", pszInterval);

    while (isspace(*pszSuff))
        pszSuff++;

    if (!*pszSuff)
        ulFactor = 1000; /* s */
    else
    {
        /* find the suffix length and check that it's only white space following it. */
        int cchSuff;
        int i = 1;
        while (pszSuff[i] && !isspace(pszSuff[i]))
            i++;
        cchSuff = i;
        while (pszSuff[i])
        {
            if (!isspace(pszSuff[i]))
                return errx(pCtx, 1, "malformed interval '%s'!\n", pszInterval);
            i++;
        }

        if (cchSuff == 2 && !strncmp (pszSuff, "ms", 2))
            ulFactor = 1;
        else if (cchSuff == 1 && *pszSuff == 's')
            ulFactor = 1000;
        else if (cchSuff == 1 && *pszSuff == 'm')
            ulFactor = 60*1000;
        else if (cchSuff == 1 && *pszSuff == 'h')
            ulFactor = 60*60*1000;
        else if (cchSuff == 1 && *pszSuff == 'd')
            ulFactor = 24*60*60*1000;
        else
            return errx(pCtx, 1, "unknown suffix '%.*s'!\n", cchSuff, pszSuff);
    }

    lTmp = cMsToSleep;
    cMsToSleep *= ulFactor;
    if ((cMsToSleep / ulFactor) != (unsigned long)lTmp)
        return errx(pCtx, 1, "time interval overflow!\n");

    /*
     * Do the actual sleeping.
     */
    if (cMsToSleep > 0)
    {
#if defined(_MSC_VER)
        Sleep(cMsToSleep);
#else
        if (cMsToSleep)
        {
            struct timespec TimeSpec;
            TimeSpec.tv_nsec = (cMsToSleep % 1000) * 1000000;
            TimeSpec.tv_sec  =  cMsToSleep / 1000;
            nanosleep(&TimeSpec, NULL);
        }
#endif
    }

    return 0;
}

#ifdef KMK_BUILTIN_STANDALONE
int main(int argc, char **argv, char **envp)
{
    KMKBUILTINCTX Ctx = { "kmk_sleep", NULL };
    return kmk_builtin_sleep(argc, argv, envp, &Ctx);
}
#endif

