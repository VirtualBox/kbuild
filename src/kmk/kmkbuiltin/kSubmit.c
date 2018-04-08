/* $Id: kSubmit.c 3224 2018-04-08 15:49:07Z knut.osmundsen@oracle.com $ */
/** @file
 * kMk Builtin command - submit job to a kWorker.
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
#include "makeint.h"
#include "job.h"
#include "variable.h"
#include "pathstuff.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#ifdef HAVE_ALLOCA_H
# include <alloca.h>
#endif
#if defined(_MSC_VER)
# include <ctype.h>
# include <io.h>
# include <direct.h>
# include <process.h>
#else
# include <unistd.h>
#endif
#ifdef KBUILD_OS_WINDOWS
# ifndef CONFIG_NEW_WIN_CHILDREN
#  include "sub_proc.h"
# else
#  include "../w32/winchildren.h"
# endif
# include "nt/nt_child_inject_standard_handles.h"
#endif

#include "kbuild.h"
#include "kmkbuiltin.h"
#include "err.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** Hashes a pid. */
#define KWORKER_PID_HASH(a_pid) ((size_t)(a_pid) % 61)

#define TUPLE(a_sz)     a_sz, sizeof(a_sz) - 1


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
typedef struct WORKERINSTANCE *PWORKERINSTANCE;
typedef struct WORKERINSTANCE
{
    /** Pointer to the next worker instance. */
    PWORKERINSTANCE         pNext;
    /** Pointer to the previous worker instance. */
    PWORKERINSTANCE         pPrev;
    /** Pointer to the next worker with the same pid hash slot. */
    PWORKERINSTANCE         pNextPidHash;
    /** 32 or 64. */
    unsigned                cBits;
    /** The process ID of the kWorker process. */
    pid_t                   pid;
    union
    {
        struct
        {
            /** The exit code. */
            int32_t         rcExit;
            /** Set to 1 if the worker is exiting. */
            uint8_t         bWorkerExiting;
            uint8_t         abUnused[3];
        } s;
        uint8_t             ab[8];
    } Result;
    /** Number of result bytes read alread.  */
    size_t                  cbResultRead;

#ifdef KBUILD_OS_WINDOWS
    /** The process handle. */
    HANDLE                  hProcess;
    /** The bi-directional pipe we use to talk to the kWorker process. */
    HANDLE                  hPipe;
    /** For overlapped read (have valid event semaphore). */
    OVERLAPPED              OverlappedRead;
# ifdef CONFIG_NEW_WIN_CHILDREN
    /** Standard output catcher (reused). */
    PWINCCWPIPE             pStdOut;
    /** Standard error catcher (reused). */
    PWINCCWPIPE             pStdErr;
# endif
#else
    /** The socket descriptor we use to talk to the kWorker process. */
    int                     fdSocket;
#endif

    /** What it's busy with.  NULL if idle. */
    struct child           *pBusyWith;
} WORKERINSTANCE;


typedef struct WORKERLIST
{
    /** The head of the list.  NULL if empty. */
    PWORKERINSTANCE         pHead;
    /** The tail of the list.  NULL if empty. */
    PWORKERINSTANCE         pTail;
    /** Number of list entries. */
    size_t                  cEntries;
} WORKERLIST;
typedef WORKERLIST *PWORKERLIST;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** List of idle worker.*/
static WORKERLIST           g_IdleList;
/** List of busy workers. */
static WORKERLIST           g_BusyList;
/** PID hash table for the workers.
 * @sa KWORKER_PID_HASH() */
static PWORKERINSTANCE      g_apPidHash[61];

#ifdef KBUILD_OS_WINDOWS
/** For naming the pipes.
 * Also indicates how many worker instances we've spawned. */
static unsigned             g_uWorkerSeqNo = 0;
#endif
/** Set if we've registred the atexit handler already. */
static int                  g_fAtExitRegistered = 0;

/** @var g_cArchBits
 * The bit count of the architecture this binary is compiled for. */
/** @var g_szArch
 * The name of the architecture this binary is compiled for. */
/** @var g_cArchBits
 * The bit count of the alternative architecture. */
/** @var g_szAltArch
 * The name of the alternative architecture. */
#if defined(KBUILD_ARCH_AMD64)
static unsigned             g_cArchBits    = 64;
static char const           g_szArch[]     = "amd64";
static unsigned             g_cAltArchBits = 32;
static char const           g_szAltArch[]  = "x86";
#elif defined(KBUILD_ARCH_X86)
static unsigned             g_cArchBits    = 32;
static char const           g_szArch[]     = "x86";
static unsigned             g_cAltArchBits = 64;
static char const           g_szAltArch[]  = "amd64";
#else
# error "Port me!"
#endif

#ifdef KBUILD_OS_WINDOWS
/** Pointer to kernel32!SetThreadGroupAffinity. */
static BOOL (WINAPI *g_pfnSetThreadGroupAffinity)(HANDLE, const GROUP_AFFINITY*, GROUP_AFFINITY *);
#endif



/**
 * Unlinks a worker instance from a list.
 *
 * @param   pList               The list.
 * @param   pWorker             The worker.
 */
static void kSubmitListUnlink(PWORKERLIST pList, PWORKERINSTANCE pWorker)
{
    PWORKERINSTANCE pNext = pWorker->pNext;
    PWORKERINSTANCE pPrev = pWorker->pPrev;

    if (pNext)
    {
        assert(pNext->pPrev == pWorker);
        pNext->pPrev = pPrev;
    }
    else
    {
        assert(pList->pTail == pWorker);
        pList->pTail = pPrev;
    }

    if (pPrev)
    {
        assert(pPrev->pNext == pWorker);
        pPrev->pNext = pNext;
    }
    else
    {
        assert(pList->pHead == pWorker);
        pList->pHead = pNext;
    }

    assert(!pList->pHead || pList->pHead->pPrev == NULL);
    assert(!pList->pTail || pList->pTail->pNext == NULL);

    assert(pList->cEntries > 0);
    pList->cEntries--;

    pWorker->pNext = NULL;
    pWorker->pPrev = NULL;
}


/**
 * Appends a worker instance to the tail of a list.
 *
 * @param   pList               The list.
 * @param   pWorker             The worker.
 */
static void kSubmitListAppend(PWORKERLIST pList, PWORKERINSTANCE pWorker)
{
    PWORKERINSTANCE pTail = pList->pTail;

    assert(pTail != pWorker);
    assert(pList->pHead != pWorker);

    pWorker->pNext = NULL;
    pWorker->pPrev = pTail;
    if (pTail != NULL)
    {
        assert(pTail->pNext == NULL);
        pTail->pNext = pWorker;
    }
    else
    {
        assert(pList->pHead == NULL);
        pList->pHead = pWorker;
    }
    pList->pTail = pWorker;

    assert(pList->pHead->pPrev == NULL);
    assert(pList->pTail->pNext == NULL);

    pList->cEntries++;
}


/**
 * Remove worker from the process ID hash table.
 *
 * @param   pWorker             The worker.
 */
static void kSubmitPidHashRemove(PWORKERINSTANCE pWorker)
{
    size_t idxHash = KWORKER_PID_HASH(pWorker->pid);
    if (g_apPidHash[idxHash] == pWorker)
        g_apPidHash[idxHash] = pWorker->pNext;
    else
    {
        PWORKERINSTANCE pPrev = g_apPidHash[idxHash];
        while (pPrev && pPrev->pNext != pWorker)
            pPrev = pPrev->pNext;
        assert(pPrev != NULL);
        if (pPrev)
            pPrev->pNext = pWorker->pNext;
    }
    pWorker->pid = -1;
}


/**
 * Looks up a worker by its process ID.
 *
 * @returns Pointer to the worker instance if found. NULL if not.
 * @param   pid                 The process ID of the worker.
 */
