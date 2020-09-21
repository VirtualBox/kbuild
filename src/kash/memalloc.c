/*	$NetBSD: memalloc.c,v 1.28 2003/08/07 09:05:34 agc Exp $	*/

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
 */

#if 0
#ifndef lint
static char sccsid[] = "@(#)memalloc.c	8.3 (Berkeley) 5/4/95";
#else
__RCSID("$NetBSD: memalloc.c,v 1.28 2003/08/07 09:05:34 agc Exp $");
#endif /* not lint */
#endif

#include <stdlib.h>
#include <stddef.h>

#include "shell.h"
#include "output.h"
#include "memalloc.h"
#include "error.h"
#include "machdep.h"
#include "mystring.h"
#include "shinstance.h"
#include "nodes.h"

/*
 * Like malloc, but returns an error when out of space.
 */

pointer
ckmalloc(shinstance *psh, size_t nbytes)
{
	pointer p;

	p = sh_malloc(psh, nbytes);
	if (p == NULL)
		error(psh, "Out of space");
	return p;
}


/*
 * Same for realloc.
 */

pointer
ckrealloc(struct shinstance *psh, pointer p, size_t nbytes)
{
	p = sh_realloc(psh, p, nbytes);
	if (p == NULL)
		error(psh, "Out of space");
	return p;
}


/*
 * Make a copy of a string in safe storage.
 */

char *
savestr(struct shinstance *psh, const char *s)
{
	char *p;
	size_t len = strlen(s);

	p = ckmalloc(psh, len + 1);
	memcpy(p, s, len + 1);
	return p;
}


/*
 * Parse trees for commands are allocated in lifo order, so we use a stack
 * to make this more efficient, and also to avoid all sorts of exception
 * handling code to handle interrupts in the middle of a parse.
 *
 * The size 504 was chosen because the Ultrix malloc handles that size
 * well.
 */

//#define MINSIZE 504		/* minimum size of a block */

//struct stack_block {
//	struct stack_block *prev;
//	char space[MINSIZE];
//};

//struct stack_block stackbase;
//struct stack_block *stackp = &stackbase;
//struct stackmark *markp;
//char *stacknxt = stackbase.space;
//int stacknleft = MINSIZE;
//int sstrnleft;
//int herefd = -1;

pointer
stalloc(shinstance *psh, size_t nbytes)
{
	char *p;

	nbytes = SHELL_ALIGN(nbytes);
	if (nbytes > (size_t)psh->stacknleft || psh->stacknleft < 0) {
		size_t blocksize;
		struct stack_block *sp;

		blocksize = nbytes;
		if (blocksize < MINSIZE)
			blocksize = MINSIZE;
		INTOFF;
		sp = ckmalloc(psh, sizeof(struct stack_block) - MINSIZE + blocksize);
		sp->prev = psh->stackp;
		psh->stacknxt = sp->space;
		psh->stacknleft = (int)blocksize;
		psh->stackp = sp;
		INTON;
	}
	p = psh->stacknxt;
	psh->stacknxt += nbytes;
	psh->stacknleft -= (int)nbytes;
	return p;
}


char *
stsavestr(struct shinstance *psh, const char *src)
{
	if (src) {
		size_t size = strlen(src) + 1;
		char *dst = (char *)stalloc(psh, size);
		return (char *)memcpy(dst, src, size);
	}
	return NULL;
}


void
stunalloc(shinstance *psh, pointer p)
{
	if (p == NULL) {		/*DEBUG */
		shfile_write(&psh->fdtab, 2, "stunalloc\n", 10);
		sh_abort(psh);
	}
	psh->stacknleft += (int)(psh->stacknxt - (char *)p);
	psh->stacknxt = p;
}



void
setstackmark(shinstance *psh, struct stackmark *mark)
{
	mark->stackp = psh->stackp;
	mark->stacknxt = psh->stacknxt;
	mark->stacknleft = psh->stacknleft;
	mark->marknext = psh->markp;
#ifdef KASH_SEPARATE_PARSER_ALLOCATOR
	mark->pstacksize = psh->pstacksize;
#endif
	psh->markp = mark;
}


void
popstackmark(shinstance *psh, struct stackmark *mark)
{
	struct stack_block *sp;

	INTOFF;
	psh->markp = mark->marknext;
	while (psh->stackp != mark->stackp) {
		sp = psh->stackp;
		psh->stackp = sp->prev;
		ckfree(psh, sp);
	}
	psh->stacknxt = mark->stacknxt;
	psh->stacknleft = mark->stacknleft;

#ifdef KASH_SEPARATE_PARSER_ALLOCATOR
	pstackpop(psh, mark->pstacksize);
#endif
	INTON;
}


