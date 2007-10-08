/* $Id: $ */
/** @file
 *
 * File management.
 *
 * Copyright (c) 2007 knut st. osmundsen <bird-src-spam@anduin.net>
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

#include "shfile.h"
#include <stdlib.h>

#ifdef KBUILD_OS_WINDOWS
//# include <io.h>
#else
# include <unistd.h>
# include <fcntl.h>
#endif


int shfile_open(shfdtab *pfdtab, const char *name, unsigned flags, mode_t mode)
{
//    return open(name, flags);
    return -1;
}

int shfile_pipe(shfdtab *pfdtab, int fds[2])
{
    return -1;
}

int shfile_dup(shfdtab *pfdtab, int fd)
{
    return -1;
}

int shfile_close(shfdtab *pfdtab, unsigned fd)
{
    return -1;
}

long shfile_read(shfdtab *pfdtab, int fd, void *buf, size_t len)
{
    return -1;
}

long shfile_write(shfdtab *pfdtab, int fd, const void *buf, size_t len)
{
    return -1;
}

long shfile_lseek(shfdtab *pfdtab, int fd, long off, int whench)
{
    return -1;
}

int shfile_fcntl(shfdtab *pfdtab, int fd, int cmd, int arg)
{
    return -1;
}

int shfile_stat(shfdtab *pfdtab, const char *path, struct stat *pst)
{
    return -1;
}

int shfile_lstat(shfdtab *pfdtab, const char *link, struct stat *pst)
{
    return -1;
}

int shfile_chdir(shfdtab *pfdtab, const char *path)
{
    return -1;
}

char *shfile_getcwd(shfdtab *pfdtab, char *buf, int len)
{
    return NULL;
}

int shfile_access(shfdtab *pfdtab, const char *path, int type)
{
    return -1;
}

int shfile_isatty(shfdtab *pfdtab, int fd)
{
    return 0;
}


int shfile_ioctl(shfdtab *pfdtab, int fd, unsigned long request, void *buf)
{
    return -1;
}

mode_t shfile_get_umask(shfdtab *pfdtab)
{
    return 022;
}


shdir *shfile_opendir(shfdtab *pfdtab, const char *dir)
{
    return NULL;
}

shdirent *shfile_readdir(struct shdir *pdir)
{
    return NULL;
}

void shfile_closedir(struct shdir *pdir)
{

}
