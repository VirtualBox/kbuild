#ifdef CONFIG_WITH_IF_CONDITIONALS
/* $Id: expreval.c 2161 2008-12-29 22:20:11Z knut.osmundsen@oracle.com $ */
/** @file
 * expreval - Expressions evaluator, C / BSD make / nmake style.
 */

/*
 * Copyright (c) 2008 knut st. osmundsen <bird-src-spam@anduin.net>
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

/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
#include "make.h"
#include <assert.h>

#include <glob.h>

#include "dep.h"
#include "filedef.h"
#include "job.h"
#include "commands.h"
#include "variable.h"
#include "rule.h"
#include "debug.h"
#include "hash.h"
#include <ctype.h>
#ifndef _MSC_VER
# include <stdint.h>
#endif
#include <stdarg.h>


/*******************************************************************************
*   Defined Constants And Macros                                               *
*******************************************************************************/
/** The max length of a string representation of a number. */
#define EXPR_NUM_LEN  ((sizeof("-9223372036854775802") + 4) & ~3)

/** The max operator stack depth. */
#define EXPR_MAX_OPERATORS  72
/** The max operand depth. */
#define EXPR_MAX_OPERANDS   128


/*******************************************************************************
*   Structures and Typedefs                                                    *
*******************************************************************************/
/** The 64-bit signed integer type we're using. */
#ifdef _MSC_VER
typedef __int64 EXPRINT64;
#else
# include <stdint.h>
typedef int64_t EXPRINT64;
#endif

/** Pointer to a evaluator instance. */
typedef struct EXPR *PEXPR;


/**
 * Operand variable type.
 */
typedef enum
{
    /** Invalid zero entry. */
    kExprVar_Invalid = 0,
    /** A number. */
    kExprVar_Num,
    /** A string in need of expanding (perhaps). */
    kExprVar_String,
    /** A simple string that doesn't need expanding. */
    kExprVar_SimpleString,
    /** A quoted string in need of expanding (perhaps). */
    kExprVar_QuotedString,
    /** A simple quoted string that doesn't need expanding. */
    kExprVar_QuotedSimpleString,
    /** The end of the valid variable types. */
    kExprVar_End
} EXPRVARTYPE;

/**
 * Operand variable.
 */
typedef struct
{
    /** The variable type. */
    EXPRVARTYPE enmType;
    /** The variable. */
    union
    {
        /** Pointer to the string. */
        char *psz;
        /** The variable. */
        EXPRINT64 i;
    } uVal;
} EXPRVAR;
/** Pointer to a operand variable. */
typedef EXPRVAR *PEXPRVAR;
/** Pointer to a const operand variable. */
typedef EXPRVAR const *PCEXPRVAR;

/**
 * Operator return statuses.
 */
typedef enum
{
    kExprRet_Error = -1,
    kExprRet_Ok = 0,
    kExprRet_Operator,
    kExprRet_Operand,
    kExprRet_EndOfExpr,
    kExprRet_End
} EXPRRET;

/**
 * Operator.
 */
typedef struct
{
    /** The operator. */
    char szOp[11];
    /** The length of the operator string. */
    char cchOp;
    /** The pair operator.
     * This is used with '(' and '?'. */
    char chPair;
    /** The precedence. Higher means higher. */
    char iPrecedence;
    /** The number of arguments it takes. */
    signed char cArgs;
    /** Pointer to the method implementing the operator. */
    EXPRRET (*pfn)(PEXPR pThis);
} EXPROP;
/** Pointer to a const operator. */
typedef EXPROP const *PCEXPROP;

/**
 * Expression evaluator instance.
 */
typedef struct EXPR
{
    /** The full expression. */
    const char *pszExpr;
    /** The current location. */
    const char *psz;
    /** The current file location, used for errors. */
    const struct floc *pFileLoc;
    /** Pending binary operator. */
    PCEXPROP pPending;
    /** Top of the operator stack. */
    int iOp;
    /** Top of the operand stack. */
    int iVar;
    /** The operator stack. */
    PCEXPROP apOps[EXPR_MAX_OPERATORS];
    /** The operand stack. */
    EXPRVAR aVars[EXPR_MAX_OPERANDS];
} EXPR;


/*******************************************************************************
*   Global Variables                                                           *
*******************************************************************************/
/** Operator start character map.
 * This indicates which characters that are starting operators and which aren't. */
static char g_auchOpStartCharMap[256];
/** Whether we've initialized the map. */
static int g_fExprInitializedMap = 0;


/*******************************************************************************
*   Internal Functions                                                         *
*******************************************************************************/
static void expr_unget_op(PEXPR pThis);
static EXPRRET expr_get_binary_or_eoe_or_rparen(PEXPR pThis);




/**
 * Displays an error message.
 *
 * The total string length must not exceed 256 bytes.
 *
 * @param   pThis       The evaluator instance.
 * @param   pszError    The message format string.
 * @param   ...         The message format args.
 */
static void expr_error(PEXPR pThis, const char *pszError, ...)
{
    char szTmp[256];
    va_list va;

    va_start(va, pszError);
    vsprintf(szTmp, pszError, va);
    va_end(va);

    fatal(pThis->pFileLoc, "%s", szTmp);
}


/**
 * Converts a number to a string.
 *
 * @returns pszDst.
 * @param   pszDst  The string buffer to write into. Assumes length of EXPR_NUM_LEN.
 * @param   iSrc    The number to convert.
 */
static char *expr_num_to_string(char *pszDst, EXPRINT64 iSrc)
{
    static const char s_szDigits[17] = "0123456789abcdef";
    char szTmp[EXPR_NUM_LEN];
    char *psz = &szTmp[EXPR_NUM_LEN - 1];
    int fNegative;

    fNegative = iSrc < 0;
    if (fNegative)
    {
        /** @todo this isn't right for INT64_MIN. */
        iSrc = -iSrc;
    }

    *psz = '\0';
    do
    {
#if 0
        *--psz = s_szDigits[iSrc & 0xf];
        iSrc >>= 4;
#else
        *--psz = s_szDigits[iSrc % 10];
        iSrc /= 10;
#endif
    } while (iSrc);

#if 0
    *--psz = 'x';
    *--psz = '0';
#endif

    if (fNegative)
      *--psz = '-';

    /* copy it into the output buffer. */
    return (char *)memcpy(pszDst, psz, &szTmp[EXPR_NUM_LEN] - psz);
}


/**
 * Attempts to convert a (simple) string into a number.
 *
 * @returns status code.
 * @param   pThis   The evaluator instance. This is optional when fQuiet is true.
 * @param   piSrc   Where to store the numeric value on success.
 * @param   pszSrc  The string to try convert.
 * @param   fQuiet  Whether we should be quiet or grumpy on failure.
 */
