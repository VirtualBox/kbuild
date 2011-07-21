/*
 * Copyright (c) 1987, 1993
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
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

#ifndef lint
static const char copyright[] =
"@(#) Copyright (c) 1987, 1993\n\
	The Regents of the University of California.  All rights reserved.\n";
#endif /* not lint */

#if 0
#ifndef lint
static char sccsid[] = "@(#)xinstall.c	8.1 (Berkeley) 7/21/93";
#endif /* not lint */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD: src/usr.bin/xinstall/xinstall.c,v 1.66 2005/01/25 14:34:57 ssouhlal Exp $");
#endif

#include "config.h"
#ifndef _MSC_VER
# include <sys/param.h>
# ifdef USE_MMAP
#  include <sys/mman.h>
# endif
# include <sys/mount.h>
# include <sys/wait.h>
# include <sys/time.h>
#endif /* !_MSC_VER */
#include <sys/stat.h>

#include <ctype.h>
#include "err.h"
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <paths.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>
#if defined(__EMX__) || defined(_MSC_VER)
# include <process.h>
#endif
#include "getopt.h"
#ifdef __sun__
# include "solfakes.h"
#endif
#ifdef _MSC_VER
# include "mscfakes.h"
#endif
#include "kmkbuiltin.h"


extern void * bsd_setmode(const char *p);
extern mode_t bsd_getmode(const void *bbox, mode_t omode);

#ifndef __unused
# define __unused
#endif

#ifndef MAXBSIZE
# define MAXBSIZE 0x20000
#endif

/* Bootstrap aid - this doesn't exist in most older releases */
#ifndef MAP_FAILED
#define MAP_FAILED ((void *)-1)	/* from <sys/mman.h> */
#endif

#define MAX_CMP_SIZE	(16 * 1024 * 1024)

#define	DIRECTORY	0x01		/* Tell install it's a directory. */
#define	SETFLAGS	0x02		/* Tell install to set flags. */
#define	NOCHANGEBITS	(UF_IMMUTABLE | UF_APPEND | SF_IMMUTABLE | SF_APPEND)
#define	BACKUP_SUFFIX	".old"

#ifndef O_BINARY
# define O_BINARY 0
#endif

#ifndef EFTYPE
# define EFTYPE EINVAL
#endif

#if defined(__WIN32__) || defined(__WIN64__) || defined(__OS2__)
# define IS_SLASH(ch)   ((ch) == '/' || (ch) == '\\')
#else
# define IS_SLASH(ch)   ((ch) == '/')
#endif

static gid_t gid;
static uid_t uid;
static int dobackup, docompare, dodir, dopreserve, dostrip, nommap, safecopy, verbose, mode_given;
static mode_t mode = S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH;
static const char *suffix = BACKUP_SUFFIX;
static int ignore_perm_errors;
static int hard_link_files_when_possible;

static struct option long_options[] =
{
    { "help",   					no_argument, 0, 261 },
    { "version",   					no_argument, 0, 262 },
    { "ignore-perm-errors",   				no_argument, 0, 263 },
    { "no-ignore-perm-errors",				no_argument, 0, 264 },
    { "hard-link-files-when-possible",			no_argument, 0, 265 },
    { "no-hard-link-files-when-possible",		no_argument, 0, 266 },
    { 0, 0,	0, 0 },
};


static int	copy(int, const char *, int, const char *, off_t);
static int	compare(int, const char *, size_t, int, const char *, size_t);
static int	create_newfile(const char *, int, struct stat *);
static int	create_tempfile(const char *, char *, size_t);
static int	install(const char *, const char *, u_long, u_int);
static int	install_dir(char *);
static u_long	numeric_id(const char *, const char *);
static int	strip(const char *);
#ifdef USE_MMAP
static int	trymmap(int);
#endif
static int	usage(FILE *);
static char    *last_slash(const char *);

