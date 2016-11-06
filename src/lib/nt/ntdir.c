/* $Id: ntdir.c 3005 2016-11-06 00:07:37Z knut.osmundsen@oracle.com $ */
/** @file
 * MSC + NT opendir, readdir, telldir, seekdir, and closedir.
 */

/*
 * Copyright (c) 2005-2016 knut st. osmundsen <bird-kBuild-spamx@anduin.net>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 * Alternatively, the content of this file may be used under the terms of the
 * GPL version 2 or later, or LGPL version 2.1 or later.
 */


/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
#include <stdio.h>
#include <errno.h>
#include <malloc.h>
#include <assert.h>

#include "ntstuff.h"
#include "nthlp.h"
#include "ntdir.h"


/**
 * Implements opendir.
 */
BirdDir_T *birdDirOpen(const char *pszPath)
{
    HANDLE hDir = birdOpenFile(pszPath,
                               FILE_READ_DATA | SYNCHRONIZE,
                               FILE_ATTRIBUTE_NORMAL,
                               FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                               FILE_OPEN,
                               FILE_DIRECTORY_FILE | FILE_OPEN_FOR_BACKUP_INTENT | FILE_SYNCHRONOUS_IO_NONALERT,
                               OBJ_CASE_INSENSITIVE);
    if (hDir != INVALID_HANDLE_VALUE)
    {
        BirdDir_T *pDir = birdDirOpenFromHandle((void *)hDir, NULL, BIRDDIR_F_CLOSE_HANDLE);
        if (pDir)
            return pDir;
        birdCloseFile(hDir);
    }
    return NULL;
}


/**
 * Alternative opendir, with extra stat() info returned by readdir().
 */
BirdDir_T *birdDirOpenExtraInfo(const char *pszPath)
{
    HANDLE hDir = birdOpenFile(pszPath,
                               FILE_READ_DATA | SYNCHRONIZE,
                               FILE_ATTRIBUTE_NORMAL,
                               FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                               FILE_OPEN,
                               FILE_DIRECTORY_FILE | FILE_OPEN_FOR_BACKUP_INTENT | FILE_SYNCHRONOUS_IO_NONALERT,
                               OBJ_CASE_INSENSITIVE);
    if (hDir != INVALID_HANDLE_VALUE)
    {
        BirdDir_T *pDir = birdDirOpenFromHandle((void *)hDir, NULL, BIRDDIR_F_CLOSE_HANDLE | BIRDDIR_F_EXTRA_INFO);
        if (pDir)
            return pDir;
        birdCloseFile(hDir);
    }
    return NULL;
}


BirdDir_T *birdDirOpenExW(void *hRoot, const wchar_t *pwszPath, const wchar_t *pwszFilter, unsigned fFlags)
{
    HANDLE hDir = birdOpenFileExW((HANDLE)hRoot,
                                  pwszPath,
                                  FILE_READ_DATA | SYNCHRONIZE,
                                  FILE_ATTRIBUTE_NORMAL,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                  FILE_OPEN,
                                  FILE_DIRECTORY_FILE | FILE_OPEN_FOR_BACKUP_INTENT | FILE_SYNCHRONOUS_IO_NONALERT,
                                  OBJ_CASE_INSENSITIVE);
    if (hDir != INVALID_HANDLE_VALUE)
    {
        BirdDir_T *pDir = birdDirOpenFromHandle((void *)hDir, pwszFilter, fFlags | BIRDDIR_F_CLOSE_HANDLE);
        if (pDir)
            return pDir;
        birdCloseFile(hDir);
    }
    return NULL;
}


/**
 * Internal worker for birdStatModTimeOnly.
 */
BirdDir_T *birdDirOpenFromHandle(void *pvHandle, const void *pvReserved, unsigned fFlags)
{
    if (!pvReserved && !(fFlags & BIRDDIR_F_STATIC_ALLOC))
    {
        /*
         * Allocate and initialize the directory enum handle.
         */
        BirdDir_T *pDir = (BirdDir_T *)birdMemAlloc(sizeof(*pDir));
        if (pDir)
        {
            pDir->uMagic        = BIRD_DIR_MAGIC;
            pDir->fFlags        = fFlags;
            pDir->pvHandle      = pvHandle;
            pDir->uDev          = 0;
            pDir->offPos        = 0;
            pDir->fHaveData     = 0;
            pDir->fFirst        = 1;
            pDir->iInfoClass    = fFlags & BIRDDIR_F_EXTRA_INFO ? MyFileIdFullDirectoryInformation : MyFileNamesInformation;
            pDir->offBuf        = 0;
            pDir->cbBuf         = 0;
            pDir->pabBuf        = NULL;
            return pDir;
        }
    }
    else
    {
        assert(!(fFlags & BIRDDIR_F_STATIC_ALLOC));
        assert(pvReserved == NULL);
    }
    birdSetErrnoToInvalidArg();
    return NULL;
}


