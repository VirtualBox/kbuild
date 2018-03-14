/* $Id: dir-nt-bird.c 3140 2018-03-14 21:28:10Z knut.osmundsen@oracle.com $ */
/** @file
 * Reimplementation of dir.c for NT using kFsCache.
 *
 * This should perform better on NT, especially on machines "infected"
 * by antivirus programs.
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
#include "nt/kFsCache.h"
#include "makeint.h"
#if defined(KMK) && !defined(__OS2__)
# include "glob/glob.h"
#else
# include <glob.h>
#endif


#include "nt_fullpath.h" /* for the time being - will be implemented here later on. */


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** User data key indicating that it's an impossible file to make.
 * See file_impossible() and file_impossible_p(). */
#define KMK_DIR_NT_IMPOSSIBLE_KEY   (~(KUPTR)7)


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * glob directory stream.
 */
typedef struct KMKNTOPENDIR
{
    /** Reference to the directory. */
    PKFSDIR         pDir;
    /** Index of the next directory entry (child) to return. */
    KU32            idxNext;
    /** The structure in which to return the directory entry.   */
    struct dirent   DirEnt;
} KMKNTOPENDIR;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** The cache.*/
PKFSCACHE   g_pFsCache = NULL;
/** Number of times dir_cache_invalid_missing was called. */
static KU32 g_cInvalidates = 0;
/** Set by dir_cache_volatile_dir to indicate that the user has marked the
 * volatile parts of the file system with custom revisioning and we only need to
 * flush these.  This is very handy when using a separate output directory
 * from the sources.  */
static KBOOL g_fFsCacheIsUsingCustomRevision = K_FALSE;


void hash_init_directories(void)
{
    g_pFsCache = kFsCacheCreate(0);
    if (g_pFsCache)
        return;
    fputs("kFsCacheCreate failed!", stderr);
    exit(9);
}


/**
 * Checks if @a pszName exists in directory @a pszDir.
 *
 * @returns 1 if it does, 0 if it doesn't.
 *
 * @param   pszDir      The directory.
 * @param   pszName     The name.
 *
 *                      If empty string, just check if the directory exists.
 *
 *                      If NULL, just read the whole cache the directory into
 *                      the cache (we always do that).
 */
int dir_file_exists_p(const char *pszDir, const char *pszName)
{
    int             fRc     = 0;
    KFSLOOKUPERROR  enmError;
    PKFSOBJ         pDirObj = kFsCacheLookupA(g_pFsCache, pszDir, &enmError);
    if (pDirObj)
    {
        if (pDirObj->bObjType == KFSOBJ_TYPE_DIR)
        {
            if (pszName != 0)
            {
                /* Empty filename is just checking out the directory. */
                if (*pszName == '\0')
                    fRc = 1;
                else
                {
                    PKFSOBJ pNameObj = kFsCacheLookupRelativeToDirA(g_pFsCache, (PKFSDIR)pDirObj, pszName, strlen(pszName),
                                                                    0/*fFlags*/, &enmError, NULL);
                    if (pNameObj)
                    {
                        fRc = pNameObj->bObjType == KFSOBJ_TYPE_MISSING;
                        kFsCacheObjRelease(g_pFsCache, pNameObj);
                    }
                }
            }
        }
        kFsCacheObjRelease(g_pFsCache, pDirObj);
    }
    return fRc;
}


/**
 * Checks if a file exists.
 *
 * @returns 1 if it does exist, 0 if it doesn't.
 * @param   pszPath     The path to check out.
 */
int file_exists_p(const char *pszPath)
{
    int             fRc;
    KFSLOOKUPERROR  enmError;
    PKFSOBJ         pPathObj = kFsCacheLookupA(g_pFsCache, pszPath, &enmError);
    if (pPathObj)
    {
        fRc = pPathObj->bObjType != KFSOBJ_TYPE_MISSING;
        kFsCacheObjRelease(g_pFsCache, pPathObj);
    }
    else
        fRc = 0;
    return fRc;
}


/**
 * Just a way for vpath.c to get a correctly cased path, I think.
 *
 * @returns Directory path in string cache.
 * @param   pszDir      The directory.
 */
