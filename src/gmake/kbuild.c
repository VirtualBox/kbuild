/* $Id: $ */
/** @file
 *
 * kBuild specific make functionality.
 *
 * Copyright (c) 2006 knut st. osmundsen <bird@innotek.de>
 *
 *
 * This file is part of kBuild.
 *
 * kBuild is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * kBuild is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with kBuild; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

/* No GNU coding style here! */

#ifdef KMK_HELPERS

#include "make.h"
#include "filedef.h"
#include "variable.h"
#include "dep.h"
#include "debug.h"

#include "kbuild.h"

#include <assert.h>
#include <stdarg.h>
#ifndef va_copy
# define va_copy(dst, src) do {(dst) = (src);} while (0)
#endif


/**
 * Gets a variable that must exist.
 * Will cause a fatal failure if the variable doesn't exist.
 *
 * @returns Pointer to the variable.
 * @param   pszName     The variable name.
 */
static struct variable *
kbuild_get_variable(const char *pszName)
{
#ifndef NDEBUG
    unsigned i;
#endif
    struct variable *pVar = lookup_variable(pszName, strlen(pszName));
    if (!pVar)
        fatal(NILF, _("variable `%s' isn't defined!"), pszName);
    if (pVar->recursive)
        fatal(NILF, _("variable `%s' is defined as `recursive' instead of `simple'!"), pszName);
#ifndef NDEBUG
    i = strlen(pVar->value);
    if (i != pVar->value_length)
    {
        printf("%d != %d %s\n", pVar->value_length, i, pVar->name);
# ifdef _MSC_VER
        __asm int 3;
# endif
        assert(0);
    }
#endif
    return pVar;
}

/**
 * Gets a variable that must exist and can be recursive.
 * Will cause a fatal failure if the variable doesn't exist.
 *
 * @returns Pointer to the variable.
 * @param   pszName     The variable name.
 */
static struct variable *
kbuild_get_recursive_variable(const char *pszName)
{
#ifndef NDEBUG
    unsigned i;
#endif
    struct variable *pVar = lookup_variable(pszName, strlen(pszName));
    if (!pVar)
        fatal(NILF, _("variable `%s' isn't defined!"), pszName);
#ifndef NDEBUG
    i = strlen(pVar->value);
    if (i != pVar->value_length)
    {
        printf("%d != %d %s\n", pVar->value_length, i, pVar->name);
# ifdef _MSC_VER
        __asm int 3;
# endif
        assert(0);
    }
#endif
    return pVar;
}

/**
 * Converts the specified variable into a 'simple' one.
 * @returns pVar.
 * @param   pVar        The variable.
 */
static struct variable *
kbuild_simplify_variable(struct variable *pVar)
{
    if (memchr(pVar->value, '$', pVar->value_length))
    {
        char *pszExpanded = allocated_variable_expand(pVar->value);
        free(pVar->value);
        pVar->value = pszExpanded;
        pVar->value_length = strlen(pVar->value);
        pVar->value_alloc_len = pVar->value_length + 1;
    }
    pVar->recursive = 0;
}

/**
 * Looks up a variable.
 * The value_length field is valid upon successful return.
 *
 * @returns Pointer to the variable. NULL if not found.
 * @param   pszName     The variable name.
 */
static struct variable *
kbuild_lookup_variable(const char *pszName)
{
    struct variable *pVar = lookup_variable(pszName, strlen(pszName));
    if (pVar)
    {
#ifndef NDEBUG
        unsigned i = strlen(pVar->value);
        if (i != pVar->value_length)
        {
            printf("%d != %d %s\n", pVar->value_length, i, pVar->name);
# ifdef _MSC_VER
            __asm int 3;
# endif
            assert(0);
        }
#endif
        /* Make sure the variable is simple, convert it if necessary. */
        if (pVar->recursive)
            kbuild_simplify_variable(pVar);
    }
    return pVar;
}

/** Same as kbuild_lookup_variable except that a '%s' in the name string
 * will be substituted with the values of the variables in the va list.  */
static struct variable *
kbuild_lookup_variable_fmt_va(const char *pszNameFmt, va_list va)
{
    va_list va2;
    size_t cchName;
    const char *pszFmt;
    char *pszName;
    char *psz;

    /* first pass, calc value name size and allocate stack buffer. */
    va_copy(va2, va);

    cchName = strlen(pszNameFmt) + 1;
    pszFmt = strchr(pszNameFmt, '%');
    while (pszFmt)
    {
        struct variable *pVar = va_arg(va, struct variable *);
        if (pVar)
            cchName += pVar->value_length;
        pszFmt = strchr(pszFmt + 1, '%');
    }
    pszName = alloca(cchName);

    /* second pass format it. */
    pszFmt = pszNameFmt;
    psz = pszName;
    for (;;)
    {
        char ch = *pszFmt++;
        if (ch != '%')
        {
            *psz++ = ch;
            if (!ch)
                break;
        }
        else
        {
            struct variable *pVar = va_arg(va2, struct variable *);
            if (pVar)
            {
                memcpy(psz, pVar->value, pVar->value_length);
                psz += pVar->value_length;
            }
        }
    }
    va_end(va2);

    return kbuild_lookup_variable(pszName);
}

/** Same as kbuild_lookup_variable except that a '%s' in the name string
 * will be substituted with the values of the variables in the ellipsis.  */
static struct variable *
kbuild_lookup_variable_fmt(const char *pszNameFmt, ...)
{
    struct variable *pVar;
    va_list va;
    va_start(va, pszNameFmt);
    pVar = kbuild_lookup_variable_fmt_va(pszNameFmt, va);
    va_end(va);
    return pVar;
}


/*
_SOURCE_TOOL = $(strip $(firstword \
    $($(target)_$(source)_$(type)TOOL.$(bld_trg).$(bld_trg_arch)) \
    $($(target)_$(source)_$(type)TOOL.$(bld_trg)) \
    $($(target)_$(source)_$(type)TOOL) \
    $($(target)_$(source)_TOOL.$(bld_trg).$(bld_trg_arch)) \
    $($(target)_$(source)_TOOL.$(bld_trg)) \
    $($(target)_$(source)_TOOL) \
    $($(source)_$(type)TOOL.$(bld_trg).$(bld_trg_arch)) \
    $($(source)_$(type)TOOL.$(bld_trg)) \
    $($(source)_$(type)TOOL) \
    $($(source)_TOOL.$(bld_trg).$(bld_trg_arch)) \
    $($(source)_TOOL.$(bld_trg)) \
    $($(source)_TOOL) \
    $($(target)_$(type)TOOL.$(bld_trg).$(bld_trg_arch)) \
    $($(target)_$(type)TOOL.$(bld_trg)) \
    $($(target)_$(type)TOOL) \
    $($(target)_TOOL.$(bld_trg).$(bld_trg_arch)) \
    $($(target)_TOOL.$(bld_trg)) \
    $($(target)_TOOL) \
    $($(type)TOOL.$(bld_trg).$(bld_trg_arch)) \
    $($(type)TOOL.$(bld_trg)) \
    $($(type)TOOL) \
    $(TOOL.$(bld_trg).$(bld_trg_arch)) \
    $(TOOL.$(bld_trg)) \
    $(TOOL) ))
*/
static struct variable *
kbuild_get_source_tool(struct variable *pTarget, struct variable *pSource, struct variable *pType,
                       struct variable *pBldTrg, struct variable *pBldTrgArch, const char *pszVarName)
{
    struct variable *pVar;
    if (    (pVar = kbuild_lookup_variable_fmt("%_%_%TOOL.%.%", pTarget, pSource, pType, pBldTrg, pBldTrgArch))
        ||  (pVar = kbuild_lookup_variable_fmt("%_%_%TOOL.%",  pTarget, pSource, pType, pBldTrg))
        ||  (pVar = kbuild_lookup_variable_fmt("%_%_%TOOL",     pTarget, pSource, pType))
        ||  (pVar = kbuild_lookup_variable_fmt("%_%_TOOL.%.%", pTarget, pSource, pBldTrg, pBldTrgArch))
        ||  (pVar = kbuild_lookup_variable_fmt("%_%_TOOL.%",   pTarget, pSource, pBldTrg))
        ||  (pVar = kbuild_lookup_variable_fmt("%_%_TOOL",      pTarget, pSource))
        ||  (pVar = kbuild_lookup_variable_fmt("%_%TOOL.%.%",  pSource, pType, pBldTrg, pBldTrgArch))
        ||  (pVar = kbuild_lookup_variable_fmt("%_%TOOL.%",    pSource, pType, pBldTrg))
        ||  (pVar = kbuild_lookup_variable_fmt("%_%TOOL",       pSource, pType))
        ||  (pVar = kbuild_lookup_variable_fmt("%_TOOL.%.%",   pSource, pBldTrg, pBldTrgArch))
        ||  (pVar = kbuild_lookup_variable_fmt("%_TOOL.%",     pSource, pBldTrg))
        ||  (pVar = kbuild_lookup_variable_fmt("%_TOOL",        pSource))
        ||  (pVar = kbuild_lookup_variable_fmt("%_%TOOL.%.%",  pTarget, pType, pBldTrg, pBldTrgArch))
        ||  (pVar = kbuild_lookup_variable_fmt("%_%TOOL.%",    pTarget, pType, pBldTrg))
        ||  (pVar = kbuild_lookup_variable_fmt("%_%TOOL",       pTarget, pType))
        ||  (pVar = kbuild_lookup_variable_fmt("%_TOOL.%.%",   pTarget, pBldTrg, pBldTrgArch))
        ||  (pVar = kbuild_lookup_variable_fmt("%_TOOL.%",     pTarget, pBldTrg))
        ||  (pVar = kbuild_lookup_variable_fmt("%_TOOL",        pTarget))
        ||  (pVar = kbuild_lookup_variable_fmt("%TOOL.%.%",    pType, pBldTrg, pBldTrgArch))
        ||  (pVar = kbuild_lookup_variable_fmt("%TOOL.%",      pType, pBldTrg))
        ||  (pVar = kbuild_lookup_variable_fmt("%TOOL",         pType))
        ||  (pVar = kbuild_lookup_variable_fmt("TOOL.%.%",     pBldTrg, pBldTrgArch))
        ||  (pVar = kbuild_lookup_variable_fmt("TOOL.%",       pBldTrg))
        ||  (pVar = kbuild_lookup_variable("TOOL"))
       )
    {
        /* strip it */
        char *psz = pVar->value;
        char *pszEnd = psz + pVar->value_length;
        while (isblank((unsigned char)*psz))
            psz++;
        while (pszEnd > psz && isblank((unsigned char)pszEnd[-1]))
            pszEnd--;
        if (pszEnd > psz)
        {
            char chSaved = *pszEnd;
            *pszEnd = '\0';
            pVar = define_variable_vl(pszVarName, strlen(pszVarName), psz, pszEnd - psz,
                                      1 /* duplicate */, o_file, 0 /* !recursive */);
            *pszEnd = chSaved;
            if (pVar)
                return pVar;
        }
    }

