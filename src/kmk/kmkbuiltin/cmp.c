/*	$NetBSD: cmp.c,v 1.15 2006/01/19 20:44:57 garbled Exp $	*/

/*
 * Copyright (c) 1987, 1990, 1993, 1994
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

/*__COPYRIGHT("@(#) Copyright (c) 1987, 1990, 1993, 1994\n\
	The Regents of the University of California.  All rights reserved.\n");
static char sccsid[] = "@(#)cmp.c	8.3 (Berkeley) 4/2/94";
__RCSID("$NetBSD: cmp.c,v 1.15 2006/01/19 20:44:57 garbled Exp $"); */

/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define FAKES_NO_GETOPT_H /* bird */
#ifdef _MSC_VER
# define MSC_DO_64_BIT_IO /* for correct off_t */
#endif
#include "config.h"
#include <sys/types.h>
#include "err.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <locale.h>
#ifndef _MSC_VER
# include <unistd.h>
#else
# include "mscfakes.h"
#endif
#include "getopt_r.h"
#include "kmkbuiltin.h"
#include "cmp_extern.h"


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
static const struct option long_options[] =
{
    { "help",   					no_argument, 0, 261 },
    { "version",   					no_argument, 0, 262 },
    { 0, 0,	0, 0 },
};


static int usage(PKMKBUILTINCTX pCtx, int is_err);

int
kmk_builtin_cmp(int argc, char **argv, char **envp, PKMKBUILTINCTX pCtx)
{
    struct getopt_state_r gos;
    off_t skip1 = 0, skip2 = 0;
    int lflag = 0, sflag = 0;
    int ch;
    char *file1, *file2;

    getopt_initialize_r(&gos, argc, argv, "ls", long_options, envp, pCtx);
    while ((ch = getopt_long_r(&gos, NULL)) != -1)
    {
        switch (ch)
        {
            case 'l':		/* print all differences */
                lflag = 1;
                break;
            case 's':		/* silent run */
                sflag = 1;
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
    }
    argv += gos.optind;
    argc -= gos.optind;

    if (argc < 2 || argc > 4)
        return usage(pCtx, 1);

    file1 = argv[0];
    file2 = argv[1];

    if (argc > 2)
    {
        char *ep;

        errno = 0;
        skip1 = strtoll(argv[2], &ep, 0);
        if (errno || ep == argv[2])
            return errx(pCtx, ERR_EXIT, "strtoll(%s,,) failed", argv[2]);

        if (argc == 4)
        {
            skip2 = strtoll(argv[3], &ep, 0);
            if (errno || ep == argv[3])
                return errx(pCtx, ERR_EXIT, "strtoll(%s,,) failed", argv[3]);
	}
    }

    return cmp_file_and_file_ex(pCtx, file1, skip1, file2, skip2, sflag, lflag, 0);
}

static int
usage(PKMKBUILTINCTX pCtx, int is_err)
{
    kmk_builtin_ctx_printf(pCtx, is_err,
                           "usage: %s [-l | -s] file1 file2 [skip1 [skip2]]\n"
                           "   or: %s --help\n"
                           "   or: %s --version\n",
                           pCtx->pszProgName, pCtx->pszProgName, pCtx->pszProgName);
    return ERR_EXIT;
}

#ifdef KMK_BUILTIN_STANDALONE
int main(int argc, char **argv, char **envp)
{
    KMKBUILTINCTX Ctx = { "kmk_cmp", NULL };
    setlocale(LC_ALL, "");
    return kmk_builtin_cmp(argc, argv, envp, &Ctx);
}
#endif

