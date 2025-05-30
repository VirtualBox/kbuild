/*-
 * Copyright (c) 1990, 1993, 1994
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
static const char copyright[] =
"@(#) Copyright (c) 1990, 1993, 1994\n\
	The Regents of the University of California.  All rights reserved.\n";
#endif /* not lint */

#ifndef lint
static char sccsid[] = "@(#)rm.c	8.5 (Berkeley) 4/18/94";
#endif /* not lint */
#include <sys/cdefs.h>
/*__FBSDID("$FreeBSD: src/bin/rm/rm.c,v 1.47 2004/04/06 20:06:50 markm Exp $");*/
#endif


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define FAKES_NO_GETOPT_H /* bird */
#include "config.h"
#include <sys/stat.h>
#if !defined(_MSC_VER) && !defined(__HAIKU__)
# include <sys/param.h>
# ifndef __gnu_hurd__
#  include <sys/mount.h>
# endif
#endif

#include "err.h"
#include <errno.h>
#include <fcntl.h>
#include "fts.h"
#include <grp.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef __HAIKU__
# include <sysexits.h>
#endif
#include <unistd.h>
#include <ctype.h>
#include "getopt_r.h"
#ifdef __HAIKU__
# include "haikufakes.h"
#endif
#ifdef __NetBSD__
# include <util.h>
# define fflagstostr(flags)    	flags_to_string(flags, "")
#endif
#ifdef KBUILD_OS_WINDOWS
# ifdef _MSC_VER
#  include "mscfakes.h"
# endif
# include "nt/ntunlink.h"
  /* Use the special unlink implementation to do rmdir too. */
# undef  rmdir
# define rmdir(a_pszPath) 	birdUnlinkForced(a_pszPath)
#endif
#if defined(__OS2__) || defined(_MSC_VER)
# include <direct.h>
# include <limits.h>
#endif
#include "kmkbuiltin.h"
#include "kbuild_protection.h"
#include "k/kDefs.h"	/* for K_OS */


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#if defined(__EMX__) || defined(KBUILD_OS_WINDOWS)
# define IS_SLASH(ch)   ( (ch) == '/' || (ch) == '\\' )
# define HAVE_DOS_PATHS 1
# define DEFAULT_PROTECTION_DEPTH 1
#else
# define IS_SLASH(ch)   ( (ch) == '/' )
# undef HAVE_DOS_PATHS
# define DEFAULT_PROTECTION_DEPTH 2
#endif

#ifdef __EMX__
#undef S_IFWHT
#undef S_ISWHT
#endif
#ifndef S_IFWHT
#define S_IFWHT 0
#define S_ISWHT(s) 0
#define undelete(s) (-1)
#endif

#if 1
#define CUR_LINE_H2(x)  "[line " #x "]"
#define CUR_LINE_H1(x)  CUR_LINE_H2(x)
#define CUR_LINE()      CUR_LINE_H1(__LINE__)
#else
# define CUR_LINE()
#endif


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
typedef struct RMINSTANCE
{
    PKMKBUILTINCTX pCtx;
    int dflag, eval, fflag, iflag, Pflag, vflag, Wflag, stdin_ok;
#ifdef KBUILD_OS_WINDOWS
    int fUseNtDeleteFile;
#endif
    uid_t uid;
    KBUILDPROTECTION g_ProtData;
} RMINSTANCE;
typedef RMINSTANCE *PRMINSTANCE;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
static struct option long_options[] =
{
    { "help",   					no_argument, 0, 261 },
    { "version",   					no_argument, 0, 262 },
    { "disable-protection",				no_argument, 0, 263 },
    { "enable-protection",				no_argument, 0, 264 },
    { "enable-full-protection",				no_argument, 0, 265 },
    { "disable-full-protection",			no_argument, 0, 266 },
    { "protection-depth",				required_argument, 0, 267 },
#ifdef KBUILD_OS_WINDOWS
    { "nt-delete-file",					no_argument, 0, 268 },
#endif
    { 0, 0,	0, 0 },
};


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
extern void bsd_strmode(mode_t mode, char *p); /* strmode.c */

