/* $Id: mscfakes.c 2113 2008-12-25 13:21:58Z knut.osmundsen@oracle.com $ */
/** @file
 * Fake Unix stuff for MSC.
 */

/*
 * Copyright (c) 2005-2008 knut st. osmundsen <bird-src-spam@anduin.net>
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
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <io.h>
#include <fcntl.h>
#include <sys/stat.h>
#include "err.h"
#include "mscfakes.h"

#define timeval windows_timeval
#include <Windows.h>
#undef timeval


/**
 * Makes corrections to a directory path that ends with a trailing slash.
 *
 * @returns temporary buffer to free.
 * @param   ppszPath    The path pointer. This is updated when necessary.
 * @param   pfMustBeDir This is set if it must be a directory, otherwise it's cleared.
 */
static char *
msc_fix_path(const char **ppszPath, int *pfMustBeDir)
{
    const char *pszPath = *ppszPath;
    const char *psz;
    char *pszNew;
    *pfMustBeDir = 0;

    /*
     * Skip any compusory trailing slashes
     */
    if (pszPath[0] == '/' || pszPath[0] == '\\')
    {
        if (   (pszPath[1] == '/' || pszPath[1] == '\\')
            &&  pszPath[2] != '/'
            &&  pszPath[2] != '\\')
            /* unc */
            pszPath += 2;
        else
            /* root slash(es) */
            pszPath++;
    }
    else if (   isalpha(pszPath[0])
             && pszPath[1] == ':')
    {
        if (pszPath[2] == '/' || pszPath[2] == '\\')
            /* drive w/ slash */
            pszPath += 3;
        else
            /* drive relative path. */
            pszPath += 2;
    }
    /* else: relative path, no skipping necessary. */

    /*
     * Any trailing slashes to drop off?
     */
    psz = strchr(pszPath, '\0');
    if (pszPath <= psz)
        return NULL;
    if (   psz[-1] != '/'
        || psz[-1] != '\\')
        return NULL;

    /* figure how many, make a copy and strip them off. */
    while (     psz > pszPath
           &&   (   psz[-1] == '/'
                 || psz[-1] == '\\'))
        psz--;
    pszNew = strdup(pszPath);
    pszNew[psz - pszPath] = '\0';

    *pfMustBeDir = 1;
    *ppszPath = pszNew; /* use this one */
    return pszNew;
}


