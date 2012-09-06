/*	$NetBSD: parser.c,v 1.59 2005/03/21 20:10:29 dsl Exp $	*/

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
static char sccsid[] = "@(#)parser.c	8.7 (Berkeley) 5/16/95";
#else
__RCSID("$NetBSD: parser.c,v 1.59 2005/03/21 20:10:29 dsl Exp $");
#endif /* not lint */
#endif

#include <stdlib.h>

#include "shell.h"
#include "parser.h"
#include "nodes.h"
#include "expand.h"	/* defines rmescapes() */
#include "eval.h"	/* defines commandname */
#include "redir.h"	/* defines copyfd() */
#include "syntax.h"
#include "options.h"
#include "input.h"
#include "output.h"
#include "var.h"
#include "error.h"
#include "memalloc.h"
#include "mystring.h"
#include "alias.h"
#include "show.h"
#ifndef SMALL
# include "myhistedit.h"
#endif
#include "cd.h"
#include "shinstance.h"

/*
 * Shell command parser.
 */

#define EOFMARKLEN 79

/* values returned by readtoken */
#include "token.h"

#define OPENBRACE '{'
#define CLOSEBRACE '}'


struct heredoc {
	struct heredoc *next;	/* next here document in list */
	union node *here;		/* redirection node */
	char *eofmark;		/* string indicating end of input */
	int striptabs;		/* if set, strip leading tabs */
};



//static int noalias = 0;		/* when set, don't handle aliases */
//struct heredoc *heredoclist;	/* list of here documents to read */
//int parsebackquote;		/* nonzero if we are inside backquotes */
//int doprompt;			/* if set, prompt the user */
//int needprompt;			/* true if interactive and at start of line */
//int lasttoken;			/* last token read */
//MKINIT int tokpushback;		/* last token pushed back */
//char *wordtext;			/* text of last word returned by readtoken */
//MKINIT int checkkwd;            /* 1 == check for kwds, 2 == also eat newlines */
//struct nodelist *backquotelist;
//union node *redirnode;
//struct heredoc *heredoc;
//int quoteflag;			/* set if (part of) last token was quoted */
//int startlinno;			/* line # where last token started */


STATIC union node *list(shinstance *, int);
STATIC union node *andor(shinstance *);
STATIC union node *pipeline(shinstance *);
STATIC union node *command(shinstance *);
STATIC union node *simplecmd(shinstance *, union node **, union node *);
STATIC union node *makename(shinstance *);
STATIC void parsefname(shinstance *);
STATIC void parseheredoc(shinstance *);
STATIC int peektoken(shinstance *);
STATIC int readtoken(shinstance *);
STATIC int xxreadtoken(shinstance *);
STATIC int readtoken1(shinstance *, int, char const *, char *, int);
STATIC int noexpand(shinstance *, char *);
SH_NORETURN_1 STATIC void synexpect(shinstance *, int) SH_NORETURN_2;
SH_NORETURN_1 STATIC void synerror(shinstance *, const char *) SH_NORETURN_2;
STATIC void setprompt(shinstance *, int);


/*
 * Read and parse a command.  Returns NEOF on end of file.  (NULL is a
 * valid parse tree indicating a blank line.)
 */

union node *
parsecmd(shinstance *psh, int interact)
{
	int t;

	psh->tokpushback = 0;
	psh->doprompt = interact;
	if (psh->doprompt)
		setprompt(psh, 1);
	else
		setprompt(psh, 0);
	psh->needprompt = 0;
	t = readtoken(psh);
	if (t == TEOF)
		return NEOF;
	if (t == TNL)
		return NULL;
	psh->tokpushback++;
	return list(psh, 1);
}


STATIC union node *
list(shinstance *psh, int nlflag)
{
	union node *n1, *n2, *n3;
	int tok;

	psh->checkkwd = 2;
	if (nlflag == 0 && tokendlist[peektoken(psh)])
		return NULL;
	n1 = NULL;
	for (;;) {
		n2 = andor(psh);
		tok = readtoken(psh);
		if (tok == TBACKGND) {
			if (n2->type == NCMD || n2->type == NPIPE) {
				n2->ncmd.backgnd = 1;
			} else if (n2->type == NREDIR) {
				n2->type = NBACKGND;
			} else {
				n3 = (union node *)stalloc(psh, sizeof (struct nredir));
				n3->type = NBACKGND;
				n3->nredir.n = n2;
				n3->nredir.redirect = NULL;
				n2 = n3;
			}
		}
		if (n1 == NULL) {
			n1 = n2;
		}
		else {
			n3 = (union node *)stalloc(psh, sizeof (struct nbinary));
			n3->type = NSEMI;
			n3->nbinary.ch1 = n1;
			n3->nbinary.ch2 = n2;
			n1 = n3;
		}
		switch (tok) {
		case TBACKGND:
		case TSEMI:
			tok = readtoken(psh);
			/* fall through */
		case TNL:
			if (tok == TNL) {
				parseheredoc(psh);
				if (nlflag)
					return n1;
			} else {
				psh->tokpushback++;
			}
			psh->checkkwd = 2;
			if (tokendlist[peektoken(psh)])
				return n1;
			break;
		case TEOF:
			if (psh->heredoclist)
				parseheredoc(psh);
			else
				pungetc(psh);		/* push back EOF on input */
			return n1;
		default:
			if (nlflag)
				synexpect(psh, -1);
			psh->tokpushback++;
			return n1;
		}
	}
}



STATIC union node *
andor(shinstance *psh)
{
	union node *n1, *n2, *n3;
	int t;

	n1 = pipeline(psh);
	for (;;) {
		if ((t = readtoken(psh)) == TAND) {
			t = NAND;
		} else if (t == TOR) {
			t = NOR;
		} else {
			psh->tokpushback++;
			return n1;
		}
		n2 = pipeline(psh);
		n3 = (union node *)stalloc(psh, sizeof (struct nbinary));
		n3->type = t;
		n3->nbinary.ch1 = n1;
		n3->nbinary.ch2 = n2;
		n1 = n3;
	}
}



