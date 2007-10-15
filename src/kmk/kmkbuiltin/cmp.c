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

/*#include <sys/cdefs.h>*/
#ifndef lint
/*__COPYRIGHT("@(#) Copyright (c) 1987, 1990, 1993, 1994\n\
	The Regents of the University of California.  All rights reserved.\n");*/
#endif /* not lint */

#ifndef lint
/*#if 0
static char sccsid[] = "@(#)cmp.c	8.3 (Berkeley) 4/2/94";
#else
__RCSID("$NetBSD: cmp.c,v 1.15 2006/01/19 20:44:57 garbled Exp $");
#endif*/
#endif /* not lint */

#include <sys/types.h>
#include <sys/stat.h>

#include "err.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _MSC_VER
# include <unistd.h>
#else
# include "mscfakes.h"
# if _MSC_VER >= 1400 /* We want 64-bit file lengths here when possible. */
#  define off_t __int64
#  define stat  _stat64
#  define fstat _fstat64
#  define lseek _lseeki64
# endif
#endif
#include <locale.h>
#include "getopt.h"

#ifndef O_BINARY
# define O_BINARY 0
#endif

/*#include "extern.h"*/

#include "kmkbuiltin.h"


static int	lflag, sflag;

static struct option long_options[] =
{
    { "help",   					no_argument, 0, 261 },
    { "version",   					no_argument, 0, 262 },
    { 0, 0,	0, 0 },
};


/* this is kind of ugly but its the simplest way to avoid namespace mess. */
#include "cmp_misc.c"
#include "cmp_special.c"
#if defined(__FreeBSD__) || defined(__NetBSD__) /** @todo more mmap capable OSes. */
#include "cmp_regular.c"
#else
#include "cmp_regular_std.c"
#endif

static int usage(FILE *);

int
kmk_builtin_cmp(int argc, char *argv[], char **envp)
{
	struct stat sb1, sb2;
	off_t skip1 = 0, skip2 = 0;
	int ch, fd1, fd2, special;
	char *file1, *file2;
        int rc;

#ifdef kmk_builtin_cmp
	setlocale(LC_ALL, "");
#endif
        /* init globals */
        lflag = sflag = 0;

        /* reset getopt and set progname. */
        g_progname = argv[0];
        opterr = 1;
        optarg = NULL;
        optopt = 0;
        optind = 0; /* init */

	while ((ch = getopt_long(argc, argv, "ls", long_options, NULL)) != -1)
		switch (ch) {
		case 'l':		/* print all differences */
			lflag = 1;
			break;
		case 's':		/* silent run */
			sflag = 1;
			break;
		case 261:
			usage(stdout);
			return 0;
		case 262:
			return kbuild_version(argv[0]);
		case '?':
		default:
			return usage(stderr);
		}
	argv += optind;
	argc -= optind;

	if (lflag && sflag)
		return errx(ERR_EXIT, "only one of -l and -s may be specified");

	if (argc < 2 || argc > 4)
		return usage(stderr);

	/* Backward compatibility -- handle "-" meaning stdin. */
	special = 0;
	if (strcmp(file1 = argv[0], "-") == 0) {
		special = 1;
		fd1 = 0;
		file1 = "stdin";
	}
	else if ((fd1 = open(file1, O_RDONLY | O_BINARY, 0)) < 0) {
		if (!sflag)
			warn("%s", file1);
		return(ERR_EXIT);
	}
	if (strcmp(file2 = argv[1], "-") == 0) {
		if (special)
			return errx(ERR_EXIT,
				"standard input may only be specified once");
		special = 1;
		fd2 = 0;
		file2 = "stdin";
	}
	else if ((fd2 = open(file2, O_RDONLY | O_BINARY, 0)) < 0) {
		if (!sflag)
			warn("%s", file2);
		if (fd1 != 0) close(fd1);
		return(ERR_EXIT);
	}

	if (argc > 2) {
		char *ep;

		errno = 0;
		skip1 = strtoll(argv[2], &ep, 0);
		if (errno || ep == argv[2]) {
			rc = usage(stderr);
			goto l_exit;
		}

		if (argc == 4) {
			skip2 = strtoll(argv[3], &ep, 0);
			if (errno || ep == argv[3]) {
				rc = usage(stderr);
				goto l_exit;
			}
		}
	}

	if (!special) {
		if (fstat(fd1, &sb1)) {
			rc = err(ERR_EXIT, "%s", file1);
			goto l_exit;
		}
		if (!S_ISREG(sb1.st_mode))
			special = 1;
		else {
			if (fstat(fd2, &sb2)) {
				rc = err(ERR_EXIT, "%s", file2);
				goto l_exit;
			}
			if (!S_ISREG(sb2.st_mode))
				special = 1;
		}
	}

	if (special)
		rc = c_special(fd1, file1, skip1, fd2, file2, skip2);
	else
		rc = c_regular(fd1, file1, skip1, sb1.st_size,
		    fd2, file2, skip2, sb2.st_size);
l_exit:
	if (fd1 != 0) close(fd1);
	if (fd2 != 0) close(fd2);
	return rc;
}

static int
usage(FILE *fp)
{
    fprintf(fp, "usage: %s [-l | -s] file1 file2 [skip1 [skip2]]\n"
				"   or: %s --help\n"
				"   or: %s --version\n",
			g_progname, g_progname, g_progname);
	return(ERR_EXIT);
}
