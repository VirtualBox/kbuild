/* $Id: kDep.c 865 2007-03-30 01:52:19Z knut.osmundsen@oracle.com $ */
/** @file
 *
 * kDep - Common Dependency Managemnt Code.
 *
 * Copyright (c) 2004-2007 knut st. osmundsen <bird-kBuild-spam@anduin.net>
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <limits.h>
#include <sys/stat.h>
#ifdef __WIN32__
# include <windows.h>
#endif
#if !defined(__WIN32__) && !defined(__OS2__)
# include <dirent.h>
#endif
#ifndef __WIN32__
# include <unistd.h>
# include <stdint.h>
#else
 typedef unsigned char  uint8_t;
 typedef unsigned short uint16_t;
 typedef unsigned int   uint32_t;
#endif

#include "kDep.h"

#ifdef NEED_ISBLANK
# define isblank(ch) ( (unsigned char)(ch) == ' ' || (unsigned char)(ch) == '\t' )
#endif

#define OFFSETOF(type, member)  ( (int)(void *)&( ((type *)(void *)0)->member) )


/*******************************************************************************
*   Global Variables                                                           *
*******************************************************************************/
/** List of dependencies. */
static PDEP g_pDeps = NULL;


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


#ifdef __WIN32__
/**
 * Corrects the case of a path and changes any path components containing
 * spaces with the short name (which can be longer).
 *
 * Expects a _fullpath!
 *
 * @param   pszPath     Pointer to the path, both input and output.
 *                      The buffer must be at least MAX_PATH in length.
 */
static void fixcase(char *pszPath)
{
#define my_assert(expr) \
    do { \
        if (!(expr)) { \
            printf("my_assert: %s, file %s, line %d\npszPath=%s\npsz=%s\n", \
                   #expr, __FILE__, __LINE__, pszPath, psz); \
            __debugbreak(); \
            exit(1); \
        } \
    } while (0)

    char *psz = pszPath;
    if (*psz == '/' || *psz == '\\')
    {
        if (psz[1] == '/' || psz[1] == '\\')
        {
            /* UNC */
            my_assert(psz[1] == '/' || psz[1] == '\\');
            my_assert(psz[2] != '/' && psz[2] != '\\');

            /* skip server name */
            psz += 2;
            while (*psz != '\\' && *psz != '/')
            {
                if (!*psz)
                    return;
                *psz++ = toupper(*psz);
            }

            /* skip the share name */
            psz++;
            my_assert(*psz != '/' && *psz != '\\');
            while (*psz != '\\' && *psz != '/')
            {
                if (!*psz)
                    return;
                *psz++ = toupper(*psz);
            }
            my_assert(*psz == '/' || *psz == '\\');
            psz++;
        }
        else
        {
            /* Unix spec */
            psz++;
        }
    }
    else
    {
        /* Drive letter */
        my_assert(psz[1] == ':');
        *psz = toupper(*psz);
        my_assert(psz[0] >= 'A' && psz[0] <= 'Z');
        my_assert(psz[2] == '/' || psz[2] == '\\');
        psz += 3;
    }

    /*
     * Pointing to the first char after the unc or drive specifier.
     */
    while (*psz)
    {
        WIN32_FIND_DATA FindFileData;
        HANDLE hDir;
        char chSaved0;
        char chSaved1;
        char *pszEnd;
        size_t cch;
        int iLongNameDiff;


        /* find the end of the component. */
        pszEnd = psz;
        while (*pszEnd && *pszEnd != '/' && *pszEnd != '\\')
            pszEnd++;
        cch = pszEnd - psz;

        /* replace the end with "?\0" */
        chSaved0 = pszEnd[0];
        chSaved1 = pszEnd[1];
        pszEnd[0] = '?';
        pszEnd[1] = '\0';

        /* find the right filename. */
        hDir = FindFirstFile(pszPath, &FindFileData);
        pszEnd[1] = chSaved1;
        if (!hDir)
        {
            pszEnd[0] = chSaved0;
            return;
        }
        pszEnd[0] = '\0';
        while (   (iLongNameDiff = _stricmp(FindFileData.cFileName, psz))
               && _stricmp(FindFileData.cAlternateFileName, psz))
        {
            if (!FindNextFile(hDir, &FindFileData))
            {
                pszEnd[0] = chSaved0;
                return;
            }
        }
        pszEnd[0] = chSaved0;
        if (iLongNameDiff || !FindFileData.cAlternateFileName[0] || !memchr(psz, ' ', cch))
            memcpy(psz, !iLongNameDiff ? FindFileData.cFileName : FindFileData.cAlternateFileName, cch);
        else
        {
            /* replace spacy name with the short name. */
            const size_t cchAlt = strlen(FindFileData.cAlternateFileName);
            const size_t cchDelta = cch - cchAlt;
            my_assert(cchAlt > 0);
            if (!cchDelta)
                memcpy(psz, FindFileData.cAlternateFileName, cch);
            else
            {   
                size_t cbLeft = strlen(pszEnd) + 1;
                if ((psz - pszPath) + cbLeft + cchAlt <= _MAX_PATH)
                {
                    memmove(psz + cchAlt, pszEnd, cbLeft);
                    pszEnd -= cchDelta;
                    memcpy(psz, FindFileData.cAlternateFileName, cchAlt);
                }
                else
                    fprintf(stderr, "kDep: case & space fixed filename is growing too long (%d bytes)! '%s'\n",
                            (psz - pszPath) + cbLeft + cchAlt, pszPath);
            }
        }
        my_assert(pszEnd[0] == chSaved0);

        /* advance to the next component */
        if (!chSaved0)
            return;
        psz = pszEnd + 1;
        my_assert(*psz != '/' && *psz != '\\');
    }
#undef my_assert
}

#elif defined(__OS2__)

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

#else

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
            *psz++;
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


#endif


/**
 * 'Optimizes' and corrects the dependencies.
 */
void depOptimize(int fFixCase)
{
    /*
     * Walk the list correct the names and re-insert them.
     */
    PDEP pDepOrg = g_pDeps;
    PDEP pDep = g_pDeps;
    g_pDeps = NULL;
    for (; pDep; pDep = pDep->pNext)
    {
#ifdef __WIN32__
        char        szFilename[_MAX_PATH + 1];
#else
        char        szFilename[PATH_MAX + 1];
#endif
        char       *pszFilename;
        struct stat s;

        /*
         * Skip some fictive names like <built-in> and <command line>.
         */
        if (    pDep->szFilename[0] == '<'
            &&  pDep->szFilename[pDep->cchFilename - 1] == '>')
            continue;
        pszFilename = pDep->szFilename;

#if !defined(__OS2__) && !defined(__WIN32__)
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
#ifdef __WIN32__
            if (_fullpath(szFilename, pszFilename, sizeof(szFilename)))
                ;
            else
#endif
                strcpy(szFilename, pszFilename);
            fixslash(szFilename);
            fixcase(szFilename);
            pszFilename = szFilename;
        }

        /*
         * Check that the file exists before we start depending on it.
         */
        if (stat(pszFilename, &s))
        {
            fprintf(stderr, "kDep: Skipping '%s' - %s!\n", szFilename, strerror(errno));
            continue;
        }

        /*
         * Insert the corrected dependency.
         */
        depAdd(pszFilename, strlen(pszFilename));
    }

#if 0 /* waste of time */
    /*
     * Free the old ones.
     */
    while (pDepOrg)
    {
        pDep = pDepOrg;
        pDepOrg = pDepOrg->pNext;
        free(pDep);
    }
#endif
}


