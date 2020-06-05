/* $Id: kill.c 3352 2020-06-05 00:31:50Z knut.osmundsen@oracle.com $ */
/** @file
 * kmk kill - Process killer.
 */

/*
 * Copyright (c) 2007-2020 knut st. osmundsen <bird-kBuild-spamx@anduin.net>
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
#include <assert.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#ifdef WINDOWS32
# include <process.h>
# include <Windows.h>
# include <tlhelp32.h>
#endif

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif
#include "err.h"
#include "kmkbuiltin.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** @name kmkKillMatchName flags
 * @{ */
#define KMKKILL_FN_EXE_SUFF   1
#define KMKKILL_FN_WILDCARD   2
#define KMKKILL_FN_WITH_PATH  4
#define KMKKILL_FN_ROOT_SLASH 8
/** @} */


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
typedef struct KMKKILLGLOBALS
{
    PKMKBUILTINCTX  pCtx;
    int             cVerbosity;
    const char     *pszCur;
} KMKKILLGLOBALS;



/**
 * Kill one process by it's PID.
 *
 * The name is optional and only for messaging.
 */
static int kmkKillProcessByPid(KMKKILLGLOBALS *pThis, pid_t pid, const char *pszName)
{
    int rcExit;
    HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE /*bInherit*/, pid);
    if (!hProcess)
        rcExit = errx(pThis->pCtx, 1, "Failed to open pid %u (%#x%s%s): %u",
                      pid, pid, pszName ? ", " : "", pszName ? pszName : "", GetLastError());
    else
    {
        if (!TerminateProcess(hProcess, DBG_TERMINATE_PROCESS))
            rcExit = errx(pThis->pCtx, 1, "TerminateProcess failed on pid %u (%#x%s%s): %u",
                          pid, pid, pszName ? ", " : "", pszName ? pszName : "", GetLastError());
        else if (pThis->cVerbosity > 0)
            kmk_builtin_ctx_printf(pThis->pCtx, 0, "Terminated %u (%#x)%s%s\n",
                                   pid, pid, pszName ? " - " : "", pszName ? pszName : "");
        CloseHandle(hProcess);
    }
    return rcExit;
}


/**
 * Does simple wilcard filename matching.
 *
 * @returns 1 on match, 0 on mismatch.
 * @param   pszPattern          The pattern.
 * @param   cchPattern          The length of the pattern.
 * @param   pchFilename         The filename to match.  This does not have to be
 *                              terminated at @a cchFilename.
 * @param   cchFilename         The length of the filename that we should match.
 * @param   cDepth              The recursion depth.
 */
static int kmkKillMatchWildcard(const char *pszPattern, size_t cchPattern,
                                const char *pchFilename, size_t cchFilename, unsigned cDepth)
{
    while (cchPattern > 0 && cchFilename > 0)
    {
        char chPat = *pszPattern++;
        cchPattern--;
        if (chPat != '*')
        {
            if (chPat != '?')
            {
                char chExe = *pchFilename;
                if (   chExe != chPat
                    && tolower(chExe) != tolower(chPat))
                    return 0;
            }
            pchFilename++;
            cchFilename--;
        }
        else
        {
            size_t off, cchExeMin;

            while (cchPattern > 0 && *pszPattern == '*')
            {
                pszPattern++;
                cchPattern--;
            }

            /* Trailing '*'? */
            if (!cchPattern)
                return 1;

            /* Final wildcard? Match the tail. */
            if (memchr(pszPattern, '*', cchPattern) == NULL)
            {
                if (memchr(pszPattern, '?', cchPattern) == NULL)
                    return cchFilename >= cchPattern
                        && strnicmp(&pchFilename[cchFilename - cchPattern], pszPattern, cchPattern) == 0;

                /* Only '?', so we know the exact length of this '*' match and can do a single tail matching. */
                return cchFilename >= cchPattern
                    && kmkKillMatchWildcard(pszPattern, cchPattern, &pchFilename[cchFilename - cchPattern], cchPattern, cDepth + 1);
            }

            /*
             * More wildcards ('*'), so we need to need to try out all matching
             * length for this one.   We start by counting non-asterisks chars
             * in the remaining pattern so we know when to stop trying.
             * This must be at least 1 char.
             */
            if (cDepth >= 32)
                return 0;

            for (off = cchExeMin = 0; off < cchPattern; off++)
                cchExeMin += pszPattern[off] != '*';
            assert(cchExeMin > 0);

            while (cchFilename >= cchExeMin)
            {
                if (kmkKillMatchWildcard(pszPattern, cchPattern, pchFilename, cchFilename, cDepth + 1))
                    return 1;
                /* next */
                pchFilename++;
                cchFilename--;
            }
            return 0;
        }
    }

    /* If there is more filename left, we won't have a match. */
    if (cchFilename != 0)
        return 0;

    /* If there is pattern left, we still have a match if it's all asterisks. */
    while (cchPattern > 0 && *pszPattern == '*')
    {
        pszPattern++;
        cchPattern--;
    }
    return cchPattern == 0;
}