static int
msc_set_errno(DWORD dwErr)
{
    switch (dwErr)
    {
        default:
        case ERROR_INVALID_FUNCTION:        errno = EINVAL; break;
        case ERROR_FILE_NOT_FOUND:          errno = ENOENT; break;
        case ERROR_PATH_NOT_FOUND:          errno = ENOENT; break;
        case ERROR_TOO_MANY_OPEN_FILES:     errno = EMFILE; break;
        case ERROR_ACCESS_DENIED:           errno = EACCES; break;
        case ERROR_INVALID_HANDLE:          errno = EBADF; break;
        case ERROR_ARENA_TRASHED:           errno = ENOMEM; break;
        case ERROR_NOT_ENOUGH_MEMORY:       errno = ENOMEM; break;
        case ERROR_INVALID_BLOCK:           errno = ENOMEM; break;
        case ERROR_BAD_ENVIRONMENT:         errno = E2BIG; break;
        case ERROR_BAD_FORMAT:              errno = ENOEXEC; break;
        case ERROR_INVALID_ACCESS:          errno = EINVAL; break;
        case ERROR_INVALID_DATA:            errno = EINVAL; break;
        case ERROR_INVALID_DRIVE:           errno = ENOENT; break;
        case ERROR_CURRENT_DIRECTORY:       errno = EACCES; break;
        case ERROR_NOT_SAME_DEVICE:         errno = EXDEV; break;
        case ERROR_NO_MORE_FILES:           errno = ENOENT; break;
        case ERROR_LOCK_VIOLATION:          errno = EACCES; break;
        case ERROR_BAD_NETPATH:             errno = ENOENT; break;
        case ERROR_NETWORK_ACCESS_DENIED:   errno = EACCES; break;
        case ERROR_BAD_NET_NAME:            errno = ENOENT; break;
        case ERROR_FILE_EXISTS:             errno = EEXIST; break;
        case ERROR_CANNOT_MAKE:             errno = EACCES; break;
        case ERROR_FAIL_I24:                errno = EACCES; break;
        case ERROR_INVALID_PARAMETER:       errno = EINVAL; break;
        case ERROR_NO_PROC_SLOTS:           errno = EAGAIN; break;
        case ERROR_DRIVE_LOCKED:            errno = EACCES; break;
        case ERROR_BROKEN_PIPE:             errno = EPIPE; break;
        case ERROR_DISK_FULL:               errno = ENOSPC; break;
        case ERROR_INVALID_TARGET_HANDLE:   errno = EBADF; break;
        case ERROR_WAIT_NO_CHILDREN:        errno = ECHILD; break;
        case ERROR_CHILD_NOT_COMPLETE:      errno = ECHILD; break;
        case ERROR_DIRECT_ACCESS_HANDLE:    errno = EBADF; break;
        case ERROR_NEGATIVE_SEEK:           errno = EINVAL; break;
        case ERROR_SEEK_ON_DEVICE:          errno = EACCES; break;
        case ERROR_DIR_NOT_EMPTY:           errno = ENOTEMPTY; break;
        case ERROR_NOT_LOCKED:              errno = EACCES; break;
        case ERROR_BAD_PATHNAME:            errno = ENOENT; break;
        case ERROR_MAX_THRDS_REACHED:       errno = EAGAIN; break;
        case ERROR_LOCK_FAILED:             errno = EACCES; break;
        case ERROR_ALREADY_EXISTS:          errno = EEXIST; break;
        case ERROR_FILENAME_EXCED_RANGE:    errno = ENOENT; break;
        case ERROR_NESTING_NOT_ALLOWED:     errno = EAGAIN; break;
    }

    return -1;
}

char *dirname(char *path)
{
    /** @todo later */
    return path;
}


int lchmod(const char *pszPath, mode_t mode)
{
    int rc = 0;
    int fMustBeDir;
    char *pszPathFree = msc_fix_path(&pszPath, &fMustBeDir);

    /*
     * Get the current attributes
     */
    DWORD fAttr = GetFileAttributes(pszPath);
    if (fAttr == INVALID_FILE_ATTRIBUTES)
        rc = msc_set_errno(GetLastError());
    else if (fMustBeDir & !(fAttr & FILE_ATTRIBUTE_DIRECTORY))
    {
        errno = ENOTDIR;
        rc = -1;
    }
    else
    {
        /*
         * Modify the attributes and try set them.
         */
        if (mode & _S_IWRITE)
            fAttr &= ~FILE_ATTRIBUTE_READONLY;
        else
            fAttr |= FILE_ATTRIBUTE_READONLY;
        if (!SetFileAttributes(pszPath, fAttr))
            rc = msc_set_errno(GetLastError());
    }

    if (pszPathFree)
    {
        int saved_errno = errno;
        free(pszPathFree);
        errno = saved_errno;
    }
    return rc;
}


int msc_chmod(const char *pszPath, mode_t mode)
{
    int rc = 0;
    int saved_errno;
    int fMustBeDir;
    char *pszPathFree = msc_fix_path(&pszPath, &fMustBeDir);

    /*
     * Get the current attributes.
     */
    DWORD fAttr = GetFileAttributes(pszPath);
    if (fAttr == INVALID_FILE_ATTRIBUTES)
        rc = msc_set_errno(GetLastError());
    else if (fMustBeDir & !(fAttr & FILE_ATTRIBUTE_DIRECTORY))
    {
        errno = ENOTDIR;
        rc = -1;
    }
    else if (fAttr & FILE_ATTRIBUTE_REPARSE_POINT)
    {
        errno = ENOSYS; /** @todo resolve symbolic link / rewrite to NtSetInformationFile. */
        rc = -1;
    }
    else
    {
        /*
         * Modify the attributes and try set them.
         */
        if (mode & _S_IWRITE)
            fAttr &= ~FILE_ATTRIBUTE_READONLY;
        else
            fAttr |= FILE_ATTRIBUTE_READONLY;
        if (!SetFileAttributes(pszPath, fAttr))
            rc = msc_set_errno(GetLastError());
    }

    if (pszPathFree)
    {
        int saved_errno = errno;
        free(pszPathFree);
        errno = saved_errno;
    }
    return rc;
}


