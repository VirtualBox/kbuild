/* $Id: redirect.c 2916 2016-09-15 11:41:42Z knut.osmundsen@oracle.com $ */
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
#ifdef __APPLE__
# define _POSIX_C_SOURCE 1 /* 10.4 sdk and unsetenv */
#endif
#include "make.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#if defined(KBUILD_OS_WINDOWS) || defined(KBUILD_OS_OS2)
# include <process.h>
#endif
#if defined(_MSC_VER)
# include <ctype.h>
# include <io.h>
# include "quote_argv.h"
#else
# include <unistd.h>
# include <spawn.h>
#endif

#include <k/kDefs.h>
#include <k/kTypes.h>
#include "err.h"
#include "kbuild_version.h"
#include "kmkbuiltin.h"
#ifdef KMK
# ifdef KBUILD_OS_WINDOWS
#  include "sub_proc.h"
#  include "pathstuff.h"
# endif
# include "job.h"
# include "variable.h"
#endif

#ifdef __OS2__
# define INCL_BASE
# include <os2.h>
# ifndef LIBPATHSTRICT
#  define LIBPATHSTRICT 3
# endif
#endif

#if !defined(KBUILD_OS_WINDOWS) && !defined(KBUILD_OS_OS2)
# define USE_POSIX_SPAWN
#endif


/* String + strlen tuple. */
#define TUPLE(a_sz)     a_sz, sizeof(a_sz) - 1


#if defined(_MSC_VER)


/** Used by safeCloseFd. */
static void __cdecl ignore_invalid_parameter(const wchar_t *a, const wchar_t *b, const wchar_t *c, unsigned d, uintptr_t e)
{
}

#endif /* _MSC_VER */


/**
 * Safely works around MS CRT's pedantic close() function.
 *
 * @param   fd      The file handle.
 */
static void safeCloseFd(int fd)
{
#ifdef _MSC_VER
    _invalid_parameter_handler pfnOld = _get_invalid_parameter_handler();
    _set_invalid_parameter_handler(ignore_invalid_parameter);
    close(fd);
    _set_invalid_parameter_handler(pfnOld);
#else
    close(fd);
#endif
}


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
            "hand side one (fd).\n"
            "\n"
            "The -c switch will close the specified file descriptor.\n"
            "\n"
            "The -Z switch zaps the environment.\n"
            "\n"
            "The -E switch is for making changes to the environment in a putenv\n"
            "fashion.\n"
            "\n"
            "The -C switch is for changing the current directory. This takes immediate\n"
            "effect, so be careful where you put it.\n"
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
        kRedirectOrder_Dup,
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
    while (cOrders-- > 0)
        if (paOrders[cOrders].fdTarget == fd)
            return K_TRUE;
    return K_FALSE;
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
# error "port me"
#endif
    int         aFdTries[32];
    int         cTries;
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
        if (fdOpened != fdTarget)
            return fdOpened;
#ifndef _MSC_VER /* Stupid, stupid MSVCRT!  No friggin way of making a handle inheritable (or not). */
        if (fcntl(fdOpened, F_SETFD, FD_CLOEXEC) != -1)
            return fdOpened;