/*
 * When the parser reads in a string, it wants to stick the string on the
 * stack and only adjust the stack pointer when it knows how big the
 * string is.  Stackblock (defined in stack.h) returns a pointer to a block
 * of space on top of the stack and stackblocklen returns the length of
 * this block.  Growstackblock will grow this space by at least one byte,
 * possibly moving it (like realloc).  Grabstackblock actually allocates the
 * part of the block that has been used.
 */

void
growstackblock(shinstance *psh)
{
	int newlen = SHELL_ALIGN(psh->stacknleft * 2 + 100);

	if (psh->stacknxt == psh->stackp->space && psh->stackp != &psh->stackbase) {
		struct stack_block *oldstackp;
		struct stackmark *xmark;
		struct stack_block *sp;

		INTOFF;
		oldstackp = psh->stackp;
		sp = psh->stackp;
		psh->stackp = sp->prev;
		sp = ckrealloc(psh, (pointer)sp,
		    sizeof(struct stack_block) - MINSIZE + newlen);
		sp->prev = psh->stackp;
		psh->stackp = sp;
		psh->stacknxt = sp->space;
		psh->stacknleft = newlen;

		/*
		 * Stack marks pointing to the start of the old block
		 * must be relocated to point to the new block
		 */
		xmark = psh->markp;
		while (xmark != NULL && xmark->stackp == oldstackp) {
			xmark->stackp = psh->stackp;
			xmark->stacknxt = psh->stacknxt;
			xmark->stacknleft = psh->stacknleft;
			xmark = xmark->marknext;
		}
		INTON;
	} else {
		char *oldspace = psh->stacknxt;
		int oldlen = psh->stacknleft;
		char *p = stalloc(psh, newlen);

		(void)memcpy(p, oldspace, oldlen);
		psh->stacknxt = p;			/* free the space */
		psh->stacknleft += newlen;		/* we just allocated */
	}
}

void
grabstackblock(shinstance *psh, int len)
{
	len = SHELL_ALIGN(len);
	psh->stacknxt += len;
	psh->stacknleft -= len;
}

/*
 * The following routines are somewhat easier to use than the above.
 * The user declares a variable of type STACKSTR, which may be declared
 * to be a register.  The macro STARTSTACKSTR initializes things.  Then
 * the user uses the macro STPUTC to add characters to the string.  In
 * effect, STPUTC(psh, c, p) is the same as *p++ = c except that the stack is
 * grown as necessary.  When the user is done, she can just leave the
 * string there and refer to it using stackblock(psh).  Or she can allocate
 * the space for it using grabstackstr().  If it is necessary to allow
 * someone else to use the stack temporarily and then continue to grow
 * the string, the user should use grabstack to allocate the space, and
 * then call ungrabstr(p) to return to the previous mode of operation.
 *
 * USTPUTC is like STPUTC except that it doesn't check for overflow.
 * CHECKSTACKSPACE can be called before USTPUTC to ensure that there
 * is space for at least one character.
 */

char *
growstackstr(shinstance *psh)
{
	int len = stackblocksize(psh);
	if (psh->herefd >= 0 && len >= 1024) {
		xwrite(psh, psh->herefd, stackblock(psh), len);
		psh->sstrnleft = len - 1;
		return stackblock(psh);
	}
	growstackblock(psh);
	psh->sstrnleft = stackblocksize(psh) - len - 1;
	return stackblock(psh) + len;
}

/*
 * Called from CHECKSTRSPACE.
 */

char *
makestrspace(shinstance *psh)
{
	int len = stackblocksize(psh) - psh->sstrnleft;
	growstackblock(psh);
	psh->sstrnleft = stackblocksize(psh) - len;
	return stackblock(psh) + len;
}


/*
 * Got better control having a dedicated function for this.
 *
 * Was: #define grabstackstr(psh, p)   stalloc((psh), stackblocksize(psh) - (psh)->sstrnleft)
 */
char *
grabstackstr(shinstance *psh, char *end)
{
	char * const pstart = stackblock(psh);
	size_t nbytes = (size_t)(end - pstart);

	kHlpAssert((uintptr_t)end >= (uintptr_t)pstart);
	/*kHlpAssert(end[-1] == '\0'); - not if it's followed by ungrabstrackstr(), sigh. */
	kHlpAssert(SHELL_ALIGN((uintptr_t)pstart) == (uintptr_t)pstart);
	kHlpAssert(stackblocksize(psh) - psh->sstrnleft >= (ssize_t)nbytes);

	nbytes = SHELL_ALIGN(nbytes);
	psh->stacknxt += nbytes;
	psh->stacknleft -= (int)nbytes;
	kHlpAssert(psh->stacknleft >= 0);

	return pstart;
}