STATIC union node *
pipeline(shinstance *psh)
{
	union node *n1, *n2, *pipenode;
	struct nodelist *lp, *prev;
	int negate;

	negate = 0;
	TRACE((psh, "pipeline: entered\n"));
	while (readtoken(psh) == TNOT)
		negate = !negate;
	psh->tokpushback++;
	n1 = command(psh);
	if (readtoken(psh) == TPIPE) {
		pipenode = (union node *)stalloc(psh, sizeof (struct npipe));
		pipenode->type = NPIPE;
		pipenode->npipe.backgnd = 0;
		lp = (struct nodelist *)stalloc(psh, sizeof (struct nodelist));
		pipenode->npipe.cmdlist = lp;
		lp->n = n1;
		do {
			prev = lp;
			lp = (struct nodelist *)stalloc(psh, sizeof (struct nodelist));
			lp->n = command(psh);
			prev->next = lp;
		} while (readtoken(psh) == TPIPE);
		lp->next = NULL;
		n1 = pipenode;
	}
	psh->tokpushback++;
	if (negate) {
		n2 = (union node *)stalloc(psh, sizeof (struct nnot));
		n2->type = NNOT;
		n2->nnot.com = n1;
		return n2;
	} else
		return n1;
}



STATIC union node *
command(shinstance *psh)
{
	union node *n1, *n2;
	union node *ap, **app;
	union node *cp, **cpp;
	union node *redir, **rpp;
	int t, negate = 0;

	psh->checkkwd = 2;
	redir = NULL;
	n1 = NULL;
	rpp = &redir;

	/* Check for redirection which may precede command */
	while (readtoken(psh) == TREDIR) {
		*rpp = n2 = psh->redirnode;
		rpp = &n2->nfile.next;
		parsefname(psh);
	}
	psh->tokpushback++;

	while (readtoken(psh) == TNOT) {
		TRACE((psh, "command: TNOT recognized\n"));
		negate = !negate;
	}
	psh->tokpushback++;

	switch (readtoken(psh)) {
	case TIF:
		n1 = (union node *)stalloc(psh, sizeof (struct nif));
		n1->type = NIF;
		n1->nif.test = list(psh, 0);
		if (readtoken(psh) != TTHEN)
			synexpect(psh, TTHEN);
		n1->nif.ifpart = list(psh, 0);
		n2 = n1;
		while (readtoken(psh) == TELIF) {
			n2->nif.elsepart = (union node *)stalloc(psh, sizeof (struct nif));
			n2 = n2->nif.elsepart;
			n2->type = NIF;
			n2->nif.test = list(psh, 0);
			if (readtoken(psh) != TTHEN)
				synexpect(psh, TTHEN);
			n2->nif.ifpart = list(psh, 0);
		}
		if (psh->lasttoken == TELSE)
			n2->nif.elsepart = list(psh, 0);
		else {
			n2->nif.elsepart = NULL;
			psh->tokpushback++;
		}
		if (readtoken(psh) != TFI)
			synexpect(psh, TFI);
		psh->checkkwd = 1;
		break;
	case TWHILE:
	case TUNTIL: {
		int got;
		n1 = (union node *)stalloc(psh, sizeof (struct nbinary));
		n1->type = (psh->lasttoken == TWHILE)? NWHILE : NUNTIL;
		n1->nbinary.ch1 = list(psh, 0);
		if ((got=readtoken(psh)) != TDO) {
TRACE((psh, "expecting DO got %s %s\n", tokname[got], got == TWORD ? psh->wordtext : ""));
			synexpect(psh, TDO);
		}
		n1->nbinary.ch2 = list(psh, 0);
		if (readtoken(psh) != TDONE)
			synexpect(psh, TDONE);
		psh->checkkwd = 1;
		break;
	}
	case TFOR:
		if (readtoken(psh) != TWORD || psh->quoteflag || ! goodname(psh->wordtext))
			synerror(psh, "Bad for loop variable");
		n1 = (union node *)stalloc(psh, sizeof (struct nfor));
		n1->type = NFOR;
		n1->nfor.var = psh->wordtext;
		if (readtoken(psh) == TWORD && ! psh->quoteflag && equal(psh->wordtext, "in")) {
			app = &ap;
			while (readtoken(psh) == TWORD) {
				n2 = (union node *)stalloc(psh, sizeof (struct narg));
				n2->type = NARG;
				n2->narg.text = psh->wordtext;
				n2->narg.backquote = psh->backquotelist;
				*app = n2;
				app = &n2->narg.next;
			}
			*app = NULL;
			n1->nfor.args = ap;
			if (psh->lasttoken != TNL && psh->lasttoken != TSEMI)
				synexpect(psh, -1);
		} else {
			static char argvars[5] = {CTLVAR, VSNORMAL|VSQUOTE,
								   '@', '=', '\0'};
			n2 = (union node *)stalloc(psh, sizeof (struct narg));
			n2->type = NARG;
			n2->narg.text = argvars;
			n2->narg.backquote = NULL;
			n2->narg.next = NULL;
			n1->nfor.args = n2;
			/*
			 * Newline or semicolon here is optional (but note
			 * that the original Bourne shell only allowed NL).
			 */
			if (psh->lasttoken != TNL && psh->lasttoken != TSEMI)
				psh->tokpushback++;
		}
		psh->checkkwd = 2;
		if ((t = readtoken(psh)) == TDO)
			t = TDONE;
		else if (t == TBEGIN)
			t = TEND;
		else
			synexpect(psh, -1);
		n1->nfor.body = list(psh, 0);
		if (readtoken(psh) != t)
			synexpect(psh, t);
		psh->checkkwd = 1;
		break;
	case TCASE:
		n1 = (union node *)stalloc(psh, sizeof (struct ncase));
		n1->type = NCASE;
		if (readtoken(psh) != TWORD)
			synexpect(psh, TWORD);
		n1->ncase.expr = n2 = (union node *)stalloc(psh, sizeof (struct narg));
		n2->type = NARG;
		n2->narg.text = psh->wordtext;
		n2->narg.backquote = psh->backquotelist;
		n2->narg.next = NULL;
		while (readtoken(psh) == TNL);
		if (psh->lasttoken != TWORD || ! equal(psh->wordtext, "in"))
			synerror(psh, "expecting \"in\"");
		cpp = &n1->ncase.cases;
		psh->noalias = 1;
		psh->checkkwd = 2, readtoken(psh);
		do {
			*cpp = cp = (union node *)stalloc(psh, sizeof (struct nclist));
			cp->type = NCLIST;
			app = &cp->nclist.pattern;
			for (;;) {
				*app = ap = (union node *)stalloc(psh, sizeof (struct narg));
				ap->type = NARG;
				ap->narg.text = psh->wordtext;
				ap->narg.backquote = psh->backquotelist;
				if (psh->checkkwd = 2, readtoken(psh) != TPIPE)
					break;
				app = &ap->narg.next;
				readtoken(psh);
			}
			ap->narg.next = NULL;
			psh->noalias = 0;
			if (psh->lasttoken != TRP) {
				synexpect(psh, TRP);
			}
			cp->nclist.body = list(psh, 0);

			psh->checkkwd = 2;
			if ((t = readtoken(psh)) != TESAC) {
				if (t != TENDCASE) {
					psh->noalias = 0;
					synexpect(psh, TENDCASE);
				} else {
					psh->noalias = 1;
					psh->checkkwd = 2;
					readtoken(psh);
				}
			}
			cpp = &cp->nclist.next;
		} while(psh->lasttoken != TESAC);
		psh->noalias = 0;
		*cpp = NULL;
		psh->checkkwd = 1;
		break;
	case TLP:
		n1 = (union node *)stalloc(psh, sizeof (struct nredir));
		n1->type = NSUBSHELL;
		n1->nredir.n = list(psh, 0);
		n1->nredir.redirect = NULL;
		if (readtoken(psh) != TRP)
			synexpect(psh, TRP);
		psh->checkkwd = 1;
		break;
	case TBEGIN:
		n1 = list(psh, 0);
		if (readtoken(psh) != TEND)
			synexpect(psh, TEND);
		psh->checkkwd = 1;
		break;
	/* Handle an empty command like other simple commands.  */
	case TSEMI:
		/*
		 * An empty command before a ; doesn't make much sense, and
		 * should certainly be disallowed in the case of `if ;'.
		 */
		if (!redir)
			synexpect(psh, -1);
	case TAND:
	case TOR:
	case TNL:
	case TEOF:
	case TWORD:
	case TRP:
		psh->tokpushback++;
		n1 = simplecmd(psh, rpp, redir);
		goto checkneg;
	default:
		synexpect(psh, -1);
		/* NOTREACHED */
	}

	/* Now check for redirection which may follow command */
	while (readtoken(psh) == TREDIR) {
		*rpp = n2 = psh->redirnode;
		rpp = &n2->nfile.next;
		parsefname(psh);
	}
	psh->tokpushback++;
	*rpp = NULL;
	if (redir) {
		if (n1->type != NSUBSHELL) {
			n2 = (union node *)stalloc(psh, sizeof (struct nredir));
			n2->type = NREDIR;
			n2->nredir.n = n1;
			n1 = n2;
		}
		n1->nredir.redirect = redir;
	}

checkneg:
	if (negate) {
		n2 = (union node *)stalloc(psh, sizeof (struct nnot));
		n2->type = NNOT;
		n2->nnot.com = n1;
		return n2;
	}
	else
		return n1;
}