static PWORKERINSTANCE kSubmitFindWorkerByPid(pid_t pid)
{
    PWORKERINSTANCE pWorker = g_apPidHash[KWORKER_PID_HASH(pid)];
    while (pWorker && pWorker->pid != pid)
        pWorker = pWorker->pNextPidHash;
    return pWorker;
}


/**
 * Calcs the path to the kWorker binary for the worker.
 *
 * @returns
 * @param   pCtx            The command execution context.
 * @param   pWorker         The worker (for its bitcount).
 * @param   pszExecutable   The output buffer.
 * @param   cbExecutable    The output buffer size.
 */
static int kSubmitCalcExecutablePath(PKMKBUILTINCTX pCtx, PWORKERINSTANCE pWorker, char *pszExecutable, size_t cbExecutable)
{
#if defined(KBUILD_OS_WINDOWS) || defined(KBUILD_OS_OS2)
    static const char   s_szWorkerName[] = "kWorker.exe";
#else
    static const char   s_szWorkerName[] = "kWorker";
#endif
    const char         *pszBinPath = get_kbuild_bin_path();
    size_t const        cchBinPath = strlen(pszBinPath);
    size_t              cchExecutable;
    if (   pWorker->cBits == g_cArchBits
        ?  cchBinPath + 1 + sizeof(s_szWorkerName) <= cbExecutable
        :  cchBinPath + 1 - sizeof(g_szArch) + sizeof(g_szAltArch) + sizeof(s_szWorkerName) <= cbExecutable )
    {
        memcpy(pszExecutable, pszBinPath, cchBinPath);
        cchExecutable = cchBinPath;

        /* Replace the arch bin directory extension with the alternative one if requested. */
        if (pWorker->cBits != g_cArchBits)
        {
            if (   cchBinPath < sizeof(g_szArch)
                || memcmp(&pszExecutable[cchBinPath - sizeof(g_szArch) + 1], g_szArch, sizeof(g_szArch) - 1) != 0)
                return errx(pCtx, 1, "KBUILD_BIN_PATH does not end with main architecture (%s) as expected: %s",
                            pszBinPath, g_szArch);
            cchExecutable -= sizeof(g_szArch) - 1;
            memcpy(&pszExecutable[cchExecutable], g_szAltArch, sizeof(g_szAltArch) - 1);
            cchExecutable += sizeof(g_szAltArch) - 1;
        }

        /* Append a slash and the worker name. */
        pszExecutable[cchExecutable++] = '/';
        memcpy(&pszExecutable[cchExecutable], s_szWorkerName, sizeof(s_szWorkerName));
        return 0;
    }
    return errx(pCtx, 1, "KBUILD_BIN_PATH is too long");
}


#ifdef KBUILD_OS_WINDOWS
/**
 * Calcs the UTF-16 path to the kWorker binary for the worker.
 *
 * @returns
 * @param   pCtx            The command execution context.
 * @param   pWorker         The worker (for its bitcount).
 * @param   pwszExecutable  The output buffer.
 * @param   cwcExecutable   The output buffer size.
 */
static int kSubmitCalcExecutablePathW(PKMKBUILTINCTX pCtx, PWORKERINSTANCE pWorker, wchar_t *pwszExecutable, size_t cwcExecutable)
{
    char szExecutable[MAX_PATH];
    int rc = kSubmitCalcExecutablePath(pCtx, pWorker, szExecutable, sizeof(szExecutable));
    if (rc == 0)
    {
        int cwc = MultiByteToWideChar(CP_ACP, 0 /*fFlags*/, szExecutable, strlen(szExecutable) + 1,
                                      pwszExecutable, cwcExecutable);
        if (cwc > 0)
            return 0;
        return errx(pCtx, 1, "MultiByteToWideChar failed on '%s': %u", szExecutable, GetLastError());
    }
    return rc;
}
#endif


/**
 * Creates a new worker process.
 *
 * @returns 0 on success, non-zero value on failure.
 * @param   pCtx                The command execution context.
 * @param   pWorker             The worker structure.  Caller does the linking
 *                              (as we might be reusing an existing worker
 *                              instance because a worker shut itself down due
 *                              to high resource leak level).
 * @param   cVerbosity          The verbosity level.
 */
