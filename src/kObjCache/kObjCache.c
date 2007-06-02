/* $Id: kObjCache.c 1002 2007-06-02 10:13:03Z knut.osmundsen@oracle.com $ */
/** @file
 *
 * kObjCache - Object Cache.
 *
 * Copyright (c) 2007 knut st. osmundsen <bird-src-spam@anduin.net>
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
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <errno.h>
#include <assert.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <limits.h>
#ifndef PATH_MAX
# define PATH_MAX _MAX_PATH /* windows */
#endif 
#if defined(__OS2__) || defined(__WIN__)
# include <process.h>
# include <io.h>
# ifdef __OS2__
#  include <unistd.h>
# endif 
#else
# include <unistd.h>
# include <sys/wait.h>
# ifndef O_BINARY
#  define O_BINARY 0
# endif
#endif 
#include "crc32.h"
#include "md5.h"


/*******************************************************************************
*   Structures and Typedefs                                                    *
*******************************************************************************/
/** A checksum list entry.
 * We keep a list checksums (of precompiler output) that matches, The planned 
 * matching algorithm doesn't require the precompiler output to be indentical, 
 * only to produce the same object files.
 */
typedef struct KOCSUM
{
    /** The next checksum. */
    struct KOCSUM *pNext;
    /** The crc32 checksum. */
    uint32_t crc32;
    /** The MD5 digest. */
    unsigned char md5[16];
} KOCSUM, *PKOCSUM;
/** Pointer to a const KOCSUM. */
typedef const KOCSUM *PCKOCSUM;

/**
 * The object cache data.
 */
typedef struct KOBJCACHE
{
    /** The cache dir that all other names are relative to. */
    char *pszDir;
    /** The name of the cache file. */
    const char *pszName;
    /** Set if the object needs to be (re)compiled. */
    unsigned fNeedCompiling;

    /** The name of new precompiled output. */
    const char *pszNewCppName;
    /** Pointer to the 'mapping' of the new precompiled output. */
    char *pszNewCppMapping;
    /** The size of the new precompiled output 'mapping'. */
    size_t cbNewCppMapping;
    /** The new checksum. */
    KOCSUM NewSum;
    /** The new object filename (relative to the cache file). */
    char *pszNewObjName;

    /** The name of the precompiled output. (relative to the cache file) */
    char *pszOldCppName;
    /** Pointer to the 'mapping' of the old precompiled output. */
    char *pszOldCppMapping;
    /** The size of the old precompiled output 'mapping'. */
    size_t cbOldCppMapping;

    /** The raw cache file buffer.
     * The const members below points into this. */
    char *pszRawFile;
    /** The head of the checksum list. */
    KOCSUM SumHead;
    /** The object filename (relative to the cache file). */
    const char *pszObjName;
    /** The compile argument vector used to build the object. */
    const char **papszArgvCompile;
    /** The size of the compile  */
    unsigned cArgvCompile;
} KOBJCACHE, *PKOBJCACHE;
/** Pointer to a const KOBJCACHE. */
typedef const KOBJCACHE *PCKOBJCACHE;


/*******************************************************************************
*   Global Variables                                                           *
*******************************************************************************/
/** Whether verbose output is enabled. */
static int g_fVerbose = 0;


/*******************************************************************************
*   Internal Functions                                                         *
*******************************************************************************/
static const char *FindFilenameInPath(const char *pszPath);
static char *AbsPath(const char *pszPath);
static char *MakePathFromDirAndFile(const char *pszName, const char *pszDir);
static char *CalcRelativeName(const char *pszPath, const char *pszDir);
static FILE *FOpenFileInDir(const char *pszName, const char *pszDir, const char *pszMode);
static int UnlinkFileInDir(const char *pszName, const char *pszDir);
static int RenameFileInDir(const char *pszOldName, const char *pszNewName, const char *pszDir);
static int DoesFileInDirExist(const char *pszName, const char *pszDir);
static void *ReadFileInDir(const char *pszName, const char *pszDir, size_t *pcbFile);
static void *xmalloc(size_t);
static void *xrealloc(void *, size_t);
static char *xstrdup(const char *);


/* crc.c */
extern uint32_t crc32(uint32_t, const void *, size_t);


/**
 * Compares two check sum entries. 
 *  
 * @returns 1 if equal, 0 if not equal. 
 *  
 * @param pSum1     The first checksum.
 * @param pSum2     The second checksum.
 */
