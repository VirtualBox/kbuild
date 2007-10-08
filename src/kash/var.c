/*	$NetBSD: var.c,v 1.36 2004/10/06 10:23:43 enami Exp $	*/

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
static char sccsid[] = "@(#)var.c	8.3 (Berkeley) 5/4/95";
#else
__RCSID("$NetBSD: var.c,v 1.36 2004/10/06 10:23:43 enami Exp $");
#endif /* not lint */
#endif

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#ifdef PC_OS2_LIBPATHS
#define INCL_BASE
#include <os2.h>

#ifndef LIBPATHSTRICT
#define LIBPATHSTRICT 3
#endif

extern APIRET
#ifdef APIENTRY
    APIENTRY
#endif
    DosQueryHeaderInfo(HMODULE hmod, ULONG ulIndex, PVOID pvBuffer, ULONG cbBuffer, ULONG ulSubFunction);
#define QHINF_EXEINFO       1 /* NE exeinfo. */
#define QHINF_READRSRCTBL   2 /* Reads from the resource table. */
#define QHINF_READFILE      3 /* Reads from the executable file. */
#define QHINF_LIBPATHLENGTH 4 /* Gets the libpath length. */
#define QHINF_LIBPATH       5 /* Gets the entire libpath. */
#define QHINF_FIXENTRY      6 /* NE only */
#define QHINF_STE           7 /* NE only */
#define QHINF_MAPSEL        8 /* NE only */

#endif


/*
 * Shell variables.
 */

#include "shell.h"
#include "output.h"
#include "expand.h"
#include "nodes.h"	/* for other headers */
#include "eval.h"	/* defines cmdenviron */
#include "exec.h"
#include "syntax.h"
#include "options.h"
#include "mail.h"
#include "var.h"
#include "memalloc.h"
#include "error.h"
#include "mystring.h"
#include "parser.h"
#include "show.h"
#ifndef SMALL
# include "myhistedit.h"
#endif
#include "shinstance.h"

//#ifdef SMALL
//#define VTABSIZE 39
//#else
//#define VTABSIZE 517
//#endif


struct varinit {
	unsigned var_off;
	int flags;
	const char *text;
	void (*func)(shinstance *, const char *);
};


//#if ATTY
//struct var vatty;
//#endif
//#ifndef SMALL
//struct var vhistsize;
//struct var vterm;
//#endif
//struct var vifs;
//struct var vmail;
//struct var vmpath;
//struct var vpath;
//#ifdef _MSC_VER
//struct var vpath2;
//#endif
//struct var vps1;
//struct var vps2;
//struct var vps4;
//struct var vvers; - unused
//struct var voptind;

#ifdef PC_OS2_LIBPATHS
//static struct var libpath_vars[4];
static const char * const libpath_envs[4] = {"LIBPATH=", "BEGINLIBPATH=", "ENDLIBPATH=", "LIBPATHSTRICT="};
#endif

const struct varinit varinit[] = {
#if ATTY
	{ offsetof(shinstance, vatty),	VSTRFIXED|VTEXTFIXED|VUNSET,	"ATTY=",
	  NULL },
#endif
#ifndef SMALL
	{ offsetof(shinstance, vhistsize),	VSTRFIXED|VTEXTFIXED|VUNSET,	"HISTSIZE=",
	  sethistsize },
#endif
	{ offsetof(shinstance, vifs),	VSTRFIXED|VTEXTFIXED,		"IFS= \t\n",
	  NULL },
	{ offsetof(shinstance, vmail),	VSTRFIXED|VTEXTFIXED|VUNSET,	"MAIL=",
	  NULL },
	{ offsetof(shinstance, vmpath),	VSTRFIXED|VTEXTFIXED|VUNSET,	"MAILPATH=",
	  NULL },
	{ offsetof(shinstance, vpath),	VSTRFIXED|VTEXTFIXED,		"PATH=" _PATH_DEFPATH,
	  changepath },
#ifdef _MSC_VER
	{ offsetof(shinstance, vpath2),	VSTRFIXED|VTEXTFIXED,		"Path=",
	  changepath },
#endif
	/*
	 * vps1 depends on uid
	 */
	{ offsetof(shinstance, vps2),	VSTRFIXED|VTEXTFIXED,		"PS2=> ",
	  NULL },
	{ offsetof(shinstance, vps4),	VSTRFIXED|VTEXTFIXED,		"PS4=+ ",
	  NULL },
#ifndef SMALL
	{ offsetof(shinstance, vterm),	VSTRFIXED|VTEXTFIXED|VUNSET,	"TERM=",
	  setterm },
#endif
	{ offsetof(shinstance, voptind),	VSTRFIXED|VTEXTFIXED|VNOFUNC,	"OPTIND=1",
	  getoptsreset },
	{ 0,	0,				NULL,
	  NULL }
};

