/*-
 * Copyright (c) 1989, 1993, 1994
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
"@(#) Copyright (c) 1989, 1993, 1994\n\
	The Regents of the University of California.  All rights reserved.\n";
#endif /* not lint */

#ifndef lint
static char sccsid[] = "@(#)chmod.c	8.8 (Berkeley) 4/1/94";
#endif /* not lint */
#endif
/*#include <sys/cdefs.h> */
/*__FBSDID("$FreeBSD: src/bin/chmod/chmod.c,v 1.33 2005/01/10 08:39:20 imp Exp $");*/

#include "config.h"
#include <sys/types.h>
#include <sys/stat.h>

#include "err.h"
#include <errno.h>
#include "fts.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _MSC_VER
# include <unistd.h>
#else
# include "mscfakes.h"
#endif
#ifdef __sun__
# include "solfakes.h"
#endif
#ifdef __HAIKU__
# include "haikufakes.h"
#endif
#include "getopt.h"
#include "kmkbuiltin.h"

extern void * bsd_setmode(const char *p);
extern mode_t bsd_getmode(const void *bbox, mode_t omode);
extern void bsd_strmode(mode_t mode, char *p);

#if (defined(__APPLE__) && !defined(_DARWIN_FEATURE_UNIX_CONFORMANCE)) || defined(__OpenBSD__)
extern int lchmod(const char *, mode_t);
#endif

static int usage(FILE *);

static struct option long_options[] =
{
    { "help",   					no_argument, 0, 261 },
    { "version",   					no_argument, 0, 262 },
    { 0, 0,	0, 0 },
};


int
kmk_builtin_chmod(int argc, char *argv[], char **envp)
{
	FTS *ftsp;
	FTSENT *p;
	mode_t *set;
	int Hflag, Lflag, Rflag, ch, fflag, fts_options, hflag, rval;
	int vflag;
	char *mode;
	mode_t newmode;
	int (*change_mode)(const char *, mode_t);

	/* kmk: reset getopt and set progname */
	g_progname = argv[0];
	opterr = 1;
	optarg = NULL;
	optopt = 0;
	optind = 0; /* init */

	set = NULL;
	Hflag = Lflag = Rflag = fflag = hflag = vflag = 0;
	while ((ch = getopt_long(argc, argv, "HLPRXfghorstuvwx", long_options, NULL)) != -1)
		switch (ch) {
		case 'H':
			Hflag = 1;
			Lflag = 0;
			break;
		case 'L':
			Lflag = 1;
			Hflag = 0;
			break;
		case 'P':
			Hflag = Lflag = 0;
			break;
		case 'R':
			Rflag = 1;
			break;
		case 'f':
			fflag = 1;
			break;
		case 'h':
			/*
			 * In System V (and probably POSIX.2) the -h option
			 * causes chmod to change the mode of the symbolic
			 * link.  4.4BSD's symbolic links didn't have modes,
			 * so it was an undocumented noop.  In FreeBSD 3.0,
			 * lchmod(2) is introduced and this option does real
			 * work.
			 */
			hflag = 1;
			break;
		/*
		 * XXX
		 * "-[rwx]" are valid mode commands.  If they are the entire
		 * argument, getopt has moved past them, so decrement optind.
		 * Regardless, we're done argument processing.
		 */
		case 'g': case 'o': case 'r': case 's':
		case 't': case 'u': case 'w': case 'X': case 'x':
			if (argv[optind - 1][0] == '-' &&
			    argv[optind - 1][1] == ch &&
			    argv[optind - 1][2] == '\0')
				--optind;
			goto done;
		case 'v':
			vflag++;
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
done:	argv += optind;
	argc -= optind;

	if (argc < 2)
		return usage(stderr);

	if (Rflag) {
		fts_options = FTS_PHYSICAL;
		if (hflag)
			return errx(1,
		"the -R and -h options may not be specified together.");
		if (Hflag)
			fts_options |= FTS_COMFOLLOW;
		if (Lflag) {
			fts_options &= ~FTS_PHYSICAL;
			fts_options |= FTS_LOGICAL;
		}
	} else
		fts_options = hflag ? FTS_PHYSICAL : FTS_LOGICAL;

	if (hflag)
		change_mode = lchmod;
	else
		change_mode = chmod;

	mode = *argv;
	if ((set = bsd_setmode(mode)) == NULL)
		return errx(1, "invalid file mode: %s", mode);

	if ((ftsp = fts_open(++argv, fts_options, 0)) == NULL)
		return err(1, "fts_open");
	for (rval = 0; (p = fts_read(ftsp)) != NULL;) {
		switch (p->fts_info) {
		case FTS_D:			/* Change it at FTS_DP. */
			if (!Rflag)
				fts_set(ftsp, p, FTS_SKIP);
			continue;
		case FTS_DNR:			/* Warn, chmod, continue. */
			warnx("fts: %s: %s", p->fts_path, strerror(p->fts_errno));
			rval = 1;
			break;
		case FTS_ERR:			/* Warn, continue. */
		case FTS_NS:
			warnx("fts: %s: %s", p->fts_path, strerror(p->fts_errno));
			rval = 1;
			continue;
		case FTS_SL:			/* Ignore. */
		case FTS_SLNONE:
			/*
			 * The only symlinks that end up here are ones that
			 * don't point to anything and ones that we found
			 * doing a physical walk.
			 */
			if (!hflag)
				continue;
			/* else */
			/* FALLTHROUGH */
		default:
			break;
		}
		newmode = bsd_getmode(set, p->fts_statp->st_mode);
		if ((newmode & ALLPERMS) == (p->fts_statp->st_mode & ALLPERMS))
			continue;
		if ((*change_mode)(p->fts_accpath, newmode) && !fflag) {
			warn("%schmod: %s", hflag ? "l" : "", p->fts_path);
			rval = 1;
		} else {
			if (vflag) {
				(void)printf("%s", p->fts_path);

				if (vflag > 1) {
					char m1[12], m2[12];

					bsd_strmode(p->fts_statp->st_mode, m1);
					bsd_strmode((p->fts_statp->st_mode &
					    S_IFMT) | newmode, m2);

					(void)printf(": 0%o [%s] -> 0%o [%s]",
					    (unsigned int)p->fts_statp->st_mode, m1,
					    (unsigned int)((p->fts_statp->st_mode & S_IFMT) | newmode), m2);
				}
				(void)printf("\n");
			}

		}
	}
	if (errno)
		rval = err(1, "fts_read");
	free(set);
	fts_close(ftsp);
	return rval;
}

int
usage(FILE *out)
{
	(void)fprintf(out,
	    "usage: %s [-fhv] [-R [-H | -L | -P]] mode file ...\n"
	    "   or: %s --version\n"
	    "   or: %s --help\n",
	    g_progname, g_progname, g_progname);

	return 1;
}
