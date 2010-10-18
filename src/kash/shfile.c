/* $Id: shfile.c 2425 2010-10-18 08:52:22Z knut.osmundsen@oracle.com $ */
/** @file
 *
 * File management.
 *
 * Copyright (c) 2007-2010 knut st. osmundsen <bird-kBuild-spamx@anduin.net>
 *
 *
 * This file is part of kBuild.
 *
 * kBuild is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * kBuild is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with kBuild; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
#include "shfile.h"
#include "shinstance.h" /* TRACE2 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#if K_OS == K_OS_WINDOWS
# include <limits.h>
# ifndef PIPE_BUF
#  define PIPE_BUF 512
# endif
# include <ntstatus.h>
# define WIN32_NO_STATUS
# include <Windows.h>
# if !defined(_WIN32_WINNT)
#  define _WIN32_WINNT 0x0502 /* Windows Server 2003 */
# endif
# include <winternl.h> //NTSTATUS
#else
# include <unistd.h>
# include <fcntl.h>
# include <dirent.h>
#endif


/*******************************************************************************
*   Defined Constants And Macros                                               *
*******************************************************************************/
/** @def SHFILE_IN_USE
 * Whether the file descriptor table stuff is actually in use or not.
 */
#if K_OS == K_OS_WINDOWS \
 || K_OS == K_OS_OPENBSD /* because of ugly pthread library pipe hacks */ \
 || !defined(SH_FORKED_MODE)
# define SHFILE_IN_USE
#endif
/** The max file table size. */
#define SHFILE_MAX          1024
/** The file table growth rate. */
#define SHFILE_GROW         64
/** The min native unix file descriptor. */
#define SHFILE_UNIX_MIN_FD  32
/** The path buffer size we use. */
#define SHFILE_MAX_PATH     4096