void
ungrabstackstr(shinstance *psh, char *s, char *p)
{
	kHlpAssert((size_t)(psh->stacknxt - p) <= SHELL_SIZE);
	kHlpAssert((uintptr_t)s >= (uintptr_t)&psh->stackp->space[0]);
	kHlpAssert((uintptr_t)p >= (uintptr_t)s);

	psh->stacknleft += (int)(psh->stacknxt - s);
	psh->stacknxt = s;
	psh->sstrnleft = (int)(psh->stacknleft - (p - s));

}


/*
 * Parser stack allocator.
 */
#ifdef KASH_SEPARATE_PARSER_ALLOCATOR

unsigned pstackrelease(shinstance *psh, pstack_block *pst, const char *caller)
{
	unsigned refs;
	if (pst) {
		refs = sh_atomic_dec(&pst->refs);
		TRACE2((NULL, "pstackrelease: %p - %u refs (%s)\n", pst, refs, caller)); K_NOREF(caller);
		if (refs == 0) {
			struct stack_block *top;
			while ((top = pst->top) != &pst->first) {
				pst->top = top->prev;
				kHlpAssert(pst->top);
				top->prev = NULL;
				sh_free(psh, top);
			}
			pst->nextbyte = NULL;
			pst->top = NULL;

			if (!psh->freepstack)
				psh->freepstack = pst;
			else
				sh_free(psh, pst);
		}
	} else
		refs = 0;
	return refs;
}

void pstackpop(shinstance *psh, unsigned target)
{
	kHlpAssert(target <= psh->pstacksize);
	while (target < psh->pstacksize) {
		unsigned idx = --psh->pstacksize;
		pstack_block *pst = psh->pstack[idx];
		psh->pstack[idx] = NULL;
		if (psh->curpstack == pst) {
			pstack_block *pstnext;
			if (idx <= 0 || (pstnext = psh->pstack[idx - 1])->done)
				psh->curpstack = NULL;
			else
				psh->curpstack = pstnext;
		}
		pstackrelease(psh, pst, "popstackmark");
	}

# ifndef NDEBUG
	if (psh->curpstack) {
		unsigned i;
		for (i = 0; i < psh->pstacksize; i++)
			if (psh->curpstack == psh->pstack[i])
				break;
		kHlpAssert(i < psh->pstacksize);
	}
# endif
}


unsigned pstackretain(pstack_block *pst)
{
	unsigned refs = sh_atomic_inc(&pst->refs);
	kHlpAssert(refs > 1);
	kHlpAssert(refs < 256 /* bogus, but useful */);
	return refs;
}

K_INLINE void pstackpush(shinstance *psh, pstack_block *pst)
{
	unsigned i = psh->pstacksize;
	if (i + 1 < psh->pstackalloced) {
		/* likely, except for the first time */
	} else {
		psh->pstack = (pstack_block **)ckrealloc(psh, psh->pstack, sizeof(psh->pstack[0]) * (i + 32));
		memset(&psh->pstack[i], 0, sizeof(psh->pstack[0]) * 32);
	}
	psh->pstack[i] = pst;
	psh->pstacksize = i + 1;
}

/* Does not make it current! */
unsigned pstackretainpush(shinstance *psh, pstack_block *pst)
{
	unsigned refs = pstackretain(pst);
	pstackpush(psh, pst);
	TRACE2((psh, "pstackretainpush: %p - entry %u - %u refs\n", pst, psh->pstacksize - 1, refs));
	return refs;
}

pstack_block *pstackallocpush(shinstance *psh)
{
	size_t const blocksize = offsetof(pstack_block, first.space) + MINSIZE;
	pstack_block *pst;

	INTOFF;

	/*
	 * Allocate and initialize it.
	 */
	pst = psh->freepstack;
	if (pst)
		psh->freepstack = NULL;
	else
		pst = (pstack_block *)ckmalloc(psh, blocksize);
	pst->nextbyte          = &pst->first.space[0];
	pst->avail             = blocksize - offsetof(pstack_block, first.space);
	pst->topsize           = blocksize - offsetof(pstack_block, first.space);
	pst->strleft           = 0;
	pst->top               = &pst->first;
	pst->allocations       = 0;
	pst->bytesalloced      = 0;
	pst->nodesalloced      = 0;
	pst->entriesalloced    = 0;
	pst->strbytesalloced   = 0;
	pst->blocks            = 0;
	pst->fragmentation     = 0;
	pst->refs              = 1;
	pst->done              = K_FALSE;
	pst->first.prev        = NULL;

	/*
	 * Push it onto the stack and make it current.
	 */
	pstackpush(psh, pst);
	psh->curpstack = pst;

	INTON;
	TRACE2((psh, "pstackallocpush: %p - entry %u\n", pst, psh->pstacksize - 1));
	return pst;
}

