/* $Id: ntunlink.c 3682 2025-08-12 23:34:19Z knut.osmundsen@oracle.com $ */
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
#include <io.h>  /* for _get_osfhandle */
#include "ntunlink.h"

#include "ntstuff.h"
#include "nthlp.h"
#include "nthlpmisc.h"


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


static int birdUnlinkInternal(HANDLE hRoot, const char *pszFile, const wchar_t *pwszFile, int fReadOnlyToo, int fFast, int fRmDir)
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
            /*
             * This uses FILE_DELETE_ON_CLOSE.
             *
             * It is only suitable if in a hurry and when 100% sure it is a regular file.
             * It will follow symbolic links by default, so subject to races and abuse.
             *
             * If used on a directory and the directory isn't empty, it will return success
             * instead of STATUS_CANNOT_DELETE.
             *
             * To stay out of trouble, we always use the OBJ_DONT_REPARSE flag here.  This
             * is a relative new addition (windows 10, build unknown), so if it ain't
             * supported or we encounter a reparse object we just fall back to the regular
             * deletion code.
             */
            static int volatile s_iSupportsDontReparse = 0;
            if (s_iSupportsDontReparse >= 0 && !fRmDir)
            {
                MY_OBJECT_ATTRIBUTES ObjAttr;
                MyInitializeObjectAttributes(&ObjAttr, &NtPath, OBJ_DONT_REPARSE | OBJ_CASE_INSENSITIVE, hRoot, NULL /*pSecAttr*/);
                rcNt = g_pfnNtDeleteFile(&ObjAttr);

                /* In case some file system does things differently than NTFS. */
                if (rcNt == STATUS_CANNOT_DELETE && fReadOnlyToo)
                {
                    birdMakeWritable(hRoot, &NtPath);
                    rcNt = g_pfnNtDeleteFile(&ObjAttr);
                }

                /* Do fallback. */
                if (rcNt == STATUS_REPARSE_POINT_ENCOUNTERED)
                    fFast = 0;
                else if (rcNt == STATUS_INVALID_PARAMETER)
                {
                    s_iSupportsDontReparse = -1;
                    fFast = 0;
                }
            }
            else
            {
                rcNt = STATUS_INVALID_PARAMETER;
                fFast = 0;
            }
        }

        if (!fFast)
        {
            /*
             * Use the set information stuff. Here we can better control reparsing.
             */
            HANDLE hFile;
            for (;;)
            {
                MY_IO_STATUS_BLOCK Ios;

                rcNt = birdOpenFileUniStr(hRoot,
                                          &NtPath,
                                          DELETE | SYNCHRONIZE,
                                          FILE_ATTRIBUTE_NORMAL,
                                          FILE_SHARE_DELETE | FILE_SHARE_READ | FILE_SHARE_WRITE,
                                          FILE_OPEN,
                                          (!fRmDir ? FILE_NON_DIRECTORY_FILE : FILE_DIRECTORY_FILE)
                                          | FILE_OPEN_FOR_BACKUP_INTENT | FILE_OPEN_REPARSE_POINT,
                                          OBJ_CASE_INSENSITIVE,
                                          &hFile);

                /* Windows distinguishes between symlinks to directories and to files, so
                   unlink(symlink-dir) will fail and we have to retry w/o the non-dir-file
                   flag and make sure it didn't turn into a pure directory. */
                if (   rcNt == STATUS_FILE_IS_A_DIRECTORY
                    && !fRmDir)
                {
                    rcNt = birdOpenFileUniStr(hRoot,
                                              &NtPath,
                                              DELETE | FILE_READ_ATTRIBUTES | SYNCHRONIZE,
                                              FILE_ATTRIBUTE_REPARSE_POINT,
                                              FILE_SHARE_DELETE | FILE_SHARE_READ | FILE_SHARE_WRITE,
                                              FILE_OPEN,
                                              FILE_OPEN_FOR_BACKUP_INTENT | FILE_OPEN_REPARSE_POINT,
                                              OBJ_CASE_INSENSITIVE,
                                              &hFile);
                    if (MY_NT_SUCCESS(rcNt))
                    {
                        MY_FILE_BASIC_INFORMATION BasicInfo;
                        memset(&BasicInfo, 0, sizeof(BasicInfo));

                        Ios.Information = -1;
                        Ios.u.Status    = -1;

                        rcNt = g_pfnNtQueryInformationFile(hFile, &Ios, &BasicInfo, sizeof(BasicInfo), MyFileBasicInformation);
                        if (   !MY_NT_SUCCESS(rcNt)
                            || !MY_NT_SUCCESS(Ios.u.Status)
                            ||    (BasicInfo.FileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY))
                               == FILE_ATTRIBUTE_DIRECTORY)
                        {
                            birdCloseFile(hFile);
                            rcNt = STATUS_FILE_IS_A_DIRECTORY;
                            break;
                        }
                    }
                }

                if (MY_NT_SUCCESS(rcNt))
                {
                    MY_FILE_DISPOSITION_INFORMATION DispInfo;
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
        else if (rcNt == STATUS_SHARING_VIOLATION && fRmDir)
            rc = birdSetErrnoToDirNotEmpty(); /* probably the case... */
        else
            rc = birdSetErrnoFromNt(rcNt);
    }
    return rc;
}


