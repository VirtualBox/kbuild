/* $Id: nt_fullpath.c 1248 2007-10-23 12:46:50Z knut.osmundsen@oracle.com $ */
/** @file
 * fixcase - fixes the case of paths, windows specific.
 */

/*
 * Copyright (c) 2004-2007 knut st. osmundsen <bird-src-spam@anduin.net>
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

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
        for (;;)
        {
            const char ch1 = *pszCur;
            const char ch2 = *pszLast;
            if (    ch1 != ch2
                &&  (ch1 != '\\' || ch2 != '/')
                &&  (ch1 != '/'  || ch2 != '\\'))
            {
                if (    tolower(ch1) != tolower(ch2)
                    &&  toupper(ch1) != toupper(ch2))
                    break;
                /* optimistic, component mismatch will be corrected in the next loop. */
                *pszCur = ch2;
            }
            if (ch1 == '/' || ch1 == '\\')
                psz = pszCur + 1;
            else if (ch1 == '\0')
            {
                psz = pszCur;
                break;
            }
            pszCur++;
            pszLast++;
        }
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

typedef struct _IO_STATUS_BLOCK
{
    union
    {
        LONG Status;
        PVOID Pointer;
    };
    ULONG_PTR Information;
} MY_IO_STATUS_BLOCK, *PMY_IO_STATUS_BLOCK;

static BOOL g_fInitialized = FALSE;
static int  g_afNtfsDrives['Z' - 'A' + 1];
static LONG (NTAPI *g_pfnNtQueryInformationFile)(HANDLE FileHandle,
    PMY_IO_STATUS_BLOCK IoStatusBlock, PVOID FileInformation,
    ULONG Length, ULONG FileInformationClass);


int
nt_get_filename_info(const char *pszPath, char *pszFull, size_t cchFull)
{
    static char                 abBuf[8192];
    PMY_FILE_NAME_INFORMATION   pFileNameInfo = (PMY_FILE_NAME_INFORMATION)abBuf;
    MY_IO_STATUS_BLOCK          Ios;
    LONG                        rcNt;
    HANDLE                      hFile;
    int                         cchOut;
    char                       *psz;

    /*
     * Check for NtQueryInformationFile the first time around.
     */
    if (!g_fInitialized)
    {
        g_fInitialized = TRUE;
        if (!getenv("KMK_DONT_USE_NT_QUERY_INFORMATION_FILE"))
            *(FARPROC *)&g_pfnNtQueryInformationFile =
                GetProcAddress(LoadLibrary("ntdll.dll"), "NtQueryInformationFile");
        if (g_pfnNtQueryInformationFile)
        {
            unsigned i;
            for (i = 0; i < sizeof(g_afNtfsDrives) / sizeof(g_afNtfsDrives[0]); i++ )
                g_afNtfsDrives[i] = -1;
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

    /*
     * Fat32 doesn't return filenames with the correct case, so restrict it
     * to NTFS volumes for now.
     */
    if (g_afNtfsDrives[*pszFull - 'A'] == -1)
    {
        char szFSName[32];

        psz[0] = '\\';
        psz[1] = '\0';
        if (    GetVolumeInformation(pszFull,
                                     NULL, 0,   /* volume name */
                                     NULL,      /* serial number */
                                     NULL,      /* max component */
                                     NULL,      /* volume attribs */
                                     szFSName,
                                     sizeof(szFSName))
            &&  !strcmp(szFSName, "NTFS"))
            g_afNtfsDrives[*pszFull - 'A'] = 1;
        else
            g_afNtfsDrives[*pszFull - 'A'] = 0;
    }
    if (!g_afNtfsDrives[*pszFull - 'A'])
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
    if (hFile)
    {
        memset(&Ios, 0, sizeof(Ios));
        rcNt = g_pfnNtQueryInformationFile(hFile, &Ios, abBuf, sizeof(abBuf), MY_FileNameInformation);
        CloseHandle(hFile);
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
                return 0;
            }
            return -3;
        }
    }
    return -2;
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
        if (strchr(pszPath, ' '))
            w32_fixcase(pszPath);
#if 0
        fprintf(stderr, "nt #%d - %s\n", ++s_cHits, pszFull);
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
#endif
}

