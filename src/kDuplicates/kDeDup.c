/* $Id: kDeDup.c 3012 2016-11-07 11:53:11Z knut.osmundsen@oracle.com $ */
/** @file
 * kDeDup - Utility that finds duplicate files, optionally hardlinking them.
 */

/*
 * Copyright (c) 2016 knut st. osmundsen <bird-kBuild-spamx@anduin.net>
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
#include <k/kTypes.h>
//#include <stdlib.h>
#include <wchar.h>
#include <string.h>
#include <stdio.h>

#include "md5.h"
//#include "sha2.h"

#include "nt/ntstuff.h"
#include "nt/ntstat.h"
#include "nt/fts-nt.h"
#include "nt/nthlp.h"
#include "nt/ntunlink.h"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * The key is made up of two cryptographic hashes, collisions are
 * highly unlikely (once SHA2 is implemented).
 */
typedef struct KDUPFILENODEKEY
{
    /** The MD5 digest of the file. */
    KU8             abMd5[16];
    /** The 256-bit SHA-2 digest of the file. */
    KU8             abSha2[32];
} KDUPFILENODEKEY;
/** Pointer to a file node.*/
typedef struct KDUPFILENODE *PKDUPFILENODE;
/**
 * Hash tree node.
 */
typedef struct KDUPFILENODE
{
    /** The is made up of two hashes. */
    KDUPFILENODEKEY mKey;
    /** Left branch. */
    PKDUPFILENODE   mpLeft;
    /** Right branch. */
    PKDUPFILENODE   mpRight;
    /** Tree height (hmm). */
    KU8             mHeight;

    /** The inode number. */
    KU64            uInode;
    /** The device number. */
    KU64            uDev;

    /** Pointer to next hard linked node (same inode and udev values). */
    PKDUPFILENODE   pNextHardLink;
    /** Pointer to next duplicate node. */
    PKDUPFILENODE   pNextDup;
    /** Pointer to next duplicate node on the global list. */
    PKDUPFILENODE   pNextGlobalDup;

    /** The path to this file (variable size). */
    wchar_t         wszPath[1];
} KDUPFILENODE;

/*#define KAVL_EQUAL_ALLOWED*/
#define KAVL_CHECK_FOR_EQUAL_INSERT
#define KAVL_MAX_STACK          32
/*#define KAVL_RANGE */
/*#define KAVL_OFFSET */
/*#define KAVL_STD_KEY_COMP*/
#define KAVLKEY                 KDUPFILENODEKEY
#define KAVLNODE                KDUPFILENODE
#define KAVL_FN(name)           kDupFileTree_ ## name
#define KAVL_TYPE(prefix,name)  prefix ## KDUPFILENODE ## name
#define KAVL_INT(name)          KDUPFILENODEINT ## name
#define KAVL_DECL(rettype)      static rettype
#define KAVL_G(key1, key2)      ( memcmp(&(key1), &(key2), sizeof(KDUPFILENODEKEY)) >  0 )
#define KAVL_E(key1, key2)      ( memcmp(&(key1), &(key2), sizeof(KDUPFILENODEKEY)) == 0 )
#define KAVL_NE(key1, key2)     ( memcmp(&(key1), &(key2), sizeof(KDUPFILENODEKEY)) != 0 )

#define register
#include <k/kAvlTmpl/kAvlBase.h>
#include <k/kAvlTmpl/kAvlDoWithAll.h>
//#include <k/kAvlTmpl/kAvlEnum.h> - busted
#include <k/kAvlTmpl/kAvlGet.h>
#include <k/kAvlTmpl/kAvlGetBestFit.h>
#include <k/kAvlTmpl/kAvlGetWithParent.h>
#include <k/kAvlTmpl/kAvlRemove2.h>
#include <k/kAvlTmpl/kAvlRemoveBestFit.h>
#include <k/kAvlTmpl/kAvlUndef.h>
#undef register


