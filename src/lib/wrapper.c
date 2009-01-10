/* $Id: wrapper.c 2243 2009-01-10 02:24:02Z knut.osmundsen@oracle.com $ */
/** @file
 * Wrapper program for various debugging purposes.
 */

/*
 * Copyright (c) 2007-2009 knut st. osmundsen <bird-kBuild-spamix@anduin.net>
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
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#ifdef _MSC_VER
# include <process.h>
#else
# include <unistd.h>
#endif

int main(int argc, char **argv, char **envp)
{
    const char *pszLogTo        = getenv("WRAPPER_LOGTO");
    const char *pszLogFileArgs  = getenv("WRAPPER_LOGFILEARGS");
    const char *pszLogEnv       = getenv("WRAPPER_LOGENV");
    const char *pszExec         = getenv("WRAPPER_EXEC");
    const char *pszSigSegv      = getenv("WRAPPER_SIGSEGV");
    const char *pszRetVal       = getenv("WRAPPER_RETVAL");
    int i;

    if (pszLogTo)
    {
        FILE *pLog = fopen(pszLogTo, "a");
        if (pLog)
        {
            fprintf(pLog, "+++ %s pid=%ld +++\n", argv[0], (long)getpid());
            for (i = 1; i < argc; i++)
            {
                fprintf(pLog, "argv[%d]: '%s'\n", i, argv[i]);
                if (pszLogFileArgs)
                {
                    FILE *pArg = fopen(argv[i], "r");
                    if (pArg)
                    {
                        int iLine = 0;
                        static char szLine[64*1024];
                        while (fgets(szLine, sizeof(szLine), pArg) && iLine++ < 42)
                            fprintf(pLog, "%2d: %s", iLine, szLine);
                        fclose(pArg);
                    }
                }
            }
            if (pszLogEnv)
                for (i = 0; envp[i]; i++)
                    fprintf(pLog, "envp[%d]: '%s'\n", i, envp[i]);
            fprintf(pLog, "--- %s pid=%ld ---\n", argv[0], (long)getpid());
            fclose(pLog);
        }
    }

    if (pszSigSegv)
    {
        char *pchIllegal = (char *)1;
        pchIllegal[0] = '\0';
    }

    if (pszExec)
    {
        /** @todo */
    }

    return pszRetVal ? atol(pszRetVal) : 1;
}