STATIC union node *
simplecmd(shinstance *psh, union node **rpp, union node *redir)
{
	union node *args, **app;
	union node **orig_rpp = rpp;
	union node *n = NULL, *n2;
	int negate = 0;

	/* If we don't have any redirections already, then we must reset */
	/* rpp to be the address of the local redir variable.  */
	if (redir == 0)
		rpp = &redir;

	args = NULL;
	app = &args;
	/*
	 * We save the incoming value, because we need this for shell
	 * functions.  There can not be a redirect or an argument between
	 * the function name and the open parenthesis.
	 */
	orig_rpp = rpp;

	while (readtoken(psh) == TNOT) {
		TRACE((psh, "command: TNOT recognized\n"));
		negate = !negate;
	}
	psh->tokpushback++;

	for (;;) {
		if (readtoken(psh) == TWORD) {
			n = (union node *)stalloc(psh, sizeof (struct narg));
			n->type = NARG;
			n->narg.text = psh->wordtext;
			n->narg.backquote = psh->backquotelist;
			*app = n;
			app = &n->narg.next;
		} else if (psh->lasttoken == TREDIR) {
			*rpp = n = psh->redirnode;
			rpp = &n->nfile.next;
			parsefname(psh);	/* read name of redirection file */
		} else if (psh->lasttoken == TLP && app == &args->narg.next
					    && rpp == orig_rpp) {
			/* We have a function */
			if (readtoken(psh) != TRP)
				synexpect(psh, TRP);
#ifdef notdef
			if (! goodname(n->narg.text))
				synerror(psh, "Bad function name");
#endif
			n->type = NDEFUN;
			n->narg.next = command(psh);
			goto checkneg;
		} else {
			psh->tokpushback++;
			break;
		}
	}
	*app = NULL;
	*rpp = NULL;
	n = (union node *)stalloc(psh, sizeof (struct ncmd));
	n->type = NCMD;
	n->ncmd.backgnd = 0;
	n->ncmd.args = args;
	n->ncmd.redirect = redir;

checkneg:
	if (negate) {
		n2 = (union node *)stalloc(psh, sizeof (struct nnot));
		n2->type = NNOT;
		n2->nnot.com = n;
		return n2;
	}
	else
		return n;
}

STATIC union node *
makename(shinstance *psh)
{
	union node *n;

	n = (union node *)stalloc(psh, sizeof (struct narg));
	n->type = NARG;
	n->narg.next = NULL;
	n->narg.text = psh->wordtext;
	n->narg.backquote = psh->backquotelist;
	return n;
}

void fixredir(shinstance *psh, union node *n, const char *text, int err)
	{
	TRACE((psh, "Fix redir %s %d\n", text, err));
	if (!err)
		n->ndup.vname = NULL;

	if (is_digit(text[0]) && text[1] == '\0')
		n->ndup.dupfd = digit_val(text[0]);
	else if (text[0] == '-' && text[1] == '\0')
		n->ndup.dupfd = -1;
	else {

		if (err)
			synerror(psh, "Bad fd number");
		else
			n->ndup.vname = makename(psh);
	}
}