static int kObjCacheSumIsEqual(PCKOCSUM pSum1, PCKOCSUM pSum2)
{
    if (pSum1 == pSum2)
        return 1;
    if (pSum1 || !pSum2)
        return 0;
    if (pSum1->crc32 != pSum2->crc32)
        return 0;
    if (memcmp(&pSum1->md5[0], &pSum2->md5[0], sizeof(pSum1->md5)))
        return 0;
    return 1;
}


/**
 * Print a fatal error message and exit with rc=1.
 * 
 * @param   pEntry      The cache entry.
 * @param   pszFormat   The message to print.
 * @param   ...         Format arguments.
 */
static void kObjCacheFatal(PCKOBJCACHE pEntry, const char *pszFormat, ...)
{
    va_list va;

    fprintf(stderr, "kObjCache %s - fatal error: ", pEntry->pszName);
    va_start(va, pszFormat);
    vfprintf(stderr, pszFormat, va);
    va_end(va);

    exit(1);
}


/**
 * Print a verbose message if verbosisty is enabled.
 * 
 * @param   pEntry      The cache entry.
 * @param   pszFormat   The message to print.
 * @param   ...         Format arguments.
 */
static void kObjCacheVerbose(PCKOBJCACHE pEntry, const char *pszFormat, ...)
{
    if (g_fVerbose)
    {
        va_list va;
    
        fprintf(stdout, "kObjCache %s - info: ", pEntry->pszName);
        va_start(va, pszFormat);
        vfprintf(stdout, pszFormat, va);
        va_end(va);
    }
}


/**
 * Creates a cache entry for the given cache file name.
 *  
 * @returns Pointer to a cache entry.
 * @param   pszFilename     The cache file name.
 */
static PKOBJCACHE kObjCacheCreate(const char *pszFilename)
{
    PKOBJCACHE pEntry;

    /*
     * Allocate an empty entry.
     */
    pEntry = xmalloc(sizeof(*pEntry));
    memset(pEntry, 0, sizeof(*pEntry));

    /*
     * Setup the directory and cache file name.
     */
    pEntry->pszDir = AbsPath(pszFilename);
    pEntry->pszName = FindFilenameInPath(pEntry->pszDir);
    if (pEntry->pszDir == pEntry->pszName)
        kObjCacheFatal(pEntry, "Failed to find abs path for '%s'!\n", pszFilename);
    ((char *)pEntry->pszName)[-1] = '\0';

    return pEntry;
}


#if 0 /* don't bother. */
/**
 * Destroys the cache entry freeing up all it's resources. 
 *  
 * @param   pEntry      The entry to free.
 */
static void kObjCacheDestroy(PKOBJCACHE pEntry)
{
    free(pEntry->pszDir);
    free(pEntry->pszName);
    while (pEntry->SumHead.pNext)
    {
        void *pv = pEntry->SumHead.pNext;
        pEntry->SumHead.pNext = pEntry->SumHead.pNext->pNext;
        if (pv != &pEntry->NewSum)
            free(pv);
    }
    free(pEntry);
}
#endif


/**
 * Reads and parses the cache file.
 *  
 * @param   pEntry      The entry to read it into.
 */
static void kObjCacheRead(PKOBJCACHE pEntry)
{
    kObjCacheVerbose(pEntry, "reading cache file...\n");
    pEntry->fNeedCompiling = 1;
}


/**
 * Writes the cache file.
 *  
 * @param   pEntry      The entry to write.
 */
static void kObjCacheWrite(PKOBJCACHE pEntry)
{
    kObjCacheVerbose(pEntry, "writing cache file...\n");
    
}


/**
 * Spawns a child in a synchronous fashion.
 * Terminating on failure.
 * 
 * @param   papszArgv       Argument vector. The cArgv element is NULL.
 * @param   cArgv           The number of arguments in the vector.
 */
