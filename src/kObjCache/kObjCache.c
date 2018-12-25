/* $Id: kObjCache.c 3239 2018-12-25 20:47:30Z knut.osmundsen@oracle.com $ */
/** @file
 * kObjCache - Object Cache.
 */

/*
 * Copyright (c) 2007-2012 knut st. osmundsen <bird-kBuild-spamx@anduin.net>
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
#if 0
# define ELECTRIC_HEAP
# include "../kmk/electric.h"
# include "../kmk/electric.c"
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
# ifdef _MAX_PATH
#  define PATH_MAX _MAX_PATH /* windows */
# else
#  define PATH_MAX 4096 /* gnu hurd */
# endif
#endif
#if defined(__OS2__) || defined(__WIN__)
# include <process.h>
# include <io.h>
# ifdef __OS2__
#  include <unistd.h>
#  include <sys/wait.h>
#  include <sys/time.h>
# endif
# if defined(_MSC_VER)
#  include <direct.h>
   typedef intptr_t pid_t;
# endif
# ifndef _P_WAIT
#  define _P_WAIT   P_WAIT
# endif
# ifndef _P_NOWAIT
#  define _P_NOWAIT P_NOWAIT
# endif
#else
# include <unistd.h>
# include <sys/wait.h>
# include <sys/time.h>
# ifndef O_BINARY
#  define O_BINARY 0
# endif
# ifndef __sun__
#  include <sys/file.h> /* flock */
# endif
#endif
#if defined(__WIN__)
# include <Windows.h>
# include "quoted_spawn.h"
#endif
#if defined(__HAIKU__)
# include <posix/sys/file.h>
#endif

#include "crc32.h"
#include "md5.h"
#include "kDep.h"


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

#define MY_IS_BLANK(a_ch)   ((a_ch) == ' ' || (a_ch) == '\t')

#define KOC_BUF_MIN         KOC_BUF_ALIGNMENT
#define KOC_BUF_INCR        KOC_BUF_ALIGNMENT
#define KOC_BUF_ALIGNMENT   (4U*1024U*1024U)


/*******************************************************************************
*   Global Variables                                                           *
*******************************************************************************/
/** Whether verbose output is enabled. */
static unsigned g_cVerbosityLevel = 0;
/** What to prefix the errors with. */
static char g_szErrorPrefix[128];

/** Read buffer shared by the cache components. */
static char g_szLine[KOBJCACHE_MAX_LINE_LEN + 16];

/** How many times we've moved memory around. */
static size_t g_cMemMoves = 0;
/** How much memory we've moved. */
static size_t g_cbMemMoved = 0;


/*******************************************************************************
*   Internal Functions                                                         *
*******************************************************************************/
static char *MakePathFromDirAndFile(const char *pszName, const char *pszDir);
static char *CalcRelativeName(const char *pszPath, const char *pszDir);
static FILE *FOpenFileInDir(const char *pszName, const char *pszDir, const char *pszMode);
static int   UnlinkFileInDir(const char *pszName, const char *pszDir);
static int   RenameFileInDir(const char *pszOldName, const char *pszNewName, const char *pszDir);
static int   DoesFileInDirExist(const char *pszName, const char *pszDir);
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


#if 0 /* unused */
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
#endif /* unused */


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
    char *psz;
    if (pszIn)
    {
        psz = strdup(pszIn);
        if (!psz)
            FatalDie("out of memory (%d)\n", (int)strlen(pszIn));
    }
    else
        psz = NULL;
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
 * Returns a millisecond timestamp.
 *
 * @returns Millisecond timestamp.
 */
static uint32_t NowMs(void)
{
#if defined(__WIN__)
    return GetTickCount();
#else
    int             iSavedErrno = errno;
    struct timeval  tv          = {0, 0};

    gettimeofday(&tv, NULL);
    errno = iSavedErrno;

    return tv.tv_sec * 1000 + tv.tv_usec / 1000;
#endif
}


/**
 * Gets the absolute path
 *
 * @returns A new heap buffer containing the absolute path.
 * @param   pszPath     The path to make absolute. (Readonly)
 */
