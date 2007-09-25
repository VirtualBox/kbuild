/*	$NetBSD: regular.c,v 1.20 2006/06/03 21:47:55 christos Exp $	*/

/*-
 * Copyright (c) 1991, 1993, 1994
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

/*#include <sys/cdefs.h>*/
#ifndef lint
/*#if 0
static char sccsid[] = "@(#)regular.c	8.3 (Berkeley) 4/2/94";
#else
__RCSID("$NetBSD: regular.c,v 1.20 2006/06/03 21:47:55 christos Exp $");
#endif*/
#endif /* not lint */

#include <sys/stat.h>

#include "err.h"
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "cmp_extern.h"

static int
c_regular(int fd1, char *file1, off_t skip1, off_t len1,
    int fd2, char *file2, off_t skip2, off_t len2)
{
	unsigned char ch, *p1, *p2, *b1 = 0, *b2 = 0;
	off_t byte, length, line, bytes_read;
	int dfound;
	size_t blk_sz, blk_cnt;

	if (sflag && len1 != len2)
		return(1);

	if (skip1 > len1)
		return eofmsg(file1, len1 + 1, 0);
	len1 -= skip1;
	if (skip2 > len2)
		return eofmsg(file2, len2 + 1, 0);
	len2 -= skip2;

	if (skip1 && lseek(fd1, skip1, SEEK_SET) < 0)
		goto l_special;
	if (skip2 && lseek(fd2, skip2, SEEK_SET) < 0) {
		if (skip1 && lseek(fd1, 0, SEEK_SET) < 0)
			return err(1, "seek failed");
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
	for (blk_sz = CMP_BUF_SIZE; length != 0; length -= blk_sz) {
		if ((off_t)blk_sz > length)
			blk_sz = length;

		bytes_read = read(fd1, b1, blk_sz);
		if (bytes_read != blk_sz)
			goto l_read_error;

		bytes_read = read(fd2, b2, blk_sz);
		if (bytes_read != blk_sz)
			goto l_read_error;

		blk_cnt = blk_sz;
		p1 = b1;
		p2 = b2;
		for (; blk_cnt--; ++p1, ++p2, ++byte) {
			if ((ch = *p1) != *p2) {
				if (!lflag) {
					free(b1);
					free(b2);
					return diffmsg(file1, file2, byte, line);
				}
				dfound = 1;
				(void)printf("%6lld %3o %3o\n",
				    (long long)byte, ch, *p2);
			}
			if (ch == '\n')
				++line;
		}
		skip1 += blk_sz;
		skip2 += blk_sz;
	}

	if (len1 != len2)
		return eofmsg(len1 > len2 ? file2 : file1, byte, line);
	if (dfound)
		return(DIFF_EXIT);
	return(0);

l_read_error:
	if (	lseek(fd1, 0, SEEK_SET) < 0
		||	lseek(fd2, 0, SEEK_SET) < 0) {
		err(1, "seek failed");
		free(b1);
		free(b2);
		return 1;
	}
l_malloc_failed:
	free(b1);
	free(b2);
l_special:
	return c_special(fd1, file1, skip1, fd2, file2, skip2);
}