STATIC void
parsefname(shinstance *psh)
{
	union node *n = psh->redirnode;

	if (readtoken(psh) != TWORD)
		synexpect(psh, -1);
	if (n->type == NHERE) {
		struct heredoc *here = psh->heredoc;
		struct heredoc *p;
		size_t i;

		if (psh->quoteflag == 0)
			n->type = NXHERE;
		TRACE((psh, "Here document %d\n", n->type));
		if (here->striptabs) {
			while (*psh->wordtext == '\t')
				psh->wordtext++;
		}
		if (! noexpand(psh, psh->wordtext) || (i = strlen(psh->wordtext)) == 0 || i > EOFMARKLEN)
			synerror(psh, "Illegal eof marker for << redirection");
		rmescapes(psh, psh->wordtext);
		here->eofmark = psh->wordtext;
		here->next = NULL;
		if (psh->heredoclist == NULL)
			psh->heredoclist = here;
		else {
			for (p = psh->heredoclist ; p->next ; p = p->next);
			p->next = here;
		}
	} else if (n->type == NTOFD || n->type == NFROMFD) {
		fixredir(psh, n, psh->wordtext, 0);
	} else {
		n->nfile.fname = makename(psh);
	}
}


/*
 * Input any here documents.
 */

STATIC void
parseheredoc(shinstance *psh)
{
	struct heredoc *here;
	union node *n;

	while (psh->heredoclist) {
		here = psh->heredoclist;
		psh->heredoclist = here->next;
		if (psh->needprompt) {
			setprompt(psh, 2);
			psh->needprompt = 0;
		}
		readtoken1(psh, pgetc(psh), here->here->type == NHERE? SQSYNTAX : DQSYNTAX,
				here->eofmark, here->striptabs);
		n = (union node *)stalloc(psh, sizeof (struct narg));
		n->narg.type = NARG;
		n->narg.next = NULL;
		n->narg.text = psh->wordtext;
		n->narg.backquote = psh->backquotelist;
		here->here->nhere.doc = n;
	}
}

STATIC int
peektoken(shinstance *psh)
{
	int t;

	t = readtoken(psh);
	psh->tokpushback++;
	return (t);
}

STATIC int
readtoken(shinstance *psh)
{
	int t;
	int savecheckkwd = psh->checkkwd;
#ifdef DEBUG
	int alreadyseen = psh->tokpushback;
#endif
	struct alias *ap;

	top:
	t = xxreadtoken(psh);

	if (psh->checkkwd) {
		/*
		 * eat newlines
		 */
		if (psh->checkkwd == 2) {
			psh->checkkwd = 0;
			while (t == TNL) {
				parseheredoc(psh);
				t = xxreadtoken(psh);
			}
		} else
			psh->checkkwd = 0;
		/*
		 * check for keywords and aliases
		 */
		if (t == TWORD && !psh->quoteflag)
		{
			const char *const *pp;

			for (pp = parsekwd; *pp; pp++) {
				if (**pp == *psh->wordtext && equal(*pp, psh->wordtext))
				{
					psh->lasttoken = t = (int)(pp -
					    parsekwd + KWDOFFSET);
					TRACE((psh, "keyword %s recognized\n", tokname[t]));
					goto out;
				}
			}
			if(!psh->noalias &&
			    (ap = lookupalias(psh, psh->wordtext, 1)) != NULL) {
				pushstring(psh, ap->val, strlen(ap->val), ap);
				psh->checkkwd = savecheckkwd;
				goto top;
			}
		}
out:
		psh->checkkwd = (t == TNOT) ? savecheckkwd : 0;
	}
#ifdef DEBUG
	if (!alreadyseen)
	    TRACE((psh, "token %s %s\n", tokname[t], t == TWORD ? psh->wordtext : ""));
	else
	    TRACE((psh, "reread token %s \"%s\"\n", tokname[t], t == TWORD ? psh->wordtext : ""));
#endif
	return (t);
}


/*
 * Read the next input token.
 * If the token is a word, we set psh->backquotelist to the list of cmds in
 *	backquotes.  We set psh->quoteflag to true if any part of the word was
 *	quoted.
 * If the token is TREDIR, then we set psh->redirnode to a structure containing
 *	the redirection.
 * In all cases, the variable psh->startlinno is set to the number of the line
 *	on which the token starts.
 *
 * [Change comment:  here documents and internal procedures]
 * [Readtoken shouldn't have any arguments.  Perhaps we should make the
 *  word parsing code into a separate routine.  In this case, readtoken
 *  doesn't need to have any internal procedures, but parseword does.
 *  We could also make parseoperator in essence the main routine, and
 *  have parseword (readtoken1?) handle both words and redirection.]
 */

#define RETURN(token)	return psh->lasttoken = token

STATIC int
xxreadtoken(shinstance *psh)
{
	int c;

	if (psh->tokpushback) {
		psh->tokpushback = 0;
		return psh->lasttoken;
	}
	if (psh->needprompt) {
		setprompt(psh, 2);
		psh->needprompt = 0;
	}
	psh->startlinno = psh->plinno;
	for (;;) {	/* until token or start of word found */
		c = pgetc_macro(psh);
		if (c == ' ' || c == '\t')
			continue;		/* quick check for white space first */
		switch (c) {
		case ' ': case '\t':
			continue;
		case '#':
			while ((c = pgetc(psh)) != '\n' && c != PEOF);
			pungetc(psh);
			continue;
		case '\\':
			if (pgetc(psh) == '\n') {
				psh->startlinno = ++psh->plinno;
				if (psh->doprompt)
					setprompt(psh, 2);
				else
					setprompt(psh, 0);
				continue;
			}
			pungetc(psh);
			goto breakloop;
		case '\n':
			psh->plinno++;
			psh->needprompt = psh->doprompt;
			RETURN(TNL);
		case PEOF:
			RETURN(TEOF);
		case '&':
			if (pgetc(psh) == '&')
				RETURN(TAND);
			pungetc(psh);
			RETURN(TBACKGND);
		case '|':
			if (pgetc(psh) == '|')
				RETURN(TOR);
			pungetc(psh);
			RETURN(TPIPE);
		case ';':
			if (pgetc(psh) == ';')
				RETURN(TENDCASE);
			pungetc(psh);
			RETURN(TSEMI);
		case '(':
			RETURN(TLP);
		case ')':
			RETURN(TRP);
		default:
			goto breakloop;
		}
	}
breakloop:
	return readtoken1(psh, c, BASESYNTAX, (char *)NULL, 0);
#undef RETURN
}



/*
 * If eofmark is NULL, read a word or a redirection symbol.  If eofmark
 * is not NULL, read a here document.  In the latter case, eofmark is the
 * word which marks the end of the document and striptabs is true if
 * leading tabs should be stripped from the document.  The argument firstc
 * is the first character of the input token or document.
 *
 * Because C does not have internal subroutines, I have simulated them
 * using goto's to implement the subroutine linkage.  The following macros
 * will run code that appears at the end of readtoken1.
 */

