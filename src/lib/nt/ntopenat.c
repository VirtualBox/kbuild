/* $Id: ntdir.c 3007 2016-11-06 16:46:43Z bird $ */
/** @file
 * MSC + NT openat API.
 */

/*
 * Copyright (c) 2005-2021 knut st. osmundsen <bird-kBuild-spamx@anduin.net>
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
#include <errno.h>
#include <assert.h>
#include <fcntl.h>
#include <io.h>

#include "ntstuff.h"
#include "nthlp.h"
#include "nthlpmisc.h"
#include "ntopenat.h"
#include "ntstat.h"


static int birdOpenInt(const char *pszPath, int fFlags, unsigned __int16 fMode)
{
    /*
     * Try open it using the CRT's open function, but deal with opening
     * directories as the CRT doesn't allow doing that.
     */
    int const iErrnoSaved = errno;
    int iErrno;
    int fd = open(pszPath, fFlags, fMode);
    if (   fd < 0
        && ((iErrno = errno) == EACCES || iErrno == ENOENT || iErrno == EISDIR || iErrno == EINVAL /*CIFS*/)
        && (fFlags & (_O_WRONLY | _O_RDWR | _O_RDONLY)) == _O_RDONLY
        && (fFlags & (_O_CREAT | _O_TRUNC | _O_EXCL)) == 0 )
    {
        BirdStat_T Stat;
        if (!birdStatFollowLink(pszPath, &Stat))
        {
            if (S_ISDIR(Stat.st_mode))
            {
                HANDLE hDir;
                errno = iErrnoSaved;
                hDir = birdOpenFile(pszPath,
                                    FILE_READ_DATA | FILE_READ_ATTRIBUTES | SYNCHRONIZE,
                                    FILE_ATTRIBUTE_NORMAL,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                    FILE_OPEN,
                                    FILE_DIRECTORY_FILE | FILE_OPEN_FOR_BACKUP_INTENT | FILE_SYNCHRONOUS_IO_NONALERT,
                                    OBJ_CASE_INSENSITIVE);
                if (hDir != INVALID_HANDLE_VALUE)
                {
                    fd = _open_osfhandle((intptr_t)hDir, fFlags);
                    if (fd >= 0)
                        return fd;
                    birdCloseFile(hDir);
                }
            }
            else
                errno = EACCES;
        }
        else
            errno = EACCES;
    }
    return fd;
}


/**
 * Implements open.
 */
int birdOpen(const char *pszPath, int fFlags, ...)
{
    unsigned __int16 fMode;
    va_list va;
    va_start(va, fFlags);
    fMode = va_arg(va, unsigned __int16);
    va_end(va);
    return birdOpenInt(pszPath, fFlags, fMode);
}



/**
 * Implements opendir.
 */
int birdOpenAt(int fdDir, const char *pszPath, int fFlags, ...)
{
    HANDLE hDir;

    /*
     * Retrieve the mode mask.
     */
    unsigned __int16 fMode;
    va_list va;
    va_start(va, fFlags);
    fMode = va_arg(va, unsigned __int16);
    va_end(va);

    /*
     * Just call 'open' directly if we can get away with it:
     */
    if (fdDir == AT_FDCWD)
        return birdOpenInt(pszPath, fFlags, fMode);

    if (IS_SLASH(pszPath[0]))
    {
        if (IS_SLASH(pszPath[1]) && !IS_SLASH(pszPath[2]) && pszPath[2] != '\0')
            return birdOpenInt(pszPath, fFlags, fMode);
    }
    else if (IS_ALPHA(pszPath[0]) && pszPath[1] == ':')
    {
        if (IS_SLASH(pszPath[2]))
            return birdOpenInt(pszPath, fFlags, fMode);
        /*
         * Drive letter relative path like "C:kernel32.dll".
         * We could try use fdDir as the CWD here if it refers to the same drive,
         * however that's can be implemented later...
         */
        return birdOpenInt(pszPath, fFlags, fMode);
    }

    /*
     * Otherwise query the path of fdDir and construct an absolute path from all that.
     * This isn't atomic and safe and stuff, but it gets the work done for now.
     */
    hDir = (HANDLE)_get_osfhandle(fdDir);
    if (hDir != INVALID_HANDLE_VALUE)
    {
        /** @todo implement me.   */
        __debugbreak();
        errno = EBADF;
    }
    return -1;
}

