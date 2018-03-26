/*-
 * Copyright (c) 1987, 1993, 1994
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
"@(#) Copyright (c) 1987, 1993, 1994\n\
	The Regents of the University of California.  All rights reserved.\n";
#endif /* not lint */

#ifndef lint
static char sccsid[] = "@(#)ln.c	8.2 (Berkeley) 3/31/94";
#endif /* not lint */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD: src/bin/ln/ln.c,v 1.33 2005/02/09 17:37:37 ru Exp $");
#endif /* no $id */

#include "config.h"
#ifndef _MSC_VER
# include <sys/param.h>
#endif
#include <sys/stat.h>

#include "err.h"
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "getopt.h"
#ifdef _MSC_VER
# include "mscfakes.h"
#endif
#include "kmkbuiltin.h"

/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
typedef struct LNINSTANCE
{
    PKMKBUILTINCTX pCtx;
    int	fflag;				/* Unlink existing files. */
    int	hflag;				/* Check new name for symlink first. */
    int	iflag;				/* Interactive mode. */
    int	sflag;				/* Symbolic, not hard, link. */
    int	vflag;				/* Verbose output. */
    int (*linkf)(const char *, const char *); /* System link call. */
    char	linkch;
} LNINSTANCE;
typedef LNINSTANCE *PLNINSTANCE;

static struct option long_options[] =
{
    { "help",   					no_argument, 0, 261 },
    { "version",   					no_argument, 0, 262 },
    { 0, 0,	0, 0 },
};


static int	linkit(PLNINSTANCE,const char *, const char *, int);
static int	usage(PKMKBUILTINCTX, int);


int
kmk_builtin_ln(int argc, char **argv, char **envp, PKMKBUILTINCTX pCtx)
{
	LNINSTANCE This;
	struct stat sb;
	char *sourcedir;
	int ch, exitval;

	/* initialize globals. */
	This.pCtx = pCtx;
	This.fflag = 0;
	This.hflag = 0;
	This.iflag = 0;
	This.sflag = 0;
	This.vflag = 0;
	This.linkch = 0;
	This.linkf = NULL;

	/* kmk: reset getopt() and set program name. */
	opterr = 1;
	optarg = NULL;
	optopt = 0;
	optind = 0; /* init */

	while ((ch = getopt_long(argc, argv, "fhinsv", long_options, NULL)) != -1)
		switch (ch) {
		case 'f':
			This.fflag = 1;
			This.iflag = 0;
			break;
		case 'h':
		case 'n':
			This.hflag = 1;
			break;
		case 'i':
			This.iflag = 1;
			This.fflag = 0;
			break;
		case 's':
			This.sflag = 1;
			break;
		case 'v':
			This.vflag = 1;
			break;
		case 261:
			usage(pCtx, 0);
			return 0;
		case 262:
			return kbuild_version(argv[0]);
		case '?':
		default:
			return usage(pCtx, 1);
		}

	argv += optind;
	argc -= optind;

	This.linkf = This.sflag ? symlink : link;
	This.linkch = This.sflag ? '-' : '=';

	switch(argc) {
	case 0:
		return usage(pCtx, 1);
		/* NOTREACHED */
	case 1:				/* ln target */
		return linkit(&This, argv[0], ".", 1);
	case 2:				/* ln target source */
		return linkit(&This, argv[0], argv[1], 0);
	default:
		;
	}
					/* ln target1 target2 directory */
	sourcedir = argv[argc - 1];
	if (This.hflag && lstat(sourcedir, &sb) == 0 && S_ISLNK(sb.st_mode)) {
		/*
		 * We were asked not to follow symlinks, but found one at
		 * the target--simulate "not a directory" error
		 */
		errno = ENOTDIR;
		return err(pCtx, 1, "st_mode: %s", sourcedir);
	}
	if (stat(sourcedir, &sb))
		return err(pCtx, 1, "stat: %s", sourcedir);
	if (!S_ISDIR(sb.st_mode))
		return usage(pCtx, 1);
	for (exitval = 0; *argv != sourcedir; ++argv)
		exitval |= linkit(&This, *argv, sourcedir, 1);
	return exitval;
}

