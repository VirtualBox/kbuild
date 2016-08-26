/* $Id: submit.c 2841 2016-08-26 20:04:50Z knut.osmundsen@oracle.com $ */
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
#include "make.h"
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

#include "kbuild.h"
#include "kmkbuiltin.h"
#include "err.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** Hashes a pid. */
#define KWORKER_PID_HASH(a_pid) ((size_t)(a_pid) % 61)


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
#ifdef KBUILD_OS_WINDOWS
    /** The process handle. */
    HANDLE                  hProcess;
    /** The bi-directional pipe we use to talk to the kWorker process. */
    HANDLE                  hPipe;
    /** For overlapped read (have valid event semaphore). */
    OVERLAPPED              OverlappedRead;
    /** The 32-bit exit code read bufffer. */
    uint32_t                u32ReadResult;
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
        assert(pList->pHead == pWorker);
        pList->pHead = pPrev;
    }

    if (pPrev)
    {
        assert(pPrev->pNext == pWorker);
        pPrev->pNext = pNext;
    }
    else
    {
        assert(pList->pTail == pWorker);
        pList->pTail = pNext;
    }

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
        pList->pTail = pWorker;
    }

    pList->cEntries++;
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
 * Creates a new worker process.
 *
 * @returns 0 on success, non-zero value on failure.
 * @param   pWorker             The worker structure.  Caller does the linking
 *                              (as we might be reusing an existing worker
 *                              instance because a worker shut itself down due
 *                              to high resource leak level).
 * @param   cVerbosity          The verbosity level.
 */
