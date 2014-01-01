/* $Id: kbuild-object.c 2720 2014-01-01 22:59:50Z knut.osmundsen@oracle.com $ */
/** @file
 * kBuild objects.
 */

/*
 * Copyright (c) 2011-2014 knut st. osmundsen <bird-kBuild-spamx@anduin.net>
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
#include <stdarg.h>


/*******************************************************************************
*   Defined Constants And Macros                                               *
*******************************************************************************/
#define WORD_IS(a_pszWord, a_cchWord, a_szWord2) \
        (  (a_cchWord) == sizeof(a_szWord2) - 1 && memcmp((a_pszWord), a_szWord2, sizeof(a_szWord2) - 1) == 0)


/*******************************************************************************
*   Structures and Typedefs                                                    *
*******************************************************************************/
/** kBuild object type.  */
enum kBuildType
{
    kBuildType_Invalid,
    kBuildType_Target,
    kBuildType_Template,
    kBuildType_Tool,
    kBuildType_Sdk,
    kBuildType_Unit
};

enum kBuildSeverity
{
    kBuildSeverity_Warning,
    kBuildSeverity_Error,
    kBuildSeverity_Fatal
};


/**
 * kBuild object data.
 */
struct kbuild_object
{
    /** The object type. */
    enum kBuildType             enmType;
    /** Object name length.  */
    size_t                      cchName;
    /** The bare name of the define. */
    char                       *pszName;
    /** The file location where this define was declared. */
    struct floc                 FileLoc;

    /** Pointer to the next element in the global list. */
    struct kbuild_object       *pGlobalNext;

    /** The variable set associated with this define. */
    struct variable_set_list   *pVariables;

    /** The parent name, NULL if none. */
    char                       *pszParent;
    /** The length of the parent name. */
    size_t                      cchParent;
    /** Pointer to the parent.  Resolved lazily, so it can be NULL even if we
     * have a parent. */
    struct kbuild_object       *pParent;

    /** The template, NULL if none.  Only applicable to targets.  Only covers the
     * primary template, not target or type specific templates.
     * @todo not sure if this is really necessary. */
    char const                 *pszTemplate;

    /** The variable prefix. */
    char                       *pszVarPrefix;
    /** The length of the variable prefix. */
    size_t                      cchVarPrefix;
};


/**
 * The data we stack during eval.
 */
struct kbuild_eval_data
{
    /** Pointer to the element below us on the stack. */
    struct kbuild_eval_data    *pStackDown;
    /** Pointer to the object. */
    struct kbuild_object       *pObj;
    /** The saved current variable set, for restoring in kBuild-endef. */
    struct variable_set_list   *pVariablesSaved;
};



/*******************************************************************************
*   Global Variables                                                           *
*******************************************************************************/
/** Linked list (LIFO) of kBuild defines.
 * @todo use a hash! */
static struct kbuild_object    *g_pHeadKbObjs = NULL;
/** Stack of kBuild evalutation contexts.
 * This is for dealing with potential recursive kBuild object definition,
 * generally believed to only happen via $(eval ) or include similar. */
struct kbuild_eval_data        *g_pTopKbEvalData = NULL;

/** Cached variable name '_TEMPLATE'.  */
static const char              *g_pszVarNmTemplate = NULL;

/** Zero if compatibility mode is disabled, non-zero if enabled.
 * If explicitily enabled, the value will be greater than 1. */
int                             g_fKbObjCompMode = 1;


/*******************************************************************************
*   Internal Functions                                                         *
*******************************************************************************/
static struct kbuild_object *
resolve_kbuild_object_parent(struct kbuild_object *pObj, int fQuiet);
static struct kbuild_object *
get_kbuild_object_parent(struct kbuild_object *pObj, enum kBuildSeverity enmSeverity);

static struct kbuild_object *
parse_kbuild_object_variable_accessor(const char *pchExpr, size_t cchExpr,
                                      enum kBuildSeverity enmSeverity, const struct floc *pFileLoc,
                                      const char **ppchVarNm, size_t *pcchVarNm, enum kBuildType *penmType);


/**
 * Initializes the kBuild object stuff.
 *
 * Requires the variable_cache to be initialized.
 */
void init_kbuild_object(void)
{
    g_pszVarNmTemplate = strcache2_add(&variable_strcache, STRING_SIZE_TUPLE("_TEMPLATE"));
}


/**
 * Reports a problem with dynamic severity level.
 *
 * @param   enmSeverity         The severity level.
 * @param   pFileLoc            The file location.
 * @param   pszFormat           The format string.
 * @param   ...                 Arguments for the format string.
 */
static void kbuild_report_problem(enum kBuildSeverity enmSeverity, const struct floc *pFileLoc,
                                  const char *pszFormat, ...)
{
    char    szBuf[8192];
    va_list va;

    va_start(va, pszFormat);
#ifdef _MSC_VER
    _vsnprintf(szBuf, sizeof(szBuf), pszFormat, va);
#else
    vsnprintf(szBuf, sizeof(szBuf), pszFormat, va);
#endif
    va_end(va);

    switch (enmSeverity)
    {
        case kBuildSeverity_Warning:
            message(0, "%s", szBuf);
            break;
        case kBuildSeverity_Error:
            error(pFileLoc, "%s", szBuf);
            break;
        default:
        case kBuildSeverity_Fatal:
            fatal(pFileLoc, "%s", szBuf);
            break;
    }
}


static const char *
eval_kbuild_type_to_string(enum kBuildType enmType)
{
    switch (enmType)
    {
        case kBuildType_Target:      return "target";
        case kBuildType_Template:    return "template";
        case kBuildType_Tool:        return "tool";
        case kBuildType_Sdk:         return "sdk";
        case kBuildType_Unit:        return "unit";
        default:
        case kBuildType_Invalid:     return "invalid";
    }
}

