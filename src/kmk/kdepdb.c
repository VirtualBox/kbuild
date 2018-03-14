/* $Id: incdep.c 2283 2009-02-24 04:54:00Z bird $ */
/** @file
 * kdepdb - Dependency database.
 */

/*
 * Copyright (c) 2009-2010 knut st. osmundsen <bird-kBuild-spamx@anduin.net>
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
#include "makeint.h"
#include "../lib/k/kDefs.h"
#include "../lib/k/kTypes.h"
#include <assert.h>
#include <glob.h>

#include "dep.h"
#include "filedef.h"
#include "job.h"
#include "commands.h"
#include "variable.h"
#include "rule.h"
#include "debug.h"
#include "strcache2.h"

#ifdef HAVE_FCNTL_H
# include <fcntl.h>
#else
# include <sys/file.h>
#endif

#if K_OS == K_WINDOWS
# include <Windows.h>
#else
# include <unistd.h>
# include <sys/mman.h>
#endif


/*******************************************************************************
*   Defined Constants And Macros                                               *
*******************************************************************************/
/** @def KDEPDB_ASSERT_SIZE
 * Check the size of an on-disk type.
 *
 * @param   Type    The type which size it being checked.
 * @param   Size    The size it should have.
 */
#ifdef __GNUC__
# define KDEPDB_ASSERT_SIZE(Type, Size) \
    extern int kDepDbAssertSize[1] __attribute__((unused)), \
               kDepDbAssertSize[sizeof(Type) == (Size)] __attribute__((unused))
#else
# define KDEPDB_ASSERT_SIZE(Type, Size) \
    typedef int kDepDbAssertSize[sizeof(Type) == (Size)]
#endif
KDEPDB_ASSERT_SIZE(KU8,  1);
KDEPDB_ASSERT_SIZE(KU16, 2);
KDEPDB_ASSERT_SIZE(KU32, 4);
KDEPDB_ASSERT_SIZE(KU64, 8);


/*******************************************************************************
*   Structures and Typedefs                                                    *
*******************************************************************************/
/**
 * File header.
 *
 * @remarks     All on-disk formats are in little-endian format.
 */
typedef struct KDEPDBHDR
{
    /** The file magic.  */
    KU8             szMagic[8];
    /** The major file format version. */
    KU8             uVerMajor;
    /** The minor file format version. */
    KU8             uVerMinor;
    /** Reserved \#2. */
    KU16            uReserved2;
    /** Reserved \#1. */
    KU32            uReserved1;
    /** The internal name of this file. */
    KU8             szName[16];
} KDEPDBHDR;
KDEPDB_ASSERT_SIZE(KDEPDBHDR, 32);

/** The file header magic value. */
#define KDEPDBHDR_MAGIC             "kDepDb\0"
/** The current major file format version number.  */
#define KDEPDBHDR_VERSION_MAJOR     0
/** The current minor file format version number.
 * Numbers above 240 indicate unsupported development variants. */
#define KDEPDBHDR_VERSION_MINOR     240


/**
 * Hash table file.
 *
 * The hash table is recreated in a new file when we have to grow it.
 */
typedef struct KDEPDBHASH
{
    /** The file header. */
    KDEPDBHDR       Hdr;
    /** The number of hash table entries. */
    KU32            cEntries;
    /** The number of hash table entries with content. */
    KU32            cUsedEntries;
    /** The number of collisions on insert. */
    KU32            cCollisions;
    /** Reserved member \#5. */
    KU32            uReserved5;
    /** Reserved member \#4. */
    KU32            uReserved4;
    /** Reserved member \#3. */
    KU32            uReserved3;
    /** Reserved member \#2. */
    KU32            uReserved2;
    /** Reserved member \#1. */
    KU32            uReserved1;
    /** The hash table. */
    KU32            auEntries[32];
} KDEPDBHASH;
KDEPDB_ASSERT_SIZE(KDEPDBHASH, 32+32+4*32);

/** The item value indicating that it is unused. */
#define KDEPDBHASH_UNUSED   KU32_C(0xffffffff)
/** The item indicating that it hash been deleted. */
#define KDEPDBHASH_DELETED  KU32_C(0xfffffffe)
/** The first special item value. */
#define KDEPDBHASH_END      KU32_C(0xfffffff0)


/**
 * A string table string entry.
 *
 * This should be a multiple of 32 bytes.
 */
typedef struct KDEPDBSTRING
{
    /** The hash number for the string. */
    KU32            uHash;
    /** The string length, excluding the zero terminator. */
    KU32            cchString;
    /** The string. */
    KU8             szString[24];
} KDEPDBSTRING;
KDEPDB_ASSERT_SIZE(KDEPDBSTRING, 32);


