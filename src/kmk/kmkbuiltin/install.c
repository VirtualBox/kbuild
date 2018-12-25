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

#if 0 /*ndef lint*/
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

#define FAKES_NO_GETOPT_H
#include "config.h"
#ifndef _MSC_VER
# include <sys/param.h>
# if !defined(__HAIKU__) && !defined(__gnu_hurd__)
#  include <sys/mount.h>
# endif
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
#ifndef __HAIKU__
# include <sysexits.h>
#endif
#ifdef __NetBSD__
# include <util.h>
# define strtofflags(a, b, c)	string_to_flags(a, b, c)
#endif
#include <unistd.h>
#if defined(__EMX__) || defined(_MSC_VER)
# include <process.h>
#endif
#include "getopt_r.h"
#ifdef __sun__
# include "solfakes.h"
#endif
#ifdef _MSC_VER
# include "mscfakes.h"
#endif
#ifdef __HAIKU__
# include "haikufakes.h"
#endif
#include "kmkbuiltin.h"
#include "k/kDefs.h"	/* for K_OS */
#include "dos2unix.h"


extern void * bsd_setmode(const char *p);
extern mode_t bsd_getmode(const void *bbox, mode_t omode);

#ifndef MAXBSIZE
# define MAXBSIZE 0x20000
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


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
typedef struct INSTALLINSTANCE
{
    PKMKBUILTINCTX pCtx;

    gid_t gid;
    uid_t uid;
    int dobackup, docompare, dodir, dopreserve, dostrip, nommap, safecopy, verbose, mode_given;
    mode_t mode;
    const char *suffix;
    int ignore_perm_errors;
    int hard_link_files_when_possible;
    int dos2unix;
} INSTALLINSTANCE;
typedef INSTALLINSTANCE *PINSTALLINSTANCE;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
static struct option long_options[] =
{
    { "help",   					no_argument, 0, 261 },
    { "version",   					no_argument, 0, 262 },
    { "ignore-perm-errors",   				no_argument, 0, 263 },
    { "no-ignore-perm-errors",				no_argument, 0, 264 },
    { "hard-link-files-when-possible",			no_argument, 0, 265 },
    { "no-hard-link-files-when-possible",		no_argument, 0, 266 },
    { "dos2unix",					no_argument, 0, 267 },
    { "unix2dos",					no_argument, 0, 268 },
    { 0, 0,	0, 0 },
};


static int	copy(PINSTALLINSTANCE, int, const char *, int *, const char *);
static int	compare(int, size_t, int, size_t);
static int	create_newfile(PINSTALLINSTANCE, const char *, int, struct stat *);
static int	create_tempfile(const char *, char *, size_t);
static int	install(PINSTALLINSTANCE, const char *, const char *, u_long, u_int);
static int	install_dir(PINSTALLINSTANCE, char *);
static u_long	numeric_id(PINSTALLINSTANCE, const char *, const char *);
static int	strip(PINSTALLINSTANCE, const char *);
static int	usage(PKMKBUILTINCTX, int);
static char    *last_slash(const char *);
static KBOOL	needs_dos2unix_conversion(const char *pszFilename);
static KBOOL	needs_unix2dos_conversion(const char *pszFilename);

