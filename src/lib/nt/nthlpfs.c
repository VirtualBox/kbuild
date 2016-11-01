/* $Id: nthlpfs.c 2985 2016-11-01 18:26:35Z knut.osmundsen@oracle.com $ */
/** @file
 * MSC + NT helpers for file system related functions.
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
#include "nthlp.h"
#include <stddef.h>
#include <string.h>
#include <wchar.h>
#include <errno.h>


/*******************************************************************************
*   Global Variables                                                           *
*******************************************************************************/
static int g_fHaveOpenReparsePoint = -1;



static int birdHasTrailingSlash(const char *pszPath)
{
    char ch, ch2;

    /* Skip leading slashes. */
    while ((ch = *pszPath) == '/' || ch == '\\')
        pszPath++;
    if (ch == '\0')
        return 0;

    /* Find the last char. */
    while ((ch2 = *++pszPath) != '\0')
        ch = ch2;

    return ch == '/' || ch == '\\' || ch == ':';
}


static int birdHasTrailingSlashW(const wchar_t *pwszPath)
{
    wchar_t wc, wc2;

    /* Skip leading slashes. */
    while ((wc = *pwszPath) == '/' || wc == '\\')
        pwszPath++;
    if (wc == '\0')
        return 0;

    /* Find the last char. */
    while ((wc2 = *++pwszPath) != '\0')
        wc = wc2;

    return wc == '/' || wc == '\\' || wc == ':';
}


static int birdIsPathDirSpec(const char *pszPath)
{
    char ch, ch2;

    /* Check for empty string. */
    ch = *pszPath;
    if (ch == '\0')
        return 0;

    /* Find the last char. */
    while ((ch2 = *++pszPath) != '\0')
        ch = ch2;

    return ch == '/' || ch == '\\' || ch == ':';
}


static int birdIsPathDirSpecW(const wchar_t *pwszPath)
{
    wchar_t wc, wc2;

    /* Check for empty string. */
    wc = *pwszPath;
    if (wc == '\0')
        return 0;

    /* Find the last char. */
    while ((wc2 = *++pwszPath) != '\0')
        wc = wc2;

    return wc == '/' || wc == '\\' || wc == ':';
}


int birdDosToNtPath(const char *pszPath, MY_UNICODE_STRING *pNtPath)
{
    MY_NTSTATUS         rcNt;
    WCHAR               wszTmp[4096];
    MY_UNICODE_STRING   TmpUniStr;
    MY_ANSI_STRING      Src;

    birdResolveImports();

    pNtPath->Length = pNtPath->MaximumLength = 0;
    pNtPath->Buffer = NULL;

    /*
     * Convert the input to wide char.
     */
    Src.Buffer              = (PCHAR)pszPath;
    Src.MaximumLength       = Src.Length = (USHORT)strlen(pszPath);

    TmpUniStr.Length        = 0;
    TmpUniStr.MaximumLength = sizeof(wszTmp) - sizeof(WCHAR);
    TmpUniStr.Buffer        = wszTmp;

    rcNt = g_pfnRtlAnsiStringToUnicodeString(&TmpUniStr, &Src, FALSE);
    if (MY_NT_SUCCESS(rcNt))
    {
        if (TmpUniStr.Length > 0 && !(TmpUniStr.Length & 1))
        {
            wszTmp[TmpUniStr.Length / sizeof(WCHAR)] = '\0';

            /*
             * Convert the wide DOS path to an NT path.
             */
            if (g_pfnRtlDosPathNameToNtPathName_U(wszTmp, pNtPath, NULL, FALSE))
                return 0;
        }
        rcNt = -1;
    }
    return birdSetErrnoFromNt(rcNt);
}


int birdDosToNtPathW(const wchar_t *pwszPath, MY_UNICODE_STRING *pNtPath)
{
    birdResolveImports();

    pNtPath->Length = pNtPath->MaximumLength = 0;
    pNtPath->Buffer = NULL;

    /*
     * Convert the wide DOS path to an NT path.
     */
    if (g_pfnRtlDosPathNameToNtPathName_U(pwszPath, pNtPath, NULL, FALSE))
        return 0;
    return birdSetErrnoFromNt(STATUS_NO_MEMORY);
}