//struct var *vartab[VTABSIZE];

STATIC int strequal(const char *, const char *);
STATIC struct var *find_var(shinstance *, const char *, struct var ***, int *);

/*
 * Initialize the varable symbol tables and import the environment
 */

#ifdef mkinit
INCLUDE "var.h"

INIT {
	char **envp;

	initvar(psh);
	for (envp = sh_environ(psh) ; *envp ; envp++) {
		if (strchr(*envp, '=')) {
			setvareq(psh, *envp, VEXPORT|VTEXTFIXED);
		}
	}
}
#endif


/*
 * This routine initializes the builtin variables.  It is called when the
 * shell is initialized and again when a shell procedure is spawned.
 */

void
initvar(shinstance *psh)
{
	const struct varinit *ip;
	struct var *vp;
	struct var **vpp;
#ifdef PC_OS2_LIBPATHS
	char *psz = ckmalloc(2048);
	int rc;
	int i;

	for (i = 0; i < 4; i++)
	{
		psh->libpath_vars[i].flags = VSTRFIXED | VOS2LIBPATH;
		psh->libpath_vars[i].func = NULL;

		if (i > 0)
		{
			psz[0] = psz[1] = psz[2] = psz[3] = '\0';
			rc = DosQueryExtLIBPATH(psz, i);
		}
		else
		{
			rc = DosQueryHeaderInfo(NULLHANDLE, 0, psz, 2048, QHINF_LIBPATH);
			psh->libpath_vars[i].flags |= VREADONLY;
		}
		if (!rc && *psz)
		{
			int cch1 = strlen(libpath_envs[i]);
			int cch2 = strlen(psz) + 1;
			psh->libpath_vars[i].text = ckmalloc(cch1 + cch2);
			memcpy(psh->libpath_vars[i].text, libpath_envs[i], cch1);
			memcpy(psh->libpath_vars[i].text + cch1, psz, cch2);
		}
		else
		{
			psh->libpath_vars[i].flags |= VUNSET | VTEXTFIXED;
			psh->libpath_vars[i].text = (char*)libpath_envs[i];
		}
		if (find_var(psh, psh->libpath_vars[i].text, &vpp, &psh->libpath_vars[i].name_len) != NULL)
			continue;
		psh->libpath_vars[i].next = *vpp;
		*vpp = &psh->libpath_vars[i];
	}
	free(psz);
#endif

	for (ip = varinit; ip->text; ip++) {
		vp = (struct var *)((char *)psh + ip->var_off);
		if (find_var(psh, ip->text, &vpp, &vp->name_len) != NULL)
			continue;
		vp->next = *vpp;
		*vpp = vp;
		vp->text = strdup(ip->text);
		vp->flags = ip->flags;
		vp->func = ip->func;
	}
	/*
	 * PS1 depends on uid
	 */
	if (find_var(psh, "PS1", &vpp, &psh->vps1.name_len) == NULL) {
		psh->vps1.next = *vpp;
		*vpp = &psh->vps1;
		psh->vps1.text = strdup(sh_geteuid(psh) ? "PS1=$ " : "PS1=# ");
		psh->vps1.flags = VSTRFIXED|VTEXTFIXED;
	}
}

/*
 * Safe version of setvar, returns 1 on success 0 on failure.
 */

