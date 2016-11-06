/* $Id: fts-nt.c 3006 2016-11-06 00:15:34Z knut.osmundsen@oracle.com $ */
/** @file
 * Source for the NT port of BSD fts.c.
 *
 * @copyright   1990, 1993, 1994 The Regents of the University of California.  All rights reserved.
 * @copyright   NT modifications Copyright (C) 2016 knut st. osmundsen <bird-klibc-spam-xiv@anduin.net>
 * @licenses    BSD3
 *
 *
 * Some hints about how the code works.
 *
 * The input directories & files are entered into a pseudo root directory and
 * processed one after another, depth first.
 *
 * Directories are completely read into memory first and arranged as linked
 * list anchored on FTS::fts_cur.  fts_read does a pop-like operation on that
 * list, freeing the nodes after they've been completely processed.
 * Subdirectories are returned twice by fts_read, the first time when it
 * decends into it (FTS_D), and the second time as it ascends from it (FTS_DP).
 *
 * In parallel to fts_read, there's the fts_children API that fetches the
 * directory content in a similar manner, but for the consumption of the API
 * caller rather than FTS itself.  The result hangs on FTS::fts_child so it can
 * be freed when the directory changes or used by fts_read when it is called
 * upon to enumerate the directory.
 *
 *
 * The NT port of the code does away with the directory changing in favor of
 * using directory relative opens (present in NT since for ever, just not
 * exposed thru Win32).  A new FTSENT member fts_dirfd has been added to make
 * this possible for API users too.
 *
 * Note! When using Win32 APIs with path input relative to the current
 *  	 directory, the internal DOS <-> NT path converter will expand it to a
 *  	 full path and subject it to the 260 char limit.
 *
 * The richer NT directory enumeration API allows us to do away with all the
 * stat() calls, and not have to do link counting and other interesting things
 * to try speed things up.  (You typical stat() implementation on windows is
 * actually a directory enum call with the name of the file as filter.)
 */

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
 *
 * $OpenBSD: fts.c,v 1.22 1999/10/03 19:22:22 millert Exp $
 */

#if 0
#if defined(LIBC_SCCS) && !defined(lint)
static char sccsid[] = "@(#)fts.c	8.6 (Berkeley) 8/14/94";
#endif /* LIBC_SCCS and not lint */
#endif

#include <errno.h>
#include "fts-nt.h"
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "nthlp.h"
#include "ntdir.h"
#include <stdio.h>//debug

static FTSENT	*fts_alloc(FTS *sp, char const *name, size_t namelen, wchar_t const *wcsname, size_t cwcname);
static FTSENT	*fts_alloc_ansi(FTS *sp, char const *name, size_t namelen);
static FTSENT	*fts_alloc_utf16(FTS *sp, wchar_t const *wcsname, size_t cwcname);
static void 	 nt_fts_free_alloc_cache(FTS *sp);
static FTSENT	*fts_build(FTS *, int);
static void	 fts_lfree(FTSENT *);
static void	 fts_load(FTS *, FTSENT *);
static size_t	 fts_maxarglen(char * const *);
static size_t	 fts_maxarglenw(wchar_t * const *);
static void	 fts_padjust(FTS *, FTSENT *);
static void	 fts_padjustw(FTS *, FTSENT *);
static int	 fts_palloc(FTS *, size_t, size_t);
static FTSENT	*fts_sort(FTS *, FTSENT *, size_t);
static int	 fts_stat(FTS *, FTSENT *, int, HANDLE);
static int	 fts_process_stats(FTSENT *, BirdStat_T const *);

#define	ISDOT(a)	(a[0] == '.' && (!a[1] || (a[1] == '.' && !a[2])))

#define	CLR(opt)	(sp->fts_options &= ~(opt))
#define	ISSET(opt)	(sp->fts_options & (opt))
#define	SET(opt)	(sp->fts_options |= (opt))

/* fts_build flags */
#define	BCHILD		1		/* fts_children */
#define	BNAMES		2		/* fts_children, names only */
#define	BREAD		3		/* fts_read */

/* NT needs these: */
#define MAXPATHLEN 260
#define MAX(a, b)  ( (a) >= (b) ? (a) : (b) )

/** Enables BirdDir_T reuse. (Saves malloc and free calls.) */
#define FTS_WITH_DIRHANDLE_REUSE
/** Enables allocation statistics. */
//#define FTS_WITH_STATISTICS
/** Enables FTSENT allocation cache. */
#define FTS_WITH_ALLOC_CACHE
/** Number of size buckets for the FTSENT allocation cache. */
#define FTS_NUM_FREE_BUCKETS    64
/** Shift for converting size to free bucket index. */
#define FTS_FREE_BUCKET_SHIFT   4
/** The FTSENT allocation alignment. */
#define FTS_ALIGN_FTSENT        (1U << FTS_FREE_BUCKET_SHIFT)

/*
 * Internal representation of an FTS, including extra implementation
 * details.  The FTS returned from fts_open points to this structure's
 * ftsp_fts member (and can be cast to an _fts_private as required)
 */
struct _fts_private {
	FTS		ftsp_fts;
#ifdef FTS_WITH_DIRHANDLE_REUSE
	/** Statically allocate directory handle. */
	BirdDir_T	dirhandle;
#endif
#ifdef FTS_WITH_ALLOC_CACHE
	/** Number of free entries in the above buckets. */
	size_t		numfree;
# ifdef FTS_WITH_STATISTICS
	size_t		allocs;
	size_t		hits;
	size_t		misses;
# endif
	/** Free FTSENT buckets (by size).
	 * This is to avoid hitting the heap, which is a little sluggish on windows. */
	struct
	{
		FTSENT		*head;
	} freebuckets[FTS_NUM_FREE_BUCKETS];
#endif
};