static void kObjCacheSpawn(PCKOBJCACHE pEntry, const char **papszArgv, unsigned cArgv, const char *pszMsg, const char *pszStdOut)
{
#if defined(__OS2__) || defined(__WIN__)
    intptr_t rc;
    int fdStdOut = -1;
    if (pszStdOut)
    {
        int fdReDir;
        fdStdOut = dup(1); /* dup2(1,-1) doesn't work right on windows */
        close(1);
        fdReDir = open(pszStdOut, O_CREAT | O_TRUNC | O_WRONLY, 0777);
        if (fdReDir < 0)
            kObjCacheFatal(pEntry, "%s - failed to create stdout redirection file '%s': %s\n", 
                           pszMsg, pszStdOut, strerror(errno));
        if (fdReDir != 1)
        {
            if (dup2(fdReDir, 1) < 0)
                kObjCacheFatal(pEntry, "%s - dup2 failed: %s\n", pszMsg, strerror(errno));
            close(fdReDir);
        }
    }

    errno = 0;
    rc = _spawnvp(_P_WAIT, papszArgv[0], papszArgv);
    if (rc < 0)
        kObjCacheFatal(pEntry, "%s - _spawnvp failed (rc=0x%p): %s\n", pszMsg, rc, strerror(errno));
    if (rc > 0)
        kObjCacheFatal(pEntry, "%s - failed rc=%d\n", pszMsg, (int)rc);
    if (fdStdOut)
    {
        close(1);
        fdStdOut = dup2(fdStdOut, 1);
        close(fdStdOut);
    }

#else
    int iStatus;
    pid_t pidWait;
    pid_t pid = fork();
    if (!pid)
    {
        if (pszStdOut)
        {
            close(1);
            fdReDir = open(pszStdOut, O_CREAT | O_TRUNC | O_WRONLY, 0777);
            if (fdReDir < 0)
                kObjCacheFatal(pEntry, "%s - failed to create stdout redirection file '%s': %s\n", 
                               pszMsg, pszStdOut, strerror(errno));
            if (fdReDir != 1)
            {
                if (dup2(fdReDir, 1) < 0)
                    kObjCacheFatal(pEntry, "%s - dup2 failed: %s\n", pszMsg, strerror(errno));
                close(fdReDir);
            }
        }

        execvp(papszArgv[0], papszArgv);
        kObjCacheFatal(pEntry, "%s - execvp failed rc=%d errno=%d %s\n", 
                       pszMsg, rc, errno, strerror(errno));
    }
    pidWait = waitpid(pid, &iStatus);
    while (pidWait < 0 && errno == EINTR)
        pidWait = waitpid(pid, &iStatus);
    if (pidWait != pid)
        kObjCacheFatal(pEntry, "%s - waitpid failed rc=%d errno=%d %s\n", 
                       pszMsg, rc, errno, strerror(errno));
    if (!WIFEXITED(iStatus))
        kObjCacheFatal(pEntry, "%s - abended (iStatus=%#x)\n", pszMsg, iStatus);
    if (WEXITSTATUS(iStatus))
        kObjCacheFatal(pEntry, "%s - failed with rc %d\n", pszMsg, WEXITSTATUS(iStatus));
#endif
    (void)cArgv;
}


/**
 * Worker for kObjCachePreCompile and calculates the checksum of 
 * the precompiler output.
 * 
 * @param   pEntry      The cache entry. NewSum will be updated.
 */
static void kObjCacheCalcChecksum(PKOBJCACHE pEntry)
{
    struct MD5Context MD5Ctx;

    /* 
     * Read/maps the entire file into a buffer and does the crc sums 
     * on the buffer. This assumes the precompiler output isn't 
     * gigantic, but that's a pretty safe assumption I hope...
     */
    pEntry->pszNewCppMapping = ReadFileInDir(pEntry->pszNewCppName, pEntry->pszDir, &pEntry->cbNewCppMapping);
    if (!pEntry->pszNewCppMapping)
        kObjCacheFatal(pEntry, "failed to open/read '%s' in '%s': %s\n", 
                       pEntry->pszNewCppName, pEntry->pszDir, strerror(errno));
    kObjCacheVerbose(pEntry, "precompiled file is %lu bytes long\n", (unsigned long)pEntry->cbNewCppMapping);

    pEntry->NewSum.crc32 = crc32(0, pEntry->pszNewCppMapping, pEntry->cbNewCppMapping);
    MD5Init(&MD5Ctx);
    MD5Update(&MD5Ctx, pEntry->pszNewCppMapping, pEntry->cbNewCppMapping);
    MD5Final(&pEntry->NewSum.md5[0], &MD5Ctx);
    kObjCacheVerbose(pEntry, "crc32=%#lx md5=08x%08x%08x%08x\n", pEntry->NewSum.crc32, 
                     ((uint32_t *)&pEntry->NewSum.md5[0])[0],
                     ((uint32_t *)&pEntry->NewSum.md5[0])[1],
                     ((uint32_t *)&pEntry->NewSum.md5[0])[2],
                     ((uint32_t *)&pEntry->NewSum.md5[0])[3]);
}


/**
 * Run the precompiler and calculate the checksum of the output.
 *  
 * @param   pEntry              The cache entry.
 * @param   papszArgvPreComp    The argument vector for executing precompiler. The cArgvPreComp'th argument must be NULL.
 * @param   cArgvPreComp        The number of arguments.
 * @param   pszPreCompName      Precompile output name. (must kick around)
 * @param   fRedirStdOut        Whether stdout needs to be redirected or not.
 */
