/* $Id: ntstat.h 2708 2013-11-21 10:26:40Z knut.osmundsen@oracle.com $ */
/** @file
 * MSC + NT stat, lstat and fstat implementation and wrappers.
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

#ifndef ___nt_ntstat_h
#define ___nt_ntstat_h

#include "nttypes.h"

#include <sys/stat.h>
#include <io.h>
#include <direct.h>

#undef stat
#undef lstat
#undef fstat


/** The distance between the NT and unix epochs given in NT time (units of 100
 *  ns). */
#define BIRD_NT_EPOCH_OFFSET_UNIX_100NS 116444736000000000LL

typedef struct BirdStat
{
    unsigned __int16    st_mode;
    unsigned __int16    st_dirsymlink;
    unsigned __int16    st_padding0[2];
    __int64             st_size;
    BirdTimeSpec_T      st_atim;
    BirdTimeSpec_T      st_mtim;
    BirdTimeSpec_T      st_ctim;
    BirdTimeSpec_T      st_birthtim;
    unsigned __int64    st_ino;
    unsigned __int64    st_dev;
    unsigned __int32    st_nlink;
    unsigned __int16    st_rdev;
    __int16             st_uid;
    __int16             st_gid;
    unsigned __int16    st_padding1[3];
    unsigned __int32    st_blksize;
    __int64             st_blocks;
} BirdStat_T;

#define BIRD_STAT_BLOCK_SIZE    512

#define st_atime        st_atim.tv_sec
#define st_ctime        st_ctim.tv_sec
#define st_mtime        st_mtim.tv_sec
#define st_birthtime    st_birthtim.tv_sec

int birdStatFollowLink(const char *pszPath, BirdStat_T *pStat);
int birdStatOnLink(const char *pszPath, BirdStat_T *pStat);
int birdStatOnFd(int fd, BirdStat_T *pStat);
int birdStatModTimeOnly(const char *pszPath, BirdTimeSpec_T *pTimeSpec, int fFollowLink);
#ifdef ___nt_ntstuff_h
void birdStatFillFromFileIdFullDirInfo(BirdStat_T *pStat, MY_FILE_ID_FULL_DIR_INFORMATION const *pBuf, const char *pszPath);
#endif

#define STAT_REDEFINED_ALREADY

#define stat                            BirdStat
#define BirdStat(a_pszPath, a_pStat)    birdStatFollowLink(a_pszPath, a_pStat)
#define lstat(a_pszPath, a_pStat)       birdStatOnLink(a_pszPath, a_pStat)
#define fstat(a_fd, a_pStat)            birdStatOnFd(a_fd, a_pStat)


#ifndef _S_IFLNK
# define _S_IFLNK       0xa000
#endif
#ifndef S_IFLNK
# define S_IFLNK        _S_IFLNK
#endif
#ifndef S_IFIFO
# define S_IFIFO        _S_IFIFO
#endif

#ifndef S_ISLNK
# define S_ISLNK(m)     (((m) & _S_IFMT) == _S_IFLNK)
#endif
#ifndef S_ISDIR
# define S_ISDIR(m)     (((m) & _S_IFMT) == _S_IFDIR)
#endif
#ifndef S_ISREG
# define S_ISREG(m)     (((m) & _S_IFMT) == _S_IFREG)
#endif

#define	S_IRWXU         (_S_IREAD | _S_IWRITE | _S_IEXEC)
#define	S_IXUSR         _S_IEXEC
#define	S_IWUSR         _S_IWRITE
#define	S_IRUSR         _S_IREAD
#define S_IRWXG         0000070
#define S_IRGRP	        0000040
#define S_IWGRP	        0000020
#define S_IXGRP         0000010
#define S_IRWXO         0000007
#define S_IROTH	        0000004
#define S_IWOTH	        0000002
#define S_IXOTH         0000001
#define	S_ISUID         0004000
#define	S_ISGID         0002000
#define ALLPERMS        0000777

#endif

