/* $Id: kbuild-read.c 2549 2011-11-09 01:22:04Z knut.osmundsen@oracle.com $ */
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
    /** The bare name of the define. */
    char                       *pszName;
    /** The file location where this define was declared. */
    struct floc                 FileLoc;

    /** Pointer to the next element in the global list. */
    struct kbuild_eval_data    *pGlobalNext;
    /** Pointer to the element below us on the stack. */
    struct kbuild_eval_data    *pStackDown;

    /** The variable set associated with this define. */
    struct variable_set_list   *pVariables;
    /** The saved current variable set, for restoring in kBuild-endef. */
    struct variable_set_list   *pVariablesSaved;

    /** The parent name, NULL if none. */
    char                       *pszParent;
    /** The inheritance method. */
    enum kBuildExtendBy         enmExtendBy;
    /** Pointer to the parent. Resolved lazily, so it can be NULL even if we have
     *  a parent. */
    struct kbuild_eval_data    *pParent;

    /** The template, NULL if none. Only applicable to targets. */
    char                       *pszTemplate;
    /** Pointer to the template. Resolved lazily, so it can be NULL even if we have
     *  a parent. */
    struct kbuild_eval_data    *pTemplate;

    /** The variable prefix.  */
    char                       *pszVarPrefix;
    /** The length of the variable prefix. */
    size_t                      cchVarPrefix;
};


/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
/** Linked list (LIFO) of kBuild defines.
 * @todo use a hash! */
struct kbuild_eval_data *g_pHeadKbDefs = NULL;
/** Stack of kBuild defines. */
struct kbuild_eval_data *g_pTopKbDef = NULL;


struct variable_set *
get_top_kbuild_variable_set(void)
{
    struct kbuild_eval_data *pTop = g_pTopKbDef;
    assert(pTop != NULL);
    return pTop->pVariables->set;
}


char *
kbuild_prefix_variable(const char *pszName, unsigned int *pcchName)
{
    struct kbuild_eval_data *pTop = g_pTopKbDef;
    char        *pszPrefixed;
    unsigned int cchPrefixed;

    assert(pTop != NULL);

    cchPrefixed = pTop->cchVarPrefix + *pcchName;
    pszPrefixed = xmalloc(cchPrefixed + 1);
    memcpy(pszPrefixed, pTop->pszVarPrefix, pTop->cchVarPrefix);
    memcpy(&pszPrefixed[pTop->cchVarPrefix], pszName, *pcchName);
    pszPrefixed[cchPrefixed] = '\0';
    *pcchName = cchPrefixed;
    return pszPrefixed;
}


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

static char *
allocate_expanded_next_token(const char **ppszCursor, const char *pszEos, unsigned int *pcchToken, int fStrip)
{
    unsigned int cchToken;
    char *pszToken = find_next_token_eos(ppszCursor, pszEos, &cchToken);
    if (pszToken)
    {
        pszToken = allocated_variable_expand_2(pszToken, cchToken, &cchToken);
        if (pszToken)
        {
            if (fStrip)
            {
                unsigned int off = 0;
                while (MY_IS_BLANK(pszToken[off]))
                    off++;
                if (off)
                {
                    cchToken -= off;
                    memmove(pszToken, &pszToken[off], cchToken + 1);
                }

                while (cchToken > 0 && MY_IS_BLANK(pszToken[cchToken - 1]))
                    pszToken[--cchToken] = '\0';
            }

            assert(cchToken == strlen(pszToken));
            if (pcchToken)
                *pcchToken = cchToken;
        }
    }
    return pszToken;
}

static struct kbuild_eval_data *
eval_kbuild_resolve_parent(struct kbuild_eval_data *pData)
{
    if (   !pData->pParent
        && pData->pszParent)
    {
        struct kbuild_eval_data *pCur = g_pHeadKbDefs;
        while (pCur)
        {
            if (   pCur->enmKind == pData->enmKind
                && !strcmp(pCur->pszName, pData->pszParent))
            {
                if (    pCur->pszParent
                    &&  (   pCur->pParent == pData
                         || !strcmp(pCur->pszParent, pData->pszName)) )
                    fatal(&pData->FileLoc, _("'%s' and '%s' are both trying to be each other children..."),
                          pData->pszName, pCur->pszName);

                pData->pParent = pCur;
                pData->pVariables->next = pData->pVariables;
                break;
            }
            pCur = pCur->pGlobalNext;
        }
    }
    return pData->pParent;
}