/**
 * Special API that takes a preallocated BirdDir_T and can be called again
 * without involving birdDirClose.
 *
 *
 */
BirdDir_T *birdDirOpenFromHandleWithReuse(BirdDir_T *pDir, void *pvHandle, const void *pvReserved, unsigned fFlags)
{
    if (!pvReserved)
    {
        /*
         * Allocate and initialize the directory enum handle.
         */
        if (pDir)
        {
            if (pDir->uMagic == BIRD_DIR_MAGIC)
            {
                if (   (pDir->fFlags & BIRDDIR_F_CLOSE_HANDLE)
                    && pDir->pvHandle != INVALID_HANDLE_VALUE)
                    birdCloseFile((HANDLE)pDir->pvHandle);
            }
            else
            {
                pDir->cbBuf     = 0;
                pDir->pabBuf    = NULL;
                pDir->uMagic    = BIRD_DIR_MAGIC;
            }
            pDir->pvHandle      = pvHandle;
            pDir->fFlags        = fFlags;
            pDir->uDev          = 0;
            pDir->offPos        = 0;
            pDir->fHaveData     = 0;
            pDir->fFirst        = 1;
            pDir->iInfoClass    = fFlags & BIRDDIR_F_EXTRA_INFO ? MyFileIdFullDirectoryInformation : MyFileNamesInformation;
            pDir->offBuf        = 0;
            return pDir;
        }
    }
    else
        assert(pvReserved == NULL);
    birdSetErrnoToInvalidArg();
    return NULL;
}


static int birdDirReadMore(BirdDir_T *pDir)
{
    MY_NTSTATUS         rcNt;
    MY_IO_STATUS_BLOCK  Ios;
    int                 fFirst;

    /*
     * Retrieve the volume serial number + creation time and create the
     * device number the first time around.  Also allocate a buffer.
     */
    fFirst = pDir->fFirst;
    if (fFirst)
    {
        union
        {
            MY_FILE_FS_VOLUME_INFORMATION VolInfo;
            unsigned char abBuf[1024];
        } uBuf;

        Ios.Information = 0;
        Ios.u.Status    = -1;
        rcNt = g_pfnNtQueryVolumeInformationFile((HANDLE)pDir->pvHandle, &Ios, &uBuf, sizeof(uBuf), MyFileFsVolumeInformation);
        if (MY_NT_SUCCESS(rcNt))
            rcNt = Ios.u.Status;
        if (MY_NT_SUCCESS(rcNt))
            pDir->uDev = uBuf.VolInfo.VolumeSerialNumber
                       | (uBuf.VolInfo.VolumeCreationTime.QuadPart << 32);
        else
            pDir->uDev = 0;

        if (!pDir->pabBuf)
        {
            /*
             * Allocate a buffer.
             *
             * Better not exceed 64KB or CIFS may throw a fit.  Also, on win10/64
             * here there is a noticable speedup when going one byte below 64KB.
             */
            pDir->cbBuf = 0xffe0;
            pDir->pabBuf = birdMemAlloc(pDir->cbBuf);
            if (!pDir->pabBuf)
                return birdSetErrnoToNoMem();
        }

        pDir->fFirst = 0;
    }

    /*
     * Read another buffer full.
     */
    Ios.Information = 0;
    Ios.u.Status    = -1;

    rcNt = g_pfnNtQueryDirectoryFile((HANDLE)pDir->pvHandle,
                                     NULL,      /* hEvent */
                                     NULL,      /* pfnApcComplete */
                                     NULL,      /* pvApcCompleteCtx */
                                     &Ios,
                                     pDir->pabBuf,
                                     pDir->cbBuf,
                                     (MY_FILE_INFORMATION_CLASS)pDir->iInfoClass,
                                     FALSE,     /* fReturnSingleEntry */
                                     NULL,      /* Filter / restart pos. */
                                     pDir->fFlags & BIRDDIR_F_RESTART_SCAN ? TRUE : FALSE); /* fRestartScan */
    if (!MY_NT_SUCCESS(rcNt))
    {
        int rc;
        if (rcNt == MY_STATUS_NO_MORE_FILES)
            rc = 0;
        else
            rc = birdSetErrnoFromNt(rcNt);
        pDir->fHaveData = 0;
        pDir->offBuf    = pDir->cbBuf;
        return rc;
    }

    pDir->offBuf    = 0;
    pDir->fHaveData = 1;
    pDir->fFlags    &= ~BIRDDIR_F_RESTART_SCAN;

    return 0;
}


