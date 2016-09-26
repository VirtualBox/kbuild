/* $Id: kFsCache.c 2967 2016-09-26 18:14:13Z knut.osmundsen@oracle.com $ */
/** @file
 * ntdircache.c - NT directory content cache.
 */

/*
 * Copyright (c) 2016 knut st. osmundsen <bird-kBuild-spamx@anduin.net>
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include <k/kHlp.h>

#include "nthlp.h"
#include "ntstat.h"

#include <stdio.h>
#include <mbstring.h>
#include <wchar.h>
#ifdef _MSC_VER
# include <intrin.h>
#endif
//#include <setjmp.h>
//#include <ctype.h>


//#include <Windows.h>
//#include <winternl.h>

#include "kFsCache.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** @def KFSCACHE_LOG2
 * More logging. */
#if 0
# define KFSCACHE_LOG2(a) KFSCACHE_LOG(a)
#else
# define KFSCACHE_LOG2(a) do { } while (0)
#endif


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Used by the code re-populating a directory.
 */
typedef struct KFSDIRREPOP
{
    /** The old papChildren array. */
    PKFSOBJ    *papOldChildren;
    /** Number of children in the array. */
    KU32        cOldChildren;
    /** The index into papOldChildren we expect to find the next entry.  */
    KU32        iNextOldChild;
    /** Add this to iNextOldChild . */
    KI32        cNextOldChildInc;
    /** Pointer to the cache (name changes). */
    PKFSCACHE   pCache;
} KFSDIRREPOP;
/** Pointer to directory re-population data. */
typedef KFSDIRREPOP *PKFSDIRREPOP;



/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static KBOOL kFsCacheRefreshObj(PKFSCACHE pCache, PKFSOBJ pObj, KFSLOOKUPERROR *penmError);


/**
 * Retains a reference to a cache object, internal version.
 *
 * @returns pObj
 * @param   pObj                The object.
 */
K_INLINE PKFSOBJ kFsCacheObjRetainInternal(PKFSOBJ pObj)
{
    KU32 cRefs = ++pObj->cRefs;
    kHlpAssert(cRefs < 16384);
    K_NOREF(cRefs);
    return pObj;
}


#ifndef NDEBUG

/**
 * Debug printing.
 * @param   pszFormat           Debug format string.
 * @param   ...                 Format argument.
 */
void kFsCacheDbgPrintfV(const char *pszFormat, va_list va)
{
    if (1)
    {
        DWORD const dwSavedErr = GetLastError();

        fprintf(stderr, "debug: ");
        vfprintf(stderr, pszFormat, va);

        SetLastError(dwSavedErr);
    }
}


/**
 * Debug printing.
 * @param   pszFormat           Debug format string.
 * @param   ...                 Format argument.
 */
void kFsCacheDbgPrintf(const char *pszFormat, ...)
{
    if (1)
    {
        va_list va;
        va_start(va, pszFormat);
        kFsCacheDbgPrintfV(pszFormat, va);
        va_end(va);
    }
}

#endif /* !NDEBUG */



/**
 * Hashes a string.
 *
 * @returns 32-bit string hash.
 * @param   pszString           String to hash.
 */
static KU32 kFsCacheStrHash(const char *pszString)
{
    /* This algorithm was created for sdbm (a public-domain reimplementation of
       ndbm) database library. it was found to do well in scrambling bits,
       causing better distribution of the keys and fewer splits. it also happens
       to be a good general hashing function with good distribution. the actual
       function is hash(i) = hash(i - 1) * 65599 + str[i]; what is included below
       is the faster version used in gawk. [there is even a faster, duff-device
       version] the magic constant 65599 was picked out of thin air while
       experimenting with different constants, and turns out to be a prime.
       this is one of the algorithms used in berkeley db (see sleepycat) and
       elsewhere. */
    KU32 uHash = 0;
    KU32 uChar;
    while ((uChar = (unsigned char)*pszString++) != 0)
        uHash = uChar + (uHash << 6) + (uHash << 16) - uHash;
    return uHash;
}


/**
 * Hashes a string.
 *
 * @returns The string length.
 * @param   pszString           String to hash.
 * @param   puHash              Where to return the 32-bit string hash.
 */
static KSIZE kFsCacheStrHashEx(const char *pszString, KU32 *puHash)
{
    const char * const pszStart = pszString;
    KU32 uHash = 0;
    KU32 uChar;
    while ((uChar = (unsigned char)*pszString) != 0)
    {
        uHash = uChar + (uHash << 6) + (uHash << 16) - uHash;
        pszString++;
    }
    *puHash = uHash;
    return pszString - pszStart;
}


/**
 * Hashes a substring.
 *
 * @returns 32-bit substring hash.
 * @param   pchString           Pointer to the substring (not terminated).
 * @param   cchString           The length of the substring.
 */
static KU32 kFsCacheStrHashN(const char *pchString, KSIZE cchString)
{
    KU32 uHash = 0;
    while (cchString-- > 0)
    {
        KU32 uChar = (unsigned char)*pchString++;
        uHash = uChar + (uHash << 6) + (uHash << 16) - uHash;
    }
    return uHash;
}


/**
 * Hashes a UTF-16 string.
 *
 * @returns The string length in wchar_t units.
 * @param   pwszString          String to hash.
 * @param   puHash              Where to return the 32-bit string hash.
 */
static KSIZE kFsCacheUtf16HashEx(const wchar_t *pwszString, KU32 *puHash)
{
    const wchar_t * const pwszStart = pwszString;
    KU32 uHash = 0;
    KU32 uChar;
    while ((uChar = *pwszString) != 0)
    {
        uHash = uChar + (uHash << 6) + (uHash << 16) - uHash;
        pwszString++;
    }
    *puHash = uHash;
    return pwszString - pwszStart;
}


/**
 * Hashes a UTF-16 substring.
 *
 * @returns 32-bit substring hash.
 * @param   pwcString           Pointer to the substring (not terminated).
 * @param   cchString           The length of the substring (in wchar_t's).
 */
static KU32 kFsCacheUtf16HashN(const wchar_t *pwcString, KSIZE cwcString)
{
    KU32 uHash = 0;
    while (cwcString-- > 0)
    {
        KU32 uChar = *pwcString++;
        uHash = uChar + (uHash << 6) + (uHash << 16) - uHash;
    }
    return uHash;
}


/**
 * For use when kFsCacheIAreEqualW hit's something non-trivial.
 *
 * @returns K_TRUE if equal, K_FALSE if different.
 * @param   pwcName1            The first string.
 * @param   pwcName2            The second string.
 * @param   cwcName             The length of the two strings (in wchar_t's).
 */
KBOOL kFsCacheIAreEqualSlowW(const wchar_t *pwcName1, const wchar_t *pwcName2, KU16 cwcName)
{
    MY_UNICODE_STRING UniStr1 = { cwcName * sizeof(wchar_t), cwcName * sizeof(wchar_t), (wchar_t *)pwcName1 };
    MY_UNICODE_STRING UniStr2 = { cwcName * sizeof(wchar_t), cwcName * sizeof(wchar_t), (wchar_t *)pwcName2 };
    return g_pfnRtlEqualUnicodeString(&UniStr1, &UniStr2, TRUE /*fCaseInsensitive*/);
}


/**
 * Compares two UTF-16 strings in a case-insensitive fashion.
 *
 * You would think we should be using _wscnicmp here instead, however it is
 * locale dependent and defaults to ASCII upper/lower handling setlocale hasn't
 * been called.
 *
 * @returns K_TRUE if equal, K_FALSE if different.
 * @param   pwcName1            The first string.
 * @param   pwcName2            The second string.
 * @param   cwcName             The length of the two strings (in wchar_t's).
 */
K_INLINE KBOOL kFsCacheIAreEqualW(const wchar_t *pwcName1, const wchar_t *pwcName2, KU32 cwcName)
{
    while (cwcName > 0)
    {
        wchar_t wc1 = *pwcName1;
        wchar_t wc2 = *pwcName2;
        if (wc1 == wc2)
        { /* not unlikely */ }
        else if (  (KU16)wc1 < (KU16)0xc0 /* U+00C0 is the first upper/lower letter after 'z'. */
                && (KU16)wc2 < (KU16)0xc0)
        {
            /* ASCII upper case. */
            if ((KU16)wc1 - (KU16)0x61 < (KU16)26)
                wc1 &= ~(wchar_t)0x20;
            if ((KU16)wc2 - (KU16)0x61 < (KU16)26)
                wc2 &= ~(wchar_t)0x20;
            if (wc1 != wc2)
                return K_FALSE;
        }
        else
            return kFsCacheIAreEqualSlowW(pwcName1, pwcName2, (KU16)cwcName);

        pwcName2++;
        pwcName1++;
        cwcName--;
    }

    return K_TRUE;
}


/**
 * Looks for '..' in the path.
 *
 * @returns K_TRUE if '..' component found, K_FALSE if not.
 * @param   pszPath             The path.
 * @param   cchPath             The length of the path.
 */
static KBOOL kFsCacheHasDotDotA(const char *pszPath, KSIZE cchPath)
{
    const char *pchDot = (const char *)kHlpMemChr(pszPath, '.', cchPath);
    while (pchDot)
    {
        if (pchDot[1] != '.')
        {
            pchDot++;
            pchDot = (const char *)kHlpMemChr(pchDot, '.', &pszPath[cchPath] - pchDot);
        }
        else
        {
            char ch;
            if (   (ch = pchDot[2]) != '\0'
                && IS_SLASH(ch))
            {
                if (pchDot == pszPath)
                    return K_TRUE;
                ch = pchDot[-1];
                if (   IS_SLASH(ch)
                    || ch == ':')
                    return K_TRUE;
            }
            pchDot = (const char *)kHlpMemChr(pchDot + 2, '.', &pszPath[cchPath] - pchDot - 2);
        }
    }

    return K_FALSE;
}


/**
 * Looks for '..' in the path.
 *
 * @returns K_TRUE if '..' component found, K_FALSE if not.
 * @param   pwszPath            The path.
 * @param   cwcPath             The length of the path (in wchar_t's).
 */
static KBOOL kFsCacheHasDotDotW(const wchar_t *pwszPath, KSIZE cwcPath)
{
    const wchar_t *pwcDot = wmemchr(pwszPath, '.', cwcPath);
    while (pwcDot)
    {
        if (pwcDot[1] != '.')
        {
            pwcDot++;
            pwcDot = wmemchr(pwcDot, '.', &pwszPath[cwcPath] - pwcDot);
        }
        else
        {
            wchar_t wch;
            if (   (wch = pwcDot[2]) != '\0'
                && IS_SLASH(wch))
            {
                if (pwcDot == pwszPath)
                    return K_TRUE;
                wch = pwcDot[-1];
                if (   IS_SLASH(wch)
                    || wch == ':')
                    return K_TRUE;
            }
            pwcDot = wmemchr(pwcDot + 2, '.', &pwszPath[cwcPath] - pwcDot - 2);
        }
    }

    return K_FALSE;
}


/**
 * Creates an ANSI hash table entry for the given path.
 *
 * @returns The hash table entry or NULL if out of memory.
 * @param   pCache              The hash
 * @param   pFsObj              The resulting object.
 * @param   pszPath             The path.
 * @param   cchPath             The length of the path.
 * @param   uHashPath           The hash of the path.
 * @param   fAbsolute           Whether it can be refreshed using an absolute
 *                              lookup or requires the slow treatment.
 * @parma   idxMissingGen       The missing generation index.
 * @param   idxHashTab          The hash table index of the path.
 * @param   enmError            The lookup error.
 */
static PKFSHASHA kFsCacheCreatePathHashTabEntryA(PKFSCACHE pCache, PKFSOBJ pFsObj, const char *pszPath, KU32 cchPath,
                                                 KU32 uHashPath, KU32 idxHashTab, BOOL fAbsolute, KU32 idxMissingGen,
                                                 KFSLOOKUPERROR enmError)
{
    PKFSHASHA pHashEntry = (PKFSHASHA)kHlpAlloc(sizeof(*pHashEntry) + cchPath + 1);
    if (pHashEntry)
    {
        pHashEntry->uHashPath       = uHashPath;
        pHashEntry->cchPath         = (KU16)cchPath;
        pHashEntry->fAbsolute       = fAbsolute;
        pHashEntry->idxMissingGen   = (KU8)idxMissingGen;
        pHashEntry->enmError        = enmError;
        pHashEntry->pszPath         = (const char *)kHlpMemCopy(pHashEntry + 1, pszPath, cchPath + 1);
        if (pFsObj)
        {
            pHashEntry->pFsObj      = kFsCacheObjRetainInternal(pFsObj);
            pHashEntry->uCacheGen   = pFsObj->bObjType != KFSOBJ_TYPE_MISSING
                                    ? pCache->auGenerations[       pFsObj->fFlags & KFSOBJ_F_USE_CUSTOM_GEN]
                                    : pCache->auGenerationsMissing[pFsObj->fFlags & KFSOBJ_F_USE_CUSTOM_GEN];
            pFsObj->abUnused[0] += 1; // for debugging
        }
        else
        {
            pHashEntry->pFsObj      = NULL;
            if (enmError != KFSLOOKUPERROR_UNSUPPORTED)
                pHashEntry->uCacheGen = pCache->auGenerationsMissing[idxMissingGen];
            else
                pHashEntry->uCacheGen = KFSOBJ_CACHE_GEN_IGNORE;
        }

        pHashEntry->pNext = pCache->apAnsiPaths[idxHashTab];
        pCache->apAnsiPaths[idxHashTab] = pHashEntry;

        pCache->cbAnsiPaths += sizeof(*pHashEntry) + cchPath + 1;
        pCache->cAnsiPaths++;
        if (pHashEntry->pNext)
            pCache->cAnsiPathCollisions++;
    }
    return pHashEntry;
}


/**
 * Creates an UTF-16 hash table entry for the given path.
 *
 * @returns The hash table entry or NULL if out of memory.
 * @param   pCache              The hash
 * @param   pFsObj              The resulting object.
 * @param   pwszPath            The path.
 * @param   cwcPath             The length of the path (in wchar_t's).
 * @param   uHashPath           The hash of the path.
 * @param   fAbsolute           Whether it can be refreshed using an absolute
 *                              lookup or requires the slow treatment.
 * @parma   idxMissingGen       The missing generation index.
 * @param   idxHashTab          The hash table index of the path.
 * @param   enmError            The lookup error.
 */
static PKFSHASHW kFsCacheCreatePathHashTabEntryW(PKFSCACHE pCache, PKFSOBJ pFsObj, const wchar_t *pwszPath, KU32 cwcPath,
                                                 KU32 uHashPath, KU32 idxHashTab, BOOL fAbsolute, KU32 idxMissingGen,
                                                 KFSLOOKUPERROR enmError)
{
    PKFSHASHW pHashEntry = (PKFSHASHW)kHlpAlloc(sizeof(*pHashEntry) + (cwcPath + 1) * sizeof(wchar_t));
    if (pHashEntry)
    {
        pHashEntry->uHashPath       = uHashPath;
        pHashEntry->cwcPath         = cwcPath;
        pHashEntry->fAbsolute       = fAbsolute;
        pHashEntry->idxMissingGen   = (KU8)idxMissingGen;
        pHashEntry->enmError        = enmError;
        pHashEntry->pwszPath        = (const wchar_t *)kHlpMemCopy(pHashEntry + 1, pwszPath, (cwcPath + 1) * sizeof(wchar_t));
        if (pFsObj)
        {
            pHashEntry->pFsObj      = kFsCacheObjRetainInternal(pFsObj);
            pHashEntry->uCacheGen   = pFsObj->bObjType != KFSOBJ_TYPE_MISSING
                                    ? pCache->auGenerations[       pFsObj->fFlags & KFSOBJ_F_USE_CUSTOM_GEN]
                                    : pCache->auGenerationsMissing[pFsObj->fFlags & KFSOBJ_F_USE_CUSTOM_GEN];
            pFsObj->abUnused[0] += 1; // for debugging
        }
        else
        {
            pHashEntry->pFsObj      = NULL;
            if (enmError != KFSLOOKUPERROR_UNSUPPORTED)
                pHashEntry->uCacheGen = pCache->auGenerationsMissing[idxMissingGen];
            else
                pHashEntry->uCacheGen = KFSOBJ_CACHE_GEN_IGNORE;
        }

        pHashEntry->pNext = pCache->apUtf16Paths[idxHashTab];
        pCache->apUtf16Paths[idxHashTab] = pHashEntry;

        pCache->cbUtf16Paths += sizeof(*pHashEntry) + (cwcPath + 1) * sizeof(wchar_t);
        pCache->cUtf16Paths++;
        if (pHashEntry->pNext)
            pCache->cAnsiPathCollisions++;
    }
    return pHashEntry;
}


/**
 * Links the child in under the parent.
 *
 * @returns K_TRUE on success, K_FALSE if out of memory.
 * @param   pParent             The parent node.
 * @param   pChild              The child node.
 */
static KBOOL kFsCacheDirAddChild(PKFSCACHE pCache, PKFSDIR pParent, PKFSOBJ pChild, KFSLOOKUPERROR *penmError)
{
    if (pParent->cChildren >= pParent->cChildrenAllocated)
    {
        void *pvNew = kHlpRealloc(pParent->papChildren, (pParent->cChildrenAllocated + 16) * sizeof(pParent->papChildren[0]));
        if (!pvNew)
            return K_FALSE;
        pParent->papChildren = (PKFSOBJ *)pvNew;
        pParent->cChildrenAllocated += 16;
        pCache->cbObjects += 16 * sizeof(pParent->papChildren[0]);
    }
    pParent->papChildren[pParent->cChildren++] = kFsCacheObjRetainInternal(pChild);
    return K_TRUE;
}


/**
 * Creates a new cache object.
 *
 * @returns Pointer (with 1 reference) to the new object.  The object will not
 *          be linked to the parent directory yet.
 *
 *          NULL if we're out of memory.
 *
 * @param   pCache          The cache.
 * @param   pParent         The parent directory.
 * @param   pszName         The ANSI name.
 * @param   cchName         The length of the ANSI name.
 * @param   pwszName        The UTF-16 name.
 * @param   cwcName         The length of the UTF-16 name.
 * @param   pszShortName    The ANSI short name, NULL if none.
 * @param   cchShortName    The length of the ANSI short name, 0 if none.
 * @param   pwszShortName   The UTF-16 short name, NULL if none.
 * @param   cwcShortName    The length of the UTF-16 short name, 0 if none.
 * @param   bObjType        The objct type.
 * @param   penmError       Where to explain failures.
 */
PKFSOBJ kFsCacheCreateObject(PKFSCACHE pCache, PKFSDIR pParent,
                             char const *pszName, KU16 cchName, wchar_t const *pwszName, KU16 cwcName,
#ifdef KFSCACHE_CFG_SHORT_NAMES
                             char const *pszShortName, KU16 cchShortName, wchar_t const *pwszShortName, KU16 cwcShortName,
#endif
                             KU8 bObjType, KFSLOOKUPERROR *penmError)
{
    /*
     * Allocate the object.
     */
    KBOOL const fDirish = bObjType != KFSOBJ_TYPE_FILE && bObjType != KFSOBJ_TYPE_OTHER;
    KSIZE const cbObj   = fDirish ? sizeof(KFSDIR) : sizeof(KFSOBJ);
    KSIZE const cbNames = (cwcName + 1) * sizeof(wchar_t)                           + cchName + 1
#ifdef KFSCACHE_CFG_SHORT_NAMES
                        + (cwcShortName > 0 ? (cwcShortName + 1) * sizeof(wchar_t)  + cchShortName + 1 : 0)
#endif
                          ;
    PKFSOBJ pObj;
    kHlpAssert(pCache->u32Magic == KFSCACHE_MAGIC);

    pObj = (PKFSOBJ)kHlpAlloc(cbObj + cbNames);
    if (pObj)
    {
        KU8 *pbExtra = (KU8 *)pObj + cbObj;

        pCache->cbObjects += cbObj + cbNames;
        pCache->cObjects++;

        /*
         * Initialize the object.
         */
        pObj->u32Magic      = KFSOBJ_MAGIC;
        pObj->cRefs         = 1;
        pObj->uCacheGen     = bObjType != KFSOBJ_TYPE_MISSING
                            ? pCache->auGenerations[pParent->Obj.fFlags & KFSOBJ_F_USE_CUSTOM_GEN]
                            : pCache->auGenerationsMissing[pParent->Obj.fFlags & KFSOBJ_F_USE_CUSTOM_GEN];
        pObj->bObjType      = bObjType;
        pObj->fHaveStats    = K_FALSE;
        pObj->abUnused[0]   = K_FALSE;
        pObj->abUnused[1]   = K_FALSE;
        pObj->fFlags        = pParent->Obj.fFlags & KFSOBJ_F_INHERITED_MASK;
        pObj->pParent       = pParent;
        pObj->uNameHash     = 0;
        pObj->pNextNameHash = NULL;
        pObj->pNameAlloc    = NULL;
        pObj->pUserDataHead = NULL;

#ifdef KFSCACHE_CFG_UTF16
        pObj->cwcParent = pParent->Obj.cwcParent + pParent->Obj.cwcName + !!pParent->Obj.cwcName;
        pObj->pwszName  = (wchar_t *)kHlpMemCopy(pbExtra, pwszName, cwcName * sizeof(wchar_t));
        pObj->cwcName   = cwcName;
        pbExtra += cwcName * sizeof(wchar_t);
        *pbExtra++ = '\0';
        *pbExtra++ = '\0';
# ifdef KFSCACHE_CFG_SHORT_NAMES
        pObj->cwcShortParent = pParent->Obj.cwcShortParent + pParent->Obj.cwcShortName + !!pParent->Obj.cwcShortName;
        if (cwcShortName)
        {
            pObj->pwszShortName = (wchar_t *)kHlpMemCopy(pbExtra, pwszShortName, cwcShortName * sizeof(wchar_t));
            pObj->cwcShortName  = cwcShortName;
            pbExtra += cwcShortName * sizeof(wchar_t);
            *pbExtra++ = '\0';
            *pbExtra++ = '\0';
        }
        else
        {
            pObj->pwszShortName = pObj->pwszName;
            pObj->cwcShortName  = cwcName;
        }
# endif
#endif
        pObj->cchParent = pParent->Obj.cchParent + pParent->Obj.cchName + !!pParent->Obj.cchName;
        pObj->pszName   = (char *)kHlpMemCopy(pbExtra, pszName, cchName);
        pObj->cchName   = cchName;
        pbExtra += cchName;
        *pbExtra++ = '\0';
# ifdef KFSCACHE_CFG_SHORT_NAMES
        pObj->cchShortParent = pParent->Obj.cchShortParent + pParent->Obj.cchShortName + !!pParent->Obj.cchShortName;
        if (cchShortName)
        {
            pObj->pszShortName = (char *)kHlpMemCopy(pbExtra, pszShortName, cchShortName);
            pObj->cchShortName = cchShortName;
            pbExtra += cchShortName;
            *pbExtra++ = '\0';
        }
        else
        {
            pObj->pszShortName = pObj->pszName;
            pObj->cchShortName = cchName;
        }
#endif
        kHlpAssert(pbExtra - (KU8 *)pObj == cbObj);

        /*
         * Type specific initialization.
         */
        if (fDirish)
        {
            PKFSDIR pDirObj = (PKFSDIR)pObj;
            pDirObj->cChildren          = 0;
            pDirObj->cChildrenAllocated = 0;
            pDirObj->papChildren        = NULL;
            pDirObj->fHashTabMask       = 0;
            pDirObj->papHashTab         = NULL;
            pDirObj->hDir               = INVALID_HANDLE_VALUE;
            pDirObj->uDevNo             = pParent->uDevNo;
            pDirObj->iLastWrite         = 0;
            pDirObj->fPopulated         = K_FALSE;
        }
    }
    else
        *penmError = KFSLOOKUPERROR_OUT_OF_MEMORY;
    return pObj;
}