/**
 * Marks the block as done, preventing it from being marked current again.
 */
void pstackmarkdone(pstack_block *pst)
{
	pst->done = K_TRUE;
}

/**
 * Allocates and pushes a new block onto the stack, min payload size @a nbytes.
 */
static void pstallocnewblock(shinstance *psh, pstack_block *pst, size_t nbytes)
{
	/* Allocate a new stack node. */
	struct stack_block *sp;
	size_t const blocksize = nbytes <= MINSIZE
	                       ? offsetof(struct stack_block, space) + MINSIZE
	                       : K_ALIGN_Z(nbytes + offsetof(struct stack_block, space), 1024);

	INTOFF;
	sp = ckmalloc(psh, blocksize);
	sp->prev = pst->top;
	pst->fragmentation += pst->avail;
	pst->topsize        = blocksize - offsetof(struct stack_block, space);
	pst->avail          = blocksize - offsetof(struct stack_block, space);
	pst->nextbyte       = sp->space;
	pst->top            = sp;
	pst->blocks        += 1;
	INTON;
}

/**
 * Tries to grow the current stack block to hold a minimum of @a nbytes,
 * will allocate a new block and copy over pending string bytes if that's not
 * possible.
 */
static void pstgrowblock(shinstance *psh, pstack_block *pst, size_t nbytes, size_t tocopy)
{
	struct stack_block *top = pst->top;
	size_t blocksize;

	kHlpAssert(pst->avail < nbytes); /* only called when we need more space */
	kHlpAssert(tocopy <= pst->avail);

	/* Double the size used thus far and add some fudge and alignment.  Make
	   sure to at least allocate MINSIZE. */
	blocksize = K_MAX(K_ALIGN_Z(pst->avail * 2 + 100 + offsetof(struct stack_block, space), 64), MINSIZE);

	/* If that isn't sufficient, do request size w/ some fudge and alignment. */
	if (blocksize < nbytes + offsetof(struct stack_block, space))
	    blocksize = K_ALIGN_Z(nbytes + offsetof(struct stack_block, space) + 100, 1024);

	/*
	 * Reallocate the current stack node if we can.
	 */
	if (   pst->nextbyte == &top->space[0] /* can't have anything else in the block */
	    && top->prev != NULL /* first block is embedded in pst and cannot be reallocated */ ) {
		top = (struct stack_block *)ckrealloc(psh, top, blocksize);
		pst->top      = top;
		pst->topsize  = blocksize - offsetof(struct stack_block, space);
		pst->avail    = blocksize - offsetof(struct stack_block, space);
		pst->nextbyte = top->space;
	}
	/*
	 * Otherwise allocate a new node and copy over the avail bytes
	 * from the old one.
	 */
	else {
		char const * const copysrc = pst->nextbyte;
		pstallocnewblock(psh, pst, nbytes);
		kHlpAssert(pst->avail >= nbytes);
		kHlpAssert(pst->avail >= tocopy);
		memcpy(pst->nextbyte, copysrc, tocopy);
	}
}

K_INLINE void *pstallocint(shinstance *psh, pstack_block *pst, size_t nbytes)
{
	void *ret;

	/*
	 * Align the size and make sure we've got sufficient bytes available:
	 */
	nbytes = SHELL_ALIGN(nbytes);
	if (pst->avail >= nbytes && (ssize_t)pst->avail >= 0) { /* likely*/ }
	else pstallocnewblock(psh, pst, nbytes);

	/*
	 * Carve out the return block.
	 */
	ret = pst->nextbyte;
	pst->nextbyte     += nbytes;
	pst->avail        -= nbytes;
	pst->bytesalloced += nbytes;
	pst->allocations  += 1;
	return ret;
}

#endif /* KASH_SEPARATE_PARSER_ALLOCATOR */


void *pstalloc(struct shinstance *psh, size_t nbytes)
{
#ifdef KASH_SEPARATE_PARSER_ALLOCATOR
	return pstallocint(psh, psh->curpstack, nbytes);
#else
	return stalloc(psh, nbytes);
#endif
}

