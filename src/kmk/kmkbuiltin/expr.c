/*	$OpenBSD: expr.c,v 1.17 2006/06/21 18:28:24 deraadt Exp $	*/
/*	$NetBSD: expr.c,v 1.3.6.1 1996/06/04 20:41:47 cgd Exp $	*/

/*
 * Written by J.T. Conklin <jtc@netbsd.org>.
 * Public domain.
 */

#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <locale.h>
#include <ctype.h>
#ifdef KMK_WITH_REGEX
#include <regex.h>
#endif
#include <setjmp.h>
#include <assert.h>
#ifdef HAVE_ALLOCA_H
# include <alloca.h>
#endif
#include "err.h"
#include "kmkbuiltin.h"

typedef struct EXPRINSTANCE *PEXPRINSTANCE;

static struct val	*make_int(PEXPRINSTANCE, int);
static struct val	*make_str(PEXPRINSTANCE, char *);
static void		 free_value(PEXPRINSTANCE, struct val *);
static int		 is_integer(struct val *, int *);
static int		 to_integer(PEXPRINSTANCE, struct val *);
static void		 to_string(PEXPRINSTANCE, struct val *);
static int		 is_zero_or_null(PEXPRINSTANCE, struct val *);
static void		 nexttoken(PEXPRINSTANCE, int);
static struct val	*eval6(PEXPRINSTANCE);
static struct val	*eval5(PEXPRINSTANCE);
static struct val	*eval4(PEXPRINSTANCE);
static struct val	*eval3(PEXPRINSTANCE);
static struct val	*eval2(PEXPRINSTANCE);
static struct val	*eval1(PEXPRINSTANCE);
static struct val	*eval0(PEXPRINSTANCE);

enum token {
	OR, AND, EQ, LT, GT, ADD, SUB, MUL, DIV, MOD, MATCH, RP, LP,
	NE, LE, GE, OPERAND, EOI
};

struct val {
	enum {
		integer,
		string
	} type;

	union {
		char	       *s;
		int		i;
	} u;
};

typedef struct EXPRINSTANCE {
    PKMKBUILTINCTX  pCtx;
    enum token      token;
    struct val     *tokval;
    char          **av;
    jmp_buf         g_expr_jmp;
    void          **recorded_allocations;
    int             num_recorded_allocations;
} EXPRINSTANCE;


static void expr_mem_record_alloc(PEXPRINSTANCE pThis, void *ptr)
{
	if (!(pThis->num_recorded_allocations & 31)) {
    		void *newtab = realloc(pThis->recorded_allocations, (pThis->num_recorded_allocations + 33) * sizeof(void *));
		if (!newtab)
			longjmp(pThis->g_expr_jmp, err(pThis->pCtx, 3, NULL));
		pThis->recorded_allocations = (void **)newtab;
	}
	pThis->recorded_allocations[pThis->num_recorded_allocations++] = ptr;
}


static void expr_mem_record_free(PEXPRINSTANCE pThis, void *ptr)
{
	int i = pThis->num_recorded_allocations;
	while (i-- > 0)
		if (pThis->recorded_allocations[i] == ptr) {
			pThis->num_recorded_allocations--;
			pThis->recorded_allocations[i] = pThis->recorded_allocations[pThis->num_recorded_allocations];
			return;
		}
	assert(i >= 0);
}

static void expr_mem_init(PEXPRINSTANCE pThis)
{
	pThis->num_recorded_allocations = 0;
	pThis->recorded_allocations = NULL;
}

static void expr_mem_cleanup(PEXPRINSTANCE pThis)
{
	if (pThis->recorded_allocations) {
		while (pThis->num_recorded_allocations-- > 0)
			free(pThis->recorded_allocations[pThis->num_recorded_allocations]);
		free(pThis->recorded_allocations);
		pThis->recorded_allocations = NULL;
	}
}


static struct val *
make_int(PEXPRINSTANCE pThis, int i)
{
	struct val     *vp;

	vp = (struct val *) malloc(sizeof(*vp));
	if (vp == NULL)
		longjmp(pThis->g_expr_jmp, err(pThis->pCtx, 3, NULL));
	expr_mem_record_alloc(pThis, vp);
	vp->type = integer;
	vp->u.i = i;
	return vp;
}