int
kmk_builtin_install(int argc, char *argv[], char ** envp)
{
	struct stat from_sb, to_sb;
	mode_t *set;
	u_long fset = 0;
	int ch, no_target;
	u_int iflags;
	char *flags;
	const char *group, *owner, *to_name;
	(void)envp;

        /* reinitialize globals */
        mode = S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH;
        suffix = BACKUP_SUFFIX;
        gid = 0;
        uid = 0;
        dobackup = docompare = dodir = dopreserve = dostrip = nommap = safecopy = verbose = mode_given = 0;
	ignore_perm_errors = geteuid() != 0;
        hard_link_files_when_possible = 0;

        /* reset getopt and set progname. */
        g_progname = argv[0];
        opterr = 1;
        optarg = NULL;
        optopt = 0;
        optind = 0; /* init */

	iflags = 0;
	group = owner = NULL;
	while ((ch = getopt_long(argc, argv, "B:bCcdf:g:Mm:o:pSsv", long_options, NULL)) != -1)
		switch(ch) {
		case 'B':
			suffix = optarg;
			/* FALLTHROUGH */
		case 'b':
			dobackup = 1;
			break;
		case 'C':
			docompare = 1;
			break;
		case 'c':
			/* For backwards compatibility. */
			break;
		case 'd':
			dodir = 1;
			break;
		case 'f':
#ifdef UF_IMMUTABLE
			flags = optarg;
			if (strtofflags(&flags, &fset, NULL))
				return errx(EX_USAGE, "%s: invalid flag", flags);
			iflags |= SETFLAGS;
#else
			(void)flags;
#endif
			break;
		case 'g':
			group = optarg;
			break;
		case 'M':
			nommap = 1;
			break;
                case 'm':
			if (!(set = bsd_setmode(optarg)))
				return errx(EX_USAGE, "invalid file mode: %s",
				            optarg);
			mode = bsd_getmode(set, 0);
			free(set);
			mode_given = 1;
			break;
		case 'o':
			owner = optarg;
			break;
		case 'p':
			docompare = dopreserve = 1;
			break;
		case 'S':
			safecopy = 1;
			break;
		case 's':
			dostrip = 1;
			break;
		case 'v':
			verbose = 1;
			break;
		case 261:
			usage(stdout);
			return 0;
		case 262:
			return kbuild_version(argv[0]);
		case 263:
			ignore_perm_errors = 1;
			break;
		case 264:
			ignore_perm_errors = 0;
			break;
                case 265:
                        hard_link_files_when_possible = 1;
                        break;
                case 266:
                        hard_link_files_when_possible = 0;
                        break;
		case '?':
		default:
			return usage(stderr);
		}
	argc -= optind;
	argv += optind;

	/* some options make no sense when creating directories */
	if (dostrip && dodir) {
		warnx("-d and -s may not be specified together");
		return usage(stderr);
	}

	/* must have at least two arguments, except when creating directories */
	if (argc == 0 || (argc == 1 && !dodir))
		return usage(stderr);

	/* need to make a temp copy so we can compare stripped version */
	if (docompare && dostrip)
		safecopy = 1;

	/* get group and owner id's */
	if (group != NULL) {
#ifndef _MSC_VER
		struct group *gp;
		if ((gp = getgrnam(group)) != NULL)
			gid = gp->gr_gid;
		else
#endif
		{
			gid = (gid_t)numeric_id(group, "group");
			if (gid == (gid_t)-1)
				return 1;
		}
	} else
		gid = (gid_t)-1;

	if (owner != NULL) {
#ifndef _MSC_VER
                struct passwd *pp;
		if ((pp = getpwnam(owner)) != NULL)
			uid = pp->pw_uid;
		else
#endif
		{
			uid = (uid_t)numeric_id(owner, "user");
			if (uid == (uid_t)-1)
				return 1;
		}
	} else
		uid = (uid_t)-1;

	if (dodir) {
		for (; *argv != NULL; ++argv) {
			int rc = install_dir(*argv);
			if (rc)
				return rc;
		}
		return EX_OK;
		/* NOTREACHED */
	}

	no_target = stat(to_name = argv[argc - 1], &to_sb);
	if (!no_target && S_ISDIR(to_sb.st_mode)) {
		for (; *argv != to_name; ++argv) {
			int rc = install(*argv, to_name, fset, iflags | DIRECTORY);
			if (rc)
				return rc;
		}
		return EX_OK;
	}

	/* can't do file1 file2 directory/file */
	if (argc != 2) {
		warnx("wrong number or types of arguments");
		return usage(stderr);
	}

	if (!no_target) {
		if (stat(*argv, &from_sb))
			return err(EX_OSERR, "%s", *argv);
		if (!S_ISREG(to_sb.st_mode)) {
			errno = EFTYPE;
			return err(EX_OSERR, "%s", to_name);
		}
		if (to_sb.st_dev == from_sb.st_dev &&
                    to_sb.st_dev != 0 &&
		    to_sb.st_ino == from_sb.st_ino &&
		    to_sb.st_ino != 0 &&
		    !hard_link_files_when_possible)
			return errx(EX_USAGE,
			            "%s and %s are the same file", *argv, to_name);
	}
	return install(*argv, to_name, fset, iflags);
}

