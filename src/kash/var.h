/*	$NetBSD: var.h,v 1.23 2004/10/02 12:16:53 dsl Exp $	*/

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
 *	@(#)var.h	8.2 (Berkeley) 5/4/95
 */

#ifndef ___var_h___
#define ___var_h___

/*
 * Shell variables.
 */

/* flags */
#define VEXPORT		0x01	/* variable is exported */
#define VREADONLY	0x02	/* variable cannot be modified */
#define VSTRFIXED	0x04	/* variable struct is statically allocated */
#define VTEXTFIXED	0x08	/* text is statically allocated */
#define VSTACK		0x10	/* text is allocated on the stack */
#define VUNSET		0x20	/* the variable is not set */
#define VNOFUNC		0x40	/* don't call the callback function */
#define VNOSET		0x80	/* do not set variable - just readonly test */
#ifdef PC_OS2_LIBPATHS
#define VOS2LIBPATH     0x8000  /* OS/2 LIBPATH related variable. */
#endif


struct var {
	struct var *next;		/* next entry in hash list */
	int flags;			/* flags are defined above */
	char *text;			/* name=value */
	int name_len;			/* length of name */
	void (*func)(const char *);
					/* function to be called when  */
					/* the variable gets set/unset */
};


struct localvar {
	struct localvar *next;		/* next local variable in list */
	struct var *vp;			/* the variable that was made local */
	int flags;			/* saved flags */
	char *text;			/* saved text */
};

/*
struct localvar *localvars;

#if ATTY
extern struct var vatty;
#endif
extern struct var vifs;
extern struct var vmail;
extern struct var vmpath;
extern struct var vpath;
#ifdef _MSC_VER
extern struct var vpath2;
#endif
extern struct var vps1;
extern struct var vps2;
extern struct var vps4;
#ifndef SMALL
extern struct var vterm;
extern struct var vtermcap;
extern struct var vhistsize;
#endif
*/

/*
 * The following macros access the values of the above variables.
 * They have to skip over the name.  They return the null string
 * for unset variables.
 */

#define ifsval(psh)	((psh)->vifs.text + 4)
#define ifsset(psh)	(((psh)->vifs.flags & VUNSET) == 0)
#define mailval(psh)	((psh)->vmail.text + 5)
#define mpathval(psh)	((psh)->vmpath.text + 9)
#ifdef _MSC_VER
#define pathval(psh)	((psh)->vpath.text[5] ? &(psh)->vpath.text[5] : &(psh)->vpath2.text[5])
#else
#define pathval(psh)	((psh)->vpath.text + 5)
#endif
#define ps1val(psh)	((psh)->vps1.text + 4)
#define ps2val(psh)	((psh)->vps2.text + 4)
#define ps4val(psh)	((psh)->vps4.text + 4)
#define optindval(psh)	((psh)->voptind.text + 7)
#ifndef SMALL
#define histsizeval(psh) ((psh)->vhistsize.text + 9)
#define termval(psh)	((psh)->vterm.text + 5)
#endif

#if ATTY
#define attyset(psh)	(((psh)->vatty.flags & VUNSET) == 0)
#endif
#define mpathset(psh)	(((psh)->vmpath.flags & VUNSET) == 0)

void initvar(struct shinstance *);
void setvar(struct shinstance *, const char *, const char *, int);
void setvareq(struct shinstance *, char *, int);
struct strlist;
void listsetvar(struct shinstance *, struct strlist *, int);
char *lookupvar(struct shinstance *, const char *);
char *bltinlookup(struct shinstance *, const char *, int);
char **environment(struct shinstance *);
void shprocvar(struct shinstance *);
int showvars(struct shinstance *, const char *, int, int);
int exportcmd(struct shinstance *, int, char **);
int localcmd(struct shinstance *, int, char **);
void mklocal(struct shinstance *, const char *, int);
void listmklocal(struct shinstance *, struct strlist *, int);
void poplocalvars(struct shinstance *);
int setvarcmd(struct shinstance *, int, char **);
int unsetcmd(struct shinstance *, int, char **);
int unsetvar(struct shinstance *, const char *, int);
int setvarsafe(struct shinstance *, const char *, const char *, int);
void print_quoted(struct shinstance *, const char *);

#endif
