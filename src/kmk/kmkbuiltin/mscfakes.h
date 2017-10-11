/* $Id: mscfakes.h 3092 2017-10-11 17:55:09Z knut.osmundsen@oracle.com $ */
/** @file
 * Unix fakes for MSC.
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

#ifndef ___mscfakes_h
#define ___mscfakes_h
#ifdef _MSC_VER

#define timeval windows_timeval

/* Include the config file (kmk's) so we don't need to duplicate stuff from it here. */
#include "config.h"

#include <io.h>
#include <direct.h>
#include <time.h>
#include <stdarg.h>
#include <malloc.h>
#include "getopt.h"
#ifndef MSCFAKES_NO_WINDOWS_H
# include <Windows.h>
#endif

#include <sys/stat.h>
#include <io.h>
#include <direct.h>
#include "nt/ntstat.h"
#include "nt/ntunlink.h"
#ifdef MSC_DO_64_BIT_IO
# if _MSC_VER >= 1400 /* We want 64-bit file lengths here when possible. */
#  define off_t __int64
#  define lseek _lseeki64
# endif
#endif

#undef timeval

#undef  PATH_MAX
#define PATH_MAX   _MAX_PATH
#undef  MAXPATHLEN
#define MAXPATHLEN _MAX_PATH

#define EX_OK 0
#define EX_OSERR 1
#define EX_NOUSER 1
#define EX_USAGE 1

#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

#define F_OK 0
#define X_OK 1
#define W_OK 2
#define R_OK 4

#define EFTYPE EINVAL

#define _PATH_DEVNULL "/dev/null"

#ifndef MAX
# define MAX(a,b) ((a) >= (b) ? (a) : (b))
#endif

typedef int mode_t;
typedef unsigned short nlink_t;
#if 0 /* found in config.h */
typedef unsigned short uid_t;
typedef unsigned short gid_t;
#endif
#if defined(_M_AMD64) || defined(_M_X64) || defined(_M_IA64) || defined(_WIN64)
typedef __int64 ssize_t;
#else
typedef long ssize_t;
#endif
typedef unsigned long u_long;
typedef unsigned int u_int;
typedef unsigned short u_short;

#if _MSC_VER >= 1600
# include <stdint.h>
#else
typedef unsigned char  uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int   uint32_t;
typedef signed char    int8_t;
typedef signed short   int16_t;
typedef signed int     int32_t;
#endif

struct timeval
{
    __time64_t tv_sec;
    long tv_usec;
};

struct iovec
{
    char *iov_base;
    size_t iov_len;
};

typedef __int64 intmax_t;
#if 0 /* found in config.h */
typedef unsigned __int64 uintmax_t;
#endif

#define chown(path, uid, gid) 0         /** @todo implement fchmod! */
char *dirname(char *path);
#define fsync(fd)  0
#define fchown(fd,uid,gid) 0
#define fchmod(fd, mode) 0              /** @todo implement fchmod! */
#define geteuid()  0
#define getegid()  0
int lchmod(const char *path, mode_t mode);
int msc_chmod(const char *path, mode_t mode);
#define chmod msc_chmod
#define lchown(path, uid, gid) chown(path, uid, gid)
int link(const char *pszDst, const char *pszLink);
int mkdir_msc(const char *path, mode_t mode);
#define mkdir(path, mode) mkdir_msc(path, mode)
#define mkfifo(path, mode) -1
#define mknod(path, mode, devno) -1
int mkstemp(char *temp);
#define readlink(link, buf, size) -1
#define reallocf(old, size) realloc(old, size)
int rmdir_msc(const char *path);
#define rmdir(path) rmdir_msc(path)
intmax_t strtoimax(const char *nptr, char **endptr, int base);
uintmax_t strtoumax(const char *nptr, char **endptr, int base);
#define strtoll(a,b,c) strtoimax(a,b,c)
#define strtoull(a,b,c) strtoumax(a,b,c)
int asprintf(char **strp, const char *fmt, ...);
int vasprintf(char **strp, const char *fmt, va_list ap);
#if _MSC_VER < 1400
int snprintf(char *buf, size_t size, const char *fmt, ...);
#else
#define snprintf _snprintf
#endif
int symlink(const char *pszDst, const char *pszLink);
int utimes(const char *pszPath, const struct timeval *paTimes);
int lutimes(const char *pszPath, const struct timeval *paTimes);
ssize_t writev(int fd, const struct iovec *vector, int count);

int gettimeofday(struct timeval *pNow, void *pvIgnored);
struct tm *localtime_r(const __time64_t *pNow, struct tm *pResult);
__time64_t timegm(struct tm *pNow);
#undef mktime
#define mktime _mktime64

/* bird write ENOSPC hacks. */
#undef write
#define write msc_write
ssize_t msc_write(int fd, const void *pvSrc, size_t cbSrc);

/*
 * MSC fake internals / helpers.
 */
int birdSetErrno(unsigned dwErr);

#endif /* _MSC_VER */
#endif