static int	check(PRMINSTANCE, char *, char *, struct stat *);
static void	checkdot(PRMINSTANCE, char **);
static int	rm_file(PRMINSTANCE, char **);
static int	rm_overwrite(PRMINSTANCE, char *, struct stat *);
static int	rm_tree(PRMINSTANCE, char **);
static int	usage(PKMKBUILTINCTX, int);



/*
 * rm --
 *	This rm is different from historic rm's, but is expected to match
 *	POSIX 1003.2 behavior.  The most visible difference is that -f
 *	has two specific effects now, ignore non-existent files and force
 * 	file removal.
 */
int
kmk_builtin_rm(int argc, char *argv[], char **envp, PKMKBUILTINCTX pCtx)
{
	RMINSTANCE This;
	struct getopt_state_r gos;
	int ch, rflag;

	/* Init global instance data */
	This.pCtx = pCtx;
	This.dflag = 0;
	This.eval = 0;
	This.fflag = 0;
	This.iflag = 0;
	This.Pflag = 0;
	This.vflag = 0;
	This.Wflag = 0;
	This.stdin_ok = 0;
#ifdef KBUILD_OS_WINDOWS
	This.fUseNtDeleteFile = 0;
#endif
	This.uid = 0;
	kBuildProtectionInit(&This.g_ProtData, pCtx);

	rflag = 0;
	getopt_initialize_r(&gos, argc, argv, "dfiPRvW", long_options, envp, pCtx);
	while ((ch = getopt_long_r(&gos, NULL)) != -1)
		switch (ch) {
		case 'd':
			This.dflag = 1;
			break;
		case 'f':
			This.fflag = 1;
			This.iflag = 0;
			break;
		case 'i':
			This.fflag = 0;
			This.iflag = 1;
			break;
		case 'P':
			This.Pflag = 1;
			break;
		case 'R':
#if 0
		case 'r':			/* Compatibility. */
#endif
			rflag = 1;
			break;
		case 'v':
			This.vflag = 1;
			break;
#ifdef FTS_WHITEOUT
		case 'W':
			This.Wflag = 1;
			break;
#endif
		case 261:
			kBuildProtectionTerm(&This.g_ProtData);
			usage(pCtx, 0);
			return 0;
		case 262:
			kBuildProtectionTerm(&This.g_ProtData);
			return kbuild_version(argv[0]);
		case 263:
			kBuildProtectionDisable(&This.g_ProtData, KBUILDPROTECTIONTYPE_RECURSIVE);
			break;
		case 264:
			kBuildProtectionEnable(&This.g_ProtData, KBUILDPROTECTIONTYPE_RECURSIVE);
			break;
		case 265:
			kBuildProtectionEnable(&This.g_ProtData, KBUILDPROTECTIONTYPE_FULL);
			break;
		case 266:
			kBuildProtectionDisable(&This.g_ProtData, KBUILDPROTECTIONTYPE_FULL);
			break;
		case 267:
			if (kBuildProtectionSetDepth(&This.g_ProtData, gos.optarg)) {
			    kBuildProtectionTerm(&This.g_ProtData);
			    return 1;
			}
			break;
#ifdef KBUILD_OS_WINDOWS
		case 268:
			This.fUseNtDeleteFile = 1;
			break;
#endif
		case '?':
		default:
			kBuildProtectionTerm(&This.g_ProtData);
			return usage(pCtx, 1);
		}
	argc -= gos.optind;
	argv += gos.optind;

	if (argc < 1) {
		kBuildProtectionTerm(&This.g_ProtData);
		if (This.fflag)
			return (0);
		return usage(pCtx, 1);
	}

	if (!kBuildProtectionScanEnv(&This.g_ProtData, envp, "KMK_RM_")) {
		checkdot(&This, argv);
		This.uid = geteuid();

		if (*argv) {
			This.stdin_ok = isatty(STDIN_FILENO);
			if (rflag)
				This.eval |= rm_tree(&This, argv);
			else
				This.eval |= rm_file(&This, argv);
		}
	} else {
		This.eval = 1;
	}

	kBuildProtectionTerm(&This.g_ProtData);
	return This.eval;
}

