/*	$NetBSD: cmp.c,v 1.15 2006/01/19 20:44:57 garbled Exp $	*/
/*	$NetBSD: misc.c,v 1.11 2007/08/22 16:59:19 christos Exp $	*/
/*	$NetBSD: regular.c,v 1.20 2006/06/03 21:47:55 christos Exp $	*/
/*	$NetBSD: special.c,v 1.12 2007/08/21 14:09:54 christos Exp $	*/

/*
 * Copyright (c) 1987, 1990, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*__COPYRIGHT("@(#) Copyright (c) 1987, 1990, 1993, 1994\n\
	The Regents of the University of California.  All rights reserved.\n");*/

#ifdef _MSC_VER
# define MSC_DO_64_BIT_IO
#endif
#include "config.h"
#include <sys/types.h>
#include <sys/stat.h>
#if defined(__FreeBSD__) || defined(__NetBSD__) /** @todo more mmap capable OSes. */
# define CMP_USE_MMAP
# include <sys/param.h>
# include <sys/mman.h>
#endif
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _MSC_VER
# include <unistd.h>
# ifndef O_BINARY
#  define O_BINARY 0
# endif
#else
# include "mscfakes.h"
#endif
#include "err.h"

#include "cmp_extern.h"


static int
errmsg(PKMKBUILTINCTX pCtx, const char *file, off_t byte, off_t line, int lflag)
{
    if (lflag)
#ifdef _MSC_VER
        return err(pCtx, ERR_EXIT, "%s: char %I64d, line %lld", file, (__int64)byte, (long long)line);
#else
        return err(pCtx, ERR_EXIT, "%s: char %lld, line %lld", file, (long long)byte, (long long)line);
#endif
    return err(pCtx, ERR_EXIT, "%s", file);
}


static int
eofmsg(PKMKBUILTINCTX pCtx, const char *file, off_t byte, off_t line, int sflag, int lflag)
{
    if (!sflag)
    {
        if (!lflag)
            warnx(pCtx, "EOF on %s", file);
        else
        {
#ifdef _MSC_VER
            if (line > 0)
                warnx(pCtx, "EOF on %s: char %I64d, line %I64d", file, (__int64)byte, (__int64)line);
            else
                warnx(pCtx, "EOF on %s: char %I64d", file, (__int64)byte);
#else
            if (line > 0)
                warnx(pCtx, "EOF on %s: char %lld, line %lld", file, (long long)byte, (long long)line);
            else
                warnx(pCtx, "EOF on %s: char %lld", file, (long long)byte);
#endif
        }
    }
    return DIFF_EXIT;
}


static int
diffmsg(PKMKBUILTINCTX pCtx, const char *file1, const char *file2, off_t byte, off_t line, int sflag)
{
    if (!sflag)
#ifdef _MSC_VER
        kmk_builtin_ctx_printf(pCtx, 0, "%s %s differ: char %I64d, line %I64d\n",
               file1, file2, (__int64)byte, (__int64)line);
#else
        kmk_builtin_ctx_printf(pCtx, 0, "%s %s differ: char %lld, line %lld\n",
               file1, file2, (long long)byte, (long long)line);
#endif
    return DIFF_EXIT;
}


/**
 * Compares two files, where one or both are non-regular ones.
 */
