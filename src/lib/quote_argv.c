/* $Id: quote_argv.c 2838 2016-08-25 21:46:25Z knut.osmundsen@oracle.com $ */
/** @file
 * quote_argv - Correctly quote argv for spawn, windows specific.
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
#include "quote_argv.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#ifndef KBUILD_OS_WINDOWS
# error "KBUILD_OS_WINDOWS not defined"
#endif


/**
 * Checks if this is an Watcom option where we must just pass thru the string
 * as-is.
 *
 * This is currnetly only used for -d (defining macros).
 *
 * @returns 1 if pass-thru, 0 if not.
 * @param   pszArg          The argument to consider.
 */
static int isWatcomPassThruOption(const char *pszArg)
{
    char ch = *pszArg++;
    if (ch != '-' && ch != '/')
        return 0;
    ch = *pszArg++;
    switch (ch)
    {
        /* Example: -d+VAR="string-value" */
        case 'd':
            if (ch == '+')
                ch = *pszArg++;
            if (!isalpha(ch) && ch != '_')
                return 0;
            return 1;

        default:
            return 0;
    }
}


/**
 * Replaces arguments in need of quoting.
 *
 * For details on how MSC parses the command line, see "Parsing C Command-Line
 * Arguments": http://msdn.microsoft.com/en-us/library/a1y7w461.aspx
 *
 * @param   argc                The argument count.
 * @param   argv                The argument vector.
 * @param   fWatcomBrainDamage  Set if we're catering for wcc, wcc386 or similar
 *                              OpenWatcom tools.  They seem to follow some
 *                              ancient or home made quoting convention.
 * @param   fFreeOrLeak         Whether to free replaced argv members
 *                              (non-zero), or just leak them (zero).  This
 *                              depends on which argv you're working on.
 *                              Suggest doing the latter if it's main()'s argv.
 */
void quote_argv(int argc, char **argv, int fWatcomBrainDamage, int fFreeOrLeak)
{
    int i;
    for (i = 0; i < argc; i++)
    {
        char *const pszOrgOrg = argv[i];
        const char *pszOrg    = pszOrgOrg;
        size_t      cchOrg    = strlen(pszOrg);
        const char *pszQuotes = (const char *)memchr(pszOrg, '"', cchOrg);
        const char *pszProblem = NULL;
        if (   pszQuotes
            || cchOrg == 0
            || (pszProblem = (const char *)memchr(pszOrg, ' ',  cchOrg)) != NULL
            || (pszProblem = (const char *)memchr(pszOrg, '\t', cchOrg)) != NULL
            || (pszProblem = (const char *)memchr(pszOrg, '\n', cchOrg)) != NULL
            || (pszProblem = (const char *)memchr(pszOrg, '\r', cchOrg)) != NULL
            || (pszProblem = (const char *)memchr(pszOrg, '&',  cchOrg)) != NULL
            || (pszProblem = (const char *)memchr(pszOrg, '>',  cchOrg)) != NULL
            || (pszProblem = (const char *)memchr(pszOrg, '<',  cchOrg)) != NULL
            || (pszProblem = (const char *)memchr(pszOrg, '|',  cchOrg)) != NULL
            || (pszProblem = (const char *)memchr(pszOrg, '%',  cchOrg)) != NULL
            || (pszProblem = (const char *)memchr(pszOrg, '\'', cchOrg)) != NULL
            || (   !fWatcomBrainDamage
                && (pszProblem = (const char *)memchr(pszOrg, '=',  cchOrg)) != NULL)
            )
        {
            char   ch;
            int    fComplicated = pszQuotes || (cchOrg > 0 && pszOrg[cchOrg - 1] == '\\');
            size_t cchNew       = fComplicated ? cchOrg * 2 + 2 : cchOrg + 2;
            char  *pszNew       = (char *)malloc(cchNew + 1 /*term*/ + 3 /*passthru hack*/);

            argv[i] = pszNew;

            /* Watcom does not grok stuff like "-i=c:\program files\watcom\h",
               it think it's a source specification. In that case the quote
               must follow the equal sign. */
            if (fWatcomBrainDamage)
            {
                size_t cchUnquoted  = 0;
                if (pszOrg[0] == '@') /* Response file quoting: @"file name.rsp" */
                    cchUnquoted = 1;
                else if (pszOrg[0] == '-' || pszOrg[0] == '/') /* Switch quoting. */
                {
                    if (isWatcomPassThruOption(pszOrg))
                        cchUnquoted = strlen(pszOrg) + 1;
                    else
                    {
                        const char *pszNeedQuoting = (const char *)memchr(pszOrg, '=', cchOrg); /* For -i=dir and similar. */
                        if (   pszNeedQuoting == NULL
                            || (uintptr_t)pszNeedQuoting > (uintptr_t)(pszProblem ? pszProblem : pszQuotes))
                            pszNeedQuoting = pszProblem ? pszProblem : pszQuotes;
                        else
                            pszNeedQuoting++;
                        cchUnquoted = pszNeedQuoting - pszOrg;
                    }
                }
                if (cchUnquoted)
                {
                    memcpy(pszNew, pszOrg, cchUnquoted);
                    pszNew += cchUnquoted;
                    pszOrg += cchUnquoted;
                    cchOrg -= cchUnquoted;
                }
            }

            *pszNew++ = '"';
            if (fComplicated)
            {
                while ((ch = *pszOrg++) != '\0')
                {
                    if (ch == '"')
                    {
                        *pszNew++ = '\\';
                        *pszNew++ = '"';
                    }
                    else if (ch == '\\')
                    {
                        /* Backslashes are a bit complicated, they depends on
                           whether a quotation mark follows them or not.  They
                           only require escaping if one does. */
                        unsigned cSlashes = 1;
                        while ((ch = *pszOrg) == '\\')
                        {
                            pszOrg++;
                            cSlashes++;
                        }
                        if (ch == '"' || ch == '\0') /* We put a " at the EOS. */
                        {
                            while (cSlashes-- > 0)
                            {
                                *pszNew++ = '\\';
                                *pszNew++ = '\\';
                            }
                        }
                        else
                            while (cSlashes-- > 0)
                                *pszNew++ = '\\';
                    }
                    else
                        *pszNew++ = ch;
                }
            }
            else
            {
                memcpy(pszNew, pszOrg, cchOrg);
                pszNew += cchOrg;
            }
            *pszNew++ = '"';
            *pszNew = '\0';

            if (fFreeOrLeak)
                free(pszOrgOrg);
        }
    }

    /*for (i = 0; i < argc; i++) fprintf(stderr, "argv[%u]=%s;;\n", i, argv[i]);*/
}

