/*	$NetBSD: printf.c,v 1.31 2005/03/22 23:55:46 dsl Exp $	*/

/*
 * Copyright (c) 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
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

/*#include <sys/cdefs.h>
#ifndef lint
#if !defined(BUILTIN) && !defined(SHELL)
__COPYRIGHT("@(#) Copyright (c) 1989, 1993\n\
	The Regents of the University of California.  All rights reserved.\n");
#endif
#endif

#ifndef lint
#if 0
static char sccsid[] = "@(#)printf.c	8.2 (Berkeley) 3/22/95";
#else
__RCSID("$NetBSD: printf.c,v 1.31 2005/03/22 23:55:46 dsl Exp $");
#endif
#endif*/ /* not lint */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define FAKES_NO_GETOPT_H /* bird */
#if !defined(KMK_BUILTIN_STANDALONE) && !defined(BUILTIN) && !defined(SHELL)
# include "../makeint.h"
# include "../filedef.h"
# include "../variable.h"
#else
# include "config.h"
#endif
#include <sys/types.h>

#include <ctype.h>
#include "err.h"
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <locale.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "getopt_r.h"
#ifdef __sun__
# include "solfakes.h"
#endif
#ifdef _MSC_VER
# include "mscfakes.h"
#endif

#include "../kmkbuiltin.h"

#ifdef KBUILD_OS_WINDOWS
/* This is a trick to speed up console output on windows. */
# include "console.h"
# undef fwrite
# define fwrite maybe_con_fwrite
#endif

#if 0
#ifdef BUILTIN		/* csh builtin */
#define kmk_builtin_printf progprintf
#endif

#ifdef SHELL		/* sh (aka ash) builtin */
#define kmk_builtin_printf printfcmd
#include "../../bin/sh/bltin/bltin.h"
#endif /* SHELL */
#endif


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#if 0 /*def __GNUC__ - bird: gcc complains about non-ISO-standard escape. */
#define ESCAPE '\e'
#else
#define ESCAPE 033
#endif

#define PF(f, func) { \
	if (fieldwidth != -1) { \
		if (precision != -1) \
			(void)wrap_printf(pThis, f, fieldwidth, precision, func); \
		else \
			(void)wrap_printf(pThis, f, fieldwidth, func); \
	} else if (precision != -1) \
		(void)wrap_printf(pThis, f, precision, func); \
	else \
		(void)wrap_printf(pThis, f, func); \
}

#define APF(cpp, f, func) { \
	if (fieldwidth != -1) { \
		if (precision != -1) \
			(void)asprintf(cpp, f, fieldwidth, precision, func); \
		else \
			(void)asprintf(cpp, f, fieldwidth, func); \
	} else if (precision != -1) \
		(void)asprintf(cpp, f, precision, func); \
	else \
		(void)asprintf(cpp, f, func); \
}


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
typedef struct PRINTFINSTANCE
{
    PKMKBUILTINCTX pCtx;
    /* former globals */
    size_t b_length;
    char *b_fmt;
    int	rval;
    char **gargv;
#ifndef KMK_BUILTIN_STANDALONE
    char *g_o;
#endif
    /* former function level statics in common_printf(); both need freeing. */
    char *a, *t;

    /* former function level statics in conv_expand(); needs freeing. */
    char *conv_str;

    /* Buffer the output because windows doesn't do line buffering of stdout. */
    size_t g_cchBuf;
    char g_achBuf[256];
} PRINTFINSTANCE;
typedef PRINTFINSTANCE *PPRINTFINSTANCE;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
static struct option long_options[] =
{
    { "help",   					no_argument, 0, 261 },
    { "version",   					no_argument, 0, 262 },
    { 0, 0,	0, 0 },
};


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static int 	 common_printf(PPRINTFINSTANCE pThis, char *argv[], PKMKBUILTINCTX pCtx);
static int 	 common_printf_inner(PPRINTFINSTANCE pThis, char *argv[]);
static void	 conv_escape_str(PPRINTFINSTANCE, char *, void (*)(PPRINTFINSTANCE, int));
static char	*conv_escape(PPRINTFINSTANCE, char *, char *);
static const char *conv_expand(PPRINTFINSTANCE, const char *);
static int	 getchr(PPRINTFINSTANCE);
static double	 getdouble(PPRINTFINSTANCE);
static int	 getwidth(PPRINTFINSTANCE);
static intmax_t	 getintmax(PPRINTFINSTANCE);
static uintmax_t getuintmax(PPRINTFINSTANCE);
static char	*getstr(PPRINTFINSTANCE);
static char	*mklong(PPRINTFINSTANCE, const char *, int, char[64]);
static void      check_conversion(PPRINTFINSTANCE, const char *, const char *);
static int	 usage(PKMKBUILTINCTX, int);

