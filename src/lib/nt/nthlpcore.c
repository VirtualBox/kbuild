/* $Id: nthlpcore.c 2715 2013-12-06 17:41:35Z knut.osmundsen@oracle.com $ */
/** @file
 * MSC + NT core helpers functions and globals.
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
#include <errno.h>
#include "nthlp.h"
#ifndef NDEBUG
# include <stdio.h>
#endif


/*******************************************************************************
*   Global Variables                                                           *
*******************************************************************************/
MY_NTSTATUS (WINAPI *g_pfnNtClose)(HANDLE);
MY_NTSTATUS (WINAPI *g_pfnNtCreateFile)(PHANDLE, MY_ACCESS_MASK, MY_OBJECT_ATTRIBUTES *, MY_IO_STATUS_BLOCK *,
                                        PLARGE_INTEGER, ULONG, ULONG, ULONG, ULONG, PVOID, ULONG);
MY_NTSTATUS (WINAPI *g_pfnNtDeleteFile)(MY_OBJECT_ATTRIBUTES *);
MY_NTSTATUS (WINAPI *g_pfnNtQueryInformationFile)(HANDLE, MY_IO_STATUS_BLOCK *, PVOID, LONG, MY_FILE_INFORMATION_CLASS);
MY_NTSTATUS (WINAPI *g_pfnNtQueryVolumeInformationFile)(HANDLE, MY_IO_STATUS_BLOCK *, PVOID, LONG, MY_FS_INFORMATION_CLASS);
MY_NTSTATUS (WINAPI *g_pfnNtQueryDirectoryFile)(HANDLE, HANDLE, MY_IO_APC_ROUTINE *, PVOID, MY_IO_STATUS_BLOCK *,
                                                PVOID, ULONG, MY_FILE_INFORMATION_CLASS, BOOLEAN,
                                                MY_UNICODE_STRING *, BOOLEAN);
MY_NTSTATUS (WINAPI *g_pfnNtSetInformationFile)(HANDLE, MY_IO_STATUS_BLOCK *, PVOID, LONG, MY_FILE_INFORMATION_CLASS);
BOOLEAN     (WINAPI *g_pfnRtlDosPathNameToNtPathName_U)(PCWSTR, MY_UNICODE_STRING *, PCWSTR *, MY_RTL_RELATIVE_NAME_U *);
MY_NTSTATUS (WINAPI *g_pfnRtlAnsiStringToUnicodeString)(MY_UNICODE_STRING *, MY_ANSI_STRING const *, BOOLEAN);


static struct
{
    FARPROC    *ppfn;
    const char *pszName;
} const g_apfnDynamicNtdll[] =
{
    { (FARPROC *)&g_pfnNtClose,                         "NtClose" },
    { (FARPROC *)&g_pfnNtCreateFile,                    "NtCreateFile" },
    { (FARPROC *)&g_pfnNtDeleteFile,                    "NtDeleteFile" },
    { (FARPROC *)&g_pfnNtQueryInformationFile,          "NtQueryInformationFile" },
    { (FARPROC *)&g_pfnNtQueryVolumeInformationFile,    "NtQueryVolumeInformationFile" },
    { (FARPROC *)&g_pfnNtQueryDirectoryFile,            "NtQueryDirectoryFile" },
    { (FARPROC *)&g_pfnNtSetInformationFile,            "NtSetInformationFile" },
    { (FARPROC *)&g_pfnRtlDosPathNameToNtPathName_U,    "RtlDosPathNameToNtPathName_U" },
    { (FARPROC *)&g_pfnRtlAnsiStringToUnicodeString,    "RtlAnsiStringToUnicodeString" },
};
/** Set to 1 if we've successfully resolved the imports, otherwise 0. */
int g_fResolvedNtImports = 0;