static struct val *
make_str(PEXPRINSTANCE pThis, char *s)
{
	struct val     *vp;

	vp = (struct val *) malloc(sizeof(*vp));
	if (vp == NULL || ((vp->u.s = strdup(s)) == NULL))
		longjmp(pThis->g_expr_jmp, err(pThis->pCtx, 3, NULL));
	expr_mem_record_alloc(pThis, vp->u.s);
	expr_mem_record_alloc(pThis, vp);
	vp->type = string;
	return vp;
}


static void
free_value(PEXPRINSTANCE pThis, struct val *vp)
{
	if (vp->type == string) {
		expr_mem_record_free(pThis, vp->u.s);
		free(vp->u.s);
	}
	free(vp);
	expr_mem_record_free(pThis, vp);
}


/* determine if vp is an integer; if so, return it's value in *r */
static int
is_integer(struct val *vp, int *r)
{
	char	       *s;
	int		neg;
	int		i;

	if (vp->type == integer) {
		*r = vp->u.i;
		return 1;
	}

	/*
	 * POSIX.2 defines an "integer" as an optional unary minus
	 * followed by digits.
	 */
	s = vp->u.s;
	i = 0;

	neg = (*s == '-');
	if (neg)
		s++;

	while (*s) {
		if (!isdigit(*s))
			return 0;

		i *= 10;
		i += *s - '0';

		s++;
	}

	if (neg)
		i *= -1;

	*r = i;
	return 1;
}


/* coerce to vp to an integer */
static int
to_integer(PEXPRINSTANCE pThis, struct val *vp)
{
	int		r;

	if (vp->type == integer)
		return 1;

	if (is_integer(vp, &r)) {
		expr_mem_record_free(pThis, vp->u.s);
		free(vp->u.s);
		vp->u.i = r;
		vp->type = integer;
		return 1;
	}

	return 0;
}


/* coerce to vp to an string */
static void
to_string(PEXPRINSTANCE pThis, struct val *vp)
{
	char	       *tmp;

	if (vp->type == string)
		return;

	if (asprintf(&tmp, "%d", vp->u.i) == -1)
		longjmp(pThis->g_expr_jmp, err(pThis->pCtx, 3, NULL));
	expr_mem_record_alloc(pThis, tmp);

	vp->type = string;
	vp->u.s = tmp;
}

static int
is_zero_or_null(PEXPRINSTANCE pThis, struct val *vp)
{
	if (vp->type == integer) {
		return (vp->u.i == 0);
	} else {
		return (*vp->u.s == 0 || (to_integer(pThis, vp) && vp->u.i == 0));
	}
	/* NOTREACHED */
}

static void
nexttoken(PEXPRINSTANCE pThis, int pat)
{
	char	       *p;

	if ((p = *pThis->av) == NULL) {
		pThis->token = EOI;
		return;
	}
	pThis->av++;


	if (pat == 0 && p[0] != '\0') {
		if (p[1] == '\0') {
			const char     *x = "|&=<>+-*/%:()";
			char	       *i;	/* index */

			if ((i = strchr(x, *p)) != NULL) {
				pThis->token = i - x;
				return;
			}
		} else if (p[1] == '=' && p[2] == '\0') {
			switch (*p) {
			case '<':
				pThis->token = LE;
				return;
			case '>':
				pThis->token = GE;
				return;
			case '!':
				pThis->token = NE;
				return;
			}
		}
	}
	pThis->tokval = make_str(pThis, p);
	pThis->token = OPERAND;
	return;
}

#ifdef __GNUC__
__attribute__((noreturn))
#endif
#ifdef _MSC_VER
__declspec(noreturn)
#endif
static void
error(PEXPRINSTANCE pThis)
{
	longjmp(pThis->g_expr_jmp, errx(pThis->pCtx, 2, "syntax error"));
	/* NOTREACHED */
}

