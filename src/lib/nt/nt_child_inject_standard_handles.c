/* $Id: nt_child_inject_standard_handles.c 3584 2023-01-20 15:29:36Z knut.osmundsen@oracle.com $ */
/** @file
 * Injecting standard handles into a child process.
 */

/*
 * Copyright (c) 2004-2018 knut st. osmundsen <bird-kBuild-spamx@anduin.net>
 *
 * This file is part of kBuild.
 *
 * kBuild is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * kBuild is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with kBuild.  If not, see <http://www.gnu.org/licenses/>
 *
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include <Windows.h>
#include <Winternl.h>
#include <stdio.h>
#include <assert.h>
#include <k/kDefs.h>
#include "nt_child_inject_standard_handles.h"

/**
 * Wrapper around ReadProcessMemory in case WOW64 tricks are needed.
 *
 * @returns Success indicator.
 * @param   hProcess    The target process.
 * @param   ullSrc      The source address (in @a hProcess).
 * @param   pvDst       The target address (this process).
 * @param   cbToRead    How much to read.
 * @param   pcbRead     Where to return how much was actually read.
 */
static BOOL MyReadProcessMemory(HANDLE hProcess, ULONGLONG ullSrc, void *pvDst, SIZE_T cbToRead, SIZE_T *pcbRead)
{
#if K_ARCH_BITS != 64
    if (ullSrc + cbToRead - 1 > ~(uintptr_t)0)
    {
        typedef NTSTATUS(NTAPI *PFN_NtWow64ReadVirtualMemory64)(HANDLE, ULONGLONG, PVOID, ULONGLONG, PULONGLONG);
        static PFN_NtWow64ReadVirtualMemory64 volatile      s_pfnNtWow64ReadVirtualMemory64= NULL;
        static BOOL volatile                                s_fInitialized = FALSE;
        PFN_NtWow64ReadVirtualMemory64                      pfnNtWow64ReadVirtualMemory64 = s_pfnNtWow64ReadVirtualMemory64;
        if (!pfnNtWow64ReadVirtualMemory64 && !s_fInitialized)
        {
            *(FARPROC *)&pfnNtWow64ReadVirtualMemory64 = GetProcAddress(GetModuleHandleA("NTDLL.DLL"), "NtWow64ReadVirtualMemory64");
            s_pfnNtWow64ReadVirtualMemory64 = pfnNtWow64ReadVirtualMemory64;
        }
        if (pfnNtWow64ReadVirtualMemory64)
        {
            struct
            {
                ULONGLONG volatile  ullBefore;
                ULONGLONG           cbRead64;
                ULONGLONG volatile  ullAfter;
            } Wtf = { ~(ULONGLONG)0, 0, ~(ULONGLONG)0 };
            NTSTATUS  rcNt = pfnNtWow64ReadVirtualMemory64(hProcess, ullSrc, pvDst, cbToRead, &Wtf.cbRead64);
            *pcbRead = (SIZE_T)Wtf.cbRead64;
            SetLastError(rcNt); /* lazy bird */
            return NT_SUCCESS(rcNt);
        }
    }
#endif
    return ReadProcessMemory(hProcess, (void *)(uintptr_t)ullSrc, pvDst, cbToRead, pcbRead);
}


/**
 * Wrapper around WriteProcessMemory in case WOW64 tricks are needed.
 *
 * @returns Success indicator.
 * @param   hProcess            The target process.
 * @param   ullDst              The target address (in @a hProcess).
 * @param   pvSrc               The source address (this process).
 * @param   cbToWrite           How much to write.
 * @param   pcbWritten          Where to return how much was actually written.
 */