int
setvarsafe(shinstance *psh, const char *name, const char *val, int flags)
{
	struct jmploc jmploc;
	struct jmploc *volatile savehandler = psh->handler;
	int err = 0;
#ifdef __GNUC__
	(void) &err;
#endif

	if (setjmp(jmploc.loc))
		err = 1;
	else {
		psh->handler = &jmploc;
		setvar(psh, name, val, flags);
	}
	psh->handler = savehandler;
	return err;
}

/*
 * Set the value of a variable.  The flags argument is ored with the
 * flags of the variable.  If val is NULL, the variable is unset.
 */

void
setvar(shinstance *psh, const char *name, const char *val, int flags)
{
	const char *p;
	const char *q;
	char *d;
	size_t len;
	int namelen;
	char *nameeq;
	int isbad;

	isbad = 0;
	p = name;
	if (! is_name(*p))
		isbad = 1;
	p++;
	for (;;) {
		if (! is_in_name(*p)) {
			if (*p == '\0' || *p == '=')
				break;
			isbad = 1;
		}
		p++;
	}
	namelen = (int)(p - name);
	if (isbad)
		error(psh, "%.*s: bad variable name", namelen, name);
	len = namelen + 2;		/* 2 is space for '=' and '\0' */
	if (val == NULL) {
		flags |= VUNSET;
	} else {
		len += strlen(val);
	}
	d = nameeq = ckmalloc(len);
	q = name;
	while (--namelen >= 0)
		*d++ = *q++;
	*d++ = '=';
	*d = '\0';
	if (val)
		scopy(val, d);
	setvareq(psh, nameeq, flags);
}



/*
 * Same as setvar except that the variable and value are passed in
 * the first argument as name=value.  Since the first argument will
 * be actually stored in the table, it should not be a string that
 * will go away.
 */

void
setvareq(shinstance *psh, char *s, int flags)
{
	struct var *vp, **vpp;
	int nlen;

	if (aflag(psh))
		flags |= VEXPORT;
	vp = find_var(psh, s, &vpp, &nlen);
	if (vp != NULL) {
		if (vp->flags & VREADONLY)
			error(psh, "%.*s: is read only", vp->name_len, s);
		if (flags & VNOSET)
			return;
		INTOFF;

		if (vp->func && (flags & VNOFUNC) == 0)
			(*vp->func)(psh, s + vp->name_len + 1);

		if ((vp->flags & (VTEXTFIXED|VSTACK)) == 0)
			ckfree(vp->text);

		vp->flags &= ~(VTEXTFIXED|VSTACK|VUNSET);
		vp->flags |= flags & ~VNOFUNC;
		vp->text = s;
#ifdef PC_OS2_LIBPATHS
		if ((vp->flags & VOS2LIBPATH) && (vp->flags & VEXPORT))
			vp->flags &= ~VEXPORT;
#endif

		/*
		 * We could roll this to a function, to handle it as
		 * a regular variable function callback, but why bother?
		 */
		if (vp == &psh->vmpath || (vp == &psh->vmail && ! mpathset(psh)))
			chkmail(psh, 1);
		INTON;
		return;
	}
	/* not found */
	if (flags & VNOSET)
		return;
	vp = ckmalloc(sizeof (*vp));
	vp->flags = flags & ~VNOFUNC;
	vp->text = s;
	vp->name_len = nlen;
	vp->next = *vpp;
	vp->func = NULL;
	*vpp = vp;
}



/*
 * Process a linked list of variable assignments.
 */

void
listsetvar(shinstance *psh, struct strlist *list, int flags)
{
	struct strlist *lp;

	INTOFF;
	for (lp = list ; lp ; lp = lp->next) {
		setvareq(psh, savestr(lp->text), flags);
	}
	INTON;
}

void
listmklocal(shinstance *psh, struct strlist *list, int flags)
{
	struct strlist *lp;

	for (lp = list ; lp ; lp = lp->next)
		mklocal(psh, lp->text, flags);
}


/*
 * Find the value of a variable.  Returns NULL if not set.
 */

char *
lookupvar(shinstance *psh, const char *name)
{
	struct var *v;

	v = find_var(psh, name, NULL, NULL);
	if (v == NULL || v->flags & VUNSET)
		return NULL;
	return v->text + v->name_len + 1;
}



/*
 * Search the environment of a builtin command.  If the second argument
 * is nonzero, return the value of a variable even if it hasn't been
 * exported.
 */

