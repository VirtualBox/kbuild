#ifndef lint
/*static char yysccsid[] = "from: @(#)yaccpar	1.9 (Berkeley) 02/21/93";*/
static char yyrcsid[] = "$Id: skeleton.c,v 1.2 1997/06/23 02:51:17 tdukes Exp $";
#endif
#define YYBYACC 1
#define YYMAJOR 1
#define YYMINOR 9
#define yyclearin (yychar=(-1))
#define yyerrok (yyerrflag=0)
#define YYRECOVERING (yyerrflag!=0)
#define YYPREFIX "yy"
/*	$NetBSD: arith.y,v 1.17 2003/09/17 17:33:36 jmmv Exp $	*/

/*-
 * Copyright (c) 1993
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
static char sccsid[] = "@(#)arith.y	8.3 (Berkeley) 5/4/95";
#else
__RCSID("$NetBSD: arith.y,v 1.17 2003/09/17 17:33:36 jmmv Exp $");
#endif /* not lint */
#endif

#include <stdlib.h>
#include "expand.h"
#include "shell.h"
#include "error.h"
#include "output.h"
#include "memalloc.h"
#include "shinstance.h"

shinstance *arith_psh;
const char *arith_buf, *arith_startbuf;

void yyerror(const char *);
#ifdef TESTARITH
int main(int , char *[]);
int error(char *);
#endif