static EXPRRET expr_string_to_num(PEXPR pThis, EXPRINT64 *piDst, const char *pszSrc, int fQuiet)
{
    EXPRRET rc = kExprRet_Ok;
    char const *psz = pszSrc;
    EXPRINT64 i;
    unsigned uBase;
    int fNegative;


    /*
     * Skip blanks.
     */
    while (isblank(*psz))
        psz++;

    /*
     * Check for '-'.
     *
     * At this point we will not need to deal with operators, this is
     * just an indicator of negative numbers. If some operator ends up
     * here it's because it came from a string expansion and thus shall
     * not be interpreted. If this turns out to be an stupid restriction
     * it can be fixed, but for now it stays like this.
     */
    fNegative = *psz == '-';
    if (fNegative)
        psz++;

    /*
     * Determin base                                                        .
     *                                                                      .
     * Recognize some exsotic prefixes here in addition to the two standard ones.
     */
    if (*psz != '0' || psz[1] == '\0' || isblank((unsigned int)psz[1]))
        uBase = 10;
    else if (psz[1] == 'x' || psz[1] == 'X')
    {
        uBase = 16;
        psz += 2;
    }
    else if (psz[1] == 'b' || psz[1] == 'B')
    {
        uBase = 2;
        psz += 2;
    }
    else if (psz[1] == 'd' || psz[1] == 'D')
    {
        uBase = 10;
        psz += 2;
    }
    else if (psz[1] == 'o' || psz[1] == 'O')
    {
        uBase = 8;
        psz += 2;
    }
    else if (isdigit(psz[1]) && psz[1] != '9' && psz[1] != '8')
    {
        uBase = 8;
        psz++;
    }
    else
        uBase = 10;

    /*
     * Convert until we hit a non-digit.
     */
    i = 0;
    for (;;)
    {
        unsigned iDigit;
        int ch = *psz;
        switch (ch)
        {
            case '0':   iDigit =  0; break;
            case '1':   iDigit =  1; break;
            case '2':   iDigit =  2; break;
            case '3':   iDigit =  3; break;
            case '4':   iDigit =  4; break;
            case '5':   iDigit =  5; break;
            case '6':   iDigit =  6; break;
            case '7':   iDigit =  7; break;
            case '8':   iDigit =  8; break;
            case '9':   iDigit =  9; break;
            case 'a':
            case 'A':   iDigit = 10; break;
            case 'b':
            case 'B':   iDigit = 11; break;
            case 'c':
            case 'C':   iDigit = 12; break;
            case 'd':
            case 'D':   iDigit = 13; break;
            case 'e':
            case 'E':   iDigit = 14; break;
            case 'f':
            case 'F':   iDigit = 15; break;

            default:
                /* is the rest white space? */
                while (isspace((unsigned int)*psz))
                    psz++;
                if (*psz != '\0')
                {
                    iDigit = uBase;
                    break;
                }
                /* fall thru */

            case '\0':
                if (fNegative)
                    i = -i;
                *piDst = i;
                return rc;
        }
        if (iDigit >= uBase)
        {
            if (fNegative)
                i = -i;
            *piDst = i;
            if (!fQuiet)
                expr_error(pThis, "Invalid a number \"%.80s\"", pszSrc);
            return kExprRet_Error;
        }

        /* add the digit and advance */
        i *= uBase;
        i += iDigit;
        psz++;
    }
    /* not reached */
}


/**
 * Checks if the variable is a string or not.
 *
 * @returns 1 if it's a string, 0 otherwise.
 * @param   pVar    The variable.
 */
static int expr_var_is_string(PCEXPRVAR pVar)
{
    return pVar->enmType >= kExprVar_String;
}


/**
 * Checks if the variable contains a string that was quoted
 * in the expression.
 *
 * @returns 1 if if was a quoted string, otherwise 0.
 * @param   pVar    The variable.
 */
static int expr_var_was_quoted(PCEXPRVAR pVar)
{
    return pVar->enmType >= kExprVar_QuotedString;
}


/**
 * Deletes a variable.
 *
 * @param   pVar    The variable.
 */
static void expr_var_delete(PEXPRVAR pVar)
{
    if (expr_var_is_string(pVar))
    {
        free(pVar->uVal.psz);
        pVar->uVal.psz = NULL;
    }
    pVar->enmType = kExprVar_Invalid;
}


/**
 * Initializes a new variables with a sub-string value.
 *
 * @param   pVar    The new variable.
 * @param   psz     The start of the string value.
 * @param   cch     The number of chars to copy.
 * @param   enmType The string type.
 */
static void expr_var_init_substring(PEXPRVAR pVar, const char *psz, size_t cch, EXPRVARTYPE enmType)
{
    /* convert string needing expanding into simple ones if possible.  */
    if (    enmType == kExprVar_String
        &&  !memchr(psz, '$', cch))
        enmType = kExprVar_SimpleString;
    else if (   enmType == kExprVar_QuotedString
             && !memchr(psz, '$', cch))
        enmType = kExprVar_QuotedSimpleString;

    pVar->enmType = enmType;
    pVar->uVal.psz = xmalloc(cch + 1);
    memcpy(pVar->uVal.psz, psz, cch);
    pVar->uVal.psz[cch] = '\0';
}


#if 0  /* unused */
/**
 * Initializes a new variables with a string value.
 *
 * @param   pVar    The new variable.
 * @param   psz     The string value.
 * @param   enmType The string type.
 */
static void expr_var_init_string(PEXPRVAR pVar, const char *psz, EXPRVARTYPE enmType)
{
    expr_var_init_substring(pVar, psz, strlen(psz), enmType);
}


/**
 * Assigns a sub-string value to a variable.
 *
 * @param   pVar    The new variable.
 * @param   psz     The start of the string value.
 * @param   cch     The number of chars to copy.
 * @param   enmType The string type.
 */
static void expr_var_assign_substring(PEXPRVAR pVar, const char *psz, size_t cch, EXPRVARTYPE enmType)
{
    expr_var_delete(pVar);
    expr_var_init_substring(pVar, psz, cch, enmType);
}


/**
 * Assignes a string value to a variable.
 *
 * @param   pVar    The variable.
 * @param   psz     The string value.
 * @param   enmType The string type.
 */
static void expr_var_assign_string(PEXPRVAR pVar, const char *psz, EXPRVARTYPE enmType)
{
    expr_var_delete(pVar);
    expr_var_init_string(pVar, psz, enmType);
}
#endif /* unused */


/**
 * Simplifies a string variable.
 *
 * @param   pVar    The variable.
 */
static void expr_var_make_simple_string(PEXPRVAR pVar)
{
    switch (pVar->enmType)
    {
        case kExprVar_Num:
        {
            char *psz = (char *)xmalloc(EXPR_NUM_LEN);
            expr_num_to_string(psz, pVar->uVal.i);
            pVar->uVal.psz = psz;
            pVar->enmType = kExprVar_SimpleString;
            break;
        }

        case kExprVar_String:
        case kExprVar_QuotedString:
        {
            char *psz;
            assert(strchr(pVar->uVal.psz, '$'));

            psz = allocated_variable_expand(pVar->uVal.psz);
            free(pVar->uVal.psz);
            pVar->uVal.psz = psz;

            pVar->enmType = pVar->enmType == kExprVar_String
                          ? kExprVar_SimpleString
                          : kExprVar_QuotedSimpleString;
            break;
        }

        case kExprVar_SimpleString:
        case kExprVar_QuotedSimpleString:
            /* nothing to do. */
            break;

        default:
            assert(0);
    }
}


#if 0 /* unused */
/**
 * Turns a variable into a string value.
 *
 * @param   pVar    The variable.
 */
static void expr_var_make_string(PEXPRVAR pVar)
{
    switch (pVar->enmType)
    {
        case kExprVar_Num:
            expr_var_make_simple_string(pVar);
            break;

        case kExprVar_String:
        case kExprVar_SimpleString:
        case kExprVar_QuotedString:
        case kExprVar_QuotedSimpleString:
            /* nothing to do. */
            break;

        default:
            assert(0);
    }
}
#endif /* unused */


/**
 * Initializes a new variables with a integer value.
 *
 * @param   pVar    The new variable.
 * @param   i       The integer value.
 */