/**
 * Creates a new object given wide char names.
 *
 * This function just converts the paths and calls kFsCacheCreateObject.
 *
 *
 * @returns Pointer (with 1 reference) to the new object.  The object will not
 *          be linked to the parent directory yet.
 *
 *          NULL if we're out of memory.
 *
 * @param   pCache          The cache.
 * @param   pParent         The parent directory.
 * @param   pszName         The ANSI name.
 * @param   cchName         The length of the ANSI name.
 * @param   pwszName        The UTF-16 name.
 * @param   cwcName         The length of the UTF-16 name.
 * @param   pwszShortName   The UTF-16 short name, NULL if none.
 * @param   cwcShortName    The length of the UTF-16 short name, 0 if none.
 * @param   bObjType        The objct type.
 * @param   penmError       Where to explain failures.
 */
PKFSOBJ kFsCacheCreateObjectW(PKFSCACHE pCache, PKFSDIR pParent, wchar_t const *pwszName, KU32 cwcName,
#ifdef KFSCACHE_CFG_SHORT_NAMES
                              wchar_t const *pwszShortName, KU32 cwcShortName,
#endif
                              KU8 bObjType, KFSLOOKUPERROR *penmError)
{
    /* Convert names to ANSI first so we know their lengths. */
    char szName[KFSCACHE_CFG_MAX_ANSI_NAME];
    int  cchName = WideCharToMultiByte(CP_ACP, 0, pwszName, cwcName, szName, sizeof(szName) - 1, NULL, NULL);
    if (cchName >= 0)
    {
#ifdef KFSCACHE_CFG_SHORT_NAMES
        char szShortName[12*3 + 1];
        int  cchShortName = 0;
        if (   cwcShortName == 0
            || (cchShortName = WideCharToMultiByte(CP_ACP, 0, pwszShortName, cwcShortName,
                                                   szShortName, sizeof(szShortName) - 1, NULL, NULL)) > 0)
#endif
        {
            return kFsCacheCreateObject(pCache, pParent,
                                        szName, cchName, pwszName, cwcName,
#ifdef KFSCACHE_CFG_SHORT_NAMES
                                        szShortName, cchShortName, pwszShortName, cwcShortName,
#endif
                                        bObjType, penmError);
        }
    }
    *penmError = KFSLOOKUPERROR_ANSI_CONVERSION_ERROR;
    return NULL;
}


/**
 * Creates a missing object.
 *
 * This is used for caching negative results.
 *
 * @returns Pointer to the newly created object on success (already linked into
 *          pParent).  No reference.
 *
 *          NULL on failure.
 *
 * @param   pCache              The cache.
 * @param   pParent             The parent directory.
 * @param   pchName             The name.
 * @param   cchName             The length of the name.
 * @param   penmError           Where to return failure explanations.
 */
static PKFSOBJ kFsCacheCreateMissingA(PKFSCACHE pCache, PKFSDIR pParent, const char *pchName, KU32 cchName,
                                      KFSLOOKUPERROR *penmError)
{
    /*
     * Just convert the name to UTF-16 and call kFsCacheCreateObject to do the job.
     */
    wchar_t wszName[KFSCACHE_CFG_MAX_PATH];
    int cwcName = MultiByteToWideChar(CP_ACP, 0, pchName, cchName, wszName, KFSCACHE_CFG_MAX_UTF16_NAME - 1);
    if (cwcName > 0)
    {
        /** @todo check that it actually doesn't exists before we add it.  We should not
         *        trust the directory enumeration here, or maybe we should?? */

        PKFSOBJ pMissing = kFsCacheCreateObject(pCache, pParent, pchName, cchName, wszName, cwcName,
#ifdef KFSCACHE_CFG_SHORT_NAMES
                                                NULL, 0, NULL, 0,
#endif
                                                KFSOBJ_TYPE_MISSING, penmError);
        if (pMissing)
        {
            KBOOL fRc = kFsCacheDirAddChild(pCache, pParent, pMissing, penmError);
            kFsCacheObjRelease(pCache, pMissing);
            return fRc ? pMissing : NULL;
        }
        return NULL;
    }
    *penmError = KFSLOOKUPERROR_UTF16_CONVERSION_ERROR;
    return NULL;
}


/**
 * Creates a missing object, UTF-16 version.
 *
 * This is used for caching negative results.
 *
 * @returns Pointer to the newly created object on success (already linked into
 *          pParent).  No reference.
 *
 *          NULL on failure.
 *
 * @param   pCache              The cache.
 * @param   pParent             The parent directory.
 * @param   pwcName             The name.
 * @param   cwcName             The length of the name.
 * @param   penmError           Where to return failure explanations.
 */
static PKFSOBJ kFsCacheCreateMissingW(PKFSCACHE pCache, PKFSDIR pParent, const wchar_t *pwcName, KU32 cwcName,
                                      KFSLOOKUPERROR *penmError)
{
    /** @todo check that it actually doesn't exists before we add it.  We should not
     *        trust the directory enumeration here, or maybe we should?? */
    PKFSOBJ pMissing = kFsCacheCreateObjectW(pCache, pParent, pwcName, cwcName,
#ifdef KFSCACHE_CFG_SHORT_NAMES
                                             NULL, 0,
#endif
                                             KFSOBJ_TYPE_MISSING, penmError);
    if (pMissing)
    {
        KBOOL fRc = kFsCacheDirAddChild(pCache, pParent, pMissing, penmError);
        kFsCacheObjRelease(pCache, pMissing);
        return fRc ? pMissing : NULL;
    }
    return NULL;
}


/**
 * Does the growing of names.
 *
 * @returns pCur
 * @param   pCache          The cache.
 * @param   pCur            The object.
 * @param   pchName         The name (not necessarily terminated).
 * @param   cchName         Name length.
 * @param   pwcName         The UTF-16 name (not necessarily terminated).
 * @param   cwcName         The length of the UTF-16 name in wchar_t's.
 * @param   pchShortName    The short name.
 * @param   cchShortName    The length of the short name.  This is 0 if no short
 *                          name.
 * @param   pwcShortName    The short UTF-16 name.
 * @param   cwcShortName    The length of the short UTF-16 name.  This is 0 if
 *                          no short name.
 */
static PKFSOBJ kFsCacheRefreshGrowNames(PKFSCACHE pCache, PKFSOBJ pCur,
                                        const char *pchName, KU32 cchName,
                                        wchar_t const *pwcName, KU32 cwcName
#ifdef KFSCACHE_CFG_SHORT_NAMES
                                        , const char *pchShortName, KU32 cchShortName,
                                        wchar_t const *pwcShortName, KU32 cwcShortName
#endif
                                        )
{
    PKFSOBJNAMEALLOC    pNameAlloc;
    char               *pch;
    KU32                cbNeeded;

    pCache->cNameGrowths++;

    /*
     * Figure out our requirements.
     */
    cbNeeded = sizeof(KU32) + cchName + 1;
#ifdef KFSCACHE_CFG_UTF16
    cbNeeded += (cwcName + 1) * sizeof(wchar_t);
#endif
#ifdef KFSCACHE_CFG_SHORT_NAMES
    cbNeeded += cchShortName + !!cchShortName;
# ifdef KFSCACHE_CFG_UTF16
    cbNeeded += (cwcShortName + !!cwcShortName) * sizeof(wchar_t);
# endif
#endif
    cbNeeded = K_ALIGN_Z(cbNeeded, 8); /* Memory will likely be 8 or 16 byte aligned, so we might just claim it. */

    /*
     * Allocate memory.
     */
    pNameAlloc = pCur->pNameAlloc;
    if (!pNameAlloc)
    {
        pNameAlloc = (PKFSOBJNAMEALLOC)kHlpAlloc(cbNeeded);
        if (!pNameAlloc)
            return pCur;
        pCache->cbObjects += cbNeeded;
        pCur->pNameAlloc = pNameAlloc;
        pNameAlloc->cb = cbNeeded;
    }
    else if (pNameAlloc->cb < cbNeeded)
    {
        pNameAlloc = (PKFSOBJNAMEALLOC)kHlpRealloc(pNameAlloc, cbNeeded);
        if (!pNameAlloc)
            return pCur;
        pCache->cbObjects += cbNeeded - pNameAlloc->cb;
        pCur->pNameAlloc = pNameAlloc;
        pNameAlloc->cb = cbNeeded;
    }

    /*
     * Copy out the new names, starting with the wide char ones to avoid misaligning them.
     */
    pch = &pNameAlloc->abSpace[0];

#ifdef KFSCACHE_CFG_UTF16
    pCur->pwszName = (wchar_t *)pch;
    pCur->cwcName  = cwcName;
    pch = kHlpMemPCopy(pch, pwcName, cwcName * sizeof(wchar_t));
    *pch++ = '\0';
    *pch++ = '\0';

# ifdef KFSCACHE_CFG_SHORT_NAMES
    if (cwcShortName == 0)
    {
        pCur->pwszShortName = pCur->pwszName;
        pCur->cwcShortName  = pCur->cwcName;
    }
    else
    {
        pCur->pwszShortName = (wchar_t *)pch;
        pCur->cwcShortName  = cwcShortName;
        pch = kHlpMemPCopy(pch, pwcShortName, cwcShortName * sizeof(wchar_t));
        *pch++ = '\0';
        *pch++ = '\0';
    }
# endif
#endif

    pCur->pszName = pch;
    pCur->cchName = cchName;
    pch = kHlpMemPCopy(pch, pchName, cchShortName);
    *pch++ = '\0';

#ifdef KFSCACHE_CFG_SHORT_NAMES
    if (cchShortName == 0)
    {
        pCur->pszShortName = pCur->pszName;
        pCur->cchShortName = pCur->cchName;
    }
    else
    {
        pCur->pszShortName = pch;
        pCur->cchShortName = cchShortName;
        pch = kHlpMemPCopy(pch, pchShortName, cchShortName);
        *pch++ = '\0';
    }
#endif

    return pCur;
}


/**
 * Worker for kFsCacheDirFindOldChild that refreshes the file ID value on an
 * object found by name.
 *
 * @returns pCur.
 * @param   pDirRePop       Repopulation data.
 * @param   pCur            The object to check the names of.
 * @param   idFile          The file ID.
 */
static PKFSOBJ kFsCacheDirRefreshOldChildFileId(PKFSDIRREPOP pDirRePop, PKFSOBJ pCur, KI64 idFile)
{
    KFSCACHE_LOG(("Refreshing %s/%s/ - %s changed file ID from %#llx -> %#llx...\n",
                  pCur->pParent->Obj.pParent->Obj.pszName, pCur->pParent->Obj.pszName, pCur->pszName,
                  pCur->Stats.st_ino, idFile));
    pCur->Stats.st_ino = idFile;
    /** @todo inform user data items...  */
    return pCur;
}


/**
 * Worker for kFsCacheDirFindOldChild that checks the names after an old object
 * has been found the file ID.
 *
 * @returns pCur.
 * @param   pDirRePop       Repopulation data.
 * @param   pCur            The object to check the names of.
 * @param   pwcName         The file name.
 * @param   cwcName         The length of the filename (in wchar_t's).
 * @param   pwcShortName    The short name, if present.
 * @param   cwcShortName    The length of the short name (in wchar_t's).
 */
static PKFSOBJ kFsCacheDirRefreshOldChildName(PKFSDIRREPOP pDirRePop, PKFSOBJ pCur, wchar_t const *pwcName, KU32 cwcName
#ifdef KFSCACHE_CFG_SHORT_NAMES
                                              , wchar_t const *pwcShortName, KU32 cwcShortName
#endif
                                              )
{
    char szName[KFSCACHE_CFG_MAX_ANSI_NAME];
    int  cchName;

    pDirRePop->pCache->cNameChanges++;

    /*
     * Convert the names to ANSI first, that way we know all the lengths.
     */
    cchName = WideCharToMultiByte(CP_ACP, 0, pwcName, cwcName, szName, sizeof(szName) - 1, NULL, NULL);
    if (cchName >= 0)
    {
#ifdef KFSCACHE_CFG_SHORT_NAMES
        char szShortName[12*3 + 1];
        int  cchShortName = 0;
        if (   cwcShortName == 0
            || (cchShortName = WideCharToMultiByte(CP_ACP, 0, pwcShortName, cwcShortName,
                                                   szShortName, sizeof(szShortName) - 1, NULL, NULL)) > 0)
#endif
        {
            /*
             * Shortening is easy for non-directory objects, for
             * directory object we're only good when the length doesn't change
             * on any of the components (cchParent et al).
             *
             * This deals with your typical xxxx.ext.tmp -> xxxx.ext renames.
             */
            if (   cchName <= pCur->cchName
#ifdef KFSCACHE_CFG_UTF16
                && cwcName <= pCur->cwcName
#endif
#ifdef KFSCACHE_CFG_SHORT_NAMES
                && (   cchShortName == 0
                    || (   cchShortName <= pCur->cchShortName
                        && pCur->pszShortName != pCur->pszName
# ifdef KFSCACHE_CFG_UTF16
                        && cwcShortName <= pCur->cwcShortName
                        && pCur->pwszShortName != pCur->pwszName
# endif
                       )
                   )
#endif
               )
            {
                if (   pCur->bObjType != KFSOBJ_TYPE_DIR
                    || (   cchName == pCur->cchName
#ifdef KFSCACHE_CFG_UTF16
                        && cwcName == pCur->cwcName
#endif
#ifdef KFSCACHE_CFG_SHORT_NAMES
                        && (   cchShortName == 0
                            || (   cchShortName == pCur->cchShortName
# ifdef KFSCACHE_CFG_UTF16
                                && cwcShortName == pCur->cwcShortName
                                )
# endif
                           )
#endif
                       )
                   )
                {
                    KFSCACHE_LOG(("Refreshing %ls - name changed to '%*.*ls'\n", pCur->pwszName, cwcName, cwcName, pwcName));
                    *(char *)kHlpMemPCopy((void *)pCur->pszName, szName, cchName) = '\0';
                    pCur->cchName = cchName;
#ifdef KFSCACHE_CFG_UTF16
                    *(wchar_t *)kHlpMemPCopy((void *)pCur->pwszName, pwcName, cwcName * sizeof(wchar_t)) = '\0';
                    pCur->cwcName = cwcName;
#endif
#ifdef KFSCACHE_CFG_SHORT_NAMES
                    *(char *)kHlpMemPCopy((void *)pCur->pszShortName, szShortName, cchShortName) = '\0';
                    pCur->cchShortName = cchShortName;
# ifdef KFSCACHE_CFG_UTF16
                    *(wchar_t *)kHlpMemPCopy((void *)pCur->pwszShortName, pwcShortName, cwcShortName * sizeof(wchar_t)) = '\0';
                    pCur->cwcShortName = cwcShortName;
# endif
#endif
                    return pCur;
                }
            }

            return kFsCacheRefreshGrowNames(pDirRePop->pCache, pCur, szName, cchName, pwcName, cwcName,
#ifdef KFSCACHE_CFG_SHORT_NAMES
                                            szShortName, cchShortName, pwcShortName, cwcShortName
#endif
                                            );
        }
    }

    fprintf(stderr, "kFsCacheDirRefreshOldChildName: WideCharToMultiByte error\n");
    return pCur;
}


/**
 * Worker for kFsCacheDirFindOldChild that checks the names after an old object
 * has been found by the file ID.
 *
 * @returns pCur.
 * @param   pDirRePop       Repopulation data.
 * @param   pCur            The object to check the names of.
 * @param   pwcName         The file name.
 * @param   cwcName         The length of the filename (in wchar_t's).
 * @param   pwcShortName    The short name, if present.
 * @param   cwcShortName    The length of the short name (in wchar_t's).
 */
K_INLINE PKFSOBJ kFsCacheDirCheckOldChildName(PKFSDIRREPOP pDirRePop, PKFSOBJ pCur, wchar_t const *pwcName, KU32 cwcName
#ifdef KFSCACHE_CFG_SHORT_NAMES
                                              , wchar_t const *pwcShortName, KU32 cwcShortName
#endif
                                              )
{
    if (   pCur->cwcName == cwcName
        && kHlpMemComp(pCur->pwszName, pwcName, cwcName * sizeof(wchar_t)) == 0)
    {
#ifdef KFSCACHE_CFG_SHORT_NAMES
        if (cwcShortName == 0
            ?    pCur->pwszShortName == pCur->pwszName
              || (   pCur->cwcShortName == cwcName
                  && kHlpMemComp(pCur->pwszShortName, pCur->pwszName, cwcName * sizeof(wchar_t)) == 0)
            :    pCur->cwcShortName == cwcShortName
              && kHlpMemComp(pCur->pwszShortName, pwcShortName, cwcShortName * sizeof(wchar_t)) == 0 )
#endif
        {
            return pCur;
        }
    }
#ifdef KFSCACHE_CFG_SHORT_NAMES
    return kFsCacheDirRefreshOldChildName(pDirRePop, pCur, pwcName, cwcName, pwcShortName, cwcShortName);
#else
    return kFsCacheDirRefreshOldChildName(pDirRePop, pCur, pwcName, cwcName);
#endif
}


/**
 * Worker for kFsCachePopuplateOrRefreshDir that locates an old child object
 * while re-populating a directory.
 *
 * @returns Pointer to the existing object if found, NULL if not.
 * @param   pDirRePop       Repopulation data.
 * @param   idFile          The file ID, 0 if none.
 * @param   pwcName         The file name.
 * @param   cwcName         The length of the filename (in wchar_t's).
 * @param   pwcShortName    The short name, if present.
 * @param   cwcShortName    The length of the short name (in wchar_t's).
 */
static PKFSOBJ kFsCacheDirFindOldChildSlow(PKFSDIRREPOP pDirRePop, KI64 idFile, wchar_t const *pwcName, KU32 cwcName
#ifdef KFSCACHE_CFG_SHORT_NAMES
                                           , wchar_t const *pwcShortName, KU32 cwcShortName
#endif
                                           )
{
    KU32        cOldChildren  = pDirRePop->cOldChildren;
    KU32 const  iNextOldChild = K_MIN(pDirRePop->iNextOldChild, cOldChildren - 1);
    KU32        iCur;
    KI32        cInc;
    KI32        cDirLefts;

    kHlpAssertReturn(cOldChildren > 0, NULL);

    /*
     * Search by file ID first, if we've got one.
     * ASSUMES that KU32 wraps around when -1 is added to 0.
     */
    if (   idFile != 0
        && idFile != KI64_MAX
        && idFile != KI64_MIN)
    {
        cInc = pDirRePop->cNextOldChildInc;
        kHlpAssert(cInc == -1 || cInc == 1);
        for (cDirLefts = 2; cDirLefts > 0; cDirLefts--)
        {
            for (iCur = iNextOldChild; iCur < cOldChildren; iCur += cInc)
            {
                PKFSOBJ pCur = pDirRePop->papOldChildren[iCur];
                if (pCur->Stats.st_ino == idFile)
                {
                    /* Remove it and check the name. */
                    pDirRePop->cOldChildren = --cOldChildren;
                    if (iCur < cOldChildren)
                        pDirRePop->papOldChildren[iCur] = pDirRePop->papOldChildren[cOldChildren];
                    else
                        cInc = -1;
                    pDirRePop->cNextOldChildInc = cInc;
                    pDirRePop->iNextOldChild    = iCur + cInc;

#ifdef KFSCACHE_CFG_SHORT_NAMES
                    return kFsCacheDirCheckOldChildName(pDirRePop, pCur, pwcName, cwcName, pwcShortName, cwcShortName);
#else
                    return kFsCacheDirCheckOldChildName(pDirRePop, pCur, pwcName, cwcName, pwcShortName, cwcShortName);
#endif
                }
            }
            cInc = -cInc;
        }
    }

    /*
     * Search by name.
     * ASSUMES that KU32 wraps around when -1 is added to 0.
     */
    cInc = pDirRePop->cNextOldChildInc;
    kHlpAssert(cInc == -1 || cInc == 1);
    for (cDirLefts = 2; cDirLefts > 0; cDirLefts--)
    {
        for (iCur = iNextOldChild; iCur < cOldChildren; iCur += cInc)
        {
            PKFSOBJ pCur = pDirRePop->papOldChildren[iCur];
            if (   (   pCur->cwcName == cwcName
                    && kFsCacheIAreEqualW(pCur->pwszName, pwcName, cwcName))
#ifdef KFSCACHE_CFG_SHORT_NAMES
                || (   pCur->cwcShortName == cwcName
                    && pCur->pwszShortName != pCur->pwszName
                    && kFsCacheIAreEqualW(pCur->pwszShortName, pwcName, cwcName))
#endif
               )
            {
                /* Do this first so the compiler can share the rest with the above file ID return. */
                if (pCur->Stats.st_ino == idFile)
                { /* likely */ }
                else
                    pCur = kFsCacheDirRefreshOldChildFileId(pDirRePop, pCur, idFile);

                /* Remove it and check the name. */
                pDirRePop->cOldChildren = --cOldChildren;
                if (iCur < cOldChildren)
                    pDirRePop->papOldChildren[iCur] = pDirRePop->papOldChildren[cOldChildren];
                else
                    cInc = -1;
                pDirRePop->cNextOldChildInc = cInc;
                pDirRePop->iNextOldChild    = iCur + cInc;

#ifdef KFSCACHE_CFG_SHORT_NAMES
                return kFsCacheDirCheckOldChildName(pDirRePop, pCur, pwcName, cwcName, pwcShortName, cwcShortName);
#else
                return kFsCacheDirCheckOldChildName(pDirRePop, pCur, pwcName, cwcName, pwcShortName, cwcShortName);
#endif
            }
        }
        cInc = -cInc;
    }

    return NULL;
}



/**
 * Worker for kFsCachePopuplateOrRefreshDir that locates an old child object
 * while re-populating a directory.
 *
 * @returns Pointer to the existing object if found, NULL if not.
 * @param   pDirRePop       Repopulation data.
 * @param   idFile          The file ID, 0 if none.
 * @param   pwcName         The file name.
 * @param   cwcName         The length of the filename (in wchar_t's).
 * @param   pwcShortName    The short name, if present.
 * @param   cwcShortName    The length of the short name (in wchar_t's).
 */