static FTS * FTSCALL
nt_fts_open_common(char * const *argv, wchar_t * const *wcsargv, int options,
    int (*compar)(const FTSENT * const *, const FTSENT * const *))
{
	struct _fts_private *priv;
	FTS *sp;
	FTSENT *p, *root;
	FTSENT *parent, *tmp;
	size_t len, nitems;

	birdResolveImports();

	/* Options check. */
	if (options & ~FTS_OPTIONMASK) {
		errno = EINVAL;
		return (NULL);
	}

	/* fts_open() requires at least one path */
	if (*argv == NULL) {
		errno = EINVAL;
		return (NULL);
	}

	/* Allocate/initialize the stream. */
	if ((priv = calloc(1, sizeof(*priv))) == NULL)
		return (NULL);
	sp = &priv->ftsp_fts;
	sp->fts_compar = compar;
	sp->fts_options = options;
	SET(FTS_NOCHDIR); /* NT: FTS_NOCHDIR is always on (for external consumes) */

	/* Shush, GCC. */
	tmp = NULL;

	/*
	 * Start out with 1K of path space, and enough, in any case,
	 * to hold the user's paths.
	 */
	if (fts_palloc(sp, MAX(argv ? fts_maxarglen(argv) : 1, MAXPATHLEN),
				   MAX(wcsargv ? fts_maxarglenw(wcsargv) : 1, MAXPATHLEN)) )
		goto mem1;

	/* Allocate/initialize root's parent. */
	if ((parent = fts_alloc(sp, NULL, 0, NULL, 0)) == NULL)
		goto mem2;
	parent->fts_level = FTS_ROOTPARENTLEVEL;

	/* Allocate/initialize root(s). */
	for (root = NULL, nitems = 0; *argv != NULL; ++argv, ++nitems) {
		/* NT: We need to do some small input transformations to make this and
		       the API user code happy.  1. Lone drive letters get a dot
		       appended so it won't matter if a slash is appended afterwards.
		       2. DOS slashes are converted to UNIX ones. */
		wchar_t *wcslash;

		if (wcsargv) {
			len = wcslen(*wcsargv);
			if (len == 2 && wcsargv[0][1] == ':') {
				wchar_t wcsdrive[4];
				wcsdrive[0] = wcsargv[0][0];
				wcsdrive[1] = ':';
				wcsdrive[2] = '.';
				wcsdrive[3] = '\0';
				p = fts_alloc_utf16(sp, wcsdrive, 3);
			} else {
				p = fts_alloc_utf16(sp, *wcsargv, len);
			}
		} else {
			len = strlen(*argv);
			if (len == 2 && argv[0][1] == ':') {
				char szdrive[4];
				szdrive[0] = argv[0][0];
				szdrive[1] = ':';
				szdrive[2] = '.';
				szdrive[3] = '\0';
				p = fts_alloc_ansi(sp, szdrive, 3);
			} else {
				p = fts_alloc_ansi(sp, *argv, len);
			}
		}
		if (p != NULL) { /* likely */ } else { goto mem3; }

		wcslash = wcschr(p->fts_wcsname, '\\');
		while (wcslash != NULL) {
			*wcslash++ = '/';
			wcslash = wcschr(p->fts_wcsname, '\\');
		}

		if (p->fts_name) {
			char *slash = strchr(p->fts_name, '\\');
			while (slash != NULL) {
				*slash++ = '/';
				slash = strchr(p->fts_name, '\\');
			}
		}

		p->fts_level = FTS_ROOTLEVEL;
		p->fts_parent = parent;
		p->fts_accpath = p->fts_name;
		p->fts_wcsaccpath = p->fts_wcsname;
		p->fts_info = fts_stat(sp, p, ISSET(FTS_COMFOLLOW), INVALID_HANDLE_VALUE);

		/* Command-line "." and ".." are real directories. */
		if (p->fts_info == FTS_DOT)
			p->fts_info = FTS_D;

		/*
		 * If comparison routine supplied, traverse in sorted
		 * order; otherwise traverse in the order specified.
		 */
		if (compar) {
			p->fts_link = root;
			root = p;
		} else {
			p->fts_link = NULL;
			if (root == NULL)
				tmp = root = p;
			else {
				tmp->fts_link = p;
				tmp = p;
			}
		}
	}
	if (compar && nitems > 1)
		root = fts_sort(sp, root, nitems);

	/*
	 * Allocate a dummy pointer and make fts_read think that we've just
	 * finished the node before the root(s); set p->fts_info to FTS_INIT
	 * so that everything about the "current" node is ignored.
	 */
	if ((sp->fts_cur = fts_alloc(sp, NULL, 0, NULL, 0)) == NULL)
		goto mem3;
	sp->fts_cur->fts_link = root;
	sp->fts_cur->fts_info = FTS_INIT;

	return (sp);

mem3:
	fts_lfree(root);
	free(parent);
mem2:
	free(sp->fts_path);
	free(sp->fts_wcspath);
mem1:
	free(sp);
	return (NULL);
}


FTS * FTSCALL
nt_fts_open(char * const *argv, int options,
    int (*compar)(const FTSENT * const *, const FTSENT * const *))
{
	return nt_fts_open_common(argv, NULL, options, compar);
}


FTS * FTSCALL
nt_fts_openw(wchar_t * const *argv, int options,
    int (*compar)(const FTSENT * const *, const FTSENT * const *))
{
	return nt_fts_open_common(NULL, argv, options, compar);
}


/**
 * Called by fts_read for FTS_ROOTLEVEL entries only.
 */
static void
fts_load(FTS *sp, FTSENT *p)
{
	size_t len;
	wchar_t *pwc;

	/*
	 * Load the stream structure for the next traversal.  Since we don't
	 * actually enter the directory until after the preorder visit, set
	 * the fts_accpath field specially so the chdir gets done to the right
	 * place and the user can access the first node.  From fts_open it's
	 * known that the path will fit.
	 */
	if (!(sp->fts_options & FTS_NO_ANSI)) {
		char *cp;
		len = p->fts_pathlen = p->fts_namelen;
		memmove(sp->fts_path, p->fts_name, len + 1);
		cp = strrchr(p->fts_name, '/');
		if (cp != NULL && (cp != p->fts_name || cp[1])) {
			len = strlen(++cp);
			memmove(p->fts_name, cp, len + 1);
			p->fts_namelen = len;
		}
		p->fts_accpath = p->fts_path = sp->fts_path;
	}

	len = p->fts_cwcpath = p->fts_cwcname;
	memmove(sp->fts_wcspath, p->fts_wcsname, (len + 1) * sizeof(wchar_t));
	pwc = wcsrchr(p->fts_wcsname, '/');
	if (pwc != NULL && (pwc != p->fts_wcsname || pwc[1])) {
		len = wcslen(++pwc);
		memmove(p->fts_wcsname, pwc, (len + 1) * sizeof(wchar_t));
		p->fts_cwcname = len;
	}
	p->fts_wcsaccpath = p->fts_wcspath = sp->fts_wcspath;

	sp->fts_dev = p->fts_dev;
}