static void expr_var_init_num(PEXPRVAR pVar, EXPRINT64 i)
{
    pVar->enmType = kExprVar_Num;
    pVar->uVal.i = i;
}


/**
 * Assigns a integer value to a variable.
 *
 * @param   pVar    The variable.
 * @param   i       The integer value.
 */
static void expr_var_assign_num(PEXPRVAR pVar, EXPRINT64 i)
{
    expr_var_delete(pVar);
    expr_var_init_num(pVar, i);
}


/**
 * Turns the variable into a number.
 *
 * @returns status code.
 * @param   pThis   The evaluator instance.
 * @param   pVar    The variable.
 */
static EXPRRET expr_var_make_num(PEXPR pThis, PEXPRVAR pVar)
{
    switch (pVar->enmType)
    {
        case kExprVar_Num:
            /* nothing to do. */
            break;

        case kExprVar_String:
            expr_var_make_simple_string(pVar);
            /* fall thru */
        case kExprVar_SimpleString:
        {
            EXPRINT64 i;
            EXPRRET rc = expr_string_to_num(pThis, &i, pVar->uVal.psz, 0 /* fQuiet */);
            if (rc < kExprRet_Ok)
                return rc;
            expr_var_assign_num(pVar, i);
            break;
        }

        case kExprVar_QuotedString:
        case kExprVar_QuotedSimpleString:
            expr_error(pThis, "Cannot convert a quoted string to a number");
            return kExprRet_Error;

        default:
            assert(0);
            return kExprRet_Error;
    }

    return kExprRet_Ok;
}


/**
 * Try to turn the variable into a number.
 *
 * @returns status code.
 * @param   pVar    The variable.
 */
static EXPRRET expr_var_try_make_num(PEXPRVAR pVar)
{
    switch (pVar->enmType)
    {
        case kExprVar_Num:
            /* nothing to do. */
            break;

        case kExprVar_String:
            expr_var_make_simple_string(pVar);
            /* fall thru */
        case kExprVar_SimpleString:
        {
            EXPRINT64 i;
            EXPRRET rc = expr_string_to_num(NULL, &i, pVar->uVal.psz, 1 /* fQuiet */);
            if (rc < kExprRet_Ok)
                return rc;
            expr_var_assign_num(pVar, i);
            break;
        }

        default:
            assert(0);
        case kExprVar_QuotedString:
        case kExprVar_QuotedSimpleString:
            /* can't do this */
            return kExprRet_Error;
    }

    return kExprRet_Ok;
}


/**
 * Initializes a new variables with a boolean value.
 *
 * @param   pVar    The new variable.
 * @param   f       The boolean value.
 */
static void expr_var_init_bool(PEXPRVAR pVar, int f)
{
    pVar->enmType = kExprVar_Num;
    pVar->uVal.i = !!f;
}


/**
 * Assigns a boolean value to a variable.
 *
 * @param   pVar    The variable.
 * @param   f       The boolean value.
 */
static void expr_var_assign_bool(PEXPRVAR pVar, int f)
{
    expr_var_delete(pVar);
    expr_var_init_bool(pVar, f);
}


/**
 * Turns the variable into an boolean.
 *
 * @returns the boolean interpretation.
 * @param   pVar    The variable.
 */
static int expr_var_make_bool(PEXPRVAR pVar)
{
    switch (pVar->enmType)
    {
        case kExprVar_Num:
            pVar->uVal.i = !!pVar->uVal.i;
            break;

        case kExprVar_String:
            expr_var_make_simple_string(pVar);
            /* fall thru */
        case kExprVar_SimpleString:
        {
            /*
             * Try convert it to a number. If that fails, use the
             * GNU make boolean logic - not empty string means true.
             */
            EXPRINT64 iVal;
            char const *psz = pVar->uVal.psz;
            while (isblank((unsigned char)*psz))
                psz++;
            if (    *psz
                &&  expr_string_to_num(NULL, &iVal, psz, 1 /* fQuiet */) >= kExprRet_Ok)
                expr_var_assign_bool(pVar, iVal != 0);
            else
                expr_var_assign_bool(pVar, *psz != '\0');
            break;
        }

        case kExprVar_QuotedString:
            expr_var_make_simple_string(pVar);
            /* fall thru */
        case kExprVar_QuotedSimpleString:
            /*
             * Use GNU make boolean logic (not empty string means true).
             * No stripping here, the string is quoted.
             */
            expr_var_assign_bool(pVar, *pVar->uVal.psz != '\0');
            break;

        default:
            assert(0);
            break;
    }

    return pVar->uVal.i;
}


/**
 * Pops a varable off the stack and deletes it.
 * @param   pThis   The evaluator instance.
 */
static void expr_pop_and_delete_var(PEXPR pThis)
{
    expr_var_delete(&pThis->aVars[pThis->iVar]);
    pThis->iVar--;
}



/**
 * Tries to make the variables the same type.
 *
 * This will not convert numbers to strings, unless one of them
 * is a quoted string.
 *
 * this will try convert both to numbers if neither is quoted. Both
 * conversions will have to suceed for this to be commited.
 *
 * All strings will be simplified.
 *
 * @returns status code. Done complaining on failure.
 *
 * @param   pThis   The evaluator instance.
 * @param   pVar1   The first variable.
 * @param   pVar2   The second variable.
 */
static EXPRRET expr_var_unify_types(PEXPR pThis, PEXPRVAR pVar1, PEXPRVAR pVar2, const char *pszOp)
{
    /*
     * Try make the variables the same type before comparing.
     */
    if (    !expr_var_was_quoted(pVar1)
        &&  !expr_var_was_quoted(pVar2))
    {
        if (    expr_var_is_string(pVar1)
            ||  expr_var_is_string(pVar2))
        {
            if (!expr_var_is_string(pVar1))
                expr_var_try_make_num(pVar2);
            else if (!expr_var_is_string(pVar2))
                expr_var_try_make_num(pVar1);
            else
            {
                /*
                 * Both are strings, simplify them then see if both can be made into numbers.
                 */
                EXPRINT64 iVar1;
                EXPRINT64 iVar2;

                expr_var_make_simple_string(pVar1);
                expr_var_make_simple_string(pVar2);

                if (    expr_string_to_num(NULL, &iVar1, pVar1->uVal.psz, 1 /* fQuiet */) >= kExprRet_Ok
                    &&  expr_string_to_num(NULL, &iVar2, pVar2->uVal.psz, 1 /* fQuiet */) >= kExprRet_Ok)
                {
                    expr_var_assign_num(pVar1, iVar1);
                    expr_var_assign_num(pVar2, iVar2);
                }
            }
        }
    }
    else
    {
        expr_var_make_simple_string(pVar1);
        expr_var_make_simple_string(pVar2);
    }

    /*
     * Complain if they aren't the same type now.
     */
    if (expr_var_is_string(pVar1) != expr_var_is_string(pVar2))
    {
        expr_error(pThis, "Unable to unify types for \"%s\"", pszOp);
        return kExprRet_Error;
    }
    return kExprRet_Ok;
}


/**
 * Is variable defined, unary.
 *
 * @returns Status code.
 * @param   pThis       The instance.
 */
static EXPRRET expr_op_defined(PEXPR pThis)
{
    PEXPRVAR            pVar = &pThis->aVars[pThis->iVar];
    struct variable    *pMakeVar;

    expr_var_make_simple_string(pVar);
    pMakeVar = lookup_variable(pVar->uVal.psz, strlen(pVar->uVal.psz));
    expr_var_assign_bool(pVar, pMakeVar && *pMakeVar->value != '\0');

    return kExprRet_Ok;
}


