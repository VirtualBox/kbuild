/* $Id: imagecache.c 3195 2018-03-27 18:09:23Z knut.osmundsen@oracle.com $ */
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

/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include "makeint.h"

#include <Windows.h>


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
typedef struct EXECCACHEENTRY
{
    /** The name hash value. */
    unsigned                uHash;
    /** The name length. */
    unsigned                cwcName;
    /** Pointer to the next name with the same hash. */
    struct EXECCACHEENTRY  *pNext;
    /** When it was last referenced. */
    unsigned                uLastRef;
    /** The module handle, LOAD_LIBRARY_AS_DATAFILE. */
    HMODULE                 hmod1;
    /** The module handle, DONT_RESOLVE_DLL_REFERENCES. */
    HMODULE                 hmod2;
    /** The executable path. */
    wchar_t                 wszName[1];
} EXECCACHEENTRY;
typedef EXECCACHEENTRY *PEXECCACHEENTRY;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** Critical section serializing all access. */
static CRITICAL_SECTION g_CritSect;
/** Set if initialized. */
static int volatile     g_fInitialized = 0;
/** The number of cached images. */
static unsigned         g_cCached;
/** Used noting when entries was last used.
 * Increased on each kmk_cache_exec_image call.  */
static unsigned         g_uNow;

/** The size of the hash table. */
#define EXECCACHE_HASHTAB_SIZE  128
/** The hash table. */
static PEXECCACHEENTRY  g_apHashTab[EXECCACHE_HASHTAB_SIZE];


/** A sleepy approach to do-once. */
static void kmk_cache_lazy_init(void)
{
    if (_InterlockedCompareExchange(&g_fInitialized, -1, 0) == 0)
    {
        InitializeCriticalSection(&g_CritSect);
        _InterlockedExchange(&g_fInitialized, 1);
    }
    else
        while (g_fInitialized != 1)
            Sleep(1);
}


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

static unsigned execcache_calc_hash(const wchar_t *pwsz, size_t *pcch)
{
    wchar_t const  * const pwszStart = pwsz;
    unsigned               hash = 0;
    int                    ch;

    while ((ch = *pwsz++) != L'\0')
        hash = ch + (hash << 6) + (hash << 16) - hash;

    *pcch = (size_t)(pwsz - pwszStart - 1);
    return hash;
}

/**
 * Caches two memory mappings of the specified image so that it isn't flushed
 * from the kernel's cache mananger.
 *
 * Not sure exactly how much this actually helps, but whatever...
 *
 * @param   pwszExec    The executable.
 */
extern void kmk_cache_exec_image_w(const wchar_t *pwszExec)
{
    /*
     * Prepare name lookup and to lazy init.
     */
    size_t              cwcName;
    const unsigned      uHash = execcache_calc_hash(pwszExec, &cwcName);
    PEXECCACHEENTRY    *ppCur = &g_apHashTab[uHash % EXECCACHE_HASHTAB_SIZE];
    PEXECCACHEENTRY     pCur;

    if (g_fInitialized != 1)
        kmk_cache_lazy_init();

    /*
     * Do the lookup.
     */
    EnterCriticalSection(&g_CritSect);
    pCur = *ppCur;
    while (pCur)
    {
        if (   pCur->uHash   == uHash
            && pCur->cwcName == cwcName
            && !memcmp(pCur->wszName, pwszExec, cwcName * sizeof(wchar_t)))
        {
            pCur->uLastRef = ++g_uNow;
            LeaveCriticalSection(&g_CritSect);
            return;
        }
        ppCur = &pCur->pNext;
        pCur = pCur->pNext;
    }
    LeaveCriticalSection(&g_CritSect);

    /*
     * Not found, create a new entry.
     */
    pCur = xmalloc(sizeof(*pCur) + cwcName * sizeof(wchar_t));
    pCur->uHash    = uHash;
    pCur->cwcName  = (unsigned)cwcName;
    pCur->pNext    = NULL;
    pCur->uLastRef = ++g_uNow;
    memcpy(pCur->wszName, pwszExec, (cwcName + 1) * sizeof(wchar_t));
    pCur->hmod1 = LoadLibraryExW(pwszExec, NULL, LOAD_LIBRARY_AS_DATAFILE);
    if (pCur->hmod1 != NULL)
        pCur->hmod2 = LoadLibraryExW(pwszExec, NULL, DONT_RESOLVE_DLL_REFERENCES);
    else
        pCur->hmod2 = NULL;

    /*
     * Insert it.
     * Take into account that we might've been racing other threads,
     * fortunately we don't evict anything from the cache.
     */
    EnterCriticalSection(&g_CritSect);
    if (*ppCur != NULL)
    {
        /* Find new end of chain and check for duplicate. */
        PEXECCACHEENTRY pCur2 = *ppCur;
        while (pCur2)
        {
            if (   pCur->uHash   == uHash
                && pCur->cwcName == cwcName
                && !memcmp(pCur->wszName, pwszExec, cwcName * sizeof(wchar_t)))
                break;
            ppCur = &pCur->pNext;
            pCur = pCur->pNext;
        }

    }
    if (*ppCur == NULL)
    {
        *ppCur = pCur;
        g_cCached++;
        LeaveCriticalSection(&g_CritSect);
    }
    else
    {
        LeaveCriticalSection(&g_CritSect);

        if (pCur->hmod1 != NULL)
            FreeLibrary(pCur->hmod1);
        if (pCur->hmod2 != NULL)
            FreeLibrary(pCur->hmod2);
        free(pCur);
    }
}

extern void kmk_cache_exec_image_a(const char *pszExec)
{
    wchar_t wszExec[260];
    int cwc = MultiByteToWideChar(CP_ACP, 0 /*fFlags*/, pszExec, strlen(pszExec) + 1, wszExec, 260);
    if (cwc > 0)
        kmk_cache_exec_image_w(wszExec);
}