#ifdef KMK_BUILTIN_STANDALONE
int main(int argc, char **argv, char **envp)
{
	KMKBUILTINCTX Ctx = { "kmk_rm", NULL };
	return kmk_builtin_rm(argc, argv, envp, &Ctx);
}
#endif

static int
rm_tree(PRMINSTANCE pThis, char **argv)
{
	FTS *fts;
	FTSENT *p;
	int needstat;
	int flags;
	int rval;

	/*
	 * Check up front before anything is deleted. This will not catch
	 * everything, but we'll check the individual items later.
	 */
	int i;
	for (i = 0; argv[i]; i++) {
		if (kBuildProtectionEnforce(&pThis->g_ProtData, KBUILDPROTECTIONTYPE_RECURSIVE, argv[i])) {
			return 1;
		}
	}

	/*
	 * Remove a file hierarchy.  If forcing removal (-f), or interactive
	 * (-i) or can't ask anyway (stdin_ok), don't stat the file.
	 */
	needstat = !pThis->uid || (!pThis->fflag && !pThis->iflag && pThis->stdin_ok);

	/*
	 * If the -i option is specified, the user can skip on the pre-order
	 * visit.  The fts_number field flags skipped directories.
	 */
#define	SKIPPED	1

	flags = FTS_PHYSICAL;
#ifndef KMK_BUILTIN_STANDALONE
	flags |= FTS_NOCHDIR; /* Must not change the directory from inside kmk! */
#endif
	if (!needstat)
		flags |= FTS_NOSTAT;
#ifdef FTS_WHITEOUT
	if (pThis->Wflag)
		flags |= FTS_WHITEOUT;
#endif
	if (!(fts = fts_open(argv, flags, NULL))) {
		return err(pThis->pCtx, 1, "fts_open");
	}
	while ((p = fts_read(fts)) != NULL) {
		const char *operation = "chflags";
		switch (p->fts_info) {
		case FTS_DNR:
			if (!pThis->fflag || p->fts_errno != ENOENT)
				pThis->eval = errx(pThis->pCtx, 1, "fts: %s: %s" CUR_LINE() "\n",
						   p->fts_path, strerror(p->fts_errno));
			continue;
		case FTS_ERR:
			fts_close(fts);
			return errx(pThis->pCtx, 1, "fts: %s: %s " CUR_LINE(), p->fts_path, strerror(p->fts_errno));
		case FTS_NS:
			/*
			 * Assume that since fts_read() couldn't stat the
			 * file, it can't be unlinked.
			 */
			if (!needstat)
				break;
			if (!pThis->fflag || p->fts_errno != ENOENT)
				pThis->eval = errx(pThis->pCtx, 1, "fts: %s: %s " CUR_LINE() "\n",
						   p->fts_path, strerror(p->fts_errno));
			continue;
		case FTS_D:
			/* Pre-order: give user chance to skip. */
			if (!pThis->fflag && !check(pThis, p->fts_path, p->fts_accpath, p->fts_statp)) {
				(void)fts_set(fts, p, FTS_SKIP);
				p->fts_number = SKIPPED;
			}
#ifdef UF_APPEND
			else if (!pThis->uid &&
				 (p->fts_statp->st_flags & (UF_APPEND|UF_IMMUTABLE)) &&
				 !(p->fts_statp->st_flags & (SF_APPEND|SF_IMMUTABLE)) &&
				 chflags(p->fts_accpath,
					 p->fts_statp->st_flags &= ~(UF_APPEND|UF_IMMUTABLE)) < 0)
				goto err;
#endif
			continue;
		case FTS_DP:
			/* Post-order: see if user skipped. */
			if (p->fts_number == SKIPPED)
				continue;
			break;
		default:
			if (!pThis->fflag && !check(pThis, p->fts_path, p->fts_accpath, p->fts_statp))
				continue;
		}

		/*
		 * Protect against deleting root files and directories.
		 */
		if (kBuildProtectionEnforce(&pThis->g_ProtData, KBUILDPROTECTIONTYPE_RECURSIVE, p->fts_accpath)) {
			fts_close(fts);
			return 1;
		}

		rval = 0;
#ifdef UF_APPEND
		if (!pThis->uid &&
		    (p->fts_statp->st_flags & (UF_APPEND|UF_IMMUTABLE)) &&
		    !(p->fts_statp->st_flags & (SF_APPEND|SF_IMMUTABLE)))
			rval = chflags(p->fts_accpath,
				       p->fts_statp->st_flags &= ~(UF_APPEND|UF_IMMUTABLE));
#endif
		if (rval == 0) {
			/*
			 * If we can't read or search the directory, may still be
			 * able to remove it.  Don't print out the un{read,search}able
			 * message unless the remove fails.
			 */
			switch (p->fts_info) {
			case FTS_DP:
			case FTS_DNR:
#ifdef KBUILD_OS_WINDOWS
				if (p->fts_parent->fts_dirfd != NT_FTS_INVALID_HANDLE_VALUE) {
				    rval = birdUnlinkForcedEx(p->fts_parent->fts_dirfd, p->fts_name);
				} else {
				    rval = birdUnlinkForced(p->fts_accpath);
				}
#else
				rval = rmdir(p->fts_accpath);
#endif
				if (rval == 0 || (pThis->fflag && errno == ENOENT)) {
					if (rval == 0 && pThis->vflag)
						kmk_builtin_ctx_printf(pThis->pCtx, 0, "%s\n", p->fts_path);
#if defined(KMK) && defined(KBUILD_OS_WINDOWS)
					if (rval == 0) {
					    extern int dir_cache_deleted_directory(const char *pszDir);
					    dir_cache_deleted_directory(p->fts_accpath);
					}
#endif
					continue;
				}
				operation = "rmdir";
				break;

#ifdef FTS_W
			case FTS_W:
				rval = undelete(p->fts_accpath);
				if (rval == 0 && (pThis->fflag && errno == ENOENT)) {
					if (pThis->vflag)
						kmk_builtin_ctx_printf(pThis->pCtx, 0, "%s\n", p->fts_path);
					continue;
				}
				operation = "undelete";
				break;
#endif

			case FTS_NS:
				/*
				 * Assume that since fts_read() couldn't stat
				 * the file, it can't be unlinked.
				 */
				if (pThis->fflag)
					continue;
				/* FALLTHROUGH */
			default:
				if (pThis->Pflag)
					if (!rm_overwrite(pThis, p->fts_accpath, NULL))
						continue;
#ifdef KBUILD_OS_WINDOWS
				if (p->fts_parent->fts_dirfd != NT_FTS_INVALID_HANDLE_VALUE) {
					if (p->fts_info != FTS_SL && p->fts_info != FTS_SLNONE) {
						rval = birdUnlinkForcedFastEx(p->fts_parent->fts_dirfd, p->fts_name);
					} else { /* NtDeleteFile doesn't work on directory links, so slow symlink deletion: */
						rval = birdUnlinkForcedEx(p->fts_parent->fts_dirfd, p->fts_name);
					}
				} else {
					if (p->fts_info != FTS_SL && p->fts_info != FTS_SLNONE) {
						rval = birdUnlinkForcedFast(p->fts_accpath);
					} else { /* NtDeleteFile doesn't work on directory links, so slow symlink deletion: */
						rval = birdUnlinkForced(p->fts_accpath);
					}
				}
#else
				rval = unlink(p->fts_accpath);
#endif

				if (rval == 0 || (pThis->fflag && errno == ENOENT)) {
					if (rval == 0 && pThis->vflag)
						kmk_builtin_ctx_printf(pThis->pCtx, 0, "%s\n", p->fts_path);
					continue;
				}
				operation = "unlink";
				break;
			}
		}
#ifdef UF_APPEND
err:
#endif
		pThis->eval = errx(pThis->pCtx, 1, "%s: %s failed: %s " CUR_LINE() "\n", p->fts_path, operation, strerror(errno));
	}
	if (errno) {
		pThis->eval = errx(pThis->pCtx, 1, "fts_read: %s " CUR_LINE() "\n", strerror(errno));
	}
	fts_close(fts);
	return pThis->eval;
}