/**
 * String table file.
 *
 * The file is insertion only and will grow forever.
 */
typedef struct KDEPDBSTRTAB
{
    /** The file header. */
    KDEPDBHDR       Hdr;
    /** The end of the valid string table indexes. */
    KU32            iStringEnd;
    /** Reserved member \#7. */
    KU32            uReserved7;
    /** Reserved member \#6. */
    KU32            uReserved6;
    /** Reserved member \#5. */
    KU32            uReserved5;
    /** Reserved member \#4. */
    KU32            uReserved4;
    /** Reserved member \#3. */
    KU32            uReserved3;
    /** Reserved member \#2. */
    KU32            uReserved2;
    /** Reserved member \#1. */
    KU32            uReserved1;
    /** The string table. */
    KDEPDBSTRING    aStrings[1];
} KDEPDBSTRTAB;
KDEPDB_ASSERT_SIZE(KDEPDBSTRTAB, 32+32+32);

/** The end of the valid string table indexes (exclusive). */
#define KDEPDBG_STRTAB_IDX_END          KU32_C(0x80000000)
/** The string was not found. */
#define KDEPDBG_STRTAB_IDX_NOT_FOUND    KU32_C(0xfffffffd)
/** Error during string table operation.  */
#define KDEPDBG_STRTAB_IDX_ERROR        KU32_C(0xfffffffe)
/** Generic invalid string table index. */
#define KDEPDBG_STRTAB_IDX_INVALID      KU32_C(0xffffffff)


/**
 * Directory entry.
 */
typedef struct KDEPDBDIRENTRY
{
    /** The string table index of the entry name.
     * Unused entries are set to KDEPDBG_STRTAB_IDX_INVALID. */
    KU32            iName;
    /** The actual data stream size.
     * Unused entries are set to KU32_MAX. */
    KU32            cbData;
    /** The number of blocks allocated for this stream.
     * Unused entries are set to KU32_MAX. */
    KU32            cBlocks;
    /** The start block number.
     * The stream is a contiguous sequence of blocks. This optimizes and
     * simplifies reading the stream at the expense of operations extending it.
     *
     * In unused entries, this serves as the free chain pointer with KU32_MAX as
     * nil value. */
    KU32            iStartBlock;
} KDEPDBDIRENTRY;
KDEPDB_ASSERT_SIZE(KDEPDBDIRENTRY, 16);

/**
 * Directory file.
 */
typedef struct KDEPDBDIR
{
    /** The file header. */
    KDEPDBHDR       Hdr;
    /** The number of entries. */
    KU32            cEntries;
    /** The head of the free chain. (Index into aEntries.) */
    KU32            iFreeHead;
    /** Reserved member \#6. */
    KU32            uReserved6;
    /** Reserved member \#5. */
    KU32            uReserved5;
    /** Reserved member \#4. */
    KU32            uReserved4;
    /** Reserved member \#3. */
    KU32            uReserved3;
    /** Reserved member \#2. */
    KU32            uReserved2;
    /** Reserved member \#1. */
    KU32            uReserved1;
    /** Directory entries. */
    KDEPDBDIRENTRY  aEntries[2];
} KDEPDBDIR;
KDEPDB_ASSERT_SIZE(KDEPDBDIR, 32+32+32);


/**
 * A block allocation bitmap.
 *
 * This can track 2^(12+8) = 2^20 = 1M blocks.
 */
typedef struct KDEPDBDATABITMAP
{
    /** Bitmap where each bit is a block.
     * 0 indicates unused blocks and 1 indicates used ones. */
    KU8             bm[4096];
} KDEPDBDATABITMAP;
KDEPDB_ASSERT_SIZE(KDEPDBDATABITMAP, 4096);

/**
 * Data file.
 *
 * The block numbering starts with this structure as block 0.
 */
typedef struct KDEPDBDATA
{
    /** The file header. */
    KDEPDBHDR       Hdr;
    /** The size of a block. */
    KU32            cbBlock;
    /** Reserved member \#7. */
    KU32            uReserved7;
    /** Reserved member \#6. */
    KU32            uReserved6;
    /** Reserved member \#5. */
    KU32            uReserved5;
    /** Reserved member \#4. */
    KU32            uReserved4;
    /** Reserved member \#3. */
    KU32            uReserved3;
    /** Reserved member \#2. */
    KU32            uReserved2;
    /** Reserved member \#1. */
    KU32            uReserved1;

    /** Block numbers for the allocation bitmaps.  */
    KU32            aiBitmaps[4096];
} KDEPDBDATA;