static int	flush_buffer(PPRINTFINSTANCE);
static void	b_count(PPRINTFINSTANCE, int);
static void	b_output(PPRINTFINSTANCE, int);
static int	wrap_putchar(PPRINTFINSTANCE, int ch);
static int	wrap_printf(PPRINTFINSTANCE, const char *, ...);



int kmk_builtin_printf(int argc, char **argv, char **envp, PKMKBUILTINCTX pCtx)
{
	PRINTFINSTANCE This;
	struct getopt_state_r gos;
	int ch;

	getopt_initialize_r(&gos, argc, argv, "", long_options, envp, pCtx);
	while ((ch = getopt_long_r(&gos, NULL)) != -1) {
		switch (ch) {
		case 261:
			usage(pCtx, 0);
			return 0;
		case 262:
			return kbuild_version(argv[0]);
		case '?':
		default:
			return usage(pCtx, 1);
		}
	}
	argc -= gos.optind;
	argv += gos.optind;

	if (argc < 1)
		return usage(pCtx, 1);

#ifndef KMK_BUILTIN_STANDALONE
	This.g_o = NULL;
#endif
	return common_printf(&This, argv, pCtx);
}

#ifdef KMK_BUILTIN_STANDALONE
int main(int argc, char **argv, char **envp)
{
	KMKBUILTINCTX Ctx = { "kmk_printf", NULL };
	setlocale(LC_ALL, "");
	return kmk_builtin_printf(argc, argv, envp, &Ctx);
}
#else /* KMK_BUILTIN_STANDALONE */
/* entry point used by function.c $(printf ..,..). */
char *kmk_builtin_func_printf(char *o, char **argv, const char *funcname)
{
	PRINTFINSTANCE This;
	int rc;
	int argc;

	for (argc = 0; argv[argc] != NULL; argc++)
		/* nothing */;
	if (argc == 0)
	    fatal(NILF, strlen(funcname) + INTSTR_LENGTH, _("$(%s): no format string\n"), funcname);

	This.g_o = o;
	rc = common_printf(&This, argv, NULL);
	o = This.g_o;

	if (rc != 0)
		fatal(NILF, strlen(funcname) + INTSTR_LENGTH, _("$(%s): failure rc=%d\n"), funcname, rc);
	return o;
}
#endif /* KMK_BUILTIN_STANDALONE */

static int common_printf(PPRINTFINSTANCE pThis, char *argv[], PKMKBUILTINCTX pCtx)
{
	int rc;

	/* Init all but g_o. */
	pThis->pCtx = pCtx;
	pThis->b_length = 0;
	pThis->b_fmt = NULL;
	pThis->rval = 0;
	pThis->gargv = NULL;
	pThis->g_cchBuf = 0;
	pThis->a = NULL;
	pThis->t = NULL;
	pThis->conv_str = NULL;

	rc = common_printf_inner(pThis, argv);

	/* Cleanup allocations. */
	if (pThis->a) {
		free(pThis->a);
		pThis->a = NULL;
	}
	if (pThis->t) {
		free(pThis->t);
		pThis->t = NULL;
	}
	if (pThis->conv_str) {
		free(pThis->conv_str);
		pThis->conv_str = NULL;
	}
	return rc;
}

