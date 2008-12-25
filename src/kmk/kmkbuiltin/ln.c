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

#ifndef _MSC_VER
# include <sys/param.h>
#endif
#include <sys/stat.h>

#include "config.h"
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

static int	fflag;				/* Unlink existing files. */
static int	hflag;				/* Check new name for symlink first. */
static int	iflag;				/* Interactive mode. */
static int	sflag;				/* Symbolic, not hard, link. */
static int	vflag;				/* Verbose output. */
					/* System link call. */
static int (*linkf)(const char *, const char *);
static char	linkch;
static struct option long_options[] =
{
    { "help",   					no_argument, 0, 261 },
    { "version",   					no_argument, 0, 262 },
    { 0, 0,	0, 0 },
};


static int	linkit(const char *, const char *, int);
static int	usage(FILE *);


int
kmk_builtin_ln(int argc, char *argv[], char **envp)
{
	struct stat sb;
	char *sourcedir;
	int ch, exitval;

	/* initialize globals. */
	fflag = hflag = iflag = sflag = vflag = 0;
	linkch = 0;
	linkf = NULL;

	/* kmk: reset getopt() and set program name. */
	g_progname = argv[0];
	opterr = 1;
	optarg = NULL;
	optopt = 0;
	optind = 0; /* init */

	while ((ch = getopt_long(argc, argv, "fhinsv", long_options, NULL)) != -1)
		switch (ch) {
		case 'f':
			fflag = 1;
			iflag = 0;
			break;
		case 'h':
		case 'n':
			hflag = 1;
			break;
		case 'i':
			iflag = 1;
			fflag = 0;
			break;
		case 's':
			sflag = 1;
			break;
		case 'v':
			vflag = 1;
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

	linkf = sflag ? symlink : link;
	linkch = sflag ? '-' : '=';

	switch(argc) {
	case 0:
		return usage(stderr);
		/* NOTREACHED */
	case 1:				/* ln target */
		return linkit(argv[0], ".", 1);
	case 2:				/* ln target source */
		return linkit(argv[0], argv[1], 0);
	default:
		;
	}
					/* ln target1 target2 directory */
	sourcedir = argv[argc - 1];
	if (hflag && lstat(sourcedir, &sb) == 0 && S_ISLNK(sb.st_mode)) {
		/*
		 * We were asked not to follow symlinks, but found one at
		 * the target--simulate "not a directory" error
		 */
		errno = ENOTDIR;
		return err(1, "%s", sourcedir);
	}
	if (stat(sourcedir, &sb))
		return err(1, "%s", sourcedir);
	if (!S_ISDIR(sb.st_mode))
		return usage(stderr);
	for (exitval = 0; *argv != sourcedir; ++argv)
		exitval |= linkit(*argv, sourcedir, 1);
	return exitval;
}

static int
linkit(const char *target, const char *source, int isdir)
{
	struct stat sb;
	const char *p;
	int ch, exists, first;
	char path[PATH_MAX];

	if (!sflag) {
		/* If target doesn't exist, quit now. */
		if (stat(target, &sb)) {
			warn("%s", target);
			return (1);
		}
		/* Only symbolic links to directories. */
		if (S_ISDIR(sb.st_mode)) {
			errno = EISDIR;
			warn("%s", target);
			return (1);
		}
	}

	/*
	 * If the source is a directory (and not a symlink if hflag),
	 * append the target's name.
	 */
	if (isdir ||
	    (lstat(source, &sb) == 0 && S_ISDIR(sb.st_mode)) ||
	    (!hflag && stat(source, &sb) == 0 && S_ISDIR(sb.st_mode))) {
#ifdef _MSC_VER || defined(__OS2__)
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
			warn("%s", target);
			return (1);
		}
		source = path;
	}

	exists = !lstat(source, &sb);
	/*
	 * If the file exists, then unlink it forcibly if -f was specified
	 * and interactively if -i was specified.
	 */
	if (fflag && exists) {
		if (unlink(source)) {
			warn("%s", source);
			return (1);
		}
	} else if (iflag && exists) {
		fflush(stdout);
		fprintf(stderr, "replace %s? ", source);

		first = ch = getchar();
		while(ch != '\n' && ch != EOF)
			ch = getchar();
		if (first != 'y' && first != 'Y') {
			fprintf(stderr, "not replaced\n");
			return (1);
		}

		if (unlink(source)) {
			warn("%s", source);
			return (1);
		}
	}

	/* Attempt the link. */
	if ((*linkf)(target, source)) {
		warn("%s", source);
		return (1);
	}
	if (vflag)
		(void)printf("%s %c> %s\n", source, linkch, target);
	return (0);
}

static int
usage(FILE *pf)
{
	fprintf(pf, "usage: %s [-fhinsv] source_file [target_file]\n"
				"   or: %s [-fhinsv] source_file ... target_dir\n"
                "   or: %s source_file target_file\n"
				"   or: %s --help\n"
				"   or: %s --version\n",
                g_progname, g_progname, g_progname, g_progname, g_progname);
	return 1;
}