static int birdDirCopyNameToEntry(WCHAR const *pwcName, ULONG cbName, BirdDirEntry_T *pEntry)
{
    int cchOut = WideCharToMultiByte(CP_ACP, 0,
                                     pwcName, cbName / sizeof(WCHAR),
                                     pEntry->d_name, sizeof(pEntry->d_name) - 1,
                                     NULL, NULL);
    if (cchOut > 0)
    {
        pEntry->d_name[cchOut] = '\0';
        pEntry->d_namlen = (unsigned __int16)cchOut;
        pEntry->d_reclen = (unsigned __int16)((size_t)&pEntry->d_name[cchOut + 1] - (size_t)pEntry);
        return 0;
    }
    return -1;
}



/**
 * Implements readdir_r().
 *
 * @remarks birdDirReadReentrantW is a copy of this.  Keep them in sync!
 */
int birdDirReadReentrant(BirdDir_T *pDir, BirdDirEntry_T *pEntry, BirdDirEntry_T **ppResult)
{
    int fSkipEntry;

    *ppResult = NULL;

    if (!pDir || pDir->uMagic != BIRD_DIR_MAGIC)
        return birdSetErrnoToBadFileNo();

    do
    {
        ULONG offNext;
        ULONG cbMinCur;

        /*
         * Read more?
         */
        if (!pDir->fHaveData)
        {
            if (birdDirReadMore(pDir) != 0)
                return -1;
            if (!pDir->fHaveData)
                return 0;
        }

        /*
         * Convert the NT data to the unixy output structure.
         */
        fSkipEntry = 0;
        switch (pDir->iInfoClass)
        {
            case MyFileNamesInformation:
            {
                MY_FILE_NAMES_INFORMATION *pInfo = (MY_FILE_NAMES_INFORMATION *)&pDir->pabBuf[pDir->offBuf];
                if (   pDir->offBuf          >= pDir->cbBuf - MIN_SIZEOF_MY_FILE_NAMES_INFORMATION
                    || pInfo->FileNameLength >= pDir->cbBuf
                    || pDir->offBuf + pInfo->FileNameLength + MIN_SIZEOF_MY_FILE_NAMES_INFORMATION > pDir->cbBuf)
                {
                    fSkipEntry = 1;
                    pDir->fHaveData = 0;
                    continue;
                }

                memset(&pEntry->d_stat, 0, sizeof(pEntry->d_stat));
                pEntry->d_stat.st_mode  = S_IFMT;
                pEntry->d_type          = DT_UNKNOWN;
                pEntry->d_reclen        = 0;
                pEntry->d_namlen        = 0;
                if (birdDirCopyNameToEntry(pInfo->FileName, pInfo->FileNameLength, pEntry) != 0)
                    fSkipEntry = 1;

                cbMinCur = MIN_SIZEOF_MY_FILE_NAMES_INFORMATION + pInfo->FileNameLength;
                offNext  = pInfo->NextEntryOffset;
                break;
            }

            case MyFileIdFullDirectoryInformation:
            {
                MY_FILE_ID_FULL_DIR_INFORMATION *pInfo = (MY_FILE_ID_FULL_DIR_INFORMATION *)&pDir->pabBuf[pDir->offBuf];
                if (   pDir->offBuf          >= pDir->cbBuf - MIN_SIZEOF_MY_FILE_ID_FULL_DIR_INFORMATION
                    || pInfo->FileNameLength >= pDir->cbBuf
                    || pDir->offBuf + pInfo->FileNameLength + MIN_SIZEOF_MY_FILE_ID_FULL_DIR_INFORMATION > pDir->cbBuf)
                {
                    fSkipEntry = 1;
                    pDir->fHaveData = 0;
                    continue;
                }

                pEntry->d_type          = DT_UNKNOWN;
                pEntry->d_reclen        = 0;
                pEntry->d_namlen        = 0;
                if (birdDirCopyNameToEntry(pInfo->FileName, pInfo->FileNameLength, pEntry) != 0)
                    fSkipEntry = 1;
                birdStatFillFromFileIdFullDirInfo(&pEntry->d_stat, pInfo, pEntry->d_name);
                pEntry->d_stat.st_dev   = pDir->uDev;
                switch (pEntry->d_stat.st_mode & S_IFMT)
                {
                    case S_IFREG:       pEntry->d_type = DT_REG; break;
                    case S_IFDIR:       pEntry->d_type = DT_DIR; break;
                    case S_IFLNK:       pEntry->d_type = DT_LNK; break;
                    case S_IFIFO:       pEntry->d_type = DT_FIFO; break;
                    case S_IFCHR:       pEntry->d_type = DT_CHR; break;
                    default:
#ifndef NDEBUG
                        __debugbreak();
#endif
                        pEntry->d_type = DT_UNKNOWN;
                        break;
                }

                cbMinCur = MIN_SIZEOF_MY_FILE_ID_FULL_DIR_INFORMATION + pInfo->FileNameLength;
                offNext  = pInfo->NextEntryOffset;
                break;
            }

            default:
                return birdSetErrnoToBadFileNo();
        }

        /*
         * Advance.
         */
        if (   offNext >= cbMinCur
            && offNext < pDir->cbBuf)
            pDir->offBuf += offNext;
        else
        {
            pDir->fHaveData = 0;
            pDir->offBuf    = pDir->cbBuf;
        }
        pDir->offPos++;
    } while (fSkipEntry);


    /*
     * Successful return.
     */
    *ppResult = pEntry;
    return 0;
}