static int common_printf_inner(PPRINTFINSTANCE pThis, char *argv[])
{
	char *fmt, *start;
	int fieldwidth, precision;
	char nextch;
	char *format;
	int ch;
	char longbuf[64];

	format = *argv;
	pThis->gargv = ++argv;

#define SKIP1	"#-+ 0"
#define SKIP2	"*0123456789"
	do {
		/*
		 * Basic algorithm is to scan the format string for conversion
		 * specifications -- once one is found, find out if the field
		 * width or precision is a '*'; if it is, gather up value.
		 * Note, format strings are reused as necessary to use up the
		 * provided arguments, arguments of zero/null string are
		 * provided to use up the format string.
		 */

		/* find next format specification */
		for (fmt = format; (ch = *fmt++) != '\0';) {
			if (ch == '\\') {
				char c_ch;
				fmt = conv_escape(pThis, fmt, &c_ch);
				wrap_putchar(pThis, c_ch);
				continue;
			}
			if (ch != '%' || (*fmt == '%' && ++fmt)) {
				(void)wrap_putchar(pThis, ch);
				continue;
			}

			/* Ok - we've found a format specification,
			   Save its address for a later printf(). */
			start = fmt - 1;

			/* skip to field width */
			fmt += strspn(fmt, SKIP1);
			fieldwidth = *fmt == '*' ? getwidth(pThis) : -1;

			/* skip to possible '.', get following precision */
			fmt += strspn(fmt, SKIP2);
			if (*fmt == '.')
				++fmt;
			precision = *fmt == '*' ? getwidth(pThis) : -1;

			fmt += strspn(fmt, SKIP2);

			ch = *fmt;
			if (!ch) {
				flush_buffer(pThis);
				warnx(pThis->pCtx, "missing format character");
				return (1);
			}
			/* null terminate format string to we can use it
			   as an argument to printf. */
			nextch = fmt[1];
			fmt[1] = 0;
			switch (ch) {

			case 'B': {
				const char *p = conv_expand(pThis, getstr(pThis));
				*fmt = 's';
				PF(start, p);
				break;
			}
			case 'b': {
				/* There has to be a better way to do this,
				 * but the string we generate might have
				 * embedded nulls. */
				char *cp = getstr(pThis);
				/* Free on entry in case shell longjumped out */
				if (pThis->a != NULL) {
					free(pThis->a);
					pThis->a = NULL;
				}
				if (pThis->t != NULL) {
					free(pThis->t);
					pThis->t = NULL;
				}
				/* Count number of bytes we want to output */
				pThis->b_length = 0;
				conv_escape_str(pThis, cp, b_count);
				pThis->t = malloc(pThis->b_length + 1);
				if (pThis->t == NULL)
					break;
				memset(pThis->t, 'x', pThis->b_length);
				pThis->t[pThis->b_length] = 0;
				/* Get printf to calculate the lengths */
				*fmt = 's';
				APF(&pThis->a, start, pThis->t);
				pThis->b_fmt = pThis->a;
				/* Output leading spaces and data bytes */
				conv_escape_str(pThis, cp, b_output);
				/* Add any trailing spaces */
				wrap_printf(pThis, "%s", pThis->b_fmt);
				break;
			}
			case 'c': {
				char p = getchr(pThis);
				PF(start, p);
				break;
			}
			case 's': {
				char *p = getstr(pThis);
				PF(start, p);
				break;
			}
			case 'd':
			case 'i': {
				intmax_t p = getintmax(pThis);
				char *f = mklong(pThis, start, ch, longbuf);
				PF(f, p);
				break;
			}
			case 'o':
			case 'u':
			case 'x':
			case 'X': {
				uintmax_t p = getuintmax(pThis);
				char *f = mklong(pThis, start, ch, longbuf);
				PF(f, p);
				break;
			}
			case 'e':
			case 'E':
			case 'f':
			case 'g':
			case 'G': {
				double p = getdouble(pThis);
				PF(start, p);
				break;
			}
			default:
				flush_buffer(pThis);
				warnx(pThis->pCtx, "%s: invalid directive", start);
				return 1;
			}
			*fmt++ = ch;
			*fmt = nextch;
			/* escape if a \c was encountered */
			if (pThis->rval & 0x100) {
				flush_buffer(pThis);
				return pThis->rval & ~0x100;
			}
		}
	} while (pThis->gargv != argv && *pThis->gargv);

	flush_buffer(pThis);
	return pThis->rval;
}


/* helper functions for conv_escape_str */

static void
/*ARGSUSED*/
b_count(PPRINTFINSTANCE pThis, int ch)
{
	pThis->b_length++;
	(void)ch;
}

/* Output one converted character for every 'x' in the 'format' */

static void
b_output(PPRINTFINSTANCE pThis, int ch)
{
	for (;;) {
		switch (*pThis->b_fmt++) {
		case 0:
			pThis->b_fmt--;
			return;
		case ' ':
			wrap_putchar(pThis, ' ');
			break;
		default:
			wrap_putchar(pThis, ch);
			return;
		}
	}
}