static int kSubmitSpawnWorker(PWORKERINSTANCE pWorker, int cVerbosity)
{
#if defined(KBUILD_OS_WINDOWS) || defined(KBUILD_OS_OS2)
    static const char s_szWorkerName[] = "kWorker.exe";
#else
    static const char s_szWorkerName[] = "kWorker";
#endif
    const char     *pszBinPath = get_kbuild_bin_path();
    size_t const    cchBinPath = strlen(pszBinPath);
    size_t          cchExectuable;
    size_t const    cbExecutableBuf = GET_PATH_MAX;
    PATH_VAR(szExecutable);

    /*
     * Construct the executable path.
     */
    if (   pWorker->cBits == g_cArchBits
        ?  cchBinPath + 1 + sizeof(s_szWorkerName) <= cbExecutableBuf
        :  cchBinPath + 1 - sizeof(g_szArch) + sizeof(g_szAltArch) + sizeof(s_szWorkerName) <= cbExecutableBuf )
    {
#ifdef KBUILD_OS_WINDOWS
        static DWORD        s_fDenyRemoteClients = ~(DWORD)0;
        wchar_t             wszPipeName[64];
        HANDLE              hWorkerPipe;
        SECURITY_ATTRIBUTES SecAttrs = { /*nLength:*/ sizeof(SecAttrs), /*pAttrs:*/ NULL, /*bInheritHandle:*/ TRUE };
#else
        int                 aiPair[2] = { -1, -1 };
#endif

        memcpy(szExecutable, pszBinPath, cchBinPath);
        cchExectuable = cchBinPath;

        /* Replace the arch bin directory extension with the alternative one if requested. */
        if (pWorker->cBits != g_cArchBits)
        {
            if (   cchBinPath < sizeof(g_szArch)
                || memcmp(&szExecutable[cchBinPath - sizeof(g_szArch) + 1], g_szArch, sizeof(g_szArch) - 1) != 0)
                return errx(1, "KBUILD_BIN_PATH does not end with main architecture (%s) as expected: %s", pszBinPath, g_szArch);
            cchExectuable -= sizeof(g_szArch) - 1;
            memcpy(&szExecutable[cchExectuable], g_szAltArch, sizeof(g_szAltArch) - 1);
            cchExectuable += sizeof(g_szAltArch) - 1;
        }

        /* Append a slash and the worker name. */
        szExecutable[cchExectuable++] = '/';
        memcpy(&szExecutable[cchExectuable], s_szWorkerName, sizeof(s_szWorkerName));

#ifdef KBUILD_OS_WINDOWS
        /*
         * Create the bi-directional pipe.  Worker end is marked inheritable, our end is not.
         */
        if (s_fDenyRemoteClients == ~(DWORD)0)
            s_fDenyRemoteClients = GetVersion() >= 0x60000 ? PIPE_REJECT_REMOTE_CLIENTS : 0;
        _snwprintf(wszPipeName, sizeof(wszPipeName), L"\\\\.\\pipe\\kmk-%u-kWorker-%u", getpid(), g_uWorkerSeqNo++);
        hWorkerPipe = CreateNamedPipeW(wszPipeName,
                                       PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE /* win2k sp2+ */,
                                       PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | s_fDenyRemoteClients,
                                       1 /* cMaxInstances */,
                                       64 /*cbOutBuffer*/,
                                       65536 /*cbInBuffer*/,
                                       0 /*cMsDefaultTimeout -> 50ms*/,
                                       &SecAttrs /* inherit */);
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
                    char        szHandleArg[16];
                    const char *apszArgs[4] = { szExecutable, "--pipe", szHandleArg, NULL };
                    _snprintf(szHandleArg, sizeof(szHandleArg), "%p", hWorkerPipe);

                    /*
                     * Create the worker process.
                     */
                    pWorker->hProcess = (HANDLE) _spawnve(_P_NOWAIT, szExecutable, apszArgs, environ);
                    if ((intptr_t)pWorker->hProcess != -1)
                    {
                        CloseHandle(hWorkerPipe);
                        pWorker->pid = GetProcessId(pWorker->hProcess);
                        if (cVerbosity > 0)
                            fprintf(stderr, "kSubmit: created %d bit worker %d\n", pWorker->cBits, pWorker->pid);
                        return 0;
                    }
                    err(1, "_spawnve(,%s,,)", szExecutable);
                    CloseHandle(pWorker->OverlappedRead.hEvent);
                    pWorker->OverlappedRead.hEvent = INVALID_HANDLE_VALUE;
                }
                else
                    errx(1, "CreateEventW failed: %u", GetLastError());
                CloseHandle(pWorker->hPipe);
                pWorker->hPipe = INVALID_HANDLE_VALUE;
            }
            else
                errx(1, "Opening named pipe failed: %u", GetLastError());
            CloseHandle(hWorkerPipe);
        }
        else
            errx(1, "CreateNamedPipeW failed: %u", GetLastError());

#else
        /*
         * Create a socket pair.
         */
        if (socketpair(AF_LOCAL, SOCK_STREAM, 0, aiPair) == 0)
        {
            pWorker->fdSocket = aiPair[1];
        }
        else
            err(1, "socketpair");
#endif
    }
    else
        errx(1, "KBUILD_BIN_PATH is too long");
    return -1;
}


/**
 * Selects an idle worker or spawns a new one.
 *
 * @returns Pointer to the selected worker instance.  NULL on error.
 * @param   pWorker             The idle worker instance to respawn.
 *                              On failure this will be freed!
 * @param   cBitsWorker         The worker bitness - 64 or 32.
 */