/**
 * Converts UNIX slashes to DOS ones and trims trailing ones.
 *
 * @returns 0
 * @param   pNtPath     The relative NT path to fix up.
 */
static int birdFixRelativeNtPathSlashesAndReturn0(MY_UNICODE_STRING *pNtPath)
{
    size_t   cwcLeft  = pNtPath->Length / sizeof(wchar_t);
    wchar_t *pwcStart = pNtPath->Buffer;
    wchar_t *pwcHit;

    /* Convert slashes. */
    while ((pwcHit = wmemchr(pwcStart,  '/', cwcLeft)) != NULL)
    {
        *pwcHit = '\\';
        cwcLeft -= pwcHit - pwcStart;
        pwcHit = pwcStart;
    }

    /* Strip trailing slashes (NT doesn't like them). */
    while (   pNtPath->Length >= sizeof(wchar_t)
           && pNtPath->Buffer[(pNtPath->Length - sizeof(wchar_t)) / sizeof(wchar_t)] == '\\')
    {
        pNtPath->Length -= sizeof(wchar_t);
        pNtPath->Buffer[pNtPath->Length / sizeof(wchar_t)] = '\0';
    }

    /* If it was all trailing slashes we convert it to a dot path. */
    if (   pNtPath->Length == 0
        && pNtPath->MaximumLength >= sizeof(wchar_t) * 2)
    {
        pNtPath->Length = sizeof(wchar_t);
        pNtPath->Buffer[0] = '.';
        pNtPath->Buffer[1] = '\0';
    }

    return 0;
}


/**
 * Similar to birdDosToNtPath, but it does call RtlDosPathNameToNtPathName_U.
 *
 * @returns 0 on success, -1 + errno on failure.
 * @param   pszPath     The relative path.
 * @param   pNtPath     Where to return the NT path.  Call birdFreeNtPath when done.
 */
int birdDosToRelativeNtPath(const char *pszPath, MY_UNICODE_STRING *pNtPath)
{
    MY_NTSTATUS         rcNt;
    MY_ANSI_STRING      Src;

    birdResolveImports();

    /*
     * Just convert to wide char.
     */
    pNtPath->Length   = pNtPath->MaximumLength = 0;
    pNtPath->Buffer   = NULL;

    Src.Buffer        = (PCHAR)pszPath;
    Src.MaximumLength = Src.Length = (USHORT)strlen(pszPath);

    rcNt = g_pfnRtlAnsiStringToUnicodeString(pNtPath, &Src, TRUE /* Allocate */);
    if (MY_NT_SUCCESS(rcNt))
        return birdFixRelativeNtPathSlashesAndReturn0(pNtPath);
    return birdSetErrnoFromNt(rcNt);
}


/**
 * Similar to birdDosToNtPathW, but it does call RtlDosPathNameToNtPathName_U.
 *
 * @returns 0 on success, -1 + errno on failure.
 * @param   pwszPath    The relative path.
 * @param   pNtPath     Where to return the NT path.  Call birdFreeNtPath when done.
 */
int birdDosToRelativeNtPathW(const wchar_t *pwszPath, MY_UNICODE_STRING *pNtPath)
{
    size_t cwcPath = wcslen(pwszPath);
    if (cwcPath < 0xfffe)
    {
        pNtPath->Length = (USHORT)(cwcPath * sizeof(wchar_t));
        pNtPath->MaximumLength = pNtPath->Length + sizeof(wchar_t);
        pNtPath->Buffer = HeapAlloc(GetProcessHeap(), 0, pNtPath->MaximumLength);
        if (pNtPath->Buffer)
        {
            memcpy(pNtPath->Buffer, pwszPath, pNtPath->MaximumLength);
            return birdFixRelativeNtPathSlashesAndReturn0(pNtPath);
        }
        errno = ENOMEM;
    }
    else
        errno = ENAMETOOLONG;
    return -1;
}