/**
 * Does file(/dir/whatever) exist, unary.
 *
 * @returns Status code.
 * @param   pThis       The instance.
 */
static EXPRRET expr_op_exists(PEXPR pThis)
{
    PEXPRVAR            pVar = &pThis->aVars[pThis->iVar];
    struct stat         st;

    expr_var_make_simple_string(pVar);
    expr_var_assign_bool(pVar, stat(pVar->uVal.psz, &st) == 0);

    return kExprRet_Ok;
}


/**
 * Is target defined, unary.
 *
 * @returns Status code.
 * @param   pThis       The instance.
 */
static EXPRRET expr_op_target(PEXPR pThis)
{
    PEXPRVAR            pVar = &pThis->aVars[pThis->iVar];
    struct file        *pFile = NULL;

    /*
     * Because of secondary target expansion, lookup the unexpanded
     * name first.
     */
#ifdef CONFIG_WITH_2ND_TARGET_EXPANSION
    if (    pVar->enmType == kExprVar_String
        ||  pVar->enmType == kExprVar_QuotedString)
    {
        pFile = lookup_file(pVar->uVal.psz);
        if (    pFile
            &&  !pFile->need_2nd_target_expansion)
            pFile = NULL;
    }
    if (!pFile)
#endif
    {
        expr_var_make_simple_string(pVar);
        pFile = lookup_file(pVar->uVal.psz);
    }

    /*
     * Always inspect the head of a multiple target rule
     * and look for a file with commands.
     */
#ifdef CONFIG_WITH_EXPLICIT_MULTITARGET
    if (pFile && pFile->multi_head)
        pFile = pFile->multi_head;
#endif

    while (pFile && !pFile->cmds)
        pFile = pFile->prev;

    expr_var_assign_bool(pVar, pFile != NULL && pFile->is_target);

    return kExprRet_Ok;
}


/**
 * Convert to boolean.
 *
 * @returns Status code.
 * @param   pThis       The instance.
 */
static EXPRRET expr_op_bool(PEXPR pThis)
{
    expr_var_make_bool(&pThis->aVars[pThis->iVar]);
    return kExprRet_Ok;
}


/**
 * Convert to number, works on quoted strings too.
 *
 * @returns Status code.
 * @param   pThis       The instance.
 */
static EXPRRET expr_op_num(PEXPR pThis)
{
    PEXPRVAR pVar = &pThis->aVars[pThis->iVar];

    /* unquote the string */
    if (pVar->enmType == kExprVar_QuotedSimpleString)
        pVar->enmType = kExprVar_SimpleString;
    else if (pVar->enmType == kExprVar_QuotedString)
        pVar->enmType = kExprVar_String;

    return expr_var_make_num(pThis, pVar);
}


/**
 * Convert to string (simplified and quoted)
 *
 * @returns Status code.
 * @param   pThis       The instance.
 */
static EXPRRET expr_op_str(PEXPR pThis)
{
    PEXPRVAR pVar = &pThis->aVars[pThis->iVar];

    expr_var_make_simple_string(pVar);
    pVar->enmType = kExprVar_QuotedSimpleString;

    return kExprRet_Ok;
}


/**
 * Pluss (dummy / make_integer)
 *
 * @returns Status code.
 * @param   pThis       The instance.
 */
static EXPRRET expr_op_pluss(PEXPR pThis)
{
    return expr_var_make_num(pThis, &pThis->aVars[pThis->iVar]);
}


/**
 * Minus (negate)
 *
 * @returns Status code.
 * @param   pThis       The instance.
 */
static EXPRRET expr_op_minus(PEXPR pThis)
{
    EXPRRET     rc;
    PEXPRVAR    pVar = &pThis->aVars[pThis->iVar];

    rc = expr_var_make_num(pThis, pVar);
    if (rc >= kExprRet_Ok)
        pVar->uVal.i = -pVar->uVal.i;

    return rc;
}



/**
 * Bitwise NOT.
 *
 * @returns Status code.
 * @param   pThis       The instance.
 */
static EXPRRET expr_op_bitwise_not(PEXPR pThis)
{
    EXPRRET     rc;
    PEXPRVAR    pVar = &pThis->aVars[pThis->iVar];

    rc = expr_var_make_num(pThis, pVar);
    if (rc >= kExprRet_Ok)
        pVar->uVal.i = ~pVar->uVal.i;

    return rc;
}


/**
 * Logical NOT.
 *
 * @returns Status code.
 * @param   pThis       The instance.
 */
static EXPRRET expr_op_logical_not(PEXPR pThis)
{
    PEXPRVAR    pVar = &pThis->aVars[pThis->iVar];

    expr_var_make_bool(pVar);
    pVar->uVal.i = !pVar->uVal.i;

    return kExprRet_Ok;
}


/**
 * Multiplication.
 *
 * @returns Status code.
 * @param   pThis       The instance.
 */
static EXPRRET expr_op_multiply(PEXPR pThis)
{
    EXPRRET     rc = kExprRet_Ok;
    PEXPRVAR    pVar1 = &pThis->aVars[pThis->iVar - 1];
    PEXPRVAR    pVar2 = &pThis->aVars[pThis->iVar];

    rc = expr_var_make_num(pThis, pVar1);
    if (rc >= kExprRet_Ok)
    {
        rc = expr_var_make_num(pThis, pVar2);
        if (rc >= kExprRet_Ok)
            pVar1->uVal.i *= pVar2->uVal.i;
    }

    expr_pop_and_delete_var(pThis);
    return rc;
}



/**
 * Division.
 *
 * @returns Status code.
 * @param   pThis       The instance.
 */
static EXPRRET expr_op_divide(PEXPR pThis)
{
    EXPRRET     rc = kExprRet_Ok;
    PEXPRVAR    pVar1 = &pThis->aVars[pThis->iVar - 1];
    PEXPRVAR    pVar2 = &pThis->aVars[pThis->iVar];

    rc = expr_var_make_num(pThis, pVar1);
    if (rc >= kExprRet_Ok)
    {
        rc = expr_var_make_num(pThis, pVar2);
        if (rc >= kExprRet_Ok)
            pVar1->uVal.i /= pVar2->uVal.i;
    }

    expr_pop_and_delete_var(pThis);
    return rc;
}



/**
 * Modulus.
 *
 * @returns Status code.
 * @param   pThis       The instance.
 */
static EXPRRET expr_op_modulus(PEXPR pThis)
{
    EXPRRET     rc = kExprRet_Ok;
    PEXPRVAR    pVar1 = &pThis->aVars[pThis->iVar - 1];
    PEXPRVAR    pVar2 = &pThis->aVars[pThis->iVar];

    rc = expr_var_make_num(pThis, pVar1);
    if (rc >= kExprRet_Ok)
    {
        rc = expr_var_make_num(pThis, pVar2);
        if (rc >= kExprRet_Ok)
            pVar1->uVal.i %= pVar2->uVal.i;
    }

    expr_pop_and_delete_var(pThis);
    return rc;
}



/**
 * Addition (numeric).
 *
 * @returns Status code.
 * @param   pThis       The instance.
 */
static EXPRRET expr_op_add(PEXPR pThis)
{
    EXPRRET     rc = kExprRet_Ok;
    PEXPRVAR    pVar1 = &pThis->aVars[pThis->iVar - 1];
    PEXPRVAR    pVar2 = &pThis->aVars[pThis->iVar];

    rc = expr_var_make_num(pThis, pVar1);
    if (rc >= kExprRet_Ok)
    {
        rc = expr_var_make_num(pThis, pVar2);
        if (rc >= kExprRet_Ok)
            pVar1->uVal.i += pVar2->uVal.i;
    }

    expr_pop_and_delete_var(pThis);
    return rc;
}