static int
c_special(PKMKBUILTINCTX pCtx, int fd1, const char *file1, off_t skip1,
          int fd2, const char *file2, off_t skip2,
          int lflag, int sflag)
{
    int fd1dup, fd2dup;
    FILE *fp1;
    int rc;

    /* duplicate because fdopen+fclose will otherwise close the handle. */
    fd1dup = dup(fd1);
    if (fd1 < 0)
        return err(pCtx, ERR_EXIT, "%s", file1);
    fp1 = fdopen(fd1dup, "rb");
    if (!fp1)
        fp1 = fdopen(fd1dup, "r");
    if (!fp1)
    {
        err(pCtx, ERR_EXIT, "%s", file1);
        close(fd1dup);
        return ERR_EXIT;
    }

    fd2dup = dup(fd2);
    if (fd2dup >= 0)
    {
        FILE *fp2 = fdopen(fd2dup, "rb");
        if (!fp2)
            fp2 = fdopen(fd2dup, "r");
        if (fp2)
        {
            off_t byte;
            off_t line;
            int ch1 = 0;
            int ch2 = 0;

            /* skipping ahead */
            rc = OK_EXIT;
            for (byte = line = 1; skip1--; byte++)
            {
                ch1 = getc(fp1);
                if (ch1 == EOF)
                    break;
                if (ch1 == '\n')
                    line++;
            }
            for (byte = line = 1; skip2--; byte++)
            {
                ch2 = getc(fp2);
                if (ch2 == EOF)
                    break;
                if (ch2 == '\n')
                    line++;
            }
            if (ch2 != EOF && ch1 != EOF)
            {
                /* compare byte by byte */
                for (byte = line = 1;; ++byte)
                {
                    ch1 = getc(fp1);
                    ch2 = getc(fp2);
                    if (ch1 == EOF || ch2 == EOF)
                        break;
                    if (ch1 != ch2)
                    {
                        if (!lflag)
                        {
                            rc = diffmsg(pCtx, file1, file2, byte, line, sflag);
                            break;
                        }
                        rc = DIFF_EXIT;
#ifdef _MSC_VER
                        kmk_builtin_ctx_printf(pCtx, 0, "%6i64d %3o %3o\n", (__int64)byte, ch1, ch2);
#else
                        kmk_builtin_ctx_printf(pCtx, 0, "%6lld %3o %3o\n", (long long)byte, ch1, ch2);
#endif
                    }
                    if (ch1 == '\n')
                        ++line;
                }
            }

            /* Check for errors and length differences (EOF). */
            if (ferror(fp1) && rc != ERR_EXIT)
                rc = errmsg(pCtx, file1, byte, line, lflag);
            if (ferror(fp2) && rc != ERR_EXIT)
                rc = errmsg(pCtx, file2, byte, line, lflag);
            if (rc == OK_EXIT)
            {
                if (feof(fp1))
                {
                    if (!feof(fp2))
                        rc = eofmsg(pCtx, file1, byte, line, sflag, lflag);
                }
                else if (feof(fp2))
                    rc = eofmsg(pCtx, file2, byte, line, sflag, lflag);
            }

            fclose(fp2);
        }
        else
        {
            rc = err(pCtx, ERR_EXIT, "%s", file2);
            close(fd2dup);
        }
    }
    else
        rc = err(pCtx, ERR_EXIT, "%s", file2);

    fclose(fp1);
    return rc;
}


#ifdef CMP_USE_MMAP
/**
 * Compare two files using mmap.
 */
static int
c_regular(PKMKBUILTINCTX pCtx, int fd1, const char *file1, off_t skip1, off_t len1,
          int fd2, const char *file2, off_t skip2, off_t len2, int sflag, int lflag)
{
    unsigned char ch, *p1, *p2, *b1, *b2;
    off_t byte, length, line;
    int dfound;
    size_t blk_sz, blk_cnt;

    if (sflag && len1 != len2)
        return DIFF_EXIT;

    if (skip1 > len1)
        return eofmsg(pCtx, file1, len1 + 1, 0, sflag, lflag);
    len1 -= skip1;
    if (skip2 > len2)
        return eofmsg(pCtx, file2, len2 + 1, 0, sflag, lflag);
    len2 -= skip2;

    byte = line = 1;
    dfound = 0;
    length = len1 <= len2 ? len1 : len2;
    for (blk_sz = 1024 * 1024; length != 0; length -= blk_sz)
    {
        if (blk_sz > length)
            blk_sz = length;
        b1 = p1 = mmap(NULL, blk_sz, PROT_READ, MAP_FILE | MAP_SHARED, fd1, skip1);
        if (p1 == MAP_FAILED)
            goto l_mmap_failed;

        b2 = p2 = mmap(NULL, blk_sz, PROT_READ, MAP_FILE | MAP_SHARED, fd2, skip2);
        if (p2 == MAP_FAILED)
        {
            munmap(p1, blk_sz);
            goto l_mmap_failed;
        }

        blk_cnt = blk_sz;
        for (; blk_cnt--; ++p1, ++p2, ++byte)
        {
            if ((ch = *p1) != *p2)
            {
                if (!lflag)
                {
                    munmap(b1, blk_sz);
                    munmap(b2, blk_sz);
                    return diffmsg(pCtx, file1, file2, byte, line, sflag);
                }
                dfound = 1;
#ifdef _MSC_VER
                kmk_builtin_ctx_printf(pCtx, 0, "%6I64d %3o %3o\n", (__int64)byte, ch, *p2);
#else
                kmk_builtin_ctx_printf(pCtx, 0, "%6lld %3o %3o\n", (long long)byte, ch, *p2);
#endif
            }
            if (ch == '\n')
                ++line;
        }
        munmap(p1 - blk_sz, blk_sz);
        munmap(p2 - blk_sz, blk_sz);
        skip1 += blk_sz;
        skip2 += blk_sz;
    }

    if (len1 != len2)
        return eofmsg(pCtx, len1 > len2 ? file2 : file1, byte, line, sflag, lflag);
    if (dfound)
        return DIFF_EXIT;
    return OK_EXIT;

l_mmap_failed:
    return c_special(pCtx, fd1, file1, skip1, fd2, file2, skip2, lflag, sflag);
}