static BOOL MyWriteProcessMemory(HANDLE hProcess, ULONGLONG ullDst, void const *pvSrc, SIZE_T cbToWrite, SIZE_T *pcbWritten)
{
#if K_ARCH_BITS != 64
    if (ullDst + cbToWrite - 1 > ~(uintptr_t)0)
    {
        typedef NTSTATUS (NTAPI *PFN_NtWow64WriteVirtualMemory64)(HANDLE, ULONGLONG, VOID const *, ULONGLONG, PULONGLONG);
        static PFN_NtWow64WriteVirtualMemory64 volatile     s_pfnNtWow64WriteVirtualMemory64= NULL;
        static BOOL volatile                                s_fInitialized = FALSE;
        PFN_NtWow64WriteVirtualMemory64                     pfnNtWow64WriteVirtualMemory64 = s_pfnNtWow64WriteVirtualMemory64;
        if (!pfnNtWow64WriteVirtualMemory64 && !s_fInitialized)
        {
            *(FARPROC *)&pfnNtWow64WriteVirtualMemory64 = GetProcAddress(GetModuleHandleA("NTDLL.DLL"), "NtWow64WriteVirtualMemory64");
            s_pfnNtWow64WriteVirtualMemory64      = pfnNtWow64WriteVirtualMemory64;
        }
        if (pfnNtWow64WriteVirtualMemory64)
        {
            struct
            {
                ULONGLONG volatile  ullBefore;
                ULONGLONG           cbWritten64;
                ULONGLONG volatile  ullAfter;
            } Wtf = { ~(ULONGLONG)0, 0, ~(ULONGLONG)0 };
            NTSTATUS  rcNt = pfnNtWow64WriteVirtualMemory64(hProcess, ullDst, pvSrc, cbToWrite, &Wtf.cbWritten64);
            *pcbWritten = (SIZE_T)Wtf.cbWritten64;
            SetLastError(rcNt); /* lazy bird */
            return NT_SUCCESS(rcNt);
        }
    }
#endif
    return WriteProcessMemory(hProcess, (void *)(uintptr_t)ullDst, pvSrc, cbToWrite, pcbWritten);
}


/**
 * Injects standard handles into a child process (created suspended).
 *
 * @returns 0 on success.  On failure a non-zero windows error or NT status,
 *          with details in @a pszErr.
 * @param   hProcess    The child process (created suspended).
 * @param   pafReplace  Selects which handles to actually replace (TRUE) and
 *                      which to leave as-is (FALSE).  The first entry is
 *                      starndard input, second is standard output, and the
 *                      final is standard error.
 * @param   pahHandles  The handle in the current process to inject into the
 *                      child process.  This runs parallel to pafReplace.  The
 *                      values NULL and INVALID_HANDLE_VALUE will be written
 *                      directly to the child without duplication.
 * @param   pszErr      Pointer to error message buffer.
 * @param   cbErr       Size of error message buffer.
 */
