/* $NetBSD: test.c,v 1.26 2005/02/10 06:56:55 simonb Exp $ */

/*
 * test(1); version 7-like  --  author Erik Baalbergen
 * modified by Eric Gisin to be used as built-in.
 * modified by Arnold Robbins to add SVR3 compatibility
 * (-x -c -b -p -u -g -k) plus Korn's -L -nt -ot -ef and new -S (socket).
 * modified by J.T. Conklin for NetBSD.
 *
 * This program is in the Public Domain.
 */

#if 0
#ifndef lint
__RCSID("$NetBSD: test.c,v 1.26 2005/02/10 06:56:55 simonb Exp $");
#endif
#endif

#include <sys/types.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "shell.h"
#include "error.h"
#include "shinstance.h"


/* test(1) accepts the following grammar:
	oexpr	::= aexpr | aexpr "-o" oexpr ;
	aexpr	::= nexpr | nexpr "-a" aexpr ;
	nexpr	::= primary | "!" primary
	primary	::= unary-operator operand
		| operand binary-operator operand
		| operand
		| "(" oexpr ")"
		;
	unary-operator ::= "-r"|"-w"|"-x"|"-f"|"-d"|"-c"|"-b"|"-p"|
		"-u"|"-g"|"-k"|"-s"|"-t"|"-z"|"-n"|"-o"|"-O"|"-G"|"-L"|"-S";

	binary-operator ::= "="|"!="|"-eq"|"-ne"|"-ge"|"-gt"|"-le"|"-lt"|
			"-nt"|"-ot"|"-ef";
	operand ::= <any legal UNIX file name>
*/

enum token {
	EOI,
	FILRD,
	FILWR,
	FILEX,
	FILEXIST,
	FILREG,
	FILDIR,
	FILCDEV,
	FILBDEV,
	FILFIFO,
	FILSOCK,
	FILSYM,
	FILGZ,
	FILTT,
	FILSUID,
	FILSGID,
	FILSTCK,
	FILNT,
	FILOT,
	FILEQ,
	FILUID,
	FILGID,
	STREZ,
	STRNZ,
	STREQ,
	STRNE,
	STRLT,
	STRGT,
	INTEQ,
	INTNE,
	INTGE,
	INTGT,
	INTLE,
	INTLT,
	UNOT,
	BAND,
	BOR,
	LPAREN,
	RPAREN,
	OPERAND
};

enum token_types {
	UNOP,
	BINOP,
	BUNOP,
	BBINOP,
	PAREN
};

static struct t_op {
	const char *op_text;
	short op_num, op_type;
} const ops [] = {
	{"-r",	FILRD,	UNOP},
	{"-w",	FILWR,	UNOP},
	{"-x",	FILEX,	UNOP},
	{"-e",	FILEXIST,UNOP},
	{"-f",	FILREG,	UNOP},
	{"-d",	FILDIR,	UNOP},
	{"-c",	FILCDEV,UNOP},
	{"-b",	FILBDEV,UNOP},
	{"-p",	FILFIFO,UNOP},
	{"-u",	FILSUID,UNOP},
	{"-g",	FILSGID,UNOP},
	{"-k",	FILSTCK,UNOP},
	{"-s",	FILGZ,	UNOP},
	{"-t",	FILTT,	UNOP},
	{"-z",	STREZ,	UNOP},
	{"-n",	STRNZ,	UNOP},
	{"-h",	FILSYM,	UNOP},		/* for backwards compat */
	{"-O",	FILUID,	UNOP},
	{"-G",	FILGID,	UNOP},
	{"-L",	FILSYM,	UNOP},
	{"-S",	FILSOCK,UNOP},
	{"=",	STREQ,	BINOP},
	{"!=",	STRNE,	BINOP},
	{"<",	STRLT,	BINOP},
	{">",	STRGT,	BINOP},
	{"-eq",	INTEQ,	BINOP},
	{"-ne",	INTNE,	BINOP},
	{"-ge",	INTGE,	BINOP},
	{"-gt",	INTGT,	BINOP},
	{"-le",	INTLE,	BINOP},
	{"-lt",	INTLT,	BINOP},
	{"-nt",	FILNT,	BINOP},
	{"-ot",	FILOT,	BINOP},
	{"-ef",	FILEQ,	BINOP},
	{"!",	UNOT,	BUNOP},
	{"-a",	BAND,	BBINOP},
	{"-o",	BOR,	BBINOP},
	{"(",	LPAREN,	PAREN},
	{")",	RPAREN,	PAREN},
	{0,	0,	0}
};

//static char **t_wp;
//static struct t_op const *t_wp_op;