/** The end of the valid block indexes (exclusive). */
#define KDEPDB_BLOCK_IDX_END            KU32_C(0xfffffff0)
/** The index of an unallocated bitmap block. */
#define KDEPDB_BLOCK_IDX_UNALLOCATED    KU32_C(0xffffffff)


/**
 * Stream storing dependencies.
 *
 * The stream name gives the output file name, so all that we need is the list
 * of files it depends on. These are serialized as a list of string table
 * indexes.
 */
typedef struct KDEPDBDEPSTREAM
{
    /** String table indexes for the dependencies. */
    KU32            aiDeps[1];
} KDEPDBDEPSTREAM;


/**
 * A file handle structure.
 */
typedef struct KDEPDBFH
{
#if K_OS == K_OS_WINDOWS
    /** The file handle. */
    HANDLE      hFile;
    /** The mapping object handle. */
    HANDLE      hMapObj;
#else
    /** The file handle. */
    int         fd;
#endif
    /** The current file size. */
    KU32        cb;
} KDEPDBFH;


/**
 * Internal control structure for a string table.
 */
typedef struct KDEPDBINTSTRTAB
{
    /** The hash file. */
    KDEPDBHASH     *pHash;
    /** The handle of the hash file. */
    KDEPDBFH        hHash;
    /** The string table file. */
    KDEPDBSTRTAB   *pStrTab;
    /** The handle of the string table file. */
    KDEPDBFH        hStrTab;
    /** The end of the allocated string table indexes (i.e. when to grow the
     * file). */
    KU32            iStringAlloced;
} KDEPDBINTSTRTAB;


/**
 * Internal control structure for a data set.
 *
 * This governs the directory file, the directory hash file and the data file.
 */
typedef struct KDEPDBINTDATASET
{
    /** The hash file. */
    KDEPDBHASH      pHash;
    /** The size of the hash file. */
    KU32            cbHash;
    /** The size of the directory file. */
    KU32            cbDir;
    /** The mapping of the directory file. */
    KDEPDBHASH      pDir;
    /** The data file. */
    KDEPDBDATA      pData;
    /** The size of the data file. */
    KU32            cbData;
    /** The handle of the hash file. */
    KDEPDBFH        hHash;
    /** The handle of the directory file. */
    KDEPDBFH        hDir;
    /** The handle of the data file. */
    KDEPDBFH        hData;
} KDEPDBINTDATASET;


/**
 * The database instance.
 *
 * To simplifiy things the database uses 8 files for storing the different kinds
 * of data. This greatly reduces the complexity compared to a single file
 * solution.
 */
typedef struct KDEPDB
{
    /** The string table. */
    KDEPDBINTSTRTAB     StrTab;
    /** The variable data set. */
    KDEPDBINTDATASET    DepSet;
    /** The command data set. */
    KDEPDBINTDATASET    CmdSet;
} KDEPDB;


/*******************************************************************************
*   Internal Functions                                                         *
*******************************************************************************/
static void *kDepDbAlloc(KSIZE cb);
static void kDepDbFree(void *pv);
static void kDepDbFHInit(KDEPDBFH *pFH);
static int  kDepDbFHUpdateSize(KDEPDBFH *pFH);
static int  kDepDbFHOpen(KDEPDBFH *pFH, const char *pszFilename, KBOOL fCreate, KBOOL *pfNew);
static int  kDepDbFHClose(KDEPDBFH *pFH);
static int  kDepDbFHWriteAt(KDEPDBFH *pFH, KU32 off, void const *pvBuf, KSIZE cbBuf);
static int  kDepDbFHMap(KDEPDBFH *pFH, void **ppvMap);
static int  kDepDbFHUnmap(KDEPDBFH *pFH, void **ppvMap);
static int  kDepDbFHGrow(KDEPDBFH *pFH, KSIZE cbNew, void **ppvMap);
static KU32 kDepDbHashString(const char *pszString, size_t cchString);


/** xmalloc wrapper. */
static void *kDepDbAlloc(KSIZE cb)
{
    return xmalloc(cb);
}

/** free wrapper. */
static void kDepDbFree(void *pv)
{
    if (pv)
        free(pv);
}


/**
 * Initializes the file handle structure so closing it without first opening it
 * will work smoothly.
 *
 * @param   pFH         The file handle structure.
 */
static void kDepDbFHInit(KDEPDBFH *pFH)
{
#if K_OS == K_OS_WINDOWS
    pFH->hFile   = INVALID_HANDLE_VALUE;
    pFH->hMapObj = INVALID_HANDLE_VALUE;
#else
    pFH->fd = -1;
#endif
    pFH->cb = 0;
}