/*
 * unlink:
 */

int birdUnlink(const char *pszFile)
{
    return birdUnlinkInternal(NULL /*hRoot*/, pszFile, NULL /*pwszFile*/, 0 /*fReadOnlyToo*/, 0 /*fFast*/, 0 /*fRmDir*/);
}


int birdUnlinkW(const wchar_t *pwszFile)
{
    return birdUnlinkInternal(NULL /*hRoot*/, NULL /*pwszFile*/, pwszFile, 0 /*fReadOnlyToo*/, 0 /*fFast*/, 0 /*fRmDir*/);
}


int birdUnlinkEx(void *hRoot, const char *pszFile)
{
    return birdUnlinkInternal((HANDLE)hRoot, pszFile, NULL /*pwszFile*/, 0 /*fReadOnlyToo*/, 0 /*fFast*/, 0 /*fRmDir*/);
}


int birdUnlinkExW(void *hRoot, const wchar_t *pwszFile)
{
    return birdUnlinkInternal((HANDLE)hRoot, NULL /*pszFile*/, pwszFile, 0 /*fReadOnlyToo*/, 0 /*fFast*/, 0 /*fRmDir*/);
}


int birdUnlinkForced(const char *pszFile)
{
    return birdUnlinkInternal(NULL /*hRoot*/, pszFile, NULL /*pwszFile*/, 1 /*fReadOnlyToo*/, 0 /*fFast*/, 0 /*fRmDir*/);
}


int birdUnlinkForcedW(const wchar_t *pwszFile)
{
    return birdUnlinkInternal(NULL /*hRoot*/, NULL /*pszFile*/, pwszFile, 1 /*fReadOnlyToo*/, 0 /*fFast*/, 0 /*fRmDir*/);
}


int birdUnlinkForcedEx(void *hRoot, const char *pszFile)
{
    return birdUnlinkInternal((HANDLE)hRoot, pszFile, NULL /*pwszFile*/, 1 /*fReadOnlyToo*/, 0 /*fFast*/, 0 /*fRmDir*/);
}


int birdUnlinkForcedExW(void *hRoot, const wchar_t *pwszFile)
{
    return birdUnlinkInternal((HANDLE)hRoot, NULL /*pszFile*/, pwszFile, 1 /*fReadOnlyToo*/, 0 /*fFast*/, 0 /*fRmDir*/);
}


int birdUnlinkForcedFast(const char *pszFile)
{
    return birdUnlinkInternal(NULL /*hRoot*/, pszFile, NULL /*pwszFile*/, 1 /*fReadOnlyToo*/, 1 /*fFast*/, 0 /*fRmDir*/);
}


int birdUnlinkForcedFastW(const wchar_t *pwszFile)
{
    return birdUnlinkInternal(NULL /*hRoot*/, NULL /*pszFile*/, pwszFile, 1 /*fReadOnlyToo*/, 1 /*fFast*/, 0 /*fRmDir*/);
}


int birdUnlinkForcedFastEx(void *hRoot, const char *pszFile)
{
    return birdUnlinkInternal((HANDLE)hRoot, pszFile, NULL /*pwszFile*/, 1 /*fReadOnlyToo*/, 1 /*fFast*/, 0 /*fRmDir*/);
}