const char *dir_name(const char *pszDir)
{
    char szTmp[MAX_PATH];
    nt_fullpath(pszDir, szTmp, sizeof(szTmp));
    return strcache_add(szTmp);
}


/**
 * Makes future file_impossible_p calls return 1 for pszPath.
 */
void file_impossible(const char *pszPath)
{
    KFSLOOKUPERROR  enmError;
    PKFSOBJ         pPathObj = kFsCacheLookupA(g_pFsCache, pszPath, &enmError);
    if (pPathObj)
    {
        kFsCacheObjAddUserData(g_pFsCache, pPathObj, KMK_DIR_NT_IMPOSSIBLE_KEY, sizeof(KFSUSERDATA));
        kFsCacheObjRelease(g_pFsCache, pPathObj);
    }
}

/**
 * Makes future file_impossible_p calls return 1 for pszPath.
 */
int file_impossible_p(const char *pszPath)
{
    int             fRc;
    KFSLOOKUPERROR  enmError;
    PKFSOBJ         pPathObj = kFsCacheLookupA(g_pFsCache, pszPath, &enmError);
    if (pPathObj)
    {
        fRc = kFsCacheObjGetUserData(g_pFsCache, pPathObj, KMK_DIR_NT_IMPOSSIBLE_KEY) != NULL;
        kFsCacheObjRelease(g_pFsCache, pPathObj);
    }
    else
        fRc = 0;
    return fRc;
}


/**
 * opendir for glob.
 *
 * @returns Pointer to DIR like handle, NULL if directory not found.
 * @param   pszDir              The directory to enumerate.
 */
static __ptr_t dir_glob_opendir(const char *pszDir)
{
    KFSLOOKUPERROR  enmError;
    PKFSOBJ         pDirObj = kFsCacheLookupA(g_pFsCache, pszDir, &enmError);
    if (pDirObj)
    {
        if (pDirObj->bObjType == KFSOBJ_TYPE_DIR)
        {
            if (kFsCacheDirEnsurePopuplated(g_pFsCache, (PKFSDIR)pDirObj, NULL))
            {
                KMKNTOPENDIR *pDir = xmalloc(sizeof(*pDir));
                pDir->pDir    = (PKFSDIR)pDirObj;
                pDir->idxNext = 0;
                return pDir;
            }
        }
        kFsCacheObjRelease(g_pFsCache, pDirObj);
    }
    return NULL;
}


/**
 * readdir for glob.
 *
 * @returns Pointer to DIR like handle, NULL if directory not found.
 * @param   pDir                Directory enum handle by dir_glob_opendir.
 */
static struct dirent *dir_glob_readdir(__ptr_t pvDir)
{
    KMKNTOPENDIR *pDir = (KMKNTOPENDIR *)pvDir;
    KU32 const    cChildren = pDir->pDir->cChildren;
    while (pDir->idxNext < cChildren)
    {
        PKFSOBJ pEntry = pDir->pDir->papChildren[pDir->idxNext++];

        /* Don't return missing objects. */
        if (pEntry->bObjType != KFSOBJ_TYPE_MISSING)
        {
            /* Copy the name that fits, trying to avoid names with spaces.
               If neither fits, skip the name. */
            if (   pEntry->cchName < sizeof(pDir->DirEnt.d_name)
                && (   pEntry->pszShortName == pEntry->pszName
                    || memchr(pEntry->pszName, ' ', pEntry->cchName) == NULL))
            {
                pDir->DirEnt.d_namlen = pEntry->cchName;
                memcpy(pDir->DirEnt.d_name, pEntry->pszName, pEntry->cchName + 1);
            }
            else if (pEntry->cchShortName < sizeof(pDir->DirEnt.d_name))
            {
                pDir->DirEnt.d_namlen = pEntry->cchShortName;
                memcpy(pDir->DirEnt.d_name, pEntry->pszShortName, pEntry->cchShortName + 1);
            }
            else
                continue;

            pDir->DirEnt.d_reclen = offsetof(struct dirent, d_name) + pDir->DirEnt.d_namlen;
            if (pEntry->bObjType == KFSOBJ_TYPE_DIR)
                pDir->DirEnt.d_type = DT_DIR;
            else if (pEntry->bObjType == KFSOBJ_TYPE_FILE)
                pDir->DirEnt.d_type = DT_REG;
            else
                pDir->DirEnt.d_type   = DT_UNKNOWN;

            return &pDir->DirEnt;
        }
    }