/** Pointer to a size tree node. */
typedef struct KDUPSIZENODE *PKDUPSIZENODE;
/**
 * Size tree node.
 */
typedef struct KDUPSIZENODE
{
    /** The file size. */
    KU64            mKey;
    /** Left branch. */
    PKDUPSIZENODE   mpLeft;
    /** Right branch. */
    PKDUPSIZENODE   mpRight;
    /** Tree height (hmm). */
    KU8             mHeight;
    /** Number of files. */
    KU32            cFiles;
    /** Tree with same sized files.
     * When cFiles is 1 the root node does not have hashes calculated yet.  */
    KDUPFILENODEROOT FileRoot;
} KDUPSIZENODE;

/*#define KAVL_EQUAL_ALLOWED*/
#define KAVL_CHECK_FOR_EQUAL_INSERT
#define KAVL_MAX_STACK          32
/*#define KAVL_RANGE */
/*#define KAVL_OFFSET */
#define KAVL_STD_KEY_COMP
#define KAVLKEY                 KU64
#define KAVLNODE                KDUPSIZENODE
#define KAVL_FN(name)           kDupSizeTree_ ## name
#define KAVL_TYPE(prefix,name)  prefix ## KDUPSIZENODE ## name
#define KAVL_INT(name)          KDUPSIZENODEINT ## name
#define KAVL_DECL(rettype)      static rettype

#include <k/kAvlTmpl/kAvlBase.h>
#include <k/kAvlTmpl/kAvlDoWithAll.h>
//#include <k/kAvlTmpl/kAvlEnum.h> - busted
#include <k/kAvlTmpl/kAvlGet.h>
#include <k/kAvlTmpl/kAvlGetBestFit.h>
#include <k/kAvlTmpl/kAvlGetWithParent.h>
#include <k/kAvlTmpl/kAvlRemove2.h>
#include <k/kAvlTmpl/kAvlRemoveBestFit.h>
#include <k/kAvlTmpl/kAvlUndef.h>


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** The verbosity level. */
static unsigned g_cVerbosity                    = 2;

/** Whether to recurse into subdirectories. */
static KBOOL    g_fRecursive                    = K_FALSE;
/** Whether to recurse into symlinked subdirectories. */
static KBOOL    g_fRecursiveViaSymlinks         = K_FALSE;
/** Whether to follow symbolicly linked files. */
static KBOOL    g_fFollowSymlinkedFiles         = K_TRUE;

/** Minimum file size to care about.   */
static KU64     g_cbMinFileSize                 = 1;
/** Maximum file size to care about.   */
static KU64     g_cbMaxFileSize                 = KU64_MAX;

/** The root of the size tree.   */
static KDUPSIZENODEROOT g_SizeRoot;

/** Global list of duplicate file with duplicates.
 * @remarks This only contains the files in the hash tree, not the ones on
 *          the KDUPFILENODE::pNextDup list. */
static PKDUPFILENODE    g_pDuplicateHead        = NULL;
/** Where to insert the next file with duplicates. */
static PKDUPFILENODE   *g_ppNextDuplicate       = &g_pDuplicateHead;

/** Number of files we're tracking. */
static KU64             g_cFiles                = 0;
/** Number of hardlinked files or files entered more than once. */
static KU64             g_cHardlinked           = 0;
/** Number of duplicates files (not hardlinked). */
static KU64             g_cDuplicates           = 0;
/** Number of duplicates files that can be hardlinked. */
static KU64             g_cDuplicatesSaved      = 0;
/** Size that could be saved if the duplicates were hardlinked. */
static KU64             g_cbDuplicatesSaved     = 0;



/**
 * Wrapper around malloc() that complains when out of memory.
 *
 * @returns Pointer to allocated memory
 * @param   cb      The size of the memory to allocate.
 */
static void *kDupAlloc(KSIZE cb)
{
    void *pvRet = malloc(cb);
    if (pvRet)
        return pvRet;
    fprintf(stderr, "kDeDup: error: out of memory! (cb=%#z)\n", cb);
    return NULL;
}

