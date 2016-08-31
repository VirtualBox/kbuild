/* $Id: restartable-syscall-wrappers.c 2851 2016-08-31 17:30:52Z knut.osmundsen@oracle.com $ */
/** @file
 * restartable-syscall-wrappers.c - Workaround for annoying S11 "features".
 *
 * The symptoms are that open or mkdir occationally fails with EINTR when
 * receiving SIGCHLD at the wrong time.  With a enough cores, this start
 * happening on a regular basis.
 *
 * The workaround here is to create our own wrappers for these syscalls which
 * will restart the syscall when appropriate.  This depends on the libc
 * providing alternative names for the syscall entry points.
 */

/*
 * Copyright (c) 2011-2016 knut st. osmundsen <bird-kBuild-spamx@anduin.net>
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
#include <sys/types.h>
#ifdef KBUILD_OS_SOLARIS
# include <string.h> /* Try drag in feature_tests.h. */
# include <ctype.h>
# undef  _RESTRICT_KYWD
# define _RESTRICT_KYWD 
# undef  __PRAGMA_REDEFINE_EXTNAME
#endif
#include <sys/stat.h>
#include <utime.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>


/*******************************************************************************
*   Defined Constants And Macros                                               *
*******************************************************************************/
/** Mangle a syscall name to it's weak alias. */
#ifdef KBUILD_OS_SOLARIS
# define WRAP(a_name) _##a_name
#elif defined(KBUILD_OS_LINUX)
# define WRAP(a_name) __##a_name
#else
# error "Port Me"
#endif

/** Mangle a syscall name with optional '64' suffix. */
#if !defined(_LP64) && _FILE_OFFSET_BITS == 64
# define WRAP64(a_name) WRAP(a_name)##64
#else
# define WRAP64(a_name) WRAP(a_name)
#endif

/** Check whether errno indicates restart.  */
#ifdef ERESTART
# define SHOULD_RESTART() (errno == EINTR || errno == ERESTART)
#else
# define SHOULD_RESTART() (errno == EINTR)
#endif

/** Used by XSTR. */
#define XSTR_INNER(x)   #x
/** Returns the expanded argument as a string. */
#define XSTR(x)         XSTR_INNER(x)


static int dlsym_libc(const char *pszSymbol, void **ppvSym)
{
    static void *s_pvLibc = NULL;
    void        *pvLibc;
    void        *pvSym;

    /*
     * Use the RTLD_NEXT dl feature if present, it's designed for doing
     * exactly what we want here.
     */
#ifdef RTLD_NEXT
    pvSym = dlsym(RTLD_NEXT, pszSymbol);
    if (pvSym)
    {
        *ppvSym = pvSym;
        return 0;
    }
#endif

    /*
     * Open libc.
     */
    pvLibc = s_pvLibc;
    if (!pvLibc)
    {
#ifdef RTLD_NOLOAD
        unsigned fFlags = RTLD_NOLOAD | RTLD_NOW;
#else
        unsigned fFlags = RTLD_GLOBAL | RTLD_NOW;
#endif
#ifdef KBUILD_OS_LINUX
        pvLibc = dlopen("/lib/libc.so.6", fFlags);
#else
        pvLibc = dlopen("/lib/libc.so", fFlags);
#endif
        if (!pvLibc)
        {
            fprintf(stderr, "restartable-syscall-wrappers: failed to dlopen libc for resolving %s: %s\n",
                    pszSymbol, dlerror());
            errno = ENOSYS;
            return -1;
        }
        /** @todo check standard symbol? */
    }

    /*
     * Resolve the symbol.
     */
    pvSym = dlsym(pvLibc, pszSymbol);
    if (!pvSym)
    {
        fprintf(stderr, "restartable-syscall-wrappers: failed to resolve %s: %s\n",
                pszSymbol, dlerror());
        errno = ENOSYS;
        return -1;
    }

    *ppvSym = pvSym;
    return 0;
}


#undef open
int open(const char *pszPath, int fFlags, ...)
{
    mode_t      fMode;
    va_list     va;
    int         fd;
    static union
    {
        int (* pfnReal)(const char *, int, ...);
        void *pvSym;
    } s_u;

    if (   !s_u.pfnReal
        && dlsym_libc("open", &s_u.pvSym) != 0)
        return -1;

    va_start(va, fFlags);
    fMode = va_arg(va, mode_t);
    va_end(va);

    do
        fd = s_u.pfnReal(pszPath, fFlags, fMode);
    while (fd == -1 && SHOULD_RESTART());
    return fd;
}

