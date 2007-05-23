/* $Id: $ */
/** @file
 *
 * Missing BSD functions on Darwin / Mac OS X.
 *
 * Copyright (c) 2006-2007 knut st. osmundsen <bird-kBuild-spam@anduin.net>
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