/**
 * Subtract (numeric).
 *
 * @returns Status code.
 * @param   pThis       The instance.
 */
static EXPRRET expr_op_sub(PEXPR pThis)
{
    EXPRRET     rc = kExprRet_Ok;
    PEXPRVAR    pVar1 = &pThis->aVars[pThis->iVar - 1];
    PEXPRVAR    pVar2 = &pThis->aVars[pThis->iVar];

    rc = expr_var_make_num(pThis, pVar1);
    if (rc >= kExprRet_Ok)
    {
        rc = expr_var_make_num(pThis, pVar2);
        if (rc >= kExprRet_Ok)
            pVar1->uVal.i -= pVar2->uVal.i;
    }

    expr_pop_and_delete_var(pThis);
    return rc;
}

/**
 * Bitwise left shift.
 *
 * @returns Status code.
 * @param   pThis       The instance.
 */
static EXPRRET expr_op_shift_left(PEXPR pThis)
{
    EXPRRET     rc = kExprRet_Ok;
    PEXPRVAR    pVar1 = &pThis->aVars[pThis->iVar - 1];
    PEXPRVAR    pVar2 = &pThis->aVars[pThis->iVar];

    rc = expr_var_make_num(pThis, pVar1);
    if (rc >= kExprRet_Ok)
    {
        rc = expr_var_make_num(pThis, pVar2);
        if (rc >= kExprRet_Ok)
            pVar1->uVal.i <<= pVar2->uVal.i;
    }

    expr_pop_and_delete_var(pThis);
    return rc;
}


/**
 * Bitwise right shift.
 *
 * @returns Status code.
 * @param   pThis       The instance.
 */
static EXPRRET expr_op_shift_right(PEXPR pThis)
{
    EXPRRET     rc = kExprRet_Ok;
    PEXPRVAR    pVar1 = &pThis->aVars[pThis->iVar - 1];
    PEXPRVAR    pVar2 = &pThis->aVars[pThis->iVar];

    rc = expr_var_make_num(pThis, pVar1);
    if (rc >= kExprRet_Ok)
    {
        rc = expr_var_make_num(pThis, pVar2);
        if (rc >= kExprRet_Ok)
            pVar1->uVal.i >>= pVar2->uVal.i;
    }

    expr_pop_and_delete_var(pThis);
    return rc;
}


/**
 * Less than or equal
 *
 * @returns Status code.
 * @param   pThis       The instance.
 */
static EXPRRET expr_op_less_or_equal_than(PEXPR pThis)
{
    EXPRRET     rc = kExprRet_Ok;
    PEXPRVAR    pVar1 = &pThis->aVars[pThis->iVar - 1];
    PEXPRVAR    pVar2 = &pThis->aVars[pThis->iVar];

    rc = expr_var_unify_types(pThis, pVar1, pVar2, "<=");
    if (rc >= kExprRet_Ok)
    {
        if (!expr_var_is_string(pVar1))
            expr_var_assign_bool(pVar1, pVar1->uVal.i <= pVar2->uVal.i);
        else
            expr_var_assign_bool(pVar1, strcmp(pVar1->uVal.psz, pVar2->uVal.psz) <= 0);
    }

    expr_pop_and_delete_var(pThis);
    return rc;
}


/**
 * Less than.
 *
 * @returns Status code.
 * @param   pThis       The instance.
 */
static EXPRRET expr_op_less_than(PEXPR pThis)
{
    EXPRRET     rc = kExprRet_Ok;
    PEXPRVAR    pVar1 = &pThis->aVars[pThis->iVar - 1];
    PEXPRVAR    pVar2 = &pThis->aVars[pThis->iVar];

    rc = expr_var_unify_types(pThis, pVar1, pVar2, "<");
    if (rc >= kExprRet_Ok)
    {
        if (!expr_var_is_string(pVar1))
            expr_var_assign_bool(pVar1, pVar1->uVal.i < pVar2->uVal.i);
        else
            expr_var_assign_bool(pVar1, strcmp(pVar1->uVal.psz, pVar2->uVal.psz) < 0);
    }

    expr_pop_and_delete_var(pThis);
    return rc;
}


/**
 * Greater or equal than
 *
 * @returns Status code.
 * @param   pThis       The instance.
 */
static EXPRRET expr_op_greater_or_equal_than(PEXPR pThis)
{
    EXPRRET     rc = kExprRet_Ok;
    PEXPRVAR    pVar1 = &pThis->aVars[pThis->iVar - 1];
    PEXPRVAR    pVar2 = &pThis->aVars[pThis->iVar];

    rc = expr_var_unify_types(pThis, pVar1, pVar2, ">=");
    if (rc >= kExprRet_Ok)
    {
        if (!expr_var_is_string(pVar1))
            expr_var_assign_bool(pVar1, pVar1->uVal.i >= pVar2->uVal.i);
        else
            expr_var_assign_bool(pVar1, strcmp(pVar1->uVal.psz, pVar2->uVal.psz) >= 0);
    }

    expr_pop_and_delete_var(pThis);
    return rc;
}


/**
 * Greater than.
 *
 * @returns Status code.
 * @param   pThis       The instance.
 */
static EXPRRET expr_op_greater_than(PEXPR pThis)
{
    EXPRRET     rc = kExprRet_Ok;
    PEXPRVAR    pVar1 = &pThis->aVars[pThis->iVar - 1];
    PEXPRVAR    pVar2 = &pThis->aVars[pThis->iVar];

    rc = expr_var_unify_types(pThis, pVar1, pVar2, ">");
    if (rc >= kExprRet_Ok)
    {
        if (!expr_var_is_string(pVar1))
            expr_var_assign_bool(pVar1, pVar1->uVal.i > pVar2->uVal.i);
        else
            expr_var_assign_bool(pVar1, strcmp(pVar1->uVal.psz, pVar2->uVal.psz) > 0);
    }

    expr_pop_and_delete_var(pThis);
    return rc;
}


/**
 * Equal.
 *
 * @returns Status code.
 * @param   pThis       The instance.
 */
static EXPRRET expr_op_equal(PEXPR pThis)
{
    EXPRRET     rc = kExprRet_Ok;
    PEXPRVAR    pVar1 = &pThis->aVars[pThis->iVar - 1];
    PEXPRVAR    pVar2 = &pThis->aVars[pThis->iVar];

    /*
     * The same type?
     */
    if (expr_var_is_string(pVar1) == expr_var_is_string(pVar2))
    {
        if (!expr_var_is_string(pVar1))
            /* numbers are simple */
            expr_var_assign_bool(pVar1, pVar1->uVal.i == pVar2->uVal.i);
        else
        {
            /* try a normal string compare. */
            expr_var_make_simple_string(pVar1);
            expr_var_make_simple_string(pVar2);
            if (!strcmp(pVar1->uVal.psz, pVar2->uVal.psz))
                expr_var_assign_bool(pVar1, 1);
            /* try convert and compare as number instead. */
            else if (   expr_var_try_make_num(pVar1) >= kExprRet_Ok
                     && expr_var_try_make_num(pVar2) >= kExprRet_Ok)
                expr_var_assign_bool(pVar1, pVar1->uVal.i == pVar2->uVal.i);
            /* ok, they really aren't equal. */
            else
                expr_var_assign_bool(pVar1, 0);
        }
    }
    else
    {
        /*
         * If the type differs, there are now two options:
         *  1. Convert the string to a valid number and compare the numbers.
         *  2. Convert an empty string to a 'false' boolean value and compare
         *     numerically. This one is a bit questionable, so we don't try this.
         */
        if (   expr_var_try_make_num(pVar1) >= kExprRet_Ok
            && expr_var_try_make_num(pVar2) >= kExprRet_Ok)
            expr_var_assign_bool(pVar1, pVar1->uVal.i == pVar2->uVal.i);
        else
        {
            expr_error(pThis, "Cannot compare strings and numbers");
            rc = kExprRet_Error;
        }
    }

    expr_pop_and_delete_var(pThis);
    return kExprRet_Ok;
}