#define ARITH_NUM 257
#define ARITH_LPAREN 258
#define ARITH_RPAREN 259
#define ARITH_OR 260
#define ARITH_AND 261
#define ARITH_BOR 262
#define ARITH_BXOR 263
#define ARITH_BAND 264
#define ARITH_EQ 265
#define ARITH_NE 266
#define ARITH_LT 267
#define ARITH_GT 268
#define ARITH_GE 269
#define ARITH_LE 270
#define ARITH_LSHIFT 271
#define ARITH_RSHIFT 272
#define ARITH_ADD 273
#define ARITH_SUB 274
#define ARITH_MUL 275
#define ARITH_DIV 276
#define ARITH_REM 277
#define ARITH_UNARYMINUS 278
#define ARITH_UNARYPLUS 279
#define ARITH_NOT 280
#define ARITH_BNOT 281
#define YYERRCODE 256
short yylhs[] = {                                        -1,
    0,    1,    1,    1,    1,    1,    1,    1,    1,    1,
    1,    1,    1,    1,    1,    1,    1,    1,    1,    1,
    1,    1,    1,    1,    1,
};
short yylen[] = {                                         2,
    1,    3,    3,    3,    3,    3,    3,    3,    3,    3,
    3,    3,    3,    3,    3,    3,    3,    3,    3,    3,
    2,    2,    2,    2,    1,
};
short yydefred[] = {                                      0,
   25,    0,    0,    0,    0,    0,    0,    0,    0,   24,
   23,   21,   22,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
    0,    2,    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,   18,   19,   20,
};
short yydgoto[] = {                                       7,
    8,
};
short yysindex[] = {                                   -255,
    0, -255, -255, -255, -255, -255,    0,  -67,  -85,    0,
    0,    0,    0, -255, -255, -255, -255, -255, -255, -255,
 -255, -255, -255, -255, -255, -255, -255, -255, -255, -255,
 -255,    0,  -50,  -34,  -19,  141, -261, -233, -233, -223,
 -223, -223, -223, -253, -253, -248, -248,    0,    0,    0,
};
short yyrindex[] = {                                      0,
    0,    0,    0,    0,    0,    0,    0,   30,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,  143,  140,  136,  131,  125,  109,  117,   61,
   73,   85,   97,   33,   47,    1,   17,    0,    0,    0,
};
short yygindex[] = {                                      0,
  142,
};
#define YYTABLESIZE 418
short yytable[] = {                                       0,
   16,    1,    2,   19,   20,   21,   22,   23,   24,   25,
   26,   27,   28,   29,   30,   31,   17,    3,    4,   27,
   28,   29,   30,   31,    5,    6,   29,   30,   31,    1,
    0,    0,   14,   21,   22,   23,   24,   25,   26,   27,
   28,   29,   30,   31,    0,    0,   15,   25,   26,   27,
   28,   29,   30,   31,    0,    0,    0,    0,    0,    0,
   11,    0,    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    9,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,   10,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,   12,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,    8,    0,
    0,    0,    0,    0,    0,    0,   13,    0,    0,    0,
    0,    0,    0,    0,    7,    0,    0,    0,    0,    0,
    6,    0,    0,    0,    0,    5,    0,    0,    0,    4,
    0,    0,    3,    9,   10,   11,   12,   13,    0,    0,
    0,    0,    0,    0,    0,   33,   34,   35,   36,   37,
   38,   39,   40,   41,   42,   43,   44,   45,   46,   47,
   48,   49,   50,   32,   14,   15,   16,   17,   18,   19,
   20,   21,   22,   23,   24,   25,   26,   27,   28,   29,
   30,   31,   14,   15,   16,   17,   18,   19,   20,   21,
   22,   23,   24,   25,   26,   27,   28,   29,   30,   31,
   15,   16,   17,   18,   19,   20,   21,   22,   23,   24,
   25,   26,   27,   28,   29,   30,   31,   16,   17,   18,
   19,   20,   21,   22,   23,   24,   25,   26,   27,   28,
   29,   30,   31,   17,   18,   19,   20,   21,   22,   23,
   24,   25,   26,   27,   28,   29,   30,   31,    0,   16,
   16,   16,   16,   16,   16,   16,   16,   16,   16,   16,
   16,   16,   16,   16,   16,   17,   17,   17,   17,   17,
   17,   17,   17,   17,   17,   17,   17,   17,   17,   17,
   17,   14,   14,   14,   14,   14,   14,   14,   14,   14,
   14,   14,   14,   14,   14,   15,   15,   15,   15,   15,
   15,   15,   15,   15,   15,   15,   15,   15,   15,   11,
   11,   11,   11,   11,   11,   11,   11,   11,   11,   11,
   11,    9,    9,    9,    9,    9,    9,    9,    9,    9,
    9,    9,    9,   10,   10,   10,   10,   10,   10,   10,
   10,   10,   10,   10,   10,   12,   12,   12,   12,   12,
   12,   12,   12,   12,   12,   12,   12,    8,    8,    8,
    8,    8,    8,    8,    8,   13,   13,   13,   13,   13,
   13,   13,   13,    7,    7,    7,    7,    7,    7,    6,
    6,    6,    6,    6,    5,    5,    5,    5,    4,    4,
    4,    3,    3,    0,   18,   19,   20,   21,   22,   23,
   24,   25,   26,   27,   28,   29,   30,   31,
};
short yycheck[] = {                                      -1,
    0,  257,  258,  265,  266,  267,  268,  269,  270,  271,
  272,  273,  274,  275,  276,  277,    0,  273,  274,  273,
  274,  275,  276,  277,  280,  281,  275,  276,  277,    0,
   -1,   -1,    0,  267,  268,  269,  270,  271,  272,  273,
  274,  275,  276,  277,   -1,   -1,    0,  271,  272,  273,
  274,  275,  276,  277,   -1,   -1,   -1,   -1,   -1,   -1,
    0,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,    0,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,    0,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,    0,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,    0,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,    0,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,    0,   -1,   -1,   -1,   -1,   -1,
    0,   -1,   -1,   -1,   -1,    0,   -1,   -1,   -1,    0,
   -1,   -1,    0,    2,    3,    4,    5,    6,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   14,   15,   16,   17,   18,
   19,   20,   21,   22,   23,   24,   25,   26,   27,   28,
   29,   30,   31,  259,  260,  261,  262,  263,  264,  265,
  266,  267,  268,  269,  270,  271,  272,  273,  274,  275,
  276,  277,  260,  261,  262,  263,  264,  265,  266,  267,
  268,  269,  270,  271,  272,  273,  274,  275,  276,  277,
  261,  262,  263,  264,  265,  266,  267,  268,  269,  270,
  271,  272,  273,  274,  275,  276,  277,  262,  263,  264,
  265,  266,  267,  268,  269,  270,  271,  272,  273,  274,
  275,  276,  277,  263,  264,  265,  266,  267,  268,  269,
  270,  271,  272,  273,  274,  275,  276,  277,   -1,  259,
  260,  261,  262,  263,  264,  265,  266,  267,  268,  269,
  270,  271,  272,  273,  274,  259,  260,  261,  262,  263,
  264,  265,  266,  267,  268,  269,  270,  271,  272,  273,
  274,  259,  260,  261,  262,  263,  264,  265,  266,  267,
  268,  269,  270,  271,  272,  259,  260,  261,  262,  263,
  264,  265,  266,  267,  268,  269,  270,  271,  272,  259,
  260,  261,  262,  263,  264,  265,  266,  267,  268,  269,
  270,  259,  260,  261,  262,  263,  264,  265,  266,  267,
  268,  269,  270,  259,  260,  261,  262,  263,  264,  265,
  266,  267,  268,  269,  270,  259,  260,  261,  262,  263,
  264,  265,  266,  267,  268,  269,  270,  259,  260,  261,
  262,  263,  264,  265,  266,  259,  260,  261,  262,  263,
  264,  265,  266,  259,  260,  261,  262,  263,  264,  259,
  260,  261,  262,  263,  259,  260,  261,  262,  259,  260,
  261,  259,  260,   -1,  264,  265,  266,  267,  268,  269,
  270,  271,  272,  273,  274,  275,  276,  277,
};
#define YYFINAL 7
#ifndef YYDEBUG
#define YYDEBUG 0
#endif
#define YYMAXTOKEN 281
#if YYDEBUG
char *yyname[] = {
"end-of-file",0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,"ARITH_NUM","ARITH_LPAREN",
"ARITH_RPAREN","ARITH_OR","ARITH_AND","ARITH_BOR","ARITH_BXOR","ARITH_BAND",
"ARITH_EQ","ARITH_NE","ARITH_LT","ARITH_GT","ARITH_GE","ARITH_LE",
"ARITH_LSHIFT","ARITH_RSHIFT","ARITH_ADD","ARITH_SUB","ARITH_MUL","ARITH_DIV",
"ARITH_REM","ARITH_UNARYMINUS","ARITH_UNARYPLUS","ARITH_NOT","ARITH_BNOT",
};
char *yyrule[] = {
"$accept : exp",
"exp : expr",
"expr : ARITH_LPAREN expr ARITH_RPAREN",
"expr : expr ARITH_OR expr",
"expr : expr ARITH_AND expr",
"expr : expr ARITH_BOR expr",
"expr : expr ARITH_BXOR expr",
"expr : expr ARITH_BAND expr",
"expr : expr ARITH_EQ expr",
"expr : expr ARITH_GT expr",
"expr : expr ARITH_GE expr",
"expr : expr ARITH_LT expr",
"expr : expr ARITH_LE expr",
"expr : expr ARITH_NE expr",
"expr : expr ARITH_LSHIFT expr",
"expr : expr ARITH_RSHIFT expr",
"expr : expr ARITH_ADD expr",
"expr : expr ARITH_SUB expr",
"expr : expr ARITH_MUL expr",
"expr : expr ARITH_DIV expr",
"expr : expr ARITH_REM expr",
"expr : ARITH_NOT expr",
"expr : ARITH_BNOT expr",
"expr : ARITH_SUB expr",
"expr : ARITH_ADD expr",
"expr : ARITH_NUM",
};
#endif
#ifndef YYSTYPE
typedef int YYSTYPE;
#endif
#ifdef YYSTACKSIZE
#undef YYMAXDEPTH
#define YYMAXDEPTH YYSTACKSIZE
#else
#ifdef YYMAXDEPTH
#define YYSTACKSIZE YYMAXDEPTH
#else
#define YYSTACKSIZE 500
#define YYMAXDEPTH 500
#endif
#endif
int yydebug;
int yynerrs;
int yyerrflag;
int yychar;
short *yyssp;
YYSTYPE *yyvsp;
YYSTYPE yyval;
YYSTYPE yylval;
short yyss[YYSTACKSIZE];
YYSTYPE yyvs[YYSTACKSIZE];
#define yystacksize YYSTACKSIZE
int
arith(shinstance *psh, const char *s)
{
	long result;

	INTOFF;
/* todo lock */
   arith_psh = psh;
	arith_buf = arith_startbuf = s;
	result = yyparse();
	arith_lex_reset();	/* reprime lex */
   arith_psh = NULL;
/* todo unlock */
	INTON;

	return (result);
}


