/* $Id: kbuild_protection.c 2001 2008-10-29 22:14:48Z knut.osmundsen@oracle.com $ */
/** @file
 * Simple File Protection.
 */

/*
 * Copyright (c) 2008 knut st. osmundsen <bird-src-spam@anduin.net>
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */


/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
#include <sys/types.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>
#if defined(_MSC_VER) || defined(__OS2__)
# include <limits.h>
# include <direct.h>
#else
# include <unistd.h>
#endif
#include "kbuild_protection.h"
#include "err.h"


/*******************************************************************************
*   Defined Constants And Macros                                               *
*******************************************************************************/
#define KBUILD_PROTECTION_MAGIC     0x00111100

#if defined(__EMX__) || defined(_MSC_VER)
# define IS_SLASH(ch)               ( (ch) == '/' || (ch) == '\\' )
# define DEFAULT_PROTECTION_DEPTH   1
#else
# define IS_SLASH(ch)               ( (ch) == '/' )
# define DEFAULT_PROTECTION_DEPTH   2
#endif



/**
 * Counts the components in the specified sub path.
 * This is a helper for count_path_components.
 *
 * etc = 1
 * etc/ = 1
 * etc/x11 = 2
 * and so and and so forth.
 */
static int countSubPathComponents(const char *pszPath, int cDepth)
{
    for (;;)
    {
        const char *pszEnd;
        size_t cch;

        /* skip slashes. */
        while (IS_SLASH(*pszPath))
            pszPath++;
        if (!*pszPath)
            break;

        /* find end of component. */
        pszEnd = pszPath;
        while (!IS_SLASH(*pszEnd) && *pszEnd)
            pszEnd++;

        /* count it, checking for '..' and '.'. */
        cch = pszEnd - pszPath;
        if (cch == 2 && pszPath[0] == '.' && pszPath[1] == '.')
        {
            if (cDepth > 0)
                cDepth--;
        }
        else if (cch != 1 || pszPath[0] != '.')
            cDepth++;

        /* advance */
        if (!*pszEnd)
            break;
        pszPath = pszEnd + 1;
    }
    return cDepth;
}


/**
 * Parses the specified path counting the number of components
 * relative to root.
 *
 * We don't check symbolic links and such, just some simple and cheap
 * path parsing.
 *
 * @param   pszPath     The path to process.
 *
 * @returns 0 or higher on success.
 *          On failure an error is printed, eval is set and -1 is returned.
 */