/**
 * Gets the length of the string representation of the given type.
 *
 * @returns The string length.
 * @param   enmType             The kBuild object type in question.
 */
static unsigned
eval_kbuild_type_to_string_length(enum kBuildType enmType)
{
    switch (enmType)
    {
        case kBuildType_Target:      return sizeof("target") - 1;
        case kBuildType_Template:    return sizeof("template") - 1;
        case kBuildType_Tool:        return sizeof("tool") - 1;
        case kBuildType_Sdk:         return sizeof("sdk") - 1;
        case kBuildType_Unit:        return sizeof("unit") - 1;
        default:
        case kBuildType_Invalid:     return sizeof("invalid") - 1;
    }
}

/**
 * Converts a string into an kBuild object type.
 *
 * @returns The type on success, kBuildType_Invalid on failure.
 * @param   pchWord             The pchWord.  Not necessarily zero terminated.
 * @param   cchWord             The length of the word.
 */
static enum kBuildType
eval_kbuild_type_from_string(const char *pchWord, size_t cchWord)
{
    if (cchWord >= 3)
    {
        if (*pchWord == 't')
        {
            if (WORD_IS(pchWord, cchWord, "target"))
                return kBuildType_Target;
            if (WORD_IS(pchWord, cchWord, "template"))
                return kBuildType_Template;
            if (WORD_IS(pchWord, cchWord, "tool"))
                return kBuildType_Tool;
        }
        else
        {
            if (WORD_IS(pchWord, cchWord, "sdk"))
                return kBuildType_Sdk;
            if (WORD_IS(pchWord, cchWord, "unit"))
                return kBuildType_Unit;
        }
    }

    return kBuildType_Invalid;
}



/**
 * Helper function for caching variable name strings.
 *
 * @returns The string cache variable name.
 * @param   pszName             The variable name.
 * @param   ppszCache           Cache variable, static or global.  Initialize to
 *                              NULL.
 */
static const char *
kbuild_variable_name(const char *pszName, const char **ppszCache)
{
    const char *pszRet = *ppszCache;
    if (!pszRet)
        *ppszCache = pszRet = strcache2_add(&variable_strcache, pszName, strlen(pszName));
    return pszRet;
}

static struct kbuild_object *
lookup_kbuild_object(enum kBuildType enmType, const char *pchName, size_t cchName)
{
    /* Linear lookup for now. */
    struct kbuild_object *pCur = g_pHeadKbObjs;
    while (pCur)
    {
        if (   pCur->enmType == enmType
            && pCur->cchName == cchName
            && !memcmp(pCur->pszName, pchName, cchName))
            return pCur;
        pCur = pCur->pGlobalNext;
    }
    return NULL;
}


/** @name Defining and modifying variables
 * @{
 */

/**
 * Checks if the variable name is valid.
 *
 * @returns 1 if valid, 0 if not.
 * @param   pchName             The variable name.
 * @param   cchName             The length of the variable name.
 */
static int
is_valid_kbuild_object_variable_name(const char *pchName, size_t cchName)
{
    if (cchName > 0)
    {
        if (!memchr(pchName, '[', cchName))
        {
            /** @todo more? */
            return 1;
        }
    }
    return 0;
}

static const char *
kbuild_replace_special_accessors(const char *pchValue, size_t *pcchValue, int *pfDuplicateValue,
                                 const struct floc *pFileLoc)
{
    size_t      cchValue    = *pcchValue;
    size_t      cbAllocated = *pfDuplicateValue ? 0 : cchValue + 1;

    /*
     * Loop thru each potential special accessor occurance in the string.
     *
     * Unfortunately, we don't have a strnstr function in the C library, so
     * we'll using memchr and doing a few more rounds in this loop.
     */
    size_t  cchLeft  = cchValue;
    char   *pchLeft  = (char *)pchValue;
    for (;;)
    {
        int     fSuper;
        char   *pch = (char *)memchr(pchLeft, '$', cchLeft);
        if (!pch)
            break;

        pch++;
        cchLeft -= pch - pchLeft;
        pchLeft  = pch;

        /* [@self] is the shorter, quit if there isn't enough room for even it. */
        if (cchLeft < sizeof("([@self]") - 1)
            break;

        /* We don't care how many dollars there are in front of a special accessor. */
        if (*pchLeft == '$')
        {
            do
            {
                cchLeft--;
                pchLeft++;
            } while (cchLeft >= sizeof("([@self]") - 1 && *pchLeft == '$');
            if (cchLeft < sizeof("([@self]") - 1)
                break;
        }

        /* Is it a special accessor? */
        if (   pchLeft[2] != '@'
            || pchLeft[1] != '['
            || pchLeft[0] != '(')
            continue;
        pchLeft += 2;
        cchLeft -= 2;
        if (!memcmp(pchLeft, STRING_SIZE_TUPLE("@self]")))
            fSuper = 0;
        else if (   cchLeft >= sizeof("@super]")
                 && !memcmp(pchLeft, STRING_SIZE_TUPLE("@super]")))
            fSuper = 1;
        else
            continue;

        /*
         * We've got something to replace. First figure what with and then
         * resize the value buffer.
         */
        if (g_pTopKbEvalData)
        {
            struct kbuild_object   *pObj       = g_pTopKbEvalData->pObj;
            size_t const            cchSpecial = fSuper ? sizeof("@super") - 1 : sizeof("@self") - 1;
            size_t                  cchName;
            size_t                  cchType;
            long                    cchDelta;
            const char             *pszName;

            if (fSuper)
            {
                pObj = get_kbuild_object_parent(pObj, kBuildSeverity_Error);
                if (!pObj)
                    continue;
            }
            pszName = pObj->pszName;
            cchName = pObj->cchName;
            cchType = eval_kbuild_type_to_string_length(pObj->enmType);
            cchDelta = cchType + 1 + cchName - cchSpecial;

            if (cchValue + cchDelta >= cbAllocated)
            {
                size_t  offLeft = pchLeft - pchValue;
                char   *pszNewValue;

                cbAllocated = cchValue + cchDelta + 1;
                if (cchValue < 1024)
                    cbAllocated = (cbAllocated + 31) & ~(size_t)31;
                else
                    cbAllocated = (cbAllocated + 255) & ~(size_t)255;
                pszNewValue = (char *)xmalloc(cbAllocated);

                memcpy(pszNewValue, pchValue, offLeft);
                memcpy(pszNewValue + offLeft + cchSpecial + cchDelta,
                       pchLeft + cchSpecial,
                       cchLeft - cchSpecial + 1);

                if (*pfDuplicateValue == 0)
                    free((char *)pchValue);
                else
                    *pfDuplicateValue = 0;

                pchValue = pszNewValue;
                pchLeft  = pszNewValue + offLeft;
            }
            else
            {
                assert(*pfDuplicateValue == 0);
                memmove(pchLeft + cchSpecial + cchDelta,
                        pchLeft + cchSpecial,
                        cchLeft - cchSpecial + 1);
            }

            cchLeft  += cchDelta;
            cchValue += cchDelta;
            *pcchValue = cchValue;

            memcpy(pchLeft, eval_kbuild_type_to_string(pObj->enmType), cchType);
            pchLeft += cchType;
            *pchLeft++ = '@';
            memcpy(pchLeft, pszName, cchName);
            pchLeft += cchName;
            cchLeft -= cchType + 1 + cchName;
        }
        else
            error(pFileLoc, _("The '$([%.*s...' accessor can only be used in the context of a kBuild object"),
                  MAX(cchLeft, 20), pchLeft);
    }

    return pchValue;
}