#define CHECKEND()	{goto checkend; checkend_return:;}
#define PARSEREDIR()	{goto parseredir; parseredir_return:;}
#define PARSESUB()	{goto parsesub; parsesub_return:;}
#define PARSEBACKQOLD()	{oldstyle = 1; goto parsebackq; parsebackq_oldreturn:;}
#define PARSEBACKQNEW()	{oldstyle = 0; goto parsebackq; parsebackq_newreturn:;}
#define	PARSEARITH()	{goto parsearith; parsearith_return:;}

/*
 * Keep track of nested doublequotes in dblquote and doublequotep.
 * We use dblquote for the first 32 levels, and we expand to a malloc'ed
 * region for levels above that. Usually we never need to malloc.
 * This code assumes that an int is 32 bits. We don't use uint32_t,
 * because the rest of the code does not.
 */
#define ISDBLQUOTE() ((varnest < 32) ? (dblquote & (1 << varnest)) : \
    (dblquotep[(varnest / 32) - 1] & (1 << (varnest % 32))))

#define SETDBLQUOTE() \
    if (varnest < 32) \
	dblquote |= (1 << varnest); \
    else \
	dblquotep[(varnest / 32) - 1] |= (1 << (varnest % 32))

#define CLRDBLQUOTE() \
    if (varnest < 32) \
	dblquote &= ~(1 << varnest); \
    else \
	dblquotep[(varnest / 32) - 1] &= ~(1 << (varnest % 32))