/** Wrapper around free() for symmetry. */
#define kDupFree(ptr) free(ptr)


static void kDupHashFile(PKDUPFILENODE pFileNode, FTSENT *pFtsEnt)
{
    KSIZE           i;
    PKDUPFILENODE  *ppHash;

    /*
     * Open the file.
     */
    HANDLE hFile;
    if (pFtsEnt && pFtsEnt->fts_parent && pFtsEnt->fts_parent->fts_dirfd != INVALID_HANDLE_VALUE)
        hFile = birdOpenFileExW(pFtsEnt->fts_parent->fts_dirfd, pFtsEnt->fts_wcsname,
                                FILE_READ_DATA | SYNCHRONIZE,
                                FILE_ATTRIBUTE_NORMAL,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                FILE_OPEN,
                                FILE_NON_DIRECTORY_FILE | FILE_OPEN_FOR_BACKUP_INTENT | FILE_SYNCHRONOUS_IO_NONALERT,
                                OBJ_CASE_INSENSITIVE);
    else
        hFile = birdOpenFileExW(NULL, pFileNode->wszPath,
                                FILE_READ_DATA | SYNCHRONIZE,
                                FILE_ATTRIBUTE_NORMAL,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                FILE_OPEN,
                                FILE_NON_DIRECTORY_FILE | FILE_OPEN_FOR_BACKUP_INTENT | FILE_SYNCHRONOUS_IO_NONALERT,
                                OBJ_CASE_INSENSITIVE);
    if (hFile != INVALID_HANDLE_VALUE)
    {
        /*
         * Init the hash calculation contexts.
         */
        struct MD5Context Md5Ctx;
        //SHA256CONTEXT Sha256Ctx;
        MD5Init(&Md5Ctx);
        //Sha256Init(&Sha256Ctx);

        /*
         * Process the file chunk by chunk.
         *
         * We could complicate this by memory mapping medium sized files, but
         * those kind of complications can wait.
         */
        for (;;)
        {
            static KU8          s_abBuffer[2*1024*1024];
            MY_NTSTATUS         rcNt;
            MY_IO_STATUS_BLOCK  Ios;
            Ios.Information = -1;
            Ios.u.Status    = -1;
            rcNt = g_pfnNtReadFile(hFile, NULL /*hEvent*/, NULL /*pfnApc*/, NULL /*pvApcCtx*/,
                                   &Ios, s_abBuffer, sizeof(s_abBuffer), NULL /*poffFile*/, NULL /*puKey*/);
            if (MY_NT_SUCCESS(rcNt))
            {
                MD5Update(&Md5Ctx, s_abBuffer, (unsigned)Ios.Information);
                //SHA256Update(&Sha256Ctx, s_abBuffer, Ios.Information);
            }
            else if (rcNt != STATUS_END_OF_FILE)
            {
                fprintf(stderr, "kDeDup: warning: Error reading '%ls': %#x\n", pFileNode->wszPath, rcNt);
                break;
            }

            /* Check for end of file. */
            if (   rcNt == STATUS_END_OF_FILE
                || Ios.Information < sizeof(s_abBuffer))
            {
                MD5Final(pFileNode->mKey.abMd5, &Md5Ctx);
                //Sha256Final(pFileNode->mKey.abSha2, &Sha256Ctx);

                birdCloseFile(hFile);
                return;
            }
        }

        birdCloseFile(hFile);
    }
    else
        fprintf(stderr, "kDeDup: warning: Failed to open '%ls': %s (%d)\n", pFileNode->wszPath, strerror(errno), errno);

    /*
     * Hashing failed.  We fake the digests by repeating the node pointer value
     * again and again, holding a collision with both SHA2 and MD5 with similar
     * digest pattern for highly unlikely.
     */
    ppHash = (PKDUPFILENODE *)&pFileNode->mKey;
    i = sizeof(pFileNode->mKey) / sizeof(*ppHash);
    while (i-- > 0)
        *ppHash++ = pFileNode;
}