/**
 * Matches a process name against the given pattern.
 *
 * @returns 1 if it matches, 0 if it doesn't.
 * @param   pszPattern      The pattern to match against.
 * @param   cchPattern      The pattern length.
 * @param   fPattern        Pattern characteristics.
 * @param   pszExeFile      The EXE filename to match (includes path).
 * @param   cchExeFile      The length of the EXE filename.
 */
static int kmkKillMatchName(const char *pszPattern, size_t cchPattern, unsigned fPattern,
                            const char *pszExeFile, size_t cchExeFile)
{
    /*
     * Automatically match the exe suffix if not present in the pattern.
     */
    if (   !(fPattern & KMKKILL_FN_EXE_SUFF)
        && cchExeFile > 4
        && stricmp(&pszExeFile[cchExeFile - 4], ".exe") == 0)
        cchExeFile -= sizeof(".exe") - 1;

    /*
     * If no path in the pattern, move pszExeFile up to the filename part.
     */
    if (!(fPattern & KMKKILL_FN_WITH_PATH)
        && (   memchr(pszExeFile, '\\', cchExeFile) != NULL
            || memchr(pszExeFile, '/', cchExeFile)  != NULL
            || memchr(pszExeFile, ':', cchExeFile)  != NULL))
    {
        size_t  offFilename = cchExeFile;
        char    ch;
        while (   offFilename > 0
               && (ch = pszExeFile[offFilename - 1]) != '\\'
               && ch != '//'
               && ch != ':')
            offFilename--;
        cchExeFile -= offFilename;
        pszExeFile += offFilename;
    }

    /*
     * Wildcard?  This only works for the filename part.
     */
    if (fPattern & KMKKILL_FN_WILDCARD)
        return kmkKillMatchWildcard(pszPattern, cchPattern, pszExeFile, cchExeFile, 0);

    /*
     * No-wildcard pattern.
     */
    if (cchExeFile >= cchPattern)
    {
        if (strnicmp(&pszExeFile[cchExeFile - cchPattern], pszPattern, cchPattern) == 0)
            return cchExeFile == cchPattern
                || (fPattern & KMKKILL_FN_ROOT_SLASH)
                || pszExeFile[cchExeFile - cchPattern - 1] == '\\'
                || pszExeFile[cchExeFile - cchPattern - 1] == '/'
                || pszExeFile[cchExeFile - cchPattern - 1] == ':';

        /*
         * Might be slash directions or multiple consequtive slashes
         * making a difference here, so compare char-by-char from the end.
         */
        if (fPattern & KMKKILL_FN_WITH_PATH)
        {
            while (cchPattern > 0 && cchExeFile > 0)
            {
                char chPat = pszPattern[--cchPattern];
                char chExe = pszExeFile[--cchExeFile];
                if (chPat == chExe)
                {
                    if (chPat != '\\' && chPat != '/')
                    {
                        if (chPat != ':' || cchPattern > 0)
                            continue;
                        return 1;
                    }
                }
                else
                {
                    chPat = tolower(chPat);
                    chExe = tolower(chExe);
                    if (chPat == chExe)
                        continue;
                    if (chPat == '/')
                        chPat = '\\';
                    if (chExe == '/')
                        chExe = '\\';
                    if (chPat != chExe)
                        return 0;
                }

                while (cchPattern > 0 && ((chPat = pszPattern[cchPattern - 1] == '\\') || chPat == '/'))
                    cchPattern--;
                if (!cchPattern)
                    return 1;

                while (cchExeFile > 0 && ((chExe = pszExeFile[cchExeFile - 1] == '\\') || chExe == '/'))
                    cchExeFile--;
            }

            if (   cchExeFile == 0
                || pszExeFile[cchExeFile - 1] == '\\'
                || pszExeFile[cchExeFile - 1] == '/'
                || pszExeFile[cchExeFile - 1] == ':')
                return 1;
        }
    }
    return 0;
}