STATIC int
readtoken1(shinstance *psh, int firstc, char const *syntax, char *eofmark, int striptabs)
{
	int c = firstc;
	char *out;
	int len;
	char line[EOFMARKLEN + 1];
	struct nodelist *bqlist;
	int quotef;
	int *dblquotep = NULL;
	size_t maxnest = 32;
	int dblquote;
	int varnest;	/* levels of variables expansion */
	int arinest;	/* levels of arithmetic expansion */
	int parenlevel;	/* levels of parens in arithmetic */
	int oldstyle;
	char const *prevsyntax;	/* syntax before arithmetic */
#if __GNUC__
	/* Avoid longjmp clobbering */
	(void) &maxnest;
	(void) &dblquotep;
	(void) &out;
	(void) &quotef;
	(void) &dblquote;
	(void) &varnest;
	(void) &arinest;
	(void) &parenlevel;
	(void) &oldstyle;
	(void) &prevsyntax;
	(void) &syntax;
#endif

	psh->startlinno = psh->plinno;
	dblquote = 0;
	varnest = 0;
	if (syntax == DQSYNTAX) {
		SETDBLQUOTE();
	}
	quotef = 0;
	bqlist = NULL;
	arinest = 0;
	parenlevel = 0;

	STARTSTACKSTR(psh, out);
	loop: {	/* for each line, until end of word */
#if ATTY
		if (c == '\034' && psh->doprompt
		 && attyset() && ! equal(termval(), "emacs")) {
			attyline();
			if (syntax == BASESYNTAX)
				return readtoken(psh);
			c = pgetc(psh);
			goto loop;
		}
#endif
		CHECKEND();	/* set c to PEOF if at end of here document */
		for (;;) {	/* until end of line or end of word */
			CHECKSTRSPACE(psh, 4, out);	/* permit 4 calls to USTPUTC */
			switch(syntax[c]) {
			case CNL:	/* '\n' */
				if (syntax == BASESYNTAX)
					goto endword;	/* exit outer loop */
				USTPUTC(psh, c, out);
				psh->plinno++;
				if (psh->doprompt)
					setprompt(psh, 2);
				else
					setprompt(psh, 0);
				c = pgetc(psh);
				goto loop;		/* continue outer loop */
			case CWORD:
				USTPUTC(psh, c, out);
				break;
			case CCTL:
				if (eofmark == NULL || ISDBLQUOTE())
					USTPUTC(psh, CTLESC, out);
				USTPUTC(psh, c, out);
				break;
			case CBACK:	/* backslash */
				c = pgetc(psh);
				if (c == PEOF) {
					USTPUTC(psh, '\\', out);
					pungetc(psh);
					break;
				}
				if (c == '\n') {
					if (psh->doprompt)
						setprompt(psh, 2);
					else
						setprompt(psh, 0);
					break;
				}
				quotef = 1;
				if (ISDBLQUOTE() && c != '\\' &&
				    c != '`' && c != '$' &&
				    (c != '"' || eofmark != NULL))
					USTPUTC(psh, '\\', out);
				if (SQSYNTAX[c] == CCTL)
					USTPUTC(psh, CTLESC, out);
				else if (eofmark == NULL) {
					USTPUTC(psh, CTLQUOTEMARK, out);
					USTPUTC(psh, c, out);
					if (varnest != 0)
						USTPUTC(psh, CTLQUOTEEND, out);
					break;
				}
				USTPUTC(psh, c, out);
				break;
			case CSQUOTE:
				if (syntax != SQSYNTAX) {
					if (eofmark == NULL)
						USTPUTC(psh, CTLQUOTEMARK, out);
					quotef = 1;
					syntax = SQSYNTAX;
					break;
				}
				if (eofmark != NULL && arinest == 0 &&
				    varnest == 0) {
					/* Ignore inside quoted here document */
					USTPUTC(psh, c, out);
					break;
				}
				/* End of single quotes... */
				if (arinest)
					syntax = ARISYNTAX;
				else {
					syntax = BASESYNTAX;
					if (varnest != 0)
						USTPUTC(psh, CTLQUOTEEND, out);
				}
				break;
			case CDQUOTE:
				if (eofmark != NULL && arinest == 0 &&
				    varnest == 0) {
					/* Ignore inside here document */
					USTPUTC(psh, c, out);
					break;
				}
				quotef = 1;
				if (arinest) {
					if (ISDBLQUOTE()) {
						syntax = ARISYNTAX;
						CLRDBLQUOTE();
					} else {
						syntax = DQSYNTAX;
						SETDBLQUOTE();
						USTPUTC(psh, CTLQUOTEMARK, out);
					}
					break;
				}
				if (eofmark != NULL)
					break;
				if (ISDBLQUOTE()) {
					if (varnest != 0)
						USTPUTC(psh, CTLQUOTEEND, out);
					syntax = BASESYNTAX;
					CLRDBLQUOTE();
				} else {
					syntax = DQSYNTAX;
					SETDBLQUOTE();
					USTPUTC(psh, CTLQUOTEMARK, out);
				}
				break;
			case CVAR:	/* '$' */
				PARSESUB();		/* parse substitution */
				break;
			case CENDVAR:	/* CLOSEBRACE */
				if (varnest > 0 && !ISDBLQUOTE()) {
					varnest--;
					USTPUTC(psh, CTLENDVAR, out);
				} else {
					USTPUTC(psh, c, out);
				}
				break;
			case CLP:	/* '(' in arithmetic */
				parenlevel++;
				USTPUTC(psh, c, out);
				break;
			case CRP:	/* ')' in arithmetic */
				if (parenlevel > 0) {
					USTPUTC(psh, c, out);
					--parenlevel;
				} else {
					if (pgetc(psh) == ')') {
						if (--arinest == 0) {
							USTPUTC(psh, CTLENDARI, out);
							syntax = prevsyntax;
							if (syntax == DQSYNTAX)
								SETDBLQUOTE();
							else
								CLRDBLQUOTE();
						} else
							USTPUTC(psh, ')', out);
					} else {
						/*
						 * unbalanced parens
						 *  (don't 2nd guess - no error)
						 */
						pungetc(psh);
						USTPUTC(psh, ')', out);
					}
				}
				break;
			case CBQUOTE:	/* '`' */
				PARSEBACKQOLD();
				break;
			case CSHEOF:
				goto endword;		/* exit outer loop */
			default:
				if (varnest == 0)
					goto endword;	/* exit outer loop */
				USTPUTC(psh, c, out);
			}
			c = pgetc_macro(psh);
		}
	}
endword:
	if (syntax == ARISYNTAX)
		synerror(psh, "Missing '))'");
	if (syntax != BASESYNTAX && ! psh->parsebackquote && eofmark == NULL)
		synerror(psh, "Unterminated quoted string");
	if (varnest != 0) {
		psh->startlinno = psh->plinno;
		/* { */
		synerror(psh, "Missing '}'");
	}
	USTPUTC(psh, '\0', out);
	len = (int)(out - stackblock(psh));
	out = stackblock(psh);
	if (eofmark == NULL) {
		if ((c == '>' || c == '<')
		 && quotef == 0
		 && len <= 2
		 && (*out == '\0' || is_digit(*out))) {
			PARSEREDIR();
			return psh->lasttoken = TREDIR;
		} else {
			pungetc(psh);
		}
	}
	psh->quoteflag = quotef;
	psh->backquotelist = bqlist;
	grabstackblock(psh, len);
	psh->wordtext = out;
	if (dblquotep != NULL)
	    ckfree(psh, dblquotep);
	return psh->lasttoken = TWORD;
/* end of readtoken routine */



/*
 * Check to see whether we are at the end of the here document.  When this
 * is called, c is set to the first character of the next input line.  If
 * we are at the end of the here document, this routine sets the c to PEOF.
 */

checkend: {
	if (eofmark) {
		if (striptabs) {
			while (c == '\t')
				c = pgetc(psh);
		}
		if (c == *eofmark) {
			if (pfgets(psh, line, sizeof line) != NULL) {
				char *p, *q;

				p = line;
				for (q = eofmark + 1 ; *q && *p == *q ; p++, q++);
				if (*p == '\n' && *q == '\0') {
					c = PEOF;
					psh->plinno++;
					psh->needprompt = psh->doprompt;
				} else {
					pushstring(psh, line, strlen(line), NULL);
				}
			}
		}
	}
	goto checkend_return;
}


/*
 * Parse a redirection operator.  The variable "out" points to a string
 * specifying the fd to be redirected.  The variable "c" contains the
 * first character of the redirection operator.
 */

parseredir: {
	char fd = *out;
	union node *np;

	np = (union node *)stalloc(psh, sizeof (struct nfile));
	if (c == '>') {
		np->nfile.fd = 1;
		c = pgetc(psh);
		if (c == '>')
			np->type = NAPPEND;
		else if (c == '|')
			np->type = NCLOBBER;
		else if (c == '&')
			np->type = NTOFD;
		else {
			np->type = NTO;
			pungetc(psh);
		}
	} else {	/* c == '<' */
		np->nfile.fd = 0;
		switch (c = pgetc(psh)) {
		case '<':
			if (sizeof (struct nfile) != sizeof (struct nhere)) {
				np = (union node *)stalloc(psh, sizeof (struct nhere));
				np->nfile.fd = 0;
			}
			np->type = NHERE;
			psh->heredoc = (struct heredoc *)stalloc(psh, sizeof (struct heredoc));
			psh->heredoc->here = np;
			if ((c = pgetc(psh)) == '-') {
				psh->heredoc->striptabs = 1;
			} else {
				psh->heredoc->striptabs = 0;
				pungetc(psh);
			}
			break;

		case '&':
			np->type = NFROMFD;
			break;

		case '>':
			np->type = NFROMTO;
			break;

		default:
			np->type = NFROM;
			pungetc(psh);
			break;
		}
	}
	if (fd != '\0')
		np->nfile.fd = digit_val(fd);
	psh->redirnode = np;
	goto parseredir_return;
}


/*
 * Parse a substitution.  At this point, we have read the dollar sign
 * and nothing else.
 */

parsesub: {
	int subtype;
	int typeloc;
	int flags;
	char *p;
	static const char types[] = "}-+?=";

	c = pgetc(psh);
	if (c != '(' && c != OPENBRACE && !is_name(c) && !is_special(c)) {
		USTPUTC(psh, '$', out);
		pungetc(psh);
	} else if (c == '(') {	/* $(command) or $((arith)) */
		if (pgetc(psh) == '(') {
			PARSEARITH();
		} else {
			pungetc(psh);
			PARSEBACKQNEW();
		}
	} else {
		USTPUTC(psh, CTLVAR, out);
		typeloc = (int)(out - stackblock(psh));
		USTPUTC(psh, VSNORMAL, out);
		subtype = VSNORMAL;
		if (c == OPENBRACE) {
			c = pgetc(psh);
			if (c == '#') {
				if ((c = pgetc(psh)) == CLOSEBRACE)
					c = '#';
				else
					subtype = VSLENGTH;
			}
			else
				subtype = 0;
		}
		if (is_name(c)) {
			do {
				STPUTC(psh, c, out);
				c = pgetc(psh);
			} while (is_in_name(c));
		} else if (is_digit(c)) {
			do {
				USTPUTC(psh, c, out);
				c = pgetc(psh);
			} while (is_digit(c));
		}
		else if (is_special(c)) {
			USTPUTC(psh, c, out);
			c = pgetc(psh);
		}
		else
badsub:			synerror(psh, "Bad substitution");

		STPUTC(psh, '=', out);
		flags = 0;
		if (subtype == 0) {
			switch (c) {
			case ':':
				flags = VSNUL;
				c = pgetc(psh);
				/*FALLTHROUGH*/
			default:
				p = strchr(types, c);
				if (p == NULL)
					goto badsub;
				subtype = (int)(p - types + VSNORMAL);
				break;
			case '%':
			case '#':
				{
					int cc = c;
					subtype = c == '#' ? VSTRIMLEFT :
							     VSTRIMRIGHT;
					c = pgetc(psh);
					if (c == cc)
						subtype++;
					else
						pungetc(psh);
					break;
				}
			}
		} else {
			pungetc(psh);
		}
		if (ISDBLQUOTE() || arinest)
			flags |= VSQUOTE;
		*(stackblock(psh) + typeloc) = subtype | flags;
		if (subtype != VSNORMAL) {
			varnest++;
			if (varnest >= (int)maxnest) {
				dblquotep = ckrealloc(psh, dblquotep, maxnest / 8);
				dblquotep[(maxnest / 32) - 1] = 0;
				maxnest += 32;
			}
		}
	}
	goto parsesub_return;
}


/*
 * Called to parse command substitutions.  Newstyle is set if the command
 * is enclosed inside $(...); nlpp is a pointer to the head of the linked
 * list of commands (passed by reference), and savelen is the number of
 * characters on the top of the stack which must be preserved.
 */

parsebackq: {
	struct nodelist **nlpp;
	int savepbq;
	union node *n;
	char *volatile str;
	struct jmploc jmploc;
	struct jmploc *volatile savehandler;
	int savelen;
	int saveprompt;
#ifdef __GNUC__
	(void) &saveprompt;
#endif

	savepbq = psh->parsebackquote;
	if (setjmp(jmploc.loc)) {
		if (str)
			ckfree(psh, str);
		psh->parsebackquote = 0;
		psh->handler = savehandler;
		longjmp(psh->handler->loc, 1);
	}
	INTOFF;
	str = NULL;
	savelen = (int)(out - stackblock(psh));
	if (savelen > 0) {
		str = ckmalloc(psh, savelen);
		memcpy(str, stackblock(psh), savelen);
	}
	savehandler = psh->handler;
	psh->handler = &jmploc;
	INTON;
        if (oldstyle) {
                /* We must read until the closing backquote, giving special
                   treatment to some slashes, and then push the string and
                   reread it as input, interpreting it normally.  */
                char *pout;
                int pc;
                int psavelen;
                char *pstr;


                STARTSTACKSTR(psh, pout);
		for (;;) {
			if (psh->needprompt) {
				setprompt(psh, 2);
				psh->needprompt = 0;
			}
			switch (pc = pgetc(psh)) {
			case '`':
				goto done;

			case '\\':
                                if ((pc = pgetc(psh)) == '\n') {
					psh->plinno++;
					if (psh->doprompt)
						setprompt(psh, 2);
					else
						setprompt(psh, 0);
					/*
					 * If eating a newline, avoid putting
					 * the newline into the new character
					 * stream (via the STPUTC after the
					 * switch).
					 */
					continue;
				}
                                if (pc != '\\' && pc != '`' && pc != '$'
                                    && (!ISDBLQUOTE() || pc != '"'))
                                        STPUTC(psh, '\\', pout);
				break;

			case '\n':
				psh->plinno++;
				psh->needprompt = psh->doprompt;
				break;

			case PEOF:
			        psh->startlinno = psh->plinno;
				synerror(psh, "EOF in backquote substitution");
 				break;

			default:
				break;
			}
			STPUTC(psh, pc, pout);
                }
done:
                STPUTC(psh, '\0', pout);
                psavelen = (int)(pout - stackblock(psh));
                if (psavelen > 0) {
			pstr = grabstackstr(psh, pout);
			setinputstring(psh, pstr, 1);
                }
        }
	nlpp = &bqlist;
	while (*nlpp)
		nlpp = &(*nlpp)->next;
	*nlpp = (struct nodelist *)stalloc(psh, sizeof (struct nodelist));
	(*nlpp)->next = NULL;
	psh->parsebackquote = oldstyle;

	if (oldstyle) {
		saveprompt = psh->doprompt;
		psh->doprompt = 0;
	}

	n = list(psh, 0);

	if (oldstyle)
		psh->doprompt = saveprompt;
	else {
		if (readtoken(psh) != TRP)
			synexpect(psh, TRP);
	}

	(*nlpp)->n = n;
        if (oldstyle) {
		/*
		 * Start reading from old file again, ignoring any pushed back
		 * tokens left from the backquote parsing
		 */
                popfile(psh);
		psh->tokpushback = 0;
	}
	while (stackblocksize(psh) <= savelen)
		growstackblock(psh);
	STARTSTACKSTR(psh, out);
	if (str) {
		memcpy(out, str, savelen);
		STADJUST(psh, savelen, out);
		INTOFF;
		ckfree(psh, str);
		str = NULL;
		INTON;
	}
	psh->parsebackquote = savepbq;
	psh->handler = savehandler;
	if (arinest || ISDBLQUOTE())
		USTPUTC(psh, CTLBACKQ | CTLQUOTE, out);
	else
		USTPUTC(psh, CTLBACKQ, out);
	if (oldstyle)
		goto parsebackq_oldreturn;
	else
		goto parsebackq_newreturn;
}

/*
 * Parse an arithmetic expansion (indicate start of one and set state)
 */
parsearith: {

	if (++arinest == 1) {
		prevsyntax = syntax;
		syntax = ARISYNTAX;
		USTPUTC(psh, CTLARI, out);
		if (ISDBLQUOTE())
			USTPUTC(psh, '"',out);
		else
			USTPUTC(psh, ' ',out);
	} else {
		/*
		 * we collapse embedded arithmetic expansion to
		 * parenthesis, which should be equivalent
		 */
		USTPUTC(psh, '(', out);
	}
	goto parsearith_return;
}

} /* end of readtoken */