K_INLINE PKFSOBJ kFsCacheDirFindOldChild(PKFSDIRREPOP pDirRePop, KI64 idFile, wchar_t const *pwcName, KU32 cwcName
#ifdef KFSCACHE_CFG_SHORT_NAMES
                                         , wchar_t const *pwcShortName, KU32 cwcShortName
#endif
                                         )
{
    /*
     * We only check the iNextOldChild element here, hoping that the compiler
     * will actually inline this code, letting the slow version of the function
     * do the rest.
     */
    KU32 cOldChildren = pDirRePop->cOldChildren;
    if (cOldChildren > 0)
    {
        KU32 const  iNextOldChild = K_MIN(pDirRePop->iNextOldChild, cOldChildren - 1);
        PKFSOBJ     pCur          = pDirRePop->papOldChildren[iNextOldChild];

        if (   pCur->Stats.st_ino == idFile
            && idFile != 0
            && idFile != KI64_MAX
            && idFile != KI64_MIN)
            pCur = kFsCacheDirCheckOldChildName(pDirRePop, pCur, pwcName, cwcName, pwcShortName, cwcShortName);
        else if (   pCur->cwcName == cwcName
                 && kHlpMemComp(pCur->pwszName,  pwcName, cwcName * sizeof(wchar_t)) == 0)
        {
            if (pCur->Stats.st_ino == idFile)
            { /* likely */ }
            else
                pCur = kFsCacheDirRefreshOldChildFileId(pDirRePop, pCur, idFile);

#ifdef KFSCACHE_CFG_SHORT_NAMES
            if (cwcShortName == 0
                ?    pCur->pwszShortName == pCur->pwszName
                  || (   pCur->cwcShortName == cwcName
                      && kHlpMemComp(pCur->pwszShortName, pCur->pwszName, cwcName * sizeof(wchar_t)) == 0)
                :    pCur->cwcShortName == cwcShortName
                  && kHlpMemComp(pCur->pwszShortName, pwcShortName, cwcShortName * sizeof(wchar_t)) == 0 )
             { /* likely */ }
             else
                 pCur = kFsCacheDirRefreshOldChildName(pDirRePop, pCur, pwcName, cwcName, pwcShortName, cwcShortName);
#endif
        }
        else
            pCur = NULL;
        if (pCur)
        {
            /*
             * Got a match.  Remove the child from the array, replacing it with
             * the last element.  (This means we're reversing the second half of
             * the elements, which is why we need cNextOldChildInc.)
             */
            pDirRePop->cOldChildren = --cOldChildren;
            if (iNextOldChild < cOldChildren)
                pDirRePop->papOldChildren[iNextOldChild] = pDirRePop->papOldChildren[cOldChildren];
            pDirRePop->iNextOldChild = iNextOldChild + pDirRePop->cNextOldChildInc;
            return pCur;
        }

#ifdef KFSCACHE_CFG_SHORT_NAMES
        return kFsCacheDirFindOldChildSlow(pDirRePop, idFile, pwcName, cwcName, pwcShortName, cwcShortName);
#else
        return kFsCacheDirFindOldChildSlow(pDirRePop, idFile, pwcName, cwcName);
#endif
    }

    return NULL;
}



/**
 * Does the initial directory populating or refreshes it if it has been
 * invalidated.
 *
 * This assumes the parent directory is opened.
 *
 * @returns K_TRUE on success, K_FALSE on error.
 * @param   pCache              The cache.
 * @param   pDir                The directory.
 * @param   penmError           Where to store K_FALSE explanation.
 */
static KBOOL kFsCachePopuplateOrRefreshDir(PKFSCACHE pCache, PKFSDIR pDir, KFSLOOKUPERROR *penmError)
{
    KBOOL                       fRefreshing = K_FALSE;
    KFSDIRREPOP                 DirRePop    = { NULL, 0, 0, 0, NULL };
    MY_UNICODE_STRING           UniStrStar  = { 1 * sizeof(wchar_t), 2 * sizeof(wchar_t), L"*" };

    /** @todo May have to make this more flexible wrt information classes since
     *        older windows versions (XP, w2K) might not correctly support the
     *        ones with file ID on all file systems. */
#ifdef KFSCACHE_CFG_SHORT_NAMES
    MY_FILE_INFORMATION_CLASS const enmInfoClassWithId = MyFileIdBothDirectoryInformation;
    MY_FILE_INFORMATION_CLASS       enmInfoClass = MyFileIdBothDirectoryInformation;
#else
    MY_FILE_INFORMATION_CLASS const enmInfoClassWithId = MyFileIdFullDirectoryInformation;
    MY_FILE_INFORMATION_CLASS       enmInfoClass = MyFileIdFullDirectoryInformation;
#endif
    MY_NTSTATUS                 rcNt;
    MY_IO_STATUS_BLOCK          Ios;
    union
    {
        /* Include the structures for better alignment. */
        MY_FILE_ID_BOTH_DIR_INFORMATION     WithId;
        MY_FILE_ID_FULL_DIR_INFORMATION     NoId;
        /* Buffer padding. We're using a 56KB buffer here to avoid size troubles with CIFS and such. */
        KU8                                 abBuf[56*1024];
    } uBuf;


    /*
     * Open the directory.
     */
    if (pDir->hDir == INVALID_HANDLE_VALUE)
    {
        MY_OBJECT_ATTRIBUTES    ObjAttr;
        MY_UNICODE_STRING       UniStr;

        kHlpAssert(!pDir->fPopulated);

        Ios.Information = -1;
        Ios.u.Status    = -1;

        UniStr.Buffer        = (wchar_t *)pDir->Obj.pwszName;
        UniStr.Length        = (USHORT)(pDir->Obj.cwcName * sizeof(wchar_t));
        UniStr.MaximumLength = UniStr.Length + sizeof(wchar_t);

        kHlpAssertStmtReturn(pDir->Obj.pParent, *penmError = KFSLOOKUPERROR_INTERNAL_ERROR, K_FALSE);
        kHlpAssertStmtReturn(pDir->Obj.pParent->hDir != INVALID_HANDLE_VALUE, *penmError = KFSLOOKUPERROR_INTERNAL_ERROR, K_FALSE);
        MyInitializeObjectAttributes(&ObjAttr, &UniStr, OBJ_CASE_INSENSITIVE, pDir->Obj.pParent->hDir, NULL /*pSecAttr*/);

        /** @todo FILE_OPEN_REPARSE_POINT? */
        rcNt = g_pfnNtCreateFile(&pDir->hDir,
                                 FILE_READ_DATA | FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES | SYNCHRONIZE,
                                 &ObjAttr,
                                 &Ios,
                                 NULL, /*cbFileInitialAlloc */
                                 FILE_ATTRIBUTE_NORMAL,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                 FILE_OPEN,
                                 FILE_DIRECTORY_FILE | FILE_OPEN_FOR_BACKUP_INTENT | FILE_SYNCHRONOUS_IO_NONALERT,
                                 NULL, /*pEaBuffer*/
                                 0);   /*cbEaBuffer*/
        if (MY_NT_SUCCESS(rcNt))
        {  /* likely */ }
        else
        {
            pDir->hDir = INVALID_HANDLE_VALUE;
            *penmError = KFSLOOKUPERROR_DIR_OPEN_ERROR;
            return K_FALSE;
        }
    }
    /*
     * When re-populating, we replace papChildren in the directory and pick
     * from the old one as we go along.
     */
    else if (pDir->fPopulated)
    {
        KU32  cAllocated;
        void *pvNew;

        /* Make sure we really need to do this first. */
        if (!pDir->fNeedRePopulating)
        {
            if (   pDir->Obj.uCacheGen == KFSOBJ_CACHE_GEN_IGNORE
                || pDir->Obj.uCacheGen == pCache->auGenerations[pDir->Obj.fFlags & KFSOBJ_F_USE_CUSTOM_GEN])
                return K_TRUE;
            if (   kFsCacheRefreshObj(pCache, &pDir->Obj, penmError)
                && !pDir->fNeedRePopulating)
                return K_TRUE;
        }

        /* Yes we do need to. */
        cAllocated = K_ALIGN_Z(pDir->cChildren, 16);
        pvNew      = kHlpAlloc(sizeof(pDir->papChildren[0]) * cAllocated);
        if (pvNew)
        {
            DirRePop.papOldChildren     = pDir->papChildren;
            DirRePop.cOldChildren       = pDir->cChildren;
            DirRePop.iNextOldChild      = 0;
            DirRePop.cNextOldChildInc   = 1;
            DirRePop.pCache             = pCache;

            pDir->cChildren             = 0;
            pDir->cChildrenAllocated    = cAllocated;
            pDir->papChildren           = (PKFSOBJ *)pvNew;
        }
        else
        {
            *penmError = KFSLOOKUPERROR_OUT_OF_MEMORY;
            return K_FALSE;
        }

        fRefreshing = K_TRUE;
    }
    if (!fRefreshing)
        KFSCACHE_LOG(("Populating %s...\n", pDir->Obj.pszName));
    else
        KFSCACHE_LOG(("Refreshing %s...\n", pDir->Obj.pszName));

    /*
     * Enumerate the directory content.
     *
     * Note! The "*" filter is necessary because kFsCacheRefreshObj may have
     *       previously quried a single file name and just passing NULL would
     *       restart that single file name query.
     */
    Ios.Information = -1;
    Ios.u.Status    = -1;
    rcNt = g_pfnNtQueryDirectoryFile(pDir->hDir,
                                     NULL,      /* hEvent */
                                     NULL,      /* pfnApcComplete */
                                     NULL,      /* pvApcCompleteCtx */
                                     &Ios,
                                     &uBuf,
                                     sizeof(uBuf),
                                     enmInfoClass,
                                     FALSE,     /* fReturnSingleEntry */
                                     &UniStrStar, /* Filter / restart pos. */
                                     TRUE);     /* fRestartScan */
    while (MY_NT_SUCCESS(rcNt))
    {
        /*
         * Process the entries in the buffer.
         */
        KSIZE offBuf = 0;
        for (;;)
        {
            union
            {
                KU8                             *pb;
#ifdef KFSCACHE_CFG_SHORT_NAMES
                MY_FILE_ID_BOTH_DIR_INFORMATION *pWithId;
                MY_FILE_BOTH_DIR_INFORMATION    *pNoId;
#else
                MY_FILE_ID_FULL_DIR_INFORMATION *pWithId;
                MY_FILE_FULL_DIR_INFORMATION    *pNoId;
#endif
            }           uPtr;
            PKFSOBJ     pCur;
            KU32        offNext;
            KU32        cbMinCur;
            wchar_t    *pwchFilename;

            /* ASSUME only the FileName member differs between the two structures. */
            uPtr.pb = &uBuf.abBuf[offBuf];
            if (enmInfoClass == enmInfoClassWithId)
            {
                pwchFilename = &uPtr.pWithId->FileName[0];
                cbMinCur  = (KU32)((uintptr_t)&uPtr.pWithId->FileName[0] - (uintptr_t)uPtr.pWithId);
                cbMinCur += uPtr.pNoId->FileNameLength;
            }
            else
            {
                pwchFilename = &uPtr.pNoId->FileName[0];
                cbMinCur  = (KU32)((uintptr_t)&uPtr.pNoId->FileName[0] - (uintptr_t)uPtr.pNoId);
                cbMinCur += uPtr.pNoId->FileNameLength;
            }

            /* We need to skip the '.' and '..' entries. */
            if (   *pwchFilename != '.'
                ||  uPtr.pNoId->FileNameLength > 4
                ||  !(   uPtr.pNoId->FileNameLength == 2
                      ||  (   uPtr.pNoId->FileNameLength == 4
                           && pwchFilename[1] == '.') )
               )
            {
                KBOOL       fRc;
                KU8 const   bObjType = uPtr.pNoId->FileAttributes & FILE_ATTRIBUTE_DIRECTORY ? KFSOBJ_TYPE_DIR
                                     : uPtr.pNoId->FileAttributes & (FILE_ATTRIBUTE_DEVICE | FILE_ATTRIBUTE_REPARSE_POINT)
                                     ? KFSOBJ_TYPE_OTHER : KFSOBJ_TYPE_FILE;

                /*
                 * If refreshing, we must first see if this directory entry already
                 * exists.
                 */
                if (!fRefreshing)
                    pCur = NULL;
                else
                {
                    pCur = kFsCacheDirFindOldChild(&DirRePop,
                                                   enmInfoClass == enmInfoClassWithId ? uPtr.pWithId->FileId.QuadPart : 0,
                                                   pwchFilename, uPtr.pWithId->FileNameLength / sizeof(wchar_t)
#ifdef KFSCACHE_CFG_SHORT_NAMES
                                                   , uPtr.pWithId->ShortName, uPtr.pWithId->ShortNameLength / sizeof(wchar_t)
#endif
                                                   );
                    if (pCur)
                    {
                        if (pCur->bObjType == bObjType)
                        {
                            if (pCur->bObjType == KFSOBJ_TYPE_DIR)
                            {
                                PKFSDIR pCurDir = (PKFSDIR)pCur;
                                if (   !pCurDir->fPopulated
                                    ||  (   pCurDir->iLastWrite == uPtr.pWithId->LastWriteTime.QuadPart
                                         && (pCur->fFlags & KFSOBJ_F_WORKING_DIR_MTIME) ) )
                                { /* kind of likely */ }
                                else
                                {
                                    KFSCACHE_LOG(("Refreshing %s/%s/ - %s/ needs re-populating...\n",
                                                  pDir->Obj.pParent->Obj.pszName, pDir->Obj.pszName, pCur->pszName));
                                    pCurDir->fNeedRePopulating = K_TRUE;
                                }
                            }
                        }
                        else if (pCur->bObjType == KFSOBJ_TYPE_MISSING)
                        {
                            KFSCACHE_LOG(("Refreshing %s/%s/ - %s appeared as %u, was missing.\n",
                                          pDir->Obj.pParent->Obj.pszName, pDir->Obj.pszName, pCur->pszName, bObjType));
                            pCur->bObjType = bObjType;
                        }
                        else
                        {
                            KFSCACHE_LOG(("Refreshing %s/%s/ - %s changed type from %u to %u! Dropping old object.\n",
                                          pDir->Obj.pParent->Obj.pszName, pDir->Obj.pszName, pCur->pszName,
                                          pCur->bObjType, bObjType));
                            kFsCacheObjRelease(pCache, pCur);
                            pCur = NULL;
                        }
                    }
                    else
                        KFSCACHE_LOG(("Refreshing %s/%s/ - %*.*ls added.\n", pDir->Obj.pParent->Obj.pszName, pDir->Obj.pszName,
                                      uPtr.pNoId->FileNameLength / sizeof(wchar_t), uPtr.pNoId->FileNameLength / sizeof(wchar_t),
                                      pwchFilename));
                }

                if (!pCur)
                {
                    /*
                     * Create the entry (not linked yet).
                     */
                    pCur = kFsCacheCreateObjectW(pCache, pDir, pwchFilename, uPtr.pNoId->FileNameLength / sizeof(wchar_t),
#ifdef KFSCACHE_CFG_SHORT_NAMES
                                                 uPtr.pNoId->ShortName, uPtr.pNoId->ShortNameLength / sizeof(wchar_t),
#endif
                                                 bObjType, penmError);
                    if (!pCur)
                        return K_FALSE;
                    kHlpAssert(pCur->cRefs == 1);
                }

#ifdef KFSCACHE_CFG_SHORT_NAMES
                if (enmInfoClass == enmInfoClassWithId)
                    birdStatFillFromFileIdBothDirInfo(&pCur->Stats, uPtr.pWithId, pCur->pszName);
                else
                    birdStatFillFromFileBothDirInfo(&pCur->Stats, uPtr.pNoId, pCur->pszName);
#else
                if (enmInfoClass == enmInfoClassWithId)
                    birdStatFillFromFileIdFullDirInfo(&pCur->Stats, uPtr.pWithId, pCur->pszName);
                else
                    birdStatFillFromFileFullDirInfo(&pCur->Stats, uPtr.pNoId, pCur->pszName);
#endif
                pCur->Stats.st_dev = pDir->uDevNo;
                pCur->fHaveStats   = K_TRUE;

                /*
                 * Add the entry to the directory.
                 */
                fRc = kFsCacheDirAddChild(pCache, pDir, pCur, penmError);
                kFsCacheObjRelease(pCache, pCur);
                if (fRc)
                { /* likely */ }
                else
                {
                    rcNt = STATUS_NO_MEMORY;
                    break;
                }
            }
            /*
             * When seeing '.' we update the directory info.
             */
            else if (uPtr.pNoId->FileNameLength == 2)
            {
                pDir->iLastWrite = uPtr.pNoId->LastWriteTime.QuadPart;
#ifdef KFSCACHE_CFG_SHORT_NAMES
                if (enmInfoClass == enmInfoClassWithId)
                    birdStatFillFromFileIdBothDirInfo(&pDir->Obj.Stats, uPtr.pWithId, pDir->Obj.pszName);
                else
                    birdStatFillFromFileBothDirInfo(&pDir->Obj.Stats, uPtr.pNoId, pDir->Obj.pszName);
#else
                if (enmInfoClass == enmInfoClassWithId)
                    birdStatFillFromFileIdFullDirInfo(&pDir->Obj.Stats, uPtr.pWithId, pDir->Obj.pszName);
                else
                    birdStatFillFromFileFullDirInfo(&pDir->Obj.Stats, uPtr.pNoId, pDir->Obj.pszName);
#endif
            }

            /*
             * Advance.
             */
            offNext = uPtr.pNoId->NextEntryOffset;
            if (   offNext >= cbMinCur
                && offNext < sizeof(uBuf))
                offBuf += offNext;
            else
                break;
        }

        /*
         * Read the next chunk.
         */
        rcNt = g_pfnNtQueryDirectoryFile(pDir->hDir,
                                         NULL,      /* hEvent */
                                         NULL,      /* pfnApcComplete */
                                         NULL,      /* pvApcCompleteCtx */
                                         &Ios,
                                         &uBuf,
                                         sizeof(uBuf),
                                         enmInfoClass,
                                         FALSE,     /* fReturnSingleEntry */
                                         &UniStrStar, /* Filter / restart pos. */
                                         FALSE);    /* fRestartScan */
    }

    if (rcNt == MY_STATUS_NO_MORE_FILES)
    {
        /*
         * If refreshing, add missing children objects and ditch the rest.
         * We ignore errors while adding missing children (lazy bird).
         */
        if (!fRefreshing)
        { /* more likely */ }
        else
        {
            while (DirRePop.cOldChildren > 0)
            {
                KFSLOOKUPERROR enmErrorIgn;
                PKFSOBJ pOldChild = DirRePop.papOldChildren[--DirRePop.cOldChildren];
                if (pOldChild->bObjType == KFSOBJ_TYPE_MISSING)
                    kFsCacheDirAddChild(pCache, pDir, pOldChild, &enmErrorIgn);
                else
                {
                    KFSCACHE_LOG(("Refreshing %s/%s/ - %s was removed.\n",
                                  pDir->Obj.pParent->Obj.pszName, pDir->Obj.pszName, pOldChild->pszName));
                    kHlpAssert(pOldChild->bObjType != KFSOBJ_TYPE_DIR);
                    /* Remove from hash table. */
                    if (pOldChild->uNameHash != 0)
                    {
                        KU32    idx = pOldChild->uNameHash & pDir->fHashTabMask;
                        PKFSOBJ pPrev = pDir->papHashTab[idx];
                        if (pPrev == pOldChild)
                            pDir->papHashTab[idx] = pOldChild->pNextNameHash;
                        else
                        {
                            while (pPrev && pPrev->pNextNameHash != pOldChild)
                                pPrev = pPrev->pNextNameHash;
                            kHlpAssert(pPrev);
                            if (pPrev)
                                pPrev->pNextNameHash = pOldChild->pNextNameHash;
                        }
                        pOldChild->uNameHash = 0;
                    }
                }
                kFsCacheObjRelease(pCache, pOldChild);
            }
            kHlpFree(DirRePop.papOldChildren);
        }

        /*
         * Mark the directory as fully populated and up to date.
         */
        pDir->fPopulated        = K_TRUE;
        pDir->fNeedRePopulating = K_FALSE;
        if (pDir->Obj.uCacheGen != KFSOBJ_CACHE_GEN_IGNORE)
            pDir->Obj.uCacheGen = pCache->auGenerations[pDir->Obj.fFlags & KFSOBJ_F_USE_CUSTOM_GEN];
        return K_TRUE;
    }

    /*
     * If we failed during refresh, add back remaining old children.
     */
    if (!fRefreshing)
    {
        while (DirRePop.cOldChildren > 0)
        {
            KFSLOOKUPERROR enmErrorIgn;
            PKFSOBJ pOldChild = DirRePop.papOldChildren[--DirRePop.cOldChildren];
            kFsCacheDirAddChild(pCache, pDir, pOldChild, &enmErrorIgn);
            kFsCacheObjRelease(pCache, pOldChild);
        }
        kHlpFree(DirRePop.papOldChildren);
    }

    kHlpAssertMsgFailed(("%#x\n", rcNt));
    *penmError = KFSLOOKUPERROR_DIR_READ_ERROR;
    return K_TRUE;
}


/**
 * Does the initial directory populating or refreshes it if it has been
 * invalidated.
 *
 * This assumes the parent directory is opened.
 *
 * @returns K_TRUE on success, K_FALSE on error.
 * @param   pCache              The cache.
 * @param   pDir                The directory.
 * @param   penmError           Where to store K_FALSE explanation.  Optional.
 */
KBOOL kFsCacheDirEnsurePopuplated(PKFSCACHE pCache, PKFSDIR pDir, KFSLOOKUPERROR *penmError)
{
    KFSLOOKUPERROR enmIgnored;
    if (   pDir->fPopulated
        && !pDir->fNeedRePopulating
        && (   pDir->Obj.uCacheGen == KFSOBJ_CACHE_GEN_IGNORE
            || pDir->Obj.uCacheGen == pCache->auGenerations[pDir->Obj.fFlags & KFSOBJ_F_USE_CUSTOM_GEN]) )
        return K_TRUE;
    return kFsCachePopuplateOrRefreshDir(pCache, pDir, penmError ? penmError : &enmIgnored);
}


/**
 * Checks whether the modified timestamp differs on this directory.
 *
 * @returns K_TRUE if possibly modified, K_FALSE if definitely not modified.
 * @param   pDir                The directory..
 */
static KBOOL kFsCacheDirIsModified(PKFSDIR pDir)
{
    if (   pDir->hDir != INVALID_HANDLE_VALUE
        && (pDir->Obj.fFlags & KFSOBJ_F_WORKING_DIR_MTIME) )
    {
        if (!pDir->fNeedRePopulating)
        {
            MY_IO_STATUS_BLOCK          Ios;
            MY_FILE_BASIC_INFORMATION   BasicInfo;
            MY_NTSTATUS                 rcNt;

            Ios.Information = -1;
            Ios.u.Status    = -1;

            rcNt = g_pfnNtQueryInformationFile(pDir->hDir, &Ios, &BasicInfo, sizeof(BasicInfo), MyFileBasicInformation);
            if (MY_NT_SUCCESS(rcNt))
            {
                if (BasicInfo.LastWriteTime.QuadPart != pDir->iLastWrite)
                {
                    pDir->fNeedRePopulating = K_TRUE;
                    return K_TRUE;
                }
                return K_FALSE;
            }
        }
    }
    /* The cache root never changes. */
    else if (!pDir->Obj.pParent)
        return K_FALSE;

    return K_TRUE;
}


