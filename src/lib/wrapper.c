/* $Id: wrapper.c 2851 2016-08-31 17:30:52Z knut.osmundsen@oracle.com $ */
/** @file
 * Wrapper program for various debugging purposes.
 */

/*
 * Copyright (c) 2007-2016 knut st. osmundsen <bird-kBuild-spamx@anduin.net>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 * Alternatively, the content of this file may be used under the terms of the
 * GPL version 2 or later, or LGPL version 2.1 or later.
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