static u_long
numeric_id(const char *name, const char *type)
{
	u_long val;
	char *ep;

	/*
	 * XXX
	 * We know that uid_t's and gid_t's are unsigned longs.
	 */
	errno = 0;
	val = strtoul(name, &ep, 10);
	if (errno)
		return err(-1, "%s", name);
	if (*ep != '\0')
		return errx(-1, "unknown %s %s", type, name);
	return (val);
}

/*
 * install --
 *	build a path name and install the file
 */
static int
install(const char *from_name, const char *to_name, u_long fset, u_int flags)
{
	struct stat from_sb, temp_sb, to_sb;
	struct timeval tvb[2];
	int devnull, files_match, from_fd, serrno, target;
	int tempcopy, temp_fd, to_fd;
	char backup[MAXPATHLEN], *p, pathbuf[MAXPATHLEN], tempfile[MAXPATHLEN];
	int rc = EX_OK;

	files_match = 0;
	from_fd = -1;
	to_fd = -1;
	temp_fd = -1;

	/* If try to install NULL file to a directory, fails. */
	if (flags & DIRECTORY
#if defined(__EMX__) || defined(_MSC_VER)
	    || (   stricmp(from_name, _PATH_DEVNULL)
		&& stricmp(from_name, "nul")
# ifdef __EMX__
		&& stricmp(from_name, "/dev/nul")
# endif
	       )
#else
	    || strcmp(from_name, _PATH_DEVNULL)
#endif
	    ) {
		if (stat(from_name, &from_sb))
			return err(EX_OSERR, "%s", from_name);
		if (!S_ISREG(from_sb.st_mode)) {
			errno = EFTYPE;
			return err(EX_OSERR, "%s", from_name);
		}
		/* Build the target path. */
		if (flags & DIRECTORY) {
			(void)snprintf(pathbuf, sizeof(pathbuf), "%s/%s",
			    to_name,
			    (p = last_slash(from_name)) ? ++p : from_name);
			to_name = pathbuf;
		}
		devnull = 0;
	} else {
		devnull = 1;
	}

	target = stat(to_name, &to_sb) == 0;

	/* Only install to regular files. */
	if (target && !S_ISREG(to_sb.st_mode)) {
		errno = EFTYPE;
		warn("%s", to_name);
		return EX_OK;
	}

	/* Only copy safe if the target exists. */
	tempcopy = safecopy && target;

	/* Try hard linking if wanted and possible. */
	if (hard_link_files_when_possible)
	{
		const char *why_not = NULL;
		if (devnull) {
			why_not = "/dev/null";
		} else if (dostrip) {
			why_not = "strip (-s)";
		} else if (docompare) {
			why_not = "compare (-C)";
		} else if (dobackup) {
			why_not = "backup (-b/-B)";
		} else if (safecopy) {
			why_not = "safe copy (-S)";
		} else if (mode != (from_sb.st_mode & ALLPERMS)) {
			printf("install: warning: Not hard linking, mode differs: 0%03o, desires 0%03o\n"
			       "install: src path '%s'\n"
			       "install: dst path '%s'\n",
			       (from_sb.st_mode & ALLPERMS), mode, from_name, to_name);
			why_not = NULL;
		} else if (uid != (uid_t)-1 && gid != from_sb.st_uid) {
			why_not = "uid mismatch";
		} else if (gid != (gid_t)-1 && gid != from_sb.st_gid) {
			why_not = "gid mismatch";
		} else {
			int rcLink = link(from_name, to_name);
			if (rcLink != 0 && errno == EEXIST) {
			    unlink(to_name);
			    rcLink = link(from_name, to_name);
			}
			if (rcLink == 0) {
			    if (verbose)
				    printf("install: %s -> %s (hardlinked)\n", from_name, to_name);
			    goto l_done;
			}
			if (verbose)
				printf("install: hard linking '%s' to '%s' failed: %s\n",
				       to_name, from_name, strerror(errno));
			why_not = NULL;
		}
		if (verbose && why_not)
		    printf("install: not hard linking '%s' to '%s' because: %s\n",
			   to_name, from_name, why_not);

		/* Can't hard link or we failed, continue as nothing happend. */
	}

	if (!devnull && (from_fd = open(from_name, O_RDONLY | O_BINARY, 0)) < 0)
		return err(EX_OSERR, "%s", from_name);

	/* If we don't strip, we can compare first. */
	if (docompare && !dostrip && target) {
		if ((to_fd = open(to_name, O_RDONLY | O_BINARY, 0)) < 0) {
			rc = err(EX_OSERR, "%s", to_name);
			goto l_done;
		}
		if (devnull)
			files_match = to_sb.st_size == 0;
		else
			files_match = !(compare(from_fd, from_name,
			    (size_t)from_sb.st_size, to_fd,
			    to_name, (size_t)to_sb.st_size));

		/* Close "to" file unless we match. */
		if (!files_match) {
			(void)close(to_fd);
			to_fd = -1;
		}
	}

	if (!files_match) {
		if (tempcopy) {
			to_fd = create_tempfile(to_name, tempfile,
			    sizeof(tempfile));
			if (to_fd < 0) {
				rc = err(EX_OSERR, "%s", tempfile);
				goto l_done;
			}
		} else {
			if ((to_fd = create_newfile(to_name, target,
			    &to_sb)) < 0) {
				rc = err(EX_OSERR, "%s", to_name);
				goto l_done;
			}
			if (verbose)
				(void)printf("install: %s -> %s\n",
				    from_name, to_name);
		}
		if (!devnull) {
			rc = copy(from_fd, from_name, to_fd,
			          tempcopy ? tempfile : to_name, from_sb.st_size);
			if (rc)
    				goto l_done;
		}
	}

	if (dostrip) {
#if defined(__EMX__) || defined(_MSC_VER)
		/* close before we strip. */
		close(to_fd);
		to_fd = -1;
#endif
		rc = strip(tempcopy ? tempfile : to_name);
		if (rc)
			goto l_done;

		/*
		 * Re-open our fd on the target, in case we used a strip
		 * that does not work in-place -- like GNU binutils strip.
		 */
#if !defined(__EMX__) && !defined(_MSC_VER)
		close(to_fd);
#endif
		to_fd = open(tempcopy ? tempfile : to_name, O_RDONLY | O_BINARY, 0);
		if (to_fd < 0) {
			rc = err(EX_OSERR, "stripping %s", to_name);
			goto l_done;
		}
	}

	/*
	 * Compare the stripped temp file with the target.
	 */
	if (docompare && dostrip && target) {
		temp_fd = to_fd;

		/* Re-open to_fd using the real target name. */
		if ((to_fd = open(to_name, O_RDONLY | O_BINARY, 0)) < 0) {
			rc = err(EX_OSERR, "%s", to_name);
			goto l_done;
		}

		if (fstat(temp_fd, &temp_sb)) {
			serrno = errno;
			(void)unlink(tempfile);
			errno = serrno;
			rc = err(EX_OSERR, "%s", tempfile);
			goto l_done;
		}

		if (compare(temp_fd, tempfile, (size_t)temp_sb.st_size, to_fd,
			    to_name, (size_t)to_sb.st_size) == 0) {
			/*
			 * If target has more than one link we need to
			 * replace it in order to snap the extra links.
			 * Need to preserve target file times, though.
			 */
#if !defined(_MSC_VER) && !defined(__EMX__)
			if (to_sb.st_nlink != 1) {
				tvb[0].tv_sec = to_sb.st_atime;
				tvb[0].tv_usec = 0;
				tvb[1].tv_sec = to_sb.st_mtime;
				tvb[1].tv_usec = 0;
				(void)utimes(tempfile, tvb);
			} else
#endif
                        {

				files_match = 1;
				(void)unlink(tempfile);
			}
			(void) close(temp_fd);
			temp_fd = -1;
		}
	}

	/*
	 * Move the new file into place if doing a safe copy
	 * and the files are different (or just not compared).
	 */
	if (tempcopy && !files_match) {
#ifdef UF_IMMUTABLE
		/* Try to turn off the immutable bits. */
		if (to_sb.st_flags & NOCHANGEBITS)
			(void)chflags(to_name, to_sb.st_flags & ~NOCHANGEBITS);
#endif
		if (dobackup) {
			if ((size_t)snprintf(backup, MAXPATHLEN, "%s%s", to_name,
			    suffix) != strlen(to_name) + strlen(suffix)) {
				unlink(tempfile);
				rc = errx(EX_OSERR, "%s: backup filename too long",
				          to_name);
				goto l_done;
			}
			if (verbose)
				(void)printf("install: %s -> %s\n", to_name, backup);
			if (rename(to_name, backup) < 0) {
				serrno = errno;
				unlink(tempfile);
				errno = serrno;
				rc = err(EX_OSERR, "rename: %s to %s", to_name,
				         backup);
				goto l_done;
			}
		}
		if (verbose)
			(void)printf("install: %s -> %s\n", from_name, to_name);
		if (rename(tempfile, to_name) < 0) {
			serrno = errno;
			unlink(tempfile);
			errno = serrno;
			rc = err(EX_OSERR, "rename: %s to %s",
			         tempfile, to_name);
			goto l_done;
		}

		/* Re-open to_fd so we aren't hosed by the rename(2). */
		(void) close(to_fd);
		if ((to_fd = open(to_name, O_RDONLY | O_BINARY, 0)) < 0) {
			rc = err(EX_OSERR, "%s", to_name);
			goto l_done;
		}
	}

	/*
	 * Preserve the timestamp of the source file if necessary.
	 */
	if (dopreserve && !files_match && !devnull) {
		tvb[0].tv_sec = from_sb.st_atime;
		tvb[0].tv_usec = 0;
		tvb[1].tv_sec = from_sb.st_mtime;
		tvb[1].tv_usec = 0;
		(void)utimes(to_name, tvb);
	}

	if (fstat(to_fd, &to_sb) == -1) {
		serrno = errno;
		(void)unlink(to_name);
		errno = serrno;
		rc = err(EX_OSERR, "%s", to_name);
		goto l_done;
	}

	/*
	 * Set owner, group, mode for target; do the chown first,
	 * chown may lose the setuid bits.
	 */
#ifdef UF_IMMUTABLE
	if ((gid != (gid_t)-1 && gid != to_sb.st_gid) ||
	    (uid != (uid_t)-1 && uid != to_sb.st_uid) ||
	    (mode != (to_sb.st_mode & ALLPERMS))) {
		/* Try to turn off the immutable bits. */
		if (to_sb.st_flags & NOCHANGEBITS)
			(void)fchflags(to_fd, to_sb.st_flags & ~NOCHANGEBITS);
	}
#endif

	if ((gid != (gid_t)-1 && gid != to_sb.st_gid) ||
	    (uid != (uid_t)-1 && uid != to_sb.st_uid))
		if (fchown(to_fd, uid, gid) == -1) {
    			if (errno == EPERM && ignore_perm_errors) {
				warn("%s: ignoring chown uid=%d gid=%d failure", to_name, (int)uid, (int)gid);
			} else {
				serrno = errno;
				(void)unlink(to_name);
				errno = serrno;
				rc = err(EX_OSERR,"%s: chown/chgrp", to_name);
				goto l_done;
			}
		}

	if (mode != (to_sb.st_mode & ALLPERMS))
		if (fchmod(to_fd, mode)) {
			serrno = errno;
			if (serrno == EPERM && ignore_perm_errors) {
				fchmod(to_fd, mode & (ALLPERMS & ~0007000));
				errno = errno;
				warn("%s: ignoring chmod 0%o failure", to_name, (int)(mode & ALLPERMS));
			} else  {
				serrno = errno;
				(void)unlink(to_name);
				errno = serrno;
				rc = err(EX_OSERR, "%s: chmod", to_name);
				goto l_done;
			}
		}

	/*
	 * If provided a set of flags, set them, otherwise, preserve the
	 * flags, except for the dump flag.
	 * NFS does not support flags.  Ignore EOPNOTSUPP flags if we're just
	 * trying to turn off UF_NODUMP.  If we're trying to set real flags,
	 * then warn if the the fs doesn't support it, otherwise fail.
	 */
#ifdef UF_IMMUTABLE
	if (!devnull && (flags & SETFLAGS ||
	    (from_sb.st_flags & ~UF_NODUMP) != to_sb.st_flags) &&
	    fchflags(to_fd,
	    flags & SETFLAGS ? fset : from_sb.st_flags & ~UF_NODUMP)) {
		if (flags & SETFLAGS) {
			if (errno == EOPNOTSUPP)
				warn("%s: chflags", to_name);
			else {
				serrno = errno;
				(void)unlink(to_name);
				errno = serrno;
				rc = err(EX_OSERR, "%s: chflags", to_name);
				goto l_done;
			}
		}
	}
#endif

l_done:
	if (to_fd >= 0)
		(void)close(to_fd);
	if (temp_fd >= 0)
		(void)close(temp_fd);
	if (!devnull)
		(void)close(from_fd);
	return rc;
}

