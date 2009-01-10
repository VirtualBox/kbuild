/* $Id: err.c 2243 2009-01-10 02:24:02Z knut.osmundsen@oracle.com $ */
/** @file
 * Override err.h so we get the program name right.
 */

/*
 * Copyright (c) 2005-2009 knut st. osmundsen <bird-kBuild-spamix@anduin.net>
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


/** The current program name. */
const char *g_progname = "kmk";


int err(int eval, const char *fmt, ...)
{
    va_list args;
    int error = errno;
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
    fprintf(stderr, "%s: ", g_progname);
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fprintf(stderr, ": %s\n", strerror(error));
}

void warnx(const char *fmt, ...)
{
    va_list args;
    fprintf(stderr, "%s: ", g_progname);
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fprintf(stderr, "\n");
}

