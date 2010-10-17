/*-
 * Copyright (c) 1989, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Ken Smith of The State University of New York at Buffalo.
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
static char sccsid[] = "@(#)mv.c	8.2 (Berkeley) 4/2/94";
#endif /* not lint */
#endif
#if 0
#include <sys/cdefs.h>
__FBSDID("$FreeBSD: src/bin/mv/mv.c,v 1.46 2005/09/05 04:36:08 csjp Exp $");
#endif

#include "config.h"
#include <sys/types.h>
#ifndef _MSC_VER
# ifdef CROSS_DEVICE_MOVE
#  include <sys/acl.h>
# endif
# include <sys/param.h>
# include <sys/time.h>
# include <sys/wait.h>
# include <sys/mount.h>
#endif
#include <sys/stat.h>

#include "err.h"
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <limits.h>
#include <paths.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>
#include "getopt.h"
#ifdef __sun__
# include "solfakes.h"
#endif
#ifdef _MSC_VER
# include "mscfakes.h"
#endif
#include "kmkbuiltin.h"


static int fflg, iflg, nflg, vflg;
static struct option long_options[] =
{
    { "help",   					no_argument, 0, 261 },
    { "version",   					no_argument, 0, 262 },
    { 0, 0,	0, 0 },
};


static int	do_move(char *, char *);
#ifdef CROSS_DEVICE_MOVE
static int	fastcopy(char *, char *, struct stat *);
static int	copy(char *, char *);
#endif
static int	usage(FILE *);

extern void bsd_strmode(mode_t mode, char *p);

#if !defined(__FreeBSD__) && !defined(__APPLE__) && !defined(__DragonFly__) && !defined(__OpenBSD__)
# ifdef __OS2__
static
# endif
const char *user_from_uid(uid_t id, int x)
{
	static char s_buf[64];
	sprintf(s_buf, "%ld", (long int)id);
	(void)x;
	return s_buf;
}
# ifdef __OS2__
static
# endif
const char *group_from_gid(gid_t id, int x)
{
	static char s_buf[64];
	sprintf(s_buf, "%ld", (long int)id);
	(void)x;
	return s_buf;
}
#endif /* 'not in libc' */