static int wrap_putchar(PPRINTFINSTANCE pThis, int ch)
{
#ifndef KMK_BUILTIN_STANDALONE
	if (pThis->g_o) {
		char sz[2];
		sz[0] = ch; sz[1] = '\0';
		pThis->g_o = variable_buffer_output(pThis->g_o, sz, 1);
	}
	else
#endif
	/* Buffered output. */
	if (pThis->g_cchBuf + 1 < sizeof(pThis->g_achBuf)) {
		pThis->g_achBuf[pThis->g_cchBuf++] = ch;
	} else {
		int rc = flush_buffer(pThis);
		pThis->g_achBuf[pThis->g_cchBuf++] = ch;
		if (rc)
			return -1;
	}
	return 0;
}

static int wrap_printf(PPRINTFINSTANCE pThis, const char * fmt, ...)
{
	ssize_t cchRet;
	va_list va;
	char *pszTmp;

	va_start(va, fmt);
	cchRet = vasprintf(&pszTmp, fmt, va);
	va_end(va);
	if (cchRet >= 0) {
#ifndef KMK_BUILTIN_STANDALONE
		if (pThis->g_o) {
			pThis->g_o = variable_buffer_output(pThis->g_o, pszTmp, cchRet);
		} else
#endif
		{
			if (cchRet + pThis->g_cchBuf <= sizeof(pThis->g_achBuf)) {
				/* We've got space in the buffer. */
				memcpy(&pThis->g_achBuf[pThis->g_cchBuf], pszTmp, cchRet);
				pThis->g_cchBuf += cchRet;
			} else {
				/* Try write out complete lines. */
				const char *pszLeft = pszTmp;
				ssize_t     cchLeft = cchRet;

				while (cchLeft > 0) {
					const char *pchNewLine = strchr(pszLeft, '\n');
					ssize_t     cchLine    = pchNewLine ? pchNewLine - pszLeft + 1 : cchLeft;
					if (pThis->g_cchBuf + cchLine <= sizeof(pThis->g_achBuf)) {
						memcpy(&pThis->g_achBuf[pThis->g_cchBuf], pszLeft, cchLine);
						pThis->g_cchBuf += cchLine;
					} else {
						if (flush_buffer(pThis) < 0) {
							return -1;
						}
#ifndef KMK_BUILTIN_STANDALONE
						if (output_write_text(pThis->pCtx->pOut, 0,pszLeft, cchLine) < 1)
#else
						if (fwrite(pszLeft, cchLine, 1, stdout) < 1)
#endif

							return -1;
					}
					pszLeft += cchLine;
					cchLeft -= cchLine;
				}
			}
		}
		free(pszTmp);
	}
	return (int)cchRet;
}

/**
 * Flushes the g_abBuf/g_cchBuf.
 */
static int flush_buffer(PPRINTFINSTANCE pThis)
{
    ssize_t cchToWrite = pThis->g_cchBuf;
    if (cchToWrite > 0) {
#ifndef KMK_BUILTIN_STANDALONE
		ssize_t cchWritten = output_write_text(pThis->pCtx->pOut, 0, pThis->g_achBuf, cchToWrite);
#else
		ssize_t cchWritten = fwrite(pThis->g_achBuf, 1, cchToWrite, stdout);
#endif
		pThis->g_cchBuf = 0;
		if (cchWritten >= cchToWrite) {
			/* likely */
		} else {
			ssize_t off = cchWritten;
			if (cchWritten >= 0) {
				off = cchWritten;
			} else if (errno == EINTR) {
				cchWritten = 0;
			} else {
				return -1;
			}

			while (off < cchToWrite) {
#ifndef KMK_BUILTIN_STANDALONE
				cchWritten = output_write_text(pThis->pCtx->pOut, 0, &pThis->g_achBuf[off], cchToWrite - off);
#else
				cchWritten = fwrite(&pThis->g_achBuf[off], 1, cchToWrite - off, stdout);
#endif
				if (cchWritten > 0) {
					off += cchWritten;
				} else if (errno == EINTR) {
					/* nothing */
				} else {
					return -1;
				}
			}
		}
    }
    return 0;
}



/*
 * Print SysV echo(1) style escape string
 *	Halts processing string if a \c escape is encountered.
 */