static KBOOL kFsCacheRefreshMissing(PKFSCACHE pCache, PKFSOBJ pMissing, KFSLOOKUPERROR *penmError)
{
    /*
     * If we can, we start by checking whether the parent directory
     * has been modified.   If it has, we need to check if this entry
     * was added or not, most likely it wasn't added.
     */
    if (!kFsCacheDirIsModified(pMissing->pParent))
    {
        KFSCACHE_LOG(("Parent of missing not written to %s/%s\n", pMissing->pParent->Obj.pszName, pMissing->pszName));
        pMissing->uCacheGen = pCache->auGenerationsMissing[pMissing->fFlags & KFSOBJ_F_USE_CUSTOM_GEN];
    }
    else
    {
        MY_UNICODE_STRING           UniStr;
        MY_OBJECT_ATTRIBUTES        ObjAttr;
        MY_FILE_BASIC_INFORMATION   BasicInfo;
        MY_NTSTATUS                 rcNt;

        UniStr.Buffer        = (wchar_t *)pMissing->pwszName;
        UniStr.Length        = (USHORT)(pMissing->cwcName * sizeof(wchar_t));
        UniStr.MaximumLength = UniStr.Length + sizeof(wchar_t);

        kHlpAssert(pMissing->pParent->hDir != INVALID_HANDLE_VALUE);
        MyInitializeObjectAttributes(&ObjAttr, &UniStr, OBJ_CASE_INSENSITIVE, pMissing->pParent->hDir, NULL /*pSecAttr*/);

        rcNt = g_pfnNtQueryAttributesFile(&ObjAttr, &BasicInfo);
        if (!MY_NT_SUCCESS(rcNt))
        {
            /*
             * Probably more likely that a missing node stays missing.
             */
            pMissing->uCacheGen = pCache->auGenerationsMissing[pMissing->fFlags & KFSOBJ_F_USE_CUSTOM_GEN];
            KFSCACHE_LOG(("Still missing %s/%s\n", pMissing->pParent->Obj.pszName, pMissing->pszName));
        }
        else
        {
            /*
             * We must metamorphose this node.  This is tedious business
             * because we need to check the file name casing.  We might
             * just as well update the parent directory...
             */
            KU8 const   bObjType = BasicInfo.FileAttributes & FILE_ATTRIBUTE_DIRECTORY ? KFSOBJ_TYPE_DIR
                                 : BasicInfo.FileAttributes & (FILE_ATTRIBUTE_DEVICE | FILE_ATTRIBUTE_REPARSE_POINT)
                                 ? KFSOBJ_TYPE_OTHER : KFSOBJ_TYPE_FILE;

            KFSCACHE_LOG(("Birth of %s/%s as %d with attribs %#x...\n",
                          pMissing->pParent->Obj.pszName, pMissing->pszName, bObjType, BasicInfo.FileAttributes));
            pMissing->bObjType  = bObjType;
            pMissing->uCacheGen = pCache->auGenerations[pMissing->fFlags & KFSOBJ_F_USE_CUSTOM_GEN];
/**
 * @todo refresh missing object names when it appears.
 */
        }
    }

    return K_TRUE;
}


static KBOOL kFsCacheRefreshMissingIntermediateDir(PKFSCACHE pCache, PKFSOBJ pMissing, KFSLOOKUPERROR *penmError)
{
    if (kFsCacheRefreshMissing(pCache, pMissing, penmError))
    {
        if (   pMissing->bObjType == KFSOBJ_TYPE_DIR
            || pMissing->bObjType == KFSOBJ_TYPE_MISSING)
            return K_TRUE;
        *penmError = KFSLOOKUPERROR_PATH_COMP_NOT_DIR;
    }

    return K_FALSE;
}


/**
 * Generic object refresh.
 *
 * This does not refresh the content of directories.
 *
 * @returns K_TRUE on success.  K_FALSE and *penmError on failure.
 * @param   pCache              The cache.
 * @param   pObj                The object.
 * @param   penmError           Where to return error info.
 */
static KBOOL kFsCacheRefreshObj(PKFSCACHE pCache, PKFSOBJ pObj, KFSLOOKUPERROR *penmError)
{
    KBOOL fRc;

    /*
     * Since we generally assume nothing goes away in this cache, we only really
     * have a hard time with negative entries.  So, missing stuff goes to
     * complicated land.
     */
    if (pObj->bObjType == KFSOBJ_TYPE_MISSING)
        fRc = kFsCacheRefreshMissing(pCache, pObj, penmError);
    else
    {
        /*
         * This object is supposed to exist, so all we need to do is query essential
         * stats again.  Since we've already got handles on directories, there are
         * two ways to go about this.
         */
        union
        {
            MY_FILE_NETWORK_OPEN_INFORMATION    FullInfo;
            MY_FILE_STANDARD_INFORMATION        StdInfo;
#ifdef KFSCACHE_CFG_SHORT_NAMES
            MY_FILE_ID_BOTH_DIR_INFORMATION     WithId;
            //MY_FILE_BOTH_DIR_INFORMATION        NoId;
#else
            MY_FILE_ID_FULL_DIR_INFORMATION     WithId;
            //MY_FILE_FULL_DIR_INFORMATION        NoId;
#endif
            KU8                                 abPadding[  sizeof(wchar_t) * KFSCACHE_CFG_MAX_UTF16_NAME
                                                          + sizeof(MY_FILE_ID_BOTH_DIR_INFORMATION)];
        } uBuf;
        MY_IO_STATUS_BLOCK                      Ios;
        MY_NTSTATUS                             rcNt;
        if (   pObj->bObjType != KFSOBJ_TYPE_DIR
            || ((PKFSDIR)pObj)->hDir == INVALID_HANDLE_VALUE)
        {
#if 1
            /* This always works and doesn't mess up NtQueryDirectoryFile. */
            MY_UNICODE_STRING    UniStr;
            MY_OBJECT_ATTRIBUTES ObjAttr;

            UniStr.Buffer        = (wchar_t *)pObj->pwszName;
            UniStr.Length        = (USHORT)(pObj->cwcName * sizeof(wchar_t));
            UniStr.MaximumLength = UniStr.Length + sizeof(wchar_t);

            kHlpAssert(pObj->pParent->hDir != INVALID_HANDLE_VALUE);
            MyInitializeObjectAttributes(&ObjAttr, &UniStr, OBJ_CASE_INSENSITIVE, pObj->pParent->hDir, NULL /*pSecAttr*/);

            rcNt = g_pfnNtQueryFullAttributesFile(&ObjAttr, &uBuf.FullInfo);
            if (MY_NT_SUCCESS(rcNt))
            {
                pObj->Stats.st_size          = uBuf.FullInfo.EndOfFile.QuadPart;
                birdNtTimeToTimeSpec(uBuf.FullInfo.CreationTime.QuadPart,   &pObj->Stats.st_birthtim);
                birdNtTimeToTimeSpec(uBuf.FullInfo.ChangeTime.QuadPart,     &pObj->Stats.st_ctim);
                birdNtTimeToTimeSpec(uBuf.FullInfo.LastWriteTime.QuadPart,  &pObj->Stats.st_mtim);
                birdNtTimeToTimeSpec(uBuf.FullInfo.LastAccessTime.QuadPart, &pObj->Stats.st_atim);
                pObj->Stats.st_attribs       = uBuf.FullInfo.FileAttributes;
                pObj->Stats.st_blksize       = 65536;
                pObj->Stats.st_blocks        = (uBuf.FullInfo.AllocationSize.QuadPart + BIRD_STAT_BLOCK_SIZE - 1)
                                             / BIRD_STAT_BLOCK_SIZE;
            }
#else
            /* This alternative lets us keep the inode number up to date and
               detect name case changes.
               Update: This doesn't work on windows 7, it ignores the UniStr
                       and continue with the "*" search. So, we're using the
                       above query instead for the time being. */
            MY_UNICODE_STRING    UniStr;
# ifdef KFSCACHE_CFG_SHORT_NAMES
            MY_FILE_INFORMATION_CLASS enmInfoClass = MyFileIdBothDirectoryInformation;
# else
            MY_FILE_INFORMATION_CLASS enmInfoClass = MyFileIdFullDirectoryInformation;
# endif

            UniStr.Buffer        = (wchar_t *)pObj->pwszName;
            UniStr.Length        = (USHORT)(pObj->cwcName * sizeof(wchar_t));
            UniStr.MaximumLength = UniStr.Length + sizeof(wchar_t);

            kHlpAssert(pObj->pParent->hDir != INVALID_HANDLE_VALUE);

            Ios.Information = -1;
            Ios.u.Status    = -1;
            rcNt = g_pfnNtQueryDirectoryFile(pObj->pParent->hDir,
                                             NULL,      /* hEvent */
                                             NULL,      /* pfnApcComplete */
                                             NULL,      /* pvApcCompleteCtx */
                                             &Ios,
                                             &uBuf,
                                             sizeof(uBuf),
                                             enmInfoClass,
                                             TRUE,      /* fReturnSingleEntry */
                                             &UniStr,   /* Filter / restart pos. */
                                             TRUE);     /* fRestartScan */

            if (MY_NT_SUCCESS(rcNt))
            {
                if (pObj->Stats.st_ino == uBuf.WithId.FileId.QuadPart)
                    KFSCACHE_LOG(("Refreshing %s/%s, no ID change...\n", pObj->pParent->Obj.pszName, pObj->pszName));
                else if (   pObj->cwcName == uBuf.WithId.FileNameLength / sizeof(wchar_t)
# ifdef KFSCACHE_CFG_SHORT_NAMES
                         && (  uBuf.WithId.ShortNameLength == 0
                             ?    pObj->pwszName == pObj->pwszShortName
                               || (   pObj->cwcName == pObj->cwcShortName
                                   && memcmp(pObj->pwszName, pObj->pwszShortName, pObj->cwcName * sizeof(wchar_t)) == 0)
                             : pObj->cwcShortName == uBuf.WithId.ShortNameLength / sizeof(wchar_t)
                               && memcmp(pObj->pwszShortName, uBuf.WithId.ShortName, uBuf.WithId.ShortNameLength) == 0
                            )
# endif
                         && memcmp(pObj->pwszName, uBuf.WithId.FileName, uBuf.WithId.FileNameLength) == 0
                         )
                {
                    KFSCACHE_LOG(("Refreshing %s/%s, ID changed %#llx -> %#llx...\n",
                                  pObj->pParent->Obj.pszName, pObj->pszName, pObj->Stats.st_ino, uBuf.WithId.FileId.QuadPart));
                    pObj->Stats.st_ino = uBuf.WithId.FileId.QuadPart;
                }
                else
                {
                    KFSCACHE_LOG(("Refreshing %s/%s, ID changed %#llx -> %#llx and names too...\n",
                                  pObj->pParent->Obj.pszName, pObj->pszName, pObj->Stats.st_ino, uBuf.WithId.FileId.QuadPart));
                    fprintf(stderr, "kFsCacheRefreshObj - ID + name change not implemented!!\n");
                    __debugbreak();
                    pObj->Stats.st_ino = uBuf.WithId.FileId.QuadPart;
                    /** @todo implement as needed.   */
                }

                pObj->Stats.st_size          = uBuf.WithId.EndOfFile.QuadPart;
                birdNtTimeToTimeSpec(uBuf.WithId.CreationTime.QuadPart,   &pObj->Stats.st_birthtim);
                birdNtTimeToTimeSpec(uBuf.WithId.ChangeTime.QuadPart,     &pObj->Stats.st_ctim);
                birdNtTimeToTimeSpec(uBuf.WithId.LastWriteTime.QuadPart,  &pObj->Stats.st_mtim);
                birdNtTimeToTimeSpec(uBuf.WithId.LastAccessTime.QuadPart, &pObj->Stats.st_atim);
                pObj->Stats.st_attribs       = uBuf.WithId.FileAttributes;
                pObj->Stats.st_blksize       = 65536;
                pObj->Stats.st_blocks        = (uBuf.WithId.AllocationSize.QuadPart + BIRD_STAT_BLOCK_SIZE - 1)
                                             / BIRD_STAT_BLOCK_SIZE;
            }
#endif
            if (MY_NT_SUCCESS(rcNt))
            {
                pObj->uCacheGen = pCache->auGenerations[pObj->fFlags & KFSOBJ_F_USE_CUSTOM_GEN];
                fRc = K_TRUE;
            }
            else
            {
                /* ouch! */
                kHlpAssertMsgFailed(("%#x\n", rcNt));
                fprintf(stderr, "kFsCacheRefreshObj - rcNt=%#x on non-dir - not implemented!\n", rcNt);
                __debugbreak();
                fRc = K_FALSE;
            }
        }
        else
        {
            /*
             * An open directory.  Query information via the handle, the
             * file ID shouldn't have been able to change, so we can use
             * NtQueryInformationFile.  Right...
             */
            PKFSDIR pDir = (PKFSDIR)pObj;
            Ios.Information = -1;
            Ios.u.Status    = -1;
            rcNt = g_pfnNtQueryInformationFile(pDir->hDir, &Ios, &uBuf.FullInfo, sizeof(uBuf.FullInfo),
                                               MyFileNetworkOpenInformation);
            if (MY_NT_SUCCESS(rcNt))
                rcNt = Ios.u.Status;
            if (MY_NT_SUCCESS(rcNt))
            {
                pObj->Stats.st_size          = uBuf.FullInfo.EndOfFile.QuadPart;
                birdNtTimeToTimeSpec(uBuf.FullInfo.CreationTime.QuadPart,   &pObj->Stats.st_birthtim);
                birdNtTimeToTimeSpec(uBuf.FullInfo.ChangeTime.QuadPart,     &pObj->Stats.st_ctim);
                birdNtTimeToTimeSpec(uBuf.FullInfo.LastWriteTime.QuadPart,  &pObj->Stats.st_mtim);
                birdNtTimeToTimeSpec(uBuf.FullInfo.LastAccessTime.QuadPart, &pObj->Stats.st_atim);
                pObj->Stats.st_attribs       = uBuf.FullInfo.FileAttributes;
                pObj->Stats.st_blksize       = 65536;
                pObj->Stats.st_blocks        = (uBuf.FullInfo.AllocationSize.QuadPart + BIRD_STAT_BLOCK_SIZE - 1)
                                             / BIRD_STAT_BLOCK_SIZE;

                if (   pDir->iLastWrite == uBuf.FullInfo.LastWriteTime.QuadPart
                    && (pObj->fFlags & KFSOBJ_F_WORKING_DIR_MTIME) )
                    KFSCACHE_LOG(("Refreshing %s/%s/ - no re-populating necessary.\n",
                                  pObj->pParent->Obj.pszName, pObj->pszName));
                else
                {
                    KFSCACHE_LOG(("Refreshing %s/%s/ - needs re-populating...\n",
                                  pObj->pParent->Obj.pszName, pObj->pszName));
                    pDir->fNeedRePopulating = K_TRUE;
#if 0
                    /* Refresh the link count. */
                    rcNt = g_pfnNtQueryInformationFile(pDir->hDir, &Ios, &StdInfo, sizeof(StdInfo), FileStandardInformation);
                    if (MY_NT_SUCCESS(rcNt))
                        rcNt = Ios.s.Status;
                    if (MY_NT_SUCCESS(rcNt))
                        pObj->Stats.st_nlink = StdInfo.NumberOfLinks;
#endif
                }
            }
            if (MY_NT_SUCCESS(rcNt))
            {
                pObj->uCacheGen = pCache->auGenerations[pObj->fFlags & KFSOBJ_F_USE_CUSTOM_GEN];
                fRc = K_TRUE;
            }
            else
            {
                /* ouch! */
                kHlpAssertMsgFailed(("%#x\n", rcNt));
                fprintf(stderr, "kFsCacheRefreshObj - rcNt=%#x on dir - not implemented!\n", rcNt);
                __debugbreak();
                fRc = K_FALSE;
            }
        }
    }

    return fRc;
}



/**
 * Looks up a drive letter.
 *
 * Will enter the drive if necessary.
 *
 * @returns Pointer to the root directory of the drive or an update-to-date
 *          missing node.
 * @param   pCache              The cache.
 * @param   chLetter            The uppercased drive letter.
 * @param   fFlags              Lookup flags, KFSCACHE_LOOKUP_F_XXX.
 * @param   penmError           Where to return details as to why the lookup
 *                              failed.
 */
static PKFSOBJ kFsCacheLookupDrive(PKFSCACHE pCache, char chLetter, KU32 fFlags, KFSLOOKUPERROR *penmError)
{
    KU32 const          uNameHash = chLetter - 'A';
    PKFSOBJ             pCur      = pCache->RootDir.papHashTab[uNameHash];

    KU32                cLeft;
    PKFSOBJ            *ppCur;
    MY_UNICODE_STRING   NtPath;
    wchar_t             wszTmp[8];
    char                szTmp[4];

    /*
     * Custom drive letter hashing.
     */
    kHlpAssert((uNameHash & pCache->RootDir.fHashTabMask) == uNameHash);
    while (pCur)
    {
        if (   pCur->uNameHash == uNameHash
            && pCur->cchName == 2
            && pCur->pszName[0] == chLetter
            && pCur->pszName[1] == ':')
        {
            if (pCur->bObjType == KFSOBJ_TYPE_DIR)
                return pCur;
            if (   (fFlags & KFSCACHE_LOOKUP_F_NO_REFRESH)
                || kFsCacheRefreshMissingIntermediateDir(pCache, pCur, penmError))
                return pCur;
            return NULL;
        }
        pCur = pCur->pNextNameHash;
    }

    /*
     * Make 100% sure it's not there.
     */
    cLeft = pCache->RootDir.cChildren;
    ppCur = pCache->RootDir.papChildren;
    while (cLeft-- > 0)
    {
        pCur = *ppCur++;
        if (   pCur->cchName == 2
            && pCur->pszName[0] == chLetter
            && pCur->pszName[1] == ':')
        {
            if (pCur->bObjType == KFSOBJ_TYPE_DIR)
                return pCur;
            kHlpAssert(pCur->bObjType == KFSOBJ_TYPE_MISSING);
            if (   (fFlags & KFSCACHE_LOOKUP_F_NO_REFRESH)
                || kFsCacheRefreshMissingIntermediateDir(pCache, pCur, penmError))
                return pCur;
            return NULL;
        }
    }

    if (fFlags & KFSCACHE_LOOKUP_F_NO_INSERT)
    {
        *penmError = KFSLOOKUPERROR_PATH_COMP_NOT_FOUND; /* close enough */
        return NULL;
    }

    /*
     * Need to add it.  We always keep the drive letters open for the benefit
     * of kFsCachePopuplateOrRefreshDir and others.
     */
    wszTmp[0] = szTmp[0] = chLetter;
    wszTmp[1] = szTmp[1] = ':';
    wszTmp[2] = szTmp[2] = '\\';
    wszTmp[3] = '.';
    wszTmp[4] = '\0';
    szTmp[2] = '\0';

    NtPath.Buffer        = NULL;
    NtPath.Length        = 0;
    NtPath.MaximumLength = 0;
    if (g_pfnRtlDosPathNameToNtPathName_U(wszTmp, &NtPath, NULL, NULL))
    {
        HANDLE      hDir;
        MY_NTSTATUS rcNt;
        rcNt = birdOpenFileUniStr(&NtPath,
                                  FILE_READ_DATA  | FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES | SYNCHRONIZE,
                                  FILE_ATTRIBUTE_NORMAL,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                  FILE_OPEN,
                                  FILE_DIRECTORY_FILE | FILE_OPEN_FOR_BACKUP_INTENT | FILE_SYNCHRONOUS_IO_NONALERT,
                                  OBJ_CASE_INSENSITIVE,
                                  &hDir);
        birdFreeNtPath(&NtPath);
        if (MY_NT_SUCCESS(rcNt))
        {
            PKFSDIR pDir = (PKFSDIR)kFsCacheCreateObject(pCache, &pCache->RootDir, szTmp, 2, wszTmp, 2,
#ifdef KFSCACHE_CFG_SHORT_NAMES
                                                         NULL, 0, NULL, 0,
#endif
                                                         KFSOBJ_TYPE_DIR, penmError);
            if (pDir)
            {
                /*
                 * We need a little bit of extra info for a drive root.  These things are typically
                 * inherited by subdirectories down the tree, so, we do it all here for till that changes.
                 */
                union
                {
                    MY_FILE_FS_VOLUME_INFORMATION       VolInfo;
                    MY_FILE_FS_ATTRIBUTE_INFORMATION    FsAttrInfo;
                    char abPadding[sizeof(MY_FILE_FS_VOLUME_INFORMATION) + 512];
                } uBuf;
                MY_IO_STATUS_BLOCK Ios;
                KBOOL fRc;

                kHlpAssert(pDir->hDir == INVALID_HANDLE_VALUE);
                pDir->hDir = hDir;

                if (birdStatHandle(hDir, &pDir->Obj.Stats, pDir->Obj.pszName) == 0)
                {
                    pDir->Obj.fHaveStats = K_TRUE;
                    pDir->uDevNo = pDir->Obj.Stats.st_dev;
                }
                else
                {
                    /* Just in case. */
                    pDir->Obj.fHaveStats = K_FALSE;
                    rcNt = birdQueryVolumeDeviceNumber(hDir, &uBuf.VolInfo, sizeof(uBuf), &pDir->uDevNo);
                    kHlpAssertMsg(MY_NT_SUCCESS(rcNt), ("%#x\n", rcNt));
                }

                /* Get the file system. */
                pDir->Obj.fFlags &= ~(KFSOBJ_F_NTFS | KFSOBJ_F_WORKING_DIR_MTIME);
                Ios.Information = -1;
                Ios.u.Status    = -1;
                rcNt = g_pfnNtQueryVolumeInformationFile(hDir, &Ios, &uBuf.FsAttrInfo, sizeof(uBuf),
                                                         MyFileFsAttributeInformation);
                if (MY_NT_SUCCESS(rcNt))
                    rcNt = Ios.u.Status;
                if (MY_NT_SUCCESS(rcNt))
                {
                    if (   uBuf.FsAttrInfo.FileSystemName[0] == 'N'
                        && uBuf.FsAttrInfo.FileSystemName[1] == 'T'
                        && uBuf.FsAttrInfo.FileSystemName[2] == 'F'
                        && uBuf.FsAttrInfo.FileSystemName[3] == 'S'
                        && uBuf.FsAttrInfo.FileSystemName[4] == '\0')
                    {
                        DWORD dwDriveType = GetDriveTypeW(wszTmp);
                        if (   dwDriveType == DRIVE_FIXED
                            || dwDriveType == DRIVE_RAMDISK)
                            pDir->Obj.fFlags |= KFSOBJ_F_NTFS | KFSOBJ_F_WORKING_DIR_MTIME;
                    }
                }

                /*
                 * Link the new drive letter into the root dir.
                 */
                fRc = kFsCacheDirAddChild(pCache, &pCache->RootDir, &pDir->Obj, penmError);
                kFsCacheObjRelease(pCache, &pDir->Obj);
                if (fRc)
                {
                    pDir->Obj.pNextNameHash = pCache->RootDir.papHashTab[uNameHash];
                    pCache->RootDir.papHashTab[uNameHash] = &pDir->Obj;
                    return &pDir->Obj;
                }
                return NULL;
            }

            g_pfnNtClose(hDir);
            return NULL;
        }

        /* Assume it doesn't exist if this happens... This may be a little to
           restrictive wrt status code checks. */
        kHlpAssertMsgStmtReturn(   rcNt == MY_STATUS_OBJECT_NAME_NOT_FOUND
                                || rcNt == MY_STATUS_OBJECT_PATH_NOT_FOUND
                                || rcNt == MY_STATUS_OBJECT_PATH_INVALID
                                || rcNt == MY_STATUS_OBJECT_PATH_SYNTAX_BAD,
                                ("%#x\n", rcNt),
                                *penmError = KFSLOOKUPERROR_DIR_OPEN_ERROR,
                                NULL);
    }
    else
    {
        kHlpAssertFailed();
        *penmError = KFSLOOKUPERROR_OUT_OF_MEMORY;
        return NULL;
    }

    /*
     * Maybe create a missing entry.
     */
    if (pCache->fFlags & KFSCACHE_F_MISSING_OBJECTS)
    {
        PKFSOBJ pMissing = kFsCacheCreateObject(pCache, &pCache->RootDir, szTmp, 2, wszTmp, 2,
#ifdef KFSCACHE_CFG_SHORT_NAMES
                                        NULL, 0, NULL, 0,
#endif
                                        KFSOBJ_TYPE_MISSING, penmError);
        if (pMissing)
        {
            KBOOL fRc = kFsCacheDirAddChild(pCache, &pCache->RootDir, pMissing, penmError);
            kFsCacheObjRelease(pCache, pMissing);
            return fRc ? pMissing : NULL;
        }
    }
    else
    {
        /** @todo this isn't necessary correct for a root spec.   */
        *penmError = KFSLOOKUPERROR_PATH_COMP_NOT_FOUND;
    }
    return NULL;
}


