/*	$NetBSD: exec.c,v 1.37 2003/08/07 09:05:31 agc Exp $	*/

/*-
 * Copyright (c) 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Kenneth Almquist.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
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
static char sccsid[] = "@(#)exec.c	8.4 (Berkeley) 6/8/95";
#else
__RCSID("$NetBSD: exec.c,v 1.37 2003/08/07 09:05:31 agc Exp $");
#endif /* not lint */
#endif

#include <sys/types.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

/*
 * When commands are first encountered, they are entered in a hash table.
 * This ensures that a full path search will not have to be done for them
 * on each invocation.
 *
 * We should investigate converting to a linear search, even though that
 * would make the command name "hash" a misnomer.
 */

#include "shell.h"
#include "main.h"
#include "nodes.h"
#include "parser.h"
#include "redir.h"
#include "eval.h"
#include "exec.h"
#include "builtins.h"
#include "var.h"
#include "options.h"
#include "input.h"
#include "output.h"
#include "syntax.h"
#include "memalloc.h"
#include "error.h"
#include "init.h"
#include "mystring.h"
#include "show.h"
#include "jobs.h"
#include "alias.h"
#ifdef __INNOTEK_LIBC__
#include <InnoTekLIBC/backend.h>
#endif
#include "shinstance.h"

//#define CMDTABLESIZE 31		/* should be prime */
//#define ARB 1			/* actual size determined at run time */
//
//
//
//struct tblentry {
//	struct tblentry *next;	/* next entry in hash chain */
//	union param param;	/* definition of builtin function */
//	short cmdtype;		/* index identifying command */
//	char rehash;		/* if set, cd done since entry created */
//	char cmdname[ARB];	/* name of command */
//};
//
//
//STATIC struct tblentry *cmdtable[CMDTABLESIZE];
//STATIC int builtinloc = -1;		/* index in path of %builtin, or -1 */
//int exerrno = 0;			/* Last exec error */


STATIC void tryexec(shinstance *, char *, char **, char **, int);
STATIC void execinterp(shinstance *, char **, char **);
STATIC void printentry(shinstance *, struct tblentry *, int);
STATIC void clearcmdentry(shinstance *, int);
STATIC struct tblentry *cmdlookup(shinstance *, const char *, int);
STATIC void delete_cmd_entry(shinstance *);
#ifdef PC_EXE_EXTS
STATIC int stat_pc_exec_exts(shinstance *, char *fullname, struct stat *st, int has_ext);
#endif


extern char *const parsekwd[];

/*
 * Exec a program.  Never returns.  If you change this routine, you may
 * have to change the find_command routine as well.
 */

SH_NORETURN_1 void
shellexec(shinstance *psh, char **argv, char **envp, const char *path, int idx)
{
	char *cmdname;
	int e;
	const char *argv0 = argv[0];
	int argv0len = (int)strlen(argv0);
	char kmkcmd[48];
#ifdef PC_EXE_EXTS
        int has_ext = argv0len - 4;
        has_ext = has_ext > 0
            && argv0[has_ext] == '.'
            /* use strstr and upper/lower permuated extensions to avoid multiple strcasecmp calls. */
            && strstr("exe;" "Exe;" "EXe;" "EXE;" "ExE;" "eXe;" "eXE;" "exE;"
                      "cmd;" "Cmd;" "CMd;" "CMD;" "CmD;" "cMd;" "cMD;" "cmD;"
                      "com;" "Com;" "COm;" "COM;" "CoM;" "cOm;" "cOM;" "coM;"
                      "bat;" "Bat;" "BAt;" "BAT;" "BaT;" "bAt;" "bAT;" "baT;"
                      "btm;" "Btm;" "BTm;" "BTM;" "BtM;" "bTm;" "bTM;" "btM;",
		      argv0 + has_ext + 1)
               != NULL;
#else
	const int has_ext = 1;
#endif
	TRACE((psh, "shellexec: argv[0]=%s idx=%d\n", argv0, idx));
	if (strchr(argv0, '/') != NULL) {
		cmdname = stalloc(psh, argv0len + 5);
		strcpy(cmdname, argv0);
		tryexec(psh, cmdname, argv, envp, has_ext);
		TRACE((psh, "shellexec: cmdname=%s\n", cmdname));
		stunalloc(psh, cmdname);
		e = errno;
	} else {
		/* Before we search the PATH, transform kmk_builtin_% to kmk_% so we don't
		   need to be too careful mixing internal and external kmk command. */
		if (   argv0len > 12
		    && argv0len < 42
		    && strncmp(argv0, "kmk_builtin_", 12) == 0
		    && strpbrk(argv0 + 12, "./\\-:;<>") == NULL) {
			memcpy(kmkcmd, "kmk_", 4);
			memcpy(&kmkcmd[4], argv0 + 12, argv0len + 1 - 8);
			TRACE((psh, "shellexec: dropped '_builtin' from %s to %s\n", argv0, kmkcmd));
			argv0len -= 8;
			argv0 = kmkcmd;
		}

		e = ENOENT;
		while ((cmdname = padvance(psh, &path, argv0)) != NULL) {
			if (--idx < 0 && psh->pathopt == NULL) {
				tryexec(psh, cmdname, argv, envp, has_ext);
				if (errno != ENOENT && errno != ENOTDIR)
					e = errno;
			}
			stunalloc(psh, cmdname);
		}
	}

	/* Map to POSIX errors */
	switch (e) {
	case EACCES:
		psh->exerrno = 126;
		break;
	case ENOENT:
		psh->exerrno = 127;
		break;
	default:
		psh->exerrno = 2;
		break;
	}
	TRACE((psh, "shellexec failed for '%s', errno %d, suppressint %d\n",
		argv[0], e, psh->suppressint ));
	exerror(psh, EXEXEC, "%s: %s", argv[0], errmsg(psh, e, E_EXEC));
	/* NOTREACHED */
}