char *
bltinlookup(shinstance *psh, const char *name, int doall)
{
	struct strlist *sp;
	struct var *v;

	for (sp = psh->cmdenviron ; sp ; sp = sp->next) {
		if (strequal(sp->text, name))
			return strchr(sp->text, '=') + 1;
	}

	v = find_var(psh, name, NULL, NULL);

	if (v == NULL || v->flags & VUNSET || (!doall && !(v->flags & VEXPORT)))
		return NULL;
	return v->text + v->name_len + 1;
}



/*
 * Generate a list of exported variables.  This routine is used to construct
 * the third argument to execve when executing a program.
 */

char **
environment(shinstance *psh)
{
	int nenv;
	struct var **vpp;
	struct var *vp;
	char **env;
	char **ep;

	nenv = 0;
	for (vpp = psh->vartab ; vpp < psh->vartab + VTABSIZE ; vpp++) {
		for (vp = *vpp ; vp ; vp = vp->next)
			if (vp->flags & VEXPORT)
				nenv++;
	}
	ep = env = stalloc(psh, (nenv + 1) * sizeof *env);
	for (vpp = psh->vartab ; vpp < psh->vartab + VTABSIZE ; vpp++) {
		for (vp = *vpp ; vp ; vp = vp->next)
			if (vp->flags & VEXPORT)
				*ep++ = vp->text;
	}
	*ep = NULL;

#ifdef PC_OS2_LIBPATHS
	/*
	 * Set the libpaths now as this is exec() time.
	 */
	for (nenv = 0; nenv < 3; nenv++)
		DosSetExtLIBPATH(strchr(psh->libpath_vars[nenv].text, '=') + 1, nenv);
#endif

	return env;
}


/*
 * Called when a shell procedure is invoked to clear out nonexported
 * variables.  It is also necessary to reallocate variables of with
 * VSTACK set since these are currently allocated on the stack.
 */

#ifdef mkinit
void shprocvar(shinstance *psh);

SHELLPROC {
	shprocvar(psh);
}
#endif

void
shprocvar(shinstance *psh)
{
	struct var **vpp;
	struct var *vp, **prev;

	for (vpp = psh->vartab ; vpp < psh->vartab + VTABSIZE ; vpp++) {
		for (prev = vpp ; (vp = *prev) != NULL ; ) {
			if ((vp->flags & VEXPORT) == 0) {
				*prev = vp->next;
				if ((vp->flags & VTEXTFIXED) == 0)
					ckfree(vp->text);
				if ((vp->flags & VSTRFIXED) == 0)
					ckfree(vp);
			} else {
				if (vp->flags & VSTACK) {
					vp->text = savestr(vp->text);
					vp->flags &=~ VSTACK;
				}
				prev = &vp->next;
			}
		}
	}
	initvar(psh);
}



/*
 * Command to list all variables which are set.  Currently this command
 * is invoked from the set command when the set command is called without
 * any variables.
 */

void
print_quoted(shinstance *psh, const char *p)
{
	const char *q;

	if (strcspn(p, "|&;<>()$`\\\"' \t\n*?[]#~=%") == strlen(p)) {
		out1fmt(psh, "%s", p);
		return;
	}
	while (*p) {
		if (*p == '\'') {
			out1fmt(psh, "\\'");
			p++;
			continue;
		}
		q = strchr(p, '\'');
		if (!q) {
			out1fmt(psh, "'%s'", p );
			return;
		}
		out1fmt(psh, "'%.*s'", (int)(q - p), p );
		p = q;
	}
}

static int
sort_var(const void *v_v1, const void *v_v2)
{
	const struct var * const *v1 = v_v1;
	const struct var * const *v2 = v_v2;

	/* XXX Will anyone notice we include the '=' of the shorter name? */
	return strcoll((*v1)->text, (*v2)->text);
}

/*
 * POSIX requires that 'set' (but not export or readonly) output the
 * variables in lexicographic order - by the locale's collating order (sigh).
 * Maybe we could keep them in an ordered balanced binary tree
 * instead of hashed lists.
 * For now just roll 'em through qsort for printing...
 */

