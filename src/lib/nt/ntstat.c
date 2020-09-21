/* $Id: ntstat.c 3485 2020-09-21 12:25:08Z knut.osmundsen@oracle.com $ */
/** @file
 * MSC + NT stat, lstat and fstat.
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
#include "ntstat.h"


#undef stat

static int birdIsExecutableExtension(const char *pszExt)
{
    switch (pszExt[0])
    {
        default:
            return 0;

        case 'e': /* exe */
            return pszExt[1] == 'x' && pszExt[2] == 'e' && pszExt[3] == '\0';

        case 'b': /* bat */
            return pszExt[1] == 'a' && pszExt[2] == 't' && pszExt[3] == '\0';

        case 'v': /* vbs */
            return pszExt[1] == 'b' && pszExt[2] == 's' && pszExt[3] == '\0';

        case 'c': /* com and cmd */
            return (pszExt[1] == 'o' && pszExt[2] == 'm' && pszExt[3] == '\0')
                || (pszExt[1] == 'm' && pszExt[2] == 'd' && pszExt[3] == '\0');
    }
}


static int birdIsFileExecutable(const char *pszName)
{
    if (pszName)
    {
        const char     *pszExt = NULL;
        char            szExt[8];
        size_t          cchExt;
        unsigned        i;
        char            ch;

        /* Look for a 3 char extension. */
        ch = *pszName++;
        if (!ch)
            return 0;

        while ((ch = *pszName++) != '\0')
            if (ch == '.')
                pszExt = pszName;

        if (!pszExt)
            return 0;
        pszExt++;
        cchExt = pszName - pszExt;
        if (cchExt != 3)
            return 0;

        /* Copy the extension out and lower case it.  Fail immediately on non-alpha chars. */
        for (i = 0; i < cchExt; i++, pszExt++)
        {
            ch = *pszExt;
            if (ch >= 'a' && ch <= 'z')
            { /* likely */ }
            else if (ch >= 'A' && ch <= 'Z')
                ch += 'a' - 'A';
            else
                return 0;
            szExt[i] = ch;
        }
        szExt[i] = '\0';

        return birdIsExecutableExtension(szExt);
    }

    return 0;
}


/**
 * @a pwcName could be the full path.
 */
static int birdIsFileExecutableW(WCHAR const *pwcName, size_t cwcName)
{
    char            szExt[8];
    unsigned        cchExt;
    unsigned        i;
    WCHAR const    *pwc;

    /* Look for a 3 char extension. */
    if (cwcName > 2 && pwcName[cwcName - 2] == '.')
        return 0;
    else if (cwcName > 3 && pwcName[cwcName - 3] == '.')
        return 0;
    else if (cwcName > 4 && pwcName[cwcName - 4] == '.')
        cchExt = 3;
    else
        return 0;

    /* Copy the extension out and lower case it.  Fail immediately on non-alpha chars. */
    pwc = &pwcName[cwcName - cchExt];
    for (i = 0; i < cchExt; i++, pwc++)
    {
        WCHAR wc = *pwc;
        if (wc >= 'a' && wc <= 'z')
        { /* likely */ }
        else if (wc >= 'A' && wc <= 'Z')
            wc += 'a' - 'A';
        else
            return 0;
        szExt[i] = (char)wc;
    }
    szExt[i] = '\0';

    return birdIsExecutableExtension(szExt);
}


static unsigned short birdFileInfoToMode(ULONG fAttribs, ULONG uReparseTag,
                                         const char *pszName, const wchar_t *pwszName, size_t cbNameW,
                                         unsigned __int8 *pfIsDirSymlink, unsigned __int8 *pfIsMountPoint)
{
    unsigned short fMode;

    /* File type. */
    *pfIsDirSymlink = 0;
    *pfIsMountPoint = 0;
    if (!(fAttribs & FILE_ATTRIBUTE_REPARSE_POINT))
    {
        if (fAttribs & FILE_ATTRIBUTE_DIRECTORY)
            fMode = S_IFDIR;
        else
            fMode = S_IFREG;
    }
    else
    {
        switch (uReparseTag)
        {
            case IO_REPARSE_TAG_SYMLINK:
                *pfIsDirSymlink = !!(fAttribs & FILE_ATTRIBUTE_DIRECTORY);
                fMode = S_IFLNK;
                break;

            case IO_REPARSE_TAG_MOUNT_POINT:
                *pfIsMountPoint = 1;
            default:
                if (fAttribs & FILE_ATTRIBUTE_DIRECTORY)
                    fMode = S_IFDIR;
                else
                    fMode = S_IFREG;
                break;
        }
    }

    /* Access mask. */
    fMode |= S_IROTH | S_IRGRP | S_IRUSR;
    if (!(fAttribs & FILE_ATTRIBUTE_READONLY))
        fMode |= S_IWOTH | S_IWGRP | S_IWUSR;
    if (   (fAttribs & FILE_ATTRIBUTE_DIRECTORY)
        || (pwszName
            ? birdIsFileExecutableW(pwszName, cbNameW / sizeof(wchar_t))
            : birdIsFileExecutable(pszName)) )
        fMode |= S_IXOTH | S_IXGRP | S_IXUSR;

    return fMode;
}