#else /* non-mmap c_regular: */

/**
 * Compare two files without mmaping them.
 */
static int
c_regular(PKMKBUILTINCTX pCtx, int fd1, const char *file1, off_t skip1, off_t len1,
          int fd2, const char *file2, off_t skip2, off_t len2, int sflag, int lflag)
{
    unsigned char ch, *p1, *p2, *b1 = 0, *b2 = 0;
    off_t byte, length, line, bytes_read;
    int dfound;
    size_t blk_sz, blk_cnt;

    if (sflag && len1 != len2)
        return DIFF_EXIT;

    if (skip1 > len1)
        return eofmsg(pCtx, file1, len1 + 1, 0, sflag, lflag);
    len1 -= skip1;
    if (skip2 > len2)
        return eofmsg(pCtx, file2, len2 + 1, 0, sflag, lflag);
    len2 -= skip2;

    if (skip1 && lseek(fd1, skip1, SEEK_SET) < 0)
        goto l_special;
    if (skip2 && lseek(fd2, skip2, SEEK_SET) < 0)
    {
        if (skip1 && lseek(fd1, 0, SEEK_SET) < 0)
            return err(pCtx, 1, "seek failed");
        goto l_special;
    }

#define CMP_BUF_SIZE (128*1024)

    b1 = malloc(CMP_BUF_SIZE);
    b2 = malloc(CMP_BUF_SIZE);
    if (!b1 || !b2)
        goto l_malloc_failed;

    byte = line = 1;
    dfound = 0;
    length = len1;
    if (length > len2)
        length = len2;
    for (blk_sz = CMP_BUF_SIZE; length != 0; length -= blk_sz)
    {
        if ((off_t)blk_sz > length)
            blk_sz = (size_t)length;

        bytes_read = read(fd1, b1, blk_sz);
        if (bytes_read != (off_t)blk_sz)
            goto l_read_error;

        bytes_read = read(fd2, b2, blk_sz);
        if (bytes_read != (off_t)blk_sz)
            goto l_read_error;

        blk_cnt = blk_sz;
        p1 = b1;
        p2 = b2;
        for (; blk_cnt--; ++p1, ++p2, ++byte)
        {
            if ((ch = *p1) != *p2)
            {
                if (!lflag)
                {
                    free(b1);
                    free(b2);
                    return diffmsg(pCtx, file1, file2, byte, line, sflag);
                }
                dfound = 1;
#ifdef _MSC_VER
                kmk_builtin_ctx_printf(pCtx, 0, "%6I64d %3o %3o\n", (__int64)byte, ch, *p2);
#else
                kmk_builtin_ctx_printf(pCtx, 0, "%6lld %3o %3o\n", (long long)byte, ch, *p2);
#endif
            }
            if (ch == '\n')
                ++line;
        }
        skip1 += blk_sz;
        skip2 += blk_sz;
    }

    if (len1 != len2)
        return eofmsg(pCtx, len1 > len2 ? file2 : file1, byte, line, sflag, lflag);
    if (dfound)
        return DIFF_EXIT;
    return OK_EXIT;

l_read_error:
    if (    lseek(fd1, 0, SEEK_SET) < 0
        ||  lseek(fd2, 0, SEEK_SET) < 0)
    {
        err(pCtx, 1, "seek failed");
        free(b1);
        free(b2);
        return 1;
    }
l_malloc_failed:
    free(b1);
    free(b2);
l_special:
    return c_special(pCtx, fd1, file1, skip1, fd2, file2, skip2, lflag, sflag);
}
#endif  /* non-mmap c_regular */


