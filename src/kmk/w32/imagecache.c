/* $Id: imagecache.c 2640 2012-09-09 01:49:16Z knut.osmundsen@oracle.com $ */
/** @file
 * kBuild specific executable image cache for Windows.
 */

/*
 * Copyright (c) 2012 knut st. osmundsen <bird-kBuild-spamx@anduin.net>
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

/* No GNU coding style here! */

/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
#include "make.h"

#include <Windows.h>


/*******************************************************************************
*   Structures and Typedefs                                                    *
*******************************************************************************/
typedef struct EXECCACHEENTRY
{
    /** The name hash value. */
    unsigned                uHash;
    /** The name length. */
    unsigned                cchName;
    /** Pointer to the next name with the same hash. */
    struct EXECCACHEENTRY  *pNext;
    /** When it was last referenced. */
    unsigned                uLastRef;
    /** The module handle. */
    HMODULE                 hmod1;
    /** The module handle. */
    HMODULE                 hmod2;
    /** The executable path. */
    char                    szName[1];
} EXECCACHEENTRY;
typedef EXECCACHEENTRY *PEXECCACHEENTRY;

/*******************************************************************************
*   Global Variables                                                           *
*******************************************************************************/
/** The number of cached images. */
static unsigned         g_cCached;
/** Used noting when entries was last used.
 * Increased on each kmk_cache_exec_image call.  */
static unsigned         g_uNow;

/** The size of the hash table. */
#define EXECCACHE_HASHTAB_SIZE  128
/** The hash table. */
static PEXECCACHEENTRY  g_apHashTab[EXECCACHE_HASHTAB_SIZE];


/* sdbm:
   This algorithm was created for sdbm (a public-domain reimplementation of
   ndbm) database library. it was found to do well in scrambling bits,
   causing better distribution of the keys and fewer splits. it also happens
   to be a good general hashing function with good distribution. the actual
   function is hash(i) = hash(i - 1) * 65599 + str[i]; what is included below
   is the faster version used in gawk. [there is even a faster, duff-device
   version] the magic constant 65599 was picked out of thin air while
   experimenting with different constants, and turns out to be a prime.
   this is one of the algorithms used in berkeley db (see sleepycat) and
   elsewhere. */

static unsigned execcache_calc_hash(const char *psz, unsigned *pcch)
{
    unsigned char  *puch = (unsigned char *)psz;
    unsigned        hash = 0;
    int             ch;

    while ((ch = *puch++))
        hash = ch + (hash << 6) + (hash << 16) - hash;

    *pcch = (unsigned)(puch - psz - 1);
    return hash;
}


extern void kmk_cache_exec_image(const char *pszExec)
{
    /*
     * Lookup the name.
     */
    unsigned            cchName;
    const unsigned      uHash = execcache_calc_hash(pszExec, &cchName);
    PEXECCACHEENTRY    *ppCur = &g_apHashTab[uHash % EXECCACHE_HASHTAB_SIZE];
    PEXECCACHEENTRY     pCur  = *ppCur;
    while (pCur)
    {
        if (   pCur->uHash   == uHash
            && pCur->cchName == cchName
            && !memcmp(pCur->szName, pszExec, cchName))
        {
            pCur->uLastRef = ++g_uNow;
            return;
        }
        ppCur = &pCur->pNext;
        pCur = pCur->pNext;
    }

    /*
     * Not found, create a new entry.
     */
    pCur = xmalloc(sizeof(*pCur) + cchName);
    pCur->uHash    = uHash;
    pCur->cchName  = cchName;
    pCur->pNext    = NULL;
    pCur->uLastRef = ++g_uNow;
    memcpy(pCur->szName, pszExec, cchName + 1);
    pCur->hmod1 = LoadLibraryEx(pszExec, NULL, LOAD_LIBRARY_AS_DATAFILE);
    if (pCur->hmod1 != NULL)
        pCur->hmod2 = LoadLibraryEx(pszExec, NULL, DONT_RESOLVE_DLL_REFERENCES);
    else
        pCur->hmod2 = NULL;

    *ppCur = pCur;
    g_cCached++;
}