/*
 * compare --
 *	compare two files; non-zero means files differ
 */
static int
compare(int from_fd, const char *from_name __unused, size_t from_len,
	int to_fd, const char *to_name __unused, size_t to_len)
{
	char *p, *q;
	int rv;
	int done_compare;

	rv = 0;
	if (from_len != to_len)
		return 1;

	if (from_len <= MAX_CMP_SIZE) {
#ifdef USE_MMAP
		done_compare = 0;
		if (trymmap(from_fd) && trymmap(to_fd)) {
			p = mmap(NULL, from_len, PROT_READ, MAP_SHARED, from_fd, (off_t)0);
			if (p == (char *)MAP_FAILED)
				goto out;
			q = mmap(NULL, from_len, PROT_READ, MAP_SHARED, to_fd, (off_t)0);
			if (q == (char *)MAP_FAILED) {
				munmap(p, from_len);
				goto out;
			}

			rv = memcmp(p, q, from_len);
			munmap(p, from_len);
			munmap(q, from_len);
			done_compare = 1;
		}
	out:
#else
	(void)p; (void)q;
	done_compare = 0;
#endif
		if (!done_compare) {
			char buf1[MAXBSIZE];
			char buf2[MAXBSIZE];
			int n1, n2;

			rv = 0;
			lseek(from_fd, 0, SEEK_SET);
			lseek(to_fd, 0, SEEK_SET);
			while (rv == 0) {
				n1 = read(from_fd, buf1, sizeof(buf1));
				if (n1 == 0)
					break;		/* EOF */
				else if (n1 > 0) {
					n2 = read(to_fd, buf2, n1);
					if (n2 == n1)
						rv = memcmp(buf1, buf2, n1);
					else
						rv = 1;	/* out of sync */
				} else
					rv = 1;		/* read failure */
			}
			lseek(from_fd, 0, SEEK_SET);
			lseek(to_fd, 0, SEEK_SET);
		}
	} else
		rv = 1;	/* don't bother in this case */

	return rv;
}

