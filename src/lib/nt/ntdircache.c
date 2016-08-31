/* $Id: ntdircache.c 2851 2016-08-31 17:30:52Z knut.osmundsen@oracle.com $ */
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
//#include <intrin.h>
//#include <setjmp.h>
//#include <ctype.h>


//#include <Windows.h>
//#include <winternl.h>



/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** @def KFSCACHE_CFG_UTF16
 * Whether to compile in the UTF-16 names support. */
#define KFSCACHE_CFG_UTF16                  1
/** @def KFSCACHE_CFG_SHORT_NAMES
 * Whether to compile in the short name support. */
#define KFSCACHE_CFG_SHORT_NAMES            1
/** @def KFSCACHE_CFG_PATH_HASH_TAB_SIZE
 * Size of the path hash table. */
#define KFSCACHE_CFG_PATH_HASH_TAB_SIZE     16381
/** The max length paths we consider. */
#define KFSCACHE_CFG_MAX_PATH               1024
/** The max ANSI name length. */
#define KFSCACHE_CFG_MAX_ANSI_NAME          (256*3 + 16)
/** The max UTF-16 name length. */
#define KFSCACHE_CFG_MAX_UTF16_NAME         (256*2 + 16)



/** Special KFSOBJ::uCacheGen number indicating that it does not apply. */
#define KFSWOBJ_CACHE_GEN_IGNORE            KU32_MAX

/** @def KW_LOG
 * Generic logging.
 * @param a     Argument list for kFsCacheDbgPrintf  */
#ifndef NDEBUG
# define KFSCACHE_LOG(a) kFsCacheDbgPrintf a
#else
# define KFSCACHE_LOG(a) do { } while (0)
#endif


/** @name KFSOBJ_TYPE_XXX - KFSOBJ::bObjType
 * @{  */
/** Directory, type KFSDIR. */
#define KFSOBJ_TYPE_DIR         KU8_C(0x01)
/** Regular file - type KFSOBJ. */
#define KFSOBJ_TYPE_FILE        KU8_C(0x02)
/** Other file - type KFSOBJ. */
#define KFSOBJ_TYPE_OTHER       KU8_C(0x03)
/** Caching of a negative result - type KFSOBJ.
 * @remarks We will allocate enough space for the largest cache node, so this
 *          can metamorph into any other object should it actually turn up.  */
#define KFSOBJ_TYPE_MISSING     KU8_C(0x04)
///** Invalidated entry flag. */
//#define KFSOBJ_TYPE_F_INVALID   KU8_C(0x20)
/** @} */

/** @name KFSOBJ_F_XXX - KFSOBJ::fFlags
 * @{ */
/** Whether the file system update the modified timestamp of directories
 * when something is removed from it or added to it.
 * @remarks They say NTFS is the only windows filesystem doing this.  */
#define KFSOBJ_F_WORKING_DIR_MTIME      KU32_C(0x00000001)
/** NTFS file system volume. */
#define KFSOBJ_F_NTFS                   KU32_C(0x80000000)
/** @} */


#define IS_ALPHA(ch) ( ((ch) >= 'A' && (ch) <= 'Z') || ((ch) >= 'a' && (ch) <= 'z') )
#define IS_SLASH(ch) ((ch) == '\\' || (ch) == '/')


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/** Pointer to a core object.  */
typedef struct KFSOBJ *PKFSOBJ;
/** Pointer to a directory object.  */
typedef struct KFSDIR *PKFSDIR;
/** Pointer to a directory hash table entry. */
typedef struct KFSOBJHASH *PKFSOBJHASH;


/**
 * Directory hash table entry.
 *
 * There can be two of these per directory entry when the short name differs
 * from the long name.
 */
typedef struct KFSOBJHASH
{
    /** Pointer to the next entry with the same hash. */
    PKFSOBJHASH         pNext;
    /** Pointer to the object. */
    PKFSOBJ             pObj;
} KFSOBJHASH;


/**
 * Base cache node.
 */
typedef struct KFSOBJ
{
    /** Magic value (KFSOBJ_MAGIC). */
    KU32                u32Magic;
    /** Number of references. */
    KU32 volatile       cRefs;
    /** The cache generation, see KFSWOBJ_CACHE_GEN_IGNORE. */
    KU32                uCacheGen;
    /** The object type, KFSOBJ_TYPE_XXX.   */
    KU8                 bObjType;
    /** Set if the Stats member is valid, clear if not. */
    KBOOL               fHaveStats;
    /** Unused flags. */
    KBOOL               abUnused[2];
    /** Flags, KFSOBJ_F_XXX. */
    KU32                fFlags;

    /** Pointer to the parent (directory).
     * This is only NULL for a root. */
    PKFSDIR             pParent;

    /** The directory name.  (Allocated after the structure.) */
    const char         *pszName;
    /** The length of pszName. */
    KU16                cchName;
    /** The length of the parent path (up to where pszName starts).
     * @note This is valuable when constructing an absolute path to this node by
     *       means of the parent pointer (no need for recursion). */
    KU16                cchParent;
#ifdef KFSCACHE_CFG_UTF16
    /** The length of pwszName (in wchar_t's). */
    KU16                cwcName;
    /** The length of the parent UTF-16 path (in wchar_t's).
     * @note This is valuable when constructing an absolute path to this node by
     *       means of the parent pointer (no need for recursion). */
    KU16                cwcParent;
    /** The UTF-16 object name.  (Allocated after the structure.) */
    const wchar_t      *pwszName;
#endif

#ifdef KFSCACHE_CFG_SHORT_NAMES
    /** The short object name.  (Allocated after the structure, could be same
     *  as pszName.) */
    const char         *pszShortName;
    /** The length of pszShortName. */
    KU16                cchShortName;
    /** The length of the short parent path (up to where pszShortName starts). */
    KU16                cchShortParent;
# ifdef KFSCACHE_CFG_UTF16
    /** The length of pwszShortName (in wchar_t's). */
    KU16                cwcShortName;
    /** The length of the short parent UTF-16 path (in wchar_t's). */
    KU16                cwcShortParent;
    /** The UTF-16 short object name.  (Allocated after the structure, possibly
     *  same as pwszName.) */
    const wchar_t      *pwszShortName;
# endif
#endif

    /** Stats - only valid when fHaveStats is set. */
    BirdStat_T          Stats;
} KFSOBJ;

/** The magic for a KFSOBJ structure (Thelonious Sphere Monk). */
#define KFSOBJ_MAGIC                KU32_C(0x19171010)


/**
 * Directory node in the cache.
 */