/**
 * Not equal.
 *
 * @returns Status code.
 * @param   pThis       The instance.
 */
static EXPRRET expr_op_not_equal(PEXPR pThis)
{
    EXPRRET rc = expr_op_equal(pThis);
    if (rc >= kExprRet_Ok)
        rc = expr_op_logical_not(pThis);
    return rc;
}


/**
 * Bitwise AND.
 *
 * @returns Status code.
 * @param   pThis       The instance.
 */
static EXPRRET expr_op_bitwise_and(PEXPR pThis)
{
    PEXPRVAR    pVar1 = &pThis->aVars[pThis->iVar - 1];
    PEXPRVAR    pVar2 = &pThis->aVars[pThis->iVar];
    EXPRRET     rc;

    rc = expr_var_make_num(pThis, pVar1);
    if (rc >= kExprRet_Ok)
    {
        rc = expr_var_make_num(pThis, pVar2);
        if (rc >= kExprRet_Ok)
            pVar1->uVal.i &= pVar2->uVal.i;
    }

    expr_pop_and_delete_var(pThis);
    return kExprRet_Ok;
}


/**
 * Bitwise XOR.
 *
 * @returns Status code.
 * @param   pThis       The instance.
 */
static EXPRRET expr_op_bitwise_xor(PEXPR pThis)
{
    PEXPRVAR    pVar1 = &pThis->aVars[pThis->iVar - 1];
    PEXPRVAR    pVar2 = &pThis->aVars[pThis->iVar];
    EXPRRET     rc;

    rc = expr_var_make_num(pThis, pVar1);
    if (rc >= kExprRet_Ok)
    {
        rc = expr_var_make_num(pThis, pVar2);
        if (rc >= kExprRet_Ok)
            pVar1->uVal.i ^= pVar2->uVal.i;
    }

    expr_pop_and_delete_var(pThis);
    return kExprRet_Ok;
}


/**
 * Bitwise OR.
 *
 * @returns Status code.
 * @param   pThis       The instance.
 */
static EXPRRET expr_op_bitwise_or(PEXPR pThis)
{
    PEXPRVAR    pVar1 = &pThis->aVars[pThis->iVar - 1];
    PEXPRVAR    pVar2 = &pThis->aVars[pThis->iVar];
    EXPRRET     rc;

    rc = expr_var_make_num(pThis, pVar1);
    if (rc >= kExprRet_Ok)
    {
        rc = expr_var_make_num(pThis, pVar2);
        if (rc >= kExprRet_Ok)
            pVar1->uVal.i |= pVar2->uVal.i;
    }

    expr_pop_and_delete_var(pThis);
    return kExprRet_Ok;
}


/**
 * Logical AND.
 *
 * @returns Status code.
 * @param   pThis       The instance.
 */
static EXPRRET expr_op_logical_and(PEXPR pThis)
{
    PEXPRVAR    pVar1 = &pThis->aVars[pThis->iVar - 1];
    PEXPRVAR    pVar2 = &pThis->aVars[pThis->iVar];

    if (   expr_var_make_bool(pVar1)
        && expr_var_make_bool(pVar2))
        expr_var_assign_bool(pVar1, 1);
    else
        expr_var_assign_bool(pVar1, 0);

    expr_pop_and_delete_var(pThis);
    return kExprRet_Ok;
}


/**
 * Logical OR.
 *
 * @returns Status code.
 * @param   pThis       The instance.
 */
static EXPRRET expr_op_logical_or(PEXPR pThis)
{
    PEXPRVAR    pVar1 = &pThis->aVars[pThis->iVar - 1];
    PEXPRVAR    pVar2 = &pThis->aVars[pThis->iVar];

    if (   expr_var_make_bool(pVar1)
        || expr_var_make_bool(pVar2))
        expr_var_assign_bool(pVar1, 1);
    else
        expr_var_assign_bool(pVar1, 0);

    expr_pop_and_delete_var(pThis);
    return kExprRet_Ok;
}


/**
 * Left parenthesis.
 *
 * @returns Status code.
 * @param   pThis       The instance.
 */
static EXPRRET expr_op_left_parenthesis(PEXPR pThis)
{
    /*
     * There should be a right parenthesis operator lined up for us now,
     * eat it. If not found there is an inbalance.
     */
    EXPRRET rc = expr_get_binary_or_eoe_or_rparen(pThis);
    if (    rc == kExprRet_Operator
        &&  pThis->apOps[pThis->iOp]->szOp[0] == ')')
    {
        /* pop it and get another one which we can leave pending. */
        pThis->iOp--;
        rc = expr_get_binary_or_eoe_or_rparen(pThis);
        if (rc >= kExprRet_Ok)
            expr_unget_op(pThis);
    }
    else
    {
        expr_error(pThis, "Missing ')'");
        rc = kExprRet_Error;
    }

    return rc;
}


/**
 * Right parenthesis, dummy that's never actually called.
 *
 * @returns Status code.
 * @param   pThis       The instance.
 */
static EXPRRET expr_op_right_parenthesis(PEXPR pThis)
{
    assert(0);
    (void)pThis;
    return kExprRet_Ok;
}





/**
 * The operator table.
 *
 * This table is NOT ordered by precedence, but for linear search
 * allowing for first match to return the correct operator. This
 * means that || must come before |, or else | will match all.
 */
static const EXPROP g_aExprOps[] =
{
#define EXPR_OP(szOp, iPrecedence, cArgs, pfn)  {  szOp, sizeof(szOp) - 1, '\0', iPrecedence, cArgs, pfn }
    /*        Name, iPrecedence,  cArgs,    pfn    */
    EXPR_OP("defined",     90,      1,    expr_op_defined),
    EXPR_OP("exists",      90,      1,    expr_op_exists),
    EXPR_OP("target",      90,      1,    expr_op_target),
    EXPR_OP("bool",        90,      1,    expr_op_bool),
    EXPR_OP("num",         90,      1,    expr_op_num),
    EXPR_OP("str",         90,      1,    expr_op_str),
    EXPR_OP("+",           80,      1,    expr_op_pluss),
    EXPR_OP("-",           80,      1,    expr_op_minus),
    EXPR_OP("~",           80,      1,    expr_op_bitwise_not),
    EXPR_OP("*",           75,      2,    expr_op_multiply),
    EXPR_OP("/",           75,      2,    expr_op_divide),
    EXPR_OP("%",           75,      2,    expr_op_modulus),
    EXPR_OP("+",           70,      2,    expr_op_add),
    EXPR_OP("-",           70,      2,    expr_op_sub),
    EXPR_OP("<<",          65,      2,    expr_op_shift_left),
    EXPR_OP(">>",          65,      2,    expr_op_shift_right),
    EXPR_OP("<=",          60,      2,    expr_op_less_or_equal_than),
    EXPR_OP("<",           60,      2,    expr_op_less_than),
    EXPR_OP(">=",          60,      2,    expr_op_greater_or_equal_than),
    EXPR_OP(">",           60,      2,    expr_op_greater_than),
    EXPR_OP("==",          55,      2,    expr_op_equal),
    EXPR_OP("!=",          55,      2,    expr_op_not_equal),
    EXPR_OP("!",           80,      1,    expr_op_logical_not),
    EXPR_OP("^",           45,      2,    expr_op_bitwise_xor),
    EXPR_OP("&&",          35,      2,    expr_op_logical_and),
    EXPR_OP("&",           50,      2,    expr_op_bitwise_and),
    EXPR_OP("||",          30,      2,    expr_op_logical_or),
    EXPR_OP("|",           40,      2,    expr_op_bitwise_or),
            { "(", 1, ')',   10,      1,    expr_op_left_parenthesis },
            { ")", 1, '(',   10,      0,    expr_op_right_parenthesis },
 /*         { "?", 1, ':',    5,      2,    expr_op_question },
            { ":", 1, '?',    5,      2,    expr_op_colon }, -- too weird for now. */
#undef EXPR_OP
};