static void syntax(shinstance *, const char *, const char *);
static int oexpr(shinstance *, enum token);
static int aexpr(shinstance *, enum token);
static int nexpr(shinstance *, enum token);
static int primary(shinstance *, enum token);
static int binop(shinstance *);
static int filstat(shinstance *, char *, enum token);
static enum token t_lex(shinstance *, char *);
static int isoperand(shinstance *);
static int getn(shinstance *, const char *);
static int newerf(shinstance *, const char *, const char *);
static int olderf(shinstance *, const char *, const char *);
static int equalf(shinstance *, const char *, const char *);


int
testcmd(shinstance *psh, int argc, char **argv)
{
	int res;

	if (strcmp(argv[0], "[") == 0) {
		if (strcmp(argv[--argc], "]"))
			error(psh, "missing ]");
		argv[argc] = NULL;
	}

	if (argc < 2)
		return 1;

	psh->t_wp_op = NULL;
	psh->t_wp = &argv[1];
	res = !oexpr(psh, t_lex(psh, *psh->t_wp));

	if (*psh->t_wp != NULL && *++psh->t_wp != NULL)
		syntax(psh, *psh->t_wp, "unexpected operator");

	return res;
}

static void
syntax(shinstance *psh, const char *op, const char *msg)
{

	if (op && *op)
		error(psh, "%s: %s", op, msg);
	else
		error(psh, "%s", msg);
}

static int
oexpr(shinstance *psh, enum token n)
{
	int res;

	res = aexpr(psh, n);
	if (t_lex(psh, *++psh->t_wp) == BOR)
		return oexpr(psh, t_lex(psh, *++psh->t_wp)) || res;
	psh->t_wp--;
	return res;
}

static int
aexpr(shinstance *psh, enum token n)
{
	int res;

	res = nexpr(psh, n);
	if (t_lex(psh, *++psh->t_wp) == BAND)
		return aexpr(psh, t_lex(psh, *++psh->t_wp)) && res;
	psh->t_wp--;
	return res;
}

static int
nexpr(shinstance *psh, enum token n)
{

	if (n == UNOT)
		return !nexpr(psh, t_lex(psh, *++psh->t_wp));
	return primary(psh, n);
}

static int
primary(shinstance *psh, enum token n)
{
	enum token nn;
	int res;

	if (n == EOI)
		return 0;		/* missing expression */
	if (n == LPAREN) {
		if ((nn = t_lex(psh, *++psh->t_wp)) == RPAREN)
			return 0;	/* missing expression */
		res = oexpr(psh, nn);
		if (t_lex(psh, *++psh->t_wp) != RPAREN)
			syntax(psh, NULL, "closing paren expected");
		return res;
	}
	if (psh->t_wp_op && psh->t_wp_op->op_type == UNOP) {
		/* unary expression */
		if (*++psh->t_wp == NULL)
			syntax(psh, psh->t_wp_op->op_text, "argument expected");
		switch (n) {
		case STREZ:
			return strlen(*psh->t_wp) == 0;
		case STRNZ:
			return strlen(*psh->t_wp) != 0;
		case FILTT:
			return shfile_isatty(&psh->fdtab, getn(psh, *psh->t_wp));
		default:
			return filstat(psh, *psh->t_wp, n);
		}
	}

	if (t_lex(psh, psh->t_wp[1]), psh->t_wp_op && psh->t_wp_op->op_type == BINOP) {
		return binop(psh);
	}

	return strlen(*psh->t_wp) > 0;
}

static int
binop(shinstance *psh)
{
	const char *opnd1, *opnd2;
	struct t_op const *op;

	opnd1 = *psh->t_wp;
	(void) t_lex(psh, *++psh->t_wp);
	op = psh->t_wp_op;

	if ((opnd2 = *++psh->t_wp) == NULL)
		syntax(psh, op->op_text, "argument expected");

	switch (op->op_num) {
	case STREQ:
		return strcmp(opnd1, opnd2) == 0;
	case STRNE:
		return strcmp(opnd1, opnd2) != 0;
	case STRLT:
		return strcmp(opnd1, opnd2) < 0;
	case STRGT:
		return strcmp(opnd1, opnd2) > 0;
	case INTEQ:
		return getn(psh, opnd1) == getn(psh, opnd2);
	case INTNE:
		return getn(psh, opnd1) != getn(psh, opnd2);
	case INTGE:
		return getn(psh, opnd1) >= getn(psh, opnd2);
	case INTGT:
		return getn(psh, opnd1) > getn(psh, opnd2);
	case INTLE:
		return getn(psh, opnd1) <= getn(psh, opnd2);
	case INTLT:
		return getn(psh, opnd1) < getn(psh, opnd2);
	case FILNT:
		return newerf(psh, opnd1, opnd2);
	case FILOT:
		return olderf(psh, opnd1, opnd2);
	case FILEQ:
		return equalf(psh, opnd1, opnd2);
	default:
		sh_abort(psh);
		/* NOTREACHED */
		return -1;
	}
}

