/* $NetBSD: test.c,v 1.33 2007/06/24 18:54:58 christos Exp $ */

/*
 * test(1); version 7-like  --  author Erik Baalbergen
 * modified by Eric Gisin to be used as built-in.
 * modified by Arnold Robbins to add SVR3 compatibility
 * (-x -c -b -p -u -g -k) plus Korn's -L -nt -ot -ef and new -S (socket).
 * modified by J.T. Conklin for NetBSD.
 *
 * This program is in the Public Domain.
 */

/*#include <sys/cdefs.h>
#ifndef lint
__RCSID("$NetBSD: test.c,v 1.33 2007/06/24 18:54:58 christos Exp $");
#endif*/

#include <sys/stat.h>
#include <sys/types.h>

#include <ctype.h>
#include "err.h"
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _MSC_VER
# include <direct.h>
# include <io.h>
# include <process.h>
# include "mscfakes.h"
#else
# include <unistd.h>
#endif
#include <stdarg.h>
#include <sys/stat.h>

#include "kmkbuiltin.h"

#ifndef __arraycount
# define __arraycount(a) 	( sizeof(a) / sizeof(a[0]) )
#endif


/* test(1) accepts the following grammar:
	oexpr	::= aexpr | aexpr "-o" oexpr ;
	aexpr	::= nexpr | nexpr "-a" aexpr ;
	nexpr	::= primary | "!" primary
	primary	::= unary-operator operand
		| operand binary-operator operand
		| operand
		| "(" oexpr ")"
		;
	unary-operator ::= "-r"|"-w"|"-x"|"-f"|"-d"|"-c"|"-b"|"-p"|
		"-u"|"-g"|"-k"|"-s"|"-t"|"-z"|"-n"|"-o"|"-O"|"-G"|"-L"|"-S";

	binary-operator ::= "="|"!="|"-eq"|"-ne"|"-ge"|"-gt"|"-le"|"-lt"|
			"-nt"|"-ot"|"-ef";
	operand ::= <any legal UNIX file name>
*/

enum token {
	EOI,
	FILRD,
	FILWR,
	FILEX,
	FILEXIST,
	FILREG,
	FILDIR,
	FILCDEV,
	FILBDEV,
	FILFIFO,
	FILSOCK,
	FILSYM,
	FILGZ,
	FILTT,
	FILSUID,
	FILSGID,
	FILSTCK,
	FILNT,
	FILOT,
	FILEQ,
	FILUID,
	FILGID,
	STREZ,
	STRNZ,
	STREQ,
	STRNE,
	STRLT,
	STRGT,
	INTEQ,
	INTNE,
	INTGE,
	INTGT,
	INTLE,
	INTLT,
	UNOT,
	BAND,
	BOR,
	LPAREN,
	RPAREN,
	OPERAND
};

enum token_types {
	UNOP,
	BINOP,
	BUNOP,
	BBINOP,
	PAREN
};

struct t_op {
	const char *op_text;
	short op_num, op_type;
};

static const struct t_op cop[] = {
	{"!",	UNOT,	BUNOP},
	{"(",	LPAREN,	PAREN},
	{")",	RPAREN,	PAREN},
	{"<",	STRLT,	BINOP},
	{"=",	STREQ,	BINOP},
	{">",	STRGT,	BINOP},
};

static const struct t_op cop2[] = {
	{"!=",	STRNE,	BINOP},
};

static const struct t_op mop3[] = {
	{"ef",	FILEQ,	BINOP},
	{"eq",	INTEQ,	BINOP},
	{"ge",	INTGE,	BINOP},
	{"gt",	INTGT,	BINOP},
	{"le",	INTLE,	BINOP},
	{"lt",	INTLT,	BINOP},
	{"ne",	INTNE,	BINOP},
	{"nt",	FILNT,	BINOP},
	{"ot",	FILOT,	BINOP},
};