STATIC void
tryexec(shinstance *psh, char *cmd, char **argv, char **envp, int has_ext)
{
	int e;
#ifdef EXEC_HASH_BANG_SCRIPT
	char *p;
#endif
#ifdef PC_EXE_EXTS
        /* exploit the effect of stat_pc_exec_exts which adds the
         * correct extentions to the file.
         */
        struct stat st;
        if (!has_ext)
            stat_pc_exec_exts(psh, cmd, &st, 0);
#endif
#if defined(__INNOTEK_LIBC__) && defined(EXEC_HASH_BANG_SCRIPT)
	__libc_Back_gfProcessHandleHashBangScripts = 0;
#endif

#ifdef SYSV
	do {
		sh_execve(psh, cmd, argv, envp);
	} while (errno == EINTR);
#else
	sh_execve(psh, cmd, (const char * const*)argv, (const char * const*)envp);
#endif
	e = errno;
	if (e == ENOEXEC) {
		initshellproc(psh);
		setinputfile(psh, cmd, 0);
		psh->commandname = psh->arg0 = savestr(psh, argv[0]);
#ifdef EXEC_HASH_BANG_SCRIPT
		pgetc(psh); pungetc(psh);		/* fill up input buffer */
		p = psh->parsenextc;
		if (psh->parsenleft > 2 && p[0] == '#' && p[1] == '!') {
			argv[0] = cmd;
			execinterp(psh, argv, envp);
		}
#endif
		setparam(psh, argv + 1);
		exraise(psh, EXSHELLPROC);
	}
	errno = e;
}

#ifdef EXEC_HASH_BANG_SCRIPT

/*
 * Checks if NAME is the (base) name of the shell executable or something
 * very similar.
 */
STATIC int
is_shell_exe_name(const char *name)
{
    return equal(name, "kmk_ash")
        || equal(name, "kmk_sh")
	|| equal(name, "kash")
        || equal(name, "sh");
}

/*
 * Execute an interpreter introduced by "#!", for systems where this
 * feature has not been built into the kernel.  If the interpreter is
 * the shell, return (effectively ignoring the "#!").  If the execution
 * of the interpreter fails, exit.
 *
 * This code peeks inside the input buffer in order to avoid actually
 * reading any input.  It would benefit from a rewrite.
 */

#define NEWARGS 16

