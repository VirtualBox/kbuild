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
#endif
#ifndef _MSC_VER
#include <sys/cdefs.h>
#endif 
//__FBSDID("$FreeBSD: src/bin/mkdir/mkdir.c,v 1.28 2004/04/06 20:06:48 markm Exp $");

#include <sys/types.h>
#include <sys/stat.h>

//#include <err.h>
#include <errno.h>
#ifndef _MSC_VER
#include <libgen.h>
#endif 
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _MSC_VER
#include <sysexits.h>
#include <unistd.h>
#endif 

#ifdef _MSC_VER
#define setmode setmode_msc
#include <stdarg.h>
#include <io.h>
#include <direct.h>
#undef setmode
#include "getopt.h"

typedef int mode_t;
#define EX_USAGE 1
void warn(const char *fmt, ...)
{
    int err = errno;
    va_list args;
    va_start(args, fmt);
    fprintf(stderr, "mkdir: ");
    vfprintf(stderr, fmt, args);
    fprintf(stderr, ": %s\n", strerror(err));
    va_end(args);
}

int mkdir_msc(const char *path, mode_t mode)
{
    int rc = mkdir(path);
    if (rc)
    {
        int len = strlen(path);
        if (len > 0 && (path[len - 1] == '/' || path[len - 1] == '\\'))
        {
            char *str = strdup(path);
            while (len > 0 && (str[len - 1] == '/' || str[len - 1] == '\\'))
                str[--len] = '\0';
            rc = mkdir(str);
            free(str);
        }
    }
    return rc;
}
#define mkdir(a,b) mkdir_msc(a,b)

char *dirname(char *path)
{         
    return path;
}

#define S_ISDIR(m)  (((m) & _S_IFMT) == _S_IFDIR)
#define S_IRWXG 0000070
#define S_IRWXO 0000007
#define	S_IRWXU (_S_IREAD | _S_IWRITE | _S_IEXEC)
#define	S_IXUSR _S_IEXEC
#define	S_IWUSR _S_IWRITE
#define	S_IRUSR _S_IREAD

#endif 

extern void * setmode(const char *p);
extern mode_t getmode(const void *bbox, mode_t omode);

static int	build(char *, mode_t);
static int	usage(void);

static int vflag;

int
kmk_builtin_mkdir(int argc, char *argv[])
{
	int ch, exitval, success, pflag;
	mode_t omode, *set = (mode_t *)NULL;
	char *mode;

	omode = pflag = 0;
	mode = NULL;
        vflag = 0;
        opterr = 1;
        optarg = NULL;
        optopt = 0;
#if defined(__FreeBSD__) || defined(__EMX__)
        optreset = 1;
        optind = 1;
#else
        optind = 0; /* init */
#endif 
	while ((ch = getopt(argc, argv, "m:pv")) != -1)
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
		case '?':
                default:
			return usage();
		}

	argc -= optind;
	argv += optind;
	if (argv[0] == NULL)
		return usage();

	if (mode == NULL) {
		omode = S_IRWXU | S_IRWXG | S_IRWXO;
	} else {
		if ((set = setmode(mode)) == NULL) {
			fprintf(stderr, "%s: invalid file mode: %s\n", mode, argv[0]);
                        return 1;
                }
		omode = getmode(set, S_IRWXU | S_IRWXG | S_IRWXO);
		free(set);
	}

	for (exitval = 0; *argv != NULL; ++argv) {
		success = 1;
		if (pflag) {
			if (build(*argv, omode))
				success = 0;
		} else if (mkdir(*argv, omode) < 0) {
			if (errno == ENOTDIR || errno == ENOENT)
				fprintf(stderr, "%s: %s: %s\n", argv[0], dirname(*argv), strerror(errno));
			else
				fprintf(stderr, "%s: %s: %s\n", argv[0], *argv, strerror(errno));
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
			fprintf(stderr, "%s: %s: %s\n", argv[0], *argv, strerror(errno));
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

	p = path;
	oumask = 0;
	retval = 0;
	if (p[0] == '/')		/* Skip leading '/'. */
		++p;
	for (first = 1, last = 0; !last ; ++p) {
		if (p[0] == '\0')
			last = 1;
		else if (p[0] != '/')
			continue;
		*p = '\0';
		if (p[1] == '\0')
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
			if (errno == EEXIST || errno == EISDIR) {
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
usage(void)
{

	(void)fprintf(stderr, "usage: mkdir [-pv] [-m mode] directory ...\n");
	return EX_USAGE;
}