/**
 * Slow path that allocates the child hash table and enters the given one.
 *
 * Allocation fialures are ignored.
 *
 * @param   pCache              The cache (for stats).
 * @param   pDir                The directory.
 * @param   uNameHash           The name hash  to enter @a pChild under.
 * @param   pChild              The child to enter into the hash table.
 */
static void kFsCacheDirAllocHashTabAndEnterChild(PKFSCACHE pCache, PKFSDIR pDir, KU32 uNameHash, PKFSOBJ pChild)
{
    if (uNameHash != 0) /* paranoia ^ 4! */
    {
        /*
         * Double the current number of children and round up to a multiple of
         * two so we can avoid division.
         */
        KU32 cbHashTab;
        KU32 cEntries;
        kHlpAssert(pDir->cChildren > 0);
        if (pDir->cChildren <= KU32_MAX / 4)
        {
#if defined(_MSC_VER) && 1
            KU32 cEntriesRaw = pDir->cChildren * 2;
            KU32 cEntriesShift;
            kHlpAssert(sizeof(cEntries) == (unsigned long));
            if (_BitScanReverse(&cEntriesShift, cEntriesRaw))
            {
                if (   K_BIT32(cEntriesShift) < cEntriesRaw
                    && cEntriesShift < 31U)
                    cEntriesShift++;
                cEntries = K_BIT32(cEntriesShift);
            }
            else
            {
                kHlpAssertFailed();
                cEntries = KU32_MAX / 2 + 1;
            }
#else
            cEntries = pDir->cChildren * 2 - 1;
            cEntries |= cEntries >> 1;
            cEntries |= cEntries >> 2;
            cEntries |= cEntries >> 4;
            cEntries |= cEntries >> 8;
            cEntries |= cEntries >> 16;
            cEntries++;
#endif
        }
        else
            cEntries = KU32_MAX / 2 + 1;
        kHlpAssert((cEntries & (cEntries -  1)) == 0);

        cbHashTab = cEntries * sizeof(pDir->papHashTab[0]);
        pDir->papHashTab = (PKFSOBJ *)kHlpAllocZ(cbHashTab);
        if (pDir->papHashTab)
        {
            KU32 idx;
            pDir->fHashTabMask = cEntries - 1;
            pCache->cbObjects += cbHashTab;
            pCache->cChildHashTabs++;
            pCache->cChildHashEntriesTotal += cEntries;

            /*
             * Insert it.
             */
            pChild->uNameHash     = uNameHash;
            idx = uNameHash & (pDir->fHashTabMask);
            pChild->pNextNameHash = pDir->papHashTab[idx];
            pDir->papHashTab[idx] = pChild;
            pCache->cChildHashed++;
        }
    }
}


/**
 * Look up a child node, ANSI version.
 *
 * @returns Pointer to the child if found, NULL if not.
 * @param   pCache              The cache.
 * @param   pParent             The parent directory to search.
 * @param   pchName             The child name to search for (not terminated).
 * @param   cchName             The length of the child name.
 */
static PKFSOBJ kFsCacheFindChildA(PKFSCACHE pCache, PKFSDIR pParent, const char *pchName, KU32 cchName)
{
    /*
     * Check for '.' first ('..' won't appear).
     */
    if (cchName != 1 || *pchName != '.')
    {
        PKFSOBJ    *ppCur;
        KU32        cLeft;
        KU32        uNameHash;

        /*
         * Do hash table lookup.
         *
         * This caches previous lookups, which should be useful when looking up
         * intermediate directories at least.
         */
        if (pParent->papHashTab != NULL)
        {
            PKFSOBJ pCur;
            uNameHash = kFsCacheStrHashN(pchName, cchName);
            pCur = pParent->papHashTab[uNameHash & pParent->fHashTabMask];
            while (pCur)
            {
                if (   pCur->uNameHash == uNameHash
                    && (   (   pCur->cchName == cchName
                            && _mbsnicmp(pCur->pszName, pchName, cchName) == 0)
#ifdef KFSCACHE_CFG_SHORT_NAMES
                        || (   pCur->cchShortName == cchName
                            && pCur->pszShortName != pCur->pszName
                            && _mbsnicmp(pCur->pszShortName, pchName, cchName) == 0)
#endif
                        )
                   )
                {
                    pCache->cChildHashHits++;
                    pCache->cChildSearches++;
                    return pCur;
                }
                pCur = pCur->pNextNameHash;
            }
        }
        else
            uNameHash = 0;

        /*
         * Do linear search.
         */
        cLeft = pParent->cChildren;
        ppCur = pParent->papChildren;
        while (cLeft-- > 0)
        {
            PKFSOBJ pCur = *ppCur++;
            if (   (   pCur->cchName == cchName
                    && _mbsnicmp(pCur->pszName, pchName, cchName) == 0)
#ifdef KFSCACHE_CFG_SHORT_NAMES
                || (   pCur->cchShortName == cchName
                    && pCur->pszShortName != pCur->pszName
                    && _mbsnicmp(pCur->pszShortName, pchName, cchName) == 0)
#endif
               )
            {
                /*
                 * Consider entering it into the parent hash table.
                 * Note! We hash the input, not the name we found.
                 */
                if (   pCur->uNameHash == 0
                    && pParent->cChildren >= 2)
                {
                    if (pParent->papHashTab)
                    {
                        if (uNameHash != 0)
                        {
                            KU32 idxNameHash = uNameHash & pParent->fHashTabMask;
                            pCur->uNameHash     = uNameHash;
                            pCur->pNextNameHash = pParent->papHashTab[idxNameHash];
                            pParent->papHashTab[idxNameHash] = pCur;
                            if (pCur->pNextNameHash)
                                pCache->cChildHashCollisions++;
                            pCache->cChildHashed++;
                        }
                    }
                    else
                        kFsCacheDirAllocHashTabAndEnterChild(pCache, pParent, kFsCacheStrHashN(pchName, cchName), pCur);
                }

                pCache->cChildSearches++;
                return pCur;
            }
        }

        pCache->cChildSearches++;
        return NULL;
    }
    return &pParent->Obj;
}


/**
 * Look up a child node, UTF-16 version.
 *
 * @returns Pointer to the child if found, NULL if not.
 * @param   pCache              The cache.
 * @param   pParent             The parent directory to search.
 * @param   pwcName             The child name to search for (not terminated).
 * @param   cwcName             The length of the child name (in wchar_t's).
 */
static PKFSOBJ kFsCacheFindChildW(PKFSCACHE pCache, PKFSDIR pParent, const wchar_t *pwcName, KU32 cwcName)
{
    /*
     * Check for '.' first ('..' won't appear).
     */
    if (cwcName != 1 || *pwcName != '.')
    {
        PKFSOBJ    *ppCur;
        KU32        cLeft;
        KU32        uNameHash;

        /*
         * Do hash table lookup.
         *
         * This caches previous lookups, which should be useful when looking up
         * intermediate directories at least.
         */
        if (pParent->papHashTab != NULL)
        {
            PKFSOBJ pCur;
            uNameHash = kFsCacheUtf16HashN(pwcName, cwcName);
            pCur = pParent->papHashTab[uNameHash & pParent->fHashTabMask];
            while (pCur)
            {
                if (   pCur->uNameHash == uNameHash
                    && (   (   pCur->cwcName == cwcName
                            && kFsCacheIAreEqualW(pCur->pwszName, pwcName, cwcName))
#ifdef KFSCACHE_CFG_SHORT_NAMES
                         || (   pCur->cwcShortName == cwcName
                             && pCur->pwszShortName != pCur->pwszName
                             && kFsCacheIAreEqualW(pCur->pwszShortName, pwcName, cwcName))
#endif
                       )
                   )
                {
                    pCache->cChildHashHits++;
                    pCache->cChildSearches++;
                    return pCur;
                }
                pCur = pCur->pNextNameHash;
            }
        }
        else
            uNameHash = 0;

        /*
         * Do linear search.
         */
        cLeft = pParent->cChildren;
        ppCur = pParent->papChildren;
        while (cLeft-- > 0)
        {
            PKFSOBJ pCur = *ppCur++;
            if (   (   pCur->cwcName == cwcName
                    && kFsCacheIAreEqualW(pCur->pwszName, pwcName, cwcName))
#ifdef KFSCACHE_CFG_SHORT_NAMES
                || (   pCur->cwcShortName == cwcName
                    && pCur->pwszShortName != pCur->pwszName
                    && kFsCacheIAreEqualW(pCur->pwszShortName, pwcName, cwcName))
#endif
               )
            {
                /*
                 * Consider entering it into the parent hash table.
                 * Note! We hash the input, not the name we found.
                 */
                if (   pCur->uNameHash == 0
                    && pParent->cChildren >= 4)
                {
                    if (pParent->papHashTab)
                    {
                        if (uNameHash != 0)
                        {
                            KU32 idxNameHash = uNameHash & pParent->fHashTabMask;
                            pCur->uNameHash     = uNameHash;
                            pCur->pNextNameHash = pParent->papHashTab[idxNameHash];
                            pParent->papHashTab[idxNameHash] = pCur;
                            if (pCur->pNextNameHash)
                                pCache->cChildHashCollisions++;
                            pCache->cChildHashed++;
                        }
                    }
                    else
                        kFsCacheDirAllocHashTabAndEnterChild(pCache, pParent, kFsCacheUtf16HashN(pwcName, cwcName), pCur);
                }

                pCache->cChildSearches++;
                return pCur;
            }
        }
        pCache->cChildSearches++;
        return NULL;
    }
    return &pParent->Obj;
}


/**
 * Looks up a UNC share, ANSI version.
 *
 * We keep both the server and share in the root directory entry.  This means we
 * have to clean up the entry name before we can insert it.
 *
 * @returns Pointer to the share root directory or an update-to-date missing
 *          node.
 * @param   pCache              The cache.
 * @param   pszPath             The path.
 * @param   fFlags              Lookup flags, KFSCACHE_LOOKUP_F_XXX.
 * @param   poff                Where to return the root dire.
 * @param   penmError           Where to return details as to why the lookup
 *                              failed.
 */
static PKFSOBJ kFsCacheLookupUncShareA(PKFSCACHE pCache, const char *pszPath, KU32 fFlags,
                                       KU32 *poff, KFSLOOKUPERROR *penmError)
{
    /*
     * Special case: Long path prefix w/ drive letter following it.
     * Note! Must've been converted from wide char to ANSI.
     */
    if (   IS_SLASH(pszPath[0])
        && IS_SLASH(pszPath[1])
        && pszPath[2] == '?'
        && IS_SLASH(pszPath[3])
        && IS_ALPHA(pszPath[4])
        && pszPath[5] == ':'
        && IS_SLASH(pszPath[6]) )
    {
        *poff = 4 + 2;
        return kFsCacheLookupDrive(pCache, pszPath[4], fFlags, penmError);
    }

#if 0 /* later */
    KU32 offStartServer;
    KU32 offEndServer;
    KU32 offStartShare;

    KU32 offEnd = 2;
    while (IS_SLASH(pszPath[offEnd]))
        offEnd++;

    offStartServer = offEnd;
    while (   (ch = pszPath[offEnd]) != '\0'
           && !IS_SLASH(ch))
        offEnd++;
    offEndServer = offEnd;

    if (ch != '\0')
    { /* likely */ }
    else
    {
        *penmError = KFSLOOKUPERROR_NOT_FOUND;
        return NULL;
    }

    while (IS_SLASH(pszPath[offEnd]))
        offEnd++;
    offStartServer = offEnd;
    while (   (ch = pszPath[offEnd]) != '\0'
           && !IS_SLASH(ch))
        offEnd++;
#endif
    *penmError = KFSLOOKUPERROR_UNSUPPORTED;
    return NULL;
}


/**
 * Looks up a UNC share, UTF-16 version.
 *
 * We keep both the server and share in the root directory entry.  This means we
 * have to clean up the entry name before we can insert it.
 *
 * @returns Pointer to the share root directory or an update-to-date missing
 *          node.
 * @param   pCache              The cache.
 * @param   pwszPath            The path.
 * @param   fFlags              Lookup flags, KFSCACHE_LOOKUP_F_XXX.
 * @param   poff                Where to return the root dir.
 * @param   penmError           Where to return details as to why the lookup
 *                              failed.
 */
static PKFSOBJ kFsCacheLookupUncShareW(PKFSCACHE pCache, const wchar_t *pwszPath, KU32 fFlags,
                                       KU32 *poff, KFSLOOKUPERROR *penmError)
{
    /*
     * Special case: Long path prefix w/ drive letter following it.
     */
    if (   IS_SLASH(pwszPath[0])
        && IS_SLASH(pwszPath[1])
        && pwszPath[2] == '?'
        && IS_SLASH(pwszPath[3])
        && IS_ALPHA(pwszPath[4])
        && pwszPath[5] == ':'
        && IS_SLASH(pwszPath[6]) )
    {
        *poff = 4 + 2;
        return kFsCacheLookupDrive(pCache, (char)pwszPath[4], fFlags, penmError);
    }


#if 0 /* later */
    KU32 offStartServer;
    KU32 offEndServer;
    KU32 offStartShare;

    KU32 offEnd = 2;
    while (IS_SLASH(pwszPath[offEnd]))
        offEnd++;

    offStartServer = offEnd;
    while (   (ch = pwszPath[offEnd]) != '\0'
           && !IS_SLASH(ch))
        offEnd++;
    offEndServer = offEnd;

    if (ch != '\0')
    { /* likely */ }
    else
    {
        *penmError = KFSLOOKUPERROR_NOT_FOUND;
        return NULL;
    }

    while (IS_SLASH(pwszPath[offEnd]))
        offEnd++;
    offStartServer = offEnd;
    while (   (ch = pwszPath[offEnd]) != '\0'
           && !IS_SLASH(ch))
        offEnd++;
#endif
    *penmError = KFSLOOKUPERROR_UNSUPPORTED;
    return NULL;
}


/**
 * Walks an full path relative to the given directory, ANSI version.
 *
 * This will create any missing nodes while walking.
 *
 * The caller will have to do the path hash table insertion of the result.
 *
 * @returns Pointer to the tree node corresponding to @a pszPath.
 *          NULL on lookup failure, see @a penmError for details.
 * @param   pCache              The cache.
 * @param   pParent             The directory to start the lookup in.
 * @param   pszPath             The path to walk.
 * @param   cchPath             The length of the path.
 * @param   fFlags              Lookup flags, KFSCACHE_LOOKUP_F_XXX.
 * @param   penmError           Where to return details as to why the lookup
 *                              failed.
 * @param   ppLastAncestor      Where to return the last parent element found
 *                              (referenced) in case of error an path/file not
 *                              found problem.  Optional.
 */
PKFSOBJ kFsCacheLookupRelativeToDirA(PKFSCACHE pCache, PKFSDIR pParent, const char *pszPath, KU32 cchPath, KU32 fFlags,
                                     KFSLOOKUPERROR *penmError, PKFSOBJ *ppLastAncestor)
{
    /*
     * Walk loop.
     */
    KU32 off = 0;
    if (ppLastAncestor)
        *ppLastAncestor = NULL;
    for (;;)
    {
        PKFSOBJ pChild;

        /*
         * Find the end of the component, counting trailing slashes.
         */
        char    ch;
        KU32    cchSlashes = 0;
        KU32    offEnd     = off + 1;
        while ((ch = pszPath[offEnd]) != '\0')
        {
            if (!IS_SLASH(ch))
                offEnd++;
            else
            {
                do
                    cchSlashes++;
                while (IS_SLASH(pszPath[offEnd + cchSlashes]));
                break;
            }
        }

        /*
         * Do we need to populate or refresh this directory first?
         */
        if (   !pParent->fNeedRePopulating
            && pParent->fPopulated
            && (   pParent->Obj.uCacheGen == KFSOBJ_CACHE_GEN_IGNORE
                || pParent->Obj.uCacheGen == pCache->auGenerations[pParent->Obj.fFlags & KFSOBJ_F_USE_CUSTOM_GEN]) )
        { /* likely */ }
        else if (   (fFlags & (KFSCACHE_LOOKUP_F_NO_INSERT | fFlags & KFSCACHE_LOOKUP_F_NO_REFRESH))
                 || kFsCachePopuplateOrRefreshDir(pCache, pParent, penmError))
        { /* likely */ }
        else
            return NULL;

        /*
         * Search the current node for the name.
         *
         * If we don't find it, we may insert a missing node depending on
         * the cache configuration.
         */
        pChild = kFsCacheFindChildA(pCache, pParent, &pszPath[off], offEnd - off);
        if (pChild != NULL)
        { /* probably likely */ }
        else
        {
            if (    (pCache->fFlags & KFSCACHE_F_MISSING_OBJECTS)
                && !(fFlags & KFSCACHE_LOOKUP_F_NO_INSERT))
                pChild = kFsCacheCreateMissingA(pCache, pParent, &pszPath[off], offEnd - off, penmError);
            if (cchSlashes == 0 || offEnd + cchSlashes >= cchPath)
            {
                if (pChild)
                    return kFsCacheObjRetainInternal(pChild);
                *penmError = KFSLOOKUPERROR_NOT_FOUND;
            }
            else
                *penmError = KFSLOOKUPERROR_PATH_COMP_NOT_FOUND;
            if (ppLastAncestor)
                *ppLastAncestor = kFsCacheObjRetainInternal(&pParent->Obj);
            return NULL;
        }

        /* Advance off and check if we're done already. */
        off = offEnd + cchSlashes;
        if (   cchSlashes == 0
            || off >= cchPath)
        {
            if (   pChild->bObjType != KFSOBJ_TYPE_MISSING
                || pChild->uCacheGen == KFSOBJ_CACHE_GEN_IGNORE
                || pChild->uCacheGen == pCache->auGenerationsMissing[pChild->fFlags & KFSOBJ_F_USE_CUSTOM_GEN]
                || (fFlags & KFSCACHE_LOOKUP_F_NO_REFRESH)
                || kFsCacheRefreshMissing(pCache, pChild, penmError) )
            { /* likely */ }
            else
                return NULL;
            return kFsCacheObjRetainInternal(pChild);
        }

        /*
         * Check that it's a directory.  If a missing entry, we may have to
         * refresh it and re-examin it.
         */
        if (pChild->bObjType == KFSOBJ_TYPE_DIR)
            pParent = (PKFSDIR)pChild;
        else if (pChild->bObjType != KFSOBJ_TYPE_MISSING)
        {
            *penmError = KFSLOOKUPERROR_PATH_COMP_NOT_DIR;
            if (ppLastAncestor)
                *ppLastAncestor = kFsCacheObjRetainInternal(&pParent->Obj);
            return NULL;
        }
        else if (   pChild->uCacheGen == KFSOBJ_CACHE_GEN_IGNORE
                 || pChild->uCacheGen == pCache->auGenerationsMissing[pChild->fFlags & KFSOBJ_F_USE_CUSTOM_GEN]
                 || (fFlags & KFSCACHE_LOOKUP_F_NO_REFRESH))
        {
            *penmError = KFSLOOKUPERROR_PATH_COMP_NOT_FOUND;
            if (ppLastAncestor)
                *ppLastAncestor = kFsCacheObjRetainInternal(&pParent->Obj);
            return NULL;
        }
        else if (kFsCacheRefreshMissingIntermediateDir(pCache, pChild, penmError))
            pParent = (PKFSDIR)pChild;
        else
        {
            if (ppLastAncestor)
                *ppLastAncestor = kFsCacheObjRetainInternal(&pParent->Obj);
            return NULL;
        }
    }

    return NULL;

}


/**
 * Walks an full path relative to the given directory, UTF-16 version.
 *
 * This will create any missing nodes while walking.
 *
 * The caller will have to do the path hash table insertion of the result.
 *
 * @returns Pointer to the tree node corresponding to @a pszPath.
 *          NULL on lookup failure, see @a penmError for details.
 * @param   pCache              The cache.
 * @param   pParent             The directory to start the lookup in.
 * @param   pszPath             The path to walk.  No dot-dot bits allowed!
 * @param   cchPath             The length of the path.
 * @param   fFlags              Lookup flags, KFSCACHE_LOOKUP_F_XXX.
 * @param   penmError           Where to return details as to why the lookup
 *                              failed.
 * @param   ppLastAncestor      Where to return the last parent element found
 *                              (referenced) in case of error an path/file not
 *                              found problem.  Optional.
 */
