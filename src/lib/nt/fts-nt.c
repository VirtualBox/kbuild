/* $Id: $ */
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

static FTSENT	*fts_alloc(FTS *, char *, size_t);
static FTSENT	*fts_build(FTS *, int);
static void	 fts_lfree(FTSENT *);
static void	 fts_load(FTS *, FTSENT *);
static size_t	 fts_maxarglen(char * const *);
static void	 fts_padjust(FTS *, FTSENT *);
static int	 fts_palloc(FTS *, size_t);
static FTSENT	*fts_sort(FTS *, FTSENT *, size_t);
static int	 fts_stat(FTS *, FTSENT *, int, HANDLE);
static int   fts_process_stats(FTSENT *, BirdStat_T const *);

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

#define AT_SYMLINK_NOFOLLOW 1
#define fstatat(hDir, pszPath, pStat, fFlags) birdStatAt((hDir), (pszPath), (pStat), (fFlags) != AT_SYMLINK_NOFOLLOW)
#define FTS_NT_DUMMY_SYMFD_VALUE 	((HANDLE)~(intptr_t)(2)) /* current process */

/*
 * Internal representation of an FTS, including extra implementation
 * details.  The FTS returned from fts_open points to this structure's
 * ftsp_fts member (and can be cast to an _fts_private as required)
 */
struct _fts_private {
	FTS		ftsp_fts;
};


FTS * FTSCALL
fts_open(char * const *argv, int options,
    int (*compar)(const FTSENT * const *, const FTSENT * const *))
{
	struct _fts_private *priv;
	FTS *sp;
	FTSENT *p, *root;
	FTSENT *parent, *tmp;
	size_t len, nitems;

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
	if (fts_palloc(sp, MAX(fts_maxarglen(argv), MAXPATHLEN)))
		goto mem1;

	/* Allocate/initialize root's parent. */
	if ((parent = fts_alloc(sp, "", 0)) == NULL)
		goto mem2;
	parent->fts_level = FTS_ROOTPARENTLEVEL;

	/* Allocate/initialize root(s). */
	for (root = NULL, nitems = 0; *argv != NULL; ++argv, ++nitems) {
		/* NT: We need to do some small input transformations to make this and
		       the API user code happy.  1. Lone drive letters get a dot
		       appended so it won't matter if a slash is appended afterwards.
			   2. DOS slashes are converted to UNIX ones. */
		char *slash;
		len = strlen(*argv);
		if (len == 2 && argv[0][1] == ':') {
			char tmp[4];
			tmp[0] = argv[0][0];
			tmp[1] = ':';
			tmp[2] = '.';
			tmp[3] = '\0';
			p = fts_alloc(sp, tmp, 3);
		} else {
			p = fts_alloc(sp, *argv, len);
		}
#if 1 /* bird */
		if (p != NULL) { /* likely */ } else { goto mem3; }
#endif
		slash = strchr(p->fts_name, '\\');
		while (slash != NULL) {
			*slash++ = '/';
			slash = strchr(p->fts_name, '\\');
		}
		p->fts_level = FTS_ROOTLEVEL;
		p->fts_parent = parent;
		p->fts_accpath = p->fts_name;
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
	if ((sp->fts_cur = fts_alloc(sp, "", 0)) == NULL)
		goto mem3;
	sp->fts_cur->fts_link = root;
	sp->fts_cur->fts_info = FTS_INIT;

	return (sp);

mem3:	fts_lfree(root);
	free(parent);
mem2:	free(sp->fts_path);
mem1:	free(sp);
	return (NULL);
}


static void
fts_load(FTS *sp, FTSENT *p)
{
	size_t len;
	char *cp;

	/*
	 * Load the stream structure for the next traversal.  Since we don't
	 * actually enter the directory until after the preorder visit, set
	 * the fts_accpath field specially so the chdir gets done to the right
	 * place and the user can access the first node.  From fts_open it's
	 * known that the path will fit.
	 */
	len = p->fts_pathlen = p->fts_namelen;
	memmove(sp->fts_path, p->fts_name, len + 1);
/** @todo check for ':' and '\\'? */
	if ((cp = strrchr(p->fts_name, '/')) && (cp != p->fts_name || cp[1])) {
		len = strlen(++cp);
		memmove(p->fts_name, cp, len + 1);
		p->fts_namelen = len;
	}
	p->fts_accpath = p->fts_path = sp->fts_path;
	sp->fts_dev = p->fts_dev;
}

int FTSCALL
fts_close(FTS *sp)
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

	/* Free up the stream pointer. */
	free(sp);
	return (0);
}

/*
 * Special case of "/" at the end of the path so that slashes aren't
 * appended which would cause paths to be written as "....//foo".
 */
