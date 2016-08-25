/* $Id: submit.c 2840 2016-08-25 21:47:04Z knut.osmundsen@oracle.com $ */
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

#include "kmkbuiltin.h"
#include "err.h"

#ifdef __OS2__
# define INCL_BASE
# include <os2.h>
# ifndef LIBPATHSTRICT
#  define LIBPATHSTRICT 3
# endif
#endif

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
    /** 32 or 64. */
    unsigned                cBits;
    /** The process handle. */
    HANDLE                  hProcess;

} WORKERINSTANCE;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
static PWORKERINSTANCE g_pIdleHead;
static PWORKERINSTANCE g_pIdleTail;



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
    char          **papszEnv = NULL;
    const char     *pszCwd = NULL;
    unsigned        cBitsWorker = 0;
    int             fWatcomBrainDamage = 0;
    int             cVerbosity = 0;
    size_t const    cbCwdBuf = GET_PATH_MAX;
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
                    {
                        const char *pszEqual = strchr(pszValue, '=');
                        if (pszEqual)
                        {
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
                                if ((cEnvVars + 2) > cAllocatedEnvVars)
                                {
                                    cAllocatedEnvVars = (cEnvVars + 2 + 0xf) & ~(unsigned)0xf;
                                    pChild->environment = papszEnv = (char **)xrealloc(papszEnv,
                                                                                       cAllocatedEnvVars * sizeof(papszEnv[0]));
                                }
                                papszEnv[cEnvVars++] = xstrdup(pszValue);
                                papszEnv[cEnvVars]   = NULL;
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
                        break;
                    }

                    case 'U':
                    {
                        if (strchr(pszValue, '=') == NULL)
                        {
                            unsigned     cRemoved = 0;
                            size_t const cchVar = strlen(pszValue);
                            for (iEnvVar = 0; iEnvVar < cEnvVars; iEnvVar++)
                                if (   strncmp(papszEnv[iEnvVar], pszValue, cchVar) == 0
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
                            if (cVerbosity > 0 && !cRemoved)
                                fprintf(stderr, "kSubmit: not found '%s'\n", pszValue);
                        }
                        else
                            return errx(1, "Found invalid variable name character '=' in: -U %s", pszValue);
                        break;
                    }

                    case 'C':
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
                                else if (szCwd[1] == ':' && isalpha(szCwd[0]))
                                    offDst = 2; /* Take drive letter from CWD. */
                                else
                                    return errx(1, "UNC relative CWD not implemented: cur='%s' new='%s'", szCwd, pszValue);
                            }
                            else if (   pszValue[1] == ':'
                                     && isalpha(pszValue[0]))
                            {
                                if (pszValue[2] == '/'|| pszValue[2] == '\\')
                                    offDst = 0; /* DOS style absolute path. */
                                else if (   szCwd[1] == ':'
                                         && tolower(szCwd[0]) == tolower(pszValue[0]) )
                                {
                                    pszValue += 2; /* Same drive as CWD, append drive relative path from value. */
                                    cchNewCwd -= 2;
                                    offDst = strlen(szCwd);
                                }
                                else
                                {
                                    /* Get current CWD on the specified drive and append value. */
                                    int iDrive = tolower(pszValue[0]) - 'a' + 1;
                                    if (!_getdcwd(iDrive, szCwd, cbCwdBuf))
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
                                offDst = strlen(szCwd); /* Relative path, append to the existing CWD value. */

                            /* Do the copying. */
#ifdef HAVE_DOS_PATHS
                            if (offDst > 0 && szCwd[offDst - 1] != '/' && szCwd[offDst - 1] != '\\')
#else
                            if (offDst > 0 && szCwd[offDst - 1] != '/')
#endif
                                 szCwd[offDst++] = '/';
                            if (offDst + cchNewCwd >= cbCwdBuf)
                                return errx(1, "Too long CWD: %*.*s%s", offDst, offDst, szCwd, pszValue);
                            memcpy(&szCwd[offDst], pszValue, cchNewCwd + 1);
                        }
                        /* else: relative, no change - quitely ignore. */
                        break;
                    }

                    case '3':
                        cBitsWorker = 32;
                        break;

                    case '6':
                        cBitsWorker = 64;
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

    }
    else
    {
        errx(1, "Nothing to executed!");
        rcExit = usage(stderr, argv[0]);
    }

    return rcExit;
}