/**
 * Analyzes pattern for kmkKillMatchName().
 *
 * @returns Pattern characteristics.
 * @param   pszPattern          The pattern.
 * @param   cchPattern          The pattern length.
 */
static unsigned kmkKillAnalyzePattern(const char *pszPattern, size_t cchPattern)
{
    unsigned fPattern = 0;
    if (cchPattern > 4 && stricmp(&pszPattern[cchPattern - 4], ".exe") == 0)
        fPattern |= KMKKILL_FN_EXE_SUFF;
    if (memchr(pszPattern, '*', cchPattern) != NULL)
        fPattern |= KMKKILL_FN_WILDCARD;
    if (memchr(pszPattern, '?', cchPattern)  != NULL)
        fPattern |= KMKKILL_FN_WILDCARD;
    if (memchr(pszPattern, '/', cchPattern)  != NULL)
        fPattern |= KMKKILL_FN_WITH_PATH;
    if (memchr(pszPattern, '\\', cchPattern) != NULL)
        fPattern |= KMKKILL_FN_WITH_PATH;
    if (*pszPattern == '\\' || *pszPattern == '/')
        fPattern |= KMKKILL_FN_ROOT_SLASH;
    return fPattern;
}


/**
 * Enumerate processes and kill the ones matching the pattern.
 */
static int kmkKillProcessByName(KMKKILLGLOBALS *pThis, const char *pszPattern)
{
    HANDLE         hSnapshot;
    int            rcExit     = 0;
    size_t const   cchPattern = strlen(pszPattern);
    unsigned const fPattern   = kmkKillAnalyzePattern(pszPattern, cchPattern);
    if (cchPattern == 0)
        return errx(pThis->pCtx, 1, "Empty process name!");
    if ((fPattern & (KMKKILL_FN_WILDCARD | KMKKILL_FN_WITH_PATH)) == (KMKKILL_FN_WILDCARD | KMKKILL_FN_WITH_PATH))
        return errx(pThis->pCtx, 1, "Wildcard and paths cannot be mixed: %s", pszPattern);

    hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (!hSnapshot)
        rcExit = errx(pThis->pCtx, 1, "CreateToolhelp32Snapshot failed: %d", GetLastError());
    else
    {
        union
        {
            PROCESSENTRY32  Entry;
            char            achBuf[8192];
        } u;

        memset(&u, 0, sizeof(u));
        u.Entry.dwSize = sizeof(u) - sizeof(".exe");
        SetLastError(NO_ERROR);
        if (Process32First(hSnapshot, &u.Entry))
        {
            for (;;)
            {
                /* Kill it if it matches. */
                u.achBuf[sizeof(u.achBuf) - sizeof(".exe")] = '\0';
                if (   u.Entry.szExeFile[0] != '\0'
                    && kmkKillMatchName(pszPattern, cchPattern, fPattern, u.Entry.szExeFile, strlen(u.Entry.szExeFile)))
                {
                    int rcExit2 = kmkKillProcessByPid(pThis, u.Entry.th32ProcessID, u.Entry.szExeFile);
                    if (rcExit2 != 0 && rcExit == 0)
                        rcExit = rcExit2;
                }

                /* next */
                u.Entry.dwSize = sizeof(u) - sizeof(".exe");
                SetLastError(NO_ERROR);
                if (!Process32Next(hSnapshot, &u.Entry))
                {
                    if (GetLastError() != ERROR_NO_MORE_FILES)
                        rcExit = errx(pThis->pCtx, 1, "Process32Next failed: %d", GetLastError());
                    break;
                }
            }
        }
        else
            rcExit = errx(pThis->pCtx, 1, "Process32First failed: %d", GetLastError());
        CloseHandle(hSnapshot);
    }
    return rcExit;

}