static int
filstat(shinstance *psh, char *nm, enum token mode)
{
	struct stat s;

	if (mode == FILSYM
		? shfile_lstat(&psh->fdtab, nm, &s)
		: shfile_stat(&psh->fdtab, nm, &s))
		return 0;

	switch (mode) {
	case FILRD:
		return shfile_access(&psh->fdtab, nm, R_OK) == 0;
	case FILWR:
		return shfile_access(&psh->fdtab, nm, W_OK) == 0;
	case FILEX:
		return shfile_access(&psh->fdtab, nm, X_OK) == 0;
	case FILEXIST:
		return shfile_access(&psh->fdtab, nm, F_OK) == 0;
	case FILREG:
		return S_ISREG(s.st_mode);
	case FILDIR:
		return S_ISDIR(s.st_mode);
	case FILCDEV:
#ifdef S_ISCHR
		return S_ISCHR(s.st_mode);
#else
        return 0;
#endif
	case FILBDEV:
#ifdef S_ISBLK
		return S_ISBLK(s.st_mode);
#else
        return 0;
#endif
	case FILFIFO:
#ifdef S_ISFIFO
		return S_ISFIFO(s.st_mode);
#else
        return 0;
#endif
	case FILSOCK:
#ifdef S_ISSOCK
		return S_ISSOCK(s.st_mode);
#else
        return 0;
#endif
	case FILSYM:
		return S_ISLNK(s.st_mode);
	case FILSUID:
		return (s.st_mode & S_ISUID) != 0;
	case FILSGID:
		return (s.st_mode & S_ISGID) != 0;
	case FILSTCK:
#ifdef S_ISVTX
		return (s.st_mode & S_ISVTX) != 0;
#else
        return 0;
#endif
	case FILGZ:
		return s.st_size > (off_t)0;
	case FILUID:
		return s.st_uid == sh_geteuid(psh);
	case FILGID:
		return s.st_gid == sh_getegid(psh);
	default:
		return 1;
	}
}

static enum token
t_lex(shinstance *psh, char *s)
{
	struct t_op const *op;

	op = ops;

	if (s == 0) {
		psh->t_wp_op = NULL;
		return EOI;
	}
	while (op->op_text) {
		if (strcmp(s, op->op_text) == 0) {
			if ((op->op_type == UNOP && isoperand(psh)) ||
			    (op->op_num == LPAREN && *(psh->t_wp+1) == 0))
				break;
			psh->t_wp_op = op;
			return op->op_num;
		}
		op++;
	}
	psh->t_wp_op = NULL;
	return OPERAND;
}

static int
isoperand(shinstance *psh)
{
	struct t_op const *op;
	char *s, *t;

	op = ops;
	if ((s  = *(psh->t_wp+1)) == 0)
		return 1;
	if ((t = *(psh->t_wp+2)) == 0)
		return 0;
	while (op->op_text) {
		if (strcmp(s, op->op_text) == 0)
	    		return op->op_type == BINOP &&
	    		    (t[0] != ')' || t[1] != '\0');
		op++;
	}
	return 0;
}

/* atoi with error detection */
static int
getn(shinstance *psh, const char *s)
{
	char *p;
	long r;

	errno = 0;
	r = strtol(s, &p, 10);

	if (errno != 0)
	      error(psh, "%s: out of range", s);

	while (isspace((unsigned char)*p))
	      p++;

	if (*p)
	      error(psh, "%s: bad number", s);

	return (int) r;
}

static int
newerf(shinstance *psh, const char *f1, const char *f2)
{
	struct stat b1, b2;

	return (shfile_stat(&psh->fdtab, f1, &b1) == 0 &&
		shfile_stat(&psh->fdtab, f2, &b2) == 0 &&
		b1.st_mtime > b2.st_mtime);
}

static int
olderf(shinstance *psh, const char *f1, const char *f2)
{
	struct stat b1, b2;

	return (shfile_stat(&psh->fdtab, f1, &b1) == 0 &&
		shfile_stat(&psh->fdtab, f2, &b2) == 0 &&
		b1.st_mtime < b2.st_mtime);
}

static int
equalf(shinstance *psh, const char *f1, const char *f2)
{
	struct stat b1, b2;

	return (shfile_stat(&psh->fdtab, f1, &b1) == 0 &&
		shfile_stat(&psh->fdtab, f2, &b2) == 0 &&
		b1.st_dev == b2.st_dev &&
		b1.st_ino == b2.st_ino);
}