#ifdef mkinit
RESET {
	psh->tokpushback = 0;
	psh->checkkwd = 0;
}
#endif

/*
 * Returns true if the text contains nothing to expand (no dollar signs
 * or backquotes).
 */

STATIC int
noexpand(shinstance *psh, char *text)
{
	char *p;
	char c;

	p = text;
	while ((c = *p++) != '\0') {
		if (c == CTLQUOTEMARK)
			continue;
		if (c == CTLESC)
			p++;
		else if (BASESYNTAX[(int)c] == CCTL)
			return 0;
	}
	return 1;
}


/*
 * Return true if the argument is a legal variable name (a letter or
 * underscore followed by zero or more letters, underscores, and digits).
 */

int
goodname(const char *name)
{
	const char *p;

	p = name;
	if (! is_name(*p))
		return 0;
	while (*++p) {
		if (! is_in_name(*p))
			return 0;
	}
	return 1;
}


/*
 * Called when an unexpected token is read during the parse.  The argument
 * is the token that is expected, or -1 if more than one type of token can
 * occur at this point.
 */

SH_NORETURN_1 STATIC void
synexpect(shinstance *psh, int token)
{
	char msg[64];

	if (token >= 0) {
		fmtstr(msg, 64, "%s unexpected (expecting %s)",
			tokname[psh->lasttoken], tokname[token]);
	} else {
		fmtstr(msg, 64, "%s unexpected", tokname[psh->lasttoken]);
	}
	synerror(psh, msg);
	/* NOTREACHED */
}


