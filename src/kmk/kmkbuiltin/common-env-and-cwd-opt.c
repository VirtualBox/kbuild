/* $Id: common-env-and-cwd-opt.c 3332 2020-04-19 23:08:16Z knut.osmundsen@oracle.com $ */
/** @file
 * kMk Builtin command - Commmon environment and CWD option handling code.
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
#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "kmkbuiltin.h"
#include "err.h"


/** The environment variable compare function.
 * We must use case insensitive compare on windows (Path vs PATH).  */
#ifdef KBUILD_OS_WINDOWS
# define KSUBMIT_ENV_NCMP   _strnicmp
#else
# define KSUBMIT_ENV_NCMP   strncmp
#endif


/**
 * Duplicates a read-only enviornment vector.
 *
 * @returns The duplicate enviornment.
 * @param   pCtx                The built-in command context.
 * @param   papszEnv            The read-only vector.
 * @param   cEnvVars            The number of variables.
 * @param   pcAllocatedEnvVars  The allocated papszEnv size.  This is zero on
 *                              input and non-zero on successful return.
 * @param   cVerbosity          The verbosity level.
 */
static char **kBuiltinOptEnvDuplicate(PKMKBUILTINCTX pCtx, char **papszEnv, unsigned cEnvVars, unsigned *pcAllocatedEnvVars,
                                      int cVerbosity)
{
    unsigned cAllocatedEnvVars = (cEnvVars + 2 + 0xf) & ~(unsigned)0xf;
    char    **papszEnvNew      = malloc(cAllocatedEnvVars * sizeof(papszEnvNew[0]));
    assert(*pcAllocatedEnvVars == 0);
    if (papszEnvNew)
    {
        unsigned i;
        for (i = 0; i < cEnvVars; i++)
        {
            papszEnvNew[i] = strdup(papszEnv[i]);
            if (!papszEnvNew)
            {
                while (i-- > 0)
                    free(papszEnvNew[i]);
                free(papszEnvNew);
                errx(pCtx, 1, "out of memory for duplicating environment variables!", i);
                return NULL;
            }
        }
        papszEnvNew[i] = NULL;
        *pcAllocatedEnvVars = cAllocatedEnvVars;
    }
    else
        errx(pCtx, 1, "out of memory for duplicating environment vector!");
    return papszEnvNew;
}


/**
 * Common worker for kBuiltinOptEnvSet and kBuiltinOptEnvAppendPrepend that adds
 * a new variable to the environment.
 *
 * @returns 0 on success, non-zero exit code on error.
 * @param   pCtx                The built-in command context.
 * @param   papszEnv            The environment vector.
 * @param   pcEnvVars           Pointer to the variable holding the number of
 *                              environment variables held by @a papszEnv.
 * @param   pcAllocatedEnvVars  Pointer to the variable holding max size of the
 *                              environment vector.
 * @param   cVerbosity          The verbosity level.
 * @param   pszValue            The var=value string to apply.
 */
static int kBuiltinOptEnvAddVar(PKMKBUILTINCTX pCtx, char ***ppapszEnv, unsigned *pcEnvVars, unsigned *pcAllocatedEnvVars,
                                int cVerbosity, const char *pszValue)
{
    /* Append new variable. We probably need to resize the vector. */
    char   **papszEnv = *ppapszEnv;
    unsigned cEnvVars = *pcEnvVars;
    if ((cEnvVars + 2) > *pcAllocatedEnvVars)
    {
        *pcAllocatedEnvVars = (cEnvVars + 2 + 0xf) & ~(unsigned)0xf;
        papszEnv = (char **)realloc(papszEnv, *pcAllocatedEnvVars * sizeof(papszEnv[0]));
        if (!papszEnv)
            return errx(pCtx, 1, "out of memory growing environment vector!");
        *ppapszEnv = papszEnv;
    }
    papszEnv[cEnvVars] = strdup(pszValue);
    if (!papszEnv[cEnvVars])
        return errx(pCtx, 1, "out of memory adding environment variable!");
    papszEnv[++cEnvVars]   = NULL;
    *pcEnvVars = cEnvVars;
    if (cVerbosity > 0)
        warnx(pCtx, "added '%s'", papszEnv[cEnvVars - 1]);
    return 0;
}