static int kSubmitRespawnWorker(PWORKERINSTANCE pWorker, int cVerbosity)
{
    size_t idxHash;

    /*
     * Clean up after the old worker.
     */
#ifdef KBUILD_OS_WINDOWS
    DWORD   rcWait;

    /* Close the pipe handle first, breaking the pipe in case it's not already
       busted up.  Close the event semaphore too before waiting for the process. */
    if (!CloseHandle(pWorker->hPipe))
        warnx("CloseHandle(pWorker->hPipe): %u", GetLastError());
    pWorker->hPipe = INVALID_HANDLE_VALUE;

    if (!CloseHandle(pWorker->OverlappedRead.hEvent))
        warnx("CloseHandle(pWorker->OverlappedRead.hEvent): %u", GetLastError());
    pWorker->OverlappedRead.hEvent = INVALID_HANDLE_VALUE;

    /* It's probably shutdown already, if not give it 10 milliseconds before
       we terminate it forcefully. */
    rcWait = WaitForSingleObject(pWorker->hProcess, 10);
    if (rcWait != WAIT_OBJECT_0)
    {
        BOOL fRc = TerminateProcess(pWorker->hProcess, 127);
        rcWait = WaitForSingleObject(pWorker->hProcess, 100);
        if (rcWait != WAIT_OBJECT_0)
            warnx("WaitForSingleObject returns %u (and TerminateProcess %d)", rcWait, fRc);
    }

    if (!CloseHandle(pWorker->hProcess))
        warnx("CloseHandle(pWorker->hProcess): %u", GetLastError());
    pWorker->hProcess = INVALID_HANDLE_VALUE;

#else
    pid_t   pidWait;
    int     rc;

    if (close(pWorker->fdSocket) != 0)
        warn("close(pWorker->fdSocket)");
    pWorker->fdSocket = -1;

    kill(pWorker->pid, SIGTERM);
    pidWait = waitpid(pWorker->pid, &rc, 0);
    if (pidWait != pWorker->pid)
        warn("waitpid(pWorker->pid,,0)");
#endif

    /*
     * Unlink it from the hash table.
     */
    idxHash = KWORKER_PID_HASH(pWorker->pid);
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

    /*
     * Respawn it.
     */
    if (kSubmitSpawnWorker(pWorker, cVerbosity) == 0)
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
static PWORKERINSTANCE kSubmitSelectWorkSpawnNewIfNecessary(unsigned cBitsWorker, int cVerbosity)
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
    if (kSubmitSpawnWorker(pWorker, cVerbosity) == 0)
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

    free(pWorker);
    return NULL;
}


/**
 * Composes a JOB mesage for a worker.
 *
 * @returns Pointer to the message.
 * @param   pszExecutable   The executable to run.
 * @param   papszArgs       The argument vector.
 * @param   papszEnvVars    The environment vector.
 * @param   pszCwd          The current directory.
 * @param   pcbMsg          Where to return the message length.
 */
static void *kSubmitComposeJobMessage(const char *pszExecutable, char **papszArgs, char **papszEnvVars,
                                      const char *pszCwd, uint32_t *pcbMsg)
{
    size_t   i;
    size_t   cbTmp;
    uint32_t cbMsg;
    uint8_t *pbMsg;
    uint8_t *pbCursor;

    /*
     * Calculate the message length first.
     */
    cbMsg  = sizeof(cbMsg);
    cbMsg += sizeof("JOB");
    cbMsg += strlen(pszExecutable) + 1;

    for (i = 0; papszArgs[i] != NULL; i++)
        cbMsg += strlen(papszArgs[i]) + 1;
    cbMsg += 1;

    for (i = 0; papszEnvVars[i] != NULL; i++)
        cbMsg += strlen(papszEnvVars[i]) + 1;
    cbMsg += 1;

    cbMsg += strlen(pszCwd) + 1;

    /*
     * Compose the message.
     */
    pbMsg = pbCursor = xmalloc(cbMsg);

    memcpy(pbCursor, &cbMsg, sizeof(cbMsg));
    pbCursor += sizeof(cbMsg);
    memcpy(pbCursor, "JOB", sizeof("JOB"));
    pbCursor += sizeof("JOB");

    cbTmp = strlen(pszExecutable) + 1;
    memcpy(pbCursor, pszExecutable, cbTmp);
    pbCursor += cbTmp;

    for (i = 0; papszArgs[i] != NULL; i++)
    {
        cbTmp = strlen(papszArgs[i]) + 1;
        memcpy(pbCursor, papszArgs[i], cbTmp);
        pbCursor += cbTmp;
    }
    *pbCursor++ = '\0';

    for (i = 0; papszEnvVars[i] != NULL; i++)
    {
        cbTmp = strlen(papszEnvVars[i]) + 1;
        memcpy(pbCursor, papszEnvVars[i], cbTmp);
        pbCursor += cbTmp;
    }
    *pbCursor++ = '\0';

    cbTmp = strlen(pszCwd) + 1;
    memcpy(pbCursor, pszCwd, cbTmp);
    pbCursor += cbTmp;

    assert(pbCursor - pbMsg == (size_t)cbMsg);

    /* done */
    *pcbMsg = cbMsg;
    return pbMsg;
}