static int countPathComponents(const char *pszPath)
{
    int cComponents = 0;

    /*
     * Deal with root, UNC, drive letter.
 */
#if defined(_MSC_VER) || defined(__OS2__)
    if (IS_SLASH(pszPath[0]) && IS_SLASH(pszPath[1]) && !IS_SLASH(pszPath[2]))
    {
        /* skip the root - UNC */
        pszPath += 3;
        while (!IS_SLASH(*pszPath) && *pszPath) /* server name */
            pszPath++;
        while (IS_SLASH(*pszPath))
            pszPath++;
        while (!IS_SLASH(*pszPath) && *pszPath) /* share name */
            pszPath++;
        while (IS_SLASH(*pszPath))
            pszPath++;
    }
    else
    {
        unsigned uDriveLetter = (unsigned)toupper(pszPath[0]) - (unsigned)'A';
        if (uDriveLetter <= (unsigned)('Z' - 'A') && pszPath[1] == ':')
            uDriveLetter++; /* A == 1 */
        else
            uDriveLetter = 0; /* 0 == default */

        if (!IS_SLASH(pszPath[uDriveLetter ? 2 : 0]))
        {
            /*
             * Relative path, must count cwd depth first.
             */
#ifdef __OS2__ /** @todo remove when ticket 194 has been fixed */
            char *pszCwd = _getdcwd(uDriveLetter, NULL, PATH_MAX);
#else
            char *pszCwd = _getdcwd(uDriveLetter, NULL, 0);
#endif
            char *pszTmp = pszCwd;
            if (!pszTmp)
            {
                err(1, "_getdcwd");
                return -1;
            }

            if (IS_SLASH(pszTmp[0]) && IS_SLASH(pszTmp[1]))
            {
                /* skip the root - UNC */
                pszTmp += 2;
                while (!IS_SLASH(*pszTmp) && *pszTmp) /* server name */
                    pszTmp++;
                while (IS_SLASH(*pszTmp))
                    pszTmp++;
                while (!IS_SLASH(*pszTmp) && *pszTmp) /* share name */
                    pszTmp++;
            }
            else
            {
                /* skip the drive letter and while we're at it, the root slash too. */
                pszTmp += 1 + (pszTmp[1] == ':');
            }
            cComponents = countSubPathComponents(pszTmp, 0);
            free(pszCwd);
        }
        else
        {
            /* skip the drive letter and while we're at it, the root slash too. */
            pszPath += uDriveLetter ? 3 : 1;
        }
    }
#else  /* !WIN && !OS2 */
    if (!IS_SLASH(pszPath[0]))
    {
        /*
         * Relative path, must count cwd depth first.
         */
        char szCwd[4096];
        if (!getcwd(szCwd, sizeof(szCwd)))
        {
            err(1, "getcwd");
            return -1;
        }
        cComponents = countSubPathComponents(szCwd, 0);
    }
#endif /* !WIN && !OS2 */

    /*
     * We're now past any UNC or drive letter crap, possibly positioned
     * at the root slash or at the start of a path component at the
     * given depth. Count the remainder.
     */
    return countSubPathComponents(pszPath, cComponents);
}


/**
 * Initializes the instance data.
 *
 * @param   pThis   Pointer to the instance data.
 */
void kBuildProtectionInit(PKBUILDPROTECTION pThis)
{
    pThis->uMagic = KBUILD_PROTECTION_MAGIC;
    pThis->afTypes[KBUILDPROTECTIONTYPE_FULL] = 0;
    pThis->afTypes[KBUILDPROTECTIONTYPE_RECURSIVE] = 1;
    pThis->cProtectionDepth = DEFAULT_PROTECTION_DEPTH;
}


/**
 * Destroys the instance data.
 *
 * @param   pThis   Pointer to the instance data.
 */
void kBuildProtectionTerm(PKBUILDPROTECTION pThis)
{
    pThis->uMagic = 0;
}


void kBuildProtectionEnable(PKBUILDPROTECTION pThis, KBUILDPROTECTIONTYPE enmType)
{
    assert(pThis->uMagic == KBUILD_PROTECTION_MAGIC);
    assert(enmType < KBUILDPROTECTIONTYPE_MAX && enmType >= KBUILDPROTECTIONTYPE_FIRST);
    pThis->afTypes[enmType] |= 1;
}


void kBuildProtectionDisable(PKBUILDPROTECTION pThis, KBUILDPROTECTIONTYPE enmType)
{
    assert(pThis->uMagic == KBUILD_PROTECTION_MAGIC);
    assert(enmType < KBUILDPROTECTIONTYPE_MAX && enmType >= KBUILDPROTECTIONTYPE_FIRST);
    pThis->afTypes[enmType] &= ~1U;
}


/**
 * Sets the protection depth according to the option argument.
 *
 * @param   pszValue    The value.
 *
 * @returns 0 on success, -1 and errx on failure.
 */
int  kBuildProtectionSetDepth(PKBUILDPROTECTION pThis, const char *pszValue)
{
    /* skip leading blanks, they don't count either way. */
    while (isspace(*pszValue))
        pszValue++;

    /* number or path? */
    if (!isdigit(*pszValue) || strpbrk(pszValue, ":/\\"))
        pThis->cProtectionDepth = countPathComponents(pszValue);
    else
    {
        char *pszMore = 0;
        pThis->cProtectionDepth = strtol(pszValue, &pszMore, 0);
        if (pThis->cProtectionDepth != 0 && pszMore)
        {
            /* trailing space is harmless. */
            while (isspace(*pszMore))
                pszMore++;
        }
        if (!pThis->cProtectionDepth || pszValue == pszMore || *pszMore)
            return errx(1, "bogus protection depth: %s", pszValue);
    }

    if (pThis->cProtectionDepth < 1)
        return  errx(1, "bogus protection depth: %s", pszValue);
    return 0;
}