    /*
     * Fake the '.' and '..' directories because they're not part of papChildren above.
     */
    if (pDir->idxNext < cChildren + 2)
    {
        pDir->idxNext++;
        pDir->DirEnt.d_type    = DT_DIR;
        pDir->DirEnt.d_namlen  = pDir->idxNext - cChildren;
        pDir->DirEnt.d_reclen  = offsetof(struct dirent, d_name) + pDir->DirEnt.d_namlen;
        pDir->DirEnt.d_name[0] = '.';
        pDir->DirEnt.d_name[1] = '.';
        pDir->DirEnt.d_name[pDir->DirEnt.d_namlen] = '\0';
        return &pDir->DirEnt;
    }

    return NULL;
}


/**
 * closedir for glob.
 *
 * @param   pDir                Directory enum handle by dir_glob_opendir.
 */
static void dir_glob_closedir(__ptr_t pvDir)
{
    KMKNTOPENDIR *pDir = (KMKNTOPENDIR *)pvDir;
    kFsCacheObjRelease(g_pFsCache, &pDir->pDir->Obj);
    pDir->pDir = NULL;
    free(pDir);
}


/**
 * stat for glob.
 *
 * @returns 0 on success, -1 + errno on failure.
 * @param   pszPath             The path to stat.
 * @param   pStat               Where to return the info.
 */
static int dir_glob_stat(const char *pszPath, struct stat *pStat)
{
    KFSLOOKUPERROR  enmError;
    PKFSOBJ         pPathObj = kFsCacheLookupA(g_pFsCache, pszPath, &enmError);
/** @todo follow symlinks vs. on symlink!   */
    if (pPathObj)
    {
        if (pPathObj->bObjType != KFSOBJ_TYPE_MISSING)
        {
            kHlpAssert(pPathObj->fHaveStats); /* currently always true. */
            *pStat = pPathObj->Stats;
            kFsCacheObjRelease(g_pFsCache, pPathObj);
            return 0;
        }
        kFsCacheObjRelease(g_pFsCache, pPathObj);
    }
    errno = ENOENT;
    return -1;
}


/**
 * lstat for glob.
 *
 * @returns 0 on success, -1 + errno on failure.
 * @param   pszPath             The path to stat.
 * @param   pStat               Where to return the info.
 */
static int dir_glob_lstat(const char *pszPath, struct stat *pStat)
{
    KFSLOOKUPERROR  enmError;
    PKFSOBJ         pPathObj = kFsCacheLookupA(g_pFsCache, pszPath, &enmError);
    if (pPathObj)
    {
        if (pPathObj->bObjType != KFSOBJ_TYPE_MISSING)
        {
            kHlpAssert(pPathObj->fHaveStats); /* currently always true. */
            *pStat = pPathObj->Stats;
            kFsCacheObjRelease(g_pFsCache, pPathObj);
            return 0;
        }
        kFsCacheObjRelease(g_pFsCache, pPathObj);
        errno = ENOENT;
    }
    else
        errno =    enmError == KFSLOOKUPERROR_NOT_DIR
                || enmError == KFSLOOKUPERROR_PATH_COMP_NOT_DIR
              ? ENOTDIR : ENOENT;

    return -1;
}


/**
 * Checks if @a pszDir exists and is a directory.
 *
 * @returns 1 if is directory, 0 if isn't or doesn't exists.
 * @param   pszDir              The alleged directory.
 */
static int dir_globl_dir_exists_p(const char *pszDir)
{
    int             fRc;
    KFSLOOKUPERROR  enmError;
    PKFSOBJ         pDirObj = kFsCacheLookupA(g_pFsCache, pszDir, &enmError);
    if (pDirObj)
    {
        fRc = pDirObj->bObjType == KFSOBJ_TYPE_DIR;
        kFsCacheObjRelease(g_pFsCache, pDirObj);
    }
    else
        fRc = 0;
    return fRc;

}