static int
eval_kbuild_define_xxxx(struct kbuild_eval_data **ppData, const struct floc *pFileLoc,
                        const char *pszLine, const char *pszEos, int fIgnoring, enum kBuildDef enmKind)
{
    unsigned int            cch;
    unsigned int            cchName;
    char                    ch;
    char                   *psz;
    const char             *pszPrefix;
    struct kbuild_eval_data *pData;

    if (fIgnoring)
        return 0;

    /*
     * Create a new kBuild eval data item.
     */
    pData = xmalloc(sizeof(*pData));
    pData->enmKind          = enmKind;
    pData->pszName          = NULL;
    pData->FileLoc          = *pFileLoc;

    pData->pGlobalNext      = g_pHeadKbDefs;
    g_pHeadKbDefs           = pData;

    pData->pStackDown       = *ppData;
    *ppData = g_pTopKbDef   = pData;
    pData->pVariables       = create_new_variable_set();
    pData->pVariablesSaved  = NULL;

    pData->pszParent        = NULL;
    pData->enmExtendBy      = kBuildExtendBy_NoParent;
    pData->pParent          = NULL;

    pData->pszTemplate      = NULL;
    pData->pTemplate        = NULL;

    pData->pszVarPrefix     = NULL;
    pData->cchVarPrefix     = 0;

    /*
     * The first word is the name.
     */
    pData->pszName = allocate_expanded_next_token(&pszLine, pszEos, &cchName, 1 /*strip*/);
    if (!pData->pszName || !*pData->pszName)
        fatal(pFileLoc, _("The kBuild define requires a name"));

    psz = pData->pszName;
    while ((ch = *psz++) != '\0')
        if (!isgraph(ch))
        {
            error(pFileLoc, _("The 'kBuild-define-%s' name '%s' contains one or more invalid characters"),
                  eval_kbuild_kind_to_string(enmKind), pData->pszName);
            break;
        }

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
            pData->pszParent = allocate_expanded_next_token(&pszLine, pszEos, &cch, 1 /*strip*/);
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
        else if (WORD_IS(psz, cch, "using"))
        {
            /* Template directive. */
            if (enmKind != kBuildDef_Tool)
                fatal(pFileLoc, _("'using <template>' can only be used with 'kBuild-define-target'"));
            if (pData->pszTemplate != NULL)
                fatal(pFileLoc, _("'using' can only occure once"));

            pData->pszTemplate = allocate_expanded_next_token(&pszLine, pszEos, &cch, 1 /*fStrip*/);
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
    switch (enmKind)
    {
        case kBuildDef_Target:      pszPrefix = ""; break;
        case kBuildDef_Template:    pszPrefix = "TEMPLATE_"; break;
        case kBuildDef_Tool:        pszPrefix = "TOOL_"; break;
        case kBuildDef_Sdk:         pszPrefix = "SDK_"; break;
        case kBuildDef_Unit:        pszPrefix = "UNIT_"; break;
        default:
            fatal(pFileLoc, _("enmKind=%d"), enmKind);
            return -1;
    }
    cch = strlen(pszPrefix);
    pData->cchVarPrefix = cch + cchName;
    pData->pszVarPrefix = xmalloc(pData->cchVarPrefix + 1);
    memcpy(pData->pszVarPrefix, pszPrefix, cch);
    memcpy(&pData->pszVarPrefix[cch], pData->pszName, cchName);

    /*
     * Try resolve the parent and change the current variable set.
     */
    eval_kbuild_resolve_parent(pData);
    pData->pVariablesSaved = current_variable_set_list;
    current_variable_set_list = pData->pVariables;

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
        error(pFileLoc, _("'kBuild-endef-%s' does not match 'kBuild-define-%s %s'"),
              eval_kbuild_kind_to_string(enmKind), eval_kbuild_kind_to_string(pData->enmKind), pData->pszName);

    /*
     * The endef-kbuild may optionally be followed by the target name.
     * It should match the name given to the kBuild-define.
     */
    pszName = allocate_expanded_next_token(&pszLine, pszEos, &cchName, 1 /*fStrip*/);
    if (pszName)
    {
        if (strcmp(pszName, pData->pszName))
            error(pFileLoc, _("'kBuild-endef-%s %s' does not match 'kBuild-define-%s %s'"),
                  eval_kbuild_kind_to_string(enmKind), pszName,
                  eval_kbuild_kind_to_string(pData->enmKind), pData->pszName);
        free(pszName);
    }

    /*
     * Pop a define off the stack.
     */
    assert(pData == g_pTopKbDef);
    *ppData = g_pTopKbDef  = pData->pStackDown;
    pData->pStackDown      = NULL;
    current_variable_set_list = pData->pVariablesSaved;
    pData->pVariablesSaved = NULL;

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

void print_kbuild_data_base(void)
{
    struct kbuild_eval_data *pCur;

    puts(_("\n# kBuild defines"));

    for (pCur = g_pHeadKbDefs; pCur; pCur = pCur->pGlobalNext)
    {
        printf("\nkBuild-define-%s %s",
               eval_kbuild_kind_to_string(pCur->enmKind), pCur->pszName);
        if (pCur->pszParent)
        {
            printf(" extending %s", pCur->pszParent);
            switch (pCur->enmExtendBy)
            {
                case kBuildExtendBy_Overriding: break;
                case kBuildExtendBy_Appending:  printf(" by appending"); break;
                case kBuildExtendBy_Prepending: printf(" by prepending"); break;
                default:                        printf(" by ?!?");
            }
        }
        if (pCur->pszTemplate)
            printf(" using %s", pCur->pszTemplate);
        putchar('\n');

        print_variable_set(pCur->pVariables->set, "");

        printf("kBuild-endef-%s  %s\n",
               eval_kbuild_kind_to_string(pCur->enmKind), pCur->pszName);
    }
    /** @todo hash stats. */
}

void print_kbuild_define_stats(void)
{
    /* later when hashing stuff */
}