/**
 * Frees a string returned by birdDosToNtPath, birdDosToNtPathW or
 * birdDosToRelativeNtPath.
 *
 * @param   pNtPath             The the NT path to free.
 */
void birdFreeNtPath(MY_UNICODE_STRING *pNtPath)
{
    HeapFree(GetProcessHeap(), 0, pNtPath->Buffer);
    pNtPath->Buffer = NULL;
    pNtPath->Length = 0;
    pNtPath->MaximumLength = 0;
}


MY_NTSTATUS birdOpenFileUniStr(HANDLE hRoot, MY_UNICODE_STRING *pNtPath, ACCESS_MASK fDesiredAccess, ULONG fFileAttribs,
                               ULONG fShareAccess, ULONG fCreateDisposition, ULONG fCreateOptions, ULONG fObjAttribs,
                               HANDLE *phFile)
{
    MY_IO_STATUS_BLOCK      Ios;
    MY_OBJECT_ATTRIBUTES    ObjAttr;
    MY_NTSTATUS             rcNt;

    birdResolveImports();

    if (  (fCreateOptions & FILE_OPEN_REPARSE_POINT)
        && g_fHaveOpenReparsePoint == 0)
        fCreateOptions &= ~FILE_OPEN_REPARSE_POINT;

    Ios.Information = -1;
    Ios.u.Status = 0;
    MyInitializeObjectAttributes(&ObjAttr, pNtPath, fObjAttribs, hRoot, NULL /*pSecAttr*/);

    rcNt = g_pfnNtCreateFile(phFile,
                             fDesiredAccess,
                             &ObjAttr,
                             &Ios,
                             NULL,   /* cbFileInitialAlloc */
                             fFileAttribs,
                             fShareAccess,
                             fCreateDisposition,
                             fCreateOptions,
                             NULL,   /* pEaBuffer */
                             0);     /* cbEaBuffer*/
    if (   rcNt == STATUS_INVALID_PARAMETER
        && g_fHaveOpenReparsePoint < 0
        && (fCreateOptions & FILE_OPEN_REPARSE_POINT))
    {
        fCreateOptions &= ~FILE_OPEN_REPARSE_POINT;

        Ios.Information = -1;
        Ios.u.Status = 0;
        MyInitializeObjectAttributes(&ObjAttr, pNtPath, fObjAttribs, NULL /*hRoot*/, NULL /*pSecAttr*/);

        rcNt = g_pfnNtCreateFile(phFile,
                                 fDesiredAccess,
                                 &ObjAttr,
                                 &Ios,
                                 NULL,   /* cbFileInitialAlloc */
                                 fFileAttribs,
                                 fShareAccess,
                                 fCreateDisposition,
                                 fCreateOptions,
                                 NULL,   /* pEaBuffer */
                                 0);     /* cbEaBuffer*/
        if (rcNt != STATUS_INVALID_PARAMETER)
            g_fHaveOpenReparsePoint = 0;
    }
    return rcNt;
}


HANDLE birdOpenFile(const char *pszPath, ACCESS_MASK fDesiredAccess, ULONG fFileAttribs,
                    ULONG fShareAccess, ULONG fCreateDisposition, ULONG fCreateOptions, ULONG fObjAttribs)
{
    MY_UNICODE_STRING   NtPath;
    MY_NTSTATUS         rcNt;

    /*
     * Adjust inputs.
     */
    if (birdIsPathDirSpec(pszPath))
        fCreateOptions |= FILE_DIRECTORY_FILE;

    /*
     * Convert the path and call birdOpenFileUniStr to do the real work.
     */
    if (birdDosToNtPath(pszPath, &NtPath) == 0)
    {
        HANDLE hFile;
        rcNt = birdOpenFileUniStr(NULL /*hRoot*/, &NtPath, fDesiredAccess, fFileAttribs, fShareAccess,
                                  fCreateDisposition, fCreateOptions, fObjAttribs, &hFile);
        birdFreeNtPath(&NtPath);
        if (MY_NT_SUCCESS(rcNt))
            return hFile;
        birdSetErrnoFromNt(rcNt);
    }

    return INVALID_HANDLE_VALUE;
}


