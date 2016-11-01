/* $Id: nthlp.h 2985 2016-11-01 18:26:35Z knut.osmundsen@oracle.com $ */
/** @file
 * MSC + NT helper functions.
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

#ifndef ___nt_nthlp_h
#define ___nt_nthlp_h

#include "ntstuff.h"
#include "nttypes.h"


/** Lazy resolving of the NTDLL imports. */
#define birdResolveImports() do { if (g_fResolvedNtImports) {} else birdResolveImportsWorker(); } while (0)
void        birdResolveImportsWorker(void);
extern int  g_fResolvedNtImports;

void       *birdTmpAlloc(size_t cb);
void        birdTmpFree(void *pv);

void       *birdMemAlloc(size_t cb);
void       *birdMemAllocZ(size_t cb);
void        birdMemFree(void *pv);

int         birdSetErrnoFromNt(MY_NTSTATUS rcNt);
int         birdSetErrnoFromWin32(DWORD dwErr);
int         birdSetErrnoToNoMem(void);
int         birdSetErrnoToInvalidArg(void);
int         birdSetErrnoToBadFileNo(void);


HANDLE      birdOpenFile(const char *pszPath, ACCESS_MASK fDesiredAccess, ULONG fFileAttribs,
                         ULONG fShareAccess, ULONG fCreateDisposition, ULONG fCreateOptions, ULONG fObjAttribs);
HANDLE      birdOpenFileW(const wchar_t *pwszPath, ACCESS_MASK fDesiredAccess, ULONG fFileAttribs,
                          ULONG fShareAccess, ULONG fCreateDisposition, ULONG fCreateOptions, ULONG fObjAttribs);
HANDLE      birdOpenFileEx(HANDLE hRoot, const char *pszPath, ACCESS_MASK fDesiredAccess, ULONG fFileAttribs,
                           ULONG fShareAccess, ULONG fCreateDisposition, ULONG fCreateOptions, ULONG fObjAttribs);
HANDLE      birdOpenFileExW(HANDLE hRoot, const wchar_t *pwszPath, ACCESS_MASK fDesiredAccess, ULONG fFileAttribs,
                            ULONG fShareAccess, ULONG fCreateDisposition, ULONG fCreateOptions, ULONG fObjAttribs);
HANDLE      birdOpenParentDir(HANDLE hRoot, const char *pszPath, ACCESS_MASK fDesiredAccess, ULONG fFileAttribs,
                              ULONG fShareAccess, ULONG fCreateDisposition, ULONG fCreateOptions, ULONG fObjAttribs,
                              MY_UNICODE_STRING *pNameUniStr);
HANDLE      birdOpenParentDirW(HANDLE hRoot, const wchar_t *pwszPath, ACCESS_MASK fDesiredAccess, ULONG fFileAttribs,
                               ULONG fShareAccess, ULONG fCreateDisposition, ULONG fCreateOptions, ULONG fObjAttribs,
                               MY_UNICODE_STRING *pNameUniStr);
MY_NTSTATUS birdOpenFileUniStr(HANDLE hRoot, MY_UNICODE_STRING *pNtPath, ACCESS_MASK fDesiredAccess, ULONG fFileAttribs,
                               ULONG fShareAccess, ULONG fCreateDisposition, ULONG fCreateOptions, ULONG fObjAttribs,
                               HANDLE *phFile);
HANDLE      birdOpenCurrentDirectory(void);
void        birdCloseFile(HANDLE hFile);
int         birdDosToNtPath(const char *pszPath, MY_UNICODE_STRING *pNtPath);
void        birdFreeNtPath(MY_UNICODE_STRING *pNtPath);


static __inline void birdNtTimeToTimeSpec(__int64 iNtTime, BirdTimeSpec_T *pTimeSpec)
{
    iNtTime -= BIRD_NT_EPOCH_OFFSET_UNIX_100NS;
    pTimeSpec->tv_sec  = iNtTime / 10000000;
    pTimeSpec->tv_nsec = (iNtTime % 10000000) * 100;
}


#endif

