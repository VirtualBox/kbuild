/* $Id: startuphacks-win.c 2243 2009-01-10 02:24:02Z knut.osmundsen@oracle.com $ */
/** @file
 * kBuild - Alternative argument parser for the windows startup code.
 *
 * @todo Update license when SED is updated.
 */

/*
 * Copyright (c) 2006-2009 knut st. osmundsen <bird-kBuild-spamix@anduin.net>
 *
 * parse_args(): Copyright (c) 1992-1998 by Eberhard Mattes
 *
 *
 * This file is part of kBuild.
 *
 * kBuild is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * kBuild is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with kBuild; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
#include <stdlib.h>
#include <malloc.h>
#include <Windows.h>


/*******************************************************************************
*   Internal Functions                                                         *
*******************************************************************************/
static int parse_args(const char *pszSrc, char **argv, char *pchPool);


/*******************************************************************************
*   Global Variables                                                           *
*******************************************************************************/
/** argument count found by parse_args(). */
static int  g_cArgs = 0;
/** the argument vector, for __getmainargs(). */
static char **g_papszArgs = NULL;



int __cdecl _setargv(void)
{
    static char s_szProgramName[MAX_PATH + 1];
    const char *pszCmdLine;
    char       *pszCmdLineBuf;
    int         cb;

    /*
     * Set the program name.
     */
    GetModuleFileName(NULL, s_szProgramName, MAX_PATH);
    s_szProgramName[MAX_PATH] = '\0';
#if _MSC_VER >= 1400 && !defined(CRTDLL) && !defined(_DLL)
    _set_pgmptr(s_szProgramName);
#endif

    /*
     * Get the commandline, use the program name if nothings available.
     */
    pszCmdLine = (const char *)GetCommandLineA();
    if (!pszCmdLine || !*pszCmdLine)
        pszCmdLine = s_szProgramName;

    /*
     * Parse the argument commandline emitting the unix argument vector.
     */
    cb = parse_args(pszCmdLine, NULL, NULL);
    g_papszArgs = malloc(sizeof(*g_papszArgs) * (g_cArgs + 2));
    if (!g_papszArgs)
        return -1;
    pszCmdLineBuf = malloc(cb);
    if (!pszCmdLineBuf)
        return -1;
    parse_args(pszCmdLine, g_papszArgs, pszCmdLineBuf);
    g_papszArgs[g_cArgs] = g_papszArgs[g_cArgs + 1] = NULL;

    /* set return variables */
    __argc = g_cArgs;
    __argv = g_papszArgs;
    return 0;
}


/* when linking with the crtexe.c, the __getmainargs() call will redo the _setargv job inside the msvc*.dll. */
int __cdecl __getmainargs(int *pargc, char ***pargv, char ***penvp, int dowildcard, /*_startupinfo*/ void *startinfo)
{
    __argc = *pargc = g_cArgs;
    __argv = *pargv = g_papszArgs;
    *penvp = _environ;
    return 0;
}

#if defined(_M_IX86)
int (__cdecl * _imp____getmainargs)(int *, char ***, char ***, int, /*_startupinfo*/ void *) = __getmainargs;
#else
int (__cdecl * __imp___getmainargs)(int *, char ***, char ***, int, /*_startupinfo*/ void *) = __getmainargs;
#endif



/**
 * Parses the argument string passed in as pszSrc.
 *
 * @returns size of the processed arguments.
 * @param   pszSrc  Pointer to the commandline that's to be parsed.
 * @param   argv    Pointer to argument vector to put argument pointers in. NULL allowed.
 * @param   pchPool Pointer to memory pchPool to put the arguments into. NULL allowed.
 */
static int parse_args(const char *pszSrc, char **argv, char *pchPool)
{
    int   bs;
    char  chQuote;
    char *pfFlags;
    int   cbArgs;

#define PUTC(c) do { ++cbArgs; if (pchPool != NULL) *pchPool++ = (c); } while (0)
#define PUTV    do { ++g_cArgs; if (argv != NULL) *argv++ = pchPool; } while (0)
#define WHITE(c) ((c) == ' ' || (c) == '\t')

#define _ARG_DQUOTE   0x01          /* Argument quoted (")                  */
#define _ARG_RESPONSE 0x02          /* Argument read from response file     */
#define _ARG_WILDCARD 0x04          /* Argument expanded from wildcard      */
#define _ARG_ENV      0x08          /* Argument from environment            */
#define _ARG_NONZERO  0x80          /* Always set, to avoid end of string   */

    g_cArgs = 0; cbArgs = 0;

#if 0
    /* argv[0] */
    PUTC((char)_ARG_NONZERO);
    PUTV;
    for (;;)
    {
        PUTC(*pszSrc);
        if (*pszSrc == 0)
            break;
        ++pszSrc;
    }
    ++pszSrc;
#endif

    for (;;)
    {
        while (WHITE(*pszSrc))
            ++pszSrc;
        if (*pszSrc == 0)
            break;
        pfFlags = pchPool;
        PUTC((char)_ARG_NONZERO);
        PUTV;
        bs = 0; chQuote = 0;
        for (;;)
        {
            if (!chQuote ? (*pszSrc == '"' || *pszSrc == '\'') : *pszSrc == chQuote)
            {
                while (bs >= 2)
                {
                    PUTC('\\');
                    bs -= 2;
                }
                if (bs & 1)
                    PUTC(*pszSrc);
                else
                {
                    chQuote = chQuote ? 0 : *pszSrc;
                    if (pfFlags != NULL)
                        *pfFlags |= _ARG_DQUOTE;
                }
                bs = 0;
            }
            else if (*pszSrc == '\\')
                ++bs;
            else
            {
                while (bs != 0)
                {
                    PUTC('\\');
                    --bs;
                }
                if (*pszSrc == 0 || (WHITE(*pszSrc) && !chQuote))
                    break;
                PUTC(*pszSrc);
            }
            ++pszSrc;
        }
        PUTC(0);
    }
    return cbArgs;
}