/*
 * create_tempfile --
 *	create a temporary file based on path and open it
 */
int
create_tempfile(const char *path, char *temp, size_t tsize)
{
	char *p;

	(void)strncpy(temp, path, tsize);
	temp[tsize - 1] = '\0';
	if ((p = last_slash(temp)) != NULL)
		p++;
	else
		p = temp;
	(void)strncpy(p, "INS@XXXX", &temp[tsize - 1] - p);
	temp[tsize - 1] = '\0';
	return (mkstemp(temp));
}

/*
 * create_newfile --
 *	create a new file, overwriting an existing one if necessary
 */
int
create_newfile(const char *path, int target, struct stat *sbp)
{
	char backup[MAXPATHLEN];
	int saved_errno = 0;
	int newfd;

	if (target) {
		/*
		 * Unlink now... avoid ETXTBSY errors later.  Try to turn
		 * off the append/immutable bits -- if we fail, go ahead,
		 * it might work.
		 */
#ifdef UF_IMMUTABLE
		if (sbp->st_flags & NOCHANGEBITS)
			(void)chflags(path, sbp->st_flags & ~NOCHANGEBITS);
#endif

		if (dobackup) {
			if ((size_t)snprintf(backup, MAXPATHLEN, "%s%s",
			    path, suffix) != strlen(path) + strlen(suffix)) {
				errx(EX_OSERR, "%s: backup filename too long",
				     path);
				errno = ENAMETOOLONG;
				return -1;
			}
			(void)snprintf(backup, MAXPATHLEN, "%s%s",
			    path, suffix);
			if (verbose)
				(void)printf("install: %s -> %s\n",
				    path, backup);
			if (rename(path, backup) < 0) {
				err(EX_OSERR, "rename: %s to %s", path, backup);
				return -1;
			}
		} else
			if (unlink(path) < 0)
				saved_errno = errno;
	}

	newfd = open(path, O_CREAT | O_RDWR | O_TRUNC | O_BINARY, S_IRUSR | S_IWUSR);
	if (newfd < 0 && saved_errno != 0)
		errno = saved_errno;
	return newfd;
}