int link(const char *pszDst, const char *pszLink)
{
    errno = ENOSYS;
    err(1, "link() is not implemented on windows!");
    return -1;
}


int mkdir_msc(const char *path, mode_t mode)
{
    int rc = (mkdir)(path);
    if (rc)
    {
        size_t len = strlen(path);
        if (len > 0 && (path[len - 1] == '/' || path[len - 1] == '\\'))
        {
            char *str = strdup(path);
            while (len > 0 && (str[len - 1] == '/' || str[len - 1] == '\\'))
                str[--len] = '\0';
            rc = (mkdir)(str);
            free(str);
        }
    }
    return rc;
}

int rmdir_msc(const char *path)
{
    int rc = (rmdir)(path);
    if (rc)
    {
        size_t len = strlen(path);
        if (len > 0 && (path[len - 1] == '/' || path[len - 1] == '\\'))
        {
            char *str = strdup(path);
            while (len > 0 && (str[len - 1] == '/' || str[len - 1] == '\\'))
                str[--len] = '\0';
            rc = (rmdir)(str);
            free(str);
        }
    }
    return rc;
}


static int doname(char *pszX, char *pszEnd)
{
    static char s_szChars[] = "Xabcdefghijklmnopqrstuwvxyz1234567890";
    int rc = 0;
    do
    {
        char ch;

        pszEnd++;
        ch = *(strchr(s_szChars, *pszEnd) + 1);
        if (ch)
        {
            *pszEnd = ch;
            return 0;
        }
        *pszEnd = 'a';
    } while (pszEnd != pszX);
    return 1;
}


int mkstemp(char *temp)
{
    char *pszX = strchr(temp, 'X');
    char *pszEnd = strchr(pszX, '\0');
    int cTries = 1000;
    while (--cTries > 0)
    {
        int fd;
        if (doname(pszX, pszEnd))
            return -1;
        fd = open(temp, _O_EXCL | _O_CREAT | _O_BINARY | _O_RDWR, 0777);
        if (fd >= 0)
            return fd;
    }
    return -1;
}


/** Unix to DOS. */
static char *fix_slashes(char *psz)
{
    char *pszRet = psz;
    for (; *psz; psz++)
        if (*psz == '/')
            *psz = '\\';
    return pszRet;
}


/** Calcs the SYMBOLIC_LINK_FLAG_DIRECTORY flag for CreatesymbolcLink.  */
static DWORD is_directory(const char *pszPath, const char *pszRelativeTo)
{
    size_t cchPath = strlen(pszPath);
    struct stat st;
    if (cchPath > 0 && pszPath[cchPath - 1] == '\\' || pszPath[cchPath - 1] == '/')
        return 1; /* SYMBOLIC_LINK_FLAG_DIRECTORY */

    if (stat(pszPath, &st))
    {
        size_t cchRelativeTo = strlen(pszRelativeTo);
        char *psz = malloc(cchPath + cchRelativeTo + 4);
        memcpy(psz, pszRelativeTo, cchRelativeTo);
        memcpy(psz + cchRelativeTo, "\\", 1);
        memcpy(psz + cchRelativeTo + 1, pszPath, cchPath + 1);
        if (stat(pszPath, &st))
            st.st_mode = _S_IFREG;
        free(psz);
    }

    return (st.st_mode & _S_IFMT) == _S_IFDIR ? 1 : 0;
}