static void kObjCachePreCompile(PKOBJCACHE pEntry, const char **papszArgvPreComp, unsigned cArgvPreComp, const char *pszPreCompName, int fRedirStdOut)
{
    /*
     * Rename the old precompiled output to '-old'.
     * We'll discard the old output and keep the new output, but because
     * we might with to do a quick matchup later we can't remove it just now.
     */
    if (    pEntry->pszOldCppName 
        &&  DoesFileInDirExist(pEntry->pszOldCppName, pEntry->pszDir))
    {
        size_t cch = strlen(pEntry->pszOldCppName);
        char *psz = xmalloc(cch + sizeof("-old"));
        memcpy(psz, pEntry->pszOldCppName, cch);
        memcpy(psz + cch, "-old", sizeof("-old"));

        kObjCacheVerbose(pEntry, "renaming '%s' to '%s' in '%s'\n", pEntry->pszOldCppName, psz, pEntry->pszDir);
        UnlinkFileInDir(psz, pEntry->pszDir);
        if (RenameFileInDir(pEntry->pszOldCppName, psz, pEntry->pszDir))
            kObjCacheFatal(pEntry, "failed to rename '%s' -> '%s' in '%s': %s\n", 
                           pEntry->pszOldCppName, psz, pEntry->pszDir, strerror(errno));
        free(pEntry->pszOldCppName);
        pEntry->pszOldCppName = psz;
    }
    pEntry->pszNewCppName = CalcRelativeName(pszPreCompName, pEntry->pszDir);

    /*
     * Precompile it and calculate the checksum on the output.
     */
    kObjCacheVerbose(pEntry, "precompiling -> '%s'...\n", pEntry->pszNewCppName);
    if (fRedirStdOut)
        kObjCacheSpawn(pEntry, papszArgvPreComp, cArgvPreComp, "precompile", pszPreCompName);
    else
        kObjCacheSpawn(pEntry, papszArgvPreComp, cArgvPreComp, "precompile", NULL);
    kObjCacheCalcChecksum(pEntry);
}


/**
 * Worker for kObjCacheCompileIfNeeded that compares the 
 * precompiled output. 
 * 
 * @returns 1 if matching, 0 if not matching. 
 * @param   pEntry      The entry containing the names of the files to compare.
 *                      The entry is not updated in any way.
 */
static int kObjCacheCompareOldAndNewOutput(PCKOBJCACHE pEntry)
{
    /** @todo do some quick but fancy comparing that determins whether code 
     * has actually changed or moved. We can ignore declarations and typedefs that
     * has just been moved up/down a bit. The typical case is adding a new error 
     * #define that doesn't influence the current compile job. */
    return 0;
}


/**
 * Worker for kObjCacheCompileIfNeeded that does the actual (re)compilation. 
 * 
 * @returns 1 if matching, 0 if not matching. 
 * @param   pEntry              The cache entry.
 * @param   papszArgvCompile    The argument vector for invoking the compiler. The cArgvCompile'th entry must be NULL.
 * @param   cArgvCompile        The number of arguments in the vector.
 * @param   pszObjName          The name of the object file.
 */
static void kObjCacheCompileIt(PKOBJCACHE pEntry, const char **papszArgvCompile, unsigned cArgvCompile, const char *pszObjName)
{
    /*
     * Delete the old object file and precompiler output.
     */
    if (pEntry->pszObjName)
    {
        UnlinkFileInDir(pEntry->pszObjName, pEntry->pszDir);
        pEntry->pszObjName = NULL;
    }
    pEntry->pszNewObjName = CalcRelativeName(pszObjName, pEntry->pszDir);

    /*
     * Release buffers we no longer need before starting the compile.
     */
    free(pEntry->pszNewCppMapping);
    pEntry->pszNewCppMapping = NULL;
    free(pEntry->pszOldCppMapping);
    pEntry->pszOldCppMapping = NULL;

    /*
     * Do the recompilation.
     */
    kObjCacheVerbose(pEntry, "compiling -> '%s'...\n", pEntry->pszNewObjName);
    pEntry->papszArgvCompile = papszArgvCompile;
    pEntry->cArgvCompile = cArgvCompile;
    kObjCacheSpawn(pEntry, papszArgvCompile, cArgvCompile, "compile", NULL);
}


