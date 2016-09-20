/* $Id: kWorker.c 2949 2016-09-20 16:39:06Z knut.osmundsen@oracle.com $ */
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
//#undef NDEBUG
//#define K_STRICT 1
//#define KW_LOG_ENABLED

#define PSAPI_VERSION 1
#include <k/kHlp.h>
#include <k/kLdr.h>

#include <stdio.h>
#include <intrin.h>
#include <setjmp.h>
#include <ctype.h>
#include <errno.h>

#include "nt/ntstat.h"
#include "kbuild_version.h"

#include "nt/ntstuff.h"
#include <psapi.h>

#include "nt/kFsCache.h"
#include "nt_fullpath.h"
#include "quote_argv.h"
#include "md5.h"

#include "../kmk/kmkbuiltin.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** @def WITH_TEMP_MEMORY_FILES
 * Enables temporary memory files for cl.exe.  */
#define WITH_TEMP_MEMORY_FILES

/** @def WITH_HASH_MD5_CACHE
 * Enables caching of MD5 sums for cl.exe.
 * This prevents wasting time on rehashing common headers each time
 * they are included. */
#define WITH_HASH_MD5_CACHE

/** @def WITH_CRYPT_CTX_REUSE
 * Enables reusing crypt contexts.  The Visual C++ compiler always creates a
 * context which is only used for MD5 and maybe some random bytes (VS 2010).
 * So, only create it once and add a reference to it instead of creating new
 * ones.  Saves registry access among other things. */
#define WITH_CRYPT_CTX_REUSE

/** @def WITH_CONSOLE_OUTPUT_BUFFERING
 * Enables buffering of all console output as well as removal of annoying
 * source file echo by cl.exe. */
#define WITH_CONSOLE_OUTPUT_BUFFERING

/** @def WITH_STD_OUT_ERR_BUFFERING
 * Enables buffering of standard output and standard error buffer as well as
 * removal of annoying source file echo by cl.exe. */
#define WITH_STD_OUT_ERR_BUFFERING

/** @def WITH_LOG_FILE
 * Log to file instead of stderr. */
#define WITH_LOG_FILE

/** @def WITH_HISTORY
 * Keep history of the last jobs.  For debugging.  */
#define WITH_HISTORY


#ifndef NDEBUG
# define KW_LOG_ENABLED
#endif

/** @def KW_LOG
 * Generic logging.
 * @param a     Argument list for kwDbgPrintf  */
#ifdef KW_LOG_ENABLED
# define KW_LOG(a) kwDbgPrintf a
#else
# define KW_LOG(a) do { } while (0)
#endif

/** @def KWLDR_LOG
 * Loader related logging.
 * @param a     Argument list for kwDbgPrintf  */
#ifdef KW_LOG_ENABLED
# define KWLDR_LOG(a) kwDbgPrintf a
#else
# define KWLDR_LOG(a) do { } while (0)
#endif


/** @def KWFS_LOG
 * FS cache logging.
 * @param a     Argument list for kwDbgPrintf  */
#ifdef KW_LOG_ENABLED
# define KWFS_LOG(a) kwDbgPrintf a
#else
# define KWFS_LOG(a) do { } while (0)
#endif

/** @def KWOUT_LOG
 * Output related logging.
 * @param a     Argument list for kwDbgPrintf  */
#ifdef KW_LOG_ENABLED
# define KWOUT_LOG(a) kwDbgPrintf a
#else
# define KWOUT_LOG(a) do { } while (0)
#endif

/** @def KWCRYPT_LOG
 * FS cache logging.
 * @param a     Argument list for kwDbgPrintf  */
#ifdef KW_LOG_ENABLED
# define KWCRYPT_LOG(a) kwDbgPrintf a
#else
# define KWCRYPT_LOG(a) do { } while (0)
#endif

/** Converts a windows handle to a handle table index.
 * @note We currently just mask off the 31th bit, and do no shifting or anything
 *     else to create an index of the handle.
 * @todo consider shifting by 2 or 3. */
#define KW_HANDLE_TO_INDEX(a_hHandle)   ((KUPTR)(a_hHandle) & ~(KUPTR)KU32_C(0x8000000))
/** Maximum handle value we can deal with.   */
#define KW_HANDLE_MAX                   0x20000

/** Max temporary file size (memory backed).  */
#if K_ARCH_BITS >= 64
# define KWFS_TEMP_FILE_MAX             (256*1024*1024)
#else
# define KWFS_TEMP_FILE_MAX             (64*1024*1024)
#endif

/** Marks unfinished code.  */
#if 1
# define KWFS_TODO()    do { kwErrPrintf("\nHit TODO on line %u in %s!\n", __LINE__, __FUNCTION__); fflush(stderr); __debugbreak(); } while (0)
#else
# define KWFS_TODO()    do { kwErrPrintf("\nHit TODO on line %u in %s!\n", __LINE__, __FUNCTION__); fflush(stderr); } while (0)
#endif

/** User data key for tools. */
#define KW_DATA_KEY_TOOL                (~(KUPTR)16381)
/** User data key for a cached file. */
#define KW_DATA_KEY_CACHED_FILE         (~(KUPTR)65521)

/** String constant comma length.   */
#define TUPLE(a_sz)                     a_sz, sizeof(a_sz) - 1


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
    /** The of the loaded image bits. */
    KSIZE               cbImage;

    union
    {
        /** Data for a manually loaded image. */
        struct
        {
            /** Where we load the image. */
            KU8                *pbLoad;
            /** Virgin copy of the image. */
            KU8                *pbCopy;
            /** Ldr pvBits argument.  This is NULL till we've successfully resolved
             *  the imports. */
            void               *pvBits;
            /** The state. */
            KWMODSTATE          enmState;
#if defined(KBUILD_OS_WINDOWS) && defined(KBUILD_ARCH_AMD64)
            /** The number of entries in the table. */
            KU32                cFunctions;
            /** The function table address (in the copy). */
            PRUNTIME_FUNCTION   paFunctions;
            /** Set if we've already registered a function table already. */
            KBOOL               fRegisteredFunctionTable;
#endif
            /** Set if we share memory with other executables. */
            KBOOL               fUseLdBuf;
            /** Set after the first whole image copy is done. */
            KBOOL               fCanDoQuick;
            /** Number of quick copy chunks. */
            KU8                 cQuickCopyChunks;
            /** Number of quick zero chunks. */
            KU8                 cQuickZeroChunks;
            /** Quicker image copy instructions that skips non-writable parts when
             * possible.  Need to check fCanDoQuick, fUseLdBuf and previous executable
             * image. */
            struct
            {
                /** The copy destination.   */
                KU8            *pbDst;
                /** The copy source.   */
                KU8 const      *pbSrc;
                /** How much to copy. */
                KSIZE           cbToCopy;
            } aQuickCopyChunks[3];
            /** For handling BSS and zero alignment padding when using aQuickCopyChunks. */
            struct
            {
                /** Where to start zeroing. */
                KU8            *pbDst;
                /** How much to zero. */
                KSIZE           cbToZero;
            } aQuickZeroChunks[3];
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

    /** The module handle we present to the application.
     * This is the LoadLibraryEx return value for special modules and the
     * KWMODULE.hOurMod value for the others. */
    HMODULE             hmod;

    /** The module for non-special resource stuff, NULL if special. */
    PKWMODULE           pMod;

    /** The length of the LoadLibary filename. */
    KSIZE               cchRequest;
    /** The LoadLibrary filename. */
    char                szRequest[1];
} KWDYNLOAD;


/**
 * GetModuleHandle cache for system modules frequently queried.
 */
typedef struct KWGETMODULEHANDLECACHE
{
    const char     *pszName;
    KU8             cchName;
    KU8             cwcName;
    const wchar_t  *pwszName;
    HANDLE          hmod;
} KWGETMODULEHANDLECACHE;
typedef KWGETMODULEHANDLECACHE *PKWGETMODULEHANDLECACHE;


/**
 * A cached file.
 */
typedef struct KFSWCACHEDFILE
{
    /** The user data core. */
    KFSUSERDATA         Core;

    /** Cached file handle. */
    HANDLE              hCached;
    /** Cached file content. */
    KU8                *pbCached;
    /** The file size. */
    KU32                cbCached;
#ifdef WITH_HASH_MD5_CACHE
    /** Set if we've got a valid MD5 hash in abMd5Digest. */
    KBOOL               fValidMd5;
    /** The MD5 digest if fValidMd5 is set. */
    KU8                 abMd5Digest[16];
#endif

    /** Circular self reference. Prevents the object from ever going away and
     * keeps it handy for debugging. */
    PKFSOBJ             pFsObj;
    /** The file path (for debugging).   */
    char                szPath[1];
} KFSWCACHEDFILE;
/** Pointer to a cached filed. */
typedef KFSWCACHEDFILE *PKFSWCACHEDFILE;

#ifdef WITH_HASH_MD5_CACHE

/** Pointer to a MD5 hash instance. */
typedef struct KWHASHMD5 *PKWHASHMD5;
/**
 * A MD5 hash instance.
 */
typedef struct KWHASHMD5
{
    /** The magic value. */
    KUPTR               uMagic;
    /** Pointer to the next hash handle. */
    PKWHASHMD5          pNext;
    /** The cached file we've associated this handle with. */
    PKFSWCACHEDFILE     pCachedFile;
    /** The number of bytes we've hashed. */
    KU32                cbHashed;
    /** Set if this has gone wrong. */
    KBOOL               fGoneBad;
    /** Set if we're in fallback mode (file not cached). */
    KBOOL               fFallbackMode;
    /** Set if we've already finalized the digest. */
    KBOOL               fFinal;
    /** The MD5 fallback context. */
    struct MD5Context   Md5Ctx;
    /** The finalized digest. */
    KU8                 abDigest[16];

} KWHASHMD5;
/** Magic value for KWHASHMD5::uMagic (Les McCann). */
# define KWHASHMD5_MAGIC    KUPTR_C(0x19350923)

#endif /* WITH_HASH_MD5_CACHE */
#ifdef WITH_TEMP_MEMORY_FILES

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

#endif /* WITH_TEMP_MEMORY_FILES */
#ifdef WITH_CONSOLE_OUTPUT_BUFFERING

/**
 * Console line buffer or output full buffer.
 */
typedef struct KWOUTPUTSTREAMBUF
{
    /** The main output handle. */
    HANDLE              hOutput;
    /** Our backup handle. */
    HANDLE              hBackup;
    /** Set if this is a console handle and we're in line buffered mode.
     * When clear, we may buffer multiple lines, though try flush on line
     * boundraries when ever possible. */
    KBOOL               fIsConsole;
    /** Compressed GetFileType result. */
    KU8                 fFileType;
    KU8                 abPadding[2];
    union
    {
        /** Line buffer mode (fIsConsole == K_TRUE). */
        struct
        {
            /** Amount of pending console output in wchar_t's. */
            KU32                cwcBuf;
            /** The allocated buffer size.   */
            KU32                cwcBufAlloc;
            /** Pending console output. */
            wchar_t            *pwcBuf;
        } Con;
        /** Fully buffered mode (fIsConsole == K_FALSE). */
        struct
        {
            /** Amount of pending output (in chars). */
            KU32                cchBuf;
#ifdef WITH_STD_OUT_ERR_BUFFERING
            /** The allocated buffer size (in chars).   */
            KU32                cchBufAlloc;
            /** Pending output. */
            char               *pchBuf;
#endif
        } Fully;
    } u;
} KWOUTPUTSTREAMBUF;
/** Pointer to a console line buffer. */
typedef KWOUTPUTSTREAMBUF *PKWOUTPUTSTREAMBUF;

/**
 * Combined console buffer of complete lines.
 */
typedef struct KWCONSOLEOUTPUT
{
    /** The console output handle.
     * INVALID_HANDLE_VALUE if we haven't got a console and shouldn't be doing any
     * combined output buffering. */
    HANDLE              hOutput;
    /** The current code page for the console. */
    KU32                uCodepage;
    /** Amount of pending console output in wchar_t's. */
    KU32                cwcBuf;
    /** Number of times we've flushed it in any way (for cl.exe hack). */
    KU32                cFlushes;
    /** Pending console output. */
    wchar_t             wszBuf[8192];
} KWCONSOLEOUTPUT;
/** Pointer to a combined console buffer. */
typedef KWCONSOLEOUTPUT *PKWCONSOLEOUTPUT;

#endif /* WITH_CONSOLE_OUTPUT_BUFFERING */

/** Handle type.   */
typedef enum KWHANDLETYPE
{
    KWHANDLETYPE_INVALID = 0,
    KWHANDLETYPE_FSOBJ_READ_CACHE,
    KWHANDLETYPE_FSOBJ_READ_CACHE_MAPPING,
    KWHANDLETYPE_TEMP_FILE,
    KWHANDLETYPE_TEMP_FILE_MAPPING,
    KWHANDLETYPE_OUTPUT_BUF
} KWHANDLETYPE;

/** Handle data. */
typedef struct KWHANDLE
{
    KWHANDLETYPE        enmType;
    /** Number of references   */
    KU32                cRefs;
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
#ifdef WITH_CONSOLE_OUTPUT_BUFFERING
        /** Buffered output stream. */
        PKWOUTPUTSTREAMBUF  pOutBuf;
#endif
    } u;
} KWHANDLE;
typedef KWHANDLE *PKWHANDLE;

/**
 * Tracking one of our memory mappings.
 */
typedef struct KWMEMMAPPING
{
    /** Number of references. */
    KU32                cRefs;
    /** The mapping type (KWHANDLETYPE_FSOBJ_READ_CACHE_MAPPING or
     *  KWHANDLETYPE_TEMP_FILE_MAPPING). */
    KWHANDLETYPE        enmType;
    /** The mapping address. */
    PVOID               pvMapping;
    /** Type specific data. */
    union
    {
        /** The file system object.   */
        PKFSWCACHEDFILE     pCachedFile;
        /** Temporary file handle or mapping handle. */
        PKWFSTEMPFILE       pTempFile;
    } u;
} KWMEMMAPPING;
/** Pointer to a memory mapping tracker. */
typedef KWMEMMAPPING *PKWMEMMAPPING;


/** Pointer to a VirtualAlloc tracker entry. */
typedef struct KWVIRTALLOC *PKWVIRTALLOC;
/**
 * Tracking an VirtualAlloc allocation.
 */
typedef struct KWVIRTALLOC
{
    PKWVIRTALLOC        pNext;
    void               *pvAlloc;
    KSIZE               cbAlloc;
} KWVIRTALLOC;


/** Pointer to a heap (HeapCreate) tracker entry. */
typedef struct KWHEAP *PKWHEAP;
/**
 * Tracking an heap (HeapCreate)
 */
typedef struct KWHEAP
{
    PKWHEAP             pNext;
    HANDLE              hHeap;
} KWHEAP;


/** Pointer to a FlsAlloc/TlsAlloc tracker entry. */
typedef struct KWLOCALSTORAGE *PKWLOCALSTORAGE;
/**
 * Tracking an FlsAlloc/TlsAlloc index.
 */
typedef struct KWLOCALSTORAGE
{
    PKWLOCALSTORAGE     pNext;
    KU32                idx;
} KWLOCALSTORAGE;


/** Pointer to an at exit callback record */
typedef struct KWEXITCALLACK *PKWEXITCALLACK;
/**
 * At exit callback record.
 */
typedef struct KWEXITCALLACK
{
    PKWEXITCALLACK      pNext;
    _onexit_t           pfnCallback;
    /** At exit doesn't have an exit code. */
    KBOOL               fAtExit;
} KWEXITCALLACK;


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
    KWTOOLHINT_VISUAL_CPP_LINK,
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
            /** The main entry point. */
            KUPTR       uMainAddr;
            /** The executable. */
            PKWMODULE   pExe;
            /** List of dynamically loaded modules.
             * These will be kept loaded till the tool is destroyed (if we ever do that). */
            PKWDYNLOAD  pDynLoadHead;
            /** Module array sorted by hOurMod. */
            PKWMODULE  *papModules;
            /** Number of entries in papModules. */
            KU32        cModules;

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
    /** The NT_TIB::ExceptionList value inside the try case.
     * We restore this prior to the longjmp.  */
    void       *pOutXcptListHead;
    /** The exit code in case of longjmp.   */
    int         rcExitCode;
    /** Set if we're running. */
    KBOOL       fRunning;

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

    /** Size of the array we've allocated (ASSUMES nobody messes with it!). */
    KSIZE       cEnvVarsAllocated;
    /** The _environ msvcrt variable. */
    char      **environ;
    /** The _wenviron msvcrt variable. */
    wchar_t   **wenviron;
    /** The shadow _environ msvcrt variable. */
    char      **papszEnvVars;
    /** The shadow _wenviron msvcrt variable. */
    wchar_t   **papwszEnvVars;


    /** Handle table. */
    PKWHANDLE      *papHandles;
    /** Size of the handle table. */
    KU32            cHandles;
    /** Number of active handles in the table. */
    KU32            cActiveHandles;
    /** Number of handles in the handle table that will not be freed.   */
    KU32            cFixedHandles;
    /** Total number of leaked handles. */
    KU32            cLeakedHandles;

    /** Number of active memory mappings in paMemMappings. */
    KU32            cMemMappings;
    /** The allocated size of paMemMappings. */
    KU32            cMemMappingsAlloc;
    /** Memory mappings (MapViewOfFile / UnmapViewOfFile). */
    PKWMEMMAPPING   paMemMappings;

    /** Head of the list of temporary file. */
    PKWFSTEMPFILE   pTempFileHead;

    /** Head of the virtual alloc allocations. */
    PKWVIRTALLOC    pVirtualAllocHead;
    /** Head of the heap list (HeapCreate).
     * This is only done from images we forcibly restore.  */
    PKWHEAP         pHeapHead;
    /** Head of the FlsAlloc indexes. */
    PKWLOCALSTORAGE pFlsAllocHead;
    /** Head of the TlsAlloc indexes. */
    PKWLOCALSTORAGE pTlsAllocHead;

    /** The at exit callback head.
     * This is only done from images we forcibly restore.  */
    PKWEXITCALLACK  pExitCallbackHead;

    MY_UNICODE_STRING SavedCommandLine;

#ifdef WITH_HASH_MD5_CACHE
    /** The special MD5 hash instance. */
    PKWHASHMD5      pHashHead;
    /** ReadFile sets these while CryptHashData claims and clears them.
     *
     * This is part of the heuristics we use for MD5 caching for header files. The
     * observed pattern is that c1.dll/c1xx.dll first reads a chunk of a source or
     * header, then passes the same buffer and read byte count to CryptHashData.
     */
    struct
    {
        /** The cached file last read from. */
        PKFSWCACHEDFILE pCachedFile;
        /** The file offset of the last cached read. */
        KU32            offRead;
        /** The number of bytes read last. */
        KU32            cbRead;
        /** The buffer pointer of the last read. */
        void           *pvRead;
    } LastHashRead;
#endif

#ifdef WITH_CRYPT_CTX_REUSE
    /** Reusable crypt contexts.  */
    struct
    {
        /** The creation provider type.  */
        KU32            dwProvType;
        /** The creation flags. */
        KU32            dwFlags;
        /** The length of the container name. */
        KU32            cwcContainer;
        /** The length of the provider name. */
        KU32            cwcProvider;
        /** The container name string. */
        wchar_t        *pwszContainer;
        /** The provider name string. */
        wchar_t        *pwszProvider;
        /** The context handle. */
        HCRYPTPROV      hProv;
    }                   aCryptCtxs[4];
    /** Number of reusable crypt conexts in aCryptCtxs. */
    KU32                cCryptCtxs;
#endif


#ifdef WITH_CONSOLE_OUTPUT_BUFFERING
    /** The internal standard output handle. */
    KWHANDLE            HandleStdOut;
    /** The internal standard error handle. */
    KWHANDLE            HandleStdErr;
    /** Standard output (and whatever else) buffer. */
    KWOUTPUTSTREAMBUF   StdOut;
    /** Standard error buffer. */
    KWOUTPUTSTREAMBUF   StdErr;
    /** Combined buffer of completed lines. */
    KWCONSOLEOUTPUT     Combined;
#endif
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
    /** Only replace in the executable.
     * @todo fix the reinitialization of non-native DLLs!  */
    KBOOL       fOnlyExe;
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

/** The module currently occupying g_abDefLdBuf. */
static PKWMODULE    g_pModInLdBuf = NULL;

/** The module that previuosly occupied g_abDefLdBuf. */
static PKWMODULE    g_pModPrevInLdBuf = NULL;

/** Module hash table. */
static PKWMODULE    g_apModules[127];

/** GetModuleHandle cache. */
static KWGETMODULEHANDLECACHE g_aGetModuleHandleCache[] =
{
#define MOD_CACHE_STRINGS(str) str, sizeof(str) - 1, (sizeof(L##str) / sizeof(wchar_t)) - 1, L##str
    { MOD_CACHE_STRINGS("KERNEL32.DLL"),    NULL },
    { MOD_CACHE_STRINGS("mscoree.dll"),     NULL },
};


/** The file system cache. */
static PKFSCACHE    g_pFsCache;
/** The current directory (referenced). */
static PKFSOBJ      g_pCurDirObj = NULL;
#ifdef KBUILD_OS_WINDOWS
/** The windows system32 directory (referenced). */
static PKFSDIR      g_pWinSys32 = NULL;
#endif

/** Verbosity level. */
static int          g_cVerbose = 2;

/** Whether we should restart the worker. */
static KBOOL        g_fRestart = K_FALSE;

/* Further down. */
extern KWREPLACEMENTFUNCTION const g_aSandboxReplacements[];
extern KU32                  const g_cSandboxReplacements;

extern KWREPLACEMENTFUNCTION const g_aSandboxNativeReplacements[];
extern KU32                  const g_cSandboxNativeReplacements;

extern KWREPLACEMENTFUNCTION const g_aSandboxGetProcReplacements[];
extern KU32                  const g_cSandboxGetProcReplacements;


/** Create a larget BSS blob that with help of /IMAGEBASE:0x10000 should
 * cover the default executable link address of 0x400000.
 * @remarks Early main() makes it read+write+executable.  Attempts as having
 *          it as a separate section failed because the linker insists on
 *          writing out every zero in the uninitialized section, resulting in
 *          really big binaries. */
__declspec(align(0x1000))
static KU8          g_abDefLdBuf[16*1024*1024];

#ifdef WITH_LOG_FILE
/** Log file handle.   */
static HANDLE g_hLogFile = INVALID_HANDLE_VALUE;
#endif


#ifdef WITH_HISTORY
/** The job history. */
static char     *g_apszHistory[32];
/** Index of the next history entry. */
static unsigned  g_iHistoryNext = 0;
#endif


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static FNKLDRMODGETIMPORT kwLdrModuleGetImportCallback;
static int kwLdrModuleResolveAndLookup(const char *pszName, PKWMODULE pExe, PKWMODULE pImporter, PKWMODULE *ppMod);
static KBOOL kwSandboxHandleTableEnter(PKWSANDBOX pSandbox, PKWHANDLE pHandle, HANDLE hHandle);
#ifdef WITH_CONSOLE_OUTPUT_BUFFERING
static void kwSandboxConsoleWriteA(PKWSANDBOX pSandbox, PKWOUTPUTSTREAMBUF pLineBuf, const char *pchBuffer, KU32 cchToWrite);
#endif




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
#ifdef WITH_LOG_FILE
        DWORD       dwIgnored;
        char        szTmp[2048];
        int         cchPrefix = _snprintf(szTmp, sizeof(szTmp), "%x:%x: ", GetCurrentProcessId(), GetCurrentThreadId());
        int         cch = vsnprintf(&szTmp[cchPrefix], sizeof(szTmp) - cchPrefix, pszFormat, va);
        if (cch < (int)sizeof(szTmp) - 1 - cchPrefix)
            cch += cchPrefix;
        else
        {
            cch = sizeof(szTmp) - 1;
            szTmp[cch] = '\0';
        }

        if (g_hLogFile == INVALID_HANDLE_VALUE)
        {
            wchar_t wszFilename[128];
            _snwprintf(wszFilename, K_ELEMENTS(wszFilename), L"kWorker-%x-%x.log", GetTickCount(), GetCurrentProcessId());
            g_hLogFile = CreateFileW(wszFilename, GENERIC_WRITE, FILE_SHARE_READ, NULL /*pSecAttrs*/, CREATE_ALWAYS,
                                     FILE_ATTRIBUTE_NORMAL, NULL /*hTemplateFile*/);
        }

        WriteFile(g_hLogFile, szTmp, cch, &dwIgnored, NULL /*pOverlapped*/);
#else
        fprintf(stderr, "debug: ");
        vfprintf(stderr, pszFormat, va);
#endif

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
 * Converts the given string to UTF-16, allocating the buffer.
 *
 * @returns Pointer to the new heap allocation containing the UTF-16 version of
 *          the source string.
 * @param   pchSrc              The source string.
 * @param   cchSrc              The length of the source string.
 */
