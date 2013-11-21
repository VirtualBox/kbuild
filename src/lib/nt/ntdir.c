/* $Id: ntdir.c 2708 2013-11-21 10:26:40Z knut.osmundsen@oracle.com $ */
/** @file
 * MSC + NT opendir, readdir, telldir, seekdir, and closedir.
 */

/*
 * Copyright (c) 2005-2013 knut st. osmundsen <bird-kBuild-spamx@anduin.net>
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

#include "ntstuff.h"
#include "nthlp.h"
#include "ntdir.h"


/**
 * Internal worker for birdStatModTimeOnly.
 */
static BirdDir_T *birdDirOpenInternal(const char *pszPath, const char *pszFilter, int fMinimalInfo)
{
    HANDLE hFile = birdOpenFile(pszPath,
                                FILE_READ_DATA | SYNCHRONIZE,
                                FILE_ATTRIBUTE_NORMAL,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                FILE_OPEN,
                                FILE_DIRECTORY_FILE | FILE_OPEN_FOR_BACKUP_INTENT | FILE_SYNCHRONOUS_IO_NONALERT,
                                OBJ_CASE_INSENSITIVE);
    if (hFile != INVALID_HANDLE_VALUE)
    {
        /*
         * Allocate a handle.
         */
        BirdDir_T *pDir = (BirdDir_T *)birdMemAlloc(sizeof(*pDir));
        if (pDir)
        {
            pDir->uMagic     = BIRD_DIR_MAGIC;
            pDir->pvHandle   = (void *)hFile;
            pDir->uDev       = 0;
            pDir->offPos     = 0;
            pDir->fHaveData  = 0;
            pDir->fFirst     = 1;
            pDir->iInfoClass = fMinimalInfo ? MyFileNamesInformation : MyFileIdFullDirectoryInformation;
            pDir->offBuf     = 0;
            pDir->cbBuf      = 0;
            pDir->pabBuf     = NULL;
            return pDir;
        }

        birdCloseFile(hFile);
        birdSetErrnoToNoMem();
    }

    return NULL;
}


/**
 * Implements opendir.
 */
BirdDir_T *birdDirOpen(const char *pszPath)
{
    return birdDirOpenInternal(pszPath, NULL, 1 /*fMinimalInfo*/);
}


/**
 * Alternative opendir, with extra stat() info returned by readdir().
 */
BirdDir_T *birdDirOpenExtraInfo(const char *pszPath)
{
    return birdDirOpenInternal(pszPath, NULL, 0 /*fMinimalInfo*/);
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

        /*
         * Allocate a buffer.
         */
        pDir->cbBuf = 0x20000;
        pDir->pabBuf = birdMemAlloc(pDir->cbBuf);
        if (!pDir->pabBuf)
            return birdSetErrnoToNoMem();

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
                                     FALSE);    /* fRestartScan */
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
    birdDirReadReentrant(pDir, &pDir->DirEntry, &pRet);
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
int             birdDirClose(BirdDir_T *pDir)
{
    if (!pDir || pDir->uMagic != BIRD_DIR_MAGIC)
        return birdSetErrnoToBadFileNo();

    pDir->uMagic++;
    birdCloseFile((HANDLE)pDir->pvHandle);
    pDir->pvHandle = (void *)INVALID_HANDLE_VALUE;
    birdMemFree(pDir->pabBuf);
    pDir->pabBuf = NULL;
    birdMemFree(pDir);

    return 0;
}
