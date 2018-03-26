/*-
 * Copyright (c) 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Kevin Fall.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 4. Neither the name of the University nor the names of its contributors
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
static char const copyright[] =
"@(#) Copyright (c) 1989, 1993\n\
	The Regents of the University of California.  All rights reserved.\n";
#endif /* not lint */
#endif

#ifndef lint
#if 0
static char sccsid[] = "@(#)cat.c	8.2 (Berkeley) 4/27/95";
#endif
#endif /* not lint */
#if 0
#include <sys/cdefs.h>
__FBSDID("$FreeBSD: src/bin/cat/cat.c,v 1.32 2005/01/10 08:39:20 imp Exp $");
#else
#define NO_UDOM_SUPPORT /* kmk */
#endif

#include "config.h"
#ifndef _MSC_VER
# include <sys/param.h>
#endif
#include <sys/stat.h>
#ifndef NO_UDOM_SUPPORT
# include <sys/socket.h>
# include <sys/un.h>
# include <errno.h>
#endif

#include <ctype.h>
#include "err.h"
#include <fcntl.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stddef.h>
#include "getopt.h"
#ifdef __sun__
# include "solfakes.h"
#endif
#ifdef _MSC_VER
# include "mscfakes.h"
#endif
#include "kmkbuiltin.h"


int bflag, eflag, nflag, sflag, tflag, vflag;
/*int rval;*/
const char *filename;

static struct option long_options[] =
{
    { "help",   					no_argument, 0, 261 },
    { "version",   					no_argument, 0, 262 },
    { 0, 0,	0, 0 },
};


static int usage(PKMKBUILTINCTX pCtx, int fIsErr);
static int scanfiles(PKMKBUILTINCTX pCtx, char *argv[], int cooked);
static int cook_cat(PKMKBUILTINCTX pCtx, FILE *);
static int raw_cat(PKMKBUILTINCTX pCtx, int);

#ifndef NO_UDOM_SUPPORT
static int udom_open(PKMKBUILTINCTX pCtx, const char *path, int flags);
#endif

int
kmk_builtin_cat(int argc, char **argv, char **envp, PKMKBUILTINCTX pCtx)
{
	int ch, rc;

	/* kmk: reinitialize globals */
	bflag = eflag = nflag = sflag = tflag = vflag = 0;
	filename = NULL;

	/* kmk: reset getopt and set progname */
	opterr = 1;
	optarg = NULL;
	optopt = 0;
	optind = 0; /* init */

#ifdef KMK_BUILTIN_STANDALONE /* kmk did this already. */
	setlocale(LC_CTYPE, "");
#endif

	while ((ch = getopt_long(argc, argv, "benstuv", long_options, NULL)) != -1)
		switch (ch) {
		case 'b':
			bflag = nflag = 1;	/* -b implies -n */
			break;
		case 'e':
			eflag = vflag = 1;	/* -e implies -v */
			break;
		case 'n':
			nflag = 1;
			break;
		case 's':
			sflag = 1;
			break;
		case 't':
			tflag = vflag = 1;	/* -t implies -v */
			break;
		case 'u':
#ifdef KMK_BUILTIN_STANDALONE /* don't allow messing with stdout */
			setbuf(stdout, NULL);
#endif
			break;
		case 'v':
			vflag = 1;
			break;
		case 261:
			usage(pCtx, 0);
			return 0;
		case 262:
			return kbuild_version(argv[0]);
		default:
			return usage(pCtx, 1);
		}
	argv += optind;

	if (bflag || eflag || nflag || sflag || tflag || vflag)
		rc = scanfiles(pCtx, argv, 1);
	else
		rc = scanfiles(pCtx, argv, 0);
#ifdef KMK_BUILTIN_STANDALONE /* don't allow messing with stdout */
	if (fclose(stdout))
		return err(pCtx, 1, "stdout");
#endif
	return rc;
}

#ifdef KMK_BUILTIN_STANDALONE
int main(int argc, char **argv, char **envp)
{
    KMKBUILTINCTX Ctx = { "kmk_cat", NULL };
    return kmk_builtin_cat(argc, argv, envp, &Ctx);
}
#endif

static int
usage(PKMKBUILTINCTX pCtx, int fIsErr)
{
	kmk_builtin_ctx_printf(pCtx, fIsErr,
			       "usage: %s [-benstuv] [file ...]\n"
			       "   or: %s --help\n"
			       "   or: %s --version\n",
			       pCtx->pszProgName, pCtx->pszProgName,
			       pCtx->pszProgName);
	return 1;
}

static int
scanfiles(PKMKBUILTINCTX pCtx, char *argv[], int cooked)
{
	int i = 0;
	char *path;
	FILE *fp;
	int rc2 = 0;
	int rc = 0;

	while ((path = argv[i]) != NULL || i == 0) {
		int fd;

		if (path == NULL || strcmp(path, "-") == 0) {
			filename = "stdin";
			fd = STDIN_FILENO;
		} else {
			filename = path;
			fd = open(path, O_RDONLY);
#ifndef NO_UDOM_SUPPORT
			if (fd < 0 && errno == EOPNOTSUPP)
				fd = udom_open(pCtx, path, O_RDONLY);
#endif
		}
		if (fd < 0) {
			warn(pCtx, "%s", path);
			rc2 = 1; /* non fatal */
		} else if (cooked) {
			if (fd == STDIN_FILENO)
				rc = cook_cat(pCtx, stdin);
			else {
				fp = fdopen(fd, "r");
				rc = cook_cat(pCtx, fp);
				fclose(fp);
			}
		} else {
			rc = raw_cat(pCtx, fd);
			if (fd != STDIN_FILENO)
				close(fd);
		}
		if (rc || path == NULL)
			break;
		++i;
	}
	return !rc ? rc2 : rc;
}