int nt_child_inject_standard_handles(HANDLE hProcess, BOOL pafReplace[3], HANDLE pahHandles[3], char *pszErr, size_t cbErr)
{
    typedef NTSTATUS (NTAPI *PFN_NtQueryInformationProcess)(HANDLE, PROCESSINFOCLASS, PVOID, ULONG, PULONG);
    static PFN_NtQueryInformationProcess volatile       s_pfnNtQueryInformationProcess = NULL;
    PFN_NtQueryInformationProcess                       pfnNtQueryInformationProcess;
#if K_ARCH_BITS != 64
    static PFN_NtQueryInformationProcess volatile       s_pfnNtWow64QueryInformationProcess64 = NULL;
    PFN_NtQueryInformationProcess                       pfnNtWow64QueryInformationProcess64;

    static BOOL                 s_fHostIs64Bit = K_ARCH_BITS == 64;
    static BOOL volatile        s_fCheckedHost = FALSE;
#endif

    static const unsigned       s_offProcessParametersInPeb32 = 0x10;
    static const unsigned       s_offProcessParametersInPeb64 = 0x20;
    static const unsigned       s_offStandardInputInProcParams32 = 0x18;
    static const unsigned       s_offStandardInputInProcParams64 = 0x20;
    static const char * const   s_apszNames[3] = { "standard input", "standard  output", "standard  error" };


    ULONG                       cbActual1 = 0;
    union
    {
        PROCESS_BASIC_INFORMATION Natural;
        struct
        {
            NTSTATUS    ExitStatus;
            ULONGLONG   PebBaseAddress;
            ULONGLONG   AffinityMask;
            ULONG       BasePriority;
            ULONGLONG   UniqueProcessId;
            ULONGLONG   InheritedFromUniqueProcessId;
        } Wow64;
    }                           BasicInfo = { { 0, 0, } };
    ULONGLONG                   ullBasicInfoPeb;
    NTSTATUS                    rcNt;
    ULONGLONG                   ullPeb32 = 0;
    ULONGLONG                   ullPeb64 = 0;
    ULONGLONG                   ullProcParams32 = 0;
    ULONGLONG                   ullProcParams64 = 0;
    DWORD                       au32Handles[3] = { 0, 0, 0 };
    ULONGLONG                   au64Handles[3] = { 0, 0, 0 };
    unsigned                    iFirstToInject;
    unsigned                    cHandlesToInject;
    unsigned                    i;

    /*
     * Analyze the input to figure out exactly what we need to do.
     */
    iFirstToInject = 0;
    while (iFirstToInject < 3 && !pafReplace[iFirstToInject])
        iFirstToInject++;
    if (iFirstToInject >= 3)
        return 0;

    cHandlesToInject = 3 - iFirstToInject;
    while (   cHandlesToInject > 1
           && !pafReplace[iFirstToInject + cHandlesToInject - 1])
        cHandlesToInject--;

#if K_ARCH_BITS != 64
    /*
     * Determine host bit count first time through.
     */
    if (!s_fCheckedHost)
    {
        BOOL fAmIWow64 = FALSE;
        if (   IsWow64Process(GetCurrentProcess(), &fAmIWow64)
            && fAmIWow64)
            s_fHostIs64Bit = TRUE;
        else
            s_fHostIs64Bit = FALSE;
        s_fCheckedHost = TRUE;
    }
#endif

    /*
     * Resolve NT API first time through.
     */
    pfnNtQueryInformationProcess        = s_pfnNtQueryInformationProcess;
#if K_ARCH_BITS != 64
    pfnNtWow64QueryInformationProcess64 = s_pfnNtWow64QueryInformationProcess64;
#endif
    if (!pfnNtQueryInformationProcess)
    {
        HMODULE hmodNtDll = GetModuleHandleA("NTDLL.DLL");
#if K_ARCH_BITS != 64
        *(FARPROC *)&pfnNtWow64QueryInformationProcess64 = GetProcAddress(hmodNtDll, "NtWow64QueryInformationProcess64");
        s_pfnNtWow64QueryInformationProcess64 = pfnNtWow64QueryInformationProcess64;
#endif
        *(FARPROC *)&pfnNtQueryInformationProcess = GetProcAddress(hmodNtDll, "NtQueryInformationProcess");
        if (!pfnNtQueryInformationProcess)
        {
            _snprintf(pszErr, cbErr, "The NtQueryInformationProcess API was not found in NTDLL");
            return ERROR_PROC_NOT_FOUND;
        }
        s_pfnNtQueryInformationProcess = pfnNtQueryInformationProcess;
    }

    /*
     * Get the PEB address.
     *
     * If we're a WOW64 process, we must use NtWow64QueryInformationProcess64
     * here or the PEB address will be set to zero for 64-bit children.
     */
#if K_ARCH_BITS != 64
    if (s_fHostIs64Bit && pfnNtWow64QueryInformationProcess64)
    {
        rcNt = pfnNtWow64QueryInformationProcess64(hProcess, ProcessBasicInformation, &BasicInfo.Wow64,
                                                   sizeof(BasicInfo.Wow64), &cbActual1);
        if (!NT_SUCCESS(rcNt))
        {
            _snprintf(pszErr, cbErr, "NtWow64QueryInformationProcess64 failed: %#x", rcNt);
            return rcNt;
        }
        if ((ULONGLONG)BasicInfo.Wow64.PebBaseAddress < 0x1000)
        {
            _snprintf(pszErr, cbErr, "NtWow64QueryInformationProcess64 returned bad PebBaseAddress: %#llx",
                      BasicInfo.Wow64.PebBaseAddress);
            return ERROR_INVALID_ADDRESS;
        }
        ullBasicInfoPeb = BasicInfo.Wow64.PebBaseAddress;
    }
    else
#endif
    {
        rcNt = pfnNtQueryInformationProcess(hProcess, ProcessBasicInformation, &BasicInfo.Natural,
                                            sizeof(BasicInfo.Natural), &cbActual1);
        if (!NT_SUCCESS(rcNt))
        {
            _snprintf(pszErr, cbErr, "NtQueryInformationProcess failed: %#x", rcNt);
            return rcNt;
        }
        if ((uintptr_t)BasicInfo.Natural.PebBaseAddress < 0x1000)
        {
            _snprintf(pszErr, cbErr, "NtQueryInformationProcess returned bad PebBaseAddress: %#llx",
                      (ULONGLONG)BasicInfo.Natural.PebBaseAddress);
            return ERROR_INVALID_ADDRESS;
        }
        ullBasicInfoPeb = (uintptr_t)BasicInfo.Natural.PebBaseAddress;
    }

    /*
     * Get the 32-bit PEB if it's a WOW64 process.
     * This query should return 0 for non-WOW64 processes, but we quietly
     * ignore failures and assume non-WOW64 child.
     */
#if K_ARCH_BITS != 64
    if (!s_fHostIs64Bit)
        ullPeb32 = ullBasicInfoPeb;
    else
#endif
    {
        ULONG_PTR uPeb32Ptr = 0;
        cbActual1 = 0;
        rcNt = pfnNtQueryInformationProcess(hProcess, ProcessWow64Information, &uPeb32Ptr, sizeof(uPeb32Ptr), &cbActual1);
        if (NT_SUCCESS(rcNt) && uPeb32Ptr != 0)
        {
            ullPeb32 = uPeb32Ptr;
            ullPeb64 = ullBasicInfoPeb;
#if K_ARCH_BITS != 64
            assert(ullPeb64 != ullPeb32);
            if (ullPeb64 == ullPeb32)
                ullPeb64 = 0;
#endif
        }
        else
        {
            assert(NT_SUCCESS(rcNt));
            ullPeb64 = ullBasicInfoPeb;
        }
    }

    /*
     * Read the process parameter pointers.
     */
    if (ullPeb32)
    {
        DWORD  uProcParamPtr = 0;
        SIZE_T cbRead = 0;
        if (   MyReadProcessMemory(hProcess, ullPeb32 + s_offProcessParametersInPeb32,
                                   &uProcParamPtr, sizeof(uProcParamPtr), &cbRead)
            && cbRead == sizeof(uProcParamPtr))
            ullProcParams32 = uProcParamPtr;
        else
        {
            DWORD dwErr = GetLastError();
            _snprintf(pszErr, cbErr, "Failed to read PEB32!ProcessParameter at %#llx: %u/%#x (%u read)",
                      ullPeb32 + s_offProcessParametersInPeb32, dwErr, dwErr, cbRead);
            return dwErr ? dwErr : -1;
        }
        if (uProcParamPtr < 0x1000)
        {
            _snprintf(pszErr, cbErr, "Bad PEB32!ProcessParameter value: %#llx", ullProcParams32);
            return ERROR_INVALID_ADDRESS;
        }
    }

    if (ullPeb64)
    {
        ULONGLONG  uProcParamPtr = 0;
        SIZE_T     cbRead = 0;
        if (   MyReadProcessMemory(hProcess, ullPeb64 + s_offProcessParametersInPeb64,
                                   &uProcParamPtr, sizeof(uProcParamPtr), &cbRead)
            && cbRead == sizeof(uProcParamPtr))
            ullProcParams64 = uProcParamPtr;
        else
        {
            DWORD dwErr = GetLastError();
            _snprintf(pszErr, cbErr, "Failed to read PEB64!ProcessParameter at %p: %u/%#x (%u read)",
                      ullPeb64 + s_offProcessParametersInPeb64, dwErr, dwErr, cbRead);
            return dwErr ? dwErr : -1;
        }
        if (uProcParamPtr < 0x1000)
        {
            _snprintf(pszErr, cbErr, "Bad PEB64!ProcessParameter value: %#llx", uProcParamPtr);
            return ERROR_INVALID_ADDRESS;
        }
    }

    /*
     * If we're replacing standard input and standard error but not standard
     * output, we must read the standard output handle.  We ASSUME that in
     * WOW64 processes the two PEBs have the same value, saving a read.
     */
    if (iFirstToInject == 0 && cHandlesToInject == 3 && !pafReplace[1])
    {
        if (ullProcParams64)
        {
            SIZE_T cbRead = 0;
            if (   MyReadProcessMemory(hProcess, ullProcParams64 + s_offStandardInputInProcParams64 + sizeof(au64Handles[0]),
                                       &au64Handles[1], sizeof(au64Handles[1]), &cbRead)
                && cbRead == sizeof(au64Handles[1]))
                au32Handles[1] = (DWORD)au64Handles[1];
            else
            {
                DWORD dwErr = GetLastError();
                _snprintf(pszErr, cbErr, "Failed to read ProcessParameter64!StandardOutput at %#llx: %u/%#x (%u read)",
                          ullProcParams64 + s_offStandardInputInProcParams64 + sizeof(au64Handles[0]), dwErr, dwErr, cbRead);
                return dwErr ? dwErr : -1;
            }
        }
        else if (ullProcParams32)
        {
            SIZE_T cbRead = 0;
            if (   !MyReadProcessMemory(hProcess, ullProcParams32 + s_offStandardInputInProcParams32 + sizeof(au32Handles[0]),
                                        &au32Handles[1], sizeof(au32Handles[1]), &cbRead)
                || cbRead != sizeof(au32Handles[1]))
            {
                DWORD dwErr = GetLastError();
                _snprintf(pszErr, cbErr, "Failed to read ProcessParameter32!StandardOutput at %#llx: %u/%#x (%u read)",
                          ullProcParams32 + s_offStandardInputInProcParams32 + sizeof(au32Handles[0]), dwErr, dwErr, cbRead);
                return dwErr ? dwErr : -1;
            }
        }
    }

    /*
     * Duplicate the handles into process, preparing the two handle arrays
     * that we'll write to the guest afterwards.
     */
    for (i = iFirstToInject; i < 3; i++)
        if (pafReplace[i])
        {
            HANDLE hInChild = pahHandles[i];
            if (   hInChild == NULL
                || hInChild == INVALID_HANDLE_VALUE
                || DuplicateHandle(GetCurrentProcess(), pahHandles[i], hProcess, &hInChild,
                                   0 /*fDesiredAccess*/, TRUE /*fInheritable*/, DUPLICATE_SAME_ACCESS))
            {
                au32Handles[i] = (DWORD)(uintptr_t)hInChild;
                au64Handles[i] = (uintptr_t)hInChild;
            }
            else
            {
                DWORD dwErr = GetLastError();
                _snprintf(pszErr, cbErr, "Failed to duplicate handle %p into the child as %s: %u",
                          pahHandles[i], s_apszNames[i], dwErr);
                return dwErr ? dwErr : -1;
            }
        }

    /*
     * Write the handle arrays to the child.
     *
     * If we're a WOW64 we need to use NtWow64WriteVirtualMemory64 instead of
     * WriteProcessMemory because the latter fails with ERROR_NOACCESS (998).
     * So, we use a wrapper for doing the writing.
     */
    if (ullProcParams32)
    {
        ULONGLONG   ullDst   = ullProcParams32 + s_offStandardInputInProcParams32 + iFirstToInject * sizeof(au32Handles[0]);
        SIZE_T      cbToWrite = cHandlesToInject * sizeof(au32Handles[0]);
        SIZE_T      cbWritten = 0;
        if (   !MyWriteProcessMemory(hProcess, ullDst, &au32Handles[iFirstToInject], cbToWrite, &cbWritten)
            || cbWritten != cbToWrite)
        {
            DWORD dwErr = GetLastError();
            _snprintf(pszErr, cbErr, "Failed to write handles to ProcessParameter32 (%#llx LB %u): %u/%#x (%u written)",
                      ullDst, cbToWrite, dwErr, dwErr, cbWritten);
            return dwErr ? dwErr : ERROR_MORE_DATA;
        }
    }

    if (ullProcParams64)
    {
        ULONGLONG   ullDst    = ullProcParams64 + s_offStandardInputInProcParams64 + iFirstToInject * sizeof(au64Handles[0]);
        SIZE_T      cbToWrite = cHandlesToInject * sizeof(au64Handles[0]);
        SIZE_T      cbWritten = 0;
        if (   !MyWriteProcessMemory(hProcess, ullDst, &au64Handles[iFirstToInject], cbToWrite, &cbWritten)
            || cbWritten != cbToWrite)
        {
            DWORD dwErr = GetLastError();
            _snprintf(pszErr, cbErr, "Failed to write handles to ProcessParameter64 (%#llx LB %u): %u/%#x (%u written)",
                      ullDst, cbToWrite, dwErr, dwErr, cbWritten);
            return dwErr ? dwErr : ERROR_MORE_DATA;
        }
    }

    /* Done successfully! */
    return 0;
}