STATIC void
execinterp(shinstance *psh, char **argv, char **envp)
{
	int n;
	char *inp;
	char *outp;
	char c;
	char *p;
	char **ap;
	char *newargs[NEWARGS];
	intptr_t i;
	char **ap2;
	char **new;

	/* Split the string into arguments. */
	n = psh->parsenleft - 2;
	inp = psh->parsenextc + 2;
	ap = newargs;
	for (;;) {
		while (--n >= 0 && (*inp == ' ' || *inp == '\t'))
			inp++;
		if (n < 0)
			goto bad;
		if ((c = *inp++) == '\n')
			break;
		if (ap == &newargs[NEWARGS])
bad:		  error(psh, "Bad #! line");
		STARTSTACKSTR(psh, outp);
		do {
			STPUTC(psh, c, outp);
		} while (--n >= 0 && (c = *inp++) != ' ' && c != '\t' && c != '\n');
		STPUTC(psh, '\0', outp);
		n++, inp--;
		*ap++ = grabstackstr(psh, outp);
	}

	/* /usr/bin/env emulation, very common with kash/kmk_ash. */
	i = ap - newargs;
	if (i > 1 && equal(newargs[0], "/usr/bin/env")) {
		if (   !strchr(newargs[1], '=')
		    && newargs[1][0] != '-') {
		    /* shellexec below searches the PATH for us, so just
		       drop /usr/bin/env. */
		    TRACE((psh, "hash bang /usr/bin/env utility, dropping /usr/bin/env\n"));
		    ap--;
		    i--;
		    for (n = 0; n < i; n++)
			    newargs[n] = newargs[n + 1];
		} /* else: complicated invocation */
	}

	/* If the interpreter is the shell or a similar shell, there is
	   no need to exec. */
	if (i == 1) {
		p = strrchr(newargs[0], '/');
		if (!p)
			p = newargs[0];
		if (is_shell_exe_name(p)) {
			TRACE((psh, "hash bang self\n"));
			return;
		}
	}

	/* Combine the two argument lists and exec. */
	i = (char *)ap - (char *)newargs;		/* size in bytes */
	if (i == 0)
		error(psh, "Bad #! line");
	for (ap2 = argv ; *ap2++ != NULL ; );
	new = ckmalloc(psh, i + ((char *)ap2 - (char *)argv));
	ap = newargs, ap2 = new;
	while ((i -= sizeof (char **)) >= 0)
		*ap2++ = *ap++;
	ap = argv;
	while ((*ap2++ = *ap++))
	    /* nothing*/;
	TRACE((psh, "hash bang '%s'\n", new[0]));
	shellexec(psh, new, envp, pathval(psh), 0);
	/* NOTREACHED */
}

#endif /* EXEC_HASH_BANG_SCRIPT */


/*
 * Do a path search.  The variable path (passed by reference) should be
 * set to the start of the path before the first call; padvance will update
 * this value as it proceeds.  Successive calls to padvance will return
 * the possible path expansions in sequence.  If an option (indicated by
 * a percent sign) appears in the path entry then the global variable
 * psh->pathopt will be set to point to it; otherwise psh->pathopt will be set to
 * NULL.
 */

//const char *pathopt;

char *
padvance(shinstance *psh, const char **path, const char *name)
{
	const char *p;
	char *q;
	const char *start;
	int len;

	if (*path == NULL)
		return NULL;
	start = *path;
#ifdef PC_PATH_SEP
	for (p = start ; *p && *p != ';' && *p != '%' ; p++);
#else
	for (p = start ; *p && *p != ':' && *p != '%' ; p++);
#endif
	len = (int)(p - start + strlen(name) + 2);	/* "2" is for '/' and '\0' */
#ifdef PC_EXE_EXTS
        len += 4; /* "4" is for .exe/.com/.cmd/.bat/.btm */
#endif
	while (stackblocksize(psh) < len)
		growstackblock(psh);
	q = stackblock(psh);
	if (p != start) {
		memcpy(q, start, p - start);
		q += p - start;
		*q++ = '/';
	}
	strcpy(q, name);
	psh->pathopt = NULL;
	if (*p == '%') {
		psh->pathopt = ++p;
#ifdef PC_PATH_SEP
		while (*p && *p != ';')  p++;
#else
		while (*p && *p != ':')  p++;
#endif
	}
#ifdef PC_PATH_SEP
	if (*p == ';')
#else
	if (*p == ':')
#endif
		*path = p + 1;
	else
		*path = NULL;
	return stalloc(psh, len);
}


#ifdef PC_EXE_EXTS
STATIC int stat_pc_exec_exts(shinstance *psh, char *fullname, struct stat *st, int has_ext)
{
    /* skip the SYSV crap */
    if (shfile_stat(&psh->fdtab, fullname, st) >= 0)
        return 0;
    if (!has_ext && errno == ENOENT)
    {
        char *psz = strchr(fullname, '\0');
        memcpy(psz, ".exe", 5);
        if (shfile_stat(&psh->fdtab, fullname, st) >= 0)
            return 0;
        if (errno != ENOENT && errno != ENOTDIR)
            return -1;

        memcpy(psz, ".cmd", 5);
        if (shfile_stat(&psh->fdtab, fullname, st) >= 0)
            return 0;
        if (errno != ENOENT && errno != ENOTDIR)
            return -1;

        memcpy(psz, ".bat", 5);
        if (shfile_stat(&psh->fdtab, fullname, st) >= 0)
            return 0;
        if (errno != ENOENT && errno != ENOTDIR)
            return -1;

        memcpy(psz, ".com", 5);
        if (shfile_stat(&psh->fdtab, fullname, st) >= 0)
            return 0;
        if (errno != ENOENT && errno != ENOTDIR)
            return -1;

        memcpy(psz, ".btm", 5);
        if (shfile_stat(&psh->fdtab, fullname, st) >= 0)
            return 0;
        *psz = '\0';
    }
    return -1;
}
#endif /* PC_EXE_EXTS */