/**
 * Common worker for kBuiltinOptEnvSet and kBuiltinOptEnvAppendPrepend that
 * remove duplicates.
 *
 * @returns 0 on success, non-zero exit code on error.
 * @param   pCtx                The built-in command context.
 * @param   papszEnv            The environment vector.
 * @param   cEnvVars            Number of environment variables.
 * @param   cVerbosity          The verbosity level.
 * @param   pszValue            The var=value string to apply.
 * @param   cchVar              The length of the variable part of @a pszValue.
 * @param   iEnvVar             Where to start searching after.
 */
static int kBuiltinOptEnvRemoveDuplicates(PKMKBUILTINCTX pCtx, char **papszEnv, unsigned cEnvVars, int cVerbosity,
                                          const char *pszValue, size_t cchVar, unsigned iEnvVar)
{
    for (iEnvVar++; iEnvVar < cEnvVars; iEnvVar++)
        if (   KSUBMIT_ENV_NCMP(papszEnv[iEnvVar], pszValue, cchVar) == 0
            && papszEnv[iEnvVar][cchVar] == '=')
        {
            if (cVerbosity > 0)
                warnx(pCtx, "removing duplicate '%s'", papszEnv[iEnvVar]);
            free(papszEnv[iEnvVar]);
            cEnvVars--;
            if (iEnvVar != cEnvVars)
                papszEnv[iEnvVar] = papszEnv[cEnvVars];
            papszEnv[cEnvVars] = NULL;
            iEnvVar--;
        }
    return 0;
}


/**
 * Handles the --set var=value option.
 *
 * @returns 0 on success, non-zero exit code on error.
 * @param   pCtx                The built-in command context.
 * @param   ppapszEnv           The environment vector pointer.
 * @param   pcEnvVars           Pointer to the variable holding the number of
 *                              environment variables held by @a papszEnv.
 * @param   pcAllocatedEnvVars  Pointer to the variable holding max size of the
 *                              environment vector.
 * @param   cVerbosity          The verbosity level.
 * @param   pszValue            The var=value string to apply.
 */
int kBuiltinOptEnvSet(PKMKBUILTINCTX pCtx, char ***ppapszEnv, unsigned *pcEnvVars, unsigned *pcAllocatedEnvVars,
                      int cVerbosity, const char *pszValue)
{
    const char *pszEqual = strchr(pszValue, '=');
    if (pszEqual)
    {
        char   **papszEnv = *ppapszEnv;
        unsigned iEnvVar;
        unsigned cEnvVars = *pcEnvVars;
        size_t const cchVar = pszEqual - pszValue;

        if (!*pcAllocatedEnvVars)
        {
            papszEnv = kBuiltinOptEnvDuplicate(pCtx, papszEnv, cEnvVars, pcAllocatedEnvVars, cVerbosity);
            if (!papszEnv)
                return errx(pCtx, 1, "out of memory duplicating enviornment (setenv)!");
            *ppapszEnv = papszEnv;
        }

        for (iEnvVar = 0; iEnvVar < cEnvVars; iEnvVar++)
        {
            char *pszCur = papszEnv[iEnvVar];
            if (   KSUBMIT_ENV_NCMP(pszCur, pszValue, cchVar) == 0
                && pszCur[cchVar] == '=')
            {
                if (cVerbosity > 0)
                    warnx(pCtx, "replacing '%s' with '%s'", papszEnv[iEnvVar], pszValue);
                free(papszEnv[iEnvVar]);
                papszEnv[iEnvVar] = strdup(pszValue);
                if (!papszEnv[iEnvVar])
                    return errx(pCtx, 1, "out of memory for modified environment variable!");

                return kBuiltinOptEnvRemoveDuplicates(pCtx, papszEnv, cEnvVars, cVerbosity, pszValue, cchVar, iEnvVar);
            }
        }
        return kBuiltinOptEnvAddVar(pCtx, ppapszEnv, pcEnvVars, pcAllocatedEnvVars, cVerbosity, pszValue);
    }
    return errx(pCtx, 1, "Missing '=': -E %s", pszValue);
}


