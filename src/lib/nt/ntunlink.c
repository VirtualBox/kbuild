/* $Id: ntunlink.c 2713 2013-11-21 21:11:00Z knut.osmundsen@oracle.com $ */
/** @file
 * MSC + NT unlink and variations.
 */

/*
 * Copyright (c) 2005-2013 knut st. osmundsen <bird-kBuild-spamx@anduin.net>
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
#include <stdio.h>
#include <errno.h>
#include <malloc.h>

#include "ntstuff.h"
#include "nthlp.h"


static MY_NTSTATUS birdMakeWritable(MY_UNICODE_STRING *pNtPath)
{
    MY_NTSTATUS rcNt;
    HANDLE      hFile;

    rcNt = birdOpenFileUniStr(pNtPath,
                              FILE_WRITE_ATTRIBUTES | FILE_READ_ATTRIBUTES | SYNCHRONIZE,
                              FILE_ATTRIBUTE_NORMAL,
                              FILE_SHARE_DELETE | FILE_SHARE_READ | FILE_SHARE_WRITE,
                              FILE_OPEN,
                              FILE_OPEN_FOR_BACKUP_INTENT | FILE_SYNCHRONOUS_IO_NONALERT,
                              OBJ_CASE_INSENSITIVE,
                              &hFile);
    if (MY_NT_SUCCESS(rcNt))
    {
        MY_FILE_BASIC_INFORMATION   BasicInfo;
        MY_IO_STATUS_BLOCK          Ios;
        DWORD                       dwAttr;

        Ios.Information = -1;
        Ios.u.Status    = -1;
        rcNt = g_pfnNtQueryInformationFile(hFile, &Ios, &BasicInfo, sizeof(BasicInfo), MyFileBasicInformation);

        if (MY_NT_SUCCESS(rcNt) && MY_NT_SUCCESS(Ios.u.Status))
            dwAttr = BasicInfo.FileAttributes & ~FILE_ATTRIBUTE_READONLY;
        else
            dwAttr = FILE_ATTRIBUTE_NORMAL;
        memset(&BasicInfo, 0, sizeof(BasicInfo));
        BasicInfo.FileAttributes = dwAttr;

        Ios.Information = -1;
        Ios.u.Status    = -1;
        rcNt = g_pfnNtSetInformationFile(hFile, &Ios, &BasicInfo, sizeof(BasicInfo), MyFileBasicInformation);

        birdCloseFile(hFile);
    }

    return rcNt;
}


static int birdUnlinkInternal(const char *pszFile, int fReadOnlyToo, int fFast)
{
    MY_UNICODE_STRING   NtPath;
    int                 rc;

    rc = birdDosToNtPath(pszFile, &NtPath);
    if (rc == 0)
    {
        MY_NTSTATUS rcNt;
        if (fFast)
        {
            /* This uses FILE_DELETE_ON_CLOSE. Probably only suitable when in a hurry... */
            MY_OBJECT_ATTRIBUTES ObjAttr;
            MyInitializeObjectAttributes(&ObjAttr, &NtPath, OBJ_CASE_INSENSITIVE, NULL /*hRoot*/, NULL /*pSecAttr*/);
            rcNt = g_pfnNtDeleteFile(&ObjAttr);

            /* In case some file system does things differently than NTFS. */
            if (rcNt == STATUS_CANNOT_DELETE)
            {
                birdMakeWritable(&NtPath);
                rcNt = g_pfnNtDeleteFile(&ObjAttr);
            }
        }
        else
        {
            /* Use the set information stuff. Probably more reliable. */
            HANDLE hFile;
            int    fMayTryAgain = 1;
            for (;;)
            {
                rcNt = birdOpenFileUniStr(&NtPath,
                                          DELETE,
                                          FILE_ATTRIBUTE_NORMAL,
                                          FILE_SHARE_DELETE | FILE_SHARE_READ | FILE_SHARE_WRITE,
                                          FILE_OPEN,
                                          FILE_OPEN_FOR_BACKUP_INTENT | FILE_OPEN_REPARSE_POINT,
                                          OBJ_CASE_INSENSITIVE,
                                          &hFile);
                if (MY_NT_SUCCESS(rcNt))
                {
                    MY_FILE_DISPOSITION_INFORMATION DispInfo;
                    MY_IO_STATUS_BLOCK              Ios;

                    DispInfo.DeleteFile = TRUE;

                    Ios.Information = -1;
                    Ios.u.Status    = -1;

                    rcNt = g_pfnNtSetInformationFile(hFile, &Ios, &DispInfo, sizeof(DispInfo), MyFileDispositionInformation);

                    birdCloseFile(hFile);
                }
                if (rcNt != STATUS_CANNOT_DELETE || !fMayTryAgain)
                    break;

                fMayTryAgain = 0;
                birdMakeWritable(&NtPath);
            }
        }

        birdFreeNtPath(&NtPath);

        if (MY_NT_SUCCESS(rcNt))
            rc = 0;
        else
            rc = birdSetErrnoFromNt(rcNt);
    }
    return rc;
}


int birdUnlink(const char *pszFile)
{
    return birdUnlinkInternal(pszFile, 0 /*fReadOnlyToo*/, 0 /*fFast*/);
}


int birdUnlinkForced(const char *pszFile)
{
    return birdUnlinkInternal(pszFile, 1 /*fReadOnlyToo*/, 0 /*fFast*/);
}


int birdUnlinkForcedFast(const char *pszFile)
{
    return birdUnlinkInternal(pszFile, 1 /*fReadOnlyToo*/, 1 /*fFast*/);
}