/*** Command hashing code ***/


int
hashcmd(shinstance *psh, int argc, char **argv)
{
	struct tblentry **pp;
	struct tblentry *cmdp;
	int c;
	int verbose;
	struct cmdentry entry;
	char *name;

	verbose = 0;
	while ((c = nextopt(psh, "rv")) != '\0') {
		if (c == 'r') {
			clearcmdentry(psh, 0);
		} else if (c == 'v') {
			verbose++;
		}
	}
	if (*psh->argptr == NULL) {
		for (pp = psh->cmdtable ; pp < &psh->cmdtable[CMDTABLESIZE] ; pp++) {
			for (cmdp = *pp ; cmdp ; cmdp = cmdp->next) {
				if (verbose || cmdp->cmdtype == CMDNORMAL)
					printentry(psh, cmdp, verbose);
			}
		}
		return 0;
	}
	while ((name = *psh->argptr) != NULL) {
		if ((cmdp = cmdlookup(psh, name, 0)) != NULL
		 && (cmdp->cmdtype == CMDNORMAL
		     || (cmdp->cmdtype == CMDBUILTIN && psh->builtinloc >= 0)))
			delete_cmd_entry(psh);
		find_command(psh, name, &entry, DO_ERR, pathval(psh));
		if (verbose) {
			if (entry.cmdtype != CMDUNKNOWN) {	/* if no error msg */
				cmdp = cmdlookup(psh, name, 0);
				printentry(psh, cmdp, verbose);
			}
			output_flushall(psh);
		}
		psh->argptr++;
	}
	return 0;
}


STATIC void
printentry(shinstance *psh, struct tblentry *cmdp, int verbose)
{
	int idx;
	const char *path;
	char *name;

	switch (cmdp->cmdtype) {
	case CMDNORMAL:
		idx = cmdp->param.index;
		path = pathval(psh);
		do {
			name = padvance(psh, &path, cmdp->cmdname);
			stunalloc(psh, name);
		} while (--idx >= 0);
		out1str(psh, name);
		break;
	case CMDSPLBLTIN:
		out1fmt(psh, "special builtin %s", cmdp->cmdname);
		break;
	case CMDBUILTIN:
		out1fmt(psh, "builtin %s", cmdp->cmdname);
		break;
	case CMDFUNCTION:
		out1fmt(psh, "function %s", cmdp->cmdname);
		if (verbose) {
			struct procstat ps;
			INTOFF;
			commandtext(psh, &ps, cmdp->param.func);
			INTON;
			out1str(psh, "() { ");
			out1str(psh, ps.cmd);
			out1str(psh, "; }");
		}
		break;
	default:
		error(psh, "internal error: %s cmdtype %d", cmdp->cmdname, cmdp->cmdtype);
	}
	if (cmdp->rehash)
		out1c(psh, '*');
	out1c(psh, '\n');
}



/*
 * Resolve a command name.  If you change this routine, you may have to
 * change the shellexec routine as well.
 */

