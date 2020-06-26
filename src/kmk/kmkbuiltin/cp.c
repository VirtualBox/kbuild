/*
 * Copyright (c) 1988, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * David Hitz of Auspex Systems Inc.
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
"@(#) Copyright (c) 1988, 1993, 1994\n\
	The Regents of the University of California.  All rights reserved.\n";
#endif /* not lint */

#ifndef lint
static char sccsid[] = "@(#)cp.c	8.2 (Berkeley) 4/1/94";
#endif /* not lint */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD: src/bin/cp/cp.c,v 1.50 2004/04/06 20:06:44 markm Exp $");
#endif

/*
 * Cp copies source files to target files.
 *
 * The global PATH_T structure "to" always contains the path to the
 * current target file.  Since fts(3) does not change directories,
 * this path can be either absolute or dot-relative.
 *
 * The basic algorithm is to initialize "to" and use fts(3) to traverse
 * the file hierarchy rooted in the argument list.  A trivial case is the
 * case of 'cp file1 file2'.  The more interesting case is the case of
 * 'cp file1 file2 ... fileN dir' where the hierarchy is traversed and the
 * path (relative to the root of the traversal) is appended to dir (stored
 * in "to") to form the final target path.
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define FAKES_NO_GETOPT_H /* bird */
#include "config.h"
#include <sys/types.h>
#include <sys/stat.h>

#include <assert.h>
#include "err.h"
#include <errno.h>
#include "fts.h"
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "getopt_r.h"
#include "k/kDefs.h"
#ifdef _MSC_VER
# include "mscfakes.h"
#endif
#include "cp_extern.h"
#include "kmkbuiltin.h"
#include "kbuild_protection.h"

#if defined(_MSC_VER) || defined(__gnu_linux__) || defined(__linux__)
extern size_t strlcpy(char *, const char *, size_t);
#endif


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#ifndef S_IFWHT
#define S_IFWHT 0
#define S_ISWHT(s) 0
#define undelete(s) (-1)
#endif

#ifndef S_ISTXT
#ifdef S_ISVTX
#define S_ISTXT S_ISVTX
#else
#define S_ISTXT 0
#endif
#endif /* !S_ISTXT */

#ifndef __unused
# define __unused
#endif

#if K_OS == K_OS_WINDOWS || K_OS == K_OS_OS2
# define IS_SLASH(ch)   ((ch) == '/' || (ch) == '\\')
#else
# define IS_SLASH(ch)   ((ch) == '/')
#endif

#define	STRIP_TRAILING_SLASH(p) {					\
        while ((p).p_end > (p).p_path + 1 && IS_SLASH((p).p_end[-1]))	\
                *--(p).p_end = 0;					\
}

/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
typedef struct CPINSTANCE
{
	CPUTILSINSTANCE Utils;
	int Rflag, rflag;
	int cp_ignore_non_existing, cp_changed_only;
	KBUILDPROTECTION g_ProtData;
} CPINSTANCE;

/* have wrappers for globals in cp_extern! */


enum op { FILE_TO_FILE, FILE_TO_DIR, DIR_TO_DNE };


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
enum cp_arg {
    CP_OPT_HELP = 261,
    CP_OPT_VERSION,
    CP_OPT_IGNORE_NON_EXISTING,
    CP_OPT_CHANGED,
    CP_OPT_DISABLE_PROTECTION,
    CP_OPT_ENABLE_PROTECTION,
    CP_OPT_ENABLE_FULL_PROTECTION,
    CP_OPT_DISABLE_FULL_PROTECTION,
    CP_OPT_PROTECTION_DEPTH
};

