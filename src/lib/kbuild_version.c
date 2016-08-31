/* $Id: kbuild_version.c 2851 2016-08-31 17:30:52Z knut.osmundsen@oracle.com $ */
/** @file
 * kbuild_version(), helper function.
 */

/*
 * Copyright (c) 2007-2013 knut st. osmundsen <bird-kBuild-spamx@anduin.net>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 * Alternatively, the content of this file may be used under the terms of the
 * GPL version 2 or later, or LGPL version 2.1 or later.
 */

/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
#include "kbuild_version.h"
#include <string.h>
#include <stdio.h>


/**
 * Prints the kBuild version message and returns 0.
 *
 * @returns 0
 * @param   argv0       The argv0.
 */
int kbuild_version(const char *argv0)
{
    const char *tmp;

    /* skip the path */
    for (tmp = strpbrk(argv0, "\\/:"); tmp; tmp = strpbrk(argv0, "\\/:"))
        argv0 = tmp + 1;

    /* find the end, ignoring extenions */
    tmp = strrchr(argv0, '.');
    if (!tmp)
        tmp = strchr(argv0, '\0');

    printf("%.*s - kBuild version %d.%d.%d (r%u)\n",
           (int)(tmp - argv0), argv0,
           KBUILD_VERSION_MAJOR, KBUILD_VERSION_MINOR, KBUILD_VERSION_PATCH,
           KBUILD_SVN_REV);
    return 0;
}