/**
 * Implements readdir().
 */
BirdDirEntry_T *birdDirRead(BirdDir_T *pDir)
{
    BirdDirEntry_T *pRet = NULL;
    birdDirReadReentrant(pDir, &pDir->u.DirEntry, &pRet);
    return pRet;
}


static int birdDirCopyNameToEntryW(WCHAR const *pwcName, ULONG cbName, BirdDirEntryW_T *pEntry)
{
    ULONG cwcName = cbName / sizeof(wchar_t);
    if (cwcName < sizeof(pEntry->d_name))
    {
        memcpy(pEntry->d_name, pwcName, cbName);
        pEntry->d_name[cwcName] = '\0';
        pEntry->d_namlen = (unsigned __int16)cwcName;
        pEntry->d_reclen = (unsigned __int16)((size_t)&pEntry->d_name[cwcName + 1] - (size_t)pEntry);
        return 0;
    }
    return -1;
}


/**
 * Implements readdir_r(), UTF-16 version.
 *
 * @remarks This is a copy of birdDirReadReentrant where only the name handling
 *          and entry type differs.  Remember to keep them in sync!
 */
int birdDirReadReentrantW(BirdDir_T *pDir, BirdDirEntryW_T *pEntry, BirdDirEntryW_T **ppResult)
{
    int fSkipEntry;

    *ppResult = NULL;

    if (!pDir || pDir->uMagic != BIRD_DIR_MAGIC)
        return birdSetErrnoToBadFileNo();

    do
    {
        ULONG offNext;
        ULONG cbMinCur;

        /*
         * Read more?
         */
        if (!pDir->fHaveData)
        {
            if (birdDirReadMore(pDir) != 0)
                return -1;
            if (!pDir->fHaveData)
                return 0;
        }

        /*
         * Convert the NT data to the unixy output structure.
         */
        fSkipEntry = 0;
        switch (pDir->iInfoClass)
        {
            case MyFileNamesInformation:
            {
                MY_FILE_NAMES_INFORMATION *pInfo = (MY_FILE_NAMES_INFORMATION *)&pDir->pabBuf[pDir->offBuf];
                if (   pDir->offBuf          >= pDir->cbBuf - MIN_SIZEOF_MY_FILE_NAMES_INFORMATION
                    || pInfo->FileNameLength >= pDir->cbBuf
                    || pDir->offBuf + pInfo->FileNameLength + MIN_SIZEOF_MY_FILE_NAMES_INFORMATION > pDir->cbBuf)
                {
                    fSkipEntry = 1;
                    pDir->fHaveData = 0;
                    continue;
                }

                memset(&pEntry->d_stat, 0, sizeof(pEntry->d_stat));
                pEntry->d_stat.st_mode  = S_IFMT;
                pEntry->d_type          = DT_UNKNOWN;
                pEntry->d_reclen        = 0;
                pEntry->d_namlen        = 0;
                if (birdDirCopyNameToEntryW(pInfo->FileName, pInfo->FileNameLength, pEntry) != 0)
                    fSkipEntry = 1;

                cbMinCur = MIN_SIZEOF_MY_FILE_NAMES_INFORMATION + pInfo->FileNameLength;
                offNext  = pInfo->NextEntryOffset;
                break;
            }

            case MyFileIdFullDirectoryInformation:
            {
                MY_FILE_ID_FULL_DIR_INFORMATION *pInfo = (MY_FILE_ID_FULL_DIR_INFORMATION *)&pDir->pabBuf[pDir->offBuf];
                if (   pDir->offBuf          >= pDir->cbBuf - MIN_SIZEOF_MY_FILE_ID_FULL_DIR_INFORMATION
                    || pInfo->FileNameLength >= pDir->cbBuf
                    || pDir->offBuf + pInfo->FileNameLength + MIN_SIZEOF_MY_FILE_ID_FULL_DIR_INFORMATION > pDir->cbBuf)
                {
                    fSkipEntry = 1;
                    pDir->fHaveData = 0;
                    continue;
                }

                pEntry->d_type          = DT_UNKNOWN;
                pEntry->d_reclen        = 0;
                pEntry->d_namlen        = 0;
                if (birdDirCopyNameToEntryW(pInfo->FileName, pInfo->FileNameLength, pEntry) != 0)
                    fSkipEntry = 1;
                birdStatFillFromFileIdFullDirInfo(&pEntry->d_stat, pInfo, NULL);
                pEntry->d_stat.st_dev   = pDir->uDev;
                switch (pEntry->d_stat.st_mode & S_IFMT)
                {
                    case S_IFREG:       pEntry->d_type = DT_REG; break;
                    case S_IFDIR:       pEntry->d_type = DT_DIR; break;
                    case S_IFLNK:       pEntry->d_type = DT_LNK; break;
                    case S_IFIFO:       pEntry->d_type = DT_FIFO; break;
                    case S_IFCHR:       pEntry->d_type = DT_CHR; break;
                    default:
#ifndef NDEBUG
                        __debugbreak();
#endif
                        pEntry->d_type = DT_UNKNOWN;
                        break;
                }

                cbMinCur = MIN_SIZEOF_MY_FILE_ID_FULL_DIR_INFORMATION + pInfo->FileNameLength;
                offNext  = pInfo->NextEntryOffset;
                break;
            }

            default:
                return birdSetErrnoToBadFileNo();
        }

        /*
         * Advance.
         */
        if (   offNext >= cbMinCur
            && offNext < pDir->cbBuf)
            pDir->offBuf += offNext;
        else
        {
            pDir->fHaveData = 0;
            pDir->offBuf    = pDir->cbBuf;
        }
        pDir->offPos++;
    } while (fSkipEntry);


    /*
     * Successful return.
     */
    *ppResult = pEntry;
    return 0;
}

