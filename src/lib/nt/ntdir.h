/* $Id: ntdir.h 3005 2016-11-06 00:07:37Z knut.osmundsen@oracle.com $ */
/** @file
 * MSC + NT opendir, readdir, closedir and friends.
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

#ifndef ___nt_ntdir_h
#define ___nt_ntdir_h

#include "nttypes.h"
#include "ntstat.h"

typedef struct dirent
{
    /** Optional stat information.
     * Only provided if using birdDirOpenExtraInfo(). */
    BirdStat_T          d_stat;
    /** The record length. */
    unsigned __int16    d_reclen;
    /** The name length. */
    unsigned __int16    d_namlen;
    /** The name type. */
    unsigned char       d_type;
    /** The name. */
    char                d_name[512 - sizeof(BirdStat_T) - 2 - 2 - 1];
} BirdDirEntry_T;

typedef struct direntw
{
    /** Optional stat information.
     * Only provided if using birdDirOpenExtraInfo(). */
    BirdStat_T          d_stat;
    /** The record length. */
    unsigned __int16    d_reclen;
    /** The name length (in wchar_t). */
    unsigned __int16    d_namlen;
    /** The name type. */
    unsigned char       d_type;
    /** The name. */
    wchar_t             d_name[512 - sizeof(BirdStat_T) - 2 - 2 - 1];
} BirdDirEntryW_T;

#define d_ino           d_stat.st_ino;

/** @name d_type values.
 * @{ */
#define DT_UNKNOWN           0
#define DT_FIFO              1
#define DT_CHR               2
#define DT_DIR               4
#define DT_BLK               6
#define DT_REG               8
#define DT_LNK              10
#define DT_SOCK             12
#define DT_WHT              14
/** @}  */

/** @name BIRDDIR_F_XXX - birdDirOpenFromHandle & BirdDir_T::fFlags
 * @{ */
/** birdDirClose should also close pvHandle.  */
#define BIRDDIR_F_CLOSE_HANDLE  1U
/** birdDirClose should not close the handle.  */
#define BIRDDIR_F_KEEP_HANDLE   0U
/** Provide extra info (stat). */
#define BIRDDIR_F_EXTRA_INFO    2U
/** Whether to restart the scan. */
#define BIRDDIR_F_RESTART_SCAN  4U
/** Set if the BirdDir_T structure is statically allocated. */
#define BIRDDIR_F_STATIC_ALLOC  8U
/** @} */

typedef struct BirdDir
{
    /** Magic value. */
    unsigned            uMagic;
    /** Flags. */
    unsigned            fFlags;
    /** The directory handle. */
    void               *pvHandle;
    /** The device number (st_dev). */
    unsigned __int64    uDev;
    /** The current position. */
    long                offPos;

    /** Set if we haven't yet read anything. */
    int                 fFirst;
    /** Set if we have data in the buffer. */
    int                 fHaveData;
    /** The info type we're querying. */
    int                 iInfoClass;
    /** The current buffer position. */
    unsigned            offBuf;
    /** The number of bytes allocated for pabBuf. */
    unsigned            cbBuf;
    /** Buffer of size cbBuf. */
    unsigned char      *pabBuf;

    /** Static directory entry. */
    union
    {
        BirdDirEntry_T  DirEntry;
        BirdDirEntryW_T DirEntryW;
    } u;
} BirdDir_T;
/** Magic value for BirdDir. */
#define BIRD_DIR_MAGIC      0x19731120


BirdDir_T      *birdDirOpen(const char *pszPath);
BirdDir_T      *birdDirOpenExtraInfo(const char *pszPath);
BirdDir_T      *birdDirOpenExW(void *hRoot, const wchar_t *pwszPath, const wchar_t *pwszFilter, unsigned fFlags);
BirdDir_T      *birdDirOpenFromHandle(void *hDir, const void *pvReserved, unsigned fFlags);
BirdDir_T      *birdDirOpenFromHandleWithReuse(BirdDir_T *pDir, void *pvHandle, const void *pvReserved, unsigned fFlags);
BirdDirEntry_T *birdDirRead(BirdDir_T *pDir);
BirdDirEntryW_T *birdDirReadW(BirdDir_T *pDir);
long            birdDirTell(BirdDir_T *pDir);
void            birdDirSeek(BirdDir_T *pDir, long offDir);
int             birdDirClose(BirdDir_T *pDir);

#define opendir                     birdDirOpen
#define readdir                     birdDirRead
#define telldir                     birdDirTell
#define seekdir                     birdDirSeek
#define rewinddir(a_pDir, a_offDir) birdDirSeek(a_pDir, 0)
#define closedir                    birdDirClose
#define _D_NAMLEN(a_pEnt)           ((a_pEnt)->d_namlen)
typedef BirdDir_T DIR;

#endif