#define	NAPPEND(p)							\
	(p->fts_path[p->fts_pathlen - 1] == '/'				\
	    ? p->fts_pathlen - 1 : p->fts_pathlen)

static void
fts_free_entry(FTSENT *tmp)
{
    if (tmp != NULL) {
		if (tmp->fts_dirfd != INVALID_HANDLE_VALUE) {
			birdCloseFile(tmp->fts_dirfd);
			tmp->fts_dirfd = INVALID_HANDLE_VALUE;
		}
		free(tmp);
    }
}

FTSENT * FTSCALL
fts_read(FTS *sp)
{
	FTSENT *p, *tmp;
	int instr;
	char *t;

	/* If finished or unrecoverable error, return NULL. */
	if (sp->fts_cur == NULL || ISSET(FTS_STOP))
		return (NULL);

	/* Set current node pointer. */
	p = sp->fts_cur;

	/* Save and zero out user instructions. */
	instr = p->fts_instr;
	p->fts_instr = FTS_NOINSTR;

	/* Any type of file may be re-visited; re-stat and re-turn. */
	if (instr == FTS_AGAIN) {
		p->fts_info = fts_stat(sp, p, 0, INVALID_HANDLE_VALUE);
		return (p);
	}

	/*
	 * Following a symlink -- SLNONE test allows application to see
	 * SLNONE and recover.  If indirecting through a symlink, have
	 * keep a pointer to current location.  If unable to get that
	 * pointer, follow fails.
	 *
	 * NT: Since we don't change directory, we just set fts_symfd to a
	 *     placeholder value handle value here in case a API client
	 *     checks it. Ditto FTS_SYMFOLLOW.
	 */
	if (instr == FTS_FOLLOW &&
	    (p->fts_info == FTS_SL || p->fts_info == FTS_SLNONE)) {
		p->fts_info = fts_stat(sp, p, 1, INVALID_HANDLE_VALUE);
		if (p->fts_info == FTS_D /*&& !ISSET(FTS_NOCHDIR)*/) {
			p->fts_symfd = FTS_NT_DUMMY_SYMFD_VALUE;
			p->fts_flags |= FTS_SYMFOLLOW;
		}
		return (p);
	}

	/* Directory in pre-order. */
	if (p->fts_info == FTS_D) {
		/* If skipped or crossed mount point, do post-order visit. */
		if (instr == FTS_SKIP ||
		    (ISSET(FTS_XDEV) && p->fts_dev != sp->fts_dev)) {
			if (p->fts_flags & FTS_SYMFOLLOW) {
				p->fts_symfd = INVALID_HANDLE_VALUE;
			}
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
		if (sp->fts_child != NULL) {
			/* nothing to do */
		} else if ((sp->fts_child = fts_build(sp, BREAD)) == NULL) {
			if (ISSET(FTS_STOP))
				return (NULL);
			return (p);
		}
		p = sp->fts_child;
		sp->fts_child = NULL;
		goto name;
	}

	/* Move to the next node on this level. */
next:	tmp = p;
	if ((p = p->fts_link) != NULL) {
		/*
		 * If reached the top, return to the original directory (or
		 * the root of the tree), and load the paths for the next root.
		 */
		if (p->fts_level == FTS_ROOTLEVEL) {
			fts_free_entry(tmp);
			fts_load(sp, p);
			return (sp->fts_cur = p);
		}

		/*
		 * User may have called fts_set on the node.  If skipped,
		 * ignore.  If followed, get a file descriptor so we can
		 * get back if necessary.
		 */
		if (p->fts_instr == FTS_SKIP) {
			fts_free_entry(tmp);
			goto next;
		}
		if (p->fts_instr == FTS_FOLLOW) {
			p->fts_info = fts_stat(sp, p, 1, INVALID_HANDLE_VALUE);
			/* NT: See above regarding fts_symfd. */
			if (p->fts_info == FTS_D /*&& !ISSET(FTS_NOCHDIR)*/) {
				p->fts_symfd = FTS_NT_DUMMY_SYMFD_VALUE;
				p->fts_flags |= FTS_SYMFOLLOW;
			}
			p->fts_instr = FTS_NOINSTR;
		}

		fts_free_entry(tmp);

name:		t = sp->fts_path + NAPPEND(p->fts_parent);
		*t++ = '/';
		memmove(t, p->fts_name, p->fts_namelen + 1);
		return (sp->fts_cur = p);
	}

	/* Move up to the parent node. */
	p = tmp->fts_parent;

	if (p->fts_level == FTS_ROOTPARENTLEVEL) {
		/*
		 * Done; free everything up and set errno to 0 so the user
		 * can distinguish between error and EOF.
		 */
		fts_free_entry(tmp);
		fts_free_entry(p);
		errno = 0;
		return (sp->fts_cur = NULL);
	}

	/* NUL terminate the pathname. */
	sp->fts_path[p->fts_pathlen] = '\0';

	/*
	 * Return to the parent directory.  If at a root node or came through
	 * a symlink, go back through the file descriptor.  Otherwise, cd up
	 * one directory.
	 *
	 * NT: We're doing no fchdir, but we need to close the directory handle
	 *     and clear fts_symfd now.
	 */
	if (p->fts_flags & FTS_SYMFOLLOW) {
		p->fts_symfd = INVALID_HANDLE_VALUE;
	}
    if (p->fts_dirfd != INVALID_HANDLE_VALUE) {
		birdCloseFile(p->fts_dirfd);
		p->fts_dirfd = INVALID_HANDLE_VALUE;
    }
    fts_free_entry(tmp);
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
fts_set(FTS *sp, FTSENT *p, int instr)
{
	if (instr != 0 && instr != FTS_AGAIN && instr != FTS_FOLLOW &&
	    instr != FTS_NOINSTR && instr != FTS_SKIP) {
		errno = EINVAL;
		return (1);
	}
	p->fts_instr = instr;
	return (0);
}

#if 0
FTSENT * FTSCALL
fts_children(FTS *sp, int instr)
{
	FTSENT *p;
	int fd, rc, serrno;

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
	if (sp->fts_child != NULL)
		fts_lfree(sp->fts_child);

	if (instr == FTS_NAMEONLY) {
		SET(FTS_NAMEONLY);
		instr = BNAMES;
	} else
		instr = BCHILD;

	/*
	 * If using chdir on a relative path and called BEFORE fts_read does
	 * its chdir to the root of a traversal, we can lose -- we need to
	 * chdir into the subdirectory, and we don't know where the current
	 * directory is, so we can't get back so that the upcoming chdir by
	 * fts_read will work.
	 */
	if (p->fts_level != FTS_ROOTLEVEL || p->fts_accpath[0] == '/' ||
	    ISSET(FTS_NOCHDIR))
		return (sp->fts_child = fts_build(sp, instr));

	if ((fd = _open(".", O_RDONLY | O_CLOEXEC, 0)) < 0)
		return (NULL);
	sp->fts_child = fts_build(sp, instr);
	serrno = (sp->fts_child == NULL) ? errno : 0;
	rc = fchdir(fd);
	if (rc < 0 && serrno == 0)
		serrno = errno;
	(void)_close(fd);
	errno = serrno;
	if (rc < 0)
		return (NULL);
	return (sp->fts_child);
}
#endif /* PORTME */

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
fts_set_clientptr(FTS *sp, void *clientptr)
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
	BirdDirEntry_T *dp;
	FTSENT *p, *head;
	FTSENT *cur, *tail;
	DIR *dirp;
	void *oldaddr;
	char *cp;
	int saved_errno, doadjust;
	long level;
	size_t dnamlen, len, maxlen, nitems;
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
			cur->fts_dirfd = birdOpenFileEx(cur->fts_parent->fts_dirfd,
											cur->fts_name,
											FILE_READ_DATA | SYNCHRONIZE,
											FILE_ATTRIBUTE_NORMAL,
											FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
											FILE_OPEN,
											FILE_DIRECTORY_FILE | FILE_OPEN_FOR_BACKUP_INTENT | FILE_SYNCHRONOUS_IO_NONALERT,
											OBJ_CASE_INSENSITIVE);
		} else {
			cur->fts_dirfd = birdOpenFile(cur->fts_accpath,
										  FILE_READ_DATA | SYNCHRONIZE,
										  FILE_ATTRIBUTE_NORMAL,
										  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
										  FILE_OPEN,
										  FILE_DIRECTORY_FILE | FILE_OPEN_FOR_BACKUP_INTENT | FILE_SYNCHRONOUS_IO_NONALERT,
										  OBJ_CASE_INSENSITIVE);
		}
    } else {
		fDirOpenFlags |= BIRDDIR_F_RESTART_SCAN;
	}
	dirp = birdDirOpenFromHandle(cur->fts_dirfd, NULL, fDirOpenFlags);
	if (dirp == NULL) {
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
	 *
	 * If not changing directories set a pointer so that can just append
	 * each new name into the path.
	 */
	len = NAPPEND(cur);
	cp = sp->fts_path + len;
	*cp++ = '/';
	len++;
	maxlen = sp->fts_pathlen - len;

	level = cur->fts_level + 1;

	/* Read the directory, attaching each entry to the `link' pointer. */
	doadjust = 0;
	for (head = tail = NULL, nitems = 0; dirp && (dp = birdDirRead(dirp));) {
		dnamlen = dp->d_namlen;
		if (!ISSET(FTS_SEEDOT) && ISDOT(dp->d_name))
			continue;

		if ((p = fts_alloc(sp, dp->d_name, dnamlen)) == NULL)
			goto mem1;
		if (dnamlen >= maxlen) {	/* include space for NUL */
			oldaddr = sp->fts_path;
			if (fts_palloc(sp, dnamlen + len + 1)) {
				/*
				 * No more memory for path or structures.  Save
				 * errno, free up the current structure and the
				 * structures already allocated.
				 */
mem1:				saved_errno = errno;
				if (p)
					free(p);
				fts_lfree(head);
				birdDirClose(dirp);
				birdCloseFile(cur->fts_dirfd);
				cur->fts_dirfd = INVALID_HANDLE_VALUE;
				cur->fts_info = FTS_ERR;
				SET(FTS_STOP);
				errno = saved_errno;
				return (NULL);
			}
			/* Did realloc() change the pointer? */
			if (oldaddr != sp->fts_path) {
				doadjust = 1;
				if (1 /*ISSET(FTS_NOCHDIR)*/)
					cp = sp->fts_path + len;
			}
			maxlen = sp->fts_pathlen - len;
		}

		p->fts_level = level;
		p->fts_parent = sp->fts_cur;
		p->fts_pathlen = len + dnamlen;
		p->fts_accpath = p->fts_path;
		p->fts_stat = dp->d_stat;
		p->fts_info = fts_process_stats(p, &dp->d_stat);

		/* We walk in directory order so "ls -f" doesn't get upset. */
		p->fts_link = NULL;
		if (head == NULL)
			head = tail = p;
		else {
			tail->fts_link = p;
			tail = p;
		}
		++nitems;
	}

	birdDirClose(dirp);

	/*
	 * If realloc() changed the address of the path, adjust the
	 * addresses for the rest of the tree and the dir list.
	 */
	if (doadjust)
		fts_padjust(sp, head);

	/*
	 * Reset the path back to original state.
	 */
	sp->fts_path[cur->fts_pathlen] = '\0';

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
	const char *path;

	if (dfd == INVALID_HANDLE_VALUE) {
		path = p->fts_accpath;
	} else {
		path = p->fts_name;
	}

	/*
	 * If doing a logical walk, or application requested FTS_FOLLOW, do
	 * a stat(2).  If that fails, check for a non-existent symlink.  If
	 * fail, set the errno from the stat call.
	 */
	if (ISSET(FTS_LOGICAL) || follow) {
		if (fstatat(dfd, path, &p->fts_stat, 0)) {
			saved_errno = errno;
			if (fstatat(dfd, path, &p->fts_stat, AT_SYMLINK_NOFOLLOW)) {
				p->fts_errno = saved_errno;
				goto err;
			}
			errno = 0;
			if (S_ISLNK(p->fts_stat.st_mode))
				return (FTS_SLNONE);
		}
	} else if (fstatat(dfd, path, &p->fts_stat, AT_SYMLINK_NOFOLLOW)) {
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

		if (ISDOT(p->fts_name))
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
fts_alloc(FTS *sp, char *name, size_t namelen)
{
	FTSENT *p;
	size_t len;

	struct ftsent_withstat {
		FTSENT	ent;
		struct	stat statbuf;
	};

	/*
	 * The file name is a variable length array.  Allocate the FTSENT
	 * structure and the file name.
	 */
	len = sizeof(FTSENT) + namelen + 1;
	if ((p = malloc(len)) == NULL)
		return (NULL);

	p->fts_name = (char *)(p + 1);
	p->fts_statp = &p->fts_stat;

	/* Copy the name and guarantee NUL termination. */
	memcpy(p->fts_name, name, namelen);
	p->fts_name[namelen] = '\0';
	p->fts_namelen = namelen;
	p->fts_path = sp->fts_path;
	p->fts_errno = 0;
	p->fts_flags = 0;
	p->fts_instr = FTS_NOINSTR;
	p->fts_number = 0;
	p->fts_pointer = NULL;
	p->fts_fts = sp;
	p->fts_symfd = INVALID_HANDLE_VALUE;
	p->fts_dirfd = INVALID_HANDLE_VALUE;
	return (p);
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
fts_palloc(FTS *sp, size_t more)
{
	void *ptr;

	sp->fts_pathlen += more + 256;
	ptr = realloc(sp->fts_path, sp->fts_pathlen);
	if (ptr) {
		/*likely */
	} else {
		free(sp->fts_path);
	}
	sp->fts_path = ptr;
	return (ptr == NULL);
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

static size_t
fts_maxarglen(char * const *argv)
{
	size_t len, max;

	for (max = 0; *argv; ++argv)
		if ((len = strlen(*argv)) > max)
			max = len;
	return (max + 1);
}