SH_NORETURN_1 STATIC void
synerror(shinstance *psh, const char *msg)
{
	if (psh->commandname) {
		trace(psh, "synerror: %s: %d: Syntax error: %s", psh->commandname, psh->startlinno, msg);
		outfmt(&psh->errout, "%s: %d: ", psh->commandname, psh->startlinno);
	} else {
		trace(psh, "synerror: Syntax error: %s\n", msg);
	}
	outfmt(&psh->errout, "Syntax error: %s\n", msg);
	error(psh, (char *)NULL);
	/* NOTREACHED */
}

STATIC const char *
my_basename(const char *argv0, unsigned *lenp)
{
	const char *tmp;

    /* skip the path */
    for (tmp = strpbrk(argv0, "\\/:"); tmp; tmp = strpbrk(argv0, "\\/:"))
        argv0 = tmp + 1;

	if (lenp) {
		/* find the end, ignoring extenions */
		tmp = strrchr(argv0, '.');
		if (!tmp)
			tmp = strchr(argv0, '\0');
		*lenp = (unsigned)(tmp - argv0);
	}
	return argv0;
}


STATIC void
setprompt(shinstance *psh, int which)
{
	psh->whichprompt = which;

#ifndef SMALL
	if (!el)
#endif
	{
		/* deal with bash prompts */
		const char *prompt = getprompt(psh, NULL);
		if (!strchr(prompt, '\\')) {
			out2str(psh, prompt);
		} else {
			while (*prompt) {
				if (*prompt != '\\') {
					out2c(psh, *prompt++);
				} else {
					prompt++;
					switch (*prompt++)
					{
						/* simple */
						case '$':	out2c(psh, sh_geteuid(psh) ? '$' : '#'); break;
						case '\\': 	out2c(psh, '\\'); break;
						case 'a':	out2c(psh, '\a'); break;
						case 'e':	out2c(psh, 033); break;
						case 'n': 	out2c(psh, '\n'); break;
						case 'r': 	out2c(psh, '\r'); break;

						/* complicated */
						case 's': {
							unsigned len;
							const char *arg0 = my_basename(psh->arg0, &len);
							outfmt(psh->out2, "%.*s", len, arg0);
							break;
						}
						case 'v':
							outfmt(psh->out2, "%d.%d", KBUILD_VERSION_MAJOR,
								   KBUILD_VERSION_MINOR);
							break;
						case 'V':
							outfmt(psh->out2, "%d.%d.%d", KBUILD_VERSION_MAJOR,
								   KBUILD_VERSION_MINOR, KBUILD_VERSION_PATCH);
							break;
							out2str(psh, getpwd(psh, 1) ? getpwd(psh, 1) : "?");
							break;
						case 'w':
						case 'W': {
							const char *cwd = getpwd(psh, 1);
							const char *home = bltinlookup(psh, "HOME", 1);
							size_t home_len = home ? strlen(home) : 0;
							if (!cwd) cwd = "?";
							if (!strncmp(cwd, home, home_len)
							  && (    cwd[home_len] == '\0'
							      || (cwd[home_len] == '/' && prompt[-1] == 'w'))) {
								out2c(psh, '~');
								if (prompt[-1] == 'w' && cwd[home_len]) {
									out2str(psh, cwd + home_len);
								}
							} else if (prompt[-1] == 'w') {
								out2str(psh, cwd);
							} else {
								out2str(psh, my_basename(cwd, NULL));
							}
							break;
						}
						case '0':
						case '1':
						case '2':
						case '3': {
							unsigned int ch = prompt[-1] - '0';
							if (isdigit(*prompt)) {
								ch *= 8;
								ch += *prompt++ - '0';
							}
							if (isdigit(*prompt)) {
								ch *= 8;
								ch += *prompt++ - '0';
							}
							out2c(psh, ch);
							break;
						}

						/* ignore */
							break;
						case '!':
						case '#':
						case '@':
						case 'A':
						case 'h':
						case 'H':
						case 'j':
						case 'l':
						case 't':
						case 'T':
						case 'u':
						case '[':
							if (strchr(prompt, ']')) {
								prompt = strchr(prompt, ']') + 1;
							}
							break;
						case 'D':
							if (*prompt == '{' && strchr(prompt, '}')) {
								prompt = strchr(prompt, '}') + 1;
							}
							break;
					}

				}
			}
		}
	}
}

/*
 * called by editline -- any expansions to the prompt
 *    should be added here.
 */
const char *
getprompt(shinstance *psh, void *unused)
{
	switch (psh->whichprompt) {
	case 0:
		return "";
	case 1:
		return ps1val(psh);
	case 2:
		return ps2val(psh);
	default:
		return "<internal prompt error>";
	}
}
