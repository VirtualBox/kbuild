/* $Id: nt_fullpath.c 2413 2010-09-11 17:43:04Z knut.osmundsen@oracle.com $ */
/** @file
 * fixcase - fixes the case of paths, windows specific.
 */

/*
 * Copyright (c) 2004-2010 knut st. osmundsen <bird-kBuild-spamx@anduin.net>
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
#include <Windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <direct.h>

/*
 * Corrects the case of a path.
 * Expects a fullpath!
 * Added by bird for the $(abspath ) function and w32ify
 */
static void w32_fixcase(char *pszPath)
{
    static char     s_szLast[260];
    size_t          cchLast;

#ifndef NDEBUG
# define my_assert(expr) \
    do { \
        if (!(expr)) { \
            printf("my_assert: %s, file %s, line %d\npszPath=%s\npsz=%s\n", \
                   #expr, __FILE__, __LINE__, pszPath, psz); \
            __debugbreak(); \
            exit(1); \
        } \
    } while (0)
#else
# define my_assert(expr) do {} while (0)
#endif

    char *psz = pszPath;
    if (*psz == '/' || *psz == '\\')
    {
        if (psz[1] == '/' || psz[1] == '\\')
        {
            /* UNC */
            my_assert(psz[1] == '/' || psz[1] == '\\');
            my_assert(psz[2] != '/' && psz[2] != '\\');

            /* skip server name */
            psz += 2;
            while (*psz != '\\' && *psz != '/')
            {
                if (!*psz)
                    return;
                *psz++ = toupper(*psz);
            }

            /* skip the share name */
            psz++;
            my_assert(*psz != '/' && *psz != '\\');
            while (*psz != '\\' && *psz != '/')
            {
                if (!*psz)
                    return;
                *psz++ = toupper(*psz);
            }
            my_assert(*psz == '/' || *psz == '\\');
            psz++;
        }
        else
        {
            /* Unix spec */
            psz++;
        }
    }
    else
    {
        /* Drive letter */
        my_assert(psz[1] == ':');
        *psz = toupper(*psz);
        my_assert(psz[0] >= 'A' && psz[0] <= 'Z');
        my_assert(psz[2] == '/' || psz[2] == '\\');
        psz += 3;
    }

    /*
     * Try make use of the result from the previous call.
     * This is ignorant to slashes and similar, but may help even so.
     */
    if (    s_szLast[0] == pszPath[0]
        &&  (psz - pszPath == 1 || s_szLast[1] == pszPath[1])
        &&  (psz - pszPath <= 2 || s_szLast[2] == pszPath[2])
       )
    {
        char *pszLast = &s_szLast[psz - pszPath];
        char *pszCur = psz;
        char *pszSrc0 = pszLast;
        char *pszDst0 = pszCur;
        for (;;)
        {
            const char ch1 = *pszCur;
            const char ch2 = *pszLast;
            if (    ch1 != ch2
                &&  (ch1 != '\\' || ch2 != '/')
                &&  (ch1 != '/'  || ch2 != '\\')
                &&  tolower(ch1) != tolower(ch2)
                &&  toupper(ch1) != toupper(ch2))
                break;
            if (ch1 == '/' || ch1 == '\\')
            {
				psz = pszCur + 1;
                *pszLast = ch1; /* preserve the slashes */
            }
            else if (ch1 == '\0')
            {
                psz = pszCur;
                break;
            }
            pszCur++;
            pszLast++;
        }
        if (psz != pszDst0)
            memcpy(pszDst0, pszSrc0, psz - pszDst0);
    }

    /*
     * Pointing to the first char after the unc or drive specifier,
     * or in case of a cache hit, the first non-matching char (following a slash of course).
     */
    while (*psz)
    {
        WIN32_FIND_DATA FindFileData;
        HANDLE hDir;
        char chSaved0;
        char chSaved1;
        char *pszEnd;
        int iLongNameDiff;
        size_t cch;


        /* find the end of the component. */
        pszEnd = psz;
        while (*pszEnd && *pszEnd != '/' && *pszEnd != '\\')
            pszEnd++;
        cch = pszEnd - psz;

        /* replace the end with "?\0" */
        chSaved0 = pszEnd[0];
        chSaved1 = pszEnd[1];
        pszEnd[0] = '?';
        pszEnd[1] = '\0';

        /* find the right filename. */
        hDir = FindFirstFile(pszPath, &FindFileData);
        pszEnd[1] = chSaved1;
        if (!hDir)
        {
            cchLast = psz - pszPath;
            memcpy(s_szLast, pszPath, cchLast + 1);
            s_szLast[cchLast + 1] = '\0';
            pszEnd[0] = chSaved0;
            return;
        }
        pszEnd[0] = '\0';
        while (   (iLongNameDiff = stricmp(FindFileData.cFileName, psz))
               && stricmp(FindFileData.cAlternateFileName, psz))
        {
            if (!FindNextFile(hDir, &FindFileData))
            {
                cchLast = psz - pszPath;
                memcpy(s_szLast, pszPath, cchLast + 1);
                s_szLast[cchLast + 1] = '\0';
                pszEnd[0] = chSaved0;
                return;
            }
        }
        pszEnd[0] = chSaved0;
        if (    iLongNameDiff                           /* matched the short name */
            ||  !FindFileData.cAlternateFileName[0]     /* no short name */
            || !memchr(psz, ' ', cch))                  /* no spaces in the matching name */
            memcpy(psz, !iLongNameDiff ? FindFileData.cFileName : FindFileData.cAlternateFileName, cch);
        else
        {
            /* replace spacy name with the short name. */
            const size_t cchAlt = strlen(FindFileData.cAlternateFileName);
            const size_t cchDelta = cch - cchAlt;
            my_assert(cchAlt > 0);
            if (!cchDelta)
                memcpy(psz, FindFileData.cAlternateFileName, cch);
            else
            {
                size_t cbLeft = strlen(pszEnd) + 1;
                if ((psz - pszPath) + cbLeft + cchAlt <= _MAX_PATH)
                {
                    memmove(psz + cchAlt, pszEnd, cbLeft);
                    pszEnd -= cchDelta;
                    memcpy(psz, FindFileData.cAlternateFileName, cchAlt);
                }
                else
                    fprintf(stderr, "kBuild: case & space fixed filename is growing too long (%d bytes)! '%s'\n",
                            (psz - pszPath) + cbLeft + cchAlt, pszPath);
            }
        }
        my_assert(pszEnd[0] == chSaved0);
        FindClose(hDir);

        /* advance to the next component */
        if (!chSaved0)
        {
            psz = pszEnd;
            break;
        }
        psz = pszEnd + 1;
        my_assert(*psz != '/' && *psz != '\\');
    }

    /* *psz == '\0', the end. */
    cchLast = psz - pszPath;
    memcpy(s_szLast, pszPath, cchLast + 1);
#undef my_assert
}

#define MY_FileNameInformation 9
typedef struct _MY_FILE_NAME_INFORMATION
{
    ULONG FileNameLength;
    WCHAR FileName[1];
} MY_FILE_NAME_INFORMATION, *PMY_FILE_NAME_INFORMATION;

#define MY_FileInternalInformation 6
typedef struct _MY_FILE_INTERNAL_INFORMATION {
    LARGE_INTEGER IndexNumber;
} MY_FILE_INTERNAL_INFORMATION, *PMY_FILE_INTERNAL_INFORMATION;

#define MY_FileFsVolumeInformation 1
typedef struct _MY_FILE_FS_VOLUME_INFORMATION
{
    LARGE_INTEGER VolumeCreationTime;
    ULONG VolumeSerialNumber;
    ULONG VolumeLabelLength;
    BOOLEAN SupportsObjects;
    WCHAR VolumeLabel[/*1*/128];
} MY_FILE_FS_VOLUME_INFORMATION, *PMY_FILE_FS_VOLUME_INFORMATION;

#define MY_FileFsAttributeInformation 5
typedef struct _MY_FILE_FS_ATTRIBUTE_INFORMATION
{
    ULONG FileSystemAttributes;
    LONG MaximumComponentNameLength;
    ULONG FileSystemNameLength;
    WCHAR FileSystemName[/*1*/64];
} MY_FILE_FS_ATTRIBUTE_INFORMATION, *PMY_FILE_FS_ATTRIBUTE_INFORMATION;

#define MY_FileFsDeviceInformation 4
typedef struct MY_FILE_FS_DEVICE_INFORMATION
{
    ULONG DeviceType;
    ULONG Characteristics;
} MY_FILE_FS_DEVICE_INFORMATION, *PMY_FILE_FS_DEVICE_INFORMATION;
#define MY_FILE_DEVICE_DISK              7
#define MY_FILE_DEVICE_DISK_FILE_SYSTEM  8
#define MY_FILE_DEVICE_FILE_SYSTEM       9
#define MY_FILE_DEVICE_VIRTUAL_DISK     36


typedef struct _IO_STATUS_BLOCK
{
    union
    {
        LONG Status;
        PVOID Pointer;
    };
    ULONG_PTR Information;
} MY_IO_STATUS_BLOCK, *PMY_IO_STATUS_BLOCK;

static BOOL                             g_fInitialized = FALSE;
static int                              g_afNtfsDrives['Z' - 'A' + 1];
static MY_FILE_FS_VOLUME_INFORMATION    g_aVolumeInfo['Z' - 'A' + 1];

static LONG (NTAPI *g_pfnNtQueryInformationFile)(HANDLE FileHandle,
    PMY_IO_STATUS_BLOCK IoStatusBlock, PVOID FileInformation,
    ULONG Length, ULONG FileInformationClass);
static LONG (NTAPI *g_pfnNtQueryVolumeInformationFile)(HANDLE FileHandle,
    PMY_IO_STATUS_BLOCK IoStatusBlock, PVOID FsInformation,
    ULONG Length, ULONG FsInformationClass);


int
nt_get_filename_info(const char *pszPath, char *pszFull, size_t cchFull)
{
    static char                     abBuf[8192];
    PMY_FILE_NAME_INFORMATION       pFileNameInfo = (PMY_FILE_NAME_INFORMATION)abBuf;
    PMY_FILE_FS_VOLUME_INFORMATION  pFsVolInfo = (PMY_FILE_FS_VOLUME_INFORMATION)abBuf;
    MY_IO_STATUS_BLOCK              Ios;
    LONG                            rcNt;
    HANDLE                      hFile;
    int                             cchOut;
    char                           *psz;
    int                             iDrv;
    int                             rc;

    /*
     * Check for NtQueryInformationFile the first time around.
     */
    if (!g_fInitialized)
    {
        g_fInitialized = TRUE;
        if (!getenv("KMK_DONT_USE_NT_QUERY_INFORMATION_FILE"))
        {
            *(FARPROC *)&g_pfnNtQueryInformationFile =
                GetProcAddress(LoadLibrary("ntdll.dll"), "NtQueryInformationFile");
            *(FARPROC *)&g_pfnNtQueryVolumeInformationFile =
                GetProcAddress(LoadLibrary("ntdll.dll"), "NtQueryVolumeInformationFile");
        }
        if (    g_pfnNtQueryInformationFile
            &&  g_pfnNtQueryVolumeInformationFile)
        {
            unsigned i;
            for (i = 0; i < sizeof(g_afNtfsDrives) / sizeof(g_afNtfsDrives[0]); i++ )
                g_afNtfsDrives[i] = -1;
        }
        else
        {
            g_pfnNtQueryVolumeInformationFile = NULL;
            g_pfnNtQueryInformationFile = NULL;
        }
    }
    if (!g_pfnNtQueryInformationFile)
        return -1;

    /*
     * The FileNameInformation we get is relative to where the volume is mounted,
     * so we have to extract the driveletter prefix ourselves.
     *
     * FIXME: This will probably not work for volumes mounted in NTFS sub-directories.
     */
    psz = pszFull;
    if (pszPath[0] == '\\' || pszPath[0] == '/')
    {
        /* unc or root of volume */
        if (    (pszPath[1] == '\\' || pszPath[1] == '/')
            &&  (pszPath[2] != '\\' || pszPath[2] == '/'))
        {
#if 0 /* don't bother with unc yet. */
            /* unc - we get the server + name back */
            *psz++ = '\\';
#endif
            return -1;
        }
        /* root slash */
        *psz++ = _getdrive() + 'A' - 1;
        *psz++ = ':';
    }
    else if (pszPath[1] == ':' && isalpha(pszPath[0]))
    {
        /* drive letter */
        *psz++ = toupper(pszPath[0]);
        *psz++ = ':';
    }
    else
    {
        /* relative */
        *psz++ = _getdrive() + 'A' - 1;
        *psz++ = ':';
    }
    iDrv = *pszFull - 'A';

    /*
     * Fat32 doesn't return filenames with the correct case, so restrict it
     * to NTFS volumes for now.
     */
    if (g_afNtfsDrives[iDrv] == -1)
    {
        /* FSCTL_GET_REPARSE_POINT? Enumerate mount points? */
        g_afNtfsDrives[iDrv] = 0;
        psz[0] = '\\';
        psz[1] = '\0';
#if 1
        hFile = CreateFile(pszFull,
                           GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           NULL,
                           OPEN_EXISTING,
                           FILE_FLAG_BACKUP_SEMANTICS,
                           NULL);
        if (hFile != INVALID_HANDLE_VALUE)
        {
            PMY_FILE_FS_ATTRIBUTE_INFORMATION pFsAttrInfo = (PMY_FILE_FS_ATTRIBUTE_INFORMATION)abBuf;

            memset(&Ios, 0, sizeof(Ios));
            rcNt = g_pfnNtQueryVolumeInformationFile(hFile, &Ios, abBuf, sizeof(abBuf),
                                                     MY_FileFsAttributeInformation);
            if (    rcNt >= 0
                //&&  pFsAttrInfo->FileSystemNameLength == 4
                &&  pFsAttrInfo->FileSystemName[0] == 'N'
                &&  pFsAttrInfo->FileSystemName[1] == 'T'
                &&  pFsAttrInfo->FileSystemName[2] == 'F'
                &&  pFsAttrInfo->FileSystemName[3] == 'S'
                &&  pFsAttrInfo->FileSystemName[4] == '\0')
            {
                memset(&Ios, 0, sizeof(Ios));
                rcNt = g_pfnNtQueryVolumeInformationFile(hFile, &Ios, &g_aVolumeInfo[iDrv],
                                                         sizeof(MY_FILE_FS_VOLUME_INFORMATION),
                                                         MY_FileFsVolumeInformation);
                if (rcNt >= 0)
                {
                    DWORD dwDriveType = GetDriveType(pszFull);
                    if (    dwDriveType == DRIVE_FIXED
                        ||  dwDriveType == DRIVE_RAMDISK)
                        g_afNtfsDrives[iDrv] = 1;
                }
            }
            CloseHandle(hFile);
        }
#else
        {
            char szFSName[32];
            if (    GetVolumeInformation(pszFull,
                                         NULL, 0,   /* volume name */
                                         NULL,      /* serial number */
                                         NULL,      /* max component */
                                         NULL,      /* volume attribs */
                                         szFSName,
                                         sizeof(szFSName))
                &&  !strcmp(szFSName, "NTFS"))
            {
                g_afNtfsDrives[iDrv] = 1;
            }
        }
#endif
    }
    if (!g_afNtfsDrives[iDrv])
        return -1;

    /*
     * Try open the path and query its file name information.
     */
    hFile = CreateFile(pszPath,
                       GENERIC_READ,
                       FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                       NULL,
                       OPEN_EXISTING,
                       FILE_FLAG_BACKUP_SEMANTICS,
                       NULL);
    if (hFile != INVALID_HANDLE_VALUE)
    {
        /* check that the driver letter is correct first (reparse / symlink issues). */
        memset(&Ios, 0, sizeof(Ios));
        rcNt = g_pfnNtQueryVolumeInformationFile(hFile, &Ios, pFsVolInfo, sizeof(*pFsVolInfo), MY_FileFsVolumeInformation);
        if (rcNt >= 0)
        {
            /** @todo do a quick search and try correct the drive letter? */
            if (    pFsVolInfo->VolumeCreationTime.QuadPart == g_aVolumeInfo[iDrv].VolumeCreationTime.QuadPart
                &&  pFsVolInfo->VolumeSerialNumber == g_aVolumeInfo[iDrv].VolumeSerialNumber)
            {
                memset(&Ios, 0, sizeof(Ios));
                rcNt = g_pfnNtQueryInformationFile(hFile, &Ios, abBuf, sizeof(abBuf), MY_FileNameInformation);
                if (rcNt >= 0)
                {
                    cchOut = WideCharToMultiByte(CP_ACP, 0,
                                                 pFileNameInfo->FileName, pFileNameInfo->FileNameLength / sizeof(WCHAR),
                                                 psz, (int)(cchFull - (psz - pszFull) - 2), NULL, NULL);
                    if (cchOut > 0)
                    {
                        const char *pszEnd;
#if 0
                        /* upper case the server and share */
                        if (fUnc)
                        {
                            for (psz++; *psz != '/' && *psz != '\\'; psz++)
                                *psz = toupper(*psz);
                            for (psz++; *psz != '/' && *psz != '\\'; psz++)
                                *psz = toupper(*psz);
                        }
#endif
                        /* add trailing slash on directories if input has it. */
                        pszEnd = strchr(pszPath, '\0');
                        if (    (pszEnd[-1] == '/' || pszEnd[-1] == '\\')
                            &&  psz[cchOut - 1] != '\\'
                            &&  psz[cchOut - 1] != '//')
                            psz[cchOut++] = '\\';

                        /* make sure it's terminated */
                        psz[cchOut] = '\0';
                        rc = 0;
                    }
                    else
                        rc = -3;
                }
                else
                    rc = -4;
            }
            else
                rc = -5;
        }
        else
            rc = -6;
        CloseHandle(hFile);
    }
    else
        rc = -7;
    return rc;
}

/**
 * Somewhat similar to fullpath, except that it will fix
 * the case of existing path components.
 */
void
nt_fullpath(const char *pszPath, char *pszFull, size_t cchFull)
{
#if 0
    static int s_cHits = 0;
    static int s_cFallbacks = 0;
#endif

    /*
     * The simple case, the file / dir / whatever exists and can be
     * queried without problems and spaces.
     */
    if (nt_get_filename_info(pszPath, pszFull, cchFull) == 0)
    {
        /** @todo make nt_get_filename_info return spaceless path. */
        if (strchr(pszFull, ' '))
            w32_fixcase(pszFull);
#if 0
        fprintf(stdout, "nt #%d - %s\n", ++s_cHits, pszFull);
        fprintf(stdout, "   #%d - %s\n", s_cHits, pszPath);
#endif
        return;
    }
    if (g_pfnNtQueryInformationFile)
    {
        /* do _fullpath and drop off path elements until we get a hit... - later */
    }

    /*
     * For now, simply fall back on the old method.
     */
    _fullpath(pszFull, pszPath, cchFull);
    w32_fixcase(pszFull);
#if 0
    fprintf(stderr, "fb #%d - %s\n", ++s_cFallbacks, pszFull);
    fprintf(stderr, "   #%d - %s\n", s_cFallbacks, pszPath);
#endif
}