/**
 * Sets up pGlob with the necessary callbacks.
 *
 * @param   pGlob               Structure to populate.
 */
void dir_setup_glob(glob_t *pGlob)
{
    pGlob->gl_opendir   = dir_glob_opendir;
    pGlob->gl_readdir   = dir_glob_readdir;
    pGlob->gl_closedir  = dir_glob_closedir;
    pGlob->gl_stat      = dir_glob_stat;
#ifdef __EMX__ /* The FreeBSD implementation actually uses gl_lstat!! */
    pGlob->gl_lstat     = dir_glob_lstat;
#else
    pGlob->gl_exists    = file_exists_p;
    pGlob->gl_isdir     = dir_globl_dir_exists_p;
#endif
}


/**
 * Print statitstics.
 */
void print_dir_stats(void)
{
    FILE *pOut = stdout;
    KU32 cMisses;

    fputs("\n"
          "# NT dir cache stats:\n", pOut);
    fprintf(pOut, "#  %u objects, taking up %u (%#x) bytes, avg %u bytes\n",
            g_pFsCache->cObjects, g_pFsCache->cbObjects, g_pFsCache->cbObjects, g_pFsCache->cbObjects / g_pFsCache->cObjects);
    fprintf(pOut, "#  %u A path hashes, taking up %u (%#x) bytes, avg %u bytes, %u collision\n",
            g_pFsCache->cAnsiPaths, g_pFsCache->cbAnsiPaths, g_pFsCache->cbAnsiPaths,
            g_pFsCache->cbAnsiPaths / K_MAX(g_pFsCache->cAnsiPaths, 1), g_pFsCache->cAnsiPathCollisions);
#ifdef KFSCACHE_CFG_UTF16
    fprintf(pOut, "#  %u W path hashes, taking up %u (%#x) bytes, avg %u bytes, %u collisions\n",
            g_pFsCache->cUtf16Paths, g_pFsCache->cbUtf16Paths, g_pFsCache->cbUtf16Paths,
            g_pFsCache->cbUtf16Paths / K_MAX(g_pFsCache->cUtf16Paths, 1), g_pFsCache->cUtf16PathCollisions);
#endif
    fprintf(pOut, "#  %u child hash tables, total of %u entries, %u children inserted, %u collisions\n",
            g_pFsCache->cChildHashTabs, g_pFsCache->cChildHashEntriesTotal,
            g_pFsCache->cChildHashed, g_pFsCache->cChildHashCollisions);

    cMisses = g_pFsCache->cLookups - g_pFsCache->cPathHashHits - g_pFsCache->cWalkHits;
    fprintf(pOut, "#  %u lookups: %u (%" KU64_PRI " %%) path hash hits, %u (%" KU64_PRI "%%) walks hits, %u (%" KU64_PRI "%%) misses\n",
            g_pFsCache->cLookups,
            g_pFsCache->cPathHashHits, g_pFsCache->cPathHashHits * (KU64)100 / K_MAX(g_pFsCache->cLookups, 1),
            g_pFsCache->cWalkHits, g_pFsCache->cWalkHits * (KU64)100 / K_MAX(g_pFsCache->cLookups, 1),
            cMisses, cMisses * (KU64)100 / K_MAX(g_pFsCache->cLookups, 1));
    fprintf(pOut, "#  %u child searches, %u (%" KU64_PRI "%%) hash hits\n",
            g_pFsCache->cChildSearches,
            g_pFsCache->cChildHashHits, g_pFsCache->cChildHashHits * (KU64)100 / K_MAX(g_pFsCache->cChildSearches, 1));
}


void print_dir_data_base(void)
{
    /** @todo. */

}


