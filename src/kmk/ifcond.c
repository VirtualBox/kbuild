#ifdef CONFIG_WITH_IF_CONDITIONALS
/* $Id: ifcond.c 1723 2008-09-04 14:10:08Z knut.osmundsen@oracle.com $ */
/** @file
 * ifcond - C like if expressions.
 */

/*
 * Copyright (c) 2008 knut st. osmundsen <bird-src-spam@anduin.net>
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
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
#define IFCOND_NUM_LEN  ((sizeof("-9223372036854775802") + 4) & ~3)

/** The max operator stack depth. */
#define IFCOND_MAX_OPERATORS  72
/** The max operand depth. */
#define IFCOND_MAX_OPERANDS   128


/*******************************************************************************
*   Structures and Typedefs                                                    *
*******************************************************************************/
/** The 64-bit signed integer type we're using. */
#ifdef _MSC_VER
typedef __int64 IFCONDINT64;
#else
# include <stdint.h>
typedef int64_t IFCONDINT64;
#endif

/** Pointer to a evaluator instance. */
typedef struct IFCOND *PIFCOND;


/**
 * Operand variable type.
 */
typedef enum
{
    /** Invalid zero entry. */
    kIfCondVar_Invalid = 0,
    /** A number. */
    kIfCondVar_Num,
    /** A string in need of expanding (perhaps). */
    kIfCondVar_String,
    /** A simple string that doesn't need expanding. */
    kIfCondVar_SimpleString,
    /** A quoted string in need of expanding (perhaps). */
    kIfCondVar_QuotedString,
    /** A simple quoted string that doesn't need expanding. */
    kIfCondVar_QuotedSimpleString,
    /** The end of the valid variable types. */
    kIfCondVar_End
} IFCONDVARTYPE;

/**
 * Operand variable.
 */
typedef struct
{
    /** The variable type. */
    IFCONDVARTYPE enmType;
    /** The variable. */
    union
    {
        /** Pointer to the string. */
        char *psz;
        /** The variable. */
        IFCONDINT64 i;
    } uVal;
} IFCONDVAR;
/** Pointer to a operand variable. */
typedef IFCONDVAR *PIFCONDVAR;
/** Pointer to a const operand variable. */
typedef IFCONDVAR const *PCIFCONDVAR;

/**
 * Operator return statuses.
 */
typedef enum
{
    kIfCondRet_Error = -1,
    kIfCondRet_Ok = 0,
    kIfCondRet_Operator,
    kIfCondRet_Operand,
    kIfCondRet_EndOfExpr,
    kIfCondRet_End
} IFCONDRET;

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
    IFCONDRET (*pfn)(PIFCOND pThis);
} IFCONDOP;
/** Pointer to a const operator. */
typedef IFCONDOP const *PCIFCONDOP;

/**
 * Expression evaluator instance.
 */
typedef struct IFCOND
{
    /** The full expression. */
    const char *pszExpr;
    /** The current location. */
    const char *psz;
    /** The current file location, used for errors. */
    const struct floc *pFileLoc;
    /** Pending binary operator. */
    PCIFCONDOP pPending;
    /** Top of the operator stack. */
    int iOp;
    /** Top of the operand stack. */
    int iVar;
    /** The operator stack. */
    PCIFCONDOP apOps[IFCOND_MAX_OPERATORS];
    /** The operand stack. */
    IFCONDVAR aVars[IFCOND_MAX_OPERANDS];
} IFCOND;


/*******************************************************************************
*   Global Variables                                                           *
*******************************************************************************/
/** Operator start character map.
 * This indicates which characters that are starting operators and which aren't. */
static char g_auchOpStartCharMap[256];
/** Whether we've initialized the map. */
static int g_fIfCondInitializedMap = 0;


/*******************************************************************************
*   Internal Functions                                                         *
*******************************************************************************/
static void ifcond_unget_op(PIFCOND pThis);
static IFCONDRET ifcond_get_binary_or_eoe_or_rparen(PIFCOND pThis);




/**
 * Displays an error message.
 *
 * The total string length must not exceed 256 bytes.
 *
 * @param   pThis       The evaluator instance.
 * @param   pszError    The message format string.
 * @param   ...         The message format args.
 */
static void ifcond_error(PIFCOND pThis, const char *pszError, ...)
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
 * @param   pszDst  The string buffer to write into. Assumes length of IFCOND_NUM_LEN.
 * @param   iSrc    The number to convert.
 */