HANDLE birdOpenFileW(const wchar_t *pwszPath, ACCESS_MASK fDesiredAccess, ULONG fFileAttribs,
                     ULONG fShareAccess, ULONG fCreateDisposition, ULONG fCreateOptions, ULONG fObjAttribs)
{
    MY_UNICODE_STRING   NtPath;
    MY_NTSTATUS         rcNt;

    /*
     * Adjust inputs.
     */
    if (birdIsPathDirSpecW(pwszPath))
        fCreateOptions |= FILE_DIRECTORY_FILE;

    /*
     * Convert the path and call birdOpenFileUniStr to do the real work.
     */
    if (birdDosToNtPathW(pwszPath, &NtPath) == 0)
    {
        HANDLE hFile;
        rcNt = birdOpenFileUniStr(NULL /*hRoot*/, &NtPath, fDesiredAccess, fFileAttribs, fShareAccess,
                                  fCreateDisposition, fCreateOptions, fObjAttribs, &hFile);
        birdFreeNtPath(&NtPath);
        if (MY_NT_SUCCESS(rcNt))
            return hFile;
        birdSetErrnoFromNt(rcNt);
    }

    return INVALID_HANDLE_VALUE;
}


HANDLE birdOpenFileEx(HANDLE hRoot, const char *pszPath, ACCESS_MASK fDesiredAccess, ULONG fFileAttribs,
                      ULONG fShareAccess, ULONG fCreateDisposition, ULONG fCreateOptions, ULONG fObjAttribs)
{
    MY_UNICODE_STRING   NtPath;
    MY_NTSTATUS         rcNt;

    /*
     * Adjust inputs.
     */
    if (birdIsPathDirSpec(pszPath))
        fCreateOptions |= FILE_DIRECTORY_FILE;

    /*
     * Convert the path and call birdOpenFileUniStr to do the real work.
     */
    if (hRoot == INVALID_HANDLE_VALUE)
        hRoot = NULL;
    if ((hRoot != NULL ? birdDosToRelativeNtPath(pszPath, &NtPath) : birdDosToNtPath(pszPath, &NtPath)) == 0)
    {
        HANDLE hFile;
        rcNt = birdOpenFileUniStr(hRoot, &NtPath, fDesiredAccess, fFileAttribs, fShareAccess,
                                  fCreateDisposition, fCreateOptions, fObjAttribs, &hFile);
        birdFreeNtPath(&NtPath);
        if (MY_NT_SUCCESS(rcNt))
            return hFile;
        birdSetErrnoFromNt(rcNt);
    }

    return INVALID_HANDLE_VALUE;
}


HANDLE birdOpenFileExW(HANDLE hRoot, const wchar_t *pwszPath, ACCESS_MASK fDesiredAccess, ULONG fFileAttribs,
                       ULONG fShareAccess, ULONG fCreateDisposition, ULONG fCreateOptions, ULONG fObjAttribs)
{
    MY_UNICODE_STRING   NtPath;
    MY_NTSTATUS         rcNt;

    /*
     * Adjust inputs.
     */
    if (birdIsPathDirSpecW(pwszPath))
        fCreateOptions |= FILE_DIRECTORY_FILE;

    /*
     * Convert the path (could save ourselves this if pwszPath is perfect) and
     * call birdOpenFileUniStr to do the real work.
     */
    if (hRoot == INVALID_HANDLE_VALUE)
        hRoot = NULL;
    if ((hRoot != NULL ? birdDosToRelativeNtPathW(pwszPath, &NtPath) : birdDosToNtPathW(pwszPath, &NtPath)) == 0)
    {
        HANDLE hFile;
        rcNt = birdOpenFileUniStr(hRoot, &NtPath, fDesiredAccess, fFileAttribs, fShareAccess,
                                  fCreateDisposition, fCreateOptions, fObjAttribs, &hFile);
        birdFreeNtPath(&NtPath);
        if (MY_NT_SUCCESS(rcNt))
            return hFile;
        birdSetErrnoFromNt(rcNt);
    }

    return INVALID_HANDLE_VALUE;
}