int birdUnlinkForcedFastExW(void *hRoot, const wchar_t *pwszFile)
{
    return birdUnlinkInternal((HANDLE)hRoot, NULL /*pszFile*/, pwszFile, 1 /*fReadOnlyToo*/, 1 /*fFast*/, 0 /*fRmDir*/);
}


/*
 * rmdir
 */

int birdRmDir(const char *pszFile)
{
    return birdUnlinkInternal(NULL /*hRoot*/, pszFile, NULL /*pwszFile*/, 0 /*fReadOnlyToo*/, 0 /*fFast*/, 1 /*fRmDir*/);
}


int birdRmDirW(const wchar_t *pwszFile)
{
    return birdUnlinkInternal(NULL /*hRoot*/, NULL /*pwszFile*/, pwszFile, 0 /*fReadOnlyToo*/, 0 /*fFast*/, 1 /*fRmDir*/);
}


int birdRmDirEx(void *hRoot, const char *pszFile)
{
    return birdUnlinkInternal((HANDLE)hRoot, pszFile, NULL /*pwszFile*/, 0 /*fReadOnlyToo*/, 0 /*fFast*/, 1 /*fRmDir*/);
}


int birdRmDirExW(void *hRoot, const wchar_t *pwszFile)
{
    return birdUnlinkInternal((HANDLE)hRoot, NULL /*pszFile*/, pwszFile, 0 /*fReadOnlyToo*/, 0 /*fFast*/, 1 /*fRmDir*/);
}


int birdRmDirForced(const char *pszFile)
{
    return birdUnlinkInternal(NULL /*hRoot*/, pszFile, NULL /*pwszFile*/, 1 /*fReadOnlyToo*/, 0 /*fFast*/, 1 /*fRmDir*/);
}


int birdRmDirForcedW(const wchar_t *pwszFile)
{
    return birdUnlinkInternal(NULL /*hRoot*/, NULL /*pszFile*/, pwszFile, 1 /*fReadOnlyToo*/, 0 /*fFast*/, 1 /*fRmDir*/);
}


int birdRmDirForcedEx(void *hRoot, const char *pszFile)
{
    return birdUnlinkInternal((HANDLE)hRoot, pszFile, NULL /*pwszFile*/, 1 /*fReadOnlyToo*/, 0 /*fFast*/, 1 /*fRmDir*/);
}


int birdRmDirForcedExW(void *hRoot, const wchar_t *pwszFile)
{
    return birdUnlinkInternal((HANDLE)hRoot, NULL /*pszFile*/, pwszFile, 1 /*fReadOnlyToo*/, 0 /*fFast*/, 1 /*fRmDir*/);
}


/**
 * Implements unlinkat().
 */
int birdUnlinkAt(int fdDir, const char *pszPath, int fFlags)
{
    HANDLE hDirRoot;

    /** @todo validate input. */
    fFlags &= AT_REMOVEDIR;

    /*
     * Check the path its effectively a AT_FDCWD call.
     */
    if (fdDir != AT_FDCWD)
    {
        if (IS_SLASH(pszPath[0]))
        {
            if (IS_SLASH(pszPath[1]) && !IS_SLASH(pszPath[2]) && pszPath[2] != '\0')
                fdDir = AT_FDCWD;
        }
        else if (IS_ALPHA(pszPath[0]) && pszPath[1] == ':')
        {
            if (IS_SLASH(pszPath[2]))
                fdDir = AT_FDCWD;
            else
                /*
                 * Drive letter relative path like "C:kernel32.dll".
                 * We could try use fdDir as the CWD here if it refers to the same drive,
                 * however that's can be implemented later...
                 */
                fdDir = AT_FDCWD;
        }
    }

    /*
     * Determine hDirRoot.
     */
    if (fdDir == AT_FDCWD)
        hDirRoot = NULL;
    else
    {
        hDirRoot = (HANDLE)_get_osfhandle(fdDir);
        if (hDirRoot == INVALID_HANDLE_VALUE || hDirRoot == NULL)
            return birdSetErrnoToBadFileNo();
    }

    return birdUnlinkInternal(hDirRoot, pszPath, NULL /*pwszFile*/, 1 /*fReadOnlyToo*/, 0 /*fFast*/, !!fFlags /*fRmDir*/);
}

