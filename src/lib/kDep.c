/* $Id: kDep.c 3315 2020-03-31 01:12:19Z knut.osmundsen@oracle.com $ */
/** @file
 * kDep - Common Dependency Managemnt Code.
 */

/*
 * Copyright (c) 2004-2013 knut st. osmundsen <bird-kBuild-spamx@anduin.net>
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


/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
#ifdef KMK /* For when it gets compiled and linked into kmk. */
# include "makeint.h"
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <limits.h>
#include <sys/stat.h>
#include "k/kDefs.h"
#include "k/kTypes.h"
#if K_OS == K_OS_WINDOWS
# define USE_WIN_MMAP
# include <io.h>
# include <Windows.h>
# include "nt_fullpath.h"
# include "nt/ntstat.h"
#else
# include <dirent.h>
# include <unistd.h>
# include <stdint.h>
#endif

#include "kDep.h"

#ifdef KWORKER
extern int kwFsPathExists(const char *pszPath);
#endif


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/* For the GNU/hurd weirdo. */
#if !defined(PATH_MAX) && !defined(_MAX_PATH)
# define PATH_MAX 4096
#endif


/**
 * Initializes the dep instance.
 *
 * @param   pThis       The dep instance to init.
 */
void depInit(PDEPGLOBALS pThis)
{
    pThis->pDeps = NULL;
}


/**
 * Cleans up the dep instance (frees resources).
 *
 * @param   pThis       The dep instance to cleanup.
 */
void depCleanup(PDEPGLOBALS pThis)
{
    PDEP pDep = pThis->pDeps;
    pThis->pDeps = NULL;
    while (pDep)
    {
        PDEP pFree = pDep;
        pDep = pDep->pNext;
        free(pFree);
    }
}


/**
 * Corrects all slashes to unix slashes.
 *
 * @returns pszFilename.
 * @param   pszFilename     The filename to correct.
 */
static char *fixslash(char *pszFilename)
{
    char *psz = pszFilename;
    while ((psz = strchr(psz, '\\')) != NULL)
        *psz++ = '/';
    return pszFilename;
}


#if K_OS == K_OS_OS2

/**
 * Corrects the case of a path.
 *
 * @param   pszPath     Pointer to the path, both input and output.
 *                      The buffer must be able to hold one more byte than the string length.
 */
static void fixcase(char *pszFilename)
{
    return;
}

#elif K_OS != K_OS_WINDOWS

/**
 * Corrects the case of a path.
 *
 * @param   pszPath     Pointer to the path, both input and output.
 */
static void fixcase(char *pszFilename)
{
    char *psz;

    /*
     * Skip the root.
     */
    psz = pszFilename;
    while (*psz == '/')
        psz++;

    /*
     * Iterate all the components.
     */
    while (*psz)
    {
        char  chSlash;
        struct stat s;
        char   *pszStart = psz;

        /*
         * Find the next slash (or end of string) and terminate the string there.
         */
        while (*psz != '/' && *psz)
            psz++;
        chSlash = *psz;
        *psz = '\0';

        /*
         * Does this part exist?
         * If not we'll enumerate the directory and search for an case-insensitive match.
         */
        if (stat(pszFilename, &s))
        {
            struct dirent  *pEntry;
            DIR            *pDir;
            if (pszStart == pszFilename)
                pDir = opendir(*pszFilename ? pszFilename : ".");
            else
            {
                pszStart[-1] = '\0';
                pDir = opendir(pszFilename);
                pszStart[-1] = '/';
            }
            if (!pDir)
            {
                *psz = chSlash;
                break; /* giving up, if we fail to open the directory. */
            }

            while ((pEntry = readdir(pDir)) != NULL)
            {
                if (!strcasecmp(pEntry->d_name, pszStart))
                {
                    strcpy(pszStart, pEntry->d_name);
                    break;
                }
            }
            closedir(pDir);
            if (!pEntry)
            {
                *psz = chSlash;
                break;  /* giving up if not found. */
            }
        }

        /* restore the slash and press on. */
        *psz = chSlash;
        while (*psz == '/')
            psz++;
    }

    return;
}

#endif /* !OS/2 && !Windows */