static int
linkit(PLNINSTANCE pThis, const char *target, const char *source, int isdir)
{
	struct stat sb;
	const char *p;
	int ch, exists, first;
	char path[PATH_MAX];

	if (!pThis->sflag) {
		/* If target doesn't exist, quit now. */
		if (stat(target, &sb)) {
			warn(pThis->pCtx, "stat: %s", target);
			return (1);
		}
		/* Only symbolic links to directories. */
		if (S_ISDIR(sb.st_mode)) {
			errno = EISDIR;
			warn(pThis->pCtx, "st_mode: %s", target);
			return (1);
		}
	}

	/*
	 * If the source is a directory (and not a symlink if hflag),
	 * append the target's name.
	 */
	if (isdir ||
	    (lstat(source, &sb) == 0 && S_ISDIR(sb.st_mode)) ||
	    (!pThis->hflag && stat(source, &sb) == 0 && S_ISDIR(sb.st_mode))) {
#if defined(_MSC_VER) || defined(__OS2__)
		char *p2 = strrchr(target, '\\');
		p = strrchr(target, '/');
		if (p2 != NULL && (p == NULL || p2 > p))
			p = p2;
		if (p == NULL)
#else
		if ((p = strrchr(target, '/')) == NULL)
#endif
			p = target;
		else
			++p;
		if (snprintf(path, sizeof(path), "%s/%s", source, p) >=
		    (ssize_t)sizeof(path)) {
			errno = ENAMETOOLONG;
			warn(pThis->pCtx, "snprintf: %s", target);
			return (1);
		}
		source = path;
	}

	exists = !lstat(source, &sb);
	/*
	 * If the file exists, then unlink it forcibly if -f was specified
	 * and interactively if -i was specified.
	 */
	if (pThis->fflag && exists) {
		if (unlink(source)) {
			warn(pThis->pCtx, "unlink: %s", source);
			return (1);
		}
	} else if (pThis->iflag && exists) {
		fflush(stdout);
		fprintf(stderr, "replace %s? ", source);

		first = ch = getchar();
		while(ch != '\n' && ch != EOF)
			ch = getchar();
		if (first != 'y' && first != 'Y') {
			kmk_builtin_ctx_printf(pThis->pCtx, 1, "not replaced\n");
			return (1);
		}

		if (unlink(source)) {
			warn(pThis->pCtx, "unlink: %s", source);
			return (1);
		}
	}

	/* Attempt the link. */
	if ((*pThis->linkf)(target, source)) {
		warn(pThis->pCtx, "%s: %s", pThis->linkf == link ? "link" : "symlink", source);
		return (1);
	}
	if (pThis->vflag)
		kmk_builtin_ctx_printf(pThis->pCtx, 0, "%s %c> %s\n", source, pThis->linkch, target);
	return (0);
}

static int
usage(PKMKBUILTINCTX pCtx, int fIsErr)
{
	kmk_builtin_ctx_printf(pCtx,fIsErr,
		"usage: %s [-fhinsv] source_file [target_file]\n"
		"   or: %s [-fhinsv] source_file ... target_dir\n"
		"   or: %s source_file target_file\n"
		"   or: %s --help\n"
		"   or: %s --version\n",
		pCtx->pszProgName, pCtx->pszProgName, pCtx->pszProgName,
		pCtx->pszProgName, pCtx->pszProgName);
	return 1;
}

#ifdef KMK_BUILTIN_STANDALONE
int main(int argc, char **argv, char **envp)
{
	KMKBUILTINCTX Ctx = { "kmk_ln", NULL };
	return kmk_builtin_ln(argc, argv, envp, &Ctx);
}
#endif

