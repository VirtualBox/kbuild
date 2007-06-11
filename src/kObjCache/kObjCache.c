/* $Id: kObjCache.c 1053 2007-06-11 09:32:07Z knut.osmundsen@oracle.com $ */
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
#if 0
# define ELECTRIC_HEAP
# include "../kmk/electric.h"
#endif
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <errno.h>
#include <assert.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <limits.h>
#include <ctype.h>
#ifndef PATH_MAX
# define PATH_MAX _MAX_PATH /* windows */
#endif
#if defined(__OS2__) || defined(__WIN__)
# include <process.h>
# include <io.h>
# ifdef __OS2__
#  include <unistd.h>
# endif
# if defined(_MSC_VER)
   typedef intptr_t pid_t;
# endif
#else
# include <unistd.h>
# include <sys/wait.h>
# ifndef O_BINARY
#  define O_BINARY 0
# endif
#endif
#if defined(__WIN__)
# include <Windows.h>
#endif

#include "crc32.h"
#include "md5.h"


/*******************************************************************************
*   Defined Constants And Macros                                               *
*******************************************************************************/
/** The max line length in a cache file. */
#define KOBJCACHE_MAX_LINE_LEN  16384
#if defined(__WIN__)
# define PATH_SLASH '\\'
#else
# define PATH_SLASH '/'
#endif
#if defined(__OS2__) || defined(__WIN__)
# define IS_SLASH(ch)       ((ch) == '/' || (ch) == '\\')
# define IS_SLASH_DRV(ch)   ((ch) == '/' || (ch) == '\\' || (ch) == ':')
#else
# define IS_SLASH(ch)       ((ch) == '/')
# define IS_SLASH_DRV(ch)   ((ch) == '/')
#endif

#ifndef STDIN_FILENO
# define STDIN_FILENO 0
#endif
#ifndef STDOUT_FILENO
# define STDOUT_FILENO 1
#endif
#ifndef STDERR_FILENO
# define STDERR_FILENO 2
#endif


/*******************************************************************************
*   Global Variables                                                           *
*******************************************************************************/
/** Whether verbose output is enabled. */
static unsigned g_cVerbosityLevel = 0;
/** What to prefix the errors with. */
static char g_szErrorPrefix[128];

/** Read buffer shared by the cache components. */
static char g_szLine[KOBJCACHE_MAX_LINE_LEN + 16];


/*******************************************************************************
*   Internal Functions                                                         *
*******************************************************************************/
static char *MakePathFromDirAndFile(const char *pszName, const char *pszDir);
static char *CalcRelativeName(const char *pszPath, const char *pszDir);
static FILE *FOpenFileInDir(const char *pszName, const char *pszDir, const char *pszMode);
static int UnlinkFileInDir(const char *pszName, const char *pszDir);
static int RenameFileInDir(const char *pszOldName, const char *pszNewName, const char *pszDir);
static int DoesFileInDirExist(const char *pszName, const char *pszDir);
static void *ReadFileInDir(const char *pszName, const char *pszDir, size_t *pcbFile);


void FatalMsg(const char *pszFormat, ...)
{
    va_list va;

    if (g_szErrorPrefix[0])
        fprintf(stderr, "%s - fatal error: ", g_szErrorPrefix);
    else
        fprintf(stderr, "fatal error: ");

    va_start(va, pszFormat);
    vfprintf(stderr, pszFormat, va);
    va_end(va);
}


void FatalDie(const char *pszFormat, ...)
{
    va_list va;

    if (g_szErrorPrefix[0])
        fprintf(stderr, "%s - fatal error: ", g_szErrorPrefix);
    else
        fprintf(stderr, "fatal error: ");

    va_start(va, pszFormat);
    vfprintf(stderr, pszFormat, va);
    va_end(va);

    exit(1);
}


static void ErrorMsg(const char *pszFormat, ...)
{
    va_list va;

    if (g_szErrorPrefix[0])
        fprintf(stderr, "%s - error: ", g_szErrorPrefix);
    else
        fprintf(stderr, "error: ");

    va_start(va, pszFormat);
    vfprintf(stderr, pszFormat, va);
    va_end(va);
}


static void InfoMsg(unsigned uLevel, const char *pszFormat, ...)
{
    if (uLevel <= g_cVerbosityLevel)
    {
        va_list va;

        if (g_szErrorPrefix[0])
            fprintf(stderr, "%s - info: ", g_szErrorPrefix);
        else
            fprintf(stderr, "info: ");

        va_start(va, pszFormat);
        vfprintf(stderr, pszFormat, va);
        va_end(va);
    }
}


static void SetErrorPrefix(const char *pszPrefix, ...)
{
    int cch;
    va_list va;

    va_start(va, pszPrefix);
#if defined(_MSC_VER) || defined(__sun__)
    cch = vsprintf(g_szErrorPrefix, pszPrefix, va);
    if (cch >= sizeof(g_szErrorPrefix))
        FatalDie("Buffer overflow setting error prefix!\n");
#else
    vsnprintf(g_szErrorPrefix, sizeof(g_szErrorPrefix), pszPrefix, va);
#endif
    va_end(va);
    (void)cch;
}

#ifndef ELECTRIC_HEAP
void *xmalloc(size_t cb)
{
    void *pv = malloc(cb);
    if (!pv)
        FatalDie("out of memory (%d)\n", (int)cb);
    return pv;
}


void *xrealloc(void *pvOld, size_t cb)
{
    void *pv = realloc(pvOld, cb);
    if (!pv)
        FatalDie("out of memory (%d)\n", (int)cb);
    return pv;
}


char *xstrdup(const char *pszIn)
{
    char *psz = strdup(pszIn);
    if (!psz)
        FatalDie("out of memory (%d)\n", (int)strlen(pszIn));
    return psz;
}
#endif


void *xmallocz(size_t cb)
{
    void *pv = xmalloc(cb);
    memset(pv, 0, cb);
    return pv;
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
           &&   !IS_SLASH_DRV(pszFilename[-1]))
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
    if (cchDir > 0 && !IS_SLASH_DRV(pszDir[cchDir - 1]))
        pszBuf[cchDir++] = PATH_SLASH;
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
        while (IS_SLASH_DRV(*pszRet))
            pszRet++;
        pszRet = xstrdup(pszRet);
        free(pszAbsPath);
        return pszRet;
    }

    /*
     * Damn, it's gonna be complicated. Deal with that later.
     */
    FatalDie("complicated relative path stuff isn't implemented yet. sorry.\n");
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
 * Utility function that combines a filename and directory and passes it onto open.
 *
 * @returns open return value.
 * @param   pszName     The file name.
 * @param   pszDir      The directory path.
 * @param   fFlags      The open flags.
 * @param   fCreateMode The file creation mode.
 */