static HANDLE birdOpenParentDirCommon(HANDLE hRoot, MY_UNICODE_STRING *pNtPath, ACCESS_MASK fDesiredAccess, ULONG fFileAttribs,
                                      ULONG fShareAccess, ULONG fCreateDisposition, ULONG fCreateOptions, ULONG fObjAttribs,
                                      MY_UNICODE_STRING *pNameUniStr)
{
    MY_NTSTATUS rcNt;

    /*
     * Strip the path down to the directory.
     */
    USHORT offName = pNtPath->Length / sizeof(WCHAR);
    USHORT cwcName = offName;
    WCHAR  wc = 0;
    while (   offName > 0
           && (wc = pNtPath->Buffer[offName - 1]) != '\\'
           && wc != '/'
           && wc != ':')
        offName--;
    if (   offName > 0
        || (hRoot != NULL && cwcName > 0))
    {
        cwcName -= offName;

        /* Make a copy of the file name, if requested. */
        rcNt = STATUS_SUCCESS;
        if (pNameUniStr)
        {
            pNameUniStr->Length        = cwcName * sizeof(WCHAR);
            pNameUniStr->MaximumLength = pNameUniStr->Length + sizeof(WCHAR);
            pNameUniStr->Buffer        = (WCHAR *)HeapAlloc(GetProcessHeap(), 0, pNameUniStr->MaximumLength);
            if (pNameUniStr->Buffer)
            {
                memcpy(pNameUniStr->Buffer, &pNtPath->Buffer[offName], pNameUniStr->Length);
                pNameUniStr->Buffer[cwcName] = '\0';
            }
            else
                rcNt = STATUS_NO_MEMORY;
        }

        /* Chop, chop. */
        // Bad idea, breaks \\?\c:\pagefile.sys. //while (   offName > 0
        // Bad idea, breaks \\?\c:\pagefile.sys. //       && (   (wc = pNtPath->Buffer[offName - 1]) == '\\'
        // Bad idea, breaks \\?\c:\pagefile.sys. //           || wc == '/'))
        // Bad idea, breaks \\?\c:\pagefile.sys. //    offName--;
        if (offName == 0)
            pNtPath->Buffer[offName++] = '.'; /* Hack for dir handle + dir entry name. */
        pNtPath->Length = offName * sizeof(WCHAR);
        pNtPath->Buffer[offName] = '\0';
        if (MY_NT_SUCCESS(rcNt))
        {
            /*
             * Finally, try open the directory.
             */
            HANDLE hFile;
            fCreateOptions |= FILE_DIRECTORY_FILE;
            rcNt = birdOpenFileUniStr(hRoot, pNtPath, fDesiredAccess, fFileAttribs, fShareAccess,
                                      fCreateDisposition, fCreateOptions, fObjAttribs, &hFile);
            if (MY_NT_SUCCESS(rcNt))
            {
                birdFreeNtPath(pNtPath);
                return hFile;
            }
        }

        if (pNameUniStr)
            birdFreeNtPath(pNameUniStr);
    }
    else
        rcNt = STATUS_INVALID_PARAMETER;

    birdFreeNtPath(pNtPath);
    birdSetErrnoFromNt(rcNt);
    return INVALID_HANDLE_VALUE;
}