static void
conv_escape_str(PPRINTFINSTANCE pThis, char *str, void (*do_putchar)(PPRINTFINSTANCE, int))
{
	int value;
	int ch;
	char c;

	while ((ch = *str++) != '\0') {
		if (ch != '\\') {
			do_putchar(pThis, ch);
			continue;
		}

		ch = *str++;
		if (ch == 'c') {
			/* \c as in SYSV echo - abort all processing.... */
			pThis->rval |= 0x100;
			break;
		}

		/*
		 * %b string octal constants are not like those in C.
		 * They start with a \0, and are followed by 0, 1, 2,
		 * or 3 octal digits.
		 */
		if (ch == '0') {
			int octnum = 0, i;
			for (i = 0; i < 3; i++) {
				if (!isdigit((unsigned char)*str) || *str > '7')
					break;
				octnum = (octnum << 3) | (*str++ - '0');
			}
			do_putchar(pThis, octnum);
			continue;
		}

		/* \[M][^|-]C as defined by vis(3) */
		if (ch == 'M' && *str == '-') {
			do_putchar(pThis, 0200 | str[1]);
			str += 2;
			continue;
		}
		if (ch == 'M' && *str == '^') {
			str++;
			value = 0200;
			ch = '^';
		} else
			value = 0;
		if (ch == '^') {
			ch = *str++;
			if (ch == '?')
				value |= 0177;
			else
				value |= ch & 037;
			do_putchar(pThis, value);
			continue;
		}

		/* Finally test for sequences valid in the format string */
		str = conv_escape(pThis, str - 1, &c);
		do_putchar(pThis, c);
	}
}

/*
 * Print "standard" escape characters
 */
static char *
conv_escape(PPRINTFINSTANCE pThis, char *str, char *conv_ch)
{
	int value;
	int ch;
	char num_buf[4], *num_end;

	ch = *str++;

	switch (ch) {
	case '0': case '1': case '2': case '3':
	case '4': case '5': case '6': case '7':
		num_buf[0] = ch;
		ch = str[0];
		num_buf[1] = ch;
		num_buf[2] = ch ? str[1] : 0;
		num_buf[3] = 0;
		value = strtoul(num_buf, &num_end, 8);
		str += num_end  - (num_buf + 1);
		break;

	case 'x':
		/* Hexadecimal character constants are not required to be
		   supported (by SuS v1) because there is no consistent
		   way to detect the end of the constant.
		   Supporting 2 byte constants is a compromise. */
		ch = str[0];
		num_buf[0] = ch;
		num_buf[1] = ch ? str[1] : 0;
		num_buf[2] = 0;
		value = strtoul(num_buf, &num_end, 16);
		str += num_end - num_buf;
		break;

	case '\\':	value = '\\';	break;	/* backslash */
	case '\'':	value = '\'';	break;	/* single quote */
	case '"':	value = '"';	break;	/* double quote */
	case 'a':	value = '\a';	break;	/* alert */
	case 'b':	value = '\b';	break;	/* backspace */
	case 'e':	value = ESCAPE;	break;	/* escape */
	case 'f':	value = '\f';	break;	/* form-feed */
	case 'n':	value = '\n';	break;	/* newline */
	case 'r':	value = '\r';	break;	/* carriage-return */
	case 't':	value = '\t';	break;	/* tab */
	case 'v':	value = '\v';	break;	/* vertical-tab */

	default:
		warnx(pThis->pCtx, "unknown escape sequence `\\%c'", ch);
		pThis->rval = 1;
		value = ch;
		break;
	}

	*conv_ch = value;
	return str;
}

/* expand a string so that everything is printable */

static const char *
conv_expand(PPRINTFINSTANCE pThis, const char *str)
{
	static const char no_memory[] = "<no memory>";
	char *cp;
	int ch;

	if (pThis->conv_str)
		free(pThis->conv_str);
	/* get a buffer that is definitely large enough.... */
	pThis->conv_str = cp = malloc(4 * strlen(str) + 1);
	if (!cp)
		return no_memory;

	while ((ch = *(const unsigned char *)str++) != '\0') {
		switch (ch) {
		/* Use C escapes for expected control characters */
		case '\\':	ch = '\\';	break;	/* backslash */
		case '\'':	ch = '\'';	break;	/* single quote */
		case '"':	ch = '"';	break;	/* double quote */
		case '\a':	ch = 'a';	break;	/* alert */
		case '\b':	ch = 'b';	break;	/* backspace */
		case ESCAPE:	ch = 'e';	break;	/* escape */
		case '\f':	ch = 'f';	break;	/* form-feed */
		case '\n':	ch = 'n';	break;	/* newline */
		case '\r':	ch = 'r';	break;	/* carriage-return */
		case '\t':	ch = 't';	break;	/* tab */
		case '\v':	ch = 'v';	break;	/* vertical-tab */
		default:
			/* Copy anything printable */
			if (isprint(ch)) {
				*cp++ = ch;
				continue;
			}
			/* Use vis(3) encodings for the rest */
			*cp++ = '\\';
			if (ch & 0200) {
				*cp++ = 'M';
				ch &= ~0200;
			}
			if (ch == 0177) {
				*cp++ = '^';
				*cp++ = '?';
				continue;
			}
			if (ch < 040) {
				*cp++ = '^';
				*cp++ = ch | 0100;
				continue;
			}
			*cp++ = '-';
			*cp++ = ch;
			continue;
		}
		*cp++ = '\\';
		*cp++ = ch;
	}

	*cp = 0;
	return pThis->conv_str;
}