void birdResolveImportsWorker(void)
{
    HMODULE     hMod = LoadLibraryW(L"ntdll.dll");
    int         i    = sizeof(g_apfnDynamicNtdll) / sizeof(g_apfnDynamicNtdll[0]);
    while (i-- > 0)
    {
        const char *pszSym = g_apfnDynamicNtdll[i].pszName;
        FARPROC     pfn;
        *g_apfnDynamicNtdll[i].ppfn = pfn = GetProcAddress(hMod, pszSym);
        if (!pfn)
        {
            /* Write short message and die. */
            static const char   s_szMsg[] = "\r\nFatal error resolving NTDLL.DLL symbols!\r\nSymbol: ";
            DWORD               cbWritten;
            if (   !WriteFile(GetStdHandle(STD_ERROR_HANDLE), s_szMsg, sizeof(s_szMsg) - 1, &cbWritten, NULL)
                || !WriteFile(GetStdHandle(STD_ERROR_HANDLE), pszSym, (DWORD)strlen(pszSym), &cbWritten, NULL)
                || !WriteFile(GetStdHandle(STD_ERROR_HANDLE), "\r\n", sizeof("\r\n") - 1, &cbWritten, NULL)
                )
                *(void **)i = NULL;
            ExitProcess(127);
        }
    }

    g_fResolvedNtImports = 1;
}


void *birdTmpAlloc(size_t cb)
{
    return malloc(cb);
}


void birdTmpFree(void *pv)
{
    if (pv)
        free(pv);
}


void *birdMemAlloc(size_t cb)
{
    return malloc(cb);
}


void *birdMemAllocZ(size_t cb)
{
    return calloc(cb, 1);
}


void birdMemFree(void *pv)
{
    if (pv)
        free(pv);
}