/**
 * Check if (re-)compilation is required and do it.
 * 
 * @returns 1 if matching, 0 if not matching. 
 * @param   pEntry              The cache entry.
 * @param   papszArgvCompile    The argument vector for invoking the compiler. The cArgvCompile'th entry must be NULL.
 * @param   cArgvCompile        The number of arguments in the vector.
 * @param   pszObjName          The name of the object file.
 */
static void kObjCacheCompileIfNeeded(PKOBJCACHE pEntry, const char **papszArgvCompile, unsigned cArgvCompile, const char *pszObjName)
{
    /*
     * Does the object name differ?
     */
    if (!pEntry->fNeedCompiling)
    {
        char *pszTmp = CalcRelativeName(pszObjName, pEntry->pszDir);
        if (strcmp(pEntry->pszObjName, pszTmp))
        {
            pEntry->fNeedCompiling = 1;
            kObjCacheVerbose(pEntry, "object name changed '%s' -> '%s'\n", pEntry->pszObjName, pszTmp);
        }
        free(pszTmp);
    }

    /*
     * Does the compile command differ?
     * TODO: Ignore irrelevant options here (like warning level).
     */
    if (    !pEntry->fNeedCompiling 
        &&  pEntry->cArgvCompile != cArgvCompile)
    {
        pEntry->fNeedCompiling = 1;
        kObjCacheVerbose(pEntry, "compile argument count changed\n");
    }
    if (!pEntry->fNeedCompiling)
    {
        unsigned i;
        for (i = 0; i < cArgvCompile; i++)
            if (strcmp(papszArgvCompile[i], pEntry->papszArgvCompile[i]))
            {
                pEntry->fNeedCompiling = 1;
                kObjCacheVerbose(pEntry, "compile argument differs (%#d)\n", i);
                break;
            }
    }

    /*
     * Does the object file exist?
     */
    if (    !pEntry->fNeedCompiling
        &&  !DoesFileInDirExist(pEntry->pszObjName, pEntry->pszDir))
    {
        pEntry->fNeedCompiling = 1;
        kObjCacheVerbose(pEntry, "object file doesn't exist\n");
    }

    /*
     * Does the precompiled output differ in any significant way?
     */
    if (!pEntry->fNeedCompiling)
    {
        int fFound = 0;
        PCKOCSUM pSum;
        for (pSum = &pEntry->SumHead; pSum; pSum = pSum->pNext)
            if (kObjCacheSumIsEqual(pSum, &pEntry->NewSum))
            {
                fFound = 1;
                break;
            }
        if (!fFound)
        {
            kObjCacheVerbose(pEntry, "no checksum match - comparing output\n");
            if (!kObjCacheCompareOldAndNewOutput(pEntry))
                pEntry->fNeedCompiling = 1;
            else
            {
                /* insert the sum into the list. */
                pEntry->NewSum.pNext = pEntry->SumHead.pNext;
                pEntry->SumHead.pNext = &pEntry->NewSum;
            }
        }
    }

    /*
     * Discard the old precompiled output it's no longer needed.s
     */
    if (pEntry->pszOldCppName)
    {
        UnlinkFileInDir(pEntry->pszOldCppName, pEntry->pszDir);
        free(pEntry->pszOldCppName);
        pEntry->pszOldCppName = NULL;
    }

    /*
     * Do the compliation if found necessary.
     */
    if (pEntry->fNeedCompiling)
        kObjCacheCompileIt(pEntry, papszArgvCompile, cArgvCompile, pszObjName);
}


/**
 * Gets the absolute path 
 * 
 * @returns A new heap buffer containing the absolute path.
 * @param   pszPath     The path to make absolute. (Readonly)
 */
static char *AbsPath(const char *pszPath)
{
    char szTmp[PATH_MAX];
#if defined(__OS2__) || defined(__WIN__)
    if (!_fullpath(szTmp, *pszPath ? pszPath : ".", sizeof(szTmp)))
        return xstrdup(pszPath);
#else
    if (!realpath(pszPath, szTmp))
        return xstrdup(pszPath);
#endif
   return xstrdup(szTmp);
}


/**
 * Utility function that finds the filename part in a path.
 * 
 * @returns Pointer to the file name part (this may be "").
 * @param   pszPath     The path to parse.
 */
static const char *FindFilenameInPath(const char *pszPath)
{
    const char *pszFilename = strchr(pszPath, '\0') - 1;
    while (     pszFilename > pszPath 
#if defined(__OS2__) || defined(__WIN__)
           &&   pszFilename[-1] != ':' && pszFilename[-1] != '/' && pszFilename[-1] != '\\')
#else
           &&   pszFilename[-1] != '/')