static char *
mklong(PPRINTFINSTANCE pThis, const char *str, int ch, char copy[64])
{
	size_t len;

	len = strlen(str) - 1;
	if (len > 64 - 5) {
		warnx(pThis->pCtx, "format %s too complex\n", str);
		len = 4;
	}
	(void)memmove(copy, str, len);
#ifndef _MSC_VER
	copy[len++] = 'j';
#else
	copy[len++] = 'I';
	copy[len++] = '6';
	copy[len++] = '4';
#endif
	copy[len++] = ch;
	copy[len] = '\0';
	return copy;
}

static int
getchr(PPRINTFINSTANCE pThis)
{
	if (!*pThis->gargv)
		return 0;
	return (int)**pThis->gargv++;
}

static char *
getstr(PPRINTFINSTANCE pThis)
{
	static char empty[] = "";
	if (!*pThis->gargv)
		return empty;
	return *pThis->gargv++;
}

static int
getwidth(PPRINTFINSTANCE pThis)
{
	long val;
	char *s, *ep;

	s = *pThis->gargv;
	if (!s)
		return (0);
	pThis->gargv++;

	errno = 0;
	val = strtoul(s, &ep, 0);
	check_conversion(pThis, s, ep);

	/* Arbitrarily 'restrict' field widths to 1Mbyte */
	if (val < 0 || val > 1 << 20) {
		warnx(pThis->pCtx, "%s: invalid field width", s);
		return 0;
	}

	return val;
}

static intmax_t
getintmax(PPRINTFINSTANCE pThis)
{
	intmax_t val;
	char *cp, *ep;

	cp = *pThis->gargv;
	if (cp == NULL)
		return 0;
	pThis->gargv++;

	if (*cp == '\"' || *cp == '\'')
		return *(cp+1);

	errno = 0;
	val = strtoimax(cp, &ep, 0);
	check_conversion(pThis, cp, ep);
	return val;
}

static uintmax_t
getuintmax(PPRINTFINSTANCE pThis)
{
	uintmax_t val;
	char *cp, *ep;

	cp = *pThis->gargv;
	if (cp == NULL)
		return 0;
	pThis->gargv++;

	if (*cp == '\"' || *cp == '\'')
		return *(cp + 1);

	/* strtoumax won't error -ve values */
	while (isspace(*(unsigned char *)cp))
		cp++;
	if (*cp == '-') {
		warnx(pThis->pCtx, "%s: expected positive numeric value", cp);
		pThis->rval = 1;
		return 0;
	}

	errno = 0;
	val = strtoumax(cp, &ep, 0);
	check_conversion(pThis, cp, ep);
	return val;
}

static double
getdouble(PPRINTFINSTANCE pThis)
{
	double val;
	char *ep;
	char *s;

	s = *pThis->gargv;
	if (!s)
		return (0.0);
	pThis->gargv++;

	if (*s == '\"' || *s == '\'')
		return (double) s[1];

	errno = 0;
	val = strtod(s, &ep);
	check_conversion(pThis, s, ep);
	return val;
}

static void
check_conversion(PPRINTFINSTANCE pThis, const char *s, const char *ep)
{
	if (*ep) {
		if (ep == s)
			warnx(pThis->pCtx, "%s: expected numeric value", s);
		else
			warnx(pThis->pCtx, "%s: not completely converted", s);
		pThis->rval = 1;
	} else if (errno == ERANGE) {
		warnx(pThis->pCtx, "%s: %s", s, strerror(ERANGE));
		pThis->rval = 1;
	}
}

static int
usage(PKMKBUILTINCTX pCtx, int fIsErr)
{
	kmk_builtin_ctx_printf(pCtx, fIsErr,
	                       "usage: %s format [arg ...]\n"
	                       "   or: %s --help\n"
	                       "   or: %s --version\n",
	                       pCtx->pszProgName, pCtx->pszProgName, pCtx->pszProgName);
	return 1;
}