static struct val *
eval6(PEXPRINSTANCE pThis)
{
	struct val     *v;

	if (pThis->token == OPERAND) {
		nexttoken(pThis, 0);
		return pThis->tokval;

	} else if (pThis->token == RP) {
		nexttoken(pThis, 0);
		v = eval0(pThis);

		if (pThis->token != LP) {
			error(pThis);
			/* NOTREACHED */
		}
		nexttoken(pThis, 0);
		return v;
	} else {
		error(pThis);
	}
	/* NOTREACHED */
}

/* Parse and evaluate match (regex) expressions */
static struct val *
eval5(PEXPRINSTANCE pThis)
{
#ifdef KMK_WITH_REGEX
	regex_t		rp;
	regmatch_t	rm[2];
	char		errbuf[256];
	int		eval;
	struct val     *r;
	struct val     *v;
#endif
	struct val     *l;

	l = eval6(pThis);
	while (pThis->token == MATCH) {
#ifdef KMK_WITH_REGEX
		nexttoken(pThis, 1);
		r = eval6(pThis);

		/* coerce to both arguments to strings */
		to_string(pThis, l);
		to_string(pThis, r);

		/* compile regular expression */
		if ((eval = regcomp(&rp, r->u.s, 0)) != 0) {
			regerror(eval, &rp, errbuf, sizeof(errbuf));
			longjmp(pThis->g_expr_jmp, errx(pThis->pCtx, 2, "%s", errbuf));
		}

		/* compare string against pattern --  remember that patterns
		   are anchored to the beginning of the line */
		if (regexec(&rp, l->u.s, 2, rm, 0) == 0 && rm[0].rm_so == 0) {
			if (rm[1].rm_so >= 0) {
				*(l->u.s + rm[1].rm_eo) = '\0';
				v = make_str(pThis, l->u.s + rm[1].rm_so);

			} else {
				v = make_int(pThis, (int)(rm[0].rm_eo - rm[0].rm_so));
			}
		} else {
			if (rp.re_nsub == 0) {
				v = make_int(pThis, 0);
			} else {
				v = make_str(pThis, "");
			}
		}

		/* free arguments and pattern buffer */
		free_value(pThis, l);
		free_value(pThis, r);
		regfree(&rp);

		l = v;
#else
		longjmp(pThis->g_expr_jmp, errx(pThis->pCtx, 2, "regex not supported, sorry."));
#endif
	}

	return l;
}

/* Parse and evaluate multiplication and division expressions */
static struct val *
eval4(PEXPRINSTANCE pThis)
{
	struct val     *l, *r;
	enum token	op;

	l = eval5(pThis);
	while ((op = pThis->token) == MUL || op == DIV || op == MOD) {
		nexttoken(pThis, 0);
		r = eval5(pThis);

		if (!to_integer(pThis, l) || !to_integer(pThis, r)) {
			longjmp(pThis->g_expr_jmp, errx(pThis->pCtx, 2, "non-numeric argument"));
		}

		if (op == MUL) {
			l->u.i *= r->u.i;
		} else {
			if (r->u.i == 0) {
				longjmp(pThis->g_expr_jmp, errx(pThis->pCtx, 2, "division by zero"));
			}
			if (op == DIV) {
				l->u.i /= r->u.i;
			} else {
				l->u.i %= r->u.i;
			}
		}

		free_value(pThis, r);
	}

	return l;
}

/* Parse and evaluate addition and subtraction expressions */
static struct val *
eval3(PEXPRINSTANCE pThis)
{
	struct val     *l, *r;
	enum token	op;

	l = eval4(pThis);
	while ((op = pThis->token) == ADD || op == SUB) {
		nexttoken(pThis, 0);
		r = eval4(pThis);

		if (!to_integer(pThis, l) || !to_integer(pThis, r)) {
			longjmp(pThis->g_expr_jmp, errx(pThis->pCtx, 2, "non-numeric argument"));
		}

		if (op == ADD) {
			l->u.i += r->u.i;
		} else {
			l->u.i -= r->u.i;
		}

		free_value(pThis, r);
	}

	return l;
}