/**
 * Updates the file size.
 *
 * @returns 0 on success. Some non-zero native error code on failure.
 * @param   pFH             The file handle structure.
 */
static int  kDepDbFHUpdateSize(KDEPDBFH *pFH)
{
#if K_OS == K_OS_WINDOWS
    DWORD   rc;
    DWORD   dwHigh;
    DWORD   dwLow;

    SetLastError(0);
    dwLow = GetFileSize(File, &High);
    rc = GetLastError();
    if (rc)
    {
        pFH->cb = 0;
        return (int)rc;
    }
    if (High)
        pFH->cb = KU32_MAX;
    else
        pFH->cb = dwLow;
#else
    off_t cb;

    cb = lseek(pFH->fd, 0, SEEK_END);
    if (cb == -1)
    {
        pFH->cb = 0;
        return errno;
    }
    pFH->cb = cb;
    if ((off_t)pFH->cb != cb)
        pFH->cb = KU32_MAX;
#endif
    return 0;
}

/**
 * Opens an existing file or creates a new one.
 *
 * @returns 0 on success. Some non-zero native error code on failure.
 *
 * @param   pFH             The file handle structure.
 * @param   pszFilename     The name of the file.
 * @param   fCreate         Whether we should create the file or not.
 * @param   pfCreated       Where to return whether we created it or not.
 */
static int  kDepDbFHOpen(KDEPDBFH *pFH, const char *pszFilename, KBOOL fCreate, KBOOL *pfCreated)
{
    int                 rc;
#if K_OS == K_OS_WINDOWS
    SECURITY_ATTRIBUTES SecAttr;

    SecAttr.bInheritHandle = FALSE;
    SecAttr.lpSecurityDescriptor = NULL;
    SecAttr.nLength = 0;
    pFH->cb = 0;
    SetLastError(0);
    pFH->hFile = CreateFile(pszFilename, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, &SecAttr,
                            fCreate ? OPEN_ALWAYS : OPEN_EXISTING, 0, NULL);
    if (pFH->hFile == INVALID_HANDLE_VALUE)
        return GetLastError();
    *pfCreated = GetLastError() == 0;

#else
    int fFlags = O_RDWR;
# ifdef O_BINARY
    fFlags |= O_BINARY;
# endif
    pFH->cb = 0;
    pFH->fd = open(pszFilename, fFlags, 0);
    if (pFH->fd >= 0)
        *pfCreated = K_FALSE;
    else if (!fCreate)
        return errno;
    else
    {
        pFH->fd = open(pszFilename, fFlags | O_EXCL | O_CREAT, 0666);
        if (pFH->fd < 0)
            return errno;
        *pfCreated = K_TRUE;
    }
    fcntl(pFH->fd, F_SETFD, FD_CLOEXEC);
#endif

    /* update the size */
    rc = kDepDbFHUpdateSize(pFH);
    if (rc)
        kDepDbFHClose(pFH);
    return rc;
}

/**
 * Closes an open file.
 *
 * @returns 0 on success. Some non-zero native error code on failure.
 *
 * @param   pFH         The file handle structure.
 */
static int  kDepDbFHClose(KDEPDBFH *pFH)
{
#if K_OS == K_OS_WINDOWS
    if (pFH->hFile != INVALID_HANDLE_VALUE)
    {
        if (!CloseHandle(pFH->hFile))
            return GetLastError();
        pFH->hFile = INVALID_HANDLE_VALUE;
    }

#else
    if (pFH->fd >= 0)
    {
        if (close(pFH->fd) != 0)
            return errno;
        pFH->fd = -1;
    }
#endif
    pFH->cb = 0;
    return 0;
}

/**
 * Writes to a file.
 *
 * @returns 0 on success. Some non-zero native error code on failure.
 *
 * @param   pFH         The file handle structure.
 * @param   off         The offset into the file to start writing at.
 * @param   pvBuf       What to write.
 * @param   cbBuf       How much to write.
 */
static int  kDepDbFHWriteAt(KDEPDBFH *pFH, KU32 off, void const *pvBuf, KSIZE cbBuf)
{
#if K_OS == K_OS_WINDOWS
    ULONG cbWritten;

    if (SetFilePointer(pFH->hFile, off, NULL, FILE_CURRENT) == INVALID_SET_FILE_POINTER)
        return GetLastError();

    if (!WriteFile(pFH->hFile, pvBuf, cbBuf, &cbWritten, NULL))
        return GetLastError();
    if (cbWritten != cbBuf)
        return -1;

#else
    ssize_t cbWritten;
    if (lseek(pFH->fd, off, SEEK_SET) == -1)
        return errno;
    errno = 0;
    cbWritten = write(pFH->fd, pvBuf, cbBuf);
    if ((size_t)cbWritten != cbBuf)
        return errno ? errno : EIO;
#endif
    return kDepDbFHUpdateSize(pFH);
}