/**
 * Common worker for kBuiltinOptEnvAppend and kBuiltinOptEnvPrepend.
 *
 * @returns 0 on success, non-zero exit code on error.
 * @param   pCtx                The built-in command context.
 * @param   ppapszEnv           The environment vector pointer.
 * @param   pcEnvVars           Pointer to the variable holding the number of
 *                              environment variables held by @a papszEnv.
 * @param   pcAllocatedEnvVars  Pointer to the variable holding max size of the
 *                              environment vector.
 * @param   cVerbosity          The verbosity level.
 * @param   pszValue            The var=value string to apply.
 */
static int kBuiltinOptEnvAppendPrepend(PKMKBUILTINCTX pCtx, char ***ppapszEnv, unsigned *pcEnvVars, unsigned *pcAllocatedEnvVars,
                                       int cVerbosity, const char *pszValue, int fAppend)
{
    const char *pszEqual = strchr(pszValue, '=');
    if (pszEqual)
    {
        char   **papszEnv = *ppapszEnv;
        unsigned iEnvVar;
        unsigned cEnvVars = *pcEnvVars;
        size_t const cchVar = pszEqual - pszValue;

        if (!*pcAllocatedEnvVars)
        {
            papszEnv = kBuiltinOptEnvDuplicate(pCtx, papszEnv, cEnvVars, pcAllocatedEnvVars, cVerbosity);
            if (!papszEnv)
                return errx(pCtx, 1, "out of memory duplicating environment (append)!");
            *ppapszEnv = papszEnv;
        }

        for (iEnvVar = 0; iEnvVar < cEnvVars; iEnvVar++)
        {
            char *pszCur = papszEnv[iEnvVar];
            if (   KSUBMIT_ENV_NCMP(pszCur, pszValue, cchVar) == 0
                && pszCur[cchVar] == '=')
            {
                size_t cchOldValue = strlen(pszCur)   - cchVar - 1;
                size_t cchNewValue = strlen(pszValue) - cchVar - 1;
                char  *pszNew      = malloc(cchVar + 1 + cchOldValue + cchNewValue + 1);
                if (!pszNew)
                    return errx(pCtx, 1, "out of memory appending to environment variable!");
                if (fAppend)
                {
                    memcpy(pszNew, pszCur, cchVar + 1 + cchOldValue);
                    memcpy(&pszNew[cchVar + 1 + cchOldValue], &pszValue[cchVar + 1], cchNewValue + 1);
                }
                else
                {
                    memcpy(pszNew, pszCur, cchVar + 1); /* preserve variable name case  */
                    memcpy(&pszNew[cchVar + 1], &pszValue[cchVar + 1], cchNewValue);
                    memcpy(&pszNew[cchVar + 1 + cchNewValue], &pszCur[cchVar + 1], cchOldValue + 1);
                }

                if (cVerbosity > 0)
                    warnx(pCtx, "replacing '%s' with '%s'", pszCur, pszNew);
                free(pszCur);
                papszEnv[iEnvVar] = pszNew;

                return kBuiltinOptEnvRemoveDuplicates(pCtx, papszEnv, cEnvVars, cVerbosity, pszValue, cchVar, iEnvVar);
            }
        }
        return kBuiltinOptEnvAddVar(pCtx, ppapszEnv, pcEnvVars, pcAllocatedEnvVars, cVerbosity, pszValue);
    }
    return errx(pCtx, 1, "Missing '=': -%c %s", fAppend ? 'A' : 'D', pszValue);
}