HANDLE birdOpenParentDir(HANDLE hRoot, const char *pszPath, ACCESS_MASK fDesiredAccess, ULONG fFileAttribs,
                         ULONG fShareAccess, ULONG fCreateDisposition, ULONG fCreateOptions, ULONG fObjAttribs,
                         MY_UNICODE_STRING *pNameUniStr)
{
    /*
     * Convert the path and join up with the UTF-16 version (it'll free NtPath).
     */
    MY_UNICODE_STRING NtPath;
    if (hRoot == INVALID_HANDLE_VALUE)
        hRoot = NULL;
    if (  hRoot == NULL
        ? birdDosToNtPath(pszPath, &NtPath) == 0
        : birdDosToRelativeNtPath(pszPath, &NtPath) == 0)
        return birdOpenParentDirCommon(hRoot, &NtPath, fDesiredAccess, fFileAttribs, fShareAccess,
                                       fCreateDisposition, fCreateOptions, fObjAttribs, pNameUniStr);
    return INVALID_HANDLE_VALUE;
}


HANDLE birdOpenParentDirW(HANDLE hRoot, const wchar_t *pwszPath, ACCESS_MASK fDesiredAccess, ULONG fFileAttribs,
                          ULONG fShareAccess, ULONG fCreateDisposition, ULONG fCreateOptions, ULONG fObjAttribs,
                          MY_UNICODE_STRING *pNameUniStr)
{
    /*
     * Convert the path and join up with the ansi version (it'll free NtPath).
     */
    MY_UNICODE_STRING NtPath;
    if (hRoot == INVALID_HANDLE_VALUE)
        hRoot = NULL;
    if (  hRoot == NULL
        ? birdDosToNtPathW(pwszPath, &NtPath) == 0
        : birdDosToRelativeNtPathW(pwszPath, &NtPath) == 0)
        return birdOpenParentDirCommon(hRoot, &NtPath, fDesiredAccess, fFileAttribs, fShareAccess,
                                       fCreateDisposition, fCreateOptions, fObjAttribs, pNameUniStr);
    return INVALID_HANDLE_VALUE;
}


/**
 * Returns a handle to the current working directory of the process.
 *
 * @returns CWD handle with FILE_TRAVERSE and SYNCHRONIZE access.  May return
 *          INVALID_HANDLE_VALUE w/ errno for invalid CWD.
 */
HANDLE birdOpenCurrentDirectory(void)
{
    PMY_RTL_USER_PROCESS_PARAMETERS pProcParams;
    MY_NTSTATUS rcNt;
    HANDLE hRet = INVALID_HANDLE_VALUE;

    birdResolveImports();

    /*
     * We'll try get this from the PEB.
     */
    g_pfnRtlAcquirePebLock();
    pProcParams = (PMY_RTL_USER_PROCESS_PARAMETERS)MY_NT_CURRENT_PEB()->ProcessParameters;
    if (pProcParams != NULL)
        rcNt = g_pfnNtDuplicateObject(MY_NT_CURRENT_PROCESS, pProcParams->CurrentDirectory.Handle,
                                      MY_NT_CURRENT_PROCESS, &hRet,
                                      FILE_TRAVERSE | SYNCHRONIZE,
                                      0 /*fAttribs*/,
                                      0 /*fOptions*/);
    else
        rcNt = STATUS_INVALID_PARAMETER;
    g_pfnRtlReleasePebLock();
    if (MY_NT_SUCCESS(rcNt))
        return hRet;

    /*
     * Fallback goes thru birdOpenFileW.
     */
    return birdOpenFileW(L".",
                         FILE_TRAVERSE | SYNCHRONIZE,
                         FILE_ATTRIBUTE_NORMAL,
                         FILE_SHARE_READ | FILE_SHARE_WRITE,
                         FILE_OPEN,
                         FILE_DIRECTORY_FILE | FILE_OPEN_FOR_BACKUP_INTENT | FILE_SYNCHRONOUS_IO_NONALERT,
                         OBJ_CASE_INSENSITIVE);
}


void birdCloseFile(HANDLE hFile)
{
    birdResolveImports();
    g_pfnNtClose(hFile);
}

