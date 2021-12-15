/* $Id: ntunlink.c 3504 2021-12-15 22:50:14Z knut.osmundsen@oracle.com $ */
/** @file
 * MSC + NT unlink and variations.
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
#include "ntunlink.h"

#include "ntstuff.h"
#include "nthlp.h"


static MY_NTSTATUS birdMakeWritable(HANDLE hRoot, MY_UNICODE_STRING *pNtPath)
{
    MY_NTSTATUS rcNt;
    HANDLE      hFile;

    rcNt = birdOpenFileUniStr(hRoot,
                              pNtPath,
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
        memset(&BasicInfo, 0, sizeof(BasicInfo));
        rcNt = g_pfnNtQueryInformationFile(hFile, &Ios, &BasicInfo, sizeof(BasicInfo), MyFileBasicInformation);

        if (MY_NT_SUCCESS(rcNt) && MY_NT_SUCCESS(Ios.u.Status) && BasicInfo.FileAttributes != FILE_ATTRIBUTE_READONLY)
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


static int birdUnlinkInternal(HANDLE hRoot, const char *pszFile, const wchar_t *pwszFile, int fReadOnlyToo, int fFast)
{
    MY_UNICODE_STRING   NtPath;
    int                 rc;

    if (hRoot == INVALID_HANDLE_VALUE)
        hRoot = NULL;
    if (hRoot == NULL)
    {
        if (pwszFile)
            rc = birdDosToNtPathW(pwszFile, &NtPath);
        else
            rc = birdDosToNtPath(pszFile, &NtPath);
    }
    else
    {
        if (pwszFile)
            rc = birdDosToRelativeNtPathW(pwszFile, &NtPath);
        else
            rc = birdDosToRelativeNtPath(pszFile, &NtPath);
    }
    if (rc == 0)
    {
        MY_NTSTATUS rcNt;
        if (fFast)
        {
            /* This uses FILE_DELETE_ON_CLOSE. Probably only suitable when in a hurry... */
            MY_OBJECT_ATTRIBUTES ObjAttr;
            MyInitializeObjectAttributes(&ObjAttr, &NtPath, OBJ_CASE_INSENSITIVE, hRoot, NULL /*pSecAttr*/);
            rcNt = g_pfnNtDeleteFile(&ObjAttr);

            /* In case some file system does things differently than NTFS. */
            if (rcNt == STATUS_CANNOT_DELETE && fReadOnlyToo)
            {
                birdMakeWritable(hRoot, &NtPath);
                rcNt = g_pfnNtDeleteFile(&ObjAttr);
            }
        }
        else
        {
            /* Use the set information stuff. Probably more reliable. */
            HANDLE hFile;
            for (;;)
            {
                rcNt = birdOpenFileUniStr(hRoot,
                                          &NtPath,
                                          DELETE | SYNCHRONIZE,
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
                if (rcNt != STATUS_CANNOT_DELETE || !fReadOnlyToo)
                    break;

                fReadOnlyToo = 0;
                birdMakeWritable(hRoot, &NtPath);
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
    return birdUnlinkInternal(NULL /*hRoot*/, pszFile, NULL /*pwszFile*/, 0 /*fReadOnlyToo*/, 0 /*fFast*/);
}


int birdUnlinkW(const wchar_t *pwszFile)
{
    return birdUnlinkInternal(NULL /*hRoot*/, NULL /*pwszFile*/, pwszFile, 0 /*fReadOnlyToo*/, 0 /*fFast*/);
}


int birdUnlinkEx(void *hRoot, const char *pszFile)
{
    return birdUnlinkInternal((HANDLE)hRoot, pszFile, NULL /*pwszFile*/, 0 /*fReadOnlyToo*/, 0 /*fFast*/);
}


int birdUnlinkExW(void *hRoot, const wchar_t *pwszFile)
{
    return birdUnlinkInternal((HANDLE)hRoot, NULL /*pszFile*/, pwszFile, 0 /*fReadOnlyToo*/, 0 /*fFast*/);
}


int birdUnlinkForced(const char *pszFile)
{
    return birdUnlinkInternal(NULL /*hRoot*/, pszFile, NULL /*pwszFile*/, 1 /*fReadOnlyToo*/, 0 /*fFast*/);
}


int birdUnlinkForcedW(const wchar_t *pwszFile)
{
    return birdUnlinkInternal(NULL /*hRoot*/, NULL /*pszFile*/, pwszFile, 1 /*fReadOnlyToo*/, 0 /*fFast*/);
}


int birdUnlinkForcedEx(void *hRoot, const char *pszFile)
{
    return birdUnlinkInternal((HANDLE)hRoot, pszFile, NULL /*pwszFile*/, 1 /*fReadOnlyToo*/, 0 /*fFast*/);
}


int birdUnlinkForcedExW(void *hRoot, const wchar_t *pwszFile)
{
    return birdUnlinkInternal((HANDLE)hRoot, NULL /*pszFile*/, pwszFile, 1 /*fReadOnlyToo*/, 0 /*fFast*/);
}


int birdUnlinkForcedFast(const char *pszFile)
{
    return birdUnlinkInternal(NULL /*hRoot*/, pszFile, NULL /*pwszFile*/, 1 /*fReadOnlyToo*/, 1 /*fFast*/);
}


int birdUnlinkForcedFastW(const wchar_t *pwszFile)
{
    return birdUnlinkInternal(NULL /*hRoot*/, NULL /*pszFile*/, pwszFile, 1 /*fReadOnlyToo*/, 1 /*fFast*/);
}


int birdUnlinkForcedFastEx(void *hRoot, const char *pszFile)
{
    return birdUnlinkInternal((HANDLE)hRoot, pszFile, NULL /*pwszFile*/, 1 /*fReadOnlyToo*/, 1 /*fFast*/);
}


int birdUnlinkForcedFastExW(void *hRoot, const wchar_t *pwszFile)
{
    return birdUnlinkInternal((HANDLE)hRoot, NULL /*pszFile*/, pwszFile, 1 /*fReadOnlyToo*/, 1 /*fFast*/);
}

