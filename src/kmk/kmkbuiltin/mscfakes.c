/* $Id: mscfakes.c 3682 2025-08-12 23:34:19Z knut.osmundsen@oracle.com $ */
/** @file
 * Fake Unix stuff for MSC.
 */

/*
 * Copyright (c) 2005-2015 knut st. osmundsen <bird-kBuild-spamx@anduin.net>
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
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <io.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/timeb.h>
#include "err.h"
#include "mscfakes.h"

#include "nt/ntutimes.h"
#undef utimes
#undef lutimes
#include "nt/ntmkdirat.h"
#undef mkdir
#undef mkdirat

#include "console.h"



/*******************************************************************************
*   Internal Functions                                                         *
*******************************************************************************/
static BOOL isPipeFd(int fd);


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


int
birdSetErrno(unsigned dwErr)
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
#ifdef EMLINK
        case ERROR_TOO_MANY_LINKS:          errno = EMLINK; break;
#endif
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
        rc = birdSetErrno(GetLastError());
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
        {
            fAttr &= ~FILE_ATTRIBUTE_READONLY;
            if (fAttr == 0)
                fAttr = FILE_ATTRIBUTE_NORMAL;
        }
        else
        {
            fAttr &= ~FILE_ATTRIBUTE_NORMAL;
            fAttr |= FILE_ATTRIBUTE_READONLY;
        }
        if (!SetFileAttributes(pszPath, fAttr))
            rc = birdSetErrno(GetLastError());
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
    int fMustBeDir;
    char *pszPathFree = msc_fix_path(&pszPath, &fMustBeDir);

    /*
     * Get the current attributes.
     */
    DWORD fAttr = GetFileAttributes(pszPath);
    if (fAttr == INVALID_FILE_ATTRIBUTES)
        rc = birdSetErrno(GetLastError());
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
        {
            fAttr &= ~FILE_ATTRIBUTE_READONLY;
            if (fAttr == 0)
                fAttr = FILE_ATTRIBUTE_NORMAL;
        }
        else
        {
            fAttr &= ~FILE_ATTRIBUTE_NORMAL;
            fAttr |= FILE_ATTRIBUTE_READONLY;
        }
        if (!SetFileAttributes(pszPath, fAttr))
            rc = birdSetErrno(GetLastError());
    }

    if (pszPathFree)
    {
        int saved_errno = errno;
        free(pszPathFree);
        errno = saved_errno;
    }
    return rc;
}


typedef BOOL (WINAPI *PFNCREATEHARDLINKA)(LPCSTR, LPCSTR, LPSECURITY_ATTRIBUTES);
int link(const char *pszDst, const char *pszLink)
{
    static PFNCREATEHARDLINKA   s_pfnCreateHardLinkA = NULL;
    static int                  s_fTried = FALSE;

    /* The API was introduced in Windows 2000, so resolve it dynamically. */
    if (!s_pfnCreateHardLinkA)
    {
        if (!s_fTried)
        {
            HMODULE hmod = LoadLibrary("KERNEL32.DLL");
            if (hmod)
                *(FARPROC *)&s_pfnCreateHardLinkA = GetProcAddress(hmod, "CreateHardLinkA");
            s_fTried = TRUE;
        }
        if (!s_pfnCreateHardLinkA)
        {
            errno = ENOSYS;
            return -1;
        }
    }

    if (s_pfnCreateHardLinkA(pszLink, pszDst, NULL))
        return 0;
    return birdSetErrno(GetLastError());
}