    fatal(NILF, _("no tool for source `%s' in target `%s'!"), pSource->value, pTarget->value);
    return NULL;
}

/* Implements _SOURCE_TOOL. */
char *
func_kbuild_source_tool(char *o, char **argv, const char *pszFuncName)
{
    struct variable *pVar = kbuild_get_source_tool(kbuild_get_variable("target"),
                                                   kbuild_get_variable("source"),
                                                   kbuild_get_variable("type"),
                                                   kbuild_get_variable("bld_trg"),
                                                   kbuild_get_variable("bld_trg_arch"),
                                                   argv[0]);
    if (pVar)
         o = variable_buffer_output(o, pVar->value, pVar->value_length);
    return o;

}

/*
## Figure out where to put object files.
# @param    $1      source file
# @param    $2      normalized main target
# @remark There are two major hacks here:
#           1. Source files in the output directory are translated into a gen/ subdir.
#         	2. Catch anyone specifying $(PATH_SUB_CURRENT)/sourcefile.c.
_OBJECT_BASE = $(PATH_TARGET)/$(2)/$(call no-root-slash,$(call no-drive,$(basename \
	$(patsubst $(PATH_ROOT)/%,%,$(patsubst $(PATH_SUB_CURRENT)/%,%,$(patsubst $(PATH_TARGET)/$(2)/%,gen/%,$(1)))))))
*/
static struct variable *
kbuild_get_object_base(struct variable *pTarget, struct variable *pSource, const char *pszVarName)
{
    struct variable *pPathTarget = kbuild_get_variable("PATH_TARGET");
    struct variable *pPathRoot   = kbuild_get_variable("PATH_ROOT");
    struct variable *pPathSubCur = kbuild_get_variable("PATH_SUB_CURRENT");
    const char *pszSrcPrefix = NULL;
    const char *pszSrcEnd;
    char *pszSrc;
    char *pszResult;
    char *psz;
    size_t cch;

    /*
     * Strip the source filename of any uncessary leading path and root specs.
     */
    /* */
    if (    pSource->value_length > pPathTarget->value_length
        &&  !strncmp(pSource->value, pPathTarget->value, pPathTarget->value_length))
    {
        pszSrc = pSource->value + pPathTarget->value_length;
        pszSrcPrefix = "gen/";
    }
    else if (    pSource->value_length > pPathRoot->value_length
             &&  !strncmp(pSource->value, pPathRoot->value, pPathRoot->value_length))
    {
        pszSrc = pSource->value + pPathRoot->value_length;
        if (    *pszSrc == '/'
            &&  !strncmp(pszSrc, pPathSubCur->value, pPathSubCur->value_length))
            pszSrc += 1 + pPathSubCur->value_length;
    }
    else
        pszSrc = pSource->value;

    /* skip root specification */
#ifdef HAVE_DOS_PATHS
    if (isalpha(pszSrc[0]) && pszSrc[1] == ':')
        pszSrc += 2;
#endif
    while (*pszSrc == '/'
#ifdef HAVE_DOS_PATHS
           || *pszSrc == '\\'
#endif
           )
        pszSrc++;

    /* drop the source extension. */
    pszSrcEnd = pSource->value + pSource->value_length;
    for (;;)
    {
        pszSrcEnd--;
        if (    pszSrcEnd <= pszSrc
            ||  *pszSrcEnd == '/'
#ifdef HAVE_DOS_PATHS
            ||  *pszSrcEnd == '\\'
            ||  *pszSrcEnd == ':'
#endif
           )
        {
            pszSrcEnd = pSource->value + pSource->value_length;
            break;
        }
        if (*pszSrcEnd == '.')
            break;
    }

    /*
     * Assemble the string on the stack and define the objbase variable
     * which we then return.
     */
    cch = pPathTarget->value_length
        + 1 /* slash */
        + pTarget->value_length
        + 1 /* slash */
        + pszSrcEnd - pszSrc
        + 1;
    psz = pszResult = xmalloc(cch);

    memcpy(psz, pPathTarget->value, pPathTarget->value_length); psz += pPathTarget->value_length;
    *psz++ = '/';
    memcpy(psz, pTarget->value, pTarget->value_length); psz += pTarget->value_length;
    *psz++ = '/';
    memcpy(psz, pszSrc, pszSrcEnd - pszSrc); psz += pszSrcEnd - pszSrc;
    *psz = '\0';

    /*
     * Define the variable in the current set and return it.
     */
    return define_variable_vl(pszVarName, strlen(pszVarName), pszResult, cch - 1,
                              0 /* use pszResult */, o_file, 0 /* !recursive */);
}

/* Implements _OBJECT_BASE. */
char *
func_kbuild_object_base(char *o, char **argv, const char *pszFuncName)
{
    struct variable *pVar = kbuild_get_object_base(kbuild_lookup_variable("target"),
                                                   kbuild_lookup_variable("source"),
                                                   argv[0]);
    if (pVar)
         o = variable_buffer_output(o, pVar->value, pVar->value_length);
    return o;

}