static struct variable *
define_kbuild_object_variable_cached(struct kbuild_object *pObj, const char *pszName,
                                     const char *pchValue, size_t cchValue,
                                     int fDuplicateValue, enum variable_origin enmOrigin,
                                     int fRecursive, int fNoSpecialAccessors, const struct floc *pFileLoc)
{
    struct variable *pVar;
    size_t cchName = strcache2_get_len(&variable_strcache, pszName);

    if (fRecursive && !fNoSpecialAccessors)
        pchValue = kbuild_replace_special_accessors(pchValue, &cchValue, &fDuplicateValue, pFileLoc);

    pVar = define_variable_in_set(pszName, cchName,
                                  pchValue, cchValue, fDuplicateValue,
                                  enmOrigin, fRecursive,
                                  pObj->pVariables->set,
                                  pFileLoc);

    /* Single underscore prefixed variables gets a global alias. */
    if (   pszName[0] == '_'
        && pszName[1] != '_'
        && g_fKbObjCompMode)
    {
        struct variable *pAlias;
        size_t           cchPrefixed = pObj->cchVarPrefix + cchName;
        char            *pszPrefixed = xmalloc(cchPrefixed + 1);
        memcpy(pszPrefixed, pObj->pszVarPrefix, pObj->cchVarPrefix);
        memcpy(&pszPrefixed[pObj->cchVarPrefix], pszName, cchName);
        pszPrefixed[cchPrefixed] = '\0';

        pAlias = define_variable_alias_in_set(pszPrefixed, cchPrefixed, pVar, enmOrigin,
                                              &global_variable_set, pFileLoc);
        if (!pAlias->alias)
            error(pFileLoc, _("Error defining alias '%s'"), pszPrefixed);
    }

    return pVar;
}

#if 0
struct variable *
define_kbuild_object_variable(struct kbuild_object *pObj, const char *pchName, size_t cchName,
                              const char *pchValue, size_t cchValue,
                              int fDuplicateValue, enum variable_origin enmOrigin,
                              int fRecursive, const struct floc *pFileLoc)
{
    return define_kbuild_object_variable_cached(pObj, strcache2_add(&variable_strcache, pchName, cchName),
                                                pchValue, cchValue,
                                                fDuplicateValue, enmOrigin,
                                                fRecursive, pFileLoc);
}
#endif

/**
 * Try define a kBuild object variable via a possible accessor
 * ([type@object]var).
 *
 * @returns Pointer to the defined variable on success.
 * @retval  VAR_NOT_KBUILD_ACCESSOR if it isn't an accessor.
 *
 * @param   pchName         The variable name, not cached.
 * @param   cchName         The variable name length.  This will not be ~0U.
 * @param   pszValue        The variable value.  If @a fDuplicateValue is clear,
 *                          this should be assigned as the actual variable
 *                          value, otherwise it will be duplicated.  In the
 *                          latter case it might not be properly null
 *                          terminated.
 * @param   cchValue        The value length.
 * @param   fDuplicateValue Whether @a pszValue need to be duplicated on the
 *                          heap or is already there.
 * @param   enmOrigin       The variable origin.
 * @param   fRecursive      Whether it's a recursive variable.
 * @param   pFileLoc        The location of the variable definition.
 */
struct variable *
try_define_kbuild_object_variable_via_accessor(const char *pchName, size_t cchName,
                                               const char *pszValue, size_t cchValue, int fDuplicateValue,
                                               enum variable_origin enmOrigin, int fRecursive,
                                               struct floc const *pFileLoc)
{
    struct kbuild_object   *pObj;
    const char             *pchVarNm;
    size_t                  cchVarNm;

    pObj = parse_kbuild_object_variable_accessor(pchName, cchName, kBuildSeverity_Fatal, pFileLoc,
                                                 &pchVarNm, &cchVarNm, NULL);
    if (pObj != KOBJ_NOT_KBUILD_ACCESSOR)
    {
        assert(pObj != NULL);
        if (!is_valid_kbuild_object_variable_name(pchVarNm, cchVarNm))
            fatal(pFileLoc, _("Invalid kBuild object variable name: '%.*s' ('%s')"),
                  (int)cchVarNm, pchVarNm, (int)cchName, pchName);
        return define_kbuild_object_variable_cached(pObj, strcache2_add(&variable_strcache, pchVarNm, cchVarNm),
                                                    pszValue, cchValue, fDuplicateValue, enmOrigin, fRecursive,
                                                    0 /*fNoSpecialAccessors*/, pFileLoc);
    }