/*
 *  The exp(1) builtin.
 */
int
expcmd(shinstance *psh, int argc, char **argv)
{
	const char *p;
	char *concat;
	char **ap;
	long i;

	if (argc > 1) {
		p = argv[1];
		if (argc > 2) {
			/*
			 * concatenate arguments
			 */
			STARTSTACKSTR(psh, concat);
			ap = argv + 2;
			for (;;) {
				while (*p)
					STPUTC(psh, *p++, concat);
				if ((p = *ap++) == NULL)
					break;
				STPUTC(psh, ' ', concat);
			}
			STPUTC(psh, '\0', concat);
			p = grabstackstr(psh, concat);
		}
	} else
		p = "";

	i = arith(psh, p);

	out1fmt(psh, "%ld\n", i);
	return (! i);
}

/*************************/
#ifdef TEST_ARITH
#include <stdio.h>
main(argc, argv)
	char *argv[];
{
	printf("%d\n", exp(argv[1]));
}
error(s)
	char *s;
{
	fprintf(stderr, "exp: %s\n", s);
	exit(1);
}
#endif

void
yyerror(const char *s)
{
   shinstance *psh = arith_psh;
	yyerrok;
	yyclearin;
	arith_lex_reset();	/* reprime lex */
/** @todo unlock */
	error(psh, "arithmetic expression: %s: \"%s\"", s, arith_startbuf);
	/* NOTREACHED */
}
#define YYABORT goto yyabort
#define YYREJECT goto yyabort
#define YYACCEPT goto yyaccept
#define YYERROR goto yyerrlab
#ifdef __cplusplus
extern "C" { 
char * getenv();
int yylex();
int yyparse();
}