static int kSubmitSpawnWorker(PKMKBUILTINCTX pCtx, PWORKERINSTANCE pWorker, int cVerbosity)
{
    int rc;
#ifdef KBUILD_OS_WINDOWS
    wchar_t wszExecutable[MAX_PATH];
#else
    PATH_VAR(szExecutable);
#endif

    /*
     * Get the output path so it can be passed on as a volatile.
     */
    const char      *pszVarVolatile;
    struct variable *pVarVolatile = lookup_variable(TUPLE("PATH_OUT"));
    if (pVarVolatile)
        pszVarVolatile = "PATH_OUT";
    else
    {
        pVarVolatile = lookup_variable(TUPLE("PATH_OUT_BASE"));
        if (pVarVolatile)
            pszVarVolatile = "PATH_OUT_BASE";
        else
            warn(pCtx, "Neither PATH_OUT_BASE nor PATH_OUT was found.");
    }
    if (pVarVolatile && strchr(pVarVolatile->value, '"'))
        return errx(pCtx, -1, "%s contains double quotes.", pszVarVolatile);
    if (pVarVolatile && strlen(pVarVolatile->value) >= GET_PATH_MAX)
        return errx(pCtx, -1, "%s is too long (max %u)", pszVarVolatile, GET_PATH_MAX);

    /*
     * Construct the executable path.
     */
#ifdef KBUILD_OS_WINDOWS
    rc = kSubmitCalcExecutablePathW(pCtx, pWorker, wszExecutable, K_ELEMENTS(wszExecutable));
#else
    rc = kSubmitCalcExecutablePath(pCtx, pWorker, szExecutable, GET_PATH_MAX);
#endif
    if (rc == 0)
    {
#ifdef KBUILD_OS_WINDOWS
        static DWORD        s_fDenyRemoteClients = ~(DWORD)0;
        wchar_t             wszPipeName[128];
        HANDLE              hWorkerPipe;
        int                 iProcessorGroup = -1; /** @todo determine process group. */

        /*
         * Create the bi-directional pipe with overlapping I/O enabled.
         */
        if (s_fDenyRemoteClients == ~(DWORD)0)
            s_fDenyRemoteClients = GetVersion() >= 0x60000 ? PIPE_REJECT_REMOTE_CLIENTS : 0;
        _snwprintf(wszPipeName, sizeof(wszPipeName), L"\\\\.\\pipe\\kmk-%u-kWorker-%u-%u",
                   GetCurrentProcessId(), g_uWorkerSeqNo++, GetTickCount());
        hWorkerPipe = CreateNamedPipeW(wszPipeName,
                                       PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE /* win2k sp2+ */,
                                       PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | s_fDenyRemoteClients,
                                       1 /* cMaxInstances */,
                                       64 /*cbOutBuffer*/,
                                       65536 /*cbInBuffer*/,
                                       0 /*cMsDefaultTimeout -> 50ms*/,
                                       NULL /* pSecAttr - no inherit */);
        if (hWorkerPipe != INVALID_HANDLE_VALUE)
        {
            pWorker->hPipe = CreateFileW(wszPipeName,
                                         GENERIC_READ | GENERIC_WRITE,
                                         0 /* dwShareMode - no sharing */,
                                         NULL /*pSecAttr - no inherit */,
                                         OPEN_EXISTING,
                                         FILE_FLAG_OVERLAPPED,
                                         NULL /*hTemplate*/);
            if (pWorker->hPipe != INVALID_HANDLE_VALUE)
            {
                pWorker->OverlappedRead.hEvent = CreateEventW(NULL /*pSecAttrs - no inherit*/, TRUE /*bManualReset*/,
                                                              TRUE /*bInitialState*/, NULL /*pwszName*/);
                if (pWorker->OverlappedRead.hEvent != NULL)
                {
                    extern int          process_priority; /* main.c */
                    wchar_t             wszCommandLine[MAX_PATH * 3];
                    wchar_t            *pwszDst = wszCommandLine;
                    size_t              cwcDst = K_ELEMENTS(wszCommandLine);
                    int                 cwc;
                    DWORD               fFlags;
                    STARTUPINFOW        StartupInfo;
                    PROCESS_INFORMATION ProcInfo = { NULL, NULL, 0, 0 };

                    /*
                     * Compose the command line.
                     */
                    cwc = _snwprintf(pwszDst, cwcDst, L"\"%s\" ", wszExecutable);
                    assert(cwc > 0 && cwc < cwcDst);
                    pwszDst += cwc;
                    cwcDst  -= cwc;
                    if (pVarVolatile && *pVarVolatile->value)
                    {
                        char chEnd = strchr(pVarVolatile->value, '\0')[-1];
                        if (chEnd == '\\')
                            cwc = _snwprintf(pwszDst, cwcDst, L" --volatile \"%S.\"", pVarVolatile->value);
                        else
                            cwc = _snwprintf(pwszDst, cwcDst, L" --volatile \"%S\"", pVarVolatile->value);
                        assert(cwc > 0 && cwc < cwcDst);
                        pwszDst += cwc;
                        cwcDst  -= cwc;
                    }
                    *pwszDst = '\0';

                    /*
                     * Fill in the startup information.
                     */
                    memset(&StartupInfo, 0, sizeof(StartupInfo));
                    StartupInfo.cb = sizeof(StartupInfo);
                    GetStartupInfoW(&StartupInfo);
                    StartupInfo.dwFlags    &= ~STARTF_USESTDHANDLES;
                    StartupInfo.lpReserved2 = NULL;
                    StartupInfo.cbReserved2 = 0;

                    /*
                     * Flags and such.
                     */
                    fFlags = CREATE_SUSPENDED;
                    switch (process_priority)
                    {
                        case 1: fFlags |= CREATE_SUSPENDED | IDLE_PRIORITY_CLASS; break;
                        case 2: fFlags |= CREATE_SUSPENDED | BELOW_NORMAL_PRIORITY_CLASS; break;
                        case 3: fFlags |= CREATE_SUSPENDED | NORMAL_PRIORITY_CLASS; break;
                        case 4: fFlags |= CREATE_SUSPENDED | HIGH_PRIORITY_CLASS; break;
                        case 5: fFlags |= CREATE_SUSPENDED | REALTIME_PRIORITY_CLASS; break;
                    }

                    /*
                     * Create the worker process.
                     */
                    if (CreateProcessW(wszExecutable, wszCommandLine, NULL /*pProcSecAttr*/, NULL /*pThreadSecAttr*/,
                                       FALSE /*fInheritHandles*/, fFlags, NULL /*pwszzEnvironment*/,
                                       NULL /*pwszCwd*/, &StartupInfo, &ProcInfo))
                    {
                        char   szErrMsg[256];
                        BOOL   afReplace[3] = { TRUE, FALSE, FALSE };
                        HANDLE ahReplace[3] = { hWorkerPipe, INVALID_HANDLE_VALUE, INVALID_HANDLE_VALUE };
                        if (pWorker->pStdOut)
                        {
                            afReplace[1] = TRUE;
                            afReplace[2] = TRUE;
                            ahReplace[1] = pWorker->pStdOut->hPipeChild;
                            ahReplace[2] = pWorker->pStdErr->hPipeChild;
                        }

                        rc = nt_child_inject_standard_handles(ProcInfo.hProcess, afReplace, ahReplace, szErrMsg, sizeof(szErrMsg));
                        if (rc == 0)
                        {
                            BOOL fRet;
                            switch (process_priority)
                            {
                                case 1: fRet = SetThreadPriority(ProcInfo.hThread, THREAD_PRIORITY_IDLE); break;
                                case 2: fRet = SetThreadPriority(ProcInfo.hThread, THREAD_PRIORITY_BELOW_NORMAL); break;
                                case 3: fRet = SetThreadPriority(ProcInfo.hThread, THREAD_PRIORITY_NORMAL); break;
                                case 4: fRet = SetThreadPriority(ProcInfo.hThread, THREAD_PRIORITY_HIGHEST); break;
                                case 5: fRet = SetThreadPriority(ProcInfo.hThread, THREAD_PRIORITY_TIME_CRITICAL); break;
                                default: fRet = TRUE;
                            }
                            if (!fRet)
                                warnx(pCtx, "warning: failed to set kWorker thread priority: %u\n", GetLastError());

                            if (iProcessorGroup >= 0)
                            {
                                GROUP_AFFINITY NewAff = { ~(uintptr_t)0, (WORD)iProcessorGroup, 0, 0, 0 };
                                GROUP_AFFINITY OldAff = {             0,                     0, 0, 0, 0 };
                                if (!g_pfnSetThreadGroupAffinity(ProcInfo.hThread, &NewAff, &OldAff))
                                    warnx(pCtx, "warning: Failed to set processor group to %d: %u\n",
                                          iProcessorGroup, GetLastError());
                            }

                            /*
                             * Now, we just need to resume the thread.
                             */
                            if (ResumeThread(ProcInfo.hThread))
                            {
                                CloseHandle(hWorkerPipe);
                                CloseHandle(ProcInfo.hThread);
                                pWorker->pid      = ProcInfo.dwProcessId;
                                pWorker->hProcess = ProcInfo.hProcess;
                                if (cVerbosity > 0)
                                    warnx(pCtx, "created %d bit worker %d\n", pWorker->cBits, pWorker->pid);
                                return 0;
                            }

                            /*
                             * Failed, bail out.
                             */
                            rc = errx(pCtx, -3, "ResumeThread failed: %u", GetLastError());
                        }
                        else
                            rc = errx(pCtx, -3, "%s", szErrMsg);
                        TerminateProcess(ProcInfo.hProcess, 1234);
                        CloseHandle(ProcInfo.hThread);
                        CloseHandle(ProcInfo.hProcess);
                    }
                    else
                        rc = errx(pCtx, -2, "CreateProcessW failed: %u (exe=%S cmdline=%S)",
                                  GetLastError(), wszExecutable, wszCommandLine);
                    CloseHandle(pWorker->OverlappedRead.hEvent);
                    pWorker->OverlappedRead.hEvent = INVALID_HANDLE_VALUE;
                }
                else
                    rc = errx(pCtx, -1, "CreateEventW failed: %u", GetLastError());
                CloseHandle(pWorker->hPipe);
                pWorker->hPipe = INVALID_HANDLE_VALUE;
            }
            else
                rc = errx(pCtx, -1, "Opening named pipe failed: %u", GetLastError());
            CloseHandle(hWorkerPipe);
        }
        else
            rc = errx(pCtx, -1, "CreateNamedPipeW failed: %u", GetLastError());

#else
        /*
         * Create a socket pair.
         */
        int aiPair[2] = { -1, -1 };
        if (socketpair(AF_LOCAL, SOCK_STREAM, 0, aiPair) == 0)
        {
            pWorker->fdSocket = aiPair[1];

            rc = -1;
        }
        else
            rc = err(pCtx, -1, "socketpair");
#endif
    }
    else
        rc = errx(pCtx, -1, "KBUILD_BIN_PATH is too long");
    return rc;
}


/**
 * Selects an idle worker or spawns a new one.
 *
 * @returns Pointer to the selected worker instance.  NULL on error.
 * @param   pCtx                The command execution context.
 * @param   pWorker             The idle worker instance to respawn.
 *                              On failure this will be freed!
 * @param   cBitsWorker         The worker bitness - 64 or 32.
 */