int FTSCALL
nt_fts_close(FTS *sp)
{
	FTSENT *freep, *p;
	/*int saved_errno;*/

	/*
	 * This still works if we haven't read anything -- the dummy structure
	 * points to the root list, so we step through to the end of the root
	 * list which has a valid parent pointer.
	 */
	if (sp->fts_cur) {
		for (p = sp->fts_cur; p->fts_level >= FTS_ROOTLEVEL;) {
			freep = p;
			p = p->fts_link != NULL ? p->fts_link : p->fts_parent;
			free(freep);
		}
		free(p);
	}

	/* Free up child linked list, sort array, path buffer. */
	if (sp->fts_child)
		fts_lfree(sp->fts_child);
	if (sp->fts_array)
		free(sp->fts_array);
	free(sp->fts_path);
	free(sp->fts_wcspath);
#ifdef FTS_WITH_ALLOC_CACHE
# ifdef FTS_WITH_STATISTICS
	{
		struct _fts_private *priv = (struct _fts_private *)sp;
		fprintf(stderr, "numfree=%u allocs=%u  hits=%u (%uppt)  misses=%u (%uppt)  other=%u\n",
			priv->numfree, priv->allocs,
			priv->hits,   (unsigned)((double)priv->hits   * 1000.0 / priv->allocs),
			priv->misses, (unsigned)((double)priv->misses * 1000.0 / priv->allocs),
			priv->allocs - priv->misses - priv->hits);
        }
# endif
#endif
	nt_fts_free_alloc_cache(sp);
#ifdef FTS_WITH_DIRHANDLE_REUSE
	birdDirClose(&((struct _fts_private *)sp)->dirhandle);
#endif

	/* Free up the stream pointer. */
	free(sp);
	return (0);
}


/**
 * Frees a FTSENT structure by way of the allocation cache.
 */
static void
fts_free_entry(FTS *sp, FTSENT *tmp)
{
	if (tmp != NULL) {
		struct _fts_private *priv = (struct _fts_private *)sp;
#ifdef FTS_WITH_ALLOC_CACHE
		size_t idx;
#endif

		if (tmp->fts_dirfd == INVALID_HANDLE_VALUE) {
			/* There are probably more files than directories out there. */
		} else {
			birdCloseFile(tmp->fts_dirfd);
			tmp->fts_dirfd = INVALID_HANDLE_VALUE;
		}

#ifdef FTS_WITH_ALLOC_CACHE
		idx = (tmp->fts_alloc_size - sizeof(FTSENT)) >> FTS_FREE_BUCKET_SHIFT;
		if (idx < FTS_NUM_FREE_BUCKETS) {
			tmp->fts_link = priv->freebuckets[idx].head;
			priv->freebuckets[idx].head = tmp;
		} else {
			tmp->fts_link = priv->freebuckets[FTS_NUM_FREE_BUCKETS - 1].head;
			priv->freebuckets[FTS_NUM_FREE_BUCKETS - 1].head = tmp;
		}

		priv->numfree++;
#else
		free(tmp);
#endif
	}
}


/*
 * Special case of "/" at the end of the path so that slashes aren't
 * appended which would cause paths to be written as "....//foo".
 */
#define	NAPPEND(p)  ( p->fts_pathlen - (p->fts_path[p->fts_pathlen - 1]    ==  '/') )
#define	NAPPENDW(p) ( p->fts_cwcpath - (p->fts_wcspath[p->fts_cwcpath - 1] == L'/') )