int
kmk_builtin_mv(int argc, char *argv[], char **envp)
{
	size_t baselen, len;
	int rval;
	char *p, *endp;
	struct stat sb;
	int ch;
	char path[PATH_MAX];

	/* kmk: reinitialize globals */
	fflg = iflg = nflg = vflg = 0;

	/* kmk: reset getopt and set progname */
	g_progname = argv[0];
	opterr = 1;
	optarg = NULL;
	optopt = 0;
	optind = 0; /* init */

	while ((ch = getopt_long(argc, argv, "finv", long_options, NULL)) != -1)
		switch (ch) {
		case 'i':
			iflg = 1;
			fflg = nflg = 0;
			break;
		case 'f':
			fflg = 1;
			iflg = nflg = 0;
			break;
		case 'n':
			nflg = 1;
			fflg = iflg = 0;
			break;
		case 'v':
			vflg = 1;
			break;
		case 261:
			usage(stdout);
			return 0;
		case 262:
			return kbuild_version(argv[0]);
		default:
			return usage(stderr);
		}
	argc -= optind;
	argv += optind;

	if (argc < 2)
		return usage(stderr);

	/*
	 * If the stat on the target fails or the target isn't a directory,
	 * try the move.  More than 2 arguments is an error in this case.
	 */
	if (stat(argv[argc - 1], &sb) || !S_ISDIR(sb.st_mode)) {
		if (argc > 2)
			return usage(stderr);
		return do_move(argv[0], argv[1]);
	}

	/* It's a directory, move each file into it. */
	if (strlen(argv[argc - 1]) > sizeof(path) - 1)
		return errx(1, "%s: destination pathname too long", *argv);
	(void)strcpy(path, argv[argc - 1]);
	baselen = strlen(path);
	endp = &path[baselen];
#if defined(_MSC_VER) || defined(__EMX__)
	if (!baselen || (*(endp - 1) != '/' && *(endp - 1) != '\\' && *(endp - 1) != ':')) {
#else
	if (!baselen || *(endp - 1) != '/') {
#endif
		*endp++ = '/';
		++baselen;
	}
	for (rval = 0; --argc; ++argv) {
		/*
		 * Find the last component of the source pathname.  It
		 * may have trailing slashes.
		 */
		p = *argv + strlen(*argv);
#if defined(_MSC_VER) || defined(__EMX__)
		while (p != *argv && (p[-1] == '/' || p[-1] == '\\'))
			--p;
		while (p != *argv && p[-1] != '/' && p[-1] != '/' && p[-1] != ':')
			--p;
#else
		while (p != *argv && p[-1] == '/')
			--p;
		while (p != *argv && p[-1] != '/')
			--p;
#endif

		if ((baselen + (len = strlen(p))) >= PATH_MAX) {
			warnx("%s: destination pathname too long", *argv);
			rval = 1;
		} else {
			memmove(endp, p, (size_t)len + 1);
			if (do_move(*argv, path))
				rval = 1;
		}
	}
	return rval;
}

static int
do_move(char *from, char *to)
{
	struct stat sb;
	int ask, ch, first;
	char modep[15];

	/*
	 * Check access.  If interactive and file exists, ask user if it
	 * should be replaced.  Otherwise if file exists but isn't writable
	 * make sure the user wants to clobber it.
	 */
	if (!fflg && !access(to, F_OK)) {

		/* prompt only if source exist */
		if (lstat(from, &sb) == -1) {
			warn("%s", from);
			return (1);
		}

#define YESNO "(y/n [n]) "
		ask = 0;
		if (nflg) {
			if (vflg)
				printf("%s not overwritten\n", to);
			return (0);
		} else if (iflg) {
			(void)fprintf(stderr, "overwrite %s? %s", to, YESNO);
			ask = 1;
		} else if (access(to, W_OK) && !stat(to, &sb)) {
			bsd_strmode(sb.st_mode, modep);
			(void)fprintf(stderr, "override %s%s%s/%s for %s? %s",
			    modep + 1, modep[9] == ' ' ? "" : " ",
			    user_from_uid((unsigned long)sb.st_uid, 0),
			    group_from_gid((unsigned long)sb.st_gid, 0), to, YESNO);
			ask = 1;
		}
		if (ask) {
			first = ch = getchar();
			while (ch != '\n' && ch != EOF)
				ch = getchar();
			if (first != 'y' && first != 'Y') {
				(void)fprintf(stderr, "not overwritten\n");
				return (0);
			}
		}
	}
	if (!rename(from, to)) {
		if (vflg)
			printf("%s -> %s\n", from, to);
		return (0);
	}
#ifdef _MSC_VER
	if (errno == EEXIST) {
		remove(to);
		if (!rename(from, to)) {
			if (vflg)
				printf("%s -> %s\n", from, to);
			return (0);
		}
	}
#endif

	if (errno == EXDEV) {
#ifndef CROSS_DEVICE_MOVE
		warnx("cannot move `%s' to a different device: `%s'", from, to);
		return (1);
#else
		struct statfs sfs;
		char path[PATH_MAX];

		/*
		 * If the source is a symbolic link and is on another
		 * filesystem, it can be recreated at the destination.
		 */
		if (lstat(from, &sb) == -1) {
			warn("%s", from);
			return (1);
		}
		if (!S_ISLNK(sb.st_mode)) {
			/* Can't mv(1) a mount point. */
			if (realpath(from, path) == NULL) {
				warnx("cannot resolve %s: %s", from, path);
				return (1);
			}
			if (!statfs(path, &sfs) &&
			    !strcmp(path, sfs.f_mntonname)) {
				warnx("cannot rename a mount point");
				return (1);
			}
		}
#endif
	} else {
		warn("rename %s to %s", from, to);
		return (1);
	}

#ifdef CROSS_DEVICE_MOVE
	/*
	 * If rename fails because we're trying to cross devices, and
	 * it's a regular file, do the copy internally; otherwise, use
	 * cp and rm.
	 */
	if (lstat(from, &sb)) {
		warn("%s", from);
		return (1);
	}
	return (S_ISREG(sb.st_mode) ?
	    fastcopy(from, to, &sb) : copy(from, to));
#endif
}

#ifdef CROSS_DEVICE_MOVE
int
static fastcopy(char *from, char *to, struct stat *sbp)
{
	struct timeval tval[2];
	static u_int blen;
	static char *bp;
	mode_t oldmode;
	int nread, from_fd, to_fd;
	acl_t acl;

	if ((from_fd = open(from, O_RDONLY, 0)) < 0) {
		warn("%s", from);
		return (1);
	}
	if (blen < sbp->st_blksize) {
		if (bp != NULL)
			free(bp);
		if ((bp = malloc((size_t)sbp->st_blksize)) == NULL) {
			blen = 0;
			warnx("malloc failed");
			return (1);
		}
		blen = sbp->st_blksize;
	}
	while ((to_fd =
	    open(to, O_CREAT | O_EXCL | O_TRUNC | O_WRONLY, 0)) < 0) {
		if (errno == EEXIST && unlink(to) == 0)
			continue;
		warn("%s", to);
		(void)close(from_fd);
		return (1);
	}
	while ((nread = read(from_fd, bp, (size_t)blen)) > 0)
		if (write(to_fd, bp, (size_t)nread) != nread) {
			warn("%s", to);
			goto err;
		}
	if (nread < 0) {
		warn("%s", from);
err:		if (unlink(to))
			warn("%s: remove", to);
		(void)close(from_fd);
		(void)close(to_fd);
		return (1);
	}

	oldmode = sbp->st_mode & ALLPERMS;
	if (fchown(to_fd, sbp->st_uid, sbp->st_gid)) {
		warn("%s: set owner/group (was: %lu/%lu)", to,
		    (u_long)sbp->st_uid, (u_long)sbp->st_gid);
		if (oldmode & (S_ISUID | S_ISGID)) {
			warnx(
"%s: owner/group changed; clearing suid/sgid (mode was 0%03o)",
			    to, oldmode);
			sbp->st_mode &= ~(S_ISUID | S_ISGID);
		}
	}
	/*
	 * POSIX 1003.2c states that if _POSIX_ACL_EXTENDED is in effect
	 * for dest_file, then it's ACLs shall reflect the ACLs of the
	 * source_file.
	 */
	if (fpathconf(to_fd, _PC_ACL_EXTENDED) == 1 &&
	    fpathconf(from_fd, _PC_ACL_EXTENDED) == 1) {
		acl = acl_get_fd(from_fd);
		if (acl == NULL)
			warn("failed to get acl entries while setting %s",
			    from);
		else if (acl_set_fd(to_fd, acl) < 0)
			warn("failed to set acl entries for %s", to);
	}
	(void)close(from_fd);
	if (fchmod(to_fd, sbp->st_mode))
		warn("%s: set mode (was: 0%03o)", to, oldmode);
	/*
	 * XXX
	 * NFS doesn't support chflags; ignore errors unless there's reason
	 * to believe we're losing bits.  (Note, this still won't be right
	 * if the server supports flags and we were trying to *remove* flags
	 * on a file that we copied, i.e., that we didn't create.)
	 */
	errno = 0;
	if (fchflags(to_fd, (u_long)sbp->st_flags))
		if (errno != EOPNOTSUPP || sbp->st_flags != 0)
			warn("%s: set flags (was: 0%07o)", to, sbp->st_flags);

	tval[0].tv_sec = sbp->st_atime;
	tval[1].tv_sec = sbp->st_mtime;
	tval[0].tv_usec = tval[1].tv_usec = 0;
	if (utimes(to, tval))
		warn("%s: set times", to);

	if (close(to_fd)) {
		warn("%s", to);
		return (1);
	}

	if (unlink(from)) {
		warn("%s: remove", from);
		return (1);
	}
	if (vflg)
		printf("%s -> %s\n", from, to);
	return (0);
}

int
copy(char *from, char *to)
{
	int pid, status;

	if ((pid = fork()) == 0) {
		execl(_PATH_CP, "mv", vflg ? "-PRpv" : "-PRp", "--", from, to,
		    (char *)NULL);
		warn("%s", _PATH_CP);
		_exit(1);
	}
	if (waitpid(pid, &status, 0) == -1) {
		warn("%s: waitpid", _PATH_CP);
		return (1);
	}
	if (!WIFEXITED(status)) {
		warnx("%s: did not terminate normally", _PATH_CP);
		return (1);
	}
	if (WEXITSTATUS(status)) {
		warnx("%s: terminated with %d (non-zero) status",
		    _PATH_CP, WEXITSTATUS(status));
		return (1);
	}
	if (!(pid = vfork())) {
		execl(_PATH_RM, "mv", "-rf", "--", from, (char *)NULL);
		warn("%s", _PATH_RM);
		_exit(1);
	}
	if (waitpid(pid, &status, 0) == -1) {
		warn("%s: waitpid", _PATH_RM);
		return (1);
	}
	if (!WIFEXITED(status)) {
		warnx("%s: did not terminate normally", _PATH_RM);
		return (1);
	}
	if (WEXITSTATUS(status)) {
		warnx("%s: terminated with %d (non-zero) status",
		    _PATH_RM, WEXITSTATUS(status));
		return (1);
	}
	return (0);
}
#endif /* CROSS_DEVICE_MOVE */


static int
usage(FILE *pf)
{
	fprintf(pf, "usage: %s [-f | -i | -n] [-v] source target\n"
	            "   or: %s [-f | -i | -n] [-v] source ... directory\n"
	            "   or: %s --help\n"
	            "   or: %s --version\n",
	        g_progname, g_progname, g_progname, g_progname);
	return EX_USAGE;
}