int symlink(const char *pszDst, const char *pszLink)
{
    static BOOLEAN (WINAPI *s_pfnCreateSymbolicLinkA)(LPCSTR, LPCSTR, DWORD) = 0;
    static BOOL s_fTried = FALSE;

    if (!s_fTried)
    {
        HMODULE hmod = LoadLibrary("KERNEL32.DLL");
        if (hmod)
            *(FARPROC *)&s_pfnCreateSymbolicLinkA = GetProcAddress(hmod, "CreateSymbolicLinkA");
        s_fTried = TRUE;
    }

    if (s_pfnCreateSymbolicLinkA)
    {
        char *pszDstCopy = fix_slashes(strdup(pszDst));
        char *pszLinkCopy = fix_slashes(strdup(pszLink));
        BOOLEAN fRc = s_pfnCreateSymbolicLinkA(pszLinkCopy, pszDstCopy,
                                               is_directory(pszDstCopy, pszLinkCopy));
        DWORD err = GetLastError();
        free(pszDstCopy);
        free(pszLinkCopy);
        if (fRc)
            return 0;
        switch (err)
        {
            case ERROR_NOT_SUPPORTED:       errno = ENOSYS; break;
            case ERROR_ALREADY_EXISTS:
            case ERROR_FILE_EXISTS:         errno = EEXIST; break;
            case ERROR_DIRECTORY:           errno = ENOTDIR; break;
            case ERROR_ACCESS_DENIED:
            case ERROR_PRIVILEGE_NOT_HELD:  errno = EPERM; break;
            default:                        errno = EINVAL; break;
        }
        return -1;
    }

    errno = ENOSYS;
    err(1, "symlink() is not implemented on windows!");
    return -1;
}


#if _MSC_VER < 1400
int snprintf(char *buf, size_t size, const char *fmt, ...)
{
    int cch;
    va_list args;
    va_start(args, fmt);
    cch = vsprintf(buf, fmt, args);
    va_end(args);
    return cch;
}
#endif


int utimes(const char *pszPath, const struct timeval *paTimes)
{
    /** @todo implement me! */
    return 0;
}


int writev(int fd, const struct iovec *vector, int count)
{
    int size = 0;
    int i;
    for (i = 0; i < count; i++)
    {
        int cb = (int)write(fd, vector[i].iov_base, (int)vector[i].iov_len);
        if (cb < 0)
            return -1;
        size += cb;
    }
    return size;
}


intmax_t strtoimax(const char *nptr, char **endptr, int base)
{
    return strtol(nptr, endptr, base); /** @todo fix this. */
}


uintmax_t strtoumax(const char *nptr, char **endptr, int base)
{
    return strtoul(nptr, endptr, base); /** @todo fix this. */
}


int asprintf(char **strp, const char *fmt, ...)
{
    int rc;
    va_list va;
    va_start(va, fmt);
    rc = vasprintf(strp, fmt, va);
    va_end(va);
    return rc;
}


int vasprintf(char **strp, const char *fmt, va_list va)
{
    int rc;
    char *psz;
    size_t cb = 1024;

    *strp = NULL;
    for (;;)
    {
        va_list va2;

        psz = malloc(cb);
        if (!psz)
            return -1;

#ifdef va_copy
        va_copy(va2, va);
        rc = snprintf(psz, cb, fmt, va2);
        va_end(vaCopy);
#else
        va2 = va;
        rc = snprintf(psz, cb, fmt, va2);
#endif
        if (rc < 0 || (size_t)rc < cb)
            break;
        cb *= 2;
        free(psz);
    }

    *strp = psz;
    return rc;
}


#undef stat
/*
 * Workaround for directory names with trailing slashes.
 * Added by bird reasons stated.
 */
int
my_other_stat(const char *path, struct stat *st)
{
    int rc = stat(path, st);
    if (    rc != 0
        &&  errno == ENOENT
        &&  *path != '\0')
    {
        char *slash = strchr(path, '\0') - 1;
        if (*slash == '/' || *slash == '\\')
        {
            size_t len_path = slash - path + 1;
            char *tmp = alloca(len_path + 4);
            memcpy(tmp, path, len_path);
            tmp[len_path] = '.';
            tmp[len_path + 1] = '\0';
            errno = 0;
            rc = stat(tmp, st);
            if (    rc == 0
                &&  !S_ISDIR(st->st_mode))
            {
                errno = ENOTDIR;
                rc = -1;
            }
        }
    }
    return rc;
}

