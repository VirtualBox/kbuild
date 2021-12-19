/* $Id: win_exec_wrapper.c 3528 2021-12-19 16:32:38Z knut.osmundsen@oracle.com $ */
/** @file
 * win_exec_wrapper - Stub for exec'ing a kmk_xxx program.
 */

/*
 * Copyright (c) 2021 knut st. osmundsen <bird-kBuild-spamixx@anduin.net>
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
#include <windows.h>


VOID __stdcall BareBoneStart(VOID)
{
    DWORD               dwIgnored;
    PROCESS_INFORMATION ProcInfo        = { NULL, NULL, 0, 0 };
    WCHAR               wszExec[260];
    UINT                cwcExec         = GetModuleFileNameW(NULL, wszExec, 512);
    BOOL                fExecOk         = FALSE;
    WCHAR const * const pwszCommandLine = GetCommandLineW();
    STARTUPINFOW        StartInfo       = { sizeof(StartInfo), NULL, NULL, NULL, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, NULL, NULL, NULL, NULL};
    GetStartupInfoW(&StartInfo);

    /*
     * Make sure we've got the standard handles.
     */
    StartInfo.hStdInput  = GetStdHandle(STD_INPUT_HANDLE);
    StartInfo.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    StartInfo.hStdError  = GetStdHandle(STD_ERROR_HANDLE);
    StartInfo.dwFlags   |= STARTF_USESTDHANDLES;

    /*
     * Construct the executable path.
     */
    if (cwcExec > 10)
    {
        /* Strip the filename. */
#define IS_SEP(a_wc) ( (a_wc) == '\\' || (a_wc) == ':' || (a_wc) == '\\' )
        while (cwcExec > 3 && !IS_SEP(wszExec[cwcExec - 1]))
            cwcExec--;
        if (IS_SEP(wszExec[cwcExec - 1]))
        {
            /* Strip the separator. */
            while (cwcExec > 3 && IS_SEP(wszExec[cwcExec - 1]))
                cwcExec--;
            if (!IS_SEP(wszExec[cwcExec - 1]))
            {
                /* Strip the path component: */
                while (cwcExec > 3 && !IS_SEP(wszExec[cwcExec - 1]))
                    cwcExec--;
                if (IS_SEP(wszExec[cwcExec - 1]))
                {
                    /* Insert the target executable name: */
                    static char const s_szTargetName[] = TARGET_EXE_NAME;
                    unsigned off = 0;
                    while (off < sizeof(s_szTargetName))
                        wszExec[cwcExec++] = s_szTargetName[off++];
                    fExecOk = cwcExec <= 260;
                }
            }
        }
    }
    if (fExecOk)
    {
        /*
         * Create the real process.
         */
        if (CreateProcessW(wszExec, (WCHAR *)pwszCommandLine, NULL, NULL, TRUE /*bInheritHandles*/,
                           0 /*fFlags*/, NULL /*pwszzEnv*/, NULL /*pwszCwd*/, &StartInfo, &ProcInfo))
        {
            /*
             * Wait for it to complete.
             */
            CloseHandle(ProcInfo.hThread);
            for (;;)
            {
                DWORD dwExitCode = 1;
                WaitForSingleObject(ProcInfo.hProcess, INFINITE);
                if (GetExitCodeProcess(ProcInfo.hProcess, &dwExitCode))
                    for (;;)
                        ExitProcess(dwExitCode);
                Sleep(1);
            }
        }
        else
        {
            static const char s_szMsg[] = "error: CreateProcessW failed for " TARGET_EXE_NAME "\r\n";
            WriteFile(StartInfo.hStdError, s_szMsg, sizeof(s_szMsg) - 1, &dwIgnored, NULL);
        }
    }
    else
    {
        static const char s_szMsg[] = "error: path construction failed (" TARGET_EXE_NAME ")\r\n";
        WriteFile(StartInfo.hStdError, s_szMsg, sizeof(s_szMsg) - 1, &dwIgnored, NULL);
    }

    for (;;)
        ExitProcess(31);
}