/** Dummy end of expression fake. */
static const EXPROP g_ExprEndOfExpOp =
{
              "", 0, '\0',    0,      0,    NULL
};


/**
 * Initializes the opcode character map if necessary.
 */
static void expr_map_init(void)
{
    unsigned i;
    if (g_fExprInitializedMap)
        return;

    /*
     * Initialize it.
     */
    memset(&g_auchOpStartCharMap, 0, sizeof(g_auchOpStartCharMap));
    for (i = 0; i < sizeof(g_aExprOps) / sizeof(g_aExprOps[0]); i++)
    {
        unsigned int ch = (unsigned int)g_aExprOps[i].szOp[0];
        if (!g_auchOpStartCharMap[ch])
            g_auchOpStartCharMap[ch] = (i << 1) | 1;
    }

    g_fExprInitializedMap = 1;
}


/**
 * Looks up a character in the map.
 *
 * @returns the value for that char.
 * @retval  0 if not a potential opcode start char.
 * @retval  non-zero if it's a potential operator. The low bit is always set
 *          while the remaining 7 bits is the index into the operator table
 *          of the first match.
 *
 * @param   ch      The character.
 */
static unsigned char expr_map_get(char ch)
{
    return g_auchOpStartCharMap[(unsigned int)ch];
}


/**
 * Searches the operator table given a potential operator start char.
 *
 * @returns Pointer to the matching operator. NULL if not found.
 * @param   psz     Pointer to what can be an operator.
 * @param   uchVal  The expr_map_get value.
 * @param   fUnary  Whether it must be an unary operator or not.
 */
static PCEXPROP expr_lookup_op(char const *psz, unsigned char uchVal, int fUnary)
{
    char ch = *psz;
    unsigned i;

    for (i = uchVal >> 1; i < sizeof(g_aExprOps) / sizeof(g_aExprOps[0]); i++)
    {
        /* compare the string... */
        switch (g_aExprOps[i].cchOp)
        {
            case 1:
                if (g_aExprOps[i].szOp[0] != ch)
                    continue;
                break;
            case 2:
                if (    g_aExprOps[i].szOp[0] != ch
                    ||  g_aExprOps[i].szOp[1] != psz[1])
                    continue;
                break;
            default:
                if (    g_aExprOps[i].szOp[0] != ch
                    ||  strncmp(&g_aExprOps[i].szOp[1], psz + 1, g_aExprOps[i].cchOp - 1))
                    continue;
                break;
        }

        /* ... and the operator type. */
        if (fUnary == (g_aExprOps[i].cArgs == 1))
        {
            /* got a match! */
            return &g_aExprOps[i];
        }
    }

    return NULL;
}


/**
 * Ungets a binary operator.
 *
 * The operator is poped from the stack and put in the pending position.
 *
 * @param   pThis       The evaluator instance.
 */
static void expr_unget_op(PEXPR pThis)
{
    assert(pThis->pPending == NULL);
    assert(pThis->iOp >= 0);

    pThis->pPending = pThis->apOps[pThis->iOp];
    pThis->apOps[pThis->iOp] = NULL;
    pThis->iOp--;
}



/**
 * Get the next token, it should be a binary operator, or the end of
 * the expression, or a right parenthesis.
 *
 * The operator is pushed onto the stack and the status code indicates
 * which of the two we found.
 *
 * @returns status code. Will grumble on failure.
 * @retval  kExprRet_EndOfExpr if we encountered the end of the expression.
 * @retval  kExprRet_Operator if we encountered a binary operator or right
 *          parenthesis. It's on the operator stack.
 *
 * @param   pThis       The evaluator instance.
 */
static EXPRRET expr_get_binary_or_eoe_or_rparen(PEXPR pThis)
{
    /*
     * See if there is anything pending first.
     */
    PCEXPROP pOp = pThis->pPending;
    if (pOp)
        pThis->pPending = NULL;
    else
    {
        /*
         * Eat more of the expression.
         */
        char const *psz = pThis->psz;

        /* spaces */
        while (isspace((unsigned int)*psz))
            psz++;
        /* see what we've got. */
        if (*psz)
        {
            unsigned char uchVal = expr_map_get(*psz);
            if (uchVal)
                pOp = expr_lookup_op(psz, uchVal, 0 /* fUnary */);
            if (!pOp)
            {
                expr_error(pThis, "Expected binary operator, found \"%.42s\"...", psz);
                return kExprRet_Error;
            }
            psz += pOp->cchOp;
        }
        else
            pOp = &g_ExprEndOfExpOp;
        pThis->psz = psz;
    }

    /*
     * Push it.
     */
    if (pThis->iOp >= EXPR_MAX_OPERATORS - 1)
    {
        expr_error(pThis, "Operator stack overflow");
        return kExprRet_Error;
    }
    pThis->apOps[++pThis->iOp] = pOp;

    return pOp->iPrecedence
         ? kExprRet_Operator
         : kExprRet_EndOfExpr;
}



/**
 * Get the next token, it should be an unary operator or an operand.
 *
 * This will fail if encountering the end of the expression since
 * it is implied that there should be something more.
 *
 * The token is pushed onto the respective stack and the status code
 * indicates which it is.
 *
 * @returns status code. On failure we'll be done bitching already.
 * @retval  kExprRet_Operator if we encountered an unary operator.
 *          It's on the operator stack.
 * @retval  kExprRet_Operand if we encountered an operand operator.
 *          It's on the operand stack.
 *
 * @param   This        The evaluator instance.
 */