/**
 * Prints the dependency chain.
 *
 * @returns Pointer to the allocated dependency.
 * @param   pOutput     Output stream.
 */
void depPrint(FILE *pOutput)
{
    PDEP pDep;
    for (pDep = g_pDeps; pDep; pDep = pDep->pNext)
        fprintf(pOutput, " \\\n\t%s", pDep->szFilename);
    fprintf(pOutput, "\n\n");
}


/**
 * Prints empty dependency stubs for all dependencies.
 */
void depPrintStubs(FILE *pOutput)
{
    PDEP pDep;
    for (pDep = g_pDeps; pDep; pDep = pDep->pNext)
        fprintf(pOutput, "%s:\n\n", pDep->szFilename);
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
static unsigned sdbm(const char *str)
{
    unsigned hash = 0;
    int c;

    while ((c = *(unsigned const char *)str++))
        hash = c + (hash << 6) + (hash << 16) - hash;

    return hash;
}


/**
 * Adds a dependency.
 *
 * @returns Pointer to the allocated dependency.
 * @param   pszFilename     The filename.
 * @param   cchFilename     The length of the filename.
 */
PDEP depAdd(const char *pszFilename, size_t cchFilename)
{
    unsigned uHash = sdbm(pszFilename);
    PDEP    pDep;
    PDEP    pDepPrev;

    /*
     * Check if we've already got this one.
     */
    pDepPrev = NULL;
    for (pDep = g_pDeps; pDep; pDepPrev = pDep, pDep = pDep->pNext)
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
        fprintf(stderr, "\nOut of memory! (requested %#x bytes)\n\n", sizeof(*pDep) + cchFilename);
        exit(1);
    }

    pDep->cchFilename = cchFilename;
    memcpy(pDep->szFilename, pszFilename, cchFilename + 1);
    pDep->uHash = uHash;

    if (pDepPrev)
    {
        pDep->pNext = pDepPrev->pNext;
        pDepPrev->pNext = pDep;
    }
    else
    {
        pDep->pNext = g_pDeps;
        g_pDeps = pDep;
    }
    return pDep;
}