/**
 * Worker for handling one command line process target.
 *
 * @returns 0 on success, non-zero exit code on failure.
 */
static int kmkKillProcess(KMKKILLGLOBALS *pThis, const char *pszTarget)
{
    /*
     * Try treat the target as a pid first, then a name pattern.
     */
    char *pszNext = NULL;
    long  pid;
    errno = 0;
    pid = strtol(pszTarget, &pszNext, 0);
    if (pszNext && *pszNext == '\0' && errno == 0)
        return kmkKillProcessByPid(pThis, pid, NULL);
    return kmkKillProcessByName(pThis, pszTarget);
}


/**
 * Worker for handling one command line job target.
 *
 * @returns 0 on success, non-zero exit code on failure.
 */
static int kmkKillJob(KMKKILLGLOBALS *pThis, const char *pszTarget)
{
    int    rcExit   = 0;
    HANDLE hTempJob = NULL;
    BOOL   fIsInJob = FALSE;

    /*
     * Open the job object.
     */
    DWORD  fRights  = JOB_OBJECT_QUERY | JOB_OBJECT_TERMINATE | JOB_OBJECT_ASSIGN_PROCESS;
    HANDLE hJob     = OpenJobObjectA(fRights, FALSE /*bInheritHandle*/, pszTarget);
    if (!hJob)
    {
        fRights &= ~JOB_OBJECT_ASSIGN_PROCESS;
        hJob = OpenJobObjectA(fRights, FALSE /*bInheritHandle*/, pszTarget);
        if (!hJob)
            return errx(pThis->pCtx, 1, "Failed to open job '%s': %u", pszTarget, GetLastError());
    }

    /*
     * Are we a member of this job?  If so, temporarily move
     * to a different object so we don't kill ourselves.
     */
    if (IsProcessInJob(hJob, GetCurrentProcess(), &fIsInJob))
    {
        if (fIsInJob)
        {
            /** @todo this probably isn't working. */
            if (pThis->cVerbosity >= 1)
                kmk_builtin_ctx_printf(pThis->pCtx, 0,
                                       "kmk_kill: Is myself (%u) a member of target job (%s)", getpid(), pszTarget);
            if (!(fRights & JOB_OBJECT_ASSIGN_PROCESS))
                rcExit = errx(pThis->pCtx, 1,
                              "Is myself member of the target job and don't have the JOB_OBJECT_ASSIGN_PROCESS right.");
            else
            {
                hTempJob = CreateJobObjectA(NULL, NULL);
                if (hTempJob)
                {
                    if (!(AssignProcessToJobObject(hTempJob, GetCurrentProcess())))
                        rcExit = errx(pThis->pCtx, 1, "AssignProcessToJobObject(temp, me) failed: %u", GetLastError());
                }
            }
        }
    }
    else
        rcExit = errx(pThis->pCtx, 1, "IsProcessInJob(%s, me) failed: %u", pszTarget, GetLastError());

    /*
     * Do the killing.
     */
    if (rcExit == 0)
    {
        if (!TerminateJobObject(hJob, DBG_TERMINATE_PROCESS))
            rcExit = errx(pThis->pCtx, 1, "TerminateJobObject(%s) failed: %u", pszTarget, GetLastError());
    }

    /*
     * Cleanup.
     */
    if (hTempJob)
    {
        if (!(AssignProcessToJobObject(hJob, GetCurrentProcess())))
            rcExit = errx(pThis->pCtx, 1, "AssignProcessToJobObject(%s, me) failed: %u", pszTarget, GetLastError());
        CloseHandle(hTempJob);
    }
    CloseHandle(hJob);

    return rcExit;
}


/**
 * Show usage.
 */