#endif
        pszFilename--;
    return pszFilename;
}


/**
 * Utility function that combines a filename and a directory into a path.
 * 
 * @returns malloced buffer containing the result.
 * @param   pszName     The file name.
 * @param   pszDir      The directory path.
 */
static char *MakePathFromDirAndFile(const char *pszName, const char *pszDir)
{
    size_t cchName = strlen(pszName);
    size_t cchDir = strlen(pszDir);
    char *pszBuf = xmalloc(cchName + cchDir + 2);
    memcpy(pszBuf, pszDir, cchDir);
#if defined(__OS2__) || defined(__WIN__)
    if (cchDir > 0 && pszDir[cchDir - 1] != '/' && pszDir[cchDir - 1] != '\\' && pszDir[cchDir - 1] != ':')
#else
    if (cchDir > 0 && pszDir[cchDir - 1] != '/')
#endif 
        pszBuf[cchDir++] = '/';
    memcpy(pszBuf + cchDir, pszName, cchName + 1);
    return pszBuf;
}


/**
 * Compares two path strings to see if they are identical.
 * 
 * This doesn't do anything fancy, just the case ignoring and 
 * slash unification.
 * 
 * @returns 1 if equal, 0 otherwise.
 * @param   pszPath1    The first path.
 * @param   pszPath2    The second path.
 * @param   cch         The number of characters to compare.
 */
static int ArePathsIdentical(const char *pszPath1, const char *pszPath2, size_t cch)
{
#if defined(__OS2__) || defined(__WIN__)
    if (strnicmp(pszPath1, pszPath2, cch))
    {
        /* Slashes may differ, compare char by char. */
        const char *psz1 = pszPath1;
        const char *psz2 = pszPath2;
        for (;cch; psz1++, psz2++, cch--)
        {
            if (*psz1 != *psz2)
            {
                if (    tolower(*psz1) != tolower(*psz2)
                    &&  toupper(*psz1) != toupper(*psz2)
                    &&  *psz1 != '/'
                    &&  *psz1 != '\\'
                    &&  *psz2 != '/'
                    &&  *psz2 != '\\')
                    return 0;
            }
        }
    }
    return 1;
#else
    return !strncmp(pszPath1, pszPath2, cch);
#endif 
}


/**
 * Calculate how to get to pszPath from pszDir.
 * 
 * @returns The relative path from pszDir to path pszPath.
 * @param   pszPath     The path to the object.
 * @param   pszDir      The directory it shall be relative to.
 */
static char *CalcRelativeName(const char *pszPath, const char *pszDir)
{
    char *pszRet = NULL;
    char *pszAbsPath = NULL;
    size_t cchDir = strlen(pszDir);

    /*
     * This is indeed a bit tricky, so we'll try the easy way first...
     */
    if (ArePathsIdentical(pszPath, pszDir, cchDir))
    {
        if (pszPath[cchDir])
            pszRet = (char *)pszPath + cchDir;
        else
            pszRet = "./";
    }
    else
    {
        pszAbsPath = AbsPath(pszPath);
        if (ArePathsIdentical(pszAbsPath, pszDir, cchDir))
        {
            if (pszPath[cchDir])
                pszRet = pszAbsPath + cchDir;
            else
                pszRet = "./";
        }
    }
    if (pszRet)
    {
#if defined(__OS2__) || defined(__WIN__)
        while (*pszRet == ':' || *pszRet == '/' || *pszRet == '\\')
#else
        while (*pszRet == '/')
#endif 
            pszRet++;
        pszRet = xstrdup(pszRet);
        free(pszAbsPath);
        return pszRet;
    }

    /*
     * Damn, it's gonna be complicated. Deal with that later.
     */
    fprintf(stderr, "kObjCache: complicated relative path stuff isn't implemented yet. sorry.\n");
    exit(1);
    return NULL;
}


/**
 * Utility function that combines a filename and directory and passes it onto fopen.
 * 
 * @returns fopen return value.
 * @param   pszName     The file name.
 * @param   pszDir      The directory path.
 * @param   pszMode     The fopen mode string.
 */
static FILE *FOpenFileInDir(const char *pszName, const char *pszDir, const char *pszMode)
{
    char *pszPath = MakePathFromDirAndFile(pszName, pszDir);
    FILE *pFile = fopen(pszPath, pszMode);
    free(pszPath);
    return pFile;
}