/* duplicated in kWorker.c
 * Note! Tries avoid to produce a result with spaces since they aren't supported by makefiles.  */
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

                pszFull[offEnd] = '\0';
                memcpy(&pszFull[off], pPathObj->pszName, pPathObj->cchName);

                for (pAncestor = pPathObj->pParent; off > 0; pAncestor = pAncestor->Obj.pParent)
                {
                    kHlpAssert(off > 1);
                    kHlpAssert(pAncestor != NULL);
                    kHlpAssert(pAncestor->Obj.cchName > 0);
                    pszFull[--off] = '/';
#ifdef KFSCACHE_CFG_SHORT_NAMES
                    if (   pAncestor->Obj.pszName == pAncestor->Obj.pszShortName
                        || memchr(pAncestor->Obj.pszName, ' ', pAncestor->Obj.cchName) == NULL)
#endif
                    {
                        off -= pAncestor->Obj.cchName;
                        kHlpAssert(pAncestor->Obj.cchParent == off);
                        memcpy(&pszFull[off], pAncestor->Obj.pszName, pAncestor->Obj.cchName);
                    }
#ifdef KFSCACHE_CFG_SHORT_NAMES
                    else
                    {
                        /*
                         * The long name constains a space, so use the alternative name instead.
                         * Most likely the alternative name differs in length, usually it's shorter,
                         * so we have to shift the part of the path we've already assembled
                         * accordingly.
                         */
                        KSSIZE cchDelta = (KSSIZE)pAncestor->Obj.cchShortName - (KSSIZE)pAncestor->Obj.cchName;
                        if (cchDelta != 0)
                        {
                            if ((KSIZE)(offEnd + cchDelta) >= cbFull)
                                goto l_fallback;
                            memmove(&pszFull[off + cchDelta], &pszFull[off], offEnd + 1 - off);
                            off    += cchDelta;
                            offEnd += cchDelta;
                        }
                        off -= pAncestor->Obj.cchShortName;
                        kHlpAssert(pAncestor->Obj.cchParent == off);
                        memcpy(&pszFull[off], pAncestor->Obj.pszShortName, pAncestor->Obj.cchShortName);
                    }
#endif
                }
                kFsCacheObjRelease(g_pFsCache, pPathObj);
                return;
            }
        }
        else
        {
            if ((size_t)pPathObj->cchName + 1 < cbFull)
            {
                /* Assume no spaces here. */
                memcpy(pszFull, pPathObj->pszName, pPathObj->cchName);
                pszFull[pPathObj->cchName] = '/';
                pszFull[pPathObj->cchName + 1] = '\0';

                kFsCacheObjRelease(g_pFsCache, pPathObj);
                return;
            }
        }

        /* do fallback. */
#ifdef KFSCACHE_CFG_SHORT_NAMES
l_fallback:
#endif
        kHlpAssertFailed();
        kFsCacheObjRelease(g_pFsCache, pPathObj);
    }

    nt_fullpath(pszPath, pszFull, cbFull);
}


/**
 * Special stat call used by remake.c
 *
 * @returns 0 on success, -1 + errno on failure.
 * @param   pszPath             The path to stat.
 * @param   pStat               Where to return the mtime field only.
 */
int stat_only_mtime(const char *pszPath, struct stat *pStat)
{
    /* Currently a little expensive, so just hit the file system once the
       jobs starts comming in. */
    if (g_cInvalidates == 0)
    {
        KFSLOOKUPERROR  enmError;
        PKFSOBJ         pPathObj = kFsCacheLookupA(g_pFsCache, pszPath, &enmError);
        if (pPathObj)
        {
            if (pPathObj->bObjType != KFSOBJ_TYPE_MISSING)
            {
                kHlpAssert(pPathObj->fHaveStats); /* currently always true. */
                pStat->st_mtime = pPathObj->Stats.st_mtime;
                kFsCacheObjRelease(g_pFsCache, pPathObj);
                return 0;
            }

            kFsCacheObjRelease(g_pFsCache, pPathObj);
            errno = ENOENT;
        }
        else
            errno =    enmError == KFSLOOKUPERROR_NOT_DIR
                    || enmError == KFSLOOKUPERROR_PATH_COMP_NOT_DIR
                  ? ENOTDIR : ENOENT;
        return -1;
    }
    return birdStatModTimeOnly(pszPath, &pStat->st_mtim, 1 /*fFollowLink*/);
}

/**
 * Do cache invalidation after a job completes.
 */
