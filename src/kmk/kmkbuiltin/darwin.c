/* $Id: darwin.c 2019 2008-11-02 00:21:05Z knut.osmundsen@oracle.com $ */
/** @file
 * Missing BSD functions on Darwin / Mac OS X.
 */

/*
 * Copyright (c) 2006-2008 knut st. osmundsen <bird-src-spam@anduin.net>
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
#include <sys/stat.h>
#include <unistd.h>


int lchmod(const char *path, mode_t mode)
{
    struct stat st;
    if (lstat(path, &st))
        return -1;
    if (S_ISLNK(st.st_mode))
        return 0; /* pretend success */
    return chmod(path, mode);
}


int lutimes(const char *path, const struct timeval *tvs)
{
    struct stat st;
    if (lstat(path, &st))
        return -1;
    if (S_ISLNK(st.st_mode))
        return 0; /* pretend success */
    return utimes(path, tvs);
}

