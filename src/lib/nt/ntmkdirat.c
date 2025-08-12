/* $Id: ntmkdirat.c 3682 2025-08-12 23:34:19Z knut.osmundsen@oracle.com $ */
/** @file
 * MSC + NT mkdir & mkdirat.
 */

/*
 * Copyright (c) 2025 knut st. osmundsen <bird-kBuild-spamx@anduin.net>
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
#include "ntmkdirat.h"



/**
 * Implements mkdir.
 */
int birdMkDir(const char *pszPath, unsigned __int16 fMode)
{
    HANDLE hDir = birdOpenFile(pszPath,
                               FILE_LIST_DIRECTORY | SYNCHRONIZE,
                               FILE_ATTRIBUTE_NORMAL,
                               FILE_SHARE_READ | FILE_SHARE_WRITE /*| FILE_SHARE_DELETE*/,
                               FILE_CREATE,
                               FILE_DIRECTORY_FILE | FILE_OPEN_FOR_BACKUP_INTENT | FILE_SYNCHRONOUS_IO_NONALERT,
                               OBJ_CASE_INSENSITIVE);
    if (hDir != INVALID_HANDLE_VALUE)
    {
        birdCloseFile(hDir);
        return 0;
    }
    (void)fMode;
    return -1;
}


/**
 * Implements mkdirat.
 */
int birdMkDirAt(int fdDir, const char *pszPath, unsigned __int16 fMode)
{
    HANDLE hDirParent;

    /*
     * Redirect to regular 'mkdir' when we can.
     */
    if (fdDir == AT_FDCWD)
        return birdMkDir(pszPath, fMode);

    if (IS_SLASH(pszPath[0]))
    {
        if (IS_SLASH(pszPath[1]) && !IS_SLASH(pszPath[2]) && pszPath[2] != '\0')
            return birdMkDir(pszPath, fMode);
    }
    else if (IS_ALPHA(pszPath[0]) && pszPath[1] == ':')
    {
        if (IS_SLASH(pszPath[2]))
            return birdMkDir(pszPath, fMode);
        /*
         * Drive letter relative path like "C:kernel32.dll".
         * We could try use fdDir as the CWD here if it refers to the same drive,
         * however that's can be implemented later...
         */
        return birdMkDir(pszPath, fMode);
    }

    /*
     * Otherwise, we leave to to NT to get this done...  It isn't necessarily as
     * atomic as the opengroup spec wants, but we cannot do much better.
     */
    hDirParent = (HANDLE)_get_osfhandle(fdDir);
    if (hDirParent != INVALID_HANDLE_VALUE)
    {
        HANDLE hDir = birdOpenFileEx(hDirParent,
                                     pszPath,
                                     FILE_LIST_DIRECTORY | SYNCHRONIZE,
                                     FILE_ATTRIBUTE_NORMAL,
                                     FILE_SHARE_READ | FILE_SHARE_WRITE /*| FILE_SHARE_DELETE*/,
                                     FILE_CREATE,
                                     FILE_DIRECTORY_FILE | FILE_OPEN_FOR_BACKUP_INTENT | FILE_SYNCHRONOUS_IO_NONALERT,
                                     OBJ_CASE_INSENSITIVE);
        if (hDir != INVALID_HANDLE_VALUE)
        {
            birdCloseFile(hDir);
            return 0;
        }
    }
    return -1;
}