typedef struct KFSDIR
{
    /** The core object information. */
    KFSOBJ             Obj;

    /** Child objects. */
    PKFSOBJ            *papChildren;
    /** The number of child objects. */
    KU32                cChildren;

    /** The size of the hash table.
     * @remarks The hash table is optional and only used when there are a lot of
     *          entries in the directory. */
    KU32                cHashTab;
    /** Pointer to the hash table.
     * @todo this isn't quite there yet, structure wise. sigh.  */
    PKFSOBJHASH         paHashTab;

    /** Handle to the directory (we generally keep it open). */
    HANDLE              hDir;
    /** The device number we queried/inherited when opening it. */
    KU64                uDevNo;

    /** Set if populated. */
    KBOOL               fPopulated;
} KFSDIR;


/**
 * Lookup errors.
 */
typedef enum KFSLOOKUPERROR
{
    /** Lookup was a success. */
    KFSLOOKUPERROR_SUCCESS = 0,
    /** A path component was not found. */
    KFSLOOKUPERROR_PATH_COMP_NOT_FOUND,
    /** A path component is not a directory. */
    KFSLOOKUPERROR_PATH_COMP_NOT_DIR,
    /** The final path entry is not a directory (trailing slash). */
    KFSLOOKUPERROR_NOT_DIR,
    /** Not found. */
    KFSLOOKUPERROR_NOT_FOUND,
    /** The path is too long. */
    KFSLOOKUPERROR_PATH_TOO_LONG,
    /** Unsupported path type. */
    KFSLOOKUPERROR_UNSUPPORTED,
    /** We're out of memory. */
    KFSLOOKUPERROR_OUT_OF_MEMORY,

    /** Error opening directory. */
    KFSLOOKUPERROR_DIR_OPEN_ERROR,
    /** Error reading directory. */
    KFSLOOKUPERROR_DIR_READ_ERROR,
    /** UTF-16 to ANSI conversion error. */
    KFSLOOKUPERROR_ANSI_CONVERSION_ERROR,
    /** ANSI to UTF-16 conversion error. */
    KFSLOOKUPERROR_UTF16_CONVERSION_ERROR,
    /** Internal error. */
    KFSLOOKUPERROR_INTERNAL_ERROR
} KFSLOOKUPERROR;


/** Pointer to an ANSI path hash table entry. */
typedef struct KFSHASHA *PKFSHASHA;
/**
 * ANSI file system path hash table entry.
 * The path hash table allows us to skip parsing and walking a path.
 */
typedef struct KFSHASHA
{
    /** Next entry with the same hash table slot. */
    PKFSHASHA           pNext;
    /** Path hash value. */
    KU32                uHashPath;
    /** The path length. */
    KU32                cchPath;
    /** The cache generation ID. */
    KU32                uCacheGen;
    /** The lookup error (when pFsObj is NULL). */
    KFSLOOKUPERROR      enmError;
    /** The path.  (Allocated after the structure.) */
    const char         *pszPath;
    /** Pointer to the matching FS object.
     * This is NULL for negative path entries? */
    PKFSOBJ             pFsObj;
} KFSHASHA;


#ifdef KFSCACHE_CFG_UTF16
/** Pointer to an UTF-16 path hash table entry. */
typedef struct KFSHASHW *PKFSHASHW;
/**
 * UTF-16 file system path hash table entry. The path hash table allows us
 * to skip parsing and walking a path.
 */
typedef struct KFSHASHW
{
    /** Next entry with the same hash table slot. */
    PKFSHASHW           pNext;
    /** Path hash value. */
    KU32                uHashPath;
    /** The path length (in wchar_t units). */
    KU32                cwcPath;
    /** The cache generation ID. */
    KU32                uCacheGen;
    /** The lookup error (when pFsObj is NULL). */
    KFSLOOKUPERROR      enmError;
    /** The path.  (Allocated after the structure.) */
    const wchar_t      *pwszPath;
    /** Pointer to the matching FS object.
     * This is NULL for negative path entries? */
    PKFSOBJ             pFsObj;
} KFSHASHW;
#endif


/** @name KFSCACHE_F_XXX
 * @{ */
/** Whether to cache missing directory entries (KFSOBJ_TYPE_MISSING). */
#define KFSCACHE_F_MISSING_OBJECTS  KU32_C(0x00000001)
/** Whether to cache missing paths. */
#define KFSCACHE_F_MISSING_PATHS    KU32_C(0x00000002)
/** @} */


/** Pointer to a cache.   */
typedef struct KFSCACHE *PKFSCACHE;
/**
 * Directory cache instance.
 */
typedef struct KFSCACHE
{
    /** Magic value (KFSCACHE_MAGIC). */
    KU32                u32Magic;
    /** Cache flags. */
    KU32                fFlags;

    /** The current cache generation for objects that already exists. */
    KU32                uGeneration;
    /** The current cache generation for missing objects, negative results, ++. */
    KU32                uGenerationMissing;

    /** Number of cache objects. */
    KSIZE               cObjects;
    /** Memory occupied by the cache object structures. */
    KSIZE               cbObjects;
    /** Number of lookups. */
    KSIZE               cLookups;
    /** Number of hits in the path hash tables. */
    KSIZE               cPathHashHits;
    /** Number of hits walking the file system hierarchy. */
    KSIZE               cWalkHits;

    /** The root directory. */
    KFSDIR              RootDir;

    /** File system hash table for ANSI filename strings. */
    PKFSHASHA           apAnsiPaths[KFSCACHE_CFG_PATH_HASH_TAB_SIZE];
    /** Number of paths in the apAnsiPaths hash table. */
    KSIZE               cAnsiPaths;
    /** Number of collisions in the apAnsiPaths hash table. */
    KSIZE               cAnsiPathCollisions;
    /** Amount of memory used by the path entries. */
    KSIZE               cbAnsiPaths;

#ifdef KFSCACHE_CFG_UTF16
    /** Number of paths in the apUtf16Paths hash table. */
    KSIZE               cUtf16Paths;
    /** Number of collisions in the apUtf16Paths hash table. */
    KSIZE               cUtf16PathCollisions;
    /** Amount of memory used by the UTF-16 path entries. */
    KSIZE               cbUtf16Paths;
    /** File system hash table for UTF-16 filename strings. */
    PKFSHASHW           apUtf16Paths[KFSCACHE_CFG_PATH_HASH_TAB_SIZE];
#endif
} KFSCACHE;