int birdErrnoFromNtStatus(MY_NTSTATUS rcNt)
{
    switch (rcNt)
    {
        /* EPERM            =  1 */
        case STATUS_CANNOT_DELETE:
        case STATUS_DELETE_PENDING:
            return EPERM;
        /* ENOENT           =  2 */
        case STATUS_NOT_FOUND:
        case STATUS_OBJECT_NAME_NOT_FOUND:
        case STATUS_OBJECT_PATH_NOT_FOUND:
        case STATUS_OBJECT_NAME_INVALID:
        case STATUS_INVALID_COMPUTER_NAME:
        case STATUS_VARIABLE_NOT_FOUND:
        case STATUS_MESSAGE_NOT_FOUND:
        case STATUS_DLL_NOT_FOUND:
        case STATUS_ORDINAL_NOT_FOUND:
        case STATUS_ENTRYPOINT_NOT_FOUND:
        case STATUS_PATH_NOT_COVERED:
        case STATUS_BAD_NETWORK_PATH:
        case STATUS_DFS_EXIT_PATH_FOUND:
        case RPC_NT_OBJECT_NOT_FOUND:
            return ENOENT;
        /* ESRCH            =  3 */
        case STATUS_PROCESS_NOT_IN_JOB:
            return ESRCH;
        /* EINTR            =  4 */
        case STATUS_ALERTED:
        case STATUS_USER_APC:
            return EINTR;
        /* EIO              =  5 */
        /* ENXIO            =  6 */
        /* E2BIG            =  7 */
        /* ENOEXEC          =  8 */
        case STATUS_INVALID_IMAGE_FORMAT:
        case STATUS_INVALID_IMAGE_NE_FORMAT:
        case STATUS_INVALID_IMAGE_LE_FORMAT:
        case STATUS_INVALID_IMAGE_NOT_MZ:
        case STATUS_INVALID_IMAGE_PROTECT:
        case STATUS_INVALID_IMAGE_WIN_16:
        case STATUS_IMAGE_SUBSYSTEM_NOT_PRESENT:
        case STATUS_IMAGE_CHECKSUM_MISMATCH:
        case STATUS_IMAGE_MP_UP_MISMATCH:
        case STATUS_IMAGE_MACHINE_TYPE_MISMATCH:
        case STATUS_IMAGE_MACHINE_TYPE_MISMATCH_EXE:
        case STATUS_SYSTEM_IMAGE_BAD_SIGNATURE:
        case STATUS_SECTION_NOT_IMAGE:
        case STATUS_INVALID_IMAGE_WIN_32:
        case STATUS_INVALID_IMAGE_WIN_64:
        case STATUS_INVALID_IMAGE_HASH:
        case STATUS_IMAGE_CERT_REVOKED:
            return ENOEXEC;
        /* EBADF            =  9 */
        case STATUS_INVALID_HANDLE:
        case STATUS_PORT_CLOSED:
        case STATUS_OPLOCK_HANDLE_CLOSED:
        case STATUS_HANDLES_CLOSED:
        case STATUS_FILE_FORCED_CLOSED:
            return EBADF;
        /* ECHILD           = 10 */
        /* EAGAIN           = 11 */
        case STATUS_WMI_TRY_AGAIN:
        case STATUS_GRAPHICS_TRY_AGAIN_LATER:
        case STATUS_GRAPHICS_TRY_AGAIN_NOW:
            return EAGAIN;
        /* ENOMEM           = 12 */
        case STATUS_NO_MEMORY:
        case STATUS_HV_INSUFFICIENT_MEMORY:
        case STATUS_INSUFFICIENT_RESOURCES:
        case STATUS_REMOTE_RESOURCES:
        case STATUS_INSUFF_SERVER_RESOURCES:
            return ENOMEM;
        /* EACCES           = 13 */
        case STATUS_ACCESS_DENIED:
        case STATUS_NETWORK_ACCESS_DENIED:
        case RPC_NT_PROXY_ACCESS_DENIED:
        case STATUS_CTX_SHADOW_DENIED:
        case STATUS_CTX_WINSTATION_ACCESS_DENIED:
            return EACCES;
        /* EFAULT           = 14 */
        case STATUS_ACCESS_VIOLATION:
        case STATUS_HARDWARE_MEMORY_ERROR:
            return EFAULT;
        /* EBUSY            = 16 */
        case STATUS_PIPE_BUSY:
        case STATUS_RESOURCE_IN_USE:
            return EBUSY;
        /* EEXIST           = 17 */
        case STATUS_OBJECT_NAME_EXISTS:
        case STATUS_OBJECT_NAME_COLLISION:
        case STATUS_DUPLICATE_NAME:
            return EEXIST;
        /* EXDEV            = 18 */
        case STATUS_NOT_SAME_DEVICE:
            return EXDEV;
        /* ENODEV           = 19 */
        /* ENOTDIR          = 20 */
        case STATUS_NOT_A_DIRECTORY:
        case STATUS_DIRECTORY_IS_A_REPARSE_POINT:
        case STATUS_OBJECT_PATH_SYNTAX_BAD:
        case STATUS_OBJECT_PATH_INVALID:
        case STATUS_OBJECT_TYPE_MISMATCH:
            return ENOTDIR;
        /* EISDIR           = 21 */
        case STATUS_FILE_IS_A_DIRECTORY:
            return EISDIR;
        /* EINVAL           = 22 */
        case STATUS_INVALID_PARAMETER:
        case STATUS_INVALID_PARAMETER_1:
        case STATUS_INVALID_PARAMETER_2:
        case STATUS_INVALID_PARAMETER_3:
        case STATUS_INVALID_PARAMETER_4:
        case STATUS_INVALID_PARAMETER_5:
        case STATUS_INVALID_PARAMETER_6:
        case STATUS_INVALID_PARAMETER_7:
        case STATUS_INVALID_PARAMETER_8:
        case STATUS_INVALID_PARAMETER_9:
        case STATUS_INVALID_PARAMETER_10:
        case STATUS_INVALID_PARAMETER_11:
        case STATUS_INVALID_PARAMETER_12:
        case STATUS_INVALID_PARAMETER_MIX:
            return EINVAL;
        /* ENFILE           = 23 */
        /* EMFILE           = 24 */
        case STATUS_TOO_MANY_OPENED_FILES:
            return EMFILE;
        /* ENOTTY           = 25 */
        /* EFBIG            = 27 */
        /* ENOSPC           = 28 */
        case STATUS_DISK_FULL:
            return ENOSPC;
        /* ESPIPE           = 29 */
        /* EROFS            = 30 */
        /* EMLINK           = 31 */
        /* EPIPE            = 32 */
        case STATUS_PIPE_BROKEN:
        case RPC_NT_PIPE_CLOSED:
            return EPIPE;
        /* EDOM             = 33 */
        /* ERANGE           = 34 */
        /* EDEADLK          = 36 */
        case STATUS_POSSIBLE_DEADLOCK:
            return EDEADLK;
        /* ENAMETOOLONG     = 38 */
        case STATUS_NAME_TOO_LONG:
            return ENAMETOOLONG;
        /* ENOLCK           = 39 */
        /* ENOSYS           = 40 */
        case STATUS_NOT_SUPPORTED:
            return ENOSYS;
        /* ENOTEMPTY        = 41 */
        case STATUS_DIRECTORY_NOT_EMPTY:
            return ENOTEMPTY;
        /* EILSEQ           = 42 */
        /* EADDRINUSE       = 100 */
        /* EADDRNOTAVAIL    = 101 */
        /* EAFNOSUPPORT     = 102 */
        /* EALREADY         = 103 */
        case STATUS_INTERRUPT_VECTOR_ALREADY_CONNECTED:
        case STATUS_DEVICE_ALREADY_ATTACHED:
        case STATUS_PORT_ALREADY_SET:
        case STATUS_IMAGE_ALREADY_LOADED:
        case STATUS_TOKEN_ALREADY_IN_USE:
        case STATUS_IMAGE_ALREADY_LOADED_AS_DLL:
        case STATUS_ADDRESS_ALREADY_EXISTS:
        case STATUS_ADDRESS_ALREADY_ASSOCIATED:
            return EALREADY;
        /* EBADMSG          = 104 */
        /* ECANCELED        = 105 */
        /* ECONNABORTED     = 106 */
        /* ECONNREFUSED     = 107 */
        /* ECONNRESET       = 108 */
        /* EDESTADDRREQ     = 109 */
        /* EHOSTUNREACH     = 110 */
        case STATUS_HOST_UNREACHABLE:
            return EHOSTUNREACH;
        /* EIDRM            = 111 */
        /* EINPROGRESS      = 112 */
        /* EISCONN          = 113 */
        /* ELOOP            = 114 */
        /* EMSGSIZE         = 115 */
        /* ENETDOWN         = 116 */
        /* ENETRESET        = 117 */
        /* ENETUNREACH      = 118 */
        case STATUS_NETWORK_UNREACHABLE:
            return ENETUNREACH;
        /* ENOBUFS          = 119 */
        /* ENODATA          = 120 */
        /* ENOLINK          = 121 */
        /* ENOMSG           = 122 */
        /* ENOPROTOOPT      = 123 */
        /* ENOSR            = 124 */
        /* ENOSTR           = 125 */
        /* ENOTCONN         = 126 */
        /* ENOTRECOVERABLE  = 127 */
        /* ENOTSOCK         = 128 */
        /* ENOTSUP          = 129 */
        /* EOPNOTSUPP       = 130 */
        /* EOTHER           = 131 */
        /* EOVERFLOW        = 132 */
        /* EOWNERDEAD       = 133 */
        /* EPROTO           = 134 */
        /* EPROTONOSUPPORT  = 135 */
        /* EPROTOTYPE       = 136 */
        /* ETIME            = 137 */
        /* ETIMEDOUT        = 138 */
        case STATUS_VIRTUAL_CIRCUIT_CLOSED:
        case STATUS_TIMEOUT:
            return ETIMEDOUT;

        /* ETXTBSY          = 139 */
        case STATUS_SHARING_VIOLATION:
            return ETXTBSY;
        /* EWOULDBLOCK      = 140 */
    }

#ifndef NDEBUG
    __debugbreak();
    fprintf(stderr, "rcNt=%#x (%d)\n", rcNt, rcNt);
#endif
    return EINVAL;
}