/**
 * Sends the job message to the given worker, respawning the worker if
 * necessary.
 *
 * @returns 0 on success, non-zero on failure.
 *
 * @param   pWorker             The work to send the request to.  The worker is
 *                              on the idle list.
 * @param   pvMsg               The message to send.
 * @param   cbMsg               The size of the message.
 * @param   cVerbosity          The verbosity level.
 */
static int kSubmitSendJobMessage(PWORKERINSTANCE pWorker, void const *pvMsg, uint32_t cbMsg, int cVerbosity)
{
    int cRetries = 1;
    for (;; cRetries--)
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
            return errx(1, "Error writing to worker: %u", dwErr);
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
            return err(1, "Error writing to worker");
# error "later"
#endif

        /*
         * Broken connection. Try respawn the worker.
         */
        if (kSubmitRespawnWorker(pWorker, cVerbosity) != 0)
            return 2;
    }
}


/**
 * Handles the --set var=value option.
 *
 * @returns 0 on success, non-zero exit code on error.
 * @param   papszEnv            The environment vector.
 * @param   pcEnvVars           Pointer to the variable holding the number of
 *                              environment variables held by @a papszEnv.
 * @param   pcAllocatedEnvVars  Pointer to the variable holding max size of the
 *                              environment vector.
 * @param   cVerbosity          The verbosity level.
 * @param   pszValue            The var=value string to apply.
 */
static int kSubmitOptEnvSet(char **papszEnv, unsigned *pcEnvVars, unsigned *pcAllocatedEnvVars,
                            int cVerbosity, const char *pszValue)
{
    const char *pszEqual = strchr(pszValue, '=');
    if (pszEqual)
    {
        unsigned iEnvVar;
        unsigned cEnvVars = *pcEnvVars;
        size_t const cchVar = pszValue - pszEqual;
        for (iEnvVar = 0; iEnvVar < cEnvVars; iEnvVar++)
            if (   strncmp(papszEnv[iEnvVar], pszValue, cchVar) == 0
                && papszEnv[iEnvVar][cchVar] == '=')
            {
                if (cVerbosity > 0)
                    fprintf(stderr, "kSubmit: replacing '%s' with '%s'\n", papszEnv[iEnvVar], pszValue);
                free(papszEnv[iEnvVar]);
                papszEnv[iEnvVar] = xstrdup(pszValue);
                break;
            }
        if (iEnvVar == cEnvVars)
        {
            /* Append new variable. We probably need to resize the vector. */
            if ((cEnvVars + 2) > *pcAllocatedEnvVars)
            {
                *pcAllocatedEnvVars = (cEnvVars + 2 + 0xf) & ~(unsigned)0xf;
                papszEnv = (char **)xrealloc(papszEnv, *pcAllocatedEnvVars * sizeof(papszEnv[0]));
            }
            papszEnv[cEnvVars++] = xstrdup(pszValue);
            papszEnv[cEnvVars]   = NULL;
            *pcEnvVars = cEnvVars;
            if (cVerbosity > 0)
                fprintf(stderr, "kSubmit: added '%s'\n", papszEnv[iEnvVar]);
        }
        else
        {
            /* Check for duplicates. */
            for (iEnvVar++; iEnvVar < cEnvVars; iEnvVar++)
                if (   strncmp(papszEnv[iEnvVar], pszValue, cchVar) == 0
                    && papszEnv[iEnvVar][cchVar] == '=')
                {
                    if (cVerbosity > 0)
                        fprintf(stderr, "kSubmit: removing duplicate '%s'\n", papszEnv[iEnvVar]);
                    free(papszEnv[iEnvVar]);
                    cEnvVars--;
                    if (iEnvVar != cEnvVars)
                        papszEnv[iEnvVar] = papszEnv[cEnvVars];
                    papszEnv[cEnvVars] = NULL;
                    iEnvVar--;
                }
        }
    }
    else
        return errx(1, "Missing '=': -E %s", pszValue);

    return 0;
}


