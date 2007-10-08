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
# include <limits.h>
# ifndef PIPE_BUF
#  define PIPE_BUF 512
# endif
#else
# include <unistd.h>
# include <fcntl.h>
# include <dirent.h>
#endif


int shfile_open(shfdtab *pfdtab, const char *name, unsigned flags, mode_t mode)
{
#ifdef SH_PURE_STUB_MODE
    return -1;
#elif defined(SH_STUB_MODE)
    return open(name, flags, mode);
#else
#endif
}

int shfile_pipe(shfdtab *pfdtab, int fds[2])
{
#ifdef SH_PURE_STUB_MODE
    return -1;
#elif defined(SH_STUB_MODE)
# ifdef _MSC_VER
    return _pipe(fds, PIPE_BUF, O_BINARY);
# else
    return pipe(fds);
# endif
#else
#endif
}

int shfile_dup(shfdtab *pfdtab, int fd)
{
#ifdef SH_PURE_STUB_MODE
    return -1;
#elif defined(SH_STUB_MODE)
    return dup(fd);
#else
#endif
}

int shfile_close(shfdtab *pfdtab, unsigned fd)
{
#ifdef SH_PURE_STUB_MODE
    return -1;
#elif defined(SH_STUB_MODE)
    return close(fd);
#else
#endif
}

long shfile_read(shfdtab *pfdtab, int fd, void *buf, size_t len)
{
#ifdef SH_PURE_STUB_MODE
    return -1;
#elif defined(SH_STUB_MODE)
# ifdef _MSC_VER
    return read(fd, buf, (unsigned)len);
# else
    return read(fd, buf, len);
# endif
#else
#endif
}

long shfile_write(shfdtab *pfdtab, int fd, const void *buf, size_t len)
{
#ifdef SH_PURE_STUB_MODE
    return -1;
#elif defined(SH_STUB_MODE)
# ifdef _MSC_VER
    return write(fd, buf, (unsigned)len);
# else
    return write(fd, buf, len);
# endif
#else
#endif
}

long shfile_lseek(shfdtab *pfdtab, int fd, long off, int whench)
{
#ifdef SH_PURE_STUB_MODE
    return -1;
#elif defined(SH_STUB_MODE)
    return lseek(fd, off, whench);
#else
#endif
}

int shfile_fcntl(shfdtab *pfdtab, int fd, int cmd, int arg)
{
#ifdef SH_PURE_STUB_MODE
    return -1;
#elif defined(SH_STUB_MODE)
# ifdef _MSC_VER
    return -1;
# else
    return fcntl(fd, cmd, arg);
# endif
#else
#endif
}

int shfile_stat(shfdtab *pfdtab, const char *path, struct stat *pst)
{
#ifdef SH_PURE_STUB_MODE
    return -1;
#elif defined(SH_STUB_MODE)
    return stat(path, pst);
#else
#endif
}

int shfile_lstat(shfdtab *pfdtab, const char *link, struct stat *pst)
{
#ifdef SH_PURE_STUB_MODE
    return -1;
#elif defined(SH_STUB_MODE)
# ifdef _MSC_VER
    return stat(link, pst);
# else
    return lstat(link, pst);
# endif
#else
#endif
}

int shfile_chdir(shfdtab *pfdtab, const char *path)
{
#ifdef SH_PURE_STUB_MODE
    return -1;
#elif defined(SH_STUB_MODE)
# ifdef _MSC_VER //???
    return chdir(path);
# else
    return chdir(path);
# endif
#else
#endif
}

char *shfile_getcwd(shfdtab *pfdtab, char *buf, int len)
{
#ifdef SH_PURE_STUB_MODE
    return NULL;
#elif defined(SH_STUB_MODE)
    return getcwd(buf, len);
#else
#endif
}

int shfile_access(shfdtab *pfdtab, const char *path, int type)
{
#ifdef SH_PURE_STUB_MODE
    return -1;
#elif defined(SH_STUB_MODE)
# ifdef _MSC_VER
    type &= ~X_OK;
    return access(path, type);
# else
    return access(path, type);
# endif
#else
#endif
}

int shfile_isatty(shfdtab *pfdtab, int fd)
{
#ifdef SH_PURE_STUB_MODE
    return 0;
#elif defined(SH_STUB_MODE)
    return isatty(fd);
#else
#endif
}


int shfile_ioctl(shfdtab *pfdtab, int fd, unsigned long request, void *buf)
{
#ifdef SH_PURE_STUB_MODE
    return -1;
#elif defined(SH_STUB_MODE)
# ifdef _MSC_VER
    return -1;
# else
    return ioctl(fd, request, buf);
# endif
#else
#endif
}

mode_t shfile_get_umask(shfdtab *pfdtab)
{
#ifdef SH_PURE_STUB_MODE
    return 022;
#elif defined(SH_STUB_MODE)
    return 022;
#else
#endif
}


shdir *shfile_opendir(shfdtab *pfdtab, const char *dir)
{
#ifdef SH_PURE_STUB_MODE
    return NULL;
#elif defined(SH_STUB_MODE)
# ifdef _MSC_VER
    return NULL;
# else
    return (shdir *)opendir(dir);
# endif
#else
#endif
}

shdirent *shfile_readdir(struct shdir *pdir)
{
#ifdef SH_PURE_STUB_MODE
    return NULL;
#elif defined(SH_STUB_MODE)
# ifdef _MSC_VER
    return NULL;
# else
    struct dirent *pde = readdir((DIR *)pdir);
    return pde ? (shdirent *)&pde->d_name[0] : NULL;
# endif
#else
#endif
}

void shfile_closedir(struct shdir *pdir)
{
#ifdef SH_PURE_STUB_MODE
    return NULL;
#elif defined(SH_STUB_MODE)
# ifndef _MSC_VER
    closedir((DIR *)pdir);
# endif
#else
#endif
}