int
showvars(shinstance *psh, const char *name, int flag, int show_value)
{
	struct var **vpp;
	struct var *vp;
	const char *p;

	static struct var **list;	/* static in case we are interrupted */
	static int list_len;
	int count = 0;

	if (!list) {
		list_len = 32;
		list = ckmalloc(list_len * sizeof(*list));
	}

	for (vpp = psh->vartab ; vpp < psh->vartab + VTABSIZE ; vpp++) {
		for (vp = *vpp ; vp ; vp = vp->next) {
			if (flag && !(vp->flags & flag))
				continue;
			if (vp->flags & VUNSET && !(show_value & 2))
				continue;
			if (count >= list_len) {
				list = ckrealloc(list,
					(list_len << 1) * sizeof(*list));
				list_len <<= 1;
			}
			list[count++] = vp;
		}
	}

	qsort(list, count, sizeof(*list), sort_var);

	for (vpp = list; count--; vpp++) {
		vp = *vpp;
		if (name)
			out1fmt(psh, "%s ", name);
		for (p = vp->text ; *p != '=' ; p++)
			out1c(psh, *p);
		if (!(vp->flags & VUNSET) && show_value) {
			out1fmt(psh, "=");
			print_quoted(psh, ++p);
		}
		out1c(psh, '\n');
	}
	return 0;
}



/*
 * The export and readonly commands.
 */

int
exportcmd(shinstance *psh, int argc, char **argv)
{
	struct var *vp;
	char *name;
	const char *p;
	int flag = argv[0][0] == 'r'? VREADONLY : VEXPORT;
	int pflag;

	pflag = nextopt(psh, "p") == 'p' ? 3 : 0;
	if (argc <= 1 || pflag) {
		showvars(psh, pflag ? argv[0] : 0, flag, pflag );
		return 0;
	}

	while ((name = *psh->argptr++) != NULL) {
		if ((p = strchr(name, '=')) != NULL) {
			p++;
		} else {
			vp = find_var(psh, name, NULL, NULL);
			if (vp != NULL) {
				vp->flags |= flag;
				continue;
			}
		}
		setvar(psh, name, p, flag);
	}
	return 0;
}


/*
 * The "local" command.
 */

int
localcmd(shinstance *psh, int argc, char **argv)
{
	char *name;

	if (! in_function(psh))
		error(psh, "Not in a function");
	while ((name = *psh->argptr++) != NULL) {
		mklocal(psh, name, 0);
	}
	return 0;
}


/*
 * Make a variable a local variable.  When a variable is made local, it's
 * value and flags are saved in a localvar structure.  The saved values
 * will be restored when the shell function returns.  We handle the name
 * "-" as a special case.
 */

void
mklocal(shinstance *psh, const char *name, int flags)
{
	struct localvar *lvp;
	struct var **vpp;
	struct var *vp;

	INTOFF;
	lvp = ckmalloc(sizeof (struct localvar));
	if (name[0] == '-' && name[1] == '\0') {
		char *p;
		p = ckmalloc(sizeof_optlist);
		lvp->text = memcpy(p, psh->optlist, sizeof_optlist);
		vp = NULL;
	} else {
		vp = find_var(psh, name, &vpp, NULL);
		if (vp == NULL) {
			if (strchr(name, '='))
				setvareq(psh, savestr(name), VSTRFIXED|flags);
			else
				setvar(psh, name, NULL, VSTRFIXED|flags);
			vp = *vpp;	/* the new variable */
			lvp->text = NULL;
			lvp->flags = VUNSET;
		} else {
			lvp->text = vp->text;
			lvp->flags = vp->flags;
			vp->flags |= VSTRFIXED|VTEXTFIXED;
			if (name[vp->name_len] == '=')
				setvareq(psh, savestr(name), flags);
		}
	}
	lvp->vp = vp;
	lvp->next = psh->localvars;
	psh->localvars = lvp;
	INTON;
}


/*
 * Called after a function returns.
 */