/**
 * Creates a memory mapping of the file.
 *
 * @returns 0 on success. Some non-zero native error code on failure.
 *
 * @param   pFH         The file handle structure.
 * @param   ppvMap      Where to return the map address.
 */
static int  kDepDbFHMap(KDEPDBFH *pFH, void **ppvMap)
{
#if K_OS == K_OS_WINDOWS
    *ppvMap = NULL;
    return -1;
#else
    *ppvMap = mmap(NULL, pFH->cb, PROT_READ | PROT_WRITE, MAP_FILE | MAP_SHARED, pFH->fd, 0);
    if (*ppvMap == (void *)-1)
    {
        *ppvMap = NULL;
        return errno;
    }
#endif
    return 0;
}


/**
 * Flushes and destroys a memory of the file.
 *
 * @returns 0 on success. Some non-zero native error code on failure.
 *
 * @param   pFH         The file handle structure.
 * @param   ppvMap      The pointer to the mapping pointer. This will be set to
 *                      NULL on success.
 */
static int  kDepDbFHUnmap(KDEPDBFH *pFH, void **ppvMap)
{
#if K_OS == K_OS_WINDOWS
    return -1;
#else
    if (msync(*ppvMap, pFH->cb, MS_SYNC) == -1)
        return errno;
    if (munmap(*ppvMap, pFH->cb) == -1)
        return errno;
    *ppvMap = NULL;
#endif
    return 0;
}


/**
 * Grows the memory mapping of the file.
 *
 * The content of the new space is undefined.
 *
 * @returns 0 on success. Some non-zero native error code on failure.
 *
 * @param   pFH         The file handle structure.
 * @param   cbNew       The new mapping size.
 * @param   ppvMap      The pointer to the mapping pointer. This may change and
 *                      may be set to NULL on failure.
 */
static int  kDepDbFHGrow(KDEPDBFH *pFH, KSIZE cbNew, void **ppvMap)
{
#if K_OS == K_OS_WINDOWS
    return -1;
#else
    if ((KU32)cbNew != cbNew)
        return ERANGE;
    if (cbNew <= pFH->cb)
        return 0;

    if (munmap(*ppvMap, pFH->cb) == -1)
        return errno;
    *ppvMap = NULL;

    pFH->cb = cbNew;
    return kDepDbFHMap(pFH, ppvMap);
#endif
}


/** Macro for reading an potentially unaligned 16-bit word from a string. */
# if K_ARCH == K_ARCH_AMD64 \
  || K_ARCH == K_ARCH_X86_32 \
  || K_ARCH == K_ARCH_X86_16
#  define kDepDbHashString_get_unaligned_16bits(ptr)   ( *((const KU16 *)(ptr)) )
# elif K_ENDIAN == K_ENDIAN_LITTLE
#  define kDepDbHashString_get_unaligned_16bits(ptr)   (  (((const KU8 *)(ptr))[0]) \
                                                        | (((const KU8 *)(ptr))[1] << 8) )
# else
#  define kDepDbHashString_get_unaligned_16bits(ptr)   (  (((const KU8 *)(ptr))[0] << 8) \
                                                        | (((const KU8 *)(ptr))[1]) )
# endif


/**
 * Hash a string.
 *
 * @returns Hash value.
 *
 * @param   pszString       The string to hash.
 * @param   cchString       How much to hash.
 */