#endif
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
            if (   !kRedirectHasConflict(fdOpened, cOrders, paOrders)
#ifdef _MSC_VER
                && fdOpened != fdTarget
#endif
                )
            {
#ifndef _MSC_VER
                if (   fdOpened != fdTarget
                    || fcntl(fdOpened, F_SETFD, FD_CLOEXEC) != -1)
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

#ifndef USE_POSIX_SPAWN

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
# ifdef KBUILD_OS_WINDOWS
    HANDLE hToSave = mscGetOsHandle(fdToSave);
    if (hToSave != INVALID_HANDLE_VALUE)
    {
        pToSave->fSaved = _setmode(fdToSave, _O_BINARY);
        if (pToSave->fSaved != _O_BINARY)
            _setmode(fdToSave, pToSave->fSaved);
        if (!mscIsNativeHandleInheritable(hToSave))
            pToSave->fSaved |= _O_NOINHERIT;
    }
    if (hToSave != INVALID_HANDLE_VALUE)
# else
    pToSave->fSaved = fcntl(pToSave->fdTarget, F_GETFD, 0);
    if (pToSave->fSaved != -1)
# endif
    {
        /*
         * Try up to 32 times to get a duplicate descriptor that doesn't conflict.
         */
# ifdef KBUILD_OS_WINDOWS
        HANDLE hCurProc = GetCurrentProcess();
# endif
        int aFdTries[32];
        int cTries = 0;
        do
        {
            /* Duplicate the handle (windows makes this complicated). */
            int fdDup;
# ifdef KBUILD_OS_WINDOWS
            HANDLE hDup = INVALID_HANDLE_VALUE;
            if (!DuplicateHandle(hCurProc, hToSave, hCurProc, &hDup, 0 /* DesiredAccess */,
                                FALSE /*fInherit*/, DUPLICATE_SAME_ACCESS))
            {
                fprintf(*ppWorkingStdErr, "%s: DuplicateHandle(%#x) failed: %u\n", g_progname, hToSave, GetLastError());
                break;
            }
            fdDup = _open_osfhandle((intptr_t)hDup, pToSave->fSaved | _O_NOINHERIT);
            if (fdDup == -1)
            {
                fprintf(*ppWorkingStdErr, "%s: _open_osfhandle(%#x) failed: %u\n", g_progname, hDup, strerror(errno));
                CloseHandle(hDup);
                break;
            }
# else
            fdDup = dup(fdToSave);
            if (fdDup == -1)
            {
                fprintf(*ppWorkingStdErr, "%s: dup(%#x) failed: %u\n", g_progname, fdToSave, strerror(errno));
                break;
            }
#endif
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
 * Cleans up the file operation orders.
 *
 * This does not restore stuff, just closes handles we've opened for the guest.
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

#ifdef KBUILD_OS_WINDOWS
            if (mscDup3(paOrders[i].fdSaved, paOrders[i].fdTarget, paOrders[i].fSaved, *ppWorkingStdErr) != -1)
#else
            if (dup2(paOrders[i].fdSaved, paOrders[i].fdTarget) != -1)
#endif
            {
                close(paOrders[i].fdSaved);
                paOrders[i].fdSaved = -1;

                if (fRestoreStdErr)
                {
                    *ppWorkingStdErr = stderr;
                    assert(fileno(stderr) == paOrders[i].fdTarget);
                }
            }
#ifndef KBUILD_OS_WINDOWS
            else
                fprintf(*ppWorkingStdErr, "%s: dup2(%d,%d) failed: %s",
                        g_progname, paOrders[i].fdSaved, paOrders[i].fdTarget, strerror(errno));
#endif
        }

#ifndef KBUILD_OS_WINDOWS
        if (paOrders[i].fSaved != -1)
        {
            if (fcntl(paOrders[i].fdTarget, F_SETFD, paOrders[i].fSaved & FD_CLOEXEC) == -1)
                paOrders[i].fSaved = -1;
            else
                fprintf(*ppWorkingStdErr, "%s: fcntl(%d,F_SETFD,%s) failed: %s",
                        g_progname, paOrders[i].fdTarget, paOrders[i].fSaved & FD_CLOEXEC ? "FD_CLOEXEC" : "0", strerror(errno));
        }
#endif
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
# ifdef _MSC_VER
                if (j >= cOrders && !mscIsInheritable(fdTarget))
                    rcExit = 0;
# else
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
                            fprintf(*ppWorkingStdErr, "%s: fcntl(%d,F_SETFD,FD_CLOEXEC) failed: %s",
                                    g_progname, fdTarget, strerror(errno));
                    }
                    else if (errno == EBADF)
                        rcExit = 0;
                    else
                        fprintf(*ppWorkingStdErr, "%s: fcntl(%d,F_GETFD,0) failed: %s", g_progname, fdTarget, strerror(errno));
                }