    return VAR_NOT_KBUILD_ACCESSOR;
}

/**
 * Define a kBuild object variable in the topmost kBuild object.
 *
 * This won't be an variable accessor.
 *
 * @returns Pointer to the defined variable on success.
 *
 * @param   pchName         The variable name, not cached.
 * @param   cchName         The variable name length.  This will not be ~0U.
 * @param   pszValue        The variable value.  If @a fDuplicateValue is clear,
 *                          this should be assigned as the actual variable
 *                          value, otherwise it will be duplicated.  In the
 *                          latter case it might not be properly null
 *                          terminated.
 * @param   cchValue        The value length.
 * @param   fDuplicateValue Whether @a pszValue need to be duplicated on the
 *                          heap or is already there.
 * @param   enmOrigin       The variable origin.
 * @param   fRecursive      Whether it's a recursive variable.
 * @param   pFileLoc        The location of the variable definition.
 */
struct variable *
define_kbuild_object_variable_in_top_obj(const char *pchName, size_t cchName,
                                         const char *pszValue, size_t cchValue, int fDuplicateValue,
                                         enum variable_origin enmOrigin, int fRecursive,
                                         struct floc const *pFileLoc)
{
    assert(g_pTopKbEvalData != NULL);

    if (!is_valid_kbuild_object_variable_name(pchName, cchName))
        fatal(pFileLoc, _("Invalid kBuild object variable name: '%.*s'"), (int)cchName, pchName);

    return define_kbuild_object_variable_cached(g_pTopKbEvalData->pObj, strcache2_add(&variable_strcache, pchName, cchName),
                                                pszValue, cchValue, fDuplicateValue, enmOrigin, fRecursive,
                                                0 /*fNoSpecialAccessors*/, pFileLoc);
}

/**
 * Implements appending and prepending to a kBuild object variable.
 *
 * The variable is either accessed thru an accessor or by the topmost kBuild
 * object.
 *
 * @returns Pointer to the defined variable on success.
 *
 * @param   pchName         The variable name, not cached.
 * @param   cchName         The variable name length.  This will not be ~0U.
 * @param   pszValue        The variable value. Must be duplicated.
 * @param   cchValue        The value length.
 * @param   fSimpleValue    Whether we've already figured that it's a simple
 *                          value.  This is for optimizing appending/prepending
 *                          to an existing simple value variable.
 * @param   enmOrigin       The variable origin.
 * @param   fAppend         Append if set, prepend if clear.
 * @param   pFileLoc        The location of the variable definition.
 */