#endif
int
#if defined(__STDC__)
yyparse(void)
#else
yyparse()
#endif
{
    register int yym, yyn, yystate;
#if YYDEBUG
    register char *yys;
#ifndef __cplusplus
    extern char *getenv();
#endif

    if (yys = getenv("YYDEBUG"))
    {
        yyn = *yys;
        if (yyn >= '0' && yyn <= '9')
            yydebug = yyn - '0';
    }
#endif

    yynerrs = 0;
    yyerrflag = 0;
    yychar = (-1);

    yyssp = yyss;
    yyvsp = yyvs;
    *yyssp = yystate = 0;

yyloop:
    if ((yyn = yydefred[yystate]) != 0) goto yyreduce;
    if (yychar < 0)
    {
        if ((yychar = yylex()) < 0) yychar = 0;
#if YYDEBUG
        if (yydebug)
        {
            yys = 0;
            if (yychar <= YYMAXTOKEN) yys = yyname[yychar];
            if (!yys) yys = "illegal-symbol";
            printf("%sdebug: state %d, reading %d (%s)\n",
                    YYPREFIX, yystate, yychar, yys);
        }
#endif
    }
    if ((yyn = yysindex[yystate]) && (yyn += yychar) >= 0 &&
            yyn <= YYTABLESIZE && yycheck[yyn] == yychar)
    {
#if YYDEBUG
        if (yydebug)
            printf("%sdebug: state %d, shifting to state %d\n",
                    YYPREFIX, yystate, yytable[yyn]);
#endif
        if (yyssp >= yyss + yystacksize - 1)
        {
            goto yyoverflow;
        }
        *++yyssp = yystate = yytable[yyn];
        *++yyvsp = yylval;
        yychar = (-1);
        if (yyerrflag > 0)  --yyerrflag;
        goto yyloop;
    }
    if ((yyn = yyrindex[yystate]) && (yyn += yychar) >= 0 &&
            yyn <= YYTABLESIZE && yycheck[yyn] == yychar)
    {
        yyn = yytable[yyn];
        goto yyreduce;
    }
    if (yyerrflag) goto yyinrecovery;
    yyerror("syntax error");
#ifdef lint
    goto yyerrlab;
#endif
yyerrlab:
    ++yynerrs;
yyinrecovery:
    if (yyerrflag < 3)
    {
        yyerrflag = 3;
        for (;;)
        {
            if ((yyn = yysindex[*yyssp]) && (yyn += YYERRCODE) >= 0 &&
                    yyn <= YYTABLESIZE && yycheck[yyn] == YYERRCODE)
            {
#if YYDEBUG
                if (yydebug)
                    printf("%sdebug: state %d, error recovery shifting\
 to state %d\n", YYPREFIX, *yyssp, yytable[yyn]);
#endif
                if (yyssp >= yyss + yystacksize - 1)
                {
                    goto yyoverflow;
                }
                *++yyssp = yystate = yytable[yyn];
                *++yyvsp = yylval;
                goto yyloop;
            }
            else
            {
#if YYDEBUG
                if (yydebug)
                    printf("%sdebug: error recovery discarding state %d\n",
                            YYPREFIX, *yyssp);
#endif
                if (yyssp <= yyss) goto yyabort;
                --yyssp;
                --yyvsp;
            }
        }
    }
    else
    {
        if (yychar == 0) goto yyabort;
#if YYDEBUG
        if (yydebug)
        {
            yys = 0;
            if (yychar <= YYMAXTOKEN) yys = yyname[yychar];
            if (!yys) yys = "illegal-symbol";
            printf("%sdebug: state %d, error recovery discards token %d (%s)\n",
                    YYPREFIX, yystate, yychar, yys);
        }
#endif
        yychar = (-1);
        goto yyloop;
    }
yyreduce:
#if YYDEBUG
    if (yydebug)
        printf("%sdebug: state %d, reducing by rule %d (%s)\n",
                YYPREFIX, yystate, yyn, yyrule[yyn]);
#endif
    yym = yylen[yyn];
    yyval = yyvsp[1-yym];
    switch (yyn)
    {
case 1:
{
			return (yyvsp[0]);
		}
break;
case 2:
{ yyval = yyvsp[-1]; }
break;
case 3:
{ yyval = yyvsp[-2] ? yyvsp[-2] : yyvsp[0] ? yyvsp[0] : 0; }
break;
case 4:
{ yyval = yyvsp[-2] ? ( yyvsp[0] ? yyvsp[0] : 0 ) : 0; }
break;
case 5:
{ yyval = yyvsp[-2] | yyvsp[0]; }
break;
case 6:
{ yyval = yyvsp[-2] ^ yyvsp[0]; }
break;
case 7:
{ yyval = yyvsp[-2] & yyvsp[0]; }
break;
case 8:
{ yyval = yyvsp[-2] == yyvsp[0]; }
break;
case 9:
{ yyval = yyvsp[-2] > yyvsp[0]; }
break;
case 10:
{ yyval = yyvsp[-2] >= yyvsp[0]; }
break;
case 11:
{ yyval = yyvsp[-2] < yyvsp[0]; }
break;
case 12:
{ yyval = yyvsp[-2] <= yyvsp[0]; }
break;
case 13:
{ yyval = yyvsp[-2] != yyvsp[0]; }
break;
case 14:
{ yyval = yyvsp[-2] << yyvsp[0]; }
break;
case 15:
{ yyval = yyvsp[-2] >> yyvsp[0]; }
break;
case 16:
{ yyval = yyvsp[-2] + yyvsp[0]; }
break;
case 17:
{ yyval = yyvsp[-2] - yyvsp[0]; }
break;
case 18:
{ yyval = yyvsp[-2] * yyvsp[0]; }
break;
case 19:
{
			if (yyvsp[0] == 0)
				yyerror("division by zero");
			yyval = yyvsp[-2] / yyvsp[0];
			}
break;
case 20:
{
			if (yyvsp[0] == 0)
				yyerror("division by zero");
			yyval = yyvsp[-2] % yyvsp[0];
			}
break;
case 21:
{ yyval = !(yyvsp[0]); }
break;
case 22:
{ yyval = ~(yyvsp[0]); }
break;
case 23:
{ yyval = -(yyvsp[0]); }
break;
case 24:
{ yyval = yyvsp[0]; }
break;
    }
    yyssp -= yym;
    yystate = *yyssp;
    yyvsp -= yym;
    yym = yylhs[yyn];
    if (yystate == 0 && yym == 0)
    {
#if YYDEBUG
        if (yydebug)
            printf("%sdebug: after reduction, shifting from state 0 to\
 state %d\n", YYPREFIX, YYFINAL);
#endif
        yystate = YYFINAL;
        *++yyssp = YYFINAL;
        *++yyvsp = yyval;
        if (yychar < 0)
        {
            if ((yychar = yylex()) < 0) yychar = 0;
#if YYDEBUG
            if (yydebug)
            {
                yys = 0;
                if (yychar <= YYMAXTOKEN) yys = yyname[yychar];
                if (!yys) yys = "illegal-symbol";
                printf("%sdebug: state %d, reading %d (%s)\n",
                        YYPREFIX, YYFINAL, yychar, yys);
            }
#endif
        }
        if (yychar == 0) goto yyaccept;
        goto yyloop;
    }
    if ((yyn = yygindex[yym]) && (yyn += yystate) >= 0 &&
            yyn <= YYTABLESIZE && yycheck[yyn] == yystate)
        yystate = yytable[yyn];
    else
        yystate = yydgoto[yym];
#if YYDEBUG
    if (yydebug)
        printf("%sdebug: after reduction, shifting from state %d \
to state %d\n", YYPREFIX, *yyssp, yystate);
#endif
    if (yyssp >= yyss + yystacksize - 1)
    {
        goto yyoverflow;
    }
    *++yyssp = yystate;
    *++yyvsp = yyval;
    goto yyloop;
yyoverflow:
    yyerror("yacc stack overflow");
yyabort:
    return (1);
yyaccept:
    return (0);
}