/*
$(firstword \
	$($(target)_$(source)_OBJSUFF.$(bld_trg).$(bld_trg_arch))\
	$($(target)_$(source)_OBJSUFF.$(bld_trg))\
	$($(target)_$(source)_OBJSUFF)\
	$($(source)_OBJSUFF.$(bld_trg).$(bld_trg_arch))\
	$($(source)_OBJSUFF.$(bld_trg))\
	$($(source)_OBJSUFF)\
	$($(target)_OBJSUFF.$(bld_trg).$(bld_trg_arch))\
	$($(target)_OBJSUFF.$(bld_trg))\
	$($(target)_OBJSUFF)\
	$(TOOL_$(tool)_$(type)OBJSUFF.$(bld_trg).$(bld_trg_arch))\
	$(TOOL_$(tool)_$(type)OBJSUFF.$(bld_trg))\
	$(TOOL_$(tool)_$(type)OBJSUFF)\
	$(SUFF_OBJ))
*/
static struct variable *
kbuild_get_object_suffix(struct variable *pTarget, struct variable *pSource,
                         struct variable *pBldTrg, struct variable *pBldTrgArch,
                         const char *pszVarName)
{
    /** @todo ignore variables without content. Can generalize this and join with the tool getter. */
    struct variable *pVar;
    if (    (pVar = kbuild_lookup_variable_fmt("%_%_OBJSUFF.%.%", pTarget, pSource, pBldTrg, pBldTrgArch))
        ||  (pVar = kbuild_lookup_variable_fmt("%_%_OBJSUFF.%",   pTarget, pSource, pBldTrg))
        ||  (pVar = kbuild_lookup_variable_fmt("%_%_OBJSUFF",      pTarget, pSource))
        ||  (pVar = kbuild_lookup_variable_fmt("%_OBJSUFF.%.%",   pSource, pBldTrg, pBldTrgArch))
        ||  (pVar = kbuild_lookup_variable_fmt("%_OBJSUFF.%",     pSource, pBldTrg))
        ||  (pVar = kbuild_lookup_variable_fmt("%_OBJSUFF",        pSource))
        ||  (pVar = kbuild_lookup_variable_fmt("%_OBJSUFF.%.%",   pTarget, pBldTrg, pBldTrgArch))
        ||  (pVar = kbuild_lookup_variable_fmt("%_OBJSUFF.%",     pTarget, pBldTrg))
        ||  (pVar = kbuild_lookup_variable_fmt("%_OBJSUFF",        pTarget))
        ||  (pVar = kbuild_lookup_variable_fmt("OBJSUFF.%.%",      pBldTrg, pBldTrgArch))
        ||  (pVar = kbuild_lookup_variable_fmt("OBJSUFF.%",        pBldTrg))
        ||  (pVar = kbuild_lookup_variable("SUFF_OBJ"))
       )
    {
        /* strip it */
        char *psz = pVar->value;
        char *pszEnd = psz + pVar->value_length;
        while (isblank((unsigned char)*psz))
            psz++;
        while (pszEnd > psz && isblank((unsigned char)pszEnd[-1]))
            pszEnd--;
        if (pszEnd > psz)
        {
            char chSaved = *pszEnd;
            *pszEnd = '\0';
            pVar = define_variable_vl(pszVarName, strlen(pszVarName), psz, pszEnd - psz,
                                      1 /* duplicate */, o_file, 0 /* !recursive */);
            *pszEnd = chSaved;
            if (pVar)
                return pVar;
        }
    }

    fatal(NILF, _("no tool for source `%s' in target `%s'!"), pSource->value, pTarget->value);
    return NULL;
}

/*  */
char *
func_kbuild_object_suffix(char *o, char **argv, const char *pszFuncName)
{
    struct variable *pVar = kbuild_get_object_suffix(kbuild_get_variable("target"),
                                                     kbuild_get_variable("source"),
                                                     kbuild_get_variable("bld_trg"),
                                                     kbuild_get_variable("bld_trg_arch"),
                                                     argv[0]);
    if (pVar)
         o = variable_buffer_output(o, pVar->value, pVar->value_length);
    return o;

}




struct kbuild_sdks
{
    char *apsz[4];
    struct variable *pa;
    unsigned c;
    unsigned iGlobal;
    unsigned cGlobal;
    unsigned iTarget;
    unsigned cTarget;
    unsigned iSource;
    unsigned cSource;
    unsigned iTargetSource;
    unsigned cTargetSource;
};

/* Fills in the SDK struct (remember to free it). */
static void
kbuild_get_sdks(struct kbuild_sdks *pSdks, struct variable *pTarget, struct variable *pSource,
                struct variable *pBldType, struct variable *pBldTrg, struct variable *pBldTrgArch)
{
    int i, j;
    size_t cchTmp;
    char *pszTmp;
    unsigned cchCur;
    char *pszCur;
    char *pszIterator;

    /* basic init. */
    pSdks->pa = NULL;
    pSdks->c = 0;
    i = 0;

    /* determin required tmp variable name space. */
    cchTmp = (  pTarget->value_length + 1
              + pSource->value_length + 6
              + pBldTrg->value_length + 1
              + pBldTrgArch->value_length + 4) * 4;
    pszTmp = alloca(cchTmp);


    /* the global sdks. */
    pSdks->iGlobal = i;
    pSdks->cGlobal = 0;
    sprintf(pszTmp, "$(SDKS) $(SDKS.%s) $(SDKS.%s) $(SDKS.%s.%s)",
            pBldType->value, pBldTrg->value, pBldTrg->value, pBldTrgArch->value);
    pszIterator = pSdks->apsz[0] = allocated_variable_expand(pszTmp);
    while ((pszCur = find_next_token(&pszIterator, &cchCur)) != 0)
        pSdks->cGlobal++;
    i += pSdks->cGlobal;

    /* the target sdks.*/
    pSdks->iTarget = i;
    pSdks->cTarget = 0;
    sprintf(pszTmp, "$(%s_SDKS) $(%s_SDKS.%s) $(%s_SDKS.%s) $(%s_SDKS.%s.%s)",
            pTarget->value, pBldType->value,
            pTarget->value, pBldTrg->value,
            pTarget->value, pBldTrg->value,
            pTarget->value, pBldTrgArch->value);
    pszIterator = pSdks->apsz[1] = allocated_variable_expand(pszTmp);
    while ((pszCur = find_next_token(&pszIterator, &cchCur)) != 0)
        pSdks->cTarget++;
    i += pSdks->cTarget;

    /* the source sdks.*/
    pSdks->iSource = i;
    pSdks->cSource = 0;
    sprintf(pszTmp, "$(%s_SDKS) $(%s_SDKS.%s) $(%s_SDKS.%s) $(%s_SDKS.%s.%s)",
            pSource->value, pBldType->value,
            pSource->value, pBldTrg->value,
            pSource->value, pBldTrg->value,
            pSource->value, pBldTrgArch->value);
    pszIterator = pSdks->apsz[2] = allocated_variable_expand(pszTmp);
    while ((pszCur = find_next_token(&pszIterator, &cchCur)) != 0)
        pSdks->cSource++;
    i += pSdks->cSource;

    /* the target + source sdks. */
    pSdks->iTargetSource = i;
    pSdks->cTargetSource = 0;
    sprintf(pszTmp, "$(%s_%s_SDKS) $(%s_%s_SDKS.%s) $(%s_%s_SDKS.%s) $(%s_%s_SDKS.%s.%s)",
            pTarget->value, pSource->value, pBldType->value,
            pTarget->value, pSource->value, pBldTrg->value,
            pTarget->value, pSource->value, pBldTrg->value,
            pTarget->value, pSource->value, pBldTrgArch->value);
    pszIterator = pSdks->apsz[3] = allocated_variable_expand(pszTmp);
    while ((pszCur = find_next_token(&pszIterator, &cchCur)) != 0)
        pSdks->cTargetSource++;
    i += pSdks->cTargetSource;

    pSdks->c = i;
    if (!i)
        return;

    /*
     * Allocate the variable array and create the variables.
     */
    pSdks->pa = (struct variable *)xmalloc(sizeof(pSdks->pa[0]) * i);
    memset(pSdks->pa, 0, sizeof(pSdks->pa[0]) * i);
    for (i = j = 0; j < sizeof(pSdks->apsz) / sizeof(pSdks->apsz[0]); j++)
    {
        pszIterator = pSdks->apsz[j];
        while ((pszCur = find_next_token(&pszIterator, &cchCur)) != 0)
        {
            pSdks->pa[i].value = pszCur;
            pSdks->pa[i].value_length = cchCur;
            pszCur[cchCur] = '\0';
        }
    }
}

/* releases resources allocated in the kbuild_get_sdks. */
kbuild_put_sdks(struct kbuild_sdks *pSdks)
{
    unsigned j;
    for (j = 0; j < sizeof(pSdks->apsz) / sizeof(pSdks->apsz[0]); j++)
        free(pSdks->apsz[j]);
    free(pSdks->pa);
}