void
poplocalvars(shinstance *psh)
{
	struct localvar *lvp;
	struct var *vp;

	while ((lvp = psh->localvars) != NULL) {
		psh->localvars = lvp->next;
		vp = lvp->vp;
		TRACE((psh, "poplocalvar %s", vp ? vp->text : "-"));
		if (vp == NULL) {	/* $- saved */
			memcpy(psh->optlist, lvp->text, sizeof_optlist);
			ckfree(lvp->text);
		} else if ((lvp->flags & (VUNSET|VSTRFIXED)) == VUNSET) {
			(void)unsetvar(psh, vp->text, 0);
		} else {
			if (vp->func && (vp->flags & VNOFUNC) == 0)
				(*vp->func)(psh, lvp->text + vp->name_len + 1);
			if ((vp->flags & VTEXTFIXED) == 0)
				ckfree(vp->text);
			vp->flags = lvp->flags;
			vp->text = lvp->text;
		}
		ckfree(lvp);
	}
}


int
setvarcmd(shinstance *psh, int argc, char **argv)
{
	if (argc <= 2)
		return unsetcmd(psh, argc, argv);
	else if (argc == 3)
		setvar(psh, argv[1], argv[2], 0);
	else
		error(psh, "List assignment not implemented");
	return 0;
}


/*
 * The unset builtin command.  We unset the function before we unset the
 * variable to allow a function to be unset when there is a readonly variable
 * with the same name.
 */

int
unsetcmd(shinstance *psh, int argc, char **argv)
{
	char **ap;
	int i;
	int flg_func = 0;
	int flg_var = 0;
	int ret = 0;

	while ((i = nextopt(psh, "evf")) != '\0') {
		if (i == 'f')
			flg_func = 1;
		else
			flg_var = i;
	}
	if (flg_func == 0 && flg_var == 0)
		flg_var = 1;

	for (ap = psh->argptr; *ap ; ap++) {
		if (flg_func)
			ret |= unsetfunc(psh, *ap);
		if (flg_var)
			ret |= unsetvar(psh, *ap, flg_var == 'e');
	}
	return ret;
}


/*
 * Unset the specified variable.
 */

int
unsetvar(shinstance *psh, const char *s, int unexport)
{
	struct var **vpp;
	struct var *vp;

	vp = find_var(psh, s, &vpp, NULL);
	if (vp == NULL)
		return 1;

	if (vp->flags & VREADONLY)
		return (1);

	INTOFF;
	if (unexport) {
		vp->flags &= ~VEXPORT;
	} else {
		if (vp->text[vp->name_len + 1] != '\0')
			setvar(psh, s, nullstr, 0);
		vp->flags &= ~VEXPORT;
		vp->flags |= VUNSET;
		if ((vp->flags & VSTRFIXED) == 0) {
			if ((vp->flags & VTEXTFIXED) == 0)
				ckfree(vp->text);
			*vpp = vp->next;
			ckfree(vp);
		}
	}
	INTON;
	return 0;
}


/*
 * Returns true if the two strings specify the same varable.  The first
 * variable name is terminated by '='; the second may be terminated by
 * either '=' or '\0'.
 */

STATIC int
strequal(const char *p, const char *q)
{
	while (*p == *q++) {
		if (*p++ == '=')
			return 1;
	}
	if (*p == '=' && *(q - 1) == '\0')
		return 1;
	return 0;
}

/*
 * Search for a variable.
 * 'name' may be terminated by '=' or a NUL.
 * vppp is set to the pointer to vp, or the list head if vp isn't found
 * lenp is set to the number of charactets in 'name'
 */

STATIC struct var *
find_var(shinstance *psh, const char *name, struct var ***vppp, int *lenp)
{
	unsigned int hashval;
	int len;
	struct var *vp, **vpp;
	const char *p = name;

	hashval = 0;
	while (*p && *p != '=')
		hashval = 2 * hashval + (unsigned char)*p++;
	len = (int)(p - name);

	if (lenp)
		*lenp = len;
	vpp = &psh->vartab[hashval % VTABSIZE];
	if (vppp)
		*vppp = vpp;

	for (vp = *vpp ; vp ; vpp = &vp->next, vp = *vpp) {
		if (vp->name_len != len)
			continue;
		if (memcmp(vp->text, name, len) != 0)
			continue;
		if (vppp)
			*vppp = vpp;
		return vp;
	}
	return NULL;
}
