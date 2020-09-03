/*	$NetBSD: memalloc.h,v 1.14 2003/08/07 09:05:34 agc Exp $	*/

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
 *	@(#)memalloc.h	8.2 (Berkeley) 5/4/95
 */

struct stackmark {
	struct stack_block *stackp;
	char *stacknxt;
	int stacknleft;
	struct stackmark *marknext;
};


/*extern char *stacknxt;
extern int stacknleft;
extern int sstrnleft;
extern int herefd;*/

pointer ckmalloc(struct shinstance *, size_t);
pointer ckrealloc(struct shinstance *, pointer, size_t);
char *savestr(struct shinstance *, const char *);
pointer stalloc(struct shinstance *, size_t);
char *stsavestr(struct shinstance *, const char *);
void stunalloc(struct shinstance *, pointer);
void setstackmark(struct shinstance *, struct stackmark *);
void popstackmark(struct shinstance *, struct stackmark *);
void growstackblock(struct shinstance *);
void grabstackblock(struct shinstance *, int);
char *growstackstr(struct shinstance *);
char *makestrspace(struct shinstance *);
void ungrabstackstr(struct shinstance *, char *, char *);



#define stackblock(psh)             (psh)->stacknxt
#define stackblocksize(psh)         (psh)->stacknleft
#define STARTSTACKSTR(psh, p)       p = stackblock(psh), (psh)->sstrnleft = stackblocksize(psh)
#define STPUTC(psh, c, p)           (--(psh)->sstrnleft >= 0? (*p++ = (c)) : (p = growstackstr(psh), *p++ = (c)))
#define CHECKSTRSPACE(psh, n, p)    { if ((psh)->sstrnleft < n) p = makestrspace(psh); }
#define USTPUTC(psh, c, p)          (--(psh)->sstrnleft, *p++ = (c))
#define STACKSTRNUL(psh, p)         ((psh)->sstrnleft == 0? (p = growstackstr(psh), *p = '\0') : (*p = '\0'))
#define STUNPUTC(psh, p)            (++(psh)->sstrnleft, --p)
#define STTOPC(psh, p)	            p[-1]
#define STADJUST(psh, amount, p)    (p += (amount), (psh)->sstrnleft -= (amount))
#define grabstackstr(psh, p)        stalloc((psh), stackblocksize(psh) - (psh)->sstrnleft)

#define ckfree(psh, p)	            sh_free(psh, (pointer)(p))