int
kmk_builtin_install(int argc, char *argv[], char ** envp, PKMKBUILTINCTX pCtx)
{
	INSTALLINSTANCE This;
	struct getopt_state_r gos;
	struct stat from_sb, to_sb;
	mode_t *set;
	u_long fset = 0;
	int ch, no_target;
	u_int iflags;
	char *flags;
	const char *group, *owner, *to_name;
	(void)envp;

	/* Initialize global instance data. */
	This.pCtx = pCtx;
	This.mode = S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH;
	This.suffix = BACKUP_SUFFIX;
	This.gid = 0;
	This.uid = 0;
	This.dobackup = 0;
	This.docompare = 0;
	This.dodir = 0;
	This.dopreserve = 0;
	This.dostrip = 0;
	This.nommap = 0;
	This.safecopy = 0;
	This.verbose = 0;
	This.mode_given = 0;
	This.ignore_perm_errors = geteuid() != 0;
	This.hard_link_files_when_possible = 0;
	This.dos2unix = 0;

	iflags = 0;
	group = owner = NULL;
	getopt_initialize_r(&gos, argc, argv, "B:bCcdf:g:Mm:o:pSsv", long_options, envp, pCtx);
	while ((ch = getopt_long_r(&gos, NULL)) != -1)
		switch(ch) {
		case 'B':
			This.suffix = gos.optarg;
			/* FALLTHROUGH */
		case 'b':
			This.dobackup = 1;
			break;
		case 'C':
			This.docompare = 1;
			break;
		case 'c':
			/* For backwards compatibility. */
			break;
		case 'd':
			This.dodir = 1;
			break;
		case 'f':
#if defined(UF_IMMUTABLE) && K_OS != K_OS_GNU_KFBSD && K_OS != K_OS_GNU_HURD
			flags = optarg;
			if (strtofflags(&flags, &fset, NULL))
				return errx(pCtx, EX_USAGE, "%s: invalid flag", flags);
			iflags |= SETFLAGS;
#else
			(void)flags;
#endif
			break;
		case 'g':
			group = gos.optarg;
			break;
		case 'M':
			This.nommap = 1;
			break;
                case 'm':
			if (!(set = bsd_setmode(gos.optarg)))
				return errx(pCtx, EX_USAGE, "invalid file mode: %s", gos.optarg);
			This.mode = bsd_getmode(set, 0);
			free(set);
			This.mode_given = 1;
			break;
		case 'o':
			owner = gos.optarg;
			break;
		case 'p':
			This.docompare = This.dopreserve = 1;
			break;
		case 'S':
			This.safecopy = 1;
			break;
		case 's':
			This.dostrip = 1;
			break;
		case 'v':
			This.verbose = 1;
			break;
		case 261:
			usage(pCtx, 0);
			return 0;
		case 262:
			return kbuild_version(argv[0]);
		case 263:
			This.ignore_perm_errors = 1;
			break;
		case 264:
			This.ignore_perm_errors = 0;
			break;
                case 265:
                        This.hard_link_files_when_possible = 1;
                        break;
                case 266:
                        This.hard_link_files_when_possible = 0;
                        break;
		case 267:
			This.dos2unix = 1;
			break;
		case 268:
			This.dos2unix = -1;
			break;
		case '?':
		default:
			return usage(pCtx, 1);
		}
	argc -= gos.optind;
	argv += gos.optind;

	/* some options make no sense when creating directories */
	if (This.dostrip && This.dodir) {
		warnx(pCtx, "-d and -s may not be specified together");
		return usage(pCtx, 1);
	}

	/* must have at least two arguments, except when creating directories */
	if (argc == 0 || (argc == 1 && !This.dodir))
		return usage(pCtx, 1);

	/*   and unix2dos doesn't combine well with a couple of other options. */
	if (This.dos2unix != 0) {
		if (This.docompare) {
			warnx(pCtx, "-C/-p and --dos2unix/unix2dos may not be specified together");
			return usage(pCtx, 1);
		}
		if (This.dostrip) {
			warnx(pCtx, "-s and --dos2unix/unix2dos may not be specified together");
			return usage(pCtx, 1);
		}
	}

	/* need to make a temp copy so we can compare stripped version */
	if (This.docompare && This.dostrip)
		This.safecopy = 1;

	/* get group and owner id's */
	if (group != NULL) {
#ifndef _MSC_VER
		struct group *gp;
		if ((gp = getgrnam(group)) != NULL)
			This.gid = gp->gr_gid;
		else
#endif
		{
			This.gid = (gid_t)numeric_id(&This, group, "group");
			if (This.gid == (gid_t)-1)
				return 1;
		}
	} else
		This.gid = (gid_t)-1;

	if (owner != NULL) {
#ifndef _MSC_VER
                struct passwd *pp;
		if ((pp = getpwnam(owner)) != NULL)
			This.uid = pp->pw_uid;
		else
#endif
		{
			This.uid = (uid_t)numeric_id(&This, owner, "user");
			if (This.uid == (uid_t)-1)
				return 1;
		}
	} else
		This.uid = (uid_t)-1;

	if (This.dodir) {
		for (; *argv != NULL; ++argv) {
			int rc = install_dir(&This, *argv);
			if (rc)
				return rc;
		}
		return EX_OK;
		/* NOTREACHED */
	}

	no_target = stat(to_name = argv[argc - 1], &to_sb);
	if (!no_target && S_ISDIR(to_sb.st_mode)) {
		for (; *argv != to_name; ++argv) {
			int rc = install(&This, *argv, to_name, fset, iflags | DIRECTORY);
			if (rc)
				return rc;
		}
		return EX_OK;
	}

	/* can't do file1 file2 directory/file */
	if (argc != 2) {
		warnx(pCtx, "wrong number or types of arguments");
		return usage(pCtx, 1);
	}

	if (!no_target) {
		if (stat(*argv, &from_sb))
			return err(pCtx, EX_OSERR, "%s", *argv);
		if (!S_ISREG(to_sb.st_mode)) {
			errno = EFTYPE;
			return err(pCtx, EX_OSERR, "%s", to_name);
		}
		if (to_sb.st_dev == from_sb.st_dev &&
                    to_sb.st_dev != 0 &&
		    to_sb.st_ino == from_sb.st_ino &&
		    to_sb.st_ino != 0 &&
		    !This.hard_link_files_when_possible)
			return errx(pCtx, EX_USAGE,
			            "%s and %s are the same file", *argv, to_name);
	}
	return install(&This, *argv, to_name, fset, iflags);
}

