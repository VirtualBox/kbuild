/* $Id: winchildren.h 3199 2018-03-28 18:56:21Z knut.osmundsen@oracle.com $ */
/** @file
 * Child process creation and management for kmk.
 */

/*
 * Copyright (c) 2018 knut st. osmundsen <bird-kBuild-spamx@anduin.net>
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

#ifndef INCLUDED_WINCHILDREN_H
#define INCLUDED_WINCHILDREN_H

#ifdef DECLARE_HANDLE
/**
 * A childcare worker pipe.
 */
typedef struct WINCCWPIPE
{
    /** My end of the pipe. */
    HANDLE              hPipeMine;
    /** The child end of the pipe. */
    HANDLE              hPipeChild;
    /** The event for asynchronous reading. */
    HANDLE              hEvent;
    /** Which pipe this is (1 == stdout, 2 == stderr). */
    unsigned char       iWhich;
    /** Set if we've got a read pending already. */
    BOOL                fReadPending;
    /** Indicator that we've written out something.  This is cleared before
     * we start catching output from a new child and use in the CL.exe
     * supression heuristics. */
    BOOL                fHaveWrittenOut;
    /** Number of bytes at the start of the buffer that we've already
     * written out.  We try write out whole lines. */
    DWORD               cbWritten;
    /** The buffer offset of the read currently pending. */
    DWORD               offPendingRead;
    /** Read buffer size. */
    DWORD               cbBuffer;
    /** The read buffer allocation. */
    unsigned char      *pbBuffer;
    /** Overlapped I/O structure. */
    OVERLAPPED          Overlapped;
} WINCCWPIPE;
#endif

typedef struct WINCCWPIPE *PWINCCWPIPE;

void    MkWinChildInit(unsigned int cJobSlot);
void    MkWinChildReExecMake(char **papszArgs, char **papszEnv);
intptr_t MkWinChildGetCompleteEventHandle(void);
int     MkWinChildCreate(char **papszArgs, char **papszEnv, const char *pszShell, struct child *pMkChild, pid_t *pPid);
int     MkWinChildCreateWithStdOutPipe(char **papszArgs, char **papszEnv, int fdErr, pid_t *pPid, int *pfdReadPipe);
#ifdef KMK
struct KMKBUILTINENTRY;
int     MkWinChildCreateBuiltIn(struct KMKBUILTINENTRY const *pBuiltIn, int cArgs, char **papszArgs,
                                char **papszEnv, struct child *pMkChild, pid_t *pPid);
int     MkWinChildCreateAppend(const char *pszFilename, char **ppszAppend, size_t cbAppend, int fTruncate,
                               struct child *pMkChild, pid_t *pPid);

int     MkWinChildCreateSubmit(intptr_t hEvent, void *pvSubmitWorker, PWINCCWPIPE pStdOut, PWINCCWPIPE pStdErr, pid_t *pPid);
PWINCCWPIPE MkWinChildcareCreateWorkerPipe(unsigned iWhich, unsigned int idxWorker);
void    MkWinChildcareWorkerDrainPipes(struct WINCHILD *pChild, PWINCCWPIPE pStdOut, PWINCCWPIPE pStdErr);
void    MkWinChildcareDeleteWorkerPipe(PWINCCWPIPE pPipe);

int     MkWinChildCreateRedirect(intptr_t hProcess, pid_t *pPid);
# ifdef DECLARE_HANDLE
int     MkWinChildBuiltInExecChild(void *pvWorker, const char *pszExecutable, char **papszArgs, BOOL fQuotedArgv,
                                   char **papszEnvVars, const char *pszCwd, BOOL pafReplace[3], HANDLE pahReplace[3]);
# endif
#endif
int     MkWinChildKill(pid_t pid, int iSignal, struct child *pMkChild);
int     MkWinChildWait(int fBlock, pid_t *pPid, int *piExitCode, int *piSignal, int *pfCoreDumped, struct child **ppMkChild);
void    MkWinChildExclusiveAcquire(void);
void    MkWinChildExclusiveRelease(void);

#undef  CLOSE_ON_EXEC
#define CLOSE_ON_EXEC(a_fd) MkWinChildUnrelatedCloseOnExec(a_fd)
int     MkWinChildUnrelatedCloseOnExec(int fd);


#endif