struct variable *
kbuild_object_variable_pre_append(const char *pchName, size_t cchName,
                                  const char *pchValue, size_t cchValue, int fSimpleValue,
                                  enum variable_origin enmOrigin, int fAppend,
                                  const struct floc *pFileLoc)
{
    struct kbuild_object   *pObj;
    struct variable         VarKey;

    /*
     * Resolve the relevant kBuild object first.
     */
    if (cchName > 3 && pchName[0] == '[')
    {
        const char *pchVarNm;
        size_t      cchVarNm;
        pObj = parse_kbuild_object_variable_accessor(pchName, cchName, kBuildSeverity_Fatal, pFileLoc,
                                                     &pchVarNm, &cchVarNm, NULL);
        if (pObj != KOBJ_NOT_KBUILD_ACCESSOR)
        {
            pchName = pchVarNm;
            cchName = cchVarNm;
        }
        else
            pObj = g_pTopKbEvalData->pObj;
    }
    else
        pObj = g_pTopKbEvalData->pObj;

    /*
     * Make sure the variable name is valid.  Raise fatal error if not.
     */
    if (!is_valid_kbuild_object_variable_name(pchName, cchName))
        fatal(pFileLoc, _("Invalid kBuild object variable name: '%.*s'"), (int)cchName, pchName);

    /*
     * Get the cached name and look it up in the object's variables.
     */
    VarKey.name = strcache2_lookup(&variable_strcache, pchName, cchName);
    if (VarKey.name)
    {
        struct variable *pVar;

        VarKey.length = cchName;
        pVar = (struct variable *)hash_find_item_strcached(&pObj->pVariables->set->table, &VarKey);
        if (pVar)
        {
            /* Append/prepend to existing variable. */
            int fDuplicateValue = 1;
            if (pVar->recursive && !fSimpleValue)
                pchValue = kbuild_replace_special_accessors(pchValue, &cchValue, &fDuplicateValue, pFileLoc);

            pVar = do_variable_definition_append(pFileLoc, pVar, pchValue, cchValue, fSimpleValue, enmOrigin, fAppend);

            if (fDuplicateValue == 0)
                free((char *)pchValue);
            return pVar;
        }

        /*
         * Not found. Check ancestors if the 'override' directive isn't applied.
         */
        if (pObj->pszParent && enmOrigin != o_override)
        {
            struct kbuild_object *pParent = pObj;
            for (;;)
            {
                pParent = resolve_kbuild_object_parent(pParent, 0 /*fQuiet*/);
                if (!pParent)
                    break;

                pVar = (struct variable *)hash_find_item_strcached(&pParent->pVariables->set->table, &VarKey);
                if (pVar)
                {
                    if (pVar->value_length != ~0U)
                        assert(pVar->value_length == strlen(pVar->value));
                    else
                        pVar->value_length = strlen(pVar->value);

                    /*
                     * Combine the two values and define the variable in the
                     * specified child object.  We must disregard 'origin' a
                     * little here, so we must do the gritty stuff our selves.
                     */
                    if (   pVar->recursive
                        || fSimpleValue
                        || !cchValue
                        || memchr(pchValue, '$', cchValue) == NULL )
                    {
                        int     fDuplicateValue = 1;
                        size_t  cchNewValue;
                        char   *pszNewValue;
                        char   *pszTmp;

                        /* Just join up the two values. */
                        if (pVar->recursive && !fSimpleValue)
                            pchValue = kbuild_replace_special_accessors(pchValue, &cchValue, &fDuplicateValue, pFileLoc);
                        if (pVar->value_length == 0)
                        {
                            cchNewValue = cchValue;
                            pszNewValue = xstrndup(pchValue, cchValue);
                        }
                        else if (!cchValue)
                        {
                            cchNewValue = pVar->value_length;
                            pszNewValue = xmalloc(cchNewValue + 1);
                            memcpy(pszNewValue, pVar->value, cchNewValue + 1);
                        }
                        else
                        {
                            cchNewValue = pVar->value_length + 1 + cchValue;
                            pszNewValue = xmalloc(cchNewValue + 1);
                            if (fAppend)
                            {
                                memcpy(pszNewValue, pVar->value, pVar->value_length);
                                pszTmp = pszNewValue + pVar->value_length;
                                *pszTmp++ = ' ';
                                memcpy(pszTmp, pchValue, cchValue);
                                pszTmp[cchValue] = '\0';
                            }
                            else
                            {
                                memcpy(pszNewValue, pchValue, cchValue);
                                pszTmp = pszNewValue + cchValue;
                                *pszTmp++ = ' ';
                                memcpy(pszNewValue, pVar->value, pVar->value_length);
                                pszTmp[pVar->value_length] = '\0';
                            }
                        }

                        /* Define the new variable in the child. */
                        pVar = define_kbuild_object_variable_cached(pObj, VarKey.name,
                                                                    pszNewValue, cchNewValue, 0 /*fDuplicateValue*/,
                                                                    enmOrigin, pVar->recursive, 1 /*fNoSpecialAccessors*/,
                                                                    pFileLoc);
                        if (fDuplicateValue == 0)
                            free((char *)pchValue);
                    }
                    else
                    {
                        /* Lazy bird: Copy the variable from the ancestor and
                                      then do a normal append/prepend on it. */
                        pVar = define_kbuild_object_variable_cached(pObj, VarKey.name,
                                                                    pVar->value, pVar->value_length, 1 /*fDuplicateValue*/,
                                                                    enmOrigin, pVar->recursive, 1 /*fNoSpecialAccessors*/,
                                                                    pFileLoc);
                        append_expanded_string_to_variable(pVar, pchValue, cchValue, fAppend);
                    }
                    return pVar;
                }
            }
        }
    }
    else
        VarKey.name = strcache2_add(&variable_strcache, pchName, cchName);

    /* Variable not found. */
    return define_kbuild_object_variable_cached(pObj, VarKey.name,
                                                pchValue, cchValue, 1 /*fDuplicateValue*/, enmOrigin,
                                                1 /*fRecursive */, 0 /*fNoSpecialAccessors*/, pFileLoc);
}

/** @} */


static char *
allocate_expanded_next_token(const char **ppszCursor, const char *pszEos, size_t *pcchToken, int fStrip)
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
            return pszToken;
        }
    }

    if (pcchToken)
        *pcchToken = 0;
    return NULL;
}

static struct kbuild_object *
resolve_kbuild_object_parent(struct kbuild_object *pObj, int fQuiet)
{
    if (   !pObj->pParent
        && pObj->pszParent)
    {
        struct kbuild_object *pCur = g_pHeadKbObjs;
        while (pCur)
        {
            if (   pCur->enmType == pObj->enmType
                && !strcmp(pCur->pszName, pObj->pszParent))
            {
                if (    pCur->pszParent
                    &&  (   pCur->pParent == pObj
                         || !strcmp(pCur->pszParent, pObj->pszName)) )
                    fatal(&pObj->FileLoc, _("'%s' and '%s' are both trying to be each other children..."),
                          pObj->pszName, pCur->pszName);

                pObj->pParent = pCur;
                pObj->pVariables->next = pObj->pVariables;
                return pCur;
            }

            pCur = pCur->pGlobalNext;
        }

        /* Not found. */
        if (!fQuiet)
            error(&pObj->FileLoc, _("Could not locate parent '%s' of '%s'"), pObj->pszParent, pObj->pszName);
    }
    return pObj->pParent;
}

/**
 * Get the parent of the given object, it is expected to have one.
 *
 * @returns Pointer to the parent. NULL if we survive failure.
 * @param   pObj                The kBuild object.
 * @param   enmSeverity         The severity of a missing parent.
 */
static struct kbuild_object *
get_kbuild_object_parent(struct kbuild_object *pObj, enum kBuildSeverity enmSeverity)
{
    struct kbuild_object *pParent = pObj->pParent;
    if (pParent)
        return pParent;

    pParent = resolve_kbuild_object_parent(pObj, 1 /*fQuiet - complain below */);
    if (pParent)
        return pParent;

    if (pObj->pszParent)
        kbuild_report_problem(enmSeverity, &pObj->FileLoc,
                              _("Could not local parent '%s' for kBuild object '%s'"),
                              pObj->pszParent, pObj->pszName);
    else
        kbuild_report_problem(enmSeverity, &pObj->FileLoc,
                              _("kBuild object '%s' has no parent ([@super])"),
                              pObj->pszName);
    return NULL;
}