int birdSetErrnoFromNt(MY_NTSTATUS rcNt)
{
    errno = birdErrnoFromNtStatus(rcNt);
    return -1;
}


int birdSetErrnoFromWin32(DWORD dwErr)
{
    switch (dwErr)
    {
        default:
        case ERROR_INVALID_FUNCTION:        errno = EINVAL; break;
        case ERROR_FILE_NOT_FOUND:          errno = ENOENT; break;
        case ERROR_PATH_NOT_FOUND:          errno = ENOENT; break;
        case ERROR_TOO_MANY_OPEN_FILES:     errno = EMFILE; break;
        case ERROR_ACCESS_DENIED:           errno = EACCES; break;
        case ERROR_INVALID_HANDLE:          errno = EBADF; break;
        case ERROR_ARENA_TRASHED:           errno = ENOMEM; break;
        case ERROR_NOT_ENOUGH_MEMORY:       errno = ENOMEM; break;
        case ERROR_INVALID_BLOCK:           errno = ENOMEM; break;
        case ERROR_BAD_ENVIRONMENT:         errno = E2BIG; break;
        case ERROR_BAD_FORMAT:              errno = ENOEXEC; break;
        case ERROR_INVALID_ACCESS:          errno = EINVAL; break;
        case ERROR_INVALID_DATA:            errno = EINVAL; break;
        case ERROR_INVALID_DRIVE:           errno = ENOENT; break;
        case ERROR_CURRENT_DIRECTORY:       errno = EACCES; break;
        case ERROR_NOT_SAME_DEVICE:         errno = EXDEV; break;
        case ERROR_NO_MORE_FILES:           errno = ENOENT; break;
        case ERROR_LOCK_VIOLATION:          errno = EACCES; break;
        case ERROR_BAD_NETPATH:             errno = ENOENT; break;
        case ERROR_NETWORK_ACCESS_DENIED:   errno = EACCES; break;
        case ERROR_BAD_NET_NAME:            errno = ENOENT; break;
        case ERROR_FILE_EXISTS:             errno = EEXIST; break;
        case ERROR_CANNOT_MAKE:             errno = EACCES; break;
        case ERROR_FAIL_I24:                errno = EACCES; break;
        case ERROR_INVALID_PARAMETER:       errno = EINVAL; break;
        case ERROR_NO_PROC_SLOTS:           errno = EAGAIN; break;
        case ERROR_DRIVE_LOCKED:            errno = EACCES; break;
        case ERROR_BROKEN_PIPE:             errno = EPIPE; break;
        case ERROR_DISK_FULL:               errno = ENOSPC; break;
        case ERROR_INVALID_TARGET_HANDLE:   errno = EBADF; break;
        case ERROR_WAIT_NO_CHILDREN:        errno = ECHILD; break;
        case ERROR_CHILD_NOT_COMPLETE:      errno = ECHILD; break;
        case ERROR_DIRECT_ACCESS_HANDLE:    errno = EBADF; break;
        case ERROR_NEGATIVE_SEEK:           errno = EINVAL; break;
        case ERROR_SEEK_ON_DEVICE:          errno = EACCES; break;
        case ERROR_DIR_NOT_EMPTY:           errno = ENOTEMPTY; break;
        case ERROR_NOT_LOCKED:              errno = EACCES; break;
        case ERROR_BAD_PATHNAME:            errno = ENOENT; break;
        case ERROR_MAX_THRDS_REACHED:       errno = EAGAIN; break;
        case ERROR_LOCK_FAILED:             errno = EACCES; break;
        case ERROR_ALREADY_EXISTS:          errno = EEXIST; break;
        case ERROR_FILENAME_EXCED_RANGE:    errno = ENOENT; break;
        case ERROR_NESTING_NOT_ALLOWED:     errno = EAGAIN; break;
#ifdef EMLINK
        case ERROR_TOO_MANY_LINKS:          errno = EMLINK; break;
#endif

        case ERROR_SHARING_VIOLATION:
            errno = ETXTBSY;
            break;
    }

    return -1;
}


int birdSetErrnoToNoMem(void)
{
    errno = ENOMEM;
    return -1;
}


int birdSetErrnoToInvalidArg(void)
{
    errno = EINVAL;
    return -1;
}


int birdSetErrnoToBadFileNo(void)
{
    errno = EBADF;
    return -1;
}

