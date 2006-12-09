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

#include "err.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#ifndef _MSC_VER
#include <unistd.h>
#else
#include <malloc.h>
#include "mscfakes.h"
#endif 
#include <getopt.h>

static int rm_path(char *);
static int usage(void);

static int pflag;
static int vflag;
static int ignore_fail_on_non_empty;
static int ignore_fail_on_not_exist;

static struct option long_options[] =
{
    { "ignore-fail-on-non-empty",   no_argument, 0, 260 },
    { "ignore-fail-on-not-exist",   no_argument, 0, 261 },
    { "parents",                    no_argument, 0, 'p' },
    { "verbose",                    no_argument, 0, 'v' },
    { 0, 0,	0, 0 },
};


int
kmk_builtin_rmdir(int argc, char *argv[])
{
	int ch, errors;

	/* reinitialize globals */
	ignore_fail_on_not_exist = ignore_fail_on_non_empty = vflag = pflag = 0;
	
	/* kmk: reset getopt and set progname */
	g_progname = argv[0];
	opterr = 1;
	optarg = NULL;
	optopt = 0;
#if defined(__FreeBSD__) || defined(__EMX__) || defined(__APPLE__)
	optreset = 1;
	optind = 1;
#else
	optind = 0; /* init */
#endif
	while ((ch = getopt_long(argc, argv, "pv", long_options, NULL)) != -1)
		switch(ch) {
		case 'p':
			pflag = 1;
			break;
		case 'v':
			vflag = 1;
			break;
		case 260:
			ignore_fail_on_non_empty = 1;
			break;
		case 261:
			ignore_fail_on_not_exist = 1;
			break;
		case '?':
		default:
			return usage();
		}
	argc -= optind;
	argv += optind;

	if (argc == 0)
		return usage();

	for (errors = 0; *argv; argv++) {
		if (rmdir(*argv) < 0) {
			if (	(!ignore_fail_on_non_empty || (errno != ENOTEMPTY && errno != EPERM && errno != EACCES && errno != EINVAL))
				&& 	(!ignore_fail_on_not_exist || errno != ENOENT)) {
				warn("%s", *argv);
				errors = 1;
			}
		} else {
			if (vflag)
				printf("%s\n", *argv);
			if (pflag)
				errors |= rm_path(*argv);
		}
	}

	return errors;
}

static int
rm_path(char *path)
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
			if (ignore_fail_on_non_empty && (errno == ENOTEMPTY || errno == EPERM || errno == EACCES || errno == EINVAL))
				break;
			if (!ignore_fail_on_not_exist || errno != ENOENT) {
				warn("%s", path);
				return (1);
			}
		}
		if (vflag)
			printf("%s\n", path);
	}

	return (0);
}

static int
usage(void)
{

	(void)fprintf(stderr, "usage: rmdir [-pv --ignore-fail-on-non-empty --ignore-fail-on-not-exist] directory ...\n");
	return 1;
}