static int
eval_kbuild_define_xxxx(struct kbuild_eval_data **ppData, const struct floc *pFileLoc,
                        const char *pszLine, const char *pszEos, int fIgnoring, enum kBuildType enmType)
{
    unsigned int            cch;
    char                    ch;
    char                   *psz;
    const char             *pszPrefix;
    struct kbuild_object   *pObj;
    struct kbuild_eval_data *pData;

    if (fIgnoring)
        return 0;

    /*
     * Create a new kBuild object.
     */
    pObj = xmalloc(sizeof(*pObj));
    pObj->enmType           = enmType;
    pObj->pszName           = NULL;
    pObj->cchName           = 0;
    pObj->FileLoc           = *pFileLoc;

    pObj->pGlobalNext       = g_pHeadKbObjs;
    g_pHeadKbObjs           = pObj;

    pObj->pVariables        = create_new_variable_set();

    pObj->pszParent         = NULL;
    pObj->cchParent         = 0;
    pObj->pParent           = NULL;

    pObj->pszTemplate       = NULL;

    pObj->pszVarPrefix      = NULL;
    pObj->cchVarPrefix      = 0;

    /*
     * The first word is the name.
     */
    pObj->pszName = allocate_expanded_next_token(&pszLine, pszEos, &pObj->cchName, 1 /*strip*/);
    if (!pObj->pszName || !*pObj->pszName)
        fatal(pFileLoc, _("The kBuild define requires a name"));

    psz = pObj->pszName;
    while ((ch = *psz++) != '\0')
        if (!isgraph(ch))
        {
            error(pFileLoc, _("The 'kBuild-define-%s' name '%s' contains one or more invalid characters"),
                  eval_kbuild_type_to_string(enmType), pObj->pszName);
            break;
        }

    /*
     * Calc the variable prefix.
     */
    switch (enmType)
    {
        case kBuildType_Target:      pszPrefix = ""; break;
        case kBuildType_Template:    pszPrefix = "TEMPLATE_"; break;
        case kBuildType_Tool:        pszPrefix = "TOOL_"; break;
        case kBuildType_Sdk:         pszPrefix = "SDK_"; break;
        case kBuildType_Unit:        pszPrefix = "UNIT_"; break;
        default:
            fatal(pFileLoc, _("enmType=%d"), enmType);
            return -1;
    }
    cch = strlen(pszPrefix);
    pObj->cchVarPrefix = cch + pObj->cchName;
    pObj->pszVarPrefix = xmalloc(pObj->cchVarPrefix + 1);
    memcpy(pObj->pszVarPrefix, pszPrefix, cch);
    memcpy(&pObj->pszVarPrefix[cch], pObj->pszName, pObj->cchName);

    /*
     * Parse subsequent words.
     */
    psz = find_next_token_eos(&pszLine, pszEos, &cch);
    while (psz)
    {
        if (WORD_IS(psz, cch, "extending"))
        {
            /* Inheritance directive. */
            if (pObj->pszParent != NULL)
                fatal(pFileLoc, _("'extending' can only occure once"));
            pObj->pszParent = allocate_expanded_next_token(&pszLine, pszEos, &pObj->cchParent, 1 /*strip*/);
            if (!pObj->pszParent || !*pObj->pszParent)
                fatal(pFileLoc, _("'extending' requires a parent name"));
        }
        else if (WORD_IS(psz, cch, "using"))
        {
            char   *pszTemplate;
            size_t  cchTemplate;

            /* Template directive. */
            if (enmType != kBuildType_Target)
                fatal(pFileLoc, _("'using <template>' can only be used with 'kBuild-define-target'"));
            if (pObj->pszTemplate != NULL)
                fatal(pFileLoc, _("'using' can only occure once"));

            pszTemplate = allocate_expanded_next_token(&pszLine, pszEos, &cchTemplate, 1 /*fStrip*/);
            if (!pszTemplate || !*pszTemplate)
                fatal(pFileLoc, _("'using' requires a template name"));

            define_kbuild_object_variable_cached(pObj, g_pszVarNmTemplate, pszTemplate, cchTemplate,
                                                 0 /*fDuplicateValue*/, o_default, 0 /*fRecursive*/,
                                                 1 /*fNoSpecialAccessors*/, pFileLoc);

        }
        else
            fatal(pFileLoc, _("Don't know what '%.*s' means"), (int)cch, psz);

        /* next token */
        psz = find_next_token_eos(&pszLine, pszEos, &cch);
    }

    /*
     * Try resolve the parent.
     */
    resolve_kbuild_object_parent(pObj, 1 /*fQuiet*/);

    /*
     * Create an eval stack entry and change the current variable set.
     */
    pData = xmalloc(sizeof(*pData));
    pData->pObj             = pObj;
    pData->pVariablesSaved  = current_variable_set_list;
    current_variable_set_list = pObj->pVariables;

    pData->pStackDown       = *ppData;
    *ppData                 = pData;
    g_pTopKbEvalData        = pData;

    return 0;
}

static int
eval_kbuild_endef_xxxx(struct kbuild_eval_data **ppData, const struct floc *pFileLoc,
                       const char *pszLine, const char *pszEos, int fIgnoring, enum kBuildType enmType)
{
    struct kbuild_eval_data *pData;
    struct kbuild_object    *pObj;
    size_t                   cchName;
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
              eval_kbuild_type_to_string(enmType), eval_kbuild_type_to_string(enmType));
        return 0;
    }

    /*
     * ... and does it have a matching kind?
     */
    pObj = pData->pObj;
    if (pObj->enmType != enmType)
        error(pFileLoc, _("'kBuild-endef-%s' does not match 'kBuild-define-%s %s'"),
              eval_kbuild_type_to_string(enmType), eval_kbuild_type_to_string(pObj->enmType), pObj->pszName);

    /*
     * The endef-kbuild may optionally be followed by the target name.
     * It should match the name given to the kBuild-define.
     */
    pszName = allocate_expanded_next_token(&pszLine, pszEos, &cchName, 1 /*fStrip*/);
    if (pszName)
    {
        if (   cchName != pObj->cchName
            || strcmp(pszName, pObj->pszName))
            error(pFileLoc, _("'kBuild-endef-%s %s' does not match 'kBuild-define-%s %s'"),
                  eval_kbuild_type_to_string(enmType), pszName,
                  eval_kbuild_type_to_string(pObj->enmType), pObj->pszName);
        free(pszName);
    }

    /*
     * Pop a define off the stack.
     */
    assert(pData == g_pTopKbEvalData);
    *ppData = g_pTopKbEvalData = pData->pStackDown;
    pData->pStackDown      = NULL;
    current_variable_set_list = pData->pVariablesSaved;
    pData->pVariablesSaved = NULL;
    free(pData);

    return 0;
}