static struct option long_options[] =
{
    { "help",   					no_argument, 0, CP_OPT_HELP },
    { "version",   					no_argument, 0, CP_OPT_VERSION },
    { "ignore-non-existing",				no_argument, 0, CP_OPT_IGNORE_NON_EXISTING },
    { "changed",					no_argument, 0, CP_OPT_CHANGED },
    { "disable-protection",				no_argument, 0, CP_OPT_DISABLE_PROTECTION },
    { "enable-protection",				no_argument, 0, CP_OPT_ENABLE_PROTECTION },
    { "enable-full-protection",				no_argument, 0, CP_OPT_ENABLE_FULL_PROTECTION },
    { "disable-full-protection",			no_argument, 0, CP_OPT_DISABLE_FULL_PROTECTION },
    { "protection-depth",				required_argument, 0, CP_OPT_PROTECTION_DEPTH },
    { 0, 0,	0, 0 },
};

static char emptystring[] = "";

#if defined(SIGINFO) && defined(KMK_BUILTIN_STANDALONE)
volatile sig_atomic_t g_cp_info;
#endif

extern mode_t g_fUMask;


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static int copy(CPINSTANCE *pThis, char * const *, enum op, int);
#ifdef FTSCALL
static int FTSCALL mastercmp(const FTSENT * const *, const FTSENT * const *);
#else
static int mastercmp(const FTSENT **, const FTSENT **);
#endif
#if defined(SIGINFO) && defined(KMK_BUILTIN_STANDALONE)
static void siginfo(int __unused);
#endif
static int usage(PKMKBUILTINCTX, int);