/** Magic value for KFSCACHE::u32Magic (Jon Batiste).  */
#define KFSCACHE_MAGIC              KU32_C(0x19861111)


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
KU32 kFsCacheObjRelease(PKFSCACHE pCache, PKFSOBJ pObj);


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
static void kFsCacheDbgPrintfV(const char *pszFormat, va_list va)
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
static void kFsCacheDbgPrintf(const char *pszFormat, ...)
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
 * Hashes a string.
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

#if 0

/**
 * Converts the given string to unicode.
 *
 * @returns Length of the resulting string in wchar_t's.
 * @param   pszSrc              The source string.
 * @param   pwszDst             The destination buffer.
 * @param   cwcDst              The size of the destination buffer in wchar_t's.
 */
static KSIZE kwStrToUtf16(const char *pszSrc, wchar_t *pwszDst, KSIZE cwcDst)
{
    /* Just to the quick ASCII stuff for now. correct ansi code page stuff later some time.  */
    KSIZE offDst = 0;
    while (offDst < cwcDst)
    {
        char ch = *pszSrc++;
        pwszDst[offDst++] = ch;
        if (!ch)
            return offDst - 1;
        kHlpAssert((unsigned)ch < 127);
    }

    pwszDst[offDst - 1] = '\0';
    return offDst;
}


/**
 * Converts the given UTF-16 to a normal string.
 *
 * @returns Length of the resulting string.
 * @param   pwszSrc             The source UTF-16 string.
 * @param   pszDst              The destination buffer.
 * @param   cbDst               The size of the destination buffer in bytes.
 */
static KSIZE kwUtf16ToStr(const wchar_t *pwszSrc, char *pszDst, KSIZE cbDst)
{
    /* Just to the quick ASCII stuff for now. correct ansi code page stuff later some time.  */
    KSIZE offDst = 0;
    while (offDst < cbDst)
    {
        wchar_t wc = *pwszSrc++;
        pszDst[offDst++] = (char)wc;
        if (!wc)
            return offDst - 1;
        kHlpAssert((unsigned)wc < 127);
    }

    pszDst[offDst - 1] = '\0';
    return offDst;
}



/** UTF-16 string length.  */
static KSIZE kwUtf16Len(wchar_t const *pwsz)
{
    KSIZE cwc = 0;
    while (*pwsz != '\0')
        cwc++, pwsz++;
    return cwc;
}

/**
 * Copy out the UTF-16 string following the convension of GetModuleFileName
 */
static DWORD kwUtf16CopyStyle1(wchar_t const *pwszSrc, wchar_t *pwszDst, KSIZE cwcDst)
{
    KSIZE cwcSrc = kwUtf16Len(pwszSrc);
    if (cwcSrc + 1 <= cwcDst)
    {
        kHlpMemCopy(pwszDst, pwszSrc, (cwcSrc + 1) * sizeof(wchar_t));
        return (DWORD)cwcSrc;
    }
    if (cwcDst > 0)
    {
        KSIZE cwcDstTmp = cwcDst - 1;
        pwszDst[cwcDstTmp] = '\0';
        if (cwcDstTmp > 0)
            kHlpMemCopy(pwszDst, pwszSrc, cwcDstTmp);
    }
    SetLastError(ERROR_INSUFFICIENT_BUFFER);
    return (DWORD)cwcDst;
}


/**
 * Copy out the ANSI string following the convension of GetModuleFileName
 */
static DWORD kwStrCopyStyle1(char const *pszSrc, char *pszDst, KSIZE cbDst)
{
    KSIZE cchSrc = kHlpStrLen(pszSrc);
    if (cchSrc + 1 <= cbDst)
    {
        kHlpMemCopy(pszDst, pszSrc, cchSrc + 1);
        return (DWORD)cchSrc;
    }
    if (cbDst > 0)
    {
        KSIZE cbDstTmp = cbDst - 1;
        pszDst[cbDstTmp] = '\0';
        if (cbDstTmp > 0)
            kHlpMemCopy(pszDst, pszSrc, cbDstTmp);
    }
    SetLastError(ERROR_INSUFFICIENT_BUFFER);
    return (DWORD)cbDst;
}


/**
 * Normalizes the path so we get a consistent hash.
 *
 * @returns status code.
 * @param   pszPath             The path.
 * @param   pszNormPath         The output buffer.
 * @param   cbNormPath          The size of the output buffer.
 */
static int kwPathNormalize(const char *pszPath, char *pszNormPath, KSIZE cbNormPath)
{
    char           *pchSlash;
    KSIZE           cchNormPath;

    /*
     * We hash these to speed stuff up (nt_fullpath isn't cheap and we're
     * gonna have many repeat queries and assume nobody do case changes to
     * anything essential while kmk is running).
     */
    KU32            uHashPath;
    KU32            cchPath    = (KU32)kFsCacheStrHashEx(pszPath, &uHashPath);
    KU32 const      idxHashTab = uHashPath % K_ELEMENTS(g_apFsNormalizedPathsA);
    PKFSNORMHASHA  pHashEntry = g_apFsNormalizedPathsA[idxHashTab];
    if (pHashEntry)
    {
        do
        {
            if (   pHashEntry->uHashPath == uHashPath
                && pHashEntry->cchPath   == cchPath
                && kHlpMemComp(pHashEntry->pszPath, pszPath, cchPath) == 0)
            {
                if (cbNormPath > pHashEntry->cchNormPath)
                {
                    KFSCACHE_LOG(("kwPathNormalize(%s) - hit\n", pszPath));
                    kHlpMemCopy(pszNormPath, pHashEntry->szNormPath, pHashEntry->cchNormPath + 1);
                    return 0;
                }
                return KERR_BUFFER_OVERFLOW;
            }
            pHashEntry = pHashEntry->pNext;
        } while (pHashEntry);
    }

    /*
     * Do it the slow way.
     */
    nt_fullpath(pszPath, pszNormPath, cbNormPath);
    /** @todo nt_fullpath overflow handling?!?!?   */

    pchSlash = kHlpStrChr(pszNormPath, '/');
    while (pchSlash)
    {
        *pchSlash = '\\';
        pchSlash = kHlpStrChr(pchSlash + 1, '/');
    }

    /*
     * Create a new hash table entry (ignore failures).
     */
    cchNormPath = kHlpStrLen(pszNormPath);
    if (cchNormPath < KU16_MAX && cchPath < KU16_MAX)
    {
        pHashEntry = (PKFSNORMHASHA)kHlpAlloc(sizeof(*pHashEntry) + cchNormPath + 1 + cchPath + 1);
        if (pHashEntry)
        {
            pHashEntry->cchNormPath = (KU16)cchNormPath;
            pHashEntry->cchPath     = (KU16)cchPath;
            pHashEntry->uHashPath   = uHashPath;
            pHashEntry->pszPath     = (char *)kHlpMemCopy(&pHashEntry->szNormPath[cchNormPath + 1], pszPath, cchPath + 1);
            kHlpMemCopy(pHashEntry->szNormPath, pszNormPath, cchNormPath + 1);

            pHashEntry->pNext = g_apFsNormalizedPathsA[idxHashTab];
            g_apFsNormalizedPathsA[idxHashTab] = pHashEntry;
        }
    }

    return 0;
}


