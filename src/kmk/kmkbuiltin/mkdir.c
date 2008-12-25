/*
 * Copyright (c) 1983, 1992, 1993
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
"@(#) Copyright (c) 1983, 1992, 1993\n\
	The Regents of the University of California.  All rights reserved.\n";
#endif /* not lint */

#ifndef lint
static char sccsid[] = "@(#)mkdir.c	8.2 (Berkeley) 1/25/94";
#endif /* not lint */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD: src/bin/mkdir/mkdir.c,v 1.28 2004/04/06 20:06:48 markm Exp $");
#endif

#include "config.h"
#include <sys/types.h>
#include <sys/stat.h>

#include "err.h"
#include <errno.h>
#ifndef _MSC_VER
# include <libgen.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>
#include "getopt.h"
#ifdef _MSC_VER
# include <malloc.h>
# include "mscfakes.h"
#endif
#include "kmkbuiltin.h"


static int vflag;
static struct option long_options[] =
{
    { "help",   					no_argument, 0, 261 },
    { "version",   					no_argument, 0, 262 },
    { 0, 0,	0, 0 },
};


extern void * bsd_setmode(const char *p);
extern mode_t bsd_getmode(const void *bbox, mode_t omode);

static int	build(char *, mode_t);
static int	usage(FILE *);


int
kmk_builtin_mkdir(int argc, char *argv[], char **envp)
{
	int ch, exitval, success, pflag;
	mode_t omode, *set = (mode_t *)NULL;
	char *mode;

	omode = pflag = 0;
	mode = NULL;

	/* reinitialize globals */
	vflag = 0;

	/* kmk: reset getopt and set progname */
	g_progname = argv[0];
	opterr = 1;
	optarg = NULL;
	optopt = 0;
	optind = 0; /* init */
	while ((ch = getopt_long(argc, argv, "m:pv", long_options, NULL)) != -1)
		switch(ch) {
		case 'm':
			mode = optarg;
			break;
		case 'p':
			pflag = 1;
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

	argc -= optind;
	argv += optind;
	if (argv[0] == NULL)
		return usage(stderr);

	if (mode == NULL) {
		omode = S_IRWXU | S_IRWXG | S_IRWXO;
	} else {
		if ((set = bsd_setmode(mode)) == NULL)
                        return errx(1, "invalid file mode: %s", mode);
		omode = bsd_getmode(set, S_IRWXU | S_IRWXG | S_IRWXO);
		free(set);
	}

	for (exitval = 0; *argv != NULL; ++argv) {
		success = 1;
		if (pflag) {
			if (build(*argv, omode))
				success = 0;
		} else if (mkdir(*argv, omode) < 0) {
			if (errno == ENOTDIR || errno == ENOENT)
				warn("%s", dirname(*argv));
			else
                                warn("%s", *argv);
			success = 0;
		} else if (vflag)
			(void)printf("%s\n", *argv);

		if (!success)
			exitval = 1;
		/*
		 * The mkdir() and umask() calls both honor only the low
		 * nine bits, so if you try to set a mode including the
		 * sticky, setuid, setgid bits you lose them.  Don't do
		 * this unless the user has specifically requested a mode,
		 * as chmod will (obviously) ignore the umask.
		 */
		if (success && mode != NULL && chmod(*argv, omode) == -1) {
			warn("%s", *argv);
			exitval = 1;
		}
	}
	return exitval;
}

static int
build(char *path, mode_t omode)
{
	struct stat sb;
	mode_t numask, oumask;
	int first, last, retval;
	char *p;

	const size_t len = strlen(path);
	p = alloca(len + 1);
	path = memcpy(p, path, len + 1);

#if defined(_MSC_VER) || defined(__EMX__)
	/* correct slashes. */
	p = strchr(path, '\\');
	while (p) {
		*p++ = '/';
		p = strchr(p, '\\');
	}
#endif

	p = path;
	oumask = 0;
	retval = 0;
#if defined(_MSC_VER) || defined(__EMX__)
	if (    (    (p[0] >= 'A' && p[0] <= 'Z')
	         ||  (p[0] >= 'a' && p[0] <= 'z'))
	    && p[1] == ':')
		p += 2; /* skip the drive letter */
	else if (   p[0] == '/'
	         && p[1] == '/'
	         && p[2] != '/')
	{
		/* UNC */
		p += 2;
		while (*p && *p != '/') /* server */
			p++;
		while (*p == '/')
			p++;
		while (*p && *p != '/') /* share */
			p++;
	}
#endif
	if (p[0] == '/')		/* Skip leading '/'. */
		++p;
	for (first = 1, last = 0; !last ; ++p) {
		if (p[0] == '\0')
			last = 1;
		else if (p[0] != '/')
			continue;
		*p = '\0';
		if (!last && p[1] == '\0')
			last = 1;
		if (first) {
			/*
			 * POSIX 1003.2:
			 * For each dir operand that does not name an existing
			 * directory, effects equivalent to those cased by the
			 * following command shall occcur:
			 *
			 * mkdir -p -m $(umask -S),u+wx $(dirname dir) &&
			 *    mkdir [-m mode] dir
			 *
			 * We change the user's umask and then restore it,
			 * instead of doing chmod's.
			 */
			oumask = umask(0);
			numask = oumask & ~(S_IWUSR | S_IXUSR);
			(void)umask(numask);
			first = 0;
		}
		if (last)
			(void)umask(oumask);
		if (mkdir(path, last ? omode : S_IRWXU | S_IRWXG | S_IRWXO) < 0) {
			if (errno == EEXIST || errno == EISDIR || errno == ENOSYS /* (solaris crap) */) {
				if (stat(path, &sb) < 0) {
					warn("%s", path);
					retval = 1;
					break;
				} else if (!S_ISDIR(sb.st_mode)) {
					if (last)
						errno = EEXIST;
					else
						errno = ENOTDIR;
					warn("%s", path);
					retval = 1;
					break;
				}
			} else {
				warn("%s", path);
				retval = 1;
				break;
			}
		} else if (vflag)
			printf("%s\n", path);
		if (!last)
		    *p = '/';
	}
	if (!first && !last)
		(void)umask(oumask);
	return (retval);
}

static int
usage(FILE *pf)
{
	fprintf(pf, "usage: %s [-pv] [-m mode] directory ...\n"
	            "   or: %s --help\n"
	            "   or: %s --version\n",
	        g_progname, g_progname, g_progname);
	return EX_USAGE;
}