static int
rm_file(PRMINSTANCE pThis, char **argv)
{
	struct stat sb;
	int rval;
	char *f;

	/*
	 * Check up front before anything is deleted.
	 */
	int i;
	for (i = 0; argv[i]; i++) {
		if (kBuildProtectionEnforce(&pThis->g_ProtData, KBUILDPROTECTIONTYPE_FULL, argv[i]))
			return 1;
	}

	/*
	 * Remove a file.  POSIX 1003.2 states that, by default, attempting
	 * to remove a directory is an error, so must always stat the file.
	 */
	while ((f = *argv++) != NULL) {
		const char *operation = "?";
		/* Assume if can't stat the file, can't unlink it. */
		if (lstat(f, &sb)) {
#ifdef FTS_WHITEOUT
			if (pThis->Wflag) {
				sb.st_mode = S_IFWHT|S_IWUSR|S_IRUSR;
			} else {
#else
			{
#endif
				if (!pThis->fflag || errno != ENOENT)
					pThis->eval = errx(pThis->pCtx, 1, "%s: lstat failed: %s " CUR_LINE() "\n",
							   f, strerror(errno));
				continue;
			}
#ifdef FTS_WHITEOUT
		} else if (pThis->Wflag) {
			errx(pThis->pCtx, 1, "%s: %s\n", f, strerror(EEXIST));
			pThis->eval = 1;
			continue;
#endif
		}

		if (S_ISDIR(sb.st_mode) && !pThis->dflag) {
			pThis->eval = errx(pThis->pCtx, 1, "%s: is a directory\n", f);
			continue;
		}
		if (!pThis->fflag && !S_ISWHT(sb.st_mode) && !check(pThis, f, f, &sb))
			continue;
		rval = 0;
#ifdef UF_APPEND
		if (!pThis->uid &&
		    (sb.st_flags & (UF_APPEND|UF_IMMUTABLE)) &&
		    !(sb.st_flags & (SF_APPEND|SF_IMMUTABLE)))
			rval = chflags(f, sb.st_flags & ~(UF_APPEND|UF_IMMUTABLE));
#endif
		if (rval == 0) {
			if (S_ISWHT(sb.st_mode)) {
				rval = undelete(f);
				operation = "undelete";
			} else if (S_ISDIR(sb.st_mode)) {
				rval = rmdir(f);
				operation = "rmdir";
			} else {
				if (pThis->Pflag)
					if (!rm_overwrite(pThis, f, &sb))
						continue;
#ifndef KBUILD_OS_WINDOWS
				rval = unlink(f);
				operation = "unlink";
#else
				if (pThis->fUseNtDeleteFile) {
					rval = birdUnlinkForcedFast(f);
					operation = "NtDeleteFile";
				} else {
					rval = birdUnlinkForced(f);
					operation = "unlink";
				}
#endif
			}
		}
		if (rval && (!pThis->fflag || errno != ENOENT))
			pThis->eval = errx(pThis->pCtx, 1, "%s: %s failed: %s" CUR_LINE() "\n", f, operation, strerror(errno));
		if (pThis->vflag && rval == 0)
			kmk_builtin_ctx_printf(pThis->pCtx, 0, "%s\n", f);
	}
	return pThis->eval;
}

/*
 * rm_overwrite --
 *	Overwrite the file 3 times with varying bit patterns.
 *
 * XXX
 * This is a cheap way to *really* delete files.  Note that only regular
 * files are deleted, directories (and therefore names) will remain.
 * Also, this assumes a fixed-block file system (like FFS, or a V7 or a
 * System V file system).  In a logging file system, you'll have to have
 * kernel support.
 */
static int
rm_overwrite(PRMINSTANCE pThis, char *file, struct stat *sbp)
{
	struct stat sb;
#ifdef HAVE_FSTATFS
	struct statfs fsb;
#endif
	off_t len;
	int bsize, fd, wlen;
	char *buf = NULL;
	const char *operation = "lstat";
	int error;

	fd = -1;
	if (sbp == NULL) {
		if (lstat(file, &sb))
			goto err;
		sbp = &sb;
	}
	if (!S_ISREG(sbp->st_mode))
		return (1);
	operation = "open";
	if ((fd = open(file, O_WRONLY | KMK_OPEN_NO_INHERIT, 0)) == -1)
		goto err;
#ifdef HAVE_FSTATFS
	if (fstatfs(fd, &fsb) == -1)
		goto err;
	bsize = MAX(fsb.f_iosize, 1024);
#elif defined(HAVE_ST_BLKSIZE)
	bsize = MAX(sb.st_blksize, 1024);
#else
	bsize = 1024;
#endif
	if ((buf = malloc(bsize)) == NULL) {
		err(pThis->pCtx, 1, "%s: malloc", file);
		close(fd);
		return 1;
	}

#define	PASS(byte) {							\
	operation = "write";    					\
	memset(buf, byte, bsize);					\
	for (len = sbp->st_size; len > 0; len -= wlen) {		\
		wlen = len < bsize ? len : bsize;			\
		if (write(fd, buf, wlen) != wlen)			\
			goto err;					\
	}								\
}
	PASS(0xff);
	operation = "fsync/lseek";
	if (fsync(fd) || lseek(fd, (off_t)0, SEEK_SET))
		goto err;
	PASS(0x00);
	operation = "fsync/lseek";
	if (fsync(fd) || lseek(fd, (off_t)0, SEEK_SET))
		goto err;
	PASS(0xff);
	if (!fsync(fd) && !close(fd)) {
		free(buf);
		return (1);
	}
	operation = "fsync/close";

err:	pThis->eval = 1;
	error = errno;
	if (buf)
		free(buf);
	if (fd != -1)
		close(fd);
	errx(pThis->pCtx, 1, "%s: %s: %s: %s" CUR_LINE() "\n", operation, pThis->pCtx->pszProgName, file, strerror(error));
	return (0);
}


