/* $Id: ntutimes.c 3094 2017-10-14 03:32:50Z knut.osmundsen@oracle.com $ */
/** @file
 * MSC + NT utimes and lutimes
 */

/*
 * Copyright (c) 2005-2017 knut st. osmundsen <bird-kBuild-spamx@anduin.net>
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
#include "ntutimes.h"

#include "ntstuff.h"
#include "nthlp.h"



static int birdUtimesInternal(const char *pszPath, BirdTimeVal_T paTimes[2], int fFollowLink)
{
    HANDLE hFile = birdOpenFileEx(NULL,
                                  pszPath,
                                  FILE_WRITE_ATTRIBUTES,
                                  FILE_ATTRIBUTE_NORMAL,
                                  FILE_SHARE_DELETE | FILE_SHARE_READ | FILE_SHARE_WRITE,
                                  FILE_OPEN,
                                  FILE_OPEN_FOR_BACKUP_INTENT | (fFollowLink ? 0 : FILE_OPEN_REPARSE_POINT),
                                  OBJ_CASE_INSENSITIVE);
    if (hFile != INVALID_HANDLE_VALUE)
    {
        MY_FILE_BASIC_INFORMATION   Info;
        MY_IO_STATUS_BLOCK          Ios;
        MY_NTSTATUS                 rcNt;

        memset(&Info, 0, sizeof(0));
        if (paTimes)
        {
            Info.LastAccessTime.QuadPart = birdNtTimeFromTimeVal(&paTimes[0]);
            Info.LastWriteTime.QuadPart  = birdNtTimeFromTimeVal(&paTimes[1]);
        }
        else
        {
            /** @todo replace this with something from ntdll  */
            FILETIME Now;
            GetSystemTimeAsFileTime(&Now);
            Info.LastAccessTime.HighPart  = Now.dwHighDateTime;
            Info.LastAccessTime.LowPart   = Now.dwLowDateTime;
            Info.LastWriteTime.HighPart   = Now.dwHighDateTime;
            Info.LastWriteTime.LowPart    = Now.dwLowDateTime;
        }

        Ios.Information = -1;
        Ios.u.Status    = -1;

        rcNt = g_pfnNtSetInformationFile(hFile, &Ios, &Info, sizeof(Info), MyFileBasicInformation);

        birdCloseFile(hFile);

        if (MY_NT_SUCCESS(rcNt))
            return 0;
        birdSetErrnoFromNt(rcNt);
    }
    return -1;
}


int birdUtimes(const char *pszFile, BirdTimeVal_T paTimes[2])
{
    return birdUtimesInternal(pszFile, paTimes, 1 /*fFollowLink*/);
}

int birdLUtimes(const char *pszFile, BirdTimeVal_T paTimes[2])
{
    return birdUtimesInternal(pszFile, paTimes, 0 /*fFollowLink*/);
}