/**
 * Get the pointer to the filename part of the path.
 *
 * @returns Pointer to where the filename starts within the string pointed to by pszFilename.
 * @returns Pointer to the terminator char if no filename.
 * @param   pszPath     The path to parse.
 */
static wchar_t *kwPathGetFilenameW(const wchar_t *pwszPath)
{
    const wchar_t *pwszLast = NULL;
    for (;;)
    {
        wchar_t wc = *pwszPath;
#if K_OS == K_OS_OS2 || K_OS == K_OS_WINDOWS
        if (wc == '/' || wc == '\\' || wc == ':')
        {
            while ((wc = *++pwszPath) == '/' || wc == '\\' || wc == ':')
                /* nothing */;
            pwszLast = pwszPath;
        }
#else
        if (wc == '/')
        {
            while ((wc = *++pszFilename) == '/')
                /* betsuni */;
            pwszLast = pwszPath;
        }
#endif
        if (!wc)
            return (wchar_t *)(pwszLast ? pwszLast : pwszPath);
        pwszPath++;
    }
}


/**
 * Check if the path leads to a regular file (that exists).
 *
 * @returns K_TRUE / K_FALSE
 * @param   pszPath             Path to the file to check out.
 */
static KBOOL kwLdrModuleIsRegularFile(const char *pszPath)
{
    /* For stuff with .DLL extensions, we can use the GetFileAttribute cache to speed this up! */
    KSIZE cchPath = kHlpStrLen(pszPath);
    if (   cchPath > 3
        && pszPath[cchPath - 4] == '.'
        && (pszPath[cchPath - 3] == 'd' || pszPath[cchPath - 3] == 'D')
        && (pszPath[cchPath - 2] == 'l' || pszPath[cchPath - 2] == 'L')
        && (pszPath[cchPath - 1] == 'l' || pszPath[cchPath - 1] == 'L') )
    {
        PKFSOBJ pFsObj = kFsCacheLookupA(pszPath);
        if (pFsObj)
        {
            if (!(pFsObj->fAttribs & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_DEVICE))) /* also checks invalid */
                return K_TRUE;
        }
    }
    else
    {
        BirdStat_T Stat;
        int rc = birdStatFollowLink(pszPath, &Stat);
        if (rc == 0)
        {
            if (S_ISREG(Stat.st_mode))
                return K_TRUE;
        }
    }
    return K_FALSE;
}






/**
 * Helper for getting the extension of a UTF-16 path.
 *
 * @returns Pointer to the extension or the terminator.
 * @param   pwszPath        The path.
 * @param   pcwcExt         Where to return the length of the extension.
 */
static wchar_t const *kwFsPathGetExtW(wchar_t const *pwszPath, KSIZE *pcwcExt)
{
    wchar_t const *pwszName = pwszPath;
    wchar_t const *pwszExt  = NULL;
    for (;;)
    {
        wchar_t const wc = *pwszPath++;
        if (wc == '.')
            pwszExt = pwszPath;
        else if (wc == '/' || wc == '\\' || wc == ':')
        {
            pwszName = pwszPath;
            pwszExt = NULL;
        }
        else if (wc == '\0')
        {
            if (pwszExt)
            {
                *pcwcExt = pwszPath - pwszExt - 1;
                return pwszExt;
            }
            *pcwcExt = 0;
            return pwszPath - 1;
        }
    }
}
#endif


/**
 * Looks for '..' in the path.
 *
 * @returns K_TRUE if '..' component found, K_FALSE if not.
 * @param   pszPath             The path.
 * @param   cchPath             The length of the path.
 */