static int OpenFileInDir(const char *pszName, const char *pszDir, int fFlags, int fCreateMode)
{
    char *pszPath = MakePathFromDirAndFile(pszName, pszDir);
    int fd = open(pszPath, fFlags, fCreateMode);
    free(pszPath);
    return fd;
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
 * @returns whatever rename returns.
 * @param   pszOldName  The new file name.
 * @param   pszNewName  The old file name.
 * @param   pszDir      The directory path.
 */
static int RenameFileInDir(const char *pszOldName, const char *pszNewName, const char *pszDir)
{
    char *pszOldPath = MakePathFromDirAndFile(pszOldName, pszDir);
    char *pszNewPath = MakePathFromDirAndFile(pszNewName, pszDir);
    int rc = rename(pszOldPath, pszNewPath);
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
    int fd = open(pszPath, O_RDONLY | O_BINARY);
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


/**
 * Creates a directory including all necessary parent directories.
 *
 * @returns 0 on success, -1 + errno on failure.
 * @param   pszDir      The directory.
 */
static int MakePath(const char *pszPath)
{
    int iErr = 0;
    char *pszAbsPath = AbsPath(pszPath);
    char *psz = pszAbsPath;

    /* Skip to the root slash (PC). */
    while (!IS_SLASH(*psz) && *psz)
        psz++;
/** @todo UNC */
    for (;;)
    {
        char chSaved;

        /* skip slashes */
        while (IS_SLASH(*psz))
            psz++;
        if (!*psz)
            break;

        /* find the next slash or end and terminate the string. */
        while (!IS_SLASH(*psz) && *psz)
            psz++;
        chSaved = *psz;
        *psz = '\0';

        /* try create the directory, ignore failure because the directory already exists. */
        errno = 0;
#ifdef _MSC_VER
        if (    _mkdir(pszAbsPath)
            &&  errno != EEXIST)
#else
        if (    mkdir(pszAbsPath, 0777)
            &&  errno != EEXIST)
#endif
        {
            iErr = errno;
            break;
        }

        /* restore the slash/terminator */
        *psz = chSaved;
    }

    free(pszAbsPath);
    return iErr ? -1 : 0;
}


/**
 * Adds the arguments found in the pszCmdLine string to argument vector.
 *
 * The parsing of the pszCmdLine string isn't very sophisticated, no
 * escaping or quotes.
 *
 * @param   pcArgs      Pointer to the argument counter.
 * @param   ppapszArgs  Pointer to the argument vector pointer.
 * @param   pszCmdLine  The command line to parse and append.
 * @param   pszWedgeArg Argument to put infront of anything found in pszCmdLine.
 */
static void AppendArgs(int *pcArgs, char ***ppapszArgs, const char *pszCmdLine, const char *pszWedgeArg)
{
    int i;
    int cExtraArgs;
    const char *psz;
    char **papszArgs;

    /*
     * Count the new arguments.
     */
    cExtraArgs = 0;
    psz = pszCmdLine;
    while (*psz)
    {
        while (isspace(*psz))
            psz++;
        if (!psz)
            break;
        cExtraArgs++;
        while (!isspace(*psz) && *psz)
            psz++;
    }
    if (!cExtraArgs)
        return;

    /*
     * Allocate a new vector that can hold the arguments.
     * (Reallocating might not work since the argv might not be allocated
     *  from the heap but off the stack or somewhere... )
     */
    i = *pcArgs;
    *pcArgs = i + cExtraArgs + !!pszWedgeArg;
    papszArgs = xmalloc((*pcArgs + 1) * sizeof(char *));
    *ppapszArgs = memcpy(papszArgs, *ppapszArgs, i * sizeof(char *));

    if (pszWedgeArg)
        papszArgs[i++] = xstrdup(pszWedgeArg);

    psz = pszCmdLine;
    while (*psz)
    {
        size_t cch;
        const char *pszEnd;
        while (isspace(*psz))
            psz++;
        if (!psz)
            break;
        pszEnd = psz;
        while (!isspace(*pszEnd) && *pszEnd)
            pszEnd++;

        cch = pszEnd - psz;
        papszArgs[i] = xmalloc(cch + 1);
        memcpy(papszArgs[i], psz, cch);
        papszArgs[i][cch] = '\0';

        i++;
        psz = pszEnd;
    }

    papszArgs[i] = NULL;
}





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
    /** Valid or not. */
    unsigned fUsed;
} KOCSUM;
/** Pointer to a KOCSUM. */
typedef KOCSUM *PKOCSUM;
/** Pointer to a const KOCSUM. */
typedef const KOCSUM *PCKOCSUM;


/**
 * Temporary context record used when calculating
 * the checksum of some data.
 */
typedef struct KOCSUMCTX
{
    /** The MD5 context. */
    struct MD5Context MD5Ctx;
} KOCSUMCTX;
/** Pointer to a check context record. */
typedef KOCSUMCTX *PKOCSUMCTX;



/**
 * Initializes a checksum object with an associated context.
 *
 * @param   pSum    The checksum object.
 * @param   pCtx    The checksum context.
 */
static void kOCSumInitWithCtx(PKOCSUM pSum, PKOCSUMCTX pCtx)
{
    memset(pSum, 0, sizeof(*pSum));
    MD5Init(&pCtx->MD5Ctx);
}


/**
 * Updates the checksum calculation.
 *
 * @param   pSum    The checksum.
 * @param   pCtx    The checksum calcuation context.
 * @param   pvBuf   The input data to checksum.
 * @param   cbBuf   The size of the input data.
 */
static void kOCSumUpdate(PKOCSUM pSum, PKOCSUMCTX pCtx, const void *pvBuf, size_t cbBuf)
{
    /*
     * Take in relativly small chunks to try keep it in the cache.
     */
    const unsigned char *pb = (const unsigned char *)pvBuf;
    while (cbBuf > 0)
    {
        size_t cb = cbBuf >= 128*1024 ? 128*1024 : cbBuf;
        pSum->crc32 = crc32(pSum->crc32, pb, cb);
        MD5Update(&pCtx->MD5Ctx, pb, cb);
        cbBuf -= cb;
    }
}


/**
 * Finalizes a checksum calculation.
 *
 * @param   pSum    The checksum.
 * @param   pCtx    The checksum calcuation context.
 */
static void kOCSumFinalize(PKOCSUM pSum, PKOCSUMCTX pCtx)
{
    MD5Final(&pSum->md5[0], &pCtx->MD5Ctx);
    pSum->fUsed = 1;
}


/**
 * Init a check sum chain head.
 *
 * @param   pSumHead    The checksum head to init.
 */
static void kOCSumInit(PKOCSUM pSumHead)
{
    memset(pSumHead, 0, sizeof(*pSumHead));
}


/**
 * Parses the given string into a checksum head object.
 *
 * @returns 0 on success, -1 on format error.
 * @param   pSumHead    The checksum head to init.
 * @param   pszVal      The string to initialized it from.
 */
static int kOCSumInitFromString(PKOCSUM pSumHead, const char *pszVal)
{
    unsigned i;
    char *pszNext;
    char *pszMD5;

    memset(pSumHead, 0, sizeof(*pSumHead));

    pszMD5 = strchr(pszVal, ':');
    if (pszMD5 == NULL)
        return -1;
    *pszMD5++ = '\0';

    /* crc32 */
    pSumHead->crc32 = (uint32_t)strtoul(pszVal, &pszNext, 16);
    if (pszNext && *pszNext)
        return -1;

    /* md5 */
    for (i = 0; i < sizeof(pSumHead->md5) * 2; i++)
    {
        unsigned char ch = pszMD5[i];
        int x;
        if ((unsigned char)(ch - '0') <= 9)
            x = ch - '0';
        else if ((unsigned char)(ch - 'a') <= 5)
            x = ch - 'a' + 10;
        else if ((unsigned char)(ch - 'A') <= 5)
            x = ch - 'A' + 10;
        else
            return -1;
        if (!(i & 1))
            pSumHead->md5[i >> 1] = x << 4;
        else
            pSumHead->md5[i >> 1] |= x;
    }

    pSumHead->fUsed = 1;
    return 0;
}


/**
 * Delete a check sum chain.
 *
 * @param   pSumHead    The head of the checksum chain.
 */
static void kOCSumDeleteChain(PKOCSUM pSumHead)
{
    void *pv;
    while ((pv = pSumHead->pNext))
    {
        pSumHead = pSumHead->pNext;
        free(pv);
    }
    memset(pSumHead, 0, sizeof(*pSumHead));
}


/**
 * Insert a check sum into the chain.
 *
 * @param   pSumHead    The head of the checksum list.
 * @param   pSumAdd     The checksum to add (duplicate).
 */
static void kOCSumAdd(PKOCSUM pSumHead, PCKOCSUM pSumAdd)
{
    if (pSumHead->fUsed)
    {
        PKOCSUM pNew = xmalloc(sizeof(*pNew));
        *pNew = *pSumAdd;
        pNew->pNext = pSumHead->pNext;
        pNew->fUsed = 1;
        pSumHead->pNext = pNew;
    }
    else
    {
        *pSumHead = *pSumAdd;
        pSumHead->pNext = NULL;
        pSumHead->fUsed = 1;
    }
}


/**
 * Inserts an entrie chain into the given check sum chain.
 *
 * @param   pSumHead    The head of the checksum list.
 * @param   pSumHeadAdd The head of the checksum list to be added.
 */
static void kOCSumAddChain(PKOCSUM pSumHead, PCKOCSUM pSumHeadAdd)
{
    while (pSumHeadAdd)
    {
        kOCSumAdd(pSumHead, pSumHeadAdd);
        pSumHeadAdd = pSumHeadAdd->pNext;
    }
}



/**
 * Prints the checksum to the specified stream.
 *
 * @param   pSum    The checksum.
 * @param   pFile   The output file stream
 */
static void kOCSumFPrintf(PCKOCSUM pSum, FILE *pFile)
{
    fprintf(pFile, "%#x:%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x\n",
            pSum->crc32,
            pSum->md5[0], pSum->md5[1], pSum->md5[2], pSum->md5[3],
            pSum->md5[4], pSum->md5[5], pSum->md5[6], pSum->md5[7],
            pSum->md5[8], pSum->md5[9], pSum->md5[10], pSum->md5[11],
            pSum->md5[12], pSum->md5[13], pSum->md5[14], pSum->md5[15]);
}


/**
 * Displays the checksum (not chain!) using the InfoMsg() method.
 *
 * @param   pSum    The checksum.
 * @param   uLevel  The info message level.
 * @param   pszMsg  Message to prefix the info message with.
 */
static void kOCSumInfo(PCKOCSUM pSum, unsigned uLevel, const char *pszMsg)
{
    InfoMsg(uLevel,
            "%s: crc32=%#010x md5=%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x\n",
            pszMsg,
            pSum->crc32,
            pSum->md5[0], pSum->md5[1], pSum->md5[2], pSum->md5[3],
            pSum->md5[4], pSum->md5[5], pSum->md5[6], pSum->md5[7],
            pSum->md5[8], pSum->md5[9], pSum->md5[10], pSum->md5[11],
            pSum->md5[12], pSum->md5[13], pSum->md5[14], pSum->md5[15]);
}


/**
 * Compares two check sum entries.
 *
 * @returns 1 if equal, 0 if not equal.
 *
 * @param pSum1     The first checksum.
 * @param pSum2     The second checksum.
 */
static int kOCSumIsEqual(PCKOCSUM pSum1, PCKOCSUM pSum2)
{
    if (pSum1 == pSum2)
        return 1;
    if (!pSum1 || !pSum2)
        return 0;
    if (pSum1->crc32 != pSum2->crc32)
        return 0;
    if (memcmp(&pSum1->md5[0], &pSum2->md5[0], sizeof(pSum1->md5)))
        return 0;
    return 1;
}


/**
 * Checks if the specified checksum equals one of the
 * checksums in the chain.
 *
 * @returns 1 if equals one of them, 0 if not.
 *
 * @param pSumHead  The checksum chain too look in.
 * @param pSum      The checksum to look for.
 * @todo ugly name. fix.
 */
static int kOCSumHasEqualInChain(PCKOCSUM pSumHead, PCKOCSUM pSum)
{
    for (; pSumHead; pSumHead = pSumHead->pNext)
    {
        if (pSumHead == pSum)
            return 1;
        if (pSumHead->crc32 != pSum->crc32)
            continue;
        if (memcmp(&pSumHead->md5[0], &pSum->md5[0], sizeof(pSumHead->md5)))
            continue;
        return 1;
    }
    return 0;
}


/**
 * Checks if the checksum (chain) empty.
 *
 * @returns 1 if empty, 0 if it there is one or more checksums.
 * @param   pSum    The checksum to test.
 */
static int kOCSumIsEmpty(PCKOCSUM pSum)
{
    return !pSum->fUsed;
}






/**
 * The representation of a cache entry.
 */
typedef struct KOCENTRY
{
    /** The name of the cache entry. */
    const char *pszName;
    /** The dir that all other names are relative to. */
    char *pszDir;
    /** The absolute path. */
    char *pszAbsPath;
    /** Set if the object needs to be (re)compiled. */
    unsigned fNeedCompiling;
    /** Whether the precompiler runs in piped mode. If clear it's file
     * mode (it could be redirected stdout, but that's essentially the
     * same from our point of view). */
    unsigned fPipedPreComp;
    /** Whether the compiler runs in piped mode (precompiler output on stdin). */
    unsigned fPipedCompile;
    /** Cache entry key that's used for some quick digest validation. */
    uint32_t uKey;

    /** The file data. */
    struct KOCENTRYDATA
    {
        /** The name of file containing the precompiler output. */
        char *pszCppName;
        /** Pointer to the precompiler output. */
        char *pszCppMapping;
        /** The size of the precompiler output. 0 if not determined. */
        size_t cbCpp;
        /** The precompiler output checksums that will produce the cached object. */
        KOCSUM SumHead;
        /** The object filename (relative to the cache file). */
        char *pszObjName;
        /** The compile argument vector used to build the object. */
        char **papszArgvCompile;
        /** The size of the compile  */
        unsigned cArgvCompile;
        /** The checksum of the compiler argument vector. */
        KOCSUM SumCompArgv;
        /** The target os/arch identifier. */
        char *pszTarget;
    }
    /** The old data.*/
            Old,
    /** The new data. */
            New;
} KOCENTRY;
/** Pointer to a KOCENTRY. */
typedef KOCENTRY *PKOCENTRY;
/** Pointer to a const KOCENTRY. */
typedef const KOCENTRY *PCKOCENTRY;


/**
 * Creates a cache entry for the given cache file name.
 *
 * @returns Pointer to a cache entry.
 * @param   pszFilename     The cache file name.
 */
static PKOCENTRY kOCEntryCreate(const char *pszFilename)
{
    PKOCENTRY pEntry;
    size_t off;

    /*
     * Allocate an empty entry.
     */
    pEntry = xmallocz(sizeof(*pEntry));

    kOCSumInit(&pEntry->New.SumHead);
    kOCSumInit(&pEntry->Old.SumHead);

    kOCSumInit(&pEntry->New.SumCompArgv);
    kOCSumInit(&pEntry->Old.SumCompArgv);

    /*
     * Setup the directory and cache file name.
     */
    pEntry->pszAbsPath = AbsPath(pszFilename);
    pEntry->pszName = FindFilenameInPath(pEntry->pszAbsPath);
    off = pEntry->pszName - pEntry->pszAbsPath;
    if (!off)
        FatalDie("Failed to find abs path for '%s'!\n", pszFilename);
    pEntry->pszDir = xmalloc(off);
    memcpy(pEntry->pszDir, pEntry->pszAbsPath, off - 1);
    pEntry->pszDir[off - 1] = '\0';

    return pEntry;
}


/**
 * Destroys the cache entry freeing up all it's resources.
 *
 * @param   pEntry      The entry to free.
 */
static void kOCEntryDestroy(PKOCENTRY pEntry)
{
    free(pEntry->pszDir);
    free(pEntry->pszAbsPath);

    kOCSumDeleteChain(&pEntry->New.SumHead);
    kOCSumDeleteChain(&pEntry->Old.SumHead);

    kOCSumDeleteChain(&pEntry->New.SumCompArgv);
    kOCSumDeleteChain(&pEntry->Old.SumCompArgv);

    free(pEntry->New.pszCppName);
    free(pEntry->Old.pszCppName);

    free(pEntry->New.pszCppMapping);
    free(pEntry->Old.pszCppMapping);

    free(pEntry->New.pszObjName);
    free(pEntry->Old.pszObjName);

    free(pEntry->New.pszTarget);
    free(pEntry->Old.pszTarget);

    while (pEntry->New.cArgvCompile > 0)
        free(pEntry->New.papszArgvCompile[--pEntry->New.cArgvCompile]);
    while (pEntry->Old.cArgvCompile > 0)
        free(pEntry->Old.papszArgvCompile[--pEntry->Old.cArgvCompile]);

    free(pEntry->New.papszArgvCompile);
    free(pEntry->Old.papszArgvCompile);

    free(pEntry);
}


/**
 * Calculates the checksum of an compiler argument vector.
 *
 * @param   pEntry          The cache entry.
 * @param   papszArgv       The argument vector.
 * @param   cArgc           The number of entries in the vector.
 * @param   pszIgnorePath   Path to ignore when encountered at the end of arguments.
 *                          (Not quite safe for simple file names, but what the heck.)
 * @param   pSum            Where to store the check sum.
 */
static void kOCEntryCalcArgvSum(PKOCENTRY pEntry, const char * const *papszArgv, unsigned cArgc,
                                const char *pszIgnorePath, PKOCSUM pSum)
{
    size_t cchIgnorePath = strlen(pszIgnorePath);
    KOCSUMCTX Ctx;
    unsigned i;

    kOCSumInitWithCtx(pSum, &Ctx);
    for (i = 0; i < cArgc; i++)
    {
        size_t cch = strlen(papszArgv[i]);
        if (    cch < cchIgnorePath
            ||  !ArePathsIdentical(papszArgv[i] + cch - cchIgnorePath, pszIgnorePath, cch))
            kOCSumUpdate(pSum, &Ctx, papszArgv[i], cch + 1);
    }
    kOCSumFinalize(pSum, &Ctx);
}


/**
 * Reads and parses the cache file.
 *
 * @param   pEntry      The entry to read it into.
 */
static void kOCEntryRead(PKOCENTRY pEntry)
{
    FILE *pFile;
    pFile = FOpenFileInDir(pEntry->pszName, pEntry->pszDir, "rb");
    if (pFile)
    {
        InfoMsg(4, "reading cache entry...\n");

        /*
         * Check the magic.
         */
        if (    !fgets(g_szLine, sizeof(g_szLine), pFile)
            ||  strcmp(g_szLine, "magic=kObjCacheEntry-v0.1.0\n"))
        {
            InfoMsg(2, "bad cache file (magic)\n");
            pEntry->fNeedCompiling = 1;
        }
        else
        {
            /*
             * Parse the rest of the file (relaxed order).
             */
            unsigned i;
            int fBad = 0;
            int fBadBeforeMissing = 1;
            while (fgets(g_szLine, sizeof(g_szLine), pFile))
            {
                char *pszNl;
                char *pszVal;

                /* Split the line and drop the trailing newline. */
                pszVal = strchr(g_szLine, '=');
                if ((fBad = pszVal == NULL))
                    break;
                *pszVal++ = '\0';

                pszNl = strchr(pszVal, '\n');
                if (pszNl)
                    *pszNl = '\0';

                /* string case on variable name */
                if (!strcmp(g_szLine, "obj"))
                {
                    if ((fBad = pEntry->Old.pszObjName != NULL))
                        break;
                    pEntry->Old.pszObjName = xstrdup(pszVal);
                }
                else if (!strcmp(g_szLine, "cpp"))
                {
                    if ((fBad = pEntry->Old.pszCppName != NULL))
                        break;
                    pEntry->Old.pszCppName = xstrdup(pszVal);
                }
                else if (!strcmp(g_szLine, "cpp-size"))
                {
                    char *pszNext;
                    if ((fBad = pEntry->Old.cbCpp != 0))
                        break;
                    pEntry->Old.cbCpp = strtoul(pszVal, &pszNext, 0);
                    if ((fBad = pszNext && *pszNext))
                        break;
                }
                else if (!strcmp(g_szLine, "cpp-sum"))
                {
                    KOCSUM Sum;
                    if ((fBad = kOCSumInitFromString(&Sum, pszVal)))
                        break;
                    kOCSumAdd(&pEntry->Old.SumHead, &Sum);
                }
                else if (!strcmp(g_szLine, "cc-argc"))
                {
                    if ((fBad = pEntry->Old.papszArgvCompile != NULL))
                        break;
                    pEntry->Old.cArgvCompile = atoi(pszVal); /* if wrong, we'll fail below. */
                    pEntry->Old.papszArgvCompile = xmallocz((pEntry->Old.cArgvCompile + 1) * sizeof(pEntry->Old.papszArgvCompile[0]));
                }
                else if (!strncmp(g_szLine, "cc-argv-#", sizeof("cc-argv-#") - 1))
                {
                    char *pszNext;
                    unsigned i = strtoul(&g_szLine[sizeof("cc-argv-#") - 1], &pszNext, 0);
                    if ((fBad = i >= pEntry->Old.cArgvCompile || pEntry->Old.papszArgvCompile[i] || (pszNext && *pszNext)))
                        break;
                    pEntry->Old.papszArgvCompile[i] = xstrdup(pszVal);
                }
                else if (!strcmp(g_szLine, "cc-argv-sum"))
                {
                    if ((fBad = !kOCSumIsEmpty(&pEntry->Old.SumCompArgv)))
                        break;
                    if ((fBad = kOCSumInitFromString(&pEntry->Old.SumCompArgv, pszVal)))
                        break;
                }
                else if (!strcmp(g_szLine, "target"))
                {
                    if ((fBad = pEntry->Old.pszTarget != NULL))
                        break;
                    pEntry->Old.pszTarget = xstrdup(pszVal);
                }
                else if (!strcmp(g_szLine, "key"))
                {
                    char *pszNext;
                    if ((fBad = pEntry->uKey != 0))
                        break;
                    pEntry->uKey = strtoul(pszVal, &pszNext, 0);
                    if ((fBad = pszNext && *pszNext))
                        break;
                }
                else if (!strcmp(g_szLine, "the-end"))
                {
                    fBadBeforeMissing = fBad = strcmp(pszVal, "fine");
                    break;
                }
                else
                {
                    fBad = 1;
                    break;
                }
            } /* parse loop */

            /*
             * Did we find everything and does it add up correctly?
             */
            if (!fBad && fBadBeforeMissing)
            {
                InfoMsg(2, "bad cache file (no end)\n");
                fBad = 1;
            }
            else
            {
                fBadBeforeMissing = fBad;
                if (    !fBad
                    &&  (   !pEntry->Old.papszArgvCompile
                         || !pEntry->Old.pszObjName
                         || !pEntry->Old.pszCppName
                         || kOCSumIsEmpty(&pEntry->Old.SumHead)))
                    fBad = 1;
                if (!fBad)
                    for (i = 0; i < pEntry->Old.cArgvCompile; i++)
                        if ((fBad = !pEntry->Old.papszArgvCompile[i]))
                            break;
                if (!fBad)
                {
                    KOCSUM Sum;
                    kOCEntryCalcArgvSum(pEntry, (const char * const *)pEntry->Old.papszArgvCompile,
                                        pEntry->Old.cArgvCompile, pEntry->Old.pszObjName, &Sum);
                    fBad = !kOCSumIsEqual(&pEntry->Old.SumCompArgv, &Sum);
                }
                if (fBad)
                    InfoMsg(2, "bad cache file (%s)\n", fBadBeforeMissing ? g_szLine : "missing stuff");
                else if (ferror(pFile))
                {
                    InfoMsg(2, "cache file read error\n");
                    fBad = 1;
                }

                /*
                 * Verify the existance of the object file.
                 */
                if (!fBad)
                {
                    struct stat st;
                    char *pszPath = MakePathFromDirAndFile(pEntry->Old.pszObjName, pEntry->pszDir);
                    if (stat(pszPath, &st) != 0)
                    {
                        InfoMsg(2, "failed to stat object file: %s\n", strerror(errno));
                        fBad = 1;
                    }
                    else
                    {
                        /** @todo verify size and the timestamp. */
                    }
                }
            }
            pEntry->fNeedCompiling = fBad;
        }
        fclose(pFile);
    }
    else
    {
        InfoMsg(2, "no cache file\n");
        pEntry->fNeedCompiling = 1;
    }
}


/**
 * Writes the cache file.
 *
 * @param   pEntry      The entry to write.
 */
static void kOCEntryWrite(PKOCENTRY pEntry)
{
    FILE *pFile;
    PCKOCSUM pSum;
    unsigned i;

    InfoMsg(4, "writing cache entry '%s'...\n", pEntry->pszName);
    pFile = FOpenFileInDir(pEntry->pszName, pEntry->pszDir, "wb");
    if (!pFile)
        FatalDie("Failed to open '%s' in '%s': %s\n",
                 pEntry->pszName, pEntry->pszDir, strerror(errno));

#define CHECK_LEN(expr) \
        do { int cch = expr; if (cch >= KOBJCACHE_MAX_LINE_LEN) FatalDie("Line too long: %d (max %d)\nexpr: %s\n", cch, KOBJCACHE_MAX_LINE_LEN, #expr); } while (0)

    fprintf(pFile, "magic=kObjCacheEntry-v0.1.0\n");
    CHECK_LEN(fprintf(pFile, "target=%s\n", pEntry->New.pszTarget ? pEntry->New.pszTarget : pEntry->Old.pszTarget));
    CHECK_LEN(fprintf(pFile, "key=%u\n", (unsigned long)pEntry->uKey));
    CHECK_LEN(fprintf(pFile, "obj=%s\n", pEntry->New.pszObjName ? pEntry->New.pszObjName : pEntry->Old.pszObjName));
    CHECK_LEN(fprintf(pFile, "cpp=%s\n", pEntry->New.pszCppName ? pEntry->New.pszCppName : pEntry->Old.pszCppName));
    CHECK_LEN(fprintf(pFile, "cpp-size=%lu\n", pEntry->New.pszCppName ? pEntry->New.cbCpp : pEntry->Old.cbCpp));

    if (!kOCSumIsEmpty(&pEntry->New.SumCompArgv))
    {
        CHECK_LEN(fprintf(pFile, "cc-argc=%u\n", pEntry->New.cArgvCompile));
        for (i = 0; i < pEntry->New.cArgvCompile; i++)
            CHECK_LEN(fprintf(pFile, "cc-argv-#%u=%s\n", i, pEntry->New.papszArgvCompile[i]));
        fprintf(pFile, "cc-argv-sum=");
        kOCSumFPrintf(&pEntry->New.SumCompArgv, pFile);
    }
    else
    {
        CHECK_LEN(fprintf(pFile, "cc-argc=%u\n", pEntry->Old.cArgvCompile));
        for (i = 0; i < pEntry->Old.cArgvCompile; i++)
            CHECK_LEN(fprintf(pFile, "cc-argv-#%u=%s\n", i, pEntry->Old.papszArgvCompile[i]));
        fprintf(pFile, "cc-argv-sum=");
        kOCSumFPrintf(&pEntry->Old.SumCompArgv, pFile);
    }


    for (pSum = !kOCSumIsEmpty(&pEntry->New.SumHead) ? &pEntry->New.SumHead : &pEntry->Old.SumHead;
         pSum;
         pSum = pSum->pNext)
    {
        fprintf(pFile, "cpp-sum=");
        kOCSumFPrintf(pSum, pFile);
    }

    fprintf(pFile, "the-end=fine\n");

#undef CHECK_LEN

    /*
     * Flush the file and check for errors.
     * On failure delete the file so we won't be seeing any invalid
     * files the next time or upset make with new timestamps.
     */
    errno = 0;
    if (    fflush(pFile) < 0
        ||  ferror(pFile))
    {
        int iErr = errno;
        fclose(pFile);
        UnlinkFileInDir(pEntry->pszName, pEntry->pszDir);
        FatalDie("Stream error occured while writing '%s' in '%s': %s\n",
                 pEntry->pszName, pEntry->pszDir, strerror(iErr));
    }
    fclose(pFile);
}


/**
 * Checks that the read cache entry is valid.
 * It sets fNeedCompiling if it isn't.
 *
 * @returns 1 valid, 0 invalid.
 * @param   pEntry      The cache entry.
 */
static int kOCEntryCheck(PKOCENTRY pEntry)
{
    return !pEntry->fNeedCompiling;
}


/**
 * Sets the object name and compares it with the old name if present.
 *
 * @param   pEntry      The cache entry.
 * @param   pszObjName  The new object name.
 */
static void kOCEntrySetCompileObjName(PKOCENTRY pEntry, const char *pszObjName)
{
    assert(!pEntry->New.pszObjName);
    pEntry->New.pszObjName = CalcRelativeName(pszObjName, pEntry->pszDir);

    if (    !pEntry->fNeedCompiling
        &&  (   !pEntry->Old.pszObjName
             || strcmp(pEntry->New.pszObjName, pEntry->Old.pszObjName)))
    {
        InfoMsg(2, "object file name differs\n");
        pEntry->fNeedCompiling = 1;
    }

    if (    !pEntry->fNeedCompiling
        &&  !DoesFileInDirExist(pEntry->New.pszObjName, pEntry->pszDir))
    {
        InfoMsg(2, "object file doesn't exist\n");
        pEntry->fNeedCompiling = 1;
    }
}


/**
 * Set the new compiler args, calc their checksum, and comparing them with any old ones.
 *
 * @param   pEntry              The cache entry.
 * @param   papszArgvCompile    The new argument vector for compilation.
 * @param   cArgvCompile        The number of arguments in the vector.
 *
 * @remark  Must call kOCEntrySetCompileObjName before this function!
 */
static void kOCEntrySetCompileArgv(PKOCENTRY pEntry, const char * const *papszArgvCompile, unsigned cArgvCompile)
{
    unsigned i;

    /* call me only once! */
    assert(!pEntry->New.cArgvCompile);
    /* call kOCEntrySetCompilerObjName first! */
    assert(pEntry->New.pszObjName);

    /*
     * Copy the argument vector and calculate the checksum.
     */
    pEntry->New.cArgvCompile = cArgvCompile;
    pEntry->New.papszArgvCompile = xmalloc((cArgvCompile + 1) * sizeof(pEntry->New.papszArgvCompile[0]));
    for (i = 0; i < cArgvCompile; i++)
        pEntry->New.papszArgvCompile[i] = xstrdup(papszArgvCompile[i]);
    pEntry->New.papszArgvCompile[i] = NULL; /* for exev/spawnv */

    kOCEntryCalcArgvSum(pEntry, papszArgvCompile, cArgvCompile, pEntry->New.pszObjName, &pEntry->New.SumCompArgv);
    kOCSumInfo(&pEntry->New.SumCompArgv, 4, "comp-argv");

    /*
     * Compare with the old argument vector.
     */
    if (    !pEntry->fNeedCompiling
        &&  !kOCSumIsEqual(&pEntry->New.SumCompArgv, &pEntry->Old.SumCompArgv))
    {
        InfoMsg(2, "compiler args differs\n");
        pEntry->fNeedCompiling = 1;
    }
}


/**
 * Sets the arch/os target and compares it with the old name if present.
 *
 * @param   pEntry      The cache entry.
 * @param   pszObjName  The new object name.
 */
static void kOCEntrySetTarget(PKOCENTRY pEntry, const char *pszTarget)
{
    assert(!pEntry->New.pszTarget);
    pEntry->New.pszTarget = xstrdup(pszTarget);

    if (    !pEntry->fNeedCompiling
        &&  (   !pEntry->Old.pszTarget
             || strcmp(pEntry->New.pszTarget, pEntry->Old.pszTarget)))
    {
        InfoMsg(2, "target differs\n");
        pEntry->fNeedCompiling = 1;
    }
}


/**
 * Sets the precompiler output filename.
 * We don't generally care if this matches the old name or not.
 *
 * @param   pEntry      The cache entry.
 * @param   pszCppName  The precompiler output filename.
 */
static void kOCEntrySetCppName(PKOCENTRY pEntry, const char *pszCppName)
{
    assert(!pEntry->New.pszCppName);
    pEntry->New.pszCppName = CalcRelativeName(pszCppName, pEntry->pszDir);
}


/**
 * Sets the piped mode of the precompiler and compiler.
 *
 * @param   pEntry                  The cache entry.
 * @param   fRedirPreCompStdOut     Whether the precompiler is in piped mode.
 * @param   fRedirCompileStdIn      Whether the compiler is in piped mode.
 */
static void kOCEntrySetPipedMode(PKOCENTRY pEntry, int fRedirPreCompStdOut, int fRedirCompileStdIn)
{
    pEntry->fPipedPreComp = fRedirPreCompStdOut;
    pEntry->fPipedCompile = fRedirCompileStdIn;
}


/**
 * Spawns a child in a synchronous fashion.
 * Terminating on failure.
 *
 * @param   papszArgv       Argument vector. The cArgv element is NULL.
 * @param   cArgv           The number of arguments in the vector.
 */
static void kOCEntrySpawn(PCKOCENTRY pEntry, const char * const *papszArgv, unsigned cArgv, const char *pszMsg, const char *pszStdOut)
{
#if defined(__OS2__) || defined(__WIN__)
    intptr_t rc;
    int fdStdOut = -1;
    if (pszStdOut)
    {
        int fdReDir;
        fdStdOut = dup(STDOUT_FILENO);
        close(STDOUT_FILENO);
        fdReDir = open(pszStdOut, O_CREAT | O_TRUNC | O_WRONLY, 0666);
        if (fdReDir < 0)
            FatalDie("%s - failed to create stdout redirection file '%s': %s\n",
                     pszMsg, pszStdOut, strerror(errno));

        if (fdReDir != STDOUT_FILENO)
        {
            if (dup2(fdReDir, STDOUT_FILENO) < 0)
                FatalDie("%s - dup2 failed: %s\n", pszMsg, strerror(errno));
            close(fdReDir);
        }
    }

    errno = 0;
    rc = _spawnvp(_P_WAIT, papszArgv[0], papszArgv);
    if (rc < 0)
        FatalDie("%s - _spawnvp failed (rc=0x%p): %s\n", pszMsg, rc, strerror(errno));
    if (rc > 0)
        FatalDie("%s - failed rc=%d\n", pszMsg, (int)rc);
    if (fdStdOut)
    {
        close(STDOUT_FILENO);
        fdStdOut = dup2(fdStdOut, STDOUT_FILENO);
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
            int fdReDir;

            close(STDOUT_FILENO);
            fdReDir = open(pszStdOut, O_CREAT | O_TRUNC | O_WRONLY, 0666);
            if (fdReDir < 0)
                FatalDie("%s - failed to create stdout redirection file '%s': %s\n",
                         pszMsg, pszStdOut, strerror(errno));
            if (fdReDir != STDOUT_FILENO)
            {
                if (dup2(fdReDir, STDOUT_FILENO) < 0)
                    FatalDie("%s - dup2 failed: %s\n", pszMsg, strerror(errno));
                close(fdReDir);
            }
        }

        execvp(papszArgv[0], (char **)papszArgv);
        FatalDie("%s - execvp failed: %s\n",
                 pszMsg, strerror(errno));
    }
    if (pid == -1)
        FatalDie("%s - fork() failed: %s\n", pszMsg, strerror(errno));

    pidWait = waitpid(pid, &iStatus, 0);
    while (pidWait < 0 && errno == EINTR)
        pidWait = waitpid(pid, &iStatus, 0);
    if (pidWait != pid)
        FatalDie("%s - waitpid failed rc=%d: %s\n",
                 pszMsg, pidWait, strerror(errno));
    if (!WIFEXITED(iStatus))
        FatalDie("%s - abended (iStatus=%#x)\n", pszMsg, iStatus);
    if (WEXITSTATUS(iStatus))
        FatalDie("%s - failed with rc %d\n", pszMsg, WEXITSTATUS(iStatus));
#endif
    (void)cArgv;
}


/**
 * Spawns child with optional redirection of stdin and stdout.
 *
 * @param   pEntry          The cache entry.
 * @param   papszArgv       Argument vector. The cArgv element is NULL.
 * @param   cArgv           The number of arguments in the vector.
 * @param   fdStdIn         Child stdin, -1 if it should inherit our stdin. Will be closed.
 * @param   fdStdOut        Child stdout, -1 if it should inherit our stdout. Will be closed.
 * @param   pszMsg          Message to start the info/error messages with.
 */
static pid_t kOCEntrySpawnChild(PCKOCENTRY pEntry, const char * const *papszArgv, unsigned cArgv, int fdStdIn, int fdStdOut, const char *pszMsg)
{
    pid_t pid;
    int fdSavedStdOut = -1;
    int fdSavedStdIn = -1;

    /*
     * Setup redirection.
     */
    if (fdStdOut != -1 && fdStdOut != STDOUT_FILENO)
    {
        fdSavedStdOut = dup(STDOUT_FILENO);
        if (dup2(fdStdOut, STDOUT_FILENO) < 0)
            FatalDie("%s - dup2(,1) failed: %s\n", pszMsg, strerror(errno));
        close(fdStdOut);
#ifndef __WIN__
        fcntl(fdSavedStdOut, F_SETFD, FD_CLOEXEC);
#endif
    }
    if (fdStdIn != -1 && fdStdIn != STDIN_FILENO)
    {
        fdSavedStdIn = dup(STDIN_FILENO);
        if (dup2(fdStdIn, STDIN_FILENO) < 0)
            FatalDie("%s - dup2(,0) failed: %s\n", pszMsg, strerror(errno));
        close(fdStdIn);
#ifndef __WIN__
        fcntl(fdSavedStdIn, F_SETFD, FD_CLOEXEC);
#endif
    }

    /*
     * Create the child process.
     */
#if defined(__OS2__) || defined(__WIN__)
    errno = 0;
    pid = _spawnvp(_P_NOWAIT, papszArgv[0], papszArgv);
    if (pid == -1)
        FatalDie("precompile - _spawnvp failed: %s\n", strerror(errno));

#else
    pid = fork();
    if (!pid)
    {
        execvp(papszArgv[0], (char **)papszArgv);
        FatalDie("precompile - execvp failed: %s\n", strerror(errno));
    }
    if (pid == -1)
        FatalDie("precompile - fork() failed: %s\n", strerror(errno));
#endif

    /*
     * Restore stdout & stdin.
     */
    if (fdSavedStdIn != -1)
    {
        close(STDIN_FILENO);
        dup2(fdStdOut, STDIN_FILENO);
        close(fdSavedStdIn);
    }
    if (fdSavedStdOut != -1)
    {
        close(STDOUT_FILENO);
        dup2(fdSavedStdOut, STDOUT_FILENO);
        close(fdSavedStdOut);
    }

    InfoMsg(3, "%s - spawned %ld\n", pszMsg, (long)pid);
    (void)cArgv;
    (void)pEntry;
    return pid;
}


/**
 * Waits for a child and exits fatally if the child failed in any way.
 *
 * @param   pEntry      The cache entry.
 * @param   pid         The child to wait for.
 * @param   pszMsg      Message to start the info/error messages with.
 */
static void kOCEntryWaitChild(PCKOCENTRY pEntry, pid_t pid, const char *pszMsg)
{
    int iStatus = -1;
    pid_t pidWait;
    InfoMsg(3, "%s - wait-child %ld\n", pszMsg, (long)pid);

#ifdef __WIN__
    pidWait = _cwait(&iStatus, pid, _WAIT_CHILD);
    if (pidWait == -1)
        FatalDie("%s - waitpid failed: %s\n", pszMsg, strerror(errno));
    if (iStatus)
        FatalDie("%s - failed with rc %d\n", pszMsg, iStatus);
#else
    pidWait = waitpid(pid, &iStatus, 0);
    while (pidWait < 0 && errno == EINTR)
        pidWait = waitpid(pid, &iStatus, 0);
    if (pidWait != pid)
        FatalDie("%s - waitpid failed rc=%d: %s\n", pidWait, strerror(errno));
    if (!WIFEXITED(iStatus))
        FatalDie("%s - abended (iStatus=%#x)\n", pszMsg, iStatus);
    if (WEXITSTATUS(iStatus))
        FatalDie("%s - failed with rc %d\n", pszMsg, WEXITSTATUS(iStatus));
#endif
    (void)pEntry;
}


/**
 * Creates a pipe for setting up redirected stdin/stdout.
 *
 * @param   pEntry          The cache entry.
 * @param   pFDs            Where to store the two file descriptors.
 * @param   pszMsg          The operation message for info/error messages.
 */
static void kOCEntryCreatePipe(PKOCENTRY pEntry, int *pFDs, const char *pszMsg)
{
    pFDs[0] = pFDs[1] = -1;
#if defined(__WIN__)
    if (_pipe(pFDs, 0, _O_NOINHERIT | _O_BINARY) < 0)
#else
    if (pipe(pFDs) < 0)
#endif
        FatalDie("%s - pipe failed: %s\n", pszMsg, strerror(errno));
#if !defined(__WIN__)
    fcntl(pFDs[0], F_SETFD, FD_CLOEXEC);
    fcntl(pFDs[1], F_SETFD, FD_CLOEXEC);
#endif
}


/**
 * Spawns a child that produces output to stdout.
 *
 * @param   papszArgv       Argument vector. The cArgv element is NULL.
 * @param   cArgv           The number of arguments in the vector.
 * @param   pszMsg          The operation message for info/error messages.
 * @param   pfnConsumer     Pointer to a consumer callback function that is responsible
 *                          for servicing the child output and closing the pipe.
 */
static void kOCEntrySpawnProducer(PKOCENTRY pEntry, const char * const *papszArgv, unsigned cArgv, const char *pszMsg,
                                  void (*pfnConsumer)(PKOCENTRY, int))
{
    int fds[2];
    pid_t pid;

    kOCEntryCreatePipe(pEntry, fds, pszMsg);
    pid = kOCEntrySpawnChild(pEntry, papszArgv, cArgv, -1, fds[1 /* write */], pszMsg);

    pfnConsumer(pEntry, fds[0 /* read */]);

    kOCEntryWaitChild(pEntry, pid, pszMsg);
}


/**
 * Spawns a child that consumes input on stdin.
 *
 * @param   papszArgv       Argument vector. The cArgv element is NULL.
 * @param   cArgv           The number of arguments in the vector.
 * @param   pszMsg          The operation message for info/error messages.
 * @param   pfnProducer     Pointer to a producer callback function that is responsible
 *                          for serving the child input and closing the pipe.
 */
static void kOCEntrySpawnConsumer(PKOCENTRY pEntry, const char * const *papszArgv, unsigned cArgv, const char *pszMsg,
                                  void (*pfnProducer)(PKOCENTRY, int))
{
    int fds[2];
    pid_t pid;

    kOCEntryCreatePipe(pEntry, fds, pszMsg);
    pid = kOCEntrySpawnChild(pEntry, papszArgv, cArgv, fds[0 /* read */], -1, pszMsg);

    pfnProducer(pEntry, fds[1 /* write */]);

    kOCEntryWaitChild(pEntry, pid, pszMsg);
}


/**
 * Spawns two child processes, one producing output and one consuming.
 * Terminating on failure.
 *
 * @param   papszArgv       Argument vector. The cArgv element is NULL.
 * @param   cArgv           The number of arguments in the vector.
 * @param   pszMsg          The operation message for info/error messages.
 * @param   pfnConsumer     Pointer to a consumer callback function that is responsible
 *                          for servicing the child output and closing the pipe.
 */
static void kOCEntrySpawnTee(PKOCENTRY pEntry, const char * const *papszProdArgv, unsigned cProdArgv,
                             const char * const *papszConsArgv, unsigned cConsArgv,
                             const char *pszMsg, void (*pfnTeeConsumer)(PKOCENTRY, int, int))
{
    int fds[2];
    int fdIn, fdOut;
    pid_t pidProducer, pidConsumer;

    /*
     * The producer.
     */
    kOCEntryCreatePipe(pEntry, fds, pszMsg);
    pidConsumer = kOCEntrySpawnChild(pEntry, papszProdArgv, cProdArgv, -1, fds[1 /* write */], pszMsg);
    fdIn = fds[0 /* read */];

    /*
     * The consumer.
     */
    kOCEntryCreatePipe(pEntry, fds, pszMsg);
    pidProducer = kOCEntrySpawnChild(pEntry, papszConsArgv, cConsArgv, fds[0 /* read */], -1, pszMsg);
    fdOut = fds[1 /* write */];

    /*
     * Hand it on to the tee consumer.
     */
    pfnTeeConsumer(pEntry, fdIn, fdOut);

    /*
     * Reap the children.
     */
    kOCEntryWaitChild(pEntry, pidProducer, pszMsg);
    kOCEntryWaitChild(pEntry, pidConsumer, pszMsg);
}


/**
 * Reads the output from the precompiler.
 *
 * @param   pEntry      The cache entry. New.cbCpp and New.pszCppMapping will be updated.
 * @param   pWhich      Specifies what to read (old/new).
 * @param   fNonFatal   Whether failure is fatal or not.
 */
static int kOCEntryReadCppOutput(PKOCENTRY pEntry, struct KOCENTRYDATA *pWhich, int fNonFatal)
{
    pWhich->pszCppMapping = ReadFileInDir(pWhich->pszCppName, pEntry->pszDir, &pWhich->cbCpp);
    if (!pWhich->pszCppMapping)
    {
        if (!fNonFatal)
            FatalDie("failed to open/read '%s' in '%s': %s\n",
                     pWhich->pszCppName, pEntry->pszDir, strerror(errno));
        InfoMsg(2, "failed to open/read '%s' in '%s': %s\n",
                pWhich->pszCppName, pEntry->pszDir, strerror(errno));
        return -1;
    }

    InfoMsg(3, "precompiled file is %lu bytes long\n", (unsigned long)pWhich->cbCpp);
    return 0;
}


/**
 * Worker for kOCEntryPreCompile and calculates the checksum of
 * the precompiler output.
 *
 * @param   pEntry      The cache entry. NewSum will be updated.
 */
static void kOCEntryCalcChecksum(PKOCENTRY pEntry)
{
    KOCSUMCTX Ctx;
    kOCSumInitWithCtx(&pEntry->New.SumHead, &Ctx);
    kOCSumUpdate(&pEntry->New.SumHead, &Ctx, pEntry->New.pszCppMapping, pEntry->New.cbCpp);
    kOCSumFinalize(&pEntry->New.SumHead, &Ctx);
    kOCSumInfo(&pEntry->New.SumHead, 4, "cpp (file)");
}


/**
 * This consumes the precompiler output and checksums it.
 *
 * @param   pEntry  The cache entry.
 * @param   fdIn    The precompiler output pipe.
 * @param   fdOut   The compiler input pipe, -1 if no compiler.
 */
static void kOCEntryPreCompileConsumer(PKOCENTRY pEntry, int fdIn)
{
    KOCSUMCTX Ctx;
    long cbLeft;
    long cbAlloc;
    char *psz;

    kOCSumInitWithCtx(&pEntry->New.SumHead, &Ctx);
    cbAlloc = pEntry->Old.cbCpp ? (pEntry->Old.cbCpp + 4*1024*1024 + 4096) & ~(4*1024*1024 - 1) : 4*1024*1024;
    cbLeft = cbAlloc;
    pEntry->New.pszCppMapping = psz = xmalloc(cbAlloc);
    for (;;)
    {
        /*
         * Read data from the pipe.
         */
        long cbRead = read(fdIn, psz, cbLeft - 1);
        if (!cbRead)
            break;
        if (cbRead < 0)
        {
            if (errno == EINTR)
                continue;
            FatalDie("precompile - read(%d,,%ld) failed: %s\n",
                     fdIn, (long)cbLeft, strerror(errno));
        }

        /*
         * Process the data.
         */
        psz[cbRead] = '\0';
        kOCSumUpdate(&pEntry->New.SumHead, &Ctx, psz, cbRead);

        /*
         * Advance.
         */
        psz += cbRead;
        cbLeft -= cbRead;
        if (cbLeft <= 1)
        {
            size_t off = psz - pEntry->New.pszCppMapping;
            cbLeft = 4*1024*1024;
            cbAlloc += cbLeft;
            pEntry->New.pszCppMapping = xrealloc(pEntry->New.pszCppMapping, cbAlloc);
            psz = pEntry->New.pszCppMapping + off;
        }
    }

    close(fdIn);
    pEntry->New.cbCpp = cbAlloc - cbLeft;
    kOCSumFinalize(&pEntry->New.SumHead, &Ctx);
    kOCSumInfo(&pEntry->New.SumHead, 4, "cpp (pipe)");
}




/**
 * Run the precompiler and calculate the checksum of the output.
 *
 * @param   pEntry              The cache entry.
 * @param   papszArgvPreComp    The argument vector for executing precompiler. The cArgvPreComp'th argument must be NULL.
 * @param   cArgvPreComp        The number of arguments.
 */
static void kOCEntryPreCompile(PKOCENTRY pEntry, const char * const *papszArgvPreComp, unsigned cArgvPreComp)
{
    /*
     * If we're executing the precompiler in piped mode, it's relatively simple.
     */
    if (pEntry->fPipedPreComp)
        kOCEntrySpawnProducer(pEntry, papszArgvPreComp, cArgvPreComp, "precompile",
                              kOCEntryPreCompileConsumer);
    else
    {
        /*
         * Rename the old precompiled output to '-old' so the precompiler won't
         * overwrite it when we execute it.
         */
        if (    pEntry->Old.pszCppName
            &&  DoesFileInDirExist(pEntry->Old.pszCppName, pEntry->pszDir))
        {
            size_t cch = strlen(pEntry->Old.pszCppName);
            char *psz = xmalloc(cch + sizeof("-old"));
            memcpy(psz, pEntry->Old.pszCppName, cch);
            memcpy(psz + cch, "-old", sizeof("-old"));

            InfoMsg(3, "renaming '%s' to '%s' in '%s'\n", pEntry->Old.pszCppName, psz, pEntry->pszDir);
            UnlinkFileInDir(psz, pEntry->pszDir);
            if (RenameFileInDir(pEntry->Old.pszCppName, psz, pEntry->pszDir))
                FatalDie("failed to rename '%s' -> '%s' in '%s': %s\n",
                         pEntry->Old.pszCppName, psz, pEntry->pszDir, strerror(errno));
            free(pEntry->Old.pszCppName);
            pEntry->Old.pszCppName = psz;
        }

        /*
         * Precompile it and calculate the checksum on the output.
         */
        InfoMsg(3, "precompiling -> '%s'...\n", pEntry->New.pszCppName);
        kOCEntrySpawn(pEntry, papszArgvPreComp, cArgvPreComp, "precompile", NULL);
        kOCEntryReadCppOutput(pEntry, &pEntry->New, 0 /* fatal */);
        kOCEntryCalcChecksum(pEntry);
    }
}


/**
 * Worker function for kOCEntryTeeConsumer and kOCEntryCompileIt that
 * writes the precompiler output to disk.
 *
 * @param   pEntry      The cache entry.
 * @param   fFreeIt     Whether we can free it after writing it or not.
 */
static void kOCEntryWriteCppOutput(PKOCENTRY pEntry, int fFreeIt)
{
    /*
     * Remove old files.
     */
    if (pEntry->Old.pszCppName)
        UnlinkFileInDir(pEntry->Old.pszCppName, pEntry->pszDir);
    if (pEntry->New.pszCppName)
        UnlinkFileInDir(pEntry->New.pszCppName, pEntry->pszDir);

    /*
     * Write it to disk if we've got a file name.
     */
    if (pEntry->New.pszCppName)
    {
        long cbLeft;
        char *psz;
        int fd = OpenFileInDir(pEntry->New.pszCppName, pEntry->pszDir,
                               O_WRONLY | O_CREAT | O_TRUNC | O_BINARY, 0666);
        if (fd == -1)
            FatalDie("Failed to create '%s' in '%s': %s\n",
                     pEntry->New.pszCppName, pEntry->pszDir, strerror(errno));
        psz = pEntry->New.pszCppMapping;
        cbLeft = pEntry->New.cbCpp;
        while (cbLeft > 0)
        {
            long cbWritten = write(fd, psz, cbLeft);
            if (cbWritten < 0)
            {
                int iErr = errno;
                if (iErr == EINTR)
                    continue;
                close(fd);
                UnlinkFileInDir(pEntry->New.pszCppName, pEntry->pszDir);
                FatalDie("error writing '%s' in '%s': %s\n",
                         pEntry->New.pszCppName, pEntry->pszDir, strerror(iErr));
            }

            psz += cbWritten;
            cbLeft -= cbWritten;
        }
        close(fd);
    }

    /*
     * Free it.
     */
    if (fFreeIt)
    {
        free(pEntry->New.pszCppMapping);
        pEntry->New.pszCppMapping = NULL;
    }
}


/**
 * kOCEntrySpawnConsumer callback that passes the precompiler
 * output to the compiler and writes it to the disk (latter only when necesary).
 *
 * @param   pEntry      The cache entry.
 * @param   fdOut       The pipe handle connected to the childs stdin.
 */
static void kOCEntryCompileProducer(PKOCENTRY pEntry, int fdOut)
{
    const char *psz = pEntry->New.pszCppMapping;
    long cbLeft = pEntry->New.cbCpp;
    while (cbLeft > 0)
    {
        long cbWritten = write(fdOut, psz, cbLeft);
        if (cbWritten < 0)
        {
            if (errno == EINTR)
                continue;
            FatalDie("compile - write(%d,,%ld) failed: %s\n", fdOut, cbLeft, strerror(errno));
        }
        psz += cbWritten;
        cbLeft -= cbWritten;
    }
    close(fdOut);

    if (pEntry->fPipedPreComp)
        kOCEntryWriteCppOutput(pEntry, 1 /* free it */);
}


/**
 * Does the actual compiling.
 *
 * @param   pEntry      The cache entry.
 */
static void kOCEntryCompileIt(PKOCENTRY pEntry)
{
    /*
     * Delete the object files and free old cpp output that's no longer needed.
     */
    if (pEntry->Old.pszObjName)
        UnlinkFileInDir(pEntry->Old.pszObjName, pEntry->pszDir);
    UnlinkFileInDir(pEntry->New.pszObjName, pEntry->pszDir);

    free(pEntry->Old.pszCppMapping);
    pEntry->Old.pszCppMapping = NULL;
    if (!pEntry->fPipedPreComp && !pEntry->fPipedCompile)
    {
        free(pEntry->New.pszCppMapping);
        pEntry->New.pszCppMapping = NULL;
    }

    /*
     * Do the (re-)compile job.
     */
    if (pEntry->fPipedCompile)
    {
        if (    !pEntry->fPipedPreComp
            &&  !pEntry->New.pszCppMapping)
            kOCEntryReadCppOutput(pEntry, &pEntry->New, 0 /* fatal */);
        InfoMsg(3, "compiling -> '%s'...\n", pEntry->New.pszObjName);
        kOCEntrySpawnConsumer(pEntry, (const char * const *)pEntry->New.papszArgvCompile, pEntry->New.cArgvCompile,
                              "compile", kOCEntryCompileProducer);
    }
    else
    {
        if (pEntry->fPipedPreComp)
            kOCEntryWriteCppOutput(pEntry, 1 /* free it */);
        InfoMsg(3, "compiling -> '%s'...\n", pEntry->New.pszObjName);
        kOCEntrySpawn(pEntry, (const char * const *)pEntry->New.papszArgvCompile, pEntry->New.cArgvCompile, "compile", NULL);
    }
}


/**
 * kOCEntrySpawnTee callback that works sort of like 'tee'.
 *
 * It will calculate the precompiled output checksum and
 * write it to disk while the compiler is busy compiling it.
 *
 * @param   pEntry  The cache entry.
 * @param   fdIn    The input handle (connected to the precompiler).
 * @param   fdOut   The output handle (connected to the compiler).
 */
static void kOCEntryTeeConsumer(PKOCENTRY pEntry, int fdIn, int fdOut)
{
    KOCSUMCTX Ctx;
    long cbLeft;
    long cbAlloc;
    char *psz;

    kOCSumInitWithCtx(&pEntry->New.SumHead, &Ctx);
    cbAlloc = pEntry->Old.cbCpp ? (pEntry->Old.cbCpp + 4*1024*1024 + 4096) & ~(4*1024*1024 - 1) : 4*1024*1024;
    cbLeft = cbAlloc;
    pEntry->New.pszCppMapping = psz = xmalloc(cbAlloc);
    InfoMsg(3, "precompiler|compile - starting passhtru...\n");
    for (;;)
    {
        /*
         * Read data from the pipe.
         */
        long cbRead = read(fdIn, psz, cbLeft - 1);
        if (!cbRead)
            break;
        if (cbRead < 0)
        {
            if (errno == EINTR)
                continue;
            FatalDie("precompile|compile - read(%d,,%ld) failed: %s\n",
                     fdIn, (long)cbLeft, strerror(errno));
        }
        InfoMsg(3, "precompiler|compile - read %d\n", cbRead);

        /*
         * Process the data.
         */
        psz[cbRead] = '\0';
        kOCSumUpdate(&pEntry->New.SumHead, &Ctx, psz, cbRead);
        do
        {
            long cbWritten = write(fdOut, psz, cbRead);
            if (cbWritten < 0)
            {
                if (errno == EINTR)
                    continue;
                FatalDie("precompile|compile - write(%d,,%ld) failed: %s\n", fdOut, cbRead, strerror(errno));
            }
            psz += cbWritten;
            cbRead -= cbWritten;
            cbLeft -= cbWritten;
        } while (cbRead > 0);

        /*
         * Expand the buffer?
         */
        if (cbLeft <= 1)
        {
            size_t off = psz - pEntry->New.pszCppMapping;
            cbLeft = 4*1024*1024;
            cbAlloc += cbLeft;
            pEntry->New.pszCppMapping = xrealloc(pEntry->New.pszCppMapping, cbAlloc);
            psz = pEntry->New.pszCppMapping + off;
        }
    }
    InfoMsg(3, "precompiler|compile - done passhtru\n");

    close(fdIn);
    close(fdOut);
    pEntry->New.cbCpp = cbAlloc - cbLeft;
    kOCSumFinalize(&pEntry->New.SumHead, &Ctx);
    kOCSumInfo(&pEntry->New.SumHead, 4, "cpp (tee)");

    /*
     * Write the precompiler output to disk and free the memory it
     * occupies while the compiler is busy compiling.
     */
    kOCEntryWriteCppOutput(pEntry, 1 /* free it */);
}


/**
 * Performs pre-compile and compile in one go (typical clean build scenario).
 *
 * @param   pEntry              The cache entry.
 * @param   papszArgvPreComp    The argument vector for executing precompiler. The cArgvPreComp'th argument must be NULL.
 * @param   cArgvPreComp        The number of arguments.
 */
static void kOCEntryPreCompileAndCompile(PKOCENTRY pEntry, const char * const *papszArgvPreComp, unsigned cArgvPreComp)
{
    if (    pEntry->fPipedCompile
        &&  pEntry->fPipedPreComp)
    {
        /*
         * Clean up old stuff first.
         */
        if (pEntry->Old.pszObjName)
            UnlinkFileInDir(pEntry->Old.pszObjName, pEntry->pszDir);
        if (pEntry->New.pszObjName)
            UnlinkFileInDir(pEntry->New.pszObjName, pEntry->pszDir);
        if (pEntry->Old.pszCppName)
            UnlinkFileInDir(pEntry->Old.pszCppName, pEntry->pszDir);
        if (pEntry->New.pszCppName)
            UnlinkFileInDir(pEntry->New.pszCppName, pEntry->pszDir);

        /*
         * Do the actual compile and write the precompiler output to disk.
         */
        kOCEntrySpawnTee(pEntry, papszArgvPreComp, cArgvPreComp,
                         (const char * const *)pEntry->New.papszArgvCompile, pEntry->New.cArgvCompile,
                         "precompile|compile", kOCEntryTeeConsumer);
    }
    else
    {
        kOCEntryPreCompile(pEntry, papszArgvPreComp, cArgvPreComp);
        kOCEntryCompileIt(pEntry);
    }
}


/**
 * Check whether the string is a '#line' statement.
 *
 * @returns 1 if it is, 0 if it isn't.
 * @param   psz         The line to examin.
 * @parma   piLine      Where to store the line number.
 * @parma   ppszFile    Where to store the start of the filename.
 */
static int kOCEntryIsLineStatement(const char *psz, unsigned *piLine, const char **ppszFile)
{
    unsigned iLine;

    /* Expect a hash. */
    if (*psz++ != '#')
        return 0;

    /* Skip blanks between '#' and the line / number */
    while (*psz == ' ' || *psz == '\t')
        psz++;

    /* Skip the 'line' if present. */
    if (!strncmp(psz, "line", sizeof("line") - 1))
        psz += sizeof("line");

    /* Expect a line number now. */
    if ((unsigned char)(*psz - '0') > 9)
        return 0;
    iLine = 0;
    do
    {
        iLine *= 10;
        iLine += (*psz - '0');
        psz++;
    }
    while ((unsigned char)(*psz - '0') <= 9);

    /* Expect one or more space now. */
    if (*psz != ' ' && *psz != '\t')
        return 0;
    do  psz++;
    while (*psz == ' ' || *psz == '\t');

    /* that's good enough. */
    *piLine = iLine;
    *ppszFile = psz;
    return 1;
}


/**
 * Scan backwards for the previous #line statement.
 *
 * @returns The filename in the previous statement.
 * @param   pszStart        Where to start.
 * @param   pszStop         Where to stop. Less than pszStart.
 * @param   piLine          The line number count to adjust.
 */
static const char *kOCEntryFindFileStatement(const char *pszStart, const char *pszStop, unsigned *piLine)
{
    unsigned iLine = *piLine;
    assert(pszStart >= pszStop);
    while (pszStart >= pszStop)
    {
        if (*pszStart == '\n')
            iLine++;
        else if (*pszStart == '#')
        {
            unsigned iLineTmp;
            const char *pszFile;
            const char *psz = pszStart - 1;
            while (psz >= pszStop && (*psz == ' ' || *psz =='\t'))
                psz--;
            if (    (psz < pszStop || *psz == '\n')
                &&  kOCEntryIsLineStatement(pszStart, &iLineTmp, &pszFile))
            {
                *piLine = iLine + iLineTmp - 1;
                return pszFile;
            }
        }
        pszStart--;
    }
    return NULL;
}


/**
 * Worker for kOCEntryCompareOldAndNewOutput() that compares the
 * precompiled output using a fast but not very good method.
 *
 * @returns 1 if matching, 0 if not matching.
 * @param   pEntry      The entry containing the names of the files to compare.
 *                      The entry is not updated in any way.
 */
static int kOCEntryCompareFast(PCKOCENTRY pEntry)
{
    const char *        psz1 = pEntry->New.pszCppMapping;
    const char * const  pszEnd1 = psz1 + pEntry->New.cbCpp;
    const char *        psz2 = pEntry->Old.pszCppMapping;
    const char * const  pszEnd2 = psz2 + pEntry->Old.cbCpp;

    assert(*pszEnd1 == '\0');
    assert(*pszEnd2 == '\0');

    /*
     * Iterate block by block and backtrack when we find a difference.
     */
    for (;;)
    {
        size_t cch = pszEnd1 - psz1;
        if (cch > (size_t)(pszEnd2 - psz2))
            cch = pszEnd2 - psz2;
        if (cch > 4096)
            cch = 4096;
        if (    cch
            &&  !memcmp(psz1, psz2, cch))
        {
            /* no differences */
            psz1 += cch;
            psz2 += cch;
        }
        else
        {
            /*
             * Pinpoint the difference exactly and the try find the start
             * of that line. Then skip forward until we find something to
             * work on that isn't spaces, #line statements or closing curly
             * braces.
             *
             * The closing curly braces are ignored because they are frequently
             * found at the end of header files (__END_DECLS) and the worst
             * thing that may happen if it isn't one of these braces we're
             * ignoring is that the final line in a function block is a little
             * bit off in the debug info.
             *
             * Since we might be skipping a few new empty headers, it is
             * possible that we will omit this header from the dependencies
             * when using VCC. This might not be a problem, since it seems
             * we'll have to use the precompiler output to generate the deps
             * anyway.
             */
            const char *psz;
            const char *pszMismatch1;
            const char *pszFile1 = NULL;
            unsigned    iLine1 = 0;
            unsigned    cCurlyBraces1 = 0;
            const char *pszMismatch2;
            const char *pszFile2 = NULL;
            unsigned    iLine2 = 0;
            unsigned    cCurlyBraces2 = 0;

            /* locate the difference. */
            while (cch >= 512 && !memcmp(psz1, psz2, 512))
                psz1 += 512, psz2 += 512, cch -= 512;
            while (cch >= 64 && !memcmp(psz1, psz2, 64))
                psz1 += 64, psz2 += 64, cch -= 64;
            while (*psz1 == *psz2 && cch > 0)
                psz1++, psz2++, cch--;

            /* locate the start of that line. */
            psz = psz1;
            while (     psz > pEntry->New.pszCppMapping
                   &&   psz[-1] != '\n')
                psz--;
            psz2 -= (psz1 - psz);
            pszMismatch2 = psz2;
            pszMismatch1 = psz1 = psz;

            /* Parse the 1st file line by line. */
            while (psz1 < pszEnd1)
            {
                if (*psz1 == '\n')
                {
                    psz1++;
                    iLine1++;
                }
                else
                {
                    psz = psz1;
                    while (isspace(*psz) && *psz != '\n')
                        psz++;
                    if (*psz == '\n')
                    {
                        psz1 = psz + 1;
                        iLine1++;
                    }
                    else if (*psz == '#' && kOCEntryIsLineStatement(psz, &iLine1, &pszFile1))
                    {
                        psz1 = memchr(psz, '\n', pszEnd1 - psz);
                        if (!psz1++)
                            psz1 = pszEnd1;
                    }
                    else if (*psz == '}')
                    {
                        do psz++;
                        while (isspace(*psz) && *psz != '\n');
                        if (*psz == '\n')
                            iLine1++;
                        else if (psz != pszEnd1)
                            break;
                        cCurlyBraces1++;
                        psz1 = psz;
                    }
                    else if (psz == pszEnd1)
                        psz1 = psz;
                    else /* found something that can be compared. */
                        break;
                }
            }

            /* Ditto for the 2nd file. */
            while (psz2 < pszEnd2)
            {
                if (*psz2 == '\n')
                {
                    psz2++;
                    iLine2++;
                }
                else
                {
                    psz = psz2;
                    while (isspace(*psz) && *psz != '\n')
                        psz++;
                    if (*psz == '\n')
                    {
                        psz2 = psz + 1;
                        iLine2++;
                    }
                    else if (*psz == '#' && kOCEntryIsLineStatement(psz, &iLine2, &pszFile2))
                    {
                        psz2 = memchr(psz, '\n', pszEnd2 - psz);
                        if (!psz2++)
                            psz2 = pszEnd2;
                    }
                    else if (*psz == '}')
                    {
                        do psz++;
                        while (isspace(*psz) && *psz != '\n');
                        if (*psz == '\n')
                            iLine2++;
                        else if (psz != pszEnd2)
                            break;
                        cCurlyBraces2++;
                        psz2 = psz;
                    }
                    else if (psz == pszEnd2)
                        psz2 = psz;
                    else /* found something that can be compared. */
                        break;
                }
            }

            /* Match the number of ignored closing curly braces. */
            if (cCurlyBraces1 != cCurlyBraces2)
                return 0;

            /* Reaching the end of any of them means the return statement can decide. */
            if (   psz1 == pszEnd1
                || psz2 == pszEnd2)
                break;

            /* Match the current line. */
            psz = memchr(psz1, '\n', pszEnd1 - psz1);
            if (!psz)
                psz = pszEnd1;
            cch = psz - psz1;
            if (psz2 + cch > pszEnd2)
                break;
            if (memcmp(psz1, psz2, cch))
                break;

            /* Check that we're at the same location now. */
            if (!pszFile1)
                pszFile1 = kOCEntryFindFileStatement(pszMismatch1, pEntry->New.pszCppMapping, &iLine1);
            if (!pszFile2)
                pszFile2 = kOCEntryFindFileStatement(pszMismatch2, pEntry->Old.pszCppMapping, &iLine2);
            if (pszFile1 && pszFile2)
            {
                if (iLine1 != iLine2)
                    break;
                while (*pszFile1 == *pszFile2 && *pszFile1 != '\n' && *pszFile1)
                    pszFile1++, pszFile2++;
                if (*pszFile1 != *pszFile2)
                    break;
            }
            else if (pszFile1 || pszFile2)
            {
                assert(0); /* this shouldn't happen. */
                break;
            }

            /* Try align psz1 on 8 or 4 bytes so at least one of the buffers are aligned. */
            psz1 += cch;
            psz2 += cch;
            if (cch >= ((uintptr_t)psz1 & 7))
            {
                psz2 -= ((uintptr_t)psz1 & 7);
                psz1 -= ((uintptr_t)psz1 & 7);
            }
            else if (cch >= ((uintptr_t)psz1 & 3))
            {
                psz2 -= ((uintptr_t)psz1 & 3);
                psz1 -= ((uintptr_t)psz1 & 3);
            }
        }
    }

    return psz1 == pszEnd1
        && psz2 == pszEnd2;
}


/**
 * Worker for kOCEntryCompileIfNeeded that compares the
 * precompiled output.
 *
 * @returns 1 if matching, 0 if not matching.
 * @param   pEntry      The entry containing the names of the files to compare.
 *                      This will load the old cpp output (changing pszOldCppName and Old.cbCpp).
 */
static int kOCEntryCompareOldAndNewOutput(PKOCENTRY pEntry)
{
    /*
     * I may implement a more sophisticated alternative method later... maybe.
     */
    if (kOCEntryReadCppOutput(pEntry, &pEntry->Old, 1 /* nonfatal */) == -1)
        return 0;
    //if ()
    //    return kOCEntryCompareBest(pEntry);
    return kOCEntryCompareFast(pEntry);
}


/**
 * Check if re-compilation is required.
 * This sets the fNeedCompile flag.
 *
 * @param   pEntry              The cache entry.
 */
static void kOCEntryCalcRecompile(PKOCENTRY pEntry)
{
    if (pEntry->fNeedCompiling)
        return;

    /*
     * Check if the precompiler output differ in any significant way?
     */
    if (!kOCSumHasEqualInChain(&pEntry->Old.SumHead, &pEntry->New.SumHead))
    {
        InfoMsg(2, "no checksum match - comparing output\n");
        if (!kOCEntryCompareOldAndNewOutput(pEntry))
            pEntry->fNeedCompiling = 1;
        else
            kOCSumAddChain(&pEntry->New.SumHead, &pEntry->Old.SumHead);
    }
}


/**
 * Does this cache entry need compiling or what?
 *
 * @returns 1 if it does, 0 if it doesn't.
 * @param   pEntry      The cache entry in question.
 */
static int kOCEntryNeedsCompiling(PCKOCENTRY pEntry)
{
    return pEntry->fNeedCompiling;
}


/**
 * Worker function for kOCEntryCopy.
 *
 * @param   pEntry      The entry we're coping to, which pszTo is relative to.
 * @param   pszTo       The destination.
 * @param   pszFrom     The source. This path will be freed.
 */
static void kOCEntryCopyFile(PCKOCENTRY pEntry, const char *pszTo, char *pszSrc)
{
    char *pszDst = MakePathFromDirAndFile(pszTo, pEntry->pszDir);
    char *pszBuf = xmalloc(256 * 1024);
    char *psz;
    int fdSrc;
    int fdDst;

    /*
     * Open the files.
     */
    fdSrc = open(pszSrc, O_RDONLY | O_BINARY);
    if (fdSrc == -1)
        FatalDie("failed to open '%s': %s\n", pszSrc, strerror(errno));

    unlink(pszDst);
    fdDst = open(pszDst, O_WRONLY | O_CREAT | O_TRUNC | O_BINARY, 0666);
    if (fdDst == -1)
        FatalDie("failed to create '%s': %s\n", pszDst, strerror(errno));

    /*
     * Copy them.
     */
    for (;;)
    {
        /* read a chunk. */
        long cbRead = read(fdSrc, pszBuf, 256*1024);
        if (cbRead < 0)
        {
            if (errno == EINTR)
                continue;
            FatalDie("read '%s' failed: %s\n", pszSrc, strerror(errno));
        }
        if (!cbRead)
            break; /* eof */

        /* write the chunk. */
        psz = pszBuf;
        do
        {
            long cbWritten = write(fdDst, psz, cbRead);
            if (cbWritten < 0)
            {
                if (errno == EINTR)
                    continue;
                FatalDie("write '%s' failed: %s\n", pszSrc, strerror(errno));
            }
            psz += cbWritten;
            cbRead -= cbWritten;
        } while (cbRead > 0);
    }

    /* cleanup */
    if (close(fdDst) != 0)
        FatalDie("closing '%s' failed: %s\n", pszDst, strerror(errno));
    close(fdSrc);
    free(pszBuf);
    free(pszDst);
    free(pszSrc);
}


/**
 * Copies the object (and whatever else) from one cache entry to another.
 *
 * This is called when a matching cache entry has been found and we don't
 * need to recompile anything.
 *
 * @param   pEntry      The entry to copy to.
 * @param   pFrom       The entry to copy from.
 */
static void kOCEntryCopy(PKOCENTRY pEntry, PCKOCENTRY pFrom)
{
    kOCEntryCopyFile(pEntry, pEntry->New.pszObjName,
                     MakePathFromDirAndFile(pFrom->New.pszObjName
                                            ? pFrom->New.pszObjName : pFrom->Old.pszObjName,
                                            pFrom->pszDir));
}


/**
 * Gets the absolute path to the cache entry.
 *
 * @returns absolute path to the cache entry.
 * @param   pEntry      The cache entry in question.
 */
static const char *kOCEntryAbsPath(PCKOCENTRY pEntry)
{
    return pEntry->pszAbsPath;
}






/**
 * Digest of one cache entry.
 *
 * This contains all the information required to find a matching
 * cache entry without having to open each of the files.
 */
typedef struct KOCDIGEST
{
    /** The relative path to the entry. Optional if pszAbsPath is set. */
    char *pszRelPath;
    /** The absolute path to the entry. Optional if pszRelPath is set. */
    char *pszAbsPath;
    /** The target os/arch identifier. */
    char *pszTarget;
    /** A unique number assigned to the entry when it's (re)-inserted
     * into the cache. This is used for simple consitency checking. */
    uint32_t uKey;
    /** The checksum of the compile argument vector. */
    KOCSUM SumCompArgv;
    /** The list of precompiler output checksums that's . */
    KOCSUM SumHead;
} KOCDIGEST;
/** Pointer to a file digest. */
typedef KOCDIGEST *PKOCDIGEST;
/** Pointer to a const file digest. */
typedef KOCDIGEST *PCKOCDIGEST;


/**
 * Initializes the specified digest.
 *
 * @param   pDigest     The digest.
 */
static void kOCDigestInit(PKOCDIGEST pDigest)
{
    memset(pDigest, 0, sizeof(*pDigest));
    kOCSumInit(&pDigest->SumHead);
}


/**
 * Initializes the digest for the specified entry.
 *
 * @param   pDigest     The (uninitialized) digest.
 * @param   pEntry      The entry.
 */
static void kOCDigestInitFromEntry(PKOCDIGEST pDigest, PCKOCENTRY pEntry)
{
    kOCDigestInit(pDigest);

    pDigest->uKey = pEntry->uKey;
    pDigest->pszTarget = xstrdup(pEntry->New.pszTarget ? pEntry->New.pszTarget : pEntry->Old.pszTarget);

    kOCSumInit(&pDigest->SumCompArgv);
    if (!kOCSumIsEmpty(&pEntry->New.SumCompArgv))
        kOCSumAdd(&pDigest->SumCompArgv, &pEntry->New.SumCompArgv);
    else
        kOCSumAdd(&pDigest->SumCompArgv, &pEntry->Old.SumCompArgv);

    kOCSumInit(&pDigest->SumHead);
    if (!kOCSumIsEmpty(&pEntry->New.SumHead))
        kOCSumAddChain(&pDigest->SumHead, &pEntry->New.SumHead);
    else
        kOCSumAddChain(&pDigest->SumHead, &pEntry->Old.SumHead);

    /** @todo implement selective relative path support. */
    pDigest->pszRelPath = NULL;
    pDigest->pszAbsPath = xstrdup(kOCEntryAbsPath(pEntry));
}


/**
 * Purges a digest, freeing all resources and returning
 * it to the initial state.
 *
 * @param   pDigest     The digest.
 */
static void kOCDigestPurge(PKOCDIGEST pDigest)
{
    free(pDigest->pszRelPath);
    free(pDigest->pszAbsPath);
    free(pDigest->pszTarget);
    pDigest->pszTarget = pDigest->pszAbsPath = pDigest->pszRelPath = NULL;
    pDigest->uKey = 0;
    kOCSumDeleteChain(&pDigest->SumCompArgv);
    kOCSumDeleteChain(&pDigest->SumHead);
}


/**
 * Returns the absolute path to the entry, calculating
 * the path if necessary.
 *
 * @returns absolute path.
 * @param   pDigest     The digest.
 * @param   pszDir      The cache directory that it might be relative to.
 */
static const char *kOCDigestAbsPath(PCKOCDIGEST pDigest, const char *pszDir)
{
    if (!pDigest->pszAbsPath)
    {
        char *pszPath = MakePathFromDirAndFile(pDigest->pszRelPath, pszDir);
        ((PKOCDIGEST)pDigest)->pszAbsPath = AbsPath(pszPath);
        free(pszPath);
    }
    return pDigest->pszAbsPath;
}


/**
 * Checks that the digest matches the
 *
 * @returns 1 if valid, 0 if invalid in some way.
 *
 * @param   pDigest     The digest to validate.
 * @param   pEntry      What to validate it against.
 */
static int kOCDigestIsValid(PCKOCDIGEST pDigest, PCKOCENTRY pEntry)
{
    PCKOCSUM pSum;
    PCKOCSUM pSumEntry;

    if (pDigest->uKey != pEntry->uKey)
        return 0;

    if (!kOCSumIsEqual(&pDigest->SumCompArgv,
                       kOCSumIsEmpty(&pEntry->New.SumCompArgv)
                       ? &pEntry->Old.SumCompArgv : &pEntry->New.SumCompArgv))
        return 0;

    if (strcmp(pDigest->pszTarget, pEntry->New.pszTarget ? pEntry->New.pszTarget : pEntry->Old.pszTarget))
        return 0;

    /* match the checksums */
    pSumEntry = kOCSumIsEmpty(&pEntry->New.SumHead)
              ? &pEntry->Old.SumHead : &pEntry->New.SumHead;
    for (pSum = &pDigest->SumHead; pSum; pSum = pSum->pNext)
        if (!kOCSumHasEqualInChain(pSumEntry, pSum))
            return 0;

    return 1;
}





/**
 * The structure for the central cache entry.
 */
typedef struct KOBJCACHE
{
    /** The entry name. */
    const char *pszName;
    /** The dir that relative names in the digest are relative to. */
    char *pszDir;
    /** The absolute path. */
    char *pszAbsPath;

    /** The cache file descriptor. */
    int fd;
    /** The stream associated with fd. */
    FILE *pFile;
    /** Whether it's currently locked or not. */
    unsigned fLocked;
    /** Whether the cache file is dirty and needs writing back. */
    unsigned fDirty;
    /** Whether this is a new cache or not. */
    unsigned fNewCache;

    /** The cache file generation. */
    uint32_t uGeneration;
    /** The next valid key. (Determin at load time.) */
    uint32_t uNextKey;

    /** Number of digests in paDigests. */
    unsigned cDigests;
    /** Array of digests for the KOCENTRY objects in the cache. */
    PKOCDIGEST paDigests;

} KOBJCACHE;
/** Pointer to a cache. */
typedef KOBJCACHE *PKOBJCACHE;
/** Pointer to a const cache. */
typedef KOBJCACHE const *PCKOBJCACHE;


/**
 * Creates an empty cache.
 *
 * This doesn't touch the file system, it just create the data structure.
 *
 * @returns Pointer to a cache.
 * @param   pszCacheFile        The cache file.
 */
static PKOBJCACHE kObjCacheCreate(const char *pszCacheFile)
{
    PKOBJCACHE pCache;
    size_t off;

    /*
     * Allocate an empty entry.
     */
    pCache = xmallocz(sizeof(*pCache));
    pCache->fd = -1;

    /*
     * Setup the directory and cache file name.
     */
    pCache->pszAbsPath = AbsPath(pszCacheFile);
    pCache->pszName = FindFilenameInPath(pCache->pszAbsPath);
    off = pCache->pszName - pCache->pszAbsPath;
    if (!off)
        FatalDie("Failed to find abs path for '%s'!\n", pszCacheFile);
    pCache->pszDir = xmalloc(off);
    memcpy(pCache->pszDir, pCache->pszAbsPath, off - 1);
    pCache->pszDir[off - 1] = '\0';

    return pCache;
}


/**
 * Destroys the cache - closing any open files, freeing up heap memory and such.
 *
 * @param   pCache      The cache.
 */
static void kObjCacheDestroy(PKOBJCACHE pCache)
{
    if (pCache->pFile)
    {
        errno = 0;
        if (fclose(pCache->pFile) != 0)
            FatalMsg("fclose failed: %s\n", strerror(errno));
        pCache->pFile = NULL;
        pCache->fd = -1;
    }
    free(pCache->paDigests);
    free(pCache->pszAbsPath);
    free(pCache->pszDir);
    free(pCache);
}


/**
 * Purges the data in the cache object.
 *
 * @param   pCache      The cache object.
 */
static void kObjCachePurge(PKOBJCACHE pCache)
{
    while (pCache->cDigests > 0)
        kOCDigestPurge(&pCache->paDigests[--pCache->cDigests]);
    free(pCache->paDigests);
    pCache->paDigests = NULL;
    pCache->uGeneration = 0;
    pCache->uNextKey = 0;
}


/**
 * (Re-)reads the file.
 *
 * @param   pCache      The cache to (re)-read.
 */
static void kObjCacheRead(PKOBJCACHE pCache)
{
    unsigned i;
    char szBuf[8192];
    int fBad = 0;

    InfoMsg(4, "reading cache file...\n");

    /*
     * Rewind the file & stream, and associate a temporary buffer
     * with the stream to speed up reading.
     */
    if (lseek(pCache->fd, 0, SEEK_SET) == -1)
        FatalDie("lseek(cache-fd) failed: %s\n", strerror(errno));
    rewind(pCache->pFile);
    if (setvbuf(pCache->pFile, szBuf, _IOFBF, sizeof(szBuf)) != 0)
        FatalDie("fdopen(cache-fd,rb) failed: %s\n", strerror(errno));

    /*
     * Read magic and generation.
     */
    if (    !fgets(g_szLine, sizeof(g_szLine), pCache->pFile)
        ||  strcmp(g_szLine, "magic=kObjCache-v0.1.0\n"))
    {
        InfoMsg(2, "bad cache file (magic)\n");
        fBad = 1;
    }
    else if (    !fgets(g_szLine, sizeof(g_szLine), pCache->pFile)
             ||  strncmp(g_szLine, "generation=", sizeof("generation=") - 1))
    {
        InfoMsg(2, "bad cache file (generation)\n");
        fBad = 1;
    }
    else if (   pCache->uGeneration
             && pCache->uGeneration == atol(&g_szLine[sizeof("generation=") - 1]))
    {
        InfoMsg(3, "drop re-read unmodified cache file\n");
        fBad = 0;
    }
    else
    {
        int fBadBeforeMissing;

        /*
         * Read everything (anew).
         */
        kObjCachePurge(pCache);
        do
        {
            PKOCDIGEST pDigest;
            char *pszNl;
            char *pszVal;
            char *psz;

            /* Split the line and drop the trailing newline. */
            pszVal = strchr(g_szLine, '=');
            if ((fBad = pszVal == NULL))
                break;
            *pszVal++ = '\0';

            pszNl = strchr(pszVal, '\n');
            if (pszNl)
                *pszNl = '\0';

            /* digest '#'? */
            psz = strchr(g_szLine, '#');
            if (psz)
            {
                char *pszNext;
                i = strtoul(++psz, &pszNext, 0);
                if ((fBad = pszNext && *pszNext))
                    break;
                if ((fBad = i >= pCache->cDigests))
                    break;
                pDigest = &pCache->paDigests[i];
                *psz = '\0';
            }
            else
                pDigest = NULL;


            /* string case on value name. */
            if (!strcmp(g_szLine, "sum-#"))
            {
                KOCSUM Sum;
                if ((fBad = kOCSumInitFromString(&Sum, pszVal) != 0))
                    break;
                kOCSumAdd(&pDigest->SumHead, &Sum);
            }
            else if (!strcmp(g_szLine, "digest-abs-#"))
            {
                if ((fBad = pDigest->pszAbsPath != NULL))
                    break;
                pDigest->pszAbsPath = xstrdup(pszVal);
            }
            else if (!strcmp(g_szLine, "digest-rel-#"))
            {
                if ((fBad = pDigest->pszRelPath != NULL))
                    break;
                pDigest->pszRelPath = xstrdup(pszVal);
            }
            else if (!strcmp(g_szLine, "key-#"))
            {
                if ((fBad = pDigest->uKey != 0))
                    break;
                pDigest->uKey = strtoul(pszVal, &psz, 0);
                if ((fBad = psz && *psz))
                    break;
                if (pDigest->uKey >= pCache->uNextKey)
                    pCache->uNextKey = pDigest->uKey + 1;
            }
            else if (!strcmp(g_szLine, "comp-argv-sum-#"))
            {
                if ((fBad = !kOCSumIsEmpty(&pDigest->SumCompArgv)))
                    break;
                if ((fBad = kOCSumInitFromString(&pDigest->SumCompArgv, pszVal) != 0))
                    break;
            }
            else if (!strcmp(g_szLine, "target-#"))
            {
                if ((fBad = pDigest->pszTarget != NULL))
                    break;
                pDigest->pszTarget = xstrdup(pszVal);
            }
            else if (!strcmp(g_szLine, "digests"))
            {
                if ((fBad = pCache->paDigests != NULL))
                    break;
                pCache->cDigests = strtoul(pszVal, &psz, 0);
                if ((fBad = psz && *psz))
                    break;
                i = (pCache->cDigests + 4) & ~3;
                pCache->paDigests = xmalloc(i * sizeof(pCache->paDigests[0]));
                for (i = 0; i < pCache->cDigests; i++)
                    kOCDigestInit(&pCache->paDigests[i]);
            }
            else if (!strcmp(g_szLine, "generation"))
            {
                if ((fBad = pCache->uGeneration != 0))
                    break;
                pCache->uGeneration = strtoul(pszVal, &psz, 0);
                if ((fBad = psz && *psz))
                    break;
            }
            else if (!strcmp(g_szLine, "the-end"))
            {
                fBad = strcmp(pszVal, "fine");
                break;
            }
            else
            {
                fBad = 1;
                break;
            }
        } while (fgets(g_szLine, sizeof(g_szLine), pCache->pFile));

        /*
         * Did we find everything?
         */
        fBadBeforeMissing = fBad;
        if (    !fBad
            &&  !pCache->uGeneration)
            fBad = 1;
        if (!fBad)
            for (i = 0; i < pCache->cDigests; i++)
            {
                if ((fBad = kOCSumIsEmpty(&pCache->paDigests[i].SumCompArgv)))
                    break;
                if ((fBad = kOCSumIsEmpty(&pCache->paDigests[i].SumHead)))
                    break;
                if ((fBad = pCache->paDigests[i].uKey == 0))
                    break;
                if ((fBad = pCache->paDigests[i].pszAbsPath == NULL
                         && pCache->paDigests[i].pszRelPath == NULL))
                    break;
                if ((fBad = pCache->paDigests[i].pszTarget == NULL))
                    break;
                InfoMsg(4, "digest-%u: %s\n", i, pCache->paDigests[i].pszAbsPath
                        ? pCache->paDigests[i].pszAbsPath : pCache->paDigests[i].pszRelPath);
            }
        if (fBad)
            InfoMsg(2, "bad cache file (%s)\n", fBadBeforeMissing ? g_szLine : "missing stuff");
        else if (ferror(pCache->pFile))
        {
            InfoMsg(2, "cache file read error\n");
            fBad = 1;
        }
    }
    if (fBad)
    {
        kObjCachePurge(pCache);
        pCache->fNewCache = 1;
    }

    /*
     * Disassociate the buffer from the stream changing
     * it to non-buffered mode.
     */
    if (setvbuf(pCache->pFile, NULL, _IONBF, 0) != 0)
        FatalDie("setvbuf(,0,,0) failed: %s\n", strerror(errno));
}


/**
 * Re-writes the cache file.
 *
 * @param   pCache      The cache to commit and unlock.
 */
static void kObjCacheWrite(PKOBJCACHE pCache)
{
    unsigned i;
    off_t cb;
    char szBuf[8192];
    assert(pCache->fLocked);
    assert(pCache->fDirty);

    /*
     * Rewind the file & stream, and associate a temporary buffer
     * with the stream to speed up the writing.
     */
    if (lseek(pCache->fd, 0, SEEK_SET) == -1)
        FatalDie("lseek(cache-fd) failed: %s\n", strerror(errno));
    rewind(pCache->pFile);
    if (setvbuf(pCache->pFile, szBuf, _IOFBF, sizeof(szBuf)) != 0)
        FatalDie("setvbuf failed: %s\n", strerror(errno));

    /*
     * Write the header.
     */
    pCache->uGeneration++;
    fprintf(pCache->pFile,
            "magic=kObjCache-v0.1.0\n"
            "generation=%d\n"
            "digests=%d\n",
            pCache->uGeneration,
            pCache->cDigests);

    /*
     * Write the digests.
     */
    for (i = 0; i < pCache->cDigests; i++)
    {
        PCKOCDIGEST pDigest = &pCache->paDigests[i];
        PKOCSUM pSum;

        if (pDigest->pszAbsPath)
            fprintf(pCache->pFile, "digest-abs-#%u=%s\n", i, pDigest->pszAbsPath);
        if (pDigest->pszRelPath)
            fprintf(pCache->pFile, "digest-rel-#%u=%s\n", i, pDigest->pszRelPath);
        fprintf(pCache->pFile, "key-#%u=%u\n", i, pDigest->uKey);
        fprintf(pCache->pFile, "target-#%u=%s\n", i, pDigest->pszTarget);
        fprintf(pCache->pFile, "comp-argv-sum-#%u=", i);
        kOCSumFPrintf(&pDigest->SumCompArgv, pCache->pFile);
        for (pSum = &pDigest->SumHead; pSum; pSum = pSum->pNext)
        {
            fprintf(pCache->pFile, "sum-#%u=", i);
            kOCSumFPrintf(pSum, pCache->pFile);
        }
    }

    /*
     * Close the stream and unlock fhe file.
     * (Closing the stream shouldn't close the file handle IIRC...)
     */
    fprintf(pCache->pFile, "the-end=fine\n");
    errno = 0;
    if (    fflush(pCache->pFile) < 0
        ||  ferror(pCache->pFile))
    {
        int iErr = errno;
        fclose(pCache->pFile);
        UnlinkFileInDir(pCache->pszName, pCache->pszDir);
        FatalDie("Stream error occured while writing '%s' in '%s': %s\n",
                 pCache->pszName, pCache->pszDir, strerror(iErr));
    }
    if (setvbuf(pCache->pFile, NULL, _IONBF, 0) != 0)
        FatalDie("setvbuf(,0,,0) failed: %s\n", strerror(errno));

    cb = lseek(pCache->fd, 0, SEEK_CUR);
    if (cb == -1)
        FatalDie("lseek(cache-file,0,CUR) failed: %s\n", strerror(errno));
#if defined(__WIN__)
    if (_chsize(pCache->fd, cb) == -1)
#else
    if (ftruncate(pCache->fd, cb) == -1)
#endif
        FatalDie("file truncation failed: %s\n", strerror(errno));
    InfoMsg(4, "wrote '%s' in '%s', %d bytes\n", pCache->pszName, pCache->pszDir, cb);
}


/**
 * Cleans out all invalid digests.s
 *
 * This is done periodically from the unlock routine to make
 * sure we don't accidentally accumulate stale digests.
 *
 * @param   pCache      The cache to chek.
 */
static void kObjCacheClean(PKOBJCACHE pCache)
{
    unsigned i = pCache->cDigests;
    while (i-- > 0)
    {
        /*
         * Try open it and purge it if it's bad.
         * (We don't kill the entry file because that's kmk clean's job.)
         */
        PCKOCDIGEST pDigest = &pCache->paDigests[i];
        PKOCENTRY pEntry = kOCEntryCreate(kOCDigestAbsPath(pDigest, pCache->pszDir));
        kOCEntryRead(pEntry);
        if (    !kOCEntryCheck(pEntry)
            ||  !kOCDigestIsValid(pDigest, pEntry))
        {
            unsigned cLeft;
            kOCDigestPurge(pDigest);

            pCache->cDigests--;
            cLeft = pCache->cDigests - i;
            if (cLeft)
                memmove(pDigest, pDigest + 1, cLeft * sizeof(*pDigest));

            pCache->fDirty = 1;
        }
        kOCEntryDestroy(pEntry);
    }
}


/**
 * Locks the cache for exclusive access.
 *
 * This will open the file if necessary and lock the entire file
 * using the best suitable platform API (tricky).
 *
 * @param   pCache      The cache to lock.
 */
static void kObjCacheLock(PKOBJCACHE pCache)
{
    struct stat st;
#if defined(__WIN__)
    OVERLAPPED OverLapped;
#endif

    assert(!pCache->fLocked);

    /*
     * Open it?
     */
    if (pCache->fd < 0)
    {
        pCache->fd = OpenFileInDir(pCache->pszName, pCache->pszDir, O_CREAT | O_RDWR | O_BINARY, 0666);
        if (pCache->fd == -1)
        {
            MakePath(pCache->pszDir);
            pCache->fd = OpenFileInDir(pCache->pszName, pCache->pszDir, O_CREAT | O_RDWR | O_BINARY, 0666);
            if (pCache->fd == -1)
                FatalDie("Failed to create '%s' in '%s': %s\n", pCache->pszName, pCache->pszDir, strerror(errno));
        }

        pCache->pFile = fdopen(pCache->fd, "r+b");
        if (!pCache->pFile)
            FatalDie("fdopen failed: %s\n", strerror(errno));
        if (setvbuf(pCache->pFile, NULL, _IONBF, 0) != 0)
            FatalDie("setvbuf(,0,,0) failed: %s\n", strerror(errno));
    }

    /*
     * Lock it.
     */
#if defined(__WIN__)
    memset(&OverLapped, 0, sizeof(OverLapped));
    if (!LockFileEx((HANDLE)_get_osfhandle(pCache->fd), LOCKFILE_EXCLUSIVE_LOCK, 0, ~0, 0, &OverLapped))
        FatalDie("Failed to lock the cache file: Windows Error %d\n", GetLastError());
#else
    if (flock(pCache->fd, LOCK_EX) != 0)
        FatalDie("Failed to lock the cache file: %s\n", strerror(errno));
#endif
    pCache->fLocked = 1;

    /*
     * Check for new cache and read it it's an existing cache.
     *
     * There is no point in initializing a new cache until we've finished
     * compiling and has something to put into it, so we'll leave it as a
     * 0 byte file.
     */
    if (fstat(pCache->fd, &st) == -1)
        FatalDie("fstat(cache-fd) failed: %s\n", strerror(errno));
    if (st.st_size)
        kObjCacheRead(pCache);
    else
    {
        pCache->fNewCache = 1;
        InfoMsg(2, "the cache file is empty\n");
    }
}


/**
 * Unlocks the cache (without writing anything back).
 *
 * @param   pCache      The cache to unlock.
 */
static void kObjCacheUnlock(PKOBJCACHE pCache)
{
#if defined(__WIN__)
    OVERLAPPED OverLapped;
#endif
    assert(pCache->fLocked);

    /*
     * Write it back if it's dirty.
     */
    if (pCache->fDirty)
    {
        if (    pCache->cDigests >= 16
            &&  (pCache->uGeneration % 19) == 19)
            kObjCacheClean(pCache);
        kObjCacheWrite(pCache);
        pCache->fDirty = 0;
    }

    /*
     * Lock it.
     */
#if defined(__WIN__)
    memset(&OverLapped, 0, sizeof(OverLapped));
    if (!UnlockFileEx((HANDLE)_get_osfhandle(pCache->fd), 0, ~0U, 0, &OverLapped))
        FatalDie("Failed to unlock the cache file: Windows Error %d\n", GetLastError());
#else
    if (flock(pCache->fd, LOCK_UN) != 0)
        FatalDie("Failed to unlock the cache file: %s\n", strerror(errno));
#endif
    pCache->fLocked = 0;
}


/**
 * Removes the entry from the cache.
 *
 * The entry doesn't need to be in the cache.
 * The cache entry (file) itself is not touched.
 *
 * @param   pCache      The cache.
 * @param   pEntry      The entry.
 */
static void kObjCacheRemoveEntry(PKOBJCACHE pCache, PCKOCENTRY pEntry)
{
    unsigned i = pCache->cDigests;
    while (i-- > 0)
    {
        PKOCDIGEST pDigest = &pCache->paDigests[i];
        if (ArePathsIdentical(kOCDigestAbsPath(pDigest, pCache->pszDir),
                              kOCEntryAbsPath(pEntry), ~0U))
        {
            unsigned cLeft;
            kOCDigestPurge(pDigest);

            pCache->cDigests--;
            cLeft = pCache->cDigests - i;
            if (cLeft)
                memmove(pDigest, pDigest + 1, cLeft * sizeof(*pDigest));

            pCache->fDirty = 1;
            InfoMsg(3, "removing entry '%s'; %d left.\n", kOCEntryAbsPath(pEntry), pCache->cDigests);
        }
    }
}


/**
 * Inserts the entry into the cache.
 *
 * The cache entry (file) itself is not touched by this operation,
 * the pEntry object otoh is.
 *
 * @param   pCache      The cache.
 * @param   pEntry      The entry.
 */
static void kObjCacheInsertEntry(PKOBJCACHE pCache, PKOCENTRY pEntry)
{
    unsigned i;

    /*
     * Find a new key.
     */
    pEntry->uKey = pCache->uNextKey++;
    if (!pEntry->uKey)
        pEntry->uKey = pCache->uNextKey++;
    i = pCache->cDigests;
    while (i-- > 0)
        if (pCache->paDigests[i].uKey == pEntry->uKey)
        {
            pEntry->uKey = pCache->uNextKey++;
            if (!pEntry->uKey)
                pEntry->uKey = pCache->uNextKey++;
            i = pCache->cDigests;
        }

    /*
     * Reallocate the digest array?
     */
    if (    !(pCache->cDigests & 3)
        &&  (pCache->cDigests || !pCache->paDigests))
        pCache->paDigests = xrealloc(pCache->paDigests, sizeof(pCache->paDigests[0]) * (pCache->cDigests + 4));

    /*
     * Create a new digest.
     */
    kOCDigestInitFromEntry(&pCache->paDigests[pCache->cDigests], pEntry);
    pCache->cDigests++;
    InfoMsg(4, "Inserted digest #%u: %s\n", pCache->cDigests - 1, kOCEntryAbsPath(pEntry));

    pCache->fDirty = 1;
}


/**
 * Find a matching cache entry.
 */
static PKOCENTRY kObjCacheFindMatchingEntry(PKOBJCACHE pCache, PCKOCENTRY pEntry)
{
    unsigned i = pCache->cDigests;

    assert(pEntry->fNeedCompiling);
    assert(!kOCSumIsEmpty(&pEntry->New.SumCompArgv));
    assert(!kOCSumIsEmpty(&pEntry->New.SumHead));

    while (i-- > 0)
    {
        /*
         * Matching?
         */
        PCKOCDIGEST pDigest = &pCache->paDigests[i];
        if (    kOCSumIsEqual(&pDigest->SumCompArgv, &pEntry->New.SumCompArgv)
            &&  kOCSumHasEqualInChain(&pDigest->SumHead, &pEntry->New.SumHead))
        {
            /*
             * Try open it.
             */
            unsigned cLeft;
            PKOCENTRY pRetEntry = kOCEntryCreate(kOCDigestAbsPath(pDigest, pCache->pszDir));
            kOCEntryRead(pRetEntry);
            if (    kOCEntryCheck(pRetEntry)
                &&  kOCDigestIsValid(pDigest, pRetEntry))
                return pRetEntry;
            kOCEntryDestroy(pRetEntry);

            /* bad entry, purge it. */
            InfoMsg(3, "removing bad digest '%s'\n", kOCDigestAbsPath(pDigest, pCache->pszDir));
            kOCDigestPurge(pDigest);

            pCache->cDigests--;
            cLeft = pCache->cDigests - i;
            if (cLeft)
                memmove(pDigest, pDigest + 1, cLeft * sizeof(*pDigest));

            pCache->fDirty = 1;
        }
    }

    return NULL;
}


/**
 * Is this a new cache?
 *
 * @returns 1 if new, 0 if not new.
 * @param   pEntry      The entry.
 */
static int kObjCacheIsNew(PKOBJCACHE pCache)
{
    return pCache->fNewCache;
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
    printf("syntax: kObjCache [--kObjCache-options] [-v|--verbose]\n"
           "            <  [-c|--cache-file <cache-file>]\n"
           "             | [-n|--name <name-in-cache>] [[-d|--cache-dir <cache-dir>]] >\n"
           "            <-f|--file <local-cache-file>>\n"
           "            <-t|--target <target-name>>\n"
           "            [-r|--redir-stdout] [-p|--passthru]\n"
           "            --kObjCache-cpp <filename> <precompiler + args>\n"
           "            --kObjCache-cc <object> <compiler + args>\n"
           "            [--kObjCache-both [args]]\n"
           "            [--kObjCache-cpp|--kObjCache-cc [more args]]\n"
           "        kObjCache <-V|--version>\n"
           "        kObjCache [-?|/?|-h|/h|--help|/help]\n"
           "\n"
           "The env.var. KOBJCACHE_DIR sets the default cache diretory (-d).\n"
           "The env.var. KOBJCACHE_OPTS allow you to specifie additional options\n"
           "without having to mess with the makefiles. These are appended with "
           "a --kObjCache-options between them and the command args.\n"
           "\n");
    return 0;
}


int main(int argc, char **argv)
{
    PKOBJCACHE pCache;
    PKOCENTRY pEntry;

    const char *pszCacheDir = getenv("KOBJCACHE_DIR");
    const char *pszCacheName = NULL;
    const char *pszCacheFile = NULL;
    const char *pszEntryFile = NULL;

    const char **papszArgvPreComp = NULL;
    unsigned cArgvPreComp = 0;
    const char *pszPreCompName = NULL;
    int fRedirPreCompStdOut = 0;

    const char **papszArgvCompile = NULL;
    unsigned cArgvCompile = 0;
    const char *pszObjName = NULL;
    int fRedirCompileStdIn = 0;

    const char *pszTarget = NULL;

    enum { kOC_Options, kOC_CppArgv, kOC_CcArgv, kOC_BothArgv } enmMode = kOC_Options;

    size_t cch;
    char *psz;
    int i;

    SetErrorPrefix("kObjCache");

    /*
     * Arguments passed in the environmnet?
     */
    psz = getenv("KOBJCACHE_OPTS");
    if (psz)
        AppendArgs(&argc, &argv, psz, "--kObjCache-options");

    /*
     * Parse the arguments.
     */
    if (argc <= 1)
        return usage();
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
        else if (!strcmp(argv[i], "--kObjCache-options"))
            enmMode = kOC_Options;
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
        else if (!strcmp(argv[i], "-f") || !strcmp(argv[i], "--entry-file"))
        {
            if (i + 1 >= argc)
                return SyntaxError("%s requires a cache entry filename!\n", argv[i]);
            pszEntryFile = argv[++i];
        }
        else if (!strcmp(argv[i], "-c") || !strcmp(argv[i], "--cache-file"))
        {
            if (i + 1 >= argc)
                return SyntaxError("%s requires a cache filename!\n", argv[i]);
            pszCacheFile = argv[++i];
        }
        else if (!strcmp(argv[i], "-n") || !strcmp(argv[i], "--name"))
        {
            if (i + 1 >= argc)
                return SyntaxError("%s requires a cache name!\n", argv[i]);
            pszCacheName = argv[++i];
        }
        else if (!strcmp(argv[i], "-d") || !strcmp(argv[i], "--cache-dir"))
        {
            if (i + 1 >= argc)
                return SyntaxError("%s requires a cache directory!\n", argv[i]);
            pszCacheDir = argv[++i];
        }
        else if (!strcmp(argv[i], "-t") || !strcmp(argv[i], "--target"))
        {
            if (i + 1 >= argc)
                return SyntaxError("%s requires a target platform/arch name!\n", argv[i]);
            pszTarget = argv[++i];
        }
        else if (!strcmp(argv[i], "-p") || !strcmp(argv[i], "--passthru"))
            fRedirPreCompStdOut = fRedirCompileStdIn = 1;
        else if (!strcmp(argv[i], "-r") || !strcmp(argv[i], "--redir-stdout"))
            fRedirPreCompStdOut = 1;
        else if (!strcmp(argv[i], "-v") || !strcmp(argv[i], "--verbose"))
            g_cVerbosityLevel++;
        else if (!strcmp(argv[i], "-q") || !strcmp(argv[i], "--quiet"))
            g_cVerbosityLevel = 0;
        else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "-?")
              || !strcmp(argv[i], "/h") || !strcmp(argv[i], "/?") || !strcmp(argv[i], "/help"))
            return usage();
        else if (!strcmp(argv[i], "-V") || !strcmp(argv[i], "--version"))
        {
            printf("kObjCache v0.1.0 ($Revision: 1053 $)\n");
            return 0;
        }
        else
            return SyntaxError("Doesn't grok '%s'!\n", argv[i]);
    }
    if (!pszEntryFile)
        return SyntaxError("No cache entry filename (-f)!\n");
    if (!pszTarget)
        return SyntaxError("No target name (-t)!\n");
    if (!cArgvCompile)
        return SyntaxError("No compiler arguments (--kObjCache-cc)!\n");
    if (!cArgvPreComp)
        return SyntaxError("No precompiler arguments (--kObjCache-cc)!\n");

    /*
     * Calc the cache file name.
     * It's a bit messy since the extension has to be replaced.
     */
    if (!pszCacheFile)
    {
        if (!pszCacheDir)
            return SyntaxError("No cache dir (-d / KOBJCACHE_DIR) and no cache filename!\n");
        if (!pszCacheName)
        {
            psz = (char *)FindFilenameInPath(pszEntryFile);
            if (!*psz)
                return SyntaxError("The cache file (-f) specifies a directory / nothing!\n");
            cch = psz - pszEntryFile;
            pszCacheName = memcpy(xmalloc(cch + 5), psz, cch + 1);
            psz = strrchr(pszCacheName, '.');
            if (!psz || psz <= pszCacheName)
                psz = (char *)pszCacheName + cch;
            memcpy(psz, ".koc", sizeof(".koc") - 1);
        }
        pszCacheFile = MakePathFromDirAndFile(pszCacheName, pszCacheDir);
    }

    /*
     * Create and initialize the two objects we'll be working on.
     *
     * We're supposed to be the only ones actually writing to the local file,
     * so it's perfectly fine to read it here before we lock it. This simplifies
     * the detection of object name and compiler argument changes.
     */
    SetErrorPrefix("kObjCache - %s", FindFilenameInPath(pszCacheFile));
    pCache = kObjCacheCreate(pszCacheFile);

    pEntry = kOCEntryCreate(pszEntryFile);
    kOCEntryRead(pEntry);
    kOCEntrySetCompileObjName(pEntry, pszObjName);
    kOCEntrySetCompileArgv(pEntry, papszArgvCompile, cArgvCompile);
    kOCEntrySetTarget(pEntry, pszTarget);
    kOCEntrySetCppName(pEntry, pszPreCompName);
    kOCEntrySetPipedMode(pEntry, fRedirPreCompStdOut, fRedirCompileStdIn);

    /*
     * Open (& lock) the two files and do validity checks and such.
     */
    kObjCacheLock(pCache);
    if (    kObjCacheIsNew(pCache)
        &&  kOCEntryNeedsCompiling(pEntry))
    {
        /*
         * Both files are missing/invalid.
         * Optimize this path as it is frequently used when making a clean build.
         */
        kObjCacheUnlock(pCache);
        InfoMsg(1, "doing full compile\n");
        kOCEntryPreCompileAndCompile(pEntry, papszArgvPreComp, cArgvPreComp);
        kObjCacheLock(pCache);
    }
    else
    {
        /*
         * Do the precompile (don't need to lock the cache file for this).
         */
        kObjCacheUnlock(pCache);
        kOCEntryPreCompile(pEntry, papszArgvPreComp, cArgvPreComp);

        /*
         * Check if we need to recompile. If we do, try see if the is a cache entry first.
         */
        kOCEntryCalcRecompile(pEntry);
        if (kOCEntryNeedsCompiling(pEntry))
        {
            PKOCENTRY pUseEntry;
            kObjCacheLock(pCache);
            kObjCacheRemoveEntry(pCache, pEntry);
            pUseEntry = kObjCacheFindMatchingEntry(pCache, pEntry);
            if (pUseEntry)
            {
                InfoMsg(1, "using cache entry '%s'\n", kOCEntryAbsPath(pUseEntry));
                kOCEntryCopy(pEntry, pUseEntry);
                kOCEntryDestroy(pUseEntry);
            }
            else
            {
                kObjCacheUnlock(pCache);
                InfoMsg(1, "recompiling\n");
                kOCEntryCompileIt(pEntry);
                kObjCacheLock(pCache);
            }
        }
        else
        {
            InfoMsg(1, "no need to recompile\n");
            kObjCacheLock(pCache);
        }
    }

    /*
     * Update the cache files.
     */
    kObjCacheRemoveEntry(pCache, pEntry);
    kObjCacheInsertEntry(pCache, pEntry);
    kOCEntryWrite(pEntry);
    kObjCacheUnlock(pCache);
    kObjCacheDestroy(pCache);
    return 0;
}