static int kSubmitRespawnWorker(PKMKBUILTINCTX pCtx, PWORKERINSTANCE pWorker, int cVerbosity)
{
    /*
     * Clean up after the old worker.
     */
#ifdef KBUILD_OS_WINDOWS
    DWORD   rcWait;

    /* Close the pipe handle first, breaking the pipe in case it's not already
       busted up.  Close the event semaphore too before waiting for the process. */
    if (pWorker->hPipe != INVALID_HANDLE_VALUE)
    {
        if (!CloseHandle(pWorker->hPipe))
            warnx(pCtx, "CloseHandle(pWorker->hPipe): %u", GetLastError());
        pWorker->hPipe = INVALID_HANDLE_VALUE;
    }

    if (!CloseHandle(pWorker->OverlappedRead.hEvent))
        warnx(pCtx, "CloseHandle(pWorker->OverlappedRead.hEvent): %u", GetLastError());
    pWorker->OverlappedRead.hEvent = INVALID_HANDLE_VALUE;

    if (pWorker->pStdOut)
        MkWinChildcareWorkerDrainPipes(NULL, pWorker->pStdOut, pWorker->pStdErr);

    /* It's probably shutdown already, if not give it 10 milliseconds before
       we terminate it forcefully. */
    rcWait = WaitForSingleObject(pWorker->hProcess, 10);
    if (rcWait != WAIT_OBJECT_0)
    {
        BOOL fRc = TerminateProcess(pWorker->hProcess, 127);

        if (pWorker->pStdOut)
            MkWinChildcareWorkerDrainPipes(NULL, pWorker->pStdOut, pWorker->pStdErr);

        rcWait = WaitForSingleObject(pWorker->hProcess, 100);
        if (rcWait != WAIT_OBJECT_0)
            warnx(pCtx, "WaitForSingleObject returns %u (and TerminateProcess %d)", rcWait, fRc);
    }

    if (pWorker->pStdOut)
        MkWinChildcareWorkerDrainPipes(NULL, pWorker->pStdOut, pWorker->pStdErr);

    if (!CloseHandle(pWorker->hProcess))
        warnx(pCtx, "CloseHandle(pWorker->hProcess): %u", GetLastError());
    pWorker->hProcess = INVALID_HANDLE_VALUE;

#else
    pid_t   pidWait;
    int     rc;

    if (pWorker->fdSocket != -1)
    {
        if (close(pWorker->fdSocket) != 0)
            warn(pCtx, "close(pWorker->fdSocket)");
        pWorker->fdSocket = -1;
    }

    kill(pWorker->pid, SIGTERM);
    pidWait = waitpid(pWorker->pid, &rc, 0);
    if (pidWait != pWorker->pid)
        warn(pCtx, "waitpid(pWorker->pid,,0)");
#endif

    /*
     * Unlink it from the hash table.
     */
    kSubmitPidHashRemove(pWorker);

    /*
     * Respawn it.
     */
    if (kSubmitSpawnWorker(pCtx, pWorker, cVerbosity) == 0)
    {
        /*
         * Insert it into the process ID hash table and idle list.
         */
        size_t idxHash = KWORKER_PID_HASH(pWorker->pid);
        pWorker->pNextPidHash = g_apPidHash[idxHash];
        g_apPidHash[idxHash] = pWorker;
        return 0;
    }

    kSubmitListUnlink(&g_IdleList, pWorker);
    free(pWorker);
    return -1;
}


/**
 * Selects an idle worker or spawns a new one.
 *
 * @returns Pointer to the selected worker instance.  NULL on error.
 * @param   cBitsWorker         The worker bitness - 64 or 32.
 */
static PWORKERINSTANCE kSubmitSelectWorkSpawnNewIfNecessary(PKMKBUILTINCTX pCtx, unsigned cBitsWorker, int cVerbosity)
{
    /*
     * Lookup up an idle worker.
     */
    PWORKERINSTANCE pWorker = g_IdleList.pHead;
    while (pWorker)
    {
        if (pWorker->cBits == cBitsWorker)
            return pWorker;
        pWorker = pWorker->pNext;
    }

    /*
     * Create a new worker instance.
     */
    pWorker = (PWORKERINSTANCE)xcalloc(sizeof(*pWorker));
    pWorker->cBits = cBitsWorker;
#if defined(CONFIG_NEW_WIN_CHILDREN) && defined(KBUILD_OS_WINDOWS)
    if (output_sync != OUTPUT_SYNC_NONE)
    {
        pWorker->pStdOut = MkWinChildcareCreateWorkerPipe(1, g_uWorkerSeqNo << 1);
        pWorker->pStdErr = MkWinChildcareCreateWorkerPipe(2, g_uWorkerSeqNo << 1);
    }
    if (   output_sync == OUTPUT_SYNC_NONE
        || (   pWorker->pStdOut != NULL
            && pWorker->pStdErr != NULL))
#endif
    {
        if (kSubmitSpawnWorker(pCtx, pWorker, cVerbosity) == 0)
        {
            /*
             * Insert it into the process ID hash table and idle list.
             */
            size_t idxHash = KWORKER_PID_HASH(pWorker->pid);
            pWorker->pNextPidHash = g_apPidHash[idxHash];
            g_apPidHash[idxHash] = pWorker;

            kSubmitListAppend(&g_IdleList, pWorker);
            return pWorker;
        }
    }
#if defined(CONFIG_NEW_WIN_CHILDREN) && defined(KBUILD_OS_WINDOWS)
    if (pWorker->pStdErr)
        MkWinChildcareDeleteWorkerPipe(pWorker->pStdErr);
    if (pWorker->pStdOut)
        MkWinChildcareDeleteWorkerPipe(pWorker->pStdOut);
#endif

    free(pWorker);
    return NULL;
}


/**
 * Composes a JOB mesage for a worker.
 *
 * @returns Pointer to the message.
 * @param   pszExecutable       The executable to run.
 * @param   papszArgs           The argument vector.
 * @param   papszEnvVars        The environment vector.
 * @param   pszCwd              The current directory.
 * @param   fWatcomBrainDamage  The wcc/wcc386 workaround.
 * @param   fNoPchCaching       Whether to disable precompiled header caching.
 * @param   papszPostCmdArgs    The post command and it's arguments.
 * @param   cPostCmdArgs        Number of post command argument, including the
 *                              command.  Zero if no post command scheduled.
 * @param   pcbMsg              Where to return the message length.
 */