static EXPRRET expr_get_unary_or_operand(PEXPR pThis)
{
    EXPRRET       rc;
    unsigned char uchVal;
    PCEXPROP      pOp;
    char const   *psz = pThis->psz;

    /*
     * Eat white space and make sure there is something after it.
     */
    while (isspace((unsigned int)*psz))
        psz++;
    if (!*psz)
    {
        expr_error(pThis, "Unexpected end of expression");
        return kExprRet_Error;
    }

    /*
     * Is it an operator?
     */
    pOp = NULL;
    uchVal = expr_map_get(*psz);
    if (uchVal)
        pOp = expr_lookup_op(psz, uchVal, 1 /* fUnary */);
    if (pOp)
    {
        /*
         * Push the operator onto the stack.
         */
        if (pThis->iVar < EXPR_MAX_OPERANDS - 1)
        {
            pThis->apOps[++pThis->iOp] = pOp;
            rc = kExprRet_Operator;
        }
        else
        {
            expr_error(pThis, "Operator stack overflow");
            rc = kExprRet_Error;
        }
        psz += pOp->cchOp;
    }
    else if (pThis->iVar < EXPR_MAX_OPERANDS - 1)
    {
        /*
         * It's an operand. Figure out where it ends and
         * push it onto the stack.
         */
        const char *pszStart;

        rc = kExprRet_Ok;
        if (*psz == '"')
        {
            pszStart = ++psz;
            while (*psz && *psz != '"')
                psz++;
            expr_var_init_substring(&pThis->aVars[++pThis->iVar], pszStart, psz - pszStart, kExprVar_QuotedString);
            if (*psz)
                psz++;
        }
        else if (*psz == '\'')
        {
            pszStart = ++psz;
            while (*psz && *psz != '\'')
                psz++;
            expr_var_init_substring(&pThis->aVars[++pThis->iVar], pszStart, psz - pszStart, kExprVar_QuotedSimpleString);
            if (*psz)
                psz++;
        }
        else
        {
            char    achPars[20];
            int     iPar = -1;
            char    chEndPar = '\0';
            char    ch, ch2;

            pszStart = psz;
            while ((ch = *psz) != '\0')
            {
                /* $(adsf) or ${asdf} needs special handling. */
                if (    ch == '$'
                    &&  (   (ch2 = psz[1]) == '('
                         || ch2 == '{'))
                {
                    psz++;
                    if (iPar > (int)(sizeof(achPars) / sizeof(achPars[0])))
                    {
                        expr_error(pThis, "Too deep nesting of variable expansions");
                        rc = kExprRet_Error;
                        break;
                    }
                    achPars[++iPar] = chEndPar = ch2 == '(' ? ')' : '}';
                }
                else if (ch == chEndPar)
                {
                    iPar--;
                    chEndPar = iPar >= 0 ? achPars[iPar] : '\0';
                }
                else if (!chEndPar)
                {
                    /** @todo combine isspace and expr_map_get! */
                    unsigned chVal = expr_map_get(ch);
                    if (chVal)
                    {
                        pOp = expr_lookup_op(psz, uchVal, 0 /* fUnary */);
                        if (pOp)
                            break;
                    }
                    if (isspace((unsigned char)ch))
                        break;
                }

                /* next */
                psz++;
            }

            if (rc == kExprRet_Ok)
                expr_var_init_substring(&pThis->aVars[++pThis->iVar], pszStart, psz - pszStart, kExprVar_String);
        }
    }
    else
    {
        expr_error(pThis, "Operand stack overflow");
        rc = kExprRet_Error;
    }
    pThis->psz = psz;

    return rc;
}


/**
 * Evaluates the current expression.
 *
 * @returns status code.
 *
 * @param   pThis       The instance.
 */
static EXPRRET expr_eval(PEXPR pThis)
{
    EXPRRET  rc;
    PCEXPROP pOp;

    /*
     * The main loop.
     */
    for (;;)
    {
        /*
         * Eat unary operators until we hit an operand.
         */
        do  rc = expr_get_unary_or_operand(pThis);
        while (rc == kExprRet_Operator);
        if (rc < kExprRet_Error)
            break;

        /*
         * Look for a binary operator, right parenthesis or end of expression.
         */
        rc = expr_get_binary_or_eoe_or_rparen(pThis);
        if (rc < kExprRet_Error)
            break;
        expr_unget_op(pThis);

        /*
         * Pop operators and apply them.
         *
         * Parenthesis will be handed via precedence, where the left parenthesis
         * will go pop the right one and make another operator pending.
         */
        while (   pThis->iOp >= 0
               && pThis->apOps[pThis->iOp]->iPrecedence >= pThis->pPending->iPrecedence)
        {
            pOp = pThis->apOps[pThis->iOp--];
            assert(pThis->iVar + 1 >= pOp->cArgs);
            rc = pOp->pfn(pThis);
            if (rc < kExprRet_Error)
                break;
        }
        if (rc < kExprRet_Error)
            break;

        /*
         * Get the next binary operator or end of expression.
         * There should be no right parenthesis here.
         */
        rc = expr_get_binary_or_eoe_or_rparen(pThis);
        if (rc < kExprRet_Error)
            break;
        pOp = pThis->apOps[pThis->iOp];
        if (!pOp->iPrecedence)
            break;  /* end of expression */
        if (!pOp->cArgs)
        {
            expr_error(pThis, "Unexpected \"%s\"", pOp->szOp);
            rc = kExprRet_Error;
            break;
        }
    }

    return rc;
}


/**
 * Destroys the given instance.
 *
 * @param   pThis       The instance to destroy.
 */
static void expr_destroy(PEXPR pThis)
{
    while (pThis->iVar >= 0)
    {
        expr_var_delete(pThis->aVars);
        pThis->iVar--;
    }
    free(pThis);
}


/**
 * Instantiates an expression evaluator.
 *
 * @returns The instance.
 *
 * @param   pszExpr     What to parse.
 *                      This must stick around until expr_destroy.
 */
static PEXPR expr_create(char const *pszExpr)
{
    PEXPR pThis = (PEXPR)xmalloc(sizeof(*pThis));
    pThis->pszExpr = pszExpr;
    pThis->psz = pszExpr;
    pThis->pFileLoc = NULL;
    pThis->pPending = NULL;
    pThis->iVar = -1;
    pThis->iOp = -1;

    expr_map_init();
    return pThis;
}


/**
 * Evaluates the given if expression.
 *
 * @returns -1, 0 or 1. (GNU make conditional check convention, see read.c.)
 * @retval  -1 if the expression is invalid.
 * @retval  0 if the expression is true
 * @retval  1 if the expression is false.
 *
 * @param   line    The expression.
 * @param   flocp   The file location, used for errors.
 */
int expr_eval_if_conditionals(const char *line, const struct floc *flocp)
{
    /*
     * Instantiate the expression evaluator and let
     * it have a go at it.
     */
    int rc = -1;
    PEXPR pExpr = expr_create(line);
    pExpr->pFileLoc = flocp;
    if (expr_eval(pExpr) >= kExprRet_Ok)
    {
        /*
         * Convert the result (on top of the stack) to boolean and
         * set our return value accordingly.
         */
        if (expr_var_make_bool(&pExpr->aVars[0]))
            rc = 0;
        else
            rc = 1;
    }
    expr_destroy(pExpr);

    return rc;
}


/**
 * Evaluates the given expression and returns the result as a string.
 *
 * @returns variable buffer position.
 *
 * @param   o       The current variable buffer position.
 * @param   expr    The expression.
 */
char *expr_eval_to_string(char *o, const char *expr)
{
    /*
     * Instantiate the expression evaluator and let
     * it have a go at it.
     */
    PEXPR pExpr = expr_create(expr);
    if (expr_eval(pExpr) >= kExprRet_Ok)
    {
        /*
         * Convert the result (on top of the stack) to a string
         * and copy it out the variable buffer.
         */
        PEXPRVAR pVar = &pExpr->aVars[0];
        expr_var_make_simple_string(pVar);
        o = variable_buffer_output(o, pVar->uVal.psz, strlen(pVar->uVal.psz));
    }
    else
        o = variable_buffer_output(o, "<expression evaluation failed>", sizeof("<expression evaluation failed>") - 1);
    expr_destroy(pExpr);

    return o;
}


#endif /* CONFIG_WITH_IF_CONDITIONALS */