PKFSOBJ kFsCacheLookupRelativeToDirW(PKFSCACHE pCache, PKFSDIR pParent, const wchar_t *pwszPath, KU32 cwcPath, KU32 fFlags,
                                     KFSLOOKUPERROR *penmError, PKFSOBJ *ppLastAncestor)
{
    /*
     * Walk loop.
     */
    KU32 off = 0;
    if (ppLastAncestor)
        *ppLastAncestor = NULL;
    for (;;)
    {
        PKFSOBJ pChild;

        /*
         * Find the end of the component, counting trailing slashes.
         */
        wchar_t wc;
        KU32    cwcSlashes = 0;
        KU32    offEnd     = off + 1;
        while ((wc = pwszPath[offEnd]) != '\0')
        {
            if (!IS_SLASH(wc))
                offEnd++;
            else
            {
                do
                    cwcSlashes++;
                while (IS_SLASH(pwszPath[offEnd + cwcSlashes]));
                break;
            }
        }

        /*
         * Do we need to populate or refresh this directory first?
         */
        if (   !pParent->fNeedRePopulating
            && pParent->fPopulated
            && (   pParent->Obj.uCacheGen == KFSOBJ_CACHE_GEN_IGNORE
                || pParent->Obj.uCacheGen == pCache->auGenerations[pParent->Obj.fFlags & KFSOBJ_F_USE_CUSTOM_GEN]) )
        { /* likely */ }
        else if (   (fFlags & (KFSCACHE_LOOKUP_F_NO_INSERT | fFlags & KFSCACHE_LOOKUP_F_NO_REFRESH))
                 || kFsCachePopuplateOrRefreshDir(pCache, pParent, penmError))
        { /* likely */ }
        else
            return NULL;

        /*
         * Search the current node for the name.
         *
         * If we don't find it, we may insert a missing node depending on
         * the cache configuration.
         */
        pChild = kFsCacheFindChildW(pCache, pParent, &pwszPath[off], offEnd - off);
        if (pChild != NULL)
        { /* probably likely */ }
        else
        {
            if (    (pCache->fFlags & KFSCACHE_F_MISSING_OBJECTS)
                && !(fFlags & KFSCACHE_LOOKUP_F_NO_INSERT))
                pChild = kFsCacheCreateMissingW(pCache, pParent, &pwszPath[off], offEnd - off, penmError);
            if (cwcSlashes == 0 || offEnd + cwcSlashes >= cwcPath)
            {
                if (pChild)
                    return kFsCacheObjRetainInternal(pChild);
                *penmError = KFSLOOKUPERROR_NOT_FOUND;
            }
            else
                *penmError = KFSLOOKUPERROR_PATH_COMP_NOT_FOUND;
            if (ppLastAncestor)
                *ppLastAncestor = kFsCacheObjRetainInternal(&pParent->Obj);
            return NULL;
        }

        /* Advance off and check if we're done already. */
        off = offEnd + cwcSlashes;
        if (   cwcSlashes == 0
            || off >= cwcPath)
        {
            if (   pChild->bObjType != KFSOBJ_TYPE_MISSING
                || pChild->uCacheGen == KFSOBJ_CACHE_GEN_IGNORE
                || pChild->uCacheGen == pCache->auGenerationsMissing[pChild->fFlags & KFSOBJ_F_USE_CUSTOM_GEN]
                || (fFlags & KFSCACHE_LOOKUP_F_NO_REFRESH)
                || kFsCacheRefreshMissing(pCache, pChild, penmError) )
            { /* likely */ }
            else
                return NULL;
            return kFsCacheObjRetainInternal(pChild);
        }

        /*
         * Check that it's a directory.  If a missing entry, we may have to
         * refresh it and re-examin it.
         */
        if (pChild->bObjType == KFSOBJ_TYPE_DIR)
            pParent = (PKFSDIR)pChild;
        else if (pChild->bObjType != KFSOBJ_TYPE_MISSING)
        {
            *penmError = KFSLOOKUPERROR_PATH_COMP_NOT_DIR;
            if (ppLastAncestor)
                *ppLastAncestor = kFsCacheObjRetainInternal(&pParent->Obj);
            return NULL;
        }
        else if (   pChild->uCacheGen == KFSOBJ_CACHE_GEN_IGNORE
                 || pChild->uCacheGen == pCache->auGenerationsMissing[pChild->fFlags & KFSOBJ_F_USE_CUSTOM_GEN]
                 || (fFlags & KFSCACHE_LOOKUP_F_NO_REFRESH) )

        {
            *penmError = KFSLOOKUPERROR_PATH_COMP_NOT_FOUND;
            if (ppLastAncestor)
                *ppLastAncestor = kFsCacheObjRetainInternal(&pParent->Obj);
            return NULL;
        }
        else if (kFsCacheRefreshMissingIntermediateDir(pCache, pChild, penmError))
            pParent = (PKFSDIR)pChild;
        else
        {
            if (ppLastAncestor)
                *ppLastAncestor = kFsCacheObjRetainInternal(&pParent->Obj);
            return NULL;
        }
    }

    return NULL;

}

/**
 * Walk the file system tree for the given absolute path, entering it into the
 * hash table.
 *
 * This will create any missing nodes while walking.
 *
 * The caller will have to do the path hash table insertion of the result.
 *
 * @returns Pointer to the tree node corresponding to @a pszPath.
 *          NULL on lookup failure, see @a penmError for details.
 * @param   pCache              The cache.
 * @param   pszPath             The path to walk. No dot-dot bits allowed!
 * @param   cchPath             The length of the path.
 * @param   fFlags              Lookup flags, KFSCACHE_LOOKUP_F_XXX.
 * @param   penmError           Where to return details as to why the lookup
 *                              failed.
 * @param   ppLastAncestor      Where to return the last parent element found
 *                              (referenced) in case of error an path/file not
 *                              found problem.  Optional.
 */
static PKFSOBJ kFsCacheLookupAbsoluteA(PKFSCACHE pCache, const char *pszPath, KU32 cchPath, KU32 fFlags,
                                       KFSLOOKUPERROR *penmError, PKFSOBJ *ppLastAncestor)
{
    PKFSOBJ     pRoot;
    KU32        cchSlashes;
    KU32        offEnd;

    KFSCACHE_LOG2(("kFsCacheLookupAbsoluteA(%s)\n", pszPath));

    /*
     * The root "directory" needs special handling, so we keep it outside the
     * main search loop. (Special: Cannot enumerate it, UNCs, ++.)
     */
    cchSlashes = 0;
    if (   pszPath[1] == ':'
        && IS_ALPHA(pszPath[0]))
    {
        /* Drive letter. */
        offEnd = 2;
        kHlpAssert(IS_SLASH(pszPath[2]));
        pRoot = kFsCacheLookupDrive(pCache, toupper(pszPath[0]), fFlags, penmError);
    }
    else if (   IS_SLASH(pszPath[0])
             && IS_SLASH(pszPath[1]) )
        pRoot = kFsCacheLookupUncShareA(pCache, pszPath, fFlags, &offEnd, penmError);
    else
    {
        *penmError = KFSLOOKUPERROR_UNSUPPORTED;
        return NULL;
    }
    if (pRoot)
    { /* likely */ }
    else
        return NULL;

    /* Count slashes trailing the root spec. */
    if (offEnd < cchPath)
    {
        kHlpAssert(IS_SLASH(pszPath[offEnd]));
        do
            cchSlashes++;
        while (IS_SLASH(pszPath[offEnd + cchSlashes]));
    }

    /* Done already? */
    if (offEnd >= cchPath)
    {
        if (   pRoot->uCacheGen == KFSOBJ_CACHE_GEN_IGNORE
            || pRoot->uCacheGen == (  pRoot->bObjType != KFSOBJ_TYPE_MISSING
                                    ? pCache->auGenerations[       pRoot->fFlags & KFSOBJ_F_USE_CUSTOM_GEN]
                                    : pCache->auGenerationsMissing[pRoot->fFlags & KFSOBJ_F_USE_CUSTOM_GEN])
            || (fFlags & KFSCACHE_LOOKUP_F_NO_REFRESH)
            || kFsCacheRefreshObj(pCache, pRoot, penmError))
            return kFsCacheObjRetainInternal(pRoot);
        return NULL;
    }

    /* Check that we've got a valid result and not a cached negative one. */
    if (pRoot->bObjType == KFSOBJ_TYPE_DIR)
    { /* likely */ }
    else
    {
        kHlpAssert(pRoot->bObjType == KFSOBJ_TYPE_MISSING);
        kHlpAssert(   pRoot->uCacheGen == KFSOBJ_CACHE_GEN_IGNORE
                   || pRoot->uCacheGen == pCache->auGenerationsMissing[pRoot->fFlags & KFSOBJ_F_USE_CUSTOM_GEN]);
        return pRoot;
    }

    /*
     * Now that we've found a valid root directory, lookup the
     * remainder of the path starting with it.
     */
    return kFsCacheLookupRelativeToDirA(pCache, (PKFSDIR)pRoot, &pszPath[offEnd + cchSlashes],
                                        cchPath - offEnd - cchSlashes, fFlags, penmError, ppLastAncestor);
}


/**
 * Walk the file system tree for the given absolute path, UTF-16 version.
 *
 * This will create any missing nodes while walking.
 *
 * The caller will have to do the path hash table insertion of the result.
 *
 * @returns Pointer to the tree node corresponding to @a pszPath.
 *          NULL on lookup failure, see @a penmError for details.
 * @param   pCache              The cache.
 * @param   pwszPath            The path to walk.
 * @param   cwcPath             The length of the path (in wchar_t's).
 * @param   fFlags              Lookup flags, KFSCACHE_LOOKUP_F_XXX.
 * @param   penmError           Where to return details as to why the lookup
 *                              failed.
 * @param   ppLastAncestor      Where to return the last parent element found
 *                              (referenced) in case of error an path/file not
 *                              found problem.  Optional.
 */
static PKFSOBJ kFsCacheLookupAbsoluteW(PKFSCACHE pCache, const wchar_t *pwszPath, KU32 cwcPath, KU32 fFlags,
                                       KFSLOOKUPERROR *penmError, PKFSOBJ *ppLastAncestor)
{
    PKFSDIR     pParent = &pCache->RootDir;
    PKFSOBJ     pRoot;
    KU32        off;
    KU32        cwcSlashes;
    KU32        offEnd;

    KFSCACHE_LOG2(("kFsCacheLookupAbsoluteW(%ls)\n", pwszPath));

    /*
     * The root "directory" needs special handling, so we keep it outside the
     * main search loop. (Special: Cannot enumerate it, UNCs, ++.)
     */
    cwcSlashes = 0;
    off        = 0;
    if (   pwszPath[1] == ':'
        && IS_ALPHA(pwszPath[0]))
    {
        /* Drive letter. */
        offEnd = 2;
        kHlpAssert(IS_SLASH(pwszPath[2]));
        pRoot = kFsCacheLookupDrive(pCache, toupper(pwszPath[0]), fFlags, penmError);
    }
    else if (   IS_SLASH(pwszPath[0])
             && IS_SLASH(pwszPath[1]) )
        pRoot = kFsCacheLookupUncShareW(pCache, pwszPath, fFlags, &offEnd, penmError);
    else
    {
        *penmError = KFSLOOKUPERROR_UNSUPPORTED;
        return NULL;
    }
    if (pRoot)
    { /* likely */ }
    else
        return NULL;

    /* Count slashes trailing the root spec. */
    if (offEnd < cwcPath)
    {
        kHlpAssert(IS_SLASH(pwszPath[offEnd]));
        do
            cwcSlashes++;
        while (IS_SLASH(pwszPath[offEnd + cwcSlashes]));
    }

    /* Done already? */
    if (offEnd >= cwcPath)
    {
        if (   pRoot->uCacheGen == KFSOBJ_CACHE_GEN_IGNORE
            || pRoot->uCacheGen == (pRoot->bObjType != KFSOBJ_TYPE_MISSING
                                    ? pCache->auGenerations[       pRoot->fFlags & KFSOBJ_F_USE_CUSTOM_GEN]
                                    : pCache->auGenerationsMissing[pRoot->fFlags & KFSOBJ_F_USE_CUSTOM_GEN])
            || (fFlags & KFSCACHE_LOOKUP_F_NO_REFRESH)
            || kFsCacheRefreshObj(pCache, pRoot, penmError))
            return kFsCacheObjRetainInternal(pRoot);
        return NULL;
    }

    /* Check that we've got a valid result and not a cached negative one. */
    if (pRoot->bObjType == KFSOBJ_TYPE_DIR)
    { /* likely */ }
    else
    {
        kHlpAssert(pRoot->bObjType == KFSOBJ_TYPE_MISSING);
        kHlpAssert(   pRoot->uCacheGen == KFSOBJ_CACHE_GEN_IGNORE
                   || pRoot->uCacheGen == pCache->auGenerationsMissing[pRoot->fFlags & KFSOBJ_F_USE_CUSTOM_GEN]);
        return pRoot;
    }

    /*
     * Now that we've found a valid root directory, lookup the
     * remainder of the path starting with it.
     */
    return kFsCacheLookupRelativeToDirW(pCache, (PKFSDIR)pRoot, &pwszPath[offEnd + cwcSlashes],
                                        cwcPath - offEnd - cwcSlashes, fFlags, penmError, ppLastAncestor);
}


/**
 * This deals with paths that are relative and paths that contains '..'
 * elements, ANSI version.
 *
 * @returns Pointer to object corresponding to @a pszPath on success.
 *          NULL if this isn't a path we care to cache.
 *
 * @param   pCache              The cache.
 * @param   pszPath             The path.
 * @param   cchPath             The length of the path.
 * @param   fFlags              Lookup flags, KFSCACHE_LOOKUP_F_XXX.
 * @param   penmError           Where to return details as to why the lookup
 *                              failed.
 * @param   ppLastAncestor      Where to return the last parent element found
 *                              (referenced) in case of error an path/file not
 *                              found problem.  Optional.
 */
static PKFSOBJ kFsCacheLookupSlowA(PKFSCACHE pCache, const char *pszPath, KU32 cchPath, KU32 fFlags,
                                   KFSLOOKUPERROR *penmError, PKFSOBJ *ppLastAncestor)
{
    /*
     * We just call GetFullPathNameA here to do the job as getcwd and _getdcwd
     * ends up calling it anyway.
     */
    char szFull[KFSCACHE_CFG_MAX_PATH];
    UINT cchFull = GetFullPathNameA(pszPath, sizeof(szFull), szFull, NULL);
    if (   cchFull >= 3
        && cchFull < sizeof(szFull))
    {
        KFSCACHE_LOG2(("kFsCacheLookupSlowA(%s)\n", pszPath));
        return kFsCacheLookupAbsoluteA(pCache, szFull, cchFull, fFlags, penmError, ppLastAncestor);
    }

    /* The path is too long! */
    kHlpAssertMsgFailed(("'%s' -> cchFull=%u\n", pszPath, cchFull));
    *penmError = KFSLOOKUPERROR_PATH_TOO_LONG;
    return NULL;
}


/**
 * This deals with paths that are relative and paths that contains '..'
 * elements, UTF-16 version.
 *
 * @returns Pointer to object corresponding to @a pszPath on success.
 *          NULL if this isn't a path we care to cache.
 *
 * @param   pCache              The cache.
 * @param   pwszPath            The path.
 * @param   cwcPath             The length of the path (in wchar_t's).
 * @param   fFlags              Lookup flags, KFSCACHE_LOOKUP_F_XXX.
 * @param   penmError           Where to return details as to why the lookup
 *                              failed.
 * @param   ppLastAncestor      Where to return the last parent element found
 *                              (referenced) in case of error an path/file not
 *                              found problem.  Optional.
 */
static PKFSOBJ kFsCacheLookupSlowW(PKFSCACHE pCache, const wchar_t *pwszPath, KU32 wcwPath, KU32 fFlags,
                                   KFSLOOKUPERROR *penmError, PKFSOBJ *ppLastAncestor)
{
    /*
     * We just call GetFullPathNameA here to do the job as getcwd and _getdcwd
     * ends up calling it anyway.
     */
    wchar_t wszFull[KFSCACHE_CFG_MAX_PATH];
    UINT cwcFull = GetFullPathNameW(pwszPath, KFSCACHE_CFG_MAX_PATH, wszFull, NULL);
    if (   cwcFull >= 3
        && cwcFull < KFSCACHE_CFG_MAX_PATH)
    {
        KFSCACHE_LOG2(("kFsCacheLookupSlowA(%ls)\n", pwszPath));
        return kFsCacheLookupAbsoluteW(pCache, wszFull, cwcFull, fFlags, penmError, ppLastAncestor);
    }

    /* The path is too long! */
    kHlpAssertMsgFailed(("'%ls' -> cwcFull=%u\n", pwszPath, cwcFull));
    *penmError = KFSLOOKUPERROR_PATH_TOO_LONG;
    return NULL;
}


/**
 * Refreshes a path hash that has expired, ANSI version.
 *
 * @returns pHash on success, NULL if removed.
 * @param   pCache              The cache.
 * @param   pHashEntry          The path hash.
 * @param   idxHashTab          The hash table entry.
 */
static PKFSHASHA kFsCacheRefreshPathA(PKFSCACHE pCache, PKFSHASHA pHashEntry, KU32 idxHashTab)
{
    PKFSOBJ pLastAncestor = NULL;
    if (!pHashEntry->pFsObj)
    {
        if (pHashEntry->fAbsolute)
            pHashEntry->pFsObj = kFsCacheLookupAbsoluteA(pCache, pHashEntry->pszPath, pHashEntry->cchPath, 0 /*fFlags*/,
                                                         &pHashEntry->enmError, &pLastAncestor);
        else
            pHashEntry->pFsObj = kFsCacheLookupSlowA(pCache, pHashEntry->pszPath, pHashEntry->cchPath, 0 /*fFlags*/,
                                                     &pHashEntry->enmError, &pLastAncestor);
    }
    else
    {
        KU8             bOldType = pHashEntry->pFsObj->bObjType;
        KFSLOOKUPERROR  enmError;
        if (kFsCacheRefreshObj(pCache, pHashEntry->pFsObj, &enmError))
        {
            if (pHashEntry->pFsObj->bObjType == bOldType)
            { }
            else
            {
                kFsCacheObjRelease(pCache, pHashEntry->pFsObj);
                if (pHashEntry->fAbsolute)
                    pHashEntry->pFsObj = kFsCacheLookupAbsoluteA(pCache, pHashEntry->pszPath, pHashEntry->cchPath, 0 /*fFlags*/,
                                                                 &pHashEntry->enmError, &pLastAncestor);
                else
                    pHashEntry->pFsObj = kFsCacheLookupSlowA(pCache, pHashEntry->pszPath, pHashEntry->cchPath, 0 /*fFlags*/,
                                                             &pHashEntry->enmError, &pLastAncestor);
            }
        }
        else
        {
            fprintf(stderr, "kFsCacheRefreshPathA - refresh failure handling not implemented!\n");
            __debugbreak();
            /** @todo just remove this entry.   */
            return NULL;
        }
    }

    if (pLastAncestor && !pHashEntry->pFsObj)
        pHashEntry->idxMissingGen = pLastAncestor->fFlags & KFSOBJ_F_USE_CUSTOM_GEN;
    pHashEntry->uCacheGen = !pHashEntry->pFsObj
                          ? pCache->auGenerationsMissing[pHashEntry->idxMissingGen]
                          : pHashEntry->pFsObj->bObjType == KFSOBJ_TYPE_MISSING
                          ? pCache->auGenerationsMissing[pHashEntry->pFsObj->fFlags & KFSOBJ_F_USE_CUSTOM_GEN]
                          : pCache->auGenerations[       pHashEntry->pFsObj->fFlags & KFSOBJ_F_USE_CUSTOM_GEN];
    if (pLastAncestor)
        kFsCacheObjRelease(pCache, pLastAncestor);
    return pHashEntry;
}


/**
 * Refreshes a path hash that has expired, UTF-16 version.
 *
 * @returns pHash on success, NULL if removed.
 * @param   pCache              The cache.
 * @param   pHashEntry          The path hash.
 * @param   idxHashTab          The hash table entry.
 */
static PKFSHASHW kFsCacheRefreshPathW(PKFSCACHE pCache, PKFSHASHW pHashEntry, KU32 idxHashTab)
{
    PKFSOBJ pLastAncestor = NULL;
    if (!pHashEntry->pFsObj)
    {
        if (pHashEntry->fAbsolute)
            pHashEntry->pFsObj = kFsCacheLookupAbsoluteW(pCache, pHashEntry->pwszPath, pHashEntry->cwcPath, 0 /*fFlags*/,
                                                         &pHashEntry->enmError, &pLastAncestor);
        else
            pHashEntry->pFsObj = kFsCacheLookupSlowW(pCache, pHashEntry->pwszPath, pHashEntry->cwcPath, 0 /*fFlags*/,
                                                     &pHashEntry->enmError, &pLastAncestor);
    }
    else
    {
        KU8             bOldType = pHashEntry->pFsObj->bObjType;
        KFSLOOKUPERROR  enmError;
        if (kFsCacheRefreshObj(pCache, pHashEntry->pFsObj, &enmError))
        {
            if (pHashEntry->pFsObj->bObjType == bOldType)
            { }
            else
            {
                kFsCacheObjRelease(pCache, pHashEntry->pFsObj);
                if (pHashEntry->fAbsolute)
                    pHashEntry->pFsObj = kFsCacheLookupAbsoluteW(pCache, pHashEntry->pwszPath, pHashEntry->cwcPath, 0 /*fFlags*/,
                                                                 &pHashEntry->enmError, &pLastAncestor);
                else
                    pHashEntry->pFsObj = kFsCacheLookupSlowW(pCache, pHashEntry->pwszPath, pHashEntry->cwcPath, 0 /*fFlags*/,
                                                             &pHashEntry->enmError, &pLastAncestor);
            }
        }
        else
        {
            fprintf(stderr, "kFsCacheRefreshPathW - refresh failure handling not implemented!\n");
            __debugbreak();
            /** @todo just remove this entry.   */
            return NULL;
        }
    }
    if (pLastAncestor && !pHashEntry->pFsObj)
        pHashEntry->idxMissingGen = pLastAncestor->fFlags & KFSOBJ_F_USE_CUSTOM_GEN;
    pHashEntry->uCacheGen = !pHashEntry->pFsObj
                          ? pCache->auGenerationsMissing[pHashEntry->idxMissingGen]
                          : pHashEntry->pFsObj->bObjType == KFSOBJ_TYPE_MISSING
                          ? pCache->auGenerationsMissing[pHashEntry->pFsObj->fFlags & KFSOBJ_F_USE_CUSTOM_GEN]
                          : pCache->auGenerations[       pHashEntry->pFsObj->fFlags & KFSOBJ_F_USE_CUSTOM_GEN];
    if (pLastAncestor)
        kFsCacheObjRelease(pCache, pLastAncestor);
    return pHashEntry;
}


/**
 * Internal lookup worker that looks up a KFSOBJ for the given ANSI path with
 * length and hash.
 *
 * This will first try the hash table.  If not in the hash table, the file
 * system cache tree is walked, missing bits filled in and finally a hash table
 * entry is created.
 *
 * Only drive letter paths are cachable.  We don't do any UNC paths at this
 * point.
 *
 * @returns Reference to object corresponding to @a pszPath on success, this
 *          must be released by kFsCacheObjRelease.
 *          NULL if not a path we care to cache.
 * @param   pCache              The cache.
 * @param   pchPath             The path to lookup.
 * @param   cchPath             The path length.
 * @param   uHashPath           The hash of the path.
 * @param   penmError           Where to return details as to why the lookup
 *                              failed.
 */