static void *kSubmitComposeJobMessage(const char *pszExecutable, char **papszArgs, char **papszEnvVars,
                                      const char *pszCwd, int fWatcomBrainDamage, int fNoPchCaching,
                                      char **papszPostCmdArgs, uint32_t cPostCmdArgs, uint32_t *pcbMsg)
{
    size_t   cbTmp;
    uint32_t i;
    uint32_t cbMsg;
    uint32_t cArgs;
    uint32_t cEnvVars;
    uint8_t *pbMsg;
    uint8_t *pbCursor;

    /*
     * Adjust input.
     */
    if (!pszExecutable)
        pszExecutable = papszArgs[0];

    /*
     * Calculate the message length first.
     */
    cbMsg  = sizeof(cbMsg);
    cbMsg += sizeof("JOB");
    cbMsg += strlen(pszExecutable) + 1;
    cbMsg += strlen(pszCwd) + 1;

    cbMsg += sizeof(cArgs);
    for (i = 0; papszArgs[i] != NULL; i++)
        cbMsg += 1 + strlen(papszArgs[i]) + 1;
    cArgs  = i;

    cbMsg += sizeof(cArgs);
    for (i = 0; papszEnvVars[i] != NULL; i++)
        cbMsg += strlen(papszEnvVars[i]) + 1;
    cEnvVars = i;

    cbMsg += 1; /* fWatcomBrainDamage */
    cbMsg += 1; /* fNoPchCaching */

    cbMsg += sizeof(cPostCmdArgs);
    for (i = 0; i < cPostCmdArgs; i++)
        cbMsg += strlen(papszPostCmdArgs[i]) + 1;

    /*
     * Compose the message.
     */
    pbMsg = pbCursor = xmalloc(cbMsg);

    /* header */
    memcpy(pbCursor, &cbMsg, sizeof(cbMsg));
    pbCursor += sizeof(cbMsg);
    memcpy(pbCursor, "JOB", sizeof("JOB"));
    pbCursor += sizeof("JOB");

    /* executable. */
    cbTmp = strlen(pszExecutable) + 1;
    memcpy(pbCursor, pszExecutable, cbTmp);
    pbCursor += cbTmp;

    /* cwd */
    cbTmp = strlen(pszCwd) + 1;
    memcpy(pbCursor, pszCwd, cbTmp);
    pbCursor += cbTmp;

    /* argument */
    memcpy(pbCursor, &cArgs, sizeof(cArgs));
    pbCursor += sizeof(cArgs);
    for (i = 0; papszArgs[i] != NULL; i++)
    {
        *pbCursor++ = 0; /* Argument expansion flags (MSC, EMX). */
        cbTmp = strlen(papszArgs[i]) + 1;
        memcpy(pbCursor, papszArgs[i], cbTmp);
        pbCursor += cbTmp;
    }
    assert(i == cArgs);

    /* environment */
    memcpy(pbCursor, &cEnvVars, sizeof(cEnvVars));
    pbCursor += sizeof(cEnvVars);
    for (i = 0; papszEnvVars[i] != NULL; i++)
    {
        cbTmp = strlen(papszEnvVars[i]) + 1;
        memcpy(pbCursor, papszEnvVars[i], cbTmp);
        pbCursor += cbTmp;
    }
    assert(i == cEnvVars);

    /* flags */
    *pbCursor++ = fWatcomBrainDamage != 0;
    *pbCursor++ = fNoPchCaching != 0;

    /* post command */
    memcpy(pbCursor, &cPostCmdArgs, sizeof(cPostCmdArgs));
    pbCursor += sizeof(cPostCmdArgs);
    for (i = 0; i < cPostCmdArgs; i++)
    {
        cbTmp = strlen(papszPostCmdArgs[i]) + 1;
        memcpy(pbCursor, papszPostCmdArgs[i], cbTmp);
        pbCursor += cbTmp;
    }
    assert(i == cPostCmdArgs);

    assert(pbCursor - pbMsg == (size_t)cbMsg);

    /*
     * Done.
     */
    *pcbMsg = cbMsg;
    return pbMsg;
}


/**
 * Sends the job message to the given worker, respawning the worker if
 * necessary.
 *
 * @returns 0 on success, non-zero on failure.
 *
 * @param   pCtx                The command execution context.
 * @param   pWorker             The work to send the request to.  The worker is
 *                              on the idle list.
 * @param   pvMsg               The message to send.
 * @param   cbMsg               The size of the message.
 * @param   fNoRespawning       Set if
 * @param   cVerbosity          The verbosity level.
 */
static int kSubmitSendJobMessage(PKMKBUILTINCTX pCtx, PWORKERINSTANCE pWorker, void const *pvMsg, uint32_t cbMsg,
                                 int fNoRespawning, int cVerbosity)
{
    int cRetries;

    /*
     * Respawn the worker if it stopped by itself and we closed the pipe already.
     */
#ifdef KBUILD_OS_WINDOWS
    if (pWorker->hPipe == INVALID_HANDLE_VALUE)
#else
    if (pWorker->fdSocket == -1)
#endif
    {
        if (!fNoRespawning)
        {
            if (cVerbosity > 0)
                warnx(pCtx, "Respawning worker (#1)...\n");
            if (kSubmitRespawnWorker(pCtx, pWorker, cVerbosity) != 0)
                return 2;
        }

    }

    /*
     * Restart-on-broken-pipe loop. Necessary?
     */
    for (cRetries = !fNoRespawning ? 1 : 0; ; cRetries--)
    {
        /*
         * Try write the message.
         */
        uint32_t        cbLeft = cbMsg;
        uint8_t const  *pbLeft = (uint8_t const  *)pvMsg;
#ifdef KBUILD_OS_WINDOWS
        DWORD           dwErr;
        DWORD           cbWritten;
        while (WriteFile(pWorker->hPipe, pbLeft, cbLeft, &cbWritten, NULL /*pOverlapped*/))
        {
            assert(cbWritten <= cbLeft);
            cbLeft -= cbWritten;
            if (!cbLeft)
                return 0;

            /* This scenario shouldn't really ever happen. But just in case... */
            pbLeft += cbWritten;
        }
        dwErr = GetLastError();
        if (   (   dwErr != ERROR_BROKEN_PIPE
                && dwErr != ERROR_NO_DATA)
            || cRetries <= 0)
            return errx(pCtx, 1, "Error writing to worker: %u", dwErr);
#else
        ssize_t cbWritten
        while ((cbWritten = write(pWorker->fdSocket, pbLeft, cbLeft)) >= 0)
        {
            assert(cbWritten <= cbLeft);
            cbLeft -= cbWritten;
            if (!cbLeft)
                return 0;

            pbLeft += cbWritten;
        }
        if (  (   errno != EPIPE
               && errno != ENOTCONN
               && errno != ECONNRESET))
            || cRetries <= 0)
            return err(pCtx, 1, "Error writing to worker");
# error "later"
#endif

        /*
         * Broken connection. Try respawn the worker.
         */
        if (cVerbosity > 0)
            warnx(pCtx, "Respawning worker (#2)...\n");
        if (kSubmitRespawnWorker(pCtx, pWorker, cVerbosity) != 0)
            return 2;
    }
}


/**
 * Closes the connection on a worker that said it is going to exit now.
 *
 * This is a way of dealing with imperfect resource management in the worker, it
 * will monitor it a little and trigger a respawn when it looks bad.
 *
 * This function just closes the pipe / socket connection to the worker.  The
 * kSubmitSendJobMessage function will see this a trigger a respawn the next
 * time the worker is engaged.  This will usually mean there's a little delay in
 * which the process can terminate without us having to actively wait for it.
 *
 * @param   pCtx                The command execution context.
 * @param   pWorker             The worker instance.
 */
static void kSubmitCloseConnectOnExitingWorker(PKMKBUILTINCTX pCtx, PWORKERINSTANCE pWorker)
{
#ifdef KBUILD_OS_WINDOWS
    if (!CloseHandle(pWorker->hPipe))
        warnx(pCtx, "CloseHandle(pWorker->hPipe): %u", GetLastError());
    pWorker->hPipe = INVALID_HANDLE_VALUE;
#else
    if (close(pWorker->fdSocket) != 0)
        warn(pCtx, "close(pWorker->fdSocket)");
    pWorker->fdSocket = -1;
#endif
}


#ifdef KBUILD_OS_WINDOWS

/**
 * Handles read failure.
 *
 * @returns Exit code.
 * @param   pCtx                The command execution context.
 * @param   pWorker             The worker instance.
 * @param   dwErr               The error code.
 * @param   pszWhere            Where it failed.
 */
static int kSubmitWinReadFailed(PKMKBUILTINCTX pCtx, PWORKERINSTANCE pWorker, DWORD dwErr, const char *pszWhere)
{
    DWORD dwExitCode;

    if (pWorker->cbResultRead == 0)
        errx(pCtx, 1, "%s/ReadFile failed: %u", pszWhere, dwErr);
    else
        errx(pCtx, 1, "%s/ReadFile failed: %u (read %u bytes)", pszWhere, dwErr, pWorker->cbResultRead);
    assert(dwErr != 0);

    /* Complete the result. */
    pWorker->Result.s.rcExit         = 127;
    pWorker->Result.s.bWorkerExiting = 1;
    pWorker->cbResultRead            = sizeof(pWorker->Result);

    if (GetExitCodeProcess(pWorker->hProcess, &dwExitCode))
    {
        if (dwExitCode != 0)
            pWorker->Result.s.rcExit = dwExitCode;
    }

    return dwErr != 0 ? (int)(dwErr & 0x7fffffff) : 0x7fffffff;

}


/**
 * Used by
 * @returns 0 if we got the whole result, -1 if I/O is pending, and windows last
 *          error on ReadFile failure.
 * @param   pCtx                The command execution context.
 * @param   pWorker             The worker instance.
 */