/**
 * Handles the --unset var option.
 *
 * @returns 0 on success, non-zero exit code on error.
 * @param   papszEnv            The environment vector.
 * @param   pcEnvVars           Pointer to the variable holding the number of
 *                              environment variables held by @a papszEnv.
 * @param   cVerbosity          The verbosity level.
 * @param   pszVarToRemove      The name of the variable to remove.
 */
static int kSubmitOptEnvUnset(char **papszEnv, unsigned *pcEnvVars, int cVerbosity, const char *pszVarToRemove)
{
    if (strchr(pszVarToRemove, '=') == NULL)
    {
        unsigned     cRemoved = 0;
        size_t const cchVar   = strlen(pszVarToRemove);
        unsigned     cEnvVars = *pcEnvVars;
        unsigned     iEnvVar;

        for (iEnvVar = 0; iEnvVar < cEnvVars; iEnvVar++)
            if (   strncmp(papszEnv[iEnvVar], pszVarToRemove, cchVar) == 0
                && papszEnv[iEnvVar][cchVar] == '=')
            {
                if (cVerbosity > 0)
                    fprintf(stderr, !cRemoved ? "kSubmit: removing '%s'\n"
                            : "kSubmit: removing duplicate '%s'\n", papszEnv[iEnvVar]);
                free(papszEnv[iEnvVar]);
                cEnvVars--;
                if (iEnvVar != cEnvVars)
                    papszEnv[iEnvVar] = papszEnv[cEnvVars];
                papszEnv[cEnvVars] = NULL;
                cRemoved++;
                iEnvVar--;
            }
        *pcEnvVars = cEnvVars;

        if (cVerbosity > 0 && !cRemoved)
            fprintf(stderr, "kSubmit: not found '%s'\n", pszVarToRemove);
    }
    else
        return errx(1, "Found invalid variable name character '=' in: -U %s", pszVarToRemove);
    return 0;
}



/**
 * Handles the --chdir dir option.
 *
 * @returns 0 on success, non-zero exit code on error.
 * @param   pszCwd              The CWD buffer.  Contains current CWD on input,
 *                              modified by @a pszValue on output.
 * @param   cbCwdBuf            The size of the CWD buffer.
 * @param   pszValue            The --chdir value to apply.
 */
