/*-
 * Copyright (c) 1992, 1993, 1994
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
"@(#) Copyright (c) 1992, 1993, 1994\n\
	The Regents of the University of California.  All rights reserved.\n";
#endif /* not lint */

#ifndef lint
static char sccsid[] = "@(#)rmdir.c	8.3 (Berkeley) 4/2/94";
#endif /* not lint */
#endif
#if 0
#include <sys/cdefs.h>
__FBSDID("$FreeBSD: src/bin/rmdir/rmdir.c,v 1.20 2005/01/26 06:51:28 ssouhlal Exp $");
#endif


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define FAKES_NO_GETOPT_H /* bird */
#include "config.h"
#include "err.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#ifdef HAVE_ALLOCA_H
# include <alloca.h>
#endif
#include "getopt_r.h"
#include "kmkbuiltin.h"

#ifdef _MSC_VER
# include "mscfakes.h"
#endif


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
typedef struct RMDIRINSTANCE
{
    PKMKBUILTINCTX pCtx;
    int pflag;
    int vflag;
    int ignore_fail_on_non_empty;
    int ignore_fail_on_not_exist;
} RMDIRINSTANCE;
typedef RMDIRINSTANCE *PRMDIRINSTANCE;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
static struct option long_options[] =
{
    { "help",                       no_argument, 0, 262 },
    { "ignore-fail-on-non-empty",   no_argument, 0, 260 },
    { "ignore-fail-on-not-exist",   no_argument, 0, 261 },
    { "parents",                    no_argument, 0, 'p' },
    { "verbose",                    no_argument, 0, 'v' },
    { "version",                    no_argument, 0, 263 },
    { 0, 0,	0, 0 },
};



/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
#if !defined(KMK_BUILTIN_STANDALONE) && defined(KBUILD_OS_WINDOWS)
extern int dir_cache_deleted_directory(const char *pszDir);
#endif
static int rm_path(PRMDIRINSTANCE, char *);
static int usage(PKMKBUILTINCTX, int);


int
kmk_builtin_rmdir(int argc, char **argv, char **envp, PKMKBUILTINCTX pCtx)
{
	RMDIRINSTANCE This;
	struct getopt_state_r gos;
	int ch, errors;

	/* Initialize global instance. */
	This.pCtx = pCtx;
	This.ignore_fail_on_not_exist = 0;
	This.ignore_fail_on_non_empty = 0;
	This.vflag = 0;
	This.pflag = 0;

	getopt_initialize_r(&gos, argc, argv, "pv", long_options, envp, pCtx);
	while ((ch = getopt_long_r(&gos, NULL)) != -1)
		switch(ch) {
		case 'p':
			This.pflag = 1;
			break;
		case 'v':
			This.vflag = 1;
			break;
		case 260:
			This.ignore_fail_on_non_empty = 1;
			break;
		case 261:
			This.ignore_fail_on_not_exist = 1;
			break;
		case 262:
			usage(pCtx, 0);
			return 0;
		case 263:
			return kbuild_version(argv[0]);
		case '?':
		default:
			return usage(pCtx, 1);
		}
	argc -= gos.optind;
	argv += gos.optind;

	if (argc == 0)
		return /*usage(stderr)*/0;

	for (errors = 0; *argv; argv++) {
		if (rmdir(*argv) < 0) {
			if (	(   !This.ignore_fail_on_non_empty
				 || (errno != ENOTEMPTY && errno != EPERM && errno != EACCES && errno != EINVAL && errno != EEXIST))
			    &&	(   !This.ignore_fail_on_not_exist
				 || errno != ENOENT)) {
				warn(pCtx, "rmdir: %s", *argv);
				errors = 1;
				continue;
			}
			if (!This.ignore_fail_on_not_exist || errno != ENOENT)
				continue;
			/* (only ignored doesn't exist errors fall thru) */
		} else {
#if !defined(KMK_BUILTIN_STANDALONE) && defined(KBUILD_OS_WINDOWS)
			dir_cache_deleted_directory(*argv);
#endif
			if (This.vflag)
				kmk_builtin_ctx_printf(pCtx, 0, "%s\n", *argv);
		}
		if (This.pflag)
			errors |= rm_path(&This, *argv);
	}

	return errors;
}

#ifdef KMK_BUILTIN_STANDALONE
int main(int argc, char **argv, char **envp)
{
    KMKBUILTINCTX Ctx = { "kmk_rmdir", NULL };
    return kmk_builtin_rmdir(argc, argv, envp, &Ctx);
}
#endif

static int
rm_path(PRMDIRINSTANCE pThis, char *path)
{
	char *p;
	const size_t len = strlen(path);
	p = alloca(len + 1);
	path = memcpy(p, path, len + 1);

#if defined(_MSC_VER) || defined(__EMX__)
	p = strchr(path, '\\');
	while (p) {
		*p++ = '/';
		p = strchr(p, '\\');
	}
#endif

	p = path + len;
	while (--p > path && *p == '/')
		;
	*++p = '\0';
	while ((p = strrchr(path, '/')) != NULL) {
		/* Delete trailing slashes. */
		while (--p >= path && *p == '/')
			;
		*++p = '\0';
		if (p == path)
			break;
#if defined(_MSC_VER) || defined(__EMX__)
		if (p[-1] == ':' && p - 2 == path)
			break;
#endif

		if (rmdir(path) < 0) {
			if (   pThis->ignore_fail_on_non_empty
			    && (   errno == ENOTEMPTY || errno == EPERM || errno == EACCES || errno == EINVAL || errno == EEXIST))
				break;
			if (!pThis->ignore_fail_on_not_exist || errno != ENOENT) {
				warn(pThis->pCtx, "rmdir: %s", path);
				return (1);
			}
		}
#if defined(KMK) && defined(KBUILD_OS_WINDOWS)
		else {
			dir_cache_deleted_directory(path);
		}
#endif
		if (pThis->vflag)
			kmk_builtin_ctx_printf(pThis->pCtx, 0, "%s\n", path);
	}

	return (0);
}

static int
usage(PKMKBUILTINCTX pCtx, int fIsErr)
{
	kmk_builtin_ctx_printf(pCtx, fIsErr,
			       "usage: %s [-pv --ignore-fail-on-non-empty --ignore-fail-on-not-exist] directory ...\n"
			       "   or: %s --help\n"
			       "   or: %s --version\n",
			       pCtx->pszProgName, pCtx->pszProgName, pCtx->pszProgName);
	return 1;
}