/**
 * Deal with one file, adding it to the tree if it matches the criteria.
 *
 * @returns 0 on success, non-zero on failure.
 * @param   pFtsEnt         The FTS entry for the file.
 */
static int kDupDoFile(FTSENT *pFtsEnt)
{
    KU64 cbFile;

    if (g_cVerbosity >= 2)
        printf("debug: kDupDoFile(%ls)\n", pFtsEnt->fts_wcsaccpath);

    /*
     * Check that it's within the size range.
     */
    cbFile = pFtsEnt->fts_stat.st_size;
    if (   cbFile >= g_cbMinFileSize
        && cbFile <= g_cbMaxFileSize)
    {
        /*
         * Start out treating this like a unique file with a unique size, i.e.
         * allocate all the structures we might possibly need.
         */
        size_t        cbAccessPath = (wcslen(pFtsEnt->fts_wcsaccpath) + 1) * sizeof(wchar_t);
        PKDUPFILENODE pFileNode = (PKDUPFILENODE)kDupAlloc(sizeof(*pFileNode) + cbAccessPath);
        PKDUPSIZENODE pSizeNode = (PKDUPSIZENODE)kDupAlloc(sizeof(*pSizeNode));
        if (!pFileNode || !pSizeNode)
            return 3;
        g_cFiles++;

        memset(&pFileNode->mKey, 0, sizeof(pFileNode->mKey));
        pFileNode->pNextHardLink    = NULL;
        pFileNode->pNextDup         = NULL;
        pFileNode->pNextGlobalDup   = NULL;
        pFileNode->uDev             = pFtsEnt->fts_stat.st_dev;
        pFileNode->uInode           = pFtsEnt->fts_stat.st_ino;
        memcpy(pFileNode->wszPath, pFtsEnt->fts_wcsaccpath, cbAccessPath);

        pSizeNode->mKey = cbFile;
        pSizeNode->cFiles = 1;
        kDupFileTree_Init(&pSizeNode->FileRoot);
        kDupFileTree_Insert(&pSizeNode->FileRoot, pFileNode);

        /*
         * Try insert it.
         */
        if (kDupSizeTree_Insert(&g_SizeRoot, pSizeNode))
        { /* unique size, nothing more to do for now. */ }
        else
        {
            /*
             * More than one file with this size.  We may need to hash the
             * hash the file we encountered with this size, if this is the
             * second one.  In that case we should check for hardlinked or
             * double entering of the file first as well.
             */
            kDupFree(pSizeNode);
            pSizeNode = kDupSizeTree_Get(&g_SizeRoot, cbFile);
            if (pSizeNode->cFiles == 1)
            {
                PKDUPFILENODE pFirstFileNode = pSizeNode->FileRoot.mpRoot;
                if (   pFirstFileNode->uInode == pFileNode->uInode
                    && pFileNode->uInode != 0
                    && pFirstFileNode->uDev   == pFileNode->uDev)
                {
                    pFileNode->pNextHardLink      = pFirstFileNode->pNextHardLink;
                    pFirstFileNode->pNextHardLink = pFileNode;
                    if (g_cVerbosity >= 1)
                        printf("Found hardlinked: '%ls' -> '%ls' (ino:%#" KX64_PRI " dev:%#" KX64_PRI ")\n",
                               pFileNode->wszPath, pFirstFileNode->wszPath, pFileNode->uInode, pFileNode->uDev);
                    g_cHardlinked += 1;
                    return 0;
                }

                kDupHashFile(pFirstFileNode, NULL);
            }
            kDupHashFile(pFileNode, pFtsEnt);

            if (kDupFileTree_Insert(&pSizeNode->FileRoot, pFileNode))
            {  /* great, unique content */ }
            else
            {
                /*
                 * Duplicate content.  Could be hardlinked or a duplicate entry.
                 */
                PKDUPFILENODE pDupFileNode = kDupFileTree_Get(&pSizeNode->FileRoot, pFileNode->mKey);
                if (   pDupFileNode->uInode == pFileNode->uInode
                    && pFileNode->uInode != 0
                    && pDupFileNode->uDev   == pFileNode->uDev)
                {
                    pFileNode->pNextHardLink = pDupFileNode->pNextHardLink;
                    pDupFileNode->pNextHardLink = pFileNode;
                    if (g_cVerbosity >= 1)
                        printf("Found hardlinked: '%ls' -> '%ls' (ino:%#" KX64_PRI " dev:%#" KX64_PRI ")\n",
                               pFileNode->wszPath, pDupFileNode->wszPath, pFileNode->uInode, pFileNode->uDev);
                    g_cHardlinked += 1;
                }
                else
                {
                    KBOOL fDifferentDev;

                    /* Genuinly duplicate (or inode numbers are busted). */
                    if (!pDupFileNode->pNextDup)
                    {
                        *g_ppNextDuplicate = pDupFileNode;
                        g_ppNextDuplicate = &pDupFileNode->pNextGlobalDup;
                    }

                    /* The list is sorted by device to better facility hardlinking later. */
                    while (   (fDifferentDev = pDupFileNode->uDev != pFileNode->uDev)
                           && pDupFileNode->pNextDup)
                        pDupFileNode = pDupFileNode->pNextDup;

                    pFileNode->pNextDup = pDupFileNode->pNextDup;
                    pDupFileNode->pNextDup = pFileNode;

                    g_cDuplicates += 1;
                    if (!fDifferentDev)
                    {
                        g_cDuplicatesSaved += 1;
                        g_cbDuplicatesSaved += pFtsEnt->fts_stat.st_blocks * BIRD_STAT_BLOCK_SIZE;
                        if (g_cVerbosity >= 1)
                            printf("Found duplicate: '%ls' <-> '%ls'\n", pFileNode->wszPath, pDupFileNode->wszPath);
                    }
                    else if (g_cVerbosity >= 1)
                        printf("Found duplicate: '%ls' <-> '%ls' (devices differ).\n", pFileNode->wszPath, pDupFileNode->wszPath);
                }
            }
        }
    }
    else if (g_cVerbosity >= 1)
        printf("Skipping '%ls' because %" KU64_PRI " bytes is outside the size range.\n",
               pFtsEnt->fts_wcsaccpath, cbFile);
    return 0;
}