/**
 * 'Optimizes' and corrects the dependencies.
 */
void depOptimize(PDEPGLOBALS pThis, int fFixCase, int fQuiet, const char *pszIgnoredExt)
{
    /*
     * Walk the list correct the names and re-insert them.
     */
    size_t  cchIgnoredExt = pszIgnoredExt ? strlen(pszIgnoredExt) : 0;
    PDEP    pDepOrg = pThis->pDeps;
    PDEP    pDep = pThis->pDeps;
    pThis->pDeps = NULL;
    for (; pDep; pDep = pDep->pNext)
    {
#ifndef PATH_MAX
        char        szFilename[_MAX_PATH + 1];
#else
        char        szFilename[PATH_MAX + 1];
#endif
        char       *pszFilename;
#if !defined(KWORKER) && !defined(KMK)
        struct stat s;
#endif

        /*
         * Skip some fictive names like <built-in> and <command line>.
         */
        if (    pDep->szFilename[0] == '<'
            &&  pDep->szFilename[pDep->cchFilename - 1] == '>')
            continue;
        pszFilename = pDep->szFilename;

        /*
         * Skip pszIgnoredExt if given.
         */
        if (   pszIgnoredExt
            && pDep->cchFilename > cchIgnoredExt
            && memcmp(&pDep->szFilename[pDep->cchFilename - cchIgnoredExt], pszIgnoredExt, cchIgnoredExt) == 0)
            continue;

#if K_OS != K_OS_OS2 && K_OS != K_OS_WINDOWS
        /*
         * Skip any drive letters from compilers running in wine.
         */
        if (pszFilename[1] == ':')
            pszFilename += 2;
#endif

        /*
         * The microsoft compilers are notoriously screwing up the casing.
         * This will screw up kmk (/ GNU Make).
         */
        if (fFixCase)
        {
#if K_OS == K_OS_WINDOWS
            nt_fullpath_cached(pszFilename, szFilename, sizeof(szFilename));
            fixslash(szFilename);
#else
            strcpy(szFilename, pszFilename);
            fixslash(szFilename);
            fixcase(szFilename);
#endif
            pszFilename = szFilename;
        }

        /*
         * Check that the file exists before we start depending on it.
         */
        errno = 0;
#ifdef KWORKER
        if (!kwFsPathExists(pszFilename))
#elif defined(KMK)
        if (!file_exists_p(pszFilename))
#elif K_OS == K_OS_WINDOWS
        if (birdStatModTimeOnly(pszFilename, &s.st_mtim, 1 /*fFollowLink*/) != 0)
#else
        if (stat(pszFilename, &s) != 0)
#endif
        {
            if (   !fQuiet
                || errno != ENOENT
                || (   pszFilename[0] != '/'
                    && pszFilename[0] != '\\'
                    && (   !isalpha(pszFilename[0])
                        || pszFilename[1] != ':'
                        || (    pszFilename[2] != '/'
                            &&  pszFilename[2] != '\\')))
               )
                fprintf(stderr, "kDep: Skipping '%s' - %s!\n", pszFilename, strerror(errno));
            continue;
        }

        /*
         * Insert the corrected dependency.
         */
        depAdd(pThis, pszFilename, strlen(pszFilename));
    }

    /*
     * Free the old ones.
     */
    while (pDepOrg)
    {
        pDep = pDepOrg;
        pDepOrg = pDepOrg->pNext;
        free(pDep);
    }
}


/**
 * Write a filename that contains characters that needs escaping.
 *
 * @param   pOutput The output stream.
 * @param   pszFile The filename.
 * @param   cchFile The length of the filename.
 * @param   fDep    Whether this is for a dependency file or a target file.
 */
int depNeedsEscaping(const char *pszFile, size_t cchFile, int fDependency)
{
    return memchr(pszFile, ' ',  cchFile) != NULL
        || memchr(pszFile, '\t', cchFile) != NULL
        || memchr(pszFile, '#',  cchFile) != NULL
        || memchr(pszFile, '=',  cchFile) != NULL
        || memchr(pszFile, ';',  cchFile) != NULL
        || memchr(pszFile, '$',  cchFile) != NULL
        || memchr(pszFile, fDependency ? '|' : '%',  cchFile) != NULL;
}