void
find_command(shinstance *psh, char *name, struct cmdentry *entry, int act, const char *path)
{
	struct tblentry *cmdp, loc_cmd;
	int idx;
	int prev;
	char *fullname;
	struct stat statb;
	int e;
	int (*bltin)(shinstance*,int,char **);
	int argv0len = (int)strlen(name);
	char kmkcmd[48];
#ifdef PC_EXE_EXTS
        int has_ext = argv0len - 4;
        has_ext = has_ext > 0
            && name[has_ext] == '.'
            /* use strstr and upper/lower permuated extensions to avoid multiple strcasecmp calls. */
            && strstr("exe;" "Exe;" "EXe;" "EXE;" "ExE;" "eXe;" "eXE;" "exE;"
                      "cmd;" "Cmd;" "CMd;" "CMD;" "CmD;" "cMd;" "cMD;" "cmD;"
                      "com;" "Com;" "COm;" "COM;" "CoM;" "cOm;" "cOM;" "coM;"
                      "bat;" "Bat;" "BAt;" "BAT;" "BaT;" "bAt;" "bAT;" "baT;"
                      "btm;" "Btm;" "BTm;" "BTM;" "BtM;" "bTm;" "bTM;" "btM;",
                      name + has_ext + 1)
               != NULL;
#endif

	/* If name contains a slash, don't use PATH or hash table */
	if (strchr(name, '/') != NULL) {
		if (act & DO_ABS) {
			while (shfile_stat(&psh->fdtab, name, &statb) < 0) {
#ifdef SYSV
				if (errno == EINTR)
					continue;
#endif
				if (errno != ENOENT && errno != ENOTDIR)
					e = errno;
				entry->cmdtype = CMDUNKNOWN;
				entry->u.index = -1;
				return;
			}
			entry->cmdtype = CMDNORMAL;
			entry->u.index = -1;
			return;
		}
		entry->cmdtype = CMDNORMAL;
		entry->u.index = 0;
		return;
	}

	if (path != pathval(psh))
		act |= DO_ALTPATH;

	if (act & DO_ALTPATH && strstr(path, "%builtin") != NULL)
		act |= DO_ALTBLTIN;

	/* If name is in the table, check answer will be ok */
	if ((cmdp = cmdlookup(psh, name, 0)) != NULL) {
		do {
			switch (cmdp->cmdtype) {
			case CMDNORMAL:
				if (act & DO_ALTPATH) {
					cmdp = NULL;
					continue;
				}
				break;
			case CMDFUNCTION:
				if (act & DO_NOFUNC) {
					cmdp = NULL;
					continue;
				}
				break;
			case CMDBUILTIN:
				if ((act & DO_ALTBLTIN) || psh->builtinloc >= 0) {
					cmdp = NULL;
					continue;
				}
				break;
			}
			/* if not invalidated by cd, we're done */
			if (cmdp->rehash == 0)
				goto success;
		} while (0);
	}

	/* If %builtin not in path, check for builtin next */
	if ((act & DO_ALTPATH ? !(act & DO_ALTBLTIN) : psh->builtinloc < 0) &&
	    (bltin = find_builtin(psh, name)) != 0)
		goto builtin_success;

	/* We have to search path. */
	prev = -1;		/* where to start */
	if (cmdp) {		/* doing a rehash */
		if (cmdp->cmdtype == CMDBUILTIN)
			prev = psh->builtinloc;
		else
			prev = cmdp->param.index;
	}

	/* Before we search the PATH, transform kmk_builtin_% to kmk_% so we don't
	   need to be too careful mixing internal and external kmk command. */
	if (   argv0len > 12
	    && argv0len < (int)sizeof(kmkcmd)
	    && strncmp(name, "kmk_builtin_", 12) == 0
	    && strpbrk(name + 12, "./\\-:;<>") == NULL) {
	        memcpy(kmkcmd, "kmk_", 4);
		memcpy(&kmkcmd[4], name + 12, argv0len + 1 - 8);
		TRACE((psh, "find_command: dropped '_builtin' from %s to %s\n", name, kmkcmd));
		argv0len -= 8;
		name = kmkcmd;
	}

	e = ENOENT;
	idx = -1;
loop:
	while ((fullname = padvance(psh, &path, name)) != NULL) {
		stunalloc(psh, fullname);
		idx++;
		if (psh->pathopt) {
			if (prefix("builtin", psh->pathopt)) {
				if ((bltin = find_builtin(psh, name)) == 0)
					goto loop;
				goto builtin_success;
			} else if (prefix("func", psh->pathopt)) {
				/* handled below */
			} else {
				/* ignore unimplemented options */
				goto loop;
			}
		}
		/* if rehash, don't redo absolute path names */
		if (fullname[0] == '/' && idx <= prev) {
			if (idx < prev)
				goto loop;
			TRACE((psh, "searchexec \"%s\": no change\n", name));
			goto success;
		}
#ifdef PC_EXE_EXTS
		while (stat_pc_exec_exts(psh, fullname, &statb, has_ext) < 0) {
#else
		while (shfile_stat(&psh->fdtab, fullname, &statb) < 0) {
#endif
#ifdef SYSV
			if (errno == EINTR)
				continue;
#endif
			if (errno != ENOENT && errno != ENOTDIR)
				e = errno;

			goto loop;
		}
		e = EACCES;	/* if we fail, this will be the error */
		if (!S_ISREG(statb.st_mode))
			goto loop;
		if (psh->pathopt) {		/* this is a %func directory */
			if (act & DO_NOFUNC)
				goto loop;
			stalloc(psh, strlen(fullname) + 1);
			readcmdfile(psh, fullname);
			if ((cmdp = cmdlookup(psh, name, 0)) == NULL ||
			    cmdp->cmdtype != CMDFUNCTION)
				error(psh, "%s not defined in %s", name, fullname);
			stunalloc(psh, fullname);
			goto success;
		}
#ifdef notdef
		/* XXX this code stops root executing stuff, and is buggy
		   if you need a group from the group list. */
		if (statb.st_uid == sh_geteuid(psh)) {
			if ((statb.st_mode & 0100) == 0)
				goto loop;
		} else if (statb.st_gid == sh_getegid(psh)) {
			if ((statb.st_mode & 010) == 0)
				goto loop;
		} else {
			if ((statb.st_mode & 01) == 0)
				goto loop;
		}
#endif
		TRACE((psh, "searchexec \"%s\" returns \"%s\"\n", name, fullname));
		INTOFF;
		if (act & DO_ALTPATH) {
			stalloc(psh, strlen(fullname) + 1);
			cmdp = &loc_cmd;
		} else
			cmdp = cmdlookup(psh, name, 1);
		cmdp->cmdtype = CMDNORMAL;
		cmdp->param.index = idx;
		INTON;
		goto success;
	}

	/* We failed.  If there was an entry for this command, delete it */
	if (cmdp)
		delete_cmd_entry(psh);
	if (act & DO_ERR)
		outfmt(psh->out2, "%s: %s\n", name, errmsg(psh, e, E_EXEC));
	entry->cmdtype = CMDUNKNOWN;
	return;

builtin_success:
	INTOFF;
	if (act & DO_ALTPATH)
		cmdp = &loc_cmd;
	else
		cmdp = cmdlookup(psh, name, 1);
	if (cmdp->cmdtype == CMDFUNCTION)
		/* DO_NOFUNC must have been set */
		cmdp = &loc_cmd;
	cmdp->cmdtype = CMDBUILTIN;
	cmdp->param.bltin = bltin;
	INTON;
success:
	cmdp->rehash = 0;
	entry->cmdtype = cmdp->cmdtype;
	entry->u = cmdp->param;
}



/*
 * Search the table of builtin commands.
 */

int
(*find_builtin(shinstance *psh, char *name))(shinstance *psh, int, char **)
{
	const struct builtincmd *bp;

	for (bp = builtincmd ; bp->name ; bp++) {
		if (*bp->name == *name && equal(bp->name, name))
			return bp->builtin;
	}
	return 0;
}

int
(*find_splbltin(shinstance *psh, char *name))(shinstance *psh, int, char **)
{
	const struct builtincmd *bp;

	for (bp = splbltincmd ; bp->name ; bp++) {
		if (*bp->name == *name && equal(bp->name, name))
			return bp->builtin;
	}
	return 0;
}

/*
 * At shell startup put special builtins into hash table.
 * ensures they are executed first (see posix).
 * We stop functions being added with the same name
 * (as they are impossible to call)
 */

void
hash_special_builtins(shinstance *psh)
{
	const struct builtincmd *bp;
	struct tblentry *cmdp;

	for (bp = splbltincmd ; bp->name ; bp++) {
		cmdp = cmdlookup(psh, bp->name, 1);
		cmdp->cmdtype = CMDSPLBLTIN;
		cmdp->param.bltin = bp->builtin;
	}
}



/*
 * Called when a cd is done.  Marks all commands so the next time they
 * are executed they will be rehashed.
 */

void
hashcd(shinstance *psh)
{
	struct tblentry **pp;
	struct tblentry *cmdp;

	for (pp = psh->cmdtable ; pp < &psh->cmdtable[CMDTABLESIZE] ; pp++) {
		for (cmdp = *pp ; cmdp ; cmdp = cmdp->next) {
			if (cmdp->cmdtype == CMDNORMAL
			 || (cmdp->cmdtype == CMDBUILTIN && psh->builtinloc >= 0))
				cmdp->rehash = 1;
		}
	}
}



/*
 * Fix command hash table when PATH changed.
 * Called before PATH is changed.  The argument is the new value of PATH;
 * pathval(psh) still returns the old value at this point.
 * Called with interrupts off.
 */

void
changepath(shinstance *psh, const char *newval)
{
	const char *old, *new;
	int idx;
	int firstchange;
	int bltin;

	old = pathval(psh);
	new = newval;
	firstchange = 9999;	/* assume no change */
	idx = 0;
	bltin = -1;
	for (;;) {
		if (*old != *new) {
			firstchange = idx;
#ifdef PC_PATH_SEP
			if ((*old == '\0' && *new == ';')
			 || (*old == ';' && *new == '\0'))
#else
			if ((*old == '\0' && *new == ':')
			 || (*old == ':' && *new == '\0'))
#endif
				firstchange++;
			old = new;	/* ignore subsequent differences */
		}
		if (*new == '\0')
			break;
		if (*new == '%' && bltin < 0 && prefix("builtin", new + 1))
			bltin = idx;
#ifdef PC_PATH_SEP
		if (*new == ';') {
#else
		if (*new == ':') {
#endif
			idx++;
		}
		new++, old++;
	}
	if (psh->builtinloc < 0 && bltin >= 0)
		psh->builtinloc = bltin;		/* zap builtins */
	if (psh->builtinloc >= 0 && bltin < 0)
		firstchange = 0;
	clearcmdentry(psh, firstchange);
	psh->builtinloc = bltin;
}


/*
 * Clear out command entries.  The argument specifies the first entry in
 * PATH which has changed.
 */

STATIC void
clearcmdentry(shinstance *psh, int firstchange)
{
	struct tblentry **tblp;
	struct tblentry **pp;
	struct tblentry *cmdp;

	INTOFF;
	for (tblp = psh->cmdtable ; tblp < &psh->cmdtable[CMDTABLESIZE] ; tblp++) {
		pp = tblp;
		while ((cmdp = *pp) != NULL) {
			if ((cmdp->cmdtype == CMDNORMAL &&
			     cmdp->param.index >= firstchange)
			 || (cmdp->cmdtype == CMDBUILTIN &&
			     psh->builtinloc >= firstchange)) {
				*pp = cmdp->next;
				ckfree(psh, cmdp);
			} else {
				pp = &cmdp->next;
			}
		}
	}
	INTON;
}


/*
 * Delete all functions.
 */

#ifdef mkinit
MKINIT void deletefuncs(struct shinstance *);
MKINIT void hash_special_builtins(struct shinstance *);

INIT {
	hash_special_builtins(psh);
}

SHELLPROC {
	deletefuncs(psh);
}
#endif

void
deletefuncs(shinstance *psh)
{
	struct tblentry **tblp;
	struct tblentry **pp;
	struct tblentry *cmdp;

	INTOFF;
	for (tblp = psh->cmdtable ; tblp < &psh->cmdtable[CMDTABLESIZE] ; tblp++) {
		pp = tblp;
		while ((cmdp = *pp) != NULL) {
			if (cmdp->cmdtype == CMDFUNCTION) {
				*pp = cmdp->next;
				freefunc(psh, cmdp->param.func);
				ckfree(psh, cmdp);
			} else {
				pp = &cmdp->next;
			}
		}
	}
	INTON;
}



/*
 * Locate a command in the command hash table.  If "add" is nonzero,
 * add the command to the table if it is not already present.  The
 * variable "lastcmdentry" is set to point to the address of the link
 * pointing to the entry, so that delete_cmd_entry can delete the
 * entry.
 */

struct tblentry **lastcmdentry;


STATIC struct tblentry *
cmdlookup(shinstance *psh, const char *name, int add)
{
	int hashval;
	const char *p;
	struct tblentry *cmdp;
	struct tblentry **pp;

	p = name;
	hashval = *p << 4;
	while (*p)
		hashval += *p++;
	hashval &= 0x7FFF;
	pp = &psh->cmdtable[hashval % CMDTABLESIZE];
	for (cmdp = *pp ; cmdp ; cmdp = cmdp->next) {
		if (equal(cmdp->cmdname, name))
			break;
		pp = &cmdp->next;
	}
	if (add && cmdp == NULL) {
		INTOFF;
		cmdp = *pp = ckmalloc(psh, sizeof (struct tblentry) - ARB
					+ strlen(name) + 1);
		cmdp->next = NULL;
		cmdp->cmdtype = CMDUNKNOWN;
		cmdp->rehash = 0;
		strcpy(cmdp->cmdname, name);
		INTON;
	}
	lastcmdentry = pp;
	return cmdp;
}

/*
 * Delete the command entry returned on the last lookup.
 */

STATIC void
delete_cmd_entry(shinstance *psh)
{
	struct tblentry *cmdp;

	INTOFF;
	cmdp = *lastcmdentry;
	*lastcmdentry = cmdp->next;
	ckfree(psh, cmdp);
	INTON;
}



#ifdef notdef
void
getcmdentry(shinstance *psh, char *name, struct cmdentry *entry)
{
	struct tblentry *cmdp = cmdlookup(psh, name, 0);

	if (cmdp) {
		entry->u = cmdp->param;
		entry->cmdtype = cmdp->cmdtype;
	} else {
		entry->cmdtype = CMDUNKNOWN;
		entry->u.index = 0;
	}
}
#endif


/*
 * Add a new command entry, replacing any existing command entry for
 * the same name - except special builtins.
 */

STATIC void
addcmdentry(shinstance *psh, char *name, struct cmdentry *entry)
{
	struct tblentry *cmdp;

	INTOFF;
	cmdp = cmdlookup(psh, name, 1);
	if (cmdp->cmdtype != CMDSPLBLTIN) {
		if (cmdp->cmdtype == CMDFUNCTION) {
			freefunc(psh, cmdp->param.func);
		}
		cmdp->cmdtype = entry->cmdtype;
		cmdp->param = entry->u;
	}
	INTON;
}


/*
 * Define a shell function.
 */

void
defun(shinstance *psh, char *name, union node *func)
{
	struct cmdentry entry;

	INTOFF;
	entry.cmdtype = CMDFUNCTION;
	entry.u.func = copyfunc(psh, func);
	addcmdentry(psh, name, &entry);
	INTON;
}


/*
 * Delete a function if it exists.
 */

int
unsetfunc(shinstance *psh, char *name)
{
	struct tblentry *cmdp;

	if ((cmdp = cmdlookup(psh, name, 0)) != NULL &&
	    cmdp->cmdtype == CMDFUNCTION) {
		freefunc(psh, cmdp->param.func);
		delete_cmd_entry(psh);
		return (0);
	}
	return (1);
}

/*
 * Locate and print what a word is...
 * also used for 'command -[v|V]'
 */

int
typecmd(shinstance *psh, int argc, char **argv)
{
	struct cmdentry entry;
	struct tblentry *cmdp;
	char * const *pp;
	struct alias *ap;
	int err = 0;
	char *arg;
	int c;
	int V_flag = 0;
	int v_flag = 0;
	int p_flag = 0;

	while ((c = nextopt(psh, "vVp")) != 0) {
		switch (c) {
		case 'v': v_flag = 1; break;
		case 'V': V_flag = 1; break;
		case 'p': p_flag = 1; break;
		}
	}

	if (p_flag && (v_flag || V_flag))
		error(psh, "cannot specify -p with -v or -V");

	while ((arg = *psh->argptr++)) {
		if (!v_flag)
			out1str(psh, arg);
		/* First look at the keywords */
		for (pp = parsekwd; *pp; pp++)
			if (**pp == *arg && equal(*pp, arg))
				break;

		if (*pp) {
			if (v_flag)
				err = 1;
			else
				out1str(psh, " is a shell keyword\n");
			continue;
		}

		/* Then look at the aliases */
		if ((ap = lookupalias(psh, arg, 1)) != NULL) {
			if (!v_flag)
				out1fmt(psh, " is an alias for \n");
			out1fmt(psh, "%s\n", ap->val);
			continue;
		}

		/* Then check if it is a tracked alias */
		if ((cmdp = cmdlookup(psh, arg, 0)) != NULL) {
			entry.cmdtype = cmdp->cmdtype;
			entry.u = cmdp->param;
		} else {
			/* Finally use brute force */
			find_command(psh, arg, &entry, DO_ABS, pathval(psh));
		}

		switch (entry.cmdtype) {
		case CMDNORMAL: {
			if (strchr(arg, '/') == NULL) {
				const char *path = pathval(psh);
				char *name;
				int j = entry.u.index;
				do {
					name = padvance(psh, &path, arg);
					stunalloc(psh, name);
				} while (--j >= 0);
				if (!v_flag)
					out1fmt(psh, " is%s ",
					    cmdp ? " a tracked alias for" : "");
				out1fmt(psh, "%s\n", name);
			} else {
				if (shfile_access(&psh->fdtab, arg, X_OK) == 0) {
					if (!v_flag)
						out1fmt(psh, " is ");
					out1fmt(psh, "%s\n", arg);
				} else {
					if (!v_flag)
						out1fmt(psh, ": %s\n",
						    sh_strerror(psh, errno));
					else
						err = 126;
				}
			}
 			break;
		}
		case CMDFUNCTION:
			if (!v_flag)
				out1str(psh, " is a shell function\n");
			else
				out1fmt(psh, "%s\n", arg);
			break;

		case CMDBUILTIN:
			if (!v_flag)
				out1str(psh, " is a shell builtin\n");
			else
				out1fmt(psh, "%s\n", arg);
			break;

		case CMDSPLBLTIN:
			if (!v_flag)
				out1str(psh, " is a special shell builtin\n");
			else
				out1fmt(psh, "%s\n", arg);
			break;

		default:
			if (!v_flag)
				out1str(psh, ": not found\n");
			err = 127;
			break;
		}
	}
	return err;
}