/**
 * Process the non-option arguments, creating the file tree.
 *
 * @returns 0 on success, non-zero on failure.
 * @param   papwszFtsArgs   The input in argv style.
 * @param   fFtsOptions     The FTS options.
 */
static int kDupReadAll(wchar_t **papwszFtsArgs, unsigned fFtsOptions)
{
    int  rcExit = 0;
    FTS *pFts = nt_fts_openw(papwszFtsArgs, fFtsOptions, NULL /*pfnCompare*/);
    if (pFts != NULL)
    {
        for (;;)
        {
            FTSENT *pFtsEnt = nt_fts_read(pFts);
            if (pFtsEnt)
            {
                switch (pFtsEnt->fts_info)
                {
                    case FTS_F:
                        rcExit = kDupDoFile(pFtsEnt);
                        if (rcExit == 0)
                            continue;
                        break;

                    case FTS_D:
                        if (   g_fRecursive
                            || pFtsEnt->fts_level == FTS_ROOTLEVEL) /* enumerate dirs on the command line */
                            continue;
                        rcExit = nt_fts_set(pFts, pFtsEnt, FTS_SKIP);
                        if (rcExit == 0)
                            continue;
                        fprintf(stderr, "kDeDup: internal error: nt_fts_set failed!\n");
                        rcExit = 1;
                        break;

                    case FTS_DP:
                        /* nothing to do here. */
                        break;

                    case FTS_SL:
                        /* The nice thing on windows is that we already know whether it's a
                           directory or file when encountering the symbolic link. */
                        if (   (pFtsEnt->fts_stat.st_isdirsymlink ? g_fRecursiveViaSymlinks : g_fFollowSymlinkedFiles)
                            &&  pFtsEnt->fts_number == 0)
                        {
                            pFtsEnt->fts_number++;
                            rcExit = nt_fts_set(pFts, pFtsEnt, FTS_FOLLOW);
                            if (rcExit == 0)
                                continue;
                            fprintf(stderr, "kDeDup: internal error: nt_fts_set failed!\n");
                            rcExit = 1;
                        }
                        break;

                    case FTS_DC:
                        fprintf(stderr, "kDeDup: warning: Ignoring cycle '%ls'!\n", pFtsEnt->fts_wcsaccpath);
                        continue;

                    case FTS_NS:
                        fprintf(stderr, "kDeDup: warning: Failed to stat '%ls': %s (%d)\n",
                                pFtsEnt->fts_wcsaccpath, strerror(pFtsEnt->fts_errno), pFtsEnt->fts_errno);
                        continue;

                    case FTS_DNR:
                        fprintf(stderr, "kDeDup: error: Error reading directory '%ls': %s (%d)\n",
                                pFtsEnt->fts_wcsaccpath, strerror(pFtsEnt->fts_errno), pFtsEnt->fts_errno);
                        rcExit = 1;
                        break;

                    case FTS_ERR:
                        fprintf(stderr, "kDeDup: error: Error on '%ls': %s (%d)\n",
                                pFtsEnt->fts_wcsaccpath, strerror(pFtsEnt->fts_errno), pFtsEnt->fts_errno);
                        rcExit = 1;
                        break;


                    /* ignore */
                    case FTS_SLNONE:
                    case FTS_DEFAULT:
                        break;

                    /* Not supposed to get here. */
                    default:
                        fprintf(stderr, "kDeDup: internal error: fts_info=%d - '%ls'\n",
                                pFtsEnt->fts_info, pFtsEnt->fts_wcsaccpath);
                        rcExit = 1;
                        break;
                }
            }
            else if (errno == 0)
                break;
            else
            {
                fprintf(stderr, "kDeDup: error: nt_fts_read failed: %s (%d)\n", strerror(errno), errno);
                rcExit = 1;
                break;
            }
        }

        if (nt_fts_close(pFts) != 0)
        {
            fprintf(stderr, "kDeDup: error: nt_fts_close failed: %s (%d)\n", strerror(errno), errno);
            rcExit = 1;
        }
    }
    else
    {
        fprintf(stderr, "kDeDup: error: nt_fts_openw failed: %s (%d)\n", strerror(errno), errno);
        rcExit = 1;
    }

    return rcExit;
}