static int cat_putchar(PKMKBUILTINCTX pCtx, char ch)
{
#ifndef KMK_BUILTIN_STANDALONE
	if (pCtx->pOut) {
		output_write_text(pCtx->pOut, 0, &ch, 1);
		return 0;
	}
#endif
	return putchar(ch);
}

static int
cook_cat(PKMKBUILTINCTX pCtx, FILE *fp)
{
	int ch, gobble, line, prev;
	int rc = 0;

	/* Reset EOF condition on stdin. */
	if (fp == stdin && feof(stdin))
		clearerr(stdin);

	line = gobble = 0;
	for (prev = '\n'; (ch = getc(fp)) != EOF; prev = ch) {
		if (prev == '\n') {
			if (sflag) {
				if (ch == '\n') {
					if (gobble)
						continue;
					gobble = 1;
				} else
					gobble = 0;
			}
			if (nflag && (!bflag || ch != '\n')) {
				kmk_builtin_ctx_printf(pCtx, 0, "%6d\t", ++line);
				if (ferror(stdout))
					break;
			}
		}
		if (ch == '\n') {
			if (eflag && cat_putchar(pCtx, '$') == EOF)
				break;
		} else if (ch == '\t') {
			if (tflag) {
				if (cat_putchar(pCtx, '^') == EOF || cat_putchar(pCtx, 'I') == EOF)
					break;
				continue;
			}
		} else if (vflag) {
			if (!isascii(ch) && !isprint(ch)) {
				if (cat_putchar(pCtx, 'M') == EOF || cat_putchar(pCtx, '-') == EOF)
					break;
				ch = toascii(ch);
			}
			if (iscntrl(ch)) {
				if (cat_putchar(pCtx, '^') == EOF ||
				    cat_putchar(pCtx, ch == '\177' ? '?' :
				    ch | 0100) == EOF)
					break;
				continue;
			}
		}
		if (cat_putchar(pCtx, ch) == EOF)
			break;
	}
	if (ferror(fp)) {
		warn(pCtx, "%s", filename);
		rc = 1;
		clearerr(fp);
	}
	if (ferror(stdout))
		return err(pCtx, 1, "stdout");
	return rc;
}

static int
raw_cat(PKMKBUILTINCTX pCtx, int rfd)
{
	int off, wfd = fileno(stdout);
	ssize_t nr, nw;
	static size_t bsize;
	static char *buf = NULL;
	struct stat sbuf;

	wfd = fileno(stdout);
	if (buf == NULL) {
		if (fstat(wfd, &sbuf))
			return err(pCtx, 1, "%s", filename);
#ifdef KBUILD_OS_WINDOWS
		bsize = 16384;
#else
		bsize = MAX(sbuf.st_blksize, 1024);
#endif
		if ((buf = malloc(bsize)) == NULL)
			return err(pCtx, 1, "buffer");
	}
	while ((nr = read(rfd, buf, bsize)) > 0)
		for (off = 0; nr; nr -= nw, off += nw) {
#ifndef KMK_BUILTIN_STANDALONE
			if (pCtx->pOut)
				nw = output_write_text(pCtx->pOut, 0, buf, nr);
			else
#endif
				nw = write(wfd, buf + off, (size_t)nr);
			if (nw < 0)
				return err(pCtx, 1, "stdout");
		}
	if (nr < 0) {
		warn(pCtx, "%s", filename);
		return 1;
	}
	return 0;
}

#ifndef NO_UDOM_SUPPORT

static int
udom_open(PKMKBUILTINCTX pCtx, const char *path, int flags)
{
	struct sockaddr_un sou;
	int fd;
	unsigned int len;

	bzero(&sou, sizeof(sou));

	/*
	 * Construct the unix domain socket address and attempt to connect
	 */
	fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd >= 0) {
		sou.sun_family = AF_UNIX;
		if ((len = strlcpy(sou.sun_path, path,
		    sizeof(sou.sun_path))) >= sizeof(sou.sun_path)) {
			errno = ENAMETOOLONG;
			return (-1);
		}
		len = offsetof(struct sockaddr_un, sun_path[len+1]);

		if (connect(fd, (void *)&sou, len) < 0) {
			close(fd);
			fd = -1;
		}
	}

	/*
	 * handle the open flags by shutting down appropriate directions
	 */
	if (fd >= 0) {
		switch(flags & O_ACCMODE) {
		case O_RDONLY:
			if (shutdown(fd, SHUT_WR) == -1)
				warn(pCtx, NULL);
			break;
		case O_WRONLY:
			if (shutdown(fd, SHUT_RD) == -1)
				warn(pCtx, NULL);
			break;
		default:
			break;
		}
	}
	return(fd);
}

#endif

