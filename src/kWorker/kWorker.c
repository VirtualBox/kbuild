/* $Id: kWorker.c 2859 2016-09-01 16:34:31Z knut.osmundsen@oracle.com $ */
/** @file
 * kWorker - experimental process reuse worker for Windows.
 *
 * Note! This module must be linked statically in order to avoid
 *       accidentally intercepting our own CRT calls.
 */

/*
 * Copyright (c) 2016 knut st. osmundsen <bird-kBuild-spamx@anduin.net>
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include <k/kHlp.h>
#include <k/kLdr.h>

#include <stdio.h>
#include <intrin.h>
#include <setjmp.h>
#include <ctype.h>

#include "nt/ntstat.h"
#include "kbuild_version.h"
/* lib/nt_fullpath.c */
extern void nt_fullpath(const char *pszPath, char *pszFull, size_t cchFull);

#include "nt/ntstuff.h"

#include "nt/kFsCache.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** String constant comma length.   */
#define TUPLE(a_sz)                     a_sz, sizeof(a_sz) - 1

/** @def KW_LOG
 * Generic logging.
 * @param a     Argument list for kwDbgPrintf  */
#ifndef NDEBUG
# define KW_LOG(a) kwDbgPrintf a
#else
# define KW_LOG(a) do { } while (0)
#endif


/** @def KWFS_LOG
 * FS cache logging.
 * @param a     Argument list for kwDbgPrintf  */
#ifndef NDEBUG
# define KWFS_LOG(a) kwDbgPrintf a
#else
# define KWFS_LOG(a) do { } while (0)
#endif

/** Converts a windows handle to a handle table index.
 * @note We currently just mask off the 31th bit, and do no shifting or anything
 *     else to create an index of the handle.
 * @todo consider shifting by 2 or 3. */
#define KW_HANDLE_TO_INDEX(a_hHandle)   ((KUPTR)(a_hHandle) & ~(KUPTR)KU32_C(0x8000000))
/** Maximum handle value we can deal with.   */
#define KW_HANDLE_MAX                   0x20000

/** @def WITH_TEMP_MEMORY_FILES
 * Enables temporary memory files for cl.exe.  */
#define WITH_TEMP_MEMORY_FILES

/** Max temporary file size (memory backed).  */
#if K_ARCH_BITS >= 64
# define KWFS_TEMP_FILE_MAX             (256*1024*1024)
#else
# define KWFS_TEMP_FILE_MAX             (64*1024*1024)
#endif


/** User data key for tools. */
#define KW_DATA_KEY_TOOL                (~(KUPTR)16381)
/** User data key for a cached file. */
#define KW_DATA_KEY_CACHED_FILE         (~(KUPTR)65521)


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
typedef enum KWLOCATION
{
    KWLOCATION_INVALID = 0,
    KWLOCATION_EXE_DIR,
    KWLOCATION_IMPORTER_DIR,
    KWLOCATION_SYSTEM32,
    KWLOCATION_UNKNOWN_NATIVE,
    KWLOCATION_UNKNOWN,
} KWLOCATION;

typedef enum KWMODSTATE
{
    KWMODSTATE_INVALID = 0,
    KWMODSTATE_NEEDS_BITS,
    KWMODSTATE_NEEDS_INIT,
    KWMODSTATE_BEING_INITED,
    KWMODSTATE_INIT_FAILED,
    KWMODSTATE_READY,
} KWMODSTATE;

typedef struct KWMODULE *PKWMODULE;
typedef struct KWMODULE
{
    /** Pointer to the next image. */
    PKWMODULE           pNext;
    /** The normalized path to the image. */
    const char         *pszPath;
    /** The hash of the program path. */
    KU32                uHashPath;
    /** Number of references. */
    KU32                cRefs;
    /** UTF-16 version of pszPath. */
    const wchar_t      *pwszPath;
    /** The offset of the filename in pszPath. */
    KU16                offFilename;
    /** Set if executable. */
    KBOOL               fExe;
    /** Set if native module entry. */
    KBOOL               fNative;
    /** Loader module handle. */
    PKLDRMOD            pLdrMod;
    /** The windows module handle. */
    HMODULE             hOurMod;

    union
    {
        /** Data for a manually loaded image. */
        struct
        {
            /** The of the loaded image bits. */
            KSIZE               cbImage;
            /** Where we load the image. */
            void               *pvLoad;
            /** Virgin copy of the image. */
            void               *pvCopy;
            /** Ldr pvBits argument.  This is NULL till we've successfully resolved
             *  the imports. */
            void               *pvBits;
            /** The state. */
            KWMODSTATE          enmState;
            /** Number of imported modules. */
            KSIZE               cImpMods;
            /** Import array (variable size). */
            PKWMODULE           apImpMods[1];
        } Manual;
    } u;
} KWMODULE;


typedef struct KWDYNLOAD *PKWDYNLOAD;
typedef struct KWDYNLOAD
{
    /** Pointer to the next in the list. */
    PKWDYNLOAD          pNext;

    /** The normalized path to the image. */
    const char         *pszPath;
    /** The module name (within pszPath). */
    const char         *pszModName;
    /** UTF-16 version of pszPath. */
    const wchar_t      *pwszPath;
    /** The hash of the path. */
    KU32                uHashPath;

    /** The module handle we present to the application.
     * This is the LoadLibraryEx return value for special modules and the
     * KWMODULE.hOurMod value for the others. */
    HMODULE             hmod;

    /** The module for non-special resource stuff, NULL if special. */
    PKWMODULE           pMod;
} KWDYNLOAD;


/**
 * A cached file.
 */
typedef struct KFSWCACHEDFILE
{
    /** The user data core. */
    KFSUSERDATA         Core;

    /** Cached file handle. */
    HANDLE              hCached;
    /** The file size. */
    KU32                cbCached;
    /** Cached file content. */
    KU8                *pbCached;

    /** Circular self reference. Prevents the object from ever going away and
     * keeps it handy for debugging. */
    PKFSOBJ             pFsObj;
} KFSWCACHEDFILE;
/** Pointe to a cached filed. */
typedef KFSWCACHEDFILE *PKFSWCACHEDFILE;


typedef struct KWFSTEMPFILESEG *PKWFSTEMPFILESEG;
typedef struct KWFSTEMPFILESEG
{
    /** File offset of data. */
    KU32                offData;
    /** The size of the buffer pbData points to. */
    KU32                cbDataAlloc;
    /** The segment data. */
    KU8                *pbData;
} KWFSTEMPFILESEG;

typedef struct KWFSTEMPFILE *PKWFSTEMPFILE;
typedef struct KWFSTEMPFILE
{
    /** Pointer to the next temporary file for this run. */
    PKWFSTEMPFILE       pNext;
    /** The UTF-16 path. (Allocated after this structure.)  */
    const wchar_t      *pwszPath;
    /** The path length. */
    KU16                cwcPath;
    /** Number of active handles using this file/mapping (<= 2). */
    KU8                 cActiveHandles;
    /** Number of active mappings (mapped views) (0 or 1). */
    KU8                 cMappings;
    /** The amount of space allocated in the segments. */
    KU32                cbFileAllocated;
    /** The current file size. */
    KU32                cbFile;
    /** The number of segments. */
    KU32                cSegs;
    /** Segments making up the file. */
    PKWFSTEMPFILESEG    paSegs;
} KWFSTEMPFILE;


/** Handle type.   */
typedef enum KWHANDLETYPE
{
    KWHANDLETYPE_INVALID = 0,
    KWHANDLETYPE_FSOBJ_READ_CACHE,
    KWHANDLETYPE_TEMP_FILE,
    KWHANDLETYPE_TEMP_FILE_MAPPING
    //KWHANDLETYPE_CONSOLE_CACHE
} KWHANDLETYPE;

/** Handle data. */
typedef struct KWHANDLE
{
    KWHANDLETYPE        enmType;
    /** The current file offset. */
    KU32                offFile;
    /** Handle access. */
    KU32                dwDesiredAccess;
    /** The handle. */
    HANDLE              hHandle;

    /** Type specific data. */
    union
    {
        /** The file system object.   */
        PKFSWCACHEDFILE     pCachedFile;
        /** Temporary file handle or mapping handle. */
        PKWFSTEMPFILE       pTempFile;
    } u;
} KWHANDLE;
typedef KWHANDLE *PKWHANDLE;


typedef enum KWTOOLTYPE
{
    KWTOOLTYPE_INVALID = 0,
    KWTOOLTYPE_SANDBOXED,
    KWTOOLTYPE_WATCOM,
    KWTOOLTYPE_EXEC,
    KWTOOLTYPE_END
} KWTOOLTYPE;

typedef enum KWTOOLHINT
{
    KWTOOLHINT_INVALID = 0,
    KWTOOLHINT_NONE,
    KWTOOLHINT_VISUAL_CPP_CL,
    KWTOOLHINT_END
} KWTOOLHINT;


/**
 * A kWorker tool.
 */
typedef struct KWTOOL
{
    /** The user data core structure. */
    KFSUSERDATA         Core;

    /** The normalized path to the program. */
    const char         *pszPath;
    /** UTF-16 version of pszPath. */
    wchar_t const      *pwszPath;
    /** The kind of tool. */
    KWTOOLTYPE          enmType;

    union
    {
        struct
        {
            /** The executable. */
            PKWMODULE   pExe;
            /** List of dynamically loaded modules.
             * These will be kept loaded till the tool is destroyed (if we ever do that). */
            PKWDYNLOAD  pDynLoadHead;
            /** Tool hint (for hacks and such). */
            KWTOOLHINT  enmHint;
        } Sandboxed;
    } u;
} KWTOOL;
/** Pointer to a tool. */
typedef struct KWTOOL *PKWTOOL;


typedef struct KWSANDBOX *PKWSANDBOX;
typedef struct KWSANDBOX
{
    /** The tool currently running in the sandbox. */
    PKWTOOL     pTool;
    /** Jump buffer. */
    jmp_buf     JmpBuf;
    /** The thread ID of the main thread (owner of JmpBuf). */
    DWORD       idMainThread;
    /** Copy of the NT TIB of the main thread. */
    NT_TIB      TibMainThread;
    /** The exit code in case of longjmp.   */
    int         rcExitCode;

    /** The command line.   */
    char       *pszCmdLine;
    /** The UTF-16 command line. */
    wchar_t    *pwszCmdLine;
    /** Number of arguments in papszArgs. */
    int         cArgs;
    /** The argument vector. */
    char      **papszArgs;
    /** The argument vector. */
    wchar_t   **papwszArgs;

    /** The _pgmptr msvcrt variable.  */
    char       *pgmptr;
    /** The _wpgmptr msvcrt variable. */
    wchar_t    *wpgmptr;

    /** The _initenv msvcrt variable. */
    char      **initenv;
    /** The _winitenv msvcrt variable. */
    wchar_t   **winitenv;

    /** The _environ msvcrt variable. */
    char      **environ;
    /** The _wenviron msvcrt variable. */
    wchar_t   **wenviron;


    /** Handle table. */
    PKWHANDLE      *papHandles;
    /** Size of the handle table. */
    KU32            cHandles;
    /** Number of active handles in the table. */
    KU32            cActiveHandles;

    /** Head of the list of temporary file. */
    PKWFSTEMPFILE   pTempFileHead;

    UNICODE_STRING  SavedCommandLine;
} KWSANDBOX;

/** Replacement function entry. */
typedef struct KWREPLACEMENTFUNCTION
{
    /** The function name. */
    const char *pszFunction;
    /** The length of the function name. */
    KSIZE       cchFunction;
    /** The module name (optional). */
    const char *pszModule;
    /** The replacement function or data address. */
    KUPTR       pfnReplacement;
} KWREPLACEMENTFUNCTION;
typedef KWREPLACEMENTFUNCTION const *PCKWREPLACEMENTFUNCTION;

#if 0
/** Replacement function entry. */
typedef struct KWREPLACEMENTDATA
{
    /** The function name. */
    const char *pszFunction;
    /** The length of the function name. */
    KSIZE       cchFunction;
    /** The module name (optional). */
    const char *pszModule;
    /** Function providing the replacement. */
    KUPTR     (*pfnMakeReplacement)(PKWMODULE pMod, const char *pchSymbol, KSIZE cchSymbol);
} KWREPLACEMENTDATA;
typedef KWREPLACEMENTDATA const *PCKWREPLACEMENTDATA;
#endif


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** The sandbox data. */
static KWSANDBOX    g_Sandbox;

/** Module hash table. */
static PKWMODULE    g_apModules[127];

/** The file system cache. */
static PKFSCACHE    g_pFsCache;
/** The current directory (referenced). */
static PKFSOBJ      g_pCurDirObj = NULL;

/** Verbosity level. */
static int          g_cVerbose = 2;

/** Whether we should restart the worker. */
static KBOOL        g_fRestart = K_FALSE;

/* Further down. */
extern KWREPLACEMENTFUNCTION const g_aSandboxReplacements[];
extern KU32                  const g_cSandboxReplacements;

extern KWREPLACEMENTFUNCTION const g_aSandboxNativeReplacements[];
extern KU32                  const g_cSandboxNativeReplacements;

/** Create a larget BSS blob that with help of /IMAGEBASE:0x10000 should
 * cover the default executable link address of 0x400000. */
#pragma section("DefLdBuf", write, execute, read)
__declspec(allocate("DefLdBuf"))
static KU8          g_abDefLdBuf[16*1024*1024];



/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static FNKLDRMODGETIMPORT kwLdrModuleGetImportCallback;
static int kwLdrModuleResolveAndLookup(const char *pszName, PKWMODULE pExe, PKWMODULE pImporter, PKWMODULE *ppMod);
static KBOOL kwSandboxHandleTableEnter(PKWSANDBOX pSandbox, PKWHANDLE pHandle);



/**
 * Debug printing.
 * @param   pszFormat           Debug format string.
 * @param   ...                 Format argument.
 */