/**
 * Deletes a file in a directory.
 * 
 * @returns whatever unlink returns.
 * @param   pszName     The file name.
 * @param   pszDir      The directory path.
 */
static int UnlinkFileInDir(const char *pszName, const char *pszDir)
{
    char *pszPath = MakePathFromDirAndFile(pszName, pszDir);
    int rc = unlink(pszPath);
    free(pszPath);
    return rc;
}


/**
 * Renames a file in a directory.
 * 
 * @returns whatever unlink returns.
 * @param   pszOldName  The new file name.
 * @param   pszNewName  The old file name.
 * @param   pszDir      The directory path.
 */
static int RenameFileInDir(const char *pszOldName, const char *pszNewName, const char *pszDir)
{
    char *pszOldPath = MakePathFromDirAndFile(pszOldName, pszDir);
    char *pszNewPath = MakePathFromDirAndFile(pszNewName, pszDir);
    int rc = rename(pszOldName, pszNewName);
    free(pszOldPath);
    free(pszNewPath);
    return rc;
}


/**
 * Check if a (regular) file exists in a directory.
 * 
 * @returns 1 if it exists and is a regular file, 0 if not.
 * @param   pszName     The file name.
 * @param   pszDir      The directory path.
 */
static int DoesFileInDirExist(const char *pszName, const char *pszDir)
{
    char *pszPath = MakePathFromDirAndFile(pszName, pszDir);
    struct stat st;
    int rc = stat(pszPath, &st);
    free(pszPath);
#ifdef S_ISREG
    return !rc && S_ISREG(st.st_mode);
#elif defined(_MSC_VER)
    return !rc && (st.st_mode & _S_IFMT) == _S_IFREG;
#else
#error "Port me"
#endif 
}


/**
 * Reads into memory an entire file.
 * 
 * @returns Pointer to the heap allocation containing the file.
 *          On failure NULL and errno is returned.
 * @param   pszName     The file.
 * @param   pszDir      The directory the file resides in.
 * @param   pcbFile     Where to store the file size.
 */
static void *ReadFileInDir(const char *pszName, const char *pszDir, size_t *pcbFile)
{
    int SavedErrno;
    char *pszPath = MakePathFromDirAndFile(pszName, pszDir);
    int fd = open(pszName, O_RDONLY | O_BINARY);
    if (fd >= 0)
    {
        off_t cbFile = lseek(fd, 0, SEEK_END);
        if (    cbFile >= 0
            &&  lseek(fd, 0, SEEK_SET) == 0)
        {
            char *pb = malloc(cbFile + 1);
            if (pb)
            {
                if (read(fd, pb, cbFile) == cbFile)
                {
                    close(fd);
                    pb[cbFile] = '\0';
                    *pcbFile = (size_t)cbFile;
                    return pb;
                }
                SavedErrno = errno;
                free(pb);
            }
            else
                SavedErrno = ENOMEM;
        }
        else
            SavedErrno = errno;
        close(fd);
    }
    else
        SavedErrno = errno;
    free(pszPath);
    errno = SavedErrno;
    return NULL;
}


static void *xmalloc(size_t cb)
{
    void *pv = malloc(cb);
    if (!pv)
        kObjCacheFatal(NULL, "out of memory (%d)\n", (int)cb);
    return pv;
}


static void *xrealloc(void *pvOld, size_t cb)
{
    void *pv = realloc(pvOld, cb);
    if (!pv)
        kObjCacheFatal(NULL, "out of memory (%d)\n", (int)cb);
    return pv;
}


static char *xstrdup(const char *pszIn)
{
    char *psz = strdup(pszIn);
    if (!psz)
        kObjCacheFatal(NULL, "out of memory (%d)\n", (int)strlen(pszIn));
    return psz;
}


/**
 * Prints a syntax error and returns the appropriate exit code
 * 
 * @returns approriate exit code.
 * @param   pszFormat   The syntax error message.
 * @param   ...         Message args.
 */
static int SyntaxError(const char *pszFormat, ...)
{
    va_list va;
    fprintf(stderr, "kObjCache: syntax error: ");
    va_start(va, pszFormat);
    vfprintf(stderr, pszFormat, va);
    va_end(va);
    return 1;
}


/**
 * Prints the usage.
 * @returns 0.
 */
static int usage(void)
{
    printf("syntax: kObjCache [-v|--verbose] [-f|--file] <cache-file> [-V|--version] [-r|--redir-stdout]\n"
           "                  --kObjCache-cpp <filename> <precompiler + args> \n"
           "                  --kObjCache-cc <object> <compiler + args>\n"
           "                  [--kObjCache-both [args]]\n"
           "                  [--kObjCache-cpp|--kObjCache-cc [more args]]\n"
           "\n");
    return 0;
}