static PKFSOBJ kFsCacheLookupHashedA(PKFSCACHE pCache, const char *pchPath, KU32 cchPath, KU32 uHashPath,
                                     KFSLOOKUPERROR *penmError)
{
    /*
     * Do hash table lookup of the path.
     */
    KU32        idxHashTab = uHashPath % K_ELEMENTS(pCache->apAnsiPaths);
    PKFSHASHA   pHashEntry = pCache->apAnsiPaths[idxHashTab];
    kHlpAssert(pCache->u32Magic == KFSCACHE_MAGIC);
    if (pHashEntry)
    {
        do
        {
            if (   pHashEntry->uHashPath == uHashPath
                && pHashEntry->cchPath   == cchPath
                && kHlpMemComp(pHashEntry->pszPath, pchPath, cchPath) == 0)
            {
                PKFSOBJ pFsObj;
                if (   pHashEntry->uCacheGen == KFSOBJ_CACHE_GEN_IGNORE
                    || pHashEntry->uCacheGen == (  (pFsObj = pHashEntry->pFsObj) != NULL
                                                 ? pFsObj->bObjType != KFSOBJ_TYPE_MISSING
                                                   ? pCache->auGenerations[       pFsObj->fFlags & KFSOBJ_F_USE_CUSTOM_GEN]
                                                   : pCache->auGenerationsMissing[pFsObj->fFlags & KFSOBJ_F_USE_CUSTOM_GEN]
                                                 : pCache->auGenerationsMissing[pHashEntry->idxMissingGen])
                    || (pHashEntry = kFsCacheRefreshPathA(pCache, pHashEntry, idxHashTab)) )
                {
                    pCache->cLookups++;
                    pCache->cPathHashHits++;
                    KFSCACHE_LOG2(("kFsCacheLookupA(%*.*s) - hit %p\n", cchPath, cchPath, pchPath, pHashEntry->pFsObj));
                    *penmError = pHashEntry->enmError;
                    if (pHashEntry->pFsObj)
                        return kFsCacheObjRetainInternal(pHashEntry->pFsObj);
                    return NULL;
                }
                break;
            }
            pHashEntry = pHashEntry->pNext;
        } while (pHashEntry);
    }

    /*
     * Create an entry for it by walking the file system cache and filling in the blanks.
     */
    if (   cchPath > 0
        && cchPath < KFSCACHE_CFG_MAX_PATH)
    {
        PKFSOBJ pFsObj;
        KBOOL   fAbsolute;
        PKFSOBJ pLastAncestor = NULL;

        /* Is absolute without any '..' bits? */
        if (   cchPath >= 3
            && (   (   pchPath[1] == ':'    /* Drive letter */
                    && IS_SLASH(pchPath[2])
                    && IS_ALPHA(pchPath[0]) )
                || (   IS_SLASH(pchPath[0]) /* UNC */
                    && IS_SLASH(pchPath[1]) ) )
            && !kFsCacheHasDotDotA(pchPath, cchPath) )
        {
            pFsObj = kFsCacheLookupAbsoluteA(pCache, pchPath, cchPath, 0 /*fFlags*/, penmError, &pLastAncestor);
            fAbsolute = K_TRUE;
        }
        else
        {
            pFsObj = kFsCacheLookupSlowA(pCache, pchPath, cchPath, 0 /*fFlags*/, penmError, &pLastAncestor);
            fAbsolute = K_FALSE;
        }
        if (   pFsObj
            || (   (pCache->fFlags & KFSCACHE_F_MISSING_PATHS)
                && *penmError != KFSLOOKUPERROR_PATH_TOO_LONG)
            || *penmError == KFSLOOKUPERROR_UNSUPPORTED )
            kFsCacheCreatePathHashTabEntryA(pCache, pFsObj, pchPath, cchPath, uHashPath, idxHashTab, fAbsolute,
                                            pLastAncestor ? pLastAncestor->bObjType & KFSOBJ_F_USE_CUSTOM_GEN : 0, *penmError);
        if (pLastAncestor)
            kFsCacheObjRelease(pCache, pLastAncestor);

        pCache->cLookups++;
        if (pFsObj)
            pCache->cWalkHits++;
        return pFsObj;
    }

    *penmError = KFSLOOKUPERROR_PATH_TOO_LONG;
    return NULL;
}


/**
 * Internal lookup worker that looks up a KFSOBJ for the given UTF-16 path with
 * length and hash.
 *
 * This will first try the hash table.  If not in the hash table, the file
 * system cache tree is walked, missing bits filled in and finally a hash table
 * entry is created.
 *
 * Only drive letter paths are cachable.  We don't do any UNC paths at this
 * point.
 *
 * @returns Reference to object corresponding to @a pwcPath on success, this
 *          must be released by kFsCacheObjRelease.
 *          NULL if not a path we care to cache.
 * @param   pCache              The cache.
 * @param   pwcPath             The path to lookup.
 * @param   cwcPath             The length of the path (in wchar_t's).
 * @param   uHashPath           The hash of the path.
 * @param   penmError           Where to return details as to why the lookup
 *                              failed.
 */
static PKFSOBJ kFsCacheLookupHashedW(PKFSCACHE pCache, const wchar_t *pwcPath, KU32 cwcPath, KU32 uHashPath,
                                     KFSLOOKUPERROR *penmError)
{
    /*
     * Do hash table lookup of the path.
     */
    KU32        idxHashTab = uHashPath % K_ELEMENTS(pCache->apAnsiPaths);
    PKFSHASHW   pHashEntry = pCache->apUtf16Paths[idxHashTab];
    kHlpAssert(pCache->u32Magic == KFSCACHE_MAGIC);
    if (pHashEntry)
    {
        do
        {
            if (   pHashEntry->uHashPath == uHashPath
                && pHashEntry->cwcPath   == cwcPath
                && kHlpMemComp(pHashEntry->pwszPath, pwcPath, cwcPath) == 0)
            {
                PKFSOBJ pFsObj;
                if (   pHashEntry->uCacheGen == KFSOBJ_CACHE_GEN_IGNORE
                    || pHashEntry->uCacheGen == ((pFsObj = pHashEntry->pFsObj) != NULL
                                                 ? pFsObj->bObjType != KFSOBJ_TYPE_MISSING
                                                   ? pCache->auGenerations[       pFsObj->fFlags & KFSOBJ_F_USE_CUSTOM_GEN]
                                                   : pCache->auGenerationsMissing[pFsObj->fFlags & KFSOBJ_F_USE_CUSTOM_GEN]
                                                 : pCache->auGenerationsMissing[pHashEntry->idxMissingGen])
                    || (pHashEntry = kFsCacheRefreshPathW(pCache, pHashEntry, idxHashTab)) )
                {
                    pCache->cLookups++;
                    pCache->cPathHashHits++;
                    KFSCACHE_LOG2(("kFsCacheLookupW(%*.*ls) - hit %p\n", cwcPath, cwcPath, pwcPath, pHashEntry->pFsObj));
                    *penmError = pHashEntry->enmError;
                    if (pHashEntry->pFsObj)
                        return kFsCacheObjRetainInternal(pHashEntry->pFsObj);
                    return NULL;
                }
                break;
            }
            pHashEntry = pHashEntry->pNext;
        } while (pHashEntry);
    }

    /*
     * Create an entry for it by walking the file system cache and filling in the blanks.
     */
    if (   cwcPath > 0
        && cwcPath < KFSCACHE_CFG_MAX_PATH)
    {
        PKFSOBJ pFsObj;
        KBOOL   fAbsolute;
        PKFSOBJ pLastAncestor = NULL;

        /* Is absolute without any '..' bits? */
        if (   cwcPath >= 3
            && (   (   pwcPath[1] == ':'    /* Drive letter */
                    && IS_SLASH(pwcPath[2])
                    && IS_ALPHA(pwcPath[0]) )
                || (   IS_SLASH(pwcPath[0]) /* UNC */
                    && IS_SLASH(pwcPath[1]) ) )
            && !kFsCacheHasDotDotW(pwcPath, cwcPath) )
        {
            pFsObj = kFsCacheLookupAbsoluteW(pCache, pwcPath, cwcPath, 0 /*fFlags*/, penmError, &pLastAncestor);
            fAbsolute = K_TRUE;
        }
        else
        {
            pFsObj = kFsCacheLookupSlowW(pCache, pwcPath, cwcPath, 0 /*fFlags*/, penmError, &pLastAncestor);
            fAbsolute = K_FALSE;
        }
        if (   pFsObj
            || (   (pCache->fFlags & KFSCACHE_F_MISSING_PATHS)
                && *penmError != KFSLOOKUPERROR_PATH_TOO_LONG)
            || *penmError == KFSLOOKUPERROR_UNSUPPORTED )
            kFsCacheCreatePathHashTabEntryW(pCache, pFsObj, pwcPath, cwcPath, uHashPath, idxHashTab, fAbsolute,
                                            pLastAncestor ? pLastAncestor->bObjType & KFSOBJ_F_USE_CUSTOM_GEN : 0, *penmError);
        if (pLastAncestor)
            kFsCacheObjRelease(pCache, pLastAncestor);

        pCache->cLookups++;
        if (pFsObj)
            pCache->cWalkHits++;
        return pFsObj;
    }

    *penmError = KFSLOOKUPERROR_PATH_TOO_LONG;
    return NULL;
}



/**
 * Looks up a KFSOBJ for the given ANSI path.
 *
 * This will first try the hash table.  If not in the hash table, the file
 * system cache tree is walked, missing bits filled in and finally a hash table
 * entry is created.
 *
 * Only drive letter paths are cachable.  We don't do any UNC paths at this
 * point.
 *
 * @returns Reference to object corresponding to @a pszPath on success, this
 *          must be released by kFsCacheObjRelease.
 *          NULL if not a path we care to cache.
 * @param   pCache              The cache.
 * @param   pszPath             The path to lookup.
 * @param   penmError           Where to return details as to why the lookup
 *                              failed.
 */
PKFSOBJ kFsCacheLookupA(PKFSCACHE pCache, const char *pszPath, KFSLOOKUPERROR *penmError)
{
    KU32 uHashPath;
    KU32 cchPath = (KU32)kFsCacheStrHashEx(pszPath, &uHashPath);
    return kFsCacheLookupHashedA(pCache, pszPath, cchPath, uHashPath, penmError);
}


/**
 * Looks up a KFSOBJ for the given UTF-16 path.
 *
 * This will first try the hash table.  If not in the hash table, the file
 * system cache tree is walked, missing bits filled in and finally a hash table
 * entry is created.
 *
 * Only drive letter paths are cachable.  We don't do any UNC paths at this
 * point.
 *
 * @returns Reference to object corresponding to @a pwszPath on success, this
 *          must be released by kFsCacheObjRelease.
 *          NULL if not a path we care to cache.
 * @param   pCache              The cache.
 * @param   pwszPath            The path to lookup.
 * @param   penmError           Where to return details as to why the lookup
 *                              failed.
 */
PKFSOBJ kFsCacheLookupW(PKFSCACHE pCache, const wchar_t *pwszPath, KFSLOOKUPERROR *penmError)
{
    KU32 uHashPath;
    KU32 cwcPath = (KU32)kFsCacheUtf16HashEx(pwszPath, &uHashPath);
    return kFsCacheLookupHashedW(pCache, pwszPath, cwcPath, uHashPath, penmError);
}


/**
 * Looks up a KFSOBJ for the given ANSI path.
 *
 * This will first try the hash table.  If not in the hash table, the file
 * system cache tree is walked, missing bits filled in and finally a hash table
 * entry is created.
 *
 * Only drive letter paths are cachable.  We don't do any UNC paths at this
 * point.
 *
 * @returns Reference to object corresponding to @a pchPath on success, this
 *          must be released by kFsCacheObjRelease.
 *          NULL if not a path we care to cache.
 * @param   pCache              The cache.
 * @param   pchPath             The path to lookup (does not need to be nul
 *                              terminated).
 * @param   cchPath             The path length.
 * @param   penmError           Where to return details as to why the lookup
 *                              failed.
 */
PKFSOBJ kFsCacheLookupWithLengthA(PKFSCACHE pCache, const char *pchPath, KSIZE cchPath, KFSLOOKUPERROR *penmError)
{
    return kFsCacheLookupHashedA(pCache, pchPath, (KU32)cchPath, kFsCacheStrHashN(pchPath, cchPath), penmError);
}


/**
 * Looks up a KFSOBJ for the given UTF-16 path.
 *
 * This will first try the hash table.  If not in the hash table, the file
 * system cache tree is walked, missing bits filled in and finally a hash table
 * entry is created.
 *
 * Only drive letter paths are cachable.  We don't do any UNC paths at this
 * point.
 *
 * @returns Reference to object corresponding to @a pwchPath on success, this
 *          must be released by kFsCacheObjRelease.
 *          NULL if not a path we care to cache.
 * @param   pCache              The cache.
 * @param   pwcPath             The path to lookup (does not need to be nul
 *                              terminated).
 * @param   cwcPath             The path length (in wchar_t's).
 * @param   penmError           Where to return details as to why the lookup
 *                              failed.
 */
PKFSOBJ kFsCacheLookupWithLengthW(PKFSCACHE pCache, const wchar_t *pwcPath, KSIZE cwcPath, KFSLOOKUPERROR *penmError)
{
    return kFsCacheLookupHashedW(pCache, pwcPath, (KU32)cwcPath, kFsCacheUtf16HashN(pwcPath, cwcPath), penmError);
}


/**
 * Wrapper around kFsCacheLookupA that drops KFSOBJ_TYPE_MISSING and returns
 * KFSLOOKUPERROR_NOT_FOUND instead.
 *
 * @returns Reference to object corresponding to @a pszPath on success, this
 *          must be released by kFsCacheObjRelease.
 *          NULL if not a path we care to cache.
 * @param   pCache              The cache.
 * @param   pszPath             The path to lookup.
 * @param   penmError           Where to return details as to why the lookup
 *                              failed.
 */
PKFSOBJ kFsCacheLookupNoMissingA(PKFSCACHE pCache, const char *pszPath, KFSLOOKUPERROR *penmError)
{
    PKFSOBJ pObj = kFsCacheLookupA(pCache, pszPath, penmError);
    if (pObj)
    {
        if (pObj->bObjType != KFSOBJ_TYPE_MISSING)
            return pObj;

        kFsCacheObjRelease(pCache, pObj);
        *penmError = KFSLOOKUPERROR_NOT_FOUND;
    }
    return NULL;
}


/**
 * Wrapper around kFsCacheLookupW that drops KFSOBJ_TYPE_MISSING and returns
 * KFSLOOKUPERROR_NOT_FOUND instead.
 *
 * @returns Reference to object corresponding to @a pszPath on success, this
 *          must be released by kFsCacheObjRelease.
 *          NULL if not a path we care to cache.
 * @param   pCache              The cache.
 * @param   pwszPath            The path to lookup.
 * @param   penmError           Where to return details as to why the lookup
 *                              failed.
 */
PKFSOBJ kFsCacheLookupNoMissingW(PKFSCACHE pCache, const wchar_t *pwszPath, KFSLOOKUPERROR *penmError)
{
    PKFSOBJ pObj = kFsCacheLookupW(pCache, pwszPath, penmError);
    if (pObj)
    {
        if (pObj->bObjType != KFSOBJ_TYPE_MISSING)
            return pObj;

        kFsCacheObjRelease(pCache, pObj);
        *penmError = KFSLOOKUPERROR_NOT_FOUND;
    }
    return NULL;
}


/**
 * Destroys a cache object which has a zero reference count.
 *
 * @returns 0
 * @param   pCache              The cache.
 * @param   pObj                The object.
 * @param   pszWhere            Where it was released from.
 */
KU32 kFsCacheObjDestroy(PKFSCACHE pCache, PKFSOBJ pObj, const char *pszWhere)
{
    kHlpAssert(pObj->cRefs == 0);
    kHlpAssert(pObj->pParent == NULL);
    kHlpAssert(pObj->u32Magic == KFSOBJ_MAGIC);

    KFSCACHE_LOG(("Destroying %s/%s, type=%d, pObj=%p, pszWhere=%s\n",
                  pObj->pParent ? pObj->pParent->Obj.pszName : "", pObj->pszName, pObj->bObjType, pObj, pszWhere));
    if (pObj->abUnused[1] != 0)
    {
        fprintf(stderr, "Destroying %s/%s, type=%d, path hash entries: %d!\n", pObj->pParent ? pObj->pParent->Obj.pszName : "",
                pObj->pszName, pObj->bObjType, pObj->abUnused[0]);
        __debugbreak();
    }

    /*
     * Invalidate the structure.
     */
    pObj->u32Magic = ~KFSOBJ_MAGIC;

    /*
     * Destroy any user data first.
     */
    while (pObj->pUserDataHead != NULL)
    {
        PKFSUSERDATA pUserData = pObj->pUserDataHead;
        pObj->pUserDataHead = pUserData->pNext;
        if (pUserData->pfnDestructor)
            pUserData->pfnDestructor(pCache, pObj, pUserData);
        kHlpFree(pUserData);
    }

    /*
     * Do type specific destruction
     */
    switch (pObj->bObjType)
    {
        case KFSOBJ_TYPE_MISSING:
            /* nothing else to do here */
            pCache->cbObjects -= sizeof(KFSDIR);
            break;

        case KFSOBJ_TYPE_DIR:
        {
            PKFSDIR pDir = (PKFSDIR)pObj;
            KU32    cChildren = pDir->cChildren;
            pCache->cbObjects -= sizeof(*pDir)
                               + K_ALIGN_Z(cChildren, 16) * sizeof(pDir->papChildren)
                               + (pDir->fHashTabMask + !!pDir->fHashTabMask) * sizeof(pDir->papHashTab[0]);

            pDir->cChildren   = 0;
            while (cChildren-- > 0)
                kFsCacheObjRelease(pCache, pDir->papChildren[cChildren]);
            kHlpFree(pDir->papChildren);
            pDir->papChildren = NULL;

            kHlpFree(pDir->papHashTab);
            pDir->papHashTab = NULL;
            break;
        }

        case KFSOBJ_TYPE_FILE:
        case KFSOBJ_TYPE_OTHER:
            pCache->cbObjects -= sizeof(*pObj);
            break;

        default:
            return 0;
    }

    /*
     * Common bits.
     */
    pCache->cbObjects -= pObj->cchName + 1;
#ifdef KFSCACHE_CFG_UTF16
    pCache->cbObjects -= (pObj->cwcName + 1) * sizeof(wchar_t);
#endif
#ifdef KFSCACHE_CFG_SHORT_NAMES
    if (pObj->pszName != pObj->pszShortName)
    {
        pCache->cbObjects -= pObj->cchShortName + 1;
# ifdef KFSCACHE_CFG_UTF16
        pCache->cbObjects -= (pObj->cwcShortName + 1) * sizeof(wchar_t);
# endif
    }
#endif
    pCache->cObjects--;

    if (pObj->pNameAlloc)
    {
        pCache->cbObjects -= pObj->pNameAlloc->cb;
        kHlpFree(pObj->pNameAlloc);
    }

    kHlpFree(pObj);
    return 0;
}


/**
 * Releases a reference to a cache object.
 *
 * @returns New reference count.
 * @param   pCache              The cache.
 * @param   pObj                The object.
 */
#undef kFsCacheObjRelease
KU32 kFsCacheObjRelease(PKFSCACHE pCache, PKFSOBJ pObj)
{
    if (pObj)
    {
        KU32 cRefs;
        kHlpAssert(pCache->u32Magic == KFSCACHE_MAGIC);
        kHlpAssert(pObj->u32Magic == KFSOBJ_MAGIC);

        cRefs = --pObj->cRefs;
        if (cRefs)
            return cRefs;
        return kFsCacheObjDestroy(pCache, pObj, "kFsCacheObjRelease");
    }
    return 0;
}


/**
 * Debug version of kFsCacheObjRelease
 *
 * @returns New reference count.
 * @param   pCache              The cache.
 * @param   pObj                The object.
 * @param   pszWhere            Where it's invoked from.
 */
KU32 kFsCacheObjReleaseTagged(PKFSCACHE pCache, PKFSOBJ pObj, const char *pszWhere)
{
    if (pObj)
    {
        KU32 cRefs;
        kHlpAssert(pCache->u32Magic == KFSCACHE_MAGIC);
        kHlpAssert(pObj->u32Magic == KFSOBJ_MAGIC);

        cRefs = --pObj->cRefs;
        if (cRefs)
            return cRefs;
        return kFsCacheObjDestroy(pCache, pObj, pszWhere);
    }
    return 0;
}


/**
 * Retains a reference to a cahce object.
 *
 * @returns New reference count.
 * @param   pObj                The object.
 */
KU32 kFsCacheObjRetain(PKFSOBJ pObj)
{
    KU32 cRefs;
    kHlpAssert(pCache->u32Magic == KFSCACHE_MAGIC);
    kHlpAssert(pObj->u32Magic == KFSOBJ_MAGIC);

    cRefs = ++pObj->cRefs;
    kHlpAssert(cRefs < 16384);
    return cRefs;
}


/**
 * Associates an item of user data with the given object.
 *
 * If the data needs cleaning up before being free, set the
 * PKFSUSERDATA::pfnDestructor member of the returned structure.
 *
 * @returns Pointer to the user data on success.
 *          NULL if out of memory or key already in use.
 *
 * @param   pCache              The cache.
 * @param   pObj                The object.
 * @param   uKey                The user data key.
 * @param   cbUserData          The size of the user data.
 */
PKFSUSERDATA kFsCacheObjAddUserData(PKFSCACHE pCache, PKFSOBJ pObj, KUPTR uKey, KSIZE cbUserData)
{
    kHlpAssert(cbUserData >= sizeof(*pNew));
    if (kFsCacheObjGetUserData(pCache, pObj, uKey) == NULL)
    {
        PKFSUSERDATA pNew = (PKFSUSERDATA)kHlpAllocZ(cbUserData);
        if (pNew)
        {
            pNew->uKey          = uKey;
            pNew->pfnDestructor = NULL;
            pNew->pNext         = pObj->pUserDataHead;
            pObj->pUserDataHead = pNew;
            return pNew;
        }
    }

    return NULL;
}


/**
 * Retrieves an item of user data associated with the given object.
 *
 * @returns Pointer to the associated user data if found, otherwise NULL.
 * @param   pCache              The cache.
 * @param   pObj                The object.
 * @param   uKey                The user data key.
 */
PKFSUSERDATA kFsCacheObjGetUserData(PKFSCACHE pCache, PKFSOBJ pObj, KUPTR uKey)
{
    PKFSUSERDATA pCur;

    kHlpAssert(pCache->u32Magic == KFSCACHE_MAGIC);
    kHlpAssert(pObj->u32Magic == KFSOBJ_MAGIC);

    for (pCur = pObj->pUserDataHead; pCur; pCur = pCur->pNext)
        if (pCur->uKey == uKey)
            return pCur;
    return NULL;
}


/**
 * Gets the full path to @a pObj, ANSI version.
 *
 * @returns K_TRUE on success, K_FALSE on buffer overflow (nothing stored).
 * @param   pObj                The object to get the full path to.
 * @param   pszPath             Where to return the path
 * @param   cbPath              The size of the output buffer.
 * @param   chSlash             The slash to use.
 */