FTSENT * FTSCALL
nt_fts_read(FTS *sp)
{
	FTSENT *p, *tmp;
	int instr;
	wchar_t *pwc;

	/* Set current node pointer. */
	p = sp->fts_cur;

	/* If finished or unrecoverable error, return NULL. */
	if (p != NULL && !ISSET(FTS_STOP)) {
		/* likely */
	} else {
		return (NULL);
	}

	/* Save and zero out user instructions. */
	instr = p->fts_instr;
	p->fts_instr = FTS_NOINSTR;

	/* Any type of file may be re-visited; re-stat and re-turn. */
	if (instr != FTS_AGAIN) {
		/* likely */
	} else {
		p->fts_info = fts_stat(sp, p, 0, INVALID_HANDLE_VALUE);
		return (p);
	}

	/*
	 * Following a symlink -- SLNONE test allows application to see
	 * SLNONE and recover.  If indirecting through a symlink, have
	 * keep a pointer to current location.  If unable to get that
	 * pointer, follow fails.
	 *
	 * NT: Since we don't change directory, we just set FTS_SYMFOLLOW
	 *     here in case a API client checks it.
	 */
	if (   instr != FTS_FOLLOW
	    || (p->fts_info != FTS_SL && p->fts_info != FTS_SLNONE)) {
	    /* likely */
	} else {
		p->fts_info = fts_stat(sp, p, 1, INVALID_HANDLE_VALUE);
		if (p->fts_info == FTS_D /*&& !ISSET(FTS_NOCHDIR)*/) {
			p->fts_flags |= FTS_SYMFOLLOW;
		}
		return (p);
	}

	/* Directory in pre-order. */
	if (p->fts_info == FTS_D) {
		/* If skipped or crossed mount point, do post-order visit. */
		if (  instr == FTS_SKIP
		    || (ISSET(FTS_XDEV) && p->fts_dev != sp->fts_dev)) {
			if (sp->fts_child) {
				fts_lfree(sp->fts_child);
				sp->fts_child = NULL;
			}
			p->fts_info = FTS_DP;
			return (p);
		}

		/* Rebuild if only read the names and now traversing. */
		if (sp->fts_child != NULL && ISSET(FTS_NAMEONLY)) {
			CLR(FTS_NAMEONLY);
			fts_lfree(sp->fts_child);
			sp->fts_child = NULL;
		}

		/*
		 * Cd to the subdirectory.
		 *
		 * If have already read and now fail to chdir, whack the list
		 * to make the names come out right, and set the parent errno
		 * so the application will eventually get an error condition.
		 * Set the FTS_DONTCHDIR flag so that when we logically change
		 * directories back to the parent we don't do a chdir.
		 *
		 * If haven't read do so.  If the read fails, fts_build sets
		 * FTS_STOP or the fts_info field of the node.
		 */
		if (sp->fts_child == NULL) {
			p = fts_build(sp, BREAD);
			if (p != NULL) {
			    /* likely */
			} else {
			    if (ISSET(FTS_STOP))
				    return (NULL);
			    return sp->fts_cur;
			}

		} else {
			p = sp->fts_child;
			sp->fts_child = NULL;
		}
		goto name;
	}

	/* Move to the next node on this level. */
next:	tmp = p;
	if ((p = p->fts_link) != NULL) {
		/*
		 * If reached the top, return to the original directory (or
		 * the root of the tree), and load the paths for the next root.
		 */
		if (p->fts_level != FTS_ROOTLEVEL) {
			/* likely */
		} else {
			fts_free_entry(sp, tmp);
			fts_load(sp, p);
			return (sp->fts_cur = p);
		}

		/*
		 * User may have called fts_set on the node.  If skipped,
		 * ignore.  If followed, get a file descriptor so we can
		 * get back if necessary.
		 */
		if (p->fts_instr != FTS_SKIP) {
			/* likely */
		} else {
			fts_free_entry(sp, tmp);
			goto next;
		}
		if (p->fts_instr != FTS_FOLLOW) {
			/* likely */
		} else {
			p->fts_info = fts_stat(sp, p, 1, INVALID_HANDLE_VALUE);
			/* NT: See above regarding fts_flags. */
			if (p->fts_info == FTS_D) {
				p->fts_flags |= FTS_SYMFOLLOW;
			}
			p->fts_instr = FTS_NOINSTR;
		}

		fts_free_entry(sp, tmp);

name:
		if (!(sp->fts_options & FTS_NO_ANSI)) {
			char *t = sp->fts_path + NAPPEND(p->fts_parent);
			*t++ = '/';
			memmove(t, p->fts_name, p->fts_namelen + 1);
		}
		pwc = sp->fts_wcspath + NAPPENDW(p->fts_parent);
		*pwc++ = '/';
		memmove(pwc, p->fts_wcsname, (p->fts_cwcname + 1) * sizeof(wchar_t));
		return (sp->fts_cur = p);
	}

	/* Move up to the parent node. */
	p = tmp->fts_parent;

	if (p->fts_level != FTS_ROOTPARENTLEVEL) {
		/* likely */
	} else {
		/*
		 * Done; free everything up and set errno to 0 so the user
		 * can distinguish between error and EOF.
		 */
		fts_free_entry(sp, tmp);
		fts_free_entry(sp, p);
		errno = 0;
		return (sp->fts_cur = NULL);
	}

	/* NUL terminate the pathname. */
	if (!(sp->fts_options & FTS_NO_ANSI))
		sp->fts_path[p->fts_pathlen] = '\0';
	sp->fts_wcspath[ p->fts_cwcpath] = '\0';

	/*
	 * Return to the parent directory.  If at a root node or came through
	 * a symlink, go back through the file descriptor.  Otherwise, cd up
	 * one directory.
	 *
	 * NT: We're doing no fchdir, but we need to close the directory handle.
	 */
	if (p->fts_dirfd != INVALID_HANDLE_VALUE) {
		birdCloseFile(p->fts_dirfd);
		p->fts_dirfd = INVALID_HANDLE_VALUE;
	}
	fts_free_entry(sp, tmp);
	p->fts_info = p->fts_errno ? FTS_ERR : FTS_DP;
	return (sp->fts_cur = p);
}

/*
 * Fts_set takes the stream as an argument although it's not used in this
 * implementation; it would be necessary if anyone wanted to add global
 * semantics to fts using fts_set.  An error return is allowed for similar
 * reasons.
 */
/* ARGSUSED */
int FTSCALL
nt_fts_set(FTS *sp, FTSENT *p, int instr)
{
	if (instr != 0 && instr != FTS_AGAIN && instr != FTS_FOLLOW &&
	    instr != FTS_NOINSTR && instr != FTS_SKIP) {
		errno = EINVAL;
		return (1);
	}
	p->fts_instr = instr;
	return (0);
}

FTSENT * FTSCALL
nt_fts_children(FTS *sp, int instr)
{
	FTSENT *p;

	if (instr != 0 && instr != FTS_NAMEONLY) {
		errno = EINVAL;
		return (NULL);
	}

	/* Set current node pointer. */
	p = sp->fts_cur;

	/*
	 * Errno set to 0 so user can distinguish empty directory from
	 * an error.
	 */
	errno = 0;

	/* Fatal errors stop here. */
	if (ISSET(FTS_STOP))
		return (NULL);

	/* Return logical hierarchy of user's arguments. */
	if (p->fts_info == FTS_INIT)
		return (p->fts_link);

	/*
	 * If not a directory being visited in pre-order, stop here.  Could
	 * allow FTS_DNR, assuming the user has fixed the problem, but the
	 * same effect is available with FTS_AGAIN.
	 */
	if (p->fts_info != FTS_D /* && p->fts_info != FTS_DNR */)
		return (NULL);

	/* Free up any previous child list. */
	if (sp->fts_child != NULL) {
		fts_lfree(sp->fts_child);
		sp->fts_child = NULL; /* (bird - double free for _open(".") failure in original) */
	}

	/* NT: Some BSD utility sets FTS_NAMEONLY? We don't really need this
	       optimization, but since it only hurts that utility, it can stay.  */
	if (instr == FTS_NAMEONLY) {
		assert(0); /* don't specify FTS_NAMEONLY on NT. */
		SET(FTS_NAMEONLY);
		instr = BNAMES;
	} else
		instr = BCHILD;

	return (sp->fts_child = fts_build(sp, instr));
}

