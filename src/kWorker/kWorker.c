/* $Id: kWorker.c 2834 2016-08-23 00:53:11Z knut.osmundsen@oracle.com $ */
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

#include <nt/ntstat.h>
/* lib/nt_fullpath.c */
extern void nt_fullpath(const char *pszPath, char *pszFull, size_t cchFull);
#include <Windows.h>
#include <winternl.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** Special KWFSOBJ::uCacheGen number indicating that it does not apply. */
#define KFSWOBJ_CACHE_GEN_IGNORE        KU32_MAX

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


typedef struct KWFSOBJ *PKWFSOBJ;
typedef struct KWFSOBJ
{
    /** The object name.  (Allocated after the structure.) */
    const char         *pszName;
    /** The UTF-16 object name.  (Allocated after the structure.) */
    const wchar_t      *pwszName;
    /** The length of pszName. */
    KU16                cchName;
    /** The length of UTF-16 (in wchar_t's). */
    KU16                cwcName;

    /** The number of child objects. */
    KU32                cChildren;
    /** Child objects. */
    PKWFSOBJ           *papChildren;
    /** Pointer to the parent. */
    PKWFSOBJ            pParent;

    /** The cache generation, KFSWOBJ_CACHE_GEN_IGNORE. */
    KU32                uCacheGen;
    /** The GetFileAttributes result for the file.
     * FILE_ATTRIBUTE_XXX or INVALID_FILE_ATTRIBUTES. */
    KU32                fAttribs;
    /** The GetLastError() for INVALI_FILE_ATTRIBUTES. */
    KU32                uLastError;

    /** Cached file handle. */
    HANDLE              hCached;
    /** The file size. */
    KSIZE               cbCached;
    /** Cached file content. */
    KU8                *pbCached;
} KWFSOBJ;


/** Pointer to an ANSI path hash table entry. */
typedef struct KWFSHASHA *PKWFSHASHA;
/**
 * ANSI file system path hash table entry.
 * The path hash table allows us to skip parsing and walking a path.
 */
typedef struct KWFSHASHA
{
    /** Next entry with the same hash. */
    PKWFSHASHA          pNext;
    /** Path hash value. */
    KU32                uHashPath;
    /** The path length. */
    KU32                cchPath;
    /** The path.  (Allocated after the structure.) */
    const char         *pszPath;
    /** Pointer to the matching FS object. */
    PKWFSOBJ            pFsObj;
} KWFSHASHA;


/** Pointer to an UTF-16 path hash table entry. */
typedef struct KWFSHASHW *PKWFSHASHW;
/**
 * UTF-16 file system path hash table entry. The path hash table allows us
 * to skip parsing and walking a path.
 */
typedef struct KWFSHASHW
{
    /** Next entry with the same hash. */
    PKWFSHASHW          pNext;
    /** Path hash value. */
    KU32                uHashPath;
    /** The path length (in wchar_t units). */
    KU32                cwcPath;
    /** The path.  (Allocated after the structure.) */
    const wchar_t      *pwszPath;
    /** Pointer to the matching FS object. */
    PKWFSOBJ            pFsObj;
} KWFSHASHW;


/** Handle type.   */
typedef enum KWHANDLETYPE
{
    KWHANDLETYPE_INVALID = 0,
    KWHANDLETYPE_FSOBJ_READ_CACHE
    //KWHANDLETYPE_TEMP_FILE_CACHE,
    //KWHANDLETYPE_CONSOLE_CACHE
} KWHANDLETYPE;