/**
 * Fills in a stat structure from an MY_FILE_ID_FULL_DIR_INFORMATION entry.
 *
 * @param   pStat               The stat structure.
 * @param   pBuf                The MY_FILE_ID_FULL_DIR_INFORMATION entry.
 * @remarks Caller sets st_dev.
 */
void birdStatFillFromFileIdFullDirInfo(BirdStat_T *pStat, MY_FILE_ID_FULL_DIR_INFORMATION const *pBuf)
{
    pStat->st_mode          = birdFileInfoToMode(pBuf->FileAttributes, pBuf->EaSize, NULL /*pszPath*/, pBuf->FileName,
                                                 pBuf->FileNameLength, &pStat->st_isdirsymlink, &pStat->st_ismountpoint);
    pStat->st_padding0[0]   = 0;
    pStat->st_padding0[1]   = 0;
    pStat->st_size          = pBuf->EndOfFile.QuadPart;
    birdNtTimeToTimeSpec(pBuf->CreationTime.QuadPart,   &pStat->st_birthtim);
    birdNtTimeToTimeSpec(pBuf->ChangeTime.QuadPart,     &pStat->st_ctim);
    birdNtTimeToTimeSpec(pBuf->LastWriteTime.QuadPart,  &pStat->st_mtim);
    birdNtTimeToTimeSpec(pBuf->LastAccessTime.QuadPart, &pStat->st_atim);
    pStat->st_ino           = pBuf->FileId.QuadPart;
    pStat->st_nlink         = 1;
    pStat->st_rdev          = 0;
    pStat->st_uid           = 0;
    pStat->st_gid           = 0;
    pStat->st_padding1      = 0;
    pStat->st_attribs       = pBuf->FileAttributes;
    pStat->st_blksize       = 65536;
    pStat->st_blocks        = (pBuf->AllocationSize.QuadPart + BIRD_STAT_BLOCK_SIZE - 1)
                            / BIRD_STAT_BLOCK_SIZE;
}


/**
 * Fills in a stat structure from an MY_FILE_ID_BOTH_DIR_INFORMATION entry.
 *
 * @param   pStat               The stat structure.
 * @param   pBuf                The MY_FILE_ID_BOTH_DIR_INFORMATION entry.
 * @remarks Caller sets st_dev.
 */
void birdStatFillFromFileIdBothDirInfo(BirdStat_T *pStat, MY_FILE_ID_BOTH_DIR_INFORMATION const *pBuf)
{
    pStat->st_mode          = birdFileInfoToMode(pBuf->FileAttributes, pBuf->EaSize, NULL /*pszPath*/, pBuf->FileName,
                                                 pBuf->FileNameLength, &pStat->st_isdirsymlink, &pStat->st_ismountpoint);
    pStat->st_padding0[0]   = 0;
    pStat->st_padding0[1]   = 0;
    pStat->st_size          = pBuf->EndOfFile.QuadPart;
    birdNtTimeToTimeSpec(pBuf->CreationTime.QuadPart,   &pStat->st_birthtim);
    birdNtTimeToTimeSpec(pBuf->ChangeTime.QuadPart,     &pStat->st_ctim);
    birdNtTimeToTimeSpec(pBuf->LastWriteTime.QuadPart,  &pStat->st_mtim);
    birdNtTimeToTimeSpec(pBuf->LastAccessTime.QuadPart, &pStat->st_atim);
    pStat->st_ino           = pBuf->FileId.QuadPart;
    pStat->st_nlink         = 1;
    pStat->st_rdev          = 0;
    pStat->st_uid           = 0;
    pStat->st_gid           = 0;
    pStat->st_padding1      = 0;
    pStat->st_attribs       = pBuf->FileAttributes;
    pStat->st_blksize       = 65536;
    pStat->st_blocks        = (pBuf->AllocationSize.QuadPart + BIRD_STAT_BLOCK_SIZE - 1)
                            / BIRD_STAT_BLOCK_SIZE;
}


/**
 * Fills in a stat structure from an MY_FILE_BOTH_DIR_INFORMATION entry.
 *
 * @param   pStat               The stat structure.
 * @param   pBuf                The MY_FILE_BOTH_DIR_INFORMATION entry.
 * @remarks Caller sets st_dev.
 */
void birdStatFillFromFileBothDirInfo(BirdStat_T *pStat, MY_FILE_BOTH_DIR_INFORMATION const *pBuf)
{
    pStat->st_mode          = birdFileInfoToMode(pBuf->FileAttributes, pBuf->EaSize, NULL /*pszPath*/, pBuf->FileName,
                                                 pBuf->FileNameLength, &pStat->st_isdirsymlink, &pStat->st_ismountpoint);
    pStat->st_padding0[0]   = 0;
    pStat->st_padding0[1]   = 0;
    pStat->st_size          = pBuf->EndOfFile.QuadPart;
    birdNtTimeToTimeSpec(pBuf->CreationTime.QuadPart,   &pStat->st_birthtim);
    birdNtTimeToTimeSpec(pBuf->ChangeTime.QuadPart,     &pStat->st_ctim);
    birdNtTimeToTimeSpec(pBuf->LastWriteTime.QuadPart,  &pStat->st_mtim);
    birdNtTimeToTimeSpec(pBuf->LastAccessTime.QuadPart, &pStat->st_atim);
    pStat->st_ino           = 0;
    pStat->st_nlink         = 1;
    pStat->st_rdev          = 0;
    pStat->st_uid           = 0;
    pStat->st_gid           = 0;
    pStat->st_padding1      = 0;
    pStat->st_attribs       = pBuf->FileAttributes;
    pStat->st_blksize       = 65536;
    pStat->st_blocks        = (pBuf->AllocationSize.QuadPart + BIRD_STAT_BLOCK_SIZE - 1)
                            / BIRD_STAT_BLOCK_SIZE;
}


