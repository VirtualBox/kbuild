/* $Id: dir-nt-bird.c 2861 2016-09-01 22:42:55Z knut.osmundsen@oracle.com $ */
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
#include "make.h"
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
PKFSCACHE g_pFsCache = NULL;


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
                    PKFSOBJ pNameObj = kFsCacheLookupRelativeToDirA(g_pFsCache, (PKFSDIR)pDirObj,
                                                                    pszName, strlen(pszName), &enmError);
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
            /* Copy the name that fits.  If neither fits, skip the name. */
            if (pEntry->cchName < sizeof(pDir->DirEnt.d_name))
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


void print_dir_data_base(void)
{
    /** @todo. */
}



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
                    kHlpAssert(pAncestor->ObjcchName > 0);
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
            if (pPathObj->cchName + 1 < cbFull)
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
 * Special stat call used by remake.c
 *
 * @returns 0 on success, -1 + errno on failure.
 * @param   pszPath             The path to stat.
 * @param   pStat               Where to return the mtime field only.
 */
int stat_only_mtime(const char *pszPath, struct stat *pStat)
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


/**
 * Invalidate missing bits of the directory cache.
 *
 * This is called each time a make job completes.
 */
void dir_cache_invalid_missing(void)
{
    kFsCacheInvalidateMissing(g_pFsCache);
}