#ifdef KMK_BUILTIN_STANDALONE
int main(int argc, char **argv, char **envp)
{
	KMKBUILTINCTX Ctx = { "kmk_install", NULL };
	return kmk_builtin_install(argc, argv, envp, &Ctx);
}
#endif

static u_long
numeric_id(PINSTALLINSTANCE pThis, const char *name, const char *type)
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
		return err(pThis->pCtx, -1, "%s", name);
	if (*ep != '\0')
		return errx(pThis->pCtx, -1, "unknown %s %s", type, name);
	return (val);
}

/*
 * install --
 *	build a path name and install the file
 */
static int
install(PINSTALLINSTANCE pThis, const char *from_name, const char *to_name, u_long fset, u_int flags)
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
			return err(pThis->pCtx, EX_OSERR, "%s", from_name);
		if (!S_ISREG(from_sb.st_mode)) {
			errno = EFTYPE;
			return err(pThis->pCtx, EX_OSERR, "%s", from_name);
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
		warn(pThis->pCtx, "%s", to_name);
		return EX_OK;
	}

	/* Only copy safe if the target exists. */
	tempcopy = pThis->safecopy && target;

	/* Try hard linking if wanted and possible. */
	if (pThis->hard_link_files_when_possible)
	{
#ifdef KBUILD_OS_OS2
		const char *why_not = "not supported on OS/2";
#else
		const char *why_not = NULL;
		if (devnull) {
			why_not = "/dev/null";
		} else if (pThis->dostrip) {
			why_not = "strip (-s)";
		} else if (pThis->docompare) {
			why_not = "compare (-C)";
		} else if (pThis->dobackup) {
			why_not = "backup (-b/-B)";
		} else if (pThis->safecopy) {
			why_not = "safe copy (-S)";
		} else if (lstat(from_name, &temp_sb)) {
			why_not = "lstat on source failed";
		} else if (S_ISLNK(temp_sb.st_mode)) {
			why_not = "symlink";
		} else if (!S_ISREG(temp_sb.st_mode)) {
			why_not = "not regular file";
# if defined(KBUILD_OS_WINDOWS) || defined(KBUILD_OS_OS2)
		} else if ((pThis->mode & S_IWUSR) != (from_sb.st_mode & S_IWUSR)) {
# else
		} else if (pThis->mode != (from_sb.st_mode & ALLPERMS)) {
# endif
			kmk_builtin_ctx_printf(pThis->pCtx, 0,
				"install: warning: Not hard linking, mode differs: 0%03o, desires 0%03o\n"
				"install: src path '%s'\n"
				"install: dst path '%s'\n",
				(from_sb.st_mode & ALLPERMS), pThis->mode, from_name, to_name);
			why_not = NULL;
		} else if (pThis->uid != (uid_t)-1 && pThis->uid != from_sb.st_uid) {
			why_not = "uid mismatch";
		} else if (pThis->gid != (gid_t)-1 && pThis->gid != from_sb.st_gid) {
			why_not = "gid mismatch";
		} else if (pThis->dos2unix > 0 && needs_dos2unix_conversion(from_name)) {
			why_not = "dos2unix";
		} else if (pThis->dos2unix < 0 && needs_unix2dos_conversion(from_name)) {
			why_not = "unix2dos";
		} else {
			int rcLink = link(from_name, to_name);
			if (rcLink != 0 && errno == EEXIST) {
			    unlink(to_name);
			    rcLink = link(from_name, to_name);
			}
			if (rcLink == 0) {
			    if (pThis->verbose)
				    kmk_builtin_ctx_printf(pThis->pCtx, 0,
							   "install: %s -> %s (hardlinked)\n", from_name, to_name);
			    goto l_done;
			}
			if (pThis->verbose)
				kmk_builtin_ctx_printf(pThis->pCtx, 0, "install: hard linking '%s' to '%s' failed: %s\n",
				       to_name, from_name, strerror(errno));
			why_not = NULL;
		}
#endif
		if (pThis->verbose && why_not)
		    kmk_builtin_ctx_printf(pThis->pCtx, 0, "install: not hard linking '%s' to '%s' because: %s\n",
					   to_name, from_name, why_not);

		/* Can't hard link or we failed, continue as nothing happend. */
	}

	if (!devnull && (from_fd = open(from_name, O_RDONLY | O_BINARY | KMK_OPEN_NO_INHERIT, 0)) < 0)
		return err(pThis->pCtx, EX_OSERR, "%s", from_name);

	/* If we don't strip, we can compare first. */
	if (pThis->docompare && !pThis->dostrip && target) {
		if ((to_fd = open(to_name, O_RDONLY | O_BINARY | KMK_OPEN_NO_INHERIT, 0)) < 0) {
			rc = err(pThis->pCtx, EX_OSERR, "%s", to_name);
			goto l_done;
		}
		if (devnull)
			files_match = to_sb.st_size == 0;
		else
			files_match = !compare(from_fd, (size_t)from_sb.st_size,
					       to_fd, (size_t)to_sb.st_size);

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
				rc = err(pThis->pCtx, EX_OSERR, "%s", tempfile);
				goto l_done;
			}
		} else {
			if ((to_fd = create_newfile(pThis, to_name, target, &to_sb)) < 0) {
				rc = err(pThis->pCtx, EX_OSERR, "%s", to_name);
				goto l_done;
			}
			if (pThis->verbose)
				kmk_builtin_ctx_printf(pThis->pCtx, 0, "install: %s -> %s\n", from_name, to_name);
		}
		if (!devnull) {
			rc = copy(pThis, from_fd, from_name, &to_fd, tempcopy ? tempfile : to_name);
			if (rc)
    				goto l_done;
		}
	}

	if (pThis->dostrip) {
#if defined(__EMX__) || defined(_MSC_VER)
		/* close before we strip. */
		close(to_fd);
		to_fd = -1;
#endif
		rc = strip(pThis, tempcopy ? tempfile : to_name);
		if (rc)
			goto l_done;

		/*
		 * Re-open our fd on the target, in case we used a strip
		 * that does not work in-place -- like GNU binutils strip.
		 */
#if !defined(__EMX__) && !defined(_MSC_VER)
		close(to_fd);
#endif
		to_fd = open(tempcopy ? tempfile : to_name, O_RDONLY | O_BINARY | KMK_OPEN_NO_INHERIT, 0);
		if (to_fd < 0) {
			rc = err(pThis->pCtx, EX_OSERR, "stripping %s", to_name);
			goto l_done;
		}
	}

	/*
	 * Compare the stripped temp file with the target.
	 */
	if (pThis->docompare && pThis->dostrip && target) {
		temp_fd = to_fd;

		/* Re-open to_fd using the real target name. */
		if ((to_fd = open(to_name, O_RDONLY | O_BINARY | KMK_OPEN_NO_INHERIT, 0)) < 0) {
			rc = err(pThis->pCtx, EX_OSERR, "%s", to_name);
			goto l_done;
		}

		if (fstat(temp_fd, &temp_sb)) {
			serrno = errno;
			(void)unlink(tempfile);
			errno = serrno;
			rc = err(pThis->pCtx, EX_OSERR, "%s", tempfile);
			goto l_done;
		}

		if (compare(temp_fd, (size_t)temp_sb.st_size,
			    to_fd, (size_t)to_sb.st_size) == 0) {
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
		if (pThis->dobackup) {
			if (   (size_t)snprintf(backup, MAXPATHLEN, "%s%s", to_name, pThis->suffix)
			    != strlen(to_name) + strlen(pThis->suffix)) {
				unlink(tempfile);
				rc = errx(pThis->pCtx, EX_OSERR, "%s: backup filename too long",
				          to_name);
				goto l_done;
			}
			if (pThis->verbose)
				kmk_builtin_ctx_printf(pThis->pCtx, 0, "install: %s -> %s\n", to_name, backup);
			if (rename(to_name, backup) < 0) {
				serrno = errno;
				unlink(tempfile);
				errno = serrno;
				rc = err(pThis->pCtx, EX_OSERR, "rename: %s to %s", to_name,
				         backup);
				goto l_done;
			}
		}
		if (pThis->verbose)
			kmk_builtin_ctx_printf(pThis->pCtx, 0, "install: %s -> %s\n", from_name, to_name);
		if (rename(tempfile, to_name) < 0) {
			serrno = errno;
			unlink(tempfile);
			errno = serrno;
			rc = err(pThis->pCtx, EX_OSERR, "rename: %s to %s", tempfile, to_name);
			goto l_done;
		}

		/* Re-open to_fd so we aren't hosed by the rename(2). */
		(void) close(to_fd);
		if ((to_fd = open(to_name, O_RDONLY | O_BINARY | KMK_OPEN_NO_INHERIT, 0)) < 0) {
			rc = err(pThis->pCtx, EX_OSERR, "%s", to_name);
			goto l_done;
		}
	}

	/*
	 * Preserve the timestamp of the source file if necessary.
	 */
	if (pThis->dopreserve && !files_match && !devnull) {
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
		rc = err(pThis->pCtx, EX_OSERR, "%s", to_name);
		goto l_done;
	}

	/*
	 * Set owner, group, mode for target; do the chown first,
	 * chown may lose the setuid bits.
	 */
#ifdef UF_IMMUTABLE
	if ((pThis->gid != (gid_t)-1 && pThis->gid != to_sb.st_gid) ||
	    (pThis->uid != (uid_t)-1 && pThis->uid != to_sb.st_uid) ||
	    (pThis->mode != (to_sb.st_mode & ALLPERMS))) {
		/* Try to turn off the immutable bits. */
		if (to_sb.st_flags & NOCHANGEBITS)
			(void)fchflags(to_fd, to_sb.st_flags & ~NOCHANGEBITS);
	}
#endif

	if ((pThis->gid != (gid_t)-1 && pThis->gid != to_sb.st_gid) ||
	    (pThis->uid != (uid_t)-1 && pThis->uid != to_sb.st_uid))
		if (fchown(to_fd, pThis->uid, pThis->gid) == -1) {
    			if (errno == EPERM && pThis->ignore_perm_errors) {
				warn(pThis->pCtx, "%s: ignoring chown uid=%d gid=%d failure",
				     to_name, (int)pThis->uid, (int)pThis->gid);
			} else {
				serrno = errno;
				(void)unlink(to_name);
				errno = serrno;
				rc = err(pThis->pCtx, EX_OSERR,"%s: chown/chgrp", to_name);
				goto l_done;
			}
		}

	if (pThis->mode != (to_sb.st_mode & ALLPERMS))
		if (fchmod(to_fd, pThis->mode)) {
			serrno = errno;
			if (serrno == EPERM && pThis->ignore_perm_errors) {
				fchmod(to_fd, pThis->mode & (ALLPERMS & ~0007000));
				errno = errno;
				warn(pThis->pCtx, "%s: ignoring chmod 0%o failure", to_name, (int)(pThis->mode & ALLPERMS));
			} else  {
				serrno = errno;
				(void)unlink(to_name);
				errno = serrno;
				rc = err(pThis->pCtx, EX_OSERR, "%s: chmod", to_name);
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
	if (   !devnull
	    && (flags & SETFLAGS || (from_sb.st_flags & ~UF_NODUMP) != to_sb.st_flags)
	    && fchflags(to_fd, flags & SETFLAGS ? fset : from_sb.st_flags & ~UF_NODUMP)) {
		if (flags & SETFLAGS) {
			if (errno == EOPNOTSUPP)
				warn(pThis->pCtx, "%s: chflags", to_name);
			else {
				serrno = errno;
				(void)unlink(to_name);
				errno = serrno;
				rc = err(pThis->pCtx, EX_OSERR, "%s: chflags", to_name);
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
	if (from_fd >= 0 && !devnull)
		(void)close(from_fd);
	return rc;
}

/*
 * compare --
 *	compare two files; non-zero means files differ
 */
static int
compare(int from_fd, size_t from_len, int to_fd, size_t to_len)
{
	char buf1[MAXBSIZE];
	char buf2[MAXBSIZE];
	int n1, n2;
	int rv;

	if (from_len != to_len)
		return 1;

	if (from_len <= MAX_CMP_SIZE) {
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
	static char s_szTemplate[] = "INS@XXXXXX";
	const char *p = last_slash(path);
	if (p) {
		size_t cchDir = ++p - path;
		if (cchDir + sizeof(s_szTemplate) <= tsize) {
			memcpy(temp, path, cchDir);
			memcpy(&temp[cchDir], s_szTemplate, sizeof(s_szTemplate));
		} else
			return EOVERFLOW;
	} else if (tsize >= sizeof(s_szTemplate))
		memcpy(temp, s_szTemplate, sizeof(s_szTemplate));
	else
		return EOVERFLOW;

	return (mkstemp(temp));
}

/*
 * create_newfile --
 *	create a new file, overwriting an existing one if necessary
 */
int
create_newfile(PINSTALLINSTANCE pThis, const char *path, int target, struct stat *sbp)
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

		if (pThis->dobackup) {
			if (   (size_t)snprintf(backup, MAXPATHLEN, "%s%s", path, pThis->suffix)
			    != strlen(path) + strlen(pThis->suffix)) {
				errx(pThis->pCtx, EX_OSERR, "%s: backup filename too long", path);
				errno = ENAMETOOLONG;
				return -1;
			}
			(void)snprintf(backup, MAXPATHLEN, "%s%s", path, pThis->suffix);
			if (pThis->verbose)
				kmk_builtin_ctx_printf(pThis->pCtx, 0, "install: %s -> %s\n", path, backup);
			if (rename(path, backup) < 0) {
				err(pThis->pCtx, EX_OSERR, "rename: %s to %s", path, backup);
				return -1;
			}
		} else
			if (unlink(path) < 0)
				saved_errno = errno;
	}

	newfd = open(path, O_CREAT | O_RDWR | O_TRUNC | O_BINARY | KMK_OPEN_NO_INHERIT, S_IRUSR | S_IWUSR);
	if (newfd < 0 && saved_errno != 0)
		errno = saved_errno;
	return newfd;
}

/*
 * Write error handler.
 */
static int write_error(PINSTALLINSTANCE pThis, int *ptr_to_fd, const char *to_name, int nw)
{
    int serrno = errno;
    (void)close(*ptr_to_fd);
    *ptr_to_fd = -1;
    (void)unlink(to_name);
    errno = nw > 0 ? EIO : serrno;
    return err(pThis->pCtx, EX_OSERR, "%s", to_name);
}

/*
 * Read error handler.
 */
static int read_error(PINSTALLINSTANCE pThis, const char *from_name, int *ptr_to_fd, const char *to_name)
{
    int serrno = errno;
    (void)close(*ptr_to_fd);
    *ptr_to_fd = -1;
    (void)unlink(to_name);
    errno = serrno;
    return err(pThis->pCtx, EX_OSERR, "%s", from_name);
}

/*
 * copy --
 *	copy from one file to another
 */
static int
copy(PINSTALLINSTANCE pThis, int from_fd, const char *from_name, int *ptr_to_fd, const char *to_name)
{
	KBOOL fPendingCr = K_FALSE;
	KSIZE cchDst;
	int nr, nw;
	char buf[MAXBSIZE];
	int to_fd = *ptr_to_fd;

	/* Rewind file descriptors. */
	if (lseek(from_fd, (off_t)0, SEEK_SET) == (off_t)-1)
		return err(pThis->pCtx, EX_OSERR, "lseek: %s", from_name);
	if (lseek(to_fd, (off_t)0, SEEK_SET) == (off_t)-1)
		return err(pThis->pCtx, EX_OSERR, "lseek: %s", to_name);

	if (pThis->dos2unix == 0) {
		/*
		 * Copy bytes, no conversion.
		 */
		while ((nr = read(from_fd, buf, sizeof(buf))) > 0)
			if ((nw = write(to_fd, buf, nr)) != nr)
				return write_error(pThis, ptr_to_fd, to_name, nw);
	} else if (pThis->dos2unix > 0) {
		/*
		 * CRLF -> LF is a reduction, so we can work with full buffers.
		 */
		while ((nr = read(from_fd, buf, sizeof(buf))) > 0) {
			if (   fPendingCr
				&& buf[0] != '\n'
				&& (nw = write(to_fd, "\r", 1)) != 1)
				return write_error(pThis, ptr_to_fd, to_name, nw);

			fPendingCr = dos2unix_convert_to_unix(buf, nr, buf, &cchDst);

			nw = write(to_fd, buf, cchDst);
			if (nw != (int)cchDst)
				return write_error(pThis, ptr_to_fd, to_name, nw);
		}
	} else {
		/*
		 * LF -> CRLF is an expansion, so we work with half buffers, reading
		 * into the upper half of the buffer and expanding into the full buffer.
		 * The conversion will never expand to more than the double size.
		 *
		 * Note! We do not convert valid CRLF line endings.  This gives us
		 *       valid DOS text, but no round-trip conversion.
		 */
		char * const pchSrc = &buf[sizeof(buf) / 2];
		while ((nr = read(from_fd, pchSrc, sizeof(buf) / 2)) > 0) {
			if (   fPendingCr
				&& pchSrc[0] != '\n'
				&& (nw = write(to_fd, "\r", 1))!= 1)
				return write_error(pThis, ptr_to_fd, to_name, nw);

			fPendingCr = dos2unix_convert_to_dos(pchSrc, nr, buf, &cchDst);

			nw = write(to_fd, buf, cchDst);
			if (nw != (int)cchDst)
				return write_error(pThis, ptr_to_fd, to_name, nw);
		}
	}

	/* Check for read error. */
	if (nr != 0)
		return read_error(pThis, from_name, ptr_to_fd, to_name);

	/* When converting, we might have a pending final CR to write. */
	if (   fPendingCr
	    && (nw = write(to_fd, "\r", 1))!= 1)
		return write_error(pThis, ptr_to_fd, to_name, nw);

    return EX_OK;
}

/*
 * strip --
 *	use strip(1) to strip the target file
 */
static int
strip(PINSTALLINSTANCE pThis, const char *to_name)
{
#if defined(__EMX__) || defined(_MSC_VER)
	const char *stripbin = getenv("STRIPBIN");
	if (stripbin == NULL)
		stripbin = "strip";
	(void)pThis;
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
		return err(pThis->pCtx, EX_TEMPFAIL, "fork");
	case 0:
		stripbin = getenv("STRIPBIN");
		if (stripbin == NULL)
			stripbin = "strip";
		execlp(stripbin, stripbin, to_name, (char *)NULL);
		err(pThis->pCtx, EX_OSERR, "exec(%s)", stripbin);
                exit(EX_OSERR);
	default:
		if (waitpid(pid, &status, 0) == -1 || status) {
			serrno = errno;
			(void)unlink(to_name);
                        errno = serrno;
			return err(pThis->pCtx, EX_SOFTWARE, "waitpid");
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
install_dir(PINSTALLINSTANCE pThis, char *path)
{
	char *p;
	struct stat sb;
	int ch;

	for (p = path;; ++p)
		if (   !*p
		    || (   p != path
			&& IS_SLASH(*p)
#if defined(_MSC_VER) /* stat("C:") fails (VC++ v10). Just skip it since it's unnecessary. */
		        && (p - path != 2 || p[-1] != ':')
#endif
		    )) {
			ch = *p;
			*p = '\0';
			if (stat(path, &sb)) {
				if (errno != ENOENT || mkdir(path, 0755) < 0) {
					return err(pThis->pCtx, EX_OSERR, "mkdir %s", path);
					/* NOTREACHED */
				} else if (pThis->verbose)
					kmk_builtin_ctx_printf(pThis->pCtx, 0, "install: mkdir %s\n", path);
			} else if (!S_ISDIR(sb.st_mode))
				return errx(pThis->pCtx, EX_OSERR, "%s exists but is not a directory", path);
			if (!(*p = ch))
				break;
 		}

	if ((pThis->gid != (gid_t)-1 || pThis->uid != (uid_t)-1) && chown(path, pThis->uid, pThis->gid))
		warn(pThis->pCtx, "chown %u:%u %s", pThis->uid, pThis->gid, path);
	if (chmod(path, pThis->mode))
		warn(pThis->pCtx, "chmod %o %s", pThis->mode, path);
	return EX_OK;
}

/*
 * usage --
 *	print a usage message and die
 */
static int
usage(PKMKBUILTINCTX pCtx, int fIsErr)
{
	kmk_builtin_ctx_printf(pCtx, fIsErr,
"usage: %s [-bCcpSsv] [--[no-]hard-link-files-when-possible]\n"
"            [--[no-]ignore-perm-errors] [-B suffix] [-f flags] [-g group]\n"
"            [-m mode] [-o owner] [--dos2unix|--unix2dos] file1 file2\n"
"   or: %s [-bCcpSsv] [--[no-]ignore-perm-errors] [-B suffix] [-f flags]\n"
"            [-g group] [-m mode] [-o owner] file1 ... fileN directory\n"
"   or: %s -d [-v] [-g group] [-m mode] [-o owner] directory ...\n"
"   or: %s --help\n"
"   or: %s --version\n",
		pCtx->pszProgName, pCtx->pszProgName, pCtx->pszProgName,
		pCtx->pszProgName, pCtx->pszProgName);
	return EX_USAGE;
}

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

/**
 * Checks if @a pszFilename actually needs dos2unix conversion.
 *
 * @returns boolean.
 * @param	pszFilename		The name of the file to check.
 */
static KBOOL needs_dos2unix_conversion(const char *pszFilename)
{
	KU32 fStyle = 0;
	int iErr = dos2unix_analyze_file(pszFilename, &fStyle, NULL, NULL);
	return iErr != 0
		|| (fStyle & (DOS2UNIX_STYLE_MASK | DOS2UNIX_F_BINARY)) != DOS2UNIX_STYLE_UNIX;
}

/**
 * Checks if @a pszFilename actually needs unix2dos conversion.
 *
 * @returns boolean.
 * @param	pszFilename		The name of the file to check.
 */
static KBOOL needs_unix2dos_conversion(const char *pszFilename)
{
	KU32 fStyle = 0;
	int iErr = dos2unix_analyze_file(pszFilename, &fStyle, NULL, NULL);
	return iErr != 0
		|| (fStyle & (DOS2UNIX_STYLE_MASK | DOS2UNIX_F_BINARY)) != DOS2UNIX_STYLE_DOS;
}