static const struct t_op mop2[] = {
	{"G",	FILGID,	UNOP},
	{"L",	FILSYM,	UNOP},
	{"O",	FILUID,	UNOP},
	{"S",	FILSOCK,UNOP},
	{"a",	BAND,	BBINOP},
	{"b",	FILBDEV,UNOP},
	{"c",	FILCDEV,UNOP},
	{"d",	FILDIR,	UNOP},
	{"e",	FILEXIST,UNOP},
	{"f",	FILREG,	UNOP},
	{"g",	FILSGID,UNOP},
	{"h",	FILSYM,	UNOP},		/* for backwards compat */
	{"k",	FILSTCK,UNOP},
	{"n",	STRNZ,	UNOP},
	{"o",	BOR,	BBINOP},
	{"p",	FILFIFO,UNOP},
	{"r",	FILRD,	UNOP},
	{"s",	FILGZ,	UNOP},
	{"t",	FILTT,	UNOP},
	{"u",	FILSUID,UNOP},
	{"w",	FILWR,	UNOP},
	{"x",	FILEX,	UNOP},
	{"z",	STREZ,	UNOP},
};

static char **t_wp;
static struct t_op const *t_wp_op;

static int syntax(const char *, const char *);
static int oexpr(enum token);
static int aexpr(enum token);
static int nexpr(enum token);
static int primary(enum token);
static int binop(void);
static int test_access(struct stat *, mode_t);
static int filstat(char *, enum token);
static enum token t_lex(char *);
static int isoperand(void);
static int getn(const char *);
static int newerf(const char *, const char *);
static int olderf(const char *, const char *);
static int equalf(const char *, const char *);
static int usage(const char *);

#if !defined(kmk_builtin_test) || defined(ELECTRIC_HEAP)
extern void *xmalloc(unsigned int);
#else
extern void *xmalloc(unsigned int sz)
{
    void *p = malloc(sz);
    if (!p) {
	    fprintf(stderr, "%s: malloc(%u) failed\n", g_progname, sz);
	    exit(1);
    }
    return p;
}
#endif

int kmk_builtin_test(int argc, char **argv, char **envp, char ***ppapszArgvSpawn)
{
	int res;
	char **argv_spawn;
	int i;

	g_progname = argv[0];

	/* look for the '--', '--help' and '--version'. */
	argv_spawn = NULL;
	for (i = 1; i < argc; i++) {
		if (   argv[i][0] == '-'
		    && argv[i][1] == '-') {
    			if (argv[i][2] == '\0') {
				argc = i;
				argv[i] = NULL;

                                /* skip blank arguments (happens inside kmk) */
                                while (argv[++i]) {
                                        const char *psz = argv[i];
                                        while (isspace(*psz))
                                                psz++;
                                        if (*psz)
                                                break;
                                }
				argv_spawn = &argv[i];
				break;
			}
			if (!strcmp(argv[i], "--help"))
				return usage(argv[0]);
			if (!strcmp(argv[i], "--version"))
				return kbuild_version(argv[0]);
		}
	}

	/* are we '['? then check for ']'. */
	if (strcmp(g_progname, "[") == 0) { /** @todo should skip the path in g_progname */
		if (strcmp(argv[--argc], "]"))
			return errx(1, "missing ]");
		argv[argc] = NULL;
	}

	/* evaluate the expression */
	if (argc < 2)
		res = 1;
	else {
		t_wp = &argv[1];
		res = oexpr(t_lex(*t_wp));
		if (res != -42 && *t_wp != NULL && *++t_wp != NULL)
			res = syntax(*t_wp, "unexpected operator");
		if (res == -42)
			return 1; /* don't mix syntax errors with the argv_spawn ignore */
		res = !res;
	}

	/* anything to execute on success? */
	if (argv_spawn) {
		if (res != 0 || !argv_spawn[0])
			res = 0; /* ignored */
		else {
#ifdef kmk_builtin_test
			/* try exec the specified process */
# if defined(_MSC_VER)
			res = _spawnvp(_P_WAIT, argv_spawn[0], argv_spawn);
			if (res == -1)
			    res = err(1, "_spawnvp(_P_WAIT,%s,..)", argv_spawn[i]);
# else
			execvp(argv_spawn[i], &argv_spawn[i]);
			res = err(1, "execvp(%s,..)", argv_spawn[i]);
# endif
#else /* in kmk */
			/* let job.c spawn the process, make a job.c style argv_spawn copy. */
			char *buf, *cur, **argv_new;
			size_t sz = 0;
			int argc_new = 0;
			while (argv_spawn[argc_new]) {
    				size_t len = strlen(argv_spawn[argc_new]) + 1;
				sz += (len + sizeof(void *) - 1) & ~(sizeof(void *) - 1);
				argc_new++;
			}

			argv_new = xmalloc((argc_new + 1) * sizeof(char *));
			buf = cur = xmalloc(sz);
			for (i = 0; i < argc_new; i++) {
				size_t len = strlen(argv_spawn[i]) + 1;
				argv_new[i] = memcpy(cur, argv_spawn[i], len);
				cur += (len + sizeof(void *) - 1) & ~(sizeof(void *) - 1);
			}
			argv_new[i] = NULL;

			*ppapszArgvSpawn = argv_new;
			res = 0;
#endif /* in kmk */
		}
	}

	return res;
}