static int kSubmitOptChDir(char *pszCwd, size_t cbCwdBuf, const char *pszValue)
{
    size_t cchNewCwd = strlen(pszValue);
    size_t offDst;
    if (cchNewCwd)
    {
#ifdef HAVE_DOS_PATHS
        if (*pszValue == '/' || *pszValue == '\\')
        {
            if (pszValue[1] == '/' || pszValue[1] == '\\')
                offDst = 0; /* UNC */
            else if (pszCwd[1] == ':' && isalpha(pszCwd[0]))
                offDst = 2; /* Take drive letter from CWD. */
            else
                return errx(1, "UNC relative CWD not implemented: cur='%s' new='%s'", pszCwd, pszValue);
        }
        else if (   pszValue[1] == ':'
                 && isalpha(pszValue[0]))
        {
            if (pszValue[2] == '/'|| pszValue[2] == '\\')
                offDst = 0; /* DOS style absolute path. */
            else if (   pszCwd[1] == ':'
                     && tolower(pszCwd[0]) == tolower(pszValue[0]) )
            {
                pszValue += 2; /* Same drive as CWD, append drive relative path from value. */
                cchNewCwd -= 2;
                offDst = strlen(pszCwd);
            }
            else
            {
                /* Get current CWD on the specified drive and append value. */
                int iDrive = tolower(pszValue[0]) - 'a' + 1;
                if (!_getdcwd(iDrive, pszCwd, cbCwdBuf))
                    return err(1, "_getdcwd(%d,,) failed", iDrive);
                pszValue += 2;
                cchNewCwd -= 2;
            }
        }
#else
        if (*pszValue == '/')
            offDst = 0;
#endif
        else
            offDst = strlen(pszCwd); /* Relative path, append to the existing CWD value. */

        /* Do the copying. */
#ifdef HAVE_DOS_PATHS
        if (offDst > 0 && pszCwd[offDst - 1] != '/' && pszCwd[offDst - 1] != '\\')
#else
        if (offDst > 0 && pszCwd[offDst - 1] != '/')
#endif
             pszCwd[offDst++] = '/';
        if (offDst + cchNewCwd >= cbCwdBuf)
            return errx(1, "Too long CWD: %*.*s%s", offDst, offDst, pszCwd, pszValue);
        memcpy(&pszCwd[offDst], pszValue, cchNewCwd + 1);
    }
    /* else: relative, no change - quitely ignore. */
    return 0;
}


static int usage(FILE *pOut,  const char *argv0)
{
    fprintf(pOut,
            "usage: %s [-Z|--zap-env] [-E|--set <var=val>] [-U|--unset <var=val>]\n"
            "           [-C|--chdir <dir>] [--wcc-brain-damage]\n"
            "           [-3|--32-bit] [-6|--64-bit] [-v] -- <program> [args]\n"
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
            "  -v,--verbose\n"
            "    More verbose execution.\n"
            "  -V,--version\n"
            "    Show the version number.\n"
            "  -h,--help\n"
            "    Show this usage information.\n"
            "\n"
            ,
            argv0, argv0, argv0);
    return 1;
}


