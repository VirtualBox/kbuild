/* $Id: kbuild-read.c 2548 2011-11-08 21:28:16Z knut.osmundsen@oracle.com $ */
/** @file
 * kBuild specific make functionality related to read.c.
 */

/*
 * Copyright (c) 2011 knut st. osmundsen <bird-kBuild-spamx@anduin.net>
 *
 * This file is part of kBuild.
 *
 * kBuild is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * kBuild is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with kBuild.  If not, see <http://www.gnu.org/licenses/>
 *
 */

/* No GNU coding style here! */

/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
#include "make.h"
#include "filedef.h"
#include "variable.h"
#include "dep.h"
#include "debug.h"
#include "kbuild.h"

#include <assert.h>


/*******************************************************************************
*   Defined Constants And Macros                                               *
*******************************************************************************/
#define WORD_IS(a_pszWord, a_cchWord, a_szWord2) \
        (  (a_cchWord) == sizeof(a_szWord2) - 1 && memcmp((a_pszWord), a_szWord2, sizeof(a_szWord2) - 1) == 0)


/*******************************************************************************
*   Structures and Typedefs                                                    *
*******************************************************************************/
/** Indicate which kind of kBuild define we're working on.  */
enum kBuildDef
{
    kBuildDef_Invalid,
    kBuildDef_Target,
    kBuildDef_Template,
    kBuildDef_Tool,
    kBuildDef_Sdk,
    kBuildDef_Unit
};

enum kBuildExtendBy
{
    kBuildExtendBy_NoParent,
    kBuildExtendBy_Overriding,
    kBuildExtendBy_Appending,
    kBuildExtendBy_Prepending
};


/**
 * The data we stack during eval.
 */
struct kbuild_eval_data
{
    /** The kind of define. */
    enum kBuildDef              enmKind;
    /** Pointer to the element below us on the stack. */
    struct kbuild_eval_data    *pDown;

    /** The bare name of the define. */
    char                       *pszName;

    /** The parent name, NULL if none. */
    char                       *pszParent;
    /** The inheritance method. */
    enum kBuildExtendBy         enmExtendBy;

    /** The template, NULL if none. Only applicable to targets. */
    char                       *pszTemplate;

    /** The variable prefix*/
    char                       *pszVarPrefix;
    /** The length of the variable prefix. */
    size_t                      cchVarPrefix;
};


static const char *
eval_kbuild_kind_to_string(enum kBuildDef enmKind)
{
    switch (enmKind)
    {
        case kBuildDef_Target:      return "target";
        case kBuildDef_Template:    return "template";
        case kBuildDef_Tool:        return "tool";
        case kBuildDef_Sdk:         return "sdk";
        case kBuildDef_Unit:        return "unit";
        default:
        case kBuildDef_Invalid:     return "invalid";
    }
}


static int
eval_kbuild_define_xxxx(struct kbuild_eval_data **ppData, const struct floc *pFileLoc,
                        const char *pszLine, const char *pszEos, int fIgnoring, enum kBuildDef enmKind)
{
    unsigned int             cch;
    char                    *psz;
    struct kbuild_eval_data *pData;

    /*
     * Create a new kBuild eval data item unless we're in ignore-mode.
     */
    if (fIgnoring)
        return 0;

    pData = xmalloc(sizeof(*pData));
    pData->enmKind      = enmKind;
    pData->pszName      = NULL;
    pData->pszParent    = NULL;
    pData->enmExtendBy  = kBuildExtendBy_NoParent;
    pData->pszTemplate  = NULL;
    pData->pszVarPrefix = NULL;
    pData->cchVarPrefix = 0;
    pData->pDown        = *ppData;
    *ppData             = pData;

    /*
     * The first word is the name.
     */
    pData->pszName = find_next_token_eos(&pszLine, pszEos, &cch);
    if (!pData->pszName)
    {
        error(pFileLoc, _("The kBuild define requires a name"));
        return 0;
    }
    pData->pszName = allocated_variable_expand_2(pData->pszName, cch, &cch);

    /*
     * Parse subsequent words.
     */
    psz = find_next_token_eos(&pszLine, pszEos, &cch);
    while (psz)
    {
        if (WORD_IS(psz, cch, "extending"))
        {
            /* Inheritance directive. */
            if (pData->pszParent != NULL)
                fatal(pFileLoc, _("'extending' can only occure once"));
            pData->pszParent = find_next_token_eos(&pszLine, pszEos, &cch);
            if (pData->pszParent)
                pData->pszParent = allocated_variable_expand_2(pData->pszParent, cch, &cch);
            if (!pData->pszParent || !*pData->pszParent)
                fatal(pFileLoc, _("'extending' requires a parent name"));

            pData->enmExtendBy = kBuildExtendBy_Overriding;

            /* optionally 'by overriding|prepending|appending' */
            psz = find_next_token_eos(&pszLine, pszEos, &cch);
            if (psz && WORD_IS(psz, cch, "by"))
            {
                cch = 0;
                psz = find_next_token_eos(&pszLine, pszEos, &cch);
                if (WORD_IS(psz, cch, "overriding"))
                    pData->enmExtendBy = kBuildExtendBy_Overriding;
                else if (WORD_IS(psz, cch, "appending"))
                    pData->enmExtendBy = kBuildExtendBy_Appending;
                else if (WORD_IS(psz, cch, "prepending"))
                    pData->enmExtendBy = kBuildExtendBy_Prepending;
                else
                    fatal(pFileLoc, _("Unknown 'extending by' method '%.*s'"), (int)cch, psz);

                /* next token */
                psz = find_next_token_eos(&pszLine, pszEos, &cch);
            }
        }
        else if (   WORD_IS(psz, cch, "using")
                 && enmKind == kBuildDef_Tool)
        {
            /* Template directive. */
            if (pData->pszTemplate != NULL )
                fatal(pFileLoc, _("'using' can only occure once"));
            pData->pszTemplate = find_next_token_eos(&pszLine, pszEos, &cch);
            if (pData->pszTemplate)
                pData->pszTemplate = allocated_variable_expand_2(pData->pszTemplate, cch, &cch);
            if (!pData->pszTemplate || !*pData->pszTemplate)
                fatal(pFileLoc, _("'using' requires a template name"));

            /* next token */
            psz = find_next_token_eos(&pszLine, pszEos, &cch);
        }
        else
            fatal(pFileLoc, _("Don't know what '%.*s' means"), (int)cch, psz);
    }

    /*
     * Calc the variable prefix.
     */

    /** @todo continue here...  */


    return 0;
}