static int
syntax(const char *op, const char *msg)
{

	if (op && *op)
		errx(1, "%s: %s", op, msg);
	else
		errx(1, "%s", msg);
	return -42;
}

static int
oexpr(enum token n)
{
	int res;

	res = aexpr(n);
	if (res == -42 || *t_wp == NULL)
		return res;
	if (t_lex(*++t_wp) == BOR) {
		int res2 = oexpr(t_lex(*++t_wp));
		return res2 != -42 ? res2 || res : res2;
	}
	t_wp--;
	return res;
}

static int
aexpr(enum token n)
{
	int res;

	res = nexpr(n);
	if (res == -42 || *t_wp == NULL)
		return res;
	if (t_lex(*++t_wp) == BAND) {
		int res2 = aexpr(t_lex(*++t_wp));
		return res2 != -42 ? res2 && res : res2;
	}
	t_wp--;
	return res;
}

static int
nexpr(enum token n)
{
	if (n == UNOT) {
		int res = nexpr(t_lex(*++t_wp));
		return res != -42 ? !res : res;
	}
	return primary(n);
}

static int
primary(enum token n)
{
	enum token nn;
	int res;

	if (n == EOI)
		return 0;		/* missing expression */
	if (n == LPAREN) {
		if ((nn = t_lex(*++t_wp)) == RPAREN)
			return 0;	/* missing expression */
		res = oexpr(nn);
		if (res != -42 && t_lex(*++t_wp) != RPAREN)
			return syntax(NULL, "closing paren expected");
		return res;
	}
	if (t_wp_op && t_wp_op->op_type == UNOP) {
		/* unary expression */
		if (*++t_wp == NULL)
			return syntax(t_wp_op->op_text, "argument expected");
		switch (n) {
		case STREZ:
			return strlen(*t_wp) == 0;
		case STRNZ:
			return strlen(*t_wp) != 0;
		case FILTT:
			return isatty(getn(*t_wp));
		default:
			return filstat(*t_wp, n);
		}
	}

	if (t_lex(t_wp[1]), t_wp_op && t_wp_op->op_type == BINOP) {
		return binop();
	}

	return strlen(*t_wp) > 0;
}

static int
binop(void)
{
	const char *opnd1, *opnd2;
	struct t_op const *op;

	opnd1 = *t_wp;
	(void) t_lex(*++t_wp);
	op = t_wp_op;

	if ((opnd2 = *++t_wp) == NULL)
		return syntax(op->op_text, "argument expected");

	switch (op->op_num) {
	case STREQ:
		return strcmp(opnd1, opnd2) == 0;
	case STRNE:
		return strcmp(opnd1, opnd2) != 0;
	case STRLT:
		return strcmp(opnd1, opnd2) < 0;
	case STRGT:
		return strcmp(opnd1, opnd2) > 0;
	case INTEQ:
		return getn(opnd1) == getn(opnd2);
	case INTNE:
		return getn(opnd1) != getn(opnd2);
	case INTGE:
		return getn(opnd1) >= getn(opnd2);
	case INTGT:
		return getn(opnd1) > getn(opnd2);
	case INTLE:
		return getn(opnd1) <= getn(opnd2);
	case INTLT:
		return getn(opnd1) < getn(opnd2);
	case FILNT:
		return newerf(opnd1, opnd2);
	case FILOT:
		return olderf(opnd1, opnd2);
	case FILEQ:
		return equalf(opnd1, opnd2);
	default:
		abort();
		/* NOTREACHED */
#ifdef _MSC_VER
		return -42;
#endif
	}
}