static KU32 kDepDbHashString(const char *pszString, size_t cchString)
{
    /*
     * Paul Hsieh hash SuperFast function:
     * http://www.azillionmonkeys.com/qed/hash.html
     */
    /** @todo A path for well aligned data should be added to speed up execution on
     *        alignment sensitive systems. */
    unsigned int uRem;
    KU32 uHash;
    KU32 uTmp;

    assert(sizeof(KU8) == sizeof(char));

    /* main loop, walking on 2 x KU16 */
    uHash = cchString;
    uRem  = cchString & 3;
    cchString >>= 2;
    while (cchString > 0)
    {
        uHash      += kDepDbHashString_get_unaligned_16bits(pszString);
        uTmp        = (kDepDbHashString_get_unaligned_16bits(pszString + 2) << 11) ^ uHash;
        uHash       = (uHash << 16) ^ uTmp;
        pszString  += 2 * sizeof(KU16);
        uHash      += uHash >> 11;
        cchString--;
    }

    /* the remainder */
    switch (uRem)
    {
        case 3:
            uHash += kDepDbHashString_get_unaligned_16bits(pszString);
            uHash ^= uHash << 16;
            uHash ^= pszString[sizeof(KU16)] << 18;
            uHash += uHash >> 11;
            break;
        case 2:
            uHash += kDepDbHashString_get_unaligned_16bits(pszString);
            uHash ^= uHash << 11;
            uHash += uHash >> 17;
            break;
        case 1:
            uHash += *pszString;
            uHash ^= uHash << 10;
            uHash += uHash >> 1;
            break;
    }

    /* force "avalanching" of final 127 bits. */
    uHash ^= uHash << 3;
    uHash += uHash >> 5;
    uHash ^= uHash << 4;
    uHash += uHash >> 17;
    uHash ^= uHash << 25;
    uHash += uHash >> 6;

    return uHash;
}


/***
 * Looks up a string in the string table.
 *
 * @returns The string table index.
 * @retval  KDEPDBG_STRTAB_IDX_NOT_FOUND is not found.
 * @retval  KDEPDBG_STRTAB_IDX_ERROR on internal inconsistency.
 *
 * @param   pStrTab         The string table.
 * @param   pszString       The string.
 * @param   cchStringIn     The string length.
 * @param   uHash           The hash of the string.
 */
static KU32 kDepDbStrTabLookupHashed(KDEPDBINTSTRTAB const *pStrTab, const char *pszString, size_t cchStringIn, KU32 uHash)
{
    KU32 const          cchString  = (KU32)cchStringIn;
    KDEPDBHASH const   *pHash      = pStrTab->pHash;
    KDEPDBSTRING const *paStrings  = &pStrTab->pStrTab->aStrings[0];
    KU32 const          iStringEnd = K_LE2H_U32(pStrTab->pStrTab->iStringEnd);
    KU32                iHash;

    /* sanity */
    if (cchString != cchStringIn)
        return KDEPDBG_STRTAB_IDX_NOT_FOUND;

    /*
     * Hash lookup of the string.
     */
    iHash = uHash % pHash->cEntries;
    for (;;)
    {
        KU32 iString = K_LE2H_U32(pHash->auEntries[iHash]);
        if (iString < iStringEnd)
        {
            KDEPDBSTRING const *pString = &paStrings[iString];
            if (    K_LE2H_U32(pString->uHash)     == uHash
                &&  K_LE2H_U32(pString->cchString) == cchString
                &&  !memcmp(pString->szString, pszString, cchString))
                return iString;
        }
        else if (iString == KDEPDBHASH_UNUSED)
            return KDEPDBG_STRTAB_IDX_NOT_FOUND;
        else if (iString != KDEPDBHASH_DELETED)
            return KDEPDBG_STRTAB_IDX_ERROR;

        /* advance */
        iHash = (iHash + 1) % pHash->cEntries;
    }
}


/**
 * Doubles the hash table size and rehashes it.
 *
 * @returns 0 on success, -1 on failure.
 * @param   pStrTab         The string table.
 * @todo    Rebuild from string table, we'll be accessing it anyways.
 */