int kmk_builtin_kSubmit(int argc, char **argv, char **envp, struct child *pChild)
{
    int             rcExit = 0;
    int             iArg;
    unsigned        cAllocatedEnvVars;
    unsigned        iEnvVar;
    unsigned        cEnvVars;
    char          **papszEnv            = NULL;
    const char     *pszExecutable       = NULL;
    const char     *pszCwd              = NULL;
    unsigned        cBitsWorker         = g_cArchBits;
    int             fWatcomBrainDamage  = 0;
    int             cVerbosity          = 0;
    size_t const    cbCwdBuf            = GET_PATH_MAX;
    PATH_VAR(szCwd);

    g_progname = argv[0];

    /*
     * Create default program environment.
     */
    if (getcwd_fs(szCwd, cbCwdBuf) != NULL)
    { /* likely */ }
    else
        return err(1, "getcwd_fs failed\n");

    papszEnv = pChild->environment;
    if (papszEnv)
        pChild->environment = papszEnv = target_environment(pChild->file);
    cEnvVars = 0;
    while (papszEnv[cEnvVars] != NULL)
        cEnvVars++;
    cAllocatedEnvVars = cEnvVars;

    /*
     * Parse the command line.
     */
    for (iArg = 1; iArg < argc; iArg++)
    {
        const char *pszArg = argv[iArg];
        if (*pszArg == '-')
        {
            char chOpt = *++pszArg;
            if (chOpt != '-')
            {
                if (chOpt != '\0')
                { /* likely */ }
                else
                {
                    errx(1, "Incomplete option: '-'");
                    return usage(stderr, argv[0]);
                }
            }
            else
            {
                pszArg++;

                /* '--' indicates where the bits to execute start. */
                if (*pszArg == '\0')
                {
                    iArg++;
                    break;
                }

                if (strcmp(pszArg, "watcom-brain-damage") == 0)
                {
                    fWatcomBrainDamage = 1;
                    continue;
                }

                /* convert to short. */
                if (strcmp(pszArg, "help") == 0)
                    chOpt = 'h';
                else if (strcmp(pszArg, "version") == 0)
                    chOpt = 'V';
                else if (strcmp(pszArg, "set") == 0)
                    chOpt = 'E';
                else if (strcmp(pszArg, "unset") == 0)
                    chOpt = 'U';
                else if (   strcmp(pszArg, "zap-env") == 0
                         || strcmp(pszArg, "ignore-environment") == 0 /* GNU env compatibility. */ )
                    chOpt = 'Z';
                else if (strcmp(pszArg, "chdir") == 0)
                    chOpt = 'C';
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
                    errx(1, "Unknown option: '%s'", pszArg - 2);
                    return usage(stderr, argv[0]);
                }
                pszArg = "";
            }

            do
            {
                /* Get option value first, if the option takes one. */
                const char *pszValue = NULL;
                switch (chOpt)
                {
                    case 'E':
                    case 'U':
                    case 'C':
                    case 'e':
                        if (*pszArg != '\0')
                            pszValue = pszArg + (*pszArg == ':' || *pszArg == '=');
                        else if (++iArg < argc)
                            pszValue = argv[iArg];
                        else
                        {
                            errx(1, "Option -%c requires an value!", chOpt);
                            return usage(stderr, argv[0]);
                        }
                        break;
                }

                switch (chOpt)
                {
                    case 'Z':
                    case 'i': /* GNU env compatibility. */
                        for (iEnvVar = 0; iEnvVar < cEnvVars; iEnvVar++)
                            free(papszEnv[iEnvVar]);
                        papszEnv[0] = NULL;
                        cEnvVars = 0;
                        break;

                    case 'E':
                        rcExit = kSubmitOptEnvSet(papszEnv, &cEnvVars, &cAllocatedEnvVars, cVerbosity, pszValue);
                        pChild->environment = papszEnv;
                        if (rcExit == 0)
                            break;
                        return rcExit;

                    case 'U':
                        rcExit = kSubmitOptEnvUnset(papszEnv, &cEnvVars, cVerbosity, pszValue);
                        if (rcExit == 0)
                            break;
                        return rcExit;

                    case 'C':
                        rcExit = kSubmitOptChDir(szCwd, cbCwdBuf, pszValue);
                        if (rcExit == 0)
                            break;
                        return rcExit;

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
                        usage(stdout, argv[0]);
                        return 0;

                    case 'V':
                        printf("kmk_submit - kBuild version %d.%d.%d (r%u)\n"
                               "Copyright (C) 2007-2016 knut st. osmundsen\n",
                               KBUILD_VERSION_MAJOR, KBUILD_VERSION_MINOR, KBUILD_VERSION_PATCH,
                               KBUILD_SVN_REV);
                        return 0;
                }
            } while ((chOpt = *pszArg++) != '\0');
        }
        else
        {
            errx(1, "Unknown argument: '%s'", pszArg);
            return usage(stderr, argv[0]);
        }
    }

    /*
     * Check that we've got something to execute.
     */
    if (iArg < argc)
    {
        uint32_t        cbMsg;
        void           *pvMsg   = kSubmitComposeJobMessage(pszExecutable, &argv[iArg], papszEnv, szCwd, &cbMsg);
        PWORKERINSTANCE pWorker = kSubmitSelectWorkSpawnNewIfNecessary(cBitsWorker, cVerbosity);
        if (pWorker)
        {
            rcExit = kSubmitSendJobMessage(pWorker, pvMsg, cbMsg, cVerbosity);
            if (rcExit == 0)
            {
                pWorker->pBusyWith = pChild;
                /** @todo integrate with sub_proc.c / whatever. */
            }
        }
        else
            rcExit = 1;
        free(pvMsg);
    }
    else
    {
        errx(1, "Nothing to executed!");
        rcExit = usage(stderr, argv[0]);
    }

    return rcExit;
}