#ifndef fts_get_clientptr
#error "fts_get_clientptr not defined"
#endif

void *
(FTSCALL fts_get_clientptr)(FTS *sp)
{

	return (fts_get_clientptr(sp));
}

#ifndef fts_get_stream
#error "fts_get_stream not defined"
#endif

FTS *
(FTSCALL fts_get_stream)(FTSENT *p)
{
	return (fts_get_stream(p));
}

void FTSCALL
nt_fts_set_clientptr(FTS *sp, void *clientptr)
{

	sp->fts_clientptr = clientptr;
}

/*
 * This is the tricky part -- do not casually change *anything* in here.  The
 * idea is to build the linked list of entries that are used by fts_children
 * and fts_read.  There are lots of special cases.
 *
 * The real slowdown in walking the tree is the stat calls.  If FTS_NOSTAT is
 * set and it's a physical walk (so that symbolic links can't be directories),
 * we can do things quickly.  First, if it's a 4.4BSD file system, the type
 * of the file is in the directory entry.  Otherwise, we assume that the number
 * of subdirectories in a node is equal to the number of links to the parent.
 * The former skips all stat calls.  The latter skips stat calls in any leaf
 * directories and for any files after the subdirectories in the directory have
 * been found, cutting the stat calls by about 2/3.
 *
 * NT: We do not do any link counting or stat avoiding, which invalidates the
 *     above warnings.  This function is very simple for us.
 */
static FTSENT *
fts_build(FTS *sp, int type)
{
	BirdDirEntryW_T *dp;
	FTSENT *p, *cur;
	FTSENT * volatile head,* volatile *tailp; /* volatile is to prevent aliasing trouble */
	DIR *dirp;
	int saved_errno, doadjust, doadjust_utf16;
	long level;
	size_t len, cwcdir, maxlen, cwcmax, nitems;
	unsigned fDirOpenFlags;

	/* Set current node pointer. */
	cur = sp->fts_cur;

	/*
	 * Open the directory for reading.  If this fails, we're done.
	 * If being called from fts_read, set the fts_info field.
	 *
	 * NT: We do a two stage open so we can keep the directory handle around
	 *     after we've enumerated the directory.  The dir handle is used by
	 *     us here and by the API users to more efficiently and safely open
	 *     members of the directory.
	 */
	fDirOpenFlags = BIRDDIR_F_EXTRA_INFO | BIRDDIR_F_KEEP_HANDLE;
	if (cur->fts_dirfd == INVALID_HANDLE_VALUE) {
		if (cur->fts_parent->fts_dirfd != INVALID_HANDLE_VALUE) {
			/* (This works fine for symlinks too, since we follow them.) */
			cur->fts_dirfd = birdOpenFileExW(cur->fts_parent->fts_dirfd,
			                                 cur->fts_wcsname,
			                                 FILE_READ_DATA | SYNCHRONIZE,
			                                 FILE_ATTRIBUTE_NORMAL,
			                                 FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
			                                 FILE_OPEN,
			                                 FILE_DIRECTORY_FILE | FILE_OPEN_FOR_BACKUP_INTENT | FILE_SYNCHRONOUS_IO_NONALERT,
			                                 OBJ_CASE_INSENSITIVE);
		} else {
			cur->fts_dirfd = birdOpenFileW(cur->fts_wcsaccpath,
			                               FILE_READ_DATA | SYNCHRONIZE,
			                               FILE_ATTRIBUTE_NORMAL,
			                               FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
			                               FILE_OPEN,
			                               FILE_DIRECTORY_FILE | FILE_OPEN_FOR_BACKUP_INTENT | FILE_SYNCHRONOUS_IO_NONALERT,
			                               OBJ_CASE_INSENSITIVE);
		}
		if (cur->fts_dirfd != INVALID_HANDLE_VALUE) { /* likely */ }
		else goto l_open_err;

	} else {
		fDirOpenFlags |= BIRDDIR_F_RESTART_SCAN;
	}
#ifdef FTS_WITH_DIRHANDLE_REUSE
	dirp = birdDirOpenFromHandleWithReuse(&((struct _fts_private *)sp)->dirhandle, cur->fts_dirfd, NULL, 
					      fDirOpenFlags | BIRDDIR_F_STATIC_ALLOC);
#else
	dirp = birdDirOpenFromHandle(cur->fts_dirfd, NULL, fDirOpenFlags);
#endif
	if (dirp == NULL) {
l_open_err:
		if (type == BREAD) {
			cur->fts_info = FTS_DNR;
			cur->fts_errno = errno;
		}
		return (NULL);
	}

	/*
	 * Figure out the max file name length that can be stored in the
	 * current path -- the inner loop allocates more path as necessary.
	 * We really wouldn't have to do the maxlen calculations here, we
	 * could do them in fts_read before returning the path, but it's a
	 * lot easier here since the length is part of the dirent structure.
	 */
	if (sp->fts_options & FTS_NO_ANSI) {
		len = 0;
		maxlen = 0x10000;
	} else {
		len = NAPPEND(cur);
		len++;
		maxlen = sp->fts_pathlen - len;
	}

	cwcdir = NAPPENDW(cur);
	cwcdir++;
	cwcmax = sp->fts_cwcpath - len;

	level = cur->fts_level + 1;

	/* Read the directory, attaching each entry to the `link' pointer. */
	doadjust = doadjust_utf16 = 0;
	nitems = 0;
	head = NULL;
	tailp = &head;
	while ((dp = birdDirReadW(dirp)) != NULL) {
		if (ISSET(FTS_SEEDOT) || !ISDOT(dp->d_name))  {
			/* assume dirs have two or more entries */
		} else {
			continue;
		}

		if ((p = fts_alloc_utf16(sp, dp->d_name, dp->d_namlen)) != NULL) {
			/* likely */
		} else {
			goto mem1;
		}

		/* include space for NUL */
		if (p->fts_namelen < maxlen && p->fts_cwcname < cwcmax) {
		    /* likely */
		} else {
			void *oldaddr = sp->fts_path;
			wchar_t *oldwcspath = sp->fts_wcspath;
			if (fts_palloc(sp,
			               p->fts_namelen >= maxlen ? len + p->fts_namelen + 1 : 0,
			               p->fts_cwcname >= cwcmax ? cwcdir + p->fts_cwcname + 1 : 0)) {
mem1:
				/*
				 * No more memory for path or structures.  Save
				 * errno, free up the current structure and the
				 * structures already allocated.
				 */
				saved_errno = errno;
				if (p)
					free(p);
				fts_lfree(head);
#ifndef FTS_WITH_DIRHANDLE_REUSE
				birdDirClose(dirp);
#endif
				birdCloseFile(cur->fts_dirfd);
				cur->fts_dirfd = INVALID_HANDLE_VALUE;
				cur->fts_info = FTS_ERR;
				SET(FTS_STOP);
				errno = saved_errno;
				return (NULL);
			}
			/* Did realloc() change the pointer? */
			doadjust       |= oldaddr != sp->fts_path;
			doadjust_utf16 |= oldwcspath != sp->fts_wcspath;
			maxlen = sp->fts_pathlen - len;
			cwcmax = sp->fts_cwcpath - cwcdir;
		}

		p->fts_level = level;
		p->fts_parent = sp->fts_cur;
		p->fts_pathlen = len + p->fts_namelen;
		p->fts_cwcpath = cwcdir + p->fts_cwcname;
		p->fts_accpath = p->fts_path;
		p->fts_wcsaccpath = p->fts_wcspath;
		p->fts_stat = dp->d_stat;
		p->fts_info = fts_process_stats(p, &dp->d_stat);

		/* We walk in directory order so "ls -f" doesn't get upset. */
		p->fts_link = NULL;
		*tailp = p;
		tailp = &p->fts_link;
		++nitems;
	}

#ifndef FTS_WITH_DIRHANDLE_REUSE
	birdDirClose(dirp);
#endif

	/*
	 * If realloc() changed the address of the path, adjust the
	 * addresses for the rest of the tree and the dir list.
	 */
	if (doadjust)
		fts_padjust(sp, head);
	if (doadjust_utf16)
		fts_padjustw(sp, head);

	/* If didn't find anything, return NULL. */
	if (!nitems) {
		if (type == BREAD)
			cur->fts_info = FTS_DP;
		return (NULL);
	}

	/* Sort the entries. */
	if (sp->fts_compar && nitems > 1)
		head = fts_sort(sp, head, nitems);
	return (head);
}


