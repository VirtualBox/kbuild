/* $Id: winchildren.h 3169 2018-03-21 11:27:47Z knut.osmundsen@oracle.com $ */
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


void    MkWinChildInit(unsigned int cJobSlot);
void    MkWinChildReExecMake(char **papszArgs, char **papszEnv);
intptr_t MkWinChildGetCompleteEventHandle(void);
int     MkWinChildCreate(char **papszArgs, char **papszEnv, const char *pszShell, struct child *pMkChild, pid_t *pPid);
int     MkWinChildCreateWithStdOutPipe(char **papszArgs, char **papszEnv, int fdErr, pid_t *pPid, int *pfdReadPipe);
#ifdef KMK
struct KMKBUILTINENTRY;
int     MkWinChildCreateBuiltIn(struct KMKBUILTINENTRY const *pBuiltIn, int cArgs, char **papszArgs,
                                char **papszEnv, struct child *pMkChild, pid_t *pPid);
int     MkWinChildCreateSubmit(intptr_t hEvent, void *pvSubmitWorker, pid_t *pPid);
int     MkWinChildCreateRedirect(intptr_t hProcess, pid_t *pPid);
#endif
int     MkWinChildKill(pid_t pid, int iSignal, struct child *pMkChild);
int     MkWinChildWait(int fBlock, pid_t *pPid, int *piExitCode, int *piSignal, int *pfCoreDumped, struct child **ppMkChild);
void    MkWinChildExclusiveAcquire(void);
void    MkWinChildExclusiveRelease(void);

#undef  CLOSE_ON_EXEC
#define CLOSE_ON_EXEC(a_fd) MkWinChildUnrelatedCloseOnExec(a_fd)
int     MkWinChildUnrelatedCloseOnExec(int fd);

#endif