/**
 * Implements readdir().
 */
BirdDirEntryW_T *birdDirReadW(BirdDir_T *pDir)
{
    BirdDirEntryW_T *pRet = NULL;
    birdDirReadReentrantW(pDir, &pDir->u.DirEntryW, &pRet);
    return pRet;
}


/**
 * Implements telldir().
 */
long birdDirTell(BirdDir_T *pDir)
{
    if (!pDir || pDir->uMagic != BIRD_DIR_MAGIC)
        return birdSetErrnoToBadFileNo();
    return pDir->offPos;
}


void birdDirSeek(BirdDir_T *pDir, long offDir);


/**
 * Implements closedir().
 */
int birdDirClose(BirdDir_T *pDir)
{
    if (!pDir || pDir->uMagic != BIRD_DIR_MAGIC)
        return birdSetErrnoToBadFileNo();

    pDir->uMagic++;
    if (pDir->fFlags & BIRDDIR_F_CLOSE_HANDLE)
        birdCloseFile((HANDLE)pDir->pvHandle);
    pDir->pvHandle = (void *)INVALID_HANDLE_VALUE;
    birdMemFree(pDir->pabBuf);
    pDir->pabBuf = NULL;
    if (!(pDir->fFlags & BIRDDIR_F_STATIC_ALLOC))
        birdMemFree(pDir);

    return 0;
}