/* this kind of stuff:

defs        := $(kb-src-exp defs)
	$(TOOL_$(tool)_DEFS)\
	$(TOOL_$(tool)_DEFS.$(bld_type))\
	$(TOOL_$(tool)_DEFS.$(bld_trg))\
	$(TOOL_$(tool)_DEFS.$(bld_trg_arch))\
	$(TOOL_$(tool)_DEFS.$(bld_trg).$(bld_trg_arch))\
	$(TOOL_$(tool)_DEFS.$(bld_trg_cpu))\
	$(TOOL_$(tool)_$(type)DEFS)\
	$(TOOL_$(tool)_$(type)DEFS.$(bld_type))\
	$(foreach sdk, $(SDKS.$(bld_trg)) \
				   $(SDKS.$(bld_trg).$(bld_trg_arch)) \
				   $(SDKS.$(bld_type)) \
				   $(SDKS),\
		$(SDK_$(sdk)_DEFS)\
		$(SDK_$(sdk)_DEFS.$(bld_type))\
		$(SDK_$(sdk)_DEFS.$(bld_trg))\
		$(SDK_$(sdk)_DEFS.$(bld_trg_arch))\
		$(SDK_$(sdk)_DEFS.$(bld_trg).$(bld_trg_arch))\
		$(SDK_$(sdk)_DEFS.$(bld_trg_cpu))\
		$(SDK_$(sdk)_$(type)DEFS)\
		$(SDK_$(sdk)_$(type)DEFS.$(bld_type))\
		$(SDK_$(sdk)_$(type)DEFS.$(bld_trg))\
		$(SDK_$(sdk)_$(type)DEFS.$(bld_trg_arch))\
		$(SDK_$(sdk)_$(type)DEFS.$(bld_trg).$(bld_trg_arch))\
		$(SDK_$(sdk)_$(type)DEFS.$(bld_trg_cpu)))\
	$(DEFS)\
	$(DEFS.$(bld_type))\
	$(DEFS.$(bld_trg))\
	$(DEFS.$(bld_trg_arch))\
	$(DEFS.$(bld_trg).$(bld_trg_arch))\
	$(DEFS.$(bld_trg_cpu))\
	$($(type)DEFS)\
	$($(type)DEFS.$(bld_type))\
	$($(type)DEFS.$(bld_trg))\
	$($(type)DEFS.$(bld_trg_arch))\
	$($(type)DEFS.$(bld_trg).$(bld_trg_arch))\
	$($(type)DEFS.$(bld_trg_cpu))\
	$(foreach sdk, $($(target)_SDKS.$(bld_trg)) \
				   $($(target)_SDKS.$(bld_trg).$(bld_trg_arch)) \
				   $($(target)_SDKS.$(bld_type)) \
				   $($(target)_SDKS),\
		$(SDK_$(sdk)_DEFS)\
		$(SDK_$(sdk)_DEFS.$(bld_type))\
		$(SDK_$(sdk)_DEFS.$(bld_trg))\
		$(SDK_$(sdk)_DEFS.$(bld_trg_arch))\
		$(SDK_$(sdk)_DEFS.$(bld_trg).$(bld_trg_arch))\
		$(SDK_$(sdk)_DEFS.$(bld_trg_cpu))\
		$(SDK_$(sdk)_$(type)DEFS)\
		$(SDK_$(sdk)_$(type)DEFS.$(bld_type))\
		$(SDK_$(sdk)_$(type)DEFS.$(bld_trg))\
		$(SDK_$(sdk)_$(type)DEFS.$(bld_trg_arch))\
		$(SDK_$(sdk)_$(type)DEFS.$(bld_trg).$(bld_trg_arch))\
		$(SDK_$(sdk)_$(type)DEFS.$(bld_trg_cpu)))\
	$($(target)_DEFS)\
	$($(target)_DEFS.$(bld_type))\
	$($(target)_DEFS.$(bld_trg))\
	$($(target)_DEFS.$(bld_trg_arch))\
	$($(target)_DEFS.$(bld_trg).$(bld_trg_arch))\
	$($(target)_DEFS.$(bld_trg_cpu))\
	$($(target)_$(type)DEFS)\
	$($(target)_$(type)DEFS.$(bld_type))\
	$($(target)_$(type)DEFS.$(bld_trg))\
	$($(target)_$(type)DEFS.$(bld_trg_arch))\
	$($(target)_$(type)DEFS.$(bld_trg).$(bld_trg_arch))\
	$($(target)_$(type)DEFS.$(bld_trg_cpu))\
	$(foreach sdk, $($(source)_SDKS.$(bld_trg)) \
				   $($(source)_SDKS.$(bld_trg).$(bld_trg_arch)) \
				   $($(source)_SDKS.$(bld_type)) \
				   $($(source)_SDKS),\
		$(SDK_$(sdk)_DEFS)\
		$(SDK_$(sdk)_DEFS.$(bld_type))\
		$(SDK_$(sdk)_DEFS.$(bld_trg))\
		$(SDK_$(sdk)_DEFS.$(bld_trg_arch))\
		$(SDK_$(sdk)_DEFS.$(bld_trg).$(bld_trg_arch))\
		$(SDK_$(sdk)_DEFS.$(bld_trg_cpu))\
		$(SDK_$(sdk)_$(type)DEFS)\
		$(SDK_$(sdk)_$(type)DEFS.$(bld_type))\
		$(SDK_$(sdk)_$(type)DEFS.$(bld_trg))\
		$(SDK_$(sdk)_$(type)DEFS.$(bld_trg_arch))\
		$(SDK_$(sdk)_$(type)DEFS.$(bld_trg).$(bld_trg_arch))\
		$(SDK_$(sdk)_$(type)DEFS.$(bld_trg_cpu)))\
	$($(source)_DEFS)\
	$($(source)_DEFS.$(bld_type))\
	$($(source)_DEFS.$(bld_trg))\
	$($(source)_DEFS.$(bld_trg_arch))\
	$($(source)_DEFS.$(bld_trg).$(bld_trg_arch))\
	$($(source)_DEFS.$(bld_trg_cpu))\
	$($(source)_$(type)DEFS)\
	$($(source)_$(type)DEFS.$(bld_type))\
	$($(source)_$(type)DEFS.$(bld_trg))\
	$($(source)_$(type)DEFS.$(bld_trg_arch))\
	$($(source)_$(type)DEFS.$(bld_trg).$(bld_trg_arch))\
	$($(source)_$(type)DEFS.$(bld_trg_cpu))\
	$(foreach sdk, $($(target)_$(source)_SDKS.$(bld_trg)) \
				   $($(target)_$(source)_SDKS.$(bld_trg).$(bld_trg_arch)) \
				   $($(target)_$(source)_SDKS.$(bld_type)) \
				   $($(target)_$(source)_SDKS),\
		$(SDK_$(sdk)_DEFS)\
		$(SDK_$(sdk)_DEFS.$(bld_type))\
		$(SDK_$(sdk)_DEFS.$(bld_trg))\
		$(SDK_$(sdk)_DEFS.$(bld_trg_arch))\
		$(SDK_$(sdk)_DEFS.$(bld_trg).$(bld_trg_arch))\
		$(SDK_$(sdk)_DEFS.$(bld_trg_cpu))\
		$(SDK_$(sdk)_$(type)DEFS)\
		$(SDK_$(sdk)_$(type)DEFS.$(bld_type))\
		$(SDK_$(sdk)_$(type)DEFS.$(bld_trg))\
		$(SDK_$(sdk)_$(type)DEFS.$(bld_trg_arch))\
		$(SDK_$(sdk)_$(type)DEFS.$(bld_trg).$(bld_trg_arch))\
		$(SDK_$(sdk)_$(type)DEFS.$(bld_trg_cpu)))\
	$($(target)_$(source)_DEFS)\
	$($(target)_$(source)_DEFS.$(bld_type))\
	$($(target)_$(source)_DEFS.$(bld_trg))\
	$($(target)_$(source)_DEFS.$(bld_trg_arch))\
	$($(target)_$(source)_DEFS.$(bld_trg).$(bld_trg_arch))\
	$($(target)_$(source)_DEFS.$(bld_trg_cpu))\
	$($(target)_$(source)_$(type)DEFS)\
	$($(target)_$(source)_$(type)DEFS.$(bld_type))\
	$($(target)_$(source)_$(type)DEFS.$(bld_trg))\
	$($(target)_$(source)_$(type)DEFS.$(bld_trg_arch))\
	$($(target)_$(source)_$(type)DEFS.$(bld_trg).$(bld_trg_arch))\
	$($(target)_$(source)_$(type)DEFS.$(bld_trg_cpu))
*/
static struct variable *
kbuild_collect_source_prop(struct variable *pTarget, struct variable *pSource,
                           struct variable *pTool, struct kbuild_sdks *pSdks,
                           struct variable *pType, struct variable *pBldType,
                           struct variable *pBldTrg, struct variable *pBldTrgArch, struct variable *pBldTrgCpu,
                           const char *pszProp, const char *pszVarName, int iDirection)
{
    struct variable *pVar;
    unsigned iSdk;
    int cVars, iVar, iVarEnd;
    size_t cchTotal;
    char *pszResult, *psz;
    struct
    {
        struct variable    *pVar;
        size_t              cchExp;
        char               *pszExp;
    } *paVars;

    struct variable Prop = {0};
    Prop.value = (char *)pszProp;
    Prop.value_length = strlen(pszProp);

    /*
     * Get the variables.
     */
    cVars = 12 + (pSdks->c + 1) * 12 * 4;
    paVars = alloca(cVars * sizeof(paVars[0]));

    iVar = 0;
    /* the tool (lowest priority) */
    paVars[iVar++].pVar = kbuild_lookup_variable_fmt("TOOL_%_%",      pTool, &Prop);
    paVars[iVar++].pVar = kbuild_lookup_variable_fmt("TOOL_%_%.%",    pTool, &Prop, pBldType);
    paVars[iVar++].pVar = kbuild_lookup_variable_fmt("TOOL_%_%.%",    pTool, &Prop, pBldTrg);
    paVars[iVar++].pVar = kbuild_lookup_variable_fmt("TOOL_%_%.%",    pTool, &Prop, pBldTrgArch);
    paVars[iVar++].pVar = kbuild_lookup_variable_fmt("TOOL_%_%.%.%",  pTool, &Prop, pBldTrg, pBldTrgArch);
    paVars[iVar++].pVar = kbuild_lookup_variable_fmt("TOOL_%_%.%",    pTool, &Prop, pBldTrgCpu);

    paVars[iVar++].pVar = kbuild_lookup_variable_fmt("TOOL_%_%%",     pTool, pType, &Prop);
    paVars[iVar++].pVar = kbuild_lookup_variable_fmt("TOOL_%_%%.%",   pTool, pType, &Prop, pBldType);
    paVars[iVar++].pVar = kbuild_lookup_variable_fmt("TOOL_%_%%.%",   pTool, pType, &Prop, pBldTrg);
    paVars[iVar++].pVar = kbuild_lookup_variable_fmt("TOOL_%_%%.%",   pTool, pType, &Prop, pBldTrgArch);
    paVars[iVar++].pVar = kbuild_lookup_variable_fmt("TOOL_%_%%.%.%", pTool, pType, &Prop, pBldTrg, pBldTrgArch);
    paVars[iVar++].pVar = kbuild_lookup_variable_fmt("TOOL_%_%%.%",   pTool, pType, &Prop, pBldTrgCpu);

    /* the global sdks */
    for (iSdk = pSdks->iGlobal; iSdk < pSdks->cGlobal; iSdk++)
    {
        struct variable *pSdk = &pSdks->pa[iSdk];
        paVars[iVar++].pVar = kbuild_lookup_variable_fmt("SDK_%_%",      pSdk, &Prop);
        paVars[iVar++].pVar = kbuild_lookup_variable_fmt("SDK_%_%.%",    pSdk, &Prop, pBldType);
        paVars[iVar++].pVar = kbuild_lookup_variable_fmt("SDK_%_%.%",    pSdk, &Prop, pBldTrg);
        paVars[iVar++].pVar = kbuild_lookup_variable_fmt("SDK_%_%.%",    pSdk, &Prop, pBldTrgArch);
        paVars[iVar++].pVar = kbuild_lookup_variable_fmt("SDK_%_%.%.%",  pSdk, &Prop, pBldTrg, pBldTrgArch);
        paVars[iVar++].pVar = kbuild_lookup_variable_fmt("SDK_%_%.%",    pSdk, &Prop, pBldTrgCpu);

        paVars[iVar++].pVar = kbuild_lookup_variable_fmt("SDK_%_%%",     pSdk, pType, &Prop);
        paVars[iVar++].pVar = kbuild_lookup_variable_fmt("SDK_%_%%.%",   pSdk, pType, &Prop, pBldType);
        paVars[iVar++].pVar = kbuild_lookup_variable_fmt("SDK_%_%%.%",   pSdk, pType, &Prop, pBldTrg);
        paVars[iVar++].pVar = kbuild_lookup_variable_fmt("SDK_%_%%.%",   pSdk, pType, &Prop, pBldTrgArch);
        paVars[iVar++].pVar = kbuild_lookup_variable_fmt("SDK_%_%%.%.%", pSdk, pType, &Prop, pBldTrg, pBldTrgArch);
        paVars[iVar++].pVar = kbuild_lookup_variable_fmt("SDK_%_%%.%",   pSdk, pType, &Prop, pBldTrgCpu);
    }

    /* the globals */
    paVars[iVar++].pVar = kbuild_lookup_variable_fmt("%",      &Prop);
    paVars[iVar++].pVar = kbuild_lookup_variable_fmt("%.%",    &Prop, pBldType);
    paVars[iVar++].pVar = kbuild_lookup_variable_fmt("%.%",    &Prop, pBldTrg);
    paVars[iVar++].pVar = kbuild_lookup_variable_fmt("%.%",    &Prop, pBldTrgArch);
    paVars[iVar++].pVar = kbuild_lookup_variable_fmt("%.%.%",  &Prop, pBldTrg, pBldTrgArch);
    paVars[iVar++].pVar = kbuild_lookup_variable_fmt("%.%",    &Prop, pBldTrgCpu);

    paVars[iVar++].pVar = kbuild_lookup_variable_fmt("%%",     pType, &Prop);
    paVars[iVar++].pVar = kbuild_lookup_variable_fmt("%%.%",   pType, &Prop, pBldType);
    paVars[iVar++].pVar = kbuild_lookup_variable_fmt("%%.%",   pType, &Prop, pBldTrg);
    paVars[iVar++].pVar = kbuild_lookup_variable_fmt("%%.%",   pType, &Prop, pBldTrgArch);
    paVars[iVar++].pVar = kbuild_lookup_variable_fmt("%%.%.%", pType, &Prop, pBldTrg, pBldTrgArch);
    paVars[iVar++].pVar = kbuild_lookup_variable_fmt("%%.%",   pType, &Prop, pBldTrgCpu);

    /* the target sdks */
    for (iSdk = pSdks->iTarget; iSdk < pSdks->cTarget; iSdk++)
    {
        struct variable *pSdk = &pSdks->pa[iSdk];
        paVars[iVar++].pVar = kbuild_lookup_variable_fmt("SDK_%_%",      pSdk, &Prop);
        paVars[iVar++].pVar = kbuild_lookup_variable_fmt("SDK_%_%.%",    pSdk, &Prop, pBldType);
        paVars[iVar++].pVar = kbuild_lookup_variable_fmt("SDK_%_%.%",    pSdk, &Prop, pBldTrg);
        paVars[iVar++].pVar = kbuild_lookup_variable_fmt("SDK_%_%.%",    pSdk, &Prop, pBldTrgArch);
        paVars[iVar++].pVar = kbuild_lookup_variable_fmt("SDK_%_%.%.%",  pSdk, &Prop, pBldTrg, pBldTrgArch);
        paVars[iVar++].pVar = kbuild_lookup_variable_fmt("SDK_%_%.%",    pSdk, &Prop, pBldTrgCpu);

        paVars[iVar++].pVar = kbuild_lookup_variable_fmt("SDK_%_%%",     pSdk, pType, &Prop);
        paVars[iVar++].pVar = kbuild_lookup_variable_fmt("SDK_%_%%.%",   pSdk, pType, &Prop, pBldType);
        paVars[iVar++].pVar = kbuild_lookup_variable_fmt("SDK_%_%%.%",   pSdk, pType, &Prop, pBldTrg);
        paVars[iVar++].pVar = kbuild_lookup_variable_fmt("SDK_%_%%.%",   pSdk, pType, &Prop, pBldTrgArch);
        paVars[iVar++].pVar = kbuild_lookup_variable_fmt("SDK_%_%%.%.%", pSdk, pType, &Prop, pBldTrg, pBldTrgArch);
        paVars[iVar++].pVar = kbuild_lookup_variable_fmt("SDK_%_%%.%",   pSdk, pType, &Prop, pBldTrgCpu);
    }

    /* the target */
    paVars[iVar++].pVar = kbuild_lookup_variable_fmt("%_%",      pTarget, &Prop);
    paVars[iVar++].pVar = kbuild_lookup_variable_fmt("%_%.%",    pTarget, &Prop, pBldType);
    paVars[iVar++].pVar = kbuild_lookup_variable_fmt("%_%.%",    pTarget, &Prop, pBldTrg);
    paVars[iVar++].pVar = kbuild_lookup_variable_fmt("%_%.%",    pTarget, &Prop, pBldTrgArch);
    paVars[iVar++].pVar = kbuild_lookup_variable_fmt("%_%.%.%",  pTarget, &Prop, pBldTrg, pBldTrgArch);
    paVars[iVar++].pVar = kbuild_lookup_variable_fmt("%_%.%",    pTarget, &Prop, pBldTrgCpu);

    paVars[iVar++].pVar = kbuild_lookup_variable_fmt("%_%%",     pTarget, pType, &Prop);
    paVars[iVar++].pVar = kbuild_lookup_variable_fmt("%_%%.%",   pTarget, pType, &Prop, pBldType);
    paVars[iVar++].pVar = kbuild_lookup_variable_fmt("%_%%.%",   pTarget, pType, &Prop, pBldTrg);
    paVars[iVar++].pVar = kbuild_lookup_variable_fmt("%_%%.%",   pTarget, pType, &Prop, pBldTrgArch);
    paVars[iVar++].pVar = kbuild_lookup_variable_fmt("%_%%.%.%", pTarget, pType, &Prop, pBldTrg, pBldTrgArch);
    paVars[iVar++].pVar = kbuild_lookup_variable_fmt("%_%%.%",   pTarget, pType, &Prop, pBldTrgCpu);

    /* the source sdks */
    for (iSdk = pSdks->iSource; iSdk < pSdks->cSource; iSdk++)
    {
        struct variable *pSdk = &pSdks->pa[iSdk];
        paVars[iVar++].pVar = kbuild_lookup_variable_fmt("SDK_%_%",      pSdk, &Prop);
        paVars[iVar++].pVar = kbuild_lookup_variable_fmt("SDK_%_%.%",    pSdk, &Prop, pBldType);
        paVars[iVar++].pVar = kbuild_lookup_variable_fmt("SDK_%_%.%",    pSdk, &Prop, pBldTrg);
        paVars[iVar++].pVar = kbuild_lookup_variable_fmt("SDK_%_%.%",    pSdk, &Prop, pBldTrgArch);
        paVars[iVar++].pVar = kbuild_lookup_variable_fmt("SDK_%_%.%.%",  pSdk, &Prop, pBldTrg, pBldTrgArch);
        paVars[iVar++].pVar = kbuild_lookup_variable_fmt("SDK_%_%.%",    pSdk, &Prop, pBldTrgCpu);

        paVars[iVar++].pVar = kbuild_lookup_variable_fmt("SDK_%_%%",     pSdk, pType, &Prop);
        paVars[iVar++].pVar = kbuild_lookup_variable_fmt("SDK_%_%%.%",   pSdk, pType, &Prop, pBldType);
        paVars[iVar++].pVar = kbuild_lookup_variable_fmt("SDK_%_%%.%",   pSdk, pType, &Prop, pBldTrg);
        paVars[iVar++].pVar = kbuild_lookup_variable_fmt("SDK_%_%%.%",   pSdk, pType, &Prop, pBldTrgArch);
        paVars[iVar++].pVar = kbuild_lookup_variable_fmt("SDK_%_%%.%.%", pSdk, pType, &Prop, pBldTrg, pBldTrgArch);
        paVars[iVar++].pVar = kbuild_lookup_variable_fmt("SDK_%_%%.%",   pSdk, pType, &Prop, pBldTrgCpu);
    }

    /* the source */
    paVars[iVar++].pVar = kbuild_lookup_variable_fmt("%_%",      pSource, &Prop);
    paVars[iVar++].pVar = kbuild_lookup_variable_fmt("%_%.%",    pSource, &Prop, pBldType);
    paVars[iVar++].pVar = kbuild_lookup_variable_fmt("%_%.%",    pSource, &Prop, pBldTrg);
    paVars[iVar++].pVar = kbuild_lookup_variable_fmt("%_%.%",    pSource, &Prop, pBldTrgArch);
    paVars[iVar++].pVar = kbuild_lookup_variable_fmt("%_%.%.%",  pSource, &Prop, pBldTrg, pBldTrgArch);
    paVars[iVar++].pVar = kbuild_lookup_variable_fmt("%_%.%",    pSource, &Prop, pBldTrgCpu);

    paVars[iVar++].pVar = kbuild_lookup_variable_fmt("%_%%",     pSource, pType, &Prop);
    paVars[iVar++].pVar = kbuild_lookup_variable_fmt("%_%%.%",   pSource, pType, &Prop, pBldType);
    paVars[iVar++].pVar = kbuild_lookup_variable_fmt("%_%%.%",   pSource, pType, &Prop, pBldTrg);
    paVars[iVar++].pVar = kbuild_lookup_variable_fmt("%_%%.%",   pSource, pType, &Prop, pBldTrgArch);
    paVars[iVar++].pVar = kbuild_lookup_variable_fmt("%_%%.%.%", pSource, pType, &Prop, pBldTrg, pBldTrgArch);
    paVars[iVar++].pVar = kbuild_lookup_variable_fmt("%_%%.%",   pSource, pType, &Prop, pBldTrgCpu);


    /* the target + source sdks */
    for (iSdk = pSdks->iTargetSource; iSdk < pSdks->cTargetSource; iSdk++)
    {
        struct variable *pSdk = &pSdks->pa[iSdk];
        paVars[iVar++].pVar = kbuild_lookup_variable_fmt("SDK_%_%",      pSdk, &Prop);
        paVars[iVar++].pVar = kbuild_lookup_variable_fmt("SDK_%_%.%",    pSdk, &Prop, pBldType);
        paVars[iVar++].pVar = kbuild_lookup_variable_fmt("SDK_%_%.%",    pSdk, &Prop, pBldTrg);
        paVars[iVar++].pVar = kbuild_lookup_variable_fmt("SDK_%_%.%",    pSdk, &Prop, pBldTrgArch);
        paVars[iVar++].pVar = kbuild_lookup_variable_fmt("SDK_%_%.%.%",  pSdk, &Prop, pBldTrg, pBldTrgArch);
        paVars[iVar++].pVar = kbuild_lookup_variable_fmt("SDK_%_%.%",    pSdk, &Prop, pBldTrgCpu);

        paVars[iVar++].pVar = kbuild_lookup_variable_fmt("SDK_%_%%",     pSdk, pType, &Prop);
        paVars[iVar++].pVar = kbuild_lookup_variable_fmt("SDK_%_%%.%",   pSdk, pType, &Prop, pBldType);
        paVars[iVar++].pVar = kbuild_lookup_variable_fmt("SDK_%_%%.%",   pSdk, pType, &Prop, pBldTrg);
        paVars[iVar++].pVar = kbuild_lookup_variable_fmt("SDK_%_%%.%",   pSdk, pType, &Prop, pBldTrgArch);
        paVars[iVar++].pVar = kbuild_lookup_variable_fmt("SDK_%_%%.%.%", pSdk, pType, &Prop, pBldTrg, pBldTrgArch);
        paVars[iVar++].pVar = kbuild_lookup_variable_fmt("SDK_%_%%.%",   pSdk, pType, &Prop, pBldTrgCpu);
    }

    /* the target + source */
    paVars[iVar++].pVar = kbuild_lookup_variable_fmt("%_%_%",      pTarget, pSource, &Prop);
    paVars[iVar++].pVar = kbuild_lookup_variable_fmt("%_%_%.%",    pTarget, pSource, &Prop, pBldType);
    paVars[iVar++].pVar = kbuild_lookup_variable_fmt("%_%_%.%",    pTarget, pSource, &Prop, pBldTrg);
    paVars[iVar++].pVar = kbuild_lookup_variable_fmt("%_%_%.%",    pTarget, pSource, &Prop, pBldTrgArch);
    paVars[iVar++].pVar = kbuild_lookup_variable_fmt("%_%_%.%.%",  pTarget, pSource, &Prop, pBldTrg, pBldTrgArch);
    paVars[iVar++].pVar = kbuild_lookup_variable_fmt("%_%_%.%",    pTarget, pSource, &Prop, pBldTrgCpu);

    paVars[iVar++].pVar = kbuild_lookup_variable_fmt("%_%_%%",     pTarget, pSource, pType, &Prop);
    paVars[iVar++].pVar = kbuild_lookup_variable_fmt("%_%_%%.%",   pTarget, pSource, pType, &Prop, pBldType);
    paVars[iVar++].pVar = kbuild_lookup_variable_fmt("%_%_%%.%",   pTarget, pSource, pType, &Prop, pBldTrg);
    paVars[iVar++].pVar = kbuild_lookup_variable_fmt("%_%_%%.%",   pTarget, pSource, pType, &Prop, pBldTrgArch);
    paVars[iVar++].pVar = kbuild_lookup_variable_fmt("%_%_%%.%.%", pTarget, pSource, pType, &Prop, pBldTrg, pBldTrgArch);
    paVars[iVar++].pVar = kbuild_lookup_variable_fmt("%_%_%%.%",   pTarget, pSource, pType, &Prop, pBldTrgCpu);

    assert(cVars == iVar);

    /*
     * Expand the variables and calculate the total length.
     */
    cchTotal = 0;
    iVarEnd = iDirection > -1 ? cVars : 0;
    for (iVar = iDirection > 0 ? 0 : cVars - 1; iVar != iVarEnd; iVar += iDirection)
    {
        paVars[iVar].cchExp = 0;
        if (!paVars[iVar].pVar)
            continue;
        if (    paVars[iVar].pVar->flavor == f_simple
            ||  !strchr(paVars[iVar].pVar->value, '$'))
        {
            paVars[iVar].pszExp = paVars[iVar].pVar->value;
            paVars[iVar].cchExp = paVars[iVar].pVar->value_length;
        }
        else
        {
            paVars[iVar].pszExp = allocated_variable_expand(paVars[iVar].pVar->value);
            paVars[iVar].cchExp = strlen(paVars[iVar].pszExp);
        }
        cchTotal += paVars[iVar].cchExp + 1;
    }

    /*
     * Construct the result value.
     */
    psz = pszResult = xmalloc(cchTotal + 1);
    iVarEnd = iDirection > -1 ? cVars : 0;
    for (iVar = iDirection > 0 ? 0 : cVars - 1; iVar != iVarEnd; iVar += iDirection)
    {
        if (!paVars[iVar].cchExp)
            continue;
        memcpy(psz, paVars[iVar].pszExp, paVars[iVar].cchExp);
        psz += paVars[iVar].cchExp;
        *psz++ = ' ';
        if (paVars[iVar].pszExp != paVars[iVar].pVar->value)
            free(paVars[iVar].pszExp);
    }
    if (psz != pszResult)
        psz--;
    *psz = '\0';
    cchTotal = psz - pszResult;

    pVar = define_variable_vl(pszVarName, strlen(pszVarName), pszResult, cchTotal,
                              0 /* take pszResult */ , o_file, 0 /* !recursive */);
    return pVar;
}

