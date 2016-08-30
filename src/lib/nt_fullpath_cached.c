/* $Id: nt_fullpath_cached.c 2849 2016-08-30 14:28:46Z knut.osmundsen@oracle.com $ */
/** @file
 * fixcase - fixes the case of paths, windows specific.
 */

/*
 * Copyright (c) 2004-2010 knut st. osmundsen <bird-kBuild-spamx@anduin.net>
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
#include <Windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <direct.h>
#include <assert.h>

#include "nt_fullpath.h"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
typedef struct NTFULLPATHENTRY
{
    /** Pointer to the next entry with the same hash table index. */
    struct NTFULLPATHENTRY *pNext;
    /** The input hash. */
    unsigned                uHash;
    /** The input length. */
    unsigned                cchInput;
    /** Length of the result. */
    unsigned                cchResult;
    /** The result string (stored immediately after this structure). */
    const char             *pszResult;
    /** The input string (variable length). */
    char                    szInput[1];
} NTFULLPATHENTRY;
typedef NTFULLPATHENTRY *PNTFULLPATHENTRY;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** Number of result in the nt_fullpath cache.  */
size_t              g_cNtFullPathHashEntries = 0;
/** Number of bytes used for nt_fullpath cache result entries. */
size_t              g_cbNtFullPathHashEntries = 0;
/** Number of hash table collsioins in the nt_fullpath cache.  */
size_t              g_cNtFullPathHashCollisions = 0;
/** Hash table. */
PNTFULLPATHENTRY    g_apNtFullPathHashTab[16381];


/**
 * A nt_fullpath frontend which caches the result of previous calls.
 */
void
nt_fullpath_cached(const char *pszPath, char *pszFull, size_t cchFull)
{
    PNTFULLPATHENTRY        pEntry;
    unsigned                cchInput;
    unsigned                idx;
    unsigned                cchResult;

    /* We use the sdbm hash algorithm here (see kDep.c for full details). */
    unsigned const char    *puch = (unsigned const char *)pszPath;
    unsigned                uHash = 0;
    unsigned                uChar;
    while ((uChar = *puch++) != 0)
        uHash = uChar + (uHash << 6) + (uHash << 16) - uHash;

    cchInput = (unsigned)((uintptr_t)&puch[-1] - (uintptr_t)pszPath);

    /* Do the cache lookup. */
    idx = uHash % (sizeof(g_apNtFullPathHashTab) / sizeof(g_apNtFullPathHashTab[0]));
    for (pEntry = g_apNtFullPathHashTab[idx]; pEntry != NULL; pEntry = pEntry->pNext)
        if (   pEntry->uHash == uHash
            && pEntry->cchInput == cchInput
            && memcmp(pEntry->szInput, pszPath, cchInput) == 0)
        {
            if (cchFull > pEntry->cchResult)
                memcpy(pszFull, pEntry->pszResult, pEntry->cchResult + 1);
            else
            {
                assert(0);
                memcpy(pszFull, pEntry->pszResult, cchFull);
                pszFull[cchFull - 1] = '\0';
            }
            return;
        }

    /* Make the call... */
    nt_fullpath(pszPath, pszFull, cchFull);

    /* ... and cache the result. */
    cchResult = (unsigned)strlen(pszFull);
    pEntry = malloc(sizeof(*pEntry) + cchInput + cchResult + 1);
    if (pEntry)
    {
        g_cbNtFullPathHashEntries += sizeof(*pEntry) + cchInput + cchResult + 1;
        pEntry->cchInput  = cchInput;
        pEntry->cchResult = cchResult;
        pEntry->pszResult = &pEntry->szInput[cchInput + 1];
        pEntry->uHash     = uHash;
        memcpy(pEntry->szInput, pszPath, cchInput + 1);
        memcpy((char *)pEntry->pszResult, pszFull, cchResult + 1);

        pEntry->pNext = g_apNtFullPathHashTab[idx];
        if (pEntry->pNext)
            g_cNtFullPathHashCollisions++;
        g_apNtFullPathHashTab[idx] = pEntry;

        g_cNtFullPathHashEntries++;
    }
}