/** Set errno and return. Doing a trace in debug build. */
#define RETURN_ERROR(rc, err, msg)  \
    do { \
        TRACE2((NULL, "%s: " ## msg ## " - returning %d / %d\n", __FUNCTION__, (rc), (err))); \
        errno = (err); \
        return (rc); \
    } while (0)

#if K_OS == K_OS_WINDOWS
 /* See msdos.h for description. */
# define FOPEN              0x01
# define FEOFLAG            0x02
# define FCRLF              0x04
# define FPIPE              0x08
# define FNOINHERIT         0x10
# define FAPPEND            0x20
# define FDEV               0x40
# define FTEXT              0x80

# define MY_ObjectBasicInformation 0
# define MY_FileNamesInformation 12

typedef struct
{
    ULONG           Attributes;
	ACCESS_MASK     GrantedAccess;
	ULONG           HandleCount;
	ULONG           PointerCount;
	ULONG           PagedPoolUsage;
	ULONG           NonPagedPoolUsage;
	ULONG           Reserved[3];
	ULONG           NameInformationLength;
	ULONG           TypeInformationLength;
	ULONG           SecurityDescriptorLength;
	LARGE_INTEGER   CreateTime;
} MY_OBJECT_BASIC_INFORMATION;

#if 0
typedef struct
{
    union
    {
        LONG    Status;
        PVOID   Pointer;
    };
    ULONG_PTR   Information;
} MY_IO_STATUS_BLOCK;
#else
typedef IO_STATUS_BLOCK MY_IO_STATUS_BLOCK;
#endif
typedef MY_IO_STATUS_BLOCK *PMY_IO_STATUS_BLOCK;

typedef struct
{
    ULONG   NextEntryOffset;
    ULONG   FileIndex;
    ULONG   FileNameLength;
    WCHAR   FileName[1];
} MY_FILE_NAMES_INFORMATION, *PMY_FILE_NAMES_INFORMATION;

typedef NTSTATUS (NTAPI * PFN_NtQueryObject)(HANDLE, int, void *, size_t, size_t *);
typedef NTSTATUS (NTAPI * PFN_NtQueryDirectoryFile)(HANDLE, HANDLE, void *,  void *, PMY_IO_STATUS_BLOCK, void *,
                                                    ULONG, int, int, PUNICODE_STRING, int);
typedef NTSTATUS (NTAPI * PFN_RtlUnicodeStringToAnsiString)(PANSI_STRING, PCUNICODE_STRING, int);


#endif /* K_OS_WINDOWS */


/*******************************************************************************
*   Global Variables                                                           *
*******************************************************************************/
#if K_OS == K_OS_WINDOWS
static int                              g_shfile_globals_initialized = 0;
static PFN_NtQueryObject                g_pfnNtQueryObject = NULL;
static PFN_NtQueryDirectoryFile         g_pfnNtQueryDirectoryFile = NULL;
static PFN_RtlUnicodeStringToAnsiString g_pfnRtlUnicodeStringToAnsiString = NULL;
#endif /* K_OS_WINDOWS */


#ifdef SHFILE_IN_USE

/**
 * Close the specified native handle.
 *
 * @param   native      The native file handle.
 * @param   flags       The flags in case they might come in handy later.
 */
static void shfile_native_close(intptr_t native, unsigned flags)
{
#if K_OS == K_OS_WINDOWS
    BOOL fRc = CloseHandle((HANDLE)native);
    assert(fRc); (void)fRc;
#else
    int s = errno;
    close(native);
    errno = s;
#endif
    (void)flags;
}

/**
 * Grows the descriptor table, making sure that it can hold @a fdMin,
 *
 * @returns The max(fdMin, fdFirstNew) on success, -1 on failure.
 * @param   pfdtab      The table to grow.
 * @param   fdMin       Grow to include this index.
 */
static int shfile_grow_tab_locked(shfdtab *pfdtab, int fdMin)
{
    /*
     * Grow the descriptor table.
     */
    int         fdRet = -1;
    shfile     *new_tab;
    int         new_size = pfdtab->size + SHFILE_GROW;
    while (new_size < fdMin)
        new_size += SHFILE_GROW;
    new_tab = sh_realloc(shthread_get_shell(), pfdtab->tab, new_size * sizeof(shfile));
    if (new_tab)
    {
        int     i;
        for (i = pfdtab->size; i < new_size; i++)
        {
            new_tab[i].fd = -1;
            new_tab[i].oflags = 0;
            new_tab[i].shflags = 0;
            new_tab[i].native = -1;
        }

        fdRet = pfdtab->size;
        if (fdRet < fdMin)
            fdRet = fdMin;

        pfdtab->tab = new_tab;
        pfdtab->size = new_size;
    }

    return fdRet;
}

/**
 * Inserts the file into the descriptor table.
 *
 * If we're out of memory and cannot extend the table, we'll close the
 * file, set errno to EMFILE and return -1.
 *
 * @returns The file descriptor number. -1 and errno on failure.
 * @param   pfdtab      The file descriptor table.
 * @param   native      The native file handle.
 * @param   oflags      The flags the it was opened/created with.
 * @param   shflags     The shell file flags.
 * @param   fdMin       The minimum file descriptor number, pass -1 if any is ok.
 * @param   who         Who we're doing this for (for logging purposes).
 */
static int shfile_insert(shfdtab *pfdtab, intptr_t native, unsigned oflags, unsigned shflags, int fdMin, const char *who)
{
    shmtxtmp tmp;
    int fd;
    int i;

    /*
     * Fend of bad stuff.
     */
    if (fdMin >= SHFILE_MAX)
    {
        errno = EMFILE;
        return -1;
    }
# if K_OS != K_OS_WINDOWS
    if (fcntl((int)native, F_SETFD, fcntl((int)native, F_GETFD, 0) | FD_CLOEXEC) == -1)
    {
        int e = errno;
        close((int)native);
        errno = e;
        return -1;
    }
# endif

    shmtx_enter(&pfdtab->mtx, &tmp);

    /*
     * Search for a fitting unused location.
     */
    fd = -1;
    for (i = fdMin >= 0 ? fdMin : 0; (unsigned)i < pfdtab->size; i++)
        if (pfdtab->tab[i].fd == -1)
        {
            fd = i;
            break;
        }
    if (fd == -1)
        fd = shfile_grow_tab_locked(pfdtab, fdMin);

    /*
     * Fill in the entry if we've found one.
     */
    if (fd != -1)
    {
        pfdtab->tab[fd].fd = fd;
        pfdtab->tab[fd].oflags = oflags;
        pfdtab->tab[fd].shflags = shflags;
        pfdtab->tab[fd].native = native;
    }
    else
        shfile_native_close(native, oflags);

    shmtx_leave(&pfdtab->mtx, &tmp);

    if (fd == -1)
        errno = EMFILE;
    (void)who;
    return fd;
}

#if K_OS != K_OS_WINDOWS
/**
 * Makes a copy of the native file, closes the original, and inserts the copy
 * into the descriptor table.
 *
 * If we're out of memory and cannot extend the table, we'll close the
 * file, set errno to EMFILE and return -1.
 *
 * @returns The file descriptor number. -1 and errno on failure.
 * @param   pfdtab      The file descriptor table.
 * @param   pnative     The native file handle on input, -1 on output.
 * @param   oflags      The flags the it was opened/created with.
 * @param   shflags     The shell file flags.
 * @param   fdMin       The minimum file descriptor number, pass -1 if any is ok.
 * @param   who         Who we're doing this for (for logging purposes).
 */
static int shfile_copy_insert_and_close(shfdtab *pfdtab, int *pnative, unsigned oflags, unsigned shflags, int fdMin, const char *who)
{
    int fd          = -1;
    int s           = errno;
    int native_copy = fcntl(*pnative, F_DUPFD, SHFILE_UNIX_MIN_FD);
    close(*pnative);
    *pnative = -1;
    errno = s;

    if (native_copy != -1)
        fd = shfile_insert(pfdtab, native_copy, oflags, shflags, fdMin, who);
    return fd;
}
#endif /* !K_OS_WINDOWS */

/**
 * Gets a file descriptor and lock the file descriptor table.
 *
 * @returns Pointer to the file and table ownership on success,
 *          NULL and errno set to EBADF on failure.
 * @param   pfdtab      The file descriptor table.
 * @param   fd          The file descriptor number.
 * @param   ptmp        See shmtx_enter.
 */
static shfile *shfile_get(shfdtab *pfdtab, int fd, shmtxtmp *ptmp)
{
    shfile *file = NULL;
    if (    fd >= 0
        &&  (unsigned)fd < pfdtab->size)
    {
        shmtx_enter(&pfdtab->mtx, ptmp);
        if ((unsigned)fd < pfdtab->size
            &&  pfdtab->tab[fd].fd != -1)
            file = &pfdtab->tab[fd];
        else
            shmtx_leave(&pfdtab->mtx, ptmp);
    }
    if (!file)
        errno = EBADF;
    return file;
}

/**
 * Puts back a file descriptor and releases the table ownership.
 *
 * @param   pfdtab      The file descriptor table.
 * @param   file        The file.
 * @param   ptmp        See shmtx_leave.
 */
static void shfile_put(shfdtab *pfdtab, shfile *file, shmtxtmp *ptmp)
{
    shmtx_leave(&pfdtab->mtx, ptmp);
    assert(file);
    (void)file;
}

/**
 * Constructs a path from the current directory and the passed in path.
 *
 * @returns 0 on success, -1 and errno set to ENAMETOOLONG or EINVAL on failure.
 *
 * @param   pfdtab      The file descriptor table
 * @param   path        The path the caller supplied.
 * @param   buf         Where to put the path. This is assumed to be SHFILE_MAX_PATH
 *                      chars in size.
 */
int shfile_make_path(shfdtab *pfdtab, const char *path, char *buf)
{
    size_t path_len = strlen(path);
    if (path_len == 0)
    {
        errno = EINVAL;
        return -1;
    }
    if (path_len >= SHFILE_MAX_PATH)
    {
        errno = ENAMETOOLONG;
        return -1;
    }
    if (    *path == '/'
#if K_OS == K_OS_WINDOWS || K_OS == K_OS_OS2
        ||  *path == '\\'
        ||  (   *path
             && path[1] == ':'
             && (   (*path >= 'A' && *path <= 'Z')
                 || (*path >= 'a' && *path <= 'z')))
#endif
        )
    {
        memcpy(buf, path, path_len + 1);
    }
    else
    {
        size_t      cwd_len;
        shmtxtmp    tmp;

        shmtx_enter(&pfdtab->mtx, &tmp);

        cwd_len = strlen(pfdtab->cwd);
        memcpy(buf, pfdtab->cwd, cwd_len);

        shmtx_leave(&pfdtab->mtx, &tmp);

        if (cwd_len + path_len + 1 >= SHFILE_MAX_PATH)
        {
            errno = ENAMETOOLONG;
            return -1;
        }
        if (    !cwd_len
            ||  buf[cwd_len - 1] != '/')
            buf[cwd_len++] = '/';
        memcpy(buf + cwd_len, path, path_len + 1);
    }

#if K_OS == K_OS_WINDOWS || K_OS == K_OS_OS2
    if (!strcmp(buf, "/dev/null"))
        strcpy(buf, "NUL");
#endif
    return 0;
}

# if K_OS == K_OS_WINDOWS

/**
 * Converts a DOS(/Windows) error code to errno,
 * assigning it to errno.
 *
 * @returns -1
 * @param   err     The DOS error.
 */
static int shfile_dos2errno(int err)
{
    switch (err)
    {
        case ERROR_BAD_ENVIRONMENT:         errno = E2BIG;      break;
        case ERROR_ACCESS_DENIED:           errno = EACCES;     break;
        case ERROR_CURRENT_DIRECTORY:       errno = EACCES;     break;
        case ERROR_LOCK_VIOLATION:          errno = EACCES;     break;
        case ERROR_NETWORK_ACCESS_DENIED:   errno = EACCES;     break;
        case ERROR_CANNOT_MAKE:             errno = EACCES;     break;
        case ERROR_FAIL_I24:                errno = EACCES;     break;
        case ERROR_DRIVE_LOCKED:            errno = EACCES;     break;
        case ERROR_SEEK_ON_DEVICE:          errno = EACCES;     break;
        case ERROR_NOT_LOCKED:              errno = EACCES;     break;
        case ERROR_LOCK_FAILED:             errno = EACCES;     break;
        case ERROR_NO_PROC_SLOTS:           errno = EAGAIN;     break;
        case ERROR_MAX_THRDS_REACHED:       errno = EAGAIN;     break;
        case ERROR_NESTING_NOT_ALLOWED:     errno = EAGAIN;     break;
        case ERROR_INVALID_HANDLE:          errno = EBADF;      break;
        case ERROR_INVALID_TARGET_HANDLE:   errno = EBADF;      break;
        case ERROR_DIRECT_ACCESS_HANDLE:    errno = EBADF;      break;
        case ERROR_WAIT_NO_CHILDREN:        errno = ECHILD;     break;
        case ERROR_CHILD_NOT_COMPLETE:      errno = ECHILD;     break;
        case ERROR_FILE_EXISTS:             errno = EEXIST;     break;
        case ERROR_ALREADY_EXISTS:          errno = EEXIST;     break;
        case ERROR_INVALID_FUNCTION:        errno = EINVAL;     break;
        case ERROR_INVALID_ACCESS:          errno = EINVAL;     break;
        case ERROR_INVALID_DATA:            errno = EINVAL;     break;
        case ERROR_INVALID_PARAMETER:       errno = EINVAL;     break;
        case ERROR_NEGATIVE_SEEK:           errno = EINVAL;     break;
        case ERROR_TOO_MANY_OPEN_FILES:     errno = EMFILE;     break;
        case ERROR_FILE_NOT_FOUND:          errno = ENOENT;     break;
        case ERROR_PATH_NOT_FOUND:          errno = ENOENT;     break;
        case ERROR_INVALID_DRIVE:           errno = ENOENT;     break;
        case ERROR_NO_MORE_FILES:           errno = ENOENT;     break;
        case ERROR_BAD_NETPATH:             errno = ENOENT;     break;
        case ERROR_BAD_NET_NAME:            errno = ENOENT;     break;
        case ERROR_BAD_PATHNAME:            errno = ENOENT;     break;
        case ERROR_FILENAME_EXCED_RANGE:    errno = ENOENT;     break;
        case ERROR_BAD_FORMAT:              errno = ENOEXEC;    break;
        case ERROR_ARENA_TRASHED:           errno = ENOMEM;     break;
        case ERROR_NOT_ENOUGH_MEMORY:       errno = ENOMEM;     break;
        case ERROR_INVALID_BLOCK:           errno = ENOMEM;     break;
        case ERROR_NOT_ENOUGH_QUOTA:        errno = ENOMEM;     break;
        case ERROR_DISK_FULL:               errno = ENOSPC;     break;
        case ERROR_DIR_NOT_EMPTY:           errno = ENOTEMPTY;  break;
        case ERROR_BROKEN_PIPE:             errno = EPIPE;      break;
        case ERROR_NOT_SAME_DEVICE:         errno = EXDEV;      break;
        default:                            errno = EINVAL;     break;
    }
    return -1;
}

/**
 * Converts an NT status code to errno,
 * assigning it to errno.
 *
 * @returns -1
 * @param   rcNt        The NT status code.
 */
static int shfile_nt2errno(NTSTATUS rcNt)
{
    switch (rcNt)
    {
        default:                            errno = EINVAL; break;
    }
    return -1;
}

DWORD shfile_query_handle_access_mask(HANDLE h, PACCESS_MASK pMask)
{
    MY_OBJECT_BASIC_INFORMATION     BasicInfo;
    NTSTATUS                        rcNt;

    if (!g_pfnNtQueryObject)
        return ERROR_NOT_SUPPORTED;

    rcNt = g_pfnNtQueryObject(h, MY_ObjectBasicInformation, &BasicInfo, sizeof(BasicInfo), NULL);
    if (rcNt >= 0)
    {
        *pMask = BasicInfo.GrantedAccess;
        return NO_ERROR;
    }
    if (rcNt != STATUS_INVALID_HANDLE)
        return ERROR_GEN_FAILURE;
    return ERROR_INVALID_HANDLE;
}

# endif /* K_OS == K_OS_WINDOWS */

#endif /* SHFILE_IN_USE */

/**
 * Initializes the global variables in this file.
 */
static void shfile_init_globals(void)
{
#if K_OS == K_OS_WINDOWS
    if (!g_shfile_globals_initialized)
    {
        HMODULE hNtDll = GetModuleHandle("NTDLL");
        g_pfnNtQueryObject                = (PFN_NtQueryObject)       GetProcAddress(hNtDll, "NtQueryObject");
        g_pfnNtQueryDirectoryFile         = (PFN_NtQueryDirectoryFile)GetProcAddress(hNtDll, "NtQueryDirectoryFile");
        g_pfnRtlUnicodeStringToAnsiString = (PFN_RtlUnicodeStringToAnsiString)GetProcAddress(hNtDll, "RtlUnicodeStringToAnsiString");
        if (   !g_pfnRtlUnicodeStringToAnsiString
            || !g_pfnNtQueryDirectoryFile)
        {
            /* fatal error */
        }
        g_shfile_globals_initialized = 1;
    }
#endif
}

/**
 * Initializes a file descriptor table.
 *
 * @returns 0 on success, -1 and errno on failure.
 * @param   pfdtab      The table to initialize.
 * @param   inherit     File descriptor table to inherit from. If not specified
 *                      we will inherit from the current process as it were.
 */
int shfile_init(shfdtab *pfdtab, shfdtab *inherit)
{
    int rc;

    shfile_init_globals();

    pfdtab->cwd  = NULL;
    pfdtab->size = 0;
    pfdtab->tab  = NULL;
    rc = shmtx_init(&pfdtab->mtx);
    if (!rc)
    {
#ifdef SHFILE_IN_USE
        char buf[SHFILE_MAX_PATH];
        if (getcwd(buf, sizeof(buf)))
        {
            pfdtab->cwd = sh_strdup(NULL, buf);
            if (!inherit)
            {
# if K_OS == K_OS_WINDOWS
                static const struct
                {
                    DWORD dwStdHandle;
                    unsigned fFlags;
                } aStdHandles[3] =
                {
                    { STD_INPUT_HANDLE,   _O_RDONLY },
                    { STD_OUTPUT_HANDLE,  _O_WRONLY },
                    { STD_ERROR_HANDLE,   _O_WRONLY }
                };
                int             i;
                STARTUPINFO     Info;
                ACCESS_MASK     Mask;
                DWORD           dwErr;

                rc = 0;

                /* Try pick up the Visual C++ CRT file descriptor info. */
                __try {
                    GetStartupInfo(&Info);
                } __except (EXCEPTION_EXECUTE_HANDLER) {
                    memset(&Info, 0, sizeof(Info));
                }

                if (    Info.cbReserved2 > sizeof(int)
                    &&  (uintptr_t)Info.lpReserved2 >= 0x1000
                    &&  (i = *(int *)Info.lpReserved2) >= 1
                    &&  i <= 2048
                    &&  (   Info.cbReserved2 == i * 5 + 4
                         //|| Info.cbReserved2 == i * 5 + 1 - check the cygwin sources.
                         || Info.cbReserved2 == i * 9 + 4))
                {
                    uint8_t    *paf    = (uint8_t *)Info.lpReserved2 + sizeof(int);
                    int         dwPerH = 1 + (Info.cbReserved2 == i * 9 + 4);
                    DWORD      *ph     = (DWORD *)(paf + i) + dwPerH * i;
                    HANDLE      aStdHandles2[3];
                    int         j;

                    //if (Info.cbReserved2 == i * 5 + 1) - check the cygwin sources.
                    //    i--;

                    for (j = 0; j < 3; j++)
                        aStdHandles2[j] = GetStdHandle(aStdHandles[j].dwStdHandle);

                    while (i-- > 0)
                    {
                        ph -= dwPerH;

                        if (    (paf[i] & (FOPEN | FNOINHERIT)) == FOPEN
                            &&  *ph != (uint32_t)INVALID_HANDLE_VALUE)
                        {
                            HANDLE  h = (HANDLE)(intptr_t)*ph;
                            int     fd2;
                            int     fFlags;
                            int     fFlags2;

                            if (    h == aStdHandles2[j = 0]
                                ||  h == aStdHandles2[j = 1]
                                ||  h == aStdHandles2[j = 2])
                                fFlags = aStdHandles[j].fFlags;
                            else
                            {
                                dwErr = shfile_query_handle_access_mask(h, &Mask);
                                if (dwErr == ERROR_INVALID_HANDLE)
                                    continue;
                                else if (dwErr == NO_ERROR)
                                {
                                    fFlags = 0;
                                    if (    (Mask & (GENERIC_READ | FILE_READ_DATA))
                                        &&  (Mask & (GENERIC_WRITE | FILE_WRITE_DATA | FILE_APPEND_DATA)))
                                        fFlags |= O_RDWR;
                                    else if (Mask & (GENERIC_READ | FILE_READ_DATA))
                                        fFlags |= O_RDONLY;
                                    else if (Mask & (GENERIC_WRITE | FILE_WRITE_DATA | FILE_APPEND_DATA))
                                        fFlags |= O_WRONLY;
                                    else
                                        fFlags |= O_RDWR;
                                    if ((Mask & (FILE_WRITE_DATA | FILE_APPEND_DATA)) == FILE_APPEND_DATA)
                                        fFlags |= O_APPEND;
                                }
                                else
                                    fFlags = O_RDWR;
                            }

                            if (paf[i] & FPIPE)
                                fFlags2 = SHFILE_FLAGS_PIPE;
                            else if (paf[i] & FDEV)
                                fFlags2 = SHFILE_FLAGS_TTY;
                            else
                                fFlags2 = 0;

                            fd2 = shfile_insert(pfdtab, (intptr_t)h, fFlags, fFlags2, i, "shtab_init");
                            assert(fd2 == i); (void)fd2;
                            if (fd2 != i)
                                rc = -1;
                        }
                    }
                }

                /* Check the three standard handles. */
                for (i = 0; i < 3; i++)
                    if (    (unsigned)i >= pfdtab->size
                        ||  pfdtab->tab[i].fd == -1)
                    {
                        HANDLE hFile = GetStdHandle(aStdHandles[i].dwStdHandle);
                        if (hFile != INVALID_HANDLE_VALUE)
                        {
                            DWORD       dwType  = GetFileType(hFile);
                            unsigned    fFlags  = aStdHandles[i].fFlags;
                            unsigned    fFlags2;
                            int         fd2;
                            if (dwType == FILE_TYPE_CHAR)
                                fFlags2 = SHFILE_FLAGS_TTY;
                            else if (dwType == FILE_TYPE_PIPE)
                                fFlags2 = SHFILE_FLAGS_PIPE;
                            else
                                fFlags2 = SHFILE_FLAGS_FILE;
                            fd2 = shfile_insert(pfdtab, (intptr_t)hFile, fFlags, fFlags2, i, "shtab_init");
                            assert(fd2 == i); (void)fd2;
                            if (fd2 != i)
                                rc = -1;
                        }
                    }
# else
                /*
                 * Annoying...
                 */
                int fd;

                for (fd = 0; fd < 10; fd++)
                {
                    int oflags = fcntl(fd, F_GETFL, 0);
                    if (oflags != -1)
                    {
                        int cox = fcntl(fd, F_GETFD, 0);
                        struct stat st;
                        if (   cox != -1
                            && fstat(fd, &st) != -1)
                        {
                            int native;
                            int fd2;
                            int fFlags2 = 0;
                            if (cox & FD_CLOEXEC)
                                fFlags2 |= SHFILE_FLAGS_CLOSE_ON_EXEC;
                            if (S_ISREG(st.st_mode))
                                fFlags2 |= SHFILE_FLAGS_FILE;
                            else if (S_ISDIR(st.st_mode))
                                fFlags2 |= SHFILE_FLAGS_DIR;
                            else if (S_ISCHR(st.st_mode))
                                fFlags2 |= SHFILE_FLAGS_TTY;
                            else if (S_ISFIFO(st.st_mode))
                                fFlags2 |= SHFILE_FLAGS_PIPE;
                            else
                                fFlags2 |= SHFILE_FLAGS_TTY;

                            native = fcntl(fd, F_DUPFD, SHFILE_UNIX_MIN_FD);
                            if (native == -1)
                                native = fd;
                            fd2 = shfile_insert(pfdtab, native, oflags, fFlags2, fd, "shtab_init");
                            assert(fd2 == fd); (void)fd2;
                            if (fd2 != fd)
                                rc = -1;
                            if (native != fd)
                                close(fd);
                        }
                    }
                }

# endif
            }
            else
            {
                /** @todo */
                errno = ENOSYS;
                rc = -1;
            }
        }
        else
            rc = -1;
#endif
    }
    return rc;
}

#if K_OS == K_OS_WINDOWS && defined(SHFILE_IN_USE)

/**
 * Helper for shfork.
 *
 * @param   pfdtab  The file descriptor table.
 * @param   set     Whether to make all handles inheritable (1) or
 *                  to restore them to the rigth state (0).
 * @param   hndls   Where to store the three standard handles.
 */
void shfile_fork_win(shfdtab *pfdtab, int set, intptr_t *hndls)
{
    shmtxtmp tmp;
    unsigned i;
    DWORD fFlag = set ? HANDLE_FLAG_INHERIT : 0;

    shmtx_enter(&pfdtab->mtx, &tmp);
    TRACE2((NULL, "shfile_fork_win: set=%d\n", set));

    i = pfdtab->size;
    while (i-- > 0)
    {
        if (pfdtab->tab[i].fd == i)
        {
            HANDLE hFile = (HANDLE)pfdtab->tab[i].native;
            if (set)
                TRACE2((NULL, "  #%d: native=%#x oflags=%#x shflags=%#x\n",
                        i, hFile, pfdtab->tab[i].oflags, pfdtab->tab[i].shflags));
            if (!SetHandleInformation(hFile, HANDLE_FLAG_INHERIT, fFlag))
            {
                DWORD err = GetLastError();
                assert(0);
            }
        }
    }

    if (hndls)
    {
        for (i = 0; i < 3; i++)
        {
            if (    pfdtab->size > i
                &&  pfdtab->tab[i].fd == i)
                hndls[i] = pfdtab->tab[i].native;
            else
                hndls[i] = (intptr_t)INVALID_HANDLE_VALUE;
            TRACE2((NULL, "shfile_fork_win: i=%d size=%d fd=%d native=%d hndls[%d]=%p\n",
                    i, pfdtab->size, pfdtab->tab[i].fd, pfdtab->tab[i].native, i, hndls[i]));
        }
    }

    shmtx_leave(&pfdtab->mtx, &tmp);
}

/**
 * Helper for sh_execve.
 *
 * This is called before and after CreateProcess. On the first call it
 * will mark the non-close-on-exec handles as inheritable and produce
 * the startup info for the CRT. On the second call, after CreateProcess,
 * it will restore the handle inheritability properties.
 *
 * @returns Pointer to CRT data if prepare is 1, NULL if prepare is 0.
 * @param   pfdtab  The file descriptor table.
 * @param   prepare Which call, 1 if before and 0 if after.
 * @param   sizep   Where to store the size of the returned data.
 * @param   hndls   Where to store the three standard handles.
 */
void *shfile_exec_win(shfdtab *pfdtab, int prepare, unsigned short *sizep, intptr_t *hndls)
{
    void       *pvRet;
    shmtxtmp    tmp;
    unsigned    count;
    unsigned    i;

    shmtx_enter(&pfdtab->mtx, &tmp);
    TRACE2((NULL, "shfile_exec_win: prepare=%p\n", prepare));

    count  = pfdtab->size < (0x10000-4) / (1 + sizeof(HANDLE))
           ? pfdtab->size
           : (0x10000-4) / (1 + sizeof(HANDLE));
    while (count > 3 && pfdtab->tab[count - 1].fd == -1)
        count--;

    if (prepare)
    {
        size_t      cbData = sizeof(int) + count * (1 + sizeof(HANDLE));
        uint8_t    *pbData = sh_malloc(shthread_get_shell(), cbData);
        uint8_t    *paf = pbData + sizeof(int);
        HANDLE     *pah = (HANDLE *)(paf + count);

        *(int *)pbData = count;

        i = count;
        while (i-- > 0)
        {
            if (    pfdtab->tab[i].fd == i
                &&  !(pfdtab->tab[i].shflags & SHFILE_FLAGS_CLOSE_ON_EXEC))
            {
                HANDLE hFile = (HANDLE)pfdtab->tab[i].native;
                TRACE2((NULL, "  #%d: native=%#x oflags=%#x shflags=%#x\n",
                        i, hFile, pfdtab->tab[i].oflags, pfdtab->tab[i].shflags));

                if (!SetHandleInformation(hFile, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT))
                {
                    DWORD err = GetLastError();
                    assert(0);
                }
                paf[i] = FOPEN;
                if (pfdtab->tab[i].oflags & _O_APPEND)
                    paf[i] |= FAPPEND;
                if (pfdtab->tab[i].oflags & _O_TEXT)
                    paf[i] |= FTEXT;
                switch (pfdtab->tab[i].shflags & SHFILE_FLAGS_TYPE_MASK)
                {
                    case SHFILE_FLAGS_TTY:  paf[i] |= FDEV; break;
                    case SHFILE_FLAGS_PIPE: paf[i] |= FPIPE; break;
                }
                pah[i] = hFile;
            }
            else
            {
                paf[i] = 0;
                pah[i] = INVALID_HANDLE_VALUE;
            }
        }

        for (i = 0; i < 3; i++)
        {
            if (    i < count
                &&  pfdtab->tab[i].fd == i)
                hndls[i] = pfdtab->tab[i].native;
            else
                hndls[i] = (intptr_t)INVALID_HANDLE_VALUE;
            TRACE2((NULL, "shfile_exec_win: i=%d count=%d fd=%d native=%d hndls[%d]=\n",
                    i, count, pfdtab->tab[i].fd, pfdtab->tab[i].native, i, hndls[i]));
        }

        *sizep = (unsigned short)cbData;
        pvRet = pbData;
    }
    else
    {
        assert(!hndls);
        assert(!sizep);
        i = count;
        while (i-- > 0)
        {
            if (    pfdtab->tab[i].fd == i
                &&  !(pfdtab->tab[i].shflags & SHFILE_FLAGS_CLOSE_ON_EXEC))
            {
                HANDLE hFile = (HANDLE)pfdtab->tab[i].native;
                if (!SetHandleInformation(hFile, HANDLE_FLAG_INHERIT, 0))
                {
                    DWORD err = GetLastError();
                    assert(0);
                }
            }
        }
        pvRet = NULL;
    }

    shmtx_leave(&pfdtab->mtx, &tmp);
    return pvRet;
}

#endif /* K_OS_WINDOWS */

#if K_OS != K_OS_WINDOWS
/**
 * Prepare file handles for inherting before a execve call.
 *
 * This is only used in the normal mode, so we've forked and need not worry
 * about cleaning anything up after us.  Nor do we need think about locking.
 *
 * @returns 0 on success, -1 on failure.
 */
int shfile_exec_unix(shfdtab *pfdtab)
{
    int rc = 0;
# ifdef SHFILE_IN_USE
    unsigned fd;

    for (fd = 0; fd < pfdtab->size; fd++)
    {
        if (   pfdtab->tab[fd].fd != -1
            && !(pfdtab->tab[fd].shflags & SHFILE_FLAGS_CLOSE_ON_EXEC) )
        {
            TRACE2((NULL, "shfile_exec_unix: %d => %d\n", pfdtab->tab[fd].native, fd));
            if (dup2(pfdtab->tab[fd].native, fd) < 0)
            {
                /* fatal_error(NULL, "shfile_exec_unix: failed to move %d to %d", pfdtab->tab[fd].fd, fd); */
                rc = -1;
            }
        }
    }
# endif
    return rc;
}
#endif /* !K_OS_WINDOWS */

/**
 * open().
 */
int shfile_open(shfdtab *pfdtab, const char *name, unsigned flags, mode_t mode)
{
    int fd;
#ifdef SHFILE_IN_USE
    char    absname[SHFILE_MAX_PATH];
# if K_OS == K_OS_WINDOWS
    HANDLE  hFile;
    DWORD   dwDesiredAccess;
    DWORD   dwShareMode;
    DWORD   dwCreationDisposition;
    DWORD   dwFlagsAndAttributes;
    SECURITY_ATTRIBUTES SecurityAttributes;

# ifndef _O_ACCMODE
#  define _O_ACCMODE	(_O_RDONLY|_O_WRONLY|_O_RDWR)
# endif
    switch (flags & (_O_ACCMODE | _O_APPEND))
    {
        case _O_RDONLY:             dwDesiredAccess = GENERIC_READ; break;
        case _O_RDONLY | _O_APPEND: dwDesiredAccess = GENERIC_READ; break;
        case _O_WRONLY:             dwDesiredAccess = GENERIC_WRITE; break;
        case _O_WRONLY | _O_APPEND: dwDesiredAccess = (FILE_GENERIC_WRITE & ~FILE_WRITE_DATA); break;
        case _O_RDWR:               dwDesiredAccess = GENERIC_READ | GENERIC_WRITE; break;
        case _O_RDWR   | _O_APPEND: dwDesiredAccess = GENERIC_READ | (FILE_GENERIC_WRITE & ~FILE_WRITE_DATA); break;

        default:
            RETURN_ERROR(-1, EINVAL, "invalid mode");
    }

    dwShareMode = FILE_SHARE_DELETE | FILE_SHARE_READ | FILE_SHARE_WRITE;

    SecurityAttributes.nLength = sizeof(SecurityAttributes);
    SecurityAttributes.lpSecurityDescriptor = NULL;
    SecurityAttributes.bInheritHandle = FALSE;

    if (flags & _O_CREAT)
    {
        if ((flags & (_O_EXCL | _O_TRUNC)) == (_O_EXCL | _O_TRUNC))
            RETURN_ERROR(-1, EINVAL, "_O_EXCL | _O_TRUNC");

        if (flags & _O_TRUNC)
            dwCreationDisposition = CREATE_ALWAYS; /* not 100%, but close enough */
        else if (flags & _O_EXCL)
            dwCreationDisposition = CREATE_NEW;
        else
            dwCreationDisposition = OPEN_ALWAYS;
    }
    else if (flags & _O_TRUNC)
        dwCreationDisposition = TRUNCATE_EXISTING;
    else
        dwCreationDisposition = OPEN_EXISTING;

    if (!(flags & _O_CREAT) || (mode & 0222))
        dwFlagsAndAttributes = FILE_ATTRIBUTE_NORMAL;
    else
        dwFlagsAndAttributes = FILE_ATTRIBUTE_READONLY;

    fd = shfile_make_path(pfdtab, name, &absname[0]);
    if (!fd)
    {
        SetLastError(0);
        hFile = CreateFileA(absname,
                            dwDesiredAccess,
                            dwShareMode,
                            &SecurityAttributes,
                            dwCreationDisposition,
                            dwFlagsAndAttributes,
                            NULL /* hTemplateFile */);
        if (hFile != INVALID_HANDLE_VALUE)
            fd = shfile_insert(pfdtab, (intptr_t)hFile, flags, 0, -1, "shfile_open");
        else
            fd = shfile_dos2errno(GetLastError());
    }

# else  /* K_OS != K_OS_WINDOWS */
    fd = shfile_make_path(pfdtab, name, &absname[0]);
    if (!fd)
    {
        fd = open(absname, flags, mode);
        if (fd != -1)
            fd = shfile_copy_insert_and_close(pfdtab, &fd, flags, 0, -1, "shfile_open");
    }

# endif /* K_OS != K_OS_WINDOWS */

#else
    fd = open(name, flags, mode);
#endif

    TRACE2((NULL, "shfile_open(%p:{%s}, %#x, 0%o) -> %d [%d]\n", name, name, flags, mode, fd, errno));
    return fd;
}

int shfile_pipe(shfdtab *pfdtab, int fds[2])
{
    int rc = -1;
#ifdef SHFILE_IN_USE
# if K_OS == K_OS_WINDOWS
    HANDLE hRead  = INVALID_HANDLE_VALUE;
    HANDLE hWrite = INVALID_HANDLE_VALUE;
    SECURITY_ATTRIBUTES SecurityAttributes;

    SecurityAttributes.nLength = sizeof(SecurityAttributes);
    SecurityAttributes.lpSecurityDescriptor = NULL;
    SecurityAttributes.bInheritHandle = FALSE;

    fds[1] = fds[0] = -1;
    if (CreatePipe(&hRead, &hWrite, &SecurityAttributes, 4096))
    {
        fds[0] = shfile_insert(pfdtab, (intptr_t)hRead, O_RDONLY, SHFILE_FLAGS_PIPE, -1, "shfile_pipe");
        if (fds[0] != -1)
        {
            fds[1] = shfile_insert(pfdtab, (intptr_t)hWrite, O_WRONLY, SHFILE_FLAGS_PIPE, -1, "shfile_pipe");
            if (fds[1] != -1)
                rc = 0;
        }

# else
    int native_fds[2];

    fds[1] = fds[0] = -1;
    if (!pipe(native_fds))
    {
        fds[0] = shfile_copy_insert_and_close(pfdtab, &native_fds[0], O_RDONLY, SHFILE_FLAGS_PIPE, -1, "shfile_pipe");
        if (fds[0] != -1)
        {
            fds[1] = shfile_copy_insert_and_close(pfdtab, &native_fds[1], O_WRONLY, SHFILE_FLAGS_PIPE, -1, "shfile_pipe");
            if (fds[1] != -1)
                rc = 0;
        }
# endif
        if (fds[1] == -1)
        {
            int s = errno;
            if (fds[0] != -1)
            {
                shmtxtmp tmp;
                shmtx_enter(&pfdtab->mtx, &tmp);
                rc = fds[0];
                pfdtab->tab[rc].fd = -1;
                pfdtab->tab[rc].oflags = 0;
                pfdtab->tab[rc].shflags = 0;
                pfdtab->tab[rc].native = -1;
                shmtx_leave(&pfdtab->mtx, &tmp);
            }

# if K_OS == K_OS_WINDOWS
            CloseHandle(hRead);
            CloseHandle(hWrite);
# else
            close(native_fds[0]);
            close(native_fds[1]);
# endif
            fds[0] = fds[1] = -1;
            errno = s;
            rc = -1;
        }
    }
    else
    {
# if K_OS == K_OS_WINDOWS
        errno = shfile_dos2errno(GetLastError());
# endif
        rc = -1;
    }

#else
    rc = pipe(fds);
#endif

    TRACE2((NULL, "shfile_pipe() -> %d{%d,%d} [%d]\n", rc, fds[0], fds[1], errno));
    return rc;
}

/**
 * dup().
 */
int shfile_dup(shfdtab *pfdtab, int fd)
{
    return shfile_fcntl(pfdtab,fd, F_DUPFD, 0);
}

/**
 * Move the file descriptor, closing any existing descriptor at @a fdto.
 *
 * @returns fdto on success, -1 and errno on failure.
 * @param   pfdtab          The file descriptor table.
 * @param   fdfrom          The descriptor to move.
 * @param   fdto            Where to move it.
 */
int shfile_movefd(shfdtab *pfdtab, int fdfrom, int fdto)
{
#ifdef SHFILE_IN_USE
    int         rc;
    shmtxtmp    tmp;
    shfile     *file = shfile_get(pfdtab, fdfrom, &tmp);
    if (file)
    {
        /* prepare the new entry */
        if (fdto >= (int)pfdtab->size)
            shfile_grow_tab_locked(pfdtab, fdto);
        if (fdto < (int)pfdtab->size)
        {
            if (pfdtab->tab[fdto].fd != -1)
                shfile_native_close(pfdtab->tab[fdto].native, pfdtab->tab[fdto].oflags);

            /* setup the target. */
            pfdtab->tab[fdto].fd      = fdto;
            pfdtab->tab[fdto].oflags  = file->oflags;
            pfdtab->tab[fdto].shflags = file->shflags;
            pfdtab->tab[fdto].native  = file->native;

            /* close the source. */
            file->fd        = -1;
            file->oflags    = 0;
            file->shflags   = 0;
            file->native    = -1;

            rc = fdto;
        }
        else
        {
            errno = EMFILE;
            rc = -1;
        }

        shfile_put(pfdtab, file, &tmp);
    }
    else
        rc = -1;
    return rc;

#else
    return dup2(fdfrom, fdto);
#endif
}

/**
 * Move the file descriptor to somewhere at @a fdMin or above.
 *
 * @returns the new file descriptor success, -1 and errno on failure.
 * @param   pfdtab          The file descriptor table.
 * @param   fdfrom          The descriptor to move.
 * @param   fdMin           The minimum descriptor.
 */
int shfile_movefd_above(shfdtab *pfdtab, int fdfrom, int fdMin)
{
#ifdef SHFILE_IN_USE
    int         fdto;
    shmtxtmp    tmp;
    shfile     *file = shfile_get(pfdtab, fdfrom, &tmp);
    if (file)
    {
        /* find a new place */
        int i;
        fdto = -1;
        for (i = fdMin; (unsigned)i < pfdtab->size; i++)
            if (pfdtab->tab[i].fd == -1)
            {
                fdto = i;
                break;
            }
        if (fdto == -1)
            fdto = shfile_grow_tab_locked(pfdtab, fdMin);
        if (fdto != -1)
        {
            /* setup the target. */
            pfdtab->tab[fdto].fd      = fdto;
            pfdtab->tab[fdto].oflags  = file->oflags;
            pfdtab->tab[fdto].shflags = file->shflags;
            pfdtab->tab[fdto].native  = file->native;

            /* close the source. */
            file->fd        = -1;
            file->oflags    = 0;
            file->shflags   = 0;
            file->native    = -1;
        }
        else
        {
            errno = EMFILE;
            fdto = -1;
        }

        shfile_put(pfdtab, file, &tmp);
    }
    else
        fdto = -1;
    return fdto;

#else
    int fdnew = fcntl(fdfrom, F_DUPFD, fdMin);
    if (fdnew >= 0)
        close(fdfrom);
    return fdnew;
#endif
}

/**
 * close().
 */
int shfile_close(shfdtab *pfdtab, unsigned fd)
{
    int         rc;
#ifdef SHFILE_IN_USE
    shmtxtmp    tmp;
    shfile     *file = shfile_get(pfdtab, fd, &tmp);
    if (file)
    {
        shfile_native_close(file->native, file->oflags);

        file->fd = -1;
        file->oflags = 0;
        file->shflags = 0;
        file->native = -1;

        shfile_put(pfdtab, file, &tmp);
        rc = 0;
    }
    else
        rc = -1;

#else
    rc = close(fd);
#endif

    TRACE2((NULL, "shfile_close(%d) -> %d [%d]\n", fd, rc, errno));
    return rc;
}

/**
 * read().
 */
long shfile_read(shfdtab *pfdtab, int fd, void *buf, size_t len)
{
    long        rc;
#ifdef SHFILE_IN_USE
    shmtxtmp    tmp;
    shfile     *file = shfile_get(pfdtab, fd, &tmp);
    if (file)
    {
# if K_OS == K_OS_WINDOWS
        DWORD dwRead = 0;
        if (ReadFile((HANDLE)file->native, buf, (DWORD)len, &dwRead, NULL))
            rc = dwRead;
        else
            rc = shfile_dos2errno(GetLastError());
# else
        rc = read(file->native, buf, len);
# endif

        shfile_put(pfdtab, file, &tmp);
    }
    else
        rc = -1;

#else
    rc = read(fd, buf, len);
#endif
    return rc;
}

/**
 * write().
 */
long shfile_write(shfdtab *pfdtab, int fd, const void *buf, size_t len)
{
    long        rc;
#ifdef SHFILE_IN_USE
    shmtxtmp    tmp;
    shfile     *file = shfile_get(pfdtab, fd, &tmp);
    if (file)
    {
# if K_OS == K_OS_WINDOWS
        DWORD dwWritten = 0;
        if (WriteFile((HANDLE)file->native, buf, (DWORD)len, &dwWritten, NULL))
            rc = dwWritten;
        else
            rc = shfile_dos2errno(GetLastError());
# else
        rc = write(file->native, buf, len);
# endif

        shfile_put(pfdtab, file, &tmp);
    }
    else
        rc = -1;

#else
    if (fd != shthread_get_shell()->tracefd)
    {
        struct stat s;
        int x;
        x = fstat(fd, &s);
        TRACE2((NULL, "shfile_write(%d) - %lu bytes (%d) - pos %lu - before; %o\n",
                fd, (long)s.st_size, x, (long)lseek(fd, 0, SEEK_CUR), s.st_mode ));
        errno = 0;
    }

    rc = write(fd, buf, len);
#endif

#ifdef DEBUG
    if (fd != shthread_get_shell()->tracefd)
    {
        struct stat s;
        int x;
        TRACE2((NULL, "shfile_write(%d,,%d) -> %d [%d]\n", fd, len, rc, errno));
        x=fstat(fd, &s);
        TRACE2((NULL, "shfile_write(%d) - %lu bytes (%d) - pos %lu - after\n", fd, (long)s.st_size, x, (long)lseek(fd, 0, SEEK_CUR) ));
    }
#endif
    return rc;
}

/**
 * lseek().
 */
long shfile_lseek(shfdtab *pfdtab, int fd, long off, int whench)
{
    long        rc;
#ifdef SHFILE_IN_USE
    shmtxtmp    tmp;
    shfile     *file = shfile_get(pfdtab, fd, &tmp);
    if (file)
    {
# if K_OS == K_OS_WINDOWS
        assert(SEEK_SET == FILE_BEGIN);
        assert(SEEK_CUR == FILE_CURRENT);
        assert(SEEK_END == FILE_END);
        rc = SetFilePointer((HANDLE)file->native, off, NULL, whench);
        if (rc == INVALID_SET_FILE_POINTER)
            rc = shfile_dos2errno(GetLastError());
# else
        rc = lseek(file->native, off, whench);
# endif

        shfile_put(pfdtab, file, &tmp);
    }
    else
        rc = -1;

#else
    rc = lseek(fd, off, whench);
#endif

    return rc;
}

int shfile_fcntl(shfdtab *pfdtab, int fd, int cmd, int arg)
{
    int rc;
#ifdef SHFILE_IN_USE
    shmtxtmp    tmp;
    shfile     *file = shfile_get(pfdtab, fd, &tmp);
    if (file)
    {
        switch (cmd)
        {
            case F_GETFL:
                rc = file->oflags;
                break;

            case F_SETFL:
            {
                unsigned mask = O_NONBLOCK | O_APPEND | O_BINARY | O_TEXT;
# ifdef O_DIRECT
                mask |= O_DIRECT;
# endif
# ifdef O_ASYNC
                mask |= O_ASYNC;
# endif
# ifdef O_SYNC
                mask |= O_SYNC;
# endif
                if ((file->oflags & mask) == (arg & mask))
                    rc = 0;
                else
                {
# if K_OS == K_OS_WINDOWS
                    assert(0);
                    errno = EINVAL;
                    rc = -1;
# else
                    rc = fcntl(file->native, F_SETFL, arg);
                    if (rc != -1)
                        file->oflags = (file->oflags & ~mask) | (arg & mask);
# endif
                }
                break;
            }

            case F_DUPFD:
            {
# if K_OS == K_OS_WINDOWS
                HANDLE hNew = INVALID_HANDLE_VALUE;
                if (DuplicateHandle(GetCurrentProcess(),
                                    (HANDLE)file->native,
                                    GetCurrentProcess(),
                                    &hNew,
                                    0,
                                    FALSE /* bInheritHandle */,
                                    DUPLICATE_SAME_ACCESS))
                    rc = shfile_insert(pfdtab, (intptr_t)hNew, file->oflags, file->shflags, arg, "shfile_fcntl");
                else
                    rc = shfile_dos2errno(GetLastError());
# else
                int nativeNew = fcntl(file->native, F_DUPFD, SHFILE_UNIX_MIN_FD);
                if (nativeNew != -1)
                    rc = shfile_insert(pfdtab, nativeNew, file->oflags, file->shflags, arg, "shfile_fcntl");
                else
                    rc = -1;
# endif
                break;
            }

            default:
                errno = -EINVAL;
                rc = -1;
                break;
        }

        shfile_put(pfdtab, file, &tmp);
    }
    else
        rc = -1;

#else
    rc = fcntl(fd, cmd, arg);
#endif

    switch (cmd)
    {
        case F_GETFL: TRACE2((NULL, "shfile_fcntl(%d,F_GETFL,ignored=%d) -> %d [%d]\n", fd, arg, rc, errno));  break;
        case F_SETFL: TRACE2((NULL, "shfile_fcntl(%d,F_SETFL,newflags=%#x) -> %d [%d]\n", fd, arg, rc, errno)); break;
        case F_DUPFD: TRACE2((NULL, "shfile_fcntl(%d,F_DUPFS,minfd=%d) -> %d [%d]\n", fd, arg, rc, errno)); break;
        default:  TRACE2((NULL, "shfile_fcntl(%d,%d,%d) -> %d [%d]\n", fd, cmd, arg, rc, errno)); break;
    }
    return rc;
}

int shfile_stat(shfdtab *pfdtab, const char *path, struct stat *pst)
{
#ifdef SHFILE_IN_USE
    char    abspath[SHFILE_MAX_PATH];
    int     rc;
    rc = shfile_make_path(pfdtab, path, &abspath[0]);
    if (!rc)
    {
# if K_OS == K_OS_WINDOWS
        rc = stat(abspath, pst); /** @todo re-implement stat. */
# else
        rc = stat(abspath, pst);
# endif
    }
    TRACE2((NULL, "shfile_stat(,%s,) -> %d [%d]\n", path, rc, errno));
    return rc;
#else
    return stat(path, pst);
#endif
}

int shfile_lstat(shfdtab *pfdtab, const char *path, struct stat *pst)
{
    int     rc;
#ifdef SHFILE_IN_USE
    char    abspath[SHFILE_MAX_PATH];

    rc = shfile_make_path(pfdtab, path, &abspath[0]);
    if (!rc)
    {
# if K_OS == K_OS_WINDOWS
        rc = stat(abspath, pst); /** @todo implement lstat. */
# else
        rc = lstat(abspath, pst);
# endif
    }
#else
    rc = stat(path, pst);
#endif
    TRACE2((NULL, "shfile_stat(,%s,) -> %d [%d]\n", path, rc, errno));
    return rc;
}

/**
 * chdir().
 */
int shfile_chdir(shfdtab *pfdtab, const char *path)
{
    int         rc;
#ifdef SHFILE_IN_USE
    shinstance *psh = shthread_get_shell();
    char        abspath[SHFILE_MAX_PATH];

    rc = shfile_make_path(pfdtab, path, &abspath[0]);
    if (!rc)
    {
        char *abspath_copy = sh_strdup(psh, abspath);
        char *free_me = abspath_copy;
        rc = chdir(path);
        if (!rc)
        {
            shmtxtmp    tmp;
            shmtx_enter(&pfdtab->mtx, &tmp);

            free_me = pfdtab->cwd;
            pfdtab->cwd = abspath_copy;

            shmtx_leave(&pfdtab->mtx, &tmp);
        }
        sh_free(psh, free_me);
    }
    else
        rc = -1;
#else
    rc = chdir(path);
#endif

    TRACE2((NULL, "shfile_chdir(,%s) -> %d [%d]\n", path, rc, errno));
    return rc;
}

/**
 * getcwd().
 */
char *shfile_getcwd(shfdtab *pfdtab, char *buf, int size)
{
    char       *ret;
#ifdef SHFILE_IN_USE

    ret = NULL;
    if (buf && !size)
        errno = -EINVAL;
    else
    {
        size_t      cwd_size;
        shmtxtmp    tmp;
        shmtx_enter(&pfdtab->mtx, &tmp);

        cwd_size = strlen(pfdtab->cwd) + 1;
        if (buf)
        {
            if (cwd_size <= (size_t)size)
                ret = memcpy(buf, pfdtab->cwd, cwd_size);
            else
                errno = ERANGE;
        }
        else
        {
            if ((size_t)size < cwd_size)
                size = (int)cwd_size;
            ret = sh_malloc(shthread_get_shell(), size);
            if (ret)
                ret = memcpy(ret, pfdtab->cwd, cwd_size);
            else
                errno = ENOMEM;
        }

        shmtx_leave(&pfdtab->mtx, &tmp);
    }
#else
    ret = getcwd(buf, size);
#endif

    TRACE2((NULL, "shfile_getcwd(,%p,%d) -> %s [%d]\n", buf, size, ret, errno));
    return ret;
}

/**
 * access().
 */
int shfile_access(shfdtab *pfdtab, const char *path, int type)
{
    int         rc;
#ifdef SHFILE_IN_USE
    char        abspath[SHFILE_MAX_PATH];

    rc = shfile_make_path(pfdtab, path, &abspath[0]);
    if (!rc)
    {
# ifdef _MSC_VER
        if (type & X_OK)
            type = (type & ~X_OK) | R_OK;
# endif
        rc = access(abspath, type);
    }
#else
# ifdef _MSC_VER
    if (type & X_OK)
        type = (type & ~X_OK) | R_OK;
# endif
    rc = access(path, type);
#endif

    TRACE2((NULL, "shfile_access(,%s,%#x) -> %d [%d]\n", path, type, rc, errno));
    return rc;
}

/**
 * isatty()
 */
int shfile_isatty(shfdtab *pfdtab, int fd)
{
    int         rc;
#ifdef SHFILE_IN_USE
    shmtxtmp    tmp;
    shfile     *file = shfile_get(pfdtab, fd, &tmp);
    if (file)
    {
# if K_OS == K_OS_WINDOWS
        rc = (file->shflags & SHFILE_FLAGS_TYPE_MASK) == SHFILE_FLAGS_TTY;
# else
        rc = isatty(file->native);
# endif
        shfile_put(pfdtab, file, &tmp);
    }
    else
        rc = 0;
#else
    rc = isatty(fd);
#endif

    TRACE2((NULL, "isatty(%d) -> %d [%d]\n", fd, rc, errno));
    return rc;
}

/**
 * fcntl F_SETFD / FD_CLOEXEC.
 */
int shfile_cloexec(shfdtab *pfdtab, int fd, int closeit)
{
    int         rc;
#ifdef SHFILE_IN_USE
    shmtxtmp    tmp;
    shfile     *file = shfile_get(pfdtab, fd, &tmp);
    if (file)
    {
        if (closeit)
            file->shflags |= SHFILE_FLAGS_CLOSE_ON_EXEC;
        else
            file->shflags &= ~SHFILE_FLAGS_CLOSE_ON_EXEC;
        shfile_put(pfdtab, file, &tmp);
        rc = 0;
    }
    else
        rc = -1;
#else
    rc = fcntl(fd, F_SETFD, fcntl(fd, F_GETFD, 0)
                          | (closeit ? FD_CLOEXEC : 0));
#endif

    TRACE2((NULL, "shfile_cloexec(%d, %d) -> %d [%d]\n", fd, closeit, rc, errno));
    return rc;
}


int shfile_ioctl(shfdtab *pfdtab, int fd, unsigned long request, void *buf)
{
    int         rc;
#ifdef SHFILE_IN_USE
    shmtxtmp    tmp;
    shfile     *file = shfile_get(pfdtab, fd, &tmp);
    if (file)
    {
# if K_OS == K_OS_WINDOWS
        rc = -1;
        errno = ENOSYS;
# else
        rc = ioctl(file->native, request, buf);
# endif
        shfile_put(pfdtab, file, &tmp);
    }
    else
        rc = -1;
#else
    rc = ioctl(fd, request, buf);
#endif

    TRACE2((NULL, "ioctl(%d, %#x, %p) -> %d\n", fd, request, buf, rc));
    return rc;
}


mode_t shfile_get_umask(shfdtab *pfdtab)
{
    /** @todo */
    return 022;
}

void shfile_set_umask(shfdtab *pfdtab, mode_t mask)
{
    /** @todo */
    (void)mask;
}



shdir *shfile_opendir(shfdtab *pfdtab, const char *dir)
{
#if defined(SHFILE_IN_USE) && K_OS == K_OS_WINDOWS
    shdir  *pdir = NULL;

    if (g_pfnNtQueryDirectoryFile)
    {
        char abspath[SHFILE_MAX_PATH];
        if (shfile_make_path(pfdtab, dir, &abspath[0]) == 0)
        {
            HANDLE              hFile;
            SECURITY_ATTRIBUTES SecurityAttributes;

            SecurityAttributes.nLength = sizeof(SecurityAttributes);
            SecurityAttributes.lpSecurityDescriptor = NULL;
            SecurityAttributes.bInheritHandle = FALSE;

            hFile = CreateFileA(abspath,
                                GENERIC_READ,
                                FILE_SHARE_DELETE | FILE_SHARE_READ | FILE_SHARE_WRITE,
                                &SecurityAttributes,
                                OPEN_EXISTING,
                                FILE_ATTRIBUTE_DIRECTORY | FILE_FLAG_BACKUP_SEMANTICS,
                                NULL /* hTemplateFile */);
            if (hFile != INVALID_HANDLE_VALUE)
            {
                pdir = (shdir *)sh_malloc(shthread_get_shell(), sizeof(*pdir));
                if (pdir)
                {
                    pdir->pfdtab = pfdtab;
                    pdir->native = hFile;
                    pdir->off    = ~(size_t)0;
                }
                else
                    CloseHandle(hFile);
            }
            else
                shfile_dos2errno(GetLastError());
        }
    }
    else
        errno = ENOSYS;
    return pdir;
#else
    return (shdir *)opendir(dir);
#endif
}

shdirent *shfile_readdir(struct shdir *pdir)
{
#if defined(SHFILE_IN_USE) && K_OS == K_OS_WINDOWS
    if (pdir)
    {
        NTSTATUS rcNt;

        if (   pdir->off == ~(size_t)0
            || pdir->off + sizeof(MY_FILE_NAMES_INFORMATION) >= pdir->cb)
        {
            MY_IO_STATUS_BLOCK Ios;

            memset(&Ios, 0, sizeof(Ios));
            rcNt = g_pfnNtQueryDirectoryFile(pdir->native,
                                             NULL /*Event*/,
                                             NULL /*ApcRoutine*/,
                                             NULL /*ApcContext*/,
                                             &Ios,
                                             &pdir->buf[0],
                                             sizeof(pdir->buf),
                                             MY_FileNamesInformation,
                                             FALSE /*ReturnSingleEntry*/,
                                             NULL /*FileName*/,
                                             pdir->off == ~(size_t)0 /*RestartScan*/);
            if (rcNt >= 0 && rcNt != STATUS_PENDING)
            {
                pdir->cb  = Ios.Information;
                pdir->off = 0;
            }
            else if (rcNt == STATUS_NO_MORE_FILES)
                errno = 0; /* wrong? */
            else
                shfile_nt2errno(rcNt);
        }

        if (   pdir->off != ~(size_t)0
            && pdir->off + sizeof(MY_FILE_NAMES_INFORMATION) <= pdir->cb)
        {
            PMY_FILE_NAMES_INFORMATION  pcur = (PMY_FILE_NAMES_INFORMATION)&pdir->buf[pdir->off];
            ANSI_STRING                 astr;
            UNICODE_STRING              ustr;

            astr.Length = astr.MaximumLength = sizeof(pdir->ent.name);
            astr.Buffer = &pdir->ent.name[0];

            ustr.Length = ustr.MaximumLength = pcur->FileNameLength < ~(USHORT)0 ? (USHORT)pcur->FileNameLength : ~(USHORT)0;
            ustr.Buffer = &pcur->FileName[0];

            rcNt = g_pfnRtlUnicodeStringToAnsiString(&astr, &ustr, 0/*AllocateDestinationString*/);
            if (rcNt < 0)
                sprintf(pdir->ent.name, "conversion-failed-%08x-rcNt=%08x-len=%u",
                        pcur->FileIndex, rcNt, pcur->FileNameLength);
            if (pcur->NextEntryOffset)
                pdir->off += pcur->NextEntryOffset;
            else
                pdir->off = pdir->cb;
            return &pdir->ent;
        }
    }
    else
        errno = EINVAL;
    return NULL;
#else
    struct dirent *pde = readdir((DIR *)pdir);
    return pde ? (shdirent *)&pde->d_name[0] : NULL;
#endif
}

void shfile_closedir(struct shdir *pdir)
{
#if defined(SHFILE_IN_USE) && K_OS == K_OS_WINDOWS
    if (pdir)
    {
        CloseHandle(pdir->native);
        pdir->pfdtab = NULL;
        pdir->native = INVALID_HANDLE_VALUE;
        sh_free(shthread_get_shell(), pdir);
    }
    else
        errno = EINVAL;
#else
    closedir((DIR *)pdir);
#endif
}