/** Handle data. */
typedef struct KWHANDLE
{
    KWHANDLETYPE        enmType;
    /** The current file offset. */
    KU32                offFile;
    /** The handle. */
    HANDLE              hHandle;

    /** Type specific data. */
    union
    {
        /** The file system object.   */
        PKWFSOBJ            pFsObj;
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

typedef struct KWTOOL *PKWTOOL;
typedef struct KWTOOL
{
    /** Pointer to the next in the hash collision chain. */
    PKWTOOL             pNext;
    /** The normalized path to the program. */
    const char         *pszPath;
    /** The hash of the program path. */
    KU32                uHashPath;
    /** The kind of tool. */
    KWTOOLTYPE          enmType;
    /** UTF-16 version of pszPath. */
    wchar_t const      *pwszPath;

    union
    {
        struct
        {
            /** The executable. */
            PKWMODULE   pExe;
            /** List of dynamically loaded modules.
             * These will be kept loaded till the tool is destroyed (if we ever do that). */
            PKWDYNLOAD  pDynLoadHead;
        } Sandboxed;
    } u;
} KWTOOL;


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
    const char *pszCmdLine;
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

/** Tool hash table. */
static PKWTOOL      g_apTools[63];

/** Special file system root (parent to the drive letters). */
static KWFSOBJ      g_FsRoot =
{
    /* .pszName     = */ "",
    /* .pwszName    = */ L"",
    /* .cchName     = */ 0,
    /* .cwcName     = */ 0,
    /* .cChildren   = */ 0,
    /* .papChildren = */ NULL,
    /* .pParent     = */ NULL,
    /* .uCacheGen   = */ KFSWOBJ_CACHE_GEN_IGNORE,
    /* .fAttribs    = */ FILE_ATTRIBUTE_DIRECTORY,
    /* .uLastError  = */ ERROR_PATH_NOT_FOUND,
    /* .hCached     = */ INVALID_HANDLE_VALUE,
    /* .cbCached    = */ 0,
    /* .pbCached    = */ NULL,
};
/** File system hash table for ANSI filename strings. */
static PKWFSHASHA   g_apFsAnsiPaths[1021];
/** File system hash table for UTF-16 filename strings. */
static PKWFSHASHW   g_apFsUtf16Paths[1021];
/** Special file system object returned if the path is invalid. */
static KWFSOBJ      g_FsPathNotFound =
{
    /* .pszName     = */ "",
    /* .pwszName    = */ L"",
    /* .cchName     = */ 0,
    /* .cwcName     = */ 0,
    /* .cChildren   = */ 0,
    /* .papChildren = */ NULL,
    /* .pParent     = */ NULL,
    /* .uCacheGen   = */ KFSWOBJ_CACHE_GEN_IGNORE,
    /* .fAttribs    = */ FILE_ATTRIBUTE_DIRECTORY,
    /* .uLastError  = */ ERROR_PATH_NOT_FOUND,
    /* .hCached     = */ INVALID_HANDLE_VALUE,
    /* .cbCached    = */ 0,
    /* .pbCached    = */ NULL,
};
/** The cache generation number, incremented for each sandboxed execution.
 * This is used to invalid negative results from parts of the file system. */
static KU32         g_uFsCacheGeneration = 0;

/** Verbosity level. */
static int          g_cVerbose = 2;

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
static PKWFSOBJ kwFsLookupA(const char *pszPath);
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

    fprintf(stderr, "error: ");
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
 * Normalizes the path so we get a consistent hash.
 *
 * @returns status code.
 * @param   pszPath             The path.
 * @param   pszNormPath         The output buffer.
 * @param   cbNormPath          The size of the output buffer.
 */
static int kwPathNormalize(const char *pszPath, char *pszNormPath, KSIZE cbNormPath)
{
    char *pchSlash;
    nt_fullpath(pszPath, pszNormPath, cbNormPath);

    pchSlash = kHlpStrChr(pszNormPath, '/');
    while (pchSlash)
    {
        *pchSlash = '\\';
        pchSlash = kHlpStrChr(pchSlash + 1, '/');
    }

    return 0;
}


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
    BirdStat_T  Stat;
    int rc = birdStatFollowLink(pszPath, &Stat);
    if (rc == 0)
    {
        if (S_ISREG(Stat.st_mode))
        {
            /*
             * Yes! Normalize it and look it up in the hash table.
             */
            char szNormPath[1024];
            rc = kwPathNormalize(pszPath, szNormPath, sizeof(szNormPath));
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
 * @param   pszTool             The normalized path to the tool.
 * @param   uHashPath           The hash of the tool path.
 * @param   idxHashTab          The hash table index of the tool.
 */
static PKWTOOL kwToolEntryCreate(const char *pszTool, KU32 uHashPath, unsigned idxHashTab)
{
    KSIZE   cbTool = kHlpStrLen(pszTool) + 1;
    PKWTOOL pTool  = (PKWTOOL)kHlpAllocZ(sizeof(*pTool) + cbTool + 1 + cbTool * 2 * sizeof(wchar_t));
    if (pTool)
    {
        pTool->pszPath   = (char *)kHlpMemCopy(pTool + 1, pszTool, cbTool);
        pTool->pwszPath  = (wchar_t *)(pTool->pszPath + cbTool + (cbTool & 1));
        kwStrToUtf16(pTool->pszPath, (wchar_t *)pTool->pwszPath, cbTool * 2);
        pTool->uHashPath = uHashPath;
        pTool->enmType   = KWTOOLTYPE_SANDBOXED;

        pTool->u.Sandboxed.pExe = kwLdrModuleCreateNonNative(pszTool, uHashPath, K_TRUE /*fExe*/, NULL);
        if (!pTool->u.Sandboxed.pExe)
            pTool->enmType = KWTOOLTYPE_EXEC;

        /* Link the tool. */
        pTool->pNext = g_apTools[idxHashTab];
        g_apTools[idxHashTab] = pTool;
        return pTool;
    }
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
     * Normalize the path and look up the tool in the g_apTools hash table.
     */
    char szNormPath[4096];
    int rc = kwPathNormalize(pszExe, szNormPath, sizeof(szNormPath));
    if (rc == 0)
    {
        KU32     uHashPath = kwStrHash(szNormPath);
        unsigned idxHash   = uHashPath % K_ELEMENTS(g_apTools);
        PKWTOOL  pTool     = g_apTools[idxHash];
        if (pTool)
        {
            do
            {
                if (   pTool->uHashPath == uHashPath
                    && kHlpStrComp(pTool->pszPath, szNormPath) == 0)
                    return pTool;
                pTool = pTool->pNext;
            } while (pTool);
        }

        /*
         * Not found, create new entry.
         */
       return kwToolEntryCreate(szNormPath, uHashPath, idxHash);
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


#define IS_ALPHA(ch) ( ((ch) >= 'A' && (ch) <= 'Z') || ((ch) >= 'a' && (ch) <= 'z') )
#define IS_SLASH(ch) ((ch) == '\\' || (ch) == '/')


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
 * Looks for '..' in the path.
 *
 * @returns K_TRUE if '..' component found, K_FALSE if not.
 * @param   pszPath             The path.
 * @param   cchPath             The length of the path.
 */
static KBOOL kwFsHasDotDot(const char *pszPath, KSIZE cchPath)
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


static void kwFsCreateHashTabEntryA(PKWFSOBJ pFsObj, const char *pszPath, KU32 cchPath, KU32 uHashPath, KU32 idxHashTab)
{
    PKWFSHASHA pHashEntry = (PKWFSHASHA)kHlpAlloc(sizeof(*pHashEntry) + cchPath + 1);
    if (pHashEntry)
    {
        pHashEntry->uHashPath   = uHashPath;
        pHashEntry->cchPath     = cchPath;
        pHashEntry->pszPath     = (const char *)kHlpMemCopy(pHashEntry + 1, pszPath, cchPath + 1);
        pHashEntry->pFsObj      = pFsObj;

        pHashEntry->pNext = g_apFsAnsiPaths[idxHashTab];
        g_apFsAnsiPaths[idxHashTab] = pHashEntry;
    }
}


/**
 * Refreshes a node that hash expired.
 *
 * This is for files and directories in the output directory tree.  The plan is
 * to invalid negative results for each tool execution, in case a include file
 * or directory has been created since the last time we were active.  Assuming
 * that we'll be stopped together with kmk, there is no need to invalidate
 * positive results.
 *
 * @param   pNode               The FS node.
 */
static void kwFsRefreshNode(PKWFSOBJ pNode)
{
    /** @todo implement once we've start inserting uCacheGen nodes. */
    __debugbreak();
}


/**
 * Links the child in under the parent.
 *
 * @returns K_TRUE on success, K_FALSE if out of memory.
 * @param   pParent             The parent node.
 * @param   pChild              The child node.
 */
static KBOOL kwFsLinkChild(PKWFSOBJ pParent, PKWFSOBJ pChild)
{
    if ((pParent->cChildren % 16) == 0)
    {
        void *pvNew = kHlpRealloc(pParent->papChildren, (pParent->cChildren + 16) * sizeof(pParent->papChildren[0]));
        if (!pvNew)
            return K_FALSE;
        pParent->papChildren = (PKWFSOBJ *)pvNew;
    }
    pParent->papChildren[pParent->cChildren++] = pChild;
    return K_TRUE;
}


/**
 * Creates a child node for an ANSI path.
 *
 * @returns Pointer to the child tree node on success.
 *          NULL on failure (out of memory).
 * @param   pParent             The parent node.
 * @param   pchPath             The path.
 * @param   offName             The offset of the child name into pchPath.
 * @param   cchName             The length of the child name.
 */
static PKWFSOBJ kwFsCreateChildA(PKWFSOBJ pParent, const char *pchPath, KU32 offName, KU32 cchName)
{
    char        szTmp[2048];
    DWORD const dwSavedErr = GetLastError();
    DWORD       dwAttr;
    DWORD       dwErr;
    PKWFSOBJ    pChild;

    /*
     * Get attributes.
     */
    if (pchPath[offName + cchName])
    {
        if (cchName + offName >= sizeof(szTmp))
            return NULL;
        memcpy(szTmp, pchPath, offName + cchName);
        if (offName != 0 || cchName != 2 || pchPath[1] != ':')
            szTmp[offName + cchName] = '\0';
        else
        {
            /* Change 'E:' to 'E:\\.' so that it's actually absolute. */
            szTmp[2] = '\\';
            szTmp[3] = '.';
            szTmp[4] = '\0';
        }
        pchPath = szTmp;
    }

    SetLastError(NO_ERROR);
    dwAttr = GetFileAttributesA(pchPath);
    dwErr  = GetLastError();

    /*
     * Create the entry.
     */
    pChild = (PKWFSOBJ)kHlpAlloc(sizeof(*pChild) + cchName + 1 + (cchName + 1) * sizeof(wchar_t) * 2);
    SetLastError(dwSavedErr);
    if (pChild)
    {
        pChild->pwszName    = (const wchar_t *)(pChild + 1);
        pChild->pszName     = (const char *)kHlpMemCopy((void *)&pChild->pwszName[(cchName + 1) * 2],
                                                        &pchPath[offName], cchName);
        ((char *)pChild->pszName)[cchName] = '\0';
        pChild->cwcName     = (KU16)kwStrToUtf16(pChild->pszName, (wchar_t *)pChild->pwszName, (cchName + 1) * 2);

        pChild->cchName     = cchName;
        pChild->cChildren   = 0;
        pChild->papChildren = NULL;
        pChild->pParent     = pParent;

        pChild->uCacheGen   = pParent->uCacheGen == KFSWOBJ_CACHE_GEN_IGNORE ? KFSWOBJ_CACHE_GEN_IGNORE : g_uFsCacheGeneration;
        pChild->fAttribs    = dwAttr;
        pChild->uLastError  = dwErr;

        pChild->hCached     = INVALID_HANDLE_VALUE;
        pChild->cbCached    = 0;
        pChild->pbCached    = NULL;

        if (kwFsLinkChild(pParent, pChild))
            return pChild;

        kHlpFree(pChild);
    }
    return NULL;
}


/**
 * Look up a child node, ANSI version.
 *
 * @returns Pointer to the child if found, NULL if not.
 * @param   pParent             The parent to search the children of.
 * @param   pchName             The child name to search for (not terminated).
 * @param   cchName             The length of the child name.
 */
static PKWFSOBJ kwFsFindChildA(PKWFSOBJ pParent, const char *pchName, KU32 cchName)
{
    /* Check for '.' first. */
    if (cchName != 1 || *pchName != '.')
    {
        KU32        cLeft = pParent->cChildren;
        PKWFSOBJ   *ppCur = pParent->papChildren;
        while (cLeft-- > 0)
        {
            PKWFSOBJ pCur = *ppCur++;
            if (   pCur->cchName == cchName
                && _memicmp(pCur->pszName, pchName, cchName) == 0)
            {
                if (   pCur->uCacheGen != KFSWOBJ_CACHE_GEN_IGNORE
                    && pCur->uCacheGen != g_uFsCacheGeneration)
                    kwFsRefreshNode(pCur);
                return pCur;
            }
        }
        return NULL;
    }
    return pParent;
}


/**
 * Walk the file system tree for the given absolute path, entering it into the
 * hash table.
 *
 * This will create any missing nodes while walking.
 *
 * @returns Pointer to the tree node corresponding to @a pszPath.
 *          NULL if we ran out of memory.
 * @param   pszPath             The path to walk.
 * @param   cchPath             The length of the path.
 * @param   uHashPath           The hash of the path.
 * @param   idxHashTab          Index into the hash table.
 */
static PKWFSOBJ kwFsLookupAbsoluteA(const char *pszPath, KU32 cchPath, KU32 uHashPath, KU32 idxHashTab)
{
    PKWFSOBJ    pParent = &g_FsRoot;
    KU32        off;
    KWFS_LOG(("kwFsLookupAbsoluteA(%s)\n", pszPath));

    kHlpAssert(IS_ALPHA(pszPath[0]));
    kHlpAssert(pszPath[1] == ':');
    kHlpAssert(IS_SLASH(pszPath[2]));

    off = 0;
    for (;;)
    {
        PKWFSOBJ    pChild;

        /* Find the end of the component. */
        char        ch;
        KU32        cchSlashes = 0;
        KU32        offEnd = off + 1;
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

        /* Search the current node for the name. */
        pChild = kwFsFindChildA(pParent, &pszPath[off], offEnd - off);
        if (!pChild)
        {
            pChild = kwFsCreateChildA(pParent, pszPath, off, offEnd - off);
            if (!pChild)
                break;
        }
        off = offEnd + cchSlashes;
        if (   cchSlashes == 0
            || off >= cchPath)
        {
            kwFsCreateHashTabEntryA(pChild, pszPath, cchPath, uHashPath, idxHashTab);
            return pChild;
        }

        /* Check that it's a directory (won't match INVALID_FILE_ATTRIBUTES). */
        if (!(pChild->fAttribs & FILE_ATTRIBUTE_DIRECTORY))
            return &g_FsPathNotFound;

        pParent = pChild;
    }

    return NULL;
}


/**
 * This deals with paths that are relative and paths that contains '..'
 * elements.
 *
 * @returns Pointer to object corresponding to @a pszPath on success.
 *          NULL if this isn't a path we care to cache.
 * @param   pszPath             The path.
 * @param   cchPath             The length of the path.
 * @param   uHashPath           The hash of the path.
 * @param   idxHashTab          The path table index.
 */
static PKWFSOBJ kwFsLookupSlowA(const char *pszPath, KU32 cchPath, KU32 uHashPath, KU32 idxHashTab)
{
    /* Turns out getcwd/_getdcwd uses GetFullPathName internall, so just call it directly here. */
    char szFull[2048];
    UINT cchFull = GetFullPathNameA(pszPath, sizeof(szFull), szFull, NULL);
    if (   cchFull >= 3
        && cchFull < sizeof(szFull))
    {
        KWFS_LOG(("kwFsLookupSlowA(%s)\n", pszPath));
        if (   szFull[1] == ':'
            && IS_SLASH(szFull[2])
            && IS_ALPHA(szFull[0]) )
        {
            KU32     uHashPath2  = kwStrHash(szFull);
            PKWFSOBJ pFsObj = kwFsLookupAbsoluteA(szFull, cchFull, uHashPath2, uHashPath2 % K_ELEMENTS(g_apFsAnsiPaths));
            if (pFsObj)
            {
                kwFsCreateHashTabEntryA(pFsObj, pszPath, cchPath, uHashPath, idxHashTab);
                return pFsObj;
            }
        }

        /* It's worth remembering uncacheable paths in the hash table. */
        kwFsCreateHashTabEntryA(NULL /*pFsObj*/, pszPath, cchPath, uHashPath, idxHashTab);
    }
    return NULL;
}


/**
 * Looks up a KWFSOBJ for the given ANSI path.
 *
 * This will first try the hash table.  If not in the hash table, the file
 * system cache tree is walked, missing bits filled in and finally a hash table
 * entry is created.
 *
 * Only drive letter paths are cachable.  We don't do any UNC paths at this
 * point.
 *
 *
 * @returns Pointer to object corresponding to @a pszPath on success.
 *          NULL if not a path we care to cache.
 * @param   pszPath             The path to lookup.
 */
static PKWFSOBJ kwFsLookupA(const char *pszPath)
{
    /*
     * Do hash table lookup of the path.
     */
    KU32        uHashPath;
    KU32        cchPath    = (KU32)kwStrHashEx(pszPath, &uHashPath);
    KU32        idxHashTab = uHashPath % K_ELEMENTS(g_apFsAnsiPaths);
    PKWFSHASHA  pHashEntry = g_apFsAnsiPaths[idxHashTab];
    if (pHashEntry)
    {
        do
        {
            if (   pHashEntry->uHashPath == uHashPath
                && pHashEntry->cchPath   == cchPath
                && kHlpMemComp(pHashEntry->pszPath, pszPath, cchPath) == 0)
            {
                KWFS_LOG(("kwFsLookupA(%s) - hit %p\n", pszPath, pHashEntry->pFsObj));
                return pHashEntry->pFsObj;
            }
            pHashEntry = pHashEntry->pNext;
        } while (pHashEntry);
    }

    /*
     * Create an entry for it by walking the file system cache and filling in the blanks.
     */
    if (   cchPath > 0
        && cchPath < 1024)
    {
        /* Is absolute without any '..' bits? */
        if (   cchPath >= 3
            && pszPath[1] == ':'
            && IS_SLASH(pszPath[2])
            && IS_ALPHA(pszPath[0])
            && !kwFsHasDotDot(pszPath, cchPath) )
            return kwFsLookupAbsoluteA(pszPath, cchPath, uHashPath, idxHashTab);

        /* Not UNC? */
        if (   cchPath < 2
            || !IS_SLASH(pszPath[0])
            || !IS_SLASH(pszPath[1]) )
            return kwFsLookupSlowA(pszPath, cchPath, uHashPath, idxHashTab);


        /* It's worth remembering uncacheable paths in the hash table. */
        kwFsCreateHashTabEntryA(NULL /*pFsObj*/, pszPath, cchPath, uHashPath, idxHashTab);
    }
    return NULL;
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
        return K_TRUE;

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


static KBOOL kwFsObjCacheFileCommon(PKWFSOBJ pFsObj, HANDLE hFile)
{
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
                if (ReadFile(hFile, pbCache, cbCache, NULL, NULL))
                {
                    LARGE_INTEGER offZero;
                    offZero.QuadPart = 0;
                    if (SetFilePointerEx(hFile, offZero, NULL /*poffNew*/, FILE_BEGIN))
                    {
                        pFsObj->hCached  = hFile;
                        pFsObj->cbCached = cbCache;
                        pFsObj->pbCached = pbCache;
                        return K_TRUE;
                    }
                    else
                        KWFS_LOG(("Failed to seek to start of cached file! err=%u\n", GetLastError()));
                }
                else
                    KWFS_LOG(("Failed to read %#x bytes into cache! err=%u\n", cbCache, GetLastError()));
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
    CloseHandle(hFile);
    return K_FALSE;
}


static KBOOL kwFsObjCacheFileA(PKWFSOBJ pFsObj, const char *pszFilename)
{
    HANDLE hFile;
    kHlpAssert(pFsObj->hCached == INVALID_HANDLE_VALUE);

    hFile = CreateFileA(pszFilename, GENERIC_READ, FILE_SHARE_READ, NULL /*pSecAttrs*/,
                        FILE_OPEN_IF, FILE_ATTRIBUTE_NORMAL, NULL /*hTemplateFile*/);
    if (hFile != INVALID_HANDLE_VALUE)
        return kwFsObjCacheFileCommon(pFsObj, hFile);
    return K_FALSE;
}


static KBOOL kwFsObjCacheFileW(PKWFSOBJ pFsObj, const wchar_t *pwszFilename)
{
    HANDLE hFile;
    kHlpAssert(pFsObj->hCached == INVALID_HANDLE_VALUE);

    hFile = CreateFileW(pwszFilename, GENERIC_READ, FILE_SHARE_READ, NULL /*pSecAttrs*/,
                        FILE_OPEN_IF, FILE_ATTRIBUTE_NORMAL, NULL /*hTemplateFile*/);
    if (hFile != INVALID_HANDLE_VALUE)
        return kwFsObjCacheFileCommon(pFsObj, hFile);
    return K_FALSE;
}


/** Kernel32 - Common code for CreateFileW and CreateFileA.   */
static KBOOL kwFsObjCacheCreateFile(PKWFSOBJ pFsObj, DWORD dwDesiredAccess, BOOL fInheritHandle,
                                    const char *pszFilename, const wchar_t *pwszFilename, HANDLE *phFile)
{
    *phFile = INVALID_HANDLE_VALUE;

    /*
     * At the moment we only handle existing files.
     */
    if (!(pFsObj->fAttribs & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_DEVICE))) /* also checks invalid */
    {
        if (   pFsObj->hCached != INVALID_HANDLE_VALUE
            || (pwszFilename != NULL && kwFsObjCacheFileW(pFsObj, pwszFilename))
            || (pszFilename  != NULL && kwFsObjCacheFileA(pFsObj, pszFilename)) )
        {
            HANDLE hProcSelf = GetCurrentProcess();
            if (DuplicateHandle(hProcSelf, pFsObj->hCached,
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
                    pHandle->enmType  = KWHANDLETYPE_FSOBJ_READ_CACHE;
                    pHandle->offFile  = 0;
                    pHandle->hHandle  = *phFile;
                    pHandle->u.pFsObj = pFsObj;
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
                        PKWFSOBJ pFsObj = kwFsLookupA(pszFilename);
                        if (pFsObj)
                        {
                            if (kwFsObjCacheCreateFile(pFsObj, dwDesiredAccess, pSecAttrs && pSecAttrs->bInheritHandle,
                                                       pszFilename, NULL /*pwszFilename*/, &hFile))
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
    KWFS_LOG(("SetFilePointer(%p)\n", hFile));
    return SetFilePointer(hFile, cbMove, pcbMoveHi, dwMoveMethod);
}


/** Kernel32 - SetFilePointerEx */
static BOOL WINAPI kwSandbox_Kernel32_SetFilePointerEx(HANDLE hFile, LARGE_INTEGER pcbMove, PLARGE_INTEGER poffNew,
                                                       DWORD dwMoveMethod)
{
    KWFS_LOG(("SetFilePointerEx(%p)\n", hFile));
    return SetFilePointerEx(hFile, pcbMove, poffNew, dwMoveMethod);
}


/** Kernel32 - ReadFile */
static BOOL WINAPI kwSandbox_Kernel32_ReadFile(HANDLE hFile, LPVOID pvBuffer, DWORD cbToWrite, LPDWORD pcbActuallyWritten,
                                               LPOVERLAPPED pOverlapped)
{
    KWFS_LOG(("ReadFile(%p)\n", hFile));
    return ReadFile(hFile, pvBuffer, cbToWrite, pcbActuallyWritten, pOverlapped);
}


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
        PKWFSOBJ pFsObj = kwFsLookupA(pszFilename);
        if (pFsObj)
        {
            if (pFsObj->fAttribs == INVALID_FILE_ATTRIBUTES)
                SetLastError(pFsObj->uLastError);
            KWFS_LOG(("GetFileAttributesA(%s) -> %#x [cached]\n", pszFilename, pFsObj->fAttribs));
            return pFsObj->fAttribs;
        }
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
        /** @todo rewrite to pure UTF-16. */
        char szTmp[2048];
        KSIZE cch = kwUtf16ToStr(pwszFilename, szTmp, sizeof(szTmp));
        if (cch < sizeof(szTmp))
            return kwSandbox_Kernel32_GetFileAttributesA(szTmp);
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
        /** @todo proper implementation later, for now just copy it over as it. */
        KSIZE cwcLongPath = kwUtf16Len(pwszLongPath);
        cwcRet = kwUtf16CopyStyle1(pwszLongPath, pwszShortPath, cwcShortPath);
        KWFS_LOG(("GetShortPathNameW(%ls) -> '%*.*ls' & %#x [cached]\n",
                  pwszLongPath, K_MIN(cwcShortPath, cwcRet), K_MIN(cwcShortPath, cwcRet), pwszShortPath, cwcRet));
    }
    else
    {
        cwcRet = GetShortPathNameW(pwszLongPath, pwszShortPath, cwcShortPath);
        KWFS_LOG(("GetShortPathNameW(%ls) -> '%*.*ls' & %#x\n",
                  pwszLongPath, K_MIN(cwcShortPath, cwcRet), K_MIN(cwcShortPath, cwcRet), pwszShortPath, cwcRet));
    }
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
#if 0
    { TUPLE("ReadFile"),                    NULL,       (KUPTR)kwSandbox_Kernel32_ReadFile },
    { TUPLE("SetFilePointer"),              NULL,       (KUPTR)kwSandbox_Kernel32_SetFilePointer },
    { TUPLE("SetFilePointerEx"),            NULL,       (KUPTR)kwSandbox_Kernel32_SetFilePointerEx },
#endif
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



static int kwSandboxInit(PKWSANDBOX pSandbox, PKWTOOL pTool, const char *pszCmdLine)
{
    PPEB pPeb = kwSandboxGetProcessEnvironmentBlock();
    wchar_t *pwcPool;
    KSIZE cbStrings;
    KSIZE cwc;
    int i;

    /* Simple stuff. */
    g_Sandbox.rcExitCode    = 256;
    g_Sandbox.pTool         = pTool;
    g_Sandbox.idMainThread  = GetCurrentThreadId();
    g_Sandbox.TibMainThread = *(PNT_TIB)NtCurrentTeb();
    g_Sandbox.pszCmdLine    = pszCmdLine;
    g_Sandbox.pgmptr        = (char *)pTool->pszPath;
    g_Sandbox.wpgmptr       = (wchar_t *)pTool->pwszPath;

    /*
     * Convert the command line to argc and argv.
     */
    cbStrings = parse_args(pszCmdLine, &pSandbox->cArgs, NULL /*papszArgs*/, NULL /*pchPool*/);
    pSandbox->papszArgs = (char **)kHlpAlloc(sizeof(char *) * (pSandbox->cArgs + 2) + cbStrings);
    if (!pSandbox->papszArgs)
        return KERR_NO_MEMORY;
    parse_args(pSandbox->pszCmdLine, &pSandbox->cArgs, pSandbox->papszArgs, (char *)&pSandbox->papszArgs[pSandbox->cArgs + 2]);
    pSandbox->papszArgs[pSandbox->cArgs + 0] = NULL;
    pSandbox->papszArgs[pSandbox->cArgs + 1] = NULL;

    /*
     * Convert command line and argv to UTF-16.
     * We assume each ANSI char requires a surrogate pair in the UTF-16 variant.
     */
    pSandbox->papwszArgs = (wchar_t **)kHlpAlloc(sizeof(wchar_t *) * (pSandbox->cArgs + 2) + cbStrings * 2 * sizeof(wchar_t));
    if (!pSandbox->papwszArgs)
        return KERR_NO_MEMORY;
    pwcPool = (wchar_t *)&pSandbox->papwszArgs[pSandbox->cArgs + 2];
    for (i = 0; i < pSandbox->cArgs; i++)
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
    cbStrings = (kHlpStrLen(pSandbox->pszCmdLine) + 1) * 2 * sizeof(wchar_t);
    pSandbox->pwszCmdLine = kHlpAlloc(cbStrings);
    if (!pSandbox->pwszCmdLine)
        return KERR_NO_MEMORY;
    cwc = kwStrToUtf16(pSandbox->pszCmdLine, pSandbox->pwszCmdLine, cbStrings / sizeof(wchar_t));

    pSandbox->SavedCommandLine = pPeb->ProcessParameters->CommandLine;
    pPeb->ProcessParameters->CommandLine.Buffer = pSandbox->pwszCmdLine;
    pPeb->ProcessParameters->CommandLine.Length = (USHORT)cwc * sizeof(wchar_t);


    g_uFsCacheGeneration++;
    if (g_uFsCacheGeneration == KFSWOBJ_CACHE_GEN_IGNORE)
        g_uFsCacheGeneration++;
    return 0;
}


static void kwSandboxCleanup(PKWSANDBOX pSandbox)
{
    PPEB pPeb = kwSandboxGetProcessEnvironmentBlock();
    pPeb->ProcessParameters->CommandLine = pSandbox->SavedCommandLine;
    /** @todo lots more to do here!   */
}


static int kwSandboxExec(PKWTOOL pTool, const char *pszCmdLine, int *prcExitCode)
{
    int rc;

    *prcExitCode            = 256;

    /*
     * Initialize the sandbox environment.
     */
    rc = kwSandboxInit(&g_Sandbox, pTool, pszCmdLine);
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
                int rcExitCode;
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
                        rcExitCode = pfnWin64Entrypoint(kwSandboxGetProcessEnvironmentBlock(), NULL, NULL, NULL);
                    }
                    else
                        rcExitCode = g_Sandbox.rcExitCode;
                }
                __except (EXCEPTION_EXECUTE_HANDLER)
                {
                    rcExitCode = 512;
                }
                *prcExitCode = rcExitCode;

                /*
                 * Restore the TIB and later some other stuff.
                 */
                *(PNT_TIB)NtCurrentTeb() = g_Sandbox.TibMainThread;
            }
        }
    }

    return rc;
}


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


int main(int argc, char **argv)
{
    int rc = 0;
    int i;
    argv[2] = "\"E:/vbox/svn/trunk/tools/win.x86/vcc/v10sp1/bin/amd64/cl.exe\" -c -c -TP -nologo -Zi -Zi -Zl -GR- -EHsc -GF -Zc:wchar_t- -Oy- -MT -W4 -Wall -wd4065 -wd4996 -wd4127 -wd4706 -wd4201 -wd4214 -wd4510 -wd4512 -wd4610 -wd4514 -wd4820 -wd4365 -wd4987 -wd4710 -wd4061 -wd4986 -wd4191 -wd4574 -wd4917 -wd4711 -wd4611 -wd4571 -wd4324 -wd4505 -wd4263 -wd4264 -wd4738 -wd4242 -wd4244 -WX -RTCsu -IE:/vbox/svn/trunk/tools/win.x86/vcc/v10sp1/include -IE:/vbox/svn/trunk/tools/win.x86/vcc/v10sp1/atlmfc/include -IE:/vbox/svn/trunk/tools/win.x86/sdk/v7.1/Include -IE:/vbox/svn/trunk/include -IE:/vbox/svn/trunk/out/win.amd64/debug -IE:/vbox/svn/trunk/tools/win.x86/vcc/v10sp1/include -IE:/vbox/svn/trunk/tools/win.x86/vcc/v10sp1/atlmfc/include -DVBOX -DVBOX_WITH_64_BITS_GUESTS -DVBOX_WITH_REM -DVBOX_WITH_RAW_MODE -DDEBUG -DDEBUG_bird -DDEBUG_USERNAME=bird -DRT_OS_WINDOWS -D__WIN__ -DRT_ARCH_AMD64 -D__AMD64__ -D__WIN64__ -DVBOX_WITH_DEBUGGER -DRT_LOCK_STRICT -DRT_LOCK_STRICT_ORDER -DIN_RING3 -DLOG_DISABLED -DIN_BLD_PROG -D_CRT_SECURE_NO_DEPRECATE -FdE:/vbox/svn/trunk/out/win.amd64/debug/obj/VBoxBs2Linker/VBoxBs2Linker-obj.pdb -FD -FoE:/vbox/svn/trunk/out/win.amd64/debug/obj/VBoxBs2Linker/VBoxBs2Linker.obj E:\\vbox\\svn\\trunk\\src\\VBox\\ValidationKit\\bootsectors\\VBoxBs2Linker.cpp";
#if 1
    rc = kwExecCmdLine(argv[1], argv[2]);
    //rc = kwExecCmdLine(argv[1], argv[2]);
    K_NOREF(i);
#else
// run 2: 34/1024 = 0x0 (0.033203125)
// run 1: 37/1024 = 0x0 (0.0361328125)
// kmk 1: 44/1024 = 0x0 (0.04296875)
// cmd 1: 48/1024 = 0x0 (0.046875)
    g_cVerbose = 0;
    for (i = 0; i < 1024 && rc == 0; i++)
        rc = kwExecCmdLine(argv[1], argv[2]);
#endif
    return rc;
}