/*
 * The manual, and IEEE POSIX 1003.2, suggests this should check the mode bits,
 * not use access():
 *
 *	True shall indicate only that the write flag is on.  The file is not
 *	writable on a read-only file system even if this test indicates true.
 *
 * Unfortunately IEEE POSIX 1003.1-2001, as quoted in SuSv3, says only:
 *
 *	True shall indicate that permission to read from file will be granted,
 *	as defined in "File Read, Write, and Creation".
 *
 * and that section says:
 *
 *	When a file is to be read or written, the file shall be opened with an
 *	access mode corresponding to the operation to be performed.  If file
 *	access permissions deny access, the requested operation shall fail.
 *
 * and of course access permissions are described as one might expect:
 *
 *     * If a process has the appropriate privilege:
 *
 *        * If read, write, or directory search permission is requested,
 *          access shall be granted.
 *
 *        * If execute permission is requested, access shall be granted if
 *          execute permission is granted to at least one user by the file
 *          permission bits or by an alternate access control mechanism;
 *          otherwise, access shall be denied.
 *
 *   * Otherwise:
 *
 *        * The file permission bits of a file contain read, write, and
 *          execute/search permissions for the file owner class, file group
 *          class, and file other class.
 *
 *        * Access shall be granted if an alternate access control mechanism
 *          is not enabled and the requested access permission bit is set for
 *          the class (file owner class, file group class, or file other class)
 *          to which the process belongs, or if an alternate access control
 *          mechanism is enabled and it allows the requested access; otherwise,
 *          access shall be denied.
 *
 * and when I first read this I thought:  surely we can't go about using
 * open(O_WRONLY) to try this test!  However the POSIX 1003.1-2001 Rationale
 * section for test does in fact say:
 *
 *	On historical BSD systems, test -w directory always returned false
 *	because test tried to open the directory for writing, which always
 *	fails.
 *
 * and indeed this is in fact true for Seventh Edition UNIX, UNIX 32V, and UNIX
 * System III, and thus presumably also for BSD up to and including 4.3.
 *
 * Secondly I remembered why using open() and/or access() are bogus.  They
 * don't work right for detecting read and write permissions bits when called
 * by root.
 *
 * Interestingly the 'test' in 4.4BSD was closer to correct (as per
 * 1003.2-1992) and it was implemented efficiently with stat() instead of
 * open().
 *
 * This was apparently broken in NetBSD around about 1994/06/30 when the old
 * 4.4BSD implementation was replaced with a (arguably much better coded)
 * implementation derived from pdksh.
 *
 * Note that modern pdksh is yet different again, but still not correct, at
 * least not w.r.t. 1003.2-1992.
 *
 * As I think more about it and read more of the related IEEE docs I don't like
 * that wording about 'test -r' and 'test -w' in 1003.1-2001 at all.  I very
 * much prefer the original wording in 1003.2-1992.  It is much more useful,
 * and so that's what I've implemented.
 *
 * (Note that a strictly conforming implementation of 1003.1-2001 is in fact
 * totally useless for the case in question since its 'test -w' and 'test -r'
 * can never fail for root for any existing files, i.e. files for which 'test
 * -e' succeeds.)
 *
 * The rationale for 1003.1-2001 suggests that the wording was "clarified" in
 * 1003.1-2001 to align with the 1003.2b draft.  1003.2b Draft 12 (July 1999),
 * which is the latest copy I have, does carry the same suggested wording as is
 * in 1003.1-2001, with its rationale saying:
 *
 * 	This change is a clarification and is the result of interpretation
 * 	request PASC 1003.2-92 #23 submitted for IEEE Std 1003.2-1992.
 *
 * That interpretation can be found here:
 *
 *   http://www.pasc.org/interps/unofficial/db/p1003.2/pasc-1003.2-23.html
 *
 * Not terribly helpful, unfortunately.  I wonder who that fence sitter was.
 *
 * Worse, IMVNSHO, I think the authors of 1003.2b-D12 have mis-interpreted the
 * PASC interpretation and appear to be gone against at least one widely used
 * implementation (namely 4.4BSD).  The problem is that for file access by root
 * this means that if test '-r' and '-w' are to behave as if open() were called
 * then there's no way for a shell script running as root to check if a file
 * has certain access bits set other than by the grotty means of interpreting
 * the output of 'ls -l'.  This was widely considered to be a bug in V7's
 * "test" and is, I believe, one of the reasons why direct use of access() was
 * avoided in some more recent implementations!
 *
 * I have always interpreted '-r' to match '-w' and '-x' as per the original
 * wording in 1003.2-1992, not the other way around.  I think 1003.2b goes much
 * too far the wrong way without any valid rationale and that it's best if we
 * stick with 1003.2-1992 and test the flags, and not mimic the behaviour of
 * open() since we already know very well how it will work -- existance of the
 * file is all that matters to open() for root.
 *
 * Unfortunately the SVID is no help at all (which is, I guess, partly why
 * we're in this mess in the first place :-).
 *
 * The SysV implementation (at least in the 'test' builtin in /bin/sh) does use
 * access(name, 2) even though it also goes to much greater lengths for '-x'
 * matching the 1003.2-1992 definition (which is no doubt where that definition
 * came from).
 *
 * The ksh93 implementation uses access() for '-r' and '-w' if
 * (euid==uid&&egid==gid), but uses st_mode for '-x' iff running as root.
 * i.e. it does strictly conform to 1003.1-2001 (and presumably 1003.2b).
 */