/* get a source property. */
char *
func_kbuild_source_prop(char *o, char **argv, const char *pszFuncName)
{
    struct variable *pTarget = kbuild_get_variable("target");
    struct variable *pSource = kbuild_get_variable("source");
    struct variable *pType = kbuild_get_variable("type");
    struct variable *pTool = kbuild_get_variable("tool");
    struct variable *pBldType = kbuild_get_variable("bld_type");
    struct variable *pBldTrg = kbuild_get_variable("bld_trg");
    struct variable *pBldTrgArch = kbuild_get_variable("bld_trg_arch");
    struct variable *pBldTrgCpu = kbuild_get_variable("bld_trg_cpu");
    struct variable *pVar;
    struct kbuild_sdks Sdks;
    int iDirection;
    if (!strcmp(argv[2], "left-to-right"))
        iDirection = 1;
    else if (!strcmp(argv[2], "right-to-left"))
        iDirection = 1;
    else
        fatal(NILF, _("incorrect direction argument `%s'!"), argv[2]);

    kbuild_get_sdks(&Sdks, pTarget, pSource, pBldType, pBldTrg, pBldTrgArch);

    pVar = kbuild_collect_source_prop(pTarget, pSource, pTool, &Sdks, pType,
                                      pBldType, pBldTrg, pBldTrgArch, pBldTrgCpu,
                                      argv[0], argv[1], iDirection);
    if (pVar)
         o = variable_buffer_output(o, pVar->value, pVar->value_length);

    kbuild_put_sdks(&Sdks);
    return o;

}