static int
check(PRMINSTANCE pThis, char *path, char *name, struct stat *sp)
{
	int ch, first;
	char modep[15], *flagsp;

	/* Check -i first. */
	if (pThis->iflag)
		(void)fprintf(stderr, "%s: remove %s? ", pThis->pCtx->pszProgName, path);
	else {
		/*
		 * If it's not a symbolic link and it's unwritable and we're
		 * talking to a terminal, ask.  Symbolic links are excluded
		 * because their permissions are meaningless.  Check stdin_ok
		 * first because we may not have stat'ed the file.
		 * Also skip this check if the -P option was specified because
		 * we will not be able to overwrite file contents and will
		 * barf later.
		 */
		if (!pThis->stdin_ok || S_ISLNK(sp->st_mode) || pThis->Pflag ||
		    (!access(name, W_OK) &&
#ifdef SF_APPEND
		    !(sp->st_flags & (SF_APPEND|SF_IMMUTABLE)) &&
		    (!(sp->st_flags & (UF_APPEND|UF_IMMUTABLE)) || !pThis->uid))
#else
                    1)
#endif
                    )
			return (1);
		bsd_strmode(sp->st_mode, modep);
#if defined(SF_APPEND) && K_OS != K_OS_GNU_KFBSD && K_OS != K_OS_GNU_HURD
		if ((flagsp = fflagstostr(sp->st_flags)) == NULL) {
			err(pThis->pCtx, 1, "fflagstostr");
			flagsp = "<bad-fflagstostr>";
		}
		(void)fprintf(stderr, "override %s%s%s/%s %s%sfor %s? ",
		              modep + 1, modep[9] == ' ' ? "" : " ",
		              user_from_uid(sp->st_uid, 0),
		              group_from_gid(sp->st_gid, 0),
		              *flagsp ? flagsp : "", *flagsp ? " " : "",
		              path);
		free(flagsp);
#else
		(void)flagsp;
		(void)fprintf(stderr, "override %s%s %d/%d for %s? ",
		              modep + 1, modep[9] == ' ' ? "" : " ",
		              sp->st_uid, sp->st_gid, path);
#endif
	}
	(void)fflush(stderr);

	first = ch = getchar();
	while (ch != '\n' && ch != EOF)
		ch = getchar();
	return (first == 'y' || first == 'Y');
}