int eval_kbuild_read_hook(struct kbuild_eval_data **kdata, const struct floc *flocp,
                          const char *pchWord, size_t cchWord, const char *line, const char *eos, int ignoring)
{
    enum kBuildType enmType;

    /*
     * Skip the 'kBuild-' prefix that the caller already matched.
     */
    assert(memcmp(pchWord, "kBuild-", sizeof("kBuild-") - 1) == 0);
    pchWord += sizeof("kBuild-") - 1;
    cchWord -= sizeof("kBuild-") - 1;

    /*
     * String switch.
     */
    if (   cchWord >= sizeof("define-") - 1
        && strneq(pchWord, "define-", sizeof("define-") - 1))
    {
        enmType = eval_kbuild_type_from_string(pchWord + sizeof("define-") - 1, cchWord - sizeof("define-") + 1);
        if (enmType != kBuildType_Invalid)
            return eval_kbuild_define_xxxx(kdata, flocp, line, eos, ignoring, enmType);
    }
    else if (   cchWord >= sizeof("endef-") - 1
             && strneq(pchWord, "endef-", sizeof("endef-") - 1))
    {
        enmType = eval_kbuild_type_from_string(pchWord + sizeof("endif-") - 1, cchWord - sizeof("endif-") + 1);
        if (enmType != kBuildType_Invalid)
            return eval_kbuild_endef_xxxx(kdata, flocp, line, eos, ignoring, enmType);
    }
    else if (WORD_IS(pchWord, cchWord, "endef"))
    {
        /* Terminate whatever definition is on top. */

    }

    /*
     * Everything that is prefixed with 'kBuild-' is reserved for language
     * extensions, at least until legacy assignments/whatever turns up.
     */
    error(flocp, _("Unknown syntax 'kBuild-%.*s'"), (int)cchWord, pchWord);
    return 0;
}


/** @name kBuild object variable accessor related functions
 * @{
 */

/**
 * Checks if the given name is an object variable accessor.
 *
 * @returns 1 if it is, 0 if it isn't.
 * @param   pchName             The potential kBuild variable accessor
 *                              expression.
 * @param   cchName             Length of the expression.
 */
int is_kbuild_object_variable_accessor(const char *pchName, size_t cchName)
{
    char const *pchTmp;

    /* See lookup_kbuild_object_variable for the rules. */
    if (cchName >= 1+1+1+1 && *pchName == '[')
    {
        pchName++;
        cchName--;

        pchTmp = memchr(pchName, '@', cchName);
        if (pchTmp)
        {
            cchName -= pchTmp + 1 - pchName;
            pchName  = pchTmp + 1;
            pchTmp = memchr(pchName, ']', cchName);
            if (pchTmp)
            {
                cchName -= pchTmp + 1 - pchName;
                if (cchName > 0)
                    return 1;
            }
        }
    }
    return 0;
}

/**
 * Parses a kBuild object variable accessor, resolving the object.
 *
 * @returns Pointer to the variable if found.
 * @retval  NULL if the object (or type) couldn't be resolved.
 * @retval  KOBJ_NOT_KBUILD_ACCESSOR if no a kBuild variable accessor.
 *
 * @param   pchExpr             The kBuild variable accessor expression.
 * @param   cchExpr             Length of the expression.
 * @param   enmSeverity         The minimum severity level for errors.
 * @param   pFileLoc            The file location any errors should be reported
 *                              at. Optional.
 * @param   ppchVarNm           Where to return the pointer to the start of the
 *                              variable name within the string @a pchExpr
 *                              points to. Mandatory.
 * @param   pcchVarNm           Where to return the length of the variable name.
 *                              Mandatory.
 * @param   penmType            Where to return the object type. Optional.
 */