static int kDepDbStrTabReHash(KDEPDBINTSTRTAB *pStrTab)
{
    KDEPDBSTRING const *paStrings   = &pStrTab->pStrTab->aStrings[0];
    KU32 const          iStringEnd  = K_LE2H_U32(pStrTab->pStrTab->iStringEnd);
    KDEPDBHASH         *pHash       = pStrTab->pHash;
    KDEPDBHASH          HashHdr     = *pHash;
    KU32               *pauNew;
    KU32                cEntriesNew;
    KU32                i;

    /*
     * Calc the size of the new hash table.
     */
    if (pHash->cEntries >= KU32_C(0x80000000))
        return -1;
    cEntriesNew = 1024;
    while (cEntriesNew <= pHash->cEntries)
        cEntriesNew <<= 1;

    /*
     * Allocate and initialize an empty hash table in memory.
     */
    pauNew = kDepDbAlloc(cEntriesNew * sizeof(KU32));
    if (!pauNew)
        return -1;
    i = cEntriesNew;
    while (i-- > 0)
        pauNew[i] = KDEPDBHASH_UNUSED;

    /*
     * Popuplate the new table.
     */
    HashHdr.cEntries     = K_LE2H_U32(cEntriesNew);
    HashHdr.cCollisions  = 0;
    HashHdr.cUsedEntries = 0;
    i = pHash->cEntries;
    while (i-- > 0)
    {
        KU32 iString = K_LE2H_U32(pHash->auEntries[i]);
        if (iString < iStringEnd)
        {
            KU32 iHash = (paStrings[iString].uHash % cEntriesNew);
            if (pauNew[iHash] != K_H2LE_U32(KDEPDBHASH_UNUSED))
            {
                do
                {
                    iHash = (iHash + 1) % cEntriesNew;
                    HashHdr.cCollisions++;
                } while (pauNew[iHash] != K_H2LE_U32(KDEPDBHASH_UNUSED));
            }
            pauNew[iHash] = iString;
            HashHdr.cUsedEntries++;
        }
        else if (   iString != KDEPDBHASH_UNUSED
                 && iString != KDEPDBHASH_DELETED)
        {
            kDepDbFree(pauNew);
            return -1;
        }
    }
    HashHdr.cCollisions  = K_H2LE_U32(HashHdr.cCollisions);
    HashHdr.cUsedEntries = K_H2LE_U32(HashHdr.cUsedEntries);

    /*
     * Unmap the hash, write the new hash table and map it again.
     */
    if (!kDepDbFHUnmap(&pStrTab->hHash, (void **)&pStrTab->pHash))
    {
        if (   !kDepDbFHWriteAt(&pStrTab->hHash, 0, &HashHdr, K_OFFSETOF(KDEPDBHASH, auEntries))
            && !kDepDbFHWriteAt(&pStrTab->hHash, K_OFFSETOF(KDEPDBHASH, auEntries), pauNew, sizeof(pauNew[0]) * cEntriesNew))
        {
            kDepDbFree(pauNew);
            pauNew = NULL;
            if (!kDepDbFHMap(&pStrTab->hHash, (void **)&pStrTab->pHash))
                return 0;
        }
        else
            kDepDbFHWriteAt(&pStrTab->hHash, 0, "\0\0\0\0", 4); /* file is screwed, trash the magic. */
    }

    kDepDbFree(pauNew);
    return -1;
}


/**
 * Add a string to the string table.
 *
 * If already in the table, the index of the existing entry is returned.
 *
 * @returns String index on success,
 * @retval  KDEPDBG_STRTAB_IDX_ERROR on I/O and inconsistency errors.
 *
 * @param   pStrTab     The string table.
 * @param   pszString   The string to add.
 * @param   cchStringIn The length of the string.
 * @param   uHash       The hash of the string.
 */