/**
 * Hardlink duplicates.
 */
static int kDupHardlinkDuplicates(void)
{
    int           rcExit = 0;
    PKDUPFILENODE pFileNode;
    for (pFileNode = g_pDuplicateHead; pFileNode != NULL; pFileNode = pFileNode->pNextGlobalDup)
    {
        PKDUPFILENODE pTargetFile = pFileNode;
        PKDUPFILENODE pDupFile;
        for (pDupFile = pFileNode->pNextDup; pDupFile != NULL; pDupFile = pDupFile->pNextDup)
        {
            /*
             * Can only hard link if the files are on the same device.
             */
            if (pDupFile->uDev == pTargetFile->uDev)
            {
                /** @todo compare the files?   */
                if (1)
                {
                    /*
                     * Start by renaming the orinal file before we try create the hard link.
                     */
                    static const wchar_t s_wszBackupSuffix[] = L".kDepBackup";
                    wchar_t wszBackup[0x4000];
                    size_t  cwcPath = wcslen(pDupFile->wszPath);
                    if (cwcPath + sizeof(s_wszBackupSuffix) / sizeof(wchar_t) < K_ELEMENTS(wszBackup))
                    {
                        memcpy(wszBackup, pDupFile->wszPath, cwcPath * sizeof(wchar_t));
                        memcpy(&wszBackup[cwcPath], s_wszBackupSuffix, sizeof(s_wszBackupSuffix));
                        if (MoveFileW(pDupFile->wszPath, wszBackup))
                        {
                            if (CreateHardLinkW(pDupFile->wszPath, pTargetFile->wszPath, NULL))
                            {
                                if (birdUnlinkForcedW(wszBackup) == 0)
                                {
                                    if (g_cVerbosity >= 1)
                                        printf("Hardlinked '%ls' to '%ls'.\n", pDupFile->wszPath, pTargetFile->wszPath);
                                }
                                else
                                {
                                    fprintf(stderr, "kDeDup: fatal: failed to delete '%ls' after hardlinking: %s (%d)\n",
                                            wszBackup, strerror(errno), errno);
                                    return 8;
                                }
                            }
                            else
                            {
                                fprintf(stderr, "kDeDup: error: failed to hard link '%ls' to '%ls': %u\n",
                                        pDupFile->wszPath, wszBackup, GetLastError());
                                if (!MoveFileW(wszBackup, pDupFile->wszPath))
                                {
                                    fprintf(stderr, "kDeDup: fatal: Restore back '%ls' to '%ls' after hardlinking faild: %u\n",
                                            wszBackup, pDupFile->wszPath, GetLastError());
                                    return 8;
                                }
                                rcExit = 1;
                            }
                        }
                        else
                        {
                            fprintf(stderr, "kDeDup: error: failed to rename '%ls' to '%ls': %u\n",
                                    pDupFile->wszPath, wszBackup, GetLastError());
                            rcExit = 1;
                        }
                    }
                    else
                    {
                        fprintf(stderr, "kDeDup: error: too long backup path: '%ls'\n", pDupFile->wszPath);
                        rcExit = 1;
                    }
                }
            }
            /*
             * Since the list is sorted by uDev, we now change the target file.
             */
            else
                pTargetFile = pDupFile;
        }
    }
    return rcExit;
}