int birdStatHandle2(HANDLE hFile, BirdStat_T *pStat, const char *pszPath, const wchar_t *pwszPath)
{
    int                      rc;
    MY_NTSTATUS              rcNt;
#if 0
    ULONG                    cbAll = sizeof(MY_FILE_ALL_INFORMATION) + 0x10000;
    MY_FILE_ALL_INFORMATION *pAll  = (MY_FILE_ALL_INFORMATION *)birdTmpAlloc(cbAll);
    if (pAll)
    {
        MY_IO_STATUS_BLOCK Ios;
        Ios.Information = 0;
        Ios.u.Status    = -1;
        rcNt = g_pfnNtQueryInformationFile(hFile, &Ios, pAll, cbAll, MyFileAllInformation);
        if (MY_NT_SUCCESS(rcNt))
            rcNt = Ios.u.Status;
        if (MY_NT_SUCCESS(rcNt))
        {
            pStat->st_mode          = birdFileInfoToMode(pAll->BasicInformation.FileAttributes, pszPath,
                                                         pAll->NameInformation.FileNamepAll->NameInformation.FileNameLength,
                                                         hFile, &pStat->st_isdirsymlink, &pStat->st_ismountpoint);
            pStat->st_padding0[0]   = 0;
            pStat->st_padding0[1]   = 0;
            pStat->st_size          = pAll->StandardInformation.EndOfFile.QuadPart;
            birdNtTimeToTimeSpec(pAll->BasicInformation.CreationTime.QuadPart,   &pStat->st_birthtim);
            birdNtTimeToTimeSpec(pAll->BasicInformation.ChangeTime.QuadPart,     &pStat->st_ctim);
            birdNtTimeToTimeSpec(pAll->BasicInformation.LastWriteTime.QuadPart,  &pStat->st_mtim);
            birdNtTimeToTimeSpec(pAll->BasicInformation.LastAccessTime.QuadPart, &pStat->st_atim);
            pStat->st_ino           = pAll->InternalInformation.IndexNumber.QuadPart;
            pStat->st_nlink         = pAll->StandardInformation.NumberOfLinks;
            pStat->st_rdev          = 0;
            pStat->st_uid           = 0;
            pStat->st_gid           = 0;
            pStat->st_padding1      = 0;
            pStat->st_attribs       = pAll->StandardInformation.FileAttributes;
            pStat->st_blksize       = 65536;
            pStat->st_blocks        = (pAll->StandardInformation.AllocationSize.QuadPart + BIRD_STAT_BLOCK_SIZE - 1)
                                    / BIRD_STAT_BLOCK_SIZE;

            /* Get the serial number, reusing the buffer from above. */
            rcNt = g_pfnNtQueryVolumeInformationFile(hFile, &Ios, pAll, cbAll, MyFileFsVolumeInformation);
            if (MY_NT_SUCCESS(rcNt))
                rcNt = Ios.u.Status;
            if (MY_NT_SUCCESS(rcNt))
            {
                MY_FILE_FS_VOLUME_INFORMATION const *pVolInfo = (MY_FILE_FS_VOLUME_INFORMATION const *)pAll;
                pStat->st_dev       = pVolInfo->VolumeSerialNumber
                                    | (pVolInfo->VolumeCreationTime.QuadPart << 32);
                rc = 0;
            }
            else
            {
                pStat->st_dev       = 0;
                rc = birdSetErrnoFromNt(rcNt);
            }
        }
        else
            rc = birdSetErrnoFromNt(rcNt);
    }
    else
        rc = birdSetErrnoToNoMem();
#else
    ULONG                               cbNameInfo = 0;
    MY_FILE_NAME_INFORMATION           *pNameInfo  = NULL;
    MY_FILE_STANDARD_INFORMATION        StdInfo;
    MY_FILE_BASIC_INFORMATION           BasicInfo;
    MY_FILE_INTERNAL_INFORMATION        InternalInfo;
    MY_FILE_ATTRIBUTE_TAG_INFORMATION   TagInfo;
    MY_IO_STATUS_BLOCK                  Ios;

    Ios.Information = 0;
    Ios.u.Status    = -1;
    rcNt = g_pfnNtQueryInformationFile(hFile, &Ios, &StdInfo, sizeof(StdInfo), MyFileStandardInformation);
    if (MY_NT_SUCCESS(rcNt))
        rcNt = Ios.u.Status;

    if (MY_NT_SUCCESS(rcNt))
        rcNt = g_pfnNtQueryInformationFile(hFile, &Ios, &BasicInfo, sizeof(BasicInfo), MyFileBasicInformation);
    if (MY_NT_SUCCESS(rcNt))
        rcNt = Ios.u.Status;

    if (MY_NT_SUCCESS(rcNt))
        rcNt = g_pfnNtQueryInformationFile(hFile, &Ios, &InternalInfo, sizeof(InternalInfo), MyFileInternalInformation);
    if (MY_NT_SUCCESS(rcNt))
        rcNt = Ios.u.Status;

    if (MY_NT_SUCCESS(rcNt))
    {
        if (!(BasicInfo.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
            TagInfo.ReparseTag = 0;
        else
        {
            MY_NTSTATUS rcNt2 = g_pfnNtQueryInformationFile(hFile, &Ios, &TagInfo, sizeof(TagInfo), MyFileAttributeTagInformation);
            if (   !MY_NT_SUCCESS(rcNt2)
                || !MY_NT_SUCCESS(Ios.u.Status))
                TagInfo.ReparseTag = 0;
        }
    }

    if (   MY_NT_SUCCESS(rcNt)
        && !pszPath
        && !pwszPath
        && !(BasicInfo.FileAttributes & FILE_ATTRIBUTE_DIRECTORY))
    {
        cbNameInfo = 0x10020;
        pNameInfo  = (MY_FILE_NAME_INFORMATION *)alloca(cbNameInfo);
        rcNt = g_pfnNtQueryInformationFile(hFile, &Ios, pNameInfo, cbNameInfo, MyFileNameInformation);
        if (MY_NT_SUCCESS(rcNt))
            rcNt = Ios.u.Status;
    }

    if (MY_NT_SUCCESS(rcNt))
    {
        pStat->st_mode          = birdFileInfoToMode(BasicInfo.FileAttributes, TagInfo.ReparseTag, pszPath,
                                                     pNameInfo ? pNameInfo->FileName : pwszPath,
                                                     pNameInfo ? pNameInfo->FileNameLength
                                                     : pwszPath ? wcslen(pwszPath) * sizeof(wchar_t) : 0,
                                                     &pStat->st_isdirsymlink, &pStat->st_ismountpoint);
        pStat->st_padding0[0]   = 0;
        pStat->st_padding0[1]   = 0;
        pStat->st_size          = StdInfo.EndOfFile.QuadPart;
        birdNtTimeToTimeSpec(BasicInfo.CreationTime.QuadPart,   &pStat->st_birthtim);
        birdNtTimeToTimeSpec(BasicInfo.ChangeTime.QuadPart,     &pStat->st_ctim);
        birdNtTimeToTimeSpec(BasicInfo.LastWriteTime.QuadPart,  &pStat->st_mtim);
        birdNtTimeToTimeSpec(BasicInfo.LastAccessTime.QuadPart, &pStat->st_atim);
        pStat->st_ino           = InternalInfo.IndexNumber.QuadPart;
        pStat->st_nlink         = StdInfo.NumberOfLinks;
        pStat->st_rdev          = 0;
        pStat->st_uid           = 0;
        pStat->st_gid           = 0;
        pStat->st_padding1      = 0;
        pStat->st_attribs       = BasicInfo.FileAttributes;
        pStat->st_blksize       = 65536;
        pStat->st_blocks        = (StdInfo.AllocationSize.QuadPart + BIRD_STAT_BLOCK_SIZE - 1)
                                / BIRD_STAT_BLOCK_SIZE;

        /* Get the serial number, reusing the buffer from above. */
        if (!cbNameInfo)
        {
            cbNameInfo = sizeof(MY_FILE_FS_VOLUME_INFORMATION) + 1024;
            pNameInfo  = (MY_FILE_NAME_INFORMATION *)alloca(cbNameInfo);
        }
        rcNt = g_pfnNtQueryVolumeInformationFile(hFile, &Ios, pNameInfo, cbNameInfo, MyFileFsVolumeInformation);
        if (MY_NT_SUCCESS(rcNt))
            rcNt = Ios.u.Status;
        if (MY_NT_SUCCESS(rcNt))
        {
            MY_FILE_FS_VOLUME_INFORMATION const *pVolInfo = (MY_FILE_FS_VOLUME_INFORMATION const *)pNameInfo;
            pStat->st_dev       = pVolInfo->VolumeSerialNumber
                                | (pVolInfo->VolumeCreationTime.QuadPart << 32);
            rc = 0;
        }
        else
        {
            pStat->st_dev       = 0;
            rc = birdSetErrnoFromNt(rcNt);
        }
    }
    else
        rc = birdSetErrnoFromNt(rcNt);

#endif
    return rc;
}


int birdStatHandle(HANDLE hFile, BirdStat_T *pStat, const char *pszPath)
{
    return birdStatHandle2(hFile, pStat, pszPath, NULL);
}


/**
 * Generates a device number from the volume information.
 *
 * @returns Device number.
 * @param   pVolInfo            Volume information.
 */
unsigned __int64 birdVolumeInfoToDeviceNumber(const MY_FILE_FS_VOLUME_INFORMATION *pVolInfo)
{
    return pVolInfo->VolumeSerialNumber
         | (pVolInfo->VolumeCreationTime.QuadPart << 32);
}


/**
 * Quries the volume information and generates a device number from it.
 *
 * @returns NT status code.
 * @param   hFile               The file/dir/whatever to query the volume info
 *                              and device number for.
 * @param   pVolInfo            User provided buffer for volume information.
 * @param   cbVolInfo           The size of the buffer.
 * @param   puDevNo             Where to return the device number.  This is set
 *                              to zero on failure.
 */
MY_NTSTATUS birdQueryVolumeDeviceNumber(HANDLE hFile, MY_FILE_FS_VOLUME_INFORMATION *pVolInfo, size_t cbVolInfo,
                                        unsigned __int64 *puDevNo)
{
    MY_IO_STATUS_BLOCK  Ios;
    MY_NTSTATUS         rcNt;

    Ios.u.Status    = -1;
    Ios.Information = -1;

    pVolInfo->VolumeSerialNumber = 0;
    pVolInfo->VolumeCreationTime.QuadPart = 0;

    rcNt = g_pfnNtQueryVolumeInformationFile(hFile, &Ios, pVolInfo, (LONG)cbVolInfo, MyFileFsVolumeInformation);
    if (MY_NT_SUCCESS(rcNt))
    {
        *puDevNo = birdVolumeInfoToDeviceNumber(pVolInfo);
        return Ios.u.Status;
    }
    *puDevNo = 0;
    return rcNt;
}


static int birdStatInternal(HANDLE hRoot, const char *pszPath, BirdStat_T *pStat, int fFollow)
{
    int rc;
    HANDLE hFile = birdOpenFileEx(hRoot, pszPath,
                                  FILE_READ_ATTRIBUTES,
                                  FILE_ATTRIBUTE_NORMAL,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                  FILE_OPEN,
                                  FILE_OPEN_FOR_BACKUP_INTENT | (fFollow ? 0 : FILE_OPEN_REPARSE_POINT),
                                  OBJ_CASE_INSENSITIVE);
    if (hFile != INVALID_HANDLE_VALUE)
    {
        rc = birdStatHandle2(hFile, pStat, pszPath, NULL);
        birdCloseFile(hFile);

        if (rc || !pStat->st_ismountpoint)
        { /* very likely */ }
        else
        {
            /*
             * If we hit a mount point (NTFS volume mounted under an empty NTFS directory),
             * we should return information about what's mounted there rather than the
             * directory it is mounted at as this is what UNIX does.
             */
            hFile = birdOpenFileEx(hRoot, pszPath,
                                   FILE_READ_ATTRIBUTES,
                                   FILE_ATTRIBUTE_NORMAL,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                   FILE_OPEN,
                                   FILE_OPEN_FOR_BACKUP_INTENT,
                                   OBJ_CASE_INSENSITIVE);
            if (hFile != INVALID_HANDLE_VALUE)
            {
                rc = birdStatHandle2(hFile, pStat, pszPath, NULL);
                pStat->st_ismountpoint = 2;
                birdCloseFile(hFile);
            }
        }

#if 0
        {
            static char s_szPrev[256];
            size_t cchPath = strlen(pszPath);
            if (memcmp(s_szPrev, pszPath, cchPath >= 255 ? 255 : cchPath + 1) == 0)
                fprintf(stderr, "stat: %s -> rc/errno=%d/%u\n", pszPath, rc, errno);
            else
                memcpy(s_szPrev, pszPath, cchPath + 1);
        }
#endif
        //fprintf(stderr, "stat: %s -> rc/errno=%d/%u\n", pszPath, rc, errno);
    }
    else
    {
        //fprintf(stderr, "stat: %s -> %u\n", pszPath, GetLastError());

        /*
         * On things like pagefile.sys we may get sharing violation.  We fall
         * back on directory enumeration for dealing with that.
         */
        if (   errno == ETXTBSY
            && strchr(pszPath, '*') == NULL /* Serious paranoia... */
            && strchr(pszPath, '?') == NULL)
        {
            MY_UNICODE_STRING NameUniStr;
            hFile = birdOpenParentDir(hRoot, pszPath,
                                      FILE_READ_DATA | SYNCHRONIZE,
                                      FILE_ATTRIBUTE_NORMAL,
                                      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                      FILE_OPEN,
                                      FILE_DIRECTORY_FILE | FILE_OPEN_FOR_BACKUP_INTENT | FILE_SYNCHRONOUS_IO_NONALERT,
                                      OBJ_CASE_INSENSITIVE,
                                      &NameUniStr);
            if (hFile != INVALID_HANDLE_VALUE)
            {
                MY_FILE_ID_FULL_DIR_INFORMATION *pBuf;
                ULONG               cbBuf = sizeof(*pBuf) + NameUniStr.MaximumLength + 1024;
                MY_IO_STATUS_BLOCK  Ios;
                MY_NTSTATUS         rcNt;

                pBuf = (MY_FILE_ID_FULL_DIR_INFORMATION *)alloca(cbBuf);
                Ios.u.Status    = -1;
                Ios.Information = -1;
                rcNt = g_pfnNtQueryDirectoryFile(hFile, NULL, NULL, NULL, &Ios, pBuf, cbBuf,
                                                 MyFileIdFullDirectoryInformation, FALSE, &NameUniStr, TRUE);
                if (MY_NT_SUCCESS(rcNt))
                    rcNt = Ios.u.Status;
                if (MY_NT_SUCCESS(rcNt))
                {
                    /*
                     * Convert the data.
                     */
                    birdStatFillFromFileIdFullDirInfo(pStat, pBuf);

                    /* Get the serial number, reusing the buffer from above. */
                    rcNt = birdQueryVolumeDeviceNumber(hFile, (MY_FILE_FS_VOLUME_INFORMATION *)pBuf, cbBuf, &pStat->st_dev);
                    if (MY_NT_SUCCESS(rcNt))
                        rc = 0;
                    else
                        rc = birdSetErrnoFromNt(rcNt);
                }

                birdFreeNtPath(&NameUniStr);
                birdCloseFile(hFile);

                if (MY_NT_SUCCESS(rcNt))
                    return 0;
                birdSetErrnoFromNt(rcNt);
            }
        }
        rc = -1;
    }

    return rc;
}


static int birdStatInternalW(HANDLE hRoot, const wchar_t *pwszPath, BirdStat_T *pStat, int fFollow)
{
    int rc;
    HANDLE hFile = birdOpenFileExW(hRoot, pwszPath,
                                   FILE_READ_ATTRIBUTES,
                                   FILE_ATTRIBUTE_NORMAL,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                   FILE_OPEN,
                                   FILE_OPEN_FOR_BACKUP_INTENT | (fFollow ? 0 : FILE_OPEN_REPARSE_POINT),
                                   OBJ_CASE_INSENSITIVE);
    if (hFile != INVALID_HANDLE_VALUE)
    {
        rc = birdStatHandle2(hFile, pStat, NULL, pwszPath);
        birdCloseFile(hFile);

        if (rc || !pStat->st_ismountpoint)
        { /* very likely */ }
        else
        {
            /*
             * If we hit a mount point (NTFS volume mounted under an empty NTFS directory),
             * we should return information about what's mounted there rather than the
             * directory it is mounted at as this is what UNIX does.
             */
            hFile = birdOpenFileExW(hRoot, pwszPath,
                                    FILE_READ_ATTRIBUTES,
                                    FILE_ATTRIBUTE_NORMAL,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                    FILE_OPEN,
                                    FILE_OPEN_FOR_BACKUP_INTENT,
                                    OBJ_CASE_INSENSITIVE);
            if (hFile != INVALID_HANDLE_VALUE)
            {
                rc = birdStatHandle2(hFile, pStat, NULL, pwszPath);
                pStat->st_ismountpoint = 2;
                birdCloseFile(hFile);
            }
        }
    }
    else
    {
        /*
         * On things like pagefile.sys we may get sharing violation.  We fall
         * back on directory enumeration for dealing with that.
         */
        if (   errno == ETXTBSY
            && wcschr(pwszPath, '*') == NULL /* Serious paranoia... */
            && wcschr(pwszPath, '?') == NULL)
        {
            MY_UNICODE_STRING NameUniStr;
            hFile = birdOpenParentDirW(hRoot, pwszPath,
                                       FILE_READ_DATA | SYNCHRONIZE,
                                       FILE_ATTRIBUTE_NORMAL,
                                       FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                       FILE_OPEN,
                                       FILE_DIRECTORY_FILE | FILE_OPEN_FOR_BACKUP_INTENT | FILE_SYNCHRONOUS_IO_NONALERT,
                                       OBJ_CASE_INSENSITIVE,
                                       &NameUniStr);
            if (hFile != INVALID_HANDLE_VALUE)
            {
                MY_FILE_ID_FULL_DIR_INFORMATION *pBuf;
                ULONG               cbBuf = sizeof(*pBuf) + NameUniStr.MaximumLength + 1024;
                MY_IO_STATUS_BLOCK  Ios;
                MY_NTSTATUS         rcNt;

                pBuf = (MY_FILE_ID_FULL_DIR_INFORMATION *)alloca(cbBuf);
                Ios.u.Status    = -1;
                Ios.Information = -1;
                rcNt = g_pfnNtQueryDirectoryFile(hFile, NULL, NULL, NULL, &Ios, pBuf, cbBuf,
                                                 MyFileIdFullDirectoryInformation, FALSE, &NameUniStr, TRUE);
                if (MY_NT_SUCCESS(rcNt))
                    rcNt = Ios.u.Status;
                if (MY_NT_SUCCESS(rcNt))
                {
                    /*
                     * Convert the data.
                     */
                    birdStatFillFromFileIdFullDirInfo(pStat, pBuf);

                    /* Get the serial number, reusing the buffer from above. */
                    rcNt = birdQueryVolumeDeviceNumber(hFile, (MY_FILE_FS_VOLUME_INFORMATION *)pBuf, cbBuf, &pStat->st_dev);
                    if (MY_NT_SUCCESS(rcNt))
                        rc = 0;
                    else
                        rc = birdSetErrnoFromNt(rcNt);
                }

                birdFreeNtPath(&NameUniStr);
                birdCloseFile(hFile);

                if (MY_NT_SUCCESS(rcNt))
                    return 0;
                birdSetErrnoFromNt(rcNt);
            }
        }
        rc = -1;
    }

    return rc;
}


/**
 * Implements UNIX fstat().
 */
int birdStatOnFd(int fd, BirdStat_T *pStat)
{
    int     rc;
    HANDLE  hFile = (HANDLE)_get_osfhandle(fd);
    if (hFile != INVALID_HANDLE_VALUE)
    {
        DWORD fFileType;

        birdResolveImports();

        SetLastError(NO_ERROR);
        fFileType = GetFileType(hFile) & ~FILE_TYPE_REMOTE;
        switch (fFileType)
        {
            case FILE_TYPE_DISK:
                rc = birdStatHandle2(hFile, pStat, NULL, NULL);
                break;

            case FILE_TYPE_CHAR:
            case FILE_TYPE_PIPE:
                if (fFileType == FILE_TYPE_PIPE)
                    pStat->st_mode          = S_IFIFO | 0666;
                else
                    pStat->st_mode          = S_IFCHR | 0666;
                pStat->st_padding0[0]       = 0;
                pStat->st_padding0[1]       = 0;
                pStat->st_size              = 0;
                pStat->st_atim.tv_sec       = 0;
                pStat->st_atim.tv_nsec      = 0;
                pStat->st_mtim.tv_sec       = 0;
                pStat->st_mtim.tv_nsec      = 0;
                pStat->st_ctim.tv_sec       = 0;
                pStat->st_ctim.tv_nsec      = 0;
                pStat->st_birthtim.tv_sec   = 0;
                pStat->st_birthtim.tv_nsec  = 0;
                pStat->st_ino               = 0;
                pStat->st_dev               = 0;
                pStat->st_rdev              = 0;
                pStat->st_uid               = 0;
                pStat->st_gid               = 0;
                pStat->st_padding1          = 0;
                pStat->st_attribs           = fFileType == FILE_TYPE_PIPE ? FILE_ATTRIBUTE_NORMAL : FILE_ATTRIBUTE_DEVICE;
                pStat->st_blksize           = 512;
                pStat->st_blocks            = 0;
                if (fFileType == FILE_TYPE_PIPE)
                {
                    DWORD cbAvail;
                    if (PeekNamedPipe(hFile, NULL, 0, NULL, &cbAvail, NULL))
                        pStat->st_size = cbAvail;
                }
                rc = 0;
                break;

            case FILE_TYPE_UNKNOWN:
            default:
                if (GetLastError() == NO_ERROR)
                    rc = birdSetErrnoToBadFileNo();
                else
                    rc = birdSetErrnoFromWin32(GetLastError());
                break;
        }
    }
    else
        rc = -1;
    return rc;
}


/**
 * Special case that only gets the file size and nothing else.
 */
int birdStatOnFdJustSize(int fd, __int64 *pcbFile)
{
    int     rc;
    HANDLE  hFile = (HANDLE)_get_osfhandle(fd);
    if (hFile != INVALID_HANDLE_VALUE)
    {
        LARGE_INTEGER cbLocal;
        if (GetFileSizeEx(hFile, &cbLocal))
        {
            *pcbFile = cbLocal.QuadPart;
            rc = 0;
        }
        else
        {
            BirdStat_T Stat;
            rc = birdStatOnFd(fd, &Stat);
            if (rc == 0)
                *pcbFile = Stat.st_size;
        }
    }
    else
        rc = -1;
    return rc;
}


/**
 * Implements UNIX stat().
 */
int birdStatFollowLink(const char *pszPath, BirdStat_T *pStat)
{
    return birdStatInternal(NULL, pszPath, pStat, 1 /*fFollow*/);
}


/**
 * Implements UNIX stat().
 */
int birdStatFollowLinkW(const wchar_t *pwszPath, BirdStat_T *pStat)
{
    return birdStatInternalW(NULL, pwszPath, pStat, 1 /*fFollow*/);
}


/**
 * Implements UNIX lstat().
 */
int birdStatOnLink(const char *pszPath, BirdStat_T *pStat)
{
    return birdStatInternal(NULL, pszPath, pStat, 0 /*fFollow*/);
}


/**
 * Implements UNIX lstat().
 */
int birdStatOnLinkW(const wchar_t *pwszPath, BirdStat_T *pStat)
{
    return birdStatInternalW(NULL, pwszPath, pStat, 0 /*fFollow*/);
}


/**
 * Implements an API like UNIX fstatat().
 *
 * @returns 0 on success, -1 and errno on failure.
 * @param   hRoot               NT handle pwszPath is relative to.
 * @param   pszPath             The path.
 * @param   pStat               Where to return stats.
 * @param   fFollowLink         Whether to follow links.
 */
int birdStatAt(HANDLE hRoot, const char *pszPath, BirdStat_T *pStat, int fFollowLink)
{
    return birdStatInternal(hRoot, pszPath, pStat, fFollowLink != 0);
}


/**
 * Implements an API like UNIX fstatat().
 *
 * @returns 0 on success, -1 and errno on failure.
 * @param   hRoot               NT handle pwszPath is relative to.
 * @param   pwszPath            The path.
 * @param   pStat               Where to return stats.
 * @param   fFollowLink         Whether to follow links.
 */
int birdStatAtW(HANDLE hRoot, const wchar_t *pwszPath, BirdStat_T *pStat, int fFollowLink)
{
    return birdStatInternalW(hRoot, pwszPath, pStat, fFollowLink != 0);
}


/**
 * Internal worker for birdStatModTimeOnly.
 */
static int birdStatOnlyInternal(const char *pszPath, int fFollowLink, MY_FILE_BASIC_INFORMATION *pBasicInfo)
{
    int rc;
    HANDLE hFile = birdOpenFile(pszPath,
                                FILE_READ_ATTRIBUTES,
                                FILE_ATTRIBUTE_NORMAL,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                FILE_OPEN,
                                FILE_OPEN_FOR_BACKUP_INTENT | (fFollowLink ? 0 : FILE_OPEN_REPARSE_POINT),
                                OBJ_CASE_INSENSITIVE);
    if (hFile != INVALID_HANDLE_VALUE)
    {
        MY_NTSTATUS         rcNt = 0;
        MY_IO_STATUS_BLOCK  Ios;
        Ios.Information = 0;
        Ios.u.Status    = -1;

        if (pBasicInfo)
        {
            rcNt = g_pfnNtQueryInformationFile(hFile, &Ios, pBasicInfo, sizeof(*pBasicInfo), MyFileBasicInformation);
            if (MY_NT_SUCCESS(rcNt))
                rcNt = Ios.u.Status;
        }
        birdCloseFile(hFile);

        if (MY_NT_SUCCESS(rcNt))
            rc = 0;
        else
        {
            birdSetErrnoFromNt(rcNt);
            rc = -1;
        }
    }
    else
    {
        //fprintf(stderr, "stat: %s -> %u\n", pszPath, GetLastError());

        /* On things like pagefile.sys we may get sharing violation. */
        if (GetLastError() == ERROR_SHARING_VIOLATION)
        {
            /** @todo Fall back on the parent directory enum if we run into a sharing
             *        violation. */
        }
        rc = -1;
    }
    return rc;
}


/**
 * Special function for getting the modification time.
 */
int birdStatModTimeOnly(const char *pszPath, BirdTimeSpec_T *pTimeSpec, int fFollowLink)
{
    /*
     * Convert the path and call NtQueryFullAttributesFile.
     *
     * Note! NtQueryAttributesFile cannot be used as it only returns attributes.
     */
    MY_UNICODE_STRING  NtPath;

    birdResolveImports();
    if (birdDosToNtPath(pszPath, &NtPath) == 0)
    {
        MY_OBJECT_ATTRIBUTES                ObjAttr;
        MY_FILE_NETWORK_OPEN_INFORMATION    Info;
        MY_NTSTATUS                         rcNt;

        memset(&Info, 0xfe, sizeof(Info));

        MyInitializeObjectAttributes(&ObjAttr, &NtPath, OBJ_CASE_INSENSITIVE, NULL /*hRoot*/, NULL /*pSecAttr*/);
        rcNt = g_pfnNtQueryFullAttributesFile(&ObjAttr, &Info);

        birdFreeNtPath(&NtPath);
        if (MY_NT_SUCCESS(rcNt))
        {
            birdNtTimeToTimeSpec(Info.LastWriteTime.QuadPart, pTimeSpec);

            /* Do the trailing slash check. */
            if (   (Info.FileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                || !birdIsPathDirSpec(pszPath))
            {
                MY_FILE_BASIC_INFORMATION BasicInfo;
                if (   !(Info.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
                    || !fFollowLink)
                    return 0;

                /* Fallback on birdStatOnlyInternal to follow the reparse point.  */
                if (!birdStatOnlyInternal(pszPath, fFollowLink, &BasicInfo))
                {
                    birdNtTimeToTimeSpec(BasicInfo.LastWriteTime.QuadPart, pTimeSpec);
                    return 0;
                }
            }
            else
                errno = ENOTDIR;
        }
        else
            birdSetErrnoFromNt(rcNt);
    }
    return -1;
}

/**
 * Special function for getting the file mode.
 */
int birdStatModeOnly(const char *pszPath, unsigned __int16 *pMode, int fFollowLink)
{
    /*
     * Convert the path and call NtQueryFullAttributesFile.
     */
    MY_UNICODE_STRING  NtPath;

    birdResolveImports();
    if (birdDosToNtPath(pszPath, &NtPath) == 0)
    {
        MY_OBJECT_ATTRIBUTES                ObjAttr;
        MY_FILE_BASIC_INFORMATION           Info;
        MY_NTSTATUS                         rcNt;

        memset(&Info, 0xfe, sizeof(Info));

        MyInitializeObjectAttributes(&ObjAttr, &NtPath, OBJ_CASE_INSENSITIVE, NULL /*hRoot*/, NULL /*pSecAttr*/);
        rcNt = g_pfnNtQueryAttributesFile(&ObjAttr, &Info);

        if (MY_NT_SUCCESS(rcNt))
        {
            unsigned __int8 isdirsymlink = 0;
            unsigned __int8 ismountpoint = 0;
            *pMode = birdFileInfoToMode(Info.FileAttributes, 0, pszPath, NtPath.Buffer, NtPath.Length,
                                        &isdirsymlink, &ismountpoint);

            /* Do the trailing slash check. */
            if (   (Info.FileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                || !birdIsPathDirSpec(pszPath))
            {
                if (   !(Info.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
                    || !fFollowLink)
                {
                    birdFreeNtPath(&NtPath);
                    return 0;
                }

                /* Fallback on birdStatOnlyInternal to follow the reparse point.  */
                if (!birdStatOnlyInternal(pszPath, fFollowLink, &Info))
                {
                    *pMode = birdFileInfoToMode(Info.FileAttributes, 0, pszPath, NtPath.Buffer, NtPath.Length,
                                                &isdirsymlink, &ismountpoint);
                    birdFreeNtPath(&NtPath);
                    return 0;
                }
            }
            else
                errno = ENOTDIR;
        }
        else
            birdSetErrnoFromNt(rcNt);
        birdFreeNtPath(&NtPath);
    }
    return -1;
}