/* Parse and evaluate comparison expressions */
static struct val *
eval2(PEXPRINSTANCE pThis)
{
	struct val     *l, *r;
	enum token	op;
	int		v = 0, li, ri;

	l = eval3(pThis);
	while ((op = pThis->token) == EQ || op == NE || op == LT || op == GT ||
	    op == LE || op == GE) {
		nexttoken(pThis, 0);
		r = eval3(pThis);

		if (is_integer(l, &li) && is_integer(r, &ri)) {
			switch (op) {
			case GT:
				v = (li >  ri);
				break;
			case GE:
				v = (li >= ri);
				break;
			case LT:
				v = (li <  ri);
				break;
			case LE:
				v = (li <= ri);
				break;
			case EQ:
				v = (li == ri);
				break;
			case NE:
				v = (li != ri);
				break;
			default:
				break;
			}
		} else {
			to_string(pThis, l);
			to_string(pThis, r);

			switch (op) {
			case GT:
				v = (strcoll(l->u.s, r->u.s) > 0);
				break;
			case GE:
				v = (strcoll(l->u.s, r->u.s) >= 0);
				break;
			case LT:
				v = (strcoll(l->u.s, r->u.s) < 0);
				break;
			case LE:
				v = (strcoll(l->u.s, r->u.s) <= 0);
				break;
			case EQ:
				v = (strcoll(l->u.s, r->u.s) == 0);
				break;
			case NE:
				v = (strcoll(l->u.s, r->u.s) != 0);
				break;
			default:
				break;
			}
		}

		free_value(pThis, l);
		free_value(pThis, r);
		l = make_int(pThis, v);
	}

	return l;
}

/* Parse and evaluate & expressions */
static struct val *
eval1(PEXPRINSTANCE pThis)
{
	struct val     *l, *r;

	l = eval2(pThis);
	while (pThis->token == AND) {
		nexttoken(pThis, 0);
		r = eval2(pThis);

		if (is_zero_or_null(pThis, l) || is_zero_or_null(pThis, r)) {
			free_value(pThis, l);
			free_value(pThis, r);
			l = make_int(pThis, 0);
		} else {
			free_value(pThis, r);
		}
	}

	return l;
}

/* Parse and evaluate | expressions */
static struct val *
eval0(PEXPRINSTANCE pThis)
{
	struct val     *l, *r;

	l = eval1(pThis);
	while (pThis->token == OR) {
		nexttoken(pThis, 0);
		r = eval1(pThis);

		if (is_zero_or_null(pThis, l)) {
			free_value(pThis, l);
			l = r;
		} else {
			free_value(pThis, r);
		}
	}

	return l;
}


int
kmk_builtin_expr(int argc, char **argv, char **envp, PKMKBUILTINCTX pCtx)
{
	EXPRINSTANCE This;
	struct val *vp;
	int rval;

	if (argc > 1 && !strcmp(argv[1], "--"))
		argv++;

	/* Init globals */
	This.pCtx = pCtx;
	This.token = 0;
	This.tokval = 0;
	This.av = argv + 1;
	expr_mem_init(&This);

	rval = setjmp(This.g_expr_jmp);
	if (!rval) {
		nexttoken(&This, 0);
		vp = eval0(&This);

		if (This.token != EOI) {
			error(&This);
			/* NOTREACHED */
		}

		if (vp->type == integer)
			kmk_builtin_ctx_printf(pCtx, 0, "%d\n", vp->u.i);
		else
			kmk_builtin_ctx_printf(pCtx, 0, "%s\n", vp->u.s);

		rval = is_zero_or_null(&This, vp);
	}
	/* else: longjmp */

	/* cleanup */
	expr_mem_cleanup(&This);
	return rval;
}

#ifdef KMK_BUILTIN_STANDALONE
int main(int argc, char **argv, char **envp)
{
	KMKBUILTINCTX Ctx = { "kmk_expr", NULL };
	(void) setlocale(LC_ALL, "");
	return kmk_builtin_expr(argc, argv, envp, &Ctx);
}
#endif

