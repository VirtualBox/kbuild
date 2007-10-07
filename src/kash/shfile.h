/* $Id: $ */
/** @file
 *
 * File management.
 *
 * Copyright (c) 2007 knut st. osmundsen <bird-src-spam@anduin.net>
 *
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

#ifndef ___shfile_h___
#define ___shfile_h___

#include "shtypes.h"
#include <fcntl.h>
#include <sys/stat.h>
#ifndef _MSC_VER
# include <sys/fcntl.h>
# include <unistd.h>
# ifndef O_BINARY
#  define O_BINARY  0
# endif
# ifndef O_TEXT
#  define O_TEXT    0
# endif

#else
# include <io.h>
# include <direct.h>

# define S_ISDIR(m) (((m) & _S_IFMT) == _S_IFDIR)
# define S_ISREG(m) (((m) & _S_IFMT) == _S_IFREG)
# define S_ISLNK(m) 0
# define S_IRWXU    (_S_IREAD | _S_IWRITE | _S_IEXEC)
# define S_IXUSR    _S_IEXEC
# define S_IWUSR    _S_IWRITE
# define S_IRUSR    _S_IREAD
# define S_IRWXG    0000070
# define S_IRGRP    0000040
# define S_IWGRP    0000020
# define S_IXGRP    0000010
# define S_IRWXO    0000007
# define S_IROTH    0000004
# define S_IWOTH    0000002
# define S_IXOTH    0000001
# define S_ISUID    0004000
# define S_ISGID    0002000
# define ALLPERMS   0000777

# define F_DUPFD    0
# define F_GETFD    1
# define F_SETFD    2
# define F_GETFL    3
# define F_SETFL    4
# define FD_CLOEXEC 1

# define F_OK       0
# define X_OK       1
# define W_OK       2
# define R_OK       4

#endif


/**
 * One file.
 */
typedef struct shfile
{
    int                 fd;             /**< The shell file descriptor number. */
    int                 flags;          /**< Open flags. */
    intptr_t            native;         /**< The native file descriptor number. */
} shfile;

/**
 * The file descriptor table for a shell.
 */
typedef struct shfdtab
{
    shmtx               mtx;            /**< Mutex protecting any operations on the table and it's handles. */
    char               *cwd;            /**< The current directory for this shell instance. */
    unsigned            size;           /**< The size of the table (number of entries). */
    shfile             *tab;            /**< Pointer to the table. */
} shfdtab;

int shfile_open(shfdtab *, const char *, unsigned);
int shfile_pipe(shfdtab *, int [2]);
int shfile_close(shfdtab *, unsigned);
long shfile_read(shfdtab *, int, void *, size_t);
long shfile_write(shfdtab *, int, const void *, size_t);
long shfile_lseek(shfdtab *, int, long, int);
int shfile_fcntl(shfdtab *, int fd, int cmd, int arg);

int shfile_stat(shfdtab *, const char *, struct stat *);
int shfile_lstat(shfdtab *, const char *, struct stat *);
int shfile_chdir(shfdtab *, const char *);
char *shfile_getcwd(shfdtab *, char *, int);
int shfile_isatty(shfdtab *, int);
int shfile_ioctl(shfdtab *, int, unsigned long, char *);
int shfile_access(shfdtab *, const char *, int);

#endif

