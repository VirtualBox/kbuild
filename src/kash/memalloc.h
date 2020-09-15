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
#ifdef KASH_SEPARATE_PARSER_ALLOCATOR
        unsigned pstacksize;
#endif
	struct stackmark *marknext;
};


/*extern char *stacknxt;
extern int stacknleft;
extern int sstrnleft;
extern int herefd;*/

pointer ckmalloc(struct shinstance *, size_t);
pointer ckrealloc(struct shinstance *, pointer, size_t);
char *savestr(struct shinstance *, const char *);
#define ckfree(psh, p)  sh_free(psh, (pointer)(p))

#ifndef SH_MEMALLOC_NO_STACK
pointer stalloc(struct shinstance *, size_t);
char *stsavestr(struct shinstance *, const char *);
void stunalloc(struct shinstance *, pointer);
void setstackmark(struct shinstance *, struct stackmark *);
void popstackmark(struct shinstance *, struct stackmark *);
void growstackblock(struct shinstance *);
void grabstackblock(struct shinstance *, int);
char *growstackstr(struct shinstance *);
char *makestrspace(struct shinstance *);
char *grabstackstr(struct shinstance *, char *); /* was #define using stalloc */
void ungrabstackstr(struct shinstance *, char *, char *);

#define stackblock(psh)             (psh)->stacknxt
#define stackblocksize(psh)         (psh)->stacknleft
#define STARTSTACKSTR(psh, p)       p = stackblock(psh), (psh)->sstrnleft = stackblocksize(psh)
#define STPUTC(psh, c, p)           (--(psh)->sstrnleft >= 0? (*p++ = (c)) : (p = growstackstr(psh), *p++ = (c)))
#define CHECKSTRSPACE(psh, n, p)    { if ((psh)->sstrnleft < n) p = makestrspace(psh); }
#define USTPUTC(psh, c, p)          do { assert((psh)->sstrnleft > 0); \
                                         assert(p - (char *)stackblock(psh) == stackblocksize(psh) - (psh)->sstrnleft); \
                                         --(psh)->sstrnleft; *p++ = (c); } while (0)
#define STACKSTRNUL(psh, p)         ((psh)->sstrnleft == 0? (p = growstackstr(psh), *p = '\0') : (*p = '\0'))
#define STUNPUTC(psh, p)            (++(psh)->sstrnleft, --p)
#define STADJUST(psh, amount, p)    (p += (amount), (psh)->sstrnleft -= (amount))
#endif /* SH_MEMALLOC_NO_STACK */

/** @name Stack allocator for parser.
 * This is a stripped down version of the general stack allocator interface for
 * the exclusive use of the parser, so that parsetrees can be shared with
 * subshells by simple reference counting.
 * @{ */
#ifdef KASH_SEPARATE_PARSER_ALLOCATOR
struct pstack_block;
unsigned pstackretain(struct pstack_block *);
void pstackpop(struct shinstance *, unsigned);
unsigned pstackrelease(struct shinstance *, struct pstack_block *, const char *);
unsigned pstackretainpush(struct shinstance *, struct pstack_block *);
struct pstack_block *pstackallocpush(struct shinstance *);
void pstackmarkdone(struct pstack_block *);
#endif
void *pstalloc(struct shinstance *, size_t);
union node;
union node *pstallocnode(struct shinstance *, size_t);
struct nodelist;
struct nodelist *pstalloclist(struct shinstance *);
char *pstsavestr(struct shinstance *, const char *); /* was: stsavestr */
char *pstmakestrspace(struct shinstance *, size_t, char *); /* was: makestrspace / growstackstr */
char *pstputcgrow(struct shinstance *, char *, char);
char *pstgrabstr(struct shinstance *, char *); /* was: grabstackstr / grabstackblock*/
#ifdef KASH_SEPARATE_PARSER_ALLOCATOR
# define PSTBLOCK(psh)               ((psh)->curpstack->nextbyte)
# define PSTARTSTACKSTR(psh, p) do { \
        pstack_block *pstmacro = (psh)->curpstack; \
        pstmacro->strleft = pstmacro->avail; \
        (p) = pstmacro->nextbyte; \
    } while (0)
# define PSTCHECKSTRSPACE(psh, n, p) do { \
        if ((psh)->curpstack->strleft >= (n)) {/*likely*/} \
        else { (p) = pstmakestrspace(psh, (n), (p)); assert((psh)->curpstack->strleft >= (n)); } \
    } while (0)
# define PSTUPUTC(psh, c, p) do { \
        assert((psh)->curpstack->strleft > 0); \
        (psh)->curpstack->strleft -= 1; \
        *(p)++ = (c); \
    } while (0)
# define PSTPUTC(psh, c, p) do { \
        if ((ssize_t)--(psh)->curpstack->strleft >= 0) *(p)++ = (c); \
        else (p) = pstputcgrow(psh, (p), (c)); \
    } while (0)
# define PSTPUTSTRN(psh, str, n, p) do { \
        pstack_block *pstmacro = (psh)->curpstack; \
        if (pstmacro->strleft >= (n)) {/*likely?*/} \
        else (p) = pstmakestrspace(psh, (n), (p)); \
        pstmacro->strleft -= (n); \
        memcpy((p), (str), (n)); \
        (p) += (n); \
    } while (0)
#else
# define PSTBLOCK(psh)               ((psh)->stacknxt)
# define PSTARTSTACKSTR(psh, p)      do { (p) = (psh)->stacknxt; (psh)->sstrnleft = (psh)->stacknleft; } while (0)
# define PSTCHECKSTRSPACE(psh, n, p) do { if ((psh)->sstrnleft >= (n)) {/*likely*/} \
                                         else { (p) = pstmakestrspace(psh, (n), (p)); assert((psh)->sstrnleft >= (n)); } } while (0)
# define PSTUPUTC(psh, c, p)         do { assert((psh)->sstrnleft > 0); --(psh)->sstrnleft; *(p)++ = (c); } while (0)
# define PSTPUTC(psh, c, p)          do { if (--(psh)->sstrnleft >= 0) *(p)++ = (c); else (p) = pstputcgrow(psh, (p), (c)); } while (0)
# define PSTPUTSTRN(psh, str, n, p)  do { if ((psh)->sstrnleft >= (n)) {/*likely?*/} else (p) = pstmakestrspace(psh, (n), (p)); \
                                         memcpy((p), (str), (n)); (psh)->sstrnleft -= (n); (p) += (n); } while (0)
#endif
/** @} */


