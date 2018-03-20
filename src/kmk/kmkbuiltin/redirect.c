/* $Id: redirect.c 3164 2018-03-20 03:31:22Z knut.osmundsen@oracle.com $ */
/** @file
 * kmk_redirect - Do simple program <-> file redirection (++).
 */

/*
 * Copyright (c) 2007-2016 knut st. osmundsen <bird-kBuild-spamx@anduin.net>
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

/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
#if defined(__APPLE__)
/*# define _POSIX_C_SOURCE 1  / *  10.4 sdk and unsetenv  * / - breaks O_CLOEXEC on 10.8 */
#endif
#include "makeint.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#if defined(KBUILD_OS_WINDOWS) || defined(KBUILD_OS_OS2)
# include <process.h>
#endif
#if defined(KBUILD_OS_WINDOWS) || defined(KBUILD_OS_OS2)
# include <Windows.h>
# include <Winternl.h>
#endif
#if defined(_MSC_VER)
# include <ctype.h>
# include <io.h>
# include "quote_argv.h"
#else
# ifdef __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__
#  if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ >= 1050
#   define USE_POSIX_SPAWN
#  endif
# elif !defined(KBUILD_OS_WINDOWS) && !defined(KBUILD_OS_OS2)
#  define USE_POSIX_SPAWN
# endif
# include <unistd.h>
# ifdef USE_POSIX_SPAWN
#  include <spawn.h>
# endif
# include <sys/wait.h>
#endif

#include <k/kDefs.h>
#include <k/kTypes.h>
#include "err.h"
#include "kbuild_version.h"
#if defined(__gnu_hurd__) && !defined(kmk_builtin_redirect) /* need constant */
# undef GET_PATH_MAX
# undef PATH_MAX
# define GET_PATH_MAX PATH_MAX
#endif
#include "kmkbuiltin.h"
#ifdef KMK
# include "job.h"
# include "variable.h"
# ifdef KBUILD_OS_WINDOWS
#  ifndef CONFIG_NEW_WIN_CHILDREN
#   include "sub_proc.h"
#  else
#   include "../w32/winchildren.h"
#  endif
#  include "pathstuff.h"
# endif
#endif

#ifdef __OS2__
# define INCL_BASE
# include <os2.h>
# ifndef LIBPATHSTRICT
#  define LIBPATHSTRICT 3
# endif
#endif


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/* String + strlen tuple. */
#define TUPLE(a_sz)     a_sz, sizeof(a_sz) - 1

/** Only standard handles on windows. */
#ifdef KBUILD_OS_WINDOWS
# define ONLY_TARGET_STANDARD_HANDLES
#endif


static const char *name(const char *pszName)
{
    const char *psz = strrchr(pszName, '/');
#if defined(_MSC_VER) || defined(__OS2__)
    const char *psz2 = strrchr(pszName, '\\');
    if (!psz2)
        psz2 = strrchr(pszName, ':');
    if (psz2 && (!psz || psz2 > psz))
        psz = psz2;
#endif
    return psz ? psz + 1 : pszName;
}


static int usage(FILE *pOut,  const char *argv0)
{
    argv0 = name(argv0);
    fprintf(pOut,
            "usage: %s [-[rwa+tb]<fd> <file>] [-d<fd>=<src-fd>] [-c<fd>]\n"
            "           [-Z] [-E <var=val>] [-C <dir>] [--wcc-brain-damage]\n"
            "           [-v] -- <program> [args]\n"
            "   or: %s --help\n"
            "   or: %s --version\n"
            "\n"
            "The rwa+tb is like for fopen, if not specified it defaults to w+.\n"
            "The <fd> is either a number or an alias for the standard handles:\n"
            "   i = stdin\n"
            "   o = stdout\n"
            "   e = stderr\n"
            "\n"
            "The -d switch duplicate the right hand file descriptor (src-fd) to the left\n"
            "hand side one (fd). The latter is limited to standard handles on windows.\n"
            "\n"
            "The -c switch will close the specified file descriptor. Limited to standard\n"
            "handles on windows.\n"
            "\n"
            "The -Z switch zaps the environment.\n"
            "\n"
            "The -E switch is for making changes to the environment in a putenv\n"
            "fashion.\n"
            "\n"
            "The -C switch is for changing the current directory.  Please specify an\n"
            "absolute program path as it's platform dependent whether this takes effect\n"
            "before or after the executable is located.\n"
            "\n"
            "The --wcc-brain-damage switch is to work around wcc and wcc386 (Open Watcom)\n"
            "not following normal quoting conventions on Windows, OS/2, and DOS.\n"
            "\n"
            "The -v switch is for making the thing more verbose.\n"
            "\n"
            "This command was originally just a quick hack to avoid invoking the shell\n"
            "on Windows (cygwin) where forking is very expensive and has exhibited\n"
            "stability issues on SMP machines.  It has since grown into something like\n"
            "/usr/bin/env on steroids.\n"
            ,
            argv0, argv0, argv0);
    return 2;
}


/**
 * Decoded file descriptor operations.
 */
typedef struct REDIRECTORDERS
{
    enum {
        kRedirectOrder_Invalid = 0,
        kRedirectOrder_Close,
        kRedirectOrder_Open,
        kRedirectOrder_Dup
    }           enmOrder;
    /** The target file handle. */
    int         fdTarget;
    /** The source file name, -1 on close only.
     * This is an opened file if pszFilename is set.  */
    int         fdSource;
    /** Whether to remove the file on failure cleanup. */
    int         fRemoveOnFailure;
    /** The open flags (for O_TEXT/O_BINARY) on windows. */
    int         fOpen;
    /** The filename - NULL if close only. */
    const char *pszFilename;
#ifndef USE_POSIX_SPAWN
    /** Saved file descriptor. */
    int         fdSaved;
    /** Saved flags. */
    int         fSaved;
#endif
} REDIRECTORDERS;


#ifdef _MSC_VER

/** Used by mscGetOsHandle. */
static void __cdecl ignore_invalid_parameter(const wchar_t *a, const wchar_t *b, const wchar_t *c, unsigned d, uintptr_t e)
{
}

/**
 * Safe way of getting the OS handle of a file descriptor without triggering
 * invalid parameter handling.
 *
 * @returns The handle value if open, INVALID_HANDLE_VALUE if not.
 * @param   fd                  The file descriptor in question.
 */
static HANDLE mscGetOsHandle(int fd)
{
    intptr_t                    hHandle;
    _invalid_parameter_handler  pfnOld = _get_invalid_parameter_handler();
    _set_invalid_parameter_handler(ignore_invalid_parameter);
    hHandle = _get_osfhandle(fd);
    _set_invalid_parameter_handler(pfnOld);
    return hHandle != -1 ? (HANDLE)hHandle : INVALID_HANDLE_VALUE;
}

/**
 * Checks if the specified file descriptor is open.
 *
 * @returns K_TRUE if open, K_FALSE if not.
 * @param   fd                  The file descriptor in question.
 */
static KBOOL mscIsOpenFile(int fd)
{
    return mscGetOsHandle(fd) != INVALID_HANDLE_VALUE;
}

/**
 * Checks if the native handle is inheritable.
 *
 * @returns K_TRUE if it is, K_FALSE if it isn't or isn't a valid handle.
 * @param   hHandle             The native handle.
 */
static KBOOL mscIsNativeHandleInheritable(HANDLE hHandle)
{
    DWORD fFlags = 0;
    if (GetHandleInformation(hHandle, &fFlags))
        return (fFlags & HANDLE_FLAG_INHERIT) != 0;
    return K_FALSE;
}

/**
 * Checks if the file descriptor is inheritable or not.
 *
 * @returns K_TRUE if it is, K_FALSE if it isn't or isn't a valid descriptor.
 * @param   fd                  The file descriptor in question.
 */
static KBOOL mscIsInheritable(int fd)
{
    HANDLE hHandle = mscGetOsHandle(fd);
    if (hHandle != INVALID_HANDLE_VALUE)
        return mscIsNativeHandleInheritable(hHandle);
    return K_FALSE;
}

/**
 * A dup3 like function.
 *
 * @returns fdNew on success, -1 on failure w/ error details written to pStdErr.
 * @param   fdSource            The source descriptor.
 * @param   fdNew               The new descriptor.
 * @param   fFlags              The inherit and text/binary mode flag.
 * @param   pStdErr             Working stderr to write error details to.
 */
