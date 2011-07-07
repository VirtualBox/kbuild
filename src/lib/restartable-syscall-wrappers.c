/* $Id: restartable-syscall-wrappers.c 2447 2011-07-07 11:59:15Z knut.osmundsen@oracle.com $ */
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
 * Copyright (c) 2011 knut st. osmundsen <bird-kBuild-spamx@anduin.net>
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
#include <sys/types.h>
#include <sys/stat.h>
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



extern int WRAP64(open)(const char *pszName, int fFlags, ...);
int open(const char *pszName, int fFlags, ...)
{
    mode_t      fMode;
    va_list     va;
    int         fd;

    va_start(va, fFlags);
    fMode = va_arg(va, mode_t);
    va_end(va);

    do
        fd = WRAP64(open)(pszName, fFlags, fMode);
    while (fd == -1 && SHOULD_RESTART());
    return fd;
}


#if !defined(KBUILD_OS_LINUX) /* no wrapper */
extern int WRAP(mkdir)(const char *pszName, mode_t fMode);
int mkdir(const char *pszName, mode_t fMode)
{
    int rc;
    do
        rc = WRAP(mkdir)(pszName, fMode);
    while (rc == -1 && SHOULD_RESTART());
    return rc;
}
#endif

extern int WRAP64(stat)(const char *pszName, struct stat *pStBuf);
int stat(const char *pszName, struct stat *pStBuf)
{
    int rc;
    do
        rc = WRAP64(stat)(pszName, pStBuf);
    while (rc == -1 && SHOULD_RESTART());
    return rc;
}

extern int WRAP64(lstat)(const char *pszName, struct stat *pStBuf);
int lstat(const char *pszName, struct stat *pStBuf)
{
    int rc;
    do
        rc = WRAP64(lstat)(pszName, pStBuf);
    while (rc == -1 && SHOULD_RESTART());
    return rc;
}

extern ssize_t WRAP(read)(int fd, void *pvBuf, size_t cbBuf);
ssize_t read(int fd, void *pvBuf, size_t cbBuf)
{
    ssize_t cbRead;
    do
        cbRead = WRAP(read)(fd, pvBuf, cbBuf);
    while (cbRead == -1 && SHOULD_RESTART());
    return cbRead;
}

extern ssize_t WRAP(write)(int fd, void *pvBuf, size_t cbBuf);
ssize_t write(int fd, void *pvBuf, size_t cbBuf)
{
    ssize_t cbWritten;
    do
        cbWritten = WRAP(write)(fd, pvBuf, cbBuf);
    while (cbWritten == -1 && SHOULD_RESTART());
    return cbWritten;
}

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

#undef fopen
FILE *fopen(const char *pszName, const char *pszMode)
{
    static union
    {
        FILE *(* pfnFOpen)(const char *, const char *);
        void *pvSym;
    } s_u;
    FILE *pFile;

    if (   !s_u.pfnFOpen
        && dlsym_libc("fopen", &s_u.pvSym) != 0)
        return NULL;

    do
        pFile = s_u.pfnFOpen(pszName, pszMode);
    while (!pFile && SHOULD_RESTART());
    return pFile;
}

#undef fopen64
FILE *fopen64(const char *pszName, const char *pszMode)
{
    static union
    {
        FILE *(* pfnFOpen64)(const char *, const char *);
        void *pvSym;
    } s_u;
    FILE *pFile;

    if (   !s_u.pfnFOpen64
        && dlsym_libc("fopen64", &s_u.pvSym) != 0)
        return NULL;

    do
        pFile = s_u.pfnFOpen64(pszName, pszMode);
    while (!pFile && SHOULD_RESTART());
    return pFile;
}

/** @todo chmod, chown, chgrp, times, and possible some more. */