static KU32 kDepDbStrTabAddHashed(KDEPDBINTSTRTAB *pStrTab, const char *pszString, size_t cchStringIn, KU32 uHash)
{
    KU32 const          cchString   = (KU32)cchStringIn;
    KDEPDBHASH         *pHash       = pStrTab->pHash;
    KDEPDBSTRING       *paStrings   = &pStrTab->pStrTab->aStrings[0];
    KU32 const          iStringEnd  = K_LE2H_U32(pStrTab->pStrTab->iStringEnd);
    KU32                iInsertAt   = KDEPDBHASH_UNUSED;
    KU32                cCollisions = 0;
    KU32                iHash;
    KU32                iString;
    KU32                cEntries;
    KDEPDBSTRING       *pNewString;

    /* sanity */
    if (cchString != cchStringIn)
        return KDEPDBG_STRTAB_IDX_NOT_FOUND;

    /*
     * Hash lookup of the string, finding either an existing copy or where to
     * insert the new string at in the hash table.
     */
    iHash = uHash % pHash->cEntries;
    for (;;)
    {
        iString = K_LE2H_U32(pHash->auEntries[iHash]);
        if (iString < iStringEnd)
        {
            KDEPDBSTRING const *pString = &paStrings[iString];
            if (    K_LE2H_U32(pString->uHash)     == uHash
                &&  K_LE2H_U32(pString->cchString) == cchString
                &&  !memcmp(pString->szString, pszString, cchString))
                return iString;
        }
        else
        {
            if (iInsertAt == KDEPDBHASH_UNUSED)
                iInsertAt = iHash;
            if (iString == KDEPDBHASH_UNUSED)
                break;
            if (iString != KDEPDBHASH_DELETED)
                return KDEPDBG_STRTAB_IDX_ERROR;
        }

        /* advance */
        cCollisions++;
        iHash = (iHash + 1) % pHash->cEntries;
    }

    /*
     * Add string to the string table.
     * The string table file is grown in 256KB increments and ensuring at least 64KB unused new space.
     */
    cEntries = cchString + 1 <= sizeof(paStrings[0].szString)
             ? 1
             : (cchString + 1 - sizeof(paStrings[0].szString) + sizeof(KDEPDBSTRING) - 1) / sizeof(KDEPDBSTRING);
    if (iStringEnd + cEntries > pStrTab->iStringAlloced)
    {
        KSIZE cbNewSize      = K_ALIGN_Z((iStringEnd + cEntries) * sizeof(KDEPDBSTRING) + 64*1024, 256*1024);
        KU32  iStringAlloced = (pStrTab->hStrTab.cb - K_OFFSETOF(KDEPDBSTRTAB, aStrings)) / sizeof(KDEPDBSTRING);
        if (    iStringAlloced <= pStrTab->iStringAlloced
            ||  iStringAlloced >= KDEPDBG_STRTAB_IDX_END
            ||  iStringAlloced >= KDEPDBHASH_END)
            return KDEPDBG_STRTAB_IDX_ERROR;
        if (kDepDbFHGrow(&pStrTab->hStrTab, cbNewSize, (void **)&pStrTab->pStrTab) != 0)
            return KDEPDBG_STRTAB_IDX_ERROR;
        pStrTab->iStringAlloced = iStringAlloced;
        paStrings = &pStrTab->pStrTab->aStrings[0];
    }

    pNewString = &paStrings[iStringEnd];
    pNewString->uHash     = K_H2LE_U32(uHash);
    pNewString->cchString = K_H2LE_U32(cchString);
    memcpy(&pNewString->szString, pszString, cchString);
    pNewString->szString[cchString] = '\0';

    pStrTab->pStrTab->iStringEnd = K_H2LE_U32(iStringEnd + cEntries);

    /*
     * Insert hash table entry, rehash it if necessary.
     */
    pHash->auEntries[iInsertAt] = K_H2LE_U32(iStringEnd);
    pHash->cUsedEntries = K_H2LE_U32(K_LE2H_U32(pHash->cUsedEntries) + 1);
    pHash->cCollisions  = K_H2LE_U32(K_LE2H_U32(pHash->cCollisions)  + cCollisions);
    if (    K_LE2H_U32(pHash->cUsedEntries) > K_LE2H_U32(pHash->cEntries) / 3 * 2
        &&  kDepDbStrTabReHash(pStrTab) != 0)
        return KDEPDBG_STRTAB_IDX_ERROR;

    return iStringEnd;
}


/** Wrapper for kDepDbStrTabLookupHashed.  */
static KU32 kDepDbStrTabLookupN(KDEPDBINTSTRTAB const *pStrTab, const char *pszString, size_t cchString)
{
    return kDepDbStrTabLookupHashed(pStrTab, pszString, cchString, kDepDbHashString(pszString, cchString));
}


/** Wrapper for kDepDbStrTabAddHashed.  */
static KU32 kDepDbStrTabAddN(KDEPDBINTSTRTAB *pStrTab, const char *pszString, size_t cchString)
{
    return kDepDbStrTabAddHashed(pStrTab, pszString, cchString, kDepDbHashString(pszString, cchString));
}


/** Wrapper for kDepDbStrTabLookupHashed.  */
static KU32 kDepDbStrTabLookup(KDEPDBINTSTRTAB const *pStrTab, const char *pszString)
{
    return kDepDbStrTabLookupN(pStrTab, pszString, strlen(pszString));
}


/** Wrapper for kDepDbStrTabAddHashed.  */
static KU32 kDepDbStrTabAdd(KDEPDBINTSTRTAB *pStrTab, const char *pszString)
{
    return kDepDbStrTabAddN(pStrTab, pszString, strlen(pszString));
}


/**
 * Opens the string table files, creating them if necessary.
 */
static int kDepDbStrTabInit(KDEPDBINTSTRTAB *pStrTab, const char *pszFilenameBase)
{
    size_t  cchFilenameBase = strlen(pszFilenameBase);
    char    szPath[4096];
    int     rc;
    KBOOL   fNew;

    /* Basic member init, so kDepDbStrTabTerm always works. */
    pStrTab->pHash = NULL;
    kDepDbFHInit(&pStrTab->hHash);
    pStrTab->pStrTab = NULL;
    kDepDbFHInit(&pStrTab->hStrTab);
    pStrTab->iStringAlloced = 0;

    /* check the length. */
    if (cchFilenameBase + sizeof(".strtab.hash") > sizeof(szPath))
        return -1;

    /*
     * Open the string table first.
     */
    memcpy(szPath, pszFilenameBase, cchFilenameBase);
    memcpy(&szPath[cchFilenameBase], ".strtab", sizeof(".strtab"));
    rc = kDepDbFHOpen(&pStrTab->hStrTab, szPath, K_TRUE, &fNew);


    return -1;
}

