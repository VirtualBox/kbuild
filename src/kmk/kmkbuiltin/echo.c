/* $Id: echo.c 3192 2018-03-26 20:25:56Z knut.osmundsen@oracle.com $ */
/** @file
 * kMk Builtin command - echo
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include "config.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif
#ifdef _MSC_VER
# include <io.h>
#endif

#include "kmkbuiltin.h"
#include "err.h"


int kmk_builtin_echo(int argc, char **argv, char **envp, PKMKBUILTINCTX pCtx)
{
    int     rcExit = 0;
    int     iFirst = 1;
    int     i;
    char   *pszBuf;
    size_t  cbBuf;

    /*
     * Check for the -n option.
     */
    int fNoNewLine = 0;
    if (   argc > iFirst
        && strcmp(argv[iFirst], "-n") == 0)
    {
        iFirst++;
        fNoNewLine = 1;
    }

    /*
     * Calc buffer size and allocate it.
     */
    cbBuf = 1 + 1;
    for (i = 1; i < argc; i++)
        cbBuf += (i > iFirst) + strlen(argv[i]);
    pszBuf = (char *)malloc(cbBuf);
    if (pszBuf)
    {
        /*
         * Assembler the output into the buffer.
         */
        char *pszDst = pszBuf;
        for (i = iFirst; i < argc; i++)
        {
            const char *pszArg = argv[i];
            size_t      cchArg = strlen(pszArg);

            /* Check for "\c" in final argument (same as -n). */
            if (i + 1 >= argc
                && cchArg >= 2
                && pszArg[cchArg - 2] == '\\'
                && pszArg[cchArg - 1] == 'c')
            {
                fNoNewLine = 1;
                cchArg -= 2;
            }
            if (i > iFirst)
                *pszDst++ = ' ';
            memcpy(pszDst, pszArg, cchArg);
            pszDst += cchArg;
        }
        if (!fNoNewLine)
            *pszDst++ = '\n';
        *pszDst = '\0';

        /*
         * Push it out.
         */
#ifndef KMK_BUILTIN_STANDALONE
        if (output_write_text(pCtx->pOut, 0, pszBuf, pszDst - pszBuf) == -1)
            rcExit = err(pCtx, 1, "output_write_text");
#else
        if (write(STDOUT_FILENO, pszBuf, pszDst - pszBuf) == -1)
            rcExit = err(pCtx, 1, "write");
#endif
        free(pszBuf);
    }
    else
        rcExit = err(pCtx, 1, "malloc(%lu)", (unsigned long)cbBuf);
    return rcExit;
}

#ifdef KMK_BUILTIN_STANDALONE
int main(int argc, char **argv, char **envp)
{
    KMKBUILTINCTX Ctx = { "kmk_echo", NULL };
    return kmk_builtin_echo(argc, argv, envp, &Ctx);
}
#endif