#undef open64
int open64(const char *pszPath, int fFlags, ...)
{
    mode_t      fMode;
    va_list     va;
    int         fd;
    static union
    {
        int (* pfnReal)(const char *, int, ...);
        void *pvSym;
    } s_u;

    if (   !s_u.pfnReal
        && dlsym_libc("open64", &s_u.pvSym) != 0)
        return -1;

    va_start(va, fFlags);
    fMode = va_arg(va, mode_t);
    va_end(va);

    do
        fd = s_u.pfnReal(pszPath, fFlags, fMode);
    while (fd == -1 && SHOULD_RESTART());
    return fd;
}

#define WRAP_FN(a_Name, a_ParamsWithTypes, a_ParamsNoType, a_RetType, a_RetFailed) \
    a_RetType a_Name a_ParamsWithTypes \
    { \
        static union \
        { \
            a_RetType (* pfnReal) a_ParamsWithTypes; \
            void *pvSym; \
        } s_u; \
        a_RetType rc; \
        \
        if (   !s_u.pfnReal \
            && dlsym_libc(#a_Name, &s_u.pvSym) != 0) \
            return a_RetFailed; \
        \
        do \
            rc = s_u.pfnReal a_ParamsNoType; \
        while (rc == a_RetFailed && SHOULD_RESTART()); \
        return rc; \
    } typedef int ignore_semi_colon_##a_Name

#undef mkdir
WRAP_FN(mkdir, (const char *pszPath, mode_t fMode), (pszPath, fMode), int, -1);

#undef rmdir
WRAP_FN(rmdir, (const char *pszPath, mode_t fMode), (pszPath, fMode), int, -1);

#undef unlink
WRAP_FN(unlink, (const char *pszPath), (pszPath), int, -1);

#undef remove
WRAP_FN(remove, (const char *pszPath), (pszPath), int, -1);

#undef symlink
WRAP_FN(symlink, (const char *pszFrom, const char *pszTo), (pszFrom, pszTo), int, -1);

#undef link
WRAP_FN(link, (const char *pszFrom, const char *pszTo), (pszFrom, pszTo), int, -1);

#undef stat
WRAP_FN(stat, (const char *pszPath, struct stat *pStBuf), (pszPath, pStBuf), int, -1);
#undef lstat
WRAP_FN(lstat, (const char *pszPath, struct stat *pStBuf), (pszPath, pStBuf), int, -1);

#undef stat64
WRAP_FN(stat64, (const char *pszPath, struct stat64 *pStBuf), (pszPath, pStBuf), int, -1);
#undef lstat64
WRAP_FN(lstat64, (const char *pszPath, struct stat64 *pStBuf), (pszPath, pStBuf), int, -1);

#undef read
WRAP_FN(read, (int fd, void *pvBuf, size_t cbBuf), (fd, pvBuf, cbBuf), ssize_t, -1);

#undef write
WRAP_FN(write, (int fd, void *pvBuf, size_t cbBuf), (fd, pvBuf, cbBuf), ssize_t, -1);

#undef fopen
WRAP_FN(fopen, (const char *pszPath, const char *pszMode), (pszPath, pszMode), FILE *, NULL);
#undef fopen64
WRAP_FN(fopen64, (const char *pszPath, const char *pszMode), (pszPath, pszMode), FILE *, NULL);

#undef chmod
WRAP_FN(chmod, (const char *pszPath, mode_t fMode), (pszPath, fMode), int, -1);
#undef lchmod
WRAP_FN(lchmod, (const char *pszPath, mode_t fMode), (pszPath, fMode), int, -1);

#undef chown
WRAP_FN(chown, (const char *pszPath, uid_t uid, gid_t gid), (pszPath, uid, gid), int, -1);
#undef lchown
WRAP_FN(lchown, (const char *pszPath, uid_t uid, gid_t gid), (pszPath, uid, gid), int, -1);

#undef utime
WRAP_FN(utime, (const char *pszPath, const struct utimbuf *pTimes), (pszPath, pTimes), int, -1);

#undef utimes
WRAP_FN(utimes, (const char *pszPath, const struct timeval *paTimes), (pszPath, paTimes), int, -1);

#undef pathconf
WRAP_FN(pathconf, (const char *pszPath, int iCfgNm), (pszPath, iCfgNm), long, -1);

#undef readlink
WRAP_FN(readlink, (const char *pszPath, char *pszBuf, size_t cbBuf), (pszPath, pszBuf, cbBuf), ssize_t, -1);

