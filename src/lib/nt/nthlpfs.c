/* $Id: nthlpfs.c 2703 2013-11-21 00:26:35Z knut.osmundsen@oracle.com $ */
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

#ifndef BIRD_USE_WIN32

static int birdDosToNtPath(const char *pszPath, MY_UNICODE_STRING *pNtPath)
{
    MY_NTSTATUS         rcNt;
    WCHAR               wszTmp[4096];
    MY_UNICODE_STRING   TmpUniStr;
    MY_ANSI_STRING      Src;

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


static void birdFreeNtPath(MY_UNICODE_STRING *pNtPath)
{
    HeapFree(GetProcessHeap(), 0, pNtPath->Buffer);
    pNtPath->Buffer = NULL;
}

#endif /* !BIRD_USE_WIN32 */


HANDLE birdOpenFile(const char *pszPath, ACCESS_MASK fDesiredAccess, ULONG fFileAttribs, ULONG fShareAccess,
                    ULONG fCreateDisposition, ULONG fCreateOptions, ULONG fObjAttribs)
{
    static int          s_fHaveOpenReparsePoint = -1;
    HANDLE              hFile;
#ifdef BIRD_USE_WIN32
    SECURITY_ATTRIBUTES SecAttr;
    DWORD               dwErr;
    DWORD               fW32Disp;
    DWORD               fW32Flags;
#else
    MY_UNICODE_STRING   NtPath;
    MY_NTSTATUS         rcNt;
#endif

    birdResolveImports();

    if (birdIsPathDirSpec(pszPath))
        fCreateOptions |= FILE_DIRECTORY_FILE;
    if (  (fCreateOptions & FILE_OPEN_REPARSE_POINT)
        && s_fHaveOpenReparsePoint == 0)
        fCreateOptions &= ~FILE_OPEN_REPARSE_POINT;

#ifdef BIRD_USE_WIN32
    /* NT -> W32 */

    SecAttr.nLength              = sizeof(SecAttr);
    SecAttr.lpSecurityDescriptor = NULL;
    SecAttr.bInheritHandle       = fObjAttribs & OBJ_INHERIT ? TRUE : FALSE;

    fW32Flags = 0;
    if (!(fObjAttribs & OBJ_CASE_INSENSITIVE))
        fW32Flags |= FILE_FLAG_POSIX_SEMANTICS;
    if (fCreateOptions & FILE_OPEN_FOR_BACKUP_INTENT)
        fW32Flags |= FILE_FLAG_BACKUP_SEMANTICS;
    if (fCreateOptions & FILE_OPEN_REPARSE_POINT)
        fW32Flags |= FILE_FLAG_OPEN_REPARSE_POINT;
    //?? if (fCreateOptions & FILE_DIRECTORY_FILE)
    //??    fW32Flags |= ;

    switch (fCreateDisposition)
    {
        case FILE_OPEN:             fW32Disp = OPEN_EXISTING; break;
        case FILE_CREATE:           fW32Disp = CREATE_NEW; break;
        case FILE_OPEN_IF:          fW32Disp = OPEN_ALWAYS; break;
        case FILE_OVERWRITE_IF:     fW32Disp = CREATE_ALWAYS; break;
        default:
# ifndef NDEBUG
            __debugbreak();
# endif
            return INVALID_HANDLE_VALUE;
    }

    hFile = CreateFileA(pszPath, fDesiredAccess, fShareAccess, &SecAttr, fW32Disp, fW32Flags, NULL /*hTemplateFile*/);
    if (hFile != INVALID_HANDLE_VALUE)
        return hFile;

    dwErr = GetLastError();

    /* Deal with FILE_FLAG_OPEN_REPARSE_POINT the first times around. */
    if (   dwErr == ERROR_INVALID_PARAMETER
        && s_fHaveOpenReparsePoint < 0
        && (fCreateOptions & FILE_OPEN_REPARSE_POINT) )
    {
        fCreateOptions &= ~FILE_OPEN_REPARSE_POINT;
        fW32Flags      &= ~FILE_FLAG_OPEN_REPARSE_POINT;
        hFile = CreateFileA(pszPath, fDesiredAccess, fFileAttribs, &SecAttr, fW32Disp, fW32Flags, NULL /*hTemplateFile*/);
        if (hFile != INVALID_HANDLE_VALUE)
        {
            s_fHaveOpenReparsePoint = 0;
            return hFile;
        }
    }

    birdSetErrnoFromWin32(dwErr);

#else  /* !BIRD_USE_WIN32 */

    /*
     * Call the NT API directly.
     */
    if (birdDosToNtPath(pszPath, &NtPath) == 0)
    {
        MY_IO_STATUS_BLOCK      Ios;
        MY_OBJECT_ATTRIBUTES    ObjAttr;

        Ios.Information = -1;
        Ios.u.Status = 0;

        MyInitializeObjectAttributes(&ObjAttr, &NtPath, fObjAttribs, NULL /*hRoot*/, NULL /*pSecAttr*/);

        rcNt = g_pfnNtCreateFile(&hFile,
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
        if (MY_NT_SUCCESS(rcNt))
        {
            birdFreeNtPath(&NtPath);
            return hFile;
        }

        birdFreeNtPath(&NtPath);
        birdSetErrnoFromNt(rcNt);
    }

#endif /* !BIRD_USE_WIN32 */
    return INVALID_HANDLE_VALUE;
}


void birdCloseFile(HANDLE hFile)
{
#ifdef BIRD_USE_WIN32
    CloseHandle(hFile);
#else
    birdResolveImports();
    g_pfnNtClose(hFile);
#endif
}