/**
 * @note Only used on NT with input arguments, FTS_AGAIN, and links that needs
 *  	 following.  On link information is generally retrieved during directory
 *  	 enumeration on NT, in line with it's DOS/OS2/FAT API heritage.
 */
static int
fts_stat(FTS *sp, FTSENT *p, int follow, HANDLE dfd)
{
	int saved_errno;
	const wchar_t *wcspath;

	if (dfd == INVALID_HANDLE_VALUE) {
		wcspath = p->fts_wcsaccpath;
	} else {
		wcspath = p->fts_wcsname;
	}

	/*
	 * If doing a logical walk, or application requested FTS_FOLLOW, do
	 * a stat(2).  If that fails, check for a non-existent symlink.  If
	 * fail, set the errno from the stat call.
	 */
	if (ISSET(FTS_LOGICAL) || follow) {
		if (birdStatAtW(dfd, wcspath, &p->fts_stat, 1 /*fFollowLink*/)) {
			saved_errno = errno;
			if (birdStatAtW(dfd, wcspath, &p->fts_stat, 0 /*fFollowLink*/)) {
				p->fts_errno = saved_errno;
				goto err;
			}
			errno = 0;
			if (S_ISLNK(p->fts_stat.st_mode))
				return (FTS_SLNONE);
		}
	} else if (birdStatAtW(dfd, wcspath, &p->fts_stat, 0 /*fFollowLink*/)) {
		p->fts_errno = errno;
err:		memset(&p->fts_stat, 0, sizeof(struct stat));
		return (FTS_NS);
	}
	return fts_process_stats(p, &p->fts_stat);
}

/* Shared between fts_stat and fts_build. */
static int 
fts_process_stats(FTSENT *p, BirdStat_T const *sbp)
{
	if (S_ISDIR(sbp->st_mode)) {
		FTSENT *t;
		fts_dev_t dev;
		fts_ino_t ino;

		/*
		 * Set the device/inode.  Used to find cycles and check for
		 * crossing mount points.  Also remember the link count, used
		 * in fts_build to limit the number of stat calls.  It is
		 * understood that these fields are only referenced if fts_info
		 * is set to FTS_D.
		 */
		dev = p->fts_dev = sbp->st_dev;
		ino = p->fts_ino = sbp->st_ino;
		p->fts_nlink = sbp->st_nlink;

		if (ISDOT(p->fts_wcsname))
			return (FTS_DOT);

		/*
		 * Cycle detection is done by brute force when the directory
		 * is first encountered.  If the tree gets deep enough or the
		 * number of symbolic links to directories is high enough,
		 * something faster might be worthwhile.
		 */
		for (t = p->fts_parent;
		    t->fts_level >= FTS_ROOTLEVEL; t = t->fts_parent)
			if (ino == t->fts_ino && dev == t->fts_dev) {
				p->fts_cycle = t;
				return (FTS_DC);
			}
		return (FTS_D);
	}
	if (S_ISLNK(sbp->st_mode))
		return (FTS_SL);
	if (S_ISREG(sbp->st_mode))
		return (FTS_F);
	return (FTS_DEFAULT);
}

/*
 * The comparison function takes pointers to pointers to FTSENT structures.
 * Qsort wants a comparison function that takes pointers to void.
 * (Both with appropriate levels of const-poisoning, of course!)
 * Use a trampoline function to deal with the difference.
 */
static int
fts_compar(const void *a, const void *b)
{
	FTS *parent;

	parent = (*(const FTSENT * const *)a)->fts_fts;
	return (*parent->fts_compar)(a, b);
}