static int mscDup3(int fdSource, int fdNew, int fFlags, FILE *pStdErr)
{
    if (!fFlags & _O_NOINHERIT)
    {
        /* ASSUMES fFlags doesn't include any changing _O_TEXT/_O_BINARY. */
        int fdDup = _dup2(fdSource, fdNew);
        if (fdDup != -1)
            return fdDup;
        fprintf(pStdErr, "%s: _dup2(%d,%d) failed: %s\n", g_progname, fdSource, fdNew, strerror(errno));
    }
    else
    {
        HANDLE   hSource = mscGetOsHandle(fdSource);
        unsigned cTries  = 0;
        int      aFdTries[48];

        if (hSource != INVALID_HANDLE_VALUE)
        {
            HANDLE hCurProc = GetCurrentProcess();
            BOOL   fInherit = !(fFlags & _O_NOINHERIT);

            /*
             * Make sure the old descriptor is closed and can be used again.
             */
            _invalid_parameter_handler pfnOld = _get_invalid_parameter_handler();
            _set_invalid_parameter_handler(ignore_invalid_parameter);
            close(fdNew);
            _set_invalid_parameter_handler(pfnOld);

            /*
             * Duplicate the source handle till we've got a match.
             */
            for (;;)
            {
                HANDLE hDup = INVALID_HANDLE_VALUE;
                if (DuplicateHandle(hCurProc, hSource, hCurProc, &hDup, 0 /* DesiredAccess */,
                                    fInherit, DUPLICATE_SAME_ACCESS))
                {
                    int fdDup = _open_osfhandle((intptr_t)hDup, fFlags);
                    if (fdDup != -1)
                    {
                        if (fdDup == fdNew)
                        {
                            while (cTries-- > 0)
                                close(aFdTries[cTries]);
                            return fdDup;
                        }

                        aFdTries[cTries++] = fdDup;
                        if (   fdDup < fdNew
                            && cTries < K_ELEMENTS(aFdTries))
                            continue;
                        fprintf(pStdErr, "%s: mscDup3(%d,%d): giving up! (last fdDup=%d)\n",
                                g_progname, fdSource, fdNew, fdDup);
                    }
                    else
                    {
                        fprintf(pStdErr, "%s: _open_osfhandle(%#x) failed: %u\n", g_progname, hDup, strerror(errno));
                        CloseHandle(hDup);
                    }
                }
                else
                    fprintf(pStdErr, "%s: DuplicateHandle(%#x) failed: %u\n", g_progname, hSource, GetLastError());
                break;
            }

            while (cTries-- > 0)
                close(aFdTries[cTries]);
        }
        else
            fprintf(pStdErr, "%s: mscDup3(%d,%d): source descriptor is invalid!\n", g_progname, fdSource, fdNew);
    }
    return -1;
}

#endif /* _MSC_VER */

static KBOOL kRedirectHasConflict(int fd, unsigned cOrders, REDIRECTORDERS *paOrders)
{
#ifdef ONLY_TARGET_STANDARD_HANDLES
    return fd < 3;
#else
    while (cOrders-- > 0)
        if (paOrders[cOrders].fdTarget == fd)
            return K_TRUE;
    return K_FALSE;
#endif
}


/**
 * Creates a file descriptor for @a pszFilename that does not conflict with any
 * previous orders.
 *
 * We need to be careful that there isn't a close or dup targetting the
 * temporary file descriptor we return.  Also, we need to take care with the
 * descriptor's inheritability.  It should only be inheritable if the returned
 * descriptor matches the target descriptor (@a fdTarget).
 *
 * @returns File descriptor on success, -1 & err/errx on failure.
 *
 *          The returned file descriptor is not inherited (i.e. close-on-exec),
 *          unless it matches @a fdTarget
 *
 * @param   pszFilename         The filename to open.
 * @param   fOpen               The open flags.
 * @param   fMode               The file creation mode (if applicable).
 * @param   cOrders             The number of orders.
 * @param   paOrders            The order array.
 * @param   fRemoveOnFailure    Whether to remove the file on failure.
 * @param   fdTarget            The target descriptor.
 */
static int kRedirectOpenWithoutConflict(const char *pszFilename, int fOpen, mode_t fMode,
                                        unsigned cOrders, REDIRECTORDERS *paOrders, int fRemoveOnFailure, int fdTarget)
{
#ifdef _O_NOINHERIT
    int const   fNoInherit = _O_NOINHERIT;
#elif defined(O_NOINHERIT)
    int const   fNoInherit = O_NOINHERIT;
#elif defined(O_CLOEXEC)
    int const   fNoInherit = O_CLOEXEC;
#else
    int const   fNoInherit = 0;
# define USE_FD_CLOEXEC
#endif
    int         aFdTries[32];
    unsigned    cTries;
    int         fdOpened;

#ifdef KBUILD_OS_WINDOWS
    if (strcmp(pszFilename, "/dev/null") == 0)
        pszFilename = "nul";
#endif

    /* Open it first. */
    fdOpened = open(pszFilename, fOpen | fNoInherit, fMode);
    if (fdOpened < 0)
        return err(-1, "open(%s,%#x,) failed", pszFilename, fOpen);

    /* Check for conflicts. */
    if (!kRedirectHasConflict(fdOpened, cOrders, paOrders))
    {
#ifndef KBUILD_OS_WINDOWS
        if (fdOpened != fdTarget)
            return fdOpened;
# ifndef USE_FD_CLOEXEC
        if (fcntl(fdOpened, F_SETFD, 0) != -1)
# endif
#endif
            return fdOpened;
    }

    /*
     * Do conflict resolving.
     */
    cTries = 1;
    aFdTries[cTries++] = fdOpened;
    while (cTries < K_ELEMENTS(aFdTries))
    {
        fdOpened = open(pszFilename, fOpen | fNoInherit, fMode);
        if (fdOpened >= 0)
        {
            if (!kRedirectHasConflict(fdOpened, cOrders, paOrders))
            {
#ifndef KBUILD_OS_WINDOWS
# ifdef USE_FD_CLOEXEC
                if (   fdOpened == fdTarget
                    || fcntl(fdOpened, F_SETFD, FD_CLOEXEC) != -1)
# else
                if (   fdOpened != fdTarget
                    || fcntl(fdOpened, F_SETFD, 0) != -1)
# endif
#endif
                {
                    while (cTries-- > 0)
                        close(aFdTries[cTries]);
                    return fdOpened;
                }
            }

        }
        else
        {
            err(-1, "open(%s,%#x,) #%u failed", pszFilename, cTries + 1, fOpen);
            break;
        }
        aFdTries[cTries++] = fdOpened;
    }

    /*
     * Give up.
     */
    if (fdOpened >= 0)
        errx(-1, "failed to find a conflict free file descriptor for '%s'!", pszFilename);

    while (cTries-- > 0)
        close(aFdTries[cTries]);
    return -1;
}


/**
 * Cleans up the file operation orders.
 *
 * This does not restore stuff, just closes handles we've opened for the child.
 *
 * @param   cOrders         Number of file operation orders.
 * @param   paOrders        The file operation orders.
 * @param   fFailed         Set if it's a failure.
 */
static void kRedirectCleanupFdOrders(unsigned cOrders, REDIRECTORDERS *paOrders, KBOOL fFailure)
{
    unsigned i = cOrders;
    while (i-- > 0)
    {
        if (   paOrders[i].enmOrder == kRedirectOrder_Open
            && paOrders[i].fdSource != -1)
        {
            close(paOrders[i].fdSource);
            paOrders[i].fdSource = -1;
            if (   fFailure
                && paOrders[i].fRemoveOnFailure
                && paOrders[i].pszFilename)
                remove(paOrders[i].pszFilename);
        }
    }
}


#if !defined(USE_POSIX_SPAWN) && !defined(KBUILD_OS_WINDOWS)

/**
 * Saves a file handle to one which isn't inherited and isn't affected by the
 * file orders.
 *
 * @returns 0 on success, non-zero exit code on failure.
 * @param   pToSave         Pointer to the file order to save the target
 *                          descriptor of.
 * @param   cOrders         Number of file orders.
 * @param   paOrders        The array of file orders.
 * @param   ppWorkingStdErr Pointer to a pointer to a working stderr.  This will
 *                          get replaced if we're saving stderr, so that we'll
 *                          keep having a working one to report failures to.
 */