KBOOL kFsCacheObjGetFullPathA(PKFSOBJ pObj, char *pszPath, KSIZE cbPath, char chSlash)
{
    KSIZE off = pObj->cchParent;
    kHlpAssert(pObj->u32Magic == KFSOBJ_MAGIC);
    if (off > 0)
    {
        KSIZE offEnd = off + pObj->cchName;
        if (offEnd < cbPath)
        {
            PKFSDIR pAncestor;

            pszPath[off + pObj->cchName] = '\0';
            memcpy(&pszPath[off], pObj->pszName, pObj->cchName);

            for (pAncestor = pObj->pParent; off > 0; pAncestor = pAncestor->Obj.pParent)
            {
                kHlpAssert(off > 1);
                kHlpAssert(pAncestor != NULL);
                kHlpAssert(pAncestor->Obj.cchName > 0);
                pszPath[--off] = chSlash;
                off -= pAncestor->Obj.cchName;
                kHlpAssert(pAncestor->Obj.cchParent == off);
                memcpy(&pszPath[off], pAncestor->Obj.pszName, pAncestor->Obj.cchName);
            }
            return K_TRUE;
        }
    }
    else
    {
        KBOOL const fDriveLetter = pObj->cchName == 2 && pObj->pszName[2] == ':';
        off = pObj->cchName;
        if (off + fDriveLetter < cbPath)
        {
            memcpy(pszPath, pObj->pszName, off);
            if (fDriveLetter)
                pszPath[off++] = chSlash;
            pszPath[off] = '\0';
            return K_TRUE;
        }
    }

    return K_FALSE;
}


/**
 * Gets the full path to @a pObj, UTF-16 version.
 *
 * @returns K_TRUE on success, K_FALSE on buffer overflow (nothing stored).
 * @param   pObj                The object to get the full path to.
 * @param   pszPath             Where to return the path
 * @param   cbPath              The size of the output buffer.
 * @param   wcSlash             The slash to use.
 */
KBOOL kFsCacheObjGetFullPathW(PKFSOBJ pObj, wchar_t *pwszPath, KSIZE cwcPath, wchar_t wcSlash)
{
    KSIZE off = pObj->cwcParent;
    kHlpAssert(pObj->u32Magic == KFSOBJ_MAGIC);
    if (off > 0)
    {
        KSIZE offEnd = off + pObj->cwcName;
        if (offEnd < cwcPath)
        {
            PKFSDIR pAncestor;

            pwszPath[off + pObj->cwcName] = '\0';
            memcpy(&pwszPath[off], pObj->pwszName, pObj->cwcName * sizeof(wchar_t));

            for (pAncestor = pObj->pParent; off > 0; pAncestor = pAncestor->Obj.pParent)
            {
                kHlpAssert(off > 1);
                kHlpAssert(pAncestor != NULL);
                kHlpAssert(pAncestor->Obj.cwcName > 0);
                pwszPath[--off] = wcSlash;
                off -= pAncestor->Obj.cwcName;
                kHlpAssert(pAncestor->Obj.cwcParent == off);
                memcpy(&pwszPath[off], pAncestor->Obj.pwszName, pAncestor->Obj.cwcName * sizeof(wchar_t));
            }
            return K_TRUE;
        }
    }
    else
    {
        KBOOL const fDriveLetter = pObj->cchName == 2 && pObj->pszName[2] == ':';
        off = pObj->cwcName;
        if (off + fDriveLetter < cwcPath)
        {
            memcpy(pwszPath, pObj->pwszName, off * sizeof(wchar_t));
            if (fDriveLetter)
                pwszPath[off++] = wcSlash;
            pwszPath[off] = '\0';
            return K_TRUE;
        }
    }

    return K_FALSE;
}


#ifdef KFSCACHE_CFG_SHORT_NAMES

/**
 * Gets the full short path to @a pObj, ANSI version.
 *
 * @returns K_TRUE on success, K_FALSE on buffer overflow (nothing stored).
 * @param   pObj                The object to get the full path to.
 * @param   pszPath             Where to return the path
 * @param   cbPath              The size of the output buffer.
 * @param   chSlash             The slash to use.
 */
KBOOL kFsCacheObjGetFullShortPathA(PKFSOBJ pObj, char *pszPath, KSIZE cbPath, char chSlash)
{
    KSIZE off = pObj->cchShortParent;
    kHlpAssert(pObj->u32Magic == KFSOBJ_MAGIC);
    if (off > 0)
    {
        KSIZE offEnd = off + pObj->cchShortName;
        if (offEnd < cbPath)
        {
            PKFSDIR pAncestor;

            pszPath[off + pObj->cchShortName] = '\0';
            memcpy(&pszPath[off], pObj->pszShortName, pObj->cchShortName);

            for (pAncestor = pObj->pParent; off > 0; pAncestor = pAncestor->Obj.pParent)
            {
                kHlpAssert(off > 1);
                kHlpAssert(pAncestor != NULL);
                kHlpAssert(pAncestor->Obj.cchShortName > 0);
                pszPath[--off] = chSlash;
                off -= pAncestor->Obj.cchShortName;
                kHlpAssert(pAncestor->Obj.cchShortParent == off);
                memcpy(&pszPath[off], pAncestor->Obj.pszShortName, pAncestor->Obj.cchShortName);
            }
            return K_TRUE;
        }
    }
    else
    {
        KBOOL const fDriveLetter = pObj->cchShortName == 2 && pObj->pszShortName[2] == ':';
        off = pObj->cchShortName;
        if (off + fDriveLetter < cbPath)
        {
            memcpy(pszPath, pObj->pszShortName, off);
            if (fDriveLetter)
                pszPath[off++] = chSlash;
            pszPath[off] = '\0';
            return K_TRUE;
        }
    }

    return K_FALSE;
}


/**
 * Gets the full short path to @a pObj, UTF-16 version.
 *
 * @returns K_TRUE on success, K_FALSE on buffer overflow (nothing stored).
 * @param   pObj                The object to get the full path to.
 * @param   pszPath             Where to return the path
 * @param   cbPath              The size of the output buffer.
 * @param   wcSlash             The slash to use.
 */
KBOOL kFsCacheObjGetFullShortPathW(PKFSOBJ pObj, wchar_t *pwszPath, KSIZE cwcPath, wchar_t wcSlash)
{
    KSIZE off = pObj->cwcShortParent;
    kHlpAssert(pObj->u32Magic == KFSOBJ_MAGIC);
    if (off > 0)
    {
        KSIZE offEnd = off + pObj->cwcShortName;
        if (offEnd < cwcPath)
        {
            PKFSDIR pAncestor;

            pwszPath[off + pObj->cwcShortName] = '\0';
            memcpy(&pwszPath[off], pObj->pwszShortName, pObj->cwcShortName * sizeof(wchar_t));

            for (pAncestor = pObj->pParent; off > 0; pAncestor = pAncestor->Obj.pParent)
            {
                kHlpAssert(off > 1);
                kHlpAssert(pAncestor != NULL);
                kHlpAssert(pAncestor->Obj.cwcShortName > 0);
                pwszPath[--off] = wcSlash;
                off -= pAncestor->Obj.cwcShortName;
                kHlpAssert(pAncestor->Obj.cwcShortParent == off);
                memcpy(&pwszPath[off], pAncestor->Obj.pwszShortName, pAncestor->Obj.cwcShortName * sizeof(wchar_t));
            }
            return K_TRUE;
        }
    }
    else
    {
        KBOOL const fDriveLetter = pObj->cchShortName == 2 && pObj->pszShortName[2] == ':';
        off = pObj->cwcShortName;
        if (off + fDriveLetter < cwcPath)
        {
            memcpy(pwszPath, pObj->pwszShortName, off * sizeof(wchar_t));
            if (fDriveLetter)
                pwszPath[off++] = wcSlash;
            pwszPath[off] = '\0';
            return K_TRUE;
        }
    }

    return K_FALSE;
}

#endif /* KFSCACHE_CFG_SHORT_NAMES */



/**
 * Read the specified bits from the files into the given buffer, simple version.
 *
 * @returns K_TRUE on success (all requested bytes read),
 *          K_FALSE on any kind of failure.
 *
 * @param   pCache              The cache.
 * @param   pFileObj            The file object.
 * @param   offStart            Where to start reading.
 * @param   pvBuf               Where to store what we read.
 * @param   cbToRead            How much to read (exact).
 */
KBOOL kFsCacheFileSimpleOpenReadClose(PKFSCACHE pCache, PKFSOBJ pFileObj, KU64 offStart, void *pvBuf, KSIZE cbToRead)
{
    /*
     * Open the file relative to the parent directory.
     */
    MY_NTSTATUS             rcNt;
    HANDLE                  hFile;
    MY_IO_STATUS_BLOCK      Ios;
    MY_OBJECT_ATTRIBUTES    ObjAttr;
    MY_UNICODE_STRING       UniStr;

    kHlpAssertReturn(pFileObj->bObjType == KFSOBJ_TYPE_FILE, K_FALSE);
    kHlpAssert(pFileObj->pParent);
    kHlpAssertReturn(pFileObj->pParent->hDir != INVALID_HANDLE_VALUE, K_FALSE);
    kHlpAssertReturn(offStart == 0, K_FALSE); /** @todo when needed */

    Ios.Information = -1;
    Ios.u.Status    = -1;

    UniStr.Buffer        = (wchar_t *)pFileObj->pwszName;
    UniStr.Length        = (USHORT)(pFileObj->cwcName * sizeof(wchar_t));
    UniStr.MaximumLength = UniStr.Length + sizeof(wchar_t);

    MyInitializeObjectAttributes(&ObjAttr, &UniStr, OBJ_CASE_INSENSITIVE, pFileObj->pParent->hDir, NULL /*pSecAttr*/);

    rcNt = g_pfnNtCreateFile(&hFile,
                             GENERIC_READ | SYNCHRONIZE,
                             &ObjAttr,
                             &Ios,
                             NULL, /*cbFileInitialAlloc */
                             FILE_ATTRIBUTE_NORMAL,
                             FILE_SHARE_READ,
                             FILE_OPEN,
                             FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT,
                             NULL, /*pEaBuffer*/
                             0);   /*cbEaBuffer*/
    if (MY_NT_SUCCESS(rcNt))
    {
        LARGE_INTEGER offFile;
        offFile.QuadPart = offStart;

        Ios.Information = -1;
        Ios.u.Status    = -1;
        rcNt = g_pfnNtReadFile(hFile, NULL /*hEvent*/, NULL /*pfnApcComplete*/, NULL /*pvApcCtx*/, &Ios,
                               pvBuf, (KU32)cbToRead, !offStart ? &offFile : NULL, NULL /*puKey*/);
        if (MY_NT_SUCCESS(rcNt))
            rcNt = Ios.u.Status;
        if (MY_NT_SUCCESS(rcNt))
        {
            if (Ios.Information == cbToRead)
            {
                g_pfnNtClose(hFile);
                return K_TRUE;
            }
            KFSCACHE_LOG(("Error reading %#x bytes from '%ls': Information=%p\n", pFileObj->pwszName, Ios.Information));
        }
        else
            KFSCACHE_LOG(("Error reading %#x bytes from '%ls': %#x\n", pFileObj->pwszName, rcNt));
        g_pfnNtClose(hFile);
    }
    else
        KFSCACHE_LOG(("Error opening '%ls' for caching: %#x\n", pFileObj->pwszName, rcNt));
    return K_FALSE;
}


/**
 * Invalidate all cache entries of missing files.
 *
 * @param   pCache      The cache.
 */
void kFsCacheInvalidateMissing(PKFSCACHE pCache)
{
    kHlpAssert(pCache->u32Magic == KFSOBJ_MAGIC);
    pCache->auGenerationsMissing[0]++;
    kHlpAssert(pCache->uGenerationMissing < KU32_MAX);
    KFSCACHE_LOG(("Invalidate missing %#x\n", pCache->auGenerationsMissing[0]));
}


/**
 * Invalidate all cache entries (regular, custom & missing).
 *
 * @param   pCache      The cache.
 */
void kFsCacheInvalidateAll(PKFSCACHE pCache)
{
    kHlpAssert(pCache->u32Magic == KFSOBJ_MAGIC);

    pCache->auGenerationsMissing[0]++;
    kHlpAssert(pCache->auGenerationsMissing[0] < KU32_MAX);
    pCache->auGenerationsMissing[1]++;
    kHlpAssert(pCache->auGenerationsMissing[1] < KU32_MAX);

    pCache->auGenerations[0]++;
    kHlpAssert(pCache->auGenerations[0] < KU32_MAX);
    pCache->auGenerations[1]++;
    kHlpAssert(pCache->auGenerations[1] < KU32_MAX);

    KFSCACHE_LOG(("Invalidate all - default: %#x/%#x,  custom: %#x/%#x\n",
                  pCache->auGenerationsMissing[0], pCache->auGenerations[0],
                  pCache->auGenerationsMissing[1], pCache->auGenerations[1]));
}


/**
 * Invalidate all cache entries with custom generation handling set.
 *
 * @see     kFsCacheSetupCustomRevisionForTree, KFSOBJ_F_USE_CUSTOM_GEN
 * @param   pCache      The cache.
 */
void kFsCacheInvalidateCustomMissing(PKFSCACHE pCache)
{
    kHlpAssert(pCache->u32Magic == KFSOBJ_MAGIC);
    pCache->auGenerationsMissing[1]++;
    kHlpAssert(pCache->auGenerationsMissing[1] < KU32_MAX);
    KFSCACHE_LOG(("Invalidate missing custom %#x\n", pCache->auGenerationsMissing[1]));
}


/**
 * Invalidate all cache entries with custom generation handling set, both
 * missing and regular present entries.
 *
 * @see     kFsCacheSetupCustomRevisionForTree, KFSOBJ_F_USE_CUSTOM_GEN
 * @param   pCache      The cache.
 */
void kFsCacheInvalidateCustomBoth(PKFSCACHE pCache)
{
    kHlpAssert(pCache->u32Magic == KFSOBJ_MAGIC);
    pCache->auGenerations[1]++;
    kHlpAssert(pCache->auGenerations[1] < KU32_MAX);
    pCache->auGenerationsMissing[1]++;
    kHlpAssert(pCache->auGenerationsMissing[1] < KU32_MAX);
    KFSCACHE_LOG(("Invalidate both custom %#x/%#x\n", pCache->auGenerationsMissing[1], pCache->auGenerations[1]));
}



/**
 * Applies the given flags to all the objects in a tree.
 *
 * @param   pRoot               Where to start applying the flag changes.
 * @param   fAndMask            The AND mask.
 * @param   fOrMask             The OR mask.
 */
static void kFsCacheApplyFlagsToTree(PKFSDIR pRoot, KU32 fAndMask, KU32 fOrMask)
{
    PKFSOBJ    *ppCur = ((PKFSDIR)pRoot)->papChildren;
    KU32        cLeft = ((PKFSDIR)pRoot)->cChildren;
    while (cLeft-- > 0)
    {
        PKFSOBJ pCur = *ppCur++;
        if (pCur->bObjType != KFSOBJ_TYPE_DIR)
            pCur->fFlags = (fAndMask & pCur->fFlags) | fOrMask;
        else
            kFsCacheApplyFlagsToTree((PKFSDIR)pCur, fAndMask, fOrMask);
    }

    pRoot->Obj.fFlags = (fAndMask & pRoot->Obj.fFlags) | fOrMask;
}


/**
 * Sets up using custom revisioning for the specified directory tree or file.
 *
 * There are some restrictions of the current implementation:
 *      - If the root of the sub-tree is ever deleted from the cache (i.e.
 *        deleted in real life and reflected in the cache), the setting is lost.
 *      - It is not automatically applied to the lookup paths caches.
 *
 * @returns K_TRUE on success, K_FALSE on failure.
 * @param   pCache              The cache.
 * @param   pRoot               The root of the subtree.  A non-directory is
 *                              fine, like a missing node.
 */
KBOOL kFsCacheSetupCustomRevisionForTree(PKFSCACHE pCache, PKFSOBJ pRoot)
{
    if (pRoot)
    {
        if (pRoot->bObjType == KFSOBJ_TYPE_DIR)
            kFsCacheApplyFlagsToTree((PKFSDIR)pRoot, KU32_MAX, KFSOBJ_F_USE_CUSTOM_GEN);
        else
            pRoot->fFlags |= KFSOBJ_F_USE_CUSTOM_GEN;
        return K_TRUE;
    }
    return K_FALSE;
}


/**
 * Invalidates a deleted directory, ANSI version.
 *
 * @returns K_TRUE if found and is a non-root directory. Otherwise K_FALSE.
 * @param   pCache              The cache.
 * @param   pszDir              The directory.
 */
KBOOL kFsCacheInvalidateDeletedDirectoryA(PKFSCACHE pCache, const char *pszDir)
{
    KU32            cchDir = (KU32)kHlpStrLen(pszDir);
    KFSLOOKUPERROR  enmError;
    PKFSOBJ         pFsObj;

    /* Is absolute without any '..' bits? */
    if (   cchDir >= 3
        && (   (   pszDir[1] == ':'    /* Drive letter */
                && IS_SLASH(pszDir[2])
                && IS_ALPHA(pszDir[0]) )
            || (   IS_SLASH(pszDir[0]) /* UNC */
                && IS_SLASH(pszDir[1]) ) )
        && !kFsCacheHasDotDotA(pszDir, cchDir) )
        pFsObj = kFsCacheLookupAbsoluteA(pCache, pszDir, cchDir, KFSCACHE_LOOKUP_F_NO_INSERT | KFSCACHE_LOOKUP_F_NO_REFRESH,
                                         &enmError, NULL);
    else
        pFsObj = kFsCacheLookupSlowA(pCache, pszDir, cchDir, KFSCACHE_LOOKUP_F_NO_INSERT | KFSCACHE_LOOKUP_F_NO_REFRESH,
                                     &enmError, NULL);
    if (pFsObj)
    {
        /* Is directory? */
        if (pFsObj->bObjType == KFSOBJ_TYPE_DIR)
        {
            if (pFsObj->pParent != &pCache->RootDir)
            {
                PKFSDIR pDir = (PKFSDIR)pFsObj;
                KFSCACHE_LOG(("kFsCacheInvalidateDeletedDirectoryA: %s hDir=%p\n", pszDir, pDir->hDir));
                if (pDir->hDir != INVALID_HANDLE_VALUE)
                {
                    g_pfnNtClose(pDir->hDir);
                    pDir->hDir = INVALID_HANDLE_VALUE;
                }
                pDir->fNeedRePopulating = K_TRUE;
                pDir->Obj.uCacheGen = pCache->auGenerations[pDir->Obj.fFlags & KFSOBJ_F_USE_CUSTOM_GEN] - 1;
                kFsCacheObjRetainInternal(&pDir->Obj);
                return K_TRUE;
            }
            KFSCACHE_LOG(("kFsCacheInvalidateDeletedDirectoryA: Trying to invalidate a root directory was deleted! %s\n", pszDir));
        }
        else
            KFSCACHE_LOG(("kFsCacheInvalidateDeletedDirectoryA: Trying to invalidate a non-directory: bObjType=%d %s\n",
                          pFsObj->bObjType, pszDir));
        kFsCacheObjRetainInternal(pFsObj);
    }
    else
        KFSCACHE_LOG(("kFsCacheInvalidateDeletedDirectoryA: '%s' was not found\n", pszDir));
    return K_FALSE;
}


PKFSCACHE kFsCacheCreate(KU32 fFlags)
{
    PKFSCACHE pCache;
    birdResolveImports();

    pCache = (PKFSCACHE)kHlpAllocZ(sizeof(*pCache));
    if (pCache)
    {
        /* Dummy root dir entry. */
        pCache->RootDir.Obj.u32Magic        = KFSOBJ_MAGIC;
        pCache->RootDir.Obj.cRefs           = 1;
        pCache->RootDir.Obj.uCacheGen       = KFSOBJ_CACHE_GEN_IGNORE;
        pCache->RootDir.Obj.bObjType        = KFSOBJ_TYPE_DIR;
        pCache->RootDir.Obj.fHaveStats      = K_FALSE;
        pCache->RootDir.Obj.pParent         = NULL;
        pCache->RootDir.Obj.pszName         = "";
        pCache->RootDir.Obj.cchName         = 0;
        pCache->RootDir.Obj.cchParent       = 0;
#ifdef KFSCACHE_CFG_UTF16
        pCache->RootDir.Obj.cwcName         = 0;
        pCache->RootDir.Obj.cwcParent       = 0;
        pCache->RootDir.Obj.pwszName        = L"";
#endif

#ifdef KFSCACHE_CFG_SHORT_NAMES
        pCache->RootDir.Obj.pszShortName    = NULL;
        pCache->RootDir.Obj.cchShortName    = 0;
        pCache->RootDir.Obj.cchShortParent  = 0;
# ifdef KFSCACHE_CFG_UTF16
        pCache->RootDir.Obj.cwcShortName;
        pCache->RootDir.Obj.cwcShortParent;
        pCache->RootDir.Obj.pwszShortName;
# endif
#endif
        pCache->RootDir.cChildren           = 0;
        pCache->RootDir.cChildrenAllocated  = 0;
        pCache->RootDir.papChildren         = NULL;
        pCache->RootDir.hDir                = INVALID_HANDLE_VALUE;
        pCache->RootDir.fHashTabMask        = 255; /* 256: 26 drive letters and 102 UNCs before we're half ways. */
        pCache->RootDir.papHashTab          = (PKFSOBJ *)kHlpAllocZ(256 * sizeof(pCache->RootDir.papHashTab[0]));
        if (pCache->RootDir.papHashTab)
        {
            /* The cache itself. */
            pCache->u32Magic                = KFSCACHE_MAGIC;
            pCache->fFlags                  = fFlags;
            pCache->auGenerations[0]        = KU32_MAX / 4;
            pCache->auGenerations[1]        = KU32_MAX / 32;
            pCache->auGenerationsMissing[0] = KU32_MAX / 256;
            pCache->auGenerationsMissing[1] = 1;
            pCache->cObjects                = 1;
            pCache->cbObjects               = sizeof(pCache->RootDir)
                                            + (pCache->RootDir.fHashTabMask + 1) * sizeof(pCache->RootDir.papHashTab[0]);
            pCache->cPathHashHits           = 0;
            pCache->cWalkHits               = 0;
            pCache->cChildSearches          = 0;
            pCache->cChildHashHits          = 0;
            pCache->cChildHashed            = 0;
            pCache->cChildHashTabs          = 1;
            pCache->cChildHashEntriesTotal  = pCache->RootDir.fHashTabMask + 1;
            pCache->cChildHashCollisions    = 0;
            pCache->cNameChanges            = 0;
            pCache->cNameGrowths            = 0;
            pCache->cAnsiPaths              = 0;
            pCache->cAnsiPathCollisions     = 0;
            pCache->cbAnsiPaths             = 0;
#ifdef KFSCACHE_CFG_UTF16
            pCache->cUtf16Paths             = 0;
            pCache->cUtf16PathCollisions    = 0;
            pCache->cbUtf16Paths            = 0;
#endif
            return pCache;
        }

        kHlpFree(pCache);
    }
    return NULL;
}