int mkdir_msc(const char *path, mode_t mode)
{
#if 0
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
#else
    return birdMkDir(path, mode);
#endif
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
        fd = open(temp, _O_EXCL | _O_CREAT | _O_BINARY | _O_RDWR | KMK_OPEN_NO_INHERIT, 0777);
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

    fprintf(stderr, "warning: symlink() is available on this version of Windows!\n");
    errno = ENOSYS;
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


/* We override the libc write function (in our modules only, unfortunately) so
   we can kludge our way around a ENOSPC problem observed on build servers
   capturing STDOUT and STDERR via pipes.  Apparently this may happen when the
   pipe buffer is full, even with the mscfake_init hack in place.

   XXX: Probably need to hook into fwrite as well. */
ssize_t msc_write(int fd, const void *pvSrc, size_t cbSrc)
{
#define MSC_WRITE_MAX_CHUNK (UINT_MAX / 32)
    ssize_t cbRet;
    if (cbSrc <= MSC_WRITE_MAX_CHUNK)
    {
        /* Console output optimization: */
        if (cbSrc > 0 && is_console(fd))
            return maybe_con_write(fd, pvSrc, cbSrc);

#ifndef MSC_WRITE_TEST
        cbRet = _write(fd, pvSrc, (unsigned int)cbSrc);
#else
        cbRet = -1; errno = ENOSPC;
#endif
        if (cbRet < 0)
        {
            /* ENOSPC on pipe kludge. */
            unsigned int cbLimit;
            int cSinceLastSuccess;

            if (cbSrc == 0)
                return 0;
            if (errno != ENOSPC)
                return -1;
#ifndef MSC_WRITE_TEST
            if (!isPipeFd(fd))
            {
                errno = ENOSPC;
                return -1;
            }
#endif

            /* Likely a full pipe buffer, try write smaller amounts and do some
               sleeping inbetween each unsuccessful one. */
            cbLimit = (unsigned)(cbSrc / 4);
            if (cbLimit < 4)
                cbLimit = 4;
            else if (cbLimit > 512)
                cbLimit = 512;
            cSinceLastSuccess = 0;
            cbRet = 0;
#ifdef MSC_WRITE_TEST
            cbLimit = 4;
#endif

            while ((ssize_t)cbSrc > 0)
            {
                unsigned int cbAttempt = cbSrc > cbLimit ? cbLimit : (unsigned int)cbSrc;
                ssize_t cbActual = _write(fd, pvSrc, cbAttempt);
                if (cbActual > 0)
                {
                    /* For some reason, it seems like we cannot trust _write to return
                       a number that's less or equal to the number of bytes we passed
                       in to the call.  (Also reason for signed check in loop.) */
                    if (cbActual > cbAttempt)
                        cbActual = cbAttempt;

                    pvSrc  = (char *)pvSrc + cbActual;
                    cbSrc -= cbActual;
                    cbRet += cbActual;
#ifndef MSC_WRITE_TEST
                    if (cbLimit < 32)
                        cbLimit = 32;
#endif
                    cSinceLastSuccess = 0;
                }
                else if (errno != ENOSPC)
                    return -1;
                else
                {
                    /* Delay for about 30 seconds, then just give up. */
                    cSinceLastSuccess++;
                    if (cSinceLastSuccess > 1860)
                        return -1;
                    if (cSinceLastSuccess <= 2)
                        Sleep(0);
                    else if (cSinceLastSuccess <= 66)
                    {
                        if (cbLimit >= 8)
                            cbLimit /= 2; /* Just in case the pipe buffer is very very small. */
                        Sleep(1);
                    }
                    else
                        Sleep(16);
                }
            }
        }
    }
    else
    {
        /*
         * Type limit exceeded. Split the job up.
         */
        cbRet = 0;
        while (cbSrc > 0)
        {
            size_t  cbToWrite = cbSrc > MSC_WRITE_MAX_CHUNK ? MSC_WRITE_MAX_CHUNK : cbSrc;
            ssize_t cbWritten = msc_write(fd, pvSrc, cbToWrite);
            if (cbWritten > 0)
            {
                pvSrc  = (char *)pvSrc + (size_t)cbWritten;
                cbSrc -= (size_t)cbWritten;
                cbRet += (size_t)cbWritten;
            }
            else if (cbWritten == 0 || cbRet > 0)
                break;
            else
                return -1;
        }
    }
    return cbRet;
}

ssize_t writev(int fd, const struct iovec *vector, int count)
{
    ssize_t size = 0;
    if (count > 0)
    {
        int i;

        /* To get consistent console output, we must try combine the segments
           when outputing to the console. */
        if (count > 1 && is_console(fd))
        {
            char   *pbTmp;
            ssize_t cbTotal;
            if (count == 1)
                return maybe_con_write(fd, vector[0].iov_base, (int)vector[0].iov_len);

            cbTotal = 0;
            for (i = 0; i < count; i++)
                cbTotal += vector[i].iov_len;
            pbTmp = malloc(cbTotal);
            if (pbTmp)
            {
                char *pbCur = pbTmp;
                for (i = 0; i < count; i++)
                {
                    memcpy(pbCur, vector[i].iov_base, vector[i].iov_len);
                    pbCur += vector[i].iov_len;
                }
                size = maybe_con_write(fd, pbTmp, cbTotal);
                free(pbTmp);
                return size;
            }

            /* fall back on segment by segment output. */
        }

        for (i = 0; i < count; i++)
        {
            int cb = msc_write(fd, vector[i].iov_base, (int)vector[i].iov_len);
            if (cb < 0)
                return cb;
            size += cb;
        }
    }
    return size;
}


intmax_t strtoimax(const char *nptr, char **endptr, int base)
{
    if (*nptr != '-')
        return _strtoui64(nptr, endptr, base);
    return -(intmax_t)_strtoui64(nptr + 1, endptr, base);
}


uintmax_t strtoumax(const char *nptr, char **endptr, int base)
{
    return _strtoui64(nptr, endptr, base);
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
        rc = vsnprintf(psz, cb, fmt, va2);
        va_end(va2);
#else
        va2 = va;
        rc = vsnprintf(psz, cb, fmt, va2);
#endif
        if (rc < 0 || (size_t)rc < cb)
            break;
        cb *= 2;
        free(psz);
    }

    *strp = psz;
    return rc;
}