static int kRedirectSaveHandle(REDIRECTORDERS *pToSave, unsigned cOrders, REDIRECTORDERS *paOrders, FILE **ppWorkingStdErr)
{
    int fdToSave = pToSave->fdTarget;
    int rcRet    = 10;

    /*
     * First, check if there's actually handle here that needs saving.
     */
    pToSave->fSaved = fcntl(pToSave->fdTarget, F_GETFD, 0);
    if (pToSave->fSaved != -1)
    {
        /*
         * Try up to 32 times to get a duplicate descriptor that doesn't conflict.
         */
        int aFdTries[32];
        int cTries = 0;
        do
        {
            /* Duplicate the handle (windows makes this complicated). */
            int fdDup;
            fdDup = dup(fdToSave);
            if (fdDup == -1)
            {
                fprintf(*ppWorkingStdErr, "%s: dup(%#x) failed: %u\n", g_progname, fdToSave, strerror(errno));
                break;
            }
            /* Is the duplicate usable? */
            if (!kRedirectHasConflict(fdDup, cOrders, paOrders))
            {
                pToSave->fdSaved = fdDup;
                if (   *ppWorkingStdErr == stderr
                    && fdToSave == fileno(*ppWorkingStdErr))
                {
                    *ppWorkingStdErr = fdopen(fdDup, "wt");
                    if (*ppWorkingStdErr == NULL)
                    {
                        fprintf(stderr, "%s: fdopen(%d,\"wt\") failed: %s\n", g_progname, fdDup, strerror(errno));
                        *ppWorkingStdErr = stderr;
                        close(fdDup);
                        break;
                    }
                }
                rcRet = 0;
                break;
            }

            /* Not usuable, stash it and try again. */
            aFdTries[cTries++] = fdDup;
        } while (cTries < K_ELEMENTS(aFdTries));

        /*
         * Clean up unused duplicates.
         */
        while (cTries-- > 0)
            close(aFdTries[cTries]);
    }
    else
    {
        /*
         * Nothing to save.
         */
        pToSave->fdSaved = -1;
        rcRet = 0;
    }
    return rcRet;
}


/**
 * Restores the target file descriptors affected by the file operation orders.
 *
 * @param   cOrders         Number of file operation orders.
 * @param   paOrders        The file operation orders.
 * @param   ppWorkingStdErr Pointer to a pointer to the working stderr.  If this
 *                          is one of the saved file descriptors, we'll restore
 *                          it to stderr.
 */
static void kRedirectRestoreFdOrders(unsigned cOrders, REDIRECTORDERS *paOrders, FILE **ppWorkingStdErr)
{
    int iSavedErrno = errno;
    unsigned i = cOrders;
    while (i-- > 0)
    {
        if (paOrders[i].fdSaved != -1)
        {
            KBOOL fRestoreStdErr = *ppWorkingStdErr != stderr
                                 && paOrders[i].fdSaved == fileno(*ppWorkingStdErr);
            if (dup2(paOrders[i].fdSaved, paOrders[i].fdTarget) != -1)
            {
                close(paOrders[i].fdSaved);
                paOrders[i].fdSaved = -1;

                if (fRestoreStdErr)
                {
                    *ppWorkingStdErr = stderr;
                    assert(fileno(stderr) == paOrders[i].fdTarget);
                }
            }
            else
                fprintf(*ppWorkingStdErr, "%s: dup2(%d,%d) failed: %s\n",
                        g_progname, paOrders[i].fdSaved, paOrders[i].fdTarget, strerror(errno));
        }

        if (paOrders[i].fSaved != -1)
        {
            if (fcntl(paOrders[i].fdTarget, F_SETFD, paOrders[i].fSaved & FD_CLOEXEC) != -1)
                paOrders[i].fSaved = -1;
            else
                fprintf(*ppWorkingStdErr, "%s: fcntl(%d,F_SETFD,%s) failed: %s\n",
                        g_progname, paOrders[i].fdTarget, paOrders[i].fSaved & FD_CLOEXEC ? "FD_CLOEXEC" : "0", strerror(errno));
        }
    }
    errno = iSavedErrno;
}


/**
 * Executes the file operation orders.
 *
 * @returns 0 on success, exit code on failure.
 * @param   cOrders         Number of file operation orders.
 * @param   paOrders        File operation orders to execute.
 * @param   ppWorkingStdErr Where to return a working stderr (mainly for
 *                          kRedirectRestoreFdOrders).
 */
static int kRedirectExecFdOrders(unsigned cOrders, REDIRECTORDERS *paOrders, FILE **ppWorkingStdErr)
{
    unsigned i;

    *ppWorkingStdErr = stderr;
    for (i = 0; i < cOrders; i++)
    {
        int rcExit = 10;
        switch (paOrders[i].enmOrder)
        {
            case kRedirectOrder_Close:
            {
                /* If the handle isn't used by any of the following operation,
                   just mark it as non-inheritable if necessary. */
                int const fdTarget = paOrders[i].fdTarget;
                unsigned j;
                for (j = i + 1; j < cOrders; j++)
                    if (paOrders[j].fdTarget == fdTarget)
                        break;
                if (j >= cOrders)
                {
                    paOrders[j].fSaved = fcntl(fdTarget, F_GETFD, 0);
                    if (paOrders[j].fSaved != -1)
                    {
                        if (paOrders[j].fSaved & FD_CLOEXEC)
                            rcExit = 0;
                        else if (   fcntl(fdTarget, F_SETFD, FD_CLOEXEC) != -1
                                 || errno == EBADF)
                            rcExit = 0;
                        else
                            fprintf(*ppWorkingStdErr, "%s: fcntl(%d,F_SETFD,FD_CLOEXEC) failed: %s\n",
                                    g_progname, fdTarget, strerror(errno));
                    }
                    else if (errno == EBADF)
                        rcExit = 0;
                    else
                        fprintf(*ppWorkingStdErr, "%s: fcntl(%d,F_GETFD,0) failed: %s\n", g_progname, fdTarget, strerror(errno));
                }
                else
                    rcExit = kRedirectSaveHandle(&paOrders[i], cOrders, paOrders, ppWorkingStdErr);
                break;
            }

            case kRedirectOrder_Dup:
            case kRedirectOrder_Open:
                rcExit = kRedirectSaveHandle(&paOrders[i], cOrders, paOrders, ppWorkingStdErr);
                if (rcExit == 0)
                {
                    if (dup2(paOrders[i].fdSource, paOrders[i].fdTarget) != -1)
                        rcExit = 0;
                    else
                    {
                        if (paOrders[i].enmOrder == kRedirectOrder_Open)
                            fprintf(*ppWorkingStdErr, "%s: dup2(%d [%s],%d) failed: %s\n", g_progname, paOrders[i].fdSource,
                                    paOrders[i].pszFilename, paOrders[i].fdTarget, strerror(errno));
                        else
                            fprintf(*ppWorkingStdErr, "%s: dup2(%d,%d) failed: %s\n",
                                    g_progname, paOrders[i].fdSource, paOrders[i].fdTarget, strerror(errno));
                        rcExit = 10;
                    }
                }
                break;

            default:
                fprintf(*ppWorkingStdErr, "%s: error! invalid enmOrder=%d\n", g_progname, paOrders[i].enmOrder);
                rcExit = 99;
                break;
        }

        if (rcExit != 0)
        {
            kRedirectRestoreFdOrders(i, paOrders, ppWorkingStdErr);
            return rcExit;
        }
    }

    return 0;
}

#endif /* !USE_POSIX_SPAWN */
#ifdef KBUILD_OS_WINDOWS

/**
 * Tries to locate the executable image.
 *
 * This isn't quite perfect yet...
 *
 * @returns pszExecutable or pszBuf with valid string.
 * @param   pszExecutable   The specified executable.
 * @param   pszBuf          Buffer to return a modified path in.
 * @param   cbBuf           Size of return buffer.
 * @param   pszPath         The search path.
 */
static const char *kRedirectCreateProcessWindowsFindImage(const char *pszExecutable, char *pszBuf, size_t cbBuf,
                                                          const char *pszPath)
{
    /*
     * Analyze the name.
     */
    size_t const cchExecutable = strlen(pszExecutable);
    BOOL         fHavePath = FALSE;
    BOOL         fHaveSuffix = FALSE;
    size_t       off = cchExecutable;
    while (off > 0)
    {
        char ch = pszExecutable[--off];
        if (ch == '.')
        {
            fHaveSuffix = TRUE;
            break;
        }
        if (ch == '\\' || ch == '/' || ch == ':')
        {
            fHavePath = TRUE;
            break;
        }
    }
    if (!fHavePath)
        while (off > 0)
        {
            char ch = pszExecutable[--off];
            if (ch == '\\' || ch == '/' || ch == ':')
            {
                fHavePath = TRUE;
                break;
            }
        }
   /*
    * If no path, search the path value.
    */
   if (!fHavePath)
   {
       char *pszFilename;
       DWORD cchFound = SearchPathA(pszPath, pszExecutable, fHaveSuffix ? NULL : ".exe", cbBuf, pszBuf, &pszFilename);
       if (cchFound)
           return pszBuf;
   }

   /*
    * If no suffix, try add .exe.
    */
   if (   !fHaveSuffix
       && GetFileAttributesA(pszExecutable) == INVALID_FILE_ATTRIBUTES
       && cchExecutable + 4 < cbBuf)
   {
       memcpy(pszBuf, pszExecutable, cchExecutable);
       memcpy(&pszBuf[cchExecutable], ".exe", 5);
       if (GetFileAttributesA(pszBuf) != INVALID_FILE_ATTRIBUTES)
           return pszBuf;
   }

   return pszExecutable;
}