/*
 * copy --
 *	copy from one file to another
 */
static int
copy(int from_fd, const char *from_name, int to_fd, const char *to_name,
    off_t size)
{
	int nr, nw;
	int serrno;
	char *p, buf[MAXBSIZE];
	int done_copy;

	/* Rewind file descriptors. */
	if (lseek(from_fd, (off_t)0, SEEK_SET) == (off_t)-1)
		return err(EX_OSERR, "lseek: %s", from_name);
	if (lseek(to_fd, (off_t)0, SEEK_SET) == (off_t)-1)
		return err(EX_OSERR, "lseek: %s", to_name);

	/*
	 * Mmap and write if less than 8M (the limit is so we don't totally
	 * trash memory on big files.  This is really a minor hack, but it
	 * wins some CPU back.
	 */
	done_copy = 0;
#ifdef USE_MMAP
	if (size <= 8 * 1048576 && trymmap(from_fd) &&
	    (p = mmap(NULL, (size_t)size, PROT_READ, MAP_SHARED,
		    from_fd, (off_t)0)) != (char *)MAP_FAILED) {
		if ((nw = write(to_fd, p, size)) != size) {
			serrno = errno;
			(void)unlink(to_name);
			errno = nw > 0 ? EIO : serrno;
			err(EX_OSERR, "%s", to_name);
		}
		done_copy = 1;
	}
#else
	(void)p; (void)size;
#endif
	if (!done_copy) {
		while ((nr = read(from_fd, buf, sizeof(buf))) > 0)
			if ((nw = write(to_fd, buf, nr)) != nr) {
				serrno = errno;
				(void)unlink(to_name);
				errno = nw > 0 ? EIO : serrno;
				return err(EX_OSERR, "%s", to_name);
			}
		if (nr != 0) {
			serrno = errno;
			(void)unlink(to_name);
			errno = serrno;
			return err(EX_OSERR, "%s", from_name);
		}
	}
	return EX_OK;
}

