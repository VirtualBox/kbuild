/*	$NetBSD: output.h,v 1.17 2003/08/07 09:05:36 agc Exp $	*/

/*-
 * Copyright (c) 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Kenneth Almquist.
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
 *
 *	@(#)output.h	8.2 (Berkeley) 5/4/95
 */

#ifndef OUTPUT_INCL

#include <stdarg.h>

struct output {
	char *nextc;
	int nleft;
	char *buf;
	int bufsize;
	short fd;
	short flags;
	struct shinstance *psh;
};

/*extern struct output output;
extern struct output errout;
extern struct output memout;
extern struct output *out1;
extern struct output *out2;*/
#if !defined(__GNUC__) && !defined(__attribute__)
# define __attribute__(a)
#endif

void open_mem(char *, int, struct output *);
void out1str(struct shinstance *, const char *);
void out2str(struct shinstance *, const char *);
void outstr(const char *, struct output *);
void emptyoutbuf(struct output *);
void output_flushall(struct shinstance *);
void flushout(struct output *);
void freestdout(struct shinstance *);
void outfmt(struct output *, const char *, ...)
    __attribute__((__format__(__printf__,3,4)));
void out1fmt(struct shinstance *, const char *, ...)
    __attribute__((__format__(__printf__,2,3)));
void dprintf(struct shinstance *, const char *, ...)
    __attribute__((__format__(__printf__,2,3)));
void fmtstr(char *, size_t, const char *, ...)
    __attribute__((__format__(__printf__,3,4)));
void doformat(struct output *, const char *, va_list);
int xwrite(struct shinstance *, int, char *, size_t);
int xioctl(struct shinstance *, int, unsigned long, char *);

#define outc(c, file)	(--(file)->nleft < 0? (emptyoutbuf(file), *(file)->nextc++ = (c)) : (*(file)->nextc++ = (c)))
#define out1c(psh, c)	outc(c, (psh)->out1);
#define out2c(psh, c)	outc(c, (psh)->out2);

#define OUTPUT_INCL
#endif