int
kmk_builtin_cp(int argc, char **argv, char **envp, PKMKBUILTINCTX pCtx)
{
	CPINSTANCE This;
	struct getopt_state_r gos;
	struct stat to_stat, tmp_stat;
	enum op type;
	int Hflag, Lflag, Pflag, ch, fts_options, r, have_trailing_slash, rc;
	char *target;

        /* init globals */
	This.Utils.pCtx = pCtx;
        This.Utils.to.p_end = This.Utils.to.p_path;
        This.Utils.to.target_end = emptystring;
        memset(This.Utils.to.p_path, 0, sizeof(This.Utils.to.p_path));
        This.Utils.fflag = 0;
	This.Utils.iflag = 0;
	This.Utils.nflag = 0;
	This.Utils.pflag = 0;
	This.Utils.vflag = 0;
	This.Rflag = 0;
	This.rflag = 0;
	This.cp_ignore_non_existing = This.cp_changed_only = 0;
	kBuildProtectionInit(&This.g_ProtData, pCtx);

	Hflag = Lflag = Pflag = 0;
	getopt_initialize_r(&gos, argc, argv, "HLPRfinprv", long_options, envp, pCtx);
	while ((ch = getopt_long_r(&gos, NULL)) != -1)
		switch (ch) {
		case 'H':
			Hflag = 1;
			Lflag = Pflag = 0;
			break;
		case 'L':
			Lflag = 1;
			Hflag = Pflag = 0;
			break;
		case 'P':
			Pflag = 1;
			Hflag = Lflag = 0;
			break;
		case 'R':
			This.Rflag = 1;
			break;
		case 'f':
			This.Utils.fflag = 1;
			This.Utils.iflag = This.Utils.nflag = 0;
			break;
		case 'i':
			This.Utils.iflag = 1;
			This.Utils.fflag = This.Utils.nflag = 0;
			break;
		case 'n':
			This.Utils.nflag = 1;
			This.Utils.fflag = This.Utils.iflag = 0;
			break;
		case 'p':
			This.Utils.pflag = 1;
			break;
		case 'r':
			This.rflag = 1;
			break;
		case 'v':
			This.Utils.vflag = 1;
			break;
		case CP_OPT_HELP:
			usage(pCtx, 0);
			kBuildProtectionTerm(&This.g_ProtData);
			return 0;
		case CP_OPT_VERSION:
			kBuildProtectionTerm(&This.g_ProtData);
			return kbuild_version(argv[0]);
		case CP_OPT_IGNORE_NON_EXISTING:
			This.cp_ignore_non_existing = 1;
			break;
		case CP_OPT_CHANGED:
			This.cp_changed_only = 1;
			break;
		case CP_OPT_DISABLE_PROTECTION:
			kBuildProtectionDisable(&This.g_ProtData, KBUILDPROTECTIONTYPE_RECURSIVE);
			break;
		case CP_OPT_ENABLE_PROTECTION:
			kBuildProtectionEnable(&This.g_ProtData, KBUILDPROTECTIONTYPE_RECURSIVE);
			break;
		case CP_OPT_ENABLE_FULL_PROTECTION:
			kBuildProtectionEnable(&This.g_ProtData, KBUILDPROTECTIONTYPE_FULL);
			break;
		case CP_OPT_DISABLE_FULL_PROTECTION:
			kBuildProtectionDisable(&This.g_ProtData, KBUILDPROTECTIONTYPE_FULL);
			break;
		case CP_OPT_PROTECTION_DEPTH:
			if (kBuildProtectionSetDepth(&This.g_ProtData, gos.optarg)) {
				kBuildProtectionTerm(&This.g_ProtData);
				return 1;
			}
			break;
		default:
			kBuildProtectionTerm(&This.g_ProtData);
		        return usage(pCtx, 1);
		}
	argc -= gos.optind;
	argv += gos.optind;

	if (argc < 2) {
		kBuildProtectionTerm(&This.g_ProtData);
		return usage(pCtx, 1);
	}

	fts_options = FTS_NOCHDIR | FTS_PHYSICAL;
	if (This.rflag) {
		if (This.Rflag) {
			kBuildProtectionTerm(&This.g_ProtData);
			return errx(pCtx, 1,
		    "the -R and -r options may not be specified together.");
		}
		if (Hflag || Lflag || Pflag)
			errx(pCtx, 1,
	"the -H, -L, and -P options may not be specified with the -r option.");
		fts_options &= ~FTS_PHYSICAL;
		fts_options |= FTS_LOGICAL;
        }
	if (This.Rflag) {
		if (Hflag)
			fts_options |= FTS_COMFOLLOW;
		if (Lflag) {
			fts_options &= ~FTS_PHYSICAL;
			fts_options |= FTS_LOGICAL;
		}
	} else {
		fts_options &= ~FTS_PHYSICAL;
		fts_options |= FTS_LOGICAL | FTS_COMFOLLOW;
	}
#if defined(SIGINFO) && defined(KMK_BUILTIN_STANDALONE)
	(void)signal(SIGINFO, siginfo);
#endif

	/* Save the target base in "to". */
	target = argv[--argc];
	if (strlcpy(This.Utils.to.p_path, target, sizeof(This.Utils.to.p_path)) >= sizeof(This.Utils.to.p_path)) {
		kBuildProtectionTerm(&This.g_ProtData);
		return errx(pCtx, 1, "%s: name too long", target);
	}
	This.Utils.to.p_end = This.Utils.to.p_path + strlen(This.Utils.to.p_path);
        if (This.Utils.to.p_path == This.Utils.to.p_end) {
		*This.Utils.to.p_end++ = '.';
		*This.Utils.to.p_end = 0;
	}
	have_trailing_slash = IS_SLASH(This.Utils.to.p_end[-1]);
	if (have_trailing_slash)
		STRIP_TRAILING_SLASH(This.Utils.to);
	This.Utils.to.target_end = This.Utils.to.p_end;

	/* Set end of argument list for fts(3). */
	argv[argc] = NULL;

	/*
	 * Cp has two distinct cases:
	 *
	 * cp [-R] source target
	 * cp [-R] source1 ... sourceN directory
	 *
	 * In both cases, source can be either a file or a directory.
	 *
	 * In (1), the target becomes a copy of the source. That is, if the
	 * source is a file, the target will be a file, and likewise for
	 * directories.
	 *
	 * In (2), the real target is not directory, but "directory/source".
	 */
	r = stat(This.Utils.to.p_path, &to_stat);
	if (r == -1 && errno != ENOENT) {
		kBuildProtectionTerm(&This.g_ProtData);
		return err(pCtx, 1, "stat: %s", This.Utils.to.p_path);
	}
	if (r == -1 || !S_ISDIR(to_stat.st_mode)) {
		/*
		 * Case (1).  Target is not a directory.
		 */
		if (argc > 1) {
			kBuildProtectionTerm(&This.g_ProtData);
			return usage(pCtx, 1);
		}
		/*
		 * Need to detect the case:
		 *	cp -R dir foo
		 * Where dir is a directory and foo does not exist, where
		 * we want pathname concatenations turned on but not for
		 * the initial mkdir().
		 */
		if (r == -1) {
			if (This.rflag || (This.Rflag && (Lflag || Hflag)))
				stat(*argv, &tmp_stat);
			else
				lstat(*argv, &tmp_stat);

			if (S_ISDIR(tmp_stat.st_mode) && (This.Rflag || This.rflag))
				type = DIR_TO_DNE;
			else
				type = FILE_TO_FILE;
		} else
			type = FILE_TO_FILE;

		if (have_trailing_slash && type == FILE_TO_FILE) {
			kBuildProtectionTerm(&This.g_ProtData);
			if (r == -1)
				return errx(pCtx, 1, "directory %s does not exist",
				            This.Utils.to.p_path);
			else
				return errx(pCtx, 1, "%s is not a directory", This.Utils.to.p_path);
		}
	} else
		/*
		 * Case (2).  Target is a directory.
		 */
		type = FILE_TO_DIR;

	/* Finally, check that the "to" directory isn't protected. */
	rc = 1;
	if (!kBuildProtectionScanEnv(&This.g_ProtData, envp, "KMK_CP_")
	 && !kBuildProtectionEnforce(&This.g_ProtData,
				     This.Rflag || This.rflag
				     ? KBUILDPROTECTIONTYPE_RECURSIVE
				     : KBUILDPROTECTIONTYPE_FULL,
				     This.Utils.to.p_path)) {
	    rc = copy(&This, argv, type, fts_options);
	}

	kBuildProtectionTerm(&This.g_ProtData);
	return rc;
}