static int
test_access(struct stat *sp, mode_t stmode)
{
#ifdef _MSC_VER
	/* just pretend to be root for now. */
	stmode = (stmode << 6) | (stmode << 3) | stmode;
	return !!(sp->st_mode & stmode);
#else
	gid_t *groups;
	register int n;
	uid_t euid;
	int maxgroups;

	/*
	 * I suppose we could use access() if not running as root and if we are
	 * running with ((euid == uid) && (egid == gid)), but we've already
	 * done the stat() so we might as well just test the permissions
	 * directly instead of asking the kernel to do it....
	 */
	euid = geteuid();
	if (euid == 0)				/* any bit is good enough */
		stmode = (stmode << 6) | (stmode << 3) | stmode;
 	else if (sp->st_uid == euid)
		stmode <<= 6;
	else if (sp->st_gid == getegid())
		stmode <<= 3;
	else {
		/* XXX stolen almost verbatim from ksh93.... */
		/* on some systems you can be in several groups */
		if ((maxgroups = getgroups(0, NULL)) <= 0)
			maxgroups = NGROUPS_MAX;	/* pre-POSIX system? */
		groups = xmalloc((maxgroups + 1) * sizeof(gid_t));
		n = getgroups(maxgroups, groups);
		while (--n >= 0) {
			if (groups[n] == sp->st_gid) {
				stmode <<= 3;
				break;
			}
		}
		free(groups);
	}

	return !!(sp->st_mode & stmode);
#endif
}

static int
filstat(char *nm, enum token mode)
{
	struct stat s;

	if (mode == FILSYM ? lstat(nm, &s) : stat(nm, &s))
		return 0;

	switch (mode) {
	case FILRD:
		return test_access(&s, S_IROTH);
	case FILWR:
		return test_access(&s, S_IWOTH);
	case FILEX:
		return test_access(&s, S_IXOTH);
	case FILEXIST:
		return 1; /* the successful lstat()/stat() is good enough */
	case FILREG:
		return S_ISREG(s.st_mode);
	case FILDIR:
		return S_ISDIR(s.st_mode);
	case FILCDEV:
#ifdef S_ISCHR
		return S_ISCHR(s.st_mode);
#else
		return 0;
#endif
	case FILBDEV:
#ifdef S_ISBLK
		return S_ISBLK(s.st_mode);
#else
		return 0;
#endif
	case FILFIFO:
#ifdef S_ISFIFO
		return S_ISFIFO(s.st_mode);
#else
		return 0;
#endif
	case FILSOCK:
#ifdef S_ISSOCK
		return S_ISSOCK(s.st_mode);
#else
		return 0;
#endif
	case FILSYM:
#ifdef S_ISLNK
		return S_ISLNK(s.st_mode);
#else
		return 0;
#endif
	case FILSUID:
		return (s.st_mode & S_ISUID) != 0;
	case FILSGID:
		return (s.st_mode & S_ISGID) != 0;
	case FILSTCK:
#ifdef S_ISVTX
		return (s.st_mode & S_ISVTX) != 0;
#else
		return 0;
#endif
	case FILGZ:
		return s.st_size > (off_t)0;
	case FILUID:
		return s.st_uid == geteuid();
	case FILGID:
		return s.st_gid == getegid();
	default:
		return 1;
	}
}