/**
 * Scans the environment for option overrides.
 *
 * @param   pThis       Pointer to the instance data.
 * @param   papszEnv    The environment array.
 * @param   pszPrefix   The variable prefix.
 *
 * @returns 0 on success, -1 and err*() on failure.
 */
int  kBuildProtectionScanEnv(PKBUILDPROTECTION pThis, char **papszEnv, const char *pszPrefix)
{
    unsigned i;
    const size_t cchPrefix = strlen(pszPrefix);

    for (i = 0; papszEnv[i]; i++)
    {
        const char *pszVar = papszEnv[i];
        if (!strncmp(pszVar, pszPrefix, cchPrefix))
        {
            pszVar += cchPrefix;
            if (!strncmp(pszVar, "PROTECTION_DEPTH=", sizeof("PROTECTION_DEPTH=") - 1))
            {
                const char *pszVal = pszVar + sizeof("PROTECTION_DEPTH=") - 1;
                if (kBuildProtectionSetDepth(pThis, pszVal))
                    return -1;
            }
            else if (!strncmp(pszVar, "DISABLE_PROTECTION=", sizeof("DISABLE_PROTECTION=") - 1))
                pThis->afTypes[KBUILDPROTECTIONTYPE_RECURSIVE] &= ~1U;
            else if (!strncmp(pszVar, "ENABLE_PROTECTION=", sizeof("ENABLE_PROTECTION=") - 1))
                pThis->afTypes[KBUILDPROTECTIONTYPE_RECURSIVE] |= 3;
            else if (!strncmp(pszVar, "DISABLE_FULL_PROTECTION=", sizeof("DISABLE_FULL_PROTECTION=") - 1))
                pThis->afTypes[KBUILDPROTECTIONTYPE_FULL] &= ~1U;
            else if (!strncmp(pszVar, "ENABLE_FULL_PROTECTION=", sizeof("ENABLE_FULL_PROTECTION=") - 1))
                pThis->afTypes[KBUILDPROTECTIONTYPE_FULL] |= 3;
        }
    }
    return 0;
}


/**
 * Protect the upper layers of the file system against accidental
 * or malicious deletetion attempt from within a makefile.
 *
 * @param   pszPath             The path to check.
 * @param   required_depth      The minimum number of components in the
 *                              path counting from the root.
 *
 * @returns 0 on success.
 *          On failure an error is printed and -1 is returned.
 */
int  kBuildProtectionEnforce(PCKBUILDPROTECTION pThis, KBUILDPROTECTIONTYPE enmType, const char *pszPath)
{
    assert(pThis->uMagic == KBUILD_PROTECTION_MAGIC);
    assert(enmType < KBUILDPROTECTIONTYPE_MAX && enmType >= KBUILDPROTECTIONTYPE_FIRST);

    if (   (pThis->afTypes[enmType] & 3)
        || (pThis->afTypes[KBUILDPROTECTIONTYPE_FULL] & 3))
    {
        /*
         * Count the path and compare it with the required depth.
         */
        int cComponents = countPathComponents(pszPath);
        if (cComponents < 0)
            return -1;
        if ((unsigned int)cComponents <= pThis->cProtectionDepth)
        {
            errx(1, "%s: protected", pszPath);
            return -1;
        }
    }
    return 0;
}


/**
 * Retrieve the default path protection depth.
 *
 * @returns the default value.
 */
int  kBuildProtectionDefaultDepth(void)
{
    return DEFAULT_PROTECTION_DEPTH;
}