/*
 * strip --
 *	use strip(1) to strip the target file
 */
static int
strip(const char *to_name)
{
#if defined(__EMX__) || defined(_MSC_VER)
	const char *stripbin = getenv("STRIPBIN");
	if (stripbin == NULL)
		stripbin = "strip";
	return spawnlp(P_WAIT, stripbin, stripbin, to_name, NULL);
#else
	const char *stripbin;
	int serrno, status;
        pid_t pid;

        pid = fork();
	switch (pid) {
	case -1:
		serrno = errno;
		(void)unlink(to_name);
		errno = serrno;
		return err(EX_TEMPFAIL, "fork");
	case 0:
		stripbin = getenv("STRIPBIN");
		if (stripbin == NULL)
			stripbin = "strip";
		execlp(stripbin, stripbin, to_name, (char *)NULL);
		err(EX_OSERR, "exec(%s)", stripbin);
                exit(EX_OSERR);
	default:
		if (waitpid(pid, &status, 0) == -1 || status) {
			serrno = errno;
			(void)unlink(to_name);
                        errno = serrno;
			return err(EX_SOFTWARE, "waitpid");
			/* NOTREACHED */
		}
	}
        return 0;
#endif
}

/*
 * install_dir --
 *	build directory heirarchy
 */
static int
install_dir(char *path)
{
	char *p;
	struct stat sb;
	int ch;

	for (p = path;; ++p)
		if (!*p || (p != path && IS_SLASH(*p))) {
			ch = *p;
			*p = '\0';
			if (stat(path, &sb)) {
				if (errno != ENOENT || mkdir(path, 0755) < 0) {
					return err(EX_OSERR, "mkdir %s", path);
					/* NOTREACHED */
				} else if (verbose)
					(void)printf("install: mkdir %s\n",
						     path);
			} else if (!S_ISDIR(sb.st_mode))
				return errx(EX_OSERR, "%s exists but is not a directory", path);
			if (!(*p = ch))
				break;
 		}

	if ((gid != (gid_t)-1 || uid != (uid_t)-1) && chown(path, uid, gid))
		warn("chown %u:%u %s", uid, gid, path);
	if (chmod(path, mode))
		warn("chmod %o %s", mode, path);
	return EX_OK;
}