static FTSENT *
fts_sort(FTS *sp, FTSENT *head, size_t nitems)
{
	FTSENT **ap, *p;

	/*
	 * Construct an array of pointers to the structures and call qsort(3).
	 * Reassemble the array in the order returned by qsort.  If unable to
	 * sort for memory reasons, return the directory entries in their
	 * current order.  Allocate enough space for the current needs plus
	 * 40 so don't realloc one entry at a time.
	 */
	if (nitems > sp->fts_nitems) {
		void *ptr;
		sp->fts_nitems = nitems + 40;
		ptr = realloc(sp->fts_array, sp->fts_nitems * sizeof(FTSENT *));
		if (ptr != NULL) {
			sp->fts_array = ptr;
		} else {
			free(sp->fts_array);
			sp->fts_array = NULL;
			sp->fts_nitems = 0;
			return (head);
		}
	}
	for (ap = sp->fts_array, p = head; p; p = p->fts_link)
		*ap++ = p;
	qsort(sp->fts_array, nitems, sizeof(FTSENT *), fts_compar);
	for (head = *(ap = sp->fts_array); --nitems; ++ap)
		ap[0]->fts_link = ap[1];
	ap[0]->fts_link = NULL;
	return (head);
}

static FTSENT *
fts_alloc(FTS *sp, char const *name, size_t namelen, wchar_t const *wcsname, size_t cwcname)
{
	struct _fts_private *priv = (struct _fts_private *)sp;
	FTSENT *p;
	size_t len;
#ifdef FTS_WITH_ALLOC_CACHE
	size_t aligned;
	size_t idx;
#endif

#if defined(FTS_WITH_STATISTICS) && defined(FTS_WITH_ALLOC_CACHE)
	priv->allocs++;
#endif
	/*
	 * The file name is a variable length array.  Allocate the FTSENT
	 * structure and the file name.
	 */
	len = sizeof(FTSENT) + (cwcname + 1) * sizeof(wchar_t);
	if (!(sp->fts_options & FTS_NO_ANSI))
		len += namelen + 1;

	/*
	 * To speed things up we cache entries.  This code is a little insane,
	 * but that's preferable to slow code.
	 */
#ifdef FTS_WITH_ALLOC_CACHE
	aligned = (len + FTS_ALIGN_FTSENT + 1) & ~(size_t)(FTS_ALIGN_FTSENT - 1);
	idx     = ((aligned - sizeof(FTSENT)) >> FTS_FREE_BUCKET_SHIFT);
	if (   idx < FTS_NUM_FREE_BUCKETS
	    && (p = priv->freebuckets[idx].head)
	    && p->fts_alloc_size >= len) {
		priv->freebuckets[idx].head = p->fts_link;
		priv->numfree--;
# ifdef FTS_WITH_STATISTICS
		priv->hits++;
# endif

	} else {
# ifdef FTS_WITH_STATISTICS
		priv->misses++;
# endif
		p = malloc(aligned);
		if (p) {
			p->fts_alloc_size = (unsigned)aligned;
		} else {
			nt_fts_free_alloc_cache(sp);
			p = malloc(len);
			if (!p)
				return NULL;
			p->fts_alloc_size = (unsigned)len;
		}
	}
#else  /* !FTS_WITH_ALLOC_CACHE */
	p = malloc(len);
	if (p) {
		p->fts_alloc_size = (unsigned)len;
	} else {
		return NULL;
	}
#endif /* !FTS_WITH_ALLOC_CACHE */

	/* Copy the names and guarantee NUL termination. */
	p->fts_wcsname = (wchar_t *)(p + 1);
	memcpy(p->fts_wcsname, wcsname, cwcname * sizeof(wchar_t));
	p->fts_wcsname[cwcname] = '\0';
	p->fts_cwcname = cwcname;
	if (!(sp->fts_options & FTS_NO_ANSI)) {
		p->fts_name = (char *)(p->fts_wcsname + cwcname + 1);
		memcpy(p->fts_name, name, namelen);
		p->fts_name[namelen] = '\0';
		p->fts_namelen = namelen;
	} else {
		p->fts_name = NULL;
		p->fts_namelen = 0;
	}

	p->fts_path = sp->fts_path;
	p->fts_wcspath = sp->fts_wcspath;
	p->fts_statp = &p->fts_stat;
	p->fts_errno = 0;
	p->fts_flags = 0;
	p->fts_instr = FTS_NOINSTR;
	p->fts_number = 0;
	p->fts_pointer = NULL;
	p->fts_fts = sp;
	p->fts_dirfd = INVALID_HANDLE_VALUE;
	return (p);
}


/**
 * Converts the ANSI name to UTF-16 and calls fts_alloc.
 *
 * @returns Pointer to allocated and mostly initialized FTSENT structure on
 *          success.  NULL on failure, caller needs to record it.
 * @param   sp                  Pointer to FTS instance.
 * @param   name                The ANSI name.
 * @param   namelen             The ANSI name length.
 */
static FTSENT *
fts_alloc_ansi(FTS *sp, char const *name, size_t namelen)
{
	MY_UNICODE_STRING UniStr;
	MY_ANSI_STRING AnsiStr;
	MY_NTSTATUS rcNt;
	FTSENT *pRet;

	UniStr.Buffer = NULL;
	UniStr.MaximumLength = UniStr.Length = 0;

	AnsiStr.Buffer = (char *)name;
	AnsiStr.Length = AnsiStr.MaximumLength = (USHORT)namelen;

	rcNt = g_pfnRtlAnsiStringToUnicodeString(&UniStr, &AnsiStr, TRUE /*fAllocate*/);
	if (NT_SUCCESS(rcNt)) {
		pRet = fts_alloc(sp, name, namelen, UniStr.Buffer, UniStr.Length / sizeof(wchar_t));
		HeapFree(GetProcessHeap(), 0, UniStr.Buffer);
	} else {
		pRet = NULL;
	}
	return pRet;
}


/**
 * Converts the UTF-16 name to ANSI (if necessary) and calls fts_alloc.
 *
 * @returns Pointer to allocated and mostly initialized FTSENT structure on
 *          success.  NULL on failure, caller needs to record it.
 * @param   sp                  Pointer to the FTS instance.
 * @param   wcsname             The UTF-16 name.
 * @param   cwcname		The UTF-16 name length.
 */