#ifdef KMK_BUILTIN_STANDALONE
mode_t g_fUMask;
int main(int argc, char **argv, char **envp)
{
    KMKBUILTINCTX Ctx = { "kmk_cp", NULL };
    umask(g_fUMask = umask(0077));
    return kmk_builtin_cp(argc, argv, envp, &Ctx);
}
#endif

static int
copy(CPINSTANCE *pThis, char * const *argv, enum op type, int fts_options)
{
	struct stat to_stat;
	FTS *ftsp;
	FTSENT *curr;
	int base = 0, dne, badcp, rval;
	size_t nlen;
	char *p, *target_mid;
	mode_t mask, mode;

	/*
	 * Keep an inverted copy of the umask, for use in correcting
	 * permissions on created directories when not using -p.
	 */
	mask = g_fUMask;
	assert(mask == umask(mask));
	mask = ~mask;

	if ((ftsp = fts_open(argv, fts_options, mastercmp)) == NULL)
		return err(pThis->Utils.pCtx, 1, "fts_open");
	for (badcp = rval = 0; (curr = fts_read(ftsp)) != NULL; badcp = 0) {
                int copied = 0;

		switch (curr->fts_info) {
		case FTS_NS:
			if (   pThis->cp_ignore_non_existing
			    && curr->fts_errno == ENOENT) {
				if (pThis->Utils.vflag) {
					warnx(pThis->Utils.pCtx, "fts: %s: %s", curr->fts_path,
					      strerror(curr->fts_errno));
				}
				continue;
			}
			/* fall thru */
		case FTS_DNR:
		case FTS_ERR:
			warnx(pThis->Utils.pCtx, "fts: %s: %s",
			    curr->fts_path, strerror(curr->fts_errno));
			badcp = rval = 1;
			continue;
		case FTS_DC:			/* Warn, continue. */
			warnx(pThis->Utils.pCtx, "%s: directory causes a cycle", curr->fts_path);
			badcp = rval = 1;
			continue;
		default:
			;
		}

		/*
		 * If we are in case (2) or (3) above, we need to append the
                 * source name to the target name.
                 */
		if (type != FILE_TO_FILE) {
			/*
			 * Need to remember the roots of traversals to create
			 * correct pathnames.  If there's a directory being
			 * copied to a non-existent directory, e.g.
			 *	cp -R a/dir noexist
			 * the resulting path name should be noexist/foo, not
			 * noexist/dir/foo (where foo is a file in dir), which
			 * is the case where the target exists.
			 *
			 * Also, check for "..".  This is for correct path
			 * concatenation for paths ending in "..", e.g.
			 *	cp -R .. /tmp
			 * Paths ending in ".." are changed to ".".  This is
			 * tricky, but seems the easiest way to fix the problem.
			 *
			 * XXX
			 * Since the first level MUST be FTS_ROOTLEVEL, base
			 * is always initialized.
			 */
			if (curr->fts_level == FTS_ROOTLEVEL) {
				if (type != DIR_TO_DNE) {
					p = strrchr(curr->fts_path, '/');
#if K_OS == K_OS_WINDOWS || K_OS == K_OS_OS2
                                        if (strrchr(curr->fts_path, '\\') > p)
                                            p = strrchr(curr->fts_path, '\\');
#endif
					base = (p == NULL) ? 0 :
					    (int)(p - curr->fts_path + 1);

					if (!strcmp(&curr->fts_path[base],
					    ".."))
						base += 1;
				} else
					base = curr->fts_pathlen;
			}

			p = &curr->fts_path[base];
			nlen = curr->fts_pathlen - base;
			target_mid = pThis->Utils.to.target_end;
			if (!IS_SLASH(*p) && !IS_SLASH(target_mid[-1]))
				*target_mid++ = '/';
			*target_mid = 0;
			if (target_mid - pThis->Utils.to.p_path + nlen >= PATH_MAX) {
				warnx(pThis->Utils.pCtx, "%s%s: name too long (not copied)",
				    pThis->Utils.to.p_path, p);
				badcp = rval = 1;
				continue;
			}
			(void)strncat(target_mid, p, nlen);
			pThis->Utils.to.p_end = target_mid + nlen;
			*pThis->Utils.to.p_end = 0;
			STRIP_TRAILING_SLASH(pThis->Utils.to);
		}

		if (curr->fts_info == FTS_DP) {
			/*
			 * We are nearly finished with this directory.  If we
			 * didn't actually copy it, or otherwise don't need to
			 * change its attributes, then we are done.
			 */
			if (!curr->fts_number)
				continue;
			/*
			 * If -p is in effect, set all the attributes.
			 * Otherwise, set the correct permissions, limited
			 * by the umask.  Optimise by avoiding a chmod()
			 * if possible (which is usually the case if we
			 * made the directory).  Note that mkdir() does not
			 * honour setuid, setgid and sticky bits, but we
			 * normally want to preserve them on directories.
			 */
			if (pThis->Utils.pflag) {
				if (copy_file_attribs(&pThis->Utils, curr->fts_statp, -1))
				    rval = 1;
			} else {
				mode = curr->fts_statp->st_mode;
				if ((mode & (S_ISUID | S_ISGID | S_ISTXT)) ||
				    ((mode | S_IRWXU) & mask) != (mode & mask))
					if (chmod(pThis->Utils.to.p_path, mode & mask) != 0){
						warn(pThis->Utils.pCtx, "chmod: %s", pThis->Utils.to.p_path);
						rval = 1;
					}
			}
			continue;
		}

		/* Not an error but need to remember it happened */
		if (stat(pThis->Utils.to.p_path, &to_stat) == -1)
			dne = 1;
		else {
			if (to_stat.st_dev == curr->fts_statp->st_dev &&
			    to_stat.st_dev != 0 &&
			    to_stat.st_ino == curr->fts_statp->st_ino &&
			    to_stat.st_ino != 0) {
				warnx(pThis->Utils.pCtx, "%s and %s are identical (not copied).",
				    pThis->Utils.to.p_path, curr->fts_path);
				badcp = rval = 1;
				if (S_ISDIR(curr->fts_statp->st_mode))
					(void)fts_set(ftsp, curr, FTS_SKIP);
				continue;
			}
			if (!S_ISDIR(curr->fts_statp->st_mode) &&
			    S_ISDIR(to_stat.st_mode)) {
				warnx(pThis->Utils.pCtx, "cannot overwrite directory %s with "
				    "non-directory %s",
				    pThis->Utils.to.p_path, curr->fts_path);
				badcp = rval = 1;
				continue;
			}
			dne = 0;
		}

		switch (curr->fts_statp->st_mode & S_IFMT) {
#ifdef S_IFLNK
		case S_IFLNK:
			/* Catch special case of a non-dangling symlink */
			if ((fts_options & FTS_LOGICAL) ||
			    ((fts_options & FTS_COMFOLLOW) &&
			    curr->fts_level == 0)) {
				if (copy_file(&pThis->Utils, curr, dne, pThis->cp_changed_only, &copied))
					badcp = rval = 1;
			} else {
				if (copy_link(&pThis->Utils, curr, !dne))
					badcp = rval = 1;
			}
			break;
#endif
		case S_IFDIR:
			if (!pThis->Rflag && !pThis->rflag) {
				warnx(pThis->Utils.pCtx, "%s is a directory (not copied).",
				    curr->fts_path);
				(void)fts_set(ftsp, curr, FTS_SKIP);
				badcp = rval = 1;
				break;
			}
			/*
			 * If the directory doesn't exist, create the new
			 * one with the from file mode plus owner RWX bits,
			 * modified by the umask.  Trade-off between being
			 * able to write the directory (if from directory is
			 * 555) and not causing a permissions race.  If the
			 * umask blocks owner writes, we fail..
			 */
			if (dne) {
				if (mkdir(pThis->Utils.to.p_path,
				    curr->fts_statp->st_mode | S_IRWXU) < 0)
					return err(pThis->Utils.pCtx, 1, "mkdir: %s", pThis->Utils.to.p_path);
			} else if (!S_ISDIR(to_stat.st_mode)) {
				errno = ENOTDIR;
				return err(pThis->Utils.pCtx, 1, "to-mode: %s", pThis->Utils.to.p_path);
			}
			/*
			 * Arrange to correct directory attributes later
			 * (in the post-order phase) if this is a new
			 * directory, or if the -p flag is in effect.
			 */
			curr->fts_number = pThis->Utils.pflag || dne;
			break;
#ifdef S_IFBLK
		case S_IFBLK:
#endif
		case S_IFCHR:
			if (pThis->Rflag) {
				if (copy_special(&pThis->Utils, curr->fts_statp, !dne))
					badcp = rval = 1;
			} else {
				if (copy_file(&pThis->Utils, curr, dne, pThis->cp_changed_only, &copied))
					badcp = rval = 1;
			}
			break;
#ifdef S_IFIFO
		case S_IFIFO:
#endif
			if (pThis->Rflag) {
				if (copy_fifo(&pThis->Utils, curr->fts_statp, !dne))
					badcp = rval = 1;
			} else {
				if (copy_file(&pThis->Utils, curr, dne, pThis->cp_changed_only, &copied))
					badcp = rval = 1;
			}
			break;
		default:
			if (copy_file(&pThis->Utils, curr, dne, pThis->cp_changed_only, &copied))
				badcp = rval = 1;
			break;
		}
		if (pThis->Utils.vflag && !badcp)
			kmk_builtin_ctx_printf(pThis->Utils.pCtx, 0, copied ? "%s -> %s\n" : "%s matches %s - not copied\n",
					       curr->fts_path, pThis->Utils.to.p_path);
	}
	if (errno)
		return err(pThis->Utils.pCtx, 1, "fts_read");
	return (rval);
}