/**
 * Handles the --append var=value option.
 *
 * @returns 0 on success, non-zero exit code on error.
 * @param   pCtx                The built-in command context.
 * @param   ppapszEnv           The environment vector pointer.
 * @param   pcEnvVars           Pointer to the variable holding the number of
 *                              environment variables held by @a papszEnv.
 * @param   pcAllocatedEnvVars  Pointer to the variable holding max size of the
 *                              environment vector.
 * @param   cVerbosity          The verbosity level.
 * @param   pszValue            The var=value string to apply.
 */
int kBuiltinOptEnvAppend(PKMKBUILTINCTX pCtx, char ***ppapszEnv, unsigned *pcEnvVars, unsigned *pcAllocatedEnvVars,
                         int cVerbosity, const char *pszValue)
{
    return kBuiltinOptEnvAppendPrepend(pCtx, ppapszEnv, pcEnvVars, pcAllocatedEnvVars, cVerbosity, pszValue, 1 /*fAppend*/);
}


/**
 * Handles the --prepend var=value option.
 *
 * @returns 0 on success, non-zero exit code on error.
 * @param   pCtx                The built-in command context.
 * @param   ppapszEnv           The environment vector pointer.
 * @param   pcEnvVars           Pointer to the variable holding the number of
 *                              environment variables held by @a papszEnv.
 * @param   pcAllocatedEnvVars  Pointer to the variable holding max size of the
 *                              environment vector.
 * @param   cVerbosity          The verbosity level.
 * @param   pszValue            The var=value string to apply.
 */
int kBuiltinOptEnvPrepend(PKMKBUILTINCTX pCtx, char ***ppapszEnv, unsigned *pcEnvVars, unsigned *pcAllocatedEnvVars,
                          int cVerbosity, const char *pszValue)
{
    return kBuiltinOptEnvAppendPrepend(pCtx, ppapszEnv, pcEnvVars, pcAllocatedEnvVars, cVerbosity, pszValue, 0 /*fAppend*/);
}


/**
 * Handles the --unset var option.
 *
 * @returns 0 on success, non-zero exit code on error.
 * @param   pCtx                The built-in command context.
 * @param   ppapszEnv           The environment vector pointer.
 * @param   pcEnvVars           Pointer to the variable holding the number of
 *                              environment variables held by @a papszEnv.
 * @param   pcAllocatedEnvVars  Pointer to the size of the vector allocation.
 *                              The size is zero when read-only (CRT, GNU make)
 *                              environment.
 * @param   cVerbosity          The verbosity level.
 * @param   pszVarToRemove      The name of the variable to remove.
 */
