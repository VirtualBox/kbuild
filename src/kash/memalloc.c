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

#include "shell.h"
#include "output.h"
#include "memalloc.h"
#include "error.h"
#include "machdep.h"
#include "mystring.h"
#include "shinstance.h"

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

void
ungrabstackstr(shinstance *psh, char *s, char *p)
{
	psh->stacknleft += (int)(psh->stacknxt - s);
	psh->stacknxt = s;
	psh->sstrnleft = (int)(psh->stacknleft - (p - s));

}
