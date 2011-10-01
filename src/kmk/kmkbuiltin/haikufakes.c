/* $Id: haikufakes.c 2546 2011-10-01 19:49:54Z knut.osmundsen@oracle.com $ */
/** @file
 * Fake Unix/BSD stuff for Haiku.
 */

/*
 * Copyright (c) 2005-2010 knut st. osmundsen <bird-kBuild-spamx@anduin.net>
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
#include <errno.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <sys/stat.h>
#include "haikufakes.h"


int haiku_lchmod(const char *pszPath, mode_t mode)
{
    /*
     * Weed out symbolic links.
     */
    struct stat s;
    if (    !lstat(pszPath, &s)
        &&  S_ISLNK(s.st_mode))
    {
        errno = -ENOSYS;
        return -1;
    }

    return chmod(pszPath, mode);
}