/**
 * Alternative approach on windows that use CreateProcess and doesn't require
 * any serialization wrt handles and CWD.
 *
 * @returns 0 on success, non-zero on failure to create.
 * @param   pszExecutable       The child process executable.
 * @param   cArgs               Number of arguments.
 * @param   papszArgs           The child argument vector.
 * @param   papszEnvVars        The child environment vector.
 * @param   pszCwd              The current working directory of the child.
 * @param   cOrders             Number of file operation orders.
 * @param   paOrders            The file operation orders.
 * @param   phProcess           Where to return process handle.
 */
static kRedirectCreateProcessWindows(const char *pszExecutable, int cArgs, char **papszArgs, char **papszEnvVars,
                                     const char *pszCwd, unsigned cOrders, REDIRECTORDERS *paOrders, HANDLE *phProcess)
{
    static NTSTATUS (NTAPI *s_pfnNtQueryInformationProcess)(HANDLE, PROCESSINFOCLASS, PVOID, ULONG, PULONG) = NULL;
    size_t cbArgs;
    char  *pszCmdLine;
    size_t cbEnv;
    char  *pszzEnv;
    char  *pch;
    int    i;
    int    rc;

    /*
     * Determin host bitness and APIs while we can still easily return.
     */
#if K_ARCH_BITS == 32
    BOOL   f64BitHost = TRUE;
    if (!IsWow64Process(GetCurrentProcess(), &f64BitHost))
        return errx(9, "IsWow64Process failed: %u", GetLastError());
#elif K_ARCH_BITS == 64
    BOOL const f64BitHost = TRUE;
#else
# error "K_ARCH_BITS is bad/missing"
#endif
    if (cOrders > 0 && !s_pfnNtQueryInformationProcess)
    {
        *(FARPROC *)&s_pfnNtQueryInformationProcess = GetProcAddress(GetModuleHandleA("NTDLL.DLL"), "NtQueryInformationProcess");
        if (!s_pfnNtQueryInformationProcess)
            return errx(9, "NtQueryInformationProcess not found!");
    }

    /*
     * Start by making the the command line.  We just need to put spaces
     * between the arguments since quote_argv don't the quoting already.
     */
    cbArgs = 0;
    for (i = 0; i < cArgs; i++)
        cbArgs += strlen(papszArgs[i]) + 1;
    pszCmdLine = pch = (char *)malloc(cbArgs);
    if (!pszCmdLine)
        return errx(9, "out of memory!");
    for (i = 0; i < cArgs; i++)
    {
        size_t cch;
        if (i != 0)
            *pch++ = ' ';
        cch = strlen(papszArgs[i]);
        memcpy(pch, papszArgs[i], cch);
        pch += cch;
    }
    *pch++ = '\0';
    assert(pch - pszCmdLine == cbArgs);

    /*
     * The environment vector is also simple.
     */
    cbEnv = 0;
    for (i = 0; papszEnvVars[i]; i++)
        cbEnv += strlen(papszEnvVars[i]) + 1;
    cbEnv++;
    pszzEnv = pch = (char *)malloc(cbEnv);
    if (pszzEnv)
    {
        char                szAbsExe[1024];
        const char         *pszPathVal = NULL;
        STARTUPINFOA        StartupInfo;
        PROCESS_INFORMATION ProcInfo = { NULL, NULL, 0, 0 };

        for (i = 0; papszEnvVars[i]; i++)
        {
            size_t cbSrc = strlen(papszEnvVars[i]) + 1;
            memcpy(pch, papszEnvVars[i], cbSrc);
            if (   !pszPathVal
                && cbSrc >= 5
                && pch[4] == '='
                && (pch[0] == 'P' || pch[0] == 'p')
                && (pch[1] == 'A' || pch[1] == 'a')
                && (pch[2] == 'T' || pch[2] == 't')
                && (pch[3] == 'H' || pch[3] == 'h'))
                pszPathVal = &pch[5];
            pch += cbSrc;
        }
        *pch++ = '\0';
        assert(pch - pszzEnv == cbEnv);

        /*
         * Locate the executable.
         */
        pszExecutable = kRedirectCreateProcessWindowsFindImage(pszExecutable, szAbsExe, sizeof(szAbsExe), pszPathVal);

        /*
         * Do basic startup info preparation.
         */
        memset(&StartupInfo, 0, sizeof(StartupInfo));
        StartupInfo.cb = sizeof(StartupInfo);
        GetStartupInfoA(&StartupInfo);
        StartupInfo.lpReserved2 = 0; /* No CRT file handle + descriptor info possible, sorry. */
        StartupInfo.cbReserved2 = 0;
        StartupInfo.dwFlags &= ~STARTF_USESTDHANDLES;

        /*
         * If there are no redirection orders, we're good.
         */
        if (!cOrders)
        {
            if (CreateProcessA(pszExecutable, pszCmdLine, NULL /*pProcAttrs*/, NULL /*pThreadAttrs*/,
                               FALSE /*fInheritHandles*/, 0 /*fFlags*/, pszzEnv, pszCwd, &StartupInfo, &ProcInfo))
            {
                CloseHandle(ProcInfo.hThread);
                *phProcess = ProcInfo.hProcess;
                rc = 0;
            }
            else
                rc = errx(10, "CreateProcessA(%s) failed: %u", pszExecutable, GetLastError());
        }
        else
        {
            /*
             * Execute the orders, ending up with three handles we need to
             * implant into the guest process.
             *
             * This isn't 100% perfect wrt O_APPEND, but it'll have to do for now.
             */
            BOOL   afReplace[3] = { FALSE, FALSE, FALSE };
            HANDLE ahChild[3]   = { NULL,  NULL,  NULL  };
            rc = 0;
            for (i = 0; i < (int)cOrders; i++)
            {
                int fdTarget = paOrders[i].fdTarget;
                assert(fdTarget >= 0 && fdTarget < 3);
                switch (paOrders[i].enmOrder)
                {
                    case kRedirectOrder_Open:
                        if (   (paOrders[i].fOpen & O_APPEND)
                            && lseek(paOrders[i].fdSource, 0, SEEK_END) < 0)
                            rc = err(10, "lseek-to-end failed on %d (for %d)", paOrders[i].fdSource, fdTarget);
                    case kRedirectOrder_Dup:
                        ahChild[fdTarget] = (HANDLE)_get_osfhandle(paOrders[i].fdSource);
                        if (ahChild[fdTarget] == NULL || ahChild[fdTarget] == INVALID_HANDLE_VALUE)
                            rc = err(10, "_get_osfhandle failed on %d (for %d)", paOrders[i].fdSource, fdTarget);
                        break;
                    case kRedirectOrder_Close:
                        ahChild[fdTarget] = NULL;
                        break;
                    default:
                        assert(0);
                }
                afReplace[fdTarget] = TRUE;
            }
            if (rc == 0)
            {
                /*
                 * Start the process in suspended animation so we can inject handles.
                 */
                if (CreateProcessA(pszExecutable, pszCmdLine, NULL /*pProcAttrs*/, NULL /*pThreadAttrs*/,
                                   FALSE /*fInheritHandles*/, CREATE_SUSPENDED, pszzEnv, pszCwd, &StartupInfo, &ProcInfo))
                {
                    /*
                     * Figure out where we need to write the handles.
                     */
                    ULONG                     cbActual1 = 0;
                    PROCESS_BASIC_INFORMATION BasicInfo = { 0, 0, };
                    NTSTATUS rcNt = s_pfnNtQueryInformationProcess(ProcInfo.hProcess, ProcessBasicInformation,
                                                                   &BasicInfo, sizeof(BasicInfo), &cbActual1);
                    if (NT_SUCCESS(rcNt))
                    {
                        BOOL const   f32BitPeb  = !f64BitHost;
                        ULONG  const cbChildPtr = f32BitPeb ? 4 : 8;
                        PVOID        pvSrcInPeb = (char *)BasicInfo.PebBaseAddress + (f32BitPeb ? 0x10 : 0x20);
                        char *       pbDst      = 0;
                        SIZE_T       cbActual2  = 0;
                        if (ReadProcessMemory(ProcInfo.hProcess, pvSrcInPeb, &pbDst, cbChildPtr, &cbActual2))
                        {
                            union
                            {
                                KU32 au32[3];
                                KU64 au64[3];
                            } uBuf;
                            memset(&uBuf, 0, sizeof(uBuf));
                            pbDst += f32BitPeb ? 0x18 : 0x20;
                            if (   (afReplace[0] && afReplace[1] && afReplace[2])
                                || ReadProcessMemory(ProcInfo.hProcess, pbDst, &uBuf, cbChildPtr * 3, &cbActual2))
                            {
                                for (i = 0; i < 3; i++)
                                    if (afReplace[i])
                                    {
                                        HANDLE hInChild = INVALID_HANDLE_VALUE;
                                        if (   ahChild[i] == NULL /* just closed*/
                                            || DuplicateHandle(GetCurrentProcess(), ahChild[i], ProcInfo.hProcess, &hInChild,
                                                               0, TRUE /*fInheriable*/, DUPLICATE_SAME_ACCESS))
                                        {
                                            if (f32BitPeb)
                                                uBuf.au32[i] = (KU32)(uintptr_t)hInChild;
                                            else
                                                uBuf.au64[i] = (uintptr_t)hInChild;
                                        }
                                        else
                                            rc = errx(10, "Error duplicating %p into the child: %u", ahChild[i], GetLastError());
                                    }
                                if (    rc == 0
                                    && !WriteProcessMemory(ProcInfo.hProcess, pbDst, &uBuf, cbChildPtr * 3, &cbActual2))
                                    rc = errx(10, "Error writing standard handles at %p LB %u in the child: %u",
                                              pbDst, cbChildPtr * 3, GetLastError());
                            }
                            else
                                rc = errx(10, "Error reading %p LB %u from the child: %u",
                                          pbDst, cbChildPtr * 3, GetLastError());
                        }
                        else
                            rc = errx(10, "Error reading %p LB %u from the child: %u", pvSrcInPeb, cbChildPtr);
                    }
                    else
                        rc = errx(10, "NtQueryInformationProcess failed: %#x", rcNt);

                    /* Resume the bugger... */
                    if (   rc == 0
                        && !ResumeThread(ProcInfo.hThread))
                        rc = errx(10, "ResumeThread failed: %u", GetLastError());

                    /* .. or kill it. */
                    if (rc != 0)
                        TerminateProcess(ProcInfo.hProcess, rc);

                    CloseHandle(ProcInfo.hThread);
                    *phProcess = ProcInfo.hProcess;
                    rc = 0;
                }
                else
                    rc = errx(10, "CreateProcessA(%s) failed: %u", pszExecutable, GetLastError());
            }
        }
        free(pszzEnv);
    }
    else
        rc = errx(9, "out of memory!");
    free(pszCmdLine);
    return rc;
}