#define ISDOT(a)	((a)[0] == '.' && (!(a)[1] || ((a)[1] == '.' && !(a)[2])))
static void
checkdot(PRMINSTANCE pThis, char **argv)
{
	char *p, **save, **t;
	int complained;

	complained = 0;
	for (t = argv; *t;) {
#ifdef HAVE_DOS_PATHS
		const char *tmp = p = *t;
		while (*tmp) {
			switch (*tmp) {
			case '/':
			case '\\':
			case ':':
				p = (char *)tmp + 1;
				break;
			}
			tmp++;
		}
#else
		if ((p = strrchr(*t, '/')) != NULL)
			++p;
		else
			p = *t;
#endif
		if (ISDOT(p)) {
			if (!complained++)
				warnx(pThis->pCtx, "\".\" and \"..\" may not be removed\n");
			pThis->eval = 1;
			for (save = t; (t[0] = t[1]) != NULL; ++t)
				continue;
			t = save;
		} else
			++t;
	}
}

static int
usage(PKMKBUILTINCTX pCtx, int fIsErr)
{
	kmk_builtin_ctx_printf(pCtx, fIsErr,
	                       "usage: %s [options] file ...\n"
	                       "   or: %s --help\n"
	                       "   or: %s --version\n"
	                       "\n"
	                       "Options:\n"
	                       "   -f\n"
	                       "       Attempt to remove files without prompting, regardless of the file\n"
	                       "       permission. Ignore non-existing files. Overrides previous -i's.\n"
	                       "   -i\n"
	                       "       Prompt for each file. Always.\n"
	                       "   -d\n"
	                       "       Attempt to remove directories as well as other kinds of files.\n"
	                       "   -P\n"
	                       "       Overwrite regular files before deleting; three passes: ff,0,ff\n"
	                       "   -R\n"
	                       "       Attempt to remove the file hierachy rooted in each file argument.\n"
	                       "       This option implies -d and file protection.\n"
	                       "   -v\n"
	                       "       Be verbose, show files as they are removed.\n"
	                       "   -W\n"
	                       "       Undelete white-out files.\n"
	                       "   --disable-protection\n"
	                       "       Will disable the protection file protection applied with -R.\n"
	                       "   --enable-protection\n"
	                       "       Will enable the protection file protection applied with -R.\n"
	                       "   --enable-full-protection\n"
	                       "       Will enable the protection file protection for all operations.\n"
	                       "   --disable-full-protection\n"
	                       "       Will disable the protection file protection for all operations.\n"
	                       "   --protection-depth\n"
	                       "       Number or path indicating the file protection depth. Default: %d\n"
	                       "\n"
	                       "Environment:\n"
	                       "    KMK_RM_DISABLE_PROTECTION\n"
	                       "       Same as --disable-protection. Overrides command line.\n"
	                       "    KMK_RM_ENABLE_PROTECTION\n"
	                       "       Same as --enable-protection. Overrides everyone else.\n"
	                       "    KMK_RM_ENABLE_FULL_PROTECTION\n"
	                       "       Same as --enable-full-protection. Overrides everyone else.\n"
	                       "    KMK_RM_DISABLE_FULL_PROTECTION\n"
	                       "       Same as --disable-full-protection. Overrides command line.\n"
	                       "    KMK_RM_PROTECTION_DEPTH\n"
	                       "       Same as --protection-depth. Overrides command line.\n"
	                       "\n"
	                       "The file protection of the top %d layers of the file hierarchy is there\n"
	                       "to try prevent makefiles from doing bad things to your system. This\n"
	                       "protection is not bulletproof, but should help prevent you from shooting\n"
	                       "yourself in the foot.\n"
	                       ,
	                       pCtx->pszProgName, pCtx->pszProgName, pCtx->pszProgName,
	                       kBuildProtectionDefaultDepth(), kBuildProtectionDefaultDepth());
	return EX_USAGE;
}