int kBuiltinOptEnvUnset(PKMKBUILTINCTX pCtx, char ***ppapszEnv, unsigned *pcEnvVars, unsigned *pcAllocatedEnvVars,
                        int cVerbosity, const char *pszVarToRemove)
{
    if (strchr(pszVarToRemove, '=') == NULL)
    {
        char       **papszEnv = *ppapszEnv;
        unsigned     cRemoved = 0;
        size_t const cchVar   = strlen(pszVarToRemove);
        unsigned     cEnvVars = *pcEnvVars;
        unsigned     iEnvVar;

        for (iEnvVar = 0; iEnvVar < cEnvVars; iEnvVar++)
            if (   KSUBMIT_ENV_NCMP(papszEnv[iEnvVar], pszVarToRemove, cchVar) == 0
                && papszEnv[iEnvVar][cchVar] == '=')
            {
                if (cVerbosity > 0)
                    warnx(pCtx, !cRemoved ? "removing '%s'" : "removing duplicate '%s'", papszEnv[iEnvVar]);

                if (!*pcAllocatedEnvVars)
                {
                    papszEnv = kBuiltinOptEnvDuplicate(pCtx, papszEnv, cEnvVars, pcAllocatedEnvVars, cVerbosity);
                    if (!papszEnv)
                        return errx(pCtx, 1, "out of memory duplicating environment (unset)!");
                    *ppapszEnv = papszEnv;
                }

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
            warnx(pCtx, "not found '%s'", pszVarToRemove);
    }
    else
        return errx(pCtx, 1, "Found invalid variable name character '=' in: -U %s", pszVarToRemove);
    return 0;
}


/**
 * Handles the --zap-env & --ignore-environment options.
 *
 * @returns 0 on success, non-zero exit code on error.
 * @param   pCtx                The built-in command context.
 * @param   ppapszEnv           The environment vector pointer.
 * @param   pcEnvVars           Pointer to the variable holding the number of
 *                              environment variables held by @a papszEnv.
 * @param   pcAllocatedEnvVars  Pointer to the size of the vector allocation.
 *                              The size is zero when read-only (CRT, GNU make)
 *                              environment.
 * @param   cVerbosity          The verbosity level.
 */
int kBuiltinOptEnvZap(PKMKBUILTINCTX pCtx, char ***ppapszEnv, unsigned *pcEnvVars, unsigned *pcAllocatedEnvVars, int cVerbosity)
{
    if (*pcAllocatedEnvVars > 0)
    {
        char **papszEnv = *ppapszEnv;
        unsigned i = *pcEnvVars;
        while (i-- > 0)
        {
            free(papszEnv[i]);
            papszEnv[i] = NULL;
        }
    }
    else
    {
        char **papszEnv = calloc(4, sizeof(char *));
        if (!papszEnv)
            return err(pCtx, 1, "out of memory!");
        *ppapszEnv = papszEnv;
        *pcAllocatedEnvVars = 4;
    }
    *pcEnvVars = 0;
    return 0;
}


/**
 * Cleans up afterwards, if necessary.
 *
 * @param   ppapszEnv           The environment vector pointer.
 * @param   cEnvVars            The number of variables in the vector.
 * @param   pcAllocatedEnvVars  Pointer to the size of the vector allocation.
 *                              The size is zero when read-only (CRT, GNU make)
 *                              environment.
 */
void kBuiltinOptEnvCleanup(char ***ppapszEnv, unsigned cEnvVars, unsigned *pcAllocatedEnvVars)
{
    char **papszEnv = *ppapszEnv;
    *ppapszEnv = NULL;
    if (*pcAllocatedEnvVars > 0)
    {
        *pcAllocatedEnvVars = 0;
        while (cEnvVars-- > 0)
        {
            free(papszEnv[cEnvVars]);
            papszEnv[cEnvVars] = NULL;
        }
        free(papszEnv);
    }
}


/**
 * Handles the --chdir dir option.
 *
 * @returns 0 on success, non-zero exit code on error.
 * @param   pCtx                The built-in command context.
 * @param   pszCwd              The CWD buffer.  Contains current CWD on input,
 *                              modified by @a pszValue on output.
 * @param   cbCwdBuf            The size of the CWD buffer.
 * @param   pszValue            The --chdir value to apply.
 */
int kBuiltinOptChDir(PKMKBUILTINCTX pCtx, char *pszCwd, size_t cbCwdBuf, const char *pszValue)
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
                return errx(pCtx, 1, "UNC relative CWD not implemented: cur='%s' new='%s'", pszCwd, pszValue);
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
                    return err(pCtx, 1, "_getdcwd(%d,,) failed", iDrive);
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
            return errx(pCtx, 1, "Too long CWD: %*.*s%s", offDst, offDst, pszCwd, pszValue);
        memcpy(&pszCwd[offDst], pszValue, cchNewCwd + 1);
    }
    /* else: relative, no change - quitely ignore. */
    return 0;
}