static int usage(const char *pszName, FILE *pOut)
{
    fprintf(pOut,
            "usage: %s [options] <path1> [path2 [..]]\n"
            "usage: %s <-V|--version>\n"
            "usage: %s <-h|--help>\n"
            , pszName, pszName, pszName);
    fprintf(pOut,
            "\n"
            "Options:\n"
            "  -H, --dereference-command-line, --no-dereference-command-line\n"
            "    Follow symbolic links on the command line.\n"
            "  -L, --dereference\n"
            "    Follow symbolic links while scanning directories.\n"
            "  -P, --no-dereference\n"
            "    Do not follow symbolic links while scanning directories.\n"
            "  -r, --recursive\n"
            "    Recurse into subdirectories, but do not follow links to them.\n"
            "  -R, --recursive-dereference\n"
            "    Same as -r, but also follow into symlinked subdirectories.\n"
            "  -x, --one-file-system\n"
            "    Do not consider other file system (volumes), either down thru a\n"
            "    mount point or via a symbolic link to a directory.\n"
            "  --no-one-file-system, --cross-file-systems\n"
            "    Reverses the effect of --one-file-system.\n"
            );
    return 0;
}


int wmain(int argc, wchar_t **argv)
{
    int             rcExit;

    /*
     * Process parameters.  Position.
     */
    wchar_t   **papwszFtsArgs = (wchar_t **)calloc(argc + 1, sizeof(wchar_t *));
    unsigned    cFtsArgs      = 0;
    unsigned    fFtsOptions   = FTS_NOCHDIR | FTS_NO_ANSI;
    KBOOL       fEndOfOptions = K_FALSE;
    KBOOL       fHardlinkDups = K_FALSE;
    int         i;
    for (i = 1; i < argc; i++)
    {
        wchar_t *pwszArg = argv[i];
        if (   *pwszArg == '-'
            && !fEndOfOptions)
        {
            wchar_t wcOpt = *++pwszArg;
            pwszArg++;
            if (wcOpt == '-')
            {
                /* Translate long options. */
                if (wcscmp(pwszArg, L"help") == 0)
                    wcOpt = 'h';
                else if (wcscmp(pwszArg, L"version") == 0)
                    wcOpt = 'V';
                else if (wcscmp(pwszArg, L"recursive") == 0)
                    wcOpt = 'r';
                else if (wcscmp(pwszArg, L"dereference-recursive") == 0)
                    wcOpt = 'R';
                else if (wcscmp(pwszArg, L"dereference") == 0)
                    wcOpt = 'L';
                else if (wcscmp(pwszArg, L"dereference-command-line") == 0)
                    wcOpt = 'H';
                else if (wcscmp(pwszArg, L"one-file-system") == 0)
                    wcOpt = 'x';
                /* Process long options. */
                else if (*pwszArg == '\0')
                {
                    fEndOfOptions = K_TRUE;
                    continue;
                }
                else if (wcscmp(pwszArg, L"no-recursive") == 0)
                {
                    g_fRecursive = g_fRecursiveViaSymlinks = K_FALSE;
                    continue;
                }
                else if (wcscmp(pwszArg, L"no-dereference-command-line") == 0)
                {
                    fFtsOptions &= ~FTS_COMFOLLOW;
                    continue;
                }
                else if (   wcscmp(pwszArg, L"no-one-file-system") == 0
                         || wcscmp(pwszArg, L"cross-file-systems") == 0)
                {
                    fFtsOptions &= ~FTS_XDEV;
                    continue;
                }
                else if (wcscmp(pwszArg, L"hardlink-duplicates") == 0)
                {
                    fHardlinkDups = K_TRUE;
                    continue;
                }
                else
                {
                    fprintf(stderr, "kDeDup: syntax error: Unknown option '--%ls'\n", pwszArg);
                    return 2;
                }
            }

            /* Process one or more short options. */
            do
            {
                switch (wcOpt)
                {
                    case 'r': /* --recursive */
                        g_fRecursive = K_TRUE;
                        break;

                    case 'R': /* --dereference-recursive */
                        g_fRecursive = g_fRecursiveViaSymlinks = K_TRUE;
                        break;

                    case 'H': /* --dereference-command-line */
                        fFtsOptions |= FTS_COMFOLLOW;
                        break;

                    case 'L': /* --dereference*/
                        g_fFollowSymlinkedFiles = K_TRUE;
                        break;

                    case 'x': /* --one-file-system*/
                        fFtsOptions |= FTS_XDEV;
                        break;

                    case 'q':
                        g_cVerbosity = 0;
                        break;

                    case 'v':
                        g_cVerbosity++;
                        break;


                    case 'h':
                    case '?':
                        return usage("kDeDup", stdout);

                    case 'V':
                        printf("0.0.1\n");
                        return 0;

                    default:
                        fprintf(stderr, "kDeDup: syntax error: Unknown option '-%lc'\n", wcOpt);
                        return 2;
                }

                wcOpt = *pwszArg++;
            } while (wcOpt != '\0');
        }
        else
        {
            /*
             * Append non-option arguments to the FTS argument vector.
             */
            papwszFtsArgs[cFtsArgs] = pwszArg;
            cFtsArgs++;
        }
    }

    /*
     * Do the FTS processing.
     */
    kDupSizeTree_Init(&g_SizeRoot);
    rcExit = kDupReadAll(papwszFtsArgs, fFtsOptions);
    if (rcExit == 0)
    {
        /*
         * Display the result.
         */
        printf("Found %" KU64_PRI " duplicate files, out which %" KU64_PRI " can be hardlinked saving %" KU64_PRI " bytes\n",
               g_cDuplicates, g_cDuplicatesSaved, g_cbDuplicatesSaved);

        if (fHardlinkDups)
            rcExit = kDupHardlinkDuplicates();
    }

    return rcExit;
}