/*
dep     := $(obj)$(SUFF_DEP)
obj     := $(outbase)$(objsuff)
dirdep  := $(call DIRDEP,$(dir $(outbase)))
PATH_$(target)_$(source) := $(patsubst %/,%,$(dir $(outbase)))
*/
static struct variable *
kbuild_set_object_name_and_dep_and_dirdep_and_PATH_target_source(struct variable *pTarget, struct variable *pSource,
                                                                 struct variable *pOutBase, struct variable *pObjSuff,
                                                                 const char *pszVarName, struct variable **ppDep,
                                                                 struct variable **ppDirDep)
{
    struct variable *pDepSuff = kbuild_get_variable("SUFF_DEP");
    struct variable *pObj;
    size_t cch = pOutBase->value_length + pObjSuff->value_length + pDepSuff->value_length + 1;
    char *pszResult = alloca(cch);
    char *pszName, *psz;

    /*
     * dep.
     */
    psz = pszResult;
    memcpy(psz, pOutBase->value, pOutBase->value_length);   psz += pOutBase->value_length;
    memcpy(psz, pObjSuff->value, pObjSuff->value_length);   psz += pObjSuff->value_length;
    memcpy(psz, pDepSuff->value, pDepSuff->value_length + 1);
    *ppDep = define_variable_vl("dep", 3, pszResult, cch - 1, 1 /*dup*/, o_file, 0 /* !recursive */);

