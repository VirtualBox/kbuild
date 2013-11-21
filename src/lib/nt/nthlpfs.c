/* $Id: nthlpfs.c 2713 2013-11-21 21:11:00Z knut.osmundsen@oracle.com $ */
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


void birdFreeNtPath(MY_UNICODE_STRING *pNtPath)
{
    HeapFree(GetProcessHeap(), 0, pNtPath->Buffer);
    pNtPath->Buffer = NULL;
    pNtPath->Length = 0;
    pNtPath->MaximumLength = 0;
}


MY_NTSTATUS birdOpenFileUniStr(MY_UNICODE_STRING *pNtPath, ACCESS_MASK fDesiredAccess, ULONG fFileAttribs,
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


HANDLE birdOpenFile(const char *pszPath, ACCESS_MASK fDesiredAccess, ULONG fFileAttribs, ULONG fShareAccess,
                    ULONG fCreateDisposition, ULONG fCreateOptions, ULONG fObjAttribs)
{
    MY_UNICODE_STRING   NtPath;
    MY_NTSTATUS         rcNt;

    /*
     * Adjust inputs.
     */
    if (birdIsPathDirSpec(pszPath))
        fCreateOptions |= FILE_DIRECTORY_FILE;

    /*
     * Call the NT API directly.
     */
    if (birdDosToNtPath(pszPath, &NtPath) == 0)
    {
        HANDLE hFile;
        rcNt = birdOpenFileUniStr(&NtPath, fDesiredAccess, fFileAttribs, fShareAccess,
                                  fCreateDisposition, fCreateOptions, fObjAttribs, &hFile);
        if (MY_NT_SUCCESS(rcNt))
        {
            birdFreeNtPath(&NtPath);
            return hFile;
        }

        birdFreeNtPath(&NtPath);
        birdSetErrnoFromNt(rcNt);
    }

    return INVALID_HANDLE_VALUE;
}


HANDLE birdOpenParentDir(const char *pszPath, ACCESS_MASK fDesiredAccess, ULONG fFileAttribs, ULONG fShareAccess,
                         ULONG fCreateDisposition, ULONG fCreateOptions, ULONG fObjAttribs,
                         MY_UNICODE_STRING *pNameUniStr)
{
    MY_UNICODE_STRING   NtPath;
    MY_NTSTATUS         rcNt;

    /*
     * Adjust inputs.
     */
    fCreateOptions |= FILE_DIRECTORY_FILE;

    /*
     * Convert the path and split off the filename.
     */
    if (birdDosToNtPath(pszPath, &NtPath) == 0)
    {
        USHORT offName = NtPath.Length / sizeof(WCHAR);
        USHORT cwcName = offName;
        WCHAR  wc = 0;

        while (   offName > 0
               && (wc = NtPath.Buffer[offName - 1]) != '\\'
               && wc != '/'
               && wc != ':')
            offName--;
        if (offName > 0)
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
                    memcpy(pNameUniStr->Buffer, &NtPath.Buffer[offName],pNameUniStr->Length);
                    pNameUniStr->Buffer[cwcName] = '\0';
                }
                else
                    rcNt = STATUS_NO_MEMORY;
            }

            /* Chop, chop. */
            // Bad idea, breaks \\?\c:\pagefile.sys. //while (   offName > 0
            // Bad idea, breaks \\?\c:\pagefile.sys. //       && (   (wc = NtPath.Buffer[offName - 1]) == '\\'
            // Bad idea, breaks \\?\c:\pagefile.sys. //           || wc == '/'))
            // Bad idea, breaks \\?\c:\pagefile.sys. //    offName--;
            NtPath.Length = offName * sizeof(WCHAR);
            NtPath.Buffer[offName] = '\0';
            if (MY_NT_SUCCESS(rcNt))
            {
                /*
                 * Finally, try open the directory.
                 */
                HANDLE hFile;
                rcNt = birdOpenFileUniStr(&NtPath, fDesiredAccess, fFileAttribs, fShareAccess,
                                          fCreateDisposition, fCreateOptions, fObjAttribs, &hFile);
                if (MY_NT_SUCCESS(rcNt))
                {
                    birdFreeNtPath(&NtPath);
                    return hFile;
                }
            }

            if (pNameUniStr)
                birdFreeNtPath(pNameUniStr);
        }

        birdFreeNtPath(&NtPath);
        birdSetErrnoFromNt(rcNt);
    }

    return INVALID_HANDLE_VALUE;
}


void birdCloseFile(HANDLE hFile)
{
    birdResolveImports();
    g_pfnNtClose(hFile);
}

