/* $Id: kbuild_version.c 2113 2008-12-25 13:21:58Z knut.osmundsen@oracle.com $ */
/** @file
 * kbuild_version(), helper function.
 */

/*
 * Copyright (c) 2007-2008 knut st. osmundsen <bird-src-spam@anduin.net>
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
#include "kmkbuiltin.h"
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
           tmp - argv0, argv0,
           KBUILD_VERSION_MAJOR, KBUILD_VERSION_MINOR, KBUILD_VERSION_PATCH,
           KBUILD_SVN_REV);
    return 0;
}