static void kmkKillUsage(PKMKBUILTINCTX pCtx, int fIsErr)
{
    kmk_builtin_ctx_printf(pCtx, fIsErr,
                           "usage: %s [options] <job-name|process-name|pid> [options] [...]\n"
                           "   or: %s --help\n"
                           "   or: %s --version\n"
                           "\n"
                           "Options that decide what to kill next:\n"
                           "  -p|--process  Processes, either by name or by PID. (default)\n"
                           "  -j|--job      Windows jobs.\n"
                           "\n"
                           "Other options:\n"
                           "  -q|--quiet    Quiet operation. Only real errors are displayed.\n"
                           "  -v|--verbose  Increase verbosity.\n"
                           ,
                           pCtx->pszProgName, pCtx->pszProgName, pCtx->pszProgName);
}


int kmk_builtin_kill(int argc, char **argv, char **envp, PKMKBUILTINCTX pCtx)
{
    int             rcExit = 0;
    int             i;
    KMKKILLGLOBALS  This;
    enum { kTargetJobs, kTargetProcesses } enmTarget   = kTargetProcesses;

    /* Globals. */
    This.pCtx       = pCtx;
    This.cVerbosity = 1;

    /*
     * Parse arguments.
     */
    if (argc <= 1)
    {
        kmkKillUsage(pCtx, 0);
        return 1;
    }
    for (i = 1; i < argc; i++)
    {
        if (argv[i][0] == '-')
        {
            const char *pszValue;
            const char *psz = &argv[i][1];
            char chOpt;
            chOpt = *psz++;
            if (chOpt == '-')
            {
                /* Convert long to short option. */
                if (!strcmp(psz, "job"))
                    chOpt = 'j';
                else if (!strcmp(psz, "process"))
                    chOpt = 'p';
                else if (!strcmp(psz, "quiet"))
                    chOpt = 'q';
                else if (!strcmp(psz, "verbose"))
                    chOpt = 'v';
                else if (!strcmp(psz, "help"))
                    chOpt = '?';
                else if (!strcmp(psz, "version"))
                    chOpt = 'V';
                else
                {
                    errx(pCtx, 2, "Invalid argument '%s'.", argv[i]);
                    kmkKillUsage(pCtx, 1);
                    return 2;
                }
                psz = "";
            }

            /*
             * Requires value?
             */
            switch (chOpt)
            {
                /*case '':
                    if (*psz)
                        pszValue = psz;
                    else if (++i < argc)
                        pszValue = argv[i];
                    else
                        return errx(pCtx, 2, "The '-%c' option takes a value.", chOpt);
                    break;*/

                default:
                    pszValue = NULL;
                    break;
            }


            switch (chOpt)
            {
                /*
                 * What to kill
                 */
                case 'j':
                    enmTarget = kTargetJobs;
                    break;

                case 'p':
                    enmTarget = kTargetProcesses;
                    break;

                /*
                 * How to kill processes...
                 */


                /*
                 * Noise level.
                 */
                case 'q':
                    This.cVerbosity = 0;
                    break;

                case 'v':
                    This.cVerbosity += 1;
                    break;

                /*
                 * The mandatory version & help.
                 */
                case '?':
                    kmkKillUsage(pCtx, 0);
                    return rcExit;
                case 'V':
                    return kbuild_version(argv[0]);

                /*
                 * Invalid argument.
                 */
                default:
                    errx(pCtx, 2, "Invalid argument '%s'.", argv[i]);
                    kmkKillUsage(pCtx, 1);
                    return 2;
            }
        }
        else
        {
            /*
             * Kill something.
             */
            int rcExitOne;
            This.pszCur = argv[i];
            if (enmTarget == kTargetJobs)
                rcExitOne = kmkKillJob(&This, argv[i]);
            else
                rcExitOne = kmkKillProcess(&This, argv[i]);
            if (rcExitOne != 0 && rcExit == 0)
                rcExit = rcExitOne;
        }
    }

    return rcExit;
}

#ifdef KMK_BUILTIN_STANDALONE
int main(int argc, char **argv, char **envp)
{
    KMKBUILTINCTX Ctx = { "kmk_kill", NULL };
    return kmk_builtin_kill(argc, argv, envp, &Ctx);
}
#endif