int utimes(const char *pszPath, const struct msc_timeval *paTimes)
{
    if (paTimes)
    {
        BirdTimeVal_T aTimes[2];
        aTimes[0].tv_sec  = paTimes[0].tv_sec;
        aTimes[0].tv_usec = paTimes[0].tv_usec;
        aTimes[1].tv_sec  = paTimes[1].tv_sec;
        aTimes[1].tv_usec = paTimes[1].tv_usec;
        return birdUtimes(pszPath, aTimes);
    }
    return birdUtimes(pszPath, NULL);
}


int lutimes(const char *pszPath, const struct msc_timeval *paTimes)
{
    if (paTimes)
    {
        BirdTimeVal_T aTimes[2];
        aTimes[0].tv_sec  = paTimes[0].tv_sec;
        aTimes[0].tv_usec = paTimes[0].tv_usec;
        aTimes[1].tv_sec  = paTimes[1].tv_sec;
        aTimes[1].tv_usec = paTimes[1].tv_usec;
        return birdUtimes(pszPath, aTimes);
    }
    return birdUtimes(pszPath, NULL);
}


int gettimeofday(struct msc_timeval *pNow, void *pvIgnored)
{
    struct __timeb64 Now;
    int rc = _ftime64_s(&Now);
    if (rc == 0)
    {
        pNow->tv_sec  = Now.time;
        pNow->tv_usec = Now.millitm * 1000;
        return 0;
    }
    errno = rc;
    return -1;
}


struct tm *localtime_r(const __time64_t *pNow, struct tm *pResult)
{
    int rc = _localtime64_s(pResult, pNow);
    if (rc == 0)
        return pResult;
    errno = rc;
    return NULL;
}


__time64_t timegm(struct tm *pNow)
{
    return _mkgmtime64(pNow);
}


/**
 * Checks if the given file descriptor is a pipe or not.
 *
 * @returns TRUE if pipe, FALSE if not.
 * @param   fd                  The libc file descriptor number.
 */
static BOOL isPipeFd(int fd)
{
    /* Is pipe? */
    HANDLE hFile = (HANDLE)_get_osfhandle(fd);
    if (hFile != INVALID_HANDLE_VALUE)
    {
        DWORD fType = GetFileType(hFile);
        fType &= ~FILE_TYPE_REMOTE;
        if (fType == FILE_TYPE_PIPE)
            return TRUE;
    }
    return FALSE;
}


/**
 * This is a kludge to make pipe handles blocking.
 *
 * @returns TRUE if it's now blocking, FALSE if not a pipe or we failed to fix
 *          the blocking mode.
 * @param   fd                  The libc file descriptor number.
 */
static BOOL makePipeBlocking(int fd)
{
    if (isPipeFd(fd))
    {
        /* Try fix it. */
        HANDLE hFile = (HANDLE)_get_osfhandle(fd);
        DWORD fState = 0;
        if (GetNamedPipeHandleState(hFile, &fState, NULL, NULL, NULL, NULL,  0))
        {
            fState &= ~PIPE_NOWAIT;
            fState |= PIPE_WAIT;
            if (SetNamedPipeHandleState(hFile, &fState, NULL, NULL))
                return TRUE;
        }
    }
    return FALSE;
}


/**
 * Initializes the msc fake stuff.
 * @returns 0 on success (non-zero would indicate failure, see rterr.h).
 */
int mscfake_init(void)
{
    /*
     * Kludge against _write returning ENOSPC on non-blocking pipes.
     */
    makePipeBlocking(STDOUT_FILENO);
    makePipeBlocking(STDERR_FILENO);

    return 0;
}

/*
 * Do this before main is called.
 */
#pragma section(".CRT$XIA", read)
#pragma section(".CRT$XIU", read)
#pragma section(".CRT$XIZ", read)
typedef int (__cdecl *PFNCRTINIT)(void);
static __declspec(allocate(".CRT$XIU")) PFNCRTINIT g_MscFakeInitVectorEntry = mscfake_init;