static char *AbsPath(const char *pszPath)
{
/** @todo this isn't really working as it should... */
    char szTmp[PATH_MAX];
#if defined(__OS2__)
    if (    _fullpath(szTmp, *pszPath ? pszPath : ".", sizeof(szTmp))
        &&  !realpath(pszPath, szTmp))
        return xstrdup(pszPath);
#elif defined(__WIN__)
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
    if (pszFilename < pszPath)
        return pszPath;
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
 */
static int ArePathsIdentical(const char *pszPath1, const char *pszPath2)
{
#if defined(__OS2__) || defined(__WIN__)
    if (stricmp(pszPath1, pszPath2))
    {
        /* Slashes may differ, compare char by char. */
        const char *psz1 = pszPath1;
        const char *psz2 = pszPath2;
        for (;;)
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
            if (!*psz1)
                break;
            psz1++;
            psz2++;
        }
    }
    return 1;
#else
    return !strcmp(pszPath1, pszPath2);
#endif
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
static int ArePathsIdenticalN(const char *pszPath1, const char *pszPath2, size_t cch)
{
#if defined(__OS2__) || defined(__WIN__)
    if (strnicmp(pszPath1, pszPath2, cch))
    {
        /* Slashes may differ, compare char by char. */
        const char *psz1 = pszPath1;
        const char *psz2 = pszPath2;
        for ( ; cch; psz1++, psz2++, cch--)
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
    if (ArePathsIdenticalN(pszPath, pszDir, cchDir))
    {
        if (pszPath[cchDir])
            pszRet = (char *)pszPath + cchDir;
        else
            pszRet = "./";
    }
    else
    {
        pszAbsPath = AbsPath(pszPath);
        if (ArePathsIdenticalN(pszAbsPath, pszDir, cchDir))
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
            &&  errno != EEXIST
            &&  errno != ENOSYS /* Solaris nonsensical mkdir crap. */
            &&  errno != EACCES /* Solaris nonsensical mkdir crap. */
            )
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


/**
 * Dependency collector state.
 */
typedef struct KOCDEP
{
    /** The statemachine for processing the preprocessed code stream. */
    enum KOCDEPSTATE
    {
        kOCDepState_Invalid = 0,
        kOCDepState_NeedNewLine,
        kOCDepState_NeedHash,
        kOCDepState_NeedLine_l,
        kOCDepState_NeedLine_l_HaveSpace,
        kOCDepState_NeedLine_i,
        kOCDepState_NeedLine_n,
        kOCDepState_NeedLine_e,
        kOCDepState_NeedSpaceBeforeDigit,
        kOCDepState_NeedFirstDigit,
        kOCDepState_NeedMoreDigits,
        kOCDepState_NeedQuote,
        kOCDepState_NeedEndQuote
    }   enmState;
    /** Current offset into the filename buffer. */
    uint32_t offFilename;
    /** The amount of space currently allocated for the filename buffer. */
    uint32_t cbFilenameAlloced;
    /** Pointer to the filename buffer. */
    char *pszFilename;
    /** The current dependency file. */
    PDEP pCurDep;
    /** The core dependency collector state. */
    DEPGLOBALS Core;
} KOCDEP;
/** Pointer to a KOCDEP.  */
typedef KOCDEP *PKOCDEP;


/**
 * Initializes the dependency collector state.
 *
 * @param   pDepState       The dependency collector state.
 */
static void kOCDepInit(PKOCDEP pDepState)
{
    pDepState->enmState = kOCDepState_NeedHash;
    pDepState->offFilename = 0;
    pDepState->cbFilenameAlloced = 0;
    pDepState->pszFilename = NULL;
    pDepState->pCurDep = NULL;
    depInit(&pDepState->Core);
}


/**
 * Deletes the dependency collector state, releasing all resources.
 *
 * @param   pDepState       The dependency collector state.
 */
static void kOCDepDelete(PKOCDEP pDepState)
{
    pDepState->enmState = kOCDepState_Invalid;
    free(pDepState->pszFilename);
    pDepState->pszFilename = NULL;
    depCleanup(&pDepState->Core);
}


/**
 * Unescapes a string in place.
 *
 * @returns The new string length.
 * @param   psz             The string to unescape (input and output).
 */
static size_t kOCDepUnescape(char *psz)
{
    char *pszSrc = psz;
    char *pszDst = psz;
    char ch;

    while ((ch = *pszSrc++) != '\0')
    {
        if (ch == '\\')
        {
            char ch2 = *pszSrc;
            if (ch2)
            {
                pszSrc++;
                ch = ch2;
            }
            /* else: cannot happen / just ignore */
        }
        *pszDst++ = ch;
    }

    *pszDst = '\0';
    return pszDst - psz;
}


/**
 * Checks if the character at @a offChar is escaped or not.
 *
 * @returns 1 if escaped, 0 if not.
 * @param   pach            The string (not terminated).
 * @param   offChar         The offset of the character in question.
 */
static int kOCDepIsEscaped(char *pach, size_t offChar)
{
    while (offChar > 0 && pach[offChar - 1] == '\\')
    {
        if (   offChar == 1
            || pach[offChar - 2] != '\\')
            return 1;
        offChar -= 2;
    }
    return 0;
}


static void kOCDepEnter(PKOCDEP pDepState, const char *pszUnescFilename, size_t cchFilename)
{
    if (cchFilename + 1 >= pDepState->cbFilenameAlloced)
    {
        pDepState->cbFilenameAlloced = (cchFilename + 1 + 15) & ~15;
        pDepState->pszFilename = (char *)xrealloc(pDepState->pszFilename, pDepState->cbFilenameAlloced);
    }

    memcpy(pDepState->pszFilename, pszUnescFilename, cchFilename);
    pDepState->pszFilename[cchFilename] = '\0';
    cchFilename = kOCDepUnescape(pDepState->pszFilename);

    if (   !pDepState->pCurDep
        || cchFilename != pDepState->pCurDep->cchFilename
        || strcmp(pDepState->pszFilename, pDepState->pCurDep->szFilename))
        pDepState->pCurDep = depAdd(&pDepState->Core, pDepState->pszFilename, cchFilename);
}


/**
 * This consumes the preprocessor output and generate dependencies from it.
 *
 * The trick is to look at the line directives and which files get listed there.
 *
 * @returns The new state. This is a convenience for saving code space and it
 *          isn't really meant to be of any use to the caller.
 * @param   pDepState       The dependency collector state.
 * @param   pszInput        The input.
 * @param   cchInput        The input length.
 */
static enum KOCDEPSTATE
kOCDepConsumer(PKOCDEP pDepState, const char *pszInput, size_t cchInput)
{
    enum KOCDEPSTATE enmState = pDepState->enmState;
    const char *psz;

    while (cchInput > 0)
    {
        switch (enmState)
        {
            case kOCDepState_NeedNewLine:
                psz = (const char *)memchr(pszInput, '\n', cchInput);
                if (!psz)
                    return enmState;
                psz++;
                cchInput -= psz - pszInput;
                pszInput = psz;
                /* fall thru */

            case kOCDepState_NeedHash:
                while (cchInput > 0 && MY_IS_BLANK(*pszInput))
                    cchInput--, pszInput++;
                if (!cchInput)
                    return pDepState->enmState = kOCDepState_NeedHash;

                if (*pszInput != '#')
                    break;
                pszInput++;
                cchInput--;
                enmState = kOCDepState_NeedLine_l;
                /* fall thru */

            case kOCDepState_NeedLine_l:
            case kOCDepState_NeedLine_l_HaveSpace:
                while (cchInput > 0 && MY_IS_BLANK(*pszInput))
                {
                    enmState = kOCDepState_NeedLine_l_HaveSpace;
                    cchInput--, pszInput++;
                }
                if (!cchInput)
                    return pDepState->enmState = enmState;

                if (*pszInput != 'l')
                {
                    /* # <digit> "<file>" */
                    if (enmState != kOCDepState_NeedLine_l_HaveSpace || !isdigit(*pszInput))
                        break;
                    pszInput++;
                    cchInput--;
                    enmState = kOCDepState_NeedMoreDigits;
                    continue;
                }
                pszInput++;
                if (!--cchInput)
                    return pDepState->enmState = kOCDepState_NeedLine_i;
                /* fall thru */

            case kOCDepState_NeedLine_i:
                if (*pszInput != 'i')
                    break;
                pszInput++;
                if (!--cchInput)
                    return pDepState->enmState = kOCDepState_NeedLine_n;
                /* fall thru */

            case kOCDepState_NeedLine_n:
                if (*pszInput != 'n')
                    break;
                pszInput++;
                if (!--cchInput)
                    return pDepState->enmState = kOCDepState_NeedLine_e;
                /* fall thru */

            case kOCDepState_NeedLine_e:
                if (*pszInput != 'e')
                    break;
                pszInput++;
                if (!--cchInput)
                    return pDepState->enmState = kOCDepState_NeedSpaceBeforeDigit;
                /* fall thru */

            case kOCDepState_NeedSpaceBeforeDigit:
                if (!MY_IS_BLANK(*pszInput))
                    break;
                pszInput++;
                cchInput--;
                /* fall thru */

            case kOCDepState_NeedFirstDigit:
                while (cchInput > 0 && MY_IS_BLANK(*pszInput))
                    cchInput--, pszInput++;
                if (!cchInput)
                    return pDepState->enmState = kOCDepState_NeedFirstDigit;

                if (!isdigit(*pszInput))
                    break;
                pszInput++;
                cchInput--;
                /* fall thru */

            case kOCDepState_NeedMoreDigits:
                while (cchInput > 0 && isdigit(*pszInput))
                    cchInput--, pszInput++;
                if (!cchInput)
                    return pDepState->enmState = kOCDepState_NeedMoreDigits;
                /* fall thru */

            case kOCDepState_NeedQuote:
                while (cchInput > 0 && MY_IS_BLANK(*pszInput))
                    cchInput--, pszInput++;
                if (!cchInput)
                    return pDepState->enmState = kOCDepState_NeedQuote;

                if (*pszInput != '"')
                    break;
                pszInput++;
                cchInput--;
                /* fall thru */

            case kOCDepState_NeedEndQuote:
            {
                uint32_t off = pDepState->offFilename;
                for (;;)
                {
                    char ch;

                    if (!cchInput)
                    {
                        pDepState->offFilename = off;
                        return pDepState->enmState = kOCDepState_NeedEndQuote;
                    }

                    if (off + 1 >= pDepState->cbFilenameAlloced)
                    {
                        if (!pDepState->cbFilenameAlloced)
                            pDepState->cbFilenameAlloced = 32;
                        else
                            pDepState->cbFilenameAlloced *= 2;
                        pDepState->pszFilename = (char *)xrealloc(pDepState->pszFilename, pDepState->cbFilenameAlloced);
                    }
                    pDepState->pszFilename[off] = ch = *pszInput++;
                    cchInput--;

                    if (   ch == '"'
                        && (   off == 0
                            || pDepState->pszFilename[off - 1] != '\\'
                            || !kOCDepIsEscaped(pDepState->pszFilename, off)))
                    {
                        /* Done, unescape and add the file. */
                        size_t cchFilename;

                        pDepState->pszFilename[off] = '\0';
                        cchFilename = kOCDepUnescape(pDepState->pszFilename);

                        if (   !pDepState->pCurDep
                            || cchFilename != pDepState->pCurDep->cchFilename
                            || strcmp(pDepState->pszFilename, pDepState->pCurDep->szFilename))
                            pDepState->pCurDep = depAdd(&pDepState->Core, pDepState->pszFilename, cchFilename);
                        pDepState->offFilename = 0;
                        break;
                    }

                    off++;
                }
            }
            /* fall thru */

            case kOCDepState_Invalid:
                assert(0);
                break;
        }

        /* next newline */
        enmState = kOCDepState_NeedNewLine;
    }

    return pDepState->enmState = enmState;
}


/**
 * Writes the dependencies to the specified file.
 *
 * @param   pDepState       The dependency collector state.
 * @param   pszFilename     The name of the dependency file.
 * @param   pszObjFile      The object file name, relative to @a pszObjDir.
 * @param   pszObjDir       The object file directory.
 * @param   fFixCase        Whether to fix the case of dependency files.
 * @param   fQuiet          Whether to be quiet about the dependencies.
 * @param   fGenStubs       Whether to generate stubs.
 */
static void kOCDepWriteToFile(PKOCDEP pDepState, const char *pszFilename, const char *pszObjFile, const char *pszObjDir,
                              int fFixCase, int fQuiet, int fGenStubs)
{
    char *pszObjFileAbs;
    char *psz;
    FILE *pFile = fopen(pszFilename, "w");
    if (!pFile)
        FatalMsg("Failed to open dependency file '%s': %s\n", pszFilename, strerror(errno));

    depOptimize(&pDepState->Core, fFixCase, fQuiet, NULL /*pszIgnoredExt*/);

    /* Make object file name with unix slashes. */
    pszObjFileAbs = MakePathFromDirAndFile(pszObjFile, pszObjDir);
    psz = pszObjFileAbs;
    while ((psz = strchr(psz, '\\')) != NULL)
        *psz++ = '/';

    fprintf(pFile, "%s:", pszObjFileAbs);
    free(pszObjFileAbs);
    depPrint(&pDepState->Core, pFile);
    if (fGenStubs)
        depPrintStubs(&pDepState->Core, pFile);

    if (fclose(pFile) != 0)
        FatalMsg("Failed to write dependency file '%s': %s\n", pszFilename, strerror(errno));
}


/**
 * Preprocessor output reader state.
 */
typedef struct KOCCPPRD
{
    /** Pointer to the preprocessor output. */
    char *pszBuf;
    /** Allocated buffer size. */
    size_t cbBufAlloc;
    /** Amount preprocessor output that we've completed optimizations for. */
    size_t cchDstOptimized;
    /** Offset to the start of the unoptimized source. */
    size_t offSrcUnoptimized;
    /** The offset of the next bits to process. */
    size_t offSrcCur;
    /** The offset where to put more raw preprocessor output. */
    size_t offSrcRead;
    /** The line number corresponding to offOptimized. */
    uint32_t uOptLineNo;
    /** The current line number. */
    uint32_t uCurLineNo;
    /** Set if we're done, clear if we're expecting more preprocessor output. */
    int fDone;
    /** The saved character at cchOptimized. */
    char chSaved;
    /** Whether the optimizations are enabled. */
    int fOptimize;

    /** Buffer holding the current file name (unescaped). */
    char *pszFileNmBuf;
    /** The size of the file name buffer. */
    size_t cbFileNmBuf;
    /** The length of the current file string. */
    size_t cchCurFileNm;

    /** Line directive / new line sequence buffer. */
    char *pszLineBuf;
    /** The size of the buffer pointed to by pszLineBuf. */
    size_t cbLineBuf;

    /** Set if we should work the dependency generator as well. */
    PKOCDEP pDepState;
} KOCCPPRD;
/** Pointer to a preprocessor reader state. */
typedef KOCCPPRD *PKOCCPPRD;


/**
 * Allocate the initial C preprocessor output buffer.
 *
 * @param   pCppRd      The C preprocessor reader instance.
 * @param   cbOldCpp    The size of the output the last time.  This is 0 if
 *                      there was not previous run.
 * @param   fOptimize   Whether optimizations are enabled.
 * @param   pDepState   Pointer to the dependency generator.  Must only be set
 *                      if @a fOptimize is also set.
 */
static void kOCCppRdInit(PKOCCPPRD pCppRd, size_t cbOldCpp, int fOptimize, PKOCDEP pDepState)
{
    assert(!pDepState || fOptimize);

    pCppRd->cbBufAlloc = cbOldCpp ? (cbOldCpp + KOC_BUF_INCR) & ~(KOC_BUF_ALIGNMENT - 1) : KOC_BUF_MIN;
    pCppRd->pszBuf = xmalloc(pCppRd->cbBufAlloc);
    pCppRd->cchCurFileNm = 0;
    pCppRd->cchDstOptimized = 0;
    pCppRd->offSrcUnoptimized = 0;
    pCppRd->offSrcCur = 0;
    pCppRd->offSrcRead = 0;
    pCppRd->uOptLineNo = 1;
    pCppRd->uCurLineNo = 1;
    pCppRd->fDone = 0;
    pCppRd->chSaved = 0;
    pCppRd->fOptimize = fOptimize;

    pCppRd->pszFileNmBuf = NULL;
    pCppRd->cbFileNmBuf = 0;
    pCppRd->cchCurFileNm = 0;

    pCppRd->pszLineBuf = NULL;
    pCppRd->cbLineBuf = 0;

    pCppRd->pDepState = pDepState;
}


static void kOCCppRdDelete(PKOCCPPRD pCppRd)
{
    free(pCppRd->pszBuf);
    pCppRd->pszBuf = NULL;

    free(pCppRd->pszFileNmBuf);
    pCppRd->pszFileNmBuf = NULL;

    free(pCppRd->pszLineBuf);
    pCppRd->pszLineBuf = NULL;
}


/**
 * Allocate more buffer space for the C preprocessor output.
 *
 * @param   pCppRd      The C preprocessor reader instance.
 */
static size_t kOCCppRdGrowBuffer(PKOCCPPRD pCppRd)
{
    pCppRd->cbBufAlloc += KOC_BUF_INCR;
    pCppRd->pszBuf = xrealloc(pCppRd->pszBuf, pCppRd->cbBufAlloc);

    return pCppRd->cbBufAlloc - pCppRd->offSrcRead;
}


static size_t kOCCppRdOptInsert(PKOCCPPRD pCppRd, size_t cchSrcReplaced, const char *pchInsert, size_t cchInsert)
{
    size_t offDelta = 0;
    size_t cchAvail;

    pCppRd->offSrcUnoptimized += cchSrcReplaced;
    assert(pCppRd->offSrcUnoptimized <= pCppRd->offSrcCur);
    cchAvail = pCppRd->offSrcUnoptimized - pCppRd->cchDstOptimized;
    if (cchAvail < cchInsert)
    {
        size_t const cbToMove = pCppRd->offSrcRead - pCppRd->offSrcUnoptimized;
        assert(cbToMove <= pCppRd->offSrcRead);
        offDelta = cchInsert - cchAvail;

        while (pCppRd->offSrcRead + offDelta >= pCppRd->cbBufAlloc)
            kOCCppRdGrowBuffer(pCppRd);

        g_cMemMoves++;
        g_cbMemMoved += cbToMove + 1;
        memmove(pCppRd->pszBuf + pCppRd->offSrcUnoptimized + offDelta,
                pCppRd->pszBuf + pCppRd->offSrcUnoptimized,
                cbToMove + 1);

        pCppRd->offSrcRead += offDelta;
        pCppRd->offSrcUnoptimized += offDelta;
        pCppRd->offSrcCur += offDelta;
        assert(pCppRd->offSrcRead < 1 || pCppRd->pszBuf[pCppRd->offSrcRead - 1] != '\0');
    }

    memcpy(pCppRd->pszBuf + pCppRd->cchDstOptimized, pchInsert, cchInsert);
    pCppRd->cchDstOptimized += cchInsert;

    return offDelta;
}


static void kOCCppRdOptCommit(PKOCCPPRD pCppRd)
{
    size_t cchToCommit = pCppRd->offSrcCur - pCppRd->offSrcUnoptimized;
    assert(pCppRd->offSrcUnoptimized <= pCppRd->offSrcCur);

    if (cchToCommit)
    {
        memmove(pCppRd->pszBuf + pCppRd->cchDstOptimized, pCppRd->pszBuf + pCppRd->offSrcUnoptimized, cchToCommit);
        pCppRd->cchDstOptimized += cchToCommit;
        pCppRd->offSrcUnoptimized = pCppRd->offSrcCur;
    }

    pCppRd->uOptLineNo = pCppRd->uCurLineNo;
}



static char *kOCCppRdOptGetEol(PKOCCPPRD pCppRd, char *pszCur, size_t cbLeft)
{
    char *pszEol = memchr(pszCur, '\n', cbLeft);
    if (pszEol)
    {
        if (pszCur != pszEol && pszEol[-1] == '\r')
            pszEol--;
    }
    else if (pCppRd->fDone && cbLeft)
        pszEol = pszCur + cbLeft;
    return pszEol;
}

static void kOCCppRdOptSetFile(PKOCCPPRD pCppRd, const char *pchFile, size_t cchFile)
{
    if (cchFile >= pCppRd->cbFileNmBuf)
    {
        pCppRd->cbFileNmBuf  = (cchFile + 15 + 1) & ~(size_t)15;
        pCppRd->pszFileNmBuf = xrealloc(pCppRd->pszFileNmBuf, pCppRd->cbFileNmBuf);
    }
    memcpy(pCppRd->pszFileNmBuf, pchFile, cchFile);
    pCppRd->pszFileNmBuf[cchFile] = '\0';
    pCppRd->cchCurFileNm = cchFile;
}


static size_t kOCCppRdOptFmtLine(PKOCCPPRD pCppRd, uint32_t uLine, const char *pchFile, size_t cchFile)
{
    size_t cchUsed;
    size_t cbNeeded;

    /* Make sure we've got enough buffer space. */
    cbNeeded = sizeof("#line 4888222111 \"\"\n") + cchFile;
    if (cbNeeded > pCppRd->cbLineBuf)
    {
        pCppRd->cbLineBuf  = (cbNeeded + 32 + 15) & ~(size_t)15;
        pCppRd->pszLineBuf = xrealloc(pCppRd->pszLineBuf, pCppRd->cbLineBuf);
    }

    /* Do the formatting. */
    cchUsed = sprintf(pCppRd->pszLineBuf, "#line %lu", (unsigned long)uLine);
    if (cchFile)
    {
        pCppRd->pszLineBuf[cchUsed++] = ' ';
        pCppRd->pszLineBuf[cchUsed++] = '"';
        memcpy(&pCppRd->pszLineBuf[cchUsed], pchFile, cchFile);
        cchUsed += cchFile;
        pCppRd->pszLineBuf[cchUsed++] = '"';
    }
    pCppRd->pszLineBuf[cchUsed++] = '\n';
    pCppRd->pszLineBuf[cchUsed] = '\0';

    return cchUsed;
}


static size_t kOCCppRdOptFmtNewLines(PKOCCPPRD pCppRd, uint32_t cNewLines)
{
    if (cNewLines + 1 > pCppRd->cbLineBuf)
    {
        pCppRd->cbLineBuf  = (cNewLines + 1 + 32 + 15) & ~(size_t)15;
        pCppRd->pszLineBuf = xrealloc(pCppRd->pszLineBuf, pCppRd->cbLineBuf);
    }

    memset(pCppRd->pszLineBuf, '\n', cNewLines);
    pCppRd->pszLineBuf[cNewLines] = '\0';
    return cNewLines;
}


static size_t kOCCppRdOptFlush(PKOCCPPRD pCppRd, size_t offSrcCur, int fLineDirNext)
{
    size_t offDelta = 0;
    size_t const offSrcUnoptimized = pCppRd->offSrcUnoptimized;
    assert(offSrcUnoptimized <= offSrcCur);

    if (offSrcCur > offSrcUnoptimized)
    {
        /*
         * We've got unflushed whitelines.
         */
        size_t const cchSrcInQuestion = offSrcCur - offSrcUnoptimized;
        uint32_t const cLinesInQuestion = pCppRd->uCurLineNo - pCppRd->uOptLineNo;
        size_t cchLineDir;

        if (   cLinesInQuestion <= 7
            || (cchLineDir = kOCCppRdOptFmtLine(pCppRd, pCppRd->uCurLineNo, NULL, 0)) >= cLinesInQuestion)
            cchLineDir = kOCCppRdOptFmtNewLines(pCppRd, cLinesInQuestion);

        offDelta = kOCCppRdOptInsert(pCppRd, cchSrcInQuestion, pCppRd->pszLineBuf, cchLineDir);
    }

    (void)fLineDirNext; /* Use later if required. */
    return offDelta;
}


static int kOCCppRdOptParseLine(PKOCCPPRD pCppRd, char *pszCur, char *pszEol,
                                uint32_t *puNewLineNo, char **ppszNewFile, size_t *pcchNewFile)
{
    char    *psz = pszCur;
    uint32_t uNewLineNo;
    int      fIsShort;

    /*
     * Check if it's a #line directive of some kind and parse it.
     */
    if (*psz != '#')
        return 0;
    psz++;

    fIsShort = MY_IS_BLANK(*psz);
    while (MY_IS_BLANK(*psz))
        psz++;

    if (   psz[0] == 'l'
        && psz[1] == 'i'
        && psz[2] == 'n'
        && psz[3] == 'e'
        && MY_IS_BLANK(psz[4]) )
    {
        fIsShort = 0;
        psz += 5;
        while (MY_IS_BLANK(*psz))
            psz++;
    }
    else if (fIsShort && isdigit(*psz))
        fIsShort = 1;
    else
        return 0;

    /* Parse the line number. */
    if (!isdigit(*psz))
        return 0;

    uNewLineNo = *psz++ - '0';
    while (isdigit(*psz))
    {
        uNewLineNo *= 10;
        uNewLineNo += *psz++ - '0';
    }
    if (   psz != pszEol
        && !MY_IS_BLANK(*psz))
        return 0;

    /*
     * The file name part is optional.
     */
    while (MY_IS_BLANK(*psz))
        psz++;

    if (   psz != pszEol
        && *psz == '"')
    {
        *ppszNewFile = ++psz;
        while (   psz != pszEol
               && (   *psz != '"'
                   || (   psz[-1] == '\\'
                       && kOCDepIsEscaped(psz, psz - *ppszNewFile)) )
              )
            psz++;
        if (psz == pszEol)
        {
            /** @todo complain? */
            return 0;
        }
        *pcchNewFile = psz - *ppszNewFile;

        do
            psz++;
        while (psz != pszEol && MY_IS_BLANK(*psz));
    }
    else
    {
        /* No file given => Same as the current. */
        *ppszNewFile = pCppRd->cchCurFileNm ? pCppRd->pszFileNmBuf : NULL;
        *pcchNewFile = pCppRd->cchCurFileNm;
    }
    if (psz != pszEol)
    {
        /** @todo complain? */
        return 0;
    }

    *puNewLineNo = uNewLineNo;
    return 1;
}


static char *kOCCppRdOptHandleLine(PKOCCPPRD pCppRd, char *pszCur, size_t *pcbLeft, int *pfEmptyLine, char *pszEol)
{
    size_t const offSrcLine = pCppRd->offSrcCur;
    size_t const cchSrcLine = pszEol - pCppRd->pszBuf - (pCppRd->fOptimize & 2 ? pCppRd->offSrcUnoptimized : pCppRd->offSrcCur);
    size_t const cbLeftAssert = *pcbLeft;
    char *pszNewFile;
    size_t cchNewFile;
    uint32_t uNewLineNo;
    assert(*pszEol == '\r' || *pszEol == '\n' || *pszEol == '\0');

    /* Advance to the end of the line before we do anything.  This can be a
       little confusing but it saves effort and avoid trouble in the end. */
    pCppRd->offSrcCur = pszEol - pCppRd->pszBuf;
    *pcbLeft -= pszEol - pszCur;
    assert(*pcbLeft <= cbLeftAssert); (void)cbLeftAssert;

    /*
     * Try parse the directive a '#line' one....
     */
    if (!kOCCppRdOptParseLine(pCppRd, pszCur, pszEol, &uNewLineNo, &pszNewFile, &cchNewFile))
    {
        /*
         * No line directive.  Flush pending optimizations and indicate that
         * the line isn't empty and needs to be commited at EOL.
         */
        kOCCppRdOptFlush(pCppRd, offSrcLine, 0);
        *pfEmptyLine = 0;
    }
    else
    {
        char *pszCurFile = pCppRd->cchCurFileNm ? pCppRd->pszFileNmBuf : NULL;
        if (   pszNewFile == pszCurFile
            || (   cchNewFile == pCppRd->cchCurFileNm
                && !memcmp(pszNewFile, pszCurFile, cchNewFile)) )
        {
            /*
             * A #line directive specifying the same file.
             */
            if (uNewLineNo >= pCppRd->uCurLineNo)
                *pfEmptyLine = 1;
            else
            {
                /*
                 * It went backwards, so we need to flush the old section of
                 * the file and emit another directive for starting the new one.
                 */
                size_t cchLineDir;
                if (!(pCppRd->fOptimize & 2))
                    kOCCppRdOptFlush(pCppRd, offSrcLine, 1);

                cchLineDir = kOCCppRdOptFmtLine(pCppRd, uNewLineNo, NULL, 0) - 1; /* sans \n */
                kOCCppRdOptInsert(pCppRd, cchSrcLine, pCppRd->pszLineBuf, cchLineDir);

                *pfEmptyLine = 0;
            }
        }
        else
        {
            /*
             * The #line directive changed the file.
             */
            size_t cchLineDir;

            kOCCppRdOptSetFile(pCppRd, pszNewFile, cchNewFile); /* save to do this early */
            if (!(pCppRd->fOptimize & 2))
                kOCCppRdOptFlush(pCppRd, offSrcLine, 1);

            cchLineDir = kOCCppRdOptFmtLine(pCppRd, uNewLineNo, pCppRd->pszFileNmBuf, cchNewFile) - 1; /* sans \n */
            kOCCppRdOptInsert(pCppRd, cchSrcLine, pCppRd->pszLineBuf, cchLineDir);

            if (pCppRd->pDepState)
                kOCDepEnter(pCppRd->pDepState, pCppRd->pszFileNmBuf, cchNewFile);

            *pfEmptyLine = 0;
        }

        pCppRd->uCurLineNo = uNewLineNo - 1;
    }

    return pCppRd->pszBuf + pCppRd->offSrcCur;
}


static void kOCCppRdOpt(PKOCCPPRD pCppRd)
{
    size_t cch;
    char *pszEol;
    char *pszCur = pCppRd->pszBuf + pCppRd->offSrcCur;
    size_t cbTodo = pCppRd->offSrcRead - pCppRd->offSrcCur;
    int fEmptyLine = 1;

    while (cbTodo > 0)
    {
        switch (*pszCur)
        {
            case ' ':
            case '\t':
                break;

            case '\n':
                pCppRd->offSrcCur = pszCur - pCppRd->pszBuf + 1;
                pCppRd->uCurLineNo++;
                if (!fEmptyLine)
                    kOCCppRdOptCommit(pCppRd);
                fEmptyLine = 1;
                break;

            case '\r': /* "\r\n" -> "\n" */
                if (cbTodo <= 1 && !pCppRd->fDone)
                    return;
                if (pszCur[1] == '\n' && !fEmptyLine)
                {
                    /* Commit the part up to the '\r' first, replace '\r\n' with '\n'. */
                    pCppRd->offSrcCur = pszCur - pCppRd->pszBuf;
                    kOCCppRdOptCommit(pCppRd);

                    pCppRd->offSrcCur += 2;
                    kOCCppRdOptInsert(pCppRd, 2, "\n", 1);

                    assert(cbTodo >= 2);
                    cbTodo -= 2;
                    pszCur += 2;

                    fEmptyLine = 1;
                    continue;
                }
                break;

            case '#':
                pszEol = kOCCppRdOptGetEol(pCppRd, pszCur + 1, cbTodo - 1);
                if (!pszEol)
                    return;
                pszCur = kOCCppRdOptHandleLine(pCppRd, pszCur, &cbTodo, &fEmptyLine, pszEol);
                continue;

            default:
                /*
                 * Some non-white stuff encountered, flush pending white
                 * line optimizations and skip to the end of the line.
                 */
                fEmptyLine = 0;
                pszEol = kOCCppRdOptGetEol(pCppRd, pszCur + 1, cbTodo - 1);
                if (!pszEol)
                    return;
                cch = pszEol - pszCur;

                pszCur += kOCCppRdOptFlush(pCppRd, pCppRd->offSrcCur, 0);

                assert(cch <= cbTodo);
                cbTodo -= cch;
                pszCur += cch;
                continue;
        }

        cbTodo--;
        pszCur++;
    }
}


static void kOCCppRdOptFinalize(PKOCCPPRD pCppRd)
{
    pCppRd->fDone = 1;
    assert(pCppRd->offSrcRead < 1 || pCppRd->pszBuf[pCppRd->offSrcRead - 1] != '\0');
    pCppRd->pszBuf[pCppRd->offSrcRead] = '\0';
    kOCCppRdOpt(pCppRd);

    assert(pCppRd->offSrcCur == pCppRd->offSrcRead);
    kOCCppRdOptFlush(pCppRd, pCppRd->offSrcCur, 0);
}



/**
 * Read C preprocessor output from the given file descriptor, optionally
 * optimzing it.
 *
 * @returns Number of bytes read. 0 indicates end of file.
 *
 * @param   pCppRd      The C preprocessor reader instance.
 * @param   fdIn        The file descriptor to read the raw preprocessor output
 *                      from.
 * @param   ppszRet     Where to return the pointer to the output.
 *
 * @remarks Won't return on error, calls FatalDie on those occasions.
 */
static long kOCCppRdRead(PKOCCPPRD pCppRd, int fdIn, const char **ppszRet)
{
    size_t cbLeft;
    long   cbRead;

    if (pCppRd->fOptimize)
    {
        /*
         * Optimize the C preprocessor output on the way thru.
         */
        size_t const cchOldOptimized = pCppRd->cchDstOptimized;
        if (pCppRd->chSaved)
            pCppRd->pszBuf[pCppRd->cchDstOptimized] = pCppRd->chSaved;

        do
        {
            /* Read more raw C preprocessor output. */
            cbLeft = pCppRd->cbBufAlloc - pCppRd->offSrcRead;
            if (cbLeft <= 1)
                cbLeft = kOCCppRdGrowBuffer(pCppRd);

            do
                cbRead = read(fdIn, pCppRd->pszBuf + pCppRd->offSrcRead, (long)(cbLeft - 1));
            while (cbRead < 0 && errno == EINTR);
            if (cbRead < 0)
                FatalDie("kOCCppRdRead - read(%d,,%ld) failed: %s\n",
                         fdIn, (long)(cbLeft - 1), strerror(errno));
            pCppRd->offSrcRead += cbRead;

            /* Optimize it. */
            if (!cbRead)
            {
                kOCCppRdOptFinalize(pCppRd);
                break;
            }
            kOCCppRdOpt(pCppRd);
        } while (pCppRd->cchDstOptimized == cchOldOptimized);

        *ppszRet = &pCppRd->pszBuf[cchOldOptimized];
        pCppRd->chSaved = pCppRd->pszBuf[pCppRd->cchDstOptimized];
        pCppRd->pszBuf[pCppRd->cchDstOptimized] = '\0';
        cbRead = (long)(pCppRd->cchDstOptimized - cchOldOptimized);
    }
    else
    {
        /*
         * Pass thru.
         */
        char *pszBuf;
        cbLeft = pCppRd->cbBufAlloc - pCppRd->offSrcRead;
        if (cbLeft <= 1)
            cbLeft = kOCCppRdGrowBuffer(pCppRd);
        pszBuf = pCppRd->pszBuf + pCppRd->offSrcRead;

        do
            cbRead = read(fdIn, pszBuf, (long)(cbLeft - 1));
        while (cbRead < 0 && errno == EINTR);
        if (cbRead < 0)
            FatalDie("kOCCppRdRead - read(%d,,%ld) failed: %s\n",
                     fdIn, (long)(cbLeft - 1), strerror(errno));

        *ppszRet = pszBuf;
        pCppRd->offSrcRead += cbRead;
        pszBuf[cbRead] = '\0';
    }

    return cbRead;
}


/**
 * Grabs the output buffer from the C preprocessor reader.
 *
 * @param   pCppRd              The C preprocessor reader instance.
 * @param   ppszRet             Where to return the pointer to the output.
 * @param   pcbRet              Where to return the size of the output.
 */
static void kOCCppRdGrabOutput(PKOCCPPRD pCppRd, char **ppszRet, size_t *pcbRet)
{
    assert(pCppRd->offSrcRead < 1 || pCppRd->pszBuf[pCppRd->offSrcRead - 1] != '\0');
    *ppszRet = pCppRd->pszBuf;
    *pcbRet  = pCppRd->fOptimize ? pCppRd->cchDstOptimized : pCppRd->offSrcRead;
    pCppRd->pszBuf = NULL;
    pCppRd->offSrcRead = 0;
}






/** A checksum list entry.
 * We keep a list checksums (of preprocessor output) that matches.
 *
 * The matching algorithm doesn't require the preprocessor output to be
 * indentical, only to produce the same object files.
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
 * Temporary context record used when calculating the checksum of some data.
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
        MD5Update(&pCtx->MD5Ctx, pb, (unsigned)cb);
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
    PKOCSUM pSum = pSumHead->pNext;
    while (pSum)
    {
        void *pvFree = pSum;
        pSum = pSum->pNext;
        free(pvFree);
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
    /** Whether the preprocessor runs in piped mode. If clear it's file
     * mode (it could be redirected stdout, but that's essentially the
     * same from our point of view). */
    unsigned fPipedPreComp;
    /** Whether the compiler runs in piped mode (preprocessor output on stdin). */
    unsigned fPipedCompile;
    /** The name of the pipe that we're feeding the preprocessed output to the
     *  compiler via.  This is a Windows thing. */
    char *pszNmPipeCompile;
    /** Name of the dependency file (generated from #line statements in the
     * preprocessor output). */
    char *pszMakeDepFilename;
    /** Whether to fix the case of the make depedencies. */
    int fMakeDepFixCase;
    /** Whether to do the make dependencies quietly. */
    int fMakeDepQuiet;
    /** Whether to generate stubs for headers files. */
    int fMakeDepGenStubs;
    /** The dependency collector state.  */
    KOCDEP DepState;
    /** Whether the optimizations are enabled. */
    int fOptimizeCpp;
    /** Cache entry key that's used for some quick digest validation. */
    uint32_t uKey;

    /** The file data. */
    struct KOCENTRYDATA
    {
        /** The name of file containing the preprocessor output. */
        char *pszCppName;
        /** Pointer to the preprocessor output. */
        char *pszCppMapping;
        /** The size of the preprocessor output. 0 if not determined. */
        size_t cbCpp;
        /** The preprocessor output checksums that will produce the cached object. */
        KOCSUM SumHead;
        /** The number of milliseconds spent precompiling. */
        uint32_t cMsCpp;

        /** The object filename (relative to the cache file). */
        char *pszObjName;
        /** The compile argument vector used to build the object. */
        char **papszArgvCompile;
        /** The size of the compile  */
        unsigned cArgvCompile;
        /** The checksum of the compiler argument vector. */
        KOCSUM SumCompArgv;
        /** The number of milliseconds spent compiling. */
        uint32_t cMsCompile;
        /** @todo need a list of additional output files for MSC. */
        /** @todo need compiler output (warnings). */

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

    kOCDepInit(&pEntry->DepState);

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
    /** @todo free pEntry->pszName? */
    free(pEntry->pszDir);
    free(pEntry->pszAbsPath);
    free(pEntry->pszNmPipeCompile);
    free(pEntry->pszMakeDepFilename);

    kOCDepDelete(&pEntry->DepState);

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
 * @param   pszIgnorePath1  Path to ignore when encountered at the end of
 *                          arguments. (Not quite safe for simple file names,
 *                          but what the heck.)
 * @param   pszIgnorePath2  Path to ignore when encountered at the end of
 *                          arguments. (Not quite safe for simple file names,
 *                          but what the heck.)
 * @param   pSum            Where to store the check sum.
 */
static void kOCEntryCalcArgvSum(PKOCENTRY pEntry, const char * const *papszArgv, unsigned cArgc,
                                const char *pszIgnorePath1, const char *pszIgnorePath2, PKOCSUM pSum)
{
    size_t cchIgnorePath1 = strlen(pszIgnorePath1);
    size_t cchIgnorePath2 = pszIgnorePath2 ? strlen(pszIgnorePath2) : ~(size_t)0;
    KOCSUMCTX Ctx;
    unsigned i;

    kOCSumInitWithCtx(pSum, &Ctx);
    for (i = 0; i < cArgc; i++)
    {
        size_t cch = strlen(papszArgv[i]);
        if (   (   cch < cchIgnorePath1
                || !ArePathsIdenticalN(papszArgv[i] + cch - cchIgnorePath1, pszIgnorePath1, cch))
            && (   cch < cchIgnorePath2
                || !ArePathsIdenticalN(papszArgv[i] + cch - cchIgnorePath2, pszIgnorePath2, cch)) )
            kOCSumUpdate(pSum, &Ctx, papszArgv[i], cch + 1);
    }
    kOCSumFinalize(pSum, &Ctx);

    (void)pEntry;
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
            ||  (   strcmp(g_szLine, "magic=kObjCacheEntry-v0.1.0\n")
                 && strcmp(g_szLine, "magic=kObjCacheEntry-v0.1.1\n"))
           )
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
                else if (!strcmp(g_szLine, "cpp-ms"))
                {
                    char *pszNext;
                    if ((fBad = pEntry->Old.cMsCpp != 0))
                        break;
                    pEntry->Old.cMsCpp = strtoul(pszVal, &pszNext, 0);
                    if ((fBad = pszNext && *pszNext))
                        break;
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
                    unsigned iArg = strtoul(&g_szLine[sizeof("cc-argv-#") - 1], &pszNext, 0);
                    if ((fBad = iArg >= pEntry->Old.cArgvCompile || pEntry->Old.papszArgvCompile[iArg] || (pszNext && *pszNext)))
                        break;
                    pEntry->Old.papszArgvCompile[iArg] = xstrdup(pszVal);
                }
                else if (!strcmp(g_szLine, "cc-argv-sum"))
                {
                    if ((fBad = !kOCSumIsEmpty(&pEntry->Old.SumCompArgv)))
                        break;
                    if ((fBad = kOCSumInitFromString(&pEntry->Old.SumCompArgv, pszVal)))
                        break;
                }
                else if (!strcmp(g_szLine, "cc-ms"))
                {
                    char *pszNext;
                    if ((fBad = pEntry->Old.cMsCompile != 0))
                        break;
                    pEntry->Old.cMsCompile = strtoul(pszVal, &pszNext, 0);
                    if ((fBad = pszNext && *pszNext))
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
                                        pEntry->Old.cArgvCompile, pEntry->Old.pszObjName, pEntry->Old.pszCppName,
                                        &Sum);
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

    fprintf(pFile, "magic=kObjCacheEntry-v0.1.1\n");
    CHECK_LEN(fprintf(pFile, "target=%s\n",     pEntry->New.pszTarget ? pEntry->New.pszTarget : pEntry->Old.pszTarget));
    CHECK_LEN(fprintf(pFile, "key=%lu\n",       (unsigned long)pEntry->uKey));
    CHECK_LEN(fprintf(pFile, "obj=%s\n",        pEntry->New.pszObjName ? pEntry->New.pszObjName : pEntry->Old.pszObjName));
    CHECK_LEN(fprintf(pFile, "cpp=%s\n",        pEntry->New.pszCppName ? pEntry->New.pszCppName : pEntry->Old.pszCppName));
    CHECK_LEN(fprintf(pFile, "cpp-size=%lu\n",  (unsigned long)(pEntry->New.pszCppName ? pEntry->New.cbCpp      : pEntry->Old.cbCpp)));
    CHECK_LEN(fprintf(pFile, "cpp-ms=%lu\n",    (unsigned long)(pEntry->New.pszCppName ? pEntry->New.cMsCpp     : pEntry->Old.cMsCpp)));
    CHECK_LEN(fprintf(pFile, "cc-ms=%lu\n",     (unsigned long)(pEntry->New.pszCppName ? pEntry->New.cMsCompile : pEntry->Old.cMsCompile)));

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

    kOCEntryCalcArgvSum(pEntry, papszArgvCompile, cArgvCompile, pEntry->New.pszObjName, pEntry->New.pszCppName,
                        &pEntry->New.SumCompArgv);
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
 * Sets the preprocessor output filename. We don't generally care if this
 * matches the old name or not.
 *
 * @param   pEntry      The cache entry.
 * @param   pszCppName  The preprocessor output filename.
 */
static void kOCEntrySetCppName(PKOCENTRY pEntry, const char *pszCppName)
{
    assert(!pEntry->New.pszCppName);
    pEntry->New.pszCppName = CalcRelativeName(pszCppName, pEntry->pszDir);
}


/**
 * Sets the piped mode of the preprocessor and compiler.
 *
 * @param   pEntry                  The cache entry.
 * @param   fRedirPreCompStdOut     Whether the preprocessor is in piped mode.
 * @param   fRedirCompileStdIn      Whether the compiler is in piped mode.
 * @param   pszNmPipeCompile        The name of the named pipe to use to feed
 *                                  the microsoft compiler.
 */
static void kOCEntrySetPipedMode(PKOCENTRY pEntry, int fRedirPreCompStdOut, int fRedirCompileStdIn,
                                 const char *pszNmPipeCompile)
{
    pEntry->fPipedPreComp = fRedirPreCompStdOut;
    pEntry->fPipedCompile = fRedirCompileStdIn || pszNmPipeCompile;
    pEntry->pszNmPipeCompile = xstrdup(pszNmPipeCompile);
}


/**
 * Sets the dependency file.
 *
 * @param   pEntry                  The cache entry.
 * @param   pszMakeDepFilename      The dependency filename.
 * @param   fMakeDepFixCase         Whether to fix the case of dependency files.
 * @param   fMakeDepQuiet           Whether to be quiet about the dependencies.
 * @param   fMakeDepGenStubs        Whether to generate stubs.
 */
static void kOCEntrySetDepFilename(PKOCENTRY pEntry, const char *pszMakeDepFilename,
                                   int fMakeDepFixCase, int fMakeDepQuiet, int fMakeDepGenStubs)
{
    pEntry->pszMakeDepFilename = xstrdup(pszMakeDepFilename);
    pEntry->fMakeDepFixCase = fMakeDepFixCase;
    pEntry->fMakeDepQuiet = fMakeDepQuiet;
    pEntry->fMakeDepGenStubs = fMakeDepGenStubs;
}


/**
 * Configures the preprocessor output optimizations.
 *
 * @param   pEntry                  The cache entry.
 * @param   fOptimizeCpp            The one and only flag, so far.
 */
static void kOCEntrySetOptimizations(PKOCENTRY pEntry, int fOptimizeCpp)
{
    pEntry->fOptimizeCpp = fOptimizeCpp;
}


/**
 * Spawns a child in a synchronous fashion.
 * Terminating on failure.
 *
 * @param   papszArgv       Argument vector. The cArgv element is NULL.
 * @param   pcMs            The cache entry member use for time keeping.  This
 *                          will be set to the current timestamp.
 * @param   cArgv           The number of arguments in the vector.
 * @param   pszMsg          Which operation this is, for use in messages.
 * @param   pszStdOut       Where to redirect standard out.
 */
static void kOCEntrySpawn(PCKOCENTRY pEntry, uint32_t *pcMs, const char * const *papszArgv, unsigned cArgv,
                          const char *pszMsg, const char *pszStdOut)
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

    *pcMs = NowMs();
    errno = 0;
# ifdef __WIN__
    rc = quoted_spawnvp(_P_WAIT, papszArgv[0], papszArgv);
# else
    rc = _spawnvp(_P_WAIT, papszArgv[0], papszArgv);
# endif
    *pcMs = NowMs() - *pcMs;
    if (rc < 0)
        FatalDie("%s - _spawnvp failed (rc=0x%p): %s\n", pszMsg, rc, strerror(errno));
    if (rc > 0)
        FatalDie("%s - failed rc=%d\n", pszMsg, (int)rc);
    if (fdStdOut != -1)
    {
        close(STDOUT_FILENO);
        fdStdOut = dup2(fdStdOut, STDOUT_FILENO);
        close(fdStdOut);
    }

#else
    int iStatus;
    pid_t pidWait;
    pid_t pid;

    *pcMs = NowMs();
    pid = fork();
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
    *pcMs = NowMs() - *pcMs;
    if (pidWait != pid)
        FatalDie("%s - waitpid failed rc=%d: %s\n",
                 pszMsg, pidWait, strerror(errno));
    if (!WIFEXITED(iStatus))
        FatalDie("%s - abended (iStatus=%#x)\n", pszMsg, iStatus);
    if (WEXITSTATUS(iStatus))
        FatalDie("%s - failed with rc %d\n", pszMsg, WEXITSTATUS(iStatus));
#endif

     (void)pEntry; (void)cArgv;
}


/**
 * Spawns child with optional redirection of stdin and stdout.
 *
 * @param   pEntry          The cache entry.
 * @param   pcMs            The cache entry member use for time keeping.  This
 *                          will be set to the current timestamp.
 * @param   papszArgv       Argument vector. The cArgv element is NULL.
 * @param   cArgv           The number of arguments in the vector.
 * @param   fdStdIn         Child stdin, -1 if it should inherit our stdin. Will be closed.
 * @param   fdStdOut        Child stdout, -1 if it should inherit our stdout. Will be closed.
 * @param   pszMsg          Message to start the info/error messages with.
 */
static pid_t kOCEntrySpawnChild(PCKOCENTRY pEntry, uint32_t *pcMs, const char * const *papszArgv, unsigned cArgv,
                                int fdStdIn, int fdStdOut, const char *pszMsg)
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
    *pcMs = NowMs();
#if defined(__OS2__) || defined(__WIN__)
    errno = 0;
# ifdef __WIN__
    pid = quoted_spawnvp(_P_NOWAIT, papszArgv[0], papszArgv);
# else
    pid = _spawnvp(_P_NOWAIT, papszArgv[0], papszArgv);
# endif
    if (pid == -1)
        FatalDie("preprocess - _spawnvp failed: %s\n", strerror(errno));

#else
    pid = fork();
    if (!pid)
    {
        execvp(papszArgv[0], (char **)papszArgv);
        FatalDie("preprocess - execvp failed: %s\n", strerror(errno));
    }
    if (pid == -1)
        FatalDie("preprocess - fork() failed: %s\n", strerror(errno));
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
 * @param   pcMs        The millisecond timestamp that should be convert to
 *                      elapsed time.
 * @param   pid         The child to wait for.
 * @param   pszMsg      Message to start the info/error messages with.
 */
static void kOCEntryWaitChild(PCKOCENTRY pEntry, uint32_t *pcMs, pid_t pid, const char *pszMsg)
{
    int iStatus = -1;
    pid_t pidWait;
    InfoMsg(3, "%s - wait-child %ld\n", pszMsg, (long)pid);

#ifdef __WIN__
    pidWait = _cwait(&iStatus, pid, _WAIT_CHILD);
    *pcMs = NowMs() - *pcMs;
    if (pidWait == -1)
        FatalDie("%s - waitpid failed: %s\n", pszMsg, strerror(errno));
    if (iStatus)
        FatalDie("%s - failed with rc %d\n", pszMsg, iStatus);
#else
    pidWait = waitpid(pid, &iStatus, 0);
    while (pidWait < 0 && errno == EINTR)
        pidWait = waitpid(pid, &iStatus, 0);
    *pcMs = NowMs() - *pcMs;
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
 * @param   paFDs           Where to store the two file descriptors.
 * @param   pszMsg          The operation message for info/error messages.
 * @param   pszPipeName     The pipe name if it is supposed to be named. (Windows only.)
 * @param   fText           Whether to read text mode or binary mode.
 */
static void kOCEntryCreatePipe(PKOCENTRY pEntry, int *paFDs, const char *pszPipeName, const char *pszMsg, int fText)
{
    paFDs[0] = paFDs[1] = -1;
#if defined(__WIN__)
    if (pszPipeName)
    {
        HANDLE hPipe = CreateNamedPipeA(pszPipeName,
                                       /*PIPE_ACCESS_OUTBOUND*/ PIPE_ACCESS_DUPLEX,
                                       PIPE_READMODE_BYTE | PIPE_WAIT,
                                       10 /* nMaxInstances */,
                                       0x10000 /* nOutBuffer */,
                                       0x10000 /* nInBuffer */,
                                       NMPWAIT_WAIT_FOREVER,
                                       NULL /* pSecurityAttributes */);

        if (hPipe == INVALID_HANDLE_VALUE)
            FatalDie("%s - CreateNamedPipe(%s) failed: %d\n", pszMsg, pszPipeName, GetLastError());

        paFDs[1 /* write */] = _open_osfhandle((intptr_t)hPipe, _O_WRONLY | _O_TEXT | _O_NOINHERIT);
        if (paFDs[1 /* write */] == -1)
            FatalDie("%s - _open_osfhandle failed: %d\n", pszMsg, strerror(errno));
    }
    else if (   _pipe(paFDs, 256*1024, _O_NOINHERIT | (fText ? _O_TEXT : _O_BINARY)) < 0
             && _pipe(paFDs,        0, _O_NOINHERIT | (fText ? _O_TEXT : _O_BINARY)) < 0)
#else
    if (pipe(paFDs) < 0)
#endif
        FatalDie("%s - pipe failed: %s\n", pszMsg, strerror(errno));
#if !defined(__WIN__)
    fcntl(paFDs[0], F_SETFD, FD_CLOEXEC);
    fcntl(paFDs[1], F_SETFD, FD_CLOEXEC);
#endif

    (void)pEntry;
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

    kOCEntryCreatePipe(pEntry, fds, NULL, pszMsg, pEntry->fOptimizeCpp);
    pid = kOCEntrySpawnChild(pEntry, &pEntry->New.cMsCpp, papszArgv, cArgv, -1, fds[1 /* write */], pszMsg);

    pfnConsumer(pEntry, fds[0 /* read */]);

    kOCEntryWaitChild(pEntry, &pEntry->New.cMsCpp, pid, pszMsg);
}


/**
 * Spawns a child that consumes input on stdin or via a named pipe.
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

    kOCEntryCreatePipe(pEntry, fds, pEntry->pszNmPipeCompile, pszMsg, 0 /*fText*/);
    pid = kOCEntrySpawnChild(pEntry, &pEntry->New.cMsCompile, papszArgv, cArgv, fds[0 /* read */], -1, pszMsg);
#ifdef __WIN__
    if (pEntry->pszNmPipeCompile && !ConnectNamedPipe((HANDLE)_get_osfhandle(fds[1 /* write */]), NULL))
        FatalDie("compile - ConnectNamedPipe failed: %d\n", GetLastError());
#endif

    pfnProducer(pEntry, fds[1 /* write */]);

    kOCEntryWaitChild(pEntry, &pEntry->New.cMsCompile, pid, pszMsg);
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
    kOCEntryCreatePipe(pEntry, fds, NULL, pszMsg, pEntry->fOptimizeCpp);
    pidConsumer = kOCEntrySpawnChild(pEntry, &pEntry->New.cMsCpp, papszProdArgv, cProdArgv, -1, fds[1 /* write */], pszMsg);
    fdIn = fds[0 /* read */];

    /*
     * The consumer.
     */
    kOCEntryCreatePipe(pEntry, fds, pEntry->pszNmPipeCompile, pszMsg, 0 /*fText*/);
    pidProducer = kOCEntrySpawnChild(pEntry, &pEntry->New.cMsCompile, papszConsArgv, cConsArgv, fds[0 /* read */], -1, pszMsg);
    fdOut = fds[1 /* write */];

    /*
     * Hand it on to the tee consumer.
     */
    pfnTeeConsumer(pEntry, fdIn, fdOut);

    /*
     * Reap the children.
     */
    kOCEntryWaitChild(pEntry, &pEntry->New.cMsCpp, pidProducer, pszMsg);
    kOCEntryWaitChild(pEntry, &pEntry->New.cMsCompile, pidConsumer, pszMsg);
}


/**
 * Reads the output from the preprocessor.
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

    InfoMsg(3, "preprocessed file is %lu bytes long\n", (unsigned long)pWhich->cbCpp);
    return 0;
}


/**
 * Worker for kOCEntryPreProcess and calculates the checksum of
 * the preprocessor output.
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
 * This consumes the preprocessor output and checksums it.
 *
 * @param   pEntry  The cache entry.
 * @param   fdIn    The preprocessor output pipe.
 * @param   fdOut   The compiler input pipe, -1 if no compiler.
 */
static void kOCEntryPreProcessConsumer(PKOCENTRY pEntry, int fdIn)
{
    KOCSUMCTX Ctx;
    KOCCPPRD CppRd;

    kOCSumInitWithCtx(&pEntry->New.SumHead, &Ctx);
    kOCCppRdInit(&CppRd, pEntry->Old.cbCpp, pEntry->fOptimizeCpp,
                 pEntry->pszMakeDepFilename ? &pEntry->DepState : NULL);

    for (;;)
    {
        /*
         * Read data from the pipe.
         */
        const char *psz;
        long cbRead = kOCCppRdRead(&CppRd, fdIn, &psz);
        if (!cbRead)
            break;

        /*
         * Process the data.
         */
        kOCSumUpdate(&pEntry->New.SumHead, &Ctx, psz, cbRead);
        if (pEntry->pszMakeDepFilename && !pEntry->fOptimizeCpp)
            kOCDepConsumer(&pEntry->DepState, psz, cbRead);
    }

    close(fdIn);
    kOCCppRdGrabOutput(&CppRd, &pEntry->New.pszCppMapping, &pEntry->New.cbCpp);
    kOCCppRdDelete(&CppRd);
    kOCSumFinalize(&pEntry->New.SumHead, &Ctx);
    kOCSumInfo(&pEntry->New.SumHead, 4, "cpp (pipe)");
}




/**
 * Run the preprocessor and calculate the checksum of the output.
 *
 * @param   pEntry              The cache entry.
 * @param   papszArgvPreComp    The argument vector for executing preprocessor.
 *                              The cArgvPreComp'th argument must be NULL.
 * @param   cArgvPreComp        The number of arguments.
 */
static void kOCEntryPreProcess(PKOCENTRY pEntry, const char * const *papszArgvPreComp, unsigned cArgvPreComp)
{
    /*
     * If we're executing the preprocessor in piped mode, it's relatively simple.
     */
    if (pEntry->fPipedPreComp)
        kOCEntrySpawnProducer(pEntry, papszArgvPreComp, cArgvPreComp, "preprocess",
                              kOCEntryPreProcessConsumer);
    else
    {
        /*
         * Rename the old preprocessed output to '-old' so the preprocessor won't
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
         * Preprocess it and calculate the checksum on the output.
         */
        InfoMsg(3, "precompiling -> '%s'...\n", pEntry->New.pszCppName);
        kOCEntrySpawn(pEntry, &pEntry->New.cMsCpp, papszArgvPreComp, cArgvPreComp, "preprocess", NULL);
        kOCEntryReadCppOutput(pEntry, &pEntry->New, 0 /* fatal */);
        kOCEntryCalcChecksum(pEntry);
        if (pEntry->pszMakeDepFilename)
            kOCDepConsumer(&pEntry->DepState, pEntry->New.pszCppMapping, pEntry->New.cbCpp);
    }

    if (pEntry->pszMakeDepFilename)
        kOCDepWriteToFile(&pEntry->DepState, pEntry->pszMakeDepFilename, pEntry->New.pszObjName, pEntry->pszDir,
                          pEntry->fMakeDepFixCase, pEntry->fMakeDepQuiet, pEntry->fMakeDepGenStubs);
}


/**
 * Worker function for kOCEntryTeeConsumer and kOCEntryCompileIt that
 * writes the preprocessor output to disk.
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
        cbLeft = (long)pEntry->New.cbCpp;
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
 * kOCEntrySpawnConsumer callback that passes the preprocessor output to the
 * compiler and writes it to the disk (latter only when necesary).
 *
 * @param   pEntry      The cache entry.
 * @param   fdOut       The pipe handle connected to the childs stdin.
 */
static void kOCEntryCompileProducer(PKOCENTRY pEntry, int fdOut)
{
    const char *psz = pEntry->New.pszCppMapping;
    long cbLeft = (long)pEntry->New.cbCpp;
    while (cbLeft > 0)
    {
        long cbWritten = write(fdOut, psz, cbLeft);
        if (cbWritten < 0)
        {
            if (errno == EINTR)
                continue;
#ifdef __WIN__ /* HACK */
            if (   errno == EINVAL
                && pEntry->pszNmPipeCompile
                && DisconnectNamedPipe((HANDLE)_get_osfhandle(fdOut))
                && ConnectNamedPipe((HANDLE)_get_osfhandle(fdOut), NULL))
            {
                psz = pEntry->New.pszCppMapping;
                cbLeft = (long)pEntry->New.cbCpp;
            }
            FatalDie("compile - write(%d,,%ld) failed: %s - _doserrno=%d\n", fdOut, cbLeft, strerror(errno), _doserrno);
#else
            FatalDie("compile - write(%d,,%ld) failed: %s\n", fdOut, cbLeft, strerror(errno));
#endif
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
        kOCEntrySpawnConsumer(pEntry, (const char * const *)pEntry->New.papszArgvCompile,
                              pEntry->New.cArgvCompile, "compile", kOCEntryCompileProducer);
    }
    else
    {
        if (pEntry->fPipedPreComp)
            kOCEntryWriteCppOutput(pEntry, 1 /* free it */);
        InfoMsg(3, "compiling -> '%s'...\n", pEntry->New.pszObjName);
        kOCEntrySpawn(pEntry, &pEntry->New.cMsCompile, (const char * const *)pEntry->New.papszArgvCompile,
                      pEntry->New.cArgvCompile, "compile", NULL);
    }
}


/**
 * kOCEntrySpawnTee callback that works sort of like 'tee'.
 *
 * It will calculate the preprocessed output checksum and
 * write it to disk while the compiler is busy compiling it.
 *
 * @param   pEntry  The cache entry.
 * @param   fdIn    The input handle (connected to the preprocessor).
 * @param   fdOut   The output handle (connected to the compiler).
 */
static void kOCEntryTeeConsumer(PKOCENTRY pEntry, int fdIn, int fdOut)
{
#ifdef __WIN__
    unsigned fConnectedToCompiler = fdOut == -1 || pEntry->pszNmPipeCompile == NULL;
#endif
    KOCSUMCTX Ctx;
    KOCCPPRD  CppRd;

    kOCSumInitWithCtx(&pEntry->New.SumHead, &Ctx);
    kOCCppRdInit(&CppRd, pEntry->Old.cbCpp, pEntry->fOptimizeCpp,
                 pEntry->pszMakeDepFilename ? &pEntry->DepState : NULL);
    InfoMsg(3, "preprocessor|compile - starting passhtru...\n");
    for (;;)
    {
        /*
         * Read data from the pipe.
         */
        const char *psz;
        long cbRead = kOCCppRdRead(&CppRd, fdIn, &psz);
        if (!cbRead)
            break;
        InfoMsg(3, "preprocessor|compile - read %d\n", cbRead);

        /*
         * Process the data.
         */
        kOCSumUpdate(&pEntry->New.SumHead, &Ctx, psz, cbRead);
        if (pEntry->pszMakeDepFilename && !pEntry->fOptimizeCpp)
            kOCDepConsumer(&pEntry->DepState, psz, cbRead);

#ifdef __WIN__
        if (   !fConnectedToCompiler
            && !(fConnectedToCompiler = ConnectNamedPipe((HANDLE)_get_osfhandle(fdOut), NULL)))
            FatalDie("preprocess|compile - ConnectNamedPipe failed: %d\n", GetLastError());
#endif
        do
        {
            long cbWritten = write(fdOut, psz, cbRead);
            if (cbWritten < 0)
            {
                if (errno == EINTR)
                    continue;
                FatalDie("preprocess|compile - write(%d,,%ld) failed: %s\n", fdOut, cbRead, strerror(errno));
            }
            psz += cbWritten;
            cbRead -= cbWritten;
        } while (cbRead > 0);

    }
    InfoMsg(3, "preprocessor|compile - done passhtru\n");

    close(fdIn);
    close(fdOut);
    kOCCppRdGrabOutput(&CppRd, &pEntry->New.pszCppMapping, &pEntry->New.cbCpp);
    kOCCppRdDelete(&CppRd);
    kOCSumFinalize(&pEntry->New.SumHead, &Ctx);
    kOCSumInfo(&pEntry->New.SumHead, 4, "cpp (tee)");

    /*
     * Write the preprocessor output to disk and free the memory it
     * occupies while the compiler is busy compiling.
     */
    kOCEntryWriteCppOutput(pEntry, 1 /* free it */);
}


/**
 * Performs pre-compile and compile in one go (typical clean build scenario).
 *
 * @param   pEntry              The cache entry.
 * @param   papszArgvPreComp    The argument vector for executing preprocessor.
 *                              The cArgvPreComp'th argument must be NULL.
 * @param   cArgvPreComp        The number of arguments.
 */
static void kOCEntryPreProcessAndCompile(PKOCENTRY pEntry, const char * const *papszArgvPreComp, unsigned cArgvPreComp)
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
         * Do the actual compile and write the preprocessor output to disk.
         */
        kOCEntrySpawnTee(pEntry, papszArgvPreComp, cArgvPreComp,
                         (const char * const *)pEntry->New.papszArgvCompile, pEntry->New.cArgvCompile,
                         "preprocess|compile", kOCEntryTeeConsumer);
        if (pEntry->pszMakeDepFilename)
            kOCDepWriteToFile(&pEntry->DepState, pEntry->pszMakeDepFilename,  pEntry->New.pszObjName, pEntry->pszDir,
                              pEntry->fMakeDepFixCase, pEntry->fMakeDepQuiet, pEntry->fMakeDepGenStubs);
    }
    else
    {
        kOCEntryPreProcess(pEntry, papszArgvPreComp, cArgvPreComp);
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
 * preprocessed output using a fast but not very good method.
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
             * we'll have to use the preprocessor output to generate the deps
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
            if (!psz++)
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

            /* Advance. We might now have a misaligned buffer, but that's memcmps problem... */
            psz1 += cch;
            psz2 += cch;
        }
    }

    return psz1 == pszEnd1
        && psz2 == pszEnd2;
}