static struct kbuild_object *
parse_kbuild_object_variable_accessor(const char *pchExpr, size_t cchExpr,
                                      enum kBuildSeverity enmSeverity, const struct floc *pFileLoc,
                                      const char **ppchVarNm, size_t *pcchVarNm, enum kBuildType *penmType)
{
    const char * const pchOrgExpr = pchExpr;
    size_t       const cchOrgExpr = cchExpr;
    char const        *pchTmp;

    /*
     * To accept this as an kBuild accessor, we require:
     *   1. Open bracket.
     *   2. At sign separating the type from the name.
     *   3. Closing bracket.
     *   4. At least one character following it.
     */
    if (cchExpr >= 1+1+1+1 && *pchExpr == '[')
    {
        pchExpr++;
        cchExpr--;

        pchTmp = memchr(pchExpr, '@', cchExpr);
        if (pchTmp)
        {
            const char  * const pchType = pchExpr;
            size_t        const cchType = pchTmp - pchExpr;

            cchExpr -= cchType + 1;
            pchExpr  = pchTmp + 1;
            pchTmp = memchr(pchExpr, ']', cchExpr);
            if (pchTmp)
            {
                const char * const pchObjName = pchExpr;
                size_t       const cchObjName = pchTmp - pchExpr;

                cchExpr -= cchObjName + 1;
                pchExpr  = pchTmp + 1;
                if (cchExpr > 0)
                {

                    /*
                     * It's an kBuild define variable accessor, alright.
                     */
                    *pcchVarNm = cchExpr;
                    *ppchVarNm = pchExpr;

                    /* Deal with known special accessors: [@self]VAR, [@super]VAR. */
                    if (cchType == 0)
                    {
                        int fSuper;

                        if (WORD_IS(pchObjName, cchObjName, "self"))
                            fSuper = 0;
                        else if (WORD_IS(pchObjName, cchObjName, "super"))
                            fSuper = 1;
                        else
                        {
                            kbuild_report_problem(MAX(enmSeverity, kBuildSeverity_Error), pFileLoc,
                                                  _("Invalid special kBuild object accessor: '%.*s'"),
                                                  (int)cchOrgExpr, pchOrgExpr);
                            if (penmType)
                                *penmType = kBuildType_Invalid;
                            return NULL;
                        }
                        if (g_pTopKbEvalData)
                        {
                            struct kbuild_object *pObj = g_pTopKbEvalData->pObj;
                            struct kbuild_object *pParent;

                            if (penmType)
                                *penmType = pObj->enmType;

                            if (!fSuper)
                                return pObj;

                            pParent = get_kbuild_object_parent(pObj, MAX(enmSeverity, kBuildSeverity_Error));
                            if (pParent)
                                return pParent;
                        }
                        else
                            kbuild_report_problem(MAX(enmSeverity, kBuildSeverity_Error), pFileLoc,
                                                  _("The '%.*s' accessor can only be used in the context of a kBuild object"),
                                                  (int)cchOrgExpr, pchOrgExpr);
                        if (penmType)
                            *penmType = kBuildType_Invalid;
                    }
                    else
                    {
                        /* Genric accessor. Check the type and look up the object. */
                        enum kBuildType enmType = eval_kbuild_type_from_string(pchType, cchType);
                        if (penmType)
                            *penmType = enmType;
                        if (enmType != kBuildType_Invalid)
                        {
                            struct kbuild_object *pObj = lookup_kbuild_object(enmType, pchObjName, cchObjName);
                            if (pObj)
                                return pObj;

                            /* failed. */
                            kbuild_report_problem(enmSeverity, pFileLoc,
                                                  _("kBuild object '%s' not found in kBuild variable accessor '%.*s'"),
                                                  (int)cchObjName, pchObjName, (int)cchOrgExpr, pchOrgExpr);
                        }
                        else
                            kbuild_report_problem(MAX(enmSeverity, kBuildSeverity_Error), pFileLoc,
                                                  _("Invalid type '%.*s' specified in kBuild variable accessor '%.*s'"),
                                                  (int)cchType, pchType, (int)cchOrgExpr, pchOrgExpr);
                    }
                    return NULL;
                }
            }
        }
    }

    *ppchVarNm = NULL;
    *pcchVarNm = 0;
    if (penmType)
        *penmType = kBuildType_Invalid;
    return KOBJ_NOT_KBUILD_ACCESSOR;
}

/**
 * Looks up a variable in a kBuild object.
 *
 * The caller has done minimal matching, i.e. starting square brackets and
 * minimum length.  We do the rest here.
 *
 * @returns Pointer to the variable if found.
 * @retval  NULL if not found.
 * @retval  VAR_NOT_KBUILD_ACCESSOR if no a kBuild variable accessor.
 *
 * @param   pchName             The kBuild variable accessor expression.
 * @param   cchName             Length of the expression.
 */
struct variable *
lookup_kbuild_object_variable_accessor(const char *pchName, size_t cchName)
{
    const char * const     pchOrgName = pchName;
    size_t       const     cchOrgName = cchName;
    const char *           pchVarNm;
    size_t                 cchVarNm;
    struct kbuild_object  *pObj;

    pObj = parse_kbuild_object_variable_accessor(pchName, cchName, kBuildSeverity_Warning, NULL, &pchVarNm, &cchVarNm, NULL);
    if (pObj != KOBJ_NOT_KBUILD_ACCESSOR)
    {
        if (pObj)
        {
            /*
             * Do the variable lookup.
             */
            const char *pszCachedName = strcache2_lookup(&variable_strcache, pchVarNm, cchVarNm);
            if (pszCachedName)
            {
                struct variable  VarKey;
                struct variable *pVar;
                VarKey.name   = pszCachedName;
                VarKey.length = cchName;

                pVar = (struct variable *)hash_find_item_strcached(&pObj->pVariables->set->table, &VarKey);
                if (pVar)
                    return pVar;

                /*
                 * Not found, check ancestors if any.
                 */
                if (pObj->pszParent || pObj->pszTemplate)
                {
                    struct kbuild_object *pParent = pObj;
                    for (;;)
                    {
                        pParent = resolve_kbuild_object_parent(pParent, 0 /*fQuiet*/);
                        if (!pParent)
                            break;
                        pVar = (struct variable *)hash_find_item_strcached(&pParent->pVariables->set->table, &VarKey);
                        if (pVar)
                            return pVar;
                    }
                }
            }
        }

        /* Not found one way or the other. */
        return NULL;
    }

    /* Not a kBuild object variable accessor. */
    return VAR_NOT_KBUILD_ACCESSOR;
}

/** @} */

void print_kbuild_data_base(void)
{
    struct kbuild_object *pCur;

    puts(_("\n# kBuild defines"));

    for (pCur = g_pHeadKbObjs; pCur; pCur = pCur->pGlobalNext)
    {
        printf("\nkBuild-define-%s %s",
               eval_kbuild_type_to_string(pCur->enmType), pCur->pszName);
        if (pCur->pszParent)
            printf(" extending %s", pCur->pszParent);
        if (pCur->pszTemplate)
            printf(" using %s", pCur->pszTemplate);
        putchar('\n');

        print_variable_set(pCur->pVariables->set, "");

        printf("kBuild-endef-%s  %s\n",
               eval_kbuild_type_to_string(pCur->enmType), pCur->pszName);
    }
    /** @todo hash stats. */
}

void print_kbuild_define_stats(void)
{
    /* later when hashing stuff */
}