/** @page kObjCache Benchmarks.
 *
 * (2007-06-10)
 *
 * Mac OS X debug -j 3 cached clobber build (rm -Rf out ; sync ; svn diff ; sync ; sleep 1 ; time kmk -j 3 USE_KOBJCACHE=1):
 *  real    11m28.811s
 *  user    13m59.291s
 *  sys     3m24.590s
 *
 * Mac OS X debug -j 3 cached depend build [cdefs.h] (touch include/iprt/cdefs.h ; sync ; svn diff ; sync ; sleep 1 ; time kmk -j 3 USE_KOBJCACHE=1):
 *  real    1m26.895s
 *  user    1m26.971s
 *  sys     0m32.532s
 *
 * Mac OS X debug -j 3 cached depend build [err.h] (touch include/iprt/err.h ; sync ; svn diff ; sync ; sleep 1 ; time kmk -j 3 USE_KOBJCACHE=1):
 *  real    1m18.049s
 *  user    1m20.462s
 *  sys     0m27.887s
 *
 * Mac OS X release -j 3 cached clobber build (rm -Rf out/darwin.x86/release ; sync ; svn diff ; sync ; sleep 1 ; time kmk -j 3 USE_KOBJCACHE=1 BUILD_TYPE=release):
 *  real    13m27.751s
 *  user    18m12.654s
 *  sys     3m25.170s
 *
 * Mac OS X profile -j 3 cached clobber build (rm -Rf out/darwin.x86/profile ; sync ; svn diff ; sync ; sleep 1 ; time kmk -j 3 USE_KOBJCACHE=1 BUILD_TYPE=profile):
 *  real    9m9.720s
 *  user    8m53.005s
 *  sys     2m13.110s
 *
 * Mac OS X debug -j 3 clobber build (rm -Rf out/darwin.x86/debug ; sync ; svn diff ; sync ; sleep 1 ; time kmk -j 3 BUILD_TYPE=debug):
 *  real    10m18.129s
 *  user    12m52.687s
 *  sys     2m51.277s
 *
 * Mac OS X debug -j 3 debug build [cdefs.h] (touch include/iprt/cdefs.h ; sync ; svn diff ; sync ; sleep 1 ; time kmk -j 3 BUILD_TYPE=debug):
 *  real    4m46.147s
 *  user    5m27.087s
 *  sys     1m11.775s
 *
 * Mac OS X debug -j 3 debug build [err.h] (touch include/iprt/cdefs.h ; sync ; svn diff ; sync ; sleep 1 ; time kmk -j 3 BUILD_TYPE=debug):
 *  real    4m17.572s
 *  user    5m7.450s
 *  sys     1m3.450s
 *
 * Mac OS X release -j 3 clobber build (rm -Rf out/darwin.x86/release ; sync ; svn diff ; sync ; sleep 1 ; time kmk -j 3 BUILD_TYPE=release):
 *  real    12m14.742s
 *  user    17m11.794s
 *  sys     2m51.454s
 *
 * Mac OS X profile -j 3 clobber build (rm -Rf out/darwin.x86/profile ; sync ; svn diff ; sync ; sleep 1 ; time kmk -j 3 BUILD_TYPE=profile):
 *  real    12m33.821s
 *  user    17m35.086s
 *  sys     2m53.312s
 *
 * Note. The profile build can pick object files from the release build.
 * (all with KOBJCACHE_OPTS=-v; which means a bit more output and perhaps a second or two slower.)
 */