static int
eval_kbuild_endef_xxxx(struct kbuild_eval_data **ppData, const struct floc *pFileLoc,
                       const char *pszLine, const char *pszEos, int fIgnoring, enum kBuildDef enmKind)
{
    struct kbuild_eval_data *pData;
    unsigned int             cchName;
    char                    *pszName;

    if (fIgnoring)
        return 0;

    /*
     * Is there something to pop?
     */
    pData = *ppData;
    if (!pData)
    {
        error(pFileLoc, _("kBuild-endef-%s is missing kBuild-define-%s"),
              eval_kbuild_kind_to_string(enmKind), eval_kbuild_kind_to_string(enmKind));
        return 0;
    }

    /*
     * ... and does it have a matching kind?
     */
    if (pData->enmKind != enmKind)
    {
        error(pFileLoc, _("'kBuild-endef-%s' does not match 'kBuild-define-%s %s'"),
              eval_kbuild_kind_to_string(enmKind), eval_kbuild_kind_to_string(pData->enmKind), pData->pszName);
        return 0;
    }

    /*
     * The endef-kbuild may optionally be followed by the target name.
     * It should match the name given to the kBuild-define.
     */
    pszName = find_next_token_eos(&pszLine, pszEos, &cchName);
    if (pszName)
    {
        pszName = allocated_variable_expand_2(pszName, cchName, &cchName);
        if (strcmp(pszName, pData->pszName))
            error(pFileLoc, _("'kBuild-endef-%s %s' does not match 'kBuild-define-%s %s'"),
                  eval_kbuild_kind_to_string(enmKind), pszName,
                  eval_kbuild_kind_to_string(pData->enmKind), pData->pszName);
        free(pszName);
    }

    /*
     * Pop a define off the stack.
     */
    *ppData = pData->pDown;
    free(pData->pszName);
    free(pData->pszParent);
    free(pData->pszTemplate);
    free(pData->pszVarPrefix);
    free(pData);

    return 0;
}

int eval_kbuild_define(struct kbuild_eval_data **kdata, const struct floc *flocp,
                       const char *word, unsigned int wlen, const char *line, const char *eos, int ignoring)
{
    assert(memcmp(word, "kBuild-define", sizeof("kBuild-define") - 1) == 0);
    word += sizeof("kBuild-define") - 1;
    wlen -= sizeof("kBuild-define") - 1;
    if (   wlen > 1
        && word[0] == '-')
    {
        if (WORD_IS(word, wlen, "-target"))
            return eval_kbuild_define_xxxx(kdata, flocp, line, eos,  ignoring, kBuildDef_Target);
        if (WORD_IS(word, wlen, "-template"))
            return eval_kbuild_define_xxxx(kdata, flocp, line, eos,  ignoring, kBuildDef_Template);
        if (WORD_IS(word, wlen, "-tool"))
            return eval_kbuild_define_xxxx(kdata, flocp, line, eos,  ignoring, kBuildDef_Tool);
        if (WORD_IS(word, wlen, "-sdk"))
            return eval_kbuild_define_xxxx(kdata, flocp, line, eos,  ignoring, kBuildDef_Sdk);
        if (WORD_IS(word, wlen, "-unit"))
            return eval_kbuild_define_xxxx(kdata, flocp, line, eos,  ignoring, kBuildDef_Unit);
    }

    error(flocp, _("Unknown syntax 'kBuild-define%.*s'"), (int)wlen, word);
    return 0;
}

int eval_kbuild_endef(struct kbuild_eval_data **kdata, const struct floc *flocp,
                      const char *word, unsigned int wlen, const char *line, const char *eos, int ignoring)
{
    assert(memcmp(word, "kBuild-endef", sizeof("kBuild-endef") - 1) == 0);
    word += sizeof("kBuild-endef") - 1;
    wlen -= sizeof("kBuild-endef") - 1;
    if (   wlen > 1
        && word[0] == '-')
    {
        if (WORD_IS(word, wlen, "-target"))
            return eval_kbuild_endef_xxxx(kdata, flocp, line, eos,  ignoring, kBuildDef_Target);
        if (WORD_IS(word, wlen, "-template"))
            return eval_kbuild_endef_xxxx(kdata, flocp, line, eos,  ignoring, kBuildDef_Template);
        if (WORD_IS(word, wlen, "-tool"))
            return eval_kbuild_endef_xxxx(kdata, flocp, line, eos,  ignoring, kBuildDef_Tool);
        if (WORD_IS(word, wlen, "-sdk"))
            return eval_kbuild_endef_xxxx(kdata, flocp, line, eos,  ignoring, kBuildDef_Sdk);
        if (WORD_IS(word, wlen, "-unit"))
            return eval_kbuild_endef_xxxx(kdata, flocp, line, eos,  ignoring, kBuildDef_Unit);
    }

    error(flocp, _("Unknown syntax 'kBuild-endef%.*s'"), (int)wlen, word);
    return 0;
}