static int kSubmitReadMoreResultWin(PKMKBUILTINCTX pCtx, PWORKERINSTANCE pWorker, const char *pszWhere)
{
    /*
     * Set up the result read, telling the sub_proc.c unit about it.
     */
    while (pWorker->cbResultRead < sizeof(pWorker->Result))
    {
        DWORD cbRead = 0;

        BOOL fRc = ResetEvent(pWorker->OverlappedRead.hEvent);
        assert(fRc); (void)fRc;

        pWorker->OverlappedRead.Offset     = 0;
        pWorker->OverlappedRead.OffsetHigh = 0;

        if (!ReadFile(pWorker->hPipe, &pWorker->Result.ab[pWorker->cbResultRead],
                     sizeof(pWorker->Result) - pWorker->cbResultRead,
                     &cbRead,
                     &pWorker->OverlappedRead))
        {
            DWORD dwErr = GetLastError();
            if (dwErr == ERROR_IO_PENDING)
                return -1;
            return kSubmitWinReadFailed(pCtx, pWorker, dwErr, pszWhere);
        }

        pWorker->cbResultRead += cbRead;
        assert(pWorker->cbResultRead <= sizeof(pWorker->Result));
    }
    return 0;
}

#endif /* KBUILD_OS_WINDOWS */

/**
 * Marks the worker active.
 *
 * On windows this involves setting up the async result read and telling
 * sub_proc.c about the process.
 *
 * @returns Exit code.
 * @param   pCtx                The command execution context.
 * @param   pWorker             The worker instance to mark as active.
 * @param   cVerbosity          The verbosity level.
 * @param   pChild              The kmk child to associate the job with.
 * @param   pPidSpawned         If @a *pPidSpawned is non-zero if the child is
 *                              running, otherwise the worker is already done
 *                              and we've returned the exit code of the job.
 */
static int kSubmitMarkActive(PKMKBUILTINCTX pCtx, PWORKERINSTANCE pWorker, int cVerbosity, struct child *pChild, pid_t *pPidSpawned)
{
#ifdef KBUILD_OS_WINDOWS
    int rc;
#endif

    pWorker->cbResultRead = 0;

#ifdef KBUILD_OS_WINDOWS
    /*
     * Setup the async result read on windows.  If we're slow and the worker
     * very fast, this may actually get the result immediately.
     */
l_again:
    rc = kSubmitReadMoreResultWin(pCtx, pWorker, "kSubmitMarkActive");
    if (rc == -1)
    {
# ifndef CONFIG_NEW_WIN_CHILDREN
        if (process_kmk_register_submit(pWorker->OverlappedRead.hEvent, (intptr_t)pWorker, pPidSpawned) == 0)
        { /* likely */ }
        else
        {
            /* We need to do the waiting here because sub_proc.c has too much to do. */
            warnx(pCtx, "Too many processes for sub_proc.c to handle!");
            WaitForSingleObject(pWorker->OverlappedRead.hEvent, INFINITE);
            goto l_again;
        }
# else
        if (MkWinChildCreateSubmit((intptr_t)pWorker->OverlappedRead.hEvent, pWorker,
                                   pWorker->pStdOut, pWorker->pStdErr, pChild, pPidSpawned) == 0)
        { /* likely */ }
        else
        {
            /* We need to do the waiting here because sub_proc.c has too much to do. */
            warnx(pCtx, "MkWinChildCreateSubmit failed!");
            WaitForSingleObject(pWorker->OverlappedRead.hEvent, INFINITE);
            goto l_again;
        }
# endif
    }
    else
    {
        assert(rc == 0 || pWorker->Result.s.rcExit != 0);
        if (pWorker->Result.s.bWorkerExiting)
            kSubmitCloseConnectOnExitingWorker(pCtx, pWorker);
        *pPidSpawned = 0;
        return pWorker->Result.s.rcExit;
    }
#endif

    /*
     * Mark it busy and move it to the active instance.
     */
    pWorker->pBusyWith = pChild;
#ifndef KBUILD_OS_WINDOWS
    *pPidSpawned = pWorker->pid;
#endif

    kSubmitListUnlink(&g_IdleList, pWorker);
    kSubmitListAppend(&g_BusyList, pWorker);
    return 0;
}


#ifdef KBUILD_OS_WINDOWS

/**
 * Retrieve the worker child result.
 *
 * If incomplete, we restart the ReadFile operation like kSubmitMarkActive does.
 *
 * @returns 0 on success, -1 if ReadFile was restarted.
 * @param   pvUser              The worker instance.
 * @param   fBlock              if we're to block waiting for the result or not.
 * @param   prcExit             Where to return the exit code.
 * @param   piSigNo             Where to return the signal number.
 */
int kSubmitSubProcGetResult(intptr_t pvUser, int fBlock, int *prcExit, int *piSigNo)
{
    PWORKERINSTANCE pWorker = (PWORKERINSTANCE)pvUser;
    KMKBUILTINCTX   FakeCtx = { "kSubmit/GetResult", NULL };
    PKMKBUILTINCTX  pCtx = &FakeCtx;

    /*
     * Get the overlapped result.  There should be one since we're here
     * because of a satisfied WaitForMultipleObject.
     */
    DWORD cbRead = 0;
    if (GetOverlappedResult(pWorker->hPipe, &pWorker->OverlappedRead, &cbRead, fBlock ? TRUE : FALSE))
    {
        pWorker->cbResultRead += cbRead;
        assert(pWorker->cbResultRead <= sizeof(pWorker->Result));

        /* More to be read? */
        while (pWorker->cbResultRead < sizeof(pWorker->Result))
        {
            int rc = kSubmitReadMoreResultWin(pCtx, pWorker, "kSubmitSubProcGetResult/more");
            if (rc == -1)
                return -1;
            assert(rc == 0 || pWorker->Result.s.rcExit != 0);
        }
        assert(pWorker->cbResultRead == sizeof(pWorker->Result));
    }
    else
    {
        DWORD dwErr = GetLastError();
        if (dwErr == ERROR_IO_INCOMPLETE && !fBlock)
            return -1;
        kSubmitWinReadFailed(pCtx, pWorker, dwErr, "kSubmitSubProcGetResult/result");
    }

    /*
     * Okay, we've got a result.
     */
    *prcExit = pWorker->Result.s.rcExit;
    switch (pWorker->Result.s.rcExit)
    {
        default:                                *piSigNo = 0; break;
        case CONTROL_C_EXIT:                    *piSigNo = SIGINT; break;
        case STATUS_INTEGER_DIVIDE_BY_ZERO:     *piSigNo = SIGFPE; break;
        case STATUS_ACCESS_VIOLATION:           *piSigNo = SIGSEGV; break;
        case STATUS_PRIVILEGED_INSTRUCTION:
        case STATUS_ILLEGAL_INSTRUCTION:        *piSigNo = SIGILL; break;
    }
    if (pWorker->Result.s.bWorkerExiting)
        kSubmitCloseConnectOnExitingWorker(pCtx, pWorker);

    return 0;
}


int kSubmitSubProcKill(intptr_t pvUser, int iSignal)
{
    return -1;
}


/**
 * Called by process_cleanup when it's done with the worker.
 *
 * @param   pvUser              The worker instance.
 */
void kSubmitSubProcCleanup(intptr_t pvUser)
{
    PWORKERINSTANCE pWorker = (PWORKERINSTANCE)pvUser;
    kSubmitListUnlink(&g_BusyList, pWorker);
    kSubmitListAppend(&g_IdleList, pWorker);
}

#endif /* KBUILD_OS_WINDOWS */


/**
 * atexit callback that trigger worker termination.
 */