static KBOOL kFsCacheHasDotDot(const char *pszPath, KSIZE cchPath)
{
    const char *pchDot = (const char *)kHlpMemChr(pszPath, '.', cchPath);
    while (pchDot)
    {
        if (pchDot[1] != '.')
            pchDot = (const char *)kHlpMemChr(pchDot + 1, '.', &pszPath[cchPath] - pchDot - 1);
        else
        {
            char ch;
            if (   (ch = pchDot[2]) == '\0'
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
 * Creates an ANSI hash table entry for the given path.
 *
 * @returns The hash table entry or NULL if out of memory.
 * @param   pCache              The hash
 * @param   pFsObj              The resulting object.
 * @param   pszPath             The path.
 * @param   cchPath             The length of the path.
 * @param   uHashPath           The hash of the path.
 * @param   idxHashTab          The hash table index of the path.
 * @param   enmError            The lookup error.
 */
static PKFSHASHA kFsCacheCreatePathHashTabEntryA(PKFSCACHE pCache, PKFSOBJ pFsObj, const char *pszPath, KU32 cchPath,
                                                 KU32 uHashPath, KU32 idxHashTab, KFSLOOKUPERROR enmError)
{
    PKFSHASHA pHashEntry = (PKFSHASHA)kHlpAlloc(sizeof(*pHashEntry) + cchPath + 1);
    if (pHashEntry)
    {
        pHashEntry->uHashPath   = uHashPath;
        pHashEntry->cchPath     = cchPath;
        pHashEntry->pszPath     = (const char *)kHlpMemCopy(pHashEntry + 1, pszPath, cchPath + 1);
        pHashEntry->pFsObj      = pFsObj;
        pHashEntry->enmError    = enmError;
        if (pFsObj)
            pHashEntry->uCacheGen = pCache->uGeneration;
        else if (enmError != KFSLOOKUPERROR_UNSUPPORTED)
            pHashEntry->uCacheGen = pCache->uGenerationMissing;
        else
            pHashEntry->uCacheGen = KFSWOBJ_CACHE_GEN_IGNORE;

        pHashEntry->pNext = pCache->apAnsiPaths[idxHashTab];
        pCache->apAnsiPaths[idxHashTab] = pHashEntry;

        pCache->cbAnsiPaths += sizeof(*pHashEntry) + cchPath + 1;
        pCache->cAnsiPaths++;
        if (pHashEntry->pNext)
            pCache->cAnsiPathCollisions++;
    }
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
    if ((pParent->cChildren % 16) == 0)
    {
        void *pvNew = kHlpRealloc(pParent->papChildren, (pParent->cChildren + 16) * sizeof(pParent->papChildren[0]));
        if (!pvNew)
            return K_FALSE;
        pParent->papChildren = (PKFSOBJ *)pvNew;
        pCache->cbObjects += 16 * sizeof(pParent->papChildren[0]);
    }
    pParent->papChildren[pParent->cChildren++] = pChild;
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
static PKFSOBJ kFsCacheCreateObject(PKFSCACHE pCache, PKFSDIR pParent,
                                    char const *pszName, KU16 cchName, wchar_t const *pwszName, KU16 cwcName,
#ifdef KFSCACHE_CFG_SHORT_NAMES
                                    char const *pszShortName, KU16 cchShortName, wchar_t const *pwszShortName, KU16 cwcShortName,
#endif
                                    KU8 bObjType, KFSLOOKUPERROR *penmError)
{
    /*
     * Allocate the object.
     */
    KBOOL const fDirish = bObjType != KFSOBJ_TYPE_FILE && bObjType == KFSOBJ_TYPE_OTHER;
    KSIZE const cbObj   = (fDirish ? sizeof(KFSDIR) : sizeof(KFSOBJ))
                        + (cwcName + 1) * sizeof(wchar_t)                           + cchName + 1
#ifdef KFSCACHE_CFG_SHORT_NAMES
                        + (cwcShortName > 0 ? (cwcShortName + 1) * sizeof(wchar_t)  + cchShortName + 1 : 0)
#endif
                          ;
    PKFSOBJ pObj = (PKFSOBJ)kHlpAlloc(cbObj);
    if (pObj)
    {
        KU8 *pbExtra = (KU8 *)(pObj + 1);

        pCache->cbObjects += cbObj;
        pCache->cObjects++;

        /*
         * Initialize the object.
         */
        pObj->u32Magic      = KFSOBJ_MAGIC;
        pObj->cRefs         = 1;
        pObj->uCacheGen     = bObjType != KFSOBJ_TYPE_MISSING ? pCache->uGeneration : pCache->uGenerationMissing;
        pObj->bObjType      = bObjType;
        pObj->fHaveStats    = K_FALSE;
        pObj->abUnused[0]   = K_FALSE;
        pObj->abUnused[1]   = K_FALSE;
        pObj->fFlags        = pParent->Obj.fFlags;
        pObj->pParent       = pParent;

#ifdef KFSCACHE_CFG_UTF16
        pObj->cwcParent = pParent->Obj.cwcParent + pParent->Obj.cwcName + !!pParent->Obj.cwcName;
        pObj->pwszName  = (wchar_t *)kHlpMemCopy(pbExtra, pwszName, cwcName * sizeof(wchar_t));
        pbExtra += cwcName * sizeof(wchar_t);
        *pbExtra++ = '\0';
        *pbExtra++ = '\0';
# ifdef KFSCACHE_CFG_SHORT_NAMES
        pObj->cwcShortParent = pParent->Obj.cwcShortParent + pParent->Obj.cwcShortName + !!pParent->Obj.cwcShortName;
        if (cwcShortName)
        {
            pObj->pwszShortName = (wchar_t *)kHlpMemCopy(pbExtra, pwszShortName, cwcShortName * sizeof(wchar_t));
            pbExtra += cwcShortName * sizeof(wchar_t);
            *pbExtra++ = '\0';
            *pbExtra++ = '\0';
        }
        else
        {
            pObj->pwszShortName = pObj->pwszName;
            pObj->cwcShortName  = pObj->cwcName;
        }
# endif
#endif
        pObj->cchParent = pParent->Obj.cchParent + pParent->Obj.cchName + !!pParent->Obj.cchName;
        pObj->pszName   = (char *)kHlpMemCopy(pbExtra, pszName, cchName);
        pbExtra += cchName;
        *pbExtra++ = '\0';
# ifdef KFSCACHE_CFG_SHORT_NAMES
        pObj->cchShortParent = pParent->Obj.cchShortParent + pParent->Obj.cchShortName + !!pParent->Obj.cchShortName;
        if (cchShortName)
        {
            pObj->pszShortName = (char *)kHlpMemCopy(pbExtra, pszShortName, cchShortName);
            pbExtra += cchShortName;
            *pbExtra++ = '\0';
        }
        else
        {
            pObj->pszShortName = pObj->pszName;
            pObj->cchShortName = pObj->cchName;
        }
#endif
        kHlpAssert(pbExtra - (KU8 *)pObj == cbObj);

        /*
         * Type specific initilization.
         */
        if (fDirish)
        {
            PKFSDIR pDirObj = (PKFSDIR)pObj;
            pDirObj->cChildren      = 0;
            pDirObj->papChildren    = NULL;
            pDirObj->cHashTab       = 0;
            pDirObj->paHashTab      = NULL;
            pDirObj->hDir           = INVALID_HANDLE_VALUE;
            pDirObj->uDevNo         = pParent->uDevNo;
            pDirObj->fPopulated     = K_FALSE;
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
static PKFSOBJ kFsCacheCreateObjectW(PKFSCACHE pCache, PKFSDIR pParent, wchar_t const *pwszName, KU32 cwcName,
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
    KBOOL                       fRefreshing  = K_FALSE;
    /** @todo will have to make this more flexible wrt information classes since
     *        older windows versions (XP, w2K) might not correctly support the
     *        ones with file ID on all file systems. */
#ifdef KFSCACHE_CFG_SHORT_NAMES
    MY_FILE_INFORMATION_CLASS   enmInfoClass = MyFileIdBothDirectoryInformation;
#else
    MY_FILE_INFORMATION_CLASS   enmInfoClass = MyFileIdFullDirectoryInformation;
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
    else if (pDir->fPopulated)
    {
        /** @todo refreshing directories. */
        __debugbreak();
        fRefreshing = K_TRUE;
    }


    /*
     * Enumerate the directory content.
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
                                     NULL,      /* Filter / restart pos. */
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
            wchar_t    *pwszFilename;

            /* ASSUME only the FileName member differs between the two structures. */
            uPtr.pb = &uBuf.abBuf[offBuf];
#ifdef KFSCACHE_CFG_SHORT_NAMES
            pwszFilename = enmInfoClass == MyFileIdBothDirectoryInformation
                         ? &uPtr.pWithId->FileName[0] :  &uPtr.pNoId->FileName[0];
#else
            pwszFilename = enmInfoClass == MyFileIdFullDirectoryInformation
                         ? &uPtr.pWithId->FileName[0] :  &uPtr.pNoId->FileName[0];
#endif
            /*
             * Create the entry (not linked yet).
             */
            pCur = kFsCacheCreateObjectW(pCache, pDir, pwszFilename, uPtr.pNoId->FileNameLength / sizeof(wchar_t),
#ifdef KFSCACHE_CFG_SHORT_NAMES
                                         uPtr.pNoId->ShortName, uPtr.pNoId->ShortNameLength / sizeof(wchar_t),
#endif
                                         uPtr.pNoId->FileAttributes & FILE_ATTRIBUTE_DIRECTORY ? KFSOBJ_TYPE_DIR
                                         : uPtr.pNoId->FileAttributes & (FILE_ATTRIBUTE_DEVICE | FILE_ATTRIBUTE_REPARSE_POINT)
                                         ? KFSOBJ_TYPE_OTHER : KFSOBJ_TYPE_FILE,
                                         penmError);
            if (!pCur)
                return K_FALSE;
            kHlpAssert(pCur->cRefs == 1);

#ifdef KFSCACHE_CFG_SHORT_NAMES
            if (enmInfoClass == MyFileIdBothDirectoryInformation)
                birdStatFillFromFileIdBothDirInfo(&pCur->Stats, uPtr.pWithId, pCur->pszName);
            else
                birdStatFillFromFileBothDirInfo(&pCur->Stats, uPtr.pNoId, pCur->pszName);
#else
            if (enmInfoClass == MyFileIdBothDirectoryInformation)
                birdStatFillFromFileIdFullDirInfo(&pCur->Stats, uPtr.pWithId, pCur->pszName);
            else
                birdStatFillFromFileFullDirInfo(&pCur->Stats, uPtr.pNoId, pCur->pszName);
#endif
            pCur->Stats.st_dev = pDir->uDevNo;

            /*
             * If we're updating we have to check the data.
             */
            if (fRefreshing)
            {
                __debugbreak();
            }

            /*
             * If we've still got pCur, add it to the directory.
             */
            if (pCur)
            {
                KBOOL fRc = kFsCacheDirAddChild(pCache, pDir, pCur, penmError);
                kFsCacheObjRelease(pCache, pCur);
                if (fRc)
                { /* likely */ }
                else
                    return K_FALSE;
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
                                         NULL,      /* Filter / restart pos. */
                                         FALSE);    /* fRestartScan */
    }

    if (rcNt == MY_STATUS_NO_MORE_FILES)
        return K_TRUE;
    kHlpAssertMsgFailed(("%#x\n", rcNt));
    *penmError = KFSLOOKUPERROR_DIR_READ_ERROR;
    return K_TRUE;
}


static KBOOL kFsCacheRefreshMissing(PKFSCACHE pCache, PKFSOBJ pMissing, KFSLOOKUPERROR *penmError)
{
    return K_TRUE;
}


static KBOOL kFsCacheRefreshMissingIntermediateDir(PKFSCACHE pCache, PKFSOBJ pMissing, KFSLOOKUPERROR *penmError)
{
    return K_TRUE;
}


static KBOOL kFsCacheRefreshObj(PKFSCACHE pCache, PKFSOBJ pObj, KFSLOOKUPERROR *penmError)
{
    return K_FALSE;
}


/**
 * Refreshes a path hash that has expired.
 *
 * @returns pHash on success, NULL if removed.
 * @param   pCache              The cache.
 * @param   pHashEntry          The path hash.
 * @param   idxHashTab          The hash table entry.
 */
static PKFSHASHA kFsCacheRefreshPathA(PKFSCACHE pCache, PKFSHASHA pHashEntry, KU32 idxHashTab)
{
    /** @todo implement once we've start inserting uCacheGen nodes. */
    __debugbreak();
    K_NOREF(pCache);
    K_NOREF(idxHashTab);
    return pHashEntry;
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
    /* Check for '.' first. */
    if (cchName != 1 || *pchName != '.')
    {
        KU32        cLeft;
        PKFSOBJ    *ppCur;

        if (pParent->paHashTab != NULL)
        {
            /** @todo directory hash table lookup.   */
        }

        /* Linear search. */
        cLeft = pParent->cChildren;
        ppCur = pParent->papChildren;
        while (cLeft-- > 0)
        {
            PKFSOBJ pCur = *ppCur++;
            if (   (   pCur->cchName == cchName
                    && _memicmp(pCur->pszName, pchName, cchName) == 0)
#ifdef KFSCACHE_CFG_SHORT_NAMES
                || (   pCur->cchShortName == cchName
                    && pCur->pszShortName != pCur->pszName
                    && _memicmp(pCur->pszName, pchName, cchName) == 0)
#endif
               )
                return pCur;
        }
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
 * @param   poff                Where to return the root dire.
 * @param   penmError           Where to return details as to why the lookup
 *                              failed.
 */
static PKFSOBJ kFswCacheLookupUncShareA(PKFSCACHE pCache, const char *pszPath, KU32 *poff, KFSLOOKUPERROR *penmError)
{
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
 * Looks up a drive letter.
 *
 * Will enter the drive if necessary.
 *
 * @returns Pointer to the root directory of the drive or an update-to-date
 *          missing node.
 * @param   pCache              The cache.
 * @param   chLetter            The uppercased drive letter.
 * @param   penmError           Where to return details as to why the lookup
 *                              failed.
 */
static PKFSOBJ kFswCacheLookupDrive(PKFSCACHE pCache, char chLetter, KFSLOOKUPERROR *penmError)
{
    KU32 const          uHash = chLetter - 'A';
    KU32                cLeft;
    PKFSOBJ            *ppCur;

    MY_UNICODE_STRING   NtPath;
    wchar_t             wszTmp[8];
    MY_NTSTATUS         rcNt;
    char                szTmp[4];

    /*
     * Custom drive letter hashing.
     */
    if (pCache->RootDir.paHashTab)
    {
        /** @todo PKFSOBJHASH pHash = */
    }

    /*
     * Special cased lookup.
     */
    cLeft = pCache->RootDir.cChildren;
    ppCur = pCache->RootDir.papChildren;
    while (cLeft-- > 0)
    {
        PKFSOBJ pCur = *ppCur++;
        if (   pCur->cchName == 2
            && pCur->pszName[0] == chLetter
            && pCur->pszName[1] == ':')
        {
            if (pCur->bObjType == KFSOBJ_TYPE_DIR)
                return pCur;
            kHlpAssert(pCur->bObjType == KFSOBJ_TYPE_MISSING);
            if (kFsCacheRefreshMissingIntermediateDir(pCache, pCur, penmError))
                return pCur;
            return NULL;
        }
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
        HANDLE hDir;
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

                /* Get the device number. */
                rcNt = birdQueryVolumeDeviceNumber(hDir, &uBuf.VolInfo, sizeof(uBuf), &pDir->uDevNo);
                kHlpAssertMsg(MY_NT_SUCCESS(rcNt), ("%#x\n", rcNt));

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
                return fRc ? &pDir->Obj : NULL;
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
 * @param   pszPath             The path to walk.
 * @param   cchPath             The length of the path.
 * @param   penmError           Where to return details as to why the lookup
 *                              failed.
 */
static PKFSOBJ kFsCacheLookupAbsoluteA(PKFSCACHE pCache, const char *pszPath, KU32 cchPath, KFSLOOKUPERROR *penmError)
{
    PKFSDIR     pParent = &pCache->RootDir;
    PKFSOBJ     pChild;
    KU32        off;
    char        ch;
    KU32        cchSlashes;
    KU32        offEnd;

    KFSCACHE_LOG(("kFsCacheLookupAbsoluteA(%s)\n", pszPath));

    /*
     * The root "directory" needs special handling, so we keep it outside the
     * main search loop. (Special: Cannot enumerate it, UNCs, ++.)
     */
    cchSlashes = 0;
    off        = 0;
    if (   pszPath[1] == ':'
        && IS_ALPHA(pszPath[0]))
    {
        /* Drive letter. */
        offEnd = 2;
        kHlpAssert(IS_SLASH(pszPath[2]));
        pChild = kFswCacheLookupDrive(pCache, toupper(pszPath[0]), penmError);
    }
    else if (   IS_SLASH(pszPath[0])
             && IS_SLASH(pszPath[1]) )
        pChild = kFswCacheLookupUncShareA(pCache, pszPath, &offEnd, penmError);
    else
    {
        *penmError = KFSLOOKUPERROR_UNSUPPORTED;
        return NULL;
    }
    if (pChild)
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
        if (   pChild->uCacheGen == KFSWOBJ_CACHE_GEN_IGNORE
            || pChild->uCacheGen == (pChild->bObjType != KFSOBJ_TYPE_MISSING ? pCache->uGeneration : pCache->uGenerationMissing)
            || kFsCacheRefreshObj(pCache, pChild, penmError))
            return kFsCacheObjRetainInternal(pChild);
        return NULL;
    }

    /* Check that we've got a valid result and not a cached negative one. */
    if (pChild->bObjType == KFSOBJ_TYPE_DIR)
    { /* likely */ }
    else
    {
        kHlpAssert(pChild->bObjType == KFSOBJ_TYPE_MISSING);
        kHlpAssert(pChild->uCacheGen == KFSWOBJ_CACHE_GEN_IGNORE || pChild->uCacheGen == pCache->uGenerationMissing);
        return pChild;
    }

    /* Next component. */
    pParent = (PKFSDIR)pChild;
    off     = offEnd + cchSlashes;


    /*
     * Walk loop.
     */
    for (;;)
    {
        /*
         * Find the end of the component, counting trailing slashes.
         */
        cchSlashes = 0;
        offEnd     = off + 1;
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
        if (   pParent->fPopulated
            && (   pParent->Obj.uCacheGen == KFSWOBJ_CACHE_GEN_IGNORE
                || pParent->Obj.uCacheGen == pCache->uGeneration) )
        { /* likely */ }
        else if (kFsCachePopuplateOrRefreshDir(pCache, pParent, penmError))
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
            if (pCache->fFlags & KFSCACHE_F_MISSING_OBJECTS)
                pChild = kFsCacheCreateMissingA(pCache, pParent, &pszPath[off], offEnd - off, penmError);
            if (cchSlashes == 0 || offEnd + cchSlashes >= cchPath)
            {
                if (pChild)
                    return pChild;
                *penmError = KFSLOOKUPERROR_NOT_FOUND;
            }
            else
                *penmError = KFSLOOKUPERROR_PATH_COMP_NOT_FOUND;
            return NULL;
        }

        /* Advance off and check if we're done already. */
        off = offEnd + cchSlashes;
        if (   cchSlashes == 0
            || off >= cchPath)
        {
            if (   pChild->bObjType != KFSOBJ_TYPE_MISSING
                || pChild->uCacheGen == KFSWOBJ_CACHE_GEN_IGNORE
                || pChild->uCacheGen == pCache->uGenerationMissing
                || kFsCacheRefreshMissing(pCache, pChild, penmError) )
            { /* likely */ }
            else
                return NULL;
            return pChild;
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
            return NULL;
        }
        else if (   pChild->uCacheGen == KFSWOBJ_CACHE_GEN_IGNORE
                 || pChild->uCacheGen == pCache->uGenerationMissing)
        {
            *penmError = KFSLOOKUPERROR_PATH_COMP_NOT_FOUND;
            return NULL;
        }
        else if (kFsCacheRefreshMissingIntermediateDir(pCache, pChild, penmError))
            pParent = (PKFSDIR)pChild;
        else
            return NULL;
    }

    return NULL;
}


/**
 * This deals with paths that are relative and paths that contains '..'
 * elements.
 *
 * @returns Pointer to object corresponding to @a pszPath on success.
 *          NULL if this isn't a path we care to cache.
 *
 * @param   pCache              The cache.
 * @param   pszPath             The path.
 * @param   cchPath             The length of the path.
 * @param   penmError           Where to return details as to why the lookup
 *                              failed.
 */
static PKFSOBJ kFsCacheLookupSlowA(PKFSCACHE pCache, const char *pszPath, KU32 cchPath, KFSLOOKUPERROR *penmError)
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
        PKFSOBJ pFsObj;
        KFSCACHE_LOG(("kFsCacheLookupSlowA(%s)\n", pszPath));
        pFsObj = kFsCacheLookupAbsoluteA(pCache, szFull, cchFull, penmError);

#if 0 /* No need to do this until it's actually queried. */
        /* Cache the resulting path. */
        if (   pFsObj
            || (pCache->fFlags & KFSCACHE_F_MISSING_PATHS)
            || *penmError == KFSLOOKUPERROR_UNSUPPORTED)
        {
            KU32 uHashPath = kFsCacheStrHash(szFull);
            kFsCacheCreatePathHashTabEntryA(pCache, pFsObj, pszPath, cchPath, uHashPath,
                                            uHashPath % K_ELEMENTS(pCache->apAnsiPaths), *penmError);
        }
#endif
        return pFsObj;
    }

    /* The path is too long! */
    kHlpAssertMsgFailed(("'%s' -> cchFull=%u\n", pszPath, cchFull));
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
 *          must be released by kwFsCacheRelease.
 *          NULL if not a path we care to cache.
 * @param   pCache              The cache.
 * @param   pszPath             The path to lookup.
 * @param   penmError           Where to return details as to why the lookup
 *                              failed.
 */
static PKFSOBJ kFsCacheLookupA(PKFSCACHE pCache, const char *pszPath, KFSLOOKUPERROR *penmError)
{
    /*
     * Do hash table lookup of the path.
     */
    KU32        uHashPath;
    KU32        cchPath    = (KU32)kFsCacheStrHashEx(pszPath, &uHashPath);
    KU32        idxHashTab = uHashPath % K_ELEMENTS(pCache->apAnsiPaths);
    PKFSHASHA   pHashEntry = pCache->apAnsiPaths[idxHashTab];
    if (pHashEntry)
    {
        do
        {
            if (   pHashEntry->uHashPath == uHashPath
                && pHashEntry->cchPath   == cchPath
                && kHlpMemComp(pHashEntry->pszPath, pszPath, cchPath) == 0)
            {
                if (   pHashEntry->uCacheGen == KFSWOBJ_CACHE_GEN_IGNORE
                    || pHashEntry->uCacheGen == pCache->uGeneration
                    || (pHashEntry = kFsCacheRefreshPathA(pCache, pHashEntry, idxHashTab)) )
                {
                    pCache->cLookups++;
                    pCache->cPathHashHits++;
                    KFSCACHE_LOG(("kFsCacheLookupA(%s) - hit %p\n", pszPath, pHashEntry->pFsObj));
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

        /* Is absolute without any '..' bits? */
        if (   cchPath >= 3
            && (   (   pszPath[1] == ':'    /* Drive letter */
                    && IS_SLASH(pszPath[2])
                    && IS_ALPHA(pszPath[0]) )
                || (   IS_SLASH(pszPath[0]) /* UNC */
                    && IS_SLASH(pszPath[1]) ) )
            && !kFsCacheHasDotDot(pszPath, cchPath) )
            pFsObj = kFsCacheLookupAbsoluteA(pCache, pszPath, cchPath, penmError);
        else
            pFsObj = kFsCacheLookupSlowA(pCache, pszPath, cchPath, penmError);
        if (   pFsObj
            || (   (pCache->fFlags & KFSCACHE_F_MISSING_PATHS)
                && *penmError != KFSLOOKUPERROR_PATH_TOO_LONG)
            || *penmError == KFSLOOKUPERROR_UNSUPPORTED )
            kFsCacheCreatePathHashTabEntryA(pCache, pFsObj, pszPath, cchPath, uHashPath, idxHashTab, *penmError);

        pCache->cLookups++;
        if (pFsObj)
            pCache->cWalkHits++;
        return pFsObj;
    }

    *penmError = KFSLOOKUPERROR_PATH_TOO_LONG;
    return NULL;
}


/**
 * Destroys a cache object which has a zero reference count.
 *
 * @returns 0
 * @param   pCache              The cache.
 * @param   pObj                The object.
 */
KU32 kFsCacheObjDestroy(PKFSCACHE pCache, PKFSOBJ pObj)
{
    kHlpAssert(pObj->cRefs == 0);
    kHlpAssert(pObj->pParent == NULL);

    switch (pObj->bObjType)
    {
        case KFSOBJ_TYPE_MISSING:
        //case KFSOBJ_TYPE_MISSING | KFSOBJ_TYPE_F_INVALID:
            /* nothing else to do here */
            pCache->cbObjects -= sizeof(*pObj);
            break;

        case KFSOBJ_TYPE_DIR:
        //case KFSOBJ_TYPE_DIR | KFSOBJ_TYPE_F_INVALID:
        case KFSOBJ_TYPE_FILE:
        //case KFSOBJ_TYPE_FILE | KFSOBJ_TYPE_F_INVALID:
            kHlpAssertFailed();
            break;
        default:
            kHlpAssertFailed();
    }
    pCache->cObjects--;
    free(pObj);
    return 0;
}


/**
 * Releases a reference to a cache object.
 *
 * @returns New reference count.
 * @param   pCache              The cache.
 * @param   pObj                The object.
 */
KU32 kFsCacheObjRelease(PKFSCACHE pCache, PKFSOBJ pObj)
{
    KU32 cRefs = --pObj->cRefs;
    if (cRefs)
        return cRefs;
    return kFsCacheObjDestroy(pCache, pObj);
}


/**
 * Retains a reference to a cahce object.
 *
 * @returns New reference count.
 * @param   pObj                The object.
 */
KU32 kFsCacheObjRetain(PKFSOBJ pObj)
{
    KU32 cRefs = ++pObj->cRefs;
    kHlpAssert(cRefs < 16384);
    return cRefs;
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
        pCache->RootDir.Obj.uCacheGen       = KFSWOBJ_CACHE_GEN_IGNORE;
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
        pCache->RootDir.papChildren         = NULL;
        pCache->RootDir.hDir                = INVALID_HANDLE_VALUE;
        pCache->RootDir.cHashTab            = 251;
        pCache->RootDir.paHashTab           = (PKFSOBJHASH)kHlpAllocZ(  pCache->RootDir.cHashTab
                                                                      * sizeof(pCache->RootDir.paHashTab[0]));
        if (pCache->RootDir.paHashTab)
        {
            /* The cache itself. */
            pCache->u32Magic        = KFSCACHE_MAGIC;
            pCache->fFlags          = fFlags;
            pCache->uGeneration     = 1;
            pCache->cObjects        = 1;
            pCache->cbObjects       = sizeof(pCache->RootDir) + pCache->RootDir.cHashTab * sizeof(pCache->RootDir.paHashTab[0]);
            pCache->cPathHashHits   = 0;
            pCache->cWalkHits       = 0;
            pCache->cAnsiPaths      = 0;
            pCache->cAnsiPathCollisions = 0;
            pCache->cbAnsiPaths     = 0;
#ifdef KFSCACHE_CFG_UTF16
            pCache->cUtf16Paths     = 0;
            pCache->cUtf16PathCollisions = 0;
            pCache->cbUtf16Paths    = 0;
#endif
            return pCache;
        }

        kHlpFree(pCache);
    }
    return NULL;
}