static FTSENT *
fts_alloc_utf16(FTS *sp, wchar_t const *wcsname, size_t cwcname)
{
	FTSENT *pRet;

	if (sp->fts_options & FTS_NO_ANSI) {
		pRet = fts_alloc(sp, NULL, 0, wcsname, cwcname);
	} else {
		MY_UNICODE_STRING UniStr;
		MY_ANSI_STRING AnsiStr;
		MY_NTSTATUS rcNt;

		UniStr.Buffer = (wchar_t *)wcsname;
		UniStr.MaximumLength = UniStr.Length = (USHORT)(cwcname * sizeof(wchar_t));

		AnsiStr.Buffer = NULL;
		AnsiStr.Length = AnsiStr.MaximumLength = 0;

		rcNt = g_pfnRtlUnicodeStringToAnsiString(&AnsiStr, &UniStr, TRUE /*fAllocate*/);
		if (NT_SUCCESS(rcNt)) {
			pRet = fts_alloc(sp, AnsiStr.Buffer, AnsiStr.Length, wcsname, cwcname);
			HeapFree(GetProcessHeap(), 0, AnsiStr.Buffer);
		} else {
			pRet = NULL;
		}
	}
	return pRet;
}


/**
 * Frees up the FTSENT allocation cache.
 *
 * Used by nt_fts_close, but also called by fts_alloc on alloc failure.
 *
 * @param   sp                  Pointer to the FTS instance.
 */
static void nt_fts_free_alloc_cache(FTS *sp)
{
#ifdef FTS_WITH_ALLOC_CACHE
	struct _fts_private *priv = (struct _fts_private *)sp;
	unsigned i = K_ELEMENTS(priv->freebuckets);
	while (i-- > 0) {
		FTSENT *cur = priv->freebuckets[i].head;
		priv->freebuckets[i].head = NULL;
		while (cur) {
			FTSENT *freeit = cur;
			cur = cur->fts_link;
			free(freeit);
		}
	}
	priv->numfree = 0;
#else
	(void)sp;
#endif
}


static void
fts_lfree(FTSENT *head)
{
	FTSENT *p;

	/* Free a linked list of structures. */
	while ((p = head)) {
		head = head->fts_link;
		assert(p->fts_dirfd == INVALID_HANDLE_VALUE);
		free(p);
	}
}

/*
 * Allow essentially unlimited paths; find, rm, ls should all work on any tree.
 * Most systems will allow creation of paths much longer than MAXPATHLEN, even
 * though the kernel won't resolve them.  Add the size (not just what's needed)
 * plus 256 bytes so don't realloc the path 2 bytes at a time.
 */
static int
fts_palloc(FTS *sp, size_t more, size_t cwcmore)
{
	void *ptr;

	/** @todo Isn't more and cwcmore minimum buffer sizes rather than what needs
	 *  	  to be added to the buffer??  This code makes no sense when looking at
	 *  	  the way the caller checks things out! */

	if (more) {
		sp->fts_pathlen += more + 256;
		ptr = realloc(sp->fts_path, sp->fts_pathlen);
		if (ptr) {
			sp->fts_path = ptr;
		} else {
			free(sp->fts_path);
			sp->fts_path = NULL;
			free(sp->fts_wcspath);
			sp->fts_wcspath = NULL;
			return 1;
		}
	}

	if (cwcmore) {
		sp->fts_cwcpath += cwcmore + 256;
		ptr = realloc(sp->fts_wcspath, sp->fts_cwcpath);
		if (ptr) {
			sp->fts_wcspath = ptr;
		} else {
			free(sp->fts_path);
			sp->fts_path = NULL;
			free(sp->fts_wcspath);
			sp->fts_wcspath = NULL;
			return 1;
		}
	}
	return 0;
}

/*
 * When the path is realloc'd, have to fix all of the pointers in structures
 * already returned.
 */
static void
fts_padjust(FTS *sp, FTSENT *head)
{
	FTSENT *p;
	char *addr = sp->fts_path;

#define	ADJUST(p) do {							\
	if ((p)->fts_accpath != (p)->fts_name) {			\
		(p)->fts_accpath =					\
		    (char *)addr + ((p)->fts_accpath - (p)->fts_path);	\
	}								\
	(p)->fts_path = addr;						\
} while (0)
	/* Adjust the current set of children. */
	for (p = sp->fts_child; p; p = p->fts_link)
		ADJUST(p);

	/* Adjust the rest of the tree, including the current level. */
	for (p = head; p->fts_level >= FTS_ROOTLEVEL;) {
		ADJUST(p);
		p = p->fts_link ? p->fts_link : p->fts_parent;
	}
}

/*
 * When the UTF-16 path is realloc'd, have to fix all of the pointers in
 * structures already returned.
 */
static void
fts_padjustw(FTS *sp, FTSENT *head)
{
	FTSENT *p;
	wchar_t *addr = sp->fts_wcspath;

#define	ADJUSTW(p) \
		do {	\
			if ((p)->fts_wcsaccpath != (p)->fts_wcsname) \
				(p)->fts_wcsaccpath = addr + ((p)->fts_wcsaccpath - (p)->fts_wcspath); \
			(p)->fts_wcspath = addr;						\
		} while (0)

	/* Adjust the current set of children. */
	for (p = sp->fts_child; p; p = p->fts_link)
		ADJUSTW(p);

	/* Adjust the rest of the tree, including the current level. */
	for (p = head; p->fts_level >= FTS_ROOTLEVEL;) {
		ADJUSTW(p);
		p = p->fts_link ? p->fts_link : p->fts_parent;
	}
}

static size_t
fts_maxarglen(char * const *argv)
{
	size_t len, max;

	for (max = 0; *argv; ++argv)
		if ((len = strlen(*argv)) > max)
			max = len;
	return (max + 1);
}

/** Returns the max string size (including term). */
static size_t
fts_maxarglenw(wchar_t * const *argv)
{
	size_t max = 0;
	for (; *argv; ++argv) {
		size_t len = wcslen(*argv);
		if (len > max)
			max = len;
	}
	return max + 1;
}