static void kSubmitAtExitCallback(void)
{
    PWORKERINSTANCE pWorker;
    DWORD           msStartTick;
    DWORD           cKillRaids = 0;
    KMKBUILTINCTX   FakeCtx = { "kSubmit/atexit", NULL };
    PKMKBUILTINCTX  pCtx = &FakeCtx;

    /*
     * Tell all the workers to exit by breaking the connection.
     */
    for (pWorker = g_IdleList.pHead; pWorker != NULL; pWorker = pWorker->pNext)
        kSubmitCloseConnectOnExitingWorker(pCtx, pWorker);
    for (pWorker = g_BusyList.pHead; pWorker != NULL; pWorker = pWorker->pNext)
        kSubmitCloseConnectOnExitingWorker(pCtx, pWorker);

    /*
     * Wait a little while for them to stop.
     */
    Sleep(0);
    msStartTick = GetTickCount();
    for (;;)
    {
        /*
         * Collect handles of running processes.
         */
        PWORKERINSTANCE apWorkers[MAXIMUM_WAIT_OBJECTS];
        HANDLE          ahHandles[MAXIMUM_WAIT_OBJECTS];
        DWORD           cHandles = 0;

        for (pWorker = g_IdleList.pHead; pWorker != NULL; pWorker = pWorker->pNext)
            if (pWorker->hProcess != INVALID_HANDLE_VALUE)
            {
                if (cHandles < MAXIMUM_WAIT_OBJECTS)
                {
                    apWorkers[cHandles] = pWorker;
                    ahHandles[cHandles] = pWorker->hProcess;
                }
                cHandles++;
            }
        for (pWorker = g_BusyList.pHead; pWorker != NULL; pWorker = pWorker->pNext)
            if (pWorker->hProcess != INVALID_HANDLE_VALUE)
            {
                if (cHandles < MAXIMUM_WAIT_OBJECTS)
                {
                    apWorkers[cHandles] = pWorker;
                    ahHandles[cHandles] = pWorker->hProcess;
                }
                cHandles++;
            }
        if (cHandles == 0)
            return;

        /*
         * Wait for the processes.
         */
        for (;;)
        {
            DWORD cMsElapsed = GetTickCount() - msStartTick;
            DWORD dwWait = WaitForMultipleObjects(cHandles <= MAXIMUM_WAIT_OBJECTS ? cHandles : MAXIMUM_WAIT_OBJECTS,
                                                  ahHandles, FALSE /*bWaitAll*/,
                                                  cMsElapsed < 5000 ? 5000 - cMsElapsed + 16 : 16);
            if (   dwWait >= WAIT_OBJECT_0
                && dwWait <= WAIT_OBJECT_0 + MAXIMUM_WAIT_OBJECTS)
            {
                size_t idx = dwWait - WAIT_OBJECT_0;
                CloseHandle(apWorkers[idx]->hProcess);
                apWorkers[idx]->hProcess = INVALID_HANDLE_VALUE;

                if (cHandles <= MAXIMUM_WAIT_OBJECTS)
                {
                    /* Restart the wait with the worker removed, or quit if it was the last worker. */
                    cHandles--;
                    if (!cHandles)
                        return;
                    if (idx != cHandles)
                    {
                        apWorkers[idx] = apWorkers[cHandles];
                        ahHandles[idx] = ahHandles[cHandles];
                    }
                    continue;
                }
                /* else: Reconstruct the wait array so we get maximum coverage. */
            }
            else if (dwWait == WAIT_TIMEOUT)
            {
                /* Terminate the whole bunch. */
                cKillRaids++;
                if (cKillRaids == 1 && getenv("KMK_KSUBMIT_NO_KILL") == NULL)
                {
                    warnx(pCtx, "Killing %u lingering worker processe(s)!\n", cHandles);
                    for (pWorker = g_IdleList.pHead; pWorker != NULL; pWorker = pWorker->pNext)
                        if (pWorker->hProcess != INVALID_HANDLE_VALUE)
                            TerminateProcess(pWorker->hProcess, WAIT_TIMEOUT);
                    for (pWorker = g_BusyList.pHead; pWorker != NULL; pWorker = pWorker->pNext)
                        if (pWorker->hProcess != INVALID_HANDLE_VALUE)
                            TerminateProcess(pWorker->hProcess, WAIT_TIMEOUT);
                }
                else
                {
                    warnx(pCtx, "Giving up on the last %u worker processe(s). :-(\n", cHandles);
                    return;
                }
            }
            else
            {
                /* Some kind of wait error.  Could be a bad handle, check each and remove
                   bad ones as well as completed ones. */
                size_t idx;
                warnx(pCtx, "WaitForMultipleObjects unexpectedly returned %#u (err=%u)\n",
                      dwWait, GetLastError());
                for (idx = 0; idx < cHandles; idx++)
                {
                    dwWait = WaitForSingleObject(ahHandles[idx], 0 /*ms*/);
                    if (dwWait != WAIT_TIMEOUT)
                    {
                        CloseHandle(apWorkers[idx]->hProcess);
                        apWorkers[idx]->hProcess = INVALID_HANDLE_VALUE;
                    }
                }
            }
            break;
        } /* wait loop */
    } /* outer wait loop */
}


static int kmk_builtin_kSubmit_usage(PKMKBUILTINCTX pCtx, int fIsErr)
{
    kmk_builtin_ctx_printf(pCtx, fIsErr,
                           "usage: %s [-Z|--zap-env] [-E|--set <var=val>] [-U|--unset <var=val>]\n"
                           "           [-A|--append <var=val>] [-D|--prepend <var=val>]\n"
                           "           [-C|--chdir <dir>] [--wcc-brain-damage] [--no-pch-caching]\n"
                           "           [-3|--32-bit] [-6|--64-bit] [-v]\n"
                           "           [-P|--post-cmd <cmd> [args]] -- <program> [args]\n"
                           "   or: %s --help\n"
                           "   or: %s --version\n"
                           "\n"
                           "Options:\n"
                           "  -Z, --zap-env, -i, --ignore-environment\n"
                           "    Zaps the environment. Position dependent.\n"
                           "  -E, --set <var>=[value]\n"
                           "    Sets an enviornment variable putenv fashion. Position dependent.\n"
                           "  -U, --unset <var>\n"
                           "    Removes an environment variable. Position dependent.\n"
                           "  -A, --append <var>=<value>\n"
                           "    Appends the given value to the environment variable.\n"
                           "  -D,--prepend <var>=<value>\n"
                           "    Prepends the given value to the environment variable.\n"
                           "  -C, --chdir <dir>\n"
                           "    Specifies the current directory for the program.  Relative paths\n"
                           "    are relative to the previous -C option.  Default is getcwd value.\n"
                           "  -3, --32-bit\n"
                           "    Selects a 32-bit kWorker process. Default: kmk bit count\n"
                           "  -6, --64-bit\n"
                           "    Selects a 64-bit kWorker process. Default: kmk bit count\n"
                           "  --wcc-brain-damage\n"
                           "    Works around wcc and wcc386 (Open Watcom) not following normal\n"
                           "    quoting conventions on Windows, OS/2, and DOS.\n"
                           "  --no-pch-caching\n"
                           "    Do not cache precompiled header files because they're being created.\n"
                           "  -v,--verbose\n"
                           "    More verbose execution.\n"
                           "  -P|--post-cmd <cmd> ...\n"
                           "    For running a built-in command on the output, specifying the command\n"
                           "    and all it's parameters.  Currently supported commands:\n"
                           "        kDepObj\n"
                           "  -V,--version\n"
                           "    Show the version number.\n"
                           "  -h,--help\n"
                           "    Show this usage information.\n"
                           "\n"
                           ,
                           pCtx->pszProgName, pCtx->pszProgName, pCtx->pszProgName);
    return 2;
}