static void kwDbgPrintfV(const char *pszFormat, va_list va)
{
    if (g_cVerbose >= 2)
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
static void kwDbgPrintf(const char *pszFormat, ...)
{
    if (g_cVerbose >= 2)
    {
        va_list va;
        va_start(va, pszFormat);
        kwDbgPrintfV(pszFormat, va);
        va_end(va);
    }
}


/**
 * Debugger printing.
 * @param   pszFormat           Debug format string.
 * @param   ...                 Format argument.
 */
static void kwDebuggerPrintfV(const char *pszFormat, va_list va)
{
    if (IsDebuggerPresent())
    {
        DWORD const dwSavedErr = GetLastError();
        char szTmp[2048];

        _vsnprintf(szTmp, sizeof(szTmp), pszFormat, va);
        OutputDebugStringA(szTmp);

        SetLastError(dwSavedErr);
    }
}


/**
 * Debugger printing.
 * @param   pszFormat           Debug format string.
 * @param   ...                 Format argument.
 */
static void kwDebuggerPrintf(const char *pszFormat, ...)
{
    va_list va;
    va_start(va, pszFormat);
    kwDebuggerPrintfV(pszFormat, va);
    va_end(va);
}



/**
 * Error printing.
 * @param   pszFormat           Message format string.
 * @param   ...                 Format argument.
 */
static void kwErrPrintfV(const char *pszFormat, va_list va)
{
    DWORD const dwSavedErr = GetLastError();

    fprintf(stderr, "kWorker: error: ");
    vfprintf(stderr, pszFormat, va);

    SetLastError(dwSavedErr);
}


/**
 * Error printing.
 * @param   pszFormat           Message format string.
 * @param   ...                 Format argument.
 */
static void kwErrPrintf(const char *pszFormat, ...)
{
    va_list va;
    va_start(va, pszFormat);
    kwErrPrintfV(pszFormat, va);
    va_end(va);
}


/**
 * Error printing.
 * @return  rc;
 * @param   rc                  Return value
 * @param   pszFormat           Message format string.
 * @param   ...                 Format argument.
 */
static int kwErrPrintfRc(int rc, const char *pszFormat, ...)
{
    va_list va;
    va_start(va, pszFormat);
    kwErrPrintfV(pszFormat, va);
    va_end(va);
    return rc;
}


#ifdef K_STRICT

KHLP_DECL(void) kHlpAssertMsg1(const char *pszExpr, const char *pszFile, unsigned iLine, const char *pszFunction)
{
    DWORD const dwSavedErr = GetLastError();

    fprintf(stderr,
            "\n"
            "!!Assertion failed!!\n"
            "Expression: %s\n"
            "Function :  %s\n"
            "File:       %s\n"
            "Line:       %d\n"
            ,  pszExpr, pszFunction, pszFile, iLine);

    SetLastError(dwSavedErr);
}


KHLP_DECL(void) kHlpAssertMsg2(const char *pszFormat, ...)
{
    DWORD const dwSavedErr = GetLastError();
    va_list va;

    va_start(va, pszFormat);
    fprintf(stderr, pszFormat, va);
    va_end(va);

    SetLastError(dwSavedErr);
}

#endif /* K_STRICT */


/**
 * Hashes a string.
 *
 * @returns 32-bit string hash.
 * @param   pszString           String to hash.
 */
static KU32 kwStrHash(const char *pszString)
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
static KSIZE kwStrHashEx(const char *pszString, KU32 *puHash)
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
static KSIZE kwUtf16HashEx(const wchar_t *pwszString, KU32 *puHash)
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
    KFSLOOKUPERROR enmError;
    PKFSOBJ pFsObj = kFsCacheLookupA(g_pFsCache, pszPath, &enmError);
    if (pFsObj)
    {
        KBOOL fRc;
        fRc = kFsCacheObjGetFullPathA(pFsObj, pszNormPath, cbNormPath, '\\');
        kFsCacheObjRelease(g_pFsCache, pFsObj);
        if (fRc)
            return 0;
        return KERR_BUFFER_OVERFLOW;
    }
    return KERR_FILE_NOT_FOUND;
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
 * Retains a new reference to the given module
 * @returns pMod
 * @param   pMod                The module to retain.
 */
static PKWMODULE kwLdrModuleRetain(PKWMODULE pMod)
{
    kHlpAssert(pMod->cRefs > 0);
    kHlpAssert(pMod->cRefs < 64);
    pMod->cRefs++;
    return pMod;
}


/**
 * Releases a module reference.
 *
 * @param   pMod                The module to release.
 */
static void kwLdrModuleRelease(PKWMODULE pMod)
{
    if (--pMod->cRefs == 0)
    {
        /* Unlink it. */
        if (!pMod->fExe)
        {
            PKWMODULE pPrev = NULL;
            unsigned  idx   = pMod->uHashPath % K_ELEMENTS(g_apModules);
            if (g_apModules[idx] == pMod)
                g_apModules[idx] = pMod->pNext;
            else
            {
                PKWMODULE pPrev = g_apModules[idx];
                kHlpAssert(pPrev != NULL);
                while (pPrev->pNext != pMod)
                {
                    pPrev = pPrev->pNext;
                    kHlpAssert(pPrev != NULL);
                }
                pPrev->pNext = pMod->pNext;
            }
        }

        /* Release import modules. */
        if (!pMod->fNative)
        {
            KSIZE idx = pMod->u.Manual.cImpMods;
            while (idx-- > 0)
                if (pMod->u.Manual.apImpMods[idx])
                {
                    kwLdrModuleRelease(pMod->u.Manual.apImpMods[idx]);
                    pMod->u.Manual.apImpMods[idx] = NULL;
                }
        }

        /* Free our resources. */
        kLdrModClose(pMod->pLdrMod);
        pMod->pLdrMod = NULL;

        if (!pMod->fNative)
        {
            kHlpPageFree(pMod->u.Manual.pvCopy, pMod->u.Manual.cbImage);
            kHlpPageFree(pMod->u.Manual.pvLoad, pMod->u.Manual.cbImage);
        }

        kHlpFree(pMod);
    }
    else
        kHlpAssert(pMod->cRefs < 64);
}


/**
 * Links the module into the module hash table.
 *
 * @returns pMod
 * @param   pMod                The module to link.
 */
static PKWMODULE kwLdrModuleLink(PKWMODULE pMod)
{
    unsigned idx = pMod->uHashPath % K_ELEMENTS(g_apModules);
    pMod->pNext = g_apModules[idx];
    g_apModules[idx] = pMod;
    return pMod;
}


/**
 * Replaces imports for this module according to g_aSandboxNativeReplacements.
 *
 * @param   pMod                The natively loaded module to process.
 */
static void kwLdrModuleDoNativeImportReplacements(PKWMODULE pMod)
{
    KSIZE const                 cbImage = kLdrModSize(pMod->pLdrMod);
    KU8 const * const           pbImage = (KU8 const *)pMod->hOurMod;
    IMAGE_DOS_HEADER const     *pMzHdr  = (IMAGE_DOS_HEADER const *)pbImage;
    IMAGE_NT_HEADERS const     *pNtHdrs;
    IMAGE_DATA_DIRECTORY const *pDirEnt;

    kHlpAssert(pMod->fNative);

    /*
     * Locate the export descriptors.
     */
    /* MZ header. */
    if (pMzHdr->e_magic == IMAGE_DOS_SIGNATURE)
    {
        kHlpAssertReturnVoid(pMzHdr->e_lfanew <= cbImage - sizeof(*pNtHdrs));
        pNtHdrs = (IMAGE_NT_HEADERS const *)&pbImage[pMzHdr->e_lfanew];
    }
    else
        pNtHdrs = (IMAGE_NT_HEADERS const *)pbImage;

    /* Check PE header. */
    kHlpAssertReturnVoid(pNtHdrs->Signature == IMAGE_NT_SIGNATURE);
    kHlpAssertReturnVoid(pNtHdrs->FileHeader.SizeOfOptionalHeader == sizeof(pNtHdrs->OptionalHeader));

    /* Locate the import descriptor array. */
    pDirEnt = (IMAGE_DATA_DIRECTORY const *)&pNtHdrs->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (   pDirEnt->Size > 0
        && pDirEnt->VirtualAddress != 0)
    {
        const IMAGE_IMPORT_DESCRIPTOR  *pImpDesc    = (const IMAGE_IMPORT_DESCRIPTOR *)&pbImage[pDirEnt->VirtualAddress];
        KU32                            cLeft       = pDirEnt->Size / sizeof(*pImpDesc);
        MEMORY_BASIC_INFORMATION        ProtInfo    = { NULL, NULL, 0, 0, 0, 0, 0 };
        KU8                            *pbProtRange = NULL;
        SIZE_T                          cbProtRange = 0;
        DWORD                           fOldProt    = 0;
        KU32 const                      cbPage      = 0x1000;
        BOOL                            fRc;


        kHlpAssertReturnVoid(pDirEnt->VirtualAddress < cbImage);
        kHlpAssertReturnVoid(pDirEnt->Size < cbImage);
        kHlpAssertReturnVoid(pDirEnt->VirtualAddress + pDirEnt->Size <= cbImage);

        /*
         * Walk the import descriptor array.
         * Note! This only works if there's a backup thunk array, otherwise we cannot get at the name.
         */
        while (   cLeft-- > 0
               && pImpDesc->Name > 0
               && pImpDesc->FirstThunk > 0)
        {
            KU32                iThunk;
            const char * const  pszImport   = (const char *)&pbImage[pImpDesc->Name];
            PIMAGE_THUNK_DATA   paThunks    = (PIMAGE_THUNK_DATA)&pbImage[pImpDesc->FirstThunk];
            PIMAGE_THUNK_DATA   paOrgThunks = (PIMAGE_THUNK_DATA)&pbImage[pImpDesc->OriginalFirstThunk];
            kHlpAssertReturnVoid(pImpDesc->Name < cbImage);
            kHlpAssertReturnVoid(pImpDesc->FirstThunk < cbImage);
            kHlpAssertReturnVoid(pImpDesc->OriginalFirstThunk < cbImage);
            kHlpAssertReturnVoid(pImpDesc->OriginalFirstThunk != pImpDesc->FirstThunk);
            kHlpAssertReturnVoid(pImpDesc->OriginalFirstThunk);

            /* Iterate the thunks. */
            for (iThunk = 0; paOrgThunks[iThunk].u1.Ordinal != 0; iThunk++)
            {
                KUPTR const off = paOrgThunks[iThunk].u1.Function;
                kHlpAssertReturnVoid(off < cbImage);
                if (!IMAGE_SNAP_BY_ORDINAL(off))
                {
                    IMAGE_IMPORT_BY_NAME const *pName     = (IMAGE_IMPORT_BY_NAME const *)&pbImage[off];
                    KSIZE const                 cchSymbol = kHlpStrLen(pName->Name);
                    KU32                        i         = g_cSandboxNativeReplacements;
                    while (i-- > 0)
                        if (   g_aSandboxNativeReplacements[i].cchFunction == cchSymbol
                            && kHlpMemComp(g_aSandboxNativeReplacements[i].pszFunction, pName->Name, cchSymbol) == 0)
                        {
                            if (   !g_aSandboxNativeReplacements[i].pszModule
                                || kHlpStrICompAscii(g_aSandboxNativeReplacements[i].pszModule, pszImport) == 0)
                            {
                                KW_LOG(("%s: replacing %s!%s\n", pMod->pLdrMod->pszName, pszImport, pName->Name));

                                /* The .rdata section is normally read-only, so we need to make it writable first. */
                                if ((KUPTR)&paThunks[iThunk] - (KUPTR)pbProtRange >= cbPage)
                                {
                                    /* Restore previous .rdata page. */
                                    if (fOldProt)
                                    {
                                        fRc = VirtualProtect(pbProtRange, cbProtRange, fOldProt, NULL /*pfOldProt*/);
                                        kHlpAssert(fRc);
                                        fOldProt = 0;
                                    }

                                    /* Query attributes for the current .rdata page. */
                                    pbProtRange = (KU8 *)((KUPTR)&paThunks[iThunk] & ~(KUPTR)(cbPage - 1));
                                    cbProtRange = VirtualQuery(pbProtRange, &ProtInfo, sizeof(ProtInfo));
                                    kHlpAssert(cbProtRange);
                                    if (cbProtRange)
                                    {
                                        switch (ProtInfo.Protect)
                                        {
                                            case PAGE_READWRITE:
                                            case PAGE_WRITECOPY:
                                            case PAGE_EXECUTE_READWRITE:
                                            case PAGE_EXECUTE_WRITECOPY:
                                                /* Already writable, nothing to do. */
                                                break;

                                            default:
                                                kHlpAssertMsgFailed(("%#x\n", ProtInfo.Protect));
                                            case PAGE_READONLY:
                                                cbProtRange = cbPage;
                                                fRc = VirtualProtect(pbProtRange, cbProtRange, PAGE_READWRITE, &fOldProt);
                                                break;

                                            case PAGE_EXECUTE:
                                            case PAGE_EXECUTE_READ:
                                                cbProtRange = cbPage;
                                                fRc = VirtualProtect(pbProtRange, cbProtRange, PAGE_EXECUTE_READWRITE, &fOldProt);
                                                break;
                                        }
                                        kHlpAssertStmt(fRc, fOldProt = 0);
                                    }
                                }

                                paThunks[iThunk].u1.AddressOfData = g_aSandboxNativeReplacements[i].pfnReplacement;
                                break;
                            }
                        }
                }
            }


            /* Next import descriptor. */
            pImpDesc++;
        }


        if (fOldProt)
        {
            DWORD fIgnore = 0;
            fRc = VirtualProtect(pbProtRange, cbProtRange, fOldProt, &fIgnore);
            kHlpAssertMsg(fRc, ("%u\n", GetLastError())); K_NOREF(fRc);
        }
    }

}


/**
 * Creates a module using the native loader.
 *
 * @returns Module w/ 1 reference on success, NULL on failure.
 * @param   pszPath             The normalized path to the module.
 * @param   uHashPath           The module path hash.
 * @param   fDoReplacements     Whether to do import replacements on this
 *                              module.
 */
static PKWMODULE kwLdrModuleCreateNative(const char *pszPath, KU32 uHashPath, KBOOL fDoReplacements)
{
    /*
     * Open the module and check the type.
     */
    PKLDRMOD pLdrMod;
    int rc = kLdrModOpenNative(pszPath, &pLdrMod);
    if (rc == 0)
    {
        /*
         * Create the entry.
         */
        KSIZE     cbPath = kHlpStrLen(pszPath) + 1;
        PKWMODULE pMod   = (PKWMODULE)kHlpAllocZ(sizeof(*pMod) + cbPath + 1 + cbPath * 2 * sizeof(wchar_t));
        if (pMod)
        {
            pMod->pszPath       = (char *)kHlpMemCopy(pMod + 1, pszPath, cbPath);
            pMod->pwszPath      = (wchar_t *)(pMod->pszPath + cbPath + (cbPath & 1));
            kwStrToUtf16(pMod->pszPath, (wchar_t *)pMod->pwszPath, cbPath * 2);
            pMod->uHashPath     = uHashPath;
            pMod->cRefs         = 1;
            pMod->offFilename   = (KU16)(kHlpGetFilename(pszPath) - pszPath);
            pMod->fExe          = K_FALSE;
            pMod->fNative       = K_TRUE;
            pMod->pLdrMod       = pLdrMod;
            pMod->hOurMod       = (HMODULE)(KUPTR)pLdrMod->aSegments[0].MapAddress;

            if (fDoReplacements)
            {
                DWORD const dwSavedErr = GetLastError();
                kwLdrModuleDoNativeImportReplacements(pMod);
                SetLastError(dwSavedErr);
            }

            KW_LOG(("New module: %p LB %#010x %s (native)\n",
                    (KUPTR)pMod->pLdrMod->aSegments[0].MapAddress, kLdrModSize(pMod->pLdrMod), pMod->pszPath));
            return kwLdrModuleLink(pMod);
        }
        //kLdrModClose(pLdrMod);
    }
    return NULL;
}


/**
 * Creates a module using the our own loader.
 *
 * @returns Module w/ 1 reference on success, NULL on failure.
 * @param   pszPath             The normalized path to the module.
 * @param   uHashPath           The module path hash.
 * @param   fExe                K_TRUE if this is an executable image, K_FALSE
 *                              if not.  Executable images does not get entered
 *                              into the global module table.
 * @param   pExeMod             The executable module of the process (for
 *                              resolving imports).  NULL if fExe is set.
 */
static PKWMODULE kwLdrModuleCreateNonNative(const char *pszPath, KU32 uHashPath, KBOOL fExe, PKWMODULE pExeMod)
{
    /*
     * Open the module and check the type.
     */
    PKLDRMOD pLdrMod;
    int rc = kLdrModOpen(pszPath, 0 /*fFlags*/, (KCPUARCH)K_ARCH, &pLdrMod);
    if (rc == 0)
    {
        switch (pLdrMod->enmType)
        {
            case KLDRTYPE_EXECUTABLE_FIXED:
            case KLDRTYPE_EXECUTABLE_RELOCATABLE:
            case KLDRTYPE_EXECUTABLE_PIC:
                if (!fExe)
                    rc = KERR_GENERAL_FAILURE;
                break;

            case KLDRTYPE_SHARED_LIBRARY_RELOCATABLE:
            case KLDRTYPE_SHARED_LIBRARY_PIC:
            case KLDRTYPE_SHARED_LIBRARY_FIXED:
                if (fExe)
                    rc = KERR_GENERAL_FAILURE;
                break;

            default:
                rc = KERR_GENERAL_FAILURE;
                break;
        }
        if (rc == 0)
        {
            KI32 cImports = kLdrModNumberOfImports(pLdrMod, NULL /*pvBits*/);
            if (cImports >= 0)
            {
                /*
                 * Create the entry.
                 */
                KSIZE     cbPath = kHlpStrLen(pszPath) + 1;
                PKWMODULE pMod   = (PKWMODULE)kHlpAllocZ(sizeof(*pMod)
                                                         + sizeof(pMod) * cImports
                                                         + cbPath
                                                         + cbPath * 2 * sizeof(wchar_t));
                if (pMod)
                {
                    KBOOL fFixed;

                    pMod->cRefs         = 1;
                    pMod->offFilename   = (KU16)(kHlpGetFilename(pszPath) - pszPath);
                    pMod->uHashPath     = uHashPath;
                    pMod->fExe          = fExe;
                    pMod->fNative       = K_FALSE;
                    pMod->pLdrMod       = pLdrMod;
                    pMod->u.Manual.cImpMods = (KU32)cImports;
                    pMod->pszPath       = (char *)kHlpMemCopy(&pMod->u.Manual.apImpMods[cImports + 1], pszPath, cbPath);
                    pMod->pwszPath      = (wchar_t *)(pMod->pszPath + cbPath + (cbPath & 1));
                    kwStrToUtf16(pMod->pszPath, (wchar_t *)pMod->pwszPath, cbPath * 2);

                    /*
                     * Figure out where to load it and get memory there.
                     */
                    fFixed = pLdrMod->enmType == KLDRTYPE_EXECUTABLE_FIXED
                          || pLdrMod->enmType == KLDRTYPE_SHARED_LIBRARY_FIXED;
                    pMod->u.Manual.pvLoad = fFixed ? (void *)(KUPTR)pLdrMod->aSegments[0].LinkAddress : NULL;
                    pMod->u.Manual.cbImage = kLdrModSize(pLdrMod);
                    if (   !fFixed
                        || (KUPTR)pMod->u.Manual.pvLoad - (KUPTR)g_abDefLdBuf >= sizeof(g_abDefLdBuf)
                        || sizeof(g_abDefLdBuf) - (KUPTR)pMod->u.Manual.pvLoad - (KUPTR)g_abDefLdBuf < pMod->u.Manual.cbImage)
                        rc = kHlpPageAlloc(&pMod->u.Manual.pvLoad, pMod->u.Manual.cbImage, KPROT_EXECUTE_READWRITE, fFixed);
                    if (rc == 0)
                    {
                        rc = kHlpPageAlloc(&pMod->u.Manual.pvCopy, pMod->u.Manual.cbImage, KPROT_READWRITE, K_FALSE);
                        if (rc == 0)
                        {

                            KI32 iImp;

                            /*
                             * Link the module (unless it's an executable image) and process the imports.
                             */
                            pMod->hOurMod = (HMODULE)pMod->u.Manual.pvLoad;
                            if (!fExe)
                                kwLdrModuleLink(pMod);
                            KW_LOG(("New module: %p LB %#010x %s (kLdr)\n",
                                    pMod->u.Manual.pvLoad, pMod->u.Manual.cbImage, pMod->pszPath));
                            kwDebuggerPrintf("TODO: .reload /f %s=%p\n", pMod->pszPath, pMod->u.Manual.pvLoad);

                            for (iImp = 0; iImp < cImports; iImp++)
                            {
                                char szName[1024];
                                rc = kLdrModGetImport(pMod->pLdrMod, NULL /*pvBits*/, iImp, szName, sizeof(szName));
                                if (rc == 0)
                                {
                                    rc = kwLdrModuleResolveAndLookup(szName, pExeMod, pMod, &pMod->u.Manual.apImpMods[iImp]);
                                    if (rc == 0)
                                        continue;
                                }
                                break;
                            }

                            if (rc == 0)
                            {
                                rc = kLdrModGetBits(pLdrMod, pMod->u.Manual.pvCopy, (KUPTR)pMod->u.Manual.pvLoad,
                                                    kwLdrModuleGetImportCallback, pMod);
                                if (rc == 0)
                                {
                                    pMod->u.Manual.pvBits = pMod->u.Manual.pvCopy;
                                    pMod->u.Manual.enmState = KWMODSTATE_NEEDS_BITS;
                                    return pMod;
                                }
                            }

                            kwLdrModuleRelease(pMod);
                            return NULL;
                        }

                        kHlpPageFree(pMod->u.Manual.pvLoad, pMod->u.Manual.cbImage);
                        kwErrPrintf("Failed to allocate %#x bytes\n", pMod->u.Manual.cbImage);
                    }
                    else if (fFixed)
                        kwErrPrintf("Failed to allocate %#x bytes at %p\n",
                                    pMod->u.Manual.cbImage, (void *)(KUPTR)pLdrMod->aSegments[0].LinkAddress);
                    else
                        kwErrPrintf("Failed to allocate %#x bytes\n", pMod->u.Manual.cbImage);
                }
            }
        }
        kLdrModClose(pLdrMod);
    }
    else
        kwErrPrintf("kLdrOpen failed with %#x (%d) for %s\n", rc, rc, pszPath);
    return NULL;
}


/** Implements FNKLDRMODGETIMPORT, used by kwLdrModuleCreate. */
static int kwLdrModuleGetImportCallback(PKLDRMOD pMod, KU32 iImport, KU32 iSymbol, const char *pchSymbol, KSIZE cchSymbol,
                                        const char *pszVersion, PKLDRADDR puValue, KU32 *pfKind, void *pvUser)
{
    PKWMODULE pCurMod = (PKWMODULE)pvUser;
    PKWMODULE pImpMod = pCurMod->u.Manual.apImpMods[iImport];
    int rc;
    K_NOREF(pMod);

    if (pImpMod->fNative)
        rc = kLdrModQuerySymbol(pImpMod->pLdrMod, NULL /*pvBits*/, KLDRMOD_BASEADDRESS_MAP,
                                iSymbol, pchSymbol, cchSymbol, pszVersion,
                                NULL /*pfnGetForwarder*/, NULL /*pvUSer*/,
                                puValue, pfKind);
    else
        rc = kLdrModQuerySymbol(pImpMod->pLdrMod, pImpMod->u.Manual.pvBits, (KUPTR)pImpMod->u.Manual.pvLoad,
                                iSymbol, pchSymbol, cchSymbol, pszVersion,
                                NULL /*pfnGetForwarder*/, NULL /*pvUSer*/,
                                puValue, pfKind);
    if (rc == 0)
    {
        KU32 i = g_cSandboxReplacements;
        while (i-- > 0)
            if (   g_aSandboxReplacements[i].cchFunction == cchSymbol
                && kHlpMemComp(g_aSandboxReplacements[i].pszFunction, pchSymbol, cchSymbol) == 0)
            {
                if (   !g_aSandboxReplacements[i].pszModule
                    || kHlpStrICompAscii(g_aSandboxReplacements[i].pszModule, &pImpMod->pszPath[pImpMod->offFilename]) == 0)
                {
                    KW_LOG(("replacing %s!%s\n", &pImpMod->pszPath[pImpMod->offFilename], g_aSandboxReplacements[i].pszFunction));
                    *puValue = g_aSandboxReplacements[i].pfnReplacement;
                    break;
                }
            }
    }

    //printf("iImport=%u (%s) %*.*s rc=%d\n", iImport, &pImpMod->pszPath[pImpMod->offFilename], cchSymbol, cchSymbol, pchSymbol, rc);
    return rc;

}


/**
 * Gets the main entrypoint for a module.
 *
 * @returns 0 on success, KERR on failure
 * @param   pMod                The module.
 * @param   puAddrMain          Where to return the address.
 */
static int kwLdrModuleQueryMainEntrypoint(PKWMODULE pMod, KUPTR *puAddrMain)
{
    KLDRADDR uLdrAddrMain;
    int rc = kLdrModQueryMainEntrypoint(pMod->pLdrMod,  pMod->u.Manual.pvBits, (KUPTR)pMod->u.Manual.pvLoad, &uLdrAddrMain);
    if (rc == 0)
    {
        *puAddrMain = (KUPTR)uLdrAddrMain;
        return 0;
    }
    return rc;
}


/**
 * Whether to apply g_aSandboxNativeReplacements to the imports of this module.
 *
 * @returns K_TRUE/K_FALSE.
 * @param   pszFilename         The filename (no path).
 * @param   enmLocation         The location.
 */
static KBOOL kwLdrModuleShouldDoNativeReplacements(const char *pszFilename, KWLOCATION enmLocation)
{
    if (enmLocation != KWLOCATION_SYSTEM32)
        return K_TRUE;
    return kHlpStrNICompAscii(pszFilename, TUPLE("msvc"))   == 0
        || kHlpStrNICompAscii(pszFilename, TUPLE("msdis"))  == 0
        || kHlpStrNICompAscii(pszFilename, TUPLE("mspdb"))  == 0;
}


/**
 * Whether we can load this DLL natively or not.
 *
 * @returns K_TRUE/K_FALSE.
 * @param   pszFilename         The filename (no path).
 * @param   enmLocation         The location.
 */
static KBOOL kwLdrModuleCanLoadNatively(const char *pszFilename, KWLOCATION enmLocation)
{
    if (enmLocation == KWLOCATION_SYSTEM32)
        return K_TRUE;
    if (enmLocation == KWLOCATION_UNKNOWN_NATIVE)
        return K_TRUE;
    return kHlpStrNICompAscii(pszFilename, TUPLE("msvc"))   == 0
        || kHlpStrNICompAscii(pszFilename, TUPLE("msdis"))  == 0
        || kHlpStrNICompAscii(pszFilename, TUPLE("mspdb"))  == 0;
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
        KFSLOOKUPERROR enmError;
        PKFSOBJ pFsObj = kFsCacheLookupNoMissingA(g_pFsCache, pszPath, &enmError);
        if (pFsObj)
        {
            KBOOL fRc = pFsObj->bObjType == KFSOBJ_TYPE_FILE;
            kFsCacheObjRelease(g_pFsCache, pFsObj);
            return fRc;
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
 * Worker for kwLdrModuleResolveAndLookup that checks out one possibility.
 *
 * If the file exists, we consult the module hash table before trying to load it
 * off the disk.
 *
 * @returns Pointer to module on success, NULL if not found, ~(KUPTR)0 on
 *          failure.
 * @param   pszPath             The name of the import module.
 * @param   enmLocation         The location we're searching.  This is used in
 *                              the heuristics for determining if we can use the
 *                              native loader or need to sandbox the DLL.
 * @param   pExe                The executable (optional).
 */
static PKWMODULE kwLdrModuleTryLoadDll(const char *pszPath, KWLOCATION enmLocation, PKWMODULE pExeMod)
{
    /*
     * Does the file exists and is it a regular file?
     */
    if (kwLdrModuleIsRegularFile(pszPath))
    {
        /*
         * Yes! Normalize it and look it up in the hash table.
         */
        char szNormPath[1024];
        int rc = kwPathNormalize(pszPath, szNormPath, sizeof(szNormPath));
        if (rc == 0)
        {
            const char *pszName;
            KU32 const  uHashPath = kwStrHash(szNormPath);
            unsigned    idxHash   = uHashPath % K_ELEMENTS(g_apModules);
            PKWMODULE   pMod      = g_apModules[idxHash];
            if (pMod)
            {
                do
                {
                    if (   pMod->uHashPath == uHashPath
                        && kHlpStrComp(pMod->pszPath, szNormPath) == 0)
                        return kwLdrModuleRetain(pMod);
                    pMod = pMod->pNext;
                } while (pMod);
            }

            /*
             * Not in the hash table, so we have to load it from scratch.
             */
            pszName = kHlpGetFilename(szNormPath);
            if (kwLdrModuleCanLoadNatively(pszName, enmLocation))
                pMod = kwLdrModuleCreateNative(szNormPath, uHashPath,
                                               kwLdrModuleShouldDoNativeReplacements(pszName, enmLocation));
            else
                pMod = kwLdrModuleCreateNonNative(szNormPath, uHashPath, K_FALSE /*fExe*/, pExeMod);
            if (pMod)
                return pMod;
            return (PKWMODULE)~(KUPTR)0;
        }
    }
    return NULL;
}


/**
 * Gets a reference to the module by the given name.
 *
 * We must do the search path thing, as our hash table may multiple DLLs with
 * the same base name due to different tools version and similar.  We'll use a
 * modified search sequence, though.  No point in searching the current
 * directory for instance.
 *
 * @returns 0 on success, KERR on failure.
 * @param   pszName             The name of the import module.
 * @param   pExe                The executable (optional).
 * @param   pImporter           The module doing the importing (optional).
 * @param   ppMod               Where to return the module pointer w/ reference.
 */
static int kwLdrModuleResolveAndLookup(const char *pszName, PKWMODULE pExe, PKWMODULE pImporter, PKWMODULE *ppMod)
{
    KSIZE const cchName = kHlpStrLen(pszName);
    char        szPath[1024];
    PKWMODULE   pMod = NULL;


    /* The import path. */
    if (pMod == NULL && pImporter != NULL)
    {
        if (pImporter->offFilename + cchName >= sizeof(szPath))
            return KERR_BUFFER_OVERFLOW;
        kHlpMemCopy(kHlpMemPCopy(szPath, pImporter->pszPath, pImporter->offFilename), pszName, cchName + 1);
        pMod = kwLdrModuleTryLoadDll(szPath, KWLOCATION_IMPORTER_DIR, pExe);
    }

    /* Application directory first. */
    if (pMod == NULL && pExe != NULL && pExe != pImporter)
    {
        if (pExe->offFilename + cchName >= sizeof(szPath))
            return KERR_BUFFER_OVERFLOW;
        kHlpMemCopy(kHlpMemPCopy(szPath, pExe->pszPath, pExe->offFilename), pszName, cchName + 1);
        pMod = kwLdrModuleTryLoadDll(szPath, KWLOCATION_EXE_DIR, pExe);
    }

    /* The windows directory. */
    if (pMod == NULL)
    {
        UINT cchDir = GetSystemDirectoryA(szPath, sizeof(szPath));
        if (   cchDir <= 2
            || cchDir + 1 + cchName >= sizeof(szPath))
            return KERR_BUFFER_OVERFLOW;
        szPath[cchDir++] = '\\';
        kHlpMemCopy(&szPath[cchDir], pszName, cchName + 1);
        pMod = kwLdrModuleTryLoadDll(szPath, KWLOCATION_SYSTEM32, pExe);
    }

    /* Return. */
    if (pMod != NULL && pMod != (PKWMODULE)~(KUPTR)0)
    {
        *ppMod = pMod;
        return 0;
    }
    *ppMod = NULL;
    return KERR_GENERAL_FAILURE;
}


/**
 * Does module initialization starting at @a pMod.
 *
 * This is initially used on the executable.  Later it is used by the
 * LoadLibrary interceptor.
 *
 * @returns 0 on success, error on failure.
 * @param   pMod                The module to initialize.
 */
static int kwLdrModuleInitTree(PKWMODULE pMod)
{
    int rc = 0;
    if (!pMod->fNative)
    {
        /* Need to copy bits? */
        if (pMod->u.Manual.enmState == KWMODSTATE_NEEDS_BITS)
        {
            kHlpMemCopy(pMod->u.Manual.pvLoad, pMod->u.Manual.pvCopy, pMod->u.Manual.cbImage);
            pMod->u.Manual.enmState = KWMODSTATE_NEEDS_INIT;
        }

        if (pMod->u.Manual.enmState == KWMODSTATE_NEEDS_INIT)
        {
            /* Must do imports first, but mark our module as being initialized to avoid
               endless recursion should there be a dependency loop. */
            KSIZE iImp;
            pMod->u.Manual.enmState = KWMODSTATE_BEING_INITED;

            for (iImp = 0; iImp < pMod->u.Manual.cImpMods; iImp++)
            {
                rc = kwLdrModuleInitTree(pMod->u.Manual.apImpMods[iImp]);
                if (rc != 0)
                    return rc;
            }

            rc = kLdrModCallInit(pMod->pLdrMod, pMod->u.Manual.pvLoad, (KUPTR)pMod->u.Manual.pvLoad);
            if (rc == 0)
                pMod->u.Manual.enmState = KWMODSTATE_READY;
            else
                pMod->u.Manual.enmState = KWMODSTATE_INIT_FAILED;
        }
    }
    return rc;
}




/**
 * Creates a tool entry and inserts it.
 *
 * @returns Pointer to the tool entry.  NULL on failure.
 * @param   pToolFsObj          The file object of the tool.  The created tool
 *                              will be associated with it.
 *
 *                              A reference is donated by the caller and must be
 *                              released.
 */
static PKWTOOL kwToolEntryCreate(PKFSOBJ pToolFsObj)
{
    KSIZE   cwcPath = pToolFsObj->cwcParent + pToolFsObj->cwcName + 1;
    KSIZE   cbPath  = pToolFsObj->cchParent + pToolFsObj->cchName + 1;
    PKWTOOL pTool   = (PKWTOOL)kFsCacheObjAddUserData(g_pFsCache, pToolFsObj, KW_DATA_KEY_TOOL,
                                                      sizeof(*pTool) + cwcPath * sizeof(wchar_t) + cbPath);
    if (pTool)
    {
        KBOOL fRc;
        pTool->pwszPath = (wchar_t const *)(pTool + 1);
        fRc = kFsCacheObjGetFullPathW(pToolFsObj, (wchar_t *)pTool->pwszPath, cwcPath, '\\');
        kHlpAssert(fRc); K_NOREF(fRc);

        pTool->pszPath = (char const *)&pTool->pwszPath[cwcPath];
        fRc = kFsCacheObjGetFullPathA(pToolFsObj, (char *)pTool->pszPath, cbPath, '\\');
        kHlpAssert(fRc);

        pTool->enmType = KWTOOLTYPE_SANDBOXED;
        pTool->u.Sandboxed.pExe = kwLdrModuleCreateNonNative(pTool->pszPath, kwStrHash(pTool->pszPath), K_TRUE /*fExe*/, NULL);
        if (!pTool->u.Sandboxed.pExe)
            pTool->enmType = KWTOOLTYPE_EXEC;
        else if (kHlpStrICompAscii(pToolFsObj->pszName, "cl.exe") == 0)
            pTool->u.Sandboxed.enmHint = KWTOOLHINT_VISUAL_CPP_CL;
        else
            pTool->u.Sandboxed.enmHint = KWTOOLHINT_NONE;

        kFsCacheObjRelease(g_pFsCache, pToolFsObj);
        return pTool;
    }
    kFsCacheObjRelease(g_pFsCache, pToolFsObj);
    return NULL;
}


/**
 * Looks up the given tool, creating a new tool table entry if necessary.
 *
 * @returns Pointer to the tool entry.  NULL on failure.
 * @param   pszExe              The executable for the tool (not normalized).
 */
static PKWTOOL kwToolLookup(const char *pszExe)
{
    /*
     * We associate the tools instances with the file system objects.
     */
    KFSLOOKUPERROR  enmError;
    PKFSOBJ         pToolFsObj = kFsCacheLookupA(g_pFsCache, pszExe, &enmError);
    if (pToolFsObj)
    {
        if (pToolFsObj->bObjType == KFSOBJ_TYPE_FILE)
        {
            PKWTOOL pTool = (PKWTOOL)kFsCacheObjGetUserData(g_pFsCache, pToolFsObj, KW_DATA_KEY_TOOL);
            if (pTool)
            {
                kFsCacheObjRelease(g_pFsCache, pToolFsObj);
                return pTool;
            }

            /*
             * Need to create a new tool.
             */
            return kwToolEntryCreate(pToolFsObj);
        }
        kFsCacheObjRelease(g_pFsCache, pToolFsObj);
    }
    return NULL;
}



/*
 *
 * File system cache.
 * File system cache.
 * File system cache.
 *
 */



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



/**
 * Parses the argument string passed in as pszSrc.
 *
 * @returns size of the processed arguments.
 * @param   pszSrc  Pointer to the commandline that's to be parsed.
 * @param   pcArgs  Where to return the number of arguments.
 * @param   argv    Pointer to argument vector to put argument pointers in. NULL allowed.
 * @param   pchPool Pointer to memory pchPool to put the arguments into. NULL allowed.
 *
 * @remarks Lifted from startuphacks-win.c
 */
static int parse_args(const char *pszSrc, int *pcArgs, char **argv, char *pchPool)
{
    int   bs;
    char  chQuote;
    char *pfFlags;
    int   cbArgs;
    int   cArgs;

#define PUTC(c) do { ++cbArgs; if (pchPool != NULL) *pchPool++ = (c); } while (0)
#define PUTV    do { ++cArgs;  if (argv != NULL) *argv++ = pchPool; } while (0)
#define WHITE(c) ((c) == ' ' || (c) == '\t')

#define _ARG_DQUOTE   0x01          /* Argument quoted (")                  */
#define _ARG_RESPONSE 0x02          /* Argument read from response file     */
#define _ARG_WILDCARD 0x04          /* Argument expanded from wildcard      */
#define _ARG_ENV      0x08          /* Argument from environment            */
#define _ARG_NONZERO  0x80          /* Always set, to avoid end of string   */

    cArgs  = 0;
    cbArgs = 0;

#if 0
    /* argv[0] */
    PUTC((char)_ARG_NONZERO);
    PUTV;
    for (;;)
    {
        PUTC(*pszSrc);
        if (*pszSrc == 0)
            break;
        ++pszSrc;
    }
    ++pszSrc;
#endif

    for (;;)
    {
        while (WHITE(*pszSrc))
            ++pszSrc;
        if (*pszSrc == 0)
            break;
        pfFlags = pchPool;
        PUTC((char)_ARG_NONZERO);
        PUTV;
        bs = 0; chQuote = 0;
        for (;;)
        {
            if (!chQuote ? (*pszSrc == '"' /*|| *pszSrc == '\''*/) : *pszSrc == chQuote)
            {
                while (bs >= 2)
                {
                    PUTC('\\');
                    bs -= 2;
                }
                if (bs & 1)
                    PUTC(*pszSrc);
                else
                {
                    chQuote = chQuote ? 0 : *pszSrc;
                    if (pfFlags != NULL)
                        *pfFlags |= _ARG_DQUOTE;
                }
                bs = 0;
            }
            else if (*pszSrc == '\\')
                ++bs;
            else
            {
                while (bs != 0)
                {
                    PUTC('\\');
                    --bs;
                }
                if (*pszSrc == 0 || (WHITE(*pszSrc) && !chQuote))
                    break;
                PUTC(*pszSrc);
            }
            ++pszSrc;
        }
        PUTC(0);
    }

    *pcArgs = cArgs;
    return cbArgs;
}




/*
 *
 * Process and thread related APIs.
 * Process and thread related APIs.
 * Process and thread related APIs.
 *
 */

/** ExitProcess replacement.  */
static void WINAPI kwSandbox_Kernel32_ExitProcess(UINT uExitCode)
{
    if (g_Sandbox.idMainThread == GetCurrentThreadId())
    {
        PNT_TIB pTib = (PNT_TIB)NtCurrentTeb();

        g_Sandbox.rcExitCode = (int)uExitCode;

        /* Before we jump, restore the TIB as we're not interested in any
           exception chain stuff installed by the sandboxed executable. */
        *pTib = g_Sandbox.TibMainThread;

        longjmp(g_Sandbox.JmpBuf, 1);
    }
    __debugbreak();
}


/** ExitProcess replacement.  */
static BOOL WINAPI kwSandbox_Kernel32_TerminateProcess(HANDLE hProcess, UINT uExitCode)
{
    if (hProcess == GetCurrentProcess())
        kwSandbox_Kernel32_ExitProcess(uExitCode);
    __debugbreak();
    return TerminateProcess(hProcess, uExitCode);
}


/** Normal CRT exit(). */
static void __cdecl kwSandbox_msvcrt_exit(int rcExitCode)
{
    KW_LOG(("kwSandbox_msvcrt_exit: %d\n", rcExitCode));
    kwSandbox_Kernel32_ExitProcess(rcExitCode);
}


/** Quick CRT _exit(). */
static void __cdecl kwSandbox_msvcrt__exit(int rcExitCode)
{
    /* Quick. */
    KW_LOG(("kwSandbox_msvcrt__exit %d\n", rcExitCode));
    kwSandbox_Kernel32_ExitProcess(rcExitCode);
}


/** Return to caller CRT _cexit(). */
static void __cdecl kwSandbox_msvcrt__cexit(int rcExitCode)
{
    KW_LOG(("kwSandbox_msvcrt__cexit: %d\n", rcExitCode));
    kwSandbox_Kernel32_ExitProcess(rcExitCode);
}


/** Quick return to caller CRT _c_exit(). */
static void __cdecl kwSandbox_msvcrt__c_exit(int rcExitCode)
{
    KW_LOG(("kwSandbox_msvcrt__c_exit: %d\n", rcExitCode));
    kwSandbox_Kernel32_ExitProcess(rcExitCode);
}


/** Runtime error and exit _amsg_exit(). */
static void __cdecl kwSandbox_msvcrt__amsg_exit(int iMsgNo)
{
    KW_LOG(("\nRuntime error #%u!\n", iMsgNo));
    kwSandbox_Kernel32_ExitProcess(255);
}


/** CRT - terminate().  */
static void __cdecl kwSandbox_msvcrt_terminate(void)
{
    KW_LOG(("\nRuntime - terminate!\n"));
    kwSandbox_Kernel32_ExitProcess(254);
}


/** The CRT internal __getmainargs() API. */
static int __cdecl kwSandbox_msvcrt___getmainargs(int *pargc, char ***pargv, char ***penvp,
                                                  int dowildcard, int const *piNewMode)
{
    *pargc = g_Sandbox.cArgs;
    *pargv = g_Sandbox.papszArgs;
    *penvp = g_Sandbox.environ;

    /** @todo startinfo points at a newmode (setmode) value.   */
    return 0;
}


/** The CRT internal __wgetmainargs() API. */
static int __cdecl kwSandbox_msvcrt___wgetmainargs(int *pargc, wchar_t ***pargv, wchar_t ***penvp,
                                                   int dowildcard, int const *piNewMode)
{
    *pargc = g_Sandbox.cArgs;
    *pargv = g_Sandbox.papwszArgs;
    *penvp = g_Sandbox.wenviron;

    /** @todo startinfo points at a newmode (setmode) value.   */
    return 0;
}



/** Kernel32 - GetCommandLineA()  */
static LPCSTR /*LPSTR*/ WINAPI kwSandbox_Kernel32_GetCommandLineA(VOID)
{
    return g_Sandbox.pszCmdLine;
}


/** Kernel32 - GetCommandLineW()  */
static LPCWSTR /*LPWSTR*/ WINAPI kwSandbox_Kernel32_GetCommandLineW(VOID)
{
    return g_Sandbox.pwszCmdLine;
}


/** Kernel32 - GetStartupInfoA()  */
static VOID WINAPI kwSandbox_Kernel32_GetStartupInfoA(LPSTARTUPINFOA pStartupInfo)
{
    __debugbreak();
}


/** Kernel32 - GetStartupInfoW()  */
static VOID WINAPI kwSandbox_Kernel32_GetStartupInfoW(LPSTARTUPINFOW lpStartupInfo)
{
    __debugbreak();
}


/** CRT - __p___argc().  */
static int * __cdecl kwSandbox_msvcrt___p___argc(void)
{
    return &g_Sandbox.cArgs;
}


/** CRT - __p___argv().  */
static char *** __cdecl kwSandbox_msvcrt___p___argv(void)
{
    return &g_Sandbox.papszArgs;
}


/** CRT - __p___sargv().  */
static wchar_t *** __cdecl kwSandbox_msvcrt___p___wargv(void)
{
    return &g_Sandbox.papwszArgs;
}


/** CRT - __p__acmdln().  */
static char ** __cdecl kwSandbox_msvcrt___p__acmdln(void)
{
    return (char **)&g_Sandbox.pszCmdLine;
}


/** CRT - __p__acmdln().  */
static wchar_t ** __cdecl kwSandbox_msvcrt___p__wcmdln(void)
{
    return &g_Sandbox.pwszCmdLine;
}


/** CRT - __p__pgmptr().  */
static char ** __cdecl kwSandbox_msvcrt___p__pgmptr(void)
{
    return &g_Sandbox.pgmptr;
}


/** CRT - __p__wpgmptr().  */
static wchar_t ** __cdecl kwSandbox_msvcrt___p__wpgmptr(void)
{
    return &g_Sandbox.wpgmptr;
}


/** CRT - _get_pgmptr().  */
static errno_t __cdecl kwSandbox_msvcrt__get_pgmptr(char **ppszValue)
{
    *ppszValue = g_Sandbox.pgmptr;
    return 0;
}


/** CRT - _get_wpgmptr().  */
static errno_t __cdecl kwSandbox_msvcrt__get_wpgmptr(wchar_t **ppwszValue)
{
    *ppwszValue = g_Sandbox.wpgmptr;
    return 0;
}

/** Just in case. */
static void kwSandbox_msvcrt__wincmdln(void)
{
    __debugbreak();
}


/** Just in case. */
static void kwSandbox_msvcrt__wwincmdln(void)
{
    __debugbreak();
}

/** CreateThread interceptor. */
static HANDLE WINAPI kwSandbox_Kernel32_CreateThread(LPSECURITY_ATTRIBUTES pSecAttr, SIZE_T cbStack,
                                                     PTHREAD_START_ROUTINE pfnThreadProc, PVOID pvUser,
                                                     DWORD fFlags, PDWORD pidThread)
{
    __debugbreak();
    return NULL;
}


/** _beginthread - create a new thread. */
static uintptr_t __cdecl kwSandbox_msvcrt__beginthread(void (__cdecl *pfnThreadProc)(void *), unsigned cbStack, void *pvUser)
{
    __debugbreak();
    return 0;
}


/** _beginthreadex - create a new thread. */
static uintptr_t __cdecl kwSandbox_msvcrt__beginthreadex(void *pvSecAttr, unsigned cbStack,
                                                         unsigned (__stdcall *pfnThreadProc)(void *), void *pvUser,
                                                         unsigned fCreate, unsigned *pidThread)
{
    __debugbreak();
    return 0;
}


/*
 *
 * Environment related APIs.
 * Environment related APIs.
 * Environment related APIs.
 *
 */

/** Kernel32 - GetEnvironmentVariableA()  */
static DWORD WINAPI kwSandbox_Kernel32_GetEnvironmentVariableA(LPCSTR pwszVar, LPSTR pszValue, DWORD cbValue)
{
    __debugbreak();
    return 0;
}


/** Kernel32 - GetEnvironmentVariableW()  */
static DWORD WINAPI kwSandbox_Kernel32_GetEnvironmentVariableW(LPCWSTR pwszVar, LPWSTR pwszValue, DWORD cbValue)
{
    KW_LOG(("GetEnvironmentVariableW: '%ls'\n", pwszVar));
    //__debugbreak();
    //SetLastError(ERROR_ENVVAR_NOT_FOUND);
    //return 0;
    return GetEnvironmentVariableW(pwszVar, pwszValue, cbValue);
}


/** Kernel32 - SetEnvironmentVariableA()  */
static BOOL WINAPI kwSandbox_Kernel32_SetEnvironmentVariableA(LPCSTR pszVar, LPCSTR pszValue)
{
    __debugbreak();
    return FALSE;
}


/** Kernel32 - SetEnvironmentVariableW()  */
static BOOL WINAPI kwSandbox_Kernel32_SetEnvironmentVariableW(LPCWSTR pwszVar, LPCWSTR pwszValue)
{
    KW_LOG(("SetEnvironmentVariableW: '%ls' = '%ls'\n", pwszVar, pwszValue));
    return SetEnvironmentVariableW(pwszVar, pwszValue);
    //__debugbreak();
    //return FALSE;
}


/** Kernel32 - ExpandEnvironmentStringsA()  */
static DWORD WINAPI kwSandbox_Kernel32_ExpandEnvironmentStringsA(LPCSTR pszSrc, LPSTR pwszDst, DWORD cbDst)
{
    __debugbreak();
    return 0;
}


/** Kernel32 - ExpandEnvironmentStringsW()  */
static DWORD WINAPI kwSandbox_Kernel32_ExpandEnvironmentStringsW(LPCWSTR pwszSrc, LPWSTR pwszDst, DWORD cbDst)
{
    __debugbreak();
    return 0;
}


/** CRT - _putenv(). */
static int __cdecl kwSandbox_msvcrt__putenv(const char *pszVarEqualValue)
{
    __debugbreak();
    return 0;
}


/** CRT - _wputenv(). */
static int __cdecl kwSandbox_msvcrt__wputenv(const wchar_t *pwszVarEqualValue)
{
    __debugbreak();
    return 0;
}


/** CRT - _putenv_s(). */
static errno_t __cdecl kwSandbox_msvcrt__putenv_s(const char *pszVar, const char *pszValue)
{
    __debugbreak();
    return 0;
}


/** CRT - _wputenv_s(). */
static errno_t __cdecl kwSandbox_msvcrt__wputenv_s(const wchar_t *pwszVar, const wchar_t *pwszValue)
{
    KW_LOG(("_wputenv_s: '%ls' = '%ls'\n", pwszVar, pwszValue));
    //__debugbreak();
    return SetEnvironmentVariableW(pwszVar, pwszValue) ? 0 : -1;
}


/** CRT - get pointer to the __initenv variable (initial environment).   */
static char *** __cdecl kwSandbox_msvcrt___p___initenv(void)
{
    return &g_Sandbox.initenv;
}


/** CRT - get pointer to the __winitenv variable (initial environment).   */
static wchar_t *** __cdecl kwSandbox_msvcrt___p___winitenv(void)
{
    return &g_Sandbox.winitenv;
}


/** CRT - get pointer to the _environ variable (current environment).   */
static char *** __cdecl kwSandbox_msvcrt___p__environ(void)
{
    return &g_Sandbox.environ;
}


/** CRT - get pointer to the _wenviron variable (current environment).   */
static wchar_t *** __cdecl kwSandbox_msvcrt___p__wenviron(void)
{
    return &g_Sandbox.wenviron;
}


/** CRT - get the _environ variable (current environment).
 * @remarks Not documented or prototyped?  */
static KUPTR /*void*/ __cdecl kwSandbox_msvcrt__get_environ(char ***ppapszEnviron)
{
    __debugbreak(); /** @todo check the callers expecations! */
    *ppapszEnviron = g_Sandbox.environ;
    return 0;
}


/** CRT - get the _wenviron variable (current environment).
 * @remarks Not documented or prototyped? */
static KUPTR /*void*/ __cdecl kwSandbox_msvcrt__get_wenviron(wchar_t ***ppapwszEnviron)
{
    __debugbreak(); /** @todo check the callers expecations! */
    *ppapwszEnviron = g_Sandbox.wenviron;
    return 0;
}



/*
 *
 * Loader related APIs
 * Loader related APIs
 * Loader related APIs
 *
 */


/** Kernel32 - LoadLibraryExA()   */
static HMODULE WINAPI kwSandbox_Kernel32_LoadLibraryExA(LPCSTR pszFilename, HANDLE hFile, DWORD fFlags)
{
    PKWDYNLOAD  pDynLoad;
    PKWMODULE   pMod;
    int         rc;

    if (   (fFlags & LOAD_LIBRARY_AS_DATAFILE_EXCLUSIVE)
        || (hFile != NULL && hFile != INVALID_HANDLE_VALUE) )
    {
        __debugbreak();
        return LoadLibraryExA(pszFilename, hFile, fFlags);
    }

    /*
     * Deal with resource / data DLLs.
     */
    if (fFlags & (  DONT_RESOLVE_DLL_REFERENCES
                  | LOAD_LIBRARY_AS_DATAFILE
                  | LOAD_LIBRARY_AS_IMAGE_RESOURCE) )
    {
        HMODULE hmod;
        char    szNormPath[4096];
        KU32    uHashPath;

        /* currently, only deal with those that has a path. */
        if (kHlpIsFilenameOnly(pszFilename))
        {
            __debugbreak();
            return LoadLibraryExA(pszFilename, hFile, fFlags);
        }

        /* Normalize the path. */
        rc = kwPathNormalize(pszFilename, szNormPath, sizeof(szNormPath));
        if (rc != 0)
        {
            __debugbreak();
            return LoadLibraryExA(pszFilename, hFile, fFlags);
        }

        /* Try look it up. */
        uHashPath = kwStrHash(szNormPath);
        for (pDynLoad = g_Sandbox.pTool->u.Sandboxed.pDynLoadHead; pDynLoad != NULL; pDynLoad = pDynLoad->pNext)
            if (   pDynLoad->uHashPath == uHashPath
                && kHlpStrComp(pDynLoad->pszPath, szNormPath) == 0)
            {
                if (pDynLoad->pMod == NULL)
                    return pDynLoad->hmod;
                __debugbreak();
            }

        /* Then try load it. */
        hmod = LoadLibraryExA(szNormPath, hFile, fFlags);
        if (hmod)
        {
            KSIZE cbNormPath = kHlpStrLen(szNormPath) + 1;
            pDynLoad = (PKWDYNLOAD)kHlpAlloc(sizeof(*pDynLoad) + cbNormPath + cbNormPath * 2 * sizeof(wchar_t));
            if (pDynLoad)
            {
                pDynLoad->pszPath           = (char *)kHlpMemCopy(pDynLoad + 1, szNormPath, cbNormPath);
                pDynLoad->pwszPath          = (wchar_t *)(pDynLoad->pszPath + cbNormPath + (cbNormPath & 1));
                kwStrToUtf16(pDynLoad->pszPath, (wchar_t *)pDynLoad->pwszPath, cbNormPath * 2);
                pDynLoad->pszModName        = kHlpGetFilename(pDynLoad->pszPath);
                pDynLoad->uHashPath         = uHashPath;
                pDynLoad->pMod              = NULL; /* indicates special  */
                pDynLoad->hmod              = hmod;

                pDynLoad->pNext = g_Sandbox.pTool->u.Sandboxed.pDynLoadHead;
                g_Sandbox.pTool->u.Sandboxed.pDynLoadHead = pDynLoad;
            }
            else
                __debugbreak();
        }
        return hmod;
    }

    /*
     * Normal library loading.
     * We start by being very lazy and reusing the code for resolving imports.
     */
    if (!kHlpIsFilenameOnly(pszFilename))
        pMod = kwLdrModuleTryLoadDll(pszFilename, KWLOCATION_UNKNOWN, g_Sandbox.pTool->u.Sandboxed.pExe);
    else
    {
__debugbreak();
        rc = kwLdrModuleResolveAndLookup(pszFilename, g_Sandbox.pTool->u.Sandboxed.pExe, NULL /*pImporter*/, &pMod);
        if (rc != 0)
            pMod = NULL;
    }
    if (!pMod)
    {
        __debugbreak();
        SetLastError(ERROR_MOD_NOT_FOUND);
        return NULL;
    }

    /*
     * Make sure it's initialized.
     */
    rc = kwLdrModuleInitTree(pMod);
    if (rc == 0)
    {
        /*
         * Create an dynamic loading entry for it.
         */
        pDynLoad = (PKWDYNLOAD)kHlpAlloc(sizeof(*pDynLoad));
        if (pDynLoad)
        {
            pDynLoad->pszPath           = pMod->pszPath;
            pDynLoad->pwszPath          = pMod->pwszPath;
            pDynLoad->pszModName        = kHlpGetFilename(pDynLoad->pszPath);
            pDynLoad->uHashPath         = pMod->uHashPath;
            pDynLoad->pMod              = pMod;
            pDynLoad->hmod              = pMod->hOurMod;

            pDynLoad->pNext = g_Sandbox.pTool->u.Sandboxed.pDynLoadHead;
            g_Sandbox.pTool->u.Sandboxed.pDynLoadHead = pDynLoad;

            return pDynLoad->hmod;
        }
    }

    __debugbreak();
    SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    kwLdrModuleRelease(pMod);
    return NULL;
}


/** Kernel32 - LoadLibraryExW()   */
static HMODULE WINAPI kwSandbox_Kernel32_LoadLibraryExW(LPCWSTR pwszFilename, HANDLE hFile, DWORD fFlags)
{
    char szTmp[4096];
    KSIZE cchTmp = kwUtf16ToStr(pwszFilename, szTmp, sizeof(szTmp));
    if (cchTmp < sizeof(szTmp))
        return kwSandbox_Kernel32_LoadLibraryExA(szTmp, hFile, fFlags);

    __debugbreak();
    SetLastError(ERROR_FILENAME_EXCED_RANGE);
    return NULL;
}

/** Kernel32 - LoadLibraryA()   */
static HMODULE WINAPI kwSandbox_Kernel32_LoadLibraryA(LPCSTR pszFilename)
{
    return kwSandbox_Kernel32_LoadLibraryExA(pszFilename, NULL /*hFile*/, 0 /*fFlags*/);
}


/** Kernel32 - LoadLibraryW()   */
static HMODULE WINAPI kwSandbox_Kernel32_LoadLibraryW(LPCWSTR pwszFilename)
{
    char szTmp[4096];
    KSIZE cchTmp = kwUtf16ToStr(pwszFilename, szTmp, sizeof(szTmp));
    if (cchTmp < sizeof(szTmp))
        return kwSandbox_Kernel32_LoadLibraryExA(szTmp, NULL /*hFile*/, 0 /*fFlags*/);
    __debugbreak();
    SetLastError(ERROR_FILENAME_EXCED_RANGE);
    return NULL;
}


/** Kernel32 - FreeLibrary()   */
static BOOL WINAPI kwSandbox_Kernel32_FreeLibrary(HMODULE hmod)
{
    /* Ignored, we like to keep everything loaded. */
    return TRUE;
}


/** Kernel32 - GetModuleHandleA()   */
static HMODULE WINAPI kwSandbox_Kernel32_GetModuleHandleA(LPCSTR pszModule)
{
    if (pszModule == NULL)
        return (HMODULE)g_Sandbox.pTool->u.Sandboxed.pExe->hOurMod;
    __debugbreak();
    return NULL;
}


/** Kernel32 - GetModuleHandleW()   */
static HMODULE WINAPI kwSandbox_Kernel32_GetModuleHandleW(LPCWSTR pwszModule)
{
    if (pwszModule == NULL)
        return (HMODULE)g_Sandbox.pTool->u.Sandboxed.pExe->hOurMod;
    __debugbreak();
    return NULL;
}


static PKWMODULE kwSandboxLocateModuleByHandle(PKWSANDBOX pSandbox, HMODULE hmod)
{
    PKWDYNLOAD pDynLoad;

    /* The executable. */
    if (   hmod == NULL
        || pSandbox->pTool->u.Sandboxed.pExe->hOurMod == hmod)
        return kwLdrModuleRetain(pSandbox->pTool->u.Sandboxed.pExe);

    /* Dynamically loaded images. */
    for (pDynLoad = pSandbox->pTool->u.Sandboxed.pDynLoadHead; pDynLoad != NULL; pDynLoad = pDynLoad->pNext)
        if (pDynLoad->hmod == hmod)
        {
            if (pDynLoad->pMod)
                return kwLdrModuleRetain(pDynLoad->pMod);
            __debugbreak();
            return NULL;
        }

    return NULL;
}


/** Used to debug dynamically resolved procedures. */
static UINT WINAPI kwSandbox_BreakIntoDebugger(void *pv1, void *pv2, void *pv3, void *pv4)
{
    __debugbreak();
    return -1;
}


/** Kernel32 - GetProcAddress()   */
static FARPROC WINAPI kwSandbox_Kernel32_GetProcAddress(HMODULE hmod, LPCSTR pszProc)
{
    /*
     * Try locate the module.
     */
    PKWMODULE pMod = kwSandboxLocateModuleByHandle(&g_Sandbox, hmod);
    if (pMod)
    {
        KLDRADDR uValue;
        int rc = kLdrModQuerySymbol(pMod->pLdrMod,
                                    pMod->fNative ? NULL : pMod->u.Manual.pvBits,
                                    pMod->fNative ? KLDRMOD_BASEADDRESS_MAP : (KUPTR)pMod->u.Manual.pvLoad,
                                    KU32_MAX /*iSymbol*/,
                                    pszProc,
                                    strlen(pszProc),
                                    NULL /*pszVersion*/,
                                    NULL /*pfnGetForwarder*/, NULL /*pvUser*/,
                                    &uValue,
                                    NULL /*pfKind*/);
        if (rc == 0)
        {
            static int s_cDbgGets = 0;
            s_cDbgGets++;
            KW_LOG(("GetProcAddress(%s, %s) -> %p [%d]\n", pMod->pszPath, pszProc, (KUPTR)uValue, s_cDbgGets));
            kwLdrModuleRelease(pMod);
            //if (s_cGets >= 3)
            //    return (FARPROC)kwSandbox_BreakIntoDebugger;
            return (FARPROC)(KUPTR)uValue;
        }

        __debugbreak();
        SetLastError(ERROR_PROC_NOT_FOUND);
        kwLdrModuleRelease(pMod);
        return NULL;
    }

    __debugbreak();
    return GetProcAddress(hmod, pszProc);
}


/** Kernel32 - GetModuleFileNameA()   */
static DWORD WINAPI kwSandbox_Kernel32_GetModuleFileNameA(HMODULE hmod, LPSTR pszFilename, DWORD cbFilename)
{
    PKWMODULE pMod = kwSandboxLocateModuleByHandle(&g_Sandbox, hmod);
    if (pMod != NULL)
    {
        DWORD cbRet = kwStrCopyStyle1(pMod->pszPath, pszFilename, cbFilename);
        kwLdrModuleRelease(pMod);
        return cbRet;
    }
    __debugbreak();
    return 0;
}


/** Kernel32 - GetModuleFileNameW()   */
static DWORD WINAPI kwSandbox_Kernel32_GetModuleFileNameW(HMODULE hmod, LPWSTR pwszFilename, DWORD cbFilename)
{
    PKWMODULE pMod = kwSandboxLocateModuleByHandle(&g_Sandbox, hmod);
    if (pMod)
    {
        DWORD cwcRet = kwUtf16CopyStyle1(pMod->pwszPath, pwszFilename, cbFilename);
        kwLdrModuleRelease(pMod);
        return cwcRet;
    }

    __debugbreak();
    return 0;
}



/*
 *
 * File access APIs (for speeding them up).
 * File access APIs (for speeding them up).
 * File access APIs (for speeding them up).
 *
 */


/**
 * Converts a lookup error to a windows error code.
 *
 * @returns The windows error code.
 * @param   enmError            The lookup error.
 */
static DWORD kwFsLookupErrorToWindowsError(KFSLOOKUPERROR enmError)
{
    switch (enmError)
    {
        case KFSLOOKUPERROR_NOT_FOUND:
        case KFSLOOKUPERROR_NOT_DIR:
            return ERROR_FILE_NOT_FOUND;

        case KFSLOOKUPERROR_PATH_COMP_NOT_FOUND:
        case KFSLOOKUPERROR_PATH_COMP_NOT_DIR:
            return ERROR_PATH_NOT_FOUND;

        case KFSLOOKUPERROR_PATH_TOO_LONG:
            return ERROR_FILENAME_EXCED_RANGE;

        case KFSLOOKUPERROR_OUT_OF_MEMORY:
            return ERROR_NOT_ENOUGH_MEMORY;

        default:
            return ERROR_PATH_NOT_FOUND;
    }
}

#ifdef WITH_TEMP_MEMORY_FILES

/**
 * Checks for a cl.exe temporary file.
 *
 * There are quite a bunch of these.  They seems to be passing data between the
 * first and second compiler pass.  Since they're on disk, they get subjected to
 * AV software screening and normal file consistency rules.  So, not necessarily
 * a very efficient way of handling reasonably small amounts of data.
 *
 * We make the files live in virtual memory by intercepting their  opening,
 * writing, reading, closing , mapping, unmapping, and maybe some more stuff.
 *
 * @returns K_TRUE / K_FALSE
 * @param   pwszFilename    The file name being accessed.
 */
static KBOOL kwFsIsClTempFileW(const wchar_t *pwszFilename)
{
    wchar_t const *pwszName = kwPathGetFilenameW(pwszFilename);
    if (pwszName)
    {
        /* The name starts with _CL_... */
        if (   pwszName[0] == '_'
            && pwszName[1] == 'C'
            && pwszName[2] == 'L'
            && pwszName[3] == '_' )
        {
            /* ... followed by 8 xdigits and ends with a two letter file type.  Simplify
               this check by just checking that it's alpha numerical ascii from here on. */
            wchar_t wc;
            pwszName += 4;
            while ((wc = *pwszName++) != '\0')
            {
                if (wc < 127 && iswalnum(wc))
                { /* likely */ }
                else
                    return K_FALSE;
            }
            return K_TRUE;
        }
    }
    return K_FALSE;
}


/**
 * Creates a handle to a temporary file.
 *
 * @returns The handle on success.
 *          INVALID_HANDLE_VALUE and SetLastError on failure.
 * @param   pTempFile           The temporary file.
 * @param   dwDesiredAccess     The desired access to the handle.
 * @param   fMapping            Whether this is a mapping (K_TRUE) or file
 *                              (K_FALSE) handle type.
 */
static HANDLE kwFsTempFileCreateHandle(PKWFSTEMPFILE pTempFile, DWORD dwDesiredAccess, KBOOL fMapping)
{
    /*
     * Create a handle to the temporary file.
     */
    HANDLE hFile = INVALID_HANDLE_VALUE;
    HANDLE hProcSelf = GetCurrentProcess();
    if (DuplicateHandle(hProcSelf, hProcSelf,
                        hProcSelf, &hFile,
                        SYNCHRONIZE, FALSE,
                        0 /*dwOptions*/))
    {
        PKWHANDLE pHandle = (PKWHANDLE)kHlpAlloc(sizeof(*pHandle));
        if (pHandle)
        {
            pHandle->enmType            = !fMapping ? KWHANDLETYPE_TEMP_FILE : KWHANDLETYPE_TEMP_FILE_MAPPING;
            pHandle->offFile            = 0;
            pHandle->hHandle            = hFile;
            pHandle->dwDesiredAccess    = dwDesiredAccess;
            pHandle->u.pTempFile        = pTempFile;
            if (kwSandboxHandleTableEnter(&g_Sandbox, pHandle))
            {
                pTempFile->cActiveHandles++;
                kHlpAssert(pTempFile->cActiveHandles >= 1);
                kHlpAssert(pTempFile->cActiveHandles <= 2);
                KWFS_LOG(("kwFsTempFileCreateHandle: Temporary file '%ls' -> %p\n", pTempFile->pwszPath, hFile));
                return hFile;
            }

            kHlpFree(pHandle);
        }
        else
            KWFS_LOG(("kwFsTempFileCreateHandle: Out of memory!\n"));
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    }
    else
        KWFS_LOG(("kwFsTempFileCreateHandle: DuplicateHandle failed: err=%u\n", GetLastError()));
    return INVALID_HANDLE_VALUE;
}


static HANDLE kwFsTempFileCreateW(const wchar_t *pwszFilename, DWORD dwDesiredAccess, DWORD dwCreationDisposition)
{
    HANDLE hFile;
    DWORD  dwErr;

    /*
     * Check if we've got an existing temp file.
     * ASSUME exact same path for now.
     */
    KSIZE const   cwcFilename = kwUtf16Len(pwszFilename);
    PKWFSTEMPFILE pTempFile;
    for (pTempFile = g_Sandbox.pTempFileHead; pTempFile != NULL; pTempFile = pTempFile->pNext)
    {
        /* Since the last two chars are usually the only difference, we check them manually before calling memcmp. */
        if (   pTempFile->cwcPath == cwcFilename
            && pTempFile->pwszPath[cwcFilename - 1] == pwszFilename[cwcFilename - 1]
            && pTempFile->pwszPath[cwcFilename - 2] == pwszFilename[cwcFilename - 2]
            && kHlpMemComp(pTempFile->pwszPath, pwszFilename, cwcFilename) == 0)
            break;
    }

    /*
     * Create a new temporary file instance if not found.
     */
    if (pTempFile == NULL)
    {
        KSIZE cbFilename;

        switch (dwCreationDisposition)
        {
            case CREATE_ALWAYS:
            case OPEN_ALWAYS:
                dwErr = NO_ERROR;
                break;

            case CREATE_NEW:
                kHlpAssertFailed();
                SetLastError(ERROR_ALREADY_EXISTS);
                return INVALID_HANDLE_VALUE;

            case OPEN_EXISTING:
            case TRUNCATE_EXISTING:
                kHlpAssertFailed();
                SetLastError(ERROR_FILE_NOT_FOUND);
                return INVALID_HANDLE_VALUE;

            default:
                kHlpAssertFailed();
                SetLastError(ERROR_INVALID_PARAMETER);
                return INVALID_HANDLE_VALUE;
        }

        cbFilename = (cwcFilename + 1) * sizeof(wchar_t);
        pTempFile = (PKWFSTEMPFILE)kHlpAlloc(sizeof(*pTempFile) + cbFilename);
        if (pTempFile)
        {
            pTempFile->cwcPath          = (KU16)cwcFilename;
            pTempFile->cbFile           = 0;
            pTempFile->cbFileAllocated  = 0;
            pTempFile->cActiveHandles   = 0;
            pTempFile->cMappings        = 0;
            pTempFile->cSegs            = 0;
            pTempFile->paSegs           = NULL;
            pTempFile->pwszPath         = (wchar_t const *)kHlpMemCopy(pTempFile + 1, pwszFilename, cbFilename);

            pTempFile->pNext = g_Sandbox.pTempFileHead;
            g_Sandbox.pTempFileHead = pTempFile;
            KWFS_LOG(("kwFsTempFileCreateW: Created new temporary file '%ls'\n", pwszFilename));
        }
        else
        {
            KWFS_LOG(("kwFsTempFileCreateW: Out of memory!\n"));
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            return INVALID_HANDLE_VALUE;
        }
    }
    else
    {
        switch (dwCreationDisposition)
        {
            case OPEN_EXISTING:
                dwErr = NO_ERROR;
                break;
            case OPEN_ALWAYS:
                dwErr = ERROR_ALREADY_EXISTS ;
                break;

            case TRUNCATE_EXISTING:
            case CREATE_ALWAYS:
                kHlpAssertFailed();
                pTempFile->cbFile = 0;
                dwErr = ERROR_ALREADY_EXISTS;
                break;

            case CREATE_NEW:
                kHlpAssertFailed();
                SetLastError(ERROR_FILE_EXISTS);
                return INVALID_HANDLE_VALUE;

            default:
                kHlpAssertFailed();
                SetLastError(ERROR_INVALID_PARAMETER);
                return INVALID_HANDLE_VALUE;
        }
    }

    /*
     * Create a handle to the temporary file.
     */
    hFile = kwFsTempFileCreateHandle(pTempFile, dwDesiredAccess, K_FALSE /*fMapping*/);
    if (hFile != INVALID_HANDLE_VALUE)
        SetLastError(dwErr);
    return hFile;
}

#endif /* WITH_TEMP_MEMORY_FILES */


/**
 * Checks if the file extension indicates that the file/dir is something we
 * ought to cache.
 *
 * @returns K_TRUE if cachable, K_FALSE if not.
 * @param   pszExt              The kHlpGetExt result.
 * @param   fAttrQuery          Set if it's for an attribute query, clear if for
 *                              file creation.
 */
static KBOOL kwFsIsCachableExtensionA(const char *pszExt, KBOOL fAttrQuery)
{
    char const chFirst = *pszExt;

    /* C++ header without an extension or a directory. */
    if (chFirst == '\0')
    {
        /** @todo exclude temporary files...  */
        return K_TRUE;
    }

    /* C Header: .h */
    if (chFirst == 'h' || chFirst == 'H')
    {
        char        chThird;
        char const  chSecond = pszExt[1];
        if (chSecond == '\0')
            return K_TRUE;
        chThird = pszExt[2];

        /* C++ Header: .hpp, .hxx */
        if (   (chSecond == 'p' || chSecond == 'P')
            && (chThird  == 'p' || chThird  == 'P')
            && pszExt[3] == '\0')
            return K_TRUE;
        if (   (chSecond == 'x' || chSecond == 'X')
            && (chThird  == 'x' || chThird  == 'X')
            && pszExt[3] == '\0')
            return K_TRUE;

    }
    /* Misc starting with i. */
    else if (chFirst == 'i' || chFirst == 'I')
    {
        char const chSecond = pszExt[1];
        if (chSecond != '\0')
        {
            if (chSecond == 'n' || chSecond == 'N')
            {
                char const chThird = pszExt[2];

                /* C++ inline header: .inl */
                if (   (chThird == 'l' || chThird == 'L')
                    && pszExt[3] == '\0')
                    return K_TRUE;

                /* Assembly include file: .inc */
                if (   (chThird == 'c' || chThird == 'C')
                    && pszExt[3] == '\0')
                    return K_TRUE;
            }
        }
    }
    else if (fAttrQuery)
    {
        /* Dynamic link library: .dll */
        if (chFirst == 'd' || chFirst == 'D')
        {
            char const chSecond = pszExt[1];
            if (chSecond == 'l' || chSecond == 'L')
            {
                char const chThird = pszExt[2];
                if (chThird == 'l' || chThird == 'L')
                    return K_TRUE;
            }
        }
        /* Executable file: .exe */
        else if (chFirst == 'e' || chFirst == 'E')
        {
            char const chSecond = pszExt[1];
            if (chSecond == 'x' || chSecond == 'X')
            {
                char const chThird = pszExt[2];
                if (chThird == 'e' || chThird == 'e')
                    return K_TRUE;
            }
        }
    }

    return K_FALSE;
}


/**
 * Checks if the extension of the given UTF-16 path indicates that the file/dir
 * should be cached.
 *
 * @returns K_TRUE if cachable, K_FALSE if not.
 * @param   pwszPath            The UTF-16 path to examine.
 * @param   fAttrQuery          Set if it's for an attribute query, clear if for
 *                              file creation.
 */
static KBOOL kwFsIsCachablePathExtensionW(const wchar_t *pwszPath, KBOOL fAttrQuery)
{
    /*
     * Extract the extension, check that it's in the applicable range, roughly
     * convert it to ASCII/ANSI, and feed it to kwFsIsCachableExtensionA for
     * the actual check.  This avoids a lot of code duplication.
     */
    wchar_t         wc;
    char            szExt[4];
    KSIZE           cwcExt;
    wchar_t const  *pwszExt = kwFsPathGetExtW(pwszPath, &cwcExt);
    switch (cwcExt)
    {
        case 3: if ((wchar_t)(szExt[2] = (char)(wc = pwszExt[2])) == wc) { /*likely*/ } else break;
        case 2: if ((wchar_t)(szExt[1] = (char)(wc = pwszExt[1])) == wc) { /*likely*/ } else break;
        case 1: if ((wchar_t)(szExt[0] = (char)(wc = pwszExt[0])) == wc) { /*likely*/ } else break;
        case 0:
            szExt[cwcExt] = '\0';
            return kwFsIsCachableExtensionA(szExt, fAttrQuery);
    }
    return K_FALSE;
}



/**
 * Creates a new
 *
 * @returns
 * @param   pFsObj          .
 * @param   pwszFilename    .
 */
static PKFSWCACHEDFILE kwFsObjCacheNewFile(PKFSOBJ pFsObj)
{
    HANDLE                  hFile;
    MY_IO_STATUS_BLOCK      Ios;
    MY_OBJECT_ATTRIBUTES    ObjAttr;
    MY_UNICODE_STRING       UniStr;
    MY_NTSTATUS             rcNt;

    /*
     * Open the file relative to the parent directory.
     */
    kHlpAssert(pFsObj->bObjType == KFSOBJ_TYPE_FILE);
    kHlpAssert(pFsObj->pParent);
    kHlpAssertReturn(pFsObj->pParent->hDir != INVALID_HANDLE_VALUE, NULL);

    Ios.Information = -1;
    Ios.u.Status    = -1;

    UniStr.Buffer        = (wchar_t *)pFsObj->pwszName;
    UniStr.Length        = (USHORT)(pFsObj->cwcName * sizeof(wchar_t));
    UniStr.MaximumLength = UniStr.Length + sizeof(wchar_t);

    MyInitializeObjectAttributes(&ObjAttr, &UniStr, OBJ_CASE_INSENSITIVE, pFsObj->pParent->hDir, NULL /*pSecAttr*/);

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
        /*
         * Read the whole file into memory.
         */
        LARGE_INTEGER cbFile;
        if (GetFileSizeEx(hFile, &cbFile))
        {
            if (   cbFile.QuadPart >= 0
                && cbFile.QuadPart < 16*1024*1024)
            {
                KU32 cbCache = (KU32)cbFile.QuadPart;
                KU8 *pbCache = (KU8 *)kHlpAlloc(cbCache);
                if (pbCache)
                {
                    DWORD cbActually = 0;
                    if (   ReadFile(hFile, pbCache, cbCache, &cbActually, NULL)
                        && cbActually == cbCache)
                    {
                        LARGE_INTEGER offZero;
                        offZero.QuadPart = 0;
                        if (SetFilePointerEx(hFile, offZero, NULL /*poffNew*/, FILE_BEGIN))
                        {
                            /*
                             * Create the cached file object.
                             */
                            PKFSWCACHEDFILE pCachedFile;
                            pCachedFile = (PKFSWCACHEDFILE)kFsCacheObjAddUserData(g_pFsCache, pFsObj, KW_DATA_KEY_CACHED_FILE,
                                                                                  sizeof(*pCachedFile));
                            if (pCachedFile)
                            {
                                pCachedFile->hCached  = hFile;
                                pCachedFile->cbCached = cbCache;
                                pCachedFile->pbCached = pbCache;
                                pCachedFile->pFsObj   = pFsObj;
                                kFsCacheObjRetain(pFsObj);
                                return pCachedFile;
                            }

                            KWFS_LOG(("Failed to allocate KFSWCACHEDFILE structure!\n"));
                        }
                        else
                            KWFS_LOG(("Failed to seek to start of cached file! err=%u\n", GetLastError()));
                    }
                    else
                        KWFS_LOG(("Failed to read %#x bytes into cache! err=%u cbActually=%#x\n",
                                  cbCache, GetLastError(), cbActually));
                    kHlpFree(pbCache);
                }
                else
                    KWFS_LOG(("Failed to allocate %#x bytes for cache!\n", cbCache));
            }
            else
                KWFS_LOG(("File to big to cache! %#llx\n", cbFile.QuadPart));
        }
        else
            KWFS_LOG(("File to get file size! err=%u\n", GetLastError()));
        g_pfnNtClose(hFile);
    }
    else
        KWFS_LOG(("Error opening '%ls' for caching: %#x\n", pFsObj->pwszName, rcNt));
    return NULL;
}


/**
 * Kernel32 - Common code for CreateFileW and CreateFileA.
 */
static KBOOL kwFsObjCacheCreateFile(PKFSOBJ pFsObj, DWORD dwDesiredAccess, BOOL fInheritHandle, HANDLE *phFile)
{
    *phFile = INVALID_HANDLE_VALUE;
    kHlpAssert(pFsObj->fHaveStats);

    /*
     * At the moment we only handle existing files.
     */
    if (pFsObj->bObjType == KFSOBJ_TYPE_FILE)
    {
        PKFSWCACHEDFILE pCachedFile = (PKFSWCACHEDFILE)kFsCacheObjGetUserData(g_pFsCache, pFsObj, KW_DATA_KEY_CACHED_FILE);
        if (   pCachedFile != NULL
            || (pCachedFile = kwFsObjCacheNewFile(pFsObj)) != NULL)
        {
            HANDLE hProcSelf = GetCurrentProcess();
            if (DuplicateHandle(hProcSelf, pCachedFile->hCached,
                                hProcSelf, phFile,
                                dwDesiredAccess, fInheritHandle,
                                0 /*dwOptions*/))
            {
                /*
                 * Create handle table entry for the duplicate handle.
                 */
                PKWHANDLE pHandle = (PKWHANDLE)kHlpAlloc(sizeof(*pHandle));
                if (pHandle)
                {
                    pHandle->enmType            = KWHANDLETYPE_FSOBJ_READ_CACHE;
                    pHandle->offFile            = 0;
                    pHandle->hHandle            = *phFile;
                    pHandle->dwDesiredAccess    = dwDesiredAccess;
                    pHandle->u.pCachedFile      = pCachedFile;
                    if (kwSandboxHandleTableEnter(&g_Sandbox, pHandle))
                        return K_TRUE;

                    kHlpFree(pHandle);
                }
                else
                    KWFS_LOG(("Out of memory for handle!\n"));

                CloseHandle(*phFile);
                *phFile = INVALID_HANDLE_VALUE;
            }
            else
                KWFS_LOG(("DuplicateHandle failed! err=%u\n", GetLastError()));
        }
    }
    /** @todo Deal with non-existing files if it becomes necessary (it's not for VS2010). */

    /* Do fallback, please. */
    return K_FALSE;
}


/** Kernel32 - CreateFileA */
static HANDLE WINAPI kwSandbox_Kernel32_CreateFileA(LPCSTR pszFilename, DWORD dwDesiredAccess, DWORD dwShareMode,
                                                    LPSECURITY_ATTRIBUTES pSecAttrs, DWORD dwCreationDisposition,
                                                    DWORD dwFlagsAndAttributes, HANDLE hTemplateFile)
{
    HANDLE hFile;
    if (dwCreationDisposition == FILE_OPEN_IF)
    {
        if (   dwDesiredAccess == GENERIC_READ
            || dwDesiredAccess == FILE_GENERIC_READ)
        {
            if (dwShareMode & FILE_SHARE_READ)
            {
                if (   !pSecAttrs
                    || (   pSecAttrs->nLength == sizeof(*pSecAttrs)
                        && pSecAttrs->lpSecurityDescriptor == NULL ) )
                {
                    const char *pszExt = kHlpGetExt(pszFilename);
                    if (kwFsIsCachableExtensionA(pszExt, K_FALSE /*fAttrQuery*/))
                    {
                        KFSLOOKUPERROR enmError;
                        PKFSOBJ pFsObj = kFsCacheLookupNoMissingA(g_pFsCache, pszFilename, &enmError);
                        if (pFsObj)
                        {
                            KBOOL fRc = kwFsObjCacheCreateFile(pFsObj, dwDesiredAccess, pSecAttrs && pSecAttrs->bInheritHandle,
                                                               &hFile);
                            kFsCacheObjRelease(g_pFsCache, pFsObj);
                            if (fRc)
                            {
                                KWFS_LOG(("CreateFileA(%s) -> %p [cached]\n", pszFilename, hFile));
                                return hFile;
                            }
                        }

                        /* fallback */
                        hFile = CreateFileA(pszFilename, dwDesiredAccess, dwShareMode, pSecAttrs,
                                            dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);
                        KWFS_LOG(("CreateFileA(%s) -> %p (err=%u) [fallback]\n", pszFilename, hFile, GetLastError()));
                        return hFile;
                    }
                }
            }
        }
    }

    hFile = CreateFileA(pszFilename, dwDesiredAccess, dwShareMode, pSecAttrs,
                        dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);
    KWFS_LOG(("CreateFileA(%s) -> %p\n", pszFilename, hFile));
    return hFile;
}


/** Kernel32 - CreateFileW */
static HANDLE WINAPI kwSandbox_Kernel32_CreateFileW(LPCWSTR pwszFilename, DWORD dwDesiredAccess, DWORD dwShareMode,
                                                    LPSECURITY_ATTRIBUTES pSecAttrs, DWORD dwCreationDisposition,
                                                    DWORD dwFlagsAndAttributes, HANDLE hTemplateFile)
{
    HANDLE hFile;

#ifdef WITH_TEMP_MEMORY_FILES
    /* First check for temporary files (cl.exe only). */
    if (   g_Sandbox.pTool->u.Sandboxed.enmHint == KWTOOLHINT_VISUAL_CPP_CL
        && !(dwFlagsAndAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_DEVICE | FILE_FLAG_BACKUP_SEMANTICS))
        && !(dwDesiredAccess & (GENERIC_EXECUTE | FILE_EXECUTE))
        && kwFsIsClTempFileW(pwszFilename))
    {
        hFile = kwFsTempFileCreateW(pwszFilename, dwDesiredAccess, dwCreationDisposition);
        KWFS_LOG(("CreateFileW(%ls) -> %p [temp]\n", pwszFilename, hFile));
        return hFile;
    }
#endif

    /* Then check for include files and similar. */
    if (dwCreationDisposition == FILE_OPEN_IF)
    {
        if (   dwDesiredAccess == GENERIC_READ
            || dwDesiredAccess == FILE_GENERIC_READ)
        {
            if (dwShareMode & FILE_SHARE_READ)
            {
                if (   !pSecAttrs
                    || (   pSecAttrs->nLength == sizeof(*pSecAttrs)
                        && pSecAttrs->lpSecurityDescriptor == NULL ) )
                {
                    if (kwFsIsCachablePathExtensionW(pwszFilename, K_FALSE /*fAttrQuery*/))
                    {
                        /** @todo rewrite to pure UTF-16. */
                        char szTmp[2048];
                        KSIZE cch = kwUtf16ToStr(pwszFilename, szTmp, sizeof(szTmp));
                        if (cch < sizeof(szTmp))
                            return kwSandbox_Kernel32_CreateFileA(szTmp, dwDesiredAccess, dwShareMode, pSecAttrs,
                                                                  dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);
                    }
                }
                else
                    KWFS_LOG(("CreateFileW: incompatible security attributes (nLength=%#x pDesc=%p)\n",
                              pSecAttrs->nLength, pSecAttrs->lpSecurityDescriptor));
            }
            else
                KWFS_LOG(("CreateFileW: incompatible sharing mode %#x\n", dwShareMode));
        }
        else
            KWFS_LOG(("CreateFileW: incompatible desired access %#x\n", dwDesiredAccess));
    }
    else
        KWFS_LOG(("CreateFileW: incompatible disposition %u\n", dwCreationDisposition));
    hFile = CreateFileW(pwszFilename, dwDesiredAccess, dwShareMode, pSecAttrs,
                        dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);
    KWFS_LOG(("CreateFileW(%ls) -> %p\n", pwszFilename, hFile));
    return hFile;
}


/** Kernel32 - SetFilePointer */
static DWORD WINAPI kwSandbox_Kernel32_SetFilePointer(HANDLE hFile, LONG cbMove, PLONG pcbMoveHi, DWORD dwMoveMethod)
{
    KUPTR const idxHandle = KW_HANDLE_TO_INDEX(hFile);
    if (idxHandle < g_Sandbox.cHandles)
    {
        PKWHANDLE pHandle = g_Sandbox.papHandles[idxHandle];
        if (pHandle != NULL)
        {
            KU32 cbFile;
            KI64 offMove = pcbMoveHi ? ((KI64)*pcbMoveHi << 32) | cbMove : cbMove;
            switch (pHandle->enmType)
            {
                case KWHANDLETYPE_FSOBJ_READ_CACHE:
                    cbFile = pHandle->u.pCachedFile->cbCached;
                    break;
#ifdef WITH_TEMP_MEMORY_FILES
                case KWHANDLETYPE_TEMP_FILE:
                    cbFile = pHandle->u.pTempFile->cbFile;
                    break;
                case KWHANDLETYPE_TEMP_FILE_MAPPING:
#endif
                default:
                    kHlpAssertFailed();
                    SetLastError(ERROR_INVALID_FUNCTION);
                    return INVALID_SET_FILE_POINTER;
            }

            switch (dwMoveMethod)
            {
                case FILE_BEGIN:
                    break;
                case FILE_CURRENT:
                    offMove += pHandle->offFile;
                    break;
                case FILE_END:
                    offMove += cbFile;
                    break;
                default:
                    KWFS_LOG(("SetFilePointer(%p) - invalid seek method %u! [cached]\n", hFile));
                    SetLastError(ERROR_INVALID_PARAMETER);
                    return INVALID_SET_FILE_POINTER;
            }
            if (offMove >= 0)
            {
                if (offMove >= (KSSIZE)cbFile)
                {
                    /* For read-only files, seeking beyond the end isn't useful to us, so clamp it. */
                    if (pHandle->enmType != KWHANDLETYPE_TEMP_FILE)
                        offMove = (KSSIZE)cbFile;
                    /* For writable files, seeking beyond the end is fine, but check that we've got
                       the type range for the request. */
                    else if (((KU64)offMove & KU32_MAX) != (KU64)offMove)
                    {
                        kHlpAssertMsgFailed(("%#llx\n", offMove));
                        SetLastError(ERROR_SEEK);
                        return INVALID_SET_FILE_POINTER;
                    }
                }
                pHandle->offFile = (KU32)offMove;
            }
            else
            {
                KWFS_LOG(("SetFilePointer(%p) - negative seek! [cached]\n", hFile));
                SetLastError(ERROR_NEGATIVE_SEEK);
                return INVALID_SET_FILE_POINTER;
            }
            if (pcbMoveHi)
                *pcbMoveHi = (KU64)offMove >> 32;
            KWFS_LOG(("SetFilePointer(%p) -> %#llx [cached]\n", hFile, offMove));
            SetLastError(NO_ERROR);
            return (KU32)offMove;
        }
    }
    KWFS_LOG(("SetFilePointer(%p)\n", hFile));
    return SetFilePointer(hFile, cbMove, pcbMoveHi, dwMoveMethod);
}


/** Kernel32 - SetFilePointerEx */
static BOOL WINAPI kwSandbox_Kernel32_SetFilePointerEx(HANDLE hFile, LARGE_INTEGER offMove, PLARGE_INTEGER poffNew,
                                                       DWORD dwMoveMethod)
{
    KUPTR const idxHandle = KW_HANDLE_TO_INDEX(hFile);
    if (idxHandle < g_Sandbox.cHandles)
    {
        PKWHANDLE pHandle = g_Sandbox.papHandles[idxHandle];
        if (pHandle != NULL)
        {
            KI64 offMyMove = offMove.QuadPart;
            KU32 cbFile;
            switch (pHandle->enmType)
            {
                case KWHANDLETYPE_FSOBJ_READ_CACHE:
                    cbFile = pHandle->u.pCachedFile->cbCached;
                    break;
#ifdef WITH_TEMP_MEMORY_FILES
                case KWHANDLETYPE_TEMP_FILE:
                    cbFile = pHandle->u.pTempFile->cbFile;
                    break;
                case KWHANDLETYPE_TEMP_FILE_MAPPING:
#endif
                default:
                    kHlpAssertFailed();
                    SetLastError(ERROR_INVALID_FUNCTION);
                    return INVALID_SET_FILE_POINTER;
            }

            switch (dwMoveMethod)
            {
                case FILE_BEGIN:
                    break;
                case FILE_CURRENT:
                    offMyMove += pHandle->offFile;
                    break;
                case FILE_END:
                    offMyMove += cbFile;
                    break;
                default:
                    KWFS_LOG(("SetFilePointer(%p) - invalid seek method %u! [cached]\n", hFile));
                    SetLastError(ERROR_INVALID_PARAMETER);
                    return INVALID_SET_FILE_POINTER;
            }
            if (offMyMove >= 0)
            {
                if (offMyMove >= (KSSIZE)cbFile)
                {
                    /* For read-only files, seeking beyond the end isn't useful to us, so clamp it. */
                    if (pHandle->enmType != KWHANDLETYPE_TEMP_FILE)
                        offMyMove = (KSSIZE)cbFile;
                    /* For writable files, seeking beyond the end is fine, but check that we've got
                       the type range for the request. */
                    else if (((KU64)offMyMove & KU32_MAX) != (KU64)offMyMove)
                    {
                        kHlpAssertMsgFailed(("%#llx\n", offMyMove));
                        SetLastError(ERROR_SEEK);
                        return INVALID_SET_FILE_POINTER;
                    }
                }
                pHandle->offFile = (KU32)offMyMove;
            }
            else
            {
                KWFS_LOG(("SetFilePointerEx(%p) - negative seek! [cached]\n", hFile));
                SetLastError(ERROR_NEGATIVE_SEEK);
                return INVALID_SET_FILE_POINTER;
            }
            if (poffNew)
                poffNew->QuadPart = offMyMove;
            KWFS_LOG(("SetFilePointerEx(%p) -> TRUE, %#llx [cached]\n", hFile, offMyMove));
            return TRUE;
        }
    }
    KWFS_LOG(("SetFilePointerEx(%p)\n", hFile));
    return SetFilePointerEx(hFile, offMove, poffNew, dwMoveMethod);
}


/** Kernel32 - ReadFile */
static BOOL WINAPI kwSandbox_Kernel32_ReadFile(HANDLE hFile, LPVOID pvBuffer, DWORD cbToRead, LPDWORD pcbActuallyRead,
                                               LPOVERLAPPED pOverlapped)
{
    KUPTR const idxHandle = KW_HANDLE_TO_INDEX(hFile);
    if (idxHandle < g_Sandbox.cHandles)
    {
        PKWHANDLE pHandle = g_Sandbox.papHandles[idxHandle];
        if (pHandle != NULL)
        {
            switch (pHandle->enmType)
            {
                case KWHANDLETYPE_FSOBJ_READ_CACHE:
                {
                    PKFSWCACHEDFILE pCachedFile = pHandle->u.pCachedFile;
                    KU32            cbActually = pCachedFile->cbCached - pHandle->offFile;
                    if (cbActually > cbToRead)
                        cbActually = cbToRead;
                    else if (cbActually < cbToRead)
                        ((KU8 *)pvBuffer)[cbActually] = '\0'; // hack hack hack

                    kHlpMemCopy(pvBuffer, &pCachedFile->pbCached[pHandle->offFile], cbActually);
                    pHandle->offFile += cbActually;

                    kHlpAssert(!pOverlapped); kHlpAssert(pcbActuallyRead);
                    *pcbActuallyRead = cbActually;

                    KWFS_LOG(("ReadFile(%p,,%#x) -> TRUE, %#x bytes [cached]\n", hFile, cbToRead, cbActually));
                    return TRUE;
                }

#ifdef WITH_TEMP_MEMORY_FILES
                case KWHANDLETYPE_TEMP_FILE:
                {
                    PKWFSTEMPFILE   pTempFile  = pHandle->u.pTempFile;
                    KU32            cbActually;
                    if (pHandle->offFile < pTempFile->cbFile)
                    {
                        cbActually = pTempFile->cbFile - pHandle->offFile;
                        if (cbActually > cbToRead)
                            cbActually = cbToRead;

                        /* Copy the data. */
                        if (cbActually > 0)
                        {
                            KU32                    cbLeft;
                            KU32                    offSeg;
                            KWFSTEMPFILESEG const  *paSegs = pTempFile->paSegs;

                            /* Locate the segment containing the byte at offFile. */
                            KU32 iSeg   = pTempFile->cSegs - 1;
                            kHlpAssert(pTempFile->cSegs > 0);
                            while (paSegs[iSeg].offData > pHandle->offFile)
                                iSeg--;

                            /* Copy out the data. */
                            cbLeft = cbActually;
                            offSeg = (pHandle->offFile - paSegs[iSeg].offData);
                            for (;;)
                            {
                                KU32 cbAvail = paSegs[iSeg].cbDataAlloc - offSeg;
                                if (cbAvail >= cbLeft)
                                {
                                    kHlpMemCopy(pvBuffer, &paSegs[iSeg].pbData[offSeg], cbLeft);
                                    break;
                                }

                                pvBuffer = kHlpMemPCopy(pvBuffer, &paSegs[iSeg].pbData[offSeg], cbAvail);
                                cbLeft  -= cbAvail;
                                offSeg   = 0;
                                iSeg++;
                                kHlpAssert(iSeg < pTempFile->cSegs);
                            }

                            /* Update the file offset. */
                            pHandle->offFile += cbActually;
                        }
                    }
                    /* Read does not commit file space, so return zero bytes. */
                    else
                        cbActually = 0;

                    kHlpAssert(!pOverlapped); kHlpAssert(pcbActuallyRead);
                    *pcbActuallyRead = cbActually;

                    KWFS_LOG(("ReadFile(%p,,%#x) -> TRUE, %#x bytes [temp]\n", hFile, cbToRead, (KU32)cbActually));
                    return TRUE;
                }

                case KWHANDLETYPE_TEMP_FILE_MAPPING:
#endif /* WITH_TEMP_MEMORY_FILES */
                default:
                    kHlpAssertFailed();
                    SetLastError(ERROR_INVALID_FUNCTION);
                    *pcbActuallyRead = 0;
                    return FALSE;
            }
        }
    }

    KWFS_LOG(("ReadFile(%p)\n", hFile));
    return ReadFile(hFile, pvBuffer, cbToRead, pcbActuallyRead, pOverlapped);
}


/** Kernel32 - ReadFileEx */
static BOOL WINAPI kwSandbox_Kernel32_ReadFileEx(HANDLE hFile, LPVOID pvBuffer, DWORD cbToRead, LPOVERLAPPED pOverlapped,
                                                 LPOVERLAPPED_COMPLETION_ROUTINE pfnCompletionRoutine)
{
    KUPTR const idxHandle = KW_HANDLE_TO_INDEX(hFile);
    if (idxHandle < g_Sandbox.cHandles)
    {
        PKWHANDLE pHandle = g_Sandbox.papHandles[idxHandle];
        if (pHandle != NULL)
        {
            kHlpAssertFailed();
        }
    }

    KWFS_LOG(("ReadFile(%p)\n", hFile));
    return ReadFileEx(hFile, pvBuffer, cbToRead, pOverlapped, pfnCompletionRoutine);
}

#ifdef WITH_TEMP_MEMORY_FILES

static KBOOL kwFsTempFileEnsureSpace(PKWFSTEMPFILE pTempFile, KU32 offFile, KU32 cbNeeded)
{
    KU32 cbMinFile = offFile + cbNeeded;
    if (cbMinFile >= offFile)
    {
        /* Calc how much space we've already allocated and  */
        if (cbMinFile <= pTempFile->cbFileAllocated)
            return K_TRUE;

        /* Grow the file. */
        if (cbMinFile <= KWFS_TEMP_FILE_MAX)
        {
            int  rc;
            KU32 cSegs    = pTempFile->cSegs;
            KU32 cbNewSeg = cbMinFile > 4*1024*1024 ? 256*1024 : 4*1024*1024;
            do
            {
                /* grow the segment array? */
                if ((cSegs % 16) == 0)
                {
                    void *pvNew = kHlpRealloc(pTempFile->paSegs, (cSegs + 16) * sizeof(pTempFile->paSegs[0]));
                    if (!pvNew)
                        return K_FALSE;
                    pTempFile->paSegs = (PKWFSTEMPFILESEG)pvNew;
                }

                /* Use page alloc here to simplify mapping later. */
                rc = kHlpPageAlloc((void **)&pTempFile->paSegs[cSegs].pbData, cbNewSeg, KPROT_READWRITE, K_FALSE);
                if (rc == 0)
                { /* likely */ }
                else
                {
                    cbNewSeg = 64*1024*1024;
                    rc = kHlpPageAlloc((void **)&pTempFile->paSegs[cSegs].pbData, cbNewSeg, KPROT_READWRITE, K_FALSE);
                    if (rc != 0)
                        return K_FALSE;
                }
                pTempFile->paSegs[cSegs].offData     = pTempFile->cbFileAllocated;
                pTempFile->paSegs[cSegs].cbDataAlloc = cbNewSeg;
                pTempFile->cbFileAllocated          += cbNewSeg;
                pTempFile->cSegs                     = ++cSegs;

            } while (pTempFile->cbFileAllocated < cbMinFile);

            return K_TRUE;
        }
    }

    kHlpAssertMsgFailed(("Out of bounds offFile=%#x + cbNeeded=%#x = %#x\n", offFile, cbNeeded, offFile + cbNeeded));
    return K_FALSE;
}


/** Kernel32 - WriteFile */
static BOOL WINAPI kwSandbox_Kernel32_WriteFile(HANDLE hFile, LPCVOID pvBuffer, DWORD cbToWrite, LPDWORD pcbActuallyWritten,
                                                LPOVERLAPPED pOverlapped)
{
    KUPTR const idxHandle = KW_HANDLE_TO_INDEX(hFile);
    if (idxHandle < g_Sandbox.cHandles)
    {
        PKWHANDLE pHandle = g_Sandbox.papHandles[idxHandle];
        if (pHandle != NULL)
        {
            switch (pHandle->enmType)
            {
                case KWHANDLETYPE_TEMP_FILE:
                {
                    PKWFSTEMPFILE   pTempFile  = pHandle->u.pTempFile;

                    kHlpAssert(!pOverlapped);
                    kHlpAssert(pcbActuallyWritten);

                    if (kwFsTempFileEnsureSpace(pTempFile, pHandle->offFile, cbToWrite))
                    {
                        KU32                    cbLeft;
                        KU32                    offSeg;

                        /* Locate the segment containing the byte at offFile. */
                        KWFSTEMPFILESEG const  *paSegs = pTempFile->paSegs;
                        KU32                    iSeg   = pTempFile->cSegs - 1;
                        kHlpAssert(pTempFile->cSegs > 0);
                        while (paSegs[iSeg].offData > pHandle->offFile)
                            iSeg--;

                        /* Copy in the data. */
                        cbLeft = cbToWrite;
                        offSeg = (pHandle->offFile - paSegs[iSeg].offData);
                        for (;;)
                        {
                            KU32 cbAvail = paSegs[iSeg].cbDataAlloc - offSeg;
                            if (cbAvail >= cbLeft)
                            {
                                kHlpMemCopy(&paSegs[iSeg].pbData[offSeg], pvBuffer, cbLeft);
                                break;
                            }

                            kHlpMemCopy(&paSegs[iSeg].pbData[offSeg], pvBuffer, cbAvail);
                            pvBuffer = (KU8 const *)pvBuffer + cbAvail;
                            cbLeft  -= cbAvail;
                            offSeg   = 0;
                            iSeg++;
                            kHlpAssert(iSeg < pTempFile->cSegs);
                        }

                        /* Update the file offset. */
                        pHandle->offFile += cbToWrite;
                        if (pHandle->offFile > pTempFile->cbFile)
                            pTempFile->cbFile = pHandle->offFile;

                        *pcbActuallyWritten = cbToWrite;
                        KWFS_LOG(("WriteFile(%p,,%#x) -> TRUE [temp]\n", hFile, cbToWrite));
                        return TRUE;
                    }

                    *pcbActuallyWritten = 0;
                    SetLastError(ERROR_NOT_ENOUGH_MEMORY);
                    return FALSE;
                }

                case KWHANDLETYPE_FSOBJ_READ_CACHE:
                    kHlpAssertFailed();
                    SetLastError(ERROR_ACCESS_DENIED);
                    *pcbActuallyWritten = 0;
                    return FALSE;

                default:
                case KWHANDLETYPE_TEMP_FILE_MAPPING:
                    kHlpAssertFailed();
                    SetLastError(ERROR_INVALID_FUNCTION);
                    *pcbActuallyWritten = 0;
                    return FALSE;
            }
        }
    }

    KWFS_LOG(("WriteFile(%p)\n", hFile));
    return WriteFile(hFile, pvBuffer, cbToWrite, pcbActuallyWritten, pOverlapped);
}


/** Kernel32 - WriteFileEx */
static BOOL WINAPI kwSandbox_Kernel32_WriteFileEx(HANDLE hFile, LPCVOID pvBuffer, DWORD cbToWrite, LPOVERLAPPED pOverlapped,
                                                  LPOVERLAPPED_COMPLETION_ROUTINE pfnCompletionRoutine)
{
    KUPTR const idxHandle = KW_HANDLE_TO_INDEX(hFile);
    if (idxHandle < g_Sandbox.cHandles)
    {
        PKWHANDLE pHandle = g_Sandbox.papHandles[idxHandle];
        if (pHandle != NULL)
        {
            kHlpAssertFailed();
        }
    }

    KWFS_LOG(("WriteFileEx(%p)\n", hFile));
    return WriteFileEx(hFile, pvBuffer, cbToWrite, pOverlapped, pfnCompletionRoutine);
}


/** Kernel32 - SetEndOfFile; */
static BOOL WINAPI kwSandbox_Kernel32_SetEndOfFile(HANDLE hFile)
{
    KUPTR const idxHandle = KW_HANDLE_TO_INDEX(hFile);
    if (idxHandle < g_Sandbox.cHandles)
    {
        PKWHANDLE pHandle = g_Sandbox.papHandles[idxHandle];
        if (pHandle != NULL)
        {
            switch (pHandle->enmType)
            {
                case KWHANDLETYPE_TEMP_FILE:
                {
                    PKWFSTEMPFILE   pTempFile  = pHandle->u.pTempFile;
                    if (   pHandle->offFile > pTempFile->cbFile
                        && !kwFsTempFileEnsureSpace(pTempFile, pHandle->offFile, 0))
                    {
                        kHlpAssertFailed();
                        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
                        return FALSE;
                    }

                    pTempFile->cbFile = pHandle->offFile;
                    KWFS_LOG(("SetEndOfFile(%p) -> TRUE (cbFile=%#x)\n", hFile, pTempFile->cbFile));
                    return TRUE;
                }

                case KWHANDLETYPE_FSOBJ_READ_CACHE:
                    kHlpAssertFailed();
                    SetLastError(ERROR_ACCESS_DENIED);
                    return FALSE;

                default:
                case KWHANDLETYPE_TEMP_FILE_MAPPING:
                    kHlpAssertFailed();
                    SetLastError(ERROR_INVALID_FUNCTION);
                    return FALSE;
            }
        }
    }

    KWFS_LOG(("SetEndOfFile(%p)\n", hFile));
    return SetEndOfFile(hFile);
}


/** Kernel32 - GetFileType  */
static BOOL WINAPI kwSandbox_Kernel32_GetFileType(HANDLE hFile)
{
    KUPTR const idxHandle = KW_HANDLE_TO_INDEX(hFile);
    if (idxHandle < g_Sandbox.cHandles)
    {
        PKWHANDLE pHandle = g_Sandbox.papHandles[idxHandle];
        if (pHandle != NULL)
        {
            switch (pHandle->enmType)
            {
                case KWHANDLETYPE_FSOBJ_READ_CACHE:
                    KWFS_LOG(("GetFileType(%p) -> FILE_TYPE_DISK [cached]\n", hFile));
                    return FILE_TYPE_DISK;

                case KWHANDLETYPE_TEMP_FILE:
                    KWFS_LOG(("GetFileType(%p) -> FILE_TYPE_DISK [temp]\n", hFile));
                    return FILE_TYPE_DISK;
            }
        }
    }

    KWFS_LOG(("GetFileType(%p)\n", hFile));
    return GetFileType(hFile);
}


/** Kernel32 - GetFileSize  */
static DWORD WINAPI kwSandbox_Kernel32_GetFileSize(HANDLE hFile, LPDWORD pcbHighDword)
{
    KUPTR const idxHandle = KW_HANDLE_TO_INDEX(hFile);
    if (idxHandle < g_Sandbox.cHandles)
    {
        PKWHANDLE pHandle = g_Sandbox.papHandles[idxHandle];
        if (pHandle != NULL)
        {
            if (pcbHighDword)
                *pcbHighDword = 0;
            SetLastError(NO_ERROR);
            switch (pHandle->enmType)
            {
                case KWHANDLETYPE_FSOBJ_READ_CACHE:
                    KWFS_LOG(("GetFileSize(%p) -> %#x [cached]\n", hFile, pHandle->u.pCachedFile->cbCached));
                    return pHandle->u.pCachedFile->cbCached;

                case KWHANDLETYPE_TEMP_FILE:
                    KWFS_LOG(("GetFileSize(%p) -> %#x [temp]\n", hFile, pHandle->u.pTempFile->cbFile));
                    return pHandle->u.pTempFile->cbFile;

                default:
                    kHlpAssertFailed();
                    SetLastError(ERROR_INVALID_FUNCTION);
                    return INVALID_FILE_SIZE;
            }
        }
    }

    KWFS_LOG(("GetFileSize(%p,)\n", hFile));
    return GetFileSize(hFile, pcbHighDword);
}


/** Kernel32 - GetFileSizeEx  */
static BOOL WINAPI kwSandbox_Kernel32_GetFileSizeEx(HANDLE hFile, PLARGE_INTEGER pcbFile)
{
    KUPTR const idxHandle = KW_HANDLE_TO_INDEX(hFile);
    if (idxHandle < g_Sandbox.cHandles)
    {
        PKWHANDLE pHandle = g_Sandbox.papHandles[idxHandle];
        if (pHandle != NULL)
        {
            switch (pHandle->enmType)
            {
                case KWHANDLETYPE_FSOBJ_READ_CACHE:
                    KWFS_LOG(("GetFileSizeEx(%p) -> TRUE, %#x [cached]\n", hFile, pHandle->u.pCachedFile->cbCached));
                    pcbFile->QuadPart = pHandle->u.pCachedFile->cbCached;
                    return TRUE;

                case KWHANDLETYPE_TEMP_FILE:
                    KWFS_LOG(("GetFileSizeEx(%p) -> TRUE, %#x [temp]\n", hFile, pHandle->u.pTempFile->cbFile));
                    pcbFile->QuadPart = pHandle->u.pTempFile->cbFile;
                    return TRUE;

                default:
                    kHlpAssertFailed();
                    SetLastError(ERROR_INVALID_FUNCTION);
                    return INVALID_FILE_SIZE;
            }
        }
    }

    KWFS_LOG(("GetFileSizeEx(%p,)\n", hFile));
    return GetFileSizeEx(hFile, pcbFile);
}


/** Kernel32 - CreateFileMapping  */
static HANDLE WINAPI kwSandbox_Kernel32_CreateFileMappingW(HANDLE hFile, LPSECURITY_ATTRIBUTES pSecAttrs,
                                                           DWORD fProtect, DWORD dwMaximumSizeHigh,
                                                           DWORD dwMaximumSizeLow, LPCWSTR pwszName)
{
    KUPTR const idxHandle = KW_HANDLE_TO_INDEX(hFile);
    if (idxHandle < g_Sandbox.cHandles)
    {
        PKWHANDLE pHandle = g_Sandbox.papHandles[idxHandle];
        if (pHandle != NULL)
        {
            switch (pHandle->enmType)
            {
                case KWHANDLETYPE_TEMP_FILE:
                {
                    PKWFSTEMPFILE pTempFile = pHandle->u.pTempFile;
                    if (   (   fProtect == PAGE_READONLY
                            || fProtect == PAGE_EXECUTE_READ)
                        && dwMaximumSizeHigh == 0
                        &&  (   dwMaximumSizeLow == 0
                             || dwMaximumSizeLow == pTempFile->cbFile)
                        && pwszName == NULL)
                    {
                        HANDLE hMapping = kwFsTempFileCreateHandle(pHandle->u.pTempFile, GENERIC_READ, K_TRUE /*fMapping*/);
                        KWFS_LOG(("CreateFileMappingW(%p, %u) -> %p [temp]\n", hFile, fProtect, hMapping));
                        return hMapping;
                    }
                    kHlpAssertMsgFailed(("fProtect=%#x cb=%#x'%08x name=%p\n",
                                         fProtect, dwMaximumSizeHigh, dwMaximumSizeLow, pwszName));
                    SetLastError(ERROR_ACCESS_DENIED);
                    return INVALID_HANDLE_VALUE;
                }
            }
        }
    }

    KWFS_LOG(("CreateFileMappingW(%p)\n", hFile));
    return CreateFileMappingW(hFile, pSecAttrs, fProtect, dwMaximumSizeHigh, dwMaximumSizeLow, pwszName);
}

/** Kernel32 - MapViewOfFile  */
static HANDLE WINAPI kwSandbox_Kernel32_MapViewOfFile(HANDLE hSection, DWORD dwDesiredAccess,
                                                      DWORD offFileHigh, DWORD offFileLow, SIZE_T cbToMap)
{
    KUPTR const idxHandle = KW_HANDLE_TO_INDEX(hSection);
    if (idxHandle < g_Sandbox.cHandles)
    {
        PKWHANDLE pHandle = g_Sandbox.papHandles[idxHandle];
        if (pHandle != NULL)
        {
            switch (pHandle->enmType)
            {
                case KWHANDLETYPE_FSOBJ_READ_CACHE:
                case KWHANDLETYPE_TEMP_FILE:
                    kHlpAssertFailed();
                    SetLastError(ERROR_NOT_ENOUGH_MEMORY);
                    return NULL;

                case KWHANDLETYPE_TEMP_FILE_MAPPING:
                {
                    PKWFSTEMPFILE pTempFile = pHandle->u.pTempFile;
                    if (   dwDesiredAccess == FILE_MAP_READ
                        && offFileHigh == 0
                        && offFileLow  == 0
                        && (cbToMap == 0 || cbToMap == pTempFile->cbFile) )
                    {
                        kHlpAssert(pTempFile->cMappings == 0 || pTempFile->cSegs == 1);
                        if (pTempFile->cSegs != 1)
                        {
                            KU32    iSeg;
                            KU32    cbLeft;
                            KU32    cbAll = pTempFile->cbFile ? (KU32)K_ALIGN_Z(pTempFile->cbFile, 0x2000) : 0x1000;
                            KU8    *pbAll = NULL;
                            int rc = kHlpPageAlloc((void **)&pbAll, cbAll, KPROT_READWRITE, K_FALSE);
                            if (rc != 0)
                            {
                                kHlpAssertFailed();
                                SetLastError(ERROR_NOT_ENOUGH_MEMORY);
                                return NULL;
                            }

                            cbLeft = pTempFile->cbFile;
                            for (iSeg = 0; iSeg < pTempFile->cSegs && cbLeft > 0; iSeg++)
                            {
                                KU32 cbToCopy = K_MIN(cbLeft, pTempFile->paSegs[iSeg].cbDataAlloc);
                                kHlpMemCopy(&pbAll[pTempFile->paSegs[iSeg].offData], pTempFile->paSegs[iSeg].pbData, cbToCopy);
                                cbLeft -= cbToCopy;
                            }

                            for (iSeg = 0; iSeg < pTempFile->cSegs; iSeg++)
                            {
                                kHlpPageFree(pTempFile->paSegs[iSeg].pbData, pTempFile->paSegs[iSeg].cbDataAlloc);
                                pTempFile->paSegs[iSeg].pbData = NULL;
                                pTempFile->paSegs[iSeg].cbDataAlloc = 0;
                            }

                            pTempFile->cSegs                 = 1;
                            pTempFile->cbFileAllocated       = cbAll;
                            pTempFile->paSegs[0].cbDataAlloc = cbAll;
                            pTempFile->paSegs[0].pbData      = pbAll;
                            pTempFile->paSegs[0].offData     = 0;
                        }

                        pTempFile->cMappings++;
                        kHlpAssert(pTempFile->cMappings == 1);

                        KWFS_LOG(("CreateFileMappingW(%p) -> %p [temp]\n", hSection, pTempFile->paSegs[0].pbData));
                        return pTempFile->paSegs[0].pbData;
                    }

                    kHlpAssertMsgFailed(("dwDesiredAccess=%#x offFile=%#x'%08x cbToMap=%#x (cbFile=%#x)\n",
                                         dwDesiredAccess, offFileHigh, offFileLow, cbToMap, pTempFile->cbFile));
                    SetLastError(ERROR_NOT_ENOUGH_MEMORY);
                    return NULL;
                }
            }
        }
    }

    KWFS_LOG(("MapViewOfFile(%p)\n", hSection));
    return MapViewOfFile(hSection, dwDesiredAccess, offFileHigh, offFileLow, cbToMap);
}
/** @todo MapViewOfFileEx */


/** Kernel32 - UnmapViewOfFile  */
static BOOL WINAPI kwSandbox_Kernel32_UnmapViewOfFile(LPCVOID pvBase)
{
    /* Is this one of our temporary mappings? */
    PKWFSTEMPFILE pCur = g_Sandbox.pTempFileHead;
    while (pCur)
    {
        if (   pCur->cMappings > 0
            && pCur->paSegs[0].pbData == (KU8 *)pvBase)
        {
            pCur->cMappings--;
            KWFS_LOG(("UnmapViewOfFile(%p) -> TRUE [temp]\n", pvBase));
            return TRUE;
        }
        pCur = pCur->pNext;
    }

    KWFS_LOG(("UnmapViewOfFile(%p)\n", pvBase));
    return UnmapViewOfFile(pvBase);
}

/** @todo UnmapViewOfFileEx */


#endif /* WITH_TEMP_MEMORY_FILES */

/** Kernel32 - CloseHandle */
static BOOL WINAPI kwSandbox_Kernel32_CloseHandle(HANDLE hObject)
{
    BOOL        fRet;
    KUPTR const idxHandle = KW_HANDLE_TO_INDEX(hObject);
    if (   idxHandle < g_Sandbox.cHandles
        && g_Sandbox.papHandles[idxHandle] != NULL)
    {
        fRet = CloseHandle(hObject);
        if (fRet)
        {
            PKWHANDLE pHandle = g_Sandbox.papHandles[idxHandle];
            g_Sandbox.papHandles[idxHandle] = NULL;
            g_Sandbox.cActiveHandles--;
#ifdef WITH_TEMP_MEMORY_FILES
            if (pHandle->enmType == KWHANDLETYPE_TEMP_FILE)
            {
                kHlpAssert(pHandle->u.pTempFile->cActiveHandles > 0);
                pHandle->u.pTempFile->cActiveHandles--;
            }
#endif
            kHlpFree(pHandle);
            KWFS_LOG(("CloseHandle(%p) -> TRUE [intercepted handle]\n", hObject));
        }
        else
            KWFS_LOG(("CloseHandle(%p) -> FALSE [intercepted handle] err=%u!\n", hObject, GetLastError()));
    }
    else
    {
        KWFS_LOG(("CloseHandle(%p)\n", hObject));
        fRet = CloseHandle(hObject);
    }
    return fRet;
}


/** Kernel32 - GetFileAttributesA. */
static DWORD WINAPI kwSandbox_Kernel32_GetFileAttributesA(LPCSTR pszFilename)
{
    DWORD       fRet;
    const char *pszExt = kHlpGetExt(pszFilename);
    if (kwFsIsCachableExtensionA(pszExt, K_TRUE /*fAttrQuery*/))
    {
        KFSLOOKUPERROR enmError;
        PKFSOBJ pFsObj = kFsCacheLookupNoMissingA(g_pFsCache, pszFilename, &enmError);
        if (pFsObj)
        {
            kHlpAssert(pFsObj->fHaveStats);
            fRet = pFsObj->Stats.st_attribs;
            kFsCacheObjRelease(g_pFsCache, pFsObj);
        }
        else
        {
            SetLastError(kwFsLookupErrorToWindowsError(enmError));
            fRet = INVALID_FILE_ATTRIBUTES;
        }

        KWFS_LOG(("GetFileAttributesA(%s) -> %#x [cached]\n", pszFilename, fRet));
        return fRet;
    }

    fRet = GetFileAttributesA(pszFilename);
    KWFS_LOG(("GetFileAttributesA(%s) -> %#x\n", pszFilename, fRet));
    return fRet;
}


/** Kernel32 - GetFileAttributesW. */
static DWORD WINAPI kwSandbox_Kernel32_GetFileAttributesW(LPCWSTR pwszFilename)
{
    DWORD fRet;
    if (kwFsIsCachablePathExtensionW(pwszFilename, K_TRUE /*fAttrQuery*/))
    {
        KFSLOOKUPERROR enmError;
        PKFSOBJ pFsObj = kFsCacheLookupNoMissingW(g_pFsCache, pwszFilename, &enmError);
        if (pFsObj)
        {
            kHlpAssert(pFsObj->fHaveStats);
            fRet = pFsObj->Stats.st_attribs;
            kFsCacheObjRelease(g_pFsCache, pFsObj);
        }
        else
        {
            SetLastError(kwFsLookupErrorToWindowsError(enmError));
            fRet = INVALID_FILE_ATTRIBUTES;
        }

        KWFS_LOG(("GetFileAttributesW(%ls) -> %#x [cached]\n", pwszFilename, fRet));
        return fRet;
    }

    fRet = GetFileAttributesW(pwszFilename);
    KWFS_LOG(("GetFileAttributesW(%ls) -> %#x\n", pwszFilename, fRet));
    return fRet;
}


/** Kernel32 - GetShortPathNameW - cl1[xx].dll of VS2010 does this to the
 * directory containing each include file.  We cache the result to speed
 * things up a little. */
static DWORD WINAPI kwSandbox_Kernel32_GetShortPathNameW(LPCWSTR pwszLongPath, LPWSTR pwszShortPath, DWORD cwcShortPath)
{
    DWORD cwcRet;
    if (kwFsIsCachablePathExtensionW(pwszLongPath, K_TRUE /*fAttrQuery*/))
    {
        KFSLOOKUPERROR enmError;
        PKFSOBJ pObj = kFsCacheLookupW(g_pFsCache, pwszLongPath, &enmError);
        if (pObj)
        {
            if (pObj->bObjType != KFSOBJ_TYPE_MISSING)
            {
                if (kFsCacheObjGetFullShortPathW(pObj, pwszShortPath, cwcShortPath, '\\'))
                {
                    cwcRet = (DWORD)kwUtf16Len(pwszShortPath);

                    /* Should preserve trailing slash on directory paths. */
                    if (pObj->bObjType == KFSOBJ_TYPE_DIR)
                    {
                        if (   cwcRet + 1 < cwcShortPath
                            && pwszShortPath[cwcRet - 1] != '\\')
                        {
                            KSIZE cwcIn = kwUtf16Len(pwszLongPath);
                            if (   cwcIn > 0
                                && (pwszLongPath[cwcIn - 1] == '\\' || pwszLongPath[cwcIn - 1] == '/') )
                            {
                                pwszShortPath[cwcRet++] = '\\';
                                pwszShortPath[cwcRet]   = '\0';
                            }
                        }
                    }

                    KWFS_LOG(("GetShortPathNameW(%ls) -> '%*.*ls' & %#x [cached]\n",
                              pwszLongPath, K_MIN(cwcShortPath, cwcRet), K_MIN(cwcShortPath, cwcRet), pwszShortPath, cwcRet));
                    kFsCacheObjRelease(g_pFsCache, pObj);
                    return cwcRet;
                }

                /* fall back for complicated cases. */
            }
            kFsCacheObjRelease(g_pFsCache, pObj);
        }
    }
    cwcRet = GetShortPathNameW(pwszLongPath, pwszShortPath, cwcShortPath);
    KWFS_LOG(("GetShortPathNameW(%ls) -> '%*.*ls' & %#x\n",
              pwszLongPath, K_MIN(cwcShortPath, cwcRet), K_MIN(cwcShortPath, cwcRet), pwszShortPath, cwcRet));
    return cwcRet;
}



/**
 * Functions that needs replacing for sandboxed execution.
 */
KWREPLACEMENTFUNCTION const g_aSandboxReplacements[] =
{
    /*
     * Kernel32.dll and friends.
     */
    { TUPLE("ExitProcess"),                 NULL,       (KUPTR)kwSandbox_Kernel32_ExitProcess },
    { TUPLE("TerminateProcess"),            NULL,       (KUPTR)kwSandbox_Kernel32_TerminateProcess },

    { TUPLE("LoadLibraryA"),                NULL,       (KUPTR)kwSandbox_Kernel32_LoadLibraryA },
    { TUPLE("LoadLibraryW"),                NULL,       (KUPTR)kwSandbox_Kernel32_LoadLibraryW },
    { TUPLE("LoadLibraryExA"),              NULL,       (KUPTR)kwSandbox_Kernel32_LoadLibraryExA },
    { TUPLE("LoadLibraryExW"),              NULL,       (KUPTR)kwSandbox_Kernel32_LoadLibraryExW },
    { TUPLE("FreeLibrary"),                 NULL,       (KUPTR)kwSandbox_Kernel32_FreeLibrary },
    { TUPLE("GetModuleHandleA"),            NULL,       (KUPTR)kwSandbox_Kernel32_GetModuleHandleA },
    { TUPLE("GetModuleHandleW"),            NULL,       (KUPTR)kwSandbox_Kernel32_GetModuleHandleW },
    { TUPLE("GetProcAddress"),              NULL,       (KUPTR)kwSandbox_Kernel32_GetProcAddress },
    { TUPLE("GetModuleFileNameA"),          NULL,       (KUPTR)kwSandbox_Kernel32_GetModuleFileNameA },
    { TUPLE("GetModuleFileNameW"),          NULL,       (KUPTR)kwSandbox_Kernel32_GetModuleFileNameW },

    { TUPLE("GetCommandLineA"),             NULL,       (KUPTR)kwSandbox_Kernel32_GetCommandLineA },
    { TUPLE("GetCommandLineW"),             NULL,       (KUPTR)kwSandbox_Kernel32_GetCommandLineW },
    { TUPLE("GetStartupInfoA"),             NULL,       (KUPTR)kwSandbox_Kernel32_GetStartupInfoA },
    { TUPLE("GetStartupInfoW"),             NULL,       (KUPTR)kwSandbox_Kernel32_GetStartupInfoW },

    { TUPLE("CreateThread"),                NULL,       (KUPTR)kwSandbox_Kernel32_CreateThread },

    { TUPLE("GetEnvironmentVariableA"),     NULL,       (KUPTR)kwSandbox_Kernel32_GetEnvironmentVariableA },
    { TUPLE("GetEnvironmentVariableW"),     NULL,       (KUPTR)kwSandbox_Kernel32_GetEnvironmentVariableW },
    { TUPLE("SetEnvironmentVariableA"),     NULL,       (KUPTR)kwSandbox_Kernel32_SetEnvironmentVariableA },
    { TUPLE("SetEnvironmentVariableW"),     NULL,       (KUPTR)kwSandbox_Kernel32_SetEnvironmentVariableW },
    { TUPLE("ExpandEnvironmentStringsA"),   NULL,       (KUPTR)kwSandbox_Kernel32_ExpandEnvironmentStringsA },
    { TUPLE("ExpandEnvironmentStringsW"),   NULL,       (KUPTR)kwSandbox_Kernel32_ExpandEnvironmentStringsW },

    { TUPLE("CreateFileA"),                 NULL,       (KUPTR)kwSandbox_Kernel32_CreateFileA },
    { TUPLE("CreateFileW"),                 NULL,       (KUPTR)kwSandbox_Kernel32_CreateFileW },
    { TUPLE("ReadFile"),                    NULL,       (KUPTR)kwSandbox_Kernel32_ReadFile },
    { TUPLE("ReadFileEx"),                  NULL,       (KUPTR)kwSandbox_Kernel32_ReadFileEx },
#ifdef WITH_TEMP_MEMORY_FILES
    { TUPLE("WriteFile"),                   NULL,       (KUPTR)kwSandbox_Kernel32_WriteFile },
    { TUPLE("WriteFileEx"),                 NULL,       (KUPTR)kwSandbox_Kernel32_WriteFileEx },
    { TUPLE("SetEndOfFile"),                NULL,       (KUPTR)kwSandbox_Kernel32_SetEndOfFile },
    { TUPLE("GetFileType"),                 NULL,       (KUPTR)kwSandbox_Kernel32_GetFileType },
    { TUPLE("GetFileSize"),                 NULL,       (KUPTR)kwSandbox_Kernel32_GetFileSize },
    { TUPLE("GetFileSizeEx"),               NULL,       (KUPTR)kwSandbox_Kernel32_GetFileSizeEx },
    { TUPLE("CreateFileMappingW"),          NULL,       (KUPTR)kwSandbox_Kernel32_CreateFileMappingW },
    { TUPLE("MapViewOfFile"),               NULL,       (KUPTR)kwSandbox_Kernel32_MapViewOfFile },
    { TUPLE("UnmapViewOfFile"),             NULL,       (KUPTR)kwSandbox_Kernel32_UnmapViewOfFile },
#endif
    { TUPLE("SetFilePointer"),              NULL,       (KUPTR)kwSandbox_Kernel32_SetFilePointer },
    { TUPLE("SetFilePointerEx"),            NULL,       (KUPTR)kwSandbox_Kernel32_SetFilePointerEx },
    { TUPLE("CloseHandle"),                 NULL,       (KUPTR)kwSandbox_Kernel32_CloseHandle },
    { TUPLE("GetFileAttributesA"),          NULL,       (KUPTR)kwSandbox_Kernel32_GetFileAttributesA },
    { TUPLE("GetFileAttributesW"),          NULL,       (KUPTR)kwSandbox_Kernel32_GetFileAttributesW },
    { TUPLE("GetShortPathNameW"),           NULL,       (KUPTR)kwSandbox_Kernel32_GetShortPathNameW },

    /*
     * MS Visual C++ CRTs.
     */
    { TUPLE("exit"),                        NULL,       (KUPTR)kwSandbox_msvcrt_exit },
    { TUPLE("_exit"),                       NULL,       (KUPTR)kwSandbox_msvcrt__exit },
    { TUPLE("_cexit"),                      NULL,       (KUPTR)kwSandbox_msvcrt__cexit },
    { TUPLE("_c_exit"),                     NULL,       (KUPTR)kwSandbox_msvcrt__c_exit },
    { TUPLE("_amsg_exit"),                  NULL,       (KUPTR)kwSandbox_msvcrt__amsg_exit },
    { TUPLE("terminate"),                   NULL,       (KUPTR)kwSandbox_msvcrt_terminate },

    { TUPLE("_beginthread"),                NULL,       (KUPTR)kwSandbox_msvcrt__beginthread },
    { TUPLE("_beginthreadex"),              NULL,       (KUPTR)kwSandbox_msvcrt__beginthreadex },

    { TUPLE("__argc"),                      NULL,       (KUPTR)&g_Sandbox.cArgs },
    { TUPLE("__argv"),                      NULL,       (KUPTR)&g_Sandbox.papszArgs },
    { TUPLE("__wargv"),                     NULL,       (KUPTR)&g_Sandbox.papwszArgs },
    { TUPLE("__p___argc"),                  NULL,       (KUPTR)kwSandbox_msvcrt___p___argc },
    { TUPLE("__p___argv"),                  NULL,       (KUPTR)kwSandbox_msvcrt___p___argv },
    { TUPLE("__p___wargv"),                 NULL,       (KUPTR)kwSandbox_msvcrt___p___wargv },
    { TUPLE("_acmdln"),                     NULL,       (KUPTR)&g_Sandbox.pszCmdLine },
    { TUPLE("_wcmdln"),                     NULL,       (KUPTR)&g_Sandbox.pwszCmdLine },
    { TUPLE("__p__acmdln"),                 NULL,       (KUPTR)kwSandbox_msvcrt___p__acmdln },
    { TUPLE("__p__wcmdln"),                 NULL,       (KUPTR)kwSandbox_msvcrt___p__wcmdln },
    { TUPLE("_pgmptr"),                     NULL,       (KUPTR)&g_Sandbox.pgmptr  },
    { TUPLE("_wpgmptr"),                    NULL,       (KUPTR)&g_Sandbox.wpgmptr },
    { TUPLE("_get_pgmptr"),                 NULL,       (KUPTR)kwSandbox_msvcrt__get_pgmptr },
    { TUPLE("_get_wpgmptr"),                NULL,       (KUPTR)kwSandbox_msvcrt__get_wpgmptr },
    { TUPLE("__p__pgmptr"),                 NULL,       (KUPTR)kwSandbox_msvcrt___p__pgmptr },
    { TUPLE("__p__wpgmptr"),                NULL,       (KUPTR)kwSandbox_msvcrt___p__wpgmptr },
    { TUPLE("_wincmdln"),                   NULL,       (KUPTR)kwSandbox_msvcrt__wincmdln },
    { TUPLE("_wwincmdln"),                  NULL,       (KUPTR)kwSandbox_msvcrt__wwincmdln },
    { TUPLE("__getmainargs"),               NULL,       (KUPTR)kwSandbox_msvcrt___getmainargs},
    { TUPLE("__wgetmainargs"),              NULL,       (KUPTR)kwSandbox_msvcrt___wgetmainargs},

    { TUPLE("_putenv"),                     NULL,       (KUPTR)kwSandbox_msvcrt__putenv},
    { TUPLE("_wputenv"),                    NULL,       (KUPTR)kwSandbox_msvcrt__wputenv},
    { TUPLE("_putenv_s"),                   NULL,       (KUPTR)kwSandbox_msvcrt__putenv_s},
    { TUPLE("_wputenv_s"),                  NULL,       (KUPTR)kwSandbox_msvcrt__wputenv_s},
    { TUPLE("__initenv"),                   NULL,       (KUPTR)&g_Sandbox.initenv },
    { TUPLE("__winitenv"),                  NULL,       (KUPTR)&g_Sandbox.winitenv },
    { TUPLE("__p___initenv"),               NULL,       (KUPTR)kwSandbox_msvcrt___p___initenv},
    { TUPLE("__p___winitenv"),              NULL,       (KUPTR)kwSandbox_msvcrt___p___winitenv},
    { TUPLE("_environ"),                    NULL,       (KUPTR)&g_Sandbox.environ },
    { TUPLE("_wenviron"),                   NULL,       (KUPTR)&g_Sandbox.wenviron },
    { TUPLE("_get_environ"),                NULL,       (KUPTR)kwSandbox_msvcrt__get_environ },
    { TUPLE("_get_wenviron"),               NULL,       (KUPTR)kwSandbox_msvcrt__get_wenviron },
    { TUPLE("__p__environ"),                NULL,       (KUPTR)kwSandbox_msvcrt___p__environ },
    { TUPLE("__p__wenviron"),               NULL,       (KUPTR)kwSandbox_msvcrt___p__wenviron },
};
/** Number of entries in g_aReplacements. */
KU32 const                  g_cSandboxReplacements = K_ELEMENTS(g_aSandboxReplacements);


/**
 * Functions that needs replacing in natively loaded DLLs when doing sandboxed
 * execution.
 */
KWREPLACEMENTFUNCTION const g_aSandboxNativeReplacements[] =
{
    /*
     * Kernel32.dll and friends.
     */
    { TUPLE("ExitProcess"),                 NULL,       (KUPTR)kwSandbox_Kernel32_ExitProcess },
    { TUPLE("TerminateProcess"),            NULL,       (KUPTR)kwSandbox_Kernel32_TerminateProcess },

#if 0
    { TUPLE("CreateThread"),                NULL,       (KUPTR)kwSandbox_Kernel32_CreateThread },
#endif

    { TUPLE("CreateFileA"),                 NULL,       (KUPTR)kwSandbox_Kernel32_CreateFileA },
    { TUPLE("CreateFileW"),                 NULL,       (KUPTR)kwSandbox_Kernel32_CreateFileW },
    { TUPLE("ReadFile"),                    NULL,       (KUPTR)kwSandbox_Kernel32_ReadFile },
    { TUPLE("ReadFileEx"),                  NULL,       (KUPTR)kwSandbox_Kernel32_ReadFileEx },
#ifdef WITH_TEMP_MEMORY_FILES
    { TUPLE("WriteFile"),                   NULL,       (KUPTR)kwSandbox_Kernel32_WriteFile },
    { TUPLE("WriteFileEx"),                 NULL,       (KUPTR)kwSandbox_Kernel32_WriteFileEx },
    { TUPLE("SetEndOfFile"),                NULL,       (KUPTR)kwSandbox_Kernel32_SetEndOfFile },
    { TUPLE("GetFileType"),                 NULL,       (KUPTR)kwSandbox_Kernel32_GetFileType },
    { TUPLE("GetFileSize"),                 NULL,       (KUPTR)kwSandbox_Kernel32_GetFileSize },
    { TUPLE("GetFileSizeEx"),               NULL,       (KUPTR)kwSandbox_Kernel32_GetFileSizeEx },
    { TUPLE("CreateFileMappingW"),          NULL,       (KUPTR)kwSandbox_Kernel32_CreateFileMappingW },
    { TUPLE("MapViewOfFile"),               NULL,       (KUPTR)kwSandbox_Kernel32_MapViewOfFile },
    { TUPLE("UnmapViewOfFile"),             NULL,       (KUPTR)kwSandbox_Kernel32_UnmapViewOfFile },
#endif
    { TUPLE("SetFilePointer"),              NULL,       (KUPTR)kwSandbox_Kernel32_SetFilePointer },
    { TUPLE("SetFilePointerEx"),            NULL,       (KUPTR)kwSandbox_Kernel32_SetFilePointerEx },
    { TUPLE("CloseHandle"),                 NULL,       (KUPTR)kwSandbox_Kernel32_CloseHandle },
    { TUPLE("GetFileAttributesA"),          NULL,       (KUPTR)kwSandbox_Kernel32_GetFileAttributesA },
    { TUPLE("GetFileAttributesW"),          NULL,       (KUPTR)kwSandbox_Kernel32_GetFileAttributesW },
    { TUPLE("GetShortPathNameW"),           NULL,       (KUPTR)kwSandbox_Kernel32_GetShortPathNameW },

    /*
     * MS Visual C++ CRTs.
     */
    { TUPLE("exit"),                        NULL,       (KUPTR)kwSandbox_msvcrt_exit },
    { TUPLE("_exit"),                       NULL,       (KUPTR)kwSandbox_msvcrt__exit },
    { TUPLE("_cexit"),                      NULL,       (KUPTR)kwSandbox_msvcrt__cexit },
    { TUPLE("_c_exit"),                     NULL,       (KUPTR)kwSandbox_msvcrt__c_exit },
    { TUPLE("_amsg_exit"),                  NULL,       (KUPTR)kwSandbox_msvcrt__amsg_exit },
    { TUPLE("terminate"),                   NULL,       (KUPTR)kwSandbox_msvcrt_terminate },

#if 0 /* used by mspdbXXX.dll */
    { TUPLE("_beginthread"),                NULL,       (KUPTR)kwSandbox_msvcrt__beginthread },
    { TUPLE("_beginthreadex"),              NULL,       (KUPTR)kwSandbox_msvcrt__beginthreadex },
#endif
};
/** Number of entries in g_aSandboxNativeReplacements. */
KU32 const                  g_cSandboxNativeReplacements = K_ELEMENTS(g_aSandboxNativeReplacements);


/**
 * Used by kwSandboxExec to reset the state of the module tree.
 *
 * This is done recursively.
 *
 * @param   pMod                The root of the tree to consider.
 */
static void kwSandboxResetModuleState(PKWMODULE pMod)
{
    if (   !pMod->fNative
        && pMod->u.Manual.enmState != KWMODSTATE_NEEDS_BITS)
    {
        KSIZE iImp;
        pMod->u.Manual.enmState = KWMODSTATE_NEEDS_BITS;
        iImp = pMod->u.Manual.cImpMods;
        while (iImp-- > 0)
            kwSandboxResetModuleState(pMod->u.Manual.apImpMods[iImp]);
    }
}

static PPEB kwSandboxGetProcessEnvironmentBlock(void)
{
#if K_ARCH == K_ARCH_X86_32
    return (PPEB)__readfsdword(0x030 /* offset of ProcessEnvironmentBlock in TEB */);
#elif K_ARCH == K_ARCH_AMD64
    return (PPEB)__readgsqword(0x060 /* offset of ProcessEnvironmentBlock in TEB */);
#else
# error "Port me!"
#endif
}


/**
 * Enters the given handle into the handle table.
 *
 * @returns K_TRUE on success, K_FALSE on failure.
 * @param   pSandbox            The sandbox.
 * @param   pHandle             The handle.
 */
static KBOOL kwSandboxHandleTableEnter(PKWSANDBOX pSandbox, PKWHANDLE pHandle)
{
    KUPTR const idxHandle = KW_HANDLE_TO_INDEX(pHandle->hHandle);
    kHlpAssertReturn(idxHandle < KW_HANDLE_MAX, K_FALSE);

    /*
     * Grow handle table.
     */
    if (idxHandle >= pSandbox->cHandles)
    {
        void *pvNew;
        KU32  cHandles = pSandbox->cHandles ? pSandbox->cHandles * 2 : 32;
        while (cHandles <= idxHandle)
            cHandles *= 2;
        pvNew = kHlpRealloc(pSandbox->papHandles, cHandles * sizeof(pSandbox->papHandles[0]));
        if (!pvNew)
        {
            KW_LOG(("Out of memory growing handle table to %u handles\n", cHandles));
            return K_FALSE;
        }
        pSandbox->papHandles = (PKWHANDLE *)pvNew;
        kHlpMemSet(&pSandbox->papHandles[pSandbox->cHandles], 0,
                   (cHandles - pSandbox->cHandles) * sizeof(pSandbox->papHandles[0]));
        pSandbox->cHandles = cHandles;
    }

    /*
     * Check that the entry is unused then insert it.
     */
    kHlpAssertReturn(pSandbox->papHandles[idxHandle] == NULL, K_FALSE);
    pSandbox->papHandles[idxHandle] = pHandle;
    pSandbox->cActiveHandles++;
    return K_TRUE;
}



static int kwSandboxInit(PKWSANDBOX pSandbox, PKWTOOL pTool,
                         KU32 cArgs, const char **papszArgs, KU32 cbArgs,
                         KU32 cEnvVars, const char **papszEnvVars)
{
    PPEB pPeb = kwSandboxGetProcessEnvironmentBlock();
    char *psz;
    wchar_t *pwcPool;
    KSIZE cbStrings;
    KSIZE cwc;
    KU32 i;

    /* Simple stuff. */
    g_Sandbox.rcExitCode    = 256;
    g_Sandbox.pTool         = pTool;
    g_Sandbox.idMainThread  = GetCurrentThreadId();
    g_Sandbox.TibMainThread = *(PNT_TIB)NtCurrentTeb();
    g_Sandbox.pgmptr        = (char *)pTool->pszPath;
    g_Sandbox.wpgmptr       = (wchar_t *)pTool->pwszPath;
    g_Sandbox.cArgs         = cArgs;
    g_Sandbox.papszArgs     = (char **)papszArgs;

    /*
     * Create a command line from the given argv.
     * ASSUME that it's correctly quoted by quote_argv.c already.
     */
    pSandbox->pszCmdLine = psz = (char *)kHlpAlloc(cbArgs + 1);
    if (!psz)
        return KERR_NO_MEMORY;
    psz = kHlpStrPCopy(psz, papszArgs[0]);
    for (i = 1; i < cArgs; i++)
    {
        *psz++ = ' ';
        psz = kHlpStrPCopy(psz, papszArgs[i]);
    }
    kHlpAssert((KSIZE)(&psz[1] - pSandbox->pszCmdLine) == cbArgs);
    psz[0] = '\0';
    psz[1] = '\0';

    /*
     * Convert command line and argv to UTF-16.
     * We assume each ANSI char requires a surrogate pair in the UTF-16 variant.
     */
    pSandbox->papwszArgs = (wchar_t **)kHlpAlloc(sizeof(wchar_t *) * (pSandbox->cArgs + 2) + cbArgs * 2 * sizeof(wchar_t));
    if (!pSandbox->papwszArgs)
        return KERR_NO_MEMORY;
    pwcPool = (wchar_t *)&pSandbox->papwszArgs[pSandbox->cArgs + 2];
    for (i = 0; i < cArgs; i++)
    {
        *pwcPool++ = pSandbox->papszArgs[i][-1]; /* flags */
        pSandbox->papwszArgs[i] = pwcPool;
        pwcPool += kwStrToUtf16(pSandbox->papszArgs[i], pwcPool, (strlen(pSandbox->papszArgs[i]) + 1) * 2);
        pwcPool++;
    }
    pSandbox->papwszArgs[pSandbox->cArgs + 0] = NULL;
    pSandbox->papwszArgs[pSandbox->cArgs + 1] = NULL;

    /*
     * Convert the commandline string to UTF-16, same pessimistic approach as above.
     */
    cbStrings = (cbArgs + 1) * 2 * sizeof(wchar_t);
    pSandbox->pwszCmdLine = kHlpAlloc(cbStrings);
    if (!pSandbox->pwszCmdLine)
        return KERR_NO_MEMORY;
    cwc = kwStrToUtf16(pSandbox->pszCmdLine, pSandbox->pwszCmdLine, cbStrings / sizeof(wchar_t));

    pSandbox->SavedCommandLine = pPeb->ProcessParameters->CommandLine;
    pPeb->ProcessParameters->CommandLine.Buffer = pSandbox->pwszCmdLine;
    pPeb->ProcessParameters->CommandLine.Length = (USHORT)cwc * sizeof(wchar_t);

    /*
     * Invalidate the missing cache entries.
     */
    kFsCacheInvalidateMissing(g_pFsCache);
    return 0;
}


static void kwSandboxCleanup(PKWSANDBOX pSandbox)
{
#ifdef WITH_TEMP_MEMORY_FILES
    PKWFSTEMPFILE pTempFile;
#endif
    PPEB pPeb = kwSandboxGetProcessEnvironmentBlock();
    pPeb->ProcessParameters->CommandLine = pSandbox->SavedCommandLine;
    /** @todo lots more to do here!   */

#ifdef WITH_TEMP_MEMORY_FILES
    pTempFile = pSandbox->pTempFileHead;
    pSandbox->pTempFileHead = NULL;
    while (pTempFile)
    {
        PKWFSTEMPFILE pNext = pTempFile->pNext;
        KU32          iSeg  = pTempFile->cSegs;
        while (iSeg-- > 0)
            kHlpPageFree(pTempFile->paSegs[iSeg].pbData, pTempFile->paSegs[iSeg].cbDataAlloc);
        kHlpFree(pTempFile->paSegs);
        pTempFile->pNext = NULL;
        kHlpFree(pTempFile);

        pTempFile = pNext;
    }
#endif

}


static int kwSandboxExec(PKWTOOL pTool, KU32 cArgs, const char **papszArgs, KU32 cbArgs,
                         KU32 cEnvVars, const char **papszEnvVars)
{
    int rcExit = 42;
    int rc;

    /*
     * Initialize the sandbox environment.
     */
    rc = kwSandboxInit(&g_Sandbox, pTool, cArgs, papszArgs, cbArgs, cEnvVars, papszEnvVars);
    if (rc == 0)
    {
        /*
         * Do module initialization.
         */
        kwSandboxResetModuleState(pTool->u.Sandboxed.pExe);
        rc = kwLdrModuleInitTree(pTool->u.Sandboxed.pExe);
        if (rc == 0)
        {
            /*
             * Call the main function.
             */
            KUPTR uAddrMain;
            rc = kwLdrModuleQueryMainEntrypoint(pTool->u.Sandboxed.pExe, &uAddrMain);
            if (rc == 0)
            {
                int (*pfnWin64Entrypoint)(void *pvPeb, void *, void *, void *);
                *(KUPTR *)&pfnWin64Entrypoint = uAddrMain;

                __try
                {
                    if (setjmp(g_Sandbox.JmpBuf) == 0)
                    {
#if K_ARCH == K_ARCH_AMD64
                        *(KU64*)(g_Sandbox.JmpBuf) = 0; /** @todo find other way to prevent longjmp from doing unwind! */
#else
# error "Port me!"
#endif
                        rcExit = pfnWin64Entrypoint(kwSandboxGetProcessEnvironmentBlock(), NULL, NULL, NULL);
                    }
                    else
                        rcExit = g_Sandbox.rcExitCode;
                }
                __except (EXCEPTION_EXECUTE_HANDLER)
                {
                    rcExit = 512;
                }

                /*
                 * Restore the TIB and later some other stuff.
                 */
                *(PNT_TIB)NtCurrentTeb() = g_Sandbox.TibMainThread;
            }
            else
                rcExit = 42 + 5;
        }
        else
            rcExit = 42 + 4;

        kwSandboxCleanup(&g_Sandbox);
    }
    else
        rcExit = 42 + 3;

    return rcExit;
}


#if 0
static int kwExecCmdLine(const char *pszExe, const char *pszCmdLine)
{
    int rc;
    PKWTOOL pTool = kwToolLookup(pszExe);
    if (pTool)
    {
        int rc;
        int rcExitCode;
        switch (pTool->enmType)
        {
            case KWTOOLTYPE_SANDBOXED:
                KW_LOG(("Sandboxing tool %s\n", pTool->pszPath));
                rc = kwSandboxExec(pTool, pszCmdLine, &rcExitCode);
                break;

            case KWTOOLTYPE_WATCOM:
                KW_LOG(("TODO: Watcom style tool %s\n", pTool->pszPath));
                rc = rcExitCode = 2;
                break;

            case KWTOOLTYPE_EXEC:
                KW_LOG(("TODO: Direct exec tool %s\n", pTool->pszPath));
                rc = rcExitCode = 2;
                break;

            default:
                kHlpAssertFailed();
                rc = rcExitCode = 2;
                break;
        }
        KW_LOG(("rcExitCode=%d (rc=%d)\n", rcExitCode, rc));
    }
    else
        rc = 1;
    return rc;
}
#endif


/**
 * Part 2 of the "JOB" command handler.
 *
 * @returns The exit code of the job.
 * @param   pszExecutable   The executable to execute.
 * @param   pszCwd          The current working directory of the job.
 * @param   cArgs           The number of arguments.
 * @param   papszArgs       The argument vector.
 * @param   cbArgs          The size of the argument strings and terminators.
 * @param   cEnvVars        The number of environment variables.
 * @param   papszEnvVars    The enviornment vector.
 */
static int kSubmitHandleJobUnpacked(const char *pszExecutable, const char *pszCwd,
                                    KU32 cArgs, const char **papszArgs, KU32 cbArgs,
                                    KU32 cEnvVars, const char **papszEnvVars)
{
    int rcExit;
    PKWTOOL pTool;

    /*
     * Lookup the tool.
     */
    pTool = kwToolLookup(pszExecutable);
    if (pTool)
    {
        /*
         * Change the directory if we're going to execute the job inside
         * this process.  Then invoke the tool type specific handler.
         */
        switch (pTool->enmType)
        {
            case KWTOOLTYPE_SANDBOXED:
            case KWTOOLTYPE_WATCOM:
            {
                /* Change dir. */
                KFSLOOKUPERROR  enmError;
                PKFSOBJ         pNewCurDir = kFsCacheLookupA(g_pFsCache, pszCwd, &enmError);
                if (   pNewCurDir           == g_pCurDirObj
                    && pNewCurDir->bObjType == KFSOBJ_TYPE_DIR)
                    kFsCacheObjRelease(g_pFsCache, pNewCurDir);
                else if (SetCurrentDirectoryA(pszCwd))
                {
                    kFsCacheObjRelease(g_pFsCache, g_pCurDirObj);
                    g_pCurDirObj = pNewCurDir;
                }
                else
                {
                    kwErrPrintf("SetCurrentDirectory failed with %u on '%s'\n", GetLastError(), pszCwd);
                    kFsCacheObjRelease(g_pFsCache, pNewCurDir);
                    rcExit = 42 + 1;
                    break;
                }

                /* Call specific handler. */
                if (pTool->enmType == KWTOOLTYPE_SANDBOXED)
                {
                    KW_LOG(("Sandboxing tool %s\n", pTool->pszPath));
                    rcExit = kwSandboxExec(pTool, cArgs, papszArgs, cbArgs, cEnvVars, papszEnvVars);
                }
                else
                {
                    kwErrPrintf("TODO: Watcom style tool %s\n", pTool->pszPath);
                    rcExit = 42 + 2;
                }
                break;
            }

            case KWTOOLTYPE_EXEC:
                kwErrPrintf("TODO: Direct exec tool %s\n", pTool->pszPath);
                rcExit = 42 + 2;
                break;

            default:
                kHlpAssertFailed();
                kwErrPrintf("Internal tool type corruption!!\n");
                rcExit = 42 + 2;
                g_fRestart = K_TRUE;
                break;
        }
    }
    else
        rcExit = 42 + 1;
    return rcExit;
}


/**
 * Handles a "JOB" command.
 *
 * @returns The exit code of the job.
 * @param   pszMsg              Points to the "JOB" command part of the message.
 * @param   cbMsg               Number of message bytes at @a pszMsg.  There are
 *                              4 more zero bytes after the message body to
 *                              simplify parsing.
 */
static int kSubmitHandleJob(const char *pszMsg, KSIZE cbMsg)
{
    int rcExit = 42;

    /*
     * Unpack the message.
     */
    const char     *pszExecutable;
    size_t          cbTmp;

    pszMsg += sizeof("JOB");
    cbMsg  -= sizeof("JOB");

    /* Executable name. */
    pszExecutable = pszMsg;
    cbTmp = strlen(pszMsg) + 1;
    pszMsg += cbTmp;
    if (   cbTmp < cbMsg
        && cbTmp > 2)
    {
        const char *pszCwd;
        cbMsg -= cbTmp;

        /* Current working directory. */
        pszCwd = pszMsg;
        cbTmp = strlen(pszMsg) + 1;
        pszMsg += cbTmp;
        if (   cbTmp + sizeof(KU32) < cbMsg
            && cbTmp >= 2)
        {
            KU32    cArgs;
            cbMsg  -= cbTmp;

            /* Argument count. */
            kHlpMemCopy(&cArgs, pszMsg, sizeof(cArgs));
            pszMsg += sizeof(cArgs);
            cbMsg  -= sizeof(cArgs);

            if (cArgs > 0 && cArgs < 4096)
            {
                /* The argument vector. */
                char const **papszArgs = kHlpAlloc((cArgs + 1) * sizeof(papszArgs[0]));
                if (papszArgs)
                {
                    KU32 cbArgs;
                    KU32 i;
                    for (i = 0; i < cArgs; i++)
                    {
                        papszArgs[i] = pszMsg + 1; /* First byte is expansion flags for MSC & EMX. */
                        cbTmp = 1 + strlen(pszMsg + 1) + 1;
                        pszMsg += cbTmp;
                        if (cbTmp < cbMsg)
                            cbMsg -= cbTmp;
                        else
                        {
                            cbMsg = 0;
                            break;
                        }

                    }
                    papszArgs[cArgs] = 0;
                    cbArgs = (KU32)(pszMsg - papszArgs[0]) - cArgs + 1;

                    /* Environment variable count. */
                    if (sizeof(KU32) < cbMsg)
                    {
                        KU32    cEnvVars;
                        kHlpMemCopy(&cEnvVars, pszMsg, sizeof(cEnvVars));
                        pszMsg += sizeof(cEnvVars);
                        cbMsg  -= sizeof(cEnvVars);

                        if (cEnvVars >= 0 && cEnvVars < 4096)
                        {
                            /* The argument vector. */
                            char const **papszEnvVars = kHlpAlloc((cEnvVars + 1) * sizeof(papszEnvVars[0]));
                            if (papszEnvVars)
                            {
                                KU32 i;
                                for (i = 0; i < cEnvVars; i++)
                                {
                                    papszEnvVars[i] = pszMsg;
                                    cbTmp = strlen(pszMsg) + 1;
                                    pszMsg += cbTmp;
                                    if (cbTmp < cbMsg)
                                        cbMsg -= cbTmp;
                                    else
                                    {
                                        if (   cbTmp == cbMsg
                                            && i + 1 == cEnvVars)
                                            cbMsg = 0;
                                        else
                                            cbMsg = KSIZE_MAX;
                                        break;
                                    }
                                }
                                papszEnvVars[cEnvVars] = 0;
                                if (cbMsg != KSIZE_MAX)
                                {
                                    if (cbMsg == 0)
                                    {
                                        /*
                                         * The next step.
                                         */
                                        rcExit = kSubmitHandleJobUnpacked(pszExecutable, pszCwd,
                                                                          cArgs, papszArgs, cbArgs,
                                                                          cEnvVars, papszEnvVars);
                                    }
                                    else
                                        kwErrPrintf("Message has %u bytes unknown trailing bytes\n", cbMsg);
                                }
                                else
                                    kwErrPrintf("Detected bogus message unpacking environment variables!\n");
                                kHlpFree((void *)papszEnvVars);
                            }
                            else
                                kwErrPrintf("Error allocating papszEnvVars for %u variables\n", cEnvVars);
                        }
                        else
                            kwErrPrintf("Bogus environment variable count: %u (%#x)\n", cEnvVars, cEnvVars);
                    }
                    else
                        kwErrPrintf("Detected bogus message unpacking arguments and environment variable count!\n");
                    kHlpFree((void *)papszArgs);
                }
                else
                    kwErrPrintf("Error allocating argv for %u arguments\n", cArgs);
            }
            else
                kwErrPrintf("Bogus argument count: %u (%#x)\n", cArgs, cArgs);
        }
        else
            kwErrPrintf("Detected bogus message unpacking CWD path and argument count!\n");
    }
    else
        kwErrPrintf("Detected bogus message unpacking executable path!\n");
    return rcExit;
}


/**
 * Wrapper around WriteFile / write that writes the whole @a cbToWrite.
 *
 * @retval  0 on success.
 * @retval  -1 on error (fully bitched).
 *
 * @param   hPipe               The pipe handle.
 * @param   pvBuf               The buffer to write out out.
 * @param   cbToWrite           The number of bytes to write.
 */
static int kSubmitWriteIt(HANDLE hPipe, const void *pvBuf, KU32 cbToWrite)
{
    KU8 const  *pbBuf  = (KU8 const *)pvBuf;
    KU32        cbLeft = cbToWrite;
    for (;;)
    {
        DWORD cbActuallyWritten = 0;
        if (WriteFile(hPipe, pbBuf, cbLeft, &cbActuallyWritten, NULL /*pOverlapped*/))
        {
            cbLeft -= cbActuallyWritten;
            if (!cbLeft)
                return 0;
            pbBuf  += cbActuallyWritten;
        }
        else
        {
            DWORD dwErr = GetLastError();
            if (cbLeft == cbToWrite)
                kwErrPrintf("WriteFile failed: %u\n", dwErr);
            else
                kwErrPrintf("WriteFile failed %u byte(s) in: %u\n", cbToWrite - cbLeft, dwErr);
            return -1;
        }
    }
}


/**
 * Wrapper around ReadFile / read that reads the whole @a cbToRead.
 *
 * @retval  0 on success.
 * @retval  1 on shut down (fShutdownOkay must be K_TRUE).
 * @retval  -1 on error (fully bitched).
 * @param   hPipe               The pipe handle.
 * @param   pvBuf               The buffer to read into.
 * @param   cbToRead            The number of bytes to read.
 * @param   fShutdownOkay       Whether connection shutdown while reading the
 *                              first byte is okay or not.
 */
static int kSubmitReadIt(HANDLE hPipe, void *pvBuf, KU32 cbToRead, KBOOL fMayShutdown)
{
    KU8 *pbBuf  = (KU8 *)pvBuf;
    KU32 cbLeft = cbToRead;
    for (;;)
    {
        DWORD cbActuallyRead = 0;
        if (ReadFile(hPipe, pbBuf, cbLeft, &cbActuallyRead, NULL /*pOverlapped*/))
        {
            cbLeft -= cbActuallyRead;
            if (!cbLeft)
                return 0;
            pbBuf  += cbActuallyRead;
        }
        else
        {
            DWORD dwErr = GetLastError();
            if (cbLeft == cbToRead)
            {
                if (   fMayShutdown
                    && dwErr == ERROR_BROKEN_PIPE)
                    return 1;
                kwErrPrintf("ReadFile failed: %u\n", dwErr);
            }
            else
                kwErrPrintf("ReadFile failed %u byte(s) in: %u\n", cbToRead - cbLeft, dwErr);
            return -1;
        }
    }
}


/**
 * Handles what comes after --test.
 *
 * @returns Exit code.
 * @param   argc                Number of arguments after --test.
 * @param   argv                Arguments after --test.
 */
static int kwTestRun(int argc, char **argv)
{
    int         i;
    int         j;
    int         rcExit;
    int         cRepeats;
    char        szCwd[MAX_PATH];
    const char *pszCwd = getcwd(szCwd, sizeof(szCwd));
    KU32        cbArgs;
    KU32        cEnvVars;

    /*
     * Parse arguments.
     */
    /* Repeat count. */
    i = 0;
    if (i >= argc)
        return kwErrPrintfRc(2, "--test takes an repeat count argument or '--'!\n");
    if (strcmp(argv[i], "--") != 0)
    {
        cRepeats = atoi(argv[i]);
        if (cRepeats <= 0)
            return kwErrPrintfRc(2, "The repeat count '%s' is zero, negative or invalid!\n", argv[i]);
        i++;

        /* Optional directory change. */
        if (   i < argc
            && strcmp(argv[i], "--chdir") == 0)
        {
            i++;
            if (i >= argc)
                return kwErrPrintfRc(2, "--chdir takes an argument!\n");
            pszCwd = argv[i++];
        }

        /* Check for '--'. */
        if (i >= argc)
            return kwErrPrintfRc(2, "Missing '--'\n");
        if (strcmp(argv[i], "--") != 0)
            return kwErrPrintfRc(2, "Expected '--' found '%s'\n", argv[i]);
        i++;
    }
    else
    {
        cRepeats = 1;
        i++;
    }
    if (i >= argc)
        return kwErrPrintfRc(2, "Nothing to execute after '--'!\n");

    /*
     * Do the job.
     */
    cbArgs = 0;
    for (j = i; j < argc; j++)
        cbArgs += (KU32)strlen(argv[j]) + 1;

    cEnvVars = 0;
    while (environ[cEnvVars] != NULL)
        cEnvVars++;

    for (j = 0; j < cRepeats; j++)
    {
        rcExit = kSubmitHandleJobUnpacked(argv[i], pszCwd, argc - i, &argv[i], cbArgs, cEnvVars, environ);
    }

    return rcExit;
}



int main(int argc, char **argv)
{
#if 1
    KSIZE   cbMsgBuf = 0;
    KU8    *pbMsgBuf = NULL;
    int     i;
    HANDLE  hPipe = INVALID_HANDLE_VALUE;

    /*
     * Create the cache.
     */
    g_pFsCache = kFsCacheCreate(KFSCACHE_F_MISSING_OBJECTS | KFSCACHE_F_MISSING_PATHS);
    if (!g_pFsCache)
        return kwErrPrintfRc(3, "kFsCacheCreate failed!\n");

    /*
     * Parse arguments.
     */
    for (i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "--pipe") == 0)
        {
            i++;
            if (i < argc)
            {
                char *pszEnd = NULL;
                unsigned __int64 u64Value = _strtoui64(argv[i], &pszEnd, 16);
                if (   *argv[i]
                    && pszEnd != NULL
                    && *pszEnd == '\0'
                    && u64Value != 0
                    && u64Value != (uintptr_t)INVALID_HANDLE_VALUE
                    && (uintptr_t)u64Value == u64Value)
                    hPipe = (HANDLE)(uintptr_t)u64Value;
                else
                    return kwErrPrintfRc(2, "Invalid --pipe argument: %s\n", argv[i]);
            }
            else
                return kwErrPrintfRc(2, "--pipe takes an argument!\n");
        }
        else if (strcmp(argv[i], "--test") == 0)
            return kwTestRun(argc - i - 1, &argv[i + 1]);
        else if (   strcmp(argv[i], "--help") == 0
                 || strcmp(argv[i], "-h") == 0
                 || strcmp(argv[i], "-?") == 0)
        {
            printf("usage: kWorker --pipe <pipe-handle>\n"
                   "usage: kWorker <--help|-h>\n"
                   "usage: kWorker <--version|-V>\n"
                   "usage: kWorker --test [<times> [--chdir <dir>]] -- args\n"
                   "\n"
                   "This is an internal kmk program that is used via the builtin_kSubmit.\n");
            return 0;
        }
        else if (   strcmp(argv[i], "--version") == 0
                 || strcmp(argv[i], "-V") == 0)
            return kbuild_version(argv[0]);
        else
            return kwErrPrintfRc(2, "Unknown argument '%s'\n", argv[i]);
    }

    if (hPipe == INVALID_HANDLE_VALUE)
        return kwErrPrintfRc(2, "Missing --pipe <pipe-handle> argument!\n");

    /*
     * Serve the pipe.
     */
    for (;;)
    {
        KU32 cbMsg = 0;
        int rc = kSubmitReadIt(hPipe, &cbMsg, sizeof(cbMsg), K_TRUE /*fShutdownOkay*/);
        if (rc == 0)
        {
            /* Make sure the message length is within sane bounds.  */
            if (   cbMsg > 4
                && cbMsg <= 256*1024*1024)
            {
                /* Reallocate the message buffer if necessary.  We add 4 zero bytes.  */
                if (cbMsg + 4 <= cbMsgBuf)
                { /* likely */ }
                else
                {
                    cbMsgBuf = K_ALIGN_Z(cbMsg + 4, 2048);
                    pbMsgBuf = kHlpRealloc(pbMsgBuf, cbMsgBuf);
                    if (!pbMsgBuf)
                        return kwErrPrintfRc(1, "Failed to allocate %u bytes for a message buffer!\n", cbMsgBuf);
                }

                /* Read the whole message into the buffer, making sure there is are a 4 zero bytes following it. */
                *(KU32 *)pbMsgBuf = cbMsg;
                rc = kSubmitReadIt(hPipe, &pbMsgBuf[sizeof(cbMsg)], cbMsg - sizeof(cbMsg), K_FALSE /*fShutdownOkay*/);
                if (rc == 0)
                {
                    const char *psz;

                    pbMsgBuf[cbMsg]     = '\0';
                    pbMsgBuf[cbMsg + 1] = '\0';
                    pbMsgBuf[cbMsg + 2] = '\0';
                    pbMsgBuf[cbMsg + 3] = '\0';

                    /* The first string after the header is the command. */
                    psz = (const char *)&pbMsgBuf[sizeof(cbMsg)];
                    if (strcmp(psz, "JOB") == 0)
                    {
                        struct
                        {
                            KI32 rcExitCode;
                            KU8  bExiting;
                            KU8  abZero[3];
                        } Reply;
                        Reply.rcExitCode = kSubmitHandleJob(psz, cbMsg - sizeof(cbMsg));
                        Reply.bExiting   = g_fRestart;
                        Reply.abZero[0]  = 0;
                        Reply.abZero[1]  = 0;
                        Reply.abZero[2]  = 0;
                        rc = kSubmitWriteIt(hPipe, &Reply, sizeof(Reply));
                        if (   rc == 0
                            && !g_fRestart)
                            continue;
                    }
                    else
                        rc = kwErrPrintfRc(-1, "Unknown command: '%s'\n", psz);
                }
            }
            else
                rc = kwErrPrintfRc(-1, "Bogus message length: %u (%#x)\n", cbMsg, cbMsg);
        }
        return rc > 0 ? 0 : 1;
    }