# endif
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
                            fprintf(*ppWorkingStdErr, "%s: dup2(%d [%s],%d) failed: %s", g_progname, paOrders[i].fdSource,
                                    paOrders[i].pszFilename, paOrders[i].fdTarget, strerror(errno));
                        else
                            fprintf(*ppWorkingStdErr, "%s: dup2(%d,%d) failed: %s",
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


/**
 * Does the child spawning .
 *
 * @returns Exit code.
 * @param   pszExecutable       The child process executable.
 * @param   cArgs               Number of arguments.
 * @param   papszArgs           The child argument vector.
 * @param   fWatcomBrainDamage  Whether MSC need to do quoting according to
 *                              weird Watcom WCC rules.
 * @param   papszEnv            The child environment vector.
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
static int kRedirectDoSpawn(const char *pszExecutable, int cArgs, char **papszArgs, int fWatcomBrainDamage, char **papszEnv,
                            const char *pszCwd, const char *pszSavedCwd, unsigned cOrders, REDIRECTORDERS *paOrders,
#ifdef USE_POSIX_SPAWN
                            posix_spawn_file_actions_t *pFileActions,
#endif
                            unsigned cVerbosity,
#ifdef KMK
                            pid_t *pPidSpawned,
#endif
                            KBOOL *pfIsChildExitCode)
{
    int     rcExit;
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

        /*
         * Change working directory if so requested.
         */
        if (pszSavedCwd)
        {
            if (chdir(pszCwd) < 0)
                rcExit = errx(10, "Failed to change directory to '%s'", pszCwd);
        }
        if (rcExit == 0)
        {
#ifndef USE_POSIX_SPAWN
            /*
             * Execute the file orders.
             */
            FILE *pWorkingStdErr = NULL;
            rcExit = kRedirectExecFdOrders(cOrders, paOrders, &pWorkingStdErr);
            if (rcExit == 0)
#endif
            {
#ifdef KMK
                /*
                 * We're spawning from within kmk.
                 */
#if defined(KBUILD_OS_WINDOWS)
                /* Windows is slightly complicated due to handles and sub_proc.c. */
                HANDLE  hProcess = (HANDLE)_spawnvpe(_P_NOWAIT, pszExecutable, papszArgs, papszEnv);
                kRedirectRestoreFdOrders(cOrders, paOrders, &pWorkingStdErr);
                if ((intptr_t)hProcess != -1)
                {
                    if (process_kmk_register_redirect(hProcess, pPidSpawned) == 0)
                    {
                        if (cVerbosity > 0)
                            warnx("debug: spawned %d", *pPidSpawned);
                    }
                    else
                    {
                        DWORD dwTmp;
                        warn("sub_proc is out of slots, waiting for child...");
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
                else
                    rcExit = err(10, "_spawnvpe(%s) failed", pszExecutable);

# elif defined(KBUILD_OS_OS2)
                *pPidSpawned = _spawnve(_P_NOWAIT, pszExecutable, papszArgs, papszEnv);
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
#else
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
#endif

#else  /* !KMK */
                /*
                 * Spawning from inside the kmk_redirect executable.
                 */
# if defined(KBUILD_OS_WINDOWS) || defined(KBUILD_OS_OS2)
                errno  = 0;
                rcExit = (int)_spawnvpe(_P_WAIT, pszExecutable, papszArgs, papszEnv);
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

        /*
         * Restore the current directory.
         */
        if (pszSavedCwd)
        {
            if (chdir(pszSavedCwd) < 0)
                warn("Failed to restore directory to '%s'", pszSavedCwd);
        }
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
    char          **papszEnv           = NULL;
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
    papszEnv = pChild->environment;
    if (!papszEnv)
        pChild->environment = papszEnv = target_environment(pChild->file);
    cEnvVars = 0;
    while (papszEnv[cEnvVars] != NULL)
        cEnvVars++;
    cAllocatedEnvVars = cEnvVars;
#else
    /* We make a copy and we manage ourselves: */
    cEnvVars = 0;
    while (envp[cEnvVars] != NULL)
        cEnvVars++;

    cAllocatedEnvVars = cEnvVars + 4;
    papszEnv = malloc((cAllocatedEnvVars + 1) * sizeof(papszEnv));
    if (!papszEnv)
        return errx(9, "out of memory!");

    iEnvVar = cEnvVars;
    papszEnv[iEnvVar] = NULL;
    while (iEnvVar-- > 0)
    {
        papszEnv[iEnvVar] = strdup(envp[iEnvVar]);
        if (!papszEnv[iEnvVar])
        {
            while (iEnvVar-- > 0)
                free(papszEnv[iEnvVar]);
            free(papszEnv);
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
                /* '--' indicates where the bits to execute start. */
                if (*pszArg == '\0')
                {
                    iArg++;
                    break;
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
                        apszSavedLibPaths[ulVar] = calloc(1024 * 2);
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
                        rcExit = kBuiltinOptEnvSet(&papszEnv, &cEnvVars, &cAllocatedEnvVars, cVerbosity, pszValue);
#ifdef KMK
                        pChild->environment = papszEnv;
#endif
                    }
                    else
                    {
                        char *pszCopy = strdup(pszValue);
                        if (pszCopy)
                        {
                            pszCopy[pchEqual - pszValue] = '\0';
                            rcExit = kBuiltinOptEnvUnset(papszEnv, &cEnvVars, cVerbosity, pszCopy);
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
                    rcExit = kBuiltinOptEnvUnset(papszEnv, &cEnvVars, cVerbosity, pszValue);
                continue;
            }

            /*
             * Zap environment switch?
             */
            if (   chOpt == 'Z'
                || chOpt == 'i' /* GNU env compatibility. */ )
            {
                for (iEnvVar = 0; iEnvVar < cEnvVars; iEnvVar++)
                    free(papszEnv[iEnvVar]);
                papszEnv[0] = NULL;
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
                    if (fdOpened != fdSource)
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
        rcExit = kRedirectDoSpawn(pszExecutable, argc - iArg, &argv[iArg], fWatcomBrainDamage, papszEnv, szCwd, pszSavedCwd,
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
        free(papszEnv[iEnvVar]);
    free(papszEnv);
#endif
#ifdef KBUILD_OS_OS2
    for (ulLibPath = 0; ulLibPath < K_ELEMENTS(apszSavedLibPaths); ulLibPath++)
        if (apszSavedLibPaths[ulLibPath] != NULL)
        {
            APIRET rc = DosSetExtLIBPATH(apszSavedLibPaths[ulLibPath], ulLibPath);
            if (rc != 0)
                warnx("DosSetExtLIBPATH('%s',%u) failed with %u when restoring the original values!",
                      apszSavedLibPaths[ulLibPath], ulLibPath, rc);
            free(apszSavedLibPaths[ulLibPath])
        }
#endif

    return rcExit;
}