union node *pstallocnode(struct shinstance *psh, size_t nbytes)
{
#ifdef KASH_SEPARATE_PARSER_ALLOCATOR
	pstack_block * const pst = psh->curpstack;
	union node * const ret = (union node *)pstallocint(psh, pst, nbytes);
	pst->nodesalloced++;
	ret->pblock = pst;
	return ret;
#else
	return (union node *)pstalloc(psh, nbytes);
#endif
}

struct nodelist *pstalloclist(struct shinstance *psh)
{
#ifdef KASH_SEPARATE_PARSER_ALLOCATOR
	pstack_block *pst = psh->curpstack;
	pst->entriesalloced++;
	return (struct nodelist *)pstallocint(psh, pst, sizeof(struct nodelist));
#endif
	return (struct nodelist *)pstalloc(psh, sizeof(struct nodelist));
}

char *pstsavestr(struct shinstance *psh, const char *str)
{
	if (str) {
		size_t const nbytes = strlen(str) + 1;
#ifdef KASH_SEPARATE_PARSER_ALLOCATOR
		pstack_block *pst = psh->curpstack;
		pst->strbytesalloced += SHELL_ALIGN(nbytes);
		return (char *)memcpy(pstallocint(psh, pst, nbytes), str, nbytes);
#else
		return (char *)memcpy(pstalloc(psh, nbytes), str, nbytes);
#endif
	}
	return NULL;
}

char *pstmakestrspace(struct shinstance *psh, size_t minbytes, char *end)
{
#ifdef KASH_SEPARATE_PARSER_ALLOCATOR
	pstack_block *pst = psh->curpstack;
	size_t const len = end - pst->nextbyte;

	kHlpAssert(pst->avail - pst->strleft == len);
	TRACE2((psh, "pstmakestrspace: len=%u minbytes=%u (=> %u)\n", len, minbytes, len + minbytes));

	pstgrowblock(psh, pst, minbytes + len, len);

	pst->strleft = pst->avail - len;
	return pst->nextbyte + len;

#else
	size_t const len = end - stackblock(psh);

	kHlpAssert(stackblocksize(psh) - psh->sstrnleft == len);
	TRACE2((psh, "pstmakestrspace: len=%u minbytes=%u (=> %u)\n", len, minbytes, len + minbytes));

	minbytes += len;
	while (stackblocksize(psh) < minbytes)
		growstackblock(psh);

	psh->sstrnleft = (int)(stackblocksize(psh) - len);
	return (char *)stackblock(psh) + len;
#endif
}

/* PSTPUTC helper */
char *pstputcgrow(shinstance *psh, char *end, char c)
{
#ifdef KASH_SEPARATE_PARSER_ALLOCATOR
	pstack_block *pst = psh->curpstack;
	pst->strleft++; 	/* PSTPUTC() already incremented it. */
	end = pstmakestrspace(psh, 1, end);
	kHlpAssert(pst->strleft > 0);
	pst->strleft--;
#else
	psh->sstrnleft++; 	/* PSTPUTC() already incremented it. */
	end = pstmakestrspace(psh, 1, end);
	kHlpAssert(psh->sstrnleft > 0);
	psh->sstrnleft--;
#endif
	*end++ = c;
	return end;
}


char *pstgrabstr(struct shinstance *psh, char *end)
{
#ifdef KASH_SEPARATE_PARSER_ALLOCATOR
	pstack_block *pst = psh->curpstack;
	char * const pstart = pst->nextbyte;
	size_t nbytes = (size_t)(end - pstart);

	kHlpAssert((uintptr_t)end > (uintptr_t)pstart);
	kHlpAssert(end[-1] == '\0');
	kHlpAssert(SHELL_ALIGN((uintptr_t)pstart) == (uintptr_t)pstart);
	kHlpAssert(pst->avail - pst->strleft >= nbytes);

	nbytes = SHELL_ALIGN(nbytes); /** @todo don't align strings, align the other allocations. */
	pst->nextbyte += nbytes;
	pst->avail    -= nbytes;
	pst->strbytesalloced += nbytes;

	return pstart;

#else
	char * const pstart = stackblock(psh);
	size_t nbytes = (size_t)(end - pstart);

	kHlpAssert((uintptr_t)end > (uintptr_t)pstart);
	kHlpAssert(end[-1] == '\0');
	kHlpAssert(SHELL_ALIGN((uintptr_t)pstart) == (uintptr_t)pstart);
	kHlpAssert(stackblocksize(psh) - psh->sstrnleft >= nbytes);

	nbytes = SHELL_ALIGN(nbytes); /** @todo don't align strings, align the other allocations. */
	psh->stacknxt += nbytes;
	psh->stacknleft -= (int)nbytes;

	return pstart;
#endif
}