#define VTOC(x)	(const unsigned char *)((const struct t_op *)x)->op_text

static int
compare1(const void *va, const void *vb)
{
	const unsigned char *a = va;
	const unsigned char *b = VTOC(vb);

	return a[0] - b[0];
}

static int
compare2(const void *va, const void *vb)
{
	const unsigned char *a = va;
	const unsigned char *b = VTOC(vb);
	int z = a[0] - b[0];

	return z ? z : (a[1] - b[1]);
}

static struct t_op const *
findop(const char *s)
{
	if (s[0] == '-') {
		if (s[1] == '\0')
			return NULL;
		if (s[2] == '\0')
			return bsearch(s + 1, mop2, __arraycount(mop2),
			    sizeof(*mop2), compare1);
		else if (s[3] != '\0')
			return NULL;
		else
			return bsearch(s + 1, mop3, __arraycount(mop3),
			    sizeof(*mop3), compare2);
	} else {
		if (s[1] == '\0')
			return bsearch(s, cop, __arraycount(cop), sizeof(*cop),
			    compare1);
		else if (strcmp(s, cop2[0].op_text) == 0)
			return cop2;
		else
			return NULL;
	}
}

static enum token
t_lex(char *s)
{
	struct t_op const *op;

	if (s == NULL) {
		t_wp_op = NULL;
		return EOI;
	}

	if ((op = findop(s)) != NULL) {
		if (!((op->op_type == UNOP && isoperand()) ||
		    (op->op_num == LPAREN && *(t_wp+1) == 0))) {
			t_wp_op = op;
			return op->op_num;
		}
	}
	t_wp_op = NULL;
	return OPERAND;
}

static int
isoperand(void)
{
	struct t_op const *op;
	char *s, *t;

	if ((s  = *(t_wp+1)) == 0)
		return 1;
	if ((t = *(t_wp+2)) == 0)
		return 0;
	if ((op = findop(s)) != NULL)
		return op->op_type == BINOP && (t[0] != ')' || t[1] != '\0');
	return 0;
}

/* atoi with error detection */
static int
getn(const char *s)
{
	char *p;
	long r;

	errno = 0;
	r = strtol(s, &p, 10);

	if (errno != 0)
	      return errx(-42, "%s: out of range", s);

	while (isspace((unsigned char)*p))
	      p++;

	if (*p)
	      return errx(-42, "%s: bad number", s);

	return (int) r;
}

static int
newerf(const char *f1, const char *f2)
{
	struct stat b1, b2;

	return (stat(f1, &b1) == 0 &&
		stat(f2, &b2) == 0 &&
		b1.st_mtime > b2.st_mtime);
}

static int
olderf(const char *f1, const char *f2)
{
	struct stat b1, b2;

	return (stat(f1, &b1) == 0 &&
		stat(f2, &b2) == 0 &&
		b1.st_mtime < b2.st_mtime);
}

static int
equalf(const char *f1, const char *f2)
{
	struct stat b1, b2;

	return (stat(f1, &b1) == 0 &&
		stat(f2, &b2) == 0 &&
		b1.st_dev == b2.st_dev &&
		b1.st_ino == b2.st_ino);
}

static int
usage(const char *argv0)
{
	fprintf(stdout,
	        "usage: %s expression [-- <prog> [args]]\n", argv0);
	return 0; /* only used in --help. */
}