static char *ifcond_num_to_string(char *pszDst, IFCONDINT64 iSrc)
{
    static const char s_szDigits[17] = "0123456789abcdef";
    char szTmp[IFCOND_NUM_LEN];
    char *psz = &szTmp[IFCOND_NUM_LEN - 1];
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
    psz++;
    return (char *)memcpy(pszDst, psz, &szTmp[IFCOND_NUM_LEN] - psz);
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
static IFCONDRET ifcond_string_to_num(PIFCOND pThis, IFCONDINT64 *piDst, const char *pszSrc, int fQuiet)
{
    IFCONDRET rc = kIfCondRet_Ok;
    char const *psz = pszSrc;
    IFCONDINT64 i;
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
        int iDigit;
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
                ifcond_error(pThis, "Invalid a number \"%.80s\"", pszSrc);
            return kIfCondRet_Error;
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
static int ifcond_var_is_string(PCIFCONDVAR pVar)
{
    return pVar->enmType >= kIfCondVar_String;
}


/**
 * Checks if the variable contains a string that was quoted
 * in the expression.
 *
 * @returns 1 if if was a quoted string, otherwise 0.
 * @param   pVar    The variable.
 */
static int ifcond_var_was_quoted(PCIFCONDVAR pVar)
{
    return pVar->enmType >= kIfCondVar_QuotedString;
}


/**
 * Deletes a variable.
 *
 * @param   pVar    The variable.
 */
static void ifcond_var_delete(PIFCONDVAR pVar)
{
    if (ifcond_var_is_string(pVar))
    {
        free(pVar->uVal.psz);
        pVar->uVal.psz = NULL;
    }
    pVar->enmType = kIfCondVar_Invalid;
}


/**
 * Initializes a new variables with a sub-string value.
 *
 * @param   pVar    The new variable.
 * @param   psz     The start of the string value.
 * @param   cch     The number of chars to copy.
 * @param   enmType The string type.
 */
static void ifcond_var_init_substring(PIFCONDVAR pVar, const char *psz, size_t cch, IFCONDVARTYPE enmType)
{
    /* convert string needing expanding into simple ones if possible.  */
    if (    enmType == kIfCondVar_String
        &&  !memchr(psz, '$', cch))
        enmType = kIfCondVar_SimpleString;
    else if (   enmType == kIfCondVar_QuotedString
             && !memchr(psz, '$', cch))
        enmType = kIfCondVar_QuotedSimpleString;

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
static void ifcond_var_init_string(PIFCONDVAR pVar, const char *psz, IFCONDVARTYPE enmType)
{
    ifcond_var_init_substring(pVar, psz, strlen(psz), enmType);
}


/**
 * Assigns a sub-string value to a variable.
 *
 * @param   pVar    The new variable.
 * @param   psz     The start of the string value.
 * @param   cch     The number of chars to copy.
 * @param   enmType The string type.
 */
static void ifcond_var_assign_substring(PIFCONDVAR pVar, const char *psz, size_t cch, IFCONDVARTYPE enmType)
{
    ifcond_var_delete(pVar);
    ifcond_var_init_substring(pVar, psz, cch, enmType);
}


/**
 * Assignes a string value to a variable.
 *
 * @param   pVar    The variable.
 * @param   psz     The string value.
 * @param   enmType The string type.
 */
static void ifcond_var_assign_string(PIFCONDVAR pVar, const char *psz, IFCONDVARTYPE enmType)
{
    ifcond_var_delete(pVar);
    ifcond_var_init_string(pVar, psz, enmType);
}
#endif /* unused */


/**
 * Simplifies a string variable.
 *
 * @param   pVar    The variable.
 */
static void ifcond_var_make_simple_string(PIFCONDVAR pVar)
{
    switch (pVar->enmType)
    {
        case kIfCondVar_Num:
        {
            char *psz = (char *)xmalloc(IFCOND_NUM_LEN);
            ifcond_num_to_string(psz, pVar->uVal.i);
            pVar->uVal.psz = psz;
            pVar->enmType = kIfCondVar_SimpleString;
            break;
        }

        case kIfCondVar_String:
        case kIfCondVar_QuotedString:
        {
            char *psz;
            assert(strchr(pVar->uVal.psz, '$'));

            psz = allocated_variable_expand(pVar->uVal.psz);
            free(pVar->uVal.psz);
            pVar->uVal.psz = psz;

            pVar->enmType = pVar->enmType == kIfCondVar_String
                          ? kIfCondVar_SimpleString
                          : kIfCondVar_QuotedSimpleString;
            break;
        }

        case kIfCondVar_SimpleString:
        case kIfCondVar_QuotedSimpleString:
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
static void ifcond_var_make_string(PIFCONDVAR pVar)
{
    switch (pVar->enmType)
    {
        case kIfCondVar_Num:
            ifcond_var_make_simple_string(pVar);
            break;

        case kIfCondVar_String:
        case kIfCondVar_SimpleString:
        case kIfCondVar_QuotedString:
        case kIfCondVar_QuotedSimpleString:
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
static void ifcond_var_init_num(PIFCONDVAR pVar, IFCONDINT64 i)
{
    pVar->enmType = kIfCondVar_Num;
    pVar->uVal.i = i;
}


/**
 * Assigns a integer value to a variable.
 *
 * @param   pVar    The variable.
 * @param   i       The integer value.
 */
static void ifcond_var_assign_num(PIFCONDVAR pVar, IFCONDINT64 i)
{
    ifcond_var_delete(pVar);
    ifcond_var_init_num(pVar, i);
}


/**
 * Turns the variable into a number.
 *
 * @returns status code.
 * @param   pThis   The evaluator instance.
 * @param   pVar    The variable.
 */
static IFCONDRET ifcond_var_make_num(PIFCOND pThis, PIFCONDVAR pVar)
{
    switch (pVar->enmType)
    {
        case kIfCondVar_Num:
            /* nothing to do. */
            break;

        case kIfCondVar_String:
            ifcond_var_make_simple_string(pVar);
            /* fall thru */
        case kIfCondVar_SimpleString:
        {
            IFCONDINT64 i;
            IFCONDRET rc = ifcond_string_to_num(pThis, &i, pVar->uVal.psz, 0 /* fQuiet */);
            if (rc < kIfCondRet_Ok)
                return rc;
            ifcond_var_assign_num(pVar, i);
            break;
        }

        case kIfCondVar_QuotedString:
        case kIfCondVar_QuotedSimpleString:
            ifcond_error(pThis, "Cannot convert a quoted string to a number");
            return kIfCondRet_Error;

        default:
            assert(0);
            return kIfCondRet_Error;
    }

    return kIfCondRet_Ok;
}


/**
 * Try to turn the variable into a number.
 *
 * @returns status code.
 * @param   pVar    The variable.
 */
static IFCONDRET ifcond_var_try_make_num(PIFCONDVAR pVar)
{
    switch (pVar->enmType)
    {
        case kIfCondVar_Num:
            /* nothing to do. */
            break;

        case kIfCondVar_String:
            ifcond_var_make_simple_string(pVar);
            /* fall thru */
        case kIfCondVar_SimpleString:
        {
            IFCONDINT64 i;
            IFCONDRET rc = ifcond_string_to_num(NULL, &i, pVar->uVal.psz, 1 /* fQuiet */);
            if (rc < kIfCondRet_Ok)
                return rc;
            ifcond_var_assign_num(pVar, i);
            break;
        }

        default:
            assert(0);
        case kIfCondVar_QuotedString:
        case kIfCondVar_QuotedSimpleString:
            /* can't do this */
            return kIfCondRet_Error;
    }

    return kIfCondRet_Ok;
}


/**
 * Initializes a new variables with a boolean value.
 *
 * @param   pVar    The new variable.
 * @param   f       The boolean value.
 */
static void ifcond_var_init_bool(PIFCONDVAR pVar, int f)
{
    pVar->enmType = kIfCondVar_Num;
    pVar->uVal.i = !!f;
}


/**
 * Assigns a boolean value to a variable.
 *
 * @param   pVar    The variable.
 * @param   f       The boolean value.
 */
static void ifcond_var_assign_bool(PIFCONDVAR pVar, int f)
{
    ifcond_var_delete(pVar);
    ifcond_var_init_bool(pVar, f);
}


/**
 * Turns the variable into an boolean.
 *
 * @returns the boolean interpretation.
 * @param   pVar    The variable.
 */
static int ifcond_var_make_bool(PIFCONDVAR pVar)
{
    switch (pVar->enmType)
    {
        case kIfCondVar_Num:
            pVar->uVal.i = !!pVar->uVal.i;
            break;

        case kIfCondVar_String:
            ifcond_var_make_simple_string(pVar);
            /* fall thru */
        case kIfCondVar_SimpleString:
        {
            /*
             * Try convert it to a number. If that fails, use the
             * GNU make boolean logic - not empty string means true.
             */
            IFCONDINT64 iVal;
            char const *psz = pVar->uVal.psz;
            while (isblank((unsigned char)*psz))
                psz++;
            if (    *psz
                &&  ifcond_string_to_num(NULL, &iVal, psz, 1 /* fQuiet */) >= kIfCondRet_Ok)
                ifcond_var_assign_bool(pVar, iVal != 0);
            else
                ifcond_var_assign_bool(pVar, *psz != '\0');
            break;
        }

        case kIfCondVar_QuotedString:
            ifcond_var_make_simple_string(pVar);
            /* fall thru */
        case kIfCondVar_QuotedSimpleString:
            /*
             * Use GNU make boolean logic (not empty string means true).
             * No stripping here, the string is quoted.
             */
            ifcond_var_assign_bool(pVar, *pVar->uVal.psz != '\0');
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
static void ifcond_pop_and_delete_var(PIFCOND pThis)
{
    ifcond_var_delete(&pThis->aVars[pThis->iVar]);
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
static IFCONDRET ifcond_var_unify_types(PIFCOND pThis, PIFCONDVAR pVar1, PIFCONDVAR pVar2, const char *pszOp)
{
    /*
     * Try make the variables the same type before comparing.
     */
    if (    !ifcond_var_was_quoted(pVar1)
        &&  !ifcond_var_was_quoted(pVar2))
    {
        if (    ifcond_var_is_string(pVar1)
            ||  ifcond_var_is_string(pVar2))
        {
            if (!ifcond_var_is_string(pVar1))
                ifcond_var_try_make_num(pVar2);
            else if (!ifcond_var_is_string(pVar2))
                ifcond_var_try_make_num(pVar1);
            else
            {
                /*
                 * Both are strings, simplify them then see if both can be made into numbers.
                 */
                IFCONDINT64 iVar1;
                IFCONDINT64 iVar2;

                ifcond_var_make_simple_string(pVar1);
                ifcond_var_make_simple_string(pVar2);

                if (    ifcond_string_to_num(NULL, &iVar1, pVar1->uVal.psz, 1 /* fQuiet */) >= kIfCondRet_Ok
                    &&  ifcond_string_to_num(NULL, &iVar2, pVar2->uVal.psz, 1 /* fQuiet */) >= kIfCondRet_Ok)
                {
                    ifcond_var_assign_num(pVar1, iVar1);
                    ifcond_var_assign_num(pVar2, iVar2);
                }
            }
        }
    }
    else
    {
        ifcond_var_make_simple_string(pVar1);
        ifcond_var_make_simple_string(pVar2);
    }

    /*
     * Complain if they aren't the same type now.
     */
    if (ifcond_var_is_string(pVar1) != ifcond_var_is_string(pVar2))
    {
        ifcond_error(pThis, "Unable to unify types for \"%s\"", pszOp);
        return kIfCondRet_Error;
    }
    return kIfCondRet_Ok;
}


/**
 * Is variable defined, unary.
 *
 * @returns Status code.
 * @param   pThis       The instance.
 */
static IFCONDRET ifcond_op_defined(PIFCOND pThis)
{
    PIFCONDVAR          pVar = &pThis->aVars[pThis->iVar];
    struct variable    *pMakeVar;
    assert(pThis->iVar >= 0);

    ifcond_var_make_simple_string(pVar);
    pMakeVar = lookup_variable(pVar->uVal.psz, strlen(pVar->uVal.psz));
    ifcond_var_assign_bool(pVar, pMakeVar && *pMakeVar->value != '\0');

    return kIfCondRet_Ok;
}


/**
 * Is target defined, unary.
 *
 * @returns Status code.
 * @param   pThis       The instance.
 */
static IFCONDRET ifcond_op_target(PIFCOND pThis)
{
    PIFCONDVAR          pVar = &pThis->aVars[pThis->iVar];
    struct file        *pFile = NULL;
    assert(pThis->iVar >= 0);

    /*
     * Because of secondary target expansion, lookup the unexpanded
     * name first.
     */
#ifdef CONFIG_WITH_2ND_TARGET_EXPANSION
    if (    pVar->enmType == kIfCondVar_String
        ||  pVar->enmType == kIfCondVar_QuotedString)
    {
        pFile = lookup_file(pVar->uVal.psz);
        if (    pFile
            &&  !pFile->need_2nd_target_expansion)
            pFile = NULL;
    }
    if (pFile)
#endif
    {
        ifcond_var_make_simple_string(pVar);
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

    ifcond_var_assign_bool(pVar, pFile != NULL && pFile->is_target);

    return kIfCondRet_Ok;
}


/**
 * Pluss (dummy / make_integer)
 *
 * @returns Status code.
 * @param   pThis       The instance.
 */
static IFCONDRET ifcond_op_pluss(PIFCOND pThis)
{
    PIFCONDVAR pVar = &pThis->aVars[pThis->iVar];
    assert(pThis->iVar >= 0);

    return ifcond_var_make_num(pThis, pVar);
}



/**
 * Minus (negate)
 *
 * @returns Status code.
 * @param   pThis       The instance.
 */
static IFCONDRET ifcond_op_minus(PIFCOND pThis)
{
    IFCONDRET  rc;
    PIFCONDVAR pVar = &pThis->aVars[pThis->iVar];
    assert(pThis->iVar >= 0);

    rc = ifcond_var_make_num(pThis, pVar);
    if (rc >= kIfCondRet_Ok)
        pVar->uVal.i = -pVar->uVal.i;

    return rc;
}



/**
 * Bitwise NOT.
 *
 * @returns Status code.
 * @param   pThis       The instance.
 */
static IFCONDRET ifcond_op_bitwise_not(PIFCOND pThis)
{
    IFCONDRET  rc;
    PIFCONDVAR pVar = &pThis->aVars[pThis->iVar];
    assert(pThis->iVar >= 0);

    rc = ifcond_var_make_num(pThis, pVar);
    if (rc >= kIfCondRet_Ok)
        pVar->uVal.i = ~pVar->uVal.i;

    return rc;
}


/**
 * Logical NOT.
 *
 * @returns Status code.
 * @param   pThis       The instance.
 */
static IFCONDRET ifcond_op_logical_not(PIFCOND pThis)
{
    PIFCONDVAR pVar = &pThis->aVars[pThis->iVar];
    assert(pThis->iVar >= 0);

    ifcond_var_assign_bool(pVar, !ifcond_var_make_bool(pVar));

    return kIfCondRet_Ok;
}


/**
 * Multiplication.
 *
 * @returns Status code.
 * @param   pThis       The instance.
 */
static IFCONDRET ifcond_op_multiply(PIFCOND pThis)
{
    IFCONDRET   rc = kIfCondRet_Ok;
    PIFCONDVAR  pVar1 = &pThis->aVars[pThis->iVar - 1];
    PIFCONDVAR  pVar2 = &pThis->aVars[pThis->iVar];
    assert(pThis->iVar >= 1);

    rc = ifcond_var_make_num(pThis, pVar1);
    if (rc >= kIfCondRet_Ok)
    {
        rc = ifcond_var_make_num(pThis, pVar2);
        if (rc >= kIfCondRet_Ok)
            pVar1->uVal.i %= pVar2->uVal.i;
    }

    ifcond_pop_and_delete_var(pThis);
    return rc;
}



/**
 * Division.
 *
 * @returns Status code.
 * @param   pThis       The instance.
 */
static IFCONDRET ifcond_op_divide(PIFCOND pThis)
{
    IFCONDRET   rc = kIfCondRet_Ok;
    PIFCONDVAR  pVar1 = &pThis->aVars[pThis->iVar - 1];
    PIFCONDVAR  pVar2 = &pThis->aVars[pThis->iVar];
    assert(pThis->iVar >= 1);

    rc = ifcond_var_make_num(pThis, pVar1);
    if (rc >= kIfCondRet_Ok)
    {
        rc = ifcond_var_make_num(pThis, pVar2);
        if (rc >= kIfCondRet_Ok)
            pVar1->uVal.i /= pVar2->uVal.i;
    }

    ifcond_pop_and_delete_var(pThis);
    return rc;
}



/**
 * Modulus.
 *
 * @returns Status code.
 * @param   pThis       The instance.
 */
static IFCONDRET ifcond_op_modulus(PIFCOND pThis)
{
    IFCONDRET   rc = kIfCondRet_Ok;
    PIFCONDVAR  pVar1 = &pThis->aVars[pThis->iVar - 1];
    PIFCONDVAR  pVar2 = &pThis->aVars[pThis->iVar];
    assert(pThis->iVar >= 1);

    rc = ifcond_var_make_num(pThis, pVar1);
    if (rc >= kIfCondRet_Ok)
    {
        rc = ifcond_var_make_num(pThis, pVar2);
        if (rc >= kIfCondRet_Ok)
            pVar1->uVal.i %= pVar2->uVal.i;
    }

    ifcond_pop_and_delete_var(pThis);
    return rc;
}



/**
 * Addition (numeric).
 *
 * @returns Status code.
 * @param   pThis       The instance.
 */
static IFCONDRET ifcond_op_add(PIFCOND pThis)
{
    IFCONDRET   rc = kIfCondRet_Ok;
    PIFCONDVAR  pVar1 = &pThis->aVars[pThis->iVar - 1];
    PIFCONDVAR  pVar2 = &pThis->aVars[pThis->iVar];
    assert(pThis->iVar >= 1);

    rc = ifcond_var_make_num(pThis, pVar1);
    if (rc >= kIfCondRet_Ok)
    {
        rc = ifcond_var_make_num(pThis, pVar2);
        if (rc >= kIfCondRet_Ok)
            pVar1->uVal.i += pVar2->uVal.i;
    }

    ifcond_pop_and_delete_var(pThis);
    return rc;
}


/**
 * Subtract (numeric).
 *
 * @returns Status code.
 * @param   pThis       The instance.
 */
static IFCONDRET ifcond_op_sub(PIFCOND pThis)
{
    IFCONDRET   rc = kIfCondRet_Ok;
    PIFCONDVAR  pVar1 = &pThis->aVars[pThis->iVar - 1];
    PIFCONDVAR  pVar2 = &pThis->aVars[pThis->iVar];
    assert(pThis->iVar >= 1);

    rc = ifcond_var_make_num(pThis, pVar1);
    if (rc >= kIfCondRet_Ok)
    {
        rc = ifcond_var_make_num(pThis, pVar2);
        if (rc >= kIfCondRet_Ok)
            pVar1->uVal.i -= pVar2->uVal.i;
    }

    ifcond_pop_and_delete_var(pThis);
    return rc;
}

/**
 * Bitwise left shift.
 *
 * @returns Status code.
 * @param   pThis       The instance.
 */
static IFCONDRET ifcond_op_shift_left(PIFCOND pThis)
{
    IFCONDRET   rc = kIfCondRet_Ok;
    PIFCONDVAR  pVar1 = &pThis->aVars[pThis->iVar - 1];
    PIFCONDVAR  pVar2 = &pThis->aVars[pThis->iVar];
    assert(pThis->iVar >= 1);

    rc = ifcond_var_make_num(pThis, pVar1);
    if (rc >= kIfCondRet_Ok)
    {
        rc = ifcond_var_make_num(pThis, pVar2);
        if (rc >= kIfCondRet_Ok)
            pVar1->uVal.i <<= pVar2->uVal.i;
    }

    ifcond_pop_and_delete_var(pThis);
    return rc;
}


/**
 * Bitwise right shift.
 *
 * @returns Status code.
 * @param   pThis       The instance.
 */
static IFCONDRET ifcond_op_shift_right(PIFCOND pThis)
{
    IFCONDRET   rc = kIfCondRet_Ok;
    PIFCONDVAR  pVar1 = &pThis->aVars[pThis->iVar - 1];
    PIFCONDVAR  pVar2 = &pThis->aVars[pThis->iVar];
    assert(pThis->iVar >= 1);

    rc = ifcond_var_make_num(pThis, pVar1);
    if (rc >= kIfCondRet_Ok)
    {
        rc = ifcond_var_make_num(pThis, pVar2);
        if (rc >= kIfCondRet_Ok)
            pVar1->uVal.i >>= pVar2->uVal.i;
    }

    ifcond_pop_and_delete_var(pThis);
    return rc;
}


/**
 * Less than or equal
 *
 * @returns Status code.
 * @param   pThis       The instance.
 */
static IFCONDRET ifcond_op_less_or_equal_than(PIFCOND pThis)
{
    IFCONDRET   rc = kIfCondRet_Ok;
    PIFCONDVAR  pVar1 = &pThis->aVars[pThis->iVar - 1];
    PIFCONDVAR  pVar2 = &pThis->aVars[pThis->iVar];
    assert(pThis->iVar >= 1);

    rc = ifcond_var_unify_types(pThis, pVar1, pVar2, "<=");
    if (rc >= kIfCondRet_Ok)
    {
        if (!ifcond_var_is_string(pVar1))
            ifcond_var_assign_bool(pVar1, pVar1->uVal.i <= pVar2->uVal.i);
        else
            ifcond_var_assign_bool(pVar1, strcmp(pVar1->uVal.psz, pVar2->uVal.psz) <= 0);
    }

    ifcond_pop_and_delete_var(pThis);
    return rc;
}


/**
 * Less than.
 *
 * @returns Status code.
 * @param   pThis       The instance.
 */
static IFCONDRET ifcond_op_less_than(PIFCOND pThis)
{
    IFCONDRET   rc = kIfCondRet_Ok;
    PIFCONDVAR  pVar1 = &pThis->aVars[pThis->iVar - 1];
    PIFCONDVAR  pVar2 = &pThis->aVars[pThis->iVar];
    assert(pThis->iVar >= 1);

    rc = ifcond_var_unify_types(pThis, pVar1, pVar2, "<");
    if (rc >= kIfCondRet_Ok)
    {
        if (!ifcond_var_is_string(pVar1))
            ifcond_var_assign_bool(pVar1, pVar1->uVal.i < pVar2->uVal.i);
        else
            ifcond_var_assign_bool(pVar1, strcmp(pVar1->uVal.psz, pVar2->uVal.psz) < 0);
    }

    ifcond_pop_and_delete_var(pThis);
    return rc;
}


/**
 * Greater or equal than
 *
 * @returns Status code.
 * @param   pThis       The instance.
 */
static IFCONDRET ifcond_op_greater_or_equal_than(PIFCOND pThis)
{
    IFCONDRET   rc = kIfCondRet_Ok;
    PIFCONDVAR  pVar1 = &pThis->aVars[pThis->iVar - 1];
    PIFCONDVAR  pVar2 = &pThis->aVars[pThis->iVar];
    assert(pThis->iVar >= 1);

    rc = ifcond_var_unify_types(pThis, pVar1, pVar2, ">=");
    if (rc >= kIfCondRet_Ok)
    {
        if (!ifcond_var_is_string(pVar1))
            ifcond_var_assign_bool(pVar1, pVar1->uVal.i >= pVar2->uVal.i);
        else
            ifcond_var_assign_bool(pVar1, strcmp(pVar1->uVal.psz, pVar2->uVal.psz) >= 0);
    }

    ifcond_pop_and_delete_var(pThis);
    return rc;
}


/**
 * Greater than.
 *
 * @returns Status code.
 * @param   pThis       The instance.
 */
static IFCONDRET ifcond_op_greater_than(PIFCOND pThis)
{
    IFCONDRET   rc = kIfCondRet_Ok;
    PIFCONDVAR  pVar1 = &pThis->aVars[pThis->iVar - 1];
    PIFCONDVAR  pVar2 = &pThis->aVars[pThis->iVar];
    assert(pThis->iVar >= 1);

    rc = ifcond_var_unify_types(pThis, pVar1, pVar2, ">");
    if (rc >= kIfCondRet_Ok)
    {
        if (!ifcond_var_is_string(pVar1))
            ifcond_var_assign_bool(pVar1, pVar1->uVal.i > pVar2->uVal.i);
        else
            ifcond_var_assign_bool(pVar1, strcmp(pVar1->uVal.psz, pVar2->uVal.psz) > 0);
    }

    ifcond_pop_and_delete_var(pThis);
    return rc;
}


/**
 * Equal.
 *
 * @returns Status code.
 * @param   pThis       The instance.
 */
static IFCONDRET ifcond_op_equal(PIFCOND pThis)
{
    IFCONDRET   rc = kIfCondRet_Ok;
    PIFCONDVAR  pVar1 = &pThis->aVars[pThis->iVar - 1];
    PIFCONDVAR  pVar2 = &pThis->aVars[pThis->iVar];
    assert(pThis->iVar >= 1);

    /*
     * The same type?
     */
    if (ifcond_var_is_string(pVar1) == ifcond_var_is_string(pVar2))
    {
        if (!ifcond_var_is_string(pVar1))
            /* numbers are simple */
            ifcond_var_assign_bool(pVar1, pVar1->uVal.i == pVar2->uVal.i);
        else
        {
            /* try a normal string compare. */
            ifcond_var_make_simple_string(pVar1);
            ifcond_var_make_simple_string(pVar2);
            if (!strcmp(pVar1->uVal.psz, pVar2->uVal.psz))
                ifcond_var_assign_bool(pVar1, 1);
            /* try convert and compare as number instead. */
            else if (   ifcond_var_try_make_num(pVar1) >= kIfCondRet_Ok
                     && ifcond_var_try_make_num(pVar2) >= kIfCondRet_Ok)
                ifcond_var_assign_bool(pVar1, pVar1->uVal.i == pVar2->uVal.i);
            /* ok, they really aren't equal. */
            else
                ifcond_var_assign_bool(pVar1, 0);
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
        if (   ifcond_var_try_make_num(pVar1) >= kIfCondRet_Ok
            && ifcond_var_try_make_num(pVar2) >= kIfCondRet_Ok)
            ifcond_var_assign_bool(pVar1, pVar1->uVal.i == pVar2->uVal.i);
        else
        {
            ifcond_error(pThis, "Cannot compare strings and numbers");
            rc = kIfCondRet_Error;
        }
    }

    ifcond_pop_and_delete_var(pThis);
    return kIfCondRet_Ok;
}


/**
 * Not equal.
 *
 * @returns Status code.
 * @param   pThis       The instance.
 */
static IFCONDRET ifcond_op_not_equal(PIFCOND pThis)
{
    IFCONDRET rc = ifcond_op_equal(pThis);
    if (rc >= kIfCondRet_Ok)
        rc = ifcond_op_logical_not(pThis);
    return rc;
}


/**
 * Bitwise AND.
 *
 * @returns Status code.
 * @param   pThis       The instance.
 */
static IFCONDRET ifcond_op_bitwise_and(PIFCOND pThis)
{
    PIFCONDVAR  pVar1 = &pThis->aVars[pThis->iVar - 1];
    PIFCONDVAR  pVar2 = &pThis->aVars[pThis->iVar];
    IFCONDRET   rc;
    assert(pThis->iVar >= 1);

    rc = ifcond_var_make_num(pThis, pVar1);
    if (rc >= kIfCondRet_Ok)
    {
        rc = ifcond_var_make_num(pThis, pVar2);
        if (rc >= kIfCondRet_Ok)
            pVar1->uVal.i &= pVar2->uVal.i;
    }

    ifcond_pop_and_delete_var(pThis);
    return kIfCondRet_Ok;
}


/**
 * Bitwise XOR.
 *
 * @returns Status code.
 * @param   pThis       The instance.
 */
static IFCONDRET ifcond_op_bitwise_xor(PIFCOND pThis)
{
    PIFCONDVAR  pVar1 = &pThis->aVars[pThis->iVar - 1];
    PIFCONDVAR  pVar2 = &pThis->aVars[pThis->iVar];
    IFCONDRET   rc;
    assert(pThis->iVar >= 1);

    rc = ifcond_var_make_num(pThis, pVar1);
    if (rc >= kIfCondRet_Ok)
    {
        rc = ifcond_var_make_num(pThis, pVar2);
        if (rc >= kIfCondRet_Ok)
            pVar1->uVal.i ^= pVar2->uVal.i;
    }

    ifcond_pop_and_delete_var(pThis);
    return kIfCondRet_Ok;
}


/**
 * Bitwise OR.
 *
 * @returns Status code.
 * @param   pThis       The instance.
 */
static IFCONDRET ifcond_op_bitwise_or(PIFCOND pThis)
{
    PIFCONDVAR  pVar1 = &pThis->aVars[pThis->iVar - 1];
    PIFCONDVAR  pVar2 = &pThis->aVars[pThis->iVar];
    IFCONDRET   rc;
    assert(pThis->iVar >= 1);

    rc = ifcond_var_make_num(pThis, pVar1);
    if (rc >= kIfCondRet_Ok)
    {
        rc = ifcond_var_make_num(pThis, pVar2);
        if (rc >= kIfCondRet_Ok)
            pVar1->uVal.i |= pVar2->uVal.i;
    }

    ifcond_pop_and_delete_var(pThis);
    return kIfCondRet_Ok;
}


/**
 * Logical AND.
 *
 * @returns Status code.
 * @param   pThis       The instance.
 */
static IFCONDRET ifcond_op_logical_and(PIFCOND pThis)
{
    PIFCONDVAR  pVar1 = &pThis->aVars[pThis->iVar - 1];
    PIFCONDVAR  pVar2 = &pThis->aVars[pThis->iVar];
    assert(pThis->iVar >= 1);

    if (   ifcond_var_make_bool(pVar1)
        && ifcond_var_make_bool(pVar2))
        ifcond_var_assign_bool(pVar1, 1);
    else
        ifcond_var_assign_bool(pVar1, 0);

    ifcond_pop_and_delete_var(pThis);
    return kIfCondRet_Ok;
}


/**
 * Logical OR.
 *
 * @returns Status code.
 * @param   pThis       The instance.
 */
static IFCONDRET ifcond_op_logical_or(PIFCOND pThis)
{
    PIFCONDVAR  pVar1 = &pThis->aVars[pThis->iVar - 1];
    PIFCONDVAR  pVar2 = &pThis->aVars[pThis->iVar];
    assert(pThis->iVar >= 1);

    if (   ifcond_var_make_bool(pVar1)
        || ifcond_var_make_bool(pVar2))
        ifcond_var_assign_bool(pVar1, 1);
    else
        ifcond_var_assign_bool(pVar1, 0);

    ifcond_pop_and_delete_var(pThis);
    return kIfCondRet_Ok;
}


/**
 * Left parenthesis.
 *
 * @returns Status code.
 * @param   pThis       The instance.
 */
static IFCONDRET ifcond_op_left_parenthesis(PIFCOND pThis)
{
    /*
     * There should be a right parenthesis operator lined up for us now,
     * eat it. If not found there is an inbalance.
     */
    IFCONDRET rc = ifcond_get_binary_or_eoe_or_rparen(pThis);
    if (    rc == kIfCondRet_Operator
        &&  pThis->apOps[pThis->iOp]->szOp[0] == ')')
    {
        /* pop it and get another one which we can leave pending. */
        pThis->iOp--;
        rc = ifcond_get_binary_or_eoe_or_rparen(pThis);
        if (rc >= kIfCondRet_Ok)
            ifcond_unget_op(pThis);
    }
    else
    {
        ifcond_error(pThis, "Missing ')'");
        rc = kIfCondRet_Error;
    }

    return rc;
}


/**
 * Right parenthesis, dummy that's never actually called.
 *
 * @returns Status code.
 * @param   pThis       The instance.
 */
static IFCONDRET ifcond_op_right_parenthesis(PIFCOND pThis)
{
    return kIfCondRet_Ok;
}





/**
 * The operator table.
 *
 * This table is NOT ordered by precedence, but for linear search
 * allowing for first match to return the correct operator. This
 * means that || must come before |, or else | will match all.
 */
static const IFCONDOP g_aIfCondOps[] =
{
#define IFCOND_OP(szOp, iPrecedence, cArgs, pfn)  {  szOp, sizeof(szOp) - 1, '\0', iPrecedence, cArgs, pfn }
    /*        Name, iPrecedence,  cArgs,    pfn    */
    IFCOND_OP("defined",     90,      1,    ifcond_op_defined),
    IFCOND_OP("target",      90,      1,    ifcond_op_target),
    IFCOND_OP("+",           80,      1,    ifcond_op_pluss),
    IFCOND_OP("-",           80,      1,    ifcond_op_minus),
    IFCOND_OP("~",           80,      1,    ifcond_op_bitwise_not),
    IFCOND_OP("*",           75,      2,    ifcond_op_multiply),
    IFCOND_OP("/",           75,      2,    ifcond_op_divide),
    IFCOND_OP("%",           75,      2,    ifcond_op_modulus),
    IFCOND_OP("+",           70,      2,    ifcond_op_add),
    IFCOND_OP("-",           70,      2,    ifcond_op_sub),
    IFCOND_OP("<<",          65,      2,    ifcond_op_shift_left),
    IFCOND_OP(">>",          65,      2,    ifcond_op_shift_right),
    IFCOND_OP("<=",          60,      2,    ifcond_op_less_or_equal_than),
    IFCOND_OP("<",           60,      2,    ifcond_op_less_than),
    IFCOND_OP(">=",          60,      2,    ifcond_op_greater_or_equal_than),
    IFCOND_OP(">",           60,      2,    ifcond_op_greater_than),
    IFCOND_OP("==",          55,      2,    ifcond_op_equal),
    IFCOND_OP("!=",          55,      2,    ifcond_op_not_equal),
    IFCOND_OP("!",           80,      1,    ifcond_op_logical_not),
    IFCOND_OP("^",           45,      2,    ifcond_op_bitwise_xor),
    IFCOND_OP("&&",          35,      2,    ifcond_op_logical_and),
    IFCOND_OP("&",           50,      2,    ifcond_op_bitwise_and),
    IFCOND_OP("||",          30,      2,    ifcond_op_logical_or),
    IFCOND_OP("|",           40,      2,    ifcond_op_bitwise_or),
            { "(", 1, ')',   10,      1,    ifcond_op_left_parenthesis },
            { ")", 1, '(',   10,      0,    ifcond_op_right_parenthesis },
 /*         { "?", 1, ':',    5,      2,    ifcond_op_question },
            { ":", 1, '?',    5,      2,    ifcond_op_colon }, -- too weird for now. */
#undef IFCOND_OP
};

/** Dummy end of expression fake. */
static const IFCONDOP g_IfCondEndOfExpOp =
{
              "", 0, '\0',    0,      0,    NULL
};


/**
 * Initializes the opcode character map if necessary.
 */
static void ifcond_map_init(void)
{
    int i;
    if (g_fIfCondInitializedMap)
        return;

    /*
     * Initialize it.
     */
    memset(&g_auchOpStartCharMap, 0, sizeof(g_auchOpStartCharMap));
    for (i = 0; i < sizeof(g_aIfCondOps) / sizeof(g_aIfCondOps[0]); i++)
    {
        unsigned int ch = (unsigned int)g_aIfCondOps[i].szOp[0];
        if (!g_auchOpStartCharMap[ch])
            g_auchOpStartCharMap[ch] = (i << 1) | 1;
    }

    g_fIfCondInitializedMap = 1;
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
static unsigned char ifcond_map_get(char ch)
{
    return g_auchOpStartCharMap[(unsigned int)ch];
}


/**
 * Searches the operator table given a potential operator start char.
 *
 * @returns Pointer to the matching operator. NULL if not found.
 * @param   psz     Pointer to what can be an operator.
 * @param   uchVal  The ifcond_map_get value.
 * @param   fUnary  Whether it must be an unary operator or not.
 */
static PCIFCONDOP ifcond_lookup_op(char const *psz, unsigned char uchVal, int fUnary)
{
    char ch = *psz;
    int i;

    for (i = uchVal >> 1; i < sizeof(g_aIfCondOps) / sizeof(g_aIfCondOps[0]); i++)
    {
        /* compare the string... */
        switch (g_aIfCondOps[i].cchOp)
        {
            case 1:
                if (g_aIfCondOps[i].szOp[0] != ch)
                    continue;
                break;
            case 2:
                if (    g_aIfCondOps[i].szOp[0] != ch
                    ||  g_aIfCondOps[i].szOp[1] != psz[1])
                    continue;
                break;
            default:
                if (    g_aIfCondOps[i].szOp[0] != ch
                    ||  strncmp(&g_aIfCondOps[i].szOp[1], psz + 1, g_aIfCondOps[i].cchOp - 1))
                    continue;
                break;
        }

        /* ... and the operator type. */
        if (fUnary == (g_aIfCondOps[i].cArgs == 1))
        {
            /* got a match! */
            return &g_aIfCondOps[i];
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
static void ifcond_unget_op(PIFCOND pThis)
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
 * @retval  kIfCondRet_EndOfExpr if we encountered the end of the expression.
 * @retval  kIfCondRet_Operator if we encountered a binary operator or right
 *          parenthesis. It's on the operator stack.
 *
 * @param   pThis       The evaluator instance.
 */
static IFCONDRET ifcond_get_binary_or_eoe_or_rparen(PIFCOND pThis)
{
    /*
     * See if there is anything pending first.
     */
    PCIFCONDOP pOp = pThis->pPending;
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
            unsigned char uchVal = ifcond_map_get(*psz);
            if (uchVal)
                pOp = ifcond_lookup_op(psz, uchVal, 0 /* fUnary */);
            if (!pOp)
            {
                ifcond_error(pThis, "Expected binary operator, found \"%.42s\"...", psz);
                return kIfCondRet_Error;
            }
            psz += pOp->cchOp;
        }
        else
            pOp = &g_IfCondEndOfExpOp;
        pThis->psz = psz;
    }

    /*
     * Push it.
     */
    if (pThis->iOp >= IFCOND_MAX_OPERATORS - 1)
    {
        ifcond_error(pThis, "Operator stack overflow");
        return kIfCondRet_Error;
    }
    pThis->apOps[++pThis->iOp] = pOp;

    return pOp->iPrecedence
         ? kIfCondRet_Operator
         : kIfCondRet_EndOfExpr;
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
 * @retval  kIfCondRet_Operator if we encountered an unary operator.
 *          It's on the operator stack.
 * @retval  kIfCondRet_Operand if we encountered an operand operator.
 *          It's on the operand stack.
 *
 * @param   This        The evaluator instance.
 */
static IFCONDRET ifcond_get_unary_or_operand(PIFCOND pThis)
{
    IFCONDRET       rc;
    unsigned char   uchVal;
    PCIFCONDOP      pOp;
    char const     *psz = pThis->psz;

    /*
     * Eat white space and make sure there is something after it.
     */
    while (isspace((unsigned int)*psz))
        psz++;
    if (!*psz)
    {
        ifcond_error(pThis, "Unexpected end of expression");
        return kIfCondRet_Error;
    }

    /*
     * Is it an operator?
     */
    pOp = NULL;
    uchVal = ifcond_map_get(*psz);
    if (uchVal)
        pOp = ifcond_lookup_op(psz, uchVal, 1 /* fUnary */);
    if (pOp)
    {
        /*
         * Push the operator onto the stack.
         */
        if (pThis->iVar < IFCOND_MAX_OPERANDS - 1)
        {
            pThis->apOps[++pThis->iOp] = pOp;
            rc = kIfCondRet_Operator;
        }
        else
        {
            ifcond_error(pThis, "Operator stack overflow");
            rc = kIfCondRet_Error;
        }
        psz += pOp->cchOp;
    }
    else if (pThis->iVar < IFCOND_MAX_OPERANDS - 1)
    {
        /*
         * It's an operand. Figure out where it ends and
         * push it onto the stack.
         */
        const char *pszStart = psz;

        rc = kIfCondRet_Ok;
        if (*psz == '"')
        {
            pszStart++;
            while (*psz && *psz != '"')
                psz++;
            ifcond_var_init_substring(&pThis->aVars[++pThis->iVar], pszStart, psz - pszStart, kIfCondVar_QuotedString);
        }
        else if (*psz == '\'')
        {
            pszStart++;
            while (*psz && *psz != '\'')
                psz++;
            ifcond_var_init_substring(&pThis->aVars[++pThis->iVar], pszStart, psz - pszStart, kIfCondVar_QuotedSimpleString);
        }
        else
        {
            char    achPars[20];
            int     iPar = -1;
            char    chEndPar = '\0';
            char    ch;

            while ((ch = *psz) != '\0')
            {
                /* $(adsf) or ${asdf} needs special handling. */
                if (    ch == '$'
                    &&  (   psz[1] == '('
                         || psz[1] == '{'))
                {
                    psz++;
                    if (iPar > sizeof(achPars) / sizeof(achPars[0]))
                    {
                        ifcond_error(pThis, "Too deep nesting of variable expansions");
                        rc = kIfCondRet_Error;
                        break;
                    }
                    achPars[++iPar] = chEndPar = ch == '(' ? ')' : '}';
                }
                else if (ch == chEndPar)
                {
                    iPar--;
                    chEndPar = iPar >= 0 ? achPars[iPar] : '\0';
                }
                else if (!chEndPar)
                {
                    /** @todo combine isspace and ifcond_map_get! */
                    unsigned chVal = ifcond_map_get(ch);
                    if (chVal)
                    {
                        PCIFCONDOP pOp = ifcond_lookup_op(psz, uchVal, 0 /* fUnary */);
                        if (pOp)
                            break;
                    }
                    if (isspace((unsigned char)ch))
                        break;
                }

                /* next */
                psz++;
            }

            if (rc == kIfCondRet_Ok)
                ifcond_var_init_substring(&pThis->aVars[++pThis->iVar], pszStart, psz - pszStart, kIfCondVar_String);
        }
    }
    else
    {
        ifcond_error(pThis, "Operand stack overflow");
        rc = kIfCondRet_Error;
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
static IFCONDRET ifcond_eval(PIFCOND pThis)
{
    IFCONDRET  rc;
    PCIFCONDOP pOp;

    /*
     * The main loop.
     */
    for (;;)
    {
        /*
         * Eat unary operators until we hit an operand.
         */
        do  rc = ifcond_get_unary_or_operand(pThis);
        while (rc == kIfCondRet_Operator);
        if (rc < kIfCondRet_Error)
            break;

        /*
         * Look for a binary operator, right parenthesis or end of expression.
         */
        rc = ifcond_get_binary_or_eoe_or_rparen(pThis);
        if (rc < kIfCondRet_Error)
            break;
        ifcond_unget_op(pThis);

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
            rc = pOp->pfn(pThis);
            if (rc < kIfCondRet_Error)
                break;
        }
        if (rc < kIfCondRet_Error)
            break;

        /*
         * Get the next binary operator or end of expression.
         * There should be no right parenthesis here.
         */
        rc = ifcond_get_binary_or_eoe_or_rparen(pThis);
        if (rc < kIfCondRet_Error)
            break;
        pOp = pThis->apOps[pThis->iOp];
        if (!pOp->iPrecedence)
            break;  /* end of expression */
        if (!pOp->cArgs)
        {
            ifcond_error(pThis, "Unexpected \"%s\"", pOp->szOp);
            rc = kIfCondRet_Error;
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
static void ifcond_destroy(PIFCOND pThis)
{
    while (pThis->iVar >= 0)
    {
        ifcond_var_delete(pThis->aVars);
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
 *                      This must stick around until ifcond_destroy.
 */
static PIFCOND ifcond_create(char const *pszExpr)
{
    PIFCOND pThis = (PIFCOND)xmalloc(sizeof(*pThis));
    pThis->pszExpr = pszExpr;
    pThis->psz = pszExpr;
    pThis->pFileLoc = NULL;
    pThis->pPending = NULL;
    pThis->iVar = -1;
    pThis->iOp = -1;

    ifcond_map_init();
    return pThis;
}


/**
 * Evaluates the given if expression.
 *
 * @returns -1, 0 or 1.
 * @retval  -1 if the expression is invalid.
 * @retval  0 if the expression is true
 * @retval  1 if the expression is false.
 *
 * @param   line    The expression. Can modify this as we like.
 * @param   flocp   The file location, used for errors.
 */
int ifcond(char *line, const struct floc *flocp)
{
    /*
     * Instantiate the expression evaluator and let
     * it have a go at it.
     */
    int rc = -1;
    PIFCOND pIfCond = ifcond_create(line);
    pIfCond->pFileLoc = flocp;
    if (ifcond_eval(pIfCond) >= kIfCondRet_Ok)
    {
        /*
         * Convert the result (on top of the stack) to boolean and
         * set our return value accordingly.
         */
        if (ifcond_var_make_bool(&pIfCond->aVars[0]))
            rc = 0;
        else
            rc = 1;
    }
    ifcond_destroy(pIfCond);

    return rc;
}


#endif /* CONFIG_WITH_IF_CONDITIONALS */