#else
    int rc = 0;
    int i;
    argv[2] = "\"E:/vbox/svn/trunk/tools/win.x86/vcc/v10sp1/bin/amd64/cl.exe\" -c -c -TP -nologo -Zi -Zi -Zl -GR- -EHsc -GF -Zc:wchar_t- -Oy- -MT -W4 -Wall -wd4065 -wd4996 -wd4127 -wd4706 -wd4201 -wd4214 -wd4510 -wd4512 -wd4610 -wd4514 -wd4820 -wd4365 -wd4987 -wd4710 -wd4061 -wd4986 -wd4191 -wd4574 -wd4917 -wd4711 -wd4611 -wd4571 -wd4324 -wd4505 -wd4263 -wd4264 -wd4738 -wd4242 -wd4244 -WX -RTCsu -IE:/vbox/svn/trunk/tools/win.x86/vcc/v10sp1/include -IE:/vbox/svn/trunk/tools/win.x86/vcc/v10sp1/atlmfc/include -IE:/vbox/svn/trunk/tools/win.x86/sdk/v7.1/Include -IE:/vbox/svn/trunk/include -IE:/vbox/svn/trunk/out/win.amd64/debug -IE:/vbox/svn/trunk/tools/win.x86/vcc/v10sp1/include -IE:/vbox/svn/trunk/tools/win.x86/vcc/v10sp1/atlmfc/include -DVBOX -DVBOX_WITH_64_BITS_GUESTS -DVBOX_WITH_REM -DVBOX_WITH_RAW_MODE -DDEBUG -DDEBUG_bird -DDEBUG_USERNAME=bird -DRT_OS_WINDOWS -D__WIN__ -DRT_ARCH_AMD64 -D__AMD64__ -D__WIN64__ -DVBOX_WITH_DEBUGGER -DRT_LOCK_STRICT -DRT_LOCK_STRICT_ORDER -DIN_RING3 -DLOG_DISABLED -DIN_BLD_PROG -D_CRT_SECURE_NO_DEPRECATE -FdE:/vbox/svn/trunk/out/win.amd64/debug/obj/VBoxBs2Linker/VBoxBs2Linker-obj.pdb -FD -FoE:/vbox/svn/trunk/out/win.amd64/debug/obj/VBoxBs2Linker/VBoxBs2Linker.obj E:\\vbox\\svn\\trunk\\src\\VBox\\ValidationKit\\bootsectors\\VBoxBs2Linker.cpp";