    /*
     * obj
     */
    *psz = '\0';
    pObj = define_variable_vl(pszVarName, strlen(pszVarName), pszResult, psz - pszResult,
                              1/* dup */, o_file, 0 /* !recursive */);

    /*
     * PATH_$(target)_$(source) - this is global!
     */
    /* calc variable name. */
    cch = pTarget->value_length + pSource->value_length + 7;
    psz = pszName = alloca(cch + 1);
    memcpy(psz, "PATH_", sizeof("PATH_") - 1);          psz += sizeof("PATH_") - 1;
    memcpy(psz, pTarget->value, pTarget->value_length); psz += pTarget->value_length;
    *psz++ = '_';
    memcpy(psz, pSource->value, pSource->value_length + 1);

    /* strip off the filename. */
    psz = pszResult + pOutBase->value_length;
    for (;;)
    {
        psz--;
        if (psz <= pszResult)
            fatal(NULL, "whut!?! no path? result=`%s'", pszResult);
#ifdef HAVE_DOS_PATHS
        if (*psz == ':')
        {
            psz++;
            break;
        }
#endif
        if (    *psz == '/'
#ifdef HAVE_DOS_PATHS
            ||  *psz == '\\'
#endif
           )
        {
            while (     psz - 1 > pszResult
                   &&   psz[-1] == '/'
#ifdef HAVE_DOS_PATHS
                   ||   psz[-1] == '\\'
#endif
                  )
                psz--;
#ifdef HAVE_DOS_PATHS
            if (psz == pszResult + 2 && pszResult[1] == ':')
                psz++;
#endif
            break;
        }
    }
    *psz = '\0';

    /* set global variable */
    define_variable_vl_global(pszName, cch, pszResult, psz - pszResult, 1/*dup*/, o_file, 0 /* !recursive */, NILF);

    /*
     * dirdep
     */
    if (    psz[-1] != '/'
#ifdef HAVE_DOS_PATHS
        &&  psz[-1] != '\\'
        &&  psz[-1] != ':'
#endif
       )
    {
        *psz++ = '/';
        *psz = '\0';
    }
    *ppDirDep = define_variable_vl("dirdep", 6, pszResult, psz - pszResult, 1/*dup*/, o_file, 0 /* !recursive */);

    return pObj;
}