/*
 * mastercmp --
 *	The comparison function for the copy order.  The order is to copy
 *	non-directory files before directory files.  The reason for this
 *	is because files tend to be in the same cylinder group as their
 *	parent directory, whereas directories tend not to be.  Copying the
 *	files first reduces seeking.
 */
#ifdef FTSCALL
static int FTSCALL mastercmp(const FTSENT * const *a, const FTSENT * const *b)
#else
static int mastercmp(const FTSENT **a, const FTSENT **b)
#endif
{
	int a_info, b_info;

	a_info = (*a)->fts_info;
	if (a_info == FTS_ERR || a_info == FTS_NS || a_info == FTS_DNR)
		return (0);
	b_info = (*b)->fts_info;
	if (b_info == FTS_ERR || b_info == FTS_NS || b_info == FTS_DNR)
		return (0);
	if (a_info == FTS_D)
		return (-1);
	if (b_info == FTS_D)
		return (1);
	return (0);
}

#if defined(SIGINFO) && defined(KMK_BUILTIN_STANDALONE)
static void
siginfo(int sig __unused)
{

	g_cp_info = 1;
}
#endif


static int
usage(PKMKBUILTINCTX pCtx, int fIsErr)
{
	kmk_builtin_ctx_printf(pCtx, fIsErr,
"usage: %s [options] src target\n"
"   or: %s [options] src1 ... srcN directory\n"
"   or: %s --help\n"
"   or: %s --version\n"
"\n"
"Options:\n"
"   -R  Recursive copy.\n"
"   -H  Follow symbolic links on the commandline. Only valid with -R.\n"
"   -L  Follow all symbolic links. Only valid with -R.\n"
"   -P  Do not follow symbolic links. Default. Only valid with -R\n"
"   -f  Force. Overrides -i and -n.\n"
"   -i  Iteractive. Overrides -n and -f.\n"
"   -n  Don't overwrite any files. Overrides -i and -f.\n"
"   -v  Verbose.\n"
"   --ignore-non-existing\n"
"       Don't fail if the specified source file doesn't exist.\n"
"   --changed\n"
"       Only copy if changed (i.e. compare first).\n"
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
"    KMK_CP_DISABLE_PROTECTION\n"
"       Same as --disable-protection. Overrides command line.\n"
"    KMK_CP_ENABLE_PROTECTION\n"
"       Same as --enable-protection. Overrides everyone else.\n"
"    KMK_CP_ENABLE_FULL_PROTECTION\n"
"       Same as --enable-full-protection. Overrides everyone else.\n"
"    KMK_CP_DISABLE_FULL_PROTECTION\n"
"       Same as --disable-full-protection. Overrides command line.\n"
"    KMK_CP_PROTECTION_DEPTH\n"
"       Same as --protection-depth. Overrides command line.\n"
"\n"
"The file protection of the top %d layers of the file hierarchy is there\n"
"to try prevent makefiles from doing bad things to your system. This\n"
"protection is not bulletproof, but should help prevent you from shooting\n"
"yourself in the foot.\n"
		,
	        pCtx->pszProgName, pCtx->pszProgName,
		pCtx->pszProgName, pCtx->pszProgName,
	        kBuildProtectionDefaultDepth(), kBuildProtectionDefaultDepth());
	return 1;
}