# if 0
    rc = kwExecCmdLine(argv[1], argv[2]);
    rc = kwExecCmdLine(argv[1], argv[2]);
    K_NOREF(i);
# else
// Skylake (W10/amd64, only stdandard MS defender):
//     cmd 1:  48    /1024 = 0x0 (0.046875)        [for /l %i in (1,1,1024) do ...]
//     kmk 1:  44    /1024 = 0x0 (0.04296875)      [all: ; 1024 x cl.exe]
//     run 1:  37    /1024 = 0x0 (0.0361328125)    [just process creation gain]
//     run 2:  34    /1024 = 0x0 (0.033203125)     [get file attribs]
//     run 3:  32.77 /1024 = 0x0 (0.032001953125)  [read caching of headers]
//     run 4:  32.67 /1024 = 0x0 (0.031904296875)  [loader tweaking]
//     run 5:  29.144/1024 = 0x0 (0.0284609375)    [with temp files in memory]
// Dell (W7/amd64, infected by mcafee):
//     kmk 1: 285.278/1024 = 0x0 (0.278591796875)
//     run 1: 134.503/1024 = 0x0 (0.1313505859375) [w/o temp files in memory]
//     run 2:  78.161/1024 = 0x0 (0.0763291015625) [with temp files in memory]
    g_cVerbose = 0;
    for (i = 0; i < 1024 && rc == 0; i++)
        rc = kwExecCmdLine(argv[1], argv[2]);
# endif
    return rc;

#endif
}