/**
 * Worker for kOCEntryCompileIfNeeded that compares the
 * preprocessed output.
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
    /*if ()
        return kOCEntryCompareBest(pEntry);*/
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
     * Check if the preprocessor output differ in any significant way?
     */
    if (!kOCSumHasEqualInChain(&pEntry->Old.SumHead, &pEntry->New.SumHead))
    {
        if (pEntry->fOptimizeCpp & 2)
        {
            InfoMsg(2, "no checksum match - no need to compare output, -O2.\n");
            pEntry->fNeedCompiling = 1;
        }
        else
        {
            InfoMsg(2, "no checksum match - comparing output\n");
            if (!kOCEntryCompareOldAndNewOutput(pEntry))
                pEntry->fNeedCompiling = 1;
            else
                kOCSumAddChain(&pEntry->New.SumHead, &pEntry->Old.SumHead);
        }
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
 * Tries to hardlink a file.
 *
 * @returns 1 if it succeeded, 0 if it didn't.
 * @param   pszLink     The name of the hardlink.
 * @param   pszLinkTo   The file to hardlinkg @a pszDst to.
 */
static int kOCEntryTryHardlink(const char *pszLink, const char *pszLinkTo)
{
#ifdef __WIN__
    typedef BOOL (WINAPI *PFNCREATEHARDLINKA)(LPCSTR, LPCSTR, LPSECURITY_ATTRIBUTES);
    static PFNCREATEHARDLINKA   s_pfnCreateHardLinkA = NULL;
    static int                  s_fTried = FALSE;

    /* The API was introduced in Windows 2000, so resolve it dynamically. */
    if (!s_pfnCreateHardLinkA)
    {
        if (!s_fTried)
        {
            HMODULE hmod = LoadLibrary("KERNEL32.DLL");
            if (hmod)
                *(FARPROC *)&s_pfnCreateHardLinkA = GetProcAddress(hmod, "CreateHardLinkA");
            s_fTried = TRUE;
        }
        if (!s_pfnCreateHardLinkA)
            return 0;
    }

    if (!s_pfnCreateHardLinkA(pszLink, pszLinkTo, NULL))
        return 0;
#else
    if (link(pszLinkTo, pszLink) != 0)
        return 0;
#endif
    return 1;
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
    unlink(pszDst);
    if (!kOCEntryTryHardlink(pszDst, pszSrc))
    {
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
    }
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
    /** The list of preprocessor output checksums that's . */
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
             && (long)pCache->uGeneration == atol(&g_szLine[sizeof("generation=") - 1]))
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
#elif defined(__sun__)
    {
        struct flock fl;
        fl.l_whence = 0;
        fl.l_start = 0;
        fl.l_len = 0;
        fl.l_type = F_WRLCK;
        if (fcntl(pCache->fd, F_SETLKW, &fl) != 0)
            FatalDie("Failed to lock the cache file: %s\n", strerror(errno));
    }
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
#elif defined(__sun__)
    {
        struct flock fl;
        fl.l_whence = 0;
        fl.l_start = 0;
        fl.l_len = 0;
        fl.l_type = F_UNLCK;
        if (fcntl(pCache->fd, F_SETLKW, &fl) != 0)
            FatalDie("Failed to lock the cache file: %s\n", strerror(errno));
    }
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
                              kOCEntryAbsPath(pEntry)))
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
static int usage(FILE *pOut)
{
    fprintf(pOut,
            "syntax: kObjCache [--kObjCache-options] [-v|--verbose]\n"
            "            <  [-c|--cache-file <cache-file>]\n"
            "             | [-n|--name <name-in-cache>] [[-d|--cache-dir <cache-dir>]] >\n"
            "            <-f|--file <local-cache-file>>\n"
            "            <-t|--target <target-name>>\n"
            "            [-r|--redir-stdout] [-p|--passthru] [--named-pipe-compile <pipename>]\n"
            "            --kObjCache-cpp <filename> <preprocessor + args>\n"
            "            --kObjCache-cc <object> <compiler + args>\n"
            "            [--kObjCache-both [args]]\n"
            );
    fprintf(pOut,
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
    const char *pszNmPipeCompile = NULL;

    const char *pszMakeDepFilename = NULL;
    int fMakeDepFixCase = 0;
    int fMakeDepGenStubs = 0;
    int fMakeDepQuiet = 0;
    int fOptimizePreprocessorOutput = 0;

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
        return usage(stderr);
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
                    return SyntaxError("--kObjCache-cc requires an preprocessor output filename!\n");
                pszObjName = argv[i];
            }
        }
        else if (!strcmp(argv[i], "--kObjCache-both"))
            enmMode = kOC_BothArgv;
        else if (!strcmp(argv[i], "--kObjCache-options"))
            enmMode = kOC_Options;
        else if (!strcmp(argv[i], "--help"))
            return usage(stderr);
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
        else if (!strcmp(argv[i], "--named-pipe-compile"))
        {
            if (i + 1 >= argc)
                return SyntaxError("%s requires a pipe name!\n", argv[i]);
            pszNmPipeCompile = argv[++i];
            fRedirCompileStdIn = 0;
        }
        else if (!strcmp(argv[i], "-m") || !strcmp(argv[i], "--make-dep-file"))
        {
            if (i + 1 >= argc)
                return SyntaxError("%s requires a filename!\n", argv[i]);
            pszMakeDepFilename = argv[++i];
        }
        else if (!strcmp(argv[i], "--make-dep-fix-case"))
            fMakeDepFixCase = 1;
        else if (!strcmp(argv[i], "--make-dep-gen-stubs"))
            fMakeDepGenStubs = 1;
        else if (!strcmp(argv[i], "--make-dep-quiet"))
            fMakeDepQuiet = 1;
        else if (!strcmp(argv[i], "-O1") || !strcmp(argv[i], "--optimize-1"))
            fOptimizePreprocessorOutput = 1;
        else if (!strcmp(argv[i], "-O2") || !strcmp(argv[i], "--optimize-2"))
            fOptimizePreprocessorOutput = 1 | 2;
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
        {
            usage(stdout);
            return 0;
        }
        else if (!strcmp(argv[i], "-V") || !strcmp(argv[i], "--version"))
        {
            printf("kObjCache - kBuild version %d.%d.%d ($Revision: 3239 $)\n"
                   "Copyright (c) 2007-2012 knut st. osmundsen\n",
                   KBUILD_VERSION_MAJOR, KBUILD_VERSION_MINOR, KBUILD_VERSION_PATCH);
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
        return SyntaxError("No preprocessor arguments (--kObjCache-cc)!\n");

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
            memcpy(psz, ".koc", sizeof(".koc"));
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
    kOCEntrySetCppName(pEntry, pszPreCompName);
    kOCEntrySetCompileObjName(pEntry, pszObjName);
    kOCEntrySetCompileArgv(pEntry, papszArgvCompile, cArgvCompile);
    kOCEntrySetTarget(pEntry, pszTarget);
    kOCEntrySetPipedMode(pEntry, fRedirPreCompStdOut, fRedirCompileStdIn, pszNmPipeCompile);
    kOCEntrySetDepFilename(pEntry, pszMakeDepFilename, fMakeDepFixCase, fMakeDepQuiet, fMakeDepGenStubs);
    kOCEntrySetOptimizations(pEntry, fOptimizePreprocessorOutput);

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
        kOCEntryPreProcessAndCompile(pEntry, papszArgvPreComp, cArgvPreComp);
        kObjCacheLock(pCache);
    }
    else
    {
        /*
         * Do the preprocess (don't need to lock the cache file for this).
         */
        kObjCacheUnlock(pCache);
        kOCEntryPreProcess(pEntry, papszArgvPreComp, cArgvPreComp);

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
    if (fOptimizePreprocessorOutput)
    {
        InfoMsg(3, "g_cbMemMoved=%#x (%d)\n", g_cbMemMoved, g_cbMemMoved);
        InfoMsg(3, "g_cMemMoves=%#x (%d)\n", g_cMemMoves, g_cMemMoves);
    }

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