int kmk_builtin_kSubmit(int argc, char **argv, char **envp, PKMKBUILTINCTX pCtx, struct child *pChild, pid_t *pPidSpawned)
{
    int             rcExit = 0;
    int             iArg;
    unsigned        cAllocatedEnvVars;
    unsigned        cEnvVars;
    char          **papszEnvVars;
    const char     *pszExecutable       = NULL;
    int             iPostCmd            = argc;
    int             cPostCmdArgs        = 0;
    unsigned        cBitsWorker         = g_cArchBits;
    int             fWatcomBrainDamage  = 0;
    int             fNoPchCaching       = 0;
    int             cVerbosity          = 0;
    size_t const    cbCwdBuf            = GET_PATH_MAX;
    PATH_VAR(szCwd);

    /*
     * Create default program environment.
     *
     * Note! We only clean up the environment on successful return, assuming
     *       make will stop after that.
     */
    if (getcwd_fs(szCwd, cbCwdBuf) != NULL)
    { /* likely */ }
    else
        return err(pCtx, 1, "getcwd_fs failed\n");

    /* The environment starts out in read-only mode and will be duplicated if modified. */
    cAllocatedEnvVars = 0;
    papszEnvVars = envp;
    cEnvVars = 0;
    while (papszEnvVars[cEnvVars] != NULL)
        cEnvVars++;

    /*
     * Parse the command line.
     */
    for (iArg = 1; iArg < argc; iArg++)
    {
        const char *pszArg = argv[iArg];
        if (*pszArg == '-')
        {
            char chOpt = *++pszArg;
            pszArg++;
            if (chOpt != '-')
            {
                if (chOpt != '\0')
                { /* likely */ }
                else
                {
                    errx(pCtx, 1, "Incomplete option: '-'");
                    return kmk_builtin_kSubmit_usage(pCtx, 1);
                }
            }
            else
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

                if (strcmp(pszArg, "no-pch-caching") == 0)
                {
                    fNoPchCaching = 1;
                    continue;
                }

                /* convert to short. */
                if (strcmp(pszArg, "help") == 0)
                    chOpt = 'h';
                else if (strcmp(pszArg, "version") == 0)
                    chOpt = 'V';
                else if (strcmp(pszArg, "set") == 0)
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
                else if (strcmp(pszArg, "post-cmd") == 0)
                    chOpt = 'P';
                else if (strcmp(pszArg, "32-bit") == 0)
                    chOpt = '3';
                else if (strcmp(pszArg, "64-bit") == 0)
                    chOpt = '6';
                else if (strcmp(pszArg, "verbose") == 0)
                    chOpt = 'v';
                else if (strcmp(pszArg, "executable") == 0)
                    chOpt = 'e';
                else
                {
                    errx(pCtx, 2, "Unknown option: '%s'", pszArg - 2);
                    return kmk_builtin_kSubmit_usage(pCtx, 1);
                }
                pszArg = "";
            }

            do
            {
                /* Get option value first, if the option takes one. */
                const char *pszValue = NULL;
                switch (chOpt)
                {
                    case 'A':
                    case 'C':
                    case 'E':
                    case 'U':
                    case 'D':
                    case 'e':
                        if (*pszArg != '\0')
                            pszValue = pszArg + (*pszArg == ':' || *pszArg == '=');
                        else if (++iArg < argc)
                            pszValue = argv[iArg];
                        else
                        {
                            errx(pCtx, 1, "Option -%c requires a value!", chOpt);
                            return kmk_builtin_kSubmit_usage(pCtx, 1);
                        }
                        break;
                }

                switch (chOpt)
                {
                    case 'Z':
                    case 'i': /* GNU env compatibility. */
                        rcExit = kBuiltinOptEnvZap(pCtx, &papszEnvVars, &cEnvVars, &cAllocatedEnvVars, cVerbosity);
                        if (rcExit == 0)
                            break;
                        return rcExit;

                    case 'E':
                        rcExit = kBuiltinOptEnvSet(pCtx, &papszEnvVars, &cEnvVars, &cAllocatedEnvVars, cVerbosity, pszValue);
                        if (rcExit == 0)
                            break;
                        return rcExit;

                    case 'A':
                        rcExit = kBuiltinOptEnvAppend(pCtx, &papszEnvVars, &cEnvVars, &cAllocatedEnvVars, cVerbosity, pszValue);
                        if (rcExit == 0)
                            break;
                        return rcExit;

                    case 'D':
                        rcExit = kBuiltinOptEnvPrepend(pCtx, &papszEnvVars, &cEnvVars, &cAllocatedEnvVars, cVerbosity, pszValue);
                        if (rcExit == 0)
                            break;
                        return rcExit;

                    case 'U':
                        rcExit = kBuiltinOptEnvUnset(pCtx, &papszEnvVars, &cEnvVars, &cAllocatedEnvVars, cVerbosity, pszValue);
                        if (rcExit == 0)
                            break;
                        return rcExit;

                    case 'C':
                        rcExit = kBuiltinOptChDir(pCtx, szCwd, cbCwdBuf, pszValue);
                        if (rcExit == 0)
                            break;
                        return rcExit;

                    case 'P':
                        if (cPostCmdArgs > 0)
                            return errx(pCtx, 1, "The -P option can only be used once!");
                        if (*pszArg != '\0')
                            return errx(pCtx, 1, "The cmd part of the -P needs to be a separate argument!");
                        iPostCmd = ++iArg;
                        if (iArg >= argc)
                            return errx(pCtx, 1, "The -P option requires a command following it!");
                        while (iArg < argc && strcmp(argv[iArg], "--") != 0)
                            iArg++;
                        cPostCmdArgs = iArg - iPostCmd;
                        iArg--;
                        break;

                    case '3':
                        cBitsWorker = 32;
                        break;

                    case '6':
                        cBitsWorker = 64;
                        break;

                    case 'e':
                        pszExecutable = pszValue;
                        break;

                    case 'v':
                        cVerbosity++;
                        break;

                    case 'h':
                        kmk_builtin_kSubmit_usage(pCtx, 0);
                        kBuiltinOptEnvCleanup(&papszEnvVars, cEnvVars, &cAllocatedEnvVars);
                        return 0;

                    case 'V':
                        kBuiltinOptEnvCleanup(&papszEnvVars, cEnvVars, &cAllocatedEnvVars);
                        return kbuild_version(argv[0]);
                }
            } while ((chOpt = *pszArg++) != '\0');
        }
        else
        {
            errx(pCtx, 1, "Unknown argument: '%s'", pszArg);
            return kmk_builtin_kSubmit_usage(pCtx, 1);
        }
    }

    /*
     * Check that we've got something to execute.
     */
    if (iArg < argc)
    {
        uint32_t        cbMsg;
        void           *pvMsg   = kSubmitComposeJobMessage(pszExecutable, &argv[iArg], papszEnvVars, szCwd,
                                                           fWatcomBrainDamage, fNoPchCaching,
                                                           &argv[iPostCmd], cPostCmdArgs, &cbMsg);
        PWORKERINSTANCE pWorker = kSubmitSelectWorkSpawnNewIfNecessary(pCtx, cBitsWorker, cVerbosity);
        if (pWorker)
        {
            /* Before we send off the job, we should dump pending output, since
               the kWorker process currently does not coordinate its output with
               the output.c mechanics. */
#ifdef CONFIG_NEW_WIN_CHILDREN
            if (pCtx->pOut && !pWorker->pStdOut)
#else
            if (pCtx->pOut)
#endif
                output_dump(pCtx->pOut);
            rcExit = kSubmitSendJobMessage(pCtx, pWorker, pvMsg, cbMsg, 0 /*fNoRespawning*/, cVerbosity);
            if (rcExit == 0)
                rcExit = kSubmitMarkActive(pCtx, pWorker, cVerbosity, pChild, pPidSpawned);

            if (!g_fAtExitRegistered)
                if (atexit(kSubmitAtExitCallback) == 0)
                    g_fAtExitRegistered = 1;
        }
        else
            rcExit = 1;
        free(pvMsg);
    }
    else
    {
        errx(pCtx, 1, "Nothing to executed!");
        rcExit = kmk_builtin_kSubmit_usage(pCtx, 1);
    }

    kBuiltinOptEnvCleanup(&papszEnvVars, cEnvVars, &cAllocatedEnvVars);
    return rcExit;
}