void dir_cache_invalid_after_job(void)
{
    g_cInvalidates++;
    if (g_fFsCacheIsUsingCustomRevision)
        kFsCacheInvalidateCustomBoth(g_pFsCache);
    else
        kFsCacheInvalidateAll(g_pFsCache);
}

/**
 * Invalidate the whole directory cache
 *
 * Used by $(dircache-ctl invalidate)
 */
void dir_cache_invalid_all(void)
{
    g_cInvalidates++;
    kFsCacheInvalidateAll(g_pFsCache);
}

/**
 * Invalidate missing bits of the directory cache.
 *
 * Used by $(dircache-ctl invalidate-missing)
 */
void dir_cache_invalid_missing(void)
{
    g_cInvalidates++;
    kFsCacheInvalidateAll(g_pFsCache);
}

/**
 * Invalidate the volatile bits of the directory cache.
 *
 * Used by $(dircache-ctl invalidate-missing)
 */
void dir_cache_invalid_volatile(void)
{
    g_cInvalidates++;
    if (g_fFsCacheIsUsingCustomRevision)
        kFsCacheInvalidateCustomBoth(g_pFsCache);
    else
        kFsCacheInvalidateAll(g_pFsCache);
}

/**
 * Used by $(dircache-ctl ) to mark a directory subtree or file as volatile.
 *
 * The first call changes the rest of the cache to be considered non-volatile.
 *
 * @returns 0 on success, -1 on failure.
 * @param   pszDir      The directory (or file for what that is worth).
 */
int dir_cache_volatile_dir(const char *pszDir)
{
    KFSLOOKUPERROR enmError;
    PKFSOBJ pObj = kFsCacheLookupA(g_pFsCache, pszDir, &enmError);
    if (pObj)
    {
        KBOOL fRc = kFsCacheSetupCustomRevisionForTree(g_pFsCache, pObj);
        kFsCacheObjRelease(g_pFsCache, pObj);
        if (fRc)
        {
            g_fFsCacheIsUsingCustomRevision = K_TRUE;
            return 0;
        }
        OS(error, reading_file, "failed to mark '%s' as volatile", pszDir);
    }
    else
        OS(error, reading_file, "failed to mark '%s' as volatile (not found)", pszDir);
    return -1;
}

/**
 * Invalidates a deleted directory so the cache can close handles to it.
 *
 * Used by kmk_builtin_rm and kmk_builtin_rmdir.
 *
 * @returns 0 on success, -1 on failure.
 * @param   pszDir      The directory to invalidate as deleted.
 */
int dir_cache_deleted_directory(const char *pszDir)
{
    if (kFsCacheInvalidateDeletedDirectoryA(g_pFsCache, pszDir))
        return 0;
    return -1;
}


int kmk_builtin_dircache(int argc, char **argv, char **envp)
{
    if (argc >= 2)
    {
        const char *pszCmd = argv[1];
        if (strcmp(pszCmd, "invalidate") == 0)
        {
            if (argc == 2)
            {
                dir_cache_invalid_all();
                return 0;
            }
            fprintf(stderr, "kmk_builtin_dircache: the 'invalidate' command takes no arguments!\n");
        }
        else if (strcmp(pszCmd, "invalidate-missing") == 0)
        {
            if (argc == 2)
            {
                dir_cache_invalid_missing ();
                return 0;
            }
            fprintf(stderr, "kmk_builtin_dircache: the 'invalidate-missing' command takes no arguments!\n");
        }
        else if (strcmp(pszCmd, "volatile") == 0)
        {
            int i;
            for (i = 2; i < argc; i++)
                dir_cache_volatile_dir(argv[i]);
            return 0;
        }
        else if (strcmp(pszCmd, "deleted") == 0)
        {
            int i;
            for (i = 2; i < argc; i++)
                dir_cache_deleted_directory(argv[i]);
            return 0;
        }
        else
            fprintf(stderr, "kmk_builtin_dircache: Invalid command '%s'!\n", pszCmd);
    }
    else
        fprintf(stderr, "kmk_builtin_dircache: No command given!\n");

    K_NOREF(envp);
    return 2;
}