#endif /* KBUILD_OS_WINDOWS */

/**
 * Does the child spawning .
 *
 * @returns Exit code.
 * @param   pszExecutable       The child process executable.
 * @param   cArgs               Number of arguments.
 * @param   papszArgs           The child argument vector.
 * @param   fWatcomBrainDamage  Whether MSC need to do quoting according to
 *                              weird Watcom WCC rules.
 * @param   papszEnvVars        The child environment vector.
 * @param   pszCwd              The current working directory of the child.
 * @param   pszSavedCwd         The saved current working directory.  This is
 *                              NULL if the CWD doesn't need changing.
 * @param   cOrders             Number of file operation orders.
 * @param   paOrders            The file operation orders.
 * @param   pFileActions        The posix_spawn file actions.
 * @param   cVerbosity          The verbosity level.
 * @param   pPidSpawned         Where to return the PID of the spawned child
 *                              when we're inside KMK and we're return without
 *                              waiting.
 * @param   pfIsChildExitCode   Where to indicate whether the return exit code
 *                              is from the child or from our setup efforts.
 */
static int kRedirectDoSpawn(const char *pszExecutable, int cArgs, char **papszArgs, int fWatcomBrainDamage,
                            char **papszEnvVars, const char *pszCwd, const char *pszSavedCwd,
                            unsigned cOrders, REDIRECTORDERS *paOrders,
#ifdef USE_POSIX_SPAWN
                            posix_spawn_file_actions_t *pFileActions,
#endif
                            unsigned cVerbosity,
#ifdef KMK
                            pid_t *pPidSpawned,
#endif
                            KBOOL *pfIsChildExitCode)
{
    int     rcExit = 0;
    int     i;
#ifdef _MSC_VER
    char  **papszArgsOriginal = papszArgs;
#endif
    *pfIsChildExitCode = K_FALSE;

#ifdef _MSC_VER
    /*
     * Do MSC parameter quoting.
     */
    papszArgs = malloc((cArgs + 1) * sizeof(papszArgs[0]));
    if (papszArgs)
        memcpy(papszArgs, papszArgsOriginal, (cArgs + 1) * sizeof(papszArgs[0]));
    else
        return errx(9, "out of memory!");

    rcExit = quote_argv(cArgs, papszArgs, fWatcomBrainDamage, 0 /*fFreeOrLeak*/);
    if (rcExit == 0)
#endif
    {
        /*
         * Display what we're about to execute if we're in verbose mode.
         */
        if (cVerbosity > 0)
        {
            for (i = 0; i < cArgs; i++)
                warnx("debug: argv[%i]=%s<eos>", i, papszArgs[i]);
            for (i = 0; i < (int)cOrders; i++)
                switch (paOrders[i].enmOrder)
                {
                    case kRedirectOrder_Close:
                        warnx("debug: close %d\n", paOrders[i].fdTarget);
                        break;
                    case kRedirectOrder_Dup:
                        warnx("debug: dup %d to %d\n", paOrders[i].fdSource, paOrders[i].fdTarget);
                        break;
                    case kRedirectOrder_Open:
                        warnx("debug: open '%s' (%#x) as [%d ->] %d\n",
                              paOrders[i].pszFilename, paOrders[i].fOpen, paOrders[i].fdSource, paOrders[i].fdTarget);
                        break;
                    default:
                        warnx("error! invalid enmOrder=%d", paOrders[i].enmOrder);
                        assert(0);
                        break;
                }
            if (pszSavedCwd)
                warnx("debug: chdir %s\n", pszCwd);
        }

#ifndef KBUILD_OS_WINDOWS
        /*
         * Change working directory if so requested.
         */
        if (pszSavedCwd)
        {
            if (chdir(pszCwd) < 0)
                rcExit = errx(10, "Failed to change directory to '%s'", pszCwd);
        }
#endif /* KBUILD_OS_WINDOWS */
        if (rcExit == 0)
        {
# if !defined(USE_POSIX_SPAWN) && !defined(KBUILD_OS_WINDOWS)
            /*
             * Execute the file orders.
             */
            FILE *pWorkingStdErr = NULL;
            rcExit = kRedirectExecFdOrders(cOrders, paOrders, &pWorkingStdErr);
            if (rcExit == 0)
# endif
            {
# ifdef KMK
                /*
                 * We're spawning from within kmk.
                 */
# ifdef KBUILD_OS_WINDOWS
                /* Windows is slightly complicated due to handles and winchildren.c. */
                HANDLE hProcess = INVALID_HANDLE_VALUE;
                rcExit = kRedirectCreateProcessWindows(pszExecutable, cArgs, papszArgs, papszEnvVars, pszCwd,
                                                       cOrders, paOrders, &hProcess);
                if (rcExit == 0)
                {
#  ifndef CONFIG_NEW_WIN_CHILDREN
                    if (process_kmk_register_redirect(hProcess, pPidSpawned) == 0)
#  else
                    if (MkWinChildCreateRedirect((intptr_t)hProcess, pPidSpawned) == 0)
#  endif
                    {
                        if (cVerbosity > 0)
                            warnx("debug: spawned %d", *pPidSpawned);
                    }
                    else
                    {
                        DWORD dwTmp;
#  ifndef CONFIG_NEW_WIN_CHILDREN
                        warn("sub_proc is out of slots, waiting for child...");
#  else
                        warn("MkWinChildCreateRedirect failed...");
#  endif
                        dwTmp = WaitForSingleObject(hProcess, INFINITE);
                        if (dwTmp != WAIT_OBJECT_0)
                            warn("WaitForSingleObject failed: %#x\n", dwTmp);

                        if (GetExitCodeProcess(hProcess, &dwTmp))
                            rcExit = (int)dwTmp;
                        else
                        {
                            warn("GetExitCodeProcess failed: %u\n", GetLastError());
                            TerminateProcess(hProcess, 127);
                            rcExit = 127;
                        }

                        CloseHandle(hProcess);
                        *pPidSpawned = 0;
                        *pfIsChildExitCode = K_TRUE;
                    }
                }

# elif defined(KBUILD_OS_OS2)
                *pPidSpawned = _spawnvpe(P_NOWAIT, pszExecutable, papszArgs, papszEnvVars);
                kRedirectRestoreFdOrders(cOrders, paOrders, &pWorkingStdErr);
                if (*pPidSpawned != -1)
                {
                    if (cVerbosity > 0)
                        warnx("debug: spawned %d", *pPidSpawned);
                }
                else
                {
                    rcExit = err(10, "_spawnvpe(%s) failed", pszExecutable);
                    *pPidSpawned = 0;
                }
# else
                rcExit = posix_spawnp(pPidSpawned, pszExecutable, pFileActions, NULL /*pAttr*/, papszArgs, papszEnvVars);
                if (rcExit == 0)
                {
                    if (cVerbosity > 0)
                        warnx("debug: spawned %d", *pPidSpawned);
                }
                else
                {
                    rcExit = errx(10, "posix_spawnp(%s) failed: %s", pszExecutable, strerror(rcExit));
                    *pPidSpawned = 0;
                }
# endif

#else  /* !KMK */
                /*
                 * Spawning from inside the kmk_redirect executable.
                 */
# ifdef KBUILD_OS_WINDOWS
                HANDLE hProcess = INVALID_HANDLE_VALUE;
                rcExit = kRedirectCreateProcessWindows(pszExecutable, cArgs, papszArgs, papszEnvVars, pszCwd,
                                                       cOrders, paOrders, &hProcess);
                if (rcExit == 0)
                {
                    DWORD dwWait;
                    do
                        dwWait = WaitForSingleObject(hProcess, INFINITE);
                    while (dwWait == WAIT_IO_COMPLETION || dwWait == WAIT_TIMEOUT);

                    dwWait = 11;
                    if (GetExitCodeProcess(hProcess, &dwWait))
                        rcExit = dwWait;
                    else
                        rcExit = errx(11, "GetExitCodeProcess(%s) failed: %u", pszExecutable, GetLastError());
                }

#elif defined(KBUILD_OS_OS2)
                errno  = 0;
                rcExit = (int)_spawnvpe(P_WAIT, pszExecutable, papszArgs, papszEnvVars);
                kRedirectRestoreFdOrders(cOrders, paOrders, &pWorkingStdErr);
                if (rcExit != -1 || errno == 0)
                {
                    *pfIsChildExitCode = K_TRUE;
                    if (cVerbosity > 0)
                        warnx("debug: exit code: %d", rcExit);
                }
                else
                    rcExit = err(10, "_spawnvpe(%s) failed", pszExecutable);

# else
                pid_t pidChild = 0;
                rcExit = posix_spawnp(&pidChild, pszExecutable, pFileActions, NULL /*pAttr*/, papszArgs, papszEnvVars);
                if (rcExit == 0)
                {
                    *pfIsChildExitCode = K_TRUE;
                    if (cVerbosity > 0)
                        warnx("debug: spawned %d", pidChild);

                    /* Wait for the child. */
                    for (;;)
                    {
                        pid_t pid = waitpid(pidChild, &rcExit, 0 /*block*/);
                        if (pid == pidChild)
                        {
                            if (cVerbosity > 0)
                                warnx("debug: %d exit code: %d", pidChild, rcExit);
                            break;
                        }
                        if (   errno != EINTR
#  ifdef ERESTART
                            && errno != ERESTART
#  endif
                           )
                        {
                            rcExit = err(11, "waitpid failed");
                            kill(pidChild, SIGKILL);
                            break;
                        }
                    }
                }
                else
                    rcExit = errx(10, "posix_spawnp(%s) failed: %s", pszExecutable, strerror(rcExit));
# endif
#endif /* !KMK */
            }
        }

#ifndef KBUILD_OS_WINDOWS
        /*
         * Restore the current directory.
         */
        if (pszSavedCwd)
        {
            if (chdir(pszSavedCwd) < 0)
                warn("Failed to restore directory to '%s'", pszSavedCwd);
        }
#endif
    }
#ifdef _MSC_VER
    else
        rcExit = errx(9, "quite_argv failed: %u", rcExit);

    /* Restore the original argv strings, freeing the quote_argv replacements. */
    i = cArgs;
    while (i-- > 0)
        if (papszArgs[i] != papszArgsOriginal[i])
            free(papszArgs[i]);
    free(papszArgs);
#endif
    return rcExit;
}