/*
 * usage --
 *	print a usage message and die
 */
static int
usage(FILE *pf)
{
	fprintf(pf,
"usage: %s [-bCcpSsv] [--[no-]hard-link-files-when-possible]\n"
"            [--[no-]ignore-perm-errors] [-B suffix] [-f flags]\n"
"            [-g group] [-m mode] [-o owner] file1 file2\n"
"   or: %s [-bCcpSsv] [--[no-]ignore-perm-errors] [-B suffix] [-f flags]\n"
"            [-g group] [-m mode] [-o owner] file1 ... fileN directory\n"
"   or: %s -d [-v] [-g group] [-m mode] [-o owner] directory ...\n"
"   or: %s --help\n"
"   or: %s --version\n",
			g_progname, g_progname, g_progname, g_progname, g_progname);
	return EX_USAGE;
}

#ifdef USE_MMAP
/*
 * trymmap --
 *	return true (1) if mmap should be tried, false (0) if not.
 */
static int
trymmap(int fd)
{
/*
 * The ifdef is for bootstrapping - f_fstypename doesn't exist in
 * pre-Lite2-merge systems.
 */
#ifdef MFSNAMELEN
	struct statfs stfs;

	if (nommap || fstatfs(fd, &stfs) != 0)
		return (0);
	if (strcmp(stfs.f_fstypename, "ufs") == 0 ||
	    strcmp(stfs.f_fstypename, "cd9660") == 0)
		return (1);
#endif
	return (0);
}
#endif

/* figures out where the last slash or colon is. */
static char *
last_slash(const char *path)
{
#if defined(__WIN32__) || defined(__WIN64__) || defined(__OS2__)
    char *p = (char *)strrchr(path, '/');
    if (p)
    {
        char *p2 = strrchr(p, '\\');
        if (p2)
            p = p2;
    }
    else
    {
        p = (char *)strrchr(path, '\\');
        if (!p && isalpha(path[0]) && path[1] == ':')
            p = (char *)&path[1];
    }
    return p;
#else
    return strrchr(path, '/');
#endif
}