/**
 * Write a filename that contains characters that needs escaping.
 *
 * @param   pOutput The output stream.
 * @param   pszFile The filename.
 * @param   cchFile The length of the filename.
 * @param   fDep    Whether this is for a dependency file or a target file.
 */
void depEscapedWrite(FILE *pOutput, const char *pszFile, size_t cchFile, int fDepenency)
{
    size_t cchWritten = 0;
    size_t off        = 0;
    while (off < cchFile)
    {
        char const ch = pszFile[off];
        switch (ch)
        {
            default:
                off++;
                break;

            /*
             * Escaped by slash, but any preceeding slashes must be escaped too.
             * A couple of characters are only escaped on one side of the ':'.
             */
            case '%': /* target side only */
            case '|': /* dependency side only */
                if (ch != (fDepenency ? '|' : '%'))
                {
                    off++;
                    break;
                }
                /* fall thru */
            case ' ':
            case '\t':
            case '#':
            case '=': /** @todo buggy GNU make handling */
            case ';': /** @todo buggy GNU make handling */
                if (cchWritten < off)
                    fwrite(&pszFile[cchWritten], off - cchWritten, 1, pOutput);
                if (off == 0 || pszFile[off - 1] != '\\')
                {
                    fputc('\\', pOutput);
                    cchWritten = off; /* We write the escaped character with the next bunch. */
                }
                else
                {
                    size_t cchSlashes = 1;
                    while (cchSlashes < off && pszFile[off - cchSlashes - 1] == '\\')
                        cchSlashes++;
                    fwrite(&pszFile[off - cchSlashes], cchSlashes, 1, pOutput);
                    cchWritten = off - 1; /* Write a preceeding slash and the escaped character with the next bunch. */
                }
                off += 1;
                break;

            /*
             * Escaped by doubling it.
             * Implemented by including in the pending writeout job as well as in the next one.
             */
            case '$':
                fwrite(&pszFile[cchWritten], off - cchWritten + 1, 1, pOutput);
                cchWritten = off++; /* write it again the next time */
                break;
        }
    }

    /* Remainder: */
    if (cchWritten < cchFile)
        fwrite(&pszFile[cchWritten], cchFile - cchWritten, 1, pOutput);
}


/**
 * Escapes all trailing trailing slashes in a filename that ends with such.
 */
static void depPrintTrailngSlashEscape(FILE *pOutput, const char *pszFilename, size_t cchFilename)
{
    size_t cchSlashes = 1;
    while (cchSlashes < cchFilename && pszFilename[cchFilename - cchSlashes - 1] == '\\')
        cchSlashes++;
    fwrite(&pszFilename[cchFilename - cchSlashes], cchSlashes, 1, pOutput);
}


/**
 * Prints the dependency chain.
 *
 * @param   pThis       The 'dep' instance.
 * @param   pOutput     Output stream.
 */
void depPrintChain(PDEPGLOBALS pThis, FILE *pOutput)
{
    static char const g_szEntryText[]     = " \\\n\t";
    static char const g_szTailText[]      = "\n\n";
    static char const g_szTailSlashText[] = " \\\n\n";
    PDEP              pDep;
    for (pDep = pThis->pDeps; pDep; pDep = pDep->pNext)
    {
        fwrite(g_szEntryText, sizeof(g_szEntryText) - 1, 1, pOutput);
        if (!pDep->fNeedsEscaping)
            fwrite(pDep->szFilename, pDep->cchFilename, 1, pOutput);
        else
            depEscapedWrite(pOutput, pDep->szFilename, pDep->cchFilename, 1 /*fDependency*/);
        if (pDep->fTrailingSlash)
        {   /* Escape only if more dependencies.  If last, we must add a line continuation or it won't work. */
            if (pDep->pNext)
                depPrintTrailngSlashEscape(pOutput, pDep->szFilename, pDep->cchFilename);
            else
            {
                fwrite(g_szTailSlashText, sizeof(g_szTailSlashText), 1, pOutput);
                return;
            }
        }
    }

    fwrite(g_szTailText, sizeof(g_szTailText) - 1, 1, pOutput);
}