/**
 * The function that does almost everything here... ugly.
 */
#ifdef KMK
int kmk_builtin_redirect(int argc, char **argv, char **envp, struct child *pChild, pid_t *pPidSpawned)
#else
int main(int argc, char **argv, char **envp)
#endif
{
    int             rcExit = 0;
    KBOOL           fChildExitCode = K_FALSE;
#ifdef USE_POSIX_SPAWN
    posix_spawn_file_actions_t FileActions;
#endif
    unsigned        cOrders = 0;
    REDIRECTORDERS  aOrders[32];

    int             iArg;
    const char     *pszExecutable      = NULL;
    char          **papszEnvVars       = NULL;
    unsigned        cAllocatedEnvVars;
    unsigned        iEnvVar;
    unsigned        cEnvVars;
    int             fWatcomBrainDamage = 0;
    int             cVerbosity         = 0;
    char           *pszSavedCwd        = NULL;
    size_t const    cbCwdBuf           = GET_PATH_MAX;
    PATH_VAR(szCwd);
#ifdef KBUILD_OS_OS2
    ULONG           ulLibPath;
    char           *apszSavedLibPaths[LIBPATHSTRICT + 1] = { NULL, NULL, NULL, NULL };
#endif


    g_progname = argv[0];

    if (argc <= 1)
        return usage(stderr, argv[0]);

    /*
     * Create default program environment.
     */
#if defined(KMK) && defined(KBUILD_OS_WINDOWS)
    if (getcwd_fs(szCwd, cbCwdBuf) != NULL)
#else
    if (getcwd(szCwd, cbCwdBuf) != NULL)
#endif
    { /* likely */ }
    else
        return err(9, "getcwd failed");

#if defined(KMK)
    /* We get it from kmk and just count it:  */
    papszEnvVars = pChild->environment;
    if (!papszEnvVars)
        pChild->environment = papszEnvVars = target_environment(pChild->file);
    cEnvVars = 0;
    while (papszEnvVars[cEnvVars] != NULL)
        cEnvVars++;
    cAllocatedEnvVars = cEnvVars;
#else
    /* We make a copy and we manage ourselves: */
    cEnvVars = 0;
    while (envp[cEnvVars] != NULL)
        cEnvVars++;

    cAllocatedEnvVars = cEnvVars + 4;
    papszEnvVars = malloc((cAllocatedEnvVars + 1) * sizeof(papszEnvVars));
    if (!papszEnvVars)
        return errx(9, "out of memory!");

    iEnvVar = cEnvVars;
    papszEnvVars[iEnvVar] = NULL;
    while (iEnvVar-- > 0)
    {
        papszEnvVars[iEnvVar] = strdup(envp[iEnvVar]);
        if (!papszEnvVars[iEnvVar])
        {
            while (iEnvVar-- > 0)
                free(papszEnvVars[iEnvVar]);
            free(papszEnvVars);
            return errx(9, "out of memory!");
        }
    }
#endif

#ifdef USE_POSIX_SPAWN
    /*
     * Init posix attributes.
     */
    rcExit = posix_spawn_file_actions_init(&FileActions);
    if (rcExit != 0)
        rcExit = errx(9, "posix_spawn_file_actions_init failed: %s", strerror(rcExit));
#endif

    /*
     * Parse arguments.
     */
    for (iArg = 1; rcExit == 0 && iArg < argc; iArg++)
    {
        char *pszArg = argv[iArg];
        if (*pszArg == '-')
        {
            int         fd;
            char        chOpt;
            const char *pszValue;

            chOpt = *++pszArg;
            pszArg++;
            if (chOpt == '-')
            {
                /* '--' indicates where the bits to execute start.  Check if we're
                   relaunching ourselves here and just continue parsing if we are. */
                if (*pszArg == '\0')
                {
                    iArg++;
                    if (    iArg >= argc
                        || (   strcmp(argv[iArg], "kmk_builtin_redirect") != 0
                            && strcmp(argv[iArg], argv[0]) != 0))
                        break;
                    continue;
                }

                if (   strcmp(pszArg, "wcc-brain-damage") == 0
                    || strcmp(pszArg, "watcom-brain-damage") == 0)
                {
                    fWatcomBrainDamage = 1;
                    continue;
                }

                /* convert to short. */
                if (strcmp(pszArg, "help") == 0)
                    chOpt = 'h';
                else if (strcmp(pszArg, "version") == 0)
                    chOpt = 'V';
                else if (   strcmp(pszArg, "set") == 0
                         || strcmp(pszArg, "env") == 0)
                    chOpt = 'E';
                else if (strcmp(pszArg, "append") == 0)
                    chOpt = 'A';
                else if (strcmp(pszArg, "prepend") == 0)
                    chOpt = 'D';
                else if (strcmp(pszArg, "unset") == 0)
                    chOpt = 'U';
                else if (   strcmp(pszArg, "zap-env") == 0
                         || strcmp(pszArg, "ignore-environment") == 0 /* GNU env compatibility. */ )
                    chOpt = 'Z';
                else if (strcmp(pszArg, "chdir") == 0)
                    chOpt = 'C';
                else if (strcmp(pszArg, "close") == 0)
                    chOpt = 'c';
                else if (strcmp(pszArg, "verbose") == 0)
                    chOpt = 'v';
                else
                {
                    errx(2, "Unknown option: '%s'", pszArg - 2);
                    rcExit = usage(stderr, argv[0]);
                    break;
                }
                pszArg = "";
            }

            /*
             * Deal with the obligatory help and version switches first to get them out of the way.
             */
            if (chOpt == 'h')
            {
                usage(stdout, argv[0]);
                rcExit = -1;
                break;
            }
            if (chOpt == 'V')
            {
                kbuild_version(argv[0]);
                rcExit = -1;
                break;
            }

            /*
             * Get option value first, if the option takes one.
             */
            if (   chOpt == 'E'
                || chOpt == 'A'
                || chOpt == 'D'
                || chOpt == 'U'
                || chOpt == 'C'
                || chOpt == 'c'
                || chOpt == 'd'
                || chOpt == 'e')
            {
                if (*pszArg != '\0')
                    pszValue = pszArg + (*pszArg == ':' || *pszArg == '=');
                else if (++iArg < argc)
                    pszValue = argv[iArg];
                else
                {
                    errx(2, "syntax error: Option -%c requires a value!", chOpt);
                    rcExit = usage(stderr, argv[0]);
                    break;
                }
            }
            else
                pszValue = NULL;

            /*
             * Environment switch?
             */
            if (chOpt == 'E')
            {
                const char *pchEqual = strchr(pszValue, '=');
#ifdef KBUILD_OS_OS2
                if (   strncmp(pszValue, TUPLE("BEGINLIBPATH=")) == 0
                    || strncmp(pszValue, TUPLE("ENDLIBPATH=")) == 0
                    || strncmp(pszValue, TUPLE("LIBPATHSTRICT=")) == 0)
                {
                    ULONG   ulVar  = *pszValue == 'B' ? BEGIN_LIBPATH
                                   : *pszValue == 'E' ? END_LIBPATH
                                   :                    LIBPATHSTRICT;
                    APIRET  rc;
                    if (apszSavedLibPaths[ulVar] == NULL)
                    {
                        /* The max length is supposed to be 1024 bytes. */
                        apszSavedLibPaths[ulVar] = calloc(1024, 2);
                        if (apszSavedLibPaths[ulVar])
                        {
                            rc = DosQueryExtLIBPATH(apszSavedLibPaths[ulVar], ulVar);
                            if (rc)
                            {
                                rcExit = errx(9, "DosQueryExtLIBPATH(,%u) failed: %lu", ulVar, rc);
                                free(apszSavedLibPaths[ulVar]);
                                apszSavedLibPaths[ulVar] = NULL;
                            }
                        }
                        else
                            rcExit = errx(9, "out of memory!");
                    }
                    if (rcExit == 0)
                    {
                        rc = DosSetExtLIBPATH(pchEqual + 1, ulVar);
                        if (rc)
                            rcExit = errx(9, "error: DosSetExtLibPath(\"%s\", %.*s (%lu)): %lu",
                                          pchEqual, pchEqual - pszValue, pchEqual + 1, ulVar, rc);
                    }
                    continue;
                }
#endif /* KBUILD_OS_OS2 */

                /* We differ from kSubmit here and use putenv sematics. */
                if (pchEqual)
                {
                    if (pchEqual[1] != '\0')
                    {
                        rcExit = kBuiltinOptEnvSet(&papszEnvVars, &cEnvVars, &cAllocatedEnvVars, cVerbosity, pszValue);
#ifdef KMK
                        pChild->environment = papszEnvVars;
#endif
                    }
                    else
                    {
                        char *pszCopy = strdup(pszValue);
                        if (pszCopy)
                        {
                            pszCopy[pchEqual - pszValue] = '\0';
                            rcExit = kBuiltinOptEnvUnset(papszEnvVars, &cEnvVars, cVerbosity, pszCopy);
                            free(pszCopy);
                        }
                        else
                            rcExit = errx(1, "out of memory!");
                    }
                    continue;
                }
                /* Simple unset. */
                chOpt = 'U';
            }

            /*
             * Append or prepend value to and environment variable.
             */
            if (chOpt == 'A' || chOpt == 'D')
            {
#ifdef KBUILD_OS_OS2
                if (   strcmp(pszValue, "BEGINLIBPATH") == 0
                    || strcmp(pszValue, "ENDLIBPATH") == 0
                    || strcmp(pszValue, "LIBPATHSTRICT") == 0)
                    rcExit = errx(2, "error: '%s' cannot currently be appended or prepended to. Please use -E/--set for now.", pszValue);
                else
#endif
                if (chOpt == 'A')
                    rcExit = kBuiltinOptEnvAppend(&papszEnvVars, &cEnvVars, &cAllocatedEnvVars, cVerbosity, pszValue);
                else
                    rcExit = kBuiltinOptEnvPrepend(&papszEnvVars, &cEnvVars, &cAllocatedEnvVars, cVerbosity, pszValue);
                continue;
            }

            /*
             * Unset environment variable.
             */
            if (chOpt == 'U')
            {
#ifdef KBUILD_OS_OS2
                if (   strcmp(pszValue, "BEGINLIBPATH") == 0
                    || strcmp(pszValue, "ENDLIBPATH") == 0
                    || strcmp(pszValue, "LIBPATHSTRICT") == 0)
                    rcExit = errx(2, "error: '%s' cannot be unset, only set to an empty value using -E/--set.", pszValue);
                else
#endif
                    rcExit = kBuiltinOptEnvUnset(papszEnvVars, &cEnvVars, cVerbosity, pszValue);
                continue;
            }

            /*
             * Zap environment switch?
             */
            if (   chOpt == 'Z'
                || chOpt == 'i' /* GNU env compatibility. */ )
            {
                for (iEnvVar = 0; iEnvVar < cEnvVars; iEnvVar++)
                    free(papszEnvVars[iEnvVar]);
                papszEnvVars[0] = NULL;
                cEnvVars = 0;
                continue;
            }

            /*
             * Change directory switch?
             */
            if (chOpt == 'C')
            {
                if (pszSavedCwd == NULL)
                    pszSavedCwd = strdup(szCwd);
                if (pszSavedCwd)
                    rcExit = kBuiltinOptChDir(szCwd, cbCwdBuf, pszValue);
                else
                    rcExit = err(9, "out of memory!");
                continue;
            }


            /*
             * Verbose operation switch?
             */
            if (chOpt == 'v')
            {
                cVerbosity++;
                continue;
            }

            /*
             * Executable image other than the first argument following '--'.
             */
            if (chOpt == 'e')
            {
                pszExecutable = pszValue;
                continue;
            }

            /*
             * Okay, it is some file descriptor opearation.  Make sure we've got room for it.
             */
            if (cOrders + 1 < K_ELEMENTS(aOrders))
            {
                aOrders[cOrders].fdTarget         = -1;
                aOrders[cOrders].fdSource         = -1;
                aOrders[cOrders].fOpen            = 0;
                aOrders[cOrders].fRemoveOnFailure = 0;
                aOrders[cOrders].pszFilename      = NULL;
#ifndef USE_POSIX_SPAWN
                aOrders[cOrders].fdSaved          = -1;
#endif
            }
            else
            {
                rcExit = errx(2, "error: too many file actions (max: %d)", K_ELEMENTS(aOrders));
                break;
            }

            if (chOpt == 'c')
            {
                /*
                 * Close the specified file descriptor (no stderr/out/in aliases).
                 */
                char *pszTmp;
                fd = (int)strtol(pszValue, &pszTmp, 0);
                if (pszTmp == pszValue || *pszTmp != '\0')
                    rcExit = errx(2, "error: failed to convert '%s' to a number", pszValue);
                else if (fd < 0)
                    rcExit = errx(2, "error: negative fd %d (%s)", fd, pszValue);
#ifdef ONLY_TARGET_STANDARD_HANDLES
                else if (fd > 2)
                    rcExit = errx(2, "error: %d is not a standard descriptor number", fd);
#endif
                else
                {
                    aOrders[cOrders].enmOrder = kRedirectOrder_Close;
                    aOrders[cOrders].fdTarget = fd;
                    cOrders++;
#ifdef USE_POSIX_SPAWN
                    rcExit = posix_spawn_file_actions_addclose(&FileActions, fd);
                    if (rcExit != 0)
                        rcExit = errx(2, "posix_spawn_file_actions_addclose(%d) failed: %s", fd, strerror(rcExit));
#endif
                }
            }
            else if (chOpt == 'd')
            {
                /*
                 * Duplicate file handle.  Value is fdTarget=fdSource
                 */
                char *pszEqual;
                fd = (int)strtol(pszValue, &pszEqual, 0);
                if (pszEqual == pszValue)
                    rcExit = errx(2, "error: failed to convert target descriptor of '-d %s' to a number", pszValue);
                else if (fd < 0)
                    rcExit = errx(2, "error: negative target descriptor %d ('-d %s')", fd, pszValue);
#ifdef ONLY_TARGET_STANDARD_HANDLES
                else if (fd > 2)
                    rcExit = errx(2, "error: target %d is not a standard descriptor number", fd);
#endif
                else if (*pszEqual != '=')
                    rcExit = errx(2, "syntax error: expected '=' to follow target descriptor: '-d %s'", pszValue);
                else
                {
                    char *pszEnd;
                    int fdSource = (int)strtol(++pszEqual, &pszEnd, 0);
                    if (pszEnd == pszEqual || *pszEnd != '\0')
                        rcExit = errx(2, "error: failed to convert source descriptor of '-d %s' to a number", pszValue);
                    else if (fdSource < 0)
                        rcExit = errx(2, "error: negative source descriptor %d ('-d %s')", fdSource, pszValue);
                    else
                    {
                        aOrders[cOrders].enmOrder = kRedirectOrder_Dup;
                        aOrders[cOrders].fdTarget = fd;
                        aOrders[cOrders].fdSource = fdSource;
                        cOrders++;
#ifdef USE_POSIX_SPAWN
                        rcExit = posix_spawn_file_actions_adddup2(&FileActions, fdSource, fd);
                        if (rcExit != 0)
                            rcExit = errx(2, "posix_spawn_file_actions_addclose(%d) failed: %s", fd, strerror(rcExit));
#endif
                    }
                }
            }
            else
            {
                /*
                 * Open file as a given file descriptor.
                 */
                int fdOpened;
                int fOpen;

                /* mode */
                switch (chOpt)
                {
                    case 'r':
                        chOpt = *pszArg++;
                        if (chOpt == '+')
                        {
                            fOpen = O_RDWR;
                            chOpt = *pszArg++;
                        }
                        else
                            fOpen = O_RDONLY;
                        break;

                    case 'w':
                        chOpt = *pszArg++;
                        if (chOpt == '+')
                        {
                            fOpen = O_RDWR | O_CREAT | O_TRUNC;
                            chOpt = *pszArg++;
                        }
                        else
                            fOpen = O_WRONLY | O_CREAT | O_TRUNC;
                        aOrders[cOrders].fRemoveOnFailure = 1;
                        break;

                    case 'a':
                        chOpt = *pszArg++;
                        if (chOpt == '+')
                        {
                            fOpen = O_RDWR | O_CREAT | O_APPEND;
                            chOpt = *pszArg++;
                        }
                        else
                            fOpen = O_WRONLY | O_CREAT | O_APPEND;
                        break;

                    case 'i': /* make sure stdin is read-only. */
                        fOpen = O_RDONLY;
                        break;

                    case '+':
                        rcExit = errx(2, "syntax error: Unexpected '+' in '%s'", argv[iArg]);
                        continue;

                    default:
                        fOpen = O_RDWR | O_CREAT | O_TRUNC;
                        aOrders[cOrders].fRemoveOnFailure = 1;
                        break;
                }

                /* binary / text modifiers */
                switch (chOpt)
                {
                    case 'b':
                        chOpt = *pszArg++;
                    default:
#ifdef O_BINARY
                        fOpen |= O_BINARY;
#elif defined(_O_BINARY)
                        fOpen |= _O_BINARY;
#endif
                        break;

                    case 't':
#ifdef O_TEXT
                        fOpen |= O_TEXT;
#elif defined(_O_TEXT)
                        fOpen |= _O_TEXT;
#endif
                        chOpt = *pszArg++;
                        break;

                }

                /* convert to file descriptor number */
                switch (chOpt)
                {
                    case 'i':
                        fd = 0;
                        break;

                    case 'o':
                        fd = 1;
                        break;

                    case 'e':
                        fd = 2;
                        break;

                    case '0':
                        if (*pszArg == '\0')
                        {
                            fd = 0;
                            break;
                        }
                        /* fall thru */
                    case '1':
                    case '2':
                    case '3':
                    case '4':
                    case '5':
                    case '6':
                    case '7':
                    case '8':
                    case '9':
                        pszValue = pszArg - 1;
                        fd = (int)strtol(pszValue, &pszArg, 0);
                        if (pszArg == pszValue)
                            rcExit = errx(2, "error: failed to convert '%s' to a number", argv[iArg]);
                        else if (fd < 0)
                            rcExit = errx(2, "error: negative fd %d (%s)", fd, argv[iArg]);
#ifdef ONLY_TARGET_STANDARD_HANDLES
                        else if (fd > 2)
                            rcExit = errx(2, "error: %d is not a standard descriptor number", fd);
#endif
                        else
                            break;
                        continue;

                    /*
                     * Invalid argument.
                     */
                    default:
                        rcExit = errx(2, "error: failed to convert '%s' ('%s') to a file descriptor", pszArg, argv[iArg]);
                        continue;
                }

                /*
                 * Check for the filename.
                 */
                if (*pszArg != '\0')
                {
                    if (*pszArg != ':' && *pszArg != '=')
                    {
                        rcExit = errx(2, "syntax error: characters following the file descriptor: '%s' ('%s')",
                                      pszArg, argv[iArg]);
                        break;
                    }
                    pszArg++;
                }
                else if (++iArg < argc)
                    pszArg = argv[iArg];
                else
                {
                    rcExit = errx(2, "syntax error: missing filename argument.");
                    break;
                }

                /*
                 * Open the file.  We could've used posix_spawn_file_actions_addopen here,
                 * but that means complicated error reporting.  So, since we need to do
                 * this for windows anyway, just do it the same way everywhere.
                 */
                fdOpened = kRedirectOpenWithoutConflict(pszArg, fOpen, 0666, cOrders, aOrders,
                                                        aOrders[cOrders].fRemoveOnFailure, fd);
                if (fdOpened >= 0)
                {
                    aOrders[cOrders].enmOrder    = kRedirectOrder_Open;
                    aOrders[cOrders].fdTarget    = fd;
                    aOrders[cOrders].fdSource    = fdOpened;
                    aOrders[cOrders].fOpen       = fOpen;
                    aOrders[cOrders].pszFilename = pszArg;
                    cOrders++;

#ifdef USE_POSIX_SPAWN
                    if (fdOpened != fd)
                    {
                        rcExit = posix_spawn_file_actions_adddup2(&FileActions, fdOpened, fd);
                        if (rcExit != 0)
                            rcExit = err(9, "posix_spawn_file_actions_adddup2(,%d [%s], %d) failed: %s",
                                         fdOpened, fd, pszArg, strerror(rcExit));
                    }
#endif
                }
                else
                    rcExit = 9;
            }
        }
        else
        {
            errx(2, "syntax error: Invalid argument '%s'.", argv[iArg]);
            rcExit = usage(stderr, argv[0]);
        }
    }
    if (!pszExecutable)
        pszExecutable = argv[iArg];

    /*
     * Make sure there's something to execute.
     */
    if (rcExit == 0 && iArg < argc)
    {
        /*
         * Do the spawning in a separate function (main is far to large as it is by now).
         */
        rcExit = kRedirectDoSpawn(pszExecutable, argc - iArg, &argv[iArg], fWatcomBrainDamage,
                                  papszEnvVars,
                                  szCwd, pszSavedCwd,
#ifdef USE_POSIX_SPAWN
                                  cOrders, aOrders, &FileActions, cVerbosity,
#else
                                  cOrders, aOrders, cVerbosity,
#endif
#ifdef KMK
                                  pPidSpawned,
#endif
                                  &fChildExitCode);
    }
    else if (rcExit == 0)
    {
        errx(2, "syntax error: nothing to execute!");
        rcExit = usage(stderr, argv[0]);
    }
    /* Help and version sets rcExit to -1. Change it to zero. */
    else if (rcExit == -1)
        rcExit = 0;

    /*
     * Cleanup.
     */
    if (pszSavedCwd)
        free(pszSavedCwd);
    kRedirectCleanupFdOrders(cOrders, aOrders, rcExit != 0 && !fChildExitCode);
#ifdef USE_POSIX_SPAWN
    posix_spawn_file_actions_destroy(&FileActions);
#endif
#ifndef KMK
    iEnvVar = cEnvVars;
    while (iEnvVar-- > 0)
        free(papszEnvVars[iEnvVar]);
    free(papszEnvVars);
#endif
#ifdef KBUILD_OS_OS2
    for (ulLibPath = 0; ulLibPath < K_ELEMENTS(apszSavedLibPaths); ulLibPath++)
        if (apszSavedLibPaths[ulLibPath] != NULL)
        {
            APIRET rc = DosSetExtLIBPATH(apszSavedLibPaths[ulLibPath], ulLibPath);
            if (rc != 0)
                warnx("DosSetExtLIBPATH('%s',%u) failed with %u when restoring the original values!",
                      apszSavedLibPaths[ulLibPath], ulLibPath, rc);
            free(apszSavedLibPaths[ulLibPath]);
        }
#endif

    return rcExit;
}

