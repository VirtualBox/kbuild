/* $Id: err.c 2911 2016-09-10 11:16:59Z knut.osmundsen@oracle.com $ */
/** @file
 * Override err.h so we get the program name right.
 */

/*
 * Copyright (c) 2005-2016 knut st. osmundsen <bird-kBuild-spamx@anduin.net>
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
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include "err.h"

#ifdef KBUILD_OS_WINDOWS
/* This is a trick to speed up console output on windows. */
# undef fwrite
# define fwrite maybe_con_fwrite
extern size_t maybe_con_fwrite(void const *, size_t, size_t, FILE *);
#endif


/** The current program name. */
const char *g_progname = "kmk";


int err(int eval, const char *fmt, ...)
{
    va_list args;
    int error = errno;

    /* stderr is unbuffered, so try format the whole message and print it in
       one go so it won't be split by other output. */
    char szMsg[4096];
    int cchMsg = snprintf(szMsg, sizeof(szMsg), "%s: ", g_progname);
    if (cchMsg < sizeof(szMsg) - 1 && cchMsg > 0)
    {
        int cchMsg2;
        va_start(args, fmt);
        cchMsg += cchMsg2 = vsnprintf(&szMsg[cchMsg], sizeof(szMsg) - cchMsg, fmt, args);
        va_end(args);

        if (   cchMsg < sizeof(szMsg) - 1
            && cchMsg2 >= 0)
        {
            cchMsg += cchMsg2 = snprintf(&szMsg[cchMsg], sizeof(szMsg) - cchMsg, ": %s\n", strerror(error));
            if (   cchMsg < sizeof(szMsg) - 1
                && cchMsg2 >= 0)
            {
                fwrite(szMsg, cchMsg, 1, stderr);
                return eval;
            }

        }

    }

    /* fallback */
    fprintf(stderr, "%s: ", g_progname);
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fprintf(stderr, ": %s\n", strerror(error));

    return eval;
}


int errx(int eval, const char *fmt, ...)
{
    va_list args;

    /* stderr is unbuffered, so try format the whole message and print it in
       one go so it won't be split by other output. */
    char szMsg[4096];
    int cchMsg = snprintf(szMsg, sizeof(szMsg), "%s: ", g_progname);
    if (cchMsg < sizeof(szMsg) - 1 && cchMsg > 0)
    {
        int cchMsg2;
        va_start(args, fmt);
        cchMsg += cchMsg2 = vsnprintf(&szMsg[cchMsg], sizeof(szMsg) - cchMsg, fmt, args);
        va_end(args);

        if (   cchMsg < sizeof(szMsg) - 1
            && cchMsg2 >= 0)
        {
            szMsg[cchMsg++] = '\n';
            fwrite(szMsg, cchMsg, 1, stderr);
            return eval;
        }

    }

    /* fallback */
    fprintf(stderr, "%s: ", g_progname);
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fprintf(stderr, "\n");

    return eval;
}

void warn(const char *fmt, ...)
{
    int error = errno;
    va_list args;

    /* stderr is unbuffered, so try format the whole message and print it in
       one go so it won't be split by other output. */
    char szMsg[4096];
    int cchMsg = snprintf(szMsg, sizeof(szMsg), "%s: ", g_progname);
    if (cchMsg < sizeof(szMsg) - 1 && cchMsg > 0)
    {
        int cchMsg2;
        va_start(args, fmt);
        cchMsg += cchMsg2 = vsnprintf(&szMsg[cchMsg], sizeof(szMsg) - cchMsg, fmt, args);
        va_end(args);

        if (   cchMsg < sizeof(szMsg) - 1
            && cchMsg2 >= 0)
        {
            cchMsg += cchMsg2 = snprintf(&szMsg[cchMsg], sizeof(szMsg) - cchMsg, ": %s\n", strerror(error));
            if (   cchMsg < sizeof(szMsg) - 1
                && cchMsg2 >= 0)
            {
                fwrite(szMsg, cchMsg, 1, stderr);
                return;
            }

        }
    }

    /* fallback */
    fprintf(stderr, "%s: ", g_progname);
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fprintf(stderr, ": %s\n", strerror(error));
}

void warnx(const char *fmt, ...)
{
    va_list args;

    /* stderr is unbuffered, so try format the whole message and print it in
       one go so it won't be split by other output. */
    char szMsg[4096];
    int cchMsg = snprintf(szMsg, sizeof(szMsg), "%s: ", g_progname);
    if (cchMsg < sizeof(szMsg) - 1 && cchMsg > 0)
    {
        int cchMsg2;
        va_start(args, fmt);
        cchMsg += cchMsg2 = vsnprintf(&szMsg[cchMsg], sizeof(szMsg) - cchMsg, fmt, args);
        va_end(args);

        if (   cchMsg < sizeof(szMsg) - 1
            && cchMsg2 >= 0)
        {
            szMsg[cchMsg++] = '\n';
            fwrite(szMsg, cchMsg, 1, stderr);
            return;
        }

    }

    /* fallback */
    fprintf(stderr, "%s: ", g_progname);
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fprintf(stderr, "\n");
}