/**
 * Prints the dependency chain with a preceeding target.
 *
 * @param   pThis           The 'dep' instance.
 * @param   pOutput         Output stream.
 * @param   pszTarget       The target filename.
 * @param   fEscapeTarget   Whether to consider escaping the target.
 */
void depPrintTargetWithDeps(PDEPGLOBALS pThis, FILE *pOutput, const char *pszTarget, int fEscapeTarget)
{
    static char const g_szSeparator[] = ":";
    size_t const cchTarget = strlen(pszTarget);
    if (!fEscapeTarget || !depNeedsEscaping(pszTarget, cchTarget, 0 /*fDependency*/))
        fwrite(pszTarget, cchTarget, 1, pOutput);
    else
        depEscapedWrite(pOutput, pszTarget, cchTarget, 0 /*fDependency*/);

    if (cchTarget == 0 || pszTarget[cchTarget - 1] != '\\')
    { /* likely */ }
    else
        depPrintTrailngSlashEscape(pOutput, pszTarget, cchTarget);
    fwrite(g_szSeparator, sizeof(g_szSeparator) - 1, 1, pOutput);

    depPrintChain(pThis, pOutput);
}


/**
 * Prints empty dependency stubs for all dependencies.
 *
 * @param   pThis       The 'dep' instance.
 * @param   pOutput     Output stream.
 */