/* setup the base variables for def_target_source_c_cpp_asm_new:

X := $(kb-src-tool tool)
x := $(kb-obj-base outbase)
x := $(kb-obj-suff objsuff)
obj     := $(outbase)$(objsuff)
PATH_$(target)_$(source) := $(patsubst %/,%,$(dir $(outbase)))

x := $(kb-src-prop DEFS,defs,left-to-right)
x := $(kb-src-prop INCS,incs,right-to-left)
x := $(kb-src-prop FLAGS,flags,right-to-left)

x := $(kb-src-prop DEPS,deps,left-to-right)
dirdep  := $(call DIRDEP,$(dir $(outbase)))
dep     := $(obj)$(SUFF_DEP)
*/
char *
func_kbuild_source_one(char *o, char **argv, const char *pszFuncName)
{
    static int s_fNoCompileCmdsDepsDefined = -1;
    struct variable *pTarget    = kbuild_get_variable("target");
    struct variable *pSource    = kbuild_get_variable("source");
    struct variable *pType      = kbuild_get_variable("type");
    struct variable *pBldType   = kbuild_get_variable("bld_type");
    struct variable *pBldTrg    = kbuild_get_variable("bld_trg");
    struct variable *pBldTrgArch= kbuild_get_variable("bld_trg_arch");
    struct variable *pBldTrgCpu = kbuild_get_variable("bld_trg_cpu");
    struct variable *pTool      = kbuild_get_source_tool(pTarget, pSource, pType, pBldTrg, pBldTrgArch, "tool");
    struct variable *pOutBase   = kbuild_get_object_base(pTarget, pSource, "outbase");
    struct variable *pObjSuff   = kbuild_get_object_suffix(pTarget, pSource, pBldTrg, pBldTrgArch, "objsuff");
    struct variable *pDefs, *pIncs, *pFlags, *pDeps, *pDirDep, *pDep, *pVar, *pOutput;
    struct variable *pObj       = kbuild_set_object_name_and_dep_and_dirdep_and_PATH_target_source(pTarget, pSource, pOutBase, pObjSuff, "obj", &pDep, &pDirDep);
    char *pszDstVar, *pszDst, *pszSrcVar, *pszSrc, *pszVal, *psz;
    char *pszSavedVarBuf;
    unsigned cchSavedVarBuf;
    size_t cch;
    struct kbuild_sdks Sdks;
    kbuild_get_sdks(&Sdks, pTarget, pSource, pBldType, pBldTrg, pBldTrgArch);

    pDefs  = kbuild_collect_source_prop(pTarget, pSource, pTool, &Sdks, pType, pBldType, pBldTrg, pBldTrgArch, pBldTrgCpu,
                                        "DEFS", "defs", 1/* left-to-right */);
    pIncs  = kbuild_collect_source_prop(pTarget, pSource, pTool, &Sdks, pType, pBldType, pBldTrg, pBldTrgArch, pBldTrgCpu,
                                        "INCS", "incs", 1/* right-to-left */);
    pFlags = kbuild_collect_source_prop(pTarget, pSource, pTool, &Sdks, pType, pBldType, pBldTrg, pBldTrgArch, pBldTrgCpu,
                                        "FLAGS", "flags", 1/* right-to-left */);
    pDeps  = kbuild_collect_source_prop(pTarget, pSource, pTool, &Sdks, pType, pBldType, pBldTrg, pBldTrgArch, pBldTrgCpu,
                                        "DEPS", "deps", 1/* left-to-right */);

    /*
    # dependencies
    ifndef NO_COMPILE_CMDS_DEPS
    _DEPFILES_INCLUDED += $(dep)
    $(if $(wildcard $(dep)),$(eval include $(dep)))
    endif
     */
    if (s_fNoCompileCmdsDepsDefined == -1)
        s_fNoCompileCmdsDepsDefined = kbuild_lookup_variable("NO_COMPILE_CMDS_DEPS") != NULL;
    if (!s_fNoCompileCmdsDepsDefined)
    {
        do_variable_definition(NILF, "_DEPFILES_INCLUDED", pDep->value, o_file, f_append, 0 /* !target_var */);
        eval_include_dep(pDep->value, NILF);
    }

    /*
    # call the tool
    $(target)_$(source)_CMDS_   := $(TOOL_$(tool)_COMPILE_$(type)_CMDS)
    $(target)_$(source)_OUTPUT_ := $(TOOL_$(tool)_COMPILE_$(type)_OUTPUT)
    $(target)_$(source)_DEPEND_ := $(TOOL_$(tool)_COMPILE_$(type)_DEPEND) $(deps) $(source)
    $(target)_$(source)_DEPORD_ := $(TOOL_$(tool)_COMPILE_$(type)_DEPORD) $(dirdep)
    */
    cch = sizeof("TOOL_") + pTool->value_length + sizeof("_COMPILE_") + pType->value_length + sizeof("_OUTPUT");
    psz = pszSrcVar = alloca(cch);
    memcpy(psz, "TOOL_", sizeof("TOOL_") - 1);          psz += sizeof("TOOL_") - 1;
    memcpy(psz, pTool->value, pTool->value_length);     psz += pTool->value_length;
    memcpy(psz, "_COMPILE_", sizeof("_COMPILE_") - 1);  psz += sizeof("_COMPILE_") - 1;
    memcpy(psz, pType->value, pType->value_length);     psz += pType->value_length;
    pszSrc = psz;

    cch = pTarget->value_length + 1 + pSource->value_length + sizeof("_OUTPUT_");
    psz = pszDstVar = alloca(cch);
    memcpy(psz, pTarget->value, pTarget->value_length); psz += pTarget->value_length;
    *psz++ = '_';
    memcpy(psz, pSource->value, pSource->value_length); psz += pSource->value_length;
    pszDst = psz;

    memcpy(pszSrc, "_CMDS", sizeof("_CMDS"));
    memcpy(pszDst, "_CMDS_", sizeof("_CMDS_"));
    pVar = kbuild_get_recursive_variable(pszSrcVar);
    do_variable_definition(NILF, pszDstVar, pVar->value, o_file, f_simple, 0 /* !target_var */);

    memcpy(pszSrc, "_OUTPUT", sizeof("_OUTPUT"));
    memcpy(pszDst, "_OUTPUT_", sizeof("_OUTPUT_"));
    pVar = kbuild_get_recursive_variable(pszSrcVar);
    pOutput = do_variable_definition(NILF, pszDstVar, pVar->value, o_file, f_simple, 0 /* !target_var */);

    memcpy(pszSrc, "_DEPEND", sizeof("_DEPEND"));
    memcpy(pszDst, "_DEPEND_", sizeof("_DEPEND_"));
    pVar = kbuild_get_recursive_variable(pszSrcVar);
    psz = pszVal = xmalloc(pVar->value_length + 1 + pDeps->value_length + 1 + pSource->value_length + 1);
    memcpy(psz, pVar->value, pVar->value_length);       psz += pVar->value_length;
    *psz++ = ' ';
    memcpy(psz, pDeps->value, pDeps->value_length);     psz += pDeps->value_length;
    *psz++ = ' ';
    memcpy(psz, pSource->value, pSource->value_length + 1);
    do_variable_definition(NILF, pszDstVar, pszVal, o_file, f_simple, 0 /* !target_var */);
    free(pszVal);

    memcpy(pszSrc, "_DEPORD", sizeof("_DEPORD"));
    memcpy(pszDst, "_DEPORD_", sizeof("_DEPORD_"));
    pVar = kbuild_get_recursive_variable(pszSrcVar);
    psz = pszVal = xmalloc(pVar->value_length + 1 + pDirDep->value_length + 1);
    memcpy(psz, pVar->value, pVar->value_length);       psz += pVar->value_length;
    *psz++ = ' ';
    memcpy(psz, pDirDep->value, pDirDep->value_length + 1);
    do_variable_definition(NILF, pszDstVar, pszVal, o_file, f_simple, 0 /* !target_var */);
    free(pszVal);

    /*
    _OUT_FILES      += $($(target)_$(source)_OUTPUT_)
    */
    pVar = kbuild_get_variable("_OUT_FILES");
    psz = pszVal = xmalloc(pVar->value_length + 1 + pOutput->value_length + 1);
    memcpy(psz, pVar->value, pVar->value_length); psz += pVar->value_length;
    *psz++ = ' ';
    memcpy(psz, pOutput->value, pOutput->value_length + 1);
    do_variable_definition(NILF, "_OUT_FILES", pszVal, o_file, f_simple, 0 /* !target_var */);
    free(pszVal);

    /*
    $(target)_OBJS_ += $(obj)
    */
    memcpy(pszDstVar + pTarget->value_length, "_OBJS_", sizeof("_OBJS_"));
    do_variable_definition(NILF, pszDstVar, pObj->value, o_file, f_append, 0 /* !target_var */);

    /*
    $(eval $(def_target_source_rule))
    */
    pVar = kbuild_get_recursive_variable("def_target_source_rule");
    pszVal = allocated_variable_expand(pVar->value);

    install_variable_buffer(&pszSavedVarBuf, &cchSavedVarBuf);
    eval_buffer(pszVal);
    restore_variable_buffer(pszSavedVarBuf, cchSavedVarBuf);

    free(pszVal);

    kbuild_put_sdks(&Sdks);
    return variable_buffer_output(o, "", 1);
}


#endif /* KMK_HELPERS */