int main(int argc, char **argv)
{
    PKOBJCACHE pEntry;

    const char *pszCacheFile;

    const char **papszArgvPreComp = NULL;
    unsigned cArgvPreComp = 0;
    const char *pszPreCompName = NULL;
    int fRedirStdOut = 0;

    const char **papszArgvCompile = NULL;
    unsigned cArgvCompile = 0;
    const char *pszObjName = NULL;

    enum { kOC_Options, kOC_CppArgv, kOC_CcArgv, kOC_BothArgv } enmMode = kOC_Options;

    int i;

    /*
     * Parse the arguments.
     */
    for (i = 1; i < argc; i++)
    {
        if (!strcmp(argv[i], "--kObjCache-cpp"))
        {
            enmMode = kOC_CppArgv;
            if (!pszPreCompName)
            {
                if (++i >= argc)
                    return SyntaxError("--kObjCache-cpp requires an object filename!\n");
                pszPreCompName = argv[i];
            }
        }
        else if (!strcmp(argv[i], "--kObjCache-cc"))
        {
            enmMode = kOC_CcArgv;
            if (!pszObjName)
            {
                if (++i >= argc)
                    return SyntaxError("--kObjCache-cc requires an precompiler output filename!\n");
                pszObjName = argv[i];
            }
        }
        else if (!strcmp(argv[i], "--kObjCache-both"))
            enmMode = kOC_BothArgv;
        else if (!strcmp(argv[i], "--help"))
            return usage();
        else if (enmMode != kOC_Options)
        {
            if (enmMode == kOC_CppArgv || enmMode == kOC_BothArgv)
            {
                if (!(cArgvPreComp % 16))
                    papszArgvPreComp = xrealloc((void *)papszArgvPreComp, (cArgvPreComp + 17) * sizeof(papszArgvPreComp[0]));
                papszArgvPreComp[cArgvPreComp++] = argv[i];
                papszArgvPreComp[cArgvPreComp] = NULL;
            }
            if (enmMode == kOC_CcArgv || enmMode == kOC_BothArgv)
            {
                if (!(cArgvCompile % 16))
                    papszArgvCompile = xrealloc((void *)papszArgvCompile, (cArgvCompile + 17) * sizeof(papszArgvCompile[0]));
                papszArgvCompile[cArgvCompile++] = argv[i];
                papszArgvCompile[cArgvCompile] = NULL;
            }
        }
        else if (!strcmp(argv[i], "-f") || !strcmp(argv[i], "--file"))
        {
            if (i + 1 >= argc)
                return SyntaxError("%s requires a cache filename!\n", argv[i]);
            pszCacheFile = argv[++i];
        }
        else if (!strcmp(argv[i], "-r") || !strcmp(argv[i], "--redir-stdout"))
            fRedirStdOut = 1;
        else if (!strcmp(argv[i], "-v") || !strcmp(argv[i], "--verbose"))
            g_fVerbose = 1;
        else if (!strcmp(argv[i], "-q") || !strcmp(argv[i], "--quiet"))
            g_fVerbose = 0;
        else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "-?"))
            return usage();
        else if (!strcmp(argv[i], "-V") || !strcmp(argv[i], "--version"))
        {
            printf("kObjCache v0.0.0 ($Revision: 1002 $)\n");
            return 0;
        }
        else
            return SyntaxError("Doesn't grok '%s'!\n", argv[i]);
    }
    if (!pszCacheFile)
        return SyntaxError("No cache file name (-f)\n");
    if (!cArgvCompile)
        return SyntaxError("No compiler arguments (--kObjCache-cc)\n");
    if (!cArgvPreComp)
        return SyntaxError("No precompiler arguments (--kObjCache-cc)\n");

    /*
     * Create a cache entry from the cache file (if found).
     */
    pEntry = kObjCacheCreate(pszCacheFile);
    kObjCacheRead(pEntry);

    /*
     * Do the compiling.
     */
    kObjCachePreCompile(pEntry, papszArgvPreComp, cArgvPreComp, pszPreCompName, fRedirStdOut);
    kObjCacheCompileIfNeeded(pEntry, papszArgvCompile, cArgvCompile, pszObjName);

    /*
     * Write the cache file.
     */
    kObjCacheWrite(pEntry);
    //kObjCacheCleanup(pEntry);
    /* kObjCacheDestroy(pEntry); - don't bother */
    return 0;
}