void depPrintStubs(PDEPGLOBALS pThis, FILE *pOutput)
{
    static char g_szTailText[] = ":\n\n";
    PDEP pDep;
    for (pDep = pThis->pDeps; pDep; pDep = pDep->pNext)
    {
        if (!pDep->fNeedsEscaping && memchr(pDep->szFilename, '%', pDep->cchFilename) == 0)
            fwrite(pDep->szFilename, pDep->cchFilename, 1, pOutput);
        else
            depEscapedWrite(pOutput, pDep->szFilename, pDep->cchFilename, 0 /*fDependency*/);

        if (pDep->cchFilename == 0 || !pDep->fTrailingSlash)
        { /* likely */ }
        else
            depPrintTrailngSlashEscape(pOutput, pDep->szFilename, pDep->cchFilename);
        fwrite(g_szTailText, sizeof(g_szTailText) - 1, 1, pOutput);
    }
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
static unsigned sdbm(const char *str, size_t size)
{
    unsigned hash = 0;
    int c;

    while (size-- > 0 && (c = *(unsigned const char *)str++))
        hash = c + (hash << 6) + (hash << 16) - hash;

    return hash;
}


/**
 * Adds a dependency.
 *
 * @returns Pointer to the allocated dependency.
 * @param   pThis       The 'dep' instance.
 * @param   pszFilename     The filename. Does not need to be terminated.
 * @param   cchFilename     The length of the filename.
 */
PDEP depAdd(PDEPGLOBALS pThis, const char *pszFilename, size_t cchFilename)
{
    unsigned    uHash = sdbm(pszFilename, cchFilename);
    PDEP        pDep;
    PDEP        pDepPrev;

    /*
     * Check if we've already got this one.
     */
    pDepPrev = NULL;
    for (pDep = pThis->pDeps; pDep; pDepPrev = pDep, pDep = pDep->pNext)
        if (    pDep->uHash == uHash
            &&  pDep->cchFilename == cchFilename
            &&  !memcmp(pDep->szFilename, pszFilename, cchFilename))
            return pDep;

    /*
     * Add it.
     */
    pDep = (PDEP)malloc(sizeof(*pDep) + cchFilename);
    if (!pDep)
    {
        fprintf(stderr, "\nOut of memory! (requested %lx bytes)\n\n",
                (unsigned long)(sizeof(*pDep) + cchFilename));
        exit(1);
    }

    pDep->cchFilename = cchFilename;
    memcpy(pDep->szFilename, pszFilename, cchFilename);
    pDep->szFilename[cchFilename] = '\0';
    pDep->fNeedsEscaping = depNeedsEscaping(pszFilename, cchFilename, 1 /*fDependency*/);
    pDep->fTrailingSlash = cchFilename > 0 && pszFilename[cchFilename - 1] == '\\';
    pDep->uHash = uHash;

    if (pDepPrev)
    {
        pDep->pNext = pDepPrev->pNext;
        pDepPrev->pNext = pDep;
    }
    else
    {
        pDep->pNext = pThis->pDeps;
        pThis->pDeps = pDep;
    }
    return pDep;
}


/**
 * Performs a hexdump.
 */
void depHexDump(const KU8 *pb, size_t cb, size_t offBase)
{
    const unsigned      cchWidth = 16;
    size_t              off = 0;
    while (off < cb)
    {
        unsigned i;
        printf("%s%0*lx %04lx:", off ? "\n" : "", (int)sizeof(pb) * 2,
               (unsigned long)offBase + (unsigned long)off, (unsigned long)off);
        for (i = 0; i < cchWidth && off + i < cb ; i++)
            printf(off + i < cb ? !(i & 7) && i ? "-%02x" : " %02x" : "   ", pb[i]);

        while (i++ < cchWidth)
                printf("   ");
        printf(" ");

        for (i = 0; i < cchWidth && off + i < cb; i++)
        {
            const KU8 u8 = pb[i];
            printf("%c", u8 < 127 && u8 >= 32 ? u8 : '.');
        }
        off += cchWidth;
        pb  += cchWidth;
    }
    printf("\n");
}


/**
 * Reads the file specified by the pInput file stream into memory.
 *
 * @returns The address of the memory mapping on success. This must be
 *          freed by calling depFreeFileMemory.
 *
 * @param   pInput      The file stream to load or map into memory.
 * @param   pcbFile     Where to return the mapping (file) size.
 * @param   ppvOpaque   Opaque data when mapping, otherwise NULL.
 */
void *depReadFileIntoMemory(FILE *pInput, size_t *pcbFile, void **ppvOpaque)
{
    void       *pvFile;
    long        cbFile;

    /*
     * Figure out file size.
     */
#if defined(_MSC_VER)
    cbFile = _filelength(fileno(pInput));
    if (cbFile < 0)
#else
    if (    fseek(pInput, 0, SEEK_END) < 0
        ||  (cbFile = ftell(pInput)) < 0
        ||  fseek(pInput, 0, SEEK_SET))
#endif
    {
        fprintf(stderr, "kDep: error: Failed to determin file size.\n");
        return NULL;
    }
    if (pcbFile)
        *pcbFile = cbFile;

    /*
     * Try mmap first.
     */
#ifdef USE_WIN_MMAP
    {
        HANDLE hMapObj = CreateFileMapping((HANDLE)_get_osfhandle(fileno(pInput)),
                                           NULL, PAGE_READONLY, 0, cbFile, NULL);
        if (hMapObj != NULL)
        {
            pvFile = MapViewOfFile(hMapObj, FILE_MAP_READ, 0, 0, cbFile);
            if (pvFile)
            {
                *ppvOpaque = hMapObj;
                return pvFile;
            }
            fprintf(stderr, "kDep: warning: MapViewOfFile failed, %d.\n", GetLastError());
            CloseHandle(hMapObj);
        }
        else
            fprintf(stderr, "kDep: warning: CreateFileMapping failed, %d.\n", GetLastError());
    }

#endif

    /*
     * Allocate memory and read the file.
     */
    pvFile = malloc(cbFile + 1);
    if (pvFile)
    {
        if (fread(pvFile, cbFile, 1, pInput))
        {
            ((KU8 *)pvFile)[cbFile] = '\0';
            *ppvOpaque = NULL;
            return pvFile;
        }
        fprintf(stderr, "kDep: error: Failed to read %ld bytes.\n", cbFile);
        free(pvFile);
    }
    else
        fprintf(stderr, "kDep: error: Failed to allocate %ld bytes (file mapping).\n", cbFile);
    return NULL;
}


/**
 * Free resources allocated by depReadFileIntoMemory.
 *
 * @param   pvFile      The address of the memory mapping.
 * @param   pvOpaque    The opaque value returned together with the mapping.
 */
void depFreeFileMemory(void *pvFile, void *pvOpaque)
{
#if defined(USE_WIN_MMAP)
    if (pvOpaque)
    {
        UnmapViewOfFile(pvFile);
        CloseHandle(pvOpaque);
        return;
    }
#endif
    free(pvFile);
}