/**
 * Compares two open files.
 */
int
cmp_fd_and_fd_ex(PKMKBUILTINCTX pCtx, int fd1, const char *file1, off_t skip1,
                 int fd2, const char *file2, off_t skip2,
                 int sflag, int lflag, int special)
{
    struct stat st1, st2;
    int rc;

    if (fstat(fd1, &st1))
        return err(pCtx, ERR_EXIT, "%s", file1);
    if (fstat(fd2, &st2))
        return err(pCtx, ERR_EXIT, "%s", file2);

    if (    !S_ISREG(st1.st_mode)
        ||  !S_ISREG(st2.st_mode)
        ||  special)
        rc = c_special(pCtx, fd1, file1, skip1,
                       fd2, file2, skip2, sflag, lflag);
    else
        rc = c_regular(pCtx, fd1, file1, skip1, st1.st_size,
                       fd2, file2, skip2, st2.st_size, sflag, lflag);
    return rc;
}


/**
 * Compares two open files.
 */
int
cmp_fd_and_fd(PKMKBUILTINCTX pCtx, int fd1, const char *file1,
              int fd2, const char *file2,
              int sflag, int lflag, int special)
{
    return cmp_fd_and_fd_ex(pCtx, fd1, file1, 0, fd2, file2, 0, sflag, lflag, special);
}


/**
 * Compares an open file with another that isn't open yet.
 */
int
cmp_fd_and_file_ex(PKMKBUILTINCTX pCtx, int fd1, const char *file1, off_t skip1,
                   const char *file2, off_t skip2,
                   int sflag, int lflag, int special)
{
    int rc;
    int fd2;

    if (!strcmp(file2, "-"))
    {
        fd2 = 0 /* stdin */;
        special = 1;
        file2 = "stdin";
    }
    else
        fd2 = open(file2, O_RDONLY | O_BINARY | KMK_OPEN_NO_INHERIT, 0);
    if (fd2 >= 0)
    {
        rc = cmp_fd_and_fd_ex(pCtx, fd1, file1, skip1,
                              fd2, file2, skip2, sflag, lflag, special);
        close(fd2);
    }
    else
    {
        if (!sflag)
            warn(pCtx, "%s", file2);
        rc = ERR_EXIT;
    }
    return rc;
}


/**
 * Compares an open file with another that isn't open yet.
 */
int
cmp_fd_and_file(PKMKBUILTINCTX pCtx, int fd1, const char *file1,
                const char *file2,
                int sflag, int lflag, int special)
{
    return cmp_fd_and_file_ex(pCtx, fd1, file1, 0,
                                   file2, 0, sflag, lflag, special);
}


/**
 * Opens and compare two files.
 */
int
cmp_file_and_file_ex(PKMKBUILTINCTX pCtx, const char *file1, off_t skip1,
                     const char *file2, off_t skip2,
                     int sflag, int lflag, int special)
{
    int fd1;
    int rc;

    if (lflag && sflag)
        return errx(pCtx, ERR_EXIT, "only one of -l and -s may be specified");

    if (!strcmp(file1, "-"))
    {
        if (!strcmp(file2, "-"))
            return errx(pCtx, ERR_EXIT, "standard input may only be specified once");
        file1 = "stdin";
        fd1 = 1;
        special = 1;
    }
    else
        fd1 = open(file1, O_RDONLY | O_BINARY | KMK_OPEN_NO_INHERIT, 0);
    if (fd1 >= 0)
    {
        rc = cmp_fd_and_file_ex(pCtx, fd1, file1, skip1,
                                     file2, skip2, sflag, lflag, special);
        close(fd1);
    }
    else
    {
        if (!sflag)
            warn(pCtx, "%s", file1);
        rc = ERR_EXIT;
    }

    return rc;
}


/**
 * Opens and compare two files.
 */
int
cmp_file_and_file(PKMKBUILTINCTX pCtx, const char *file1, const char *file2, int sflag, int lflag, int special)
{
    return cmp_file_and_file_ex(pCtx, file1, 0, file2, 0, sflag, lflag, special);
}