static wchar_t *kwStrToUtf16AllocN(const char *pchSrc, KSIZE cchSrc)
{
    DWORD const dwErrSaved = GetLastError();
    KSIZE       cwcBuf     = cchSrc + 1;
    wchar_t    *pwszBuf    = (wchar_t *)kHlpAlloc(cwcBuf * sizeof(pwszBuf));
    if (pwszBuf)
    {
        if (cchSrc > 0)
        {
            int cwcRet = MultiByteToWideChar(CP_ACP, 0, pchSrc, (int)cchSrc, pwszBuf, (int)cwcBuf - 1);
            if (cwcRet > 0)
            {
                kHlpAssert(cwcRet < (KSSIZE)cwcBuf);
                pwszBuf[cwcRet] = '\0';
            }
            else
            {
                kHlpFree(pwszBuf);

                /* Figure the length and allocate the right buffer size. */
                SetLastError(NO_ERROR);
                cwcRet = MultiByteToWideChar(CP_ACP, 0, pchSrc, (int)cchSrc, pwszBuf, 0);
                if (cwcRet)
                {
                    cwcBuf = cwcRet + 2;
                    pwszBuf = (wchar_t *)kHlpAlloc(cwcBuf * sizeof(pwszBuf));
                    if (pwszBuf)
                    {
                        SetLastError(NO_ERROR);
                        cwcRet = MultiByteToWideChar(CP_ACP, 0, pchSrc, (int)cchSrc, pwszBuf, (int)cwcBuf - 1);
                        if (cwcRet)
                        {
                            kHlpAssert(cwcRet < (KSSIZE)cwcBuf);
                            pwszBuf[cwcRet] = '\0';
                        }
                        else
                        {
                            kwErrPrintf("MultiByteToWideChar(,,%*.*s,,) -> dwErr=%d\n", cchSrc, cchSrc, pchSrc, GetLastError());
                            kHlpFree(pwszBuf);
                            pwszBuf = NULL;
                        }
                    }
                }
                else
                {
                    kwErrPrintf("MultiByteToWideChar(,,%*.*s,,NULL,0) -> dwErr=%d\n", cchSrc, cchSrc, pchSrc, GetLastError());
                    pwszBuf = NULL;
                }
            }
        }
        else
            pwszBuf[0] = '\0';
    }
    SetLastError(dwErrSaved);
    return pwszBuf;
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


/**
 * Converts the given UTF-16 to ASSI, allocating the buffer.
 *
 * @returns Pointer to the new heap allocation containing the ANSI version of
 *          the source string.
 * @param   pwcSrc              The source string.
 * @param   cwcSrc              The length of the source string.
 */
static char *kwUtf16ToStrAllocN(const wchar_t *pwcSrc, KSIZE cwcSrc)
{
    DWORD const dwErrSaved = GetLastError();
    KSIZE       cbBuf      = cwcSrc + (cwcSrc >> 1) + 1;
    char       *pszBuf     = (char *)kHlpAlloc(cbBuf);
    if (pszBuf)
    {
        if (cwcSrc > 0)
        {
            int cchRet = WideCharToMultiByte(CP_ACP, 0, pwcSrc, (int)cwcSrc, pszBuf, (int)cbBuf - 1, NULL, NULL);
            if (cchRet > 0)
            {
                kHlpAssert(cchRet < (KSSIZE)cbBuf);
                pszBuf[cchRet] = '\0';
            }
            else
            {
                kHlpFree(pszBuf);

                /* Figure the length and allocate the right buffer size. */
                SetLastError(NO_ERROR);
                cchRet = WideCharToMultiByte(CP_ACP, 0, pwcSrc, (int)cwcSrc, pszBuf, 0, NULL, NULL);
                if (cchRet)
                {
                    cbBuf = cchRet + 2;
                    pszBuf = (char *)kHlpAlloc(cbBuf);
                    if (pszBuf)
                    {
                        SetLastError(NO_ERROR);
                        cchRet = WideCharToMultiByte(CP_ACP, 0, pwcSrc, (int)cwcSrc, pszBuf, (int)cbBuf - 1, NULL, NULL);
                        if (cchRet)
                        {
                            kHlpAssert(cchRet < (KSSIZE)cbBuf);
                            pszBuf[cchRet] = '\0';
                        }
                        else
                        {
                            kwErrPrintf("WideCharToMultiByte(,,%*.*ls,,) -> dwErr=%d\n", cwcSrc, cwcSrc, pwcSrc, GetLastError());
                            kHlpFree(pszBuf);
                            pszBuf = NULL;
                        }
                    }
                }
                else
                {
                    kwErrPrintf("WideCharToMultiByte(,,%*.*ls,,NULL,0) -> dwErr=%d\n", cwcSrc, cwcSrc, pwcSrc, GetLastError());
                    pszBuf = NULL;
                }
            }
        }
        else
            pszBuf[0] = '\0';
    }
    SetLastError(dwErrSaved);
    return pszBuf;
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
            kHlpPageFree(pMod->u.Manual.pbCopy, pMod->cbImage);
            kHlpPageFree(pMod->u.Manual.pbLoad, pMod->cbImage);
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
    KSIZE const                 cbImage = (KSIZE)kLdrModSize(pMod->pLdrMod);
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
        kHlpAssertReturnVoid((KU32)pMzHdr->e_lfanew <= cbImage - sizeof(*pNtHdrs));
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
 * Creates a module from a native kLdr module handle.
 *
 * @returns Module w/ 1 reference on success, NULL on failure.
 * @param   pLdrMod             The native kLdr module.
 * @param   pszPath             The normalized path to the module.
 * @param   cbPath              The module path length with terminator.
 * @param   uHashPath           The module path hash.
 * @param   fDoReplacements     Whether to do import replacements on this
 *                              module.
 */
static PKWMODULE kwLdrModuleCreateForNativekLdrModule(PKLDRMOD pLdrMod, const char *pszPath, KSIZE cbPath, KU32 uHashPath,
                                                      KBOOL fDoReplacements)
{
    /*
     * Create the entry.
     */
    PKWMODULE pMod   = (PKWMODULE)kHlpAllocZ(sizeof(*pMod) + cbPath + cbPath * 2 * sizeof(wchar_t));
    if (pMod)
    {
        pMod->pwszPath      = (wchar_t *)(pMod + 1);
        kwStrToUtf16(pszPath, (wchar_t *)pMod->pwszPath, cbPath * 2);
        pMod->pszPath       = (char *)kHlpMemCopy((char *)&pMod->pwszPath[cbPath * 2], pszPath, cbPath);
        pMod->uHashPath     = uHashPath;
        pMod->cRefs         = 1;
        pMod->offFilename   = (KU16)(kHlpGetFilename(pszPath) - pszPath);
        pMod->fExe          = K_FALSE;
        pMod->fNative       = K_TRUE;
        pMod->pLdrMod       = pLdrMod;
        pMod->hOurMod       = (HMODULE)(KUPTR)pLdrMod->aSegments[0].MapAddress;
        pMod->cbImage       = (KSIZE)kLdrModSize(pLdrMod);

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
    return NULL;
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
        PKWMODULE pMod = kwLdrModuleCreateForNativekLdrModule(pLdrMod, pszPath, kHlpStrLen(pszPath) + 1,
                                                              uHashPath, fDoReplacements);
        if (pMod)
            return pMod;
        kLdrModClose(pLdrMod);
    }
    return NULL;
}


/**
 * Sets up the quick zero & copy tables for the non-native module.
 *
 * This is a worker for kwLdrModuleCreateNonNative.
 *
 * @param   pMod                The module.
 */
static void kwLdrModuleCreateNonNativeSetupQuickZeroAndCopy(PKWMODULE pMod)
{
    PCKLDRSEG   paSegs = pMod->pLdrMod->aSegments;
    KU32        cSegs  = pMod->pLdrMod->cSegments;
    KU32        iSeg;

    KWLDR_LOG(("Setting up quick zero & copy for %s:\n", pMod->pszPath));
    pMod->u.Manual.cQuickCopyChunks = 0;
    pMod->u.Manual.cQuickZeroChunks = 0;

    for (iSeg = 0; iSeg < cSegs; iSeg++)
        switch (paSegs[iSeg].enmProt)
        {
            case KPROT_READWRITE:
            case KPROT_WRITECOPY:
            case KPROT_EXECUTE_READWRITE:
            case KPROT_EXECUTE_WRITECOPY:
                if (paSegs[iSeg].cbMapped)
                {
                    KU32 iChunk = pMod->u.Manual.cQuickCopyChunks;
                    if (iChunk < K_ELEMENTS(pMod->u.Manual.aQuickCopyChunks))
                    {
                        /*
                         * Check for trailing zero words.
                         */
                        KSIZE cbTrailingZeros;
                        if (   paSegs[iSeg].cbMapped >= 64 * 2 * sizeof(KSIZE)
                            && (paSegs[iSeg].cbMapped & 7) == 0
                            && pMod->u.Manual.cQuickZeroChunks < K_ELEMENTS(pMod->u.Manual.aQuickZeroChunks) )
                        {
                            KSIZE const *pauNatural   = (KSIZE const *)&pMod->u.Manual.pbCopy[(KSIZE)paSegs[iSeg].RVA];
                            KSIZE        cNatural     = paSegs[iSeg].cbMapped / sizeof(KSIZE);
                            KSIZE        idxFirstZero = cNatural;
                            while (idxFirstZero > 0)
                                if (pauNatural[--idxFirstZero] == 0)
                                { /* likely */ }
                                else
                                {
                                    idxFirstZero++;
                                    break;
                                }
                            cbTrailingZeros = (cNatural - idxFirstZero) * sizeof(KSIZE);
                            if (cbTrailingZeros < 128)
                                cbTrailingZeros = 0;
                        }
                        else
                            cbTrailingZeros = 0;

                        /*
                         * Add quick copy entry.
                         */
                        if (cbTrailingZeros < paSegs[iSeg].cbMapped)
                        {
                            pMod->u.Manual.aQuickCopyChunks[iChunk].pbDst    = &pMod->u.Manual.pbLoad[(KSIZE)paSegs[iSeg].RVA];
                            pMod->u.Manual.aQuickCopyChunks[iChunk].pbSrc    = &pMod->u.Manual.pbCopy[(KSIZE)paSegs[iSeg].RVA];
                            pMod->u.Manual.aQuickCopyChunks[iChunk].cbToCopy = paSegs[iSeg].cbMapped - cbTrailingZeros;
                            pMod->u.Manual.cQuickCopyChunks = iChunk + 1;
                            KWLDR_LOG(("aQuickCopyChunks[%u]: %#p LB %#" KSIZE_PRI " <- %p (%*.*s)\n", iChunk,
                                       pMod->u.Manual.aQuickCopyChunks[iChunk].pbDst,
                                       pMod->u.Manual.aQuickCopyChunks[iChunk].cbToCopy,
                                       pMod->u.Manual.aQuickCopyChunks[iChunk].pbSrc,
                                       paSegs[iSeg].cchName, paSegs[iSeg].cchName, paSegs[iSeg].pchName));
                        }

                        /*
                         * Add quick zero entry.
                         */
                        if (cbTrailingZeros)
                        {
                            KU32 iZero = pMod->u.Manual.cQuickZeroChunks;
                            pMod->u.Manual.aQuickZeroChunks[iZero].pbDst    = pMod->u.Manual.aQuickCopyChunks[iChunk].pbDst
                                                                            + pMod->u.Manual.aQuickCopyChunks[iChunk].cbToCopy;
                            pMod->u.Manual.aQuickZeroChunks[iZero].cbToZero = cbTrailingZeros;
                            pMod->u.Manual.cQuickZeroChunks = iZero + 1;
                            KWLDR_LOG(("aQuickZeroChunks[%u]: %#p LB %#" KSIZE_PRI " <- zero (%*.*s)\n", iZero,
                                       pMod->u.Manual.aQuickZeroChunks[iZero].pbDst,
                                       pMod->u.Manual.aQuickZeroChunks[iZero].cbToZero,
                                       paSegs[iSeg].cchName, paSegs[iSeg].cchName, paSegs[iSeg].pchName));
                        }
                    }
                    else
                    {
                        /*
                         * We're out of quick copy table entries, so just copy the whole darn thing.
                         * We cannot 104% guarantee that the segments are in mapping order, so this is simpler.
                         */
                        kHlpAssertFailed();
                        pMod->u.Manual.aQuickCopyChunks[0].pbDst    = pMod->u.Manual.pbLoad;
                        pMod->u.Manual.aQuickCopyChunks[0].pbSrc    = pMod->u.Manual.pbCopy;
                        pMod->u.Manual.aQuickCopyChunks[0].cbToCopy = pMod->cbImage;
                        pMod->u.Manual.cQuickCopyChunks = 1;
                        KWLDR_LOG(("Quick copy not possible!\n"));
                        return;
                    }
                }
                break;

            default:
                break;
        }
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
#if defined(KBUILD_OS_WINDOWS) && defined(KBUILD_ARCH_AMD64)
                    pMod->u.Manual.fRegisteredFunctionTable = K_FALSE;
#endif
                    pMod->u.Manual.fUseLdBuf = K_FALSE;
                    pMod->u.Manual.fCanDoQuick = K_FALSE;
                    pMod->u.Manual.cQuickZeroChunks = 0;
                    pMod->u.Manual.cQuickCopyChunks = 0;
                    pMod->pszPath       = (char *)kHlpMemCopy(&pMod->u.Manual.apImpMods[cImports + 1], pszPath, cbPath);
                    pMod->pwszPath      = (wchar_t *)(pMod->pszPath + cbPath + (cbPath & 1));
                    kwStrToUtf16(pMod->pszPath, (wchar_t *)pMod->pwszPath, cbPath * 2);

                    /*
                     * Figure out where to load it and get memory there.
                     */
                    fFixed = pLdrMod->enmType == KLDRTYPE_EXECUTABLE_FIXED
                          || pLdrMod->enmType == KLDRTYPE_SHARED_LIBRARY_FIXED;
                    pMod->u.Manual.pbLoad = fFixed ? (KU8 *)(KUPTR)pLdrMod->aSegments[0].LinkAddress : NULL;
                    pMod->cbImage = (KSIZE)kLdrModSize(pLdrMod);
                    if (   !fFixed
                        || pLdrMod->enmType != KLDRTYPE_EXECUTABLE_FIXED /* only allow fixed executables */
                        || (KUPTR)pMod->u.Manual.pbLoad - (KUPTR)g_abDefLdBuf >= sizeof(g_abDefLdBuf)
                        || sizeof(g_abDefLdBuf) - (KUPTR)pMod->u.Manual.pbLoad - (KUPTR)g_abDefLdBuf < pMod->cbImage)
                        rc = kHlpPageAlloc((void **)&pMod->u.Manual.pbLoad, pMod->cbImage, KPROT_EXECUTE_READWRITE, fFixed);
                    else
                        pMod->u.Manual.fUseLdBuf = K_TRUE;
                    if (rc == 0)
                    {
                        rc = kHlpPageAlloc(&pMod->u.Manual.pbCopy, pMod->cbImage, KPROT_READWRITE, K_FALSE);
                        if (rc == 0)
                        {
                            KI32 iImp;

                            /*
                             * Link the module (unless it's an executable image) and process the imports.
                             */
                            pMod->hOurMod = (HMODULE)pMod->u.Manual.pbLoad;
                            if (!fExe)
                                kwLdrModuleLink(pMod);
                            KW_LOG(("New module: %p LB %#010x %s (kLdr)\n",
                                    pMod->u.Manual.pbLoad, pMod->cbImage, pMod->pszPath));
                            kwDebuggerPrintf("TODO: .reload /f %s=%p\n", pMod->pszPath, pMod->u.Manual.pbLoad);

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
                                rc = kLdrModGetBits(pLdrMod, pMod->u.Manual.pbCopy, (KUPTR)pMod->u.Manual.pbLoad,
                                                    kwLdrModuleGetImportCallback, pMod);
                                if (rc == 0)
                                {
#if defined(KBUILD_OS_WINDOWS) && defined(KBUILD_ARCH_AMD64)
                                    /*
                                     * Find the function table.  No validation here because the
                                     * loader did that already, right...
                                     */
                                    KU8                        *pbImg = (KU8 *)pMod->u.Manual.pbCopy;
                                    IMAGE_NT_HEADERS const     *pNtHdrs;
                                    IMAGE_DATA_DIRECTORY const *pXcptDir;
                                    if (((PIMAGE_DOS_HEADER)pbImg)->e_magic == IMAGE_DOS_SIGNATURE)
                                        pNtHdrs = (PIMAGE_NT_HEADERS)&pbImg[((PIMAGE_DOS_HEADER)pbImg)->e_lfanew];
                                    else
                                        pNtHdrs = (PIMAGE_NT_HEADERS)pbImg;
                                    pXcptDir = &pNtHdrs->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
                                    kHlpAssert(pNtHdrs->Signature == IMAGE_NT_SIGNATURE);
                                    if (pXcptDir->Size > 0)
                                    {
                                        pMod->u.Manual.cFunctions  = pXcptDir->Size / sizeof(pMod->u.Manual.paFunctions[0]);
                                        kHlpAssert(   pMod->u.Manual.cFunctions * sizeof(pMod->u.Manual.paFunctions[0])
                                                   == pXcptDir->Size);
                                        pMod->u.Manual.paFunctions = (PRUNTIME_FUNCTION)&pbImg[pXcptDir->VirtualAddress];
                                    }
                                    else
                                    {
                                        pMod->u.Manual.cFunctions  = 0;
                                        pMod->u.Manual.paFunctions = NULL;
                                    }
#endif

                                    kwLdrModuleCreateNonNativeSetupQuickZeroAndCopy(pMod);

                                    /*
                                     * Final finish.
                                     */
                                    pMod->u.Manual.pvBits = pMod->u.Manual.pbCopy;
                                    pMod->u.Manual.enmState = KWMODSTATE_NEEDS_BITS;
                                    return pMod;
                                }
                            }

                            kwLdrModuleRelease(pMod);
                            return NULL;
                        }

                        kHlpPageFree(pMod->u.Manual.pbLoad, pMod->cbImage);
                        kwErrPrintf("Failed to allocate %#x bytes\n", pMod->cbImage);
                    }
                    else if (fFixed)
                        kwErrPrintf("Failed to allocate %#x bytes at %p\n",
                                    pMod->cbImage, (void *)(KUPTR)pLdrMod->aSegments[0].LinkAddress);
                    else
                        kwErrPrintf("Failed to allocate %#x bytes\n", pMod->cbImage);
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
        rc = kLdrModQuerySymbol(pImpMod->pLdrMod, pImpMod->u.Manual.pvBits, (KUPTR)pImpMod->u.Manual.pbLoad,
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
                    if (   pCurMod->fExe
                        || !g_aSandboxReplacements[i].fOnlyExe)
                    {
                        KW_LOG(("replacing %s!%s\n",&pImpMod->pszPath[pImpMod->offFilename], g_aSandboxReplacements[i].pszFunction));
                        *puValue = g_aSandboxReplacements[i].pfnReplacement;
                    }
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
    int rc = kLdrModQueryMainEntrypoint(pMod->pLdrMod, pMod->u.Manual.pvBits, (KUPTR)pMod->u.Manual.pbLoad, &uLdrAddrMain);
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
 * Lazily initializes the g_pWinSys32 variable.
 */
static PKFSDIR kwLdrResolveWinSys32(void)
{
    KFSLOOKUPERROR  enmError;
    PKFSDIR         pWinSys32;

    /* Get the path first. */
    char            szSystem32[MAX_PATH];
    if (GetSystemDirectoryA(szSystem32, sizeof(szSystem32)) >= sizeof(szSystem32))
    {
        kwErrPrintf("GetSystemDirectory failed: %u\n", GetLastError());
        strcpy(szSystem32, "C:\\Windows\\System32");
    }

    /* Look it up and verify it. */
    pWinSys32 = (PKFSDIR)kFsCacheLookupA(g_pFsCache, szSystem32, &enmError);
    if (pWinSys32)
    {
        if (pWinSys32->Obj.bObjType == KFSOBJ_TYPE_DIR)
        {
            g_pWinSys32 = pWinSys32;
            return pWinSys32;
        }

        kwErrPrintf("System directory '%s' isn't of 'DIR' type: %u\n", szSystem32, g_pWinSys32->Obj.bObjType);
    }
    else
        kwErrPrintf("Failed to lookup system directory '%s': %u\n", szSystem32, enmError);
    return NULL;
}


/**
 * Whether we can load this DLL natively or not.
 *
 * @returns K_TRUE/K_FALSE.
 * @param   pszFilename         The filename (no path).
 * @param   enmLocation         The location.
 * @param   pszFullPath         The full filename and path.
 */
static KBOOL kwLdrModuleCanLoadNatively(const char *pszFilename, KWLOCATION enmLocation, const char *pszFullPath)
{
    if (enmLocation == KWLOCATION_SYSTEM32)
        return K_TRUE;
    if (enmLocation == KWLOCATION_UNKNOWN_NATIVE)
        return K_TRUE;

    /* If the location is unknown, we must check if it's some dynamic loading
       of a SYSTEM32 DLL with a full path.  We do not want to load these ourselves! */
    if (enmLocation == KWLOCATION_UNKNOWN)
    {
        PKFSDIR pWinSys32 = g_pWinSys32;
        if (!pWinSys32)
            pWinSys32 = kwLdrResolveWinSys32();
        if (pWinSys32)
        {
            KFSLOOKUPERROR enmError;
            PKFSOBJ pFsObj = kFsCacheLookupA(g_pFsCache, pszFullPath, &enmError);
            if (pFsObj)
            {
                KBOOL fInWinSys32 = pFsObj->pParent == pWinSys32;
                kFsCacheObjRelease(g_pFsCache, pFsObj);
                if (fInWinSys32)
                    return K_TRUE;
            }
        }
    }

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
            if (kwLdrModuleCanLoadNatively(pszName, enmLocation, szNormPath))
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
    char       *psz;
    PKWMODULE   pMod = NULL;
    KBOOL       fNeedSuffix = *kHlpGetExt(pszName) == '\0' && kHlpGetFilename(pszName) == pszName;
    KSIZE       cchSuffix   = fNeedSuffix ? 4 : 0;


    /* The import path. */
    if (pMod == NULL && pImporter != NULL)
    {
        if (pImporter->offFilename + cchName + cchSuffix >= sizeof(szPath))
            return KERR_BUFFER_OVERFLOW;

        psz = (char *)kHlpMemPCopy(kHlpMemPCopy(szPath, pImporter->pszPath, pImporter->offFilename), pszName, cchName + 1);
        if (fNeedSuffix)
            kHlpMemCopy(psz - 1, ".dll", sizeof(".dll"));
        pMod = kwLdrModuleTryLoadDll(szPath, KWLOCATION_IMPORTER_DIR, pExe);
    }

    /* Application directory first. */
    if (pMod == NULL && pExe != NULL && pExe != pImporter)
    {
        if (pExe->offFilename + cchName + cchSuffix >= sizeof(szPath))
            return KERR_BUFFER_OVERFLOW;
        psz = (char *)kHlpMemPCopy(kHlpMemPCopy(szPath, pExe->pszPath, pExe->offFilename), pszName, cchName + 1);
        if (fNeedSuffix)
            kHlpMemCopy(psz - 1, ".dll", sizeof(".dll"));
        pMod = kwLdrModuleTryLoadDll(szPath, KWLOCATION_EXE_DIR, pExe);
    }

    /* The windows directory. */
    if (pMod == NULL)
    {
        UINT cchDir = GetSystemDirectoryA(szPath, sizeof(szPath));
        if (   cchDir <= 2
            || cchDir + 1 + cchName + cchSuffix >= sizeof(szPath))
            return KERR_BUFFER_OVERFLOW;
        szPath[cchDir++] = '\\';
        psz = (char *)kHlpMemPCopy(&szPath[cchDir], pszName, cchName + 1);
        if (fNeedSuffix)
            kHlpMemCopy(psz - 1, ".dll", sizeof(".dll"));
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
        /*
         * Need to copy bits?
         */
        if (pMod->u.Manual.enmState == KWMODSTATE_NEEDS_BITS)
        {
            if (pMod->u.Manual.fUseLdBuf)
            {
#if defined(KBUILD_OS_WINDOWS) && defined(KBUILD_ARCH_AMD64)
                if (g_pModInLdBuf != NULL && g_pModInLdBuf != pMod && pMod->u.Manual.fRegisteredFunctionTable)
                {
                    BOOLEAN fRc = RtlDeleteFunctionTable(pMod->u.Manual.paFunctions);
                    kHlpAssert(fRc); K_NOREF(fRc);
                }
#endif
                g_pModPrevInLdBuf = g_pModInLdBuf;
                g_pModInLdBuf = pMod;
            }

            /* Do quick zeroing and copying when we can. */
            pMod->u.Manual.fCanDoQuick = K_FALSE;
            if (   pMod->u.Manual.fCanDoQuick
                && (   !pMod->u.Manual.fUseLdBuf
                    || g_pModPrevInLdBuf == pMod))
            {
                /* Zero first. */
                kHlpAssert(pMod->u.Manual.cQuickZeroChunks <= 3);
                switch (pMod->u.Manual.cQuickZeroChunks)
                {
                    case 3: kHlpMemSet(pMod->u.Manual.aQuickZeroChunks[2].pbDst, 0, pMod->u.Manual.aQuickZeroChunks[2].cbToZero);
                    case 2: kHlpMemSet(pMod->u.Manual.aQuickZeroChunks[1].pbDst, 0, pMod->u.Manual.aQuickZeroChunks[1].cbToZero);
                    case 1: kHlpMemSet(pMod->u.Manual.aQuickZeroChunks[0].pbDst, 0, pMod->u.Manual.aQuickZeroChunks[0].cbToZero);
                    case 0: break;
                }

                /* Then copy. */
                kHlpAssert(pMod->u.Manual.cQuickCopyChunks > 0);
                kHlpAssert(pMod->u.Manual.cQuickCopyChunks <= 3);
                switch (pMod->u.Manual.cQuickCopyChunks)
                {
                    case 3: kHlpMemCopy(pMod->u.Manual.aQuickCopyChunks[2].pbDst, pMod->u.Manual.aQuickCopyChunks[2].pbSrc,
                                        pMod->u.Manual.aQuickCopyChunks[2].cbToCopy);
                    case 2: kHlpMemCopy(pMod->u.Manual.aQuickCopyChunks[1].pbDst, pMod->u.Manual.aQuickCopyChunks[1].pbSrc,
                                        pMod->u.Manual.aQuickCopyChunks[1].cbToCopy);
                    case 1: kHlpMemCopy(pMod->u.Manual.aQuickCopyChunks[0].pbDst, pMod->u.Manual.aQuickCopyChunks[0].pbSrc,
                                        pMod->u.Manual.aQuickCopyChunks[0].cbToCopy);
                    case 0: break;
                }
            }
            /* Must copy the whole image. */
            else
            {
                kHlpMemCopy(pMod->u.Manual.pbLoad, pMod->u.Manual.pbCopy, pMod->cbImage);
                pMod->u.Manual.fCanDoQuick = K_TRUE;
            }
            pMod->u.Manual.enmState = KWMODSTATE_NEEDS_INIT;
        }

#if defined(KBUILD_OS_WINDOWS) && defined(KBUILD_ARCH_AMD64)
        /*
         * Need to register function table?
         */
        if (   !pMod->u.Manual.fRegisteredFunctionTable
            && pMod->u.Manual.cFunctions > 0)
        {
            pMod->u.Manual.fRegisteredFunctionTable = RtlAddFunctionTable(pMod->u.Manual.paFunctions,
                                                                          pMod->u.Manual.cFunctions,
                                                                          (KUPTR)pMod->u.Manual.pbLoad) != FALSE;
            kHlpAssert(pMod->u.Manual.fRegisteredFunctionTable);
        }
#endif

        if (pMod->u.Manual.enmState == KWMODSTATE_NEEDS_INIT)
        {
            /*
             * Must do imports first, but mark our module as being initialized to avoid
             * endless recursion should there be a dependency loop.
             */
            KSIZE iImp;
            pMod->u.Manual.enmState = KWMODSTATE_BEING_INITED;

            for (iImp = 0; iImp < pMod->u.Manual.cImpMods; iImp++)
            {
                rc = kwLdrModuleInitTree(pMod->u.Manual.apImpMods[iImp]);
                if (rc != 0)
                    return rc;
            }

            rc = kLdrModCallInit(pMod->pLdrMod, pMod->u.Manual.pbLoad, (KUPTR)pMod->hOurMod);
            if (rc == 0)
                pMod->u.Manual.enmState = KWMODSTATE_READY;
            else
                pMod->u.Manual.enmState = KWMODSTATE_INIT_FAILED;
        }
    }
    return rc;
}


/**
 * Looks up a module handle for a tool.
 *
 * @returns Referenced loader module on success, NULL on if not found.
 * @param   pTool               The tool.
 * @param   hmod                The module handle.
 */
static PKWMODULE kwToolLocateModuleByHandle(PKWTOOL pTool, HMODULE hmod)
{
    KUPTR const     uHMod = (KUPTR)hmod;
    PKWMODULE      *papMods;
    KU32            iEnd;
    KU32            i;
    PKWDYNLOAD      pDynLoad;

    /* The executable. */
    if (   hmod == NULL
        || pTool->u.Sandboxed.pExe->hOurMod == hmod)
        return kwLdrModuleRetain(pTool->u.Sandboxed.pExe);

    /*
     * Binary lookup using the module table.
     */
    papMods = pTool->u.Sandboxed.papModules;
    iEnd    = pTool->u.Sandboxed.cModules;
    if (iEnd)
    {
        KU32 iStart  = 0;
        i = iEnd / 2;
        for (;;)
        {
            KUPTR const uHModThis = (KUPTR)papMods[i]->hOurMod;
            if (uHMod < uHModThis)
            {
                iEnd = i--;
                if (iStart <= i)
                { }
                else
                    break;
            }
            else if (uHMod != uHModThis)
            {
                iStart = ++i;
                if (i < iEnd)
                { }
                else
                    break;
            }
            else
                return kwLdrModuleRetain(papMods[i]);

            i = iStart + (iEnd - iStart) / 2;
        }

#ifndef NDEBUG
        iStart = pTool->u.Sandboxed.cModules;
        while (--iStart > 0)
            kHlpAssert((KUPTR)papMods[iStart]->hOurMod != uHMod);
        kHlpAssert(i == 0 || (KUPTR)papMods[i - 1]->hOurMod < uHMod);
        kHlpAssert(i == pTool->u.Sandboxed.cModules || (KUPTR)papMods[i]->hOurMod > uHMod);
#endif
    }

    /*
     * Dynamically loaded images.
     */
    for (pDynLoad = pTool->u.Sandboxed.pDynLoadHead; pDynLoad != NULL; pDynLoad = pDynLoad->pNext)
        if (pDynLoad->hmod == hmod)
        {
            if (pDynLoad->pMod)
                return kwLdrModuleRetain(pDynLoad->pMod);
            KWFS_TODO();
            return NULL;
        }

    return NULL;
}

/**
 * Adds the given module to the tool import table.
 *
 * @returns 0 on success, non-zero on failure.
 * @param   pTool               The tool.
 * @param   pMod                The module.
 */
static int kwToolAddModule(PKWTOOL pTool, PKWMODULE pMod)
{
    /*
     * Binary lookup. Locating the right slot for it, return if already there.
     */
    KUPTR const     uHMod   = (KUPTR)pMod->hOurMod;
    PKWMODULE      *papMods = pTool->u.Sandboxed.papModules;
    KU32            iEnd    = pTool->u.Sandboxed.cModules;
    KU32            i;
    if (iEnd)
    {
        KU32        iStart  = 0;
        i = iEnd / 2;
        for (;;)
        {
            KUPTR const uHModThis = (KUPTR)papMods[i]->hOurMod;
            if (uHMod < uHModThis)
            {
                iEnd = i;
                if (iStart < i)
                { }
                else
                    break;
            }
            else if (uHMod != uHModThis)
            {
                iStart = ++i;
                if (i < iEnd)
                { }
                else
                    break;
            }
            else
            {
                /* Already there in the table. */
                return 0;
            }

            i = iStart + (iEnd - iStart) / 2;
        }
#ifndef NDEBUG
        iStart = pTool->u.Sandboxed.cModules;
        while (--iStart > 0)
        {
            kHlpAssert(papMods[iStart] != pMod);
            kHlpAssert((KUPTR)papMods[iStart]->hOurMod != uHMod);
        }
        kHlpAssert(i == 0 || (KUPTR)papMods[i - 1]->hOurMod < uHMod);
        kHlpAssert(i == pTool->u.Sandboxed.cModules || (KUPTR)papMods[i]->hOurMod > uHMod);
#endif
    }
    else
        i = 0;

    /*
     * Grow the table?
     */
    if ((pTool->u.Sandboxed.cModules % 16) == 0)
    {
        void *pvNew = kHlpRealloc(papMods, sizeof(papMods[0]) * (pTool->u.Sandboxed.cModules + 16));
        if (!pvNew)
            return KERR_NO_MEMORY;
        pTool->u.Sandboxed.papModules = papMods = (PKWMODULE *)pvNew;
    }

    /* Insert it. */
    if (i != pTool->u.Sandboxed.cModules)
        kHlpMemMove(&papMods[i + 1], &papMods[i], (pTool->u.Sandboxed.cModules - i) * sizeof(papMods[0]));
    papMods[i] = kwLdrModuleRetain(pMod);
    pTool->u.Sandboxed.cModules++;
    KW_LOG(("kwToolAddModule: %u modules after adding %p=%s\n", pTool->u.Sandboxed.cModules, uHMod, pMod->pszPath));
    return 0;
}


/**
 * Adds the given module and all its imports to the
 *
 * @returns 0 on success, non-zero on failure.
 * @param   pTool               The tool.
 * @param   pMod                The module.
 */
static int kwToolAddModuleAndImports(PKWTOOL pTool, PKWMODULE pMod)
{
    int rc = kwToolAddModule(pTool, pMod);
    if (!pMod->fNative && rc == 0)
    {
        KSIZE iImp = pMod->u.Manual.cImpMods;
        while (iImp-- > 0)
        {
            rc = kwToolAddModuleAndImports(pTool, pMod->u.Manual.apImpMods[iImp]);
            if (rc == 0)
            { }
            else
                break;
        }
    }
    return 0;
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
        if (pTool->u.Sandboxed.pExe)
        {
            int rc = kwLdrModuleQueryMainEntrypoint(pTool->u.Sandboxed.pExe, &pTool->u.Sandboxed.uMainAddr);
            if (rc == 0)
            {
                if (kHlpStrICompAscii(pToolFsObj->pszName, "cl.exe") == 0)
                    pTool->u.Sandboxed.enmHint = KWTOOLHINT_VISUAL_CPP_CL;
                else if (kHlpStrICompAscii(pToolFsObj->pszName, "link.exe") == 0)
                    pTool->u.Sandboxed.enmHint = KWTOOLHINT_VISUAL_CPP_LINK;
                else
                    pTool->u.Sandboxed.enmHint = KWTOOLHINT_NONE;
                kwToolAddModuleAndImports(pTool, pTool->u.Sandboxed.pExe);
            }
            else
            {
                kwErrPrintf("Failed to get entrypoint for '%s': %u\n", pTool->pszPath, rc);
                kwLdrModuleRelease(pTool->u.Sandboxed.pExe);
                pTool->u.Sandboxed.pExe = NULL;
                pTool->enmType = KWTOOLTYPE_EXEC;
            }
        }
        else
            pTool->enmType = KWTOOLTYPE_EXEC;

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
 * This is for kDep.
 */
int kwFsPathExists(const char *pszPath)
{
    BirdTimeSpec_T  TsIgnored;
    KFSLOOKUPERROR  enmError;
    PKFSOBJ         pFsObj = kFsCacheLookupNoMissingA(g_pFsCache, pszPath, &enmError);
    if (pFsObj)
    {
        kFsCacheObjRelease(g_pFsCache, pFsObj);
        return 1;
    }
    return birdStatModTimeOnly(pszPath, &TsIgnored, 1) == 0;
}


/* duplicated in dir-nt-bird.c */
void nt_fullpath_cached(const char *pszPath, char *pszFull, size_t cbFull)
{
    KFSLOOKUPERROR  enmError;
    PKFSOBJ         pPathObj = kFsCacheLookupA(g_pFsCache, pszPath, &enmError);
    if (pPathObj)
    {
        KSIZE off = pPathObj->cchParent;
        if (off > 0)
        {
            KSIZE offEnd = off + pPathObj->cchName;
            if (offEnd < cbFull)
            {
                PKFSDIR pAncestor;

                pszFull[off + pPathObj->cchName] = '\0';
                memcpy(&pszFull[off], pPathObj->pszName, pPathObj->cchName);

                for (pAncestor = pPathObj->pParent; off > 0; pAncestor = pAncestor->Obj.pParent)
                {
                    kHlpAssert(off > 1);
                    kHlpAssert(pAncestor != NULL);
                    kHlpAssert(pAncestor->Obj.cchName > 0);
                    pszFull[--off] = '/';
                    off -= pAncestor->Obj.cchName;
                    kHlpAssert(pAncestor->Obj.cchParent == off);
                    memcpy(&pszFull[off], pAncestor->Obj.pszName, pAncestor->Obj.cchName);
                }
                kFsCacheObjRelease(g_pFsCache, pPathObj);
                return;
            }
        }
        else
        {
            if ((size_t)pPathObj->cchName + 1 < cbFull)
            {
                memcpy(pszFull, pPathObj->pszName, pPathObj->cchName);
                pszFull[pPathObj->cchName] = '/';
                pszFull[pPathObj->cchName + 1] = '\0';

                kFsCacheObjRelease(g_pFsCache, pPathObj);
                return;
            }
        }

        /* do fallback. */
        kHlpAssertFailed();
        kFsCacheObjRelease(g_pFsCache, pPathObj);
    }

    nt_fullpath(pszPath, pszFull, cbFull);
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

/** Common worker for ExitProcess(), exit() and friends.  */
static void WINAPI kwSandboxDoExit(int uExitCode)
{
    if (g_Sandbox.idMainThread == GetCurrentThreadId())
    {
        PNT_TIB pTib = (PNT_TIB)NtCurrentTeb();

        g_Sandbox.rcExitCode = (int)uExitCode;

        /* Before we jump, restore the TIB as we're not interested in any
           exception chain stuff installed by the sandboxed executable. */
        *pTib = g_Sandbox.TibMainThread;
        pTib->ExceptionList = g_Sandbox.pOutXcptListHead;

        longjmp(g_Sandbox.JmpBuf, 1);
    }
    KWFS_TODO();
}


/** ExitProcess replacement.  */
static void WINAPI kwSandbox_Kernel32_ExitProcess(UINT uExitCode)
{
    KW_LOG(("kwSandbox_Kernel32_ExitProcess: %u\n", uExitCode));
    kwSandboxDoExit((int)uExitCode);
}


/** ExitProcess replacement.  */
static BOOL WINAPI kwSandbox_Kernel32_TerminateProcess(HANDLE hProcess, UINT uExitCode)
{
    if (hProcess == GetCurrentProcess())
        kwSandboxDoExit(uExitCode);
    KWFS_TODO();
    return TerminateProcess(hProcess, uExitCode);
}


/** Normal CRT exit(). */
static void __cdecl kwSandbox_msvcrt_exit(int rcExitCode)
{
    KW_LOG(("kwSandbox_msvcrt_exit: %d\n", rcExitCode));
    kwSandboxDoExit(rcExitCode);
}


/** Quick CRT _exit(). */
static void __cdecl kwSandbox_msvcrt__exit(int rcExitCode)
{
    /* Quick. */
    KW_LOG(("kwSandbox_msvcrt__exit %d\n", rcExitCode));
    kwSandboxDoExit(rcExitCode);
}


/** Return to caller CRT _cexit(). */
static void __cdecl kwSandbox_msvcrt__cexit(int rcExitCode)
{
    KW_LOG(("kwSandbox_msvcrt__cexit: %d\n", rcExitCode));
    kwSandboxDoExit(rcExitCode);
}


/** Quick return to caller CRT _c_exit(). */
static void __cdecl kwSandbox_msvcrt__c_exit(int rcExitCode)
{
    KW_LOG(("kwSandbox_msvcrt__c_exit: %d\n", rcExitCode));
    kwSandboxDoExit(rcExitCode);
}


/** Runtime error and exit _amsg_exit(). */
static void __cdecl kwSandbox_msvcrt__amsg_exit(int iMsgNo)
{
    KW_LOG(("\nRuntime error #%u!\n", iMsgNo));
    kwSandboxDoExit(255);
}


/** CRT - terminate().  */
static void __cdecl kwSandbox_msvcrt_terminate(void)
{
    KW_LOG(("\nRuntime - terminate!\n"));
    kwSandboxDoExit(254);
}


/** CRT - _onexit   */
static _onexit_t __cdecl kwSandbox_msvcrt__onexit(_onexit_t pfnFunc)
{
    //if (g_Sandbox.pTool->u.Sandboxed.enmHint == KWTOOLHINT_VISUAL_CPP_LINK)
    {
        PKWEXITCALLACK pCallback;
        KW_LOG(("_onexit(%p)\n", pfnFunc));
        kHlpAssert(GetCurrentThreadId() == g_Sandbox.idMainThread);

        pCallback = kHlpAlloc(sizeof(*pCallback));
        if (pCallback)
        {
            pCallback->pfnCallback = pfnFunc;
            pCallback->fAtExit     = K_FALSE;
            pCallback->pNext       = g_Sandbox.pExitCallbackHead;
            g_Sandbox.pExitCallbackHead = pCallback;
            return pfnFunc;
        }
        return NULL;
    }
    KW_LOG(("_onexit(%p) - IGNORED\n", pfnFunc));
    return pfnFunc;
}


/** CRT - atexit   */
static int __cdecl kwSandbox_msvcrt_atexit(int (__cdecl *pfnFunc)(void))
{
    //if (g_Sandbox.pTool->u.Sandboxed.enmHint == KWTOOLHINT_VISUAL_CPP_LINK)
    {
        PKWEXITCALLACK pCallback;
        kHlpAssert(GetCurrentThreadId() == g_Sandbox.idMainThread);
        KW_LOG(("atexit(%p)\n", pfnFunc));

        pCallback = kHlpAlloc(sizeof(*pCallback));
        if (pCallback)
        {
            pCallback->pfnCallback = (_onexit_t)pfnFunc;
            pCallback->fAtExit     = K_TRUE;
            pCallback->pNext       = g_Sandbox.pExitCallbackHead;
            g_Sandbox.pExitCallbackHead = pCallback;
            return 0;
        }
        return -1;
    }
    KW_LOG(("atexit(%p) - IGNORED!\n", pfnFunc));
    return 0;
}


/** Kernel32 - SetConsoleCtrlHandler(). */
static BOOL WINAPI kwSandbox_Kernel32_SetConsoleCtrlHandler(PHANDLER_ROUTINE pfnHandler, BOOL fAdd)
{
    KW_LOG(("SetConsoleCtrlHandler(%p, %d) - ignoring\n"));
    return TRUE;
}


/** The CRT internal __getmainargs() API. */
static int __cdecl kwSandbox_msvcrt___getmainargs(int *pargc, char ***pargv, char ***penvp,
                                                  int dowildcard, int const *piNewMode)
{
    kHlpAssert(GetCurrentThreadId() == g_Sandbox.idMainThread);
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
    kHlpAssert(GetCurrentThreadId() == g_Sandbox.idMainThread);
    *pargc = g_Sandbox.cArgs;
    *pargv = g_Sandbox.papwszArgs;
    *penvp = g_Sandbox.wenviron;

    /** @todo startinfo points at a newmode (setmode) value.   */
    return 0;
}



/** Kernel32 - GetCommandLineA()  */
static LPCSTR /*LPSTR*/ WINAPI kwSandbox_Kernel32_GetCommandLineA(VOID)
{
    kHlpAssert(GetCurrentThreadId() == g_Sandbox.idMainThread);
    return g_Sandbox.pszCmdLine;
}


/** Kernel32 - GetCommandLineW()  */
static LPCWSTR /*LPWSTR*/ WINAPI kwSandbox_Kernel32_GetCommandLineW(VOID)
{
    kHlpAssert(GetCurrentThreadId() == g_Sandbox.idMainThread);
    return g_Sandbox.pwszCmdLine;
}


/** Kernel32 - GetStartupInfoA()  */
static VOID WINAPI kwSandbox_Kernel32_GetStartupInfoA(LPSTARTUPINFOA pStartupInfo)
{
    KW_LOG(("GetStartupInfoA\n"));
    GetStartupInfoA(pStartupInfo);
    pStartupInfo->lpReserved  = NULL;
    pStartupInfo->lpTitle     = NULL;
    pStartupInfo->lpReserved2 = NULL;
    pStartupInfo->cbReserved2 = 0;
}


/** Kernel32 - GetStartupInfoW()  */
static VOID WINAPI kwSandbox_Kernel32_GetStartupInfoW(LPSTARTUPINFOW pStartupInfo)
{
    KW_LOG(("GetStartupInfoW\n"));
    GetStartupInfoW(pStartupInfo);
    pStartupInfo->lpReserved  = NULL;
    pStartupInfo->lpTitle     = NULL;
    pStartupInfo->lpReserved2 = NULL;
    pStartupInfo->cbReserved2 = 0;
}


/** CRT - __p___argc().  */
static int * __cdecl kwSandbox_msvcrt___p___argc(void)
{
    kHlpAssert(GetCurrentThreadId() == g_Sandbox.idMainThread);
    return &g_Sandbox.cArgs;
}


/** CRT - __p___argv().  */
static char *** __cdecl kwSandbox_msvcrt___p___argv(void)
{
    kHlpAssert(GetCurrentThreadId() == g_Sandbox.idMainThread);
    return &g_Sandbox.papszArgs;
}


/** CRT - __p___sargv().  */
static wchar_t *** __cdecl kwSandbox_msvcrt___p___wargv(void)
{
    kHlpAssert(GetCurrentThreadId() == g_Sandbox.idMainThread);
    return &g_Sandbox.papwszArgs;
}


/** CRT - __p__acmdln().  */
static char ** __cdecl kwSandbox_msvcrt___p__acmdln(void)
{
    kHlpAssert(GetCurrentThreadId() == g_Sandbox.idMainThread);
    return (char **)&g_Sandbox.pszCmdLine;
}


/** CRT - __p__acmdln().  */
static wchar_t ** __cdecl kwSandbox_msvcrt___p__wcmdln(void)
{
    kHlpAssert(GetCurrentThreadId() == g_Sandbox.idMainThread);
    return &g_Sandbox.pwszCmdLine;
}


/** CRT - __p__pgmptr().  */
static char ** __cdecl kwSandbox_msvcrt___p__pgmptr(void)
{
    kHlpAssert(GetCurrentThreadId() == g_Sandbox.idMainThread);
    return &g_Sandbox.pgmptr;
}


/** CRT - __p__wpgmptr().  */
static wchar_t ** __cdecl kwSandbox_msvcrt___p__wpgmptr(void)
{
    kHlpAssert(GetCurrentThreadId() == g_Sandbox.idMainThread);
    return &g_Sandbox.wpgmptr;
}


/** CRT - _get_pgmptr().  */
static errno_t __cdecl kwSandbox_msvcrt__get_pgmptr(char **ppszValue)
{
    kHlpAssert(GetCurrentThreadId() == g_Sandbox.idMainThread);
    *ppszValue = g_Sandbox.pgmptr;
    return 0;
}


/** CRT - _get_wpgmptr().  */
static errno_t __cdecl kwSandbox_msvcrt__get_wpgmptr(wchar_t **ppwszValue)
{
    kHlpAssert(GetCurrentThreadId() == g_Sandbox.idMainThread);
    *ppwszValue = g_Sandbox.wpgmptr;
    return 0;
}

/** Just in case. */
static void kwSandbox_msvcrt__wincmdln(void)
{
    kHlpAssert(GetCurrentThreadId() == g_Sandbox.idMainThread);
    KWFS_TODO();
}


/** Just in case. */
static void kwSandbox_msvcrt__wwincmdln(void)
{
    kHlpAssert(GetCurrentThreadId() == g_Sandbox.idMainThread);
    KWFS_TODO();
}

/** CreateThread interceptor. */
static HANDLE WINAPI kwSandbox_Kernel32_CreateThread(LPSECURITY_ATTRIBUTES pSecAttr, SIZE_T cbStack,
                                                     PTHREAD_START_ROUTINE pfnThreadProc, PVOID pvUser,
                                                     DWORD fFlags, PDWORD pidThread)
{
    HANDLE hThread = NULL;
    KW_LOG(("CreateThread: pSecAttr=%p (inh=%d) cbStack=%#x pfnThreadProc=%p pvUser=%p fFlags=%#x pidThread=%p\n",
            pSecAttr, pSecAttr ? pSecAttr->bInheritHandle : 0, cbStack, pfnThreadProc, pvUser, fFlags, pidThread));
    kHlpAssert(GetCurrentThreadId() == g_Sandbox.idMainThread);
    if (g_Sandbox.pTool->u.Sandboxed.enmHint == KWTOOLHINT_VISUAL_CPP_LINK)
    {
        /* Allow link::DbgThread. */
        hThread = CreateThread(pSecAttr, cbStack, pfnThreadProc, pvUser, fFlags, pidThread);
        KW_LOG(("CreateThread -> %p, *pidThread=%#x\n", hThread, pidThread ? *pidThread : 0));
    }
    else
        KWFS_TODO();
    return hThread;
}


/** _beginthread - create a new thread. */
static uintptr_t __cdecl kwSandbox_msvcrt__beginthread(void (__cdecl *pfnThreadProc)(void *), unsigned cbStack, void *pvUser)
{
    kHlpAssert(GetCurrentThreadId() == g_Sandbox.idMainThread);
    KWFS_TODO();
    return 0;
}


/** _beginthreadex - create a new thread. */
static uintptr_t __cdecl kwSandbox_msvcrt__beginthreadex(void *pvSecAttr, unsigned cbStack,
                                                         unsigned (__stdcall *pfnThreadProc)(void *), void *pvUser,
                                                         unsigned fCreate, unsigned *pidThread)
{
    kHlpAssert(GetCurrentThreadId() == g_Sandbox.idMainThread);
    KWFS_TODO();
    return 0;
}


/*
 *
 * Environment related APIs.
 * Environment related APIs.
 * Environment related APIs.
 *
 */

/** Kernel32 - GetEnvironmentStringsA (Watcom uses this one). */
static LPCH WINAPI kwSandbox_Kernel32_GetEnvironmentStringsA(void)
{
    char *pszzEnv;
    char *pszCur;
    KSIZE cbNeeded = 1;
    KSIZE iVar = 0;

    kHlpAssert(GetCurrentThreadId() == g_Sandbox.idMainThread);

    /* Figure how space much we need first.  */
    while ((pszCur = g_Sandbox.papszEnvVars[iVar++]) != NULL)
        cbNeeded += kHlpStrLen(pszCur) + 1;

    /* Allocate it. */
    pszzEnv = kHlpAlloc(cbNeeded);
    if (pszzEnv)
    {
        char *psz = pszzEnv;
        iVar = 0;
        while ((pszCur = g_Sandbox.papszEnvVars[iVar++]) != NULL)
        {
            KSIZE cbCur = kHlpStrLen(pszCur) + 1;
            kHlpAssert((KUPTR)(&psz[cbCur] - pszzEnv) < cbNeeded);
            psz = (char *)kHlpMemPCopy(psz, pszCur, cbCur);
        }
        *psz++ = '\0';
        kHlpAssert(psz - pszzEnv == cbNeeded);
    }

    KW_LOG(("GetEnvironmentStringsA -> %p [%u]\n", pszzEnv, cbNeeded));
#if 0
    fprintf(stderr, "GetEnvironmentStringsA: %p LB %#x\n", pszzEnv, cbNeeded);
    pszCur = pszzEnv;
    iVar = 0;
    while (*pszCur)
    {
        fprintf(stderr, "  %u:%p=%s<eos>\n\n", iVar, pszCur, pszCur);
        iVar++;
        pszCur += kHlpStrLen(pszCur) + 1;
    }
    fprintf(stderr, "  %u:%p=<eos>\n\n", iVar, pszCur);
    pszCur++;
    fprintf(stderr, "ended at %p, after %u bytes (exepcted %u)\n", pszCur, pszCur - pszzEnv, cbNeeded);
#endif
    return pszzEnv;
}


/** Kernel32 - GetEnvironmentStrings */
static LPCH WINAPI kwSandbox_Kernel32_GetEnvironmentStrings(void)
{
    KW_LOG(("GetEnvironmentStrings!\n"));
    return kwSandbox_Kernel32_GetEnvironmentStringsA();
}


/** Kernel32 - GetEnvironmentStringsW */
static LPWCH WINAPI kwSandbox_Kernel32_GetEnvironmentStringsW(void)
{
    wchar_t *pwszzEnv;
    wchar_t *pwszCur;
    KSIZE    cwcNeeded = 1;
    KSIZE    iVar = 0;

    kHlpAssert(GetCurrentThreadId() == g_Sandbox.idMainThread);

    /* Figure how space much we need first.  */
    while ((pwszCur = g_Sandbox.papwszEnvVars[iVar++]) != NULL)
        cwcNeeded += kwUtf16Len(pwszCur) + 1;

    /* Allocate it. */
    pwszzEnv = kHlpAlloc(cwcNeeded * sizeof(wchar_t));
    if (pwszzEnv)
    {
        wchar_t *pwsz = pwszzEnv;
        iVar = 0;
        while ((pwszCur = g_Sandbox.papwszEnvVars[iVar++]) != NULL)
        {
            KSIZE cwcCur = kwUtf16Len(pwszCur) + 1;
            kHlpAssert((KUPTR)(&pwsz[cwcCur] - pwszzEnv) < cwcNeeded);
            pwsz = (wchar_t *)kHlpMemPCopy(pwsz, pwszCur, cwcCur * sizeof(wchar_t));
        }
        *pwsz++ = '\0';
        kHlpAssert(pwsz - pwszzEnv == cwcNeeded);
    }

    KW_LOG(("GetEnvironmentStringsW -> %p [%u]\n", pwszzEnv, cwcNeeded));
    return pwszzEnv;
}


/** Kernel32 - FreeEnvironmentStringsA   */
static BOOL WINAPI kwSandbox_Kernel32_FreeEnvironmentStringsA(LPCH pszzEnv)
{
    KW_LOG(("FreeEnvironmentStringsA: %p -> TRUE\n", pszzEnv));
    kHlpAssert(GetCurrentThreadId() == g_Sandbox.idMainThread);
    kHlpFree(pszzEnv);
    return TRUE;
}


/** Kernel32 - FreeEnvironmentStringsW   */
static BOOL WINAPI kwSandbox_Kernel32_FreeEnvironmentStringsW(LPWCH pwszzEnv)
{
    KW_LOG(("FreeEnvironmentStringsW: %p -> TRUE\n", pwszzEnv));
    kHlpAssert(GetCurrentThreadId() == g_Sandbox.idMainThread);
    kHlpFree(pwszzEnv);
    return TRUE;
}


/**
 * Grows the environment vectors (KWSANDBOX::environ, KWSANDBOX::papszEnvVars,
 * KWSANDBOX::wenviron, and KWSANDBOX::papwszEnvVars).
 *
 * @returns 0 on success, non-zero on failure.
 * @param   pSandbox            The sandbox.
 * @param   cMin                Minimum size, including terminator.
 */
static int kwSandboxGrowEnv(PKWSANDBOX pSandbox, KSIZE cMin)
{
    void       *pvNew;
    KSIZE const cOld = pSandbox->cEnvVarsAllocated;
    KSIZE       cNew = cOld + 256;
    while (cNew < cMin)
        cNew += 256;

    pvNew = kHlpRealloc(pSandbox->environ, cNew * sizeof(pSandbox->environ[0]));
    if (pvNew)
    {
        pSandbox->environ = (char **)pvNew;
        pSandbox->environ[cOld] = NULL;

        pvNew = kHlpRealloc(pSandbox->papszEnvVars, cNew * sizeof(pSandbox->papszEnvVars[0]));
        if (pvNew)
        {
            pSandbox->papszEnvVars = (char **)pvNew;
            pSandbox->papszEnvVars[cOld] = NULL;

            pvNew = kHlpRealloc(pSandbox->wenviron, cNew * sizeof(pSandbox->wenviron[0]));
            if (pvNew)
            {
                pSandbox->wenviron = (wchar_t **)pvNew;
                pSandbox->wenviron[cOld] = NULL;

                pvNew = kHlpRealloc(pSandbox->papwszEnvVars, cNew * sizeof(pSandbox->papwszEnvVars[0]));
                if (pvNew)
                {
                    pSandbox->papwszEnvVars = (wchar_t **)pvNew;
                    pSandbox->papwszEnvVars[cOld] = NULL;

                    pSandbox->cEnvVarsAllocated = cNew;
                    KW_LOG(("kwSandboxGrowEnv: cNew=%d - crt: %p / %p; shadow: %p, %p\n",
                            cNew, pSandbox->environ, pSandbox->wenviron, pSandbox->papszEnvVars, pSandbox->papwszEnvVars));
                    return 0;
                }
            }
        }
    }
    kwErrPrintf("kwSandboxGrowEnv ran out of memory! cNew=%u\n", cNew);
    return KERR_NO_MEMORY;
}


/**
 * Sets an environment variable, ANSI style.
 *
 * @returns 0 on success, non-zero on failure.
 * @param   pSandbox            The sandbox.
 * @param   pchVar              The variable name.
 * @param   cchVar              The length of the name.
 * @param   pszValue            The value.
 */
static int kwSandboxDoSetEnvA(PKWSANDBOX pSandbox, const char *pchVar, KSIZE cchVar, const char *pszValue)
{
    /* Allocate and construct the new strings. */
    KSIZE  cchTmp = kHlpStrLen(pszValue);
    char  *pszNew = (char *)kHlpAlloc(cchVar + 1 + cchTmp + 1);
    if (pszNew)
    {
        wchar_t *pwszNew;
        kHlpMemCopy(pszNew, pchVar, cchVar);
        pszNew[cchVar] = '=';
        kHlpMemCopy(&pszNew[cchVar + 1], pszValue, cchTmp);
        cchTmp += cchVar + 1;
        pszNew[cchTmp] = '\0';

        pwszNew = kwStrToUtf16AllocN(pszNew, cchTmp);
        if (pwszNew)
        {
            /* Look it up. */
            KSIZE   iVar = 0;
            char   *pszEnv;
            while ((pszEnv = pSandbox->papszEnvVars[iVar]) != NULL)
            {
                if (   _strnicmp(pszEnv, pchVar, cchVar) == 0
                    && pszEnv[cchVar] == '=')
                {
                    KW_LOG(("kwSandboxDoSetEnvA: Replacing iVar=%d: %p='%s' and %p='%ls'\n"
                            "                              iVar=%d: %p='%s' and %p='%ls'\n",
                            iVar, pSandbox->papszEnvVars[iVar], pSandbox->papszEnvVars[iVar],
                            pSandbox->papwszEnvVars[iVar], pSandbox->papwszEnvVars[iVar],
                            iVar, pszNew, pszNew, pwszNew, pwszNew));

                    kHlpFree(pSandbox->papszEnvVars[iVar]);
                    pSandbox->papszEnvVars[iVar]  = pszNew;
                    pSandbox->environ[iVar]       = pszNew;

                    kHlpFree(pSandbox->papwszEnvVars[iVar]);
                    pSandbox->papwszEnvVars[iVar] = pwszNew;
                    pSandbox->wenviron[iVar]      = pwszNew;
                    return 0;
                }
                iVar++;
            }

            /* Not found, do we need to grow the table first? */
            if (iVar + 1 >= pSandbox->cEnvVarsAllocated)
                kwSandboxGrowEnv(pSandbox, iVar + 2);
            if (iVar + 1 < pSandbox->cEnvVarsAllocated)
            {
                KW_LOG(("kwSandboxDoSetEnvA: Adding iVar=%d: %p='%s' and %p='%ls'\n", iVar, pszNew, pszNew, pwszNew, pwszNew));

                pSandbox->papszEnvVars[iVar + 1]  = NULL;
                pSandbox->papszEnvVars[iVar]      = pszNew;
                pSandbox->environ[iVar + 1]       = NULL;
                pSandbox->environ[iVar]           = pszNew;

                pSandbox->papwszEnvVars[iVar + 1] = NULL;
                pSandbox->papwszEnvVars[iVar]     = pwszNew;
                pSandbox->wenviron[iVar + 1]      = NULL;
                pSandbox->wenviron[iVar]          = pwszNew;
                return 0;
            }

            kHlpFree(pwszNew);
        }
        kHlpFree(pszNew);
    }
    KW_LOG(("Out of memory!\n"));
    return 0;
}


/**
 * Sets an environment variable, UTF-16 style.
 *
 * @returns 0 on success, non-zero on failure.
 * @param   pSandbox            The sandbox.
 * @param   pwcVar              The variable name.
 * @param   cwcVar              The length of the name.
 * @param   pwszValue           The value.
 */
static int kwSandboxDoSetEnvW(PKWSANDBOX pSandbox, const wchar_t *pwchVar, KSIZE cwcVar, const wchar_t *pwszValue)
{
    /* Allocate and construct the new strings. */
    KSIZE    cwcTmp = kwUtf16Len(pwszValue);
    wchar_t *pwszNew = (wchar_t *)kHlpAlloc((cwcVar + 1 + cwcTmp + 1) * sizeof(wchar_t));
    if (pwszNew)
    {
        char *pszNew;
        kHlpMemCopy(pwszNew, pwchVar, cwcVar * sizeof(wchar_t));
        pwszNew[cwcVar] = '=';
        kHlpMemCopy(&pwszNew[cwcVar + 1], pwszValue, cwcTmp * sizeof(wchar_t));
        cwcTmp += cwcVar + 1;
        pwszNew[cwcVar] = '\0';

        pszNew = kwUtf16ToStrAllocN(pwszNew, cwcVar);
        if (pszNew)
        {
            /* Look it up. */
            KSIZE    iVar = 0;
            wchar_t *pwszEnv;
            while ((pwszEnv = pSandbox->papwszEnvVars[iVar]) != NULL)
            {
                if (   _wcsnicmp(pwszEnv, pwchVar, cwcVar) == 0
                    && pwszEnv[cwcVar] == '=')
                {
                    KW_LOG(("kwSandboxDoSetEnvW: Replacing iVar=%d: %p='%s' and %p='%ls'\n"
                            "                              iVar=%d: %p='%s' and %p='%ls'\n",
                            iVar, pSandbox->papszEnvVars[iVar], pSandbox->papszEnvVars[iVar],
                            pSandbox->papwszEnvVars[iVar], pSandbox->papwszEnvVars[iVar],
                            iVar, pszNew, pszNew, pwszNew, pwszNew));

                    kHlpFree(pSandbox->papszEnvVars[iVar]);
                    pSandbox->papszEnvVars[iVar]  = pszNew;
                    pSandbox->environ[iVar]       = pszNew;

                    kHlpFree(pSandbox->papwszEnvVars[iVar]);
                    pSandbox->papwszEnvVars[iVar] = pwszNew;
                    pSandbox->wenviron[iVar]      = pwszNew;
                    return 0;
                }
                iVar++;
            }

            /* Not found, do we need to grow the table first? */
            if (iVar + 1 >= pSandbox->cEnvVarsAllocated)
                kwSandboxGrowEnv(pSandbox, iVar + 2);
            if (iVar + 1 < pSandbox->cEnvVarsAllocated)
            {
                KW_LOG(("kwSandboxDoSetEnvW: Adding iVar=%d: %p='%s' and %p='%ls'\n", iVar, pszNew, pszNew, pwszNew, pwszNew));

                pSandbox->papszEnvVars[iVar + 1]  = NULL;
                pSandbox->papszEnvVars[iVar]      = pszNew;
                pSandbox->environ[iVar + 1]       = NULL;
                pSandbox->environ[iVar]           = pszNew;

                pSandbox->papwszEnvVars[iVar + 1] = NULL;
                pSandbox->papwszEnvVars[iVar]     = pwszNew;
                pSandbox->wenviron[iVar + 1]      = NULL;
                pSandbox->wenviron[iVar]          = pwszNew;
                return 0;
            }

            kHlpFree(pwszNew);
        }
        kHlpFree(pszNew);
    }
    KW_LOG(("Out of memory!\n"));
    return 0;
}


/** ANSI unsetenv worker. */
static int kwSandboxDoUnsetEnvA(PKWSANDBOX pSandbox, const char *pchVar, KSIZE cchVar)
{
    KSIZE   iVar   = 0;
    char   *pszEnv;
    while ((pszEnv = pSandbox->papszEnvVars[iVar]) != NULL)
    {
        if (   _strnicmp(pszEnv, pchVar, cchVar) == 0
            && pszEnv[cchVar] == '=')
        {
            KSIZE cVars = iVar;
            while (pSandbox->papszEnvVars[cVars])
                cVars++;
            kHlpAssert(pSandbox->papwszEnvVars[iVar] != NULL);
            kHlpAssert(pSandbox->papwszEnvVars[cVars] == NULL);

            KW_LOG(("kwSandboxDoUnsetEnvA: Removing iVar=%d: %p='%s' and %p='%ls'; new cVars=%d\n", iVar,
                    pSandbox->papszEnvVars[iVar], pSandbox->papszEnvVars[iVar],
                    pSandbox->papwszEnvVars[iVar], pSandbox->papwszEnvVars[iVar], cVars - 1));

            kHlpFree(pSandbox->papszEnvVars[iVar]);
            pSandbox->papszEnvVars[iVar]    = pSandbox->papszEnvVars[cVars];
            pSandbox->environ[iVar]         = pSandbox->papszEnvVars[cVars];
            pSandbox->papszEnvVars[cVars]   = NULL;
            pSandbox->environ[cVars]        = NULL;

            kHlpFree(pSandbox->papwszEnvVars[iVar]);
            pSandbox->papwszEnvVars[iVar]   = pSandbox->papwszEnvVars[cVars];
            pSandbox->wenviron[iVar]        = pSandbox->papwszEnvVars[cVars];
            pSandbox->papwszEnvVars[cVars]  = NULL;
            pSandbox->wenviron[cVars]       = NULL;
            return 0;
        }
        iVar++;
    }
    return KERR_ENVVAR_NOT_FOUND;
}


/** UTF-16 unsetenv worker. */
static int kwSandboxDoUnsetEnvW(PKWSANDBOX pSandbox, const wchar_t *pwcVar, KSIZE cwcVar)
{
    KSIZE    iVar   = 0;
    wchar_t *pwszEnv;
    while ((pwszEnv = pSandbox->papwszEnvVars[iVar]) != NULL)
    {
        if (   _wcsnicmp(pwszEnv, pwcVar, cwcVar) == 0
            && pwszEnv[cwcVar] == '=')
        {
            KSIZE cVars = iVar;
            while (pSandbox->papwszEnvVars[cVars])
                cVars++;
            kHlpAssert(pSandbox->papszEnvVars[iVar] != NULL);
            kHlpAssert(pSandbox->papszEnvVars[cVars] == NULL);

            KW_LOG(("kwSandboxDoUnsetEnvA: Removing iVar=%d: %p='%s' and %p='%ls'; new cVars=%d\n", iVar,
                    pSandbox->papszEnvVars[iVar], pSandbox->papszEnvVars[iVar],
                    pSandbox->papwszEnvVars[iVar], pSandbox->papwszEnvVars[iVar], cVars - 1));

            kHlpFree(pSandbox->papszEnvVars[iVar]);
            pSandbox->papszEnvVars[iVar]    = pSandbox->papszEnvVars[cVars];
            pSandbox->environ[iVar]         = pSandbox->papszEnvVars[cVars];
            pSandbox->papszEnvVars[cVars]   = NULL;
            pSandbox->environ[cVars]        = NULL;

            kHlpFree(pSandbox->papwszEnvVars[iVar]);
            pSandbox->papwszEnvVars[iVar]   = pSandbox->papwszEnvVars[cVars];
            pSandbox->wenviron[iVar]        = pSandbox->papwszEnvVars[cVars];
            pSandbox->papwszEnvVars[cVars]  = NULL;
            pSandbox->wenviron[cVars]       = NULL;
            return 0;
        }
        iVar++;
    }
    return KERR_ENVVAR_NOT_FOUND;
}



/** ANSI getenv worker. */
static char *kwSandboxDoGetEnvA(PKWSANDBOX pSandbox, const char *pchVar, KSIZE cchVar)
{
    KSIZE   iVar   = 0;
    char   *pszEnv;
    while ((pszEnv = pSandbox->papszEnvVars[iVar++]) != NULL)
        if (   _strnicmp(pszEnv, pchVar, cchVar) == 0
            && pszEnv[cchVar] == '=')
            return &pszEnv[cchVar + 1];
    return NULL;
}


/** UTF-16 getenv worker. */
static wchar_t *kwSandboxDoGetEnvW(PKWSANDBOX pSandbox, const wchar_t *pwcVar, KSIZE cwcVar)
{
    KSIZE    iVar   = 0;
    wchar_t *pwszEnv;
    while ((pwszEnv = pSandbox->papwszEnvVars[iVar++]) != NULL)
        if (   _wcsnicmp(pwszEnv, pwcVar, cwcVar) == 0
            && pwszEnv[cwcVar] == '=')
            return &pwszEnv[cwcVar + 1];
    return NULL;
}


/** Kernel32 - GetEnvironmentVariableA()  */
static DWORD WINAPI kwSandbox_Kernel32_GetEnvironmentVariableA(LPCSTR pszVar, LPSTR pszValue, DWORD cbValue)
{
    char *pszFoundValue;
    kHlpAssert(GetCurrentThreadId() == g_Sandbox.idMainThread);

    pszFoundValue = kwSandboxDoGetEnvA(&g_Sandbox, pszVar, kHlpStrLen(pszVar));
    if (pszFoundValue)
    {
        DWORD cchRet = kwStrCopyStyle1(pszFoundValue, pszValue, cbValue);
        KW_LOG(("GetEnvironmentVariableA: '%s' -> %u (%s)\n", pszVar, cchRet, pszFoundValue));
        return cchRet;
    }
    KW_LOG(("GetEnvironmentVariableA: '%s' -> 0\n", pszVar));
    SetLastError(ERROR_ENVVAR_NOT_FOUND);
    return 0;
}


/** Kernel32 - GetEnvironmentVariableW()  */
static DWORD WINAPI kwSandbox_Kernel32_GetEnvironmentVariableW(LPCWSTR pwszVar, LPWSTR pwszValue, DWORD cwcValue)
{
    wchar_t *pwszFoundValue;
    kHlpAssert(GetCurrentThreadId() == g_Sandbox.idMainThread);

    pwszFoundValue = kwSandboxDoGetEnvW(&g_Sandbox, pwszVar, kwUtf16Len(pwszVar));
    if (pwszFoundValue)
    {
        DWORD cchRet = kwUtf16CopyStyle1(pwszFoundValue, pwszValue, cwcValue);
        KW_LOG(("GetEnvironmentVariableW: '%ls' -> %u (%ls)\n", pwszVar, cchRet, pwszFoundValue));
        return cchRet;
    }
    KW_LOG(("GetEnvironmentVariableW: '%ls' -> 0\n", pwszVar));
    SetLastError(ERROR_ENVVAR_NOT_FOUND);
    return 0;
}


/** Kernel32 - SetEnvironmentVariableA()  */
static BOOL WINAPI kwSandbox_Kernel32_SetEnvironmentVariableA(LPCSTR pszVar, LPCSTR pszValue)
{
    int rc;
    kHlpAssert(GetCurrentThreadId() == g_Sandbox.idMainThread);

    if (pszValue)
        rc = kwSandboxDoSetEnvA(&g_Sandbox, pszVar, kHlpStrLen(pszVar), pszValue);
    else
    {
        kwSandboxDoUnsetEnvA(&g_Sandbox, pszVar, kHlpStrLen(pszVar));
        rc = 0; //??
    }
    if (rc == 0)
    {
        KW_LOG(("SetEnvironmentVariableA(%s,%s) -> TRUE\n", pszVar, pszValue));
        return TRUE;
    }
    SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    KW_LOG(("SetEnvironmentVariableA(%s,%s) -> FALSE!\n", pszVar, pszValue));
    return FALSE;
}


/** Kernel32 - SetEnvironmentVariableW()  */
static BOOL WINAPI kwSandbox_Kernel32_SetEnvironmentVariableW(LPCWSTR pwszVar, LPCWSTR pwszValue)
{
    int rc;
    kHlpAssert(GetCurrentThreadId() == g_Sandbox.idMainThread);

    if (pwszValue)
        rc = kwSandboxDoSetEnvW(&g_Sandbox, pwszVar, kwUtf16Len(pwszVar), pwszValue);
    else
    {
        kwSandboxDoUnsetEnvW(&g_Sandbox, pwszVar, kwUtf16Len(pwszVar));
        rc = 0; //??
    }
    if (rc == 0)
    {
        KW_LOG(("SetEnvironmentVariableA(%ls,%ls) -> TRUE\n", pwszVar, pwszValue));
        return TRUE;
    }
    SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    KW_LOG(("SetEnvironmentVariableA(%ls,%ls) -> FALSE!\n", pwszVar, pwszValue));
    return FALSE;
}


/** Kernel32 - ExpandEnvironmentStringsA()  */
static DWORD WINAPI kwSandbox_Kernel32_ExpandEnvironmentStringsA(LPCSTR pszSrc, LPSTR pwszDst, DWORD cbDst)
{
    kHlpAssert(GetCurrentThreadId() == g_Sandbox.idMainThread);
    KWFS_TODO();
    return 0;
}


/** Kernel32 - ExpandEnvironmentStringsW()  */
static DWORD WINAPI kwSandbox_Kernel32_ExpandEnvironmentStringsW(LPCWSTR pwszSrc, LPWSTR pwszDst, DWORD cbDst)
{
    kHlpAssert(GetCurrentThreadId() == g_Sandbox.idMainThread);
    KWFS_TODO();
    return 0;
}


/** CRT - _putenv(). */
static int __cdecl kwSandbox_msvcrt__putenv(const char *pszVarEqualValue)
{
    int rc;
    char const *pszEqual;
    kHlpAssert(GetCurrentThreadId() == g_Sandbox.idMainThread);

    pszEqual = kHlpStrChr(pszVarEqualValue, '=');
    if (pszEqual)
    {
        rc = kwSandboxDoSetEnvA(&g_Sandbox, pszVarEqualValue, pszEqual - pszVarEqualValue, pszEqual + 1);
        if (rc == 0)
        { }
        else
            rc = -1;
    }
    else
    {
        kwSandboxDoUnsetEnvA(&g_Sandbox, pszVarEqualValue, kHlpStrLen(pszVarEqualValue));
        rc = 0;
    }
    KW_LOG(("_putenv(%s) -> %d\n", pszVarEqualValue, rc));
    return rc;
}


/** CRT - _wputenv(). */
static int __cdecl kwSandbox_msvcrt__wputenv(const wchar_t *pwszVarEqualValue)
{
    int rc;
    wchar_t const *pwszEqual;
    kHlpAssert(GetCurrentThreadId() == g_Sandbox.idMainThread);

    pwszEqual = wcschr(pwszVarEqualValue, '=');
    if (pwszEqual)
    {
        rc = kwSandboxDoSetEnvW(&g_Sandbox, pwszVarEqualValue, pwszEqual - pwszVarEqualValue, pwszEqual + 1);
        if (rc == 0)
        { }
        else
            rc = -1;
    }
    else
    {
        kwSandboxDoUnsetEnvW(&g_Sandbox, pwszVarEqualValue, kwUtf16Len(pwszVarEqualValue));
        rc = 0;
    }
    KW_LOG(("_wputenv(%ls) -> %d\n", pwszVarEqualValue, rc));
    return rc;
}


/** CRT - _putenv_s(). */
static errno_t __cdecl kwSandbox_msvcrt__putenv_s(const char *pszVar, const char *pszValue)
{
    char const *pszEqual;
    kHlpAssert(GetCurrentThreadId() == g_Sandbox.idMainThread);

    pszEqual = kHlpStrChr(pszVar, '=');
    if (pszEqual == NULL)
    {
        if (pszValue)
        {
            int rc = kwSandboxDoSetEnvA(&g_Sandbox, pszVar, kHlpStrLen(pszVar), pszValue);
            if (rc == 0)
            {
                KW_LOG(("_putenv_s(%s,%s) -> 0\n", pszVar, pszValue));
                return 0;
            }
        }
        else
        {
            kwSandboxDoUnsetEnvA(&g_Sandbox, pszVar, kHlpStrLen(pszVar));
            KW_LOG(("_putenv_s(%ls,NULL) -> 0\n", pszVar));
            return 0;
        }
        KW_LOG(("_putenv_s(%s,%s) -> ENOMEM\n", pszVar, pszValue));
        return ENOMEM;
    }
    KW_LOG(("_putenv_s(%s,%s) -> EINVAL\n", pszVar, pszValue));
    return EINVAL;
}


/** CRT - _wputenv_s(). */
static errno_t __cdecl kwSandbox_msvcrt__wputenv_s(const wchar_t *pwszVar, const wchar_t *pwszValue)
{
    wchar_t const *pwszEqual;
    kHlpAssert(GetCurrentThreadId() == g_Sandbox.idMainThread);

    pwszEqual = wcschr(pwszVar, '=');
    if (pwszEqual == NULL)
    {
        if (pwszValue)
        {
            int rc = kwSandboxDoSetEnvW(&g_Sandbox, pwszVar, kwUtf16Len(pwszVar), pwszValue);
            if (rc == 0)
            {
                KW_LOG(("_wputenv_s(%ls,%ls) -> 0\n", pwszVar, pwszValue));
                return 0;
            }
        }
        else
        {
            kwSandboxDoUnsetEnvW(&g_Sandbox, pwszVar, kwUtf16Len(pwszVar));
            KW_LOG(("_wputenv_s(%ls,NULL) -> 0\n", pwszVar));
            return 0;
        }
        KW_LOG(("_wputenv_s(%ls,%ls) -> ENOMEM\n", pwszVar, pwszValue));
        return ENOMEM;
    }
    KW_LOG(("_wputenv_s(%ls,%ls) -> EINVAL\n", pwszVar, pwszValue));
    return EINVAL;
}


/** CRT - get pointer to the __initenv variable (initial environment).   */
static char *** __cdecl kwSandbox_msvcrt___p___initenv(void)
{
    KW_LOG(("__p___initenv\n"));
    kHlpAssert(GetCurrentThreadId() == g_Sandbox.idMainThread);
    KWFS_TODO();
    return &g_Sandbox.initenv;
}


/** CRT - get pointer to the __winitenv variable (initial environment).   */
static wchar_t *** __cdecl kwSandbox_msvcrt___p___winitenv(void)
{
    KW_LOG(("__p___winitenv\n"));
    kHlpAssert(GetCurrentThreadId() == g_Sandbox.idMainThread);
    KWFS_TODO();
    return &g_Sandbox.winitenv;
}


/** CRT - get pointer to the _environ variable (current environment).   */
static char *** __cdecl kwSandbox_msvcrt___p__environ(void)
{
    KW_LOG(("__p__environ\n"));
    kHlpAssert(GetCurrentThreadId() == g_Sandbox.idMainThread);
    return &g_Sandbox.environ;
}


/** CRT - get pointer to the _wenviron variable (current environment).   */
static wchar_t *** __cdecl kwSandbox_msvcrt___p__wenviron(void)
{
    KW_LOG(("__p__wenviron\n"));
    kHlpAssert(GetCurrentThreadId() == g_Sandbox.idMainThread);
    return &g_Sandbox.wenviron;
}


/** CRT - get the _environ variable (current environment).
 * @remarks Not documented or prototyped?  */
static KUPTR /*void*/ __cdecl kwSandbox_msvcrt__get_environ(char ***ppapszEnviron)
{
    KWFS_TODO(); /** @todo check the callers expectations! */
    kHlpAssert(GetCurrentThreadId() == g_Sandbox.idMainThread);
    *ppapszEnviron = g_Sandbox.environ;
    return 0;
}


/** CRT - get the _wenviron variable (current environment).
 * @remarks Not documented or prototyped? */
static KUPTR /*void*/ __cdecl kwSandbox_msvcrt__get_wenviron(wchar_t ***ppapwszEnviron)
{
    KWFS_TODO(); /** @todo check the callers expectations! */
    kHlpAssert(GetCurrentThreadId() == g_Sandbox.idMainThread);
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

/**
 * Kernel32 - LoadLibraryExA() worker that loads resource files and such.
 */
static HMODULE WINAPI kwSandbox_Kernel32_LoadLibraryExA_Resource(PKWDYNLOAD pDynLoad, DWORD fFlags)
{
    /* Load it first. */
    HMODULE hmod = LoadLibraryExA(pDynLoad->szRequest, NULL /*hFile*/, fFlags);
    if (hmod)
    {
        pDynLoad->hmod = hmod;
        pDynLoad->pMod = NULL; /* indicates special  */

        pDynLoad->pNext = g_Sandbox.pTool->u.Sandboxed.pDynLoadHead;
        g_Sandbox.pTool->u.Sandboxed.pDynLoadHead = pDynLoad;
        KW_LOG(("LoadLibraryExA(%s,,[resource]) -> %p\n", pDynLoad->szRequest, pDynLoad->hmod));
    }
    else
        kHlpFree(pDynLoad);
    return hmod;
}


/**
 * Kernel32 - LoadLibraryExA() worker that deals with the api-ms-xxx modules.
 */
static HMODULE WINAPI kwSandbox_Kernel32_LoadLibraryExA_VirtualApiModule(PKWDYNLOAD pDynLoad, DWORD fFlags)
{
    HMODULE     hmod;
    PKWMODULE   pMod;
    KU32        uHashPath;
    KSIZE       idxHash;
    char        szNormPath[256];
    KSIZE       cbFilename = kHlpStrLen(pDynLoad->szRequest) + 1;

    /*
     * Lower case it.
     */
    if (cbFilename <= sizeof(szNormPath))
    {
        kHlpMemCopy(szNormPath, pDynLoad->szRequest, cbFilename);
        _strlwr(szNormPath);
    }
    else
    {
        SetLastError(ERROR_FILENAME_EXCED_RANGE);
        return NULL;
    }

    /*
     * Check if it has already been loaded so we don't create an unnecessary
     * loader module for it.
     */
    uHashPath = kwStrHash(szNormPath);
    idxHash   = uHashPath % K_ELEMENTS(g_apModules);
    pMod      = g_apModules[idxHash];
    if (pMod)
    {
        do
        {
            if (   pMod->uHashPath == uHashPath
                && kHlpStrComp(pMod->pszPath, szNormPath) == 0)
            {
                pDynLoad->pMod = kwLdrModuleRetain(pMod);
                pDynLoad->hmod = pMod->hOurMod;

                pDynLoad->pNext = g_Sandbox.pTool->u.Sandboxed.pDynLoadHead;
                g_Sandbox.pTool->u.Sandboxed.pDynLoadHead = pDynLoad;
                KW_LOG(("LoadLibraryExA(%s,,) -> %p [already loaded]\n", pDynLoad->szRequest, pDynLoad->hmod));
                return pDynLoad->hmod;
            }
            pMod = pMod->pNext;
        } while (pMod);
    }


    /*
     * Try load it and make a kLdr module for it.
     */
    hmod = LoadLibraryExA(szNormPath, NULL /*hFile*/, fFlags);
    if (hmod)
    {
        PKLDRMOD pLdrMod;
        int rc = kLdrModOpenNativeByHandle((KUPTR)hmod, &pLdrMod);
        if (rc == 0)
        {
            PKWMODULE pMod = kwLdrModuleCreateForNativekLdrModule(pLdrMod, szNormPath, cbFilename, uHashPath,
                                                                  K_FALSE /*fDoReplacements*/);
            if (pMod)
            {
                kwToolAddModuleAndImports(g_Sandbox.pTool, pMod);

                pDynLoad = (PKWDYNLOAD)kHlpAlloc(sizeof(*pDynLoad) + cbFilename + cbFilename * sizeof(wchar_t));
                if (pDynLoad)
                {
                    pDynLoad->pMod = pMod;
                    pDynLoad->hmod = hmod;

                    pDynLoad->pNext = g_Sandbox.pTool->u.Sandboxed.pDynLoadHead;
                    g_Sandbox.pTool->u.Sandboxed.pDynLoadHead = pDynLoad;
                    KW_LOG(("LoadLibraryExA(%s,,) -> %p\n", pDynLoad->szRequest, pDynLoad->hmod));
                    return hmod;
                }

                KWFS_TODO();
            }
            else
                KWFS_TODO();
        }
        else
            KWFS_TODO();
    }
    kHlpFree(pDynLoad);
    return hmod;
}


/** Kernel32 - LoadLibraryExA() */
static HMODULE WINAPI kwSandbox_Kernel32_LoadLibraryExA(LPCSTR pszFilename, HANDLE hFile, DWORD fFlags)
{
    KSIZE       cchFilename = kHlpStrLen(pszFilename);
    PKWDYNLOAD  pDynLoad;
    PKWMODULE   pMod;
    int         rc;
    kHlpAssert(GetCurrentThreadId() == g_Sandbox.idMainThread);

    /*
     * Deal with a couple of extremely unlikely special cases right away.
     */
    if (   !(fFlags & LOAD_LIBRARY_AS_DATAFILE_EXCLUSIVE)
        && (hFile == NULL || hFile == INVALID_HANDLE_VALUE) )
    { /* likely */ }
    else
    {
        KWFS_TODO();
        return LoadLibraryExA(pszFilename, hFile, fFlags);
    }

    /*
     * Check if we've already got a dynload entry for this one.
     */
    for (pDynLoad = g_Sandbox.pTool->u.Sandboxed.pDynLoadHead; pDynLoad; pDynLoad = pDynLoad->pNext)
        if (   pDynLoad->cchRequest == cchFilename
            && kHlpMemComp(pDynLoad->szRequest, pszFilename, cchFilename) == 0)
        {
            if (pDynLoad->pMod)
                rc = kwLdrModuleInitTree(pDynLoad->pMod);
            else
                rc = 0;
            if (rc == 0)
            {
                KW_LOG(("LoadLibraryExA(%s,,) -> %p [cached]\n", pszFilename, pDynLoad->hmod));
                return pDynLoad->hmod;
            }
            SetLastError(ERROR_DLL_INIT_FAILED);
            return NULL;
        }

    /*
     * Allocate a dynload entry for the request.
     */
    pDynLoad = (PKWDYNLOAD)kHlpAlloc(sizeof(*pDynLoad) + cchFilename + 1);
    if (pDynLoad)
    {
        pDynLoad->cchRequest = cchFilename;
        kHlpMemCopy(pDynLoad->szRequest, pszFilename, cchFilename + 1);
    }
    else
    {
        KW_LOG(("LoadLibraryExA: Out of memory!\n"));
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return NULL;
    }

    /*
     * Deal with resource / data DLLs.
     */
    if (fFlags & (  DONT_RESOLVE_DLL_REFERENCES
                  | LOAD_LIBRARY_AS_DATAFILE
                  | LOAD_LIBRARY_AS_IMAGE_RESOURCE) )
        return kwSandbox_Kernel32_LoadLibraryExA_Resource(pDynLoad, fFlags);

    /*
     * Special case: api-ms-win-core-synch-l1-2-0 and friends (32-bit yasm, built with VS2015).
     */
    if (   strnicmp(pszFilename, TUPLE("api-ms-")) == 0
        && kHlpIsFilenameOnly(pszFilename))
        return kwSandbox_Kernel32_LoadLibraryExA_VirtualApiModule(pDynLoad, fFlags);

    /*
     * Normal library loading.
     * We start by being very lazy and reusing the code for resolving imports.
     */
    if (!kHlpIsFilenameOnly(pszFilename))
        pMod = kwLdrModuleTryLoadDll(pszFilename, KWLOCATION_UNKNOWN, g_Sandbox.pTool->u.Sandboxed.pExe);
    else
    {
        rc = kwLdrModuleResolveAndLookup(pszFilename, g_Sandbox.pTool->u.Sandboxed.pExe, NULL /*pImporter*/, &pMod);
        if (rc != 0)
            pMod = NULL;
    }
    if (pMod)
    {
        /* Enter it into the tool module table and dynamic link request cache. */
        kwToolAddModuleAndImports(g_Sandbox.pTool, pMod);

        pDynLoad->pMod = pMod;
        pDynLoad->hmod = pMod->hOurMod;

        pDynLoad->pNext = g_Sandbox.pTool->u.Sandboxed.pDynLoadHead;
        g_Sandbox.pTool->u.Sandboxed.pDynLoadHead = pDynLoad;

        /*
         * Make sure it's initialized (need to link it first since DllMain may
         * use loader APIs).
         */
        rc = kwLdrModuleInitTree(pMod);
        if (rc == 0)
        {
            KW_LOG(("LoadLibraryExA(%s,,) -> %p\n", pszFilename, pDynLoad->hmod));
            return pDynLoad->hmod;
        }

        SetLastError(ERROR_DLL_INIT_FAILED);
    }
    else
    {
        KWFS_TODO();
        kHlpFree(pDynLoad);
        SetLastError(ERROR_MOD_NOT_FOUND);
    }
    return NULL;
}


/** Kernel32 - LoadLibraryExW()   */
static HMODULE WINAPI kwSandbox_Kernel32_LoadLibraryExW(LPCWSTR pwszFilename, HANDLE hFile, DWORD fFlags)
{
    char szTmp[4096];
    KSIZE cchTmp = kwUtf16ToStr(pwszFilename, szTmp, sizeof(szTmp));
    if (cchTmp < sizeof(szTmp))
        return kwSandbox_Kernel32_LoadLibraryExA(szTmp, hFile, fFlags);

    KWFS_TODO();
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
    KWFS_TODO();
    SetLastError(ERROR_FILENAME_EXCED_RANGE);
    return NULL;
}


/** Kernel32 - FreeLibrary()   */
static BOOL WINAPI kwSandbox_Kernel32_FreeLibrary(HMODULE hmod)
{
    /* Ignored, we like to keep everything loaded. */
    kHlpAssert(GetCurrentThreadId() == g_Sandbox.idMainThread);
    return TRUE;
}


/** Kernel32 - GetModuleHandleA()   */
static HMODULE WINAPI kwSandbox_Kernel32_GetModuleHandleA(LPCSTR pszModule)
{
    KSIZE i;
    KSIZE cchModule;
    kHlpAssert(GetCurrentThreadId() == g_Sandbox.idMainThread);

    /*
     * The executable.
     */
    if (pszModule == NULL)
        return (HMODULE)g_Sandbox.pTool->u.Sandboxed.pExe->hOurMod;

    /*
     * Cache of system modules we've seen queried.
     */
    cchModule = kHlpStrLen(pszModule);
    for (i = 0; i < K_ELEMENTS(g_aGetModuleHandleCache); i++)
        if (   g_aGetModuleHandleCache[i].cchName == cchModule
            && stricmp(pszModule, g_aGetModuleHandleCache[i].pszName) == 0)
        {
            if (g_aGetModuleHandleCache[i].hmod != NULL)
                return g_aGetModuleHandleCache[i].hmod;
            return g_aGetModuleHandleCache[i].hmod = GetModuleHandleA(pszModule);
        }

    KWFS_TODO();
    return NULL;
}


/** Kernel32 - GetModuleHandleW()   */
static HMODULE WINAPI kwSandbox_Kernel32_GetModuleHandleW(LPCWSTR pwszModule)
{
    KSIZE i;
    KSIZE cwcModule;
    kHlpAssert(GetCurrentThreadId() == g_Sandbox.idMainThread);

    /*
     * The executable.
     */
    if (pwszModule == NULL)
        return (HMODULE)g_Sandbox.pTool->u.Sandboxed.pExe->hOurMod;

    /*
     * Cache of system modules we've seen queried.
     */
    cwcModule = kwUtf16Len(pwszModule);
    for (i = 0; i < K_ELEMENTS(g_aGetModuleHandleCache); i++)
        if (   g_aGetModuleHandleCache[i].cwcName == cwcModule
            && _wcsicmp(pwszModule, g_aGetModuleHandleCache[i].pwszName) == 0)
        {
            if (g_aGetModuleHandleCache[i].hmod != NULL)
                return g_aGetModuleHandleCache[i].hmod;
            return g_aGetModuleHandleCache[i].hmod = GetModuleHandleW(pwszModule);
        }

    KWFS_TODO();
    return NULL;
}


/** Used to debug dynamically resolved procedures. */
static UINT WINAPI kwSandbox_BreakIntoDebugger(void *pv1, void *pv2, void *pv3, void *pv4)
{
#ifdef _MSC_VER
    __debugbreak();
#else
    KWFS_TODO();
#endif
    return -1;
}


/** Kernel32 - GetProcAddress()   */
static FARPROC WINAPI kwSandbox_Kernel32_GetProcAddress(HMODULE hmod, LPCSTR pszProc)
{
    KSIZE i;
    PKWMODULE pMod;
    kHlpAssert(GetCurrentThreadId() == g_Sandbox.idMainThread);

    /*
     * Try locate the module.
     */
    pMod = kwToolLocateModuleByHandle(g_Sandbox.pTool, hmod);
    if (pMod)
    {
        KLDRADDR uValue;
        int rc = kLdrModQuerySymbol(pMod->pLdrMod,
                                    pMod->fNative ? NULL : pMod->u.Manual.pvBits,
                                    pMod->fNative ? KLDRMOD_BASEADDRESS_MAP : (KUPTR)pMod->u.Manual.pbLoad,
                                    KU32_MAX /*iSymbol*/,
                                    pszProc,
                                    kHlpStrLen(pszProc),
                                    NULL /*pszVersion*/,
                                    NULL /*pfnGetForwarder*/, NULL /*pvUser*/,
                                    &uValue,
                                    NULL /*pfKind*/);
        if (rc == 0)
        {
            //static int s_cDbgGets = 0;
            KU32 cchProc = (KU32)kHlpStrLen(pszProc);
            KU32 i = g_cSandboxGetProcReplacements;
            while (i-- > 0)
                if (   g_aSandboxGetProcReplacements[i].cchFunction == cchProc
                    && kHlpMemComp(g_aSandboxGetProcReplacements[i].pszFunction, pszProc, cchProc) == 0)
                {
                    if (   !g_aSandboxGetProcReplacements[i].pszModule
                        || kHlpStrICompAscii(g_aSandboxGetProcReplacements[i].pszModule, &pMod->pszPath[pMod->offFilename]) == 0)
                    {
                        if (   !g_aSandboxGetProcReplacements[i].fOnlyExe
                            || (KUPTR)_ReturnAddress() - (KUPTR)g_Sandbox.pTool->u.Sandboxed.pExe->hOurMod
                               < g_Sandbox.pTool->u.Sandboxed.pExe->cbImage)
                        {
                            uValue = g_aSandboxGetProcReplacements[i].pfnReplacement;
                            KW_LOG(("GetProcAddress(%s, %s) -> %p replaced\n", pMod->pszPath, pszProc, (KUPTR)uValue));
                        }
                        kwLdrModuleRelease(pMod);
                        return (FARPROC)(KUPTR)uValue;
                    }
                }

            KW_LOG(("GetProcAddress(%s, %s) -> %p\n", pMod->pszPath, pszProc, (KUPTR)uValue));
            kwLdrModuleRelease(pMod);
            //s_cDbgGets++;
            //if (s_cGets >= 3)
            //    return (FARPROC)kwSandbox_BreakIntoDebugger;
            return (FARPROC)(KUPTR)uValue;
        }

        KWFS_TODO();
        SetLastError(ERROR_PROC_NOT_FOUND);
        kwLdrModuleRelease(pMod);
        return NULL;
    }

    /*
     * Hmm... could be a cached module-by-name.
     */
    for (i = 0; i < K_ELEMENTS(g_aGetModuleHandleCache); i++)
        if (g_aGetModuleHandleCache[i].hmod == hmod)
            return GetProcAddress(hmod, pszProc);

    KWFS_TODO();
    return GetProcAddress(hmod, pszProc);
}


/** Kernel32 - GetModuleFileNameA()   */
static DWORD WINAPI kwSandbox_Kernel32_GetModuleFileNameA(HMODULE hmod, LPSTR pszFilename, DWORD cbFilename)
{
    PKWMODULE pMod;
    kHlpAssert(GetCurrentThreadId() == g_Sandbox.idMainThread);

    pMod = kwToolLocateModuleByHandle(g_Sandbox.pTool, hmod);
    if (pMod != NULL)
    {
        DWORD cbRet = kwStrCopyStyle1(pMod->pszPath, pszFilename, cbFilename);
        kwLdrModuleRelease(pMod);
        return cbRet;
    }
    KWFS_TODO();
    return 0;
}


/** Kernel32 - GetModuleFileNameW()   */
static DWORD WINAPI kwSandbox_Kernel32_GetModuleFileNameW(HMODULE hmod, LPWSTR pwszFilename, DWORD cbFilename)
{
    PKWMODULE pMod;
    kHlpAssert(GetCurrentThreadId() == g_Sandbox.idMainThread);

    pMod = kwToolLocateModuleByHandle(g_Sandbox.pTool, hmod);
    if (pMod)
    {
        DWORD cwcRet = kwUtf16CopyStyle1(pMod->pwszPath, pwszFilename, cbFilename);
        kwLdrModuleRelease(pMod);
        return cwcRet;
    }

    KWFS_TODO();
    return 0;
}


/** NtDll - RtlPcToFileHeader
 * This is necessary for msvcr100.dll!CxxThrowException.  */
static PVOID WINAPI kwSandbox_ntdll_RtlPcToFileHeader(PVOID pvPC, PVOID *ppvImageBase)
{
    PVOID pvRet;

    /*
     * Do a binary lookup of the module table for the current tool.
     * This will give us a
     */
    if (g_Sandbox.fRunning)
    {
        KUPTR const     uPC     = (KUPTR)pvPC;
        PKWMODULE      *papMods = g_Sandbox.pTool->u.Sandboxed.papModules;
        KU32            iEnd    = g_Sandbox.pTool->u.Sandboxed.cModules;
        KU32            i;
        if (iEnd)
        {
            KU32        iStart  = 0;
            i = iEnd / 2;
            for (;;)
            {
                KUPTR const uHModThis = (KUPTR)papMods[i]->hOurMod;
                if (uPC < uHModThis)
                {
                    iEnd = i;
                    if (iStart < i)
                    { }
                    else
                        break;
                }
                else if (uPC != uHModThis)
                {
                    iStart = ++i;
                    if (i < iEnd)
                    { }
                    else
                        break;
                }
                else
                {
                    /* This isn't supposed to happen. */
                    break;
                }

                i = iStart + (iEnd - iStart) / 2;
            }

            /* For reasons of simplicity (= copy & paste), we end up with the
               module after the one we're interested in here.  */
            i--;
            if (i < g_Sandbox.pTool->u.Sandboxed.cModules
                && papMods[i]->pLdrMod)
            {
                KSIZE uRvaPC = uPC - (KUPTR)papMods[i]->hOurMod;
                if (uRvaPC < papMods[i]->cbImage)
                {
                    *ppvImageBase = papMods[i]->hOurMod;
                    pvRet = papMods[i]->hOurMod;
                    KW_LOG(("RtlPcToFileHeader(PC=%p) -> %p, *ppvImageBase=%p [our]\n", pvPC, pvRet, *ppvImageBase));
                    return pvRet;
                }
            }
        }
        else
            i = 0;
    }

    /*
     * Call the regular API.
     */
    pvRet = RtlPcToFileHeader(pvPC, ppvImageBase);
    KW_LOG(("RtlPcToFileHeader(PC=%p) -> %p, *ppvImageBase=%p \n", pvPC, pvRet, *ppvImageBase));
    return pvRet;
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
            pHandle->cRefs              = 1;
            pHandle->offFile            = 0;
            pHandle->hHandle            = hFile;
            pHandle->dwDesiredAccess    = dwDesiredAccess;
            pHandle->u.pTempFile        = pTempFile;
            if (kwSandboxHandleTableEnter(&g_Sandbox, pHandle, hFile))
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
 * Worker for kwFsIsCacheableExtensionA and kwFsIsCacheableExtensionW
 *
 * @returns K_TRUE if cacheable, K_FALSE if not.
 * @param   wcFirst             The first extension character.
 * @param   wcSecond            The second extension character.
 * @param   wcThird             The third extension character.
 * @param   fAttrQuery          Set if it's for an attribute query, clear if for
 *                              file creation.
 */
static KBOOL kwFsIsCacheableExtensionCommon(wchar_t wcFirst, wchar_t wcSecond, wchar_t wcThird, KBOOL fAttrQuery)
{
    /* C++ header without an extension or a directory. */
    if (wcFirst == '\0')
    {
        /** @todo exclude temporary files...  */
        return K_TRUE;
    }

    /* C Header: .h */
    if (wcFirst == 'h' || wcFirst == 'H')
    {
        if (wcSecond == '\0')
            return K_TRUE;

        /* C++ Header: .hpp, .hxx */
        if (   (wcSecond == 'p' || wcSecond == 'P')
            && (wcThird  == 'p' || wcThird  == 'P'))
            return K_TRUE;
        if (   (wcSecond == 'x' || wcSecond == 'X')
            && (wcThird  == 'x' || wcThird  == 'X'))
            return K_TRUE;
    }
    /* Misc starting with i. */
    else if (wcFirst == 'i' || wcFirst == 'I')
    {
        if (wcSecond != '\0')
        {
            if (wcSecond == 'n' || wcSecond == 'N')
            {
                /* C++ inline header: .inl */
                if (wcThird == 'l' || wcThird == 'L')
                    return K_TRUE;

                /* Assembly include file: .inc */
                if (wcThird == 'c' || wcThird == 'C')
                    return K_TRUE;
            }
        }
    }
    /* Assembly header: .mac */
    else if (wcFirst == 'm' || wcFirst == 'M')
    {
        if (wcSecond == 'a' || wcSecond == 'A')
        {
            if (wcThird == 'c' || wcThird == 'C')
                return K_TRUE;
        }
    }
    else if (fAttrQuery)
    {
        /* Dynamic link library: .dll */
        if (wcFirst == 'd' || wcFirst == 'D')
        {
            if (wcSecond == 'l' || wcSecond == 'L')
            {
                if (wcThird == 'l' || wcThird == 'L')
                    return K_TRUE;
            }
        }
        /* Executable file: .exe */
        else if (wcFirst == 'e' || wcFirst == 'E')
        {
            if (wcSecond == 'x' || wcSecond == 'X')
            {
                if (wcThird == 'e' || wcThird == 'e')
                    return K_TRUE;
            }
        }
    }

    return K_FALSE;
}


/**
 * Checks if the file extension indicates that the file/dir is something we
 * ought to cache.
 *
 * @returns K_TRUE if cachable, K_FALSE if not.
 * @param   pszExt              The kHlpGetExt result.
 * @param   fAttrQuery          Set if it's for an attribute query, clear if for
 *                              file creation.
 */
static KBOOL kwFsIsCacheableExtensionA(const char *pszExt, KBOOL fAttrQuery)
{
    wchar_t const wcFirst = *pszExt;
    if (wcFirst)
    {
        wchar_t const wcSecond = pszExt[1];
        if (wcSecond)
        {
            wchar_t const wcThird = pszExt[2];
            if (pszExt[3] == '\0')
                return kwFsIsCacheableExtensionCommon(wcFirst, wcSecond, wcThird, fAttrQuery);
            return K_FALSE;
        }
        return kwFsIsCacheableExtensionCommon(wcFirst, 0, 0, fAttrQuery);
    }
    return kwFsIsCacheableExtensionCommon(0, 0, 0, fAttrQuery);
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
static KBOOL kwFsIsCacheablePathExtensionW(const wchar_t *pwszPath, KBOOL fAttrQuery)
{
    KSIZE           cwcExt;
    wchar_t const  *pwszExt = kwFsPathGetExtW(pwszPath, &cwcExt);
    switch (cwcExt)
    {
        case 3: return kwFsIsCacheableExtensionCommon(pwszExt[0], pwszExt[1], pwszExt[2], fAttrQuery);
        case 2: return kwFsIsCacheableExtensionCommon(pwszExt[0], pwszExt[1], 0,          fAttrQuery);
        case 1: return kwFsIsCacheableExtensionCommon(pwszExt[0], 0,          0,          fAttrQuery);
        case 0: return kwFsIsCacheableExtensionCommon(0,          0,          0,          fAttrQuery);
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
    kHlpAssert(GetCurrentThreadId() == g_Sandbox.idMainThread);

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
#if 0
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
#else
                HANDLE hMapping = CreateFileMappingW(hFile, NULL /*pSecAttrs*/,  PAGE_READONLY,
                                                     0 /*cbMaxLow*/, 0 /*cbMaxHigh*/, NULL /*pwszName*/);
                if (hMapping != NULL)
                {
                    KU8 *pbCache = (KU8 *)MapViewOfFile(hMapping, FILE_MAP_READ, 0 /*offFileHigh*/, 0 /*offFileLow*/, cbCache);
                    CloseHandle(hMapping);
                    if (pbCache)
                    {
#endif
                            /*
                             * Create the cached file object.
                             */
                            PKFSWCACHEDFILE pCachedFile;
                            KU32            cbPath = pFsObj->cchParent + pFsObj->cchName + 2;
                            pCachedFile = (PKFSWCACHEDFILE)kFsCacheObjAddUserData(g_pFsCache, pFsObj, KW_DATA_KEY_CACHED_FILE,
                                                                                  sizeof(*pCachedFile) + cbPath);
                            if (pCachedFile)
                            {
                                pCachedFile->hCached  = hFile;
                                pCachedFile->cbCached = cbCache;
                                pCachedFile->pbCached = pbCache;
                                pCachedFile->pFsObj   = pFsObj;
                                kFsCacheObjGetFullPathA(pFsObj, pCachedFile->szPath, cbPath, '/');
                                kFsCacheObjRetain(pFsObj);
                                KWFS_LOG(("Cached '%s': %p LB %#x, hCached=%p\n", pCachedFile->szPath, pbCache, cbCache, hFile));
                                return pCachedFile;
                            }

                            KWFS_LOG(("Failed to allocate KFSWCACHEDFILE structure!\n"));
#if 1
                    }
                    else
                        KWFS_LOG(("Failed to cache file: MapViewOfFile failed: %u\n", GetLastError()));
                }
                else
                    KWFS_LOG(("Failed to cache file: CreateFileMappingW failed: %u\n", GetLastError()));
#else
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
#endif
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
 * Kernel32 - Common code for kwFsObjCacheCreateFile and CreateFileMappingW/A.
 */
static KBOOL kwFsObjCacheCreateFileHandle(PKFSWCACHEDFILE pCachedFile, DWORD dwDesiredAccess, BOOL fInheritHandle,
                                          KBOOL fIsFileHandle, HANDLE *phFile)
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
            pHandle->enmType            = fIsFileHandle ? KWHANDLETYPE_FSOBJ_READ_CACHE : KWHANDLETYPE_FSOBJ_READ_CACHE_MAPPING;
            pHandle->cRefs              = 1;
            pHandle->offFile            = 0;
            pHandle->hHandle            = *phFile;
            pHandle->dwDesiredAccess    = dwDesiredAccess;
            pHandle->u.pCachedFile      = pCachedFile;
            if (kwSandboxHandleTableEnter(&g_Sandbox, pHandle, pHandle->hHandle))
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
    return K_FALSE;
}


/**
 * Kernel32 - Common code for CreateFileW and CreateFileA.
 */
static KBOOL kwFsObjCacheCreateFile(PKFSOBJ pFsObj, DWORD dwDesiredAccess, BOOL fInheritHandle, HANDLE *phFile)
{
    *phFile = INVALID_HANDLE_VALUE;

    /*
     * At the moment we only handle existing files.
     */
    if (pFsObj->bObjType == KFSOBJ_TYPE_FILE)
    {
        PKFSWCACHEDFILE pCachedFile = (PKFSWCACHEDFILE)kFsCacheObjGetUserData(g_pFsCache, pFsObj, KW_DATA_KEY_CACHED_FILE);
        kHlpAssert(pFsObj->fHaveStats);
        if (   pCachedFile != NULL
            || (pCachedFile = kwFsObjCacheNewFile(pFsObj)) != NULL)
        {
            if (kwFsObjCacheCreateFileHandle(pCachedFile, dwDesiredAccess, fInheritHandle, K_TRUE /*fIsFileHandle*/, phFile))
                return K_TRUE;
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

    /*
     * Check for include files and similar that we do read-only caching of.
     */
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
                    if (kwFsIsCacheableExtensionA(pszExt, K_FALSE /*fAttrQuery*/))
                    {
                        KFSLOOKUPERROR enmError;
                        PKFSOBJ pFsObj;
                        kHlpAssert(GetCurrentThreadId() == g_Sandbox.idMainThread);

                        pFsObj = kFsCacheLookupNoMissingA(g_pFsCache, pszFilename, &enmError);
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
                        /* These are for nasm and yasm header searching.  Cache will already
                           have checked the directories for the file, no need to call
                           CreateFile to do it again. */
                        else if (enmError == KFSLOOKUPERROR_NOT_FOUND)
                        {
                            KWFS_LOG(("CreateFileA(%s) -> INVALID_HANDLE_VALUE, ERROR_FILE_NOT_FOUND\n", pszFilename));
                            return INVALID_HANDLE_VALUE;
                        }
                        else if (   enmError == KFSLOOKUPERROR_PATH_COMP_NOT_FOUND
                                 || enmError == KFSLOOKUPERROR_PATH_COMP_NOT_DIR)
                        {
                            KWFS_LOG(("CreateFileA(%s) -> INVALID_HANDLE_VALUE, ERROR_PATH_NOT_FOUND\n", pszFilename));
                            return INVALID_HANDLE_VALUE;
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

    /*
     * Okay, normal.
     */
    hFile = CreateFileA(pszFilename, dwDesiredAccess, dwShareMode, pSecAttrs,
                        dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);
    if (hFile != INVALID_HANDLE_VALUE)
    {
        kHlpAssert(   KW_HANDLE_TO_INDEX(hFile) >= g_Sandbox.cHandles
                   || g_Sandbox.papHandles[KW_HANDLE_TO_INDEX(hFile)] == NULL);
    }
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
    /*
     * Check for temporary files (cl.exe only).
     */
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

    /*
     * Check for include files and similar that we do read-only caching of.
     */
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
                    if (kwFsIsCacheablePathExtensionW(pwszFilename, K_FALSE /*fAttrQuery*/))
                    {
                        KFSLOOKUPERROR enmError;
                        PKFSOBJ pFsObj;
                        kHlpAssert(GetCurrentThreadId() == g_Sandbox.idMainThread);

                        pFsObj = kFsCacheLookupNoMissingW(g_pFsCache, pwszFilename, &enmError);
                        if (pFsObj)
                        {
                            KBOOL fRc = kwFsObjCacheCreateFile(pFsObj, dwDesiredAccess, pSecAttrs && pSecAttrs->bInheritHandle,
                                                               &hFile);
                            kFsCacheObjRelease(g_pFsCache, pFsObj);
                            if (fRc)
                            {
                                KWFS_LOG(("CreateFileW(%ls) -> %p [cached]\n", pwszFilename, hFile));
                                return hFile;
                            }
                        }
                        /* These are for nasm and yasm style header searching.  Cache will
                           already have checked the directories for the file, no need to call
                           CreateFile to do it again. */
                        else if (enmError == KFSLOOKUPERROR_NOT_FOUND)
                        {
                            KWFS_LOG(("CreateFileW(%ls) -> INVALID_HANDLE_VALUE, ERROR_FILE_NOT_FOUND\n", pwszFilename));
                            return INVALID_HANDLE_VALUE;
                        }
                        else if (   enmError == KFSLOOKUPERROR_PATH_COMP_NOT_FOUND
                                 || enmError == KFSLOOKUPERROR_PATH_COMP_NOT_DIR)
                        {
                            KWFS_LOG(("CreateFileW(%ls) -> INVALID_HANDLE_VALUE, ERROR_PATH_NOT_FOUND\n", pwszFilename));
                            return INVALID_HANDLE_VALUE;
                        }

                        /* fallback */
                        hFile = CreateFileW(pwszFilename, dwDesiredAccess, dwShareMode, pSecAttrs,
                                            dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);
                        KWFS_LOG(("CreateFileW(%ls) -> %p (err=%u) [fallback]\n", pwszFilename, hFile, GetLastError()));
                        return hFile;
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

    /*
     * Okay, normal.
     */
    hFile = CreateFileW(pwszFilename, dwDesiredAccess, dwShareMode, pSecAttrs,
                        dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);
    if (hFile != INVALID_HANDLE_VALUE)
    {
        kHlpAssert(   KW_HANDLE_TO_INDEX(hFile) >= g_Sandbox.cHandles
                   || g_Sandbox.papHandles[KW_HANDLE_TO_INDEX(hFile)] == NULL);
    }
    KWFS_LOG(("CreateFileW(%ls) -> %p\n", pwszFilename, hFile));
    return hFile;
}


/** Kernel32 - SetFilePointer */
static DWORD WINAPI kwSandbox_Kernel32_SetFilePointer(HANDLE hFile, LONG cbMove, PLONG pcbMoveHi, DWORD dwMoveMethod)
{
    KUPTR const idxHandle = KW_HANDLE_TO_INDEX(hFile);
    kHlpAssert(GetCurrentThreadId() == g_Sandbox.idMainThread);
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
#endif
                case KWHANDLETYPE_TEMP_FILE_MAPPING:
                case KWHANDLETYPE_OUTPUT_BUF:
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
    kHlpAssert(GetCurrentThreadId() == g_Sandbox.idMainThread);
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
#endif
                case KWHANDLETYPE_TEMP_FILE_MAPPING:
                case KWHANDLETYPE_OUTPUT_BUF:
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
    kHlpAssert(GetCurrentThreadId() == g_Sandbox.idMainThread);
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

#ifdef WITH_HASH_MD5_CACHE
                    if (g_Sandbox.pHashHead)
                    {
                        g_Sandbox.LastHashRead.pCachedFile = pCachedFile;
                        g_Sandbox.LastHashRead.offRead     = pHandle->offFile;
                        g_Sandbox.LastHashRead.cbRead      = cbActually;
                        g_Sandbox.LastHashRead.pvRead      = pvBuffer;
                    }
#endif

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
#endif /* WITH_TEMP_MEMORY_FILES */

                case KWHANDLETYPE_TEMP_FILE_MAPPING:
                case KWHANDLETYPE_OUTPUT_BUF:
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
    kHlpAssert(GetCurrentThreadId() == g_Sandbox.idMainThread);
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

#ifdef WITH_STD_OUT_ERR_BUFFERING

/**
 * Write something to a handle, making sure everything is actually written.
 *
 * @param   hHandle         Where to write it to.
 * @param   pchBuf          What to write
 * @param   cchToWrite      How much to write.
 */
static void kwSandboxOutBufWriteIt(HANDLE hFile, char const *pchBuf, KU32 cchToWrite)
{
    if (cchToWrite > 0)
    {
        DWORD cchWritten = 0;
        if (WriteFile(hFile, pchBuf, cchToWrite, &cchWritten, NULL))
        {
            if (cchWritten == cchToWrite)
            { /* likely */ }
            else
            {
                do
                {
                    pchBuf += cchWritten;
                    cchToWrite -= cchWritten;
                    cchWritten = 0;
                } while (   cchToWrite > 0
                         && WriteFile(hFile, pchBuf, cchToWrite, &cchWritten, NULL));
            }
        }
        else
            kHlpAssertFailed();
    }
}


/**
 * Worker for WriteFile when the output isn't going to the console.
 *
 * @param   pSandbox            The sandbox.
 * @param   pOutBuf             The output buffer.
 * @param   pchBuffer           What to write.
 * @param   cchToWrite          How much to write.
 */
static void kwSandboxOutBufWrite(PKWSANDBOX pSandbox, PKWOUTPUTSTREAMBUF pOutBuf, const char *pchBuffer, KU32 cchToWrite)
{
    if (pOutBuf->u.Fully.cchBufAlloc > 0)
    { /* likely */ }
    else
    {
        /* No realloc, max size is 64KB. */
        pOutBuf->u.Fully.cchBufAlloc = 0x10000;
        pOutBuf->u.Fully.pchBuf = (char *)kHlpAlloc(pOutBuf->u.Fully.cchBufAlloc);
        if (!pOutBuf->u.Fully.pchBuf)
        {
            while (   !pOutBuf->u.Fully.pchBuf
                   && pOutBuf->u.Fully.cchBufAlloc > 64)
            {
                pOutBuf->u.Fully.cchBufAlloc /= 2;
                pOutBuf->u.Fully.pchBuf = (char *)kHlpAlloc(pOutBuf->u.Fully.cchBufAlloc);
            }
            if (!pOutBuf->u.Fully.pchBuf)
            {
                pOutBuf->u.Fully.cchBufAlloc = sizeof(pOutBuf->abPadding);
                pOutBuf->u.Fully.pchBuf      = pOutBuf->abPadding;
            }
        }
    }

    /*
     * Special case: Output ends with newline and fits in the buffer.
     */
    if (   cchToWrite > 1
        && pchBuffer[cchToWrite - 1] == '\n'
        && cchToWrite <= pOutBuf->u.Fully.cchBufAlloc - pOutBuf->u.Fully.cchBuf)
    {
        kHlpMemCopy(&pOutBuf->u.Fully.pchBuf[pOutBuf->u.Fully.cchBuf], pchBuffer, cchToWrite);
        pOutBuf->u.Fully.cchBuf += cchToWrite;
    }
    else
    {
        /*
         * Work thru the text line by line, flushing the buffer when
         * appropriate.  The buffer is not a line buffer here, it's a
         * full buffer.
         */
        while (cchToWrite > 0)
        {
            char const *pchNewLine = (const char *)memchr(pchBuffer, '\n', cchToWrite);
            KU32        cchLine    = pchNewLine ? (KU32)(pchNewLine - pchBuffer) + 1 : cchToWrite;
            if (cchLine <= pOutBuf->u.Fully.cchBufAlloc - pOutBuf->u.Fully.cchBuf)
            {
                kHlpMemCopy(&pOutBuf->u.Fully.pchBuf[pOutBuf->u.Fully.cchBuf], pchBuffer, cchLine);
                pOutBuf->u.Fully.cchBuf += cchLine;
            }
            /*
             * Option one: Flush the buffer and the current line.
             *
             * We choose this one when the line won't ever fit, or when we have
             * an incomplete line in the buffer.
             */
            else if (   cchLine >= pOutBuf->u.Fully.cchBufAlloc
                     || pOutBuf->u.Fully.cchBuf == 0
                     || pOutBuf->u.Fully.pchBuf[pOutBuf->u.Fully.cchBuf - 1] != '\n')
            {
                KWOUT_LOG(("kwSandboxOutBufWrite: flushing %u bytes, writing %u bytes\n", pOutBuf->u.Fully.cchBuf, cchLine));
                if (pOutBuf->u.Fully.cchBuf > 0)
                {
                    kwSandboxOutBufWriteIt(pOutBuf->hBackup, pOutBuf->u.Fully.pchBuf, pOutBuf->u.Fully.cchBuf);
                    pOutBuf->u.Fully.cchBuf = 0;
                }
                kwSandboxOutBufWriteIt(pOutBuf->hBackup, pchBuffer, cchLine);
            }
            /*
             * Option two: Only flush the lines in the buffer.
             */
            else
            {
                KWOUT_LOG(("kwSandboxOutBufWrite: flushing %u bytes\n", pOutBuf->u.Fully.cchBuf));
                kwSandboxOutBufWriteIt(pOutBuf->hBackup, pOutBuf->u.Fully.pchBuf, pOutBuf->u.Fully.cchBuf);
                kHlpMemCopy(&pOutBuf->u.Fully.pchBuf[0], pchBuffer, cchLine);
                pOutBuf->u.Fully.cchBuf = cchLine;
            }

            /* advance */
            pchBuffer  += cchLine;
            cchToWrite -= cchLine;
        }
    }
}

#endif  /* WITH_STD_OUT_ERR_BUFFERING */

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
                    cbNewSeg = 64*1024;
                    rc = kHlpPageAlloc((void **)&pTempFile->paSegs[cSegs].pbData, cbNewSeg, KPROT_READWRITE, K_FALSE);
                    if (rc != 0)
                    {
                        kHlpAssertFailed();
                        return K_FALSE;
                    }
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
#endif /* WITH_TEMP_MEMORY_FILES */


#if defined(WITH_TEMP_MEMORY_FILES) || defined(WITH_STD_OUT_ERR_BUFFERING)
/** Kernel32 - WriteFile */
static BOOL WINAPI kwSandbox_Kernel32_WriteFile(HANDLE hFile, LPCVOID pvBuffer, DWORD cbToWrite, LPDWORD pcbActuallyWritten,
                                                LPOVERLAPPED pOverlapped)
{
    KUPTR const idxHandle = KW_HANDLE_TO_INDEX(hFile);
    kHlpAssert(GetCurrentThreadId() == g_Sandbox.idMainThread);
    if (idxHandle < g_Sandbox.cHandles)
    {
        PKWHANDLE pHandle = g_Sandbox.papHandles[idxHandle];
        if (pHandle != NULL)
        {
            switch (pHandle->enmType)
            {
# ifdef WITH_TEMP_MEMORY_FILES
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

                    kHlpAssertFailed();
                    *pcbActuallyWritten = 0;
                    SetLastError(ERROR_NOT_ENOUGH_MEMORY);
                    return FALSE;
                }
# endif

                case KWHANDLETYPE_FSOBJ_READ_CACHE:
                    kHlpAssertFailed();
                    SetLastError(ERROR_ACCESS_DENIED);
                    *pcbActuallyWritten = 0;
                    return FALSE;

# if defined(WITH_STD_OUT_ERR_BUFFERING) || defined(WITH_CONSOLE_OUTPUT_BUFFERING)
                /*
                 * Standard output & error.
                 */
                case KWHANDLETYPE_OUTPUT_BUF:
                {
                    PKWOUTPUTSTREAMBUF pOutBuf = pHandle->u.pOutBuf;
                    if (pOutBuf->fIsConsole)
                    {
                        kwSandboxConsoleWriteA(&g_Sandbox, pOutBuf, (const char *)pvBuffer, cbToWrite);
                        KWOUT_LOG(("WriteFile(%p [console]) -> TRUE\n", hFile));
                    }
                    else
                    {
#  ifdef WITH_STD_OUT_ERR_BUFFERING
                        kwSandboxOutBufWrite(&g_Sandbox, pOutBuf, (const char *)pvBuffer, cbToWrite);
                        KWOUT_LOG(("WriteFile(%p [std%s], 's*.*', %#x) -> TRUE\n", hFile,
                                   pOutBuf == &g_Sandbox.StdErr ? "err" : "out", cbToWrite, cbToWrite, pvBuffer, cbToWrite));
#  else
                        kHlpAssertFailed();
#  endif
                    }
                    if (pcbActuallyWritten)
                        *pcbActuallyWritten = cbToWrite;
                    return TRUE;
                }
# endif

                default:
                case KWHANDLETYPE_TEMP_FILE_MAPPING:
                    kHlpAssertFailed();
                    SetLastError(ERROR_INVALID_FUNCTION);
                    *pcbActuallyWritten = 0;
                    return FALSE;
            }
        }
    }

    KWFS_LOG(("WriteFile(%p,,%#x)\n", hFile, cbToWrite));
    return WriteFile(hFile, pvBuffer, cbToWrite, pcbActuallyWritten, pOverlapped);
}


/** Kernel32 - WriteFileEx */
static BOOL WINAPI kwSandbox_Kernel32_WriteFileEx(HANDLE hFile, LPCVOID pvBuffer, DWORD cbToWrite, LPOVERLAPPED pOverlapped,
                                                  LPOVERLAPPED_COMPLETION_ROUTINE pfnCompletionRoutine)
{
    KUPTR const idxHandle = KW_HANDLE_TO_INDEX(hFile);
    kHlpAssert(GetCurrentThreadId() == g_Sandbox.idMainThread);
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

#endif /* WITH_TEMP_MEMORY_FILES || WITH_STD_OUT_ERR_BUFFERING */

#ifdef WITH_TEMP_MEMORY_FILES

/** Kernel32 - SetEndOfFile; */
static BOOL WINAPI kwSandbox_Kernel32_SetEndOfFile(HANDLE hFile)
{
    KUPTR const idxHandle = KW_HANDLE_TO_INDEX(hFile);
    kHlpAssert(GetCurrentThreadId() == g_Sandbox.idMainThread);
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

                case KWHANDLETYPE_OUTPUT_BUF:
                    kHlpAssertFailed();
                    SetLastError(pHandle->u.pOutBuf->fIsConsole ? ERROR_INVALID_OPERATION : ERROR_ACCESS_DENIED);
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
    kHlpAssert(GetCurrentThreadId() == g_Sandbox.idMainThread);
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

                case KWHANDLETYPE_OUTPUT_BUF:
                {
                    PKWOUTPUTSTREAMBUF pOutBuf = pHandle->u.pOutBuf;
                    DWORD fRet;
                    if (pOutBuf->fFileType != KU8_MAX)
                    {
                        fRet = (pOutBuf->fFileType & 0xf) | ((pOutBuf->fFileType & (FILE_TYPE_REMOTE >> 8)) << 8);
                        KWFS_LOG(("GetFileType(%p) -> %#x [outbuf]\n", hFile, fRet));
                    }
                    else
                    {
                        fRet = GetFileType(hFile);
                        KWFS_LOG(("GetFileType(%p) -> %#x [outbuf, fallback]\n", hFile, fRet));
                    }
                    return fRet;
                }

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
    kHlpAssert(GetCurrentThreadId() == g_Sandbox.idMainThread);
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

                case KWHANDLETYPE_OUTPUT_BUF:
                    /* do default */
                    break;

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
    kHlpAssert(GetCurrentThreadId() == g_Sandbox.idMainThread);
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

                case KWHANDLETYPE_OUTPUT_BUF:
                    /* do default */
                    break;

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


/** Kernel32 - CreateFileMappingW  */
static HANDLE WINAPI kwSandbox_Kernel32_CreateFileMappingW(HANDLE hFile, LPSECURITY_ATTRIBUTES pSecAttrs,
                                                           DWORD fProtect, DWORD dwMaximumSizeHigh,
                                                           DWORD dwMaximumSizeLow, LPCWSTR pwszName)
{
    HANDLE      hMapping;
    KUPTR const idxHandle = KW_HANDLE_TO_INDEX(hFile);
    kHlpAssert(GetCurrentThreadId() == g_Sandbox.idMainThread);
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
                        hMapping = kwFsTempFileCreateHandle(pHandle->u.pTempFile, GENERIC_READ, K_TRUE /*fMapping*/);
                        KWFS_LOG(("CreateFileMappingW(%p, %u) -> %p [temp]\n", hFile, fProtect, hMapping));
                        return hMapping;
                    }
                    kHlpAssertMsgFailed(("fProtect=%#x cb=%#x'%08x name=%p\n",
                                         fProtect, dwMaximumSizeHigh, dwMaximumSizeLow, pwszName));
                    SetLastError(ERROR_ACCESS_DENIED);
                    return INVALID_HANDLE_VALUE;
                }

                /* moc.exe benefits from this. */
                case KWHANDLETYPE_FSOBJ_READ_CACHE:
                {
                    PKFSWCACHEDFILE pCachedFile = pHandle->u.pCachedFile;
                    if (   (   fProtect == PAGE_READONLY
                            || fProtect == PAGE_EXECUTE_READ)
                        && dwMaximumSizeHigh == 0
                        &&  (   dwMaximumSizeLow == 0
                             || dwMaximumSizeLow == pCachedFile->cbCached)
                        && pwszName == NULL)
                    {
                        if (kwFsObjCacheCreateFileHandle(pCachedFile, GENERIC_READ, FALSE /*fInheritHandle*/,
                                                         K_FALSE /*fIsFileHandle*/, &hMapping))
                        { /* likely */ }
                        else
                            hMapping = NULL;
                        KWFS_LOG(("CreateFileMappingW(%p, %u) -> %p [temp]\n", hFile, fProtect, hMapping));
                        return hMapping;
                    }
                    kHlpAssertMsgFailed(("fProtect=%#x cb=%#x'%08x name=%p\n",
                                         fProtect, dwMaximumSizeHigh, dwMaximumSizeLow, pwszName));
                    SetLastError(ERROR_ACCESS_DENIED);
                    return INVALID_HANDLE_VALUE;


                }

                /** @todo read cached memory mapped files too for moc.   */
            }
        }
    }

    hMapping = CreateFileMappingW(hFile, pSecAttrs, fProtect, dwMaximumSizeHigh, dwMaximumSizeLow, pwszName);
    KWFS_LOG(("CreateFileMappingW(%p, %p, %#x, %#x, %#x, %p) -> %p\n",
              hFile, pSecAttrs, fProtect, dwMaximumSizeHigh, dwMaximumSizeLow, pwszName, hMapping));
    return hMapping;
}


/** Kernel32 - MapViewOfFile  */
static PVOID WINAPI kwSandbox_Kernel32_MapViewOfFile(HANDLE hSection, DWORD dwDesiredAccess,
                                                     DWORD offFileHigh, DWORD offFileLow, SIZE_T cbToMap)
{
    PVOID       pvRet;
    KUPTR const idxHandle = KW_HANDLE_TO_INDEX(hSection);
    kHlpAssert(GetCurrentThreadId() == g_Sandbox.idMainThread);
    if (idxHandle < g_Sandbox.cHandles)
    {
        PKWHANDLE pHandle = g_Sandbox.papHandles[idxHandle];
        if (pHandle != NULL)
        {
            KU32 idxMapping;

            /*
             * Ensure one free entry in the mapping tracking table first,
             * since this is common to both temporary and cached files.
             */
            if (g_Sandbox.cMemMappings + 1 <= g_Sandbox.cMemMappingsAlloc)
            { /* likely */ }
            else
            {
                void *pvNew;
                KU32 cNew = g_Sandbox.cMemMappingsAlloc;
                if (cNew)
                    cNew *= 2;
                else
                    cNew = 32;
                pvNew = kHlpRealloc(g_Sandbox.paMemMappings, cNew * sizeof(g_Sandbox.paMemMappings));
                if (pvNew)
                    g_Sandbox.paMemMappings = (PKWMEMMAPPING)pvNew;
                else
                {
                    kwErrPrintf("Failed to grow paMemMappings from %#x to %#x!\n", g_Sandbox.cMemMappingsAlloc, cNew);
                    SetLastError(ERROR_NOT_ENOUGH_MEMORY);
                    return NULL;
                }
                g_Sandbox.cMemMappingsAlloc = cNew;
            }

            /*
             * Type specific work.
             */
            switch (pHandle->enmType)
            {
                case KWHANDLETYPE_FSOBJ_READ_CACHE:
                case KWHANDLETYPE_TEMP_FILE:
                case KWHANDLETYPE_OUTPUT_BUF:
                default:
                    kHlpAssertFailed();
                    SetLastError(ERROR_INVALID_OPERATION);
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

                        pvRet = pTempFile->paSegs[0].pbData;
                        KWFS_LOG(("CreateFileMappingW(%p) -> %p [temp]\n", hSection, pvRet));
                        break;
                    }

                    kHlpAssertMsgFailed(("dwDesiredAccess=%#x offFile=%#x'%08x cbToMap=%#x (cbFile=%#x)\n",
                                         dwDesiredAccess, offFileHigh, offFileLow, cbToMap, pTempFile->cbFile));
                    SetLastError(ERROR_NOT_ENOUGH_MEMORY);
                    return NULL;
                }

                /*
                 * This is simple in comparison to the above temporary file code.
                 */
                case KWHANDLETYPE_FSOBJ_READ_CACHE_MAPPING:
                {
                    PKFSWCACHEDFILE pCachedFile = pHandle->u.pCachedFile;
                    if (   dwDesiredAccess == FILE_MAP_READ
                        && offFileHigh == 0
                        && offFileLow  == 0
                        && (cbToMap == 0 || cbToMap == pCachedFile->cbCached) )
                    {
                        pvRet = pCachedFile->pbCached;
                        KWFS_LOG(("CreateFileMappingW(%p) -> %p [cached]\n", hSection, pvRet));
                        break;
                    }
                    kHlpAssertMsgFailed(("dwDesiredAccess=%#x offFile=%#x'%08x cbToMap=%#x (cbFile=%#x)\n",
                                         dwDesiredAccess, offFileHigh, offFileLow, cbToMap, pCachedFile->cbCached));
                    SetLastError(ERROR_NOT_ENOUGH_MEMORY);
                    return NULL;
                }
            }

            /*
             * Insert into the mapping tracking table.  This is common
             * and we should only get here with a non-NULL pvRet.
             *
             * Note! We could look for duplicates and do ref counting, but it's
             *       easier to just append for now.
             */
            kHlpAssert(pvRet != NULL);
            idxMapping = g_Sandbox.cMemMappings;
            kHlpAssert(idxMapping < g_Sandbox.cMemMappingsAlloc);

            g_Sandbox.paMemMappings[idxMapping].cRefs         = 1;
            g_Sandbox.paMemMappings[idxMapping].pvMapping     = pvRet;
            g_Sandbox.paMemMappings[idxMapping].enmType       = pHandle->enmType;
            g_Sandbox.paMemMappings[idxMapping].u.pCachedFile = pHandle->u.pCachedFile;
            g_Sandbox.cMemMappings++;

            return pvRet;
        }
    }

    pvRet = MapViewOfFile(hSection, dwDesiredAccess, offFileHigh, offFileLow, cbToMap);
    KWFS_LOG(("MapViewOfFile(%p, %#x, %#x, %#x, %#x) -> %p\n",
              hSection, dwDesiredAccess, offFileHigh, offFileLow, cbToMap, pvRet));
    return pvRet;
}
/** @todo MapViewOfFileEx */


/** Kernel32 - UnmapViewOfFile  */
static BOOL WINAPI kwSandbox_Kernel32_UnmapViewOfFile(LPCVOID pvBase)
{
    /*
     * Consult the memory mapping tracker.
     */
    PKWMEMMAPPING   paMemMappings = g_Sandbox.paMemMappings;
    KU32            idxMapping    = g_Sandbox.cMemMappings;
    kHlpAssert(GetCurrentThreadId() == g_Sandbox.idMainThread);
    while (idxMapping-- > 0)
        if (paMemMappings[idxMapping].pvMapping == pvBase)
        {
            /* Type specific stuff. */
            if (paMemMappings[idxMapping].enmType == KWHANDLETYPE_TEMP_FILE_MAPPING)
            {
                KWFS_LOG(("UnmapViewOfFile(%p) -> TRUE [temp]\n", pvBase));
                paMemMappings[idxMapping].u.pTempFile->cMappings--;
            }
            else
                KWFS_LOG(("UnmapViewOfFile(%p) -> TRUE [cached]\n", pvBase));

            /* Deref and probably free it. */
            if (--paMemMappings[idxMapping].cRefs == 0)
            {
                g_Sandbox.cMemMappings--;
                if (idxMapping != g_Sandbox.cMemMappings)
                    paMemMappings[idxMapping] = paMemMappings[g_Sandbox.cMemMappings];
            }
            return TRUE;
        }

    KWFS_LOG(("UnmapViewOfFile(%p)\n", pvBase));
    return UnmapViewOfFile(pvBase);
}

/** @todo UnmapViewOfFileEx */

#endif /* WITH_TEMP_MEMORY_FILES */


/** Kernel32 - DuplicateHandle */
static BOOL WINAPI kwSandbox_Kernel32_DuplicateHandle(HANDLE hSrcProc, HANDLE hSrc, HANDLE hDstProc, PHANDLE phNew,
                                                      DWORD dwDesiredAccess, BOOL fInheritHandle, DWORD dwOptions)
{
    BOOL fRet;

    /*
     * We must catch our handles being duplicated.
     */
    if (hSrcProc == GetCurrentProcess())
    {
        KUPTR const idxHandle = KW_HANDLE_TO_INDEX(hSrc);
        kHlpAssert(GetCurrentThreadId() == g_Sandbox.idMainThread);
        if (idxHandle < g_Sandbox.cHandles)
        {
            PKWHANDLE pHandle = g_Sandbox.papHandles[idxHandle];
            if (pHandle)
            {
                fRet = DuplicateHandle(hSrcProc, hSrc, hDstProc, phNew, dwDesiredAccess, fInheritHandle, dwOptions);
                if (fRet)
                {
                    if (kwSandboxHandleTableEnter(&g_Sandbox, pHandle, *phNew))
                    {
                        pHandle->cRefs++;
                        KWFS_LOG(("DuplicateHandle(%p, %p, %p, , %#x, %d, %#x) -> TRUE, %p [intercepted handle] enmType=%d cRef=%d\n",
                                  hSrcProc, hSrc, hDstProc, dwDesiredAccess, fInheritHandle, dwOptions, *phNew,
                                  pHandle->enmType, pHandle->cRefs));
                    }
                    else
                    {
                        fRet = FALSE;
                        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
                        KWFS_LOG(("DuplicateHandle(%p, %p, %p, , %#x, %d, %#x) -> !FALSE!, %p [intercepted handle] enmType=%d\n",
                                  hSrcProc, hSrc, hDstProc, dwDesiredAccess, fInheritHandle, dwOptions, *phNew, pHandle->enmType));
                    }
                }
                else
                    KWFS_LOG(("DuplicateHandle(%p, %p, %p, , %#x, %d, %#x) -> FALSE [intercepted handle] enmType=%d\n",
                              hSrcProc, hSrc, hDstProc, dwDesiredAccess, fInheritHandle, dwOptions, pHandle->enmType));
                return fRet;
            }
        }
    }

    /*
     * Not one of ours, just do what the caller asks and log it.
     */
    fRet = DuplicateHandle(hSrcProc, hSrc, hDstProc, phNew, dwDesiredAccess, fInheritHandle, dwOptions);
    KWFS_LOG(("DuplicateHandle(%p, %p, %p, , %#x, %d, %#x) -> %d, %p\n", hSrcProc, hSrc, hDstProc, dwDesiredAccess,
              fInheritHandle, dwOptions, fRet, *phNew));
    return fRet;
}


/** Kernel32 - CloseHandle */
static BOOL WINAPI kwSandbox_Kernel32_CloseHandle(HANDLE hObject)
{
    BOOL        fRet;
    KUPTR const idxHandle = KW_HANDLE_TO_INDEX(hObject);
    kHlpAssert(GetCurrentThreadId() == g_Sandbox.idMainThread);
    if (idxHandle < g_Sandbox.cHandles)
    {
        PKWHANDLE pHandle = g_Sandbox.papHandles[idxHandle];
        if (pHandle)
        {
            /* Prevent the closing of the standard output and error handles. */
            if (   pHandle->enmType != KWHANDLETYPE_OUTPUT_BUF
                || idxHandle != KW_HANDLE_TO_INDEX(pHandle->hHandle))
            {
                fRet = CloseHandle(hObject);
                if (fRet)
                {
                    PKWHANDLE pHandle = g_Sandbox.papHandles[idxHandle];
                    g_Sandbox.papHandles[idxHandle] = NULL;
                    g_Sandbox.cActiveHandles--;
                    kHlpAssert(g_Sandbox.cActiveHandles >= g_Sandbox.cFixedHandles);
                    if (--pHandle->cRefs == 0)
                    {
#ifdef WITH_TEMP_MEMORY_FILES
                        if (pHandle->enmType == KWHANDLETYPE_TEMP_FILE)
                        {
                            kHlpAssert(pHandle->u.pTempFile->cActiveHandles > 0);
                            pHandle->u.pTempFile->cActiveHandles--;
                        }
#endif
                        kHlpFree(pHandle);
                        KWFS_LOG(("CloseHandle(%p) -> TRUE [intercepted handle, freed]\n", hObject));
                    }
                    else
                        KWFS_LOG(("CloseHandle(%p) -> TRUE [intercepted handle, not freed]\n", hObject));
                }
                else
                    KWFS_LOG(("CloseHandle(%p) -> FALSE [intercepted handle] err=%u!\n", hObject, GetLastError()));
            }
            else
            {
                KWFS_LOG(("CloseHandle(%p) -> TRUE [intercepted handle] Ignored closing of std%s!\n",
                          hObject, hObject == g_Sandbox.StdErr.hOutput ? "err" : "out"));
                fRet = TRUE;
            }
            return fRet;
        }
    }

    fRet = CloseHandle(hObject);
    KWFS_LOG(("CloseHandle(%p) -> %d\n", hObject, fRet));
    return fRet;
}


/** Kernel32 - GetFileAttributesA. */
static DWORD WINAPI kwSandbox_Kernel32_GetFileAttributesA(LPCSTR pszFilename)
{
    DWORD       fRet;
    const char *pszExt = kHlpGetExt(pszFilename);
    if (kwFsIsCacheableExtensionA(pszExt, K_TRUE /*fAttrQuery*/))
    {
        KFSLOOKUPERROR enmError;
        PKFSOBJ pFsObj;
        kHlpAssert(GetCurrentThreadId() == g_Sandbox.idMainThread);

        pFsObj = kFsCacheLookupNoMissingA(g_pFsCache, pszFilename, &enmError);
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
    if (kwFsIsCacheablePathExtensionW(pwszFilename, K_TRUE /*fAttrQuery*/))
    {
        KFSLOOKUPERROR enmError;
        PKFSOBJ pFsObj;
        kHlpAssert(GetCurrentThreadId() == g_Sandbox.idMainThread);

        pFsObj = kFsCacheLookupNoMissingW(g_pFsCache, pwszFilename, &enmError);
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


/** Kernel32 - GetShortPathNameW - c1[xx].dll of VS2010 does this to the
 * directory containing each include file.  We cache the result to speed
 * things up a little. */
static DWORD WINAPI kwSandbox_Kernel32_GetShortPathNameW(LPCWSTR pwszLongPath, LPWSTR pwszShortPath, DWORD cwcShortPath)
{
    DWORD cwcRet;
    if (kwFsIsCacheablePathExtensionW(pwszLongPath, K_TRUE /*fAttrQuery*/))
    {
        KFSLOOKUPERROR enmError;
        PKFSOBJ pObj;
        kHlpAssert(GetCurrentThreadId() == g_Sandbox.idMainThread);

        pObj = kFsCacheLookupW(g_pFsCache, pwszLongPath, &enmError);
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


#ifdef WITH_TEMP_MEMORY_FILES
/** Kernel32 - DeleteFileW
 * Skip deleting the in-memory files. */
static BOOL WINAPI kwSandbox_Kernel32_DeleteFileW(LPCWSTR pwszFilename)
{
    BOOL fRc;
    if (   g_Sandbox.pTool->u.Sandboxed.enmHint == KWTOOLHINT_VISUAL_CPP_CL
        && kwFsIsClTempFileW(pwszFilename))
    {
        kHlpAssert(GetCurrentThreadId() == g_Sandbox.idMainThread);
        KWFS_LOG(("DeleteFileW(%s) -> TRUE [temp]\n", pwszFilename));
        fRc = TRUE;
    }
    else
    {
        fRc = DeleteFileW(pwszFilename);
        KWFS_LOG(("DeleteFileW(%s) -> %d (%d)\n", pwszFilename, fRc, GetLastError()));
    }
    return fRc;
}
#endif /* WITH_TEMP_MEMORY_FILES */



#ifdef WITH_CONSOLE_OUTPUT_BUFFERING

/*
 *
 * Console output buffering.
 * Console output buffering.
 * Console output buffering.
 *
 */


/**
 * Write a wide char string to the console.
 *
 * @param   pSandbox            The sandbox which output buffer to flush.
 */
static void kwSandboxConsoleWriteIt(PKWSANDBOX pSandbox, wchar_t const *pwcBuf, KU32 cwcToWrite)
{
    if (cwcToWrite > 0)
    {
        DWORD cwcWritten = 0;
        if (WriteConsoleW(pSandbox->Combined.hOutput, pwcBuf, cwcToWrite, &cwcWritten, NULL))
        {
            if (cwcWritten == cwcToWrite)
            { /* likely */ }
            else
            {
                DWORD off = 0;
                do
                {
                    off += cwcWritten;
                    cwcWritten = 0;
                } while (   off < cwcToWrite
                         && WriteConsoleW(pSandbox->Combined.hOutput, &pwcBuf[off], cwcToWrite - off, &cwcWritten, NULL));
                kHlpAssert(off == cwcWritten);
            }
        }
        else
            kHlpAssertFailed();
        pSandbox->Combined.cFlushes++;
    }
}


/**
 * Flushes the combined console output buffer.
 *
 * @param   pSandbox            The sandbox which output buffer to flush.
 */
static void kwSandboxConsoleFlushCombined(PKWSANDBOX pSandbox)
{
    if (pSandbox->Combined.cwcBuf > 0)
    {
        KWOUT_LOG(("kwSandboxConsoleFlushCombined: %u wchars\n", pSandbox->Combined.cwcBuf));
        kwSandboxConsoleWriteIt(pSandbox, pSandbox->Combined.wszBuf, pSandbox->Combined.cwcBuf);
        pSandbox->Combined.cwcBuf = 0;
    }
}


/**
 * For handling combined buffer overflow cases line by line.
 *
 * @param   pSandbox            The sandbox.
 * @param   pwcBuf              What to add to the combined buffer.  Usually a
 *                              line, unless we're really low on buffer space.
 * @param   cwcBuf              The length of what to add.
 * @param   fBrokenLine         Whether this is a broken line.
 */
static void kwSandboxConsoleAddToCombined(PKWSANDBOX pSandbox, wchar_t const *pwcBuf, KU32 cwcBuf, KBOOL fBrokenLine)
{
    if (fBrokenLine)
        kwSandboxConsoleFlushCombined(pSandbox);
    if (pSandbox->Combined.cwcBuf + cwcBuf > K_ELEMENTS(pSandbox->Combined.wszBuf))
    {
        kwSandboxConsoleFlushCombined(pSandbox);
        kwSandboxConsoleWriteIt(pSandbox, pwcBuf, cwcBuf);
    }
    else
    {
        kHlpMemCopy(&pSandbox->Combined.wszBuf[pSandbox->Combined.cwcBuf], pwcBuf, cwcBuf * sizeof(wchar_t));
        pSandbox->Combined.cwcBuf += cwcBuf;
    }
}


/**
 * Called to final flush a line buffer via the combined buffer (if applicable).
 *
 * @param   pSandbox            The sandbox.
 * @param   pLineBuf            The line buffer.
 * @param   pszName             The line buffer name (for logging)
 */
static void kwSandboxConsoleFinalFlushLineBuf(PKWSANDBOX pSandbox, PKWOUTPUTSTREAMBUF pLineBuf, const char *pszName)
{
    if (pLineBuf->fIsConsole)
    {
        if (pLineBuf->u.Con.cwcBuf > 0)
        {
            KWOUT_LOG(("kwSandboxConsoleFinalFlushLineBuf: %s: %u wchars\n", pszName, pLineBuf->u.Con.cwcBuf));

            if (pLineBuf->u.Con.cwcBuf < pLineBuf->u.Con.cwcBufAlloc)
            {
                pLineBuf->u.Con.pwcBuf[pLineBuf->u.Con.cwcBuf++] = '\n';
                kwSandboxConsoleAddToCombined(pSandbox, pLineBuf->u.Con.pwcBuf, pLineBuf->u.Con.cwcBuf, K_FALSE /*fBrokenLine*/);
            }
            else
            {
                kwSandboxConsoleAddToCombined(pSandbox, pLineBuf->u.Con.pwcBuf, pLineBuf->u.Con.cwcBuf, K_TRUE /*fBrokenLine*/);
                kwSandboxConsoleAddToCombined(pSandbox, L"\n", 1, K_TRUE /*fBrokenLine*/);
            }
            pLineBuf->u.Con.cwcBuf = 0;
        }
    }
#ifdef WITH_STD_OUT_ERR_BUFFERING
    else if (pLineBuf->u.Fully.cchBuf > 0)
    {
        KWOUT_LOG(("kwSandboxConsoleFinalFlushLineBuf: %s: %u bytes\n", pszName, pLineBuf->u.Fully.cchBuf));

        kwSandboxOutBufWriteIt(pLineBuf->hBackup, pLineBuf->u.Fully.pchBuf, pLineBuf->u.Fully.cchBuf);
        pLineBuf->u.Fully.cchBuf = 0;
    }
#endif
}


/**
 * Called at the end of sandboxed execution to flush both stream buffers.
 *
 * @param   pSandbox            The sandbox.
 */
static void kwSandboxConsoleFlushAll(PKWSANDBOX pSandbox)
{
    /*
     * First do the cl.exe source file supression trick, if applicable.
     * The output ends up on CONOUT$ if either StdOut or StdErr is a console
     * handle.
     */
    if (   pSandbox->pTool->u.Sandboxed.enmHint == KWTOOLHINT_VISUAL_CPP_CL
        && pSandbox->Combined.cFlushes == 0)
    {
        if (   pSandbox->StdOut.fIsConsole
            || pSandbox->StdErr.fIsConsole)
        {
            if (   pSandbox->Combined.cwcBuf >= 3
                && (pSandbox->StdOut.fIsConsole ? pSandbox->StdOut.u.Con.cwcBuf : pSandbox->StdOut.u.Fully.cchBuf) == 0
                && (pSandbox->StdErr.fIsConsole ? pSandbox->StdErr.u.Con.cwcBuf : pSandbox->StdErr.u.Fully.cchBuf) == 0 )
            {
                KI32    off = pSandbox->Combined.cwcBuf - 1;
                if (pSandbox->Combined.wszBuf[off] == '\n')
                {
                    KBOOL fOk = K_TRUE;
                    while (off-- > 0)
                    {
                        wchar_t const wc = pSandbox->Combined.wszBuf[off];
                        if (iswalnum(wc) || wc == '.' || wc == ' ' || wc == '_' || wc == '-')
                        { /* likely */ }
                        else
                        {
                            fOk = K_FALSE;
                            break;
                        }
                    }
                    if (fOk)
                    {
                        KWOUT_LOG(("kwSandboxConsoleFlushAll: Dropping '%*.*ls in combined console buffer\n",
                                   pSandbox->Combined.cwcBuf, pSandbox->Combined.cwcBuf, pSandbox->Combined.wszBuf));
                        pSandbox->Combined.cwcBuf = 0;
                        return;
                    }
                }
                KWOUT_LOG(("kwSandboxConsoleFlushAll: Unable to drop '%*.*ls in combined console buffer\n",
                           pSandbox->Combined.cwcBuf, pSandbox->Combined.cwcBuf, pSandbox->Combined.wszBuf));
            }
        }
#ifdef WITH_STD_OUT_ERR_BUFFERING
        /*
         * Otherwise, it goes to standard output (redirected).
         */
        else if (   pSandbox->StdErr.u.Fully.cchBuf == 0
                 && pSandbox->StdOut.u.Fully.cchBuf >= 3)
        {
            char const *pchBuf = pSandbox->StdOut.u.Fully.pchBuf;
            KI32        off    = pSandbox->StdOut.u.Fully.cchBuf - 1;
            kHlpAssert(pSandbox->Combined.cFlushes == 0 && pSandbox->Combined.cwcBuf == 0); /* unused! */

            if (pchBuf[off] == '\n')
            {
                KBOOL fOk = K_TRUE;
                if (pchBuf[off - 1] == '\r')
                    off--;
                while (off-- > 0)
                {
                    char const ch = pchBuf[off];
                    if (isalnum(ch) || ch == '.' || ch == ' ' || ch == '_' || ch == '-')
                    { /* likely */ }
                    else
                    {
                        fOk = K_FALSE;
                        break;
                    }
                }
                if (fOk)
                {
                    KWOUT_LOG(("kwSandboxConsoleFlushAll: Dropping '%*.*s in stdout buffer\n",
                               pSandbox->StdOut.u.Fully.cchBuf, pSandbox->StdOut.u.Fully.cchBuf, pchBuf));
                    pSandbox->StdOut.u.Fully.cchBuf = 0;
                    return;
                }
            }
            KWOUT_LOG(("kwSandboxConsoleFlushAll: Unable to drop '%*.*s in stdout buffer\n",
                       pSandbox->StdOut.u.Fully.cchBuf, pSandbox->StdOut.u.Fully.cchBuf, pchBuf));
        }
#endif
    }

    /*
     * Flush the two line buffer, the the combined buffer.
     */
    kwSandboxConsoleFinalFlushLineBuf(pSandbox, &pSandbox->StdErr, "StdErr");
    kwSandboxConsoleFinalFlushLineBuf(pSandbox, &pSandbox->StdOut, "StdOut");
    kwSandboxConsoleFlushCombined(pSandbox);
}


/**
 * Writes a string to the given output stream.
 *
 * @param   pSandbox            The sandbox.
 * @param   pLineBuf            The line buffer for the output stream.
 * @param   pwcBuffer           The buffer to write.
 * @param   cwcToWrite          The number of wchar_t's in the buffer.
 */
static void kwSandboxConsoleWriteW(PKWSANDBOX pSandbox, PKWOUTPUTSTREAMBUF pLineBuf, wchar_t const *pwcBuffer, KU32 cwcToWrite)
{
    kHlpAssert(pLineBuf->fIsConsole);
    if (cwcToWrite > 0)
    {
        /*
         * First, find the start of the last incomplete line so we can figure
         * out how much line buffering we need to do.
         */
        KU32 cchLastIncompleteLine;
        KU32 offLastIncompleteLine = cwcToWrite;
        while (   offLastIncompleteLine > 0
               && pwcBuffer[offLastIncompleteLine - 1] != '\n')
            offLastIncompleteLine--;
        cchLastIncompleteLine = cwcToWrite - offLastIncompleteLine;

        /* Was there anything to line buffer? */
        if (offLastIncompleteLine < cwcToWrite)
        {
            /* Need to grow the line buffer? */
            KU32 cwcNeeded = offLastIncompleteLine != 0 ? offLastIncompleteLine : cchLastIncompleteLine + pLineBuf->u.Con.cwcBuf;
            if (cwcNeeded > pLineBuf->u.Con.cwcBufAlloc)
            {
                void *pvNew;
                KU32  cwcNew = !pLineBuf->u.Con.cwcBufAlloc ? 1024 : pLineBuf->u.Con.cwcBufAlloc * 2;
                while (cwcNew < cwcNeeded)
                    cwcNew *= 2;
                pvNew = kHlpRealloc(pLineBuf->u.Con.pwcBuf, cwcNew * sizeof(wchar_t));
                if (pvNew)
                {
                    pLineBuf->u.Con.pwcBuf = (wchar_t *)pvNew;
                    pLineBuf->u.Con.cwcBufAlloc = cwcNew;
                }
                else
                {
                    pvNew = kHlpRealloc(pLineBuf->u.Con.pwcBuf, cwcNeeded * sizeof(wchar_t));
                    if (pvNew)
                    {
                        pLineBuf->u.Con.pwcBuf = (wchar_t *)pvNew;
                        pLineBuf->u.Con.cwcBufAlloc = cwcNeeded;
                    }
                    else
                    {
                        /* This isn't perfect, but it will have to do for now. */
                        if (pLineBuf->u.Con.cwcBuf > 0)
                        {
                            kwSandboxConsoleAddToCombined(pSandbox, pLineBuf->u.Con.pwcBuf, pLineBuf->u.Con.cwcBuf,
                                                          K_TRUE /*fBrokenLine*/);
                            pLineBuf->u.Con.cwcBuf = 0;
                        }
                        kwSandboxConsoleAddToCombined(pSandbox, pwcBuffer, cwcToWrite, K_TRUE /*fBrokenLine*/);
                        return;
                    }
                }
            }

            /*
             * Handle the case where we only add to the line buffer.
             */
            if (offLastIncompleteLine == 0)
            {
                kHlpMemCopy(&pLineBuf->u.Con.pwcBuf[pLineBuf->u.Con.cwcBuf], pwcBuffer, cwcToWrite * sizeof(wchar_t));
                pLineBuf->u.Con.cwcBuf += cwcToWrite;
                return;
            }
        }

        /*
         * If there is sufficient combined buffer to handle this request, this are rather simple.
         */
        if (pLineBuf->u.Con.cwcBuf + cchLastIncompleteLine <= K_ELEMENTS(pSandbox->Combined.wszBuf))
        {
            if (pLineBuf->u.Con.cwcBuf > 0)
            {
                kHlpMemCopy(&pSandbox->Combined.wszBuf[pSandbox->Combined.cwcBuf],
                            pLineBuf->u.Con.pwcBuf, pLineBuf->u.Con.cwcBuf * sizeof(wchar_t));
                pSandbox->Combined.cwcBuf += pLineBuf->u.Con.cwcBuf;
                pLineBuf->u.Con.cwcBuf = 0;
            }

            kHlpMemCopy(&pSandbox->Combined.wszBuf[pSandbox->Combined.cwcBuf],
                        pwcBuffer, offLastIncompleteLine * sizeof(wchar_t));
            pSandbox->Combined.cwcBuf += offLastIncompleteLine;
        }
        else
        {
            /*
             * Do line-by-line processing of the input, flusing the combined buffer
             * when it becomes necessary.  We may have to write lines
             */
            KU32 off = 0;
            KU32 offNextLine = 0;

            /* If there is buffered chars, we handle the first line outside the
               main loop.  We must try our best outputting it as a complete line. */
            if (pLineBuf->u.Con.cwcBuf > 0)
            {
                while (offNextLine < cwcToWrite && pwcBuffer[offNextLine] != '\n')
                    offNextLine++;
                offNextLine++;
                kHlpAssert(offNextLine <= offLastIncompleteLine);

                if (pLineBuf->u.Con.cwcBuf + offNextLine + pSandbox->Combined.cwcBuf <= K_ELEMENTS(pSandbox->Combined.wszBuf))
                {
                    kHlpMemCopy(&pSandbox->Combined.wszBuf[pSandbox->Combined.cwcBuf],
                                pLineBuf->u.Con.pwcBuf, pLineBuf->u.Con.cwcBuf * sizeof(wchar_t));
                    pSandbox->Combined.cwcBuf += pLineBuf->u.Con.cwcBuf;
                    pLineBuf->u.Con.cwcBuf = 0;

                    kHlpMemCopy(&pSandbox->Combined.wszBuf[pSandbox->Combined.cwcBuf], pwcBuffer, offNextLine * sizeof(wchar_t));
                    pSandbox->Combined.cwcBuf += offNextLine;
                }
                else
                {
                    KU32 cwcLeft = pLineBuf->u.Con.cwcBufAlloc - pLineBuf->u.Con.cwcBuf;
                    if (cwcLeft > 0)
                    {
                        KU32 cwcCopy = K_MIN(cwcLeft, offNextLine);
                        kHlpMemCopy(&pLineBuf->u.Con.pwcBuf[pLineBuf->u.Con.cwcBuf], pwcBuffer, cwcCopy * sizeof(wchar_t));
                        pLineBuf->u.Con.cwcBuf += cwcCopy;
                        off += cwcCopy;
                    }
                    if (pLineBuf->u.Con.cwcBuf > 0)
                    {
                        kwSandboxConsoleAddToCombined(pSandbox, pLineBuf->u.Con.pwcBuf, pLineBuf->u.Con.cwcBuf,
                                                      K_TRUE /*fBrokenLine*/);
                        pLineBuf->u.Con.cwcBuf = 0;
                    }
                    if (off < offNextLine)
                        kwSandboxConsoleAddToCombined(pSandbox, &pwcBuffer[off], offNextLine - off, K_TRUE /*fBrokenLine*/);
                }
                off = offNextLine;
            }

            /* Deal with the remaining lines */
            while (off < offLastIncompleteLine)
            {
                while (offNextLine < offLastIncompleteLine && pwcBuffer[offNextLine] != '\n')
                    offNextLine++;
                offNextLine++;
                kHlpAssert(offNextLine <= offLastIncompleteLine);
                kwSandboxConsoleAddToCombined(pSandbox, &pwcBuffer[off], offNextLine - off, K_FALSE /*fBrokenLine*/);
                off = offNextLine;
            }
        }

        /*
         * Buffer any remaining incomplete line chars.
         */
        if (offLastIncompleteLine < cwcToWrite)
        {
            kHlpMemCopy(&pLineBuf->u.Con.pwcBuf[0], &pwcBuffer[offLastIncompleteLine], cchLastIncompleteLine * sizeof(wchar_t));
            pLineBuf->u.Con.cwcBuf = cchLastIncompleteLine;
        }
    }
}


/**
 * Worker for WriteConsoleA and WriteFile.
 *
 * @param   pSandbox            The sandbox.
 * @param   pLineBuf            The line buffer.
 * @param   pchBuffer           What to write.
 * @param   cchToWrite          How much to write.
 */
static void kwSandboxConsoleWriteA(PKWSANDBOX pSandbox, PKWOUTPUTSTREAMBUF pLineBuf, const char *pchBuffer, KU32 cchToWrite)
{
    /*
     * Convert it to wide char and use the 'W' to do the work.
     */
    int         cwcRet;
    KU32        cwcBuf = cchToWrite * 2 + 1;
    wchar_t    *pwcBufFree = NULL;
    wchar_t    *pwcBuf;
    kHlpAssert(pLineBuf->fIsConsole);

    if (cwcBuf <= 4096)
        pwcBuf = alloca(cwcBuf * sizeof(wchar_t));
    else
        pwcBuf = pwcBufFree = kHlpAlloc(cwcBuf * sizeof(wchar_t));

    cwcRet = MultiByteToWideChar(pSandbox->Combined.uCodepage, 0/*dwFlags*/, pchBuffer, cchToWrite, pwcBuf, cwcBuf);
    if (cwcRet > 0)
         kwSandboxConsoleWriteW(pSandbox, pLineBuf, pwcBuf, cwcRet);
    else
    {
        DWORD cchWritten;
        kHlpAssertFailed();

        /* Flush the line buffer and combined buffer before calling WriteConsoleA. */
        if (pLineBuf->u.Con.cwcBuf > 0)
        {
            kwSandboxConsoleAddToCombined(pSandbox, pLineBuf->u.Con.pwcBuf, pLineBuf->u.Con.cwcBuf, K_TRUE /*fBroken*/);
            pLineBuf->u.Con.cwcBuf = 0;
        }
        kwSandboxConsoleFlushCombined(pSandbox);

        if (WriteConsoleA(pLineBuf->hBackup, pchBuffer, cchToWrite, &cchWritten, NULL /*pvReserved*/))
        {
            if (cchWritten >= cchToWrite)
            { /* likely */ }
            else
            {
                KU32 off = 0;
                do
                {
                    off += cchWritten;
                    cchWritten = 0;
                } while (   off < cchToWrite
                         && WriteConsoleA(pLineBuf->hBackup, &pchBuffer[off], cchToWrite - off, &cchWritten, NULL));
            }
        }
    }

    if (pwcBufFree)
        kHlpFree(pwcBufFree);
}


/** Kernel32 - WriteConsoleA  */
BOOL WINAPI kwSandbox_Kernel32_WriteConsoleA(HANDLE hConOutput, CONST VOID *pvBuffer, DWORD cbToWrite, PDWORD pcbWritten,
                                             PVOID pvReserved)
{
    BOOL                fRc;
    PKWOUTPUTSTREAMBUF  pLineBuf;
    kHlpAssert(GetCurrentThreadId() == g_Sandbox.idMainThread);

    if (hConOutput == g_Sandbox.StdErr.hOutput)
        pLineBuf = &g_Sandbox.StdErr;
    else
        pLineBuf = &g_Sandbox.StdOut;
    if (pLineBuf->fIsConsole)
    {
        kwSandboxConsoleWriteA(&g_Sandbox, pLineBuf, (char const *)pvBuffer, cbToWrite);

        KWOUT_LOG(("WriteConsoleA: %p, %p LB %#x (%*.*s), %p, %p -> TRUE [cached]\n",
                   hConOutput, pvBuffer, cbToWrite, cbToWrite, cbToWrite, pvBuffer, pcbWritten, pvReserved));
        if (pcbWritten)
            *pcbWritten = cbToWrite;
        fRc = TRUE;
    }
    else
    {
        fRc = WriteConsoleA(hConOutput, pvBuffer, cbToWrite, pcbWritten, pvReserved);
        KWOUT_LOG(("WriteConsoleA: %p, %p LB %#x (%*.*s), %p, %p -> %d !fallback!\n",
                   hConOutput, pvBuffer, cbToWrite, cbToWrite, cbToWrite, pvBuffer, pcbWritten, pvReserved, fRc));
    }
    return fRc;
}


/** Kernel32 - WriteConsoleW  */
BOOL WINAPI kwSandbox_Kernel32_WriteConsoleW(HANDLE hConOutput, CONST VOID *pvBuffer, DWORD cwcToWrite, PDWORD pcwcWritten,
                                             PVOID pvReserved)
{
    BOOL                fRc;
    PKWOUTPUTSTREAMBUF  pLineBuf;
    kHlpAssert(GetCurrentThreadId() == g_Sandbox.idMainThread);

    if (hConOutput == g_Sandbox.StdErr.hOutput)
        pLineBuf = &g_Sandbox.StdErr;
    else if (hConOutput == g_Sandbox.StdOut.hOutput)
        pLineBuf = &g_Sandbox.StdOut;
    else
        pLineBuf = g_Sandbox.StdErr.fIsConsole ? &g_Sandbox.StdErr : &g_Sandbox.StdOut;
    if (pLineBuf->fIsConsole)
    {
        kwSandboxConsoleWriteW(&g_Sandbox, pLineBuf, (wchar_t const *)pvBuffer, cwcToWrite);

        KWOUT_LOG(("WriteConsoleW: %p, %p LB %#x (%*.*ls), %p, %p -> TRUE [cached]\n",
                   hConOutput, pvBuffer, cwcToWrite, cwcToWrite, cwcToWrite, pvBuffer, pcwcWritten, pvReserved));
        if (pcwcWritten)
            *pcwcWritten = cwcToWrite;
        fRc = TRUE;
    }
    else
    {
        fRc = WriteConsoleW(hConOutput, pvBuffer, cwcToWrite, pcwcWritten, pvReserved);
        KWOUT_LOG(("WriteConsoleW: %p, %p LB %#x (%*.*ls), %p, %p -> %d !fallback!\n",
                   hConOutput, pvBuffer, cwcToWrite, cwcToWrite, cwcToWrite, pvBuffer, pcwcWritten, pvReserved, fRc));
    }
    return fRc;
}

#endif /* WITH_CONSOLE_OUTPUT_BUFFERING */



/*
 *
 * Virtual memory leak prevension.
 * Virtual memory leak prevension.
 * Virtual memory leak prevension.
 *
 */

/** Kernel32 - VirtualAlloc - for c1[xx].dll 78GB leaks.   */
static PVOID WINAPI kwSandbox_Kernel32_VirtualAlloc(PVOID pvAddr, SIZE_T cb, DWORD fAllocType, DWORD fProt)
{
    PVOID pvMem = VirtualAlloc(pvAddr, cb, fAllocType, fProt);
    KW_LOG(("VirtualAlloc: pvAddr=%p cb=%p type=%#x prot=%#x -> %p (last=%d)\n",
            pvAddr, cb, fAllocType, fProt, pvMem, GetLastError()));
    if (   g_Sandbox.pTool->u.Sandboxed.enmHint == KWTOOLHINT_VISUAL_CPP_CL
        && pvMem)
    {
        PKWVIRTALLOC pTracker;
        kHlpAssert(GetCurrentThreadId() == g_Sandbox.idMainThread);

        pTracker = g_Sandbox.pVirtualAllocHead;
        while (   pTracker
               && (KUPTR)pvMem - (KUPTR)pTracker->pvAlloc >= pTracker->cbAlloc)
            pTracker = pTracker->pNext;
        if (!pTracker)
        {
            DWORD dwErr = GetLastError();
            PKWVIRTALLOC pTracker = (PKWVIRTALLOC)kHlpAlloc(sizeof(*pTracker));
            if (pTracker)
            {
                pTracker->pvAlloc = pvMem;
                pTracker->cbAlloc = cb;
                pTracker->pNext   = g_Sandbox.pVirtualAllocHead;
                g_Sandbox.pVirtualAllocHead = pTracker;
            }
            SetLastError(dwErr);
        }
    }
    return pvMem;
}


/** Kernel32 - VirtualFree.   */
static BOOL WINAPI kwSandbox_Kernel32_VirtualFree(PVOID pvAddr, SIZE_T cb, DWORD dwFreeType)
{
    BOOL fRc = VirtualFree(pvAddr, cb, dwFreeType);
    KW_LOG(("VirtualFree: pvAddr=%p cb=%p type=%#x -> %d\n", pvAddr, cb, dwFreeType, fRc));
    if (g_Sandbox.pTool->u.Sandboxed.enmHint == KWTOOLHINT_VISUAL_CPP_CL)
    {
        kHlpAssert(GetCurrentThreadId() == g_Sandbox.idMainThread);
        if (dwFreeType & MEM_RELEASE)
        {
            PKWVIRTALLOC pTracker = g_Sandbox.pVirtualAllocHead;
            if (pTracker)
            {
                if (pTracker->pvAlloc == pvAddr)
                    g_Sandbox.pVirtualAllocHead = pTracker->pNext;
                else
                {
                    PKWVIRTALLOC pPrev;
                    do
                    {
                        pPrev = pTracker;
                        pTracker = pTracker->pNext;
                    } while (pTracker && pTracker->pvAlloc != pvAddr);
                    if (pTracker)
                        pPrev->pNext = pTracker->pNext;
                }
                if (pTracker)
                    kHlpFree(pTracker);
                else
                    KW_LOG(("VirtualFree: pvAddr=%p not found!\n", pvAddr));
            }
        }
    }
    return fRc;
}


/** Kernel32 - HeapCreate / NtDll - RTlCreateHeap  */
HANDLE WINAPI kwSandbox_Kernel32_HeapCreate(DWORD fOptions, SIZE_T cbInitial, SIZE_T cbMax)
{
    HANDLE hHeap;
    kHlpAssert(GetCurrentThreadId() == g_Sandbox.idMainThread);

    hHeap = HeapCreate(fOptions, cbInitial, cbMax);
    if (hHeap != NULL)
    {
        DWORD dwErr = GetLastError();
        PKWHEAP pTracker = (PKWHEAP)kHlpAlloc(sizeof(*pTracker));
        if (pTracker)
        {
            pTracker->hHeap = hHeap;
            pTracker->pNext = g_Sandbox.pHeapHead;
            g_Sandbox.pHeapHead = pTracker;
        }

        SetLastError(dwErr);
    }
    return hHeap;

}


/** Kernel32 - HeapDestroy / NtDll - RTlDestroyHeap */
BOOL WINAPI kwSandbox_Kernel32_HeapDestroy(HANDLE hHeap)
{
    BOOL fRc = HeapDestroy(hHeap);
    KW_LOG(("HeapDestroy: hHeap=%p -> %d\n", hHeap, fRc));
    kHlpAssert(GetCurrentThreadId() == g_Sandbox.idMainThread);
    if (fRc)
    {
        PKWHEAP pTracker = g_Sandbox.pHeapHead;
        if (pTracker)
        {
            if (pTracker->hHeap == hHeap)
                g_Sandbox.pHeapHead = pTracker->pNext;
            else
            {
                PKWHEAP pPrev;
                do
                {
                    pPrev = pTracker;
                    pTracker = pTracker->pNext;
                } while (pTracker && pTracker->hHeap == hHeap);
                if (pTracker)
                    pPrev->pNext = pTracker->pNext;
            }
            if (pTracker)
                kHlpFree(pTracker);
            else
                KW_LOG(("HeapDestroy: pvAddr=%p not found!\n", hHeap));
        }
    }

    return fRc;
}



/*
 *
 * Thread/Fiber local storage leak prevention.
 * Thread/Fiber local storage leak prevention.
 * Thread/Fiber local storage leak prevention.
 *
 * Note! The FlsAlloc/Free & TlsAlloc/Free causes problems for statically
 *       linked VS2010 code like VBoxBs3ObjConverter.exe.  One thing is that
 *       we're leaking these indexes, but more importantely we crash during
 *       worker exit since the callback is triggered multiple times.
 */


/** Kernel32 - FlsAlloc  */
DWORD WINAPI kwSandbox_Kernel32_FlsAlloc(PFLS_CALLBACK_FUNCTION pfnCallback)
{
    DWORD idxFls = FlsAlloc(pfnCallback);
    KW_LOG(("FlsAlloc(%p) -> %#x\n", pfnCallback, idxFls));
    if (idxFls != FLS_OUT_OF_INDEXES)
    {
        PKWLOCALSTORAGE pTracker = (PKWLOCALSTORAGE)kHlpAlloc(sizeof(*pTracker));
        if (pTracker)
        {
            kHlpAssert(GetCurrentThreadId() == g_Sandbox.idMainThread);
            pTracker->idx = idxFls;
            pTracker->pNext = g_Sandbox.pFlsAllocHead;
            g_Sandbox.pFlsAllocHead = pTracker;
        }
    }

    return idxFls;
}

/** Kernel32 - FlsFree */
BOOL WINAPI kwSandbox_Kernel32_FlsFree(DWORD idxFls)
{
    BOOL fRc = FlsFree(idxFls);
    KW_LOG(("FlsFree(%#x) -> %d\n", idxFls, fRc));
    if (fRc)
    {
        PKWLOCALSTORAGE pTracker;
        kHlpAssert(GetCurrentThreadId() == g_Sandbox.idMainThread);

        pTracker = g_Sandbox.pFlsAllocHead;
        if (pTracker)
        {
            if (pTracker->idx == idxFls)
                g_Sandbox.pFlsAllocHead = pTracker->pNext;
            else
            {
                PKWLOCALSTORAGE pPrev;
                do
                {
                    pPrev = pTracker;
                    pTracker = pTracker->pNext;
                } while (pTracker && pTracker->idx != idxFls);
                if (pTracker)
                    pPrev->pNext = pTracker->pNext;
            }
            if (pTracker)
            {
                pTracker->idx   = FLS_OUT_OF_INDEXES;
                pTracker->pNext = NULL;
                kHlpFree(pTracker);
            }
        }
    }
    return fRc;
}


/** Kernel32 - TlsAlloc  */
DWORD WINAPI kwSandbox_Kernel32_TlsAlloc(VOID)
{
    DWORD idxTls = TlsAlloc();
    KW_LOG(("TlsAlloc() -> %#x\n", idxTls));
    if (idxTls != TLS_OUT_OF_INDEXES)
    {
        PKWLOCALSTORAGE pTracker = (PKWLOCALSTORAGE)kHlpAlloc(sizeof(*pTracker));
        if (pTracker)
        {
            kHlpAssert(GetCurrentThreadId() == g_Sandbox.idMainThread);
            pTracker->idx = idxTls;
            pTracker->pNext = g_Sandbox.pTlsAllocHead;
            g_Sandbox.pTlsAllocHead = pTracker;
        }
    }

    return idxTls;
}

/** Kernel32 - TlsFree */
BOOL WINAPI kwSandbox_Kernel32_TlsFree(DWORD idxTls)
{
    BOOL fRc = TlsFree(idxTls);
    KW_LOG(("TlsFree(%#x) -> %d\n", idxTls, fRc));
    if (fRc)
    {
        PKWLOCALSTORAGE pTracker;
        kHlpAssert(GetCurrentThreadId() == g_Sandbox.idMainThread);

        pTracker = g_Sandbox.pTlsAllocHead;
        if (pTracker)
        {
            if (pTracker->idx == idxTls)
                g_Sandbox.pTlsAllocHead = pTracker->pNext;
            else
            {
                PKWLOCALSTORAGE pPrev;
                do
                {
                    pPrev = pTracker;
                    pTracker = pTracker->pNext;
                } while (pTracker && pTracker->idx != idxTls);
                if (pTracker)
                    pPrev->pNext = pTracker->pNext;
            }
            if (pTracker)
            {
                pTracker->idx   = TLS_OUT_OF_INDEXES;
                pTracker->pNext = NULL;
                kHlpFree(pTracker);
            }
        }
    }
    return fRc;
}



/*
 *
 * Header file hashing.
 * Header file hashing.
 * Header file hashing.
 *
 * c1.dll / c1XX.dll hashes the input files.  The Visual C++ 2010 profiler
 * indicated that ~12% of the time was spent doing MD5 caluclation when
 * rebuiling openssl.  The hashing it done right after reading the source
 * via ReadFile, same buffers and sizes.
 */

#ifdef WITH_HASH_MD5_CACHE

/** AdvApi32 - CryptCreateHash */
static BOOL WINAPI kwSandbox_Advapi32_CryptCreateHash(HCRYPTPROV hProv, ALG_ID idAlg, HCRYPTKEY hKey, DWORD dwFlags,
                                                      HCRYPTHASH *phHash)
{
    BOOL fRc;

    /*
     * Only do this for cl.exe when it request normal MD5.
     */
    if (g_Sandbox.pTool->u.Sandboxed.enmHint == KWTOOLHINT_VISUAL_CPP_CL)
    {
        if (idAlg == CALG_MD5)
        {
            if (hKey == 0)
            {
                if (dwFlags == 0)
                {
                    PKWHASHMD5 pHash = (PKWHASHMD5)kHlpAllocZ(sizeof(*pHash));
                    if (pHash)
                    {
                        kHlpAssert(GetCurrentThreadId() == g_Sandbox.idMainThread);
                        pHash->uMagic        = KWHASHMD5_MAGIC;
                        pHash->cbHashed      = 0;
                        pHash->fGoneBad      = K_FALSE;
                        pHash->fFallbackMode = K_FALSE;
                        pHash->fFinal        = K_FALSE;

                        /* link it. */
                        pHash->pNext         = g_Sandbox.pHashHead;
                        g_Sandbox.pHashHead  = pHash;

                        *phHash = (KUPTR)pHash;
                        KWCRYPT_LOG(("CryptCreateHash(hProv=%p, idAlg=CALG_MD5, 0, 0, *phHash=%p) -> %d [cached]\n",
                                     hProv, *phHash, TRUE));
                        return TRUE;
                    }

                    kwErrPrintf("CryptCreateHash: out of memory!\n");
                }
                else
                    kwErrPrintf("CryptCreateHash: dwFlags=%p is not supported with CALG_MD5\n", hKey);
            }
            else
                kwErrPrintf("CryptCreateHash: hKey=%p is not supported with CALG_MD5\n", hKey);
        }
        else
            kwErrPrintf("CryptCreateHash: idAlg=%#x is not supported\n", idAlg);
    }

    /*
     * Fallback.
     */
    fRc = CryptCreateHash(hProv, idAlg, hKey, dwFlags, phHash);
    KWCRYPT_LOG(("CryptCreateHash(hProv=%p, idAlg=%#x (%d), hKey=%p, dwFlags=%#x, *phHash=%p) -> %d\n",
                 hProv, idAlg, idAlg, hKey, dwFlags, *phHash, fRc));
    return fRc;
}


/** AdvApi32 - CryptHashData */
static BOOL WINAPI kwSandbox_Advapi32_CryptHashData(HCRYPTHASH hHash, CONST BYTE *pbData, DWORD cbData, DWORD dwFlags)
{
    BOOL        fRc;
    PKWHASHMD5  pHash = g_Sandbox.pHashHead;
    kHlpAssert(GetCurrentThreadId() == g_Sandbox.idMainThread);
    while (pHash && (KUPTR)pHash != hHash)
        pHash = pHash->pNext;
    KWCRYPT_LOG(("CryptHashData(hHash=%p/%p, pbData=%p, cbData=%#x, dwFlags=%#x)\n",
                 hHash, pHash, pbData, cbData, dwFlags));
    if (pHash)
    {
        /*
         * Validate the state.
         */
        if (   pHash->uMagic == KWHASHMD5_MAGIC
            && !pHash->fFinal)
        {
            if (!pHash->fFallbackMode)
            {
                /*
                 * Does this match the previous ReadFile call to a cached file?
                 * If it doesn't, try falling back.
                 */
                if (   g_Sandbox.LastHashRead.cbRead == cbData
                    && g_Sandbox.LastHashRead.pvRead == (void *)pbData)
                {
                    PKFSWCACHEDFILE pCachedFile = g_Sandbox.LastHashRead.pCachedFile;
                    if (   pCachedFile
                        && kHlpMemComp(pbData, &pCachedFile->pbCached[g_Sandbox.LastHashRead.offRead], K_MIN(cbData, 64)) == 0)
                    {

                        if (g_Sandbox.LastHashRead.offRead == pHash->cbHashed)
                        {
                            if (   pHash->pCachedFile == NULL
                                && pHash->cbHashed == 0)
                                pHash->pCachedFile = pCachedFile;
                            if (pHash->pCachedFile == pCachedFile)
                            {
                                pHash->cbHashed += cbData;
                                g_Sandbox.LastHashRead.pCachedFile = NULL;
                                g_Sandbox.LastHashRead.pvRead      = NULL;
                                g_Sandbox.LastHashRead.cbRead      = 0;
                                g_Sandbox.LastHashRead.offRead     = 0;
                                KWCRYPT_LOG(("CryptHashData(hHash=%p/%p/%s, pbData=%p, cbData=%#x, dwFlags=%#x) -> 1 [cached]\n",
                                             hHash, pCachedFile, pCachedFile->szPath, pbData, cbData, dwFlags));
                                return TRUE;
                            }

                            /* Note! it's possible to fall back here too, if necessary. */
                            kwErrPrintf("CryptHashData: Expected pCachedFile=%p, last read was made to %p!!\n",
                                        pHash->pCachedFile, g_Sandbox.LastHashRead.pCachedFile);
                        }
                        else
                            kwErrPrintf("CryptHashData: Expected last read at %#x, instead it was made at %#x\n",
                                        pHash->cbHashed, g_Sandbox.LastHashRead.offRead);
                    }
                    else if (!pCachedFile)
                        kwErrPrintf("CryptHashData: Last pCachedFile is NULL when buffer address and size matches!\n");
                    else
                        kwErrPrintf("CryptHashData: First 64 bytes of the buffer doesn't match the cache.\n");
                }
                else if (g_Sandbox.LastHashRead.cbRead != 0 && pHash->cbHashed != 0)
                    kwErrPrintf("CryptHashData: Expected cbRead=%#x and pbData=%p, got %#x and %p instead\n",
                                g_Sandbox.LastHashRead.cbRead, g_Sandbox.LastHashRead.pvRead, cbData, pbData);
                if (pHash->cbHashed == 0)
                    pHash->fFallbackMode = K_TRUE;
                if (pHash->fFallbackMode)
                {
                    /* Initiate fallback mode (file that we don't normally cache, like .c/.cpp). */
                    pHash->fFallbackMode = K_TRUE;
                    MD5Init(&pHash->Md5Ctx);
                    MD5Update(&pHash->Md5Ctx, pbData, cbData);
                    pHash->cbHashed = cbData;
                    KWCRYPT_LOG(("CryptHashData(hHash=%p/fallback, pbData=%p, cbData=%#x, dwFlags=%#x) -> 1 [fallback!]\n",
                                 hHash, pbData, cbData, dwFlags));
                    return TRUE;
                }
                pHash->fGoneBad = K_TRUE;
                SetLastError(ERROR_INVALID_PARAMETER);
                fRc = FALSE;
            }
            else
            {
                /* fallback. */
                MD5Update(&pHash->Md5Ctx, pbData, cbData);
                pHash->cbHashed += cbData;
                fRc = TRUE;
                KWCRYPT_LOG(("CryptHashData(hHash=%p/fallback, pbData=%p, cbData=%#x, dwFlags=%#x) -> 1 [fallback]\n",
                             hHash, pbData, cbData, dwFlags));
            }
        }
        /*
         * Bad handle state.
         */
        else
        {
            if (pHash->uMagic != KWHASHMD5_MAGIC)
                kwErrPrintf("CryptHashData: Invalid cached hash handle!!\n");
            else
                kwErrPrintf("CryptHashData: Hash is already finalized!!\n");
            SetLastError(NTE_BAD_HASH);
            fRc = FALSE;
        }
    }
    else
    {

        fRc = CryptHashData(hHash, pbData, cbData, dwFlags);
        KWCRYPT_LOG(("CryptHashData(hHash=%p, pbData=%p, cbData=%#x, dwFlags=%#x) -> %d\n", hHash, pbData, cbData, dwFlags, fRc));
    }
    return fRc;
}


/** AdvApi32 - CryptGetHashParam */
static BOOL WINAPI kwSandbox_Advapi32_CryptGetHashParam(HCRYPTHASH hHash, DWORD dwParam,
                                                        BYTE *pbData, DWORD *pcbData, DWORD dwFlags)
{
    BOOL        fRc;
    PKWHASHMD5  pHash = g_Sandbox.pHashHead;
    kHlpAssert(GetCurrentThreadId() == g_Sandbox.idMainThread);
    while (pHash && (KUPTR)pHash != hHash)
        pHash = pHash->pNext;
    if (pHash)
    {
        if (pHash->uMagic == KWHASHMD5_MAGIC)
        {
            if (dwFlags == 0)
            {
                DWORD cbRet;
                void *pvRet;
                union
                {
                    DWORD dw;
                } uBuf;

                switch (dwParam)
                {
                    case HP_HASHVAL:
                    {
                        /* Check the hash progress. */
                        PKFSWCACHEDFILE pCachedFile = pHash->pCachedFile;
                        if (pCachedFile)
                        {
                            if (   pCachedFile->cbCached == pHash->cbHashed
                                && !pHash->fGoneBad)
                            {
                                if (pCachedFile->fValidMd5)
                                    KWCRYPT_LOG(("Already calculated hash for %p/%s! [hit]\n", pCachedFile, pCachedFile->szPath));
                                else
                                {
                                    MD5Init(&pHash->Md5Ctx);
                                    MD5Update(&pHash->Md5Ctx, pCachedFile->pbCached, pCachedFile->cbCached);
                                    MD5Final(pCachedFile->abMd5Digest, &pHash->Md5Ctx);
                                    pCachedFile->fValidMd5 = K_TRUE;
                                    KWCRYPT_LOG(("Calculated hash for %p/%s.\n", pCachedFile, pCachedFile->szPath));
                                }
                                pvRet = pCachedFile->abMd5Digest;
                            }
                            else
                            {
                                /* This actually happens (iprt/string.h + common/alloc/alloc.cpp), at least
                                   from what I can tell, so just deal with it. */
                                KWCRYPT_LOG(("CryptGetHashParam/HP_HASHVAL: Not at end of cached file! cbCached=%#x cbHashed=%#x fGoneBad=%d (%p/%p/%s)\n",
                                             pHash->pCachedFile->cbCached, pHash->cbHashed, pHash->fGoneBad,
                                             pHash, pCachedFile, pCachedFile->szPath));
                                pHash->fFallbackMode = K_TRUE;
                                pHash->pCachedFile   = NULL;
                                MD5Init(&pHash->Md5Ctx);
                                MD5Update(&pHash->Md5Ctx, pCachedFile->pbCached, pHash->cbHashed);
                                MD5Final(pHash->abDigest, &pHash->Md5Ctx);
                                pvRet = pHash->abDigest;
                            }
                            pHash->fFinal = K_TRUE;
                            cbRet = 16;
                            break;
                        }
                        else if (pHash->fFallbackMode)
                        {
                            if (!pHash->fFinal)
                            {
                                pHash->fFinal = K_TRUE;
                                MD5Final(pHash->abDigest, &pHash->Md5Ctx);
                            }
                            pvRet = pHash->abDigest;
                            cbRet = 16;
                            break;
                        }
                        else
                        {
                            kwErrPrintf("CryptGetHashParam/HP_HASHVAL: pCachedFile is NULL!!\n");
                            SetLastError(ERROR_INVALID_SERVER_STATE);
                        }
                        return FALSE;
                    }

                    case HP_HASHSIZE:
                        uBuf.dw = 16;
                        pvRet = &uBuf;
                        cbRet = sizeof(DWORD);
                        break;

                    case HP_ALGID:
                        uBuf.dw = CALG_MD5;
                        pvRet = &uBuf;
                        cbRet = sizeof(DWORD);
                        break;

                    default:
                        kwErrPrintf("CryptGetHashParam: Unknown dwParam=%#x\n", dwParam);
                        SetLastError(NTE_BAD_TYPE);
                        return FALSE;
                }

                /*
                 * Copy out cbRet from pvRet.
                 */
                if (pbData)
                {
                    if (*pcbData >= cbRet)
                    {
                        *pcbData = cbRet;
                        kHlpMemCopy(pbData, pvRet, cbRet);
                        if (cbRet == 4)
                            KWCRYPT_LOG(("CryptGetHashParam/%#x/%p/%p: TRUE, cbRet=%#x data=%#x [cached]\n",
                                         dwParam, pHash, pHash->pCachedFile, cbRet, (DWORD *)pbData));
                        else if (cbRet == 16)
                            KWCRYPT_LOG(("CryptGetHashParam/%#x/%p/%p: TRUE, cbRet=%#x data=%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x [cached]\n",
                                         dwParam, pHash, pHash->pCachedFile, cbRet,
                                         pbData[0],  pbData[1],  pbData[2],  pbData[3],
                                         pbData[4],  pbData[5],  pbData[6],  pbData[7],
                                         pbData[8],  pbData[9],  pbData[10], pbData[11],
                                         pbData[12], pbData[13], pbData[14], pbData[15]));
                        else
                            KWCRYPT_LOG(("CryptGetHashParam/%#x%/p%/%p: TRUE, cbRet=%#x [cached]\n",
                                         dwParam, pHash, pHash->pCachedFile, cbRet));
                        return TRUE;
                    }

                    kHlpMemCopy(pbData, pvRet, *pcbData);
                }
                SetLastError(ERROR_MORE_DATA);
                *pcbData = cbRet;
                KWCRYPT_LOG(("CryptGetHashParam/%#x: ERROR_MORE_DATA\n"));
            }
            else
            {
                kwErrPrintf("CryptGetHashParam: dwFlags is not zero: %#x!\n", dwFlags);
                SetLastError(NTE_BAD_FLAGS);
            }
        }
        else
        {
            kwErrPrintf("CryptGetHashParam: Invalid cached hash handle!!\n");
            SetLastError(NTE_BAD_HASH);
        }
        fRc = FALSE;
    }
    /*
     * Regular handle.
     */
    else
    {
        fRc = CryptGetHashParam(hHash, dwParam, pbData, pcbData, dwFlags);
        KWCRYPT_LOG(("CryptGetHashParam(hHash=%p, dwParam=%#x (%d), pbData=%p, *pcbData=%#x, dwFlags=%#x) -> %d\n",
                     hHash, dwParam, pbData, *pcbData, dwFlags, fRc));
    }

    return fRc;
}


/** AdvApi32 - CryptDestroyHash */
static BOOL WINAPI kwSandbox_Advapi32_CryptDestroyHash(HCRYPTHASH hHash)
{
    BOOL        fRc;
    PKWHASHMD5  pPrev = NULL;
    PKWHASHMD5  pHash = g_Sandbox.pHashHead;
    kHlpAssert(GetCurrentThreadId() == g_Sandbox.idMainThread);
    while (pHash && (KUPTR)pHash != hHash)
    {
        pPrev = pHash;
        pHash = pHash->pNext;
    }
    if (pHash)
    {
        if (pHash->uMagic == KWHASHMD5_MAGIC)
        {
            pHash->uMagic = 0;
            if (!pPrev)
                g_Sandbox.pHashHead = pHash->pNext;
            else
                pPrev->pNext = pHash->pNext;
            kHlpFree(pHash);
            KWCRYPT_LOG(("CryptDestroyHash(hHash=%p) -> 1 [cached]\n", hHash));
            fRc = TRUE;
        }
        else
        {
            kwErrPrintf("CryptDestroyHash: Invalid cached hash handle!!\n");
            KWCRYPT_LOG(("CryptDestroyHash(hHash=%p) -> FALSE! [cached]\n", hHash));
            SetLastError(ERROR_INVALID_HANDLE);
            fRc = FALSE;
        }
    }
    /*
     * Regular handle.
     */
    else
    {
        fRc = CryptDestroyHash(hHash);
        KWCRYPT_LOG(("CryptDestroyHash(hHash=%p) -> %d\n", hHash, fRc));
    }
    return fRc;
}

#endif /* WITH_HASH_MD5_CACHE */


/*
 *
 * Reuse crypt context.
 * Reuse crypt context.
 * Reuse crypt context.
 *
 *
 * This saves a little bit of time and registry accesses each time CL, C1 or C1XX runs.
 *
 */

#ifdef WITH_CRYPT_CTX_REUSE

/** AdvApi32 - CryptAcquireContextW.  */
static BOOL WINAPI kwSandbox_Advapi32_CryptAcquireContextW(HCRYPTPROV *phProv, LPCWSTR pwszContainer, LPCWSTR pwszProvider,
                                                           DWORD dwProvType,  DWORD dwFlags)
{
    BOOL fRet;

    /*
     * Lookup reusable context based on the input.
     */
    KSIZE const cwcContainer = pwszContainer ? kwUtf16Len(pwszContainer) : 0;
    KSIZE const cwcProvider  = pwszProvider  ? kwUtf16Len(pwszProvider) : 0;
    KU32        iCtx = g_Sandbox.cCryptCtxs;
    while (iCtx-- > 0)
    {
        if (   g_Sandbox.aCryptCtxs[iCtx].cwcContainer == cwcContainer
            && g_Sandbox.aCryptCtxs[iCtx].cwcProvider  == cwcProvider
            && g_Sandbox.aCryptCtxs[iCtx].dwProvType   == dwProvType
            && g_Sandbox.aCryptCtxs[iCtx].dwFlags      == dwFlags
            && kHlpMemComp(g_Sandbox.aCryptCtxs[iCtx].pwszContainer, pwszContainer, cwcContainer * sizeof(wchar_t)) == 0
            && kHlpMemComp(g_Sandbox.aCryptCtxs[iCtx].pwszProvider,  pwszProvider,  cwcProvider  * sizeof(wchar_t)) == 0)
        {
            if (CryptContextAddRef(g_Sandbox.aCryptCtxs[iCtx].hProv, NULL, 0))
            {
                *phProv = g_Sandbox.aCryptCtxs[iCtx].hProv;
                KWCRYPT_LOG(("CryptAcquireContextW(,%ls, %ls, %#x, %#x) -> TRUE, %p [reused]\n",
                             pwszContainer, pwszProvider, dwProvType, dwFlags, *phProv));
                return TRUE;
            }
        }
    }

    /*
     * Create it and enter it into the reused array if possible.
     */
    fRet = CryptAcquireContextW(phProv, pwszContainer, pwszProvider, dwProvType, dwFlags);
    if (fRet)
    {
        iCtx = g_Sandbox.cCryptCtxs;
        if (iCtx < K_ELEMENTS(g_Sandbox.aCryptCtxs))
        {
            /* Try duplicate the input strings. */
            g_Sandbox.aCryptCtxs[iCtx].pwszContainer = kHlpDup(pwszContainer ? pwszContainer : L"",
                                                               (cwcContainer + 1) * sizeof(wchar_t));
            if (g_Sandbox.aCryptCtxs[iCtx].pwszContainer)
            {
                g_Sandbox.aCryptCtxs[iCtx].pwszProvider  = kHlpDup(pwszProvider ? pwszProvider : L"",
                                                                   (cwcProvider + 1) * sizeof(wchar_t));
                if (g_Sandbox.aCryptCtxs[iCtx].pwszProvider)
                {
                    /* Add a couple of references just to be on the safe side and all that. */
                    HCRYPTPROV hProv = *phProv;
                    if (CryptContextAddRef(hProv, NULL, 0))
                    {
                        if (CryptContextAddRef(hProv, NULL, 0))
                        {
                            /* Okay, finish the entry and return success */
                            g_Sandbox.aCryptCtxs[iCtx].hProv      = hProv;
                            g_Sandbox.aCryptCtxs[iCtx].dwProvType = dwProvType;
                            g_Sandbox.aCryptCtxs[iCtx].dwFlags    = dwFlags;
                            g_Sandbox.cCryptCtxs = iCtx + 1;

                            KWCRYPT_LOG(("CryptAcquireContextW(,%ls, %ls, %#x, %#x) -> TRUE, %p [new]\n",
                                         pwszContainer, pwszProvider, dwProvType, dwFlags, *phProv));
                            return TRUE;
                        }
                        CryptReleaseContext(hProv, 0);
                    }
                    KWCRYPT_LOG(("CryptAcquireContextW: CryptContextAddRef failed!\n"));

                    kHlpFree(g_Sandbox.aCryptCtxs[iCtx].pwszProvider);
                    g_Sandbox.aCryptCtxs[iCtx].pwszProvider = NULL;
                }
                kHlpFree(g_Sandbox.aCryptCtxs[iCtx].pwszContainer);
                g_Sandbox.aCryptCtxs[iCtx].pwszContainer = NULL;
            }
        }
        else
            KWCRYPT_LOG(("CryptAcquireContextW: Too many crypt contexts to keep and reuse!\n"));
    }

    KWCRYPT_LOG(("CryptAcquireContextW(,%ls, %ls, %#x, %#x) -> %d, %p\n",
                 pwszContainer, pwszProvider, dwProvType, dwFlags, *phProv));
    return fRet;
}


/** AdvApi32 - CryptReleaseContext */
static BOOL WINAPI kwSandbox_Advapi32_CryptReleaseContext(HCRYPTPROV hProv, DWORD dwFlags)
{
    BOOL fRet = CryptReleaseContext(hProv, dwFlags);
    KWCRYPT_LOG(("CryptReleaseContext(%p,%#x) -> %d\n", hProv, dwFlags, fRet));
    return fRet;
}


/** AdvApi32 - CryptContextAddRef  */
static BOOL WINAPI kwSandbox_Advapi32_CryptContextAddRef(HCRYPTPROV hProv, DWORD *pdwReserved, DWORD dwFlags)
{
    BOOL fRet = CryptContextAddRef(hProv, pdwReserved, dwFlags);
    KWCRYPT_LOG(("CryptContextAddRef(%p,%p,%#x) -> %d\n", hProv, pdwReserved, dwFlags, fRet));
    return fRet;
}

#endif /* WITH_CRYPT_CTX_REUSE */

/*
 *
 * Structured exception handling.
 * Structured exception handling.
 * Structured exception handling.
 *
 */
#if defined(KBUILD_OS_WINDOWS) && defined(KBUILD_ARCH_X86)

# define EH_NONCONTINUABLE      KU32_C(0x00000001)
# define EH_UNWINDING           KU32_C(0x00000002)
# define EH_EXIT_UNWIND         KU32_C(0x00000004)
# define EH_STACK_INVALID       KU32_C(0x00000008)
# define EH_NESTED_CALL         KU32_C(0x00000010)

typedef KU32 (__cdecl * volatile PFNXCPTHANDLER)(PEXCEPTION_RECORD, struct _EXCEPTION_REGISTRATION_RECORD*, PCONTEXT,
                                                 struct _EXCEPTION_REGISTRATION_RECORD * volatile *);
typedef struct _EXCEPTION_REGISTRATION_RECORD
{
    struct _EXCEPTION_REGISTRATION_RECORD * volatile    pPrevRegRec;
    PFNXCPTHANDLER                                      pfnXcptHandler;
};


/**
 * Calls @a pfnHandler.
 */
static KU32 kwSandboxXcptCallHandler(PEXCEPTION_RECORD pXcptRec, struct _EXCEPTION_REGISTRATION_RECORD *pRegRec,
                                     PCONTEXT pXcptCtx, struct _EXCEPTION_REGISTRATION_RECORD * volatile * ppRegRec,
                                     PFNXCPTHANDLER pfnHandler)
{
# if 1
    /* This is a more robust version that isn't subject to calling
       convension cleanup disputes and such. */
    KU32 uSavedEdi;
    KU32 uSavedEsi;
    KU32 uSavedEbx;
    KU32 rcHandler;

    __asm
    {
        mov     [uSavedEdi], edi
        mov     [uSavedEsi], esi
        mov     [uSavedEbx], ebx
        mov     esi, esp
        mov     edi, esp
        mov     edi, [pXcptRec]
        mov     edx, [pRegRec]
        mov     eax, [pXcptCtx]
        mov     ebx, [ppRegRec]
        mov     ecx, [pfnHandler]
        sub     esp, 16
        and     esp, 0fffffff0h
        mov     [esp     ], edi
        mov     [esp +  4], edx
        mov     [esp +  8], eax
        mov     [esp + 12], ebx
        mov     edi, esi
        call    ecx
        mov     esp, esi
        cmp     esp, edi
        je      stack_ok
        int     3
    stack_ok:
        mov     edi, [uSavedEdi]
        mov     esi, [uSavedEsi]
        mov     ebx, [uSavedEbx]
        mov     [rcHandler], eax
    }
    return rcHandler;
# else
    return pfnHandler(pXcptRec, pRegRec, pXctpCtx, ppRegRec);
# endif
}


/**
 * Vectored exception handler that emulates x86 chained exception handler.
 *
 * This is necessary because the RtlIsValidHandler check fails for self loaded
 * code and prevents cl.exe from working.  (On AMD64 we can register function
 * tables, but on X86 cooking your own handling seems to be the only viabke
 * alternative.)
 *
 * @returns EXCEPTION_CONTINUE_SEARCH or EXCEPTION_CONTINUE_EXECUTION.
 * @param   pXcptPtrs           The exception details.
 */
static LONG CALLBACK kwSandboxVecXcptEmulateChained(PEXCEPTION_POINTERS pXcptPtrs)
{
    PNT_TIB pTib = (PNT_TIB)NtCurrentTeb();
    KW_LOG(("kwSandboxVecXcptEmulateChained: %#x\n", pXcptPtrs->ExceptionRecord->ExceptionCode));
    if (g_Sandbox.fRunning)
    {
        HANDLE const                                      hCurProc = GetCurrentProcess();
        PEXCEPTION_RECORD                                 pXcptRec = pXcptPtrs->ExceptionRecord;
        PCONTEXT                                          pXcptCtx = pXcptPtrs->ContextRecord;
        struct _EXCEPTION_REGISTRATION_RECORD *           pRegRec  = pTib->ExceptionList;
        while (((KUPTR)pRegRec & (sizeof(void *) - 3)) == 0 && pRegRec != NULL)
        {
            /* Read the exception record in a safe manner. */
            struct _EXCEPTION_REGISTRATION_RECORD   RegRec;
            DWORD                                   cbActuallyRead = 0;
            if (   ReadProcessMemory(hCurProc, pRegRec, &RegRec, sizeof(RegRec), &cbActuallyRead)
                && cbActuallyRead == sizeof(RegRec))
            {
                struct _EXCEPTION_REGISTRATION_RECORD * volatile    pDispRegRec = NULL;
                KU32                                                rcHandler;
                KW_LOG(("kwSandboxVecXcptEmulateChained: calling %p, pRegRec=%p, pPrevRegRec=%p\n",
                        RegRec.pfnXcptHandler, pRegRec, RegRec.pPrevRegRec));
                rcHandler = kwSandboxXcptCallHandler(pXcptRec, pRegRec, pXcptCtx, &pDispRegRec, RegRec.pfnXcptHandler);
                KW_LOG(("kwSandboxVecXcptEmulateChained: rcHandler=%#x pDispRegRec=%p\n", rcHandler, pDispRegRec));
                if (rcHandler == ExceptionContinueExecution)
                {
                    kHlpAssert(!(pXcptPtrs->ExceptionRecord->ExceptionFlags & EXCEPTION_NONCONTINUABLE));
                    KW_LOG(("kwSandboxVecXcptEmulateChained: returning EXCEPTION_CONTINUE_EXECUTION!\n"));
                    return EXCEPTION_CONTINUE_EXECUTION;
                }

                if (rcHandler == ExceptionContinueSearch)
                    kHlpAssert(!(pXcptPtrs->ExceptionRecord->ExceptionFlags & 8 /*EXCEPTION_STACK_INVALID*/));
                else if (rcHandler == ExceptionNestedException)
                    kHlpAssertMsgFailed(("Nested exceptions.\n"));
                else
                    kHlpAssertMsgFailed(("Invalid return %#x (%d).\n", rcHandler, rcHandler));
            }
            else
            {
                KW_LOG(("kwSandboxVecXcptEmulateChained: Bad xcpt chain entry at %p! Stopping search.\n", pRegRec));
                break;
            }

            /*
             * Next.
             */
            pRegRec = RegRec.pPrevRegRec;
        }
    }
    return EXCEPTION_CONTINUE_SEARCH;
}


/** NtDll,Kernel32 - RtlUnwind */
static VOID WINAPI kwSandbox_ntdll_RtlUnwind(struct _EXCEPTION_REGISTRATION_RECORD *pStopXcptRec, PVOID pvTargetIp,
                                             PEXCEPTION_RECORD pXcptRec, PVOID pvReturnValue)
{
    PNT_TIB pTib = (PNT_TIB)NtCurrentTeb();
    KW_LOG(("kwSandbox_ntdll_RtlUnwind: pStopXcptRec=%p pvTargetIp=%p pXctpRec=%p pvReturnValue=%p%s\n",
            pStopXcptRec, pvTargetIp, pXcptRec, pvReturnValue, g_Sandbox.fRunning ? "" : " [sandbox not running]"));
    if (g_Sandbox.fRunning)
    {
        HANDLE const                                      hCurProc = GetCurrentProcess();
        PCONTEXT                                          pXcptCtx = NULL;
        struct _EXCEPTION_REGISTRATION_RECORD *           pRegRec  = pTib->ExceptionList;

        /*
         * Update / create an exception record.
         */
        if (pXcptRec)
            pXcptRec->ExceptionFlags |= EH_UNWINDING;
        else
        {
            pXcptRec = (PEXCEPTION_RECORD)alloca(sizeof(*pXcptRec));
            kHlpMemSet(pXcptRec, 0, sizeof(*pXcptRec));
            pXcptRec->ExceptionCode  = STATUS_UNWIND;
            pXcptRec->ExceptionFlags = EH_UNWINDING;
        }
        if (!pStopXcptRec)
            pXcptRec->ExceptionFlags |= EH_EXIT_UNWIND;

        /*
         * Walk the chain till we find pStopXctpRec.
         */
        while (   ((KUPTR)pRegRec & (sizeof(void *) - 3)) == 0
               && pRegRec != NULL
               && pRegRec != pStopXcptRec)
        {
            /* Read the exception record in a safe manner. */
            struct _EXCEPTION_REGISTRATION_RECORD   RegRec;
            DWORD                                   cbActuallyRead = 0;
            if (   ReadProcessMemory(hCurProc, pRegRec, &RegRec, sizeof(RegRec), &cbActuallyRead)
                && cbActuallyRead == sizeof(RegRec))
            {
                struct _EXCEPTION_REGISTRATION_RECORD * volatile    pDispRegRec = NULL;
                KU32                                                rcHandler;
                KW_LOG(("kwSandbox_ntdll_RtlUnwind: calling %p, pRegRec=%p, pPrevRegRec=%p\n",
                        RegRec.pfnXcptHandler, pRegRec, RegRec.pPrevRegRec));
                rcHandler = kwSandboxXcptCallHandler(pXcptRec, pRegRec, pXcptCtx, &pDispRegRec, RegRec.pfnXcptHandler);
                KW_LOG(("kwSandbox_ntdll_RtlUnwind: rcHandler=%#x pDispRegRec=%p\n", rcHandler, pDispRegRec));

                if (rcHandler == ExceptionContinueSearch)
                    kHlpAssert(!(pXcptRec->ExceptionFlags & 8 /*EXCEPTION_STACK_INVALID*/));
                else if (rcHandler == ExceptionCollidedUnwind)
                    kHlpAssertMsgFailed(("Implement collided unwind!\n"));
                else
                    kHlpAssertMsgFailed(("Invalid return %#x (%d).\n", rcHandler, rcHandler));
            }
            else
            {
                KW_LOG(("kwSandbox_ntdll_RtlUnwind: Bad xcpt chain entry at %p! Stopping search.\n", pRegRec));
                break;
            }

            /*
             * Pop next.
             */
            pTib->ExceptionList = RegRec.pPrevRegRec;
            pRegRec = RegRec.pPrevRegRec;
        }
        return;
    }

    RtlUnwind(pStopXcptRec, pvTargetIp, pXcptRec, pvReturnValue);
}

#endif /* WINDOWS + X86 */


/*
 *
 * Misc function only intercepted while debugging.
 * Misc function only intercepted while debugging.
 * Misc function only intercepted while debugging.
 *
 */

#ifndef NDEBUG

/** CRT - memcpy   */
static void * __cdecl kwSandbox_msvcrt_memcpy(void *pvDst, void const *pvSrc, size_t cb)
{
    KU8 const *pbSrc = (KU8 const *)pvSrc;
    KU8       *pbDst = (KU8 *)pvDst;
    KSIZE      cbLeft = cb;
    while (cbLeft-- > 0)
        *pbDst++ = *pbSrc++;
    return pvDst;
}

#endif /* NDEBUG */



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
    { TUPLE("RtlPcToFileHeader"),           NULL,       (KUPTR)kwSandbox_ntdll_RtlPcToFileHeader },

    { TUPLE("GetCommandLineA"),             NULL,       (KUPTR)kwSandbox_Kernel32_GetCommandLineA },
    { TUPLE("GetCommandLineW"),             NULL,       (KUPTR)kwSandbox_Kernel32_GetCommandLineW },
    { TUPLE("GetStartupInfoA"),             NULL,       (KUPTR)kwSandbox_Kernel32_GetStartupInfoA },
    { TUPLE("GetStartupInfoW"),             NULL,       (KUPTR)kwSandbox_Kernel32_GetStartupInfoW },

    { TUPLE("CreateThread"),                NULL,       (KUPTR)kwSandbox_Kernel32_CreateThread },

    { TUPLE("GetEnvironmentStrings"),       NULL,       (KUPTR)kwSandbox_Kernel32_GetEnvironmentStrings },
    { TUPLE("GetEnvironmentStringsA"),      NULL,       (KUPTR)kwSandbox_Kernel32_GetEnvironmentStringsA },
    { TUPLE("GetEnvironmentStringsW"),      NULL,       (KUPTR)kwSandbox_Kernel32_GetEnvironmentStringsW },
    { TUPLE("FreeEnvironmentStringsA"),     NULL,       (KUPTR)kwSandbox_Kernel32_FreeEnvironmentStringsA },
    { TUPLE("FreeEnvironmentStringsW"),     NULL,       (KUPTR)kwSandbox_Kernel32_FreeEnvironmentStringsW },
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
    { TUPLE("DuplicateHandle"),             NULL,       (KUPTR)kwSandbox_Kernel32_DuplicateHandle },
    { TUPLE("CloseHandle"),                 NULL,       (KUPTR)kwSandbox_Kernel32_CloseHandle },
    { TUPLE("GetFileAttributesA"),          NULL,       (KUPTR)kwSandbox_Kernel32_GetFileAttributesA },
    { TUPLE("GetFileAttributesW"),          NULL,       (KUPTR)kwSandbox_Kernel32_GetFileAttributesW },
    { TUPLE("GetShortPathNameW"),           NULL,       (KUPTR)kwSandbox_Kernel32_GetShortPathNameW },
#ifdef WITH_TEMP_MEMORY_FILES
    { TUPLE("DeleteFileW"),                 NULL,       (KUPTR)kwSandbox_Kernel32_DeleteFileW },
#endif

    { TUPLE("WriteConsoleA"),               NULL,       (KUPTR)kwSandbox_Kernel32_WriteConsoleA },
    { TUPLE("WriteConsoleW"),               NULL,       (KUPTR)kwSandbox_Kernel32_WriteConsoleW },

    { TUPLE("VirtualAlloc"),                NULL,       (KUPTR)kwSandbox_Kernel32_VirtualAlloc },
    { TUPLE("VirtualFree"),                 NULL,       (KUPTR)kwSandbox_Kernel32_VirtualFree },

    { TUPLE("HeapCreate"),                  NULL,       (KUPTR)kwSandbox_Kernel32_HeapCreate,       K_TRUE /*fOnlyExe*/ },
    { TUPLE("HeapDestroy"),                 NULL,       (KUPTR)kwSandbox_Kernel32_HeapDestroy,      K_TRUE /*fOnlyExe*/ },

    { TUPLE("FlsAlloc"),                    NULL,       (KUPTR)kwSandbox_Kernel32_FlsAlloc,         K_TRUE /*fOnlyExe*/ },
    { TUPLE("FlsFree"),                     NULL,       (KUPTR)kwSandbox_Kernel32_FlsFree,          K_TRUE /*fOnlyExe*/ },
    { TUPLE("TlsAlloc"),                    NULL,       (KUPTR)kwSandbox_Kernel32_TlsAlloc,         K_TRUE /*fOnlyExe*/ },
    { TUPLE("TlsFree"),                     NULL,       (KUPTR)kwSandbox_Kernel32_TlsFree,          K_TRUE /*fOnlyExe*/ },

    { TUPLE("SetConsoleCtrlHandler"),       NULL,       (KUPTR)kwSandbox_Kernel32_SetConsoleCtrlHandler },

#if defined(KBUILD_OS_WINDOWS) && defined(KBUILD_ARCH_X86)
    { TUPLE("RtlUnwind"),                   NULL,       (KUPTR)kwSandbox_ntdll_RtlUnwind },
#endif

#ifdef WITH_HASH_MD5_CACHE
    { TUPLE("CryptCreateHash"),             NULL,       (KUPTR)kwSandbox_Advapi32_CryptCreateHash },
    { TUPLE("CryptHashData"),               NULL,       (KUPTR)kwSandbox_Advapi32_CryptHashData },
    { TUPLE("CryptGetHashParam"),           NULL,       (KUPTR)kwSandbox_Advapi32_CryptGetHashParam },
    { TUPLE("CryptDestroyHash"),            NULL,       (KUPTR)kwSandbox_Advapi32_CryptDestroyHash },
#endif

#ifdef WITH_CRYPT_CTX_REUSE
    { TUPLE("CryptAcquireContextW"),        NULL,       (KUPTR)kwSandbox_Advapi32_CryptAcquireContextW },
    { TUPLE("CryptReleaseContext"),         NULL,       (KUPTR)kwSandbox_Advapi32_CryptReleaseContext },
    { TUPLE("CryptContextAddRef"),          NULL,       (KUPTR)kwSandbox_Advapi32_CryptContextAddRef },
#endif

    /*
     * MS Visual C++ CRTs.
     */
    { TUPLE("exit"),                        NULL,       (KUPTR)kwSandbox_msvcrt_exit },
    { TUPLE("_exit"),                       NULL,       (KUPTR)kwSandbox_msvcrt__exit },
    { TUPLE("_cexit"),                      NULL,       (KUPTR)kwSandbox_msvcrt__cexit },
    { TUPLE("_c_exit"),                     NULL,       (KUPTR)kwSandbox_msvcrt__c_exit },
    { TUPLE("_amsg_exit"),                  NULL,       (KUPTR)kwSandbox_msvcrt__amsg_exit },
    { TUPLE("terminate"),                   NULL,       (KUPTR)kwSandbox_msvcrt_terminate },

    { TUPLE("onexit"),                      NULL,       (KUPTR)kwSandbox_msvcrt__onexit,            K_TRUE /*fOnlyExe*/ },
    { TUPLE("_onexit"),                     NULL,       (KUPTR)kwSandbox_msvcrt__onexit,            K_TRUE /*fOnlyExe*/ },
    { TUPLE("atexit"),                      NULL,       (KUPTR)kwSandbox_msvcrt_atexit,             K_TRUE /*fOnlyExe*/ },

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

#ifndef NDEBUG
    { TUPLE("memcpy"),                      NULL,       (KUPTR)kwSandbox_msvcrt_memcpy },
#endif
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
    { TUPLE("DuplicateHandle"),             NULL,       (KUPTR)kwSandbox_Kernel32_DuplicateHandle },
    { TUPLE("CloseHandle"),                 NULL,       (KUPTR)kwSandbox_Kernel32_CloseHandle },
    { TUPLE("GetFileAttributesA"),          NULL,       (KUPTR)kwSandbox_Kernel32_GetFileAttributesA },
    { TUPLE("GetFileAttributesW"),          NULL,       (KUPTR)kwSandbox_Kernel32_GetFileAttributesW },
    { TUPLE("GetShortPathNameW"),           NULL,       (KUPTR)kwSandbox_Kernel32_GetShortPathNameW },
#ifdef WITH_TEMP_MEMORY_FILES
    { TUPLE("DeleteFileW"),                 NULL,       (KUPTR)kwSandbox_Kernel32_DeleteFileW },
#endif
    { TUPLE("SetConsoleCtrlHandler"),       NULL,       (KUPTR)kwSandbox_Kernel32_SetConsoleCtrlHandler },

    { TUPLE("WriteConsoleA"),               NULL,       (KUPTR)kwSandbox_Kernel32_WriteConsoleA },
    { TUPLE("WriteConsoleW"),               NULL,       (KUPTR)kwSandbox_Kernel32_WriteConsoleW },

#ifdef WITH_HASH_MD5_CACHE
    { TUPLE("CryptCreateHash"),             NULL,       (KUPTR)kwSandbox_Advapi32_CryptCreateHash },
    { TUPLE("CryptHashData"),               NULL,       (KUPTR)kwSandbox_Advapi32_CryptHashData },
    { TUPLE("CryptGetHashParam"),           NULL,       (KUPTR)kwSandbox_Advapi32_CryptGetHashParam },
    { TUPLE("CryptDestroyHash"),            NULL,       (KUPTR)kwSandbox_Advapi32_CryptDestroyHash },
#endif

    { TUPLE("RtlPcToFileHeader"),           NULL,       (KUPTR)kwSandbox_ntdll_RtlPcToFileHeader },

#if defined(KBUILD_OS_WINDOWS) && defined(KBUILD_ARCH_X86)
    { TUPLE("RtlUnwind"),                   NULL,       (KUPTR)kwSandbox_ntdll_RtlUnwind },
#endif

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
 * Functions that needs replacing when queried by GetProcAddress.
 */
KWREPLACEMENTFUNCTION const g_aSandboxGetProcReplacements[] =
{
    /*
     * Kernel32.dll and friends.
     */
    { TUPLE("FlsAlloc"),                    NULL,       (KUPTR)kwSandbox_Kernel32_FlsAlloc, K_TRUE /*fOnlyExe*/ },
    { TUPLE("FlsFree"),                     NULL,       (KUPTR)kwSandbox_Kernel32_FlsFree,  K_TRUE /*fOnlyExe*/ },
    { TUPLE("TlsAlloc"),                    NULL,       (KUPTR)kwSandbox_Kernel32_TlsAlloc, K_TRUE /*fOnlyExe*/ },
    { TUPLE("TlsFree"),                     NULL,       (KUPTR)kwSandbox_Kernel32_TlsFree,  K_TRUE /*fOnlyExe*/ },
};
/** Number of entries in g_aSandboxGetProcReplacements. */
KU32 const                  g_cSandboxGetProcReplacements = K_ELEMENTS(g_aSandboxGetProcReplacements);


/**
 * Control handler.
 *
 * @returns TRUE if handled, FALSE if not.
 * @param   dwCtrlType          The signal.
 */
static BOOL WINAPI kwSandboxCtrlHandler(DWORD dwCtrlType)
{
    switch (dwCtrlType)
    {
        case CTRL_C_EVENT:
            fprintf(stderr, "kWorker: Ctrl-C\n");
            exit(9);
            break;

        case CTRL_BREAK_EVENT:
            fprintf(stderr, "kWorker: Ctrl-Break\n");
            exit(10);
            break;

        case CTRL_CLOSE_EVENT:
            fprintf(stderr, "kWorker: console closed\n");
            exit(11);
            break;

        case CTRL_LOGOFF_EVENT:
            fprintf(stderr, "kWorker: logoff event\n");
            exit(11);
            break;

        case CTRL_SHUTDOWN_EVENT:
            fprintf(stderr, "kWorker: shutdown event\n");
            exit(11);
            break;

        default:
            fprintf(stderr, "kwSandboxCtrlHandler: %#x\n", dwCtrlType);
            break;
    }
    return TRUE;
}


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
 * @param   hHandle             The handle value to enter it under (for the
 *                              duplicate handle API).
 */
static KBOOL kwSandboxHandleTableEnter(PKWSANDBOX pSandbox, PKWHANDLE pHandle, HANDLE hHandle)
{
    KUPTR const idxHandle = KW_HANDLE_TO_INDEX(hHandle);
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


/**
 * Creates a correctly quoted ANSI command line string from the given argv.
 *
 * @returns Pointer to the command line.
 * @param   cArgs               Number of arguments.
 * @param   papszArgs           The argument vector.
 * @param   fWatcomBrainDamange Whether to apply watcom rules while quoting.
 * @param   pcbCmdLine          Where to return the command line length,
 *                              including one terminator.
 */
static char *kwSandboxInitCmdLineFromArgv(KU32 cArgs, const char **papszArgs, KBOOL fWatcomBrainDamange, KSIZE *pcbCmdLine)
{
    KU32    i;
    KSIZE   cbCmdLine;
    char   *pszCmdLine;

    /* Make a copy of the argument vector that we'll be quoting. */
    char **papszQuotedArgs = alloca(sizeof(papszArgs[0]) * (cArgs + 1));
    kHlpMemCopy(papszQuotedArgs, papszArgs, sizeof(papszArgs[0]) * (cArgs + 1));

    /* Quote the arguments that need it. */
    quote_argv(cArgs, papszQuotedArgs, fWatcomBrainDamange, 0 /*leak*/);

    /* figure out cmd line length. */
    cbCmdLine = 0;
    for (i = 0; i < cArgs; i++)
        cbCmdLine += kHlpStrLen(papszQuotedArgs[i]) + 1;
    *pcbCmdLine = cbCmdLine;

    pszCmdLine = (char *)kHlpAlloc(cbCmdLine + 1);
    if (pszCmdLine)
    {
        char *psz = kHlpStrPCopy(pszCmdLine, papszQuotedArgs[0]);
        if (papszQuotedArgs[0] != papszArgs[0])
            free(papszQuotedArgs[0]);

        for (i = 1; i < cArgs; i++)
        {
            *psz++ = ' ';
            psz = kHlpStrPCopy(psz, papszQuotedArgs[i]);
            if (papszQuotedArgs[i] != papszArgs[i])
                free(papszQuotedArgs[i]);
        }
        kHlpAssert((KSIZE)(&psz[1] - pszCmdLine) == cbCmdLine);

        *psz++ = '\0';
        *psz++ = '\0';
    }

    return pszCmdLine;
}



static int kwSandboxInit(PKWSANDBOX pSandbox, PKWTOOL pTool,
                         KU32 cArgs, const char **papszArgs, KBOOL fWatcomBrainDamange,
                         KU32 cEnvVars, const char **papszEnvVars)
{
    PPEB pPeb = kwSandboxGetProcessEnvironmentBlock();
    PMY_RTL_USER_PROCESS_PARAMETERS pProcParams = (PMY_RTL_USER_PROCESS_PARAMETERS)pPeb->ProcessParameters;
    wchar_t *pwcPool;
    KSIZE cbStrings;
    KSIZE cwc;
    KSIZE cbCmdLine;
    KU32 i;

    /* Simple stuff. */
    pSandbox->rcExitCode    = 256;
    pSandbox->pTool         = pTool;
    pSandbox->idMainThread  = GetCurrentThreadId();
    pSandbox->pgmptr        = (char *)pTool->pszPath;
    pSandbox->wpgmptr       = (wchar_t *)pTool->pwszPath;
#ifdef WITH_CONSOLE_OUTPUT_BUFFERING
    if (pSandbox->StdOut.fIsConsole)
        pSandbox->StdOut.u.Con.cwcBuf   = 0;
    else
        pSandbox->StdOut.u.Fully.cchBuf = 0;
    if (pSandbox->StdErr.fIsConsole)
        pSandbox->StdErr.u.Con.cwcBuf   = 0;
    else
        pSandbox->StdErr.u.Fully.cchBuf = 0;
    pSandbox->Combined.cwcBuf   = 0;
    pSandbox->Combined.cFlushes = 0;
#endif
    pSandbox->cArgs         = cArgs;
    pSandbox->papszArgs     = (char **)papszArgs;
    pSandbox->pszCmdLine    = kwSandboxInitCmdLineFromArgv(cArgs, papszArgs, fWatcomBrainDamange, &cbCmdLine);
    if (!pSandbox->pszCmdLine)
        return KERR_NO_MEMORY;

    /*
     * Convert command line and argv to UTF-16.
     * We assume each ANSI char requires a surrogate pair in the UTF-16 variant.
     */
    pSandbox->papwszArgs = (wchar_t **)kHlpAlloc(sizeof(wchar_t *) * (pSandbox->cArgs + 2) + cbCmdLine * 2 * sizeof(wchar_t));
    if (!pSandbox->papwszArgs)
        return KERR_NO_MEMORY;
    pwcPool = (wchar_t *)&pSandbox->papwszArgs[pSandbox->cArgs + 2];
    for (i = 0; i < cArgs; i++)
    {
        *pwcPool++ = pSandbox->papszArgs[i][-1]; /* flags */
        pSandbox->papwszArgs[i] = pwcPool;
        pwcPool += kwStrToUtf16(pSandbox->papszArgs[i], pwcPool, (kHlpStrLen(pSandbox->papszArgs[i]) + 1) * 2);
        pwcPool++;
    }
    pSandbox->papwszArgs[pSandbox->cArgs + 0] = NULL;
    pSandbox->papwszArgs[pSandbox->cArgs + 1] = NULL;

    /*
     * Convert the commandline string to UTF-16, same pessimistic approach as above.
     */
    cbStrings = (cbCmdLine + 1) * 2 * sizeof(wchar_t);
    pSandbox->pwszCmdLine = kHlpAlloc(cbStrings);
    if (!pSandbox->pwszCmdLine)
        return KERR_NO_MEMORY;
    cwc = kwStrToUtf16(pSandbox->pszCmdLine, pSandbox->pwszCmdLine, cbStrings / sizeof(wchar_t));

    pSandbox->SavedCommandLine = pProcParams->CommandLine;
    pProcParams->CommandLine.Buffer = pSandbox->pwszCmdLine;
    pProcParams->CommandLine.Length = (USHORT)cwc * sizeof(wchar_t);

    /*
     * Setup the environment.
     */
    if (   cEnvVars + 2 <= pSandbox->cEnvVarsAllocated
        || kwSandboxGrowEnv(pSandbox, cEnvVars + 2) == 0)
    {
        KU32 iDst = 0;
        for (i = 0; i < cEnvVars; i++)
        {
            const char *pszVar   = papszEnvVars[i];
            KSIZE       cchVar   = kHlpStrLen(pszVar);
            if (   cchVar > 0
                && kHlpMemChr(pszVar, '=', cchVar) != NULL)
            {
                char       *pszCopy  = kHlpDup(pszVar, cchVar + 1);
                wchar_t    *pwszCopy = kwStrToUtf16AllocN(pszVar, cchVar + 1);
                if (pszCopy && pwszCopy)
                {
                    pSandbox->papszEnvVars[iDst]  = pszCopy;
                    pSandbox->environ[iDst]       = pszCopy;
                    pSandbox->papwszEnvVars[iDst] = pwszCopy;
                    pSandbox->wenviron[iDst]      = pwszCopy;
                    iDst++;
                }
                else
                {
                    kHlpFree(pszCopy);
                    kHlpFree(pwszCopy);
                    return kwErrPrintfRc(KERR_NO_MEMORY, "Out of memory setting up env vars!\n");
                }
            }
            else
                kwErrPrintf("kwSandboxInit: Skipping bad env var '%s'\n", pszVar);
        }
        pSandbox->papszEnvVars[iDst]  = NULL;
        pSandbox->environ[iDst]       = NULL;
        pSandbox->papwszEnvVars[iDst] = NULL;
        pSandbox->wenviron[iDst]      = NULL;
    }
    else
        return kwErrPrintfRc(KERR_NO_MEMORY, "Error setting up environment variables: kwSandboxGrowEnv failed\n");


    /*
     * Invalidate the volatile parts of cache (kBuild output directory,
     * temporary directory, whatever).
     */
    kFsCacheInvalidateCustomBoth(g_pFsCache);

#ifdef WITH_HISTORY
    /*
     * Record command line in debug history.
     */
    kHlpFree(g_apszHistory[g_iHistoryNext]);
    g_apszHistory[g_iHistoryNext] = kHlpStrDup(pSandbox->pszCmdLine);
    g_iHistoryNext = (g_iHistoryNext + 1) % K_ELEMENTS(g_apszHistory);
#endif

    return 0;
}


/**
 * Does sandbox cleanup between jobs.
 *
 * We postpone whatever isn't externally visible (i.e. files) and doesn't
 * influence the result, so that kmk can get on with things ASAP.
 *
 * @param   pSandbox            The sandbox.
 */
static void kwSandboxCleanupLate(PKWSANDBOX pSandbox)
{
    PROCESS_MEMORY_COUNTERS     MemInfo;
    PKWVIRTALLOC                pTracker;
    PKWHEAP                     pHeap;
    PKWLOCALSTORAGE             pLocalStorage;
#ifdef WITH_HASH_MD5_CACHE
    PKWHASHMD5                  pHash;
#endif
#ifdef WITH_TEMP_MEMORY_FILES
    PKWFSTEMPFILE               pTempFile;
#endif
    PKWEXITCALLACK              pExitCallback;

    /*
     * First stuff that may cause code to run.
     */

    /* Do exit callback first. */
    pExitCallback = g_Sandbox.pExitCallbackHead;
    g_Sandbox.pExitCallbackHead = NULL;
    while (pExitCallback)
    {
        PKWEXITCALLACK  pNext = pExitCallback->pNext;
        KW_LOG(("kwSandboxCleanupLate: calling %p %sexit handler\n",
                pExitCallback->pfnCallback, pExitCallback->fAtExit ? "at" : "_on"));
        __try
        {
            pExitCallback->pfnCallback();
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            KW_LOG(("kwSandboxCleanupLate: %sexit handler %p threw an exception!\n",
                    pExitCallback->fAtExit ? "at" : "_on", pExitCallback->pfnCallback));
            kHlpAssertFailed();
        }
        kHlpFree(pExitCallback);
        pExitCallback = pNext;
    }

    /* Free left behind FlsAlloc leaks. */
    pLocalStorage = g_Sandbox.pFlsAllocHead;
    g_Sandbox.pFlsAllocHead = NULL;
    while (pLocalStorage)
    {
        PKWLOCALSTORAGE pNext = pLocalStorage->pNext;
        KW_LOG(("Freeing leaked FlsAlloc index %#x\n", pLocalStorage->idx));
        FlsFree(pLocalStorage->idx);
        kHlpFree(pLocalStorage);
        pLocalStorage = pNext;
    }

    /* Free left behind TlsAlloc leaks. */
    pLocalStorage = g_Sandbox.pTlsAllocHead;
    g_Sandbox.pTlsAllocHead = NULL;
    while (pLocalStorage)
    {
        PKWLOCALSTORAGE pNext = pLocalStorage->pNext;
        KW_LOG(("Freeing leaked TlsAlloc index %#x\n", pLocalStorage->idx));
        TlsFree(pLocalStorage->idx);
        kHlpFree(pLocalStorage);
        pLocalStorage = pNext;
    }


    /*
     * Then free resources associated with the sandbox run.
     */

    /* Open handles, except fixed handles (stdout and stderr). */
    if (pSandbox->cActiveHandles > pSandbox->cFixedHandles)
    {
        KU32 idxHandle = pSandbox->cHandles;
        while (idxHandle-- > 0)
            if (pSandbox->papHandles[idxHandle] == NULL)
            { /* likely */ }
            else
            {
                PKWHANDLE pHandle = pSandbox->papHandles[idxHandle];
                if (   pHandle->enmType != KWHANDLETYPE_OUTPUT_BUF
                    || idxHandle != KW_HANDLE_TO_INDEX(pHandle->hHandle) )
                {
                    pSandbox->papHandles[idxHandle] = NULL;
                    pSandbox->cLeakedHandles++;

                    switch (pHandle->enmType)
                    {
                        case KWHANDLETYPE_FSOBJ_READ_CACHE:
                            KWFS_LOG(("Closing leaked read cache handle: %#x/%p cRefs=%d\n",
                                      idxHandle, pHandle->hHandle, pHandle->cRefs));
                            break;
                        case KWHANDLETYPE_FSOBJ_READ_CACHE_MAPPING:
                            KWFS_LOG(("Closing leaked read mapping handle: %#x/%p cRefs=%d\n",
                                      idxHandle, pHandle->hHandle, pHandle->cRefs));
                            break;
                        case KWHANDLETYPE_OUTPUT_BUF:
                            KWFS_LOG(("Closing leaked output buf handle: %#x/%p cRefs=%d\n",
                                      idxHandle, pHandle->hHandle, pHandle->cRefs));
                            break;
                        case KWHANDLETYPE_TEMP_FILE:
                            KWFS_LOG(("Closing leaked temp file  handle: %#x/%p cRefs=%d\n",
                                      idxHandle, pHandle->hHandle, pHandle->cRefs));
                            pHandle->u.pTempFile->cActiveHandles--;
                            break;
                        case KWHANDLETYPE_TEMP_FILE_MAPPING:
                            KWFS_LOG(("Closing leaked temp mapping handle: %#x/%p cRefs=%d\n",
                                      idxHandle, pHandle->hHandle, pHandle->cRefs));
                            pHandle->u.pTempFile->cActiveHandles--;
                            break;
                        default:
                            kHlpAssertFailed();
                    }
                    if (--pHandle->cRefs == 0)
                        kHlpFree(pHandle);
                    if (--pSandbox->cActiveHandles == pSandbox->cFixedHandles)
                        break;
                }
            }
        kHlpAssert(pSandbox->cActiveHandles == pSandbox->cFixedHandles);
    }

    /* Reset memory mappings - This assumes none of the DLLs keeps any of our mappings open! */
    g_Sandbox.cMemMappings = 0;

#ifdef WITH_TEMP_MEMORY_FILES
    /* The temporary files aren't externally visible, they're all in memory. */
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

    /* Free left behind HeapCreate leaks. */
    pHeap = g_Sandbox.pHeapHead;
    g_Sandbox.pHeapHead = NULL;
    while (pHeap != NULL)
    {
        PKWHEAP pNext = pHeap->pNext;
        KW_LOG(("Freeing HeapCreate leak %p\n", pHeap->hHeap));
        HeapDestroy(pHeap->hHeap);
        pHeap = pNext;
    }

    /* Free left behind VirtualAlloc leaks. */
    pTracker = g_Sandbox.pVirtualAllocHead;
    g_Sandbox.pVirtualAllocHead = NULL;
    while (pTracker)
    {
        PKWVIRTALLOC pNext = pTracker->pNext;
        KW_LOG(("Freeing VirtualFree leak %p LB %#x\n", pTracker->pvAlloc, pTracker->cbAlloc));
        VirtualFree(pTracker->pvAlloc, 0, MEM_RELEASE);
        kHlpFree(pTracker);
        pTracker = pNext;
    }

    /* Free the environment. */
    if (pSandbox->papszEnvVars)
    {
        KU32 i;
        for (i = 0; pSandbox->papszEnvVars[i]; i++)
            kHlpFree(pSandbox->papszEnvVars[i]);
        pSandbox->environ[0]      = NULL;
        pSandbox->papszEnvVars[0] = NULL;

        for (i = 0; pSandbox->papwszEnvVars[i]; i++)
            kHlpFree(pSandbox->papwszEnvVars[i]);
        pSandbox->wenviron[0]      = NULL;
        pSandbox->papwszEnvVars[0] = NULL;
    }

#ifdef WITH_HASH_MD5_CACHE
    /*
     * Hash handles.
     */
    pHash = pSandbox->pHashHead;
    pSandbox->pHashHead = NULL;
    while (pHash)
    {
        PKWHASHMD5 pNext = pHash->pNext;
        KWCRYPT_LOG(("Freeing leaked hash instance %#p\n", pHash));
        kHlpFree(pHash);
        pHash = pNext;
    }
#endif

    /*
     * Check the memory usage.  If it's getting high, trigger a respawn
     * after the next job.
     */
    MemInfo.WorkingSetSize = 0;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &MemInfo, sizeof(MemInfo)))
    {
        /* The first time thru, we figure out approximately when to restart
           based on installed RAM and CPU threads. */
        static KU64 s_cbMaxWorkingSet = 0;
        if (s_cbMaxWorkingSet != 0)
        { /* likely */ }
        else
        {
            SYSTEM_INFO SysInfo;
            MEMORYSTATUSEX GlobalMemInfo;
            const char    *pszValue;

            /* Calculate a reasonable estimate. */
            kHlpMemSet(&SysInfo, 0, sizeof(SysInfo));
            GetNativeSystemInfo(&SysInfo);

            kHlpMemSet(&GlobalMemInfo, 0, sizeof(GlobalMemInfo));
            GlobalMemInfo.dwLength = sizeof(GlobalMemInfo);
            if (!GlobalMemoryStatusEx(&GlobalMemInfo))
#if K_ARCH_BITS >= 64
                GlobalMemInfo.ullTotalPhys = KU64_C(0x000200000000); /* 8GB */
#else
                GlobalMemInfo.ullTotalPhys = KU64_C(0x000080000000); /* 2GB */
#endif
            s_cbMaxWorkingSet = GlobalMemInfo.ullTotalPhys / (K_MAX(SysInfo.dwNumberOfProcessors, 1) * 4);
            KW_LOG(("Raw estimate of s_cbMaxWorkingSet=%" KU64_PRI "\n", s_cbMaxWorkingSet));

            /* User limit. */
            pszValue = getenv("KWORKER_MEMORY_LIMIT");
            if (pszValue != NULL)
            {
                char         *pszNext;
                unsigned long ulValue = strtol(pszValue, &pszNext, 0);
                if (*pszNext == '\0' || *pszNext == 'M')
                    s_cbMaxWorkingSet = ulValue * (KU64)1048576;
                else if (*pszNext == 'K')
                    s_cbMaxWorkingSet = ulValue * (KU64)1024;
                else if (*pszNext == 'G')
                    s_cbMaxWorkingSet = ulValue * (KU64)1073741824;
                else
                    kwErrPrintf("Unable to grok KWORKER_MEMORY_LIMIT: %s\n", pszValue);
                KW_LOG(("User s_cbMaxWorkingSet=%" KU64_PRI "\n", s_cbMaxWorkingSet));
            }

            /* Clamp it a little. */
            if (s_cbMaxWorkingSet < 168*1024*1024)
                s_cbMaxWorkingSet = 168*1024*1024;
#if K_ARCH_BITS < 64
            else
                s_cbMaxWorkingSet = K_MIN(s_cbMaxWorkingSet,
                                          SysInfo.dwProcessorType != PROCESSOR_ARCHITECTURE_AMD64
                                          ?  512*1024*1024 /* Only got 2 or 3 GB VA */
                                          : 1536*1024*1024 /* got 4GB VA */);
#endif
            if (s_cbMaxWorkingSet > GlobalMemInfo.ullTotalPhys)
                s_cbMaxWorkingSet = GlobalMemInfo.ullTotalPhys;
            KW_LOG(("Final s_cbMaxWorkingSet=%" KU64_PRI "\n", s_cbMaxWorkingSet));
        }

        /* Finally the check. */
        if (MemInfo.WorkingSetSize >= s_cbMaxWorkingSet)
        {
            KW_LOG(("WorkingSetSize = %#x - > restart next time.\n", MemInfo.WorkingSetSize));
            g_fRestart = K_TRUE;
        }
    }

    /*
     * The CRT has a max of 8192 handles, so we better restart after a while if
     * someone is leaking handles or we risk running out of descriptors.
     *
     * Note! We only detect leaks for handles we intercept.  In the case of CL.EXE
     *       doing _dup2(1, 2) (stderr ==> stdout), there isn't actually a leak.
     */
    if (pSandbox->cLeakedHandles > 6000)
    {
        KW_LOG(("LeakedHandles = %#x - > restart next time.\n", pSandbox->cLeakedHandles));
        g_fRestart = K_TRUE;
    }
}


/**
 * Does essential cleanups and restoring, anything externally visible.
 *
 * All cleanups that aren't externally visible are postponed till after we've
 * informed kmk of the result, so it can be done in the dead time between jobs.
 *
 * @param   pSandbox            The sandbox.
 */
static void kwSandboxCleanup(PKWSANDBOX pSandbox)
{
    /*
     * Restore the parent command line string.
     */
    PPEB pPeb = kwSandboxGetProcessEnvironmentBlock();
    PMY_RTL_USER_PROCESS_PARAMETERS pProcParams = (PMY_RTL_USER_PROCESS_PARAMETERS)pPeb->ProcessParameters;
    pProcParams->CommandLine    = pSandbox->SavedCommandLine;
    pProcParams->StandardOutput = pSandbox->StdOut.hOutput;
    pProcParams->StandardError  = pSandbox->StdErr.hOutput; /* CL.EXE messes with this one. */
}


static int kwSandboxExec(PKWSANDBOX pSandbox, PKWTOOL pTool, KU32 cArgs, const char **papszArgs, KBOOL fWatcomBrainDamange,
                         KU32 cEnvVars, const char **papszEnvVars)
{
    int rcExit = 42;
    int rc;

    /*
     * Initialize the sandbox environment.
     */
    rc = kwSandboxInit(&g_Sandbox, pTool, cArgs, papszArgs, fWatcomBrainDamange, cEnvVars, papszEnvVars);
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
#if K_ARCH == K_ARCH_AMD64
            int (*pfnWin64Entrypoint)(void *pvPeb, void *, void *, void *);
#elif K_ARCH == K_ARCH_X86_32
            int (__cdecl *pfnWin32Entrypoint)(void *pvPeb);
#else
# error "Port me!"
#endif

            /* Save the NT TIB first (should do that here, not in some other function). */
            PNT_TIB pTib = (PNT_TIB)NtCurrentTeb();
            pSandbox->TibMainThread = *pTib;

            /* Make the call in a guarded fashion. */
#if K_ARCH == K_ARCH_AMD64
            /* AMD64 */
            *(KUPTR *)&pfnWin64Entrypoint = pTool->u.Sandboxed.uMainAddr;
            __try
            {
                pSandbox->pOutXcptListHead = pTib->ExceptionList;
                if (setjmp(pSandbox->JmpBuf) == 0)
                {
                    *(KU64*)(pSandbox->JmpBuf) = 0; /** @todo find other way to prevent longjmp from doing unwind! */
                    pSandbox->fRunning = K_TRUE;
                    rcExit = pfnWin64Entrypoint(kwSandboxGetProcessEnvironmentBlock(), NULL, NULL, NULL);
                    pSandbox->fRunning = K_FALSE;
                }
                else
                    rcExit = pSandbox->rcExitCode;
            }
#elif K_ARCH == K_ARCH_X86_32
            /* x86 (see _tmainCRTStartup) */
            *(KUPTR *)&pfnWin32Entrypoint = pTool->u.Sandboxed.uMainAddr;
            __try
            {
                pSandbox->pOutXcptListHead = pTib->ExceptionList;
                if (setjmp(pSandbox->JmpBuf) == 0)
                {
                    //*(KU64*)(pSandbox->JmpBuf) = 0; /** @todo find other way to prevent longjmp from doing unwind! */
                    pSandbox->fRunning = K_TRUE;
                    rcExit = pfnWin32Entrypoint(kwSandboxGetProcessEnvironmentBlock());
                    pSandbox->fRunning = K_FALSE;
                }
                else
                    rcExit = pSandbox->rcExitCode;
            }
#endif
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                kwErrPrintf("Caught exception %#x!\n", GetExceptionCode());
#ifdef WITH_HISTORY
                {
                    KU32 cPrinted = 0;
                    while (cPrinted++ < 5)
                    {
                        KU32 idx = (g_iHistoryNext + K_ELEMENTS(g_apszHistory) - cPrinted) % K_ELEMENTS(g_apszHistory);
                        if (g_apszHistory[idx])
                            kwErrPrintf("cmd[%d]: %s\n", 1 - cPrinted, g_apszHistory[idx]);
                    }
                }
#endif
                rcExit = 512;
            }
            pSandbox->fRunning = K_FALSE;

            /* Now, restore the NT TIB. */
            *pTib = pSandbox->TibMainThread;
        }
        else
            rcExit = 42 + 4;

        /*
         * Flush and clean up the essential bits only, postpone whatever we
         * can till after we've replied to kmk.
         */
#ifdef WITH_CONSOLE_OUTPUT_BUFFERING
        kwSandboxConsoleFlushAll(&g_Sandbox);
#endif
        kwSandboxCleanup(&g_Sandbox);
    }
    else
        rcExit = 42 + 3;

    return rcExit;
}


/**
 * Does the post command part of a job (optional).
 *
 * @returns The exit code of the job.
 * @param   cPostCmdArgs        Number of post command arguments (includes cmd).
 * @param   papszPostCmdArgs    The post command and its argument.
 */
static int kSubmitHandleJobPostCmd(KU32 cPostCmdArgs, const char **papszPostCmdArgs)
{
    const char *pszCmd = papszPostCmdArgs[0];

    /* Allow the kmk builtin prefix. */
    static const char s_szKmkBuiltinPrefix[] = "kmk_builtin_";
    if (kHlpStrNComp(pszCmd, s_szKmkBuiltinPrefix, sizeof(s_szKmkBuiltinPrefix) - 1) == 0)
        pszCmd += sizeof(s_szKmkBuiltinPrefix) - 1;

    /* Command switch. */
    if (kHlpStrComp(pszCmd, "kDepObj") == 0)
        return kmk_builtin_kDepObj(cPostCmdArgs, (char **)papszPostCmdArgs, NULL);

    return kwErrPrintfRc(42 + 5 , "Unknown post command: '%s'\n", pszCmd);
}


/**
 * Part 2 of the "JOB" command handler.
 *
 * @returns The exit code of the job.
 * @param   pszExecutable       The executable to execute.
 * @param   pszCwd              The current working directory of the job.
 * @param   cArgs               The number of arguments.
 * @param   papszArgs           The argument vector.
 * @param   fWatcomBrainDamange Whether to apply watcom rules while quoting.
 * @param   cEnvVars            The number of environment variables.
 * @param   papszEnvVars        The environment vector.
 * @param   cPostCmdArgs        Number of post command arguments (includes cmd).
 * @param   papszPostCmdArgs    The post command and its argument.
 */
static int kSubmitHandleJobUnpacked(const char *pszExecutable, const char *pszCwd,
                                    KU32 cArgs, const char **papszArgs, KBOOL fWatcomBrainDamange,
                                    KU32 cEnvVars, const char **papszEnvVars,
                                    KU32 cPostCmdArgs, const char **papszPostCmdArgs)
{
    int rcExit;
    PKWTOOL pTool;

    KW_LOG(("\n\nkSubmitHandleJobUnpacked: '%s' in '%s' cArgs=%u cEnvVars=%u cPostCmdArgs=%u\n",
            pszExecutable, pszCwd, cArgs, cEnvVars, cPostCmdArgs));
#ifdef KW_LOG_ENABLED
    {
        KU32 i;
        for (i = 0; i < cArgs; i++)
            KW_LOG(("  papszArgs[%u]=%s\n", i, papszArgs[i]));
        for (i = 0; i < cPostCmdArgs; i++)
            KW_LOG(("  papszPostCmdArgs[%u]=%s\n", i, papszPostCmdArgs[i]));
    }
#endif

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
                    rcExit = kwSandboxExec(&g_Sandbox, pTool, cArgs, papszArgs, fWatcomBrainDamange, cEnvVars, papszEnvVars);
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

        /*
         * Do the post command, if present.
         */
        if (cPostCmdArgs && rcExit == 0)
            rcExit = kSubmitHandleJobPostCmd(cPostCmdArgs, papszPostCmdArgs);
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
    KSIZE           cbTmp;

    pszMsg += sizeof("JOB");
    cbMsg  -= sizeof("JOB");

    /* Executable name. */
    pszExecutable = pszMsg;
    cbTmp = kHlpStrLen(pszMsg) + 1;
    pszMsg += cbTmp;
    if (   cbTmp < cbMsg
        && cbTmp > 2)
    {
        const char *pszCwd;
        cbMsg -= cbTmp;

        /* Current working directory. */
        pszCwd = pszMsg;
        cbTmp = kHlpStrLen(pszMsg) + 1;
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
                    KU32 i;
                    for (i = 0; i < cArgs; i++)
                    {
                        papszArgs[i] = pszMsg + 1; /* First byte is expansion flags for MSC & EMX. */
                        cbTmp = 1 + kHlpStrLen(pszMsg + 1) + 1;
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

                    /* Environment variable count. */
                    if (cbMsg > sizeof(KU32))
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
                                for (i = 0; i < cEnvVars; i++)
                                {
                                    papszEnvVars[i] = pszMsg;
                                    cbTmp = kHlpStrLen(pszMsg) + 1;
                                    pszMsg += cbTmp;
                                    if (cbTmp < cbMsg)
                                        cbMsg -= cbTmp;
                                    else
                                    {
                                        cbMsg = 0;
                                        break;
                                    }
                                }
                                papszEnvVars[cEnvVars] = 0;

                                /* Flags (currently just watcom argument brain damanage). */
                                if (cbMsg >= sizeof(KU8))
                                {
                                    KBOOL fWatcomBrainDamange = *pszMsg++;
                                    cbMsg--;

                                    /* Post command argument count (can be zero). */
                                    if (cbMsg >= sizeof(KU32))
                                    {
                                        KU32 cPostCmdArgs;
                                        kHlpMemCopy(&cPostCmdArgs, pszMsg, sizeof(cPostCmdArgs));
                                        pszMsg += sizeof(cPostCmdArgs);
                                        cbMsg  -= sizeof(cPostCmdArgs);

                                        if (cPostCmdArgs >= 0 && cPostCmdArgs < 32)
                                        {
                                            char const *apszPostCmdArgs[32+1];
                                            for (i = 0; i < cPostCmdArgs; i++)
                                            {
                                                apszPostCmdArgs[i] = pszMsg;
                                                cbTmp = kHlpStrLen(pszMsg) + 1;
                                                pszMsg += cbTmp;
                                                if (   cbTmp < cbMsg
                                                    || (cbTmp == cbMsg && i + 1 == cPostCmdArgs))
                                                    cbMsg -= cbTmp;
                                                else
                                                {
                                                    cbMsg = KSIZE_MAX;
                                                    break;
                                                }
                                            }
                                            if (cbMsg == 0)
                                            {
                                                apszPostCmdArgs[cPostCmdArgs] = NULL;

                                                /*
                                                 * The next step.
                                                 */
                                                rcExit = kSubmitHandleJobUnpacked(pszExecutable, pszCwd,
                                                                                  cArgs, papszArgs, fWatcomBrainDamange,
                                                                                  cEnvVars, papszEnvVars,
                                                                                  cPostCmdArgs, apszPostCmdArgs);
                                            }
                                            else if (cbMsg == KSIZE_MAX)
                                                kwErrPrintf("Detected bogus message unpacking post command and its arguments!\n");
                                            else
                                                kwErrPrintf("Message has %u bytes unknown trailing bytes\n", cbMsg);
                                        }
                                        else
                                            kwErrPrintf("Bogus post command argument count: %u %#x\n", cPostCmdArgs, cPostCmdArgs);
                                    }
                                    else
                                        kwErrPrintf("Detected bogus message looking for the post command argument count!\n");
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
    KU32        cEnvVars;
    KBOOL       fWatcomBrainDamange = K_FALSE;

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
            && (   strcmp(argv[i], "--chdir") == 0
                || strcmp(argv[i], "-C")      == 0 ) )
        {
            i++;
            if (i >= argc)
                return kwErrPrintfRc(2, "--chdir takes an argument!\n");
            pszCwd = argv[i++];
        }

        /* Optional watcom flag directory change. */
        if (   i < argc
            && (   strcmp(argv[i], "--wcc-brain-damage") == 0
                || strcmp(argv[i], "--watcom-brain-damage") == 0) )
        {
            fWatcomBrainDamange = K_TRUE;
            i++;
        }

        /* Trigger breakpoint */
        if (   i < argc
            && strcmp(argv[i], "--breakpoint") == 0)
        {
            __debugbreak();
            i++;
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
    cEnvVars = 0;
    while (environ[cEnvVars] != NULL)
        cEnvVars++;

    for (j = 0; j < cRepeats; j++)
    {
        rcExit = kSubmitHandleJobUnpacked(argv[i], pszCwd,
                                          argc - i, &argv[i], fWatcomBrainDamange,
                                          cEnvVars, environ,
                                          0, NULL);
        KW_LOG(("rcExit=%d\n", rcExit));
        kwSandboxCleanupLate(&g_Sandbox);
    }

# ifdef WITH_LOG_FILE
    if (g_hLogFile != INVALID_HANDLE_VALUE && g_hLogFile != NULL)
        CloseHandle(g_hLogFile);
# endif
    return rcExit;
}


int main(int argc, char **argv)
{
    KSIZE           cbMsgBuf = 0;
    KU8            *pbMsgBuf = NULL;
    int             i;
    HANDLE          hPipe = INVALID_HANDLE_VALUE;
    const char     *pszTmp;
    KFSLOOKUPERROR  enmIgnored;
#if defined(KBUILD_OS_WINDOWS) && defined(KBUILD_ARCH_X86)
    PVOID           pvVecXcptHandler = AddVectoredExceptionHandler(0 /*called last*/, kwSandboxVecXcptEmulateChained);
#endif
#ifdef WITH_CONSOLE_OUTPUT_BUFFERING
    HANDLE                          hCurProc       = GetCurrentProcess();
    PPEB                            pPeb           = kwSandboxGetProcessEnvironmentBlock();
    PMY_RTL_USER_PROCESS_PARAMETERS pProcessParams = (PMY_RTL_USER_PROCESS_PARAMETERS)pPeb->ProcessParameters;
    DWORD                           dwType;
#endif

    /*
     * Register our Control-C and Control-Break handlers.
     */
    if (!SetConsoleCtrlHandler(kwSandboxCtrlHandler, TRUE /*fAdd*/))
        return kwErrPrintfRc(3, "SetConsoleCtrlHandler failed: %u\n", GetLastError());

    /*
     * Create the cache and mark the temporary directory as using the custom revision.
     */
    g_pFsCache = kFsCacheCreate(KFSCACHE_F_MISSING_OBJECTS | KFSCACHE_F_MISSING_PATHS);
    if (!g_pFsCache)
        return kwErrPrintfRc(3, "kFsCacheCreate failed!\n");

    pszTmp = getenv("TEMP");
    if (pszTmp && *pszTmp != '\0')
        kFsCacheSetupCustomRevisionForTree(g_pFsCache, kFsCacheLookupA(g_pFsCache, pszTmp, &enmIgnored));
    pszTmp = getenv("TMP");
    if (pszTmp && *pszTmp != '\0')
        kFsCacheSetupCustomRevisionForTree(g_pFsCache, kFsCacheLookupA(g_pFsCache, pszTmp, &enmIgnored));
    pszTmp = getenv("TMPDIR");
    if (pszTmp && *pszTmp != '\0')
        kFsCacheSetupCustomRevisionForTree(g_pFsCache, kFsCacheLookupA(g_pFsCache, pszTmp, &enmIgnored));

    /*
     * Make g_abDefLdBuf executable.
     */
    if (!VirtualProtect(g_abDefLdBuf, sizeof(g_abDefLdBuf), PAGE_EXECUTE_READWRITE, &dwType))
        return kwErrPrintfRc(3, "VirtualProtect(%p, %#x, PAGE_EXECUTE_READWRITE,NULL) failed: %u\n",
                             g_abDefLdBuf, sizeof(g_abDefLdBuf), GetLastError());

#ifdef WITH_CONSOLE_OUTPUT_BUFFERING
    /*
     * Get and duplicate the console handles.
     */
    /* Standard output. */
    g_Sandbox.StdOut.hOutput = pProcessParams->StandardOutput;
    if (!DuplicateHandle(hCurProc, pProcessParams->StandardOutput, hCurProc, &g_Sandbox.StdOut.hBackup,
                         GENERIC_WRITE, FALSE /*fInherit*/, DUPLICATE_SAME_ACCESS))
        kHlpAssertFailedStmt(g_Sandbox.StdOut.hBackup = pProcessParams->StandardOutput);
    dwType = GetFileType(g_Sandbox.StdOut.hOutput);
    g_Sandbox.StdOut.fIsConsole = dwType == FILE_TYPE_CHAR;
    g_Sandbox.StdOut.fFileType  = (dwType & ~FILE_TYPE_REMOTE) < 0xf
                                ? (KU8)((dwType & ~FILE_TYPE_REMOTE) | (dwType >> 8)) : KU8_MAX;
    g_Sandbox.HandleStdOut.enmType         = KWHANDLETYPE_OUTPUT_BUF;
    g_Sandbox.HandleStdOut.cRefs           = 0x10001;
    g_Sandbox.HandleStdOut.dwDesiredAccess = GENERIC_WRITE;
    g_Sandbox.HandleStdOut.u.pOutBuf       = &g_Sandbox.StdOut;
    g_Sandbox.HandleStdOut.hHandle         = g_Sandbox.StdOut.hOutput;
    if (g_Sandbox.StdOut.hOutput != INVALID_HANDLE_VALUE)
    {
        if (kwSandboxHandleTableEnter(&g_Sandbox, &g_Sandbox.HandleStdOut, g_Sandbox.StdOut.hOutput))
            g_Sandbox.cFixedHandles++;
        else
            return kwErrPrintfRc(3, "kwSandboxHandleTableEnter failed for StdOut (%p)!\n", g_Sandbox.StdOut.hOutput);
    }
    KWOUT_LOG(("StdOut: hOutput=%p (%p) fIsConsole=%d dwType=%#x\n",
               g_Sandbox.StdOut.hOutput, g_Sandbox.StdOut.hBackup, g_Sandbox.StdOut.fIsConsole, dwType));

    /* Standard error. */
    g_Sandbox.StdErr.hOutput = pProcessParams->StandardError;
    if (!DuplicateHandle(hCurProc, pProcessParams->StandardError, hCurProc, &g_Sandbox.StdErr.hBackup,
                         GENERIC_WRITE, FALSE /*fInherit*/, DUPLICATE_SAME_ACCESS))
        kHlpAssertFailedStmt(g_Sandbox.StdErr.hBackup = pProcessParams->StandardError);
    dwType = GetFileType(g_Sandbox.StdErr.hOutput);
    g_Sandbox.StdErr.fIsConsole = dwType == FILE_TYPE_CHAR;
    g_Sandbox.StdErr.fFileType  = (dwType & ~FILE_TYPE_REMOTE) < 0xf
                                ? (KU8)((dwType & ~FILE_TYPE_REMOTE) | (dwType >> 8)) : KU8_MAX;
    g_Sandbox.HandleStdErr.enmType         = KWHANDLETYPE_OUTPUT_BUF;
    g_Sandbox.HandleStdErr.cRefs           = 0x10001;
    g_Sandbox.HandleStdErr.dwDesiredAccess = GENERIC_WRITE;
    g_Sandbox.HandleStdErr.u.pOutBuf       = &g_Sandbox.StdErr;
    g_Sandbox.HandleStdErr.hHandle         = g_Sandbox.StdErr.hOutput;
    if (   g_Sandbox.StdErr.hOutput != INVALID_HANDLE_VALUE
        && g_Sandbox.StdErr.hOutput != g_Sandbox.StdOut.hOutput)
    {
        if (kwSandboxHandleTableEnter(&g_Sandbox, &g_Sandbox.HandleStdErr, g_Sandbox.StdErr.hOutput))
            g_Sandbox.cFixedHandles++;
        else
            return kwErrPrintfRc(3, "kwSandboxHandleTableEnter failed for StdErr (%p)!\n", g_Sandbox.StdErr.hOutput);
    }
    KWOUT_LOG(("StdErr: hOutput=%p (%p) fIsConsole=%d dwType=%#x\n",
               g_Sandbox.StdErr.hOutput, g_Sandbox.StdErr.hBackup, g_Sandbox.StdErr.fIsConsole, dwType));

    /* Combined console buffer. */
    if (g_Sandbox.StdErr.fIsConsole)
    {
        g_Sandbox.Combined.hOutput   = g_Sandbox.StdErr.hBackup;
        g_Sandbox.Combined.uCodepage = GetConsoleCP();
    }
    else if (g_Sandbox.StdOut.fIsConsole)
    {
        g_Sandbox.Combined.hOutput   = g_Sandbox.StdOut.hBackup;
        g_Sandbox.Combined.uCodepage = GetConsoleCP();
    }
    else
    {
        g_Sandbox.Combined.hOutput   = INVALID_HANDLE_VALUE;
        g_Sandbox.Combined.uCodepage = CP_ACP;
    }
    KWOUT_LOG(("Combined: hOutput=%p uCodepage=%d\n", g_Sandbox.Combined.hOutput, g_Sandbox.Combined.uCodepage));
#endif /* WITH_CONSOLE_OUTPUT_BUFFERING */


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
        else if (strcmp(argv[i], "--volatile") == 0)
        {
            i++;
            if (i < argc)
                kFsCacheSetupCustomRevisionForTree(g_pFsCache, kFsCacheLookupA(g_pFsCache, argv[i], &enmIgnored));
            else
                return kwErrPrintfRc(2, "--volatile takes an argument!\n");
        }
        else if (strcmp(argv[i], "--test") == 0)
            return kwTestRun(argc - i - 1, &argv[i + 1]);
        else if (   strcmp(argv[i], "--help") == 0
                 || strcmp(argv[i], "-h") == 0
                 || strcmp(argv[i], "-?") == 0)
        {
            printf("usage: kWorker [--volatile dir] --pipe <pipe-handle>\n"
                   "usage: kWorker <--help|-h>\n"
                   "usage: kWorker <--version|-V>\n"
                   "usage: kWorker [--volatile dir] --test [<times> [--chdir <dir>] [--breakpoint] -- args\n"
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
                        {
                            kwSandboxCleanupLate(&g_Sandbox);
                            continue;
                        }
                    }
                    else
                        rc = kwErrPrintfRc(-1, "Unknown command: '%s'\n", psz);
                }
            }
            else
                rc = kwErrPrintfRc(-1, "Bogus message length: %u (%#x)\n", cbMsg, cbMsg);
        }

        /*
         * If we're exitting because we're restarting, we need to delay till
         * kmk/kSubmit has read the result.  Windows documentation says it
         * immediately discards pipe buffers once the pipe is broken by the
         * server (us).  So, We flush the buffer and queues a 1 byte read
         * waiting for kSubmit to close the pipe when it receives the
         * bExiting = K_TRUE result.
         */
        if (g_fRestart)
        {
            KU8 b;
            FlushFileBuffers(hPipe);
            ReadFile(hPipe, &b, 1, &cbMsg, NULL);
        }

        CloseHandle(hPipe);
#ifdef WITH_LOG_FILE
        if (g_hLogFile != INVALID_HANDLE_VALUE && g_hLogFile != NULL)
            CloseHandle(g_hLogFile);
#endif
        return rc > 0 ? 0 : 1;
    }
}


/** @page pg_kWorker    kSubmit / kWorker
 *
 * @section sec_kWorker_Motivation  Motivation / Inspiration
 *
 * The kSubmit / kWorker combo was conceived as a way to speed up VirtualBox
 * builds on machines "infested" by Anti Virus protection and disk encryption
 * software.  Build times jumping from 35-40 min to 77-82 min after the machine
 * got "infected".
 *
 * Speeing up builting of Boot Sector Kit \#3 was also hightly desirable. It is
 * mainly a bunch of tiny assembly and C files being compiler a million times.
 * As some of us OS/2 users maybe recalls, the Watcom make program can run its
 * own toolchain from within the same process, saving a lot of process creation
 * and teardown overhead.
 *
 *
 * @section sec_kWorker_kSubmit     About kSubmit
 *
 * When wanting to execute a job in a kWorker instance, it must be submitted
 * using the kmk_builtin_kSubmit command in kmk.  As the name suggest, this is
 * built into kmk and does not exist as an external program.  The reason for
 * this is that it keep track of the kWorker instances.
 *
 * The kSubmit command has the --32-bit and --64-bit options for selecting
 * between 32-bit and 64-bit worker instance.  We generally assume the user of
 * the command knows which bit count the executable has, so kSubmit is spared
 * the extra work of finding out.
 *
 * The kSubmit command shares a environment and current directory manipulation
 * with the kRedirect command, but not the file redirection.  So long no file
 * operation is involed, kSubmit is a drop in kRedirect replacement.  This is
 * hand for tools like OpenWatcom, NASM and YASM which all require environment
 * and/or current directory changes to work.
 *
 * Unlike the kRedirect command, the kSubmit command can also specify an
 * internall post command to be executed after the main command succeeds.
 * Currently only kmk_builtin_kDepObj is supported.  kDepObj gathers dependency
 * information from Microsoft COFF object files and Watcom OMF object files and
 * is scheduled to replace kDepIDB.
 *
 *
 * @section sec_kWorker_Interaction kSubmit / kWorker interaction
 *
 * The kmk_builtin_kSubmit communicates with the kWorker instances over pipes.
 * A job request is written by kSubmit and kWorker read, unpacks it and executes
 * it.  When the job is completed, kWorker writes a short reply with the exit
 * code and an internal status indicating whether it is going to restart.
 *
 * The kWorker intance will reply to kSubmit before completing all the internal
 * cleanup work, so as not to delay the next job execution unnecessarily.  This
 * includes checking its own memory consumption and checking whether it needs
 * restarting.  So, a decision to restart unfortunately have to wait till after
 * the next job has completed.  This is a little bit unfortunate if the next job
 * requires a lot of memory and kWorker has already leaked/used a lot.
 *
 *
 * @section sec_kWorker_How_Works   How kWorker Works
 *
 * kWorker will load the executable specified by kSubmit into memory and call
 * it's entrypoint in a lightly sandbox'ed environment.
 *
 *
 * @subsection ssec_kWorker_Loaing      Image loading
 *
 * kWorker will manually load all the executable images into memory, fix them
 * up, and make a copy of the virgin image so it can be restored using memcpy
 * the next time it is used.
 *
 * Imported functions are monitored and replacements used for a few of them.
 * These replacements are serve the following purposes:
 *      - Provide a different command line.
 *      - Provide a different environment.
 *      - Intercept process termination.
 *      - Intercept thread creation (only linker is allowed to create threads).
 *      - Intercept file reading for caching (header files, ++) as file system
 *        access is made even slower by anti-virus software.
 *      - Intercept crypto hash APIs to cache MD5 digests of header files
 *        (c1.dll / c1xx.dll spends a noticable bit of time doing MD5).
 *      - Intercept temporary files (%TEMP%/_CL_XXXXXXyy) to keep the entirely
 *        in memory as writing files grows expensive with encryption and
 *        anti-virus software active.
 *      - Intercept some file system queries to use the kFsCache instead of
 *        going to the kernel and slowly worm thru the AV filter driver.
 *      - Intercept standard output/error and console writes to aggressivly
 *        buffer the output.  The MS CRT does not buffer either when it goes to
 *        the console, resulting in terrible performance and mixing up output
 *        with other compile jobs.
 *        This also allows us to filter out the annoying source file announcements
 *        by cl.exe.
 *      - Intercept VirtualAlloc and VirtualFree to prevent
 *        CL.EXE/C1.DLL/C1XX.DLL from leaking some 72MB internal allocat area.
 *      - Intercept FlsAlloc/FlsFree to make sure the allocations are freed and
 *        the callbacks run after each job.
 *      - Intercept HeapCreate/HeapFree to reduce leaks from statically linked
 *        executables and tools using custom heaps (like the microsoft linker).
 *        [exectuable images only]
 *      - Intercept atexit and _onexit registration to be able run them after
 *        each job instead of crashing as kWorker exits.  This also helps avoid
 *        some leaks. [executable image only]
 *
 * DLLs falls into two categories, system DLLs which we always load using the
 * native loader, and tool DLLs which can be handled like the executable or
 * optionally using the native loader.  We maintain a hardcoded white listing of
 * tool DLLs we trust to load using the native loader.
 *
 * Imports of natively loaded DLLs are processed too, but we only replace a
 * subset of the functions compared to natively loaded excutable and DLL images.
 *
 * DLLs are never unloaded and we cache LoadLibrary requests (hash the input).
 * This is to speed up job execution.
 *
 * It was thought that we needed to restore (memcpy) natively loaded tool DLLs
 * for each job run, but so far this hasn't been necessary.
 *
 *
 * @subsection ssec_kWorker_Optimizing  Optimizing the Compiler
 *
 * The Visual Studio 2010 C/C++ compiler does a poor job at processing header
 * files and uses a whole bunch of temporary files (in %TEMP%) for passing
 * intermediate representation between the first (c1/c1xx.dll) and second pass
 * (c2.dll).
 *
 * kWorker helps the compiler as best as it can.  Given a little knowledge about
 * stable and volatile file system areas, it can do a lot of caching that a
 * normal compiler driver cannot easily do when given a single file.
 *
 *
 * @subsubsection sssec_kWorker_Headers     Cache Headers Files and Searches
 *
 * The preprocessor part will open and process header files exactly as they are
 * encountered in the source files.  If string.h is included by the main source
 * and five other header files, it will be searched for (include path), opened,
 * read, MD5-summed, and pre-processed six times.  The last five times is just a
 * waste of time because of the guards or \#pragma once.  A smart compiler would
 * make a little extra effort and realize this.
 *
 * kWorker will cache help the preprocessor by remembering places where the
 * header was not found with help of kFsCache, and cache the file in memory when
 * found.  The first part is taken care of by intercepting GetFileAttributesW,
 * and the latter by intercepting CreateFileW, ReadFile and CloseFile.  Once
 * cached, the file is kept open and the CreateFileW call returns a duplicate of
 * that handle.  An internal handle table is used by ReadFile and CloseFile to
 * keep track of intercepted handles (also used for temporary file, temporary
 * file mappings, console buffering, and standard out/err buffering).
 *
 * PS. The header search optimization also comes in handy when cl.exe goes on
 *     thru the whole PATH looking for c1/c1xx.exe and c2.exe after finding
 *     c1/c1xx.dll and c2.dll.  My guess is that the compiler team can
 *     optionally compile the three pass DLLs as executables during development
 *     and problem analysis.
 *
 *
 * @subsubsection sssec_kWorker_Temp_Files  Temporary Files In Memory
 *
 * The issues of the temporary files is pretty severe on the Dell machine used
 * for benchmarking with full AV and encryption.  The synthetic benchmark
 * improved by 30% when kWorker implemented measures to keep them entirely in
 * memory.
 *
 * kWorker implement these by recognizing the filename pattern in CreateFileW
 * and creating/opening the given file as needed.  The handle returned is a
 * duplicate of the current process, thus giving us a good chance of catching
 * API calls we're not intercepting.
 *
 * In addition to CreateFileW, we also need to intercept GetFileType, ReadFile,
 * WriteFile, SetFilePointer+Ex, SetEndOfFile, and CloseFile.  The 2nd pass
 * additionally requires GetFileSize+Ex, CreateFileMappingW, MapViewOfFile and
 * UnmapViewOfFile.
 *
 *
 * @section sec_kWorker_Numbers     Some measurements.
 *
 *  - r2881 building src/VBox/Runtime:
 *     - without: 2m01.016388s = 120.016388 s
 *     - with:    1m15.165069s = 75.165069 s => 120.016388s - 75.165069s = 44.851319s => 44.85/120.02 = 37% speed up.
 *  - r2884 building vbox/debug (r110512):
 *     - without: 11m14.446609s = 674.446609 s
 *     - with:     9m01.017344s = 541.017344 s => 674.446609s - 541.017344s = 133.429265 => 133.43/674.45 = 19% speed up
 *  - r2896 building vbox/debug (r110577):
 *     - with:     8m31.182384s = 511.182384 s => 674.446609s - 511.182384s = 163.264225 = 163.26/674.45 = 24% speed up
 *  - r2920 building vbox/debug (r110702) on Skylake (W10/amd64, only standard
 *     MS Defender as AV):
 *     - without: 10m24.990389s = 624.990389s
 *     - with:    08m04.738184s = 484.738184s
 *          - delta: 624.99s - 484.74s = 140.25s
 *          - saved: 140.25/624.99 = 22% faster
 *
 *
 * @subsection subsec_kWorker_Early_Numbers     Early Experiments
 *
 * These are some early experiments doing 1024 compilations of
 * VBoxBS2Linker.cpp using a hard coded command line and looping in kWorker's
 * main function:
 *
 * Skylake (W10/amd64, only stdandard MS defender):
 *  - cmd 1:  48    /1024 = 0x0 (0.046875)        [for /l %i in (1,1,1024) do ...]
 *  - kmk 1:  44    /1024 = 0x0 (0.04296875)      [all: ; 1024 x cl.exe]
 *  - run 1:  37    /1024 = 0x0 (0.0361328125)    [just process creation gain]
 *  - run 2:  34    /1024 = 0x0 (0.033203125)     [get file attribs]
 *  - run 3:  32.77 /1024 = 0x0 (0.032001953125)  [read caching of headers]
 *  - run 4:  32.67 /1024 = 0x0 (0.031904296875)  [loader tweaking]
 *  - run 5:  29.144/1024 = 0x0 (0.0284609375)    [with temp files in memory]
 *
 * Dell (W7/amd64, infected by mcafee):
 *  - kmk 1: 285.278/1024 = 0x0 (0.278591796875)
 *  - run 1: 134.503/1024 = 0x0 (0.1313505859375) [w/o temp files in memory]
 *  - run 2:  78.161/1024 = 0x0 (0.0763291015625) [with temp files in memory]
 *
 * The command line:
 * @code{.cpp}
   "\"E:/vbox/svn/trunk/tools/win.x86/vcc/v10sp1/bin/amd64/cl.exe\" -c -c -TP -nologo -Zi -Zi -Zl -GR- -EHsc -GF -Zc:wchar_t- -Oy- -MT -W4 -Wall -wd4065 -wd4996 -wd4127 -wd4706 -wd4201 -wd4214 -wd4510 -wd4512 -wd4610 -wd4514 -wd4820 -wd4365 -wd4987 -wd4710 -wd4061 -wd4986 -wd4191 -wd4574 -wd4917 -wd4711 -wd4611 -wd4571 -wd4324 -wd4505 -wd4263 -wd4264 -wd4738 -wd4242 -wd4244 -WX -RTCsu -IE:/vbox/svn/trunk/tools/win.x86/vcc/v10sp1/include -IE:/vbox/svn/trunk/tools/win.x86/vcc/v10sp1/atlmfc/include -IE:/vbox/svn/trunk/tools/win.x86/sdk/v7.1/Include -IE:/vbox/svn/trunk/include -IE:/vbox/svn/trunk/out/win.amd64/debug -IE:/vbox/svn/trunk/tools/win.x86/vcc/v10sp1/include -IE:/vbox/svn/trunk/tools/win.x86/vcc/v10sp1/atlmfc/include -DVBOX -DVBOX_WITH_64_BITS_GUESTS -DVBOX_WITH_REM -DVBOX_WITH_RAW_MODE -DDEBUG -DDEBUG_bird -DDEBUG_USERNAME=bird -DRT_OS_WINDOWS -D__WIN__ -DRT_ARCH_AMD64 -D__AMD64__ -D__WIN64__ -DVBOX_WITH_DEBUGGER -DRT_LOCK_STRICT -DRT_LOCK_STRICT_ORDER -DIN_RING3 -DLOG_DISABLED -DIN_BLD_PROG -D_CRT_SECURE_NO_DEPRECATE -FdE:/vbox/svn/trunk/out/win.amd64/debug/obj/VBoxBs2Linker/VBoxBs2Linker-obj.pdb -FD -FoE:/vbox/svn/trunk/out/win.amd64/debug/obj/VBoxBs2Linker/VBoxBs2Linker.obj E:\\vbox\\svn\\trunk\\src\\VBox\\ValidationKit\\bootsectors\\VBoxBs2Linker.cpp"
 * @endcode
 */

