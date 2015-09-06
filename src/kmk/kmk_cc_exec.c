#ifdef CONFIG_WITH_COMPILER
/* $Id: kmk_cc_exec.c 2788 2015-09-06 15:43:10Z knut.osmundsen@oracle.com $ */
/** @file
 * kmk_cc - Make "Compiler".
 */

/*
 * Copyright (c) 2015 knut st. osmundsen <bird-kBuild-spamx@anduin.net>
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include "make.h"

#include "dep.h"
#include "variable.h"
#include "rule.h"
#include "debug.h"
#include "hash.h"
#include <ctype.h>
#ifdef HAVE_STDINT_H
# include <stdint.h>
#endif
#include <stdarg.h>
#include <assert.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** @def KMK_CC_WITH_STATS
 * Enables the collection of extra statistics. */
#ifndef KMK_CC_WITH_STATS
# ifdef CONFIG_WITH_MAKE_STATS
#  define KMK_CC_WITH_STATS
# endif
#endif

/** @def KMK_CC_STRICT
 * Indicates whether assertions and other checks are enabled. */
#ifndef KMK_CC_STRICT
# ifndef NDEBUG
#  define KMK_CC_STRICT
# endif
#endif

#ifdef KMK_CC_STRICT
# ifdef _MSC_VER
#  define KMK_CC_ASSERT(a_TrueExpr)         do { if (!(a_TrueExpr)) __debugbreak(); } while (0)
# else
#  define KMK_CC_ASSERT(a_TrueExpr)         assert(a_TrueExpr)
# endif
#else
# define KMK_CC_ASSERT(a_TrueExpr)          do {} while (0)
#endif
#define KMK_CC_ASSERT_ALIGNED(a_uValue, a_uAlignment) \
    KMK_CC_ASSERT( ((a_uValue) & ((a_uAlignment) - 1)) == 0 )


/** @def KMK_CC_OFFSETOF
 * Offsetof for simple stuff.  */
#if defined(__GNUC__)
# define KMK_CC_OFFSETOF(a_Struct, a_Member)        __builtin_offsetof(a_Struct, a_Member)
#else
# define KMK_CC_OFFSETOF(a_Struct, a_Member)        ( (uintptr_t)&( ((a_Struct *)(void *)0)->a_Member) )
#endif

/** def KMK_CC_SIZEOF_MEMBER   */
#define KMK_CC_SIZEOF_MEMBER(a_Struct, a_Member)    ( sizeof( ((a_Struct *)(void *)0x1000)->a_Member) )

/** @def KMK_CC_SIZEOF_VAR_STRUCT
 * Size of a struct with a variable sized array as the final member. */
#define KMK_CC_SIZEOF_VAR_STRUCT(a_Struct, a_FinalArrayMember, a_cArray) \
    ( KMK_CC_OFFSETOF(a_Struct, a_FinalArrayMember) + KMK_CC_SIZEOF_MEMBER(a_Struct, a_FinalArrayMember) * (a_cArray) )



/** @def KMK_CC_STATIC_ASSERT_EX
 * Compile time assertion with text.
 */
#ifdef _MSC_VER_
# if _MSC_VER >= 1600
#  define KMK_CC_STATIC_ASSERT_EX(a_Expr, a_szExpl) static_assert(a_Expr, a_szExpl)
# else
#  define KMK_CC_STATIC_ASSERT_EX(a_Expr, a_szExpl) typedef int RTASSERTVAR[(a_Expr) ? 1 : 0]
# endif
#elif defined(__GNUC__) && defined(__GXX_EXPERIMENTAL_CXX0X__)
# define KMK_CC_STATIC_ASSERT_EX(a_Expr, a_szExpl)     static_assert(a_Expr, a_szExpl)
#elif !defined(__GNUC__) && !defined(__IBMC__) && !defined(__IBMCPP__)
# define KMK_CC_STATIC_ASSERT_EX(a_Expr, a_szExpl)  typedef int KMK_CC_STATIC_ASSERT_EX_TYPE[(a_Expr) ? 1 : 0]
#else
# define KMK_CC_STATIC_ASSERT_EX(a_Expr, a_szExpl)  extern int KMK_CC_STATIC_ASSERT_EX_VAR[(aExpr) ? 1 : 0]
extern int KMK_CC_STATIC_ASSERT_EX_VAR[1];
#endif
/** @def KMK_CC_STATIC_ASSERT
 * Compile time assertion, simple variant.
 */
#define KMK_CC_STATIC_ASSERT(a_Expr)                KMK_CC_STATIC_ASSERT_EX(a_Expr, #a_Expr)


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Block of expand instructions.
 *
 * To avoid wasting space on "next" pointers, as well as a lot of time walking
 * these chains when destroying programs, we work with blocks of instructions.
 */
typedef struct kmk_cc_block
{
    /** The pointer to the next block (LIFO). */
    struct kmk_cc_block        *pNext;
    /** The size of this block. */
    uint32_t                    cbBlock;
    /** The offset of the next free byte in the block.  When set to cbBlock the
     *  block is 100% full. */
    uint32_t                    offNext;
} KMKCCBLOCK;
typedef KMKCCBLOCK *PKMKCCBLOCK;


/** @name String Expansion
 * @{*/

/**
 * String expansion statistics.
 */
typedef struct KMKCCEXPSTATS
{
    /** Recent average size. */
    uint32_t                    cchAvg;
} KMKCCEXPSTATS;
typedef KMKCCEXPSTATS *PKMKCCEXPSTATS;

/**
 * Expansion instructions.
 */
typedef enum KMKCCEXPINSTR
{
    /** Copy a plain string. */
    kKmkCcExpInstr_CopyString = 0,
    /** Insert an expanded variable value, which name we already know.  */
    kKmkCcExpInstr_PlainVariable,
    /** Insert an expanded variable value, the name is dynamic (sub prog). */
    kKmkCcExpInstr_DynamicVariable,
    /** Insert an expanded variable value, which name we already know, doing
     * search an replace on a string. */
    kKmkCcExpInstr_SearchAndReplacePlainVariable,
    /** Insert the output of function that requires no argument expansion. */
    kKmkCcExpInstr_PlainFunction,
    /** Insert the output of function that requires dynamic expansion of one ore
     * more arguments.  (Dynamic is perhaps not such a great name, but whatever.) */
    kKmkCcExpInstr_DynamicFunction,
    /** Jump to a new instruction block. */
    kKmkCcExpInstr_Jump,
    /** We're done, return.  Has no specific structure. */
    kKmkCcExpInstr_Return,
    /** The end of valid instructions (exclusive). */
    kKmkCcExpInstr_End
} KMKCCEXPINSTR;

/** Instruction core. */
typedef struct kmk_cc_exp_core
{
    /** The instruction opcode number (KMKCCEXPINSTR). */
    KMKCCEXPINSTR           enmOpCode;
} KMKCCEXPCORE;
typedef KMKCCEXPCORE *PKMKCCEXPCORE;

/**
 * String expansion subprogram.
 */
#pragma pack(1) /* save some precious bytes */
typedef struct kmk_cc_exp_subprog
{
    /** Pointer to the first instruction. */
    PKMKCCEXPCORE           pFirstInstr;
    /** Statistics. */
    KMKCCEXPSTATS           Stats;
} KMKCCEXPSUBPROG;
#pragma pack()
typedef KMKCCEXPSUBPROG *PKMKCCEXPSUBPROG;
KMK_CC_STATIC_ASSERT(sizeof(KMKCCEXPSUBPROG) == 12 || sizeof(void *) != 8);


/**
 * String expansion subprogram or plain string.
 */
#pragma pack(1) /* save some precious bytes */
typedef struct kmk_cc_exp_subprog_or_string
{
    /** Either a plain string pointer or a subprogram.   */
    union
    {
        /** Subprogram for expanding this argument. */
        KMKCCEXPSUBPROG     Subprog;
        /** Pointer to the plain string. */
        struct
        {
            /** Pointer to the string. */
            const char     *psz;
            /** String length. */
            uint32_t        cch;
        } Plain;
    } u;
    /** Set if subprogram (u.Subprog), clear if plain string (u.Plain). */
    uint8_t                 fSubprog;
    /** Set if the plain string is kept in the variable_strcache.
     * @remarks Here rather than in u.Plain to make use of alignment padding. */
    uint8_t                 fPlainIsInVarStrCache;
    /** Context/user specific. */
    uint8_t                 bUser;
    /** Context/user specific #2. */
    uint8_t                 bUser2;
} KMKCCEXPSUBPROGORPLAIN;
#pragma pack()
typedef KMKCCEXPSUBPROGORPLAIN *PKMKCCEXPSUBPROGORPLAIN;
KMK_CC_STATIC_ASSERT(  sizeof(void *) == 8
                     ? sizeof(KMKCCEXPSUBPROGORPLAIN) == 16
                     : sizeof(void *) == 4
                     ? sizeof(KMKCCEXPSUBPROGORPLAIN) == 12
                     : 1);

/**
 * kKmkCcExpInstr_CopyString instruction format.
 */
typedef struct kmk_cc_exp_copy_string
{
    /** The core instruction. */
    KMKCCEXPCORE            Core;
    /** The number of bytes to copy. */
    uint32_t                cchCopy;
    /** Pointer to the source string (not terminated at cchCopy). */
    const char             *pachSrc;
} KMKCCEXPCOPYSTRING;
typedef KMKCCEXPCOPYSTRING *PKMKCCEXPCOPYSTRING;

/**
 * kKmkCcExpInstr_PlainVariable instruction format.
 */
typedef struct kmk_cc_exp_plain_variable
{
    /** The core instruction. */
    KMKCCEXPCORE            Core;
    /** The name of the variable (points into variable_strcache). */
    const char             *pszName;
} KMKCCEXPPLAINVAR;
typedef KMKCCEXPPLAINVAR *PKMKCCEXPPLAINVAR;

/**
 * kKmkCcExpInstr_DynamicVariable instruction format.
 */
typedef struct kmk_cc_exp_dynamic_variable
{
    /** The core instruction. */
    KMKCCEXPCORE            Core;
    /** The subprogram that will give us the variable name. */
    KMKCCEXPSUBPROG         Subprog;
    /** Where to continue after this instruction.  (This is necessary since the
     * instructions of the subprogram are emitted after this instruction.) */
    PKMKCCEXPCORE           pNext;
} KMKCCEXPDYNVAR;
typedef KMKCCEXPDYNVAR *PKMKCCEXPDYNVAR;

/**
 * kKmkCcExpInstr_SearchAndReplacePlainVariable instruction format.
 */
typedef struct kmk_cc_exp_sr_plain_variable
{
    /** The core instruction. */
    KMKCCEXPCORE            Core;
    /** Where to continue after this instruction.  (This is necessary since the
     * instruction contains string data of variable size.) */
    PKMKCCEXPCORE           pNext;
    /** The name of the variable (points into variable_strcache). */
    const char             *pszName;
    /** Search pattern.  */
    const char             *pszSearchPattern;
    /** Replacement pattern. */
    const char             *pszReplacePattern;
    /** Offset into pszSearchPattern of the significant '%' char. */
    uint32_t                offPctSearchPattern;
    /** Offset into pszReplacePattern of the significant '%' char. */
    uint32_t                offPctReplacePattern;
} KMKCCEXPSRPLAINVAR;
typedef KMKCCEXPSRPLAINVAR *PKMKCCEXPSRPLAINVAR;

/**
 * Instruction format parts common to both kKmkCcExpInstr_PlainFunction and
 * kKmkCcExpInstr_DynamicFunction.
 */
typedef struct kmk_cc_exp_function_core
{
    /** The core instruction. */
    KMKCCEXPCORE            Core;
    /** Number of arguments. */
    uint32_t                cArgs; /**< @todo uint16_t to save 7 bytes of unecessary alignment padding on 64-bit systems, or merge fDirty into this member. */
    /** Set if the function could be modifying the input arguments. */
    uint8_t                 fDirty;
    /** Where to continue after this instruction.  (This is necessary since the
     * instructions are of variable size and may be followed by string data.) */
    PKMKCCEXPCORE           pNext;
    /**
     * Pointer to the function table entry.
     *
     * @returns New variable buffer position.
     * @param   pchDst      Current variable buffer position.
     * @param   papszArgs   Pointer to a NULL terminated array of argument strings.
     * @param   pszFuncName The name of the function being called.
     */
    char *                (*pfnFunction)(char *pchDst, char **papszArgs, const char *pszFuncName);
    /** Pointer to the function name in the variable string cache. */
    const char             *pszFuncName;
} KMKCCEXPFUNCCORE;
typedef KMKCCEXPFUNCCORE *PKMKCCEXPFUNCCORE;

/**
 * Instruction format for kKmkCcExpInstr_PlainFunction.
 */
typedef struct kmk_cc_exp_plain_function
{
    /** The bits comment to both plain and dynamic functions. */
    KMKCCEXPFUNCCORE        FnCore;
    /** Variable sized argument list (cArgs + 1 in length, last entry is NULL).
     * The string pointers are to memory following this instruction, to memory in
     * the next block or to memory in the variable / makefile we're working on
     * (if zero terminated appropriately). */
    const char             *apszArgs[1];
} KMKCCEXPPLAINFUNC;
typedef KMKCCEXPPLAINFUNC *PKMKCCEXPPLAINFUNC;
/** Calculates the size of an KMKCCEXPPLAINFUNC structure with the apszArgs
 * member holding a_cArgs entries plus a NULL terminator. */
#define KMKCCEXPPLAINFUNC_SIZE(a_cArgs) KMK_CC_SIZEOF_VAR_STRUCT(KMKCCEXPDYNFUNC, aArgs, (a_cArgs) + 1)

/**
 * Instruction format for kKmkCcExpInstr_DynamicFunction.
 */
typedef struct kmk_cc_exp_dyn_function
{
    /** The bits comment to both plain and dynamic functions. */
    KMKCCEXPFUNCCORE        FnCore;
    /** Variable sized argument list (FnCore.cArgs in length).
     * The subprograms / strings are allocated after this array (or in the next
     * block). */
    KMKCCEXPSUBPROGORPLAIN  aArgs[1];
} KMKCCEXPDYNFUNC;
typedef KMKCCEXPDYNFUNC *PKMKCCEXPDYNFUNC;
/** Calculates the size of an KMKCCEXPDYNFUNC structure with the apszArgs
 * member holding a_cArgs entries (no zero terminator). */
#define KMKCCEXPDYNFUNC_SIZE(a_cArgs)  KMK_CC_SIZEOF_VAR_STRUCT(KMKCCEXPDYNFUNC, aArgs, a_cArgs)

/**
 * Instruction format for kKmkCcExpInstr_Jump.
 */
typedef struct kmk_cc_exp_jump
{
    /** The core instruction. */
    KMKCCEXPCORE            Core;
    /** Where to jump to (new instruction block, typically). */
    PKMKCCEXPCORE           pNext;
} KMKCCEXPJUMP;
typedef KMKCCEXPJUMP *PKMKCCEXPJUMP;

/**
 * String expansion program.
 */
typedef struct kmk_cc_expandprog
{
    /** Pointer to the first instruction for this program. */
    PKMKCCEXPCORE           pFirstInstr;
    /** List of blocks for this program (LIFO). */
    PKMKCCBLOCK             pBlockTail;
    /** Statistics. */
    KMKCCEXPSTATS           Stats;
#ifdef KMK_CC_STRICT
    /** The hash of the input string.  Used to check that we get all the change
     * notifications we require. */
    uint32_t                uInputHash;
#endif
    /** Reference count. */
    uint32_t volatile       cRefs;
} KMKCCEXPPROG;
/** Pointer to a string expansion program. */
typedef KMKCCEXPPROG *PKMKCCEXPPROG;

/** @} */


/** @name Makefile Evaluation
 * @{  */

/** Pointer to a makefile evaluation program. */
typedef struct kmk_cc_evalprog *PKMKCCEVALPROG;

/**
 * Makefile evaluation instructions.
 */
typedef enum KMKCCEVALINSTR
{
    /** Jump instruction - KMKCCEVALJUMP. */
    kKmkCcEvalInstr_jump = 0,

    /** [local|override|export] variable  = value - KMKCCEVALASSIGN.
     * @note Can be used for target-specific variables. */
    kKmkCcEvalInstr_assign_recursive,
    /** [local|override|export] variable := value - KMKCCEVALASSIGN.
     * @note Can be used for target-specific variables. */
    kKmkCcEvalInstr_assign_simple,
    /** [local|override|export] variable += value - KMKCCEVALASSIGN.
     * @note Can be used for target-specific variables. */
    kKmkCcEvalInstr_assign_append,
    /** [local|override|export] variable -= value - KMKCCEVALASSIGN.
     * @note Can be used for target-specific variables. */
    kKmkCcEvalInstr_assign_prepend,
    /** [local|override|export] variable ?= value - KMKCCEVALASSIGN.
     * @note Can be used for target-specific variables. */
    kKmkCcEvalInstr_assign_if_new,
    /** [local|override|export] define variable ... endef - KMKCCEVALASSIGNDEF. */
    kKmkCcEvalInstr_assign_define,

    /** export variable1 [variable2...] - KMKCCEVALEXPORT. */
    kKmkCcEvalInstr_export,
    /** unexport variable1 [variable2...] - KMKCCEVALEXPORT. */
    kKmkCcEvalInstr_unexport,
    /** export - KMKCCEVALCORE. */
    kKmkCcEvalInstr_export_all,
    /** unexport - KMKCCEVALCORE. */
    kKmkCcEvalInstr_unexport_all,

    /** [else] ifdef variable - KMKCCEVALIFDEFPLAIN. */
    kKmkCcEvalInstr_ifdef_plain,
    /** [else] ifndef variable - KMKCCEVALIFDEFPLAIN. */
    kKmkCcEvalInstr_ifndef_plain,
    /** [else] ifdef variable - KMKCCEVALIFDEFDYNAMIC. */
    kKmkCcEvalInstr_ifdef_dynamic,
    /** [else] ifndef variable - KMKCCEVALIFDEFDYNAMIC. */
    kKmkCcEvalInstr_ifndef_dynamic,
    /** [else] ifeq (a,b) - KMKCCEVALIFEQ. */
    kKmkCcEvalInstr_ifeq,
    /** [else] ifeq (a,b) - KMKCCEVALIFEQ. */
    kKmkCcEvalInstr_ifneq,
    /** [else] if1of (set-a,set-b) - KMKCCEVALIF1OF. */
    kKmkCcEvalInstr_if1of,
    /** [else] ifn1of (set-a,set-b) - KMKCCEVALIF1OF. */
    kKmkCcEvalInstr_ifn1of,
    /** [else] if expr - KMKCCEVALIFEXPR. */
    kKmkCcEvalInstr_if,

    /** include file1 [file2...] - KMKCCEVALINCLUDE. */
    kKmkCcEvalInstr_include,
    /** [sinclude|-include] file1 [file2...]  - KMKCCEVALINCLUDE. */
    kKmkCcEvalInstr_include_silent,
    /** includedep file1 [file2...] - KMKCCEVALINCLUDE. */
    kKmkCcEvalInstr_includedep,
    /** includedep-queue file1 [file2...] - KMKCCEVALINCLUDE. */
    kKmkCcEvalInstr_includedep_queue,
    /** includedep-flush file1 [file2...] - KMKCCEVALINCLUDE. */
    kKmkCcEvalInstr_includedep_flush,

    /** Recipe without commands (defines dependencies) - KMKCCEVALRECIPE. */
    kKmkCcEvalInstr_recipe_no_commands,
    /** Recipe with commands (defines dependencies) - KMKCCEVALRECIPE. */
    kKmkCcEvalInstr_recipe_start_normal,
    /** Recipe with commands (defines dependencies) - KMKCCEVALRECIPE. */
    kKmkCcEvalInstr_recipe_start_double_colon,
    /** Recipe with commands (defines dependencies) - KMKCCEVALRECIPE. */
    kKmkCcEvalInstr_recipe_start_pattern,
    /** Adds more commands to the current recipe - KMKCCEVALRECIPECOMMANDS. */
    kKmkCcEvalInstr_recipe_commands,
    /** Adds more commands to the current recipe - KMKCCEVALRECIPECOMMANDS. */
    kKmkCcEvalInstr_recipe_vari,
    /** Special instruction for indicating the end of the recipe commands - KMKCCEVALCORE. */
    kKmkCcEvalInstr_recipe_end,
    /** Cancel previously defined pattern rule - KMKCCEVALRECIPE.  */
    kKmkCcEvalInstr_recipe_cancel_pattern,

    /** vpath pattern directories - KMKCCEVALVPATH. */
    kKmkCcEvalInstr_vpath,
    /** vpath pattern directories - KMKCCEVALVPATH. */
    kKmkCcEvalInstr_vpath_clear_pattern,
    /** vpath - KMKCCEVALCORE. */
    kKmkCcEvalInstr_vpath_clear_all,

    /** The end of valid instructions (exclusive). */
    kKmkCcEvalInstr_End
} KMKCCEVALINSTR;

/**
 * Instruction core common to all instructions.
 */
typedef struct kmk_cc_eval_core
{
    /** The instruction opcode number (KMKCCEVALINSTR). */
    KMKCCEVALINSTR          enmOpCode;
    /** The line number in the source this statement is associated with. */
    unsigned                iLine;
} KMKCCEVALCORE;
/** Pointer to an instruction core structure. */
typedef KMKCCEVALCORE *PKMKCCEVALCORE;

/**
 * Instruction format for kKmkCcEvalInstr_jump.
 */
typedef struct kmk_cc_eval_jump
{
    /** The core instruction. */
    KMKCCEVALCORE           Core;
    /** Where to jump to (new instruction block or endif, typically). */
    PKMKCCEVALCORE          pNext;
} KMKCCEVALJUMP;
typedef KMKCCEVALJUMP *PKMKCCEVALJUMP;

/**
 * Instruction format for kKmkCcEvalInstr_assign_recursive,
 * kKmkCcEvalInstr_assign_simple, kKmkCcEvalInstr_assign_append,
 * kKmkCcEvalInstr_assign_prepend and kKmkCcEvalInstr_assign_if_new.
 */
typedef struct kmk_cc_eval_assign
{
    /** The core instruction. */
    KMKCCEVALCORE           Core;
    /** Whether the 'export' directive was used. */
    uint8_t                 fExport;
    /** Whether the 'override' directive was used. */
    uint8_t                 fOverride;
    /** Whether the 'local' directive was used. */
    uint8_t                 fLocal;
    /** The variable name.
     * @remarks Plain text names are in variable_strcache. */
    KMKCCEXPSUBPROGORPLAIN  Variable;
    /** The value or value expression. */
    KMKCCEXPSUBPROGORPLAIN  Value;
    /** Pointer to the next instruction. */
    PKMKCCEVALCORE          pNext;
} KMKCCEVALASSIGN;
typedef KMKCCEVALASSIGN *PKMKCCEVALASSIGN;

/**
 * Instruction format for kKmkCcEvalInstr_assign_define.
 */
typedef struct kmk_cc_eval_assign_define
{
    /** The assignment core structure. */
    KMKCCEVALASSIGN         AssignCore;
    /** Makefile evaluation program compiled from the define.
     * NULL if it does not compile.
     * @todo Let's see if this is actually doable... */
    PKMKCCEVALPROG          pEvalProg;
} KMKCCEVALASSIGNDEF;
typedef KMKCCEVALASSIGNDEF *PKMKCCEVALASSIGNDEF;

/**
 * Instruction format for kKmkCcEvalInstr_export and kKmkCcEvalInstr_unexport.
 */
typedef struct kmk_cc_eval_export
{
    /** The core instruction. */
    KMKCCEVALCORE           Core;
    /** The number of variables named in aVars. */
    uint32_t                cVars;
    /** Pointer to the next instruction. */
    PKMKCCEVALCORE          pNext;
    /** The variable names.
     * Expressions will be expanded and split on space.
     * @remarks Plain text names are in variable_strcache. */
    KMKCCEXPSUBPROGORPLAIN  aVars[1];
} KMKCCEVALEXPORT;
typedef KMKCCEVALEXPORT *PKMKCCEVALEXPORT;
/** Calculates the size of an KMKCCEVALEXPORT structure for @a a_cVars. */
#define KMKCCEVALEXPORT_SIZE(a_cVars) KMK_CC_SIZEOF_VAR_STRUCT(KMKCCEVALVPATH, aVars, a_cVars)

/**
 * Core structure for all conditionals (kKmkCcEvalInstr_if*).
 */
typedef struct kmk_cc_eval_if_core
{
    /** The core instruction. */
    KMKCCEVALCORE           Core;
    /** Condition true: Pointer to the next instruction. */
    PKMKCCEVALCORE          pNextTrue;
    /** Condition false: Pointer to the next instruction (i.e. 'else if*'
     * or whatever follows 'else' / 'endif'. */
    PKMKCCEVALCORE          pNextFalse;
    /** Pointer to the previous conditional for 'else if*' directives.
     * This is to assist the compilation process. */
    PKMKCCEVALCORE          pPrevCond;
} KMKCCEVALIFCORE;
typedef KMKCCEVALIFCORE *PKMKCCEVALIFCORE;

/**
 * Instruction format for kKmkCcEvalInstr_ifdef_plain and
 * kKmkCcEvalInstr_ifndef_plain.
 * The variable name is known at compilation time.
 */
typedef struct kmk_cc_eval_ifdef_plain
{
    /** The 'if' core structure. */
    KMKCCEVALIFCORE         IfCore;
    /** The name of the variable (points into variable_strcache). */
    const char             *pszName;
} KMKCCEVALIFDEFPLAIN;
typedef KMKCCEVALIFDEFPLAIN *PKMKCCEVALIFDEFPLAIN;

/**
 * Instruction format for kKmkCcEvalInstr_ifdef_dynamic and
 * kKmkCcEvalInstr_ifndef_dynamic.
 * The variable name is dynamically expanded at run time.
 */
typedef struct kmk_cc_eval_ifdef_dynamic
{
    /** The 'if' core structure. */
    KMKCCEVALIFCORE         IfCore;
    /** The subprogram that will give us the variable name. */
    KMKCCEXPSUBPROG         NameSubProg;
} KMKCCEVALIFDEFDYNAMIC;
KMK_CC_STATIC_ASSERT(sizeof(KMKCCEVALIFDEFDYNAMIC) == 48 || sizeof(void *) != 8);
typedef KMKCCEVALIFDEFDYNAMIC *PKMKCCEVALIFDEFDYNAMIC;

/**
 * Instruction format for kKmkCcEvalInstr_ifeq and kKmkCcEvalInstr_ifneq.
 */
typedef struct kmk_cc_eval_ifeq
{
    /** The 'if' core structure. */
    KMKCCEVALIFCORE         IfCore;
    /** The left hand side string expression (dynamic or plain). */
    KMKCCEXPSUBPROGORPLAIN  Left;
    /** The rigth hand side string expression (dynamic or plain). */
    KMKCCEXPSUBPROGORPLAIN  Right;
} KMKCCEVALIFEQ;
typedef KMKCCEVALIFEQ *PKMKCCEVALIFEQ;

/**
 * Instruction format for kKmkCcEvalInstr_if1of and kKmkCcEvalInstr_ifn1of.
 *
 * @todo This can be optimized further by pre-hashing plain text items.  One of
 *       the sides are usually plain text.
 */
typedef struct kmk_cc_eval_if1of
{
    /** The 'if' core structure. */
    KMKCCEVALIFCORE         IfCore;
    /** The left hand side string expression (dynamic or plain). */
    KMKCCEXPSUBPROGORPLAIN  Left;
    /** The rigth hand side string expression (dynamic or plain). */
    KMKCCEXPSUBPROGORPLAIN  Right;
} KMKCCEVALIF1OF;
typedef KMKCCEVALIF1OF *PKMKCCEVALIF1OF;

/**
 * Instruction format for kKmkCcEvalInstr_if.
 *
 * @todo Parse and compile the expression.  At least strip whitespace in it.
 */
typedef struct kmk_cc_eval_if_expr
{
    /** The 'if' core structure. */
    KMKCCEVALIFCORE         IfCore;
    /** The expression string length. */
    uint16_t                cchExpr;
    /** The expression string. */
    char                    szExpr[1];
} KMKCCEVALIFEXPR;
typedef KMKCCEVALIFEXPR *PKMKCCEVALIFEXPR;
/** Calculates the size of an KMKCCEVALIFEXPR structure for @a a_cchExpr long
 * expression string (terminator is automatically added).  */
#define KMKCCEVALIFEXPR_SIZE(a_cchExpr) KMK_CC_SIZEOF_VAR_STRUCT(KMKCCEVALIFEXPR, szExpr, (a_cchExpr) + 1)

/**
 * Instruction format for kKmkCcEvalInstr_include,
 * kKmkCcEvalInstr_include_silent, kKmkCcEvalInstr_includedep,
 * kKmkCcEvalInstr_includedep_queue, kKmkCcEvalInstr_includedep_flush.
 */
typedef struct kmk_cc_eval_include
{
    /** The core instruction. */
    KMKCCEVALCORE           Core;
    /** The number of files. */
    uint32_t                cFiles;
    /** Pointer to the next instruction (subprogs and strings after this one). */
    PKMKCCEVALCORE          pNext;
    /** The files to be included.
     * Expressions will be expanded and split on space.
     * @todo Plain text file name could be replaced by file string cache entries. */
    KMKCCEXPSUBPROGORPLAIN  aFiles[1];
} KMKCCEVALINCLUDE;
typedef KMKCCEVALINCLUDE *PKMKCCEVALINCLUDE;
/** Calculates the size of an KMKCCEVALINCLUDE structure for @a a_cFiles files. */
#define KMKCCEVALINCLUDE_SIZE(a_cFiles) KMK_CC_SIZEOF_VAR_STRUCT(KMKCCEVALINCLUDE, aFiles, a_cFiles)

/**
 * Instruction format for kKmkCcEvalInstr_recipe_no_commands,
 * kKmkCcEvalInstr_recipe_start_normal,
 * kKmkCcEvalInstr_recipe_start_double_colon, kKmkCcEvalInstr_includedep_queue,
 * kKmkCcEvalInstr_recipe_start_pattern.
 */
typedef struct kmk_cc_eval_recipe
{
    /** The core instruction. */
    KMKCCEVALCORE           Core;
    /** The total number of files and dependencies in aFilesAndDeps. */
    uint16_t                cFilesAndDeps;

    /** Number of targets (from index 0).
     * This is always 1 if this is an explicit multitarget or pattern recipe,
     * indicating the main target. */
    uint16_t                cTargets;
    /** Explicit multitarget & patterns: First always made target. */
    uint16_t                iFirstAlwaysMadeTargets;
    /** Explicit multitarget & patterns: Number of always targets. */
    uint16_t                cAlwaysMadeTargets;
    /** Explicit multitarget: First maybe made target. */
    uint16_t                iFirstMaybeTarget;
    /** Explicit multitarget: Number of maybe made targets. */
    uint16_t                cMaybeTargets;

    /** First dependency. */
    uint16_t                iFirstDep;
    /** Number of ordinary dependnecies. */
    uint16_t                cDeps;
    /** First order only dependency. */
    uint16_t                iFirstOrderOnlyDep;
    /** Number of ordinary dependnecies. */
    uint16_t                cOrderOnlyDeps;

    /** Pointer to the next instruction (subprogs and strings after this one). */
    PKMKCCEVALCORE          pNext;
    /** The .MUST_MAKE variable value, if present.
     * If not present, this is a zero length plain string. */
    KMKCCEXPSUBPROGORPLAIN  MustMake;
    /** The target files and dependencies.
     * This is sorted into several sections, as defined by the above indexes and
     * counts.  Expressions will be expanded and split on space.
     *
     * The KMKCCEXPSUBPROGORPLAIN::bUser member is used to indicate secondary
     * expansion for a plain text entry.
     *
     * @todo Plain text file name could be replaced by file string cache entries. */
    KMKCCEXPSUBPROGORPLAIN  aFilesAndDeps[1];
} KMKCCEVALRECIPE;
typedef KMKCCEVALRECIPE *PKMKCCEVALRECIPE;
/** Calculates the size of an KMKCCEVALRECIPE structure for @a a_cFiles
 *  files. */
#define KMKCCEVALRECIPE_SIZE(a_cFilesAndDeps) KMK_CC_SIZEOF_VAR_STRUCT(KMKCCEVALRECIPE, aFilesAndDeps, a_cFilesAndDeps)

/**
 * Instruction format for kKmkCcEvalInstr_recipe_commands.
 */
typedef struct kmk_cc_eval_recipe_commands
{
    /** The core instruction. */
    KMKCCEVALCORE           Core;
    /** The number of search directories. */
    uint32_t                cCommands;
    /** Pointer to the next instruction (subprogs and strings after this one). */
    PKMKCCEVALCORE          pNext;
    /** Commands to add to the current recipe.
     * Expressions will be expanded and split on space. */
    KMKCCEXPSUBPROGORPLAIN  aCommands[1];
} KMKCCEVALRECIPECOMMANDS;
typedef KMKCCEVALRECIPECOMMANDS *PKMKCCEVALRECIPECOMMANDS;
/** Calculates the size of an KMKCCEVALRECIPECOMMANDS structure for
 * @a a_cCommands commands. */
#define KMKCCEVALRECIPECOMMANDS_SIZE(a_cCommands) KMK_CC_SIZEOF_VAR_STRUCT(KMKCCEVALRECIPECOMMANDS, aCommands, a_cCommands)

/**
 * Instruction format for kKmkCcEvalInstr_vpath and
 * kKmkCcEvalInstr_vpath_clear_pattern.
 */
typedef struct kmk_cc_eval_vpath
{
    /** The core instruction. */
    KMKCCEVALCORE           Core;
    /** The number of search directories.
     * This will be zero for kKmkCcEvalInstr_vpath_clear_pattern. */
    uint32_t                cDirs;
    /** Pointer to the next instruction (subprogs and strings after this one). */
    PKMKCCEVALCORE          pNext;
    /** The pattern. */
    KMKCCEXPSUBPROGORPLAIN  Pattern;
    /** The directory. Expressions will be expanded and split on space. */
    KMKCCEXPSUBPROGORPLAIN  aDirs[1];
} KMKCCEVALVPATH;
typedef KMKCCEVALVPATH *PKMKCCEVALVPATH;
/** Calculates the size of an KMKCCEVALVPATH structure for @a a_cFiles files. */
#define KMKCCEVALVPATH_SIZE(a_cFiles) KMK_CC_SIZEOF_VAR_STRUCT(KMKCCEVALVPATH, aDirs, a_cDirs)


/**
 * Makefile evaluation program.
 */
typedef struct kmk_cc_evalprog
{
    /** Pointer to the first instruction for this program. */
    PKMKCCEVALCORE          pFirstInstr;
    /** List of blocks for this program (LIFO). */
    PKMKCCBLOCK             pBlockTail;

} KMKCCEVALPROG;

/** @} */


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
static uint32_t g_cVarForExpandCompilations = 0;
static uint32_t g_cVarForExpandExecs = 0;
#ifdef KMK_CC_WITH_STATS
static uint32_t g_cBlockAllocated = 0;
static uint32_t g_cbAllocated = 0;
static uint32_t g_cBlocksAllocatedExpProgs = 0;
static uint32_t g_cbAllocatedExpProgs = 0;
static uint32_t g_cSingleBlockExpProgs = 0;
static uint32_t g_cTwoBlockExpProgs = 0;
static uint32_t g_cMultiBlockExpProgs = 0;
static uint32_t g_cbUnusedMemExpProgs = 0;
#endif


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static int kmk_cc_exp_compile_subprog(PKMKCCBLOCK *ppBlockTail, const char *pchStr, uint32_t cchStr, PKMKCCEXPSUBPROG pSubProg);
static char *kmk_exec_expand_subprog_to_tmp(PKMKCCEXPSUBPROG pSubProg, uint32_t *pcch);


/**
 * Initializes global variables for the 'compiler'.
 */
void kmk_cc_init(void)
{
}


/**
 * Prints stats (for kmk -p).
 */
void kmk_cc_print_stats(void)
{
    puts(_("\n# The kmk 'compiler' and kmk 'program executor':\n"));

    printf(_("# Variables compiled for string expansion: %6u\n"), g_cVarForExpandCompilations);
    printf(_("# Variables string expansion runs:         %6u\n"), g_cVarForExpandExecs);
    printf(_("# String expansion runs per compile:       %6u\n"), g_cVarForExpandExecs / g_cVarForExpandCompilations);
#ifdef KMK_CC_WITH_STATS
    printf(_("#          Single alloc block exp progs:   %6u (%u%%)\n"
             "#             Two alloc block exp progs:   %6u (%u%%)\n"
             "#   Three or more alloc block exp progs:   %6u (%u%%)\n"
             ),
           g_cSingleBlockExpProgs, (uint32_t)((uint64_t)g_cSingleBlockExpProgs * 100 / g_cVarForExpandCompilations),
           g_cTwoBlockExpProgs,    (uint32_t)((uint64_t)g_cTwoBlockExpProgs    * 100 / g_cVarForExpandCompilations),
           g_cMultiBlockExpProgs,  (uint32_t)((uint64_t)g_cMultiBlockExpProgs  * 100 / g_cVarForExpandCompilations));
    printf(_("#  Total amount of memory for exp progs: %8u bytes\n"
             "#                                    in:   %6u blocks\n"
             "#                        avg block size:   %6u bytes\n"
             "#                         unused memory: %8u bytes (%u%%)\n"
             "#           avg unused memory per block:   %6u bytes\n"
             "\n"),
           g_cbAllocatedExpProgs, g_cBlocksAllocatedExpProgs, g_cbAllocatedExpProgs / g_cBlocksAllocatedExpProgs,
           g_cbUnusedMemExpProgs, (uint32_t)((uint64_t)g_cbUnusedMemExpProgs * 100 / g_cbAllocatedExpProgs),
           g_cbUnusedMemExpProgs / g_cBlocksAllocatedExpProgs);

    printf(_("#   Total amount of block mem allocated: %8u bytes\n"), g_cbAllocated);
    printf(_("#       Total number of block allocated: %8u\n"), g_cBlockAllocated);
    printf(_("#                    Average block size: %8u byte\n"), g_cbAllocated / g_cBlockAllocated);
#endif

    puts("");
}


/*
 *
 * Various utility functions.
 * Various utility functions.
 * Various utility functions.
 *
 */

/**
 * Counts the number of dollar chars in the string.
 *
 * @returns Number of dollar chars.
 * @param   pchStr      The string to search (does not need to be zero
 *                      terminated).
 * @param   cchStr      The length of the string.
 */
static uint32_t kmk_cc_count_dollars(const char *pchStr, uint32_t cchStr)
{
    uint32_t cDollars = 0;
    const char *pch;
    while ((pch = memchr(pchStr, '$', cchStr)) != NULL)
    {
        cDollars++;
        cchStr -= pch - pchStr + 1;
        pchStr  = pch + 1;
    }
    return cDollars;
}

#ifdef KMK_CC_STRICT
/**
 * Used to check that function arguments are left alone.
 * @returns Updated hash.
 * @param   uHash       The current hash value.
 * @param   psz         The string to hash.
 */
static uint32_t kmk_cc_debug_string_hash(uint32_t uHash, const char *psz)
{
    unsigned char ch;
    while ((ch = *(unsigned char const *)psz++) != '\0')
        uHash = (uHash << 6) + (uHash << 16) - uHash + (unsigned char)ch;
    return uHash;
}

/**
 * Used to check that function arguments are left alone.
 * @returns Updated hash.
 * @param   uHash       The current hash value.
 * @param   pch         The string to hash, not terminated.
 * @param   cch         The number of chars to hash.
 */
static uint32_t kmk_cc_debug_string_hash_n(uint32_t uHash, const char *pch, uint32_t cch)
{
    while (cch-- > 0)
    {
        unsigned char ch = *(unsigned char const *)pch++;
        uHash = (uHash << 6) + (uHash << 16) - uHash + (unsigned char)ch;
    }
    return uHash;
}

#endif



/*
 *
 * The allocator.
 * The allocator.
 * The allocator.
 *
 */


/**
 * For the first allocation using the block allocator.
 *
 * @returns Pointer to the first allocation (@a cbFirst in size).
 * @param   ppBlockTail         Where to return the pointer to the first block.
 * @param   cbFirst             The size of the first allocation.
 * @param   cbHint              Hint about how much memory we might be needing.
 */
static void *kmk_cc_block_alloc_first(PKMKCCBLOCK *ppBlockTail, size_t cbFirst, size_t cbHint)
{
    uint32_t        cbBlock;
    PKMKCCBLOCK     pNewBlock;

    KMK_CC_ASSERT_ALIGNED(cbFirst, sizeof(void *));

    /*
     * Turn the hint into a block size.
     */
    if (cbHint <= 512)
    {
        if (cbHint <= 256)
            cbBlock = 128;
        else
            cbBlock = 256;
    }
    else if (cbHint < 2048)
        cbBlock = 1024;
    else if (cbHint < 3072)
        cbBlock = 2048;
    else
        cbBlock = 4096;

    /*
     * Allocate and initialize the first block.
     */
    pNewBlock = (PKMKCCBLOCK)xmalloc(cbBlock);
    pNewBlock->cbBlock = cbBlock;
    pNewBlock->offNext = sizeof(*pNewBlock) + cbFirst;
    pNewBlock->pNext   = NULL;
    *ppBlockTail = pNewBlock;

#ifdef KMK_CC_WITH_STATS
    g_cBlockAllocated++;
    g_cbAllocated += cbBlock;
#endif

    return pNewBlock + 1;
}


/**
 * Used for getting the address of the next instruction.
 *
 * @returns Pointer to the next allocation.
 * @param   pBlockTail          The allocator tail pointer.
 */
static void *kmk_cc_block_get_next_ptr(PKMKCCBLOCK pBlockTail)
{
    return (char *)pBlockTail + pBlockTail->offNext;
}


/**
 * Realigns the allocator after doing byte or string allocations.
 *
 * @param   ppBlockTail         Pointer to the allocator tail pointer.
 */
static void kmk_cc_block_realign(PKMKCCBLOCK *ppBlockTail)
{
    PKMKCCBLOCK pBlockTail = *ppBlockTail;
    if (pBlockTail->offNext & (sizeof(void *) - 1))
    {
        pBlockTail->offNext = (pBlockTail->offNext + sizeof(void *) - 1) & ~(sizeof(void *) - 1);
        KMK_CC_ASSERT(pBlockTail->cbBlock - pBlockTail->offNext >= sizeof(KMKCCEXPJUMP));
    }
}


/**
 * Grows the allocation with another block, byte allocator case.
 *
 * @returns Pointer to the byte allocation.
 * @param   ppBlockTail         Pointer to the allocator tail pointer.
 * @param   cb                  The number of bytes to allocate.
 */
static void *kmk_cc_block_byte_alloc_grow(PKMKCCBLOCK *ppBlockTail, uint32_t cb)
{
    PKMKCCBLOCK     pOldBlock  = *ppBlockTail;
    PKMKCCBLOCK     pPrevBlock = pOldBlock->pNext;
    PKMKCCBLOCK     pNewBlock;
    uint32_t        cbBlock;

    /*
     * Check if there accidentally is some space left in the previous block first.
     */
    if (   pPrevBlock
        && pPrevBlock->cbBlock - pPrevBlock->offNext >= cb)
    {
        void *pvRet = (char *)pPrevBlock + pPrevBlock->offNext;
        pPrevBlock->offNext += cb;
        return pvRet;
    }

    /*
     * Allocate a new block.
     */

    /* Figure the block size. */
    cbBlock = pOldBlock->cbBlock;
    while (cbBlock - sizeof(KMKCCEXPJUMP) - sizeof(*pNewBlock) < cb)
        cbBlock *= 2;

    /* Allocate and initialize the block it with the new instruction already accounted for. */
    pNewBlock = (PKMKCCBLOCK)xmalloc(cbBlock);
    pNewBlock->cbBlock = cbBlock;
    pNewBlock->offNext = sizeof(*pNewBlock) + cb;
    pNewBlock->pNext   = pOldBlock;
    *ppBlockTail = pNewBlock;

#ifdef KMK_CC_WITH_STATS
    g_cBlockAllocated++;
    g_cbAllocated += cbBlock;
#endif

    return pNewBlock + 1;
}


/**
 * Make a byte allocation.
 *
 * Must call kmk_cc_block_realign() when done doing byte and string allocations.
 *
 * @returns Pointer to the byte allocation (byte aligned).
 * @param   ppBlockTail         Pointer to the allocator tail pointer.
 * @param   cb                  The number of bytes to allocate.
 */
static void *kmk_cc_block_byte_alloc(PKMKCCBLOCK *ppBlockTail, uint32_t cb)
{
    PKMKCCBLOCK pBlockTail = *ppBlockTail;
    uint32_t    cbLeft = pBlockTail->cbBlock - pBlockTail->offNext;

    KMK_CC_ASSERT(cbLeft >= sizeof(KMKCCEXPJUMP));
    if (cbLeft >= cb + sizeof(KMKCCEXPJUMP))
    {
        void *pvRet = (char *)pBlockTail + pBlockTail->offNext;
        pBlockTail->offNext += cb;
        return pvRet;
    }
    return kmk_cc_block_byte_alloc_grow(ppBlockTail, cb);
}


/**
 * Duplicates the given string in a byte allocation.
 *
 * Must call kmk_cc_block_realign() when done doing byte and string allocations.
 *
 * @returns Pointer to the byte allocation (byte aligned).
 * @param   ppBlockTail         Pointer to the allocator tail pointer.
 * @param   cb                  The number of bytes to allocate.
 */
static const char *kmk_cc_block_strdup(PKMKCCBLOCK *ppBlockTail, const char *pachStr, uint32_t cchStr)
{
    char *pszCopy;
    if (cchStr)
    {
        pszCopy = kmk_cc_block_byte_alloc(ppBlockTail, cchStr + 1);
        memcpy(pszCopy, pachStr, cchStr);
        pszCopy[cchStr] = '\0';
        return pszCopy;
    }
    return "";
}


/**
 * Grows the allocation with another block, string expansion program case.
 *
 * @returns Pointer to a string expansion instruction core.
 * @param   ppBlockTail         Pointer to the allocator tail pointer.
 * @param   cb                  The number of bytes to allocate.
 */
static PKMKCCEXPCORE kmk_cc_block_alloc_exp_grow(PKMKCCBLOCK *ppBlockTail, uint32_t cb)
{
    PKMKCCBLOCK     pOldBlock = *ppBlockTail;
    PKMKCCBLOCK     pNewBlock;
    PKMKCCEXPCORE   pRet;
    PKMKCCEXPJUMP   pJump;

    /* Figure the block size. */
    uint32_t cbBlock = !pOldBlock->pNext ? 128 : pOldBlock->cbBlock;
    while (cbBlock - sizeof(KMKCCEXPJUMP) - sizeof(*pNewBlock) < cb)
        cbBlock *= 2;

    /* Allocate and initialize the block it with the new instruction already accounted for. */
    pNewBlock = (PKMKCCBLOCK)xmalloc(cbBlock);
    pNewBlock->cbBlock = cbBlock;
    pNewBlock->offNext = sizeof(*pNewBlock) + cb;
    pNewBlock->pNext   = pOldBlock;
    *ppBlockTail = pNewBlock;

#ifdef KMK_CC_WITH_STATS
    g_cBlockAllocated++;
    g_cbAllocated += cbBlock;
#endif

    pRet = (PKMKCCEXPCORE)(pNewBlock + 1);

    /* Emit jump. */
    pJump = (PKMKCCEXPJUMP)((char *)pOldBlock + pOldBlock->offNext);
    pJump->Core.enmOpCode = kKmkCcExpInstr_Jump;
    pJump->pNext = pRet;
    pOldBlock->offNext += sizeof(*pJump);
    KMK_CC_ASSERT(pOldBlock->offNext <= pOldBlock->cbBlock);

    return pRet;
}


/**
 * Allocates a string expansion instruction of size @a cb.
 *
 * @returns Pointer to a string expansion instruction core.
 * @param   ppBlockTail         Pointer to the allocator tail pointer.
 * @param   cb                  The number of bytes to allocate.
 */
static PKMKCCEXPCORE kmk_cc_block_alloc_exp(PKMKCCBLOCK *ppBlockTail, uint32_t cb)
{
    PKMKCCBLOCK pBlockTail = *ppBlockTail;
    uint32_t    cbLeft = pBlockTail->cbBlock - pBlockTail->offNext;

    KMK_CC_ASSERT(cbLeft >= sizeof(KMKCCEXPJUMP));
    KMK_CC_ASSERT( (cb & (sizeof(void *) - 1)) == 0 || cb == sizeof(KMKCCEXPCORE) /* final */ );

    if (cbLeft >= cb + sizeof(KMKCCEXPJUMP))
    {
        PKMKCCEXPCORE pRet = (PKMKCCEXPCORE)((char *)pBlockTail + pBlockTail->offNext);
        pBlockTail->offNext += cb;
        return pRet;
    }
    return kmk_cc_block_alloc_exp_grow(ppBlockTail, cb);
}


/**
 * Frees all memory used by an allocator.
 *
 * @param   ppBlockTail         The allocator tail pointer.
 */
static void kmk_cc_block_free_list(PKMKCCBLOCK pBlockTail)
{
    while (pBlockTail)
    {
        PKMKCCBLOCK pThis = pBlockTail;
        pBlockTail = pBlockTail->pNext;
        free(pThis);
    }
}

   /*
 *
 * The string expansion compiler.
 * The string expansion compiler.
 * The string expansion compiler.
 *
 */


/**
 * Emits a kKmkCcExpInstr_Return.
 *
 * @param   ppBlockTail         Pointer to the allocator tail pointer.
 */
static void kmk_cc_exp_emit_return(PKMKCCBLOCK *ppBlockTail)
{
    PKMKCCEXPCORE pCore = kmk_cc_block_alloc_exp(ppBlockTail, sizeof(*pCore));
    pCore->enmOpCode = kKmkCcExpInstr_Return;
}


/**
 * Checks if a function is known to mess up the arguments its given.
 *
 * When executing calls to "dirty" functions, all arguments must be duplicated
 * on the heap.
 *
 * @returns 1 if dirty, 0 if clean.
 * @param   pszFunction         The function name.
 */
static uint8_t kmk_cc_is_dirty_function(const char *pszFunction)
{
    switch (pszFunction[0])
    {
        default:
            return 0;

        case 'e':
            if (!strcmp(pszFunction, "eval"))
                return 1;
            if (!strcmp(pszFunction, "evalctx"))
                return 1;
            return 0;

        case 'f':
            if (!strcmp(pszFunction, "filter"))
                return 1;
            if (!strcmp(pszFunction, "filter-out"))
                return 1;
            if (!strcmp(pszFunction, "for"))
                return 1;
            return 0;

        case 's':
            if (!strcmp(pszFunction, "sort"))
                return 1;
            return 0;
    }
}


/**
 * Emits a function call instruction taking arguments that needs expanding.
 *
 * @returns 0 on success, non-zero on failure.
 * @param   ppBlockTail     Pointer to the allocator tail pointer.
 * @param   pszFunction     The function name (const string from function.c).
 * @param   pchArgs         Pointer to the arguments expression string, leading
 *                          any blanks has been stripped.
 * @param   cchArgs         The length of the arguments expression string.
 * @param   cArgs           Number of arguments found.
 * @param   chOpen          The char used to open the function call.
 * @param   chClose         The char used to close the function call.
 * @param   pfnFunction     The function implementation.
 * @param   cMaxArgs        Maximum number of arguments the function takes.
 */
static int kmk_cc_exp_emit_dyn_function(PKMKCCBLOCK *ppBlockTail, const char *pszFunction,
                                        const char *pchArgs, uint32_t cchArgs, uint32_t cArgs, char chOpen, char chClose,
                                        make_function_ptr_t pfnFunction, unsigned char cMaxArgs)
{
    uint32_t iArg;

    /*
     * The function instruction has variable size.  The maximum argument count
     * isn't quite like the minium one.  Zero means no limit.  While a non-zero
     * value means that any commas beyond the max will be taken to be part of
     * the final argument.
     */
    uint32_t            cActualArgs = cArgs <= cMaxArgs || !cMaxArgs ? cArgs : cMaxArgs;
    PKMKCCEXPDYNFUNC    pInstr  = (PKMKCCEXPDYNFUNC)kmk_cc_block_alloc_exp(ppBlockTail, KMKCCEXPDYNFUNC_SIZE(cActualArgs));
    pInstr->FnCore.Core.enmOpCode = kKmkCcExpInstr_DynamicFunction;
    pInstr->FnCore.cArgs          = cActualArgs;
    pInstr->FnCore.pfnFunction    = pfnFunction;
    pInstr->FnCore.pszFuncName    = pszFunction;
    pInstr->FnCore.fDirty         = kmk_cc_is_dirty_function(pszFunction);

    /*
     * Parse the arguments.  Plain arguments gets duplicated in the program
     * memory so that they are terminated and no extra processing is necessary
     * later on.  ASSUMES that the function implementations do NOT change
     * argument memory.  Other arguments the compiled into their own expansion
     * sub programs.
     */
    iArg = 0;
    for (;;)
    {
        /* Find the end of the argument. Check for $. */
        char     ch         = '\0';
        uint8_t  fDollar    = 0;
        int32_t  cDepth     = 0;
        uint32_t cchThisArg = 0;
        while (cchThisArg < cchArgs)
        {
            ch = pchArgs[cchThisArg];
            if (ch == chClose)
            {
                KMK_CC_ASSERT(cDepth > 0);
                if (cDepth > 0)
                    cDepth--;
            }
            else if (ch == chOpen)
                cDepth++;
            else if (ch == ',' && cDepth == 0 && iArg + 1 < cActualArgs)
                break;
            else if (ch == '$')
                fDollar = 1;
            cchThisArg++;
        }

        pInstr->aArgs[iArg].fSubprog = fDollar;
        if (fDollar)
        {
            /* Compile it. */
            int rc;
            kmk_cc_block_realign(ppBlockTail);
            rc = kmk_cc_exp_compile_subprog(ppBlockTail, pchArgs, cchThisArg, &pInstr->aArgs[iArg].u.Subprog);
            if (rc != 0)
                return rc;
        }
        else
        {
            /* Duplicate it. */
            pInstr->aArgs[iArg].u.Plain.psz = kmk_cc_block_strdup(ppBlockTail, pchArgs, cchThisArg);
            pInstr->aArgs[iArg].u.Plain.cch = cchThisArg;
        }
        iArg++;
        if (ch != ',')
            break;
        pchArgs += cchThisArg + 1;
        cchArgs -= cchThisArg + 1;
    }
    KMK_CC_ASSERT(iArg == cActualArgs);

    /*
     * Realign the allocator and take down the address of the next instruction.
     */
    kmk_cc_block_realign(ppBlockTail);
    pInstr->FnCore.pNext = (PKMKCCEXPCORE)kmk_cc_block_get_next_ptr(*ppBlockTail);
    return 0;
}


/**
 * Emits a function call instruction taking plain arguments.
 *
 * @returns 0 on success, non-zero on failure.
 * @param   ppBlockTail     Pointer to the allocator tail pointer.
 * @param   pszFunction     The function name (const string from function.c).
 * @param   pchArgs         Pointer to the arguments string, leading any blanks
 *                          has been stripped.
 * @param   cchArgs         The length of the arguments string.
 * @param   cArgs           Number of arguments found.
 * @param   chOpen          The char used to open the function call.
 * @param   chClose         The char used to close the function call.
 * @param   pfnFunction     The function implementation.
 * @param   cMaxArgs        Maximum number of arguments the function takes.
 */
static void kmk_cc_exp_emit_plain_function(PKMKCCBLOCK *ppBlockTail, const char *pszFunction,
                                           const char *pchArgs, uint32_t cchArgs, uint32_t cArgs, char chOpen, char chClose,
                                           make_function_ptr_t pfnFunction, unsigned char cMaxArgs)
{
    uint32_t iArg;

    /*
     * The function instruction has variable size.  The maximum argument count
     * isn't quite like the minium one.  Zero means no limit.  While a non-zero
     * value means that any commas beyond the max will be taken to be part of
     * the final argument.
     */
    uint32_t            cActualArgs = cArgs <= cMaxArgs || !cMaxArgs ? cArgs : cMaxArgs;
    PKMKCCEXPPLAINFUNC  pInstr  = (PKMKCCEXPPLAINFUNC)kmk_cc_block_alloc_exp(ppBlockTail, KMKCCEXPPLAINFUNC_SIZE(cActualArgs));
    pInstr->FnCore.Core.enmOpCode = kKmkCcExpInstr_PlainFunction;
    pInstr->FnCore.cArgs          = cActualArgs;
    pInstr->FnCore.pfnFunction    = pfnFunction;
    pInstr->FnCore.pszFuncName    = pszFunction;
    pInstr->FnCore.fDirty         = kmk_cc_is_dirty_function(pszFunction);

    /*
     * Parse the arguments.  Plain arguments gets duplicated in the program
     * memory so that they are terminated and no extra processing is necessary
     * later on.  ASSUMES that the function implementations do NOT change
     * argument memory.
     */
    iArg = 0;
    for (;;)
    {
        /* Find the end of the argument. */
        char     ch         = '\0';
        int32_t  cDepth     = 0;
        uint32_t cchThisArg = 0;
        while (cchThisArg < cchArgs)
        {
            ch = pchArgs[cchThisArg];
            if (ch == chClose)
            {
                KMK_CC_ASSERT(cDepth > 0);
                if (cDepth > 0)
                    cDepth--;
            }
            else if (ch == chOpen)
                cDepth++;
            else if (ch == ',' && cDepth == 0 && iArg + 1 < cActualArgs)
                break;
            cchThisArg++;
        }

        /* Duplicate it. */
        pInstr->apszArgs[iArg++] = kmk_cc_block_strdup(ppBlockTail, pchArgs, cchThisArg);
        if (ch != ',')
            break;
        pchArgs += cchThisArg + 1;
        cchArgs -= cchThisArg + 1;
    }

    KMK_CC_ASSERT(iArg == cActualArgs);
    pInstr->apszArgs[iArg] = NULL;

    /*
     * Realign the allocator and take down the address of the next instruction.
     */
    kmk_cc_block_realign(ppBlockTail);
    pInstr->FnCore.pNext = (PKMKCCEXPCORE)kmk_cc_block_get_next_ptr(*ppBlockTail);
}


/**
 * Emits a kKmkCcExpInstr_DynamicVariable.
 *
 * @returns 0 on success, non-zero on failure.
 * @param   ppBlockTail         Pointer to the allocator tail pointer.
 * @param   pchNameExpr         The name of the variable (ASSUMED presistent
 *                              thru-out the program life time).
 * @param   cchNameExpr         The length of the variable name. If zero,
 *                              nothing will be emitted.
 */
static int kmk_cc_exp_emit_dyn_variable(PKMKCCBLOCK *ppBlockTail, const char *pchNameExpr, uint32_t cchNameExpr)
{
    PKMKCCEXPDYNVAR pInstr;
    int rc;
    KMK_CC_ASSERT(cchNameExpr > 0);

    pInstr = (PKMKCCEXPDYNVAR)kmk_cc_block_alloc_exp(ppBlockTail, sizeof(*pInstr));
    pInstr->Core.enmOpCode = kKmkCcExpInstr_DynamicVariable;

    rc = kmk_cc_exp_compile_subprog(ppBlockTail, pchNameExpr, cchNameExpr, &pInstr->Subprog);

    pInstr->pNext = (PKMKCCEXPCORE)kmk_cc_block_get_next_ptr(*ppBlockTail);
    return rc;
}


/**
 * Emits either a kKmkCcExpInstr_PlainVariable or
 * kKmkCcExpInstr_SearchAndReplacePlainVariable instruction.
 *
 * @param   ppBlockTail         Pointer to the allocator tail pointer.
 * @param   pchName             The name of the variable.  (Does not need to be
 *                              valid beyond the call.)
 * @param   cchName             The length of the variable name. If zero,
 *                              nothing will be emitted.
 */
static void kmk_cc_exp_emit_plain_variable_maybe_sr(PKMKCCBLOCK *ppBlockTail, const char *pchName, uint32_t cchName)
{
    if (cchName > 0)
    {
        /*
         * Hopefully, we're not expected to do any search and replace on the
         * expanded variable string later...  Requires both ':' and '='.
         */
        const char *pchEqual;
        const char *pchColon = (const char *)memchr(pchName, ':', cchName);
        if (   pchColon == NULL
            || (pchEqual = (const char *)memchr(pchColon + 1, ':', cchName - (pchColon - pchName - 1))) == NULL
            || pchEqual == pchEqual + 1)
        {
            PKMKCCEXPPLAINVAR pInstr = (PKMKCCEXPPLAINVAR)kmk_cc_block_alloc_exp(ppBlockTail, sizeof(*pInstr));
            pInstr->Core.enmOpCode = kKmkCcExpInstr_PlainVariable;
            pInstr->pszName = strcache2_add(&variable_strcache, pchName, cchName);
        }
        else if (pchColon != pchName)
        {
            /*
             * Okay, we need to do search and replace the variable value.
             * This is performed by patsubst_expand_pat using '%' patterns.
             */
            uint32_t            cchName2   = (uint32_t)(pchColon - pchName);
            uint32_t            cchSearch  = (uint32_t)(pchEqual - pchColon - 1);
            uint32_t            cchReplace = cchName - cchName2 - cchSearch - 2;
            const char         *pchPct;
            char               *psz;
            PKMKCCEXPSRPLAINVAR pInstr;

            pInstr = (PKMKCCEXPSRPLAINVAR)kmk_cc_block_alloc_exp(ppBlockTail, sizeof(*pInstr));
            pInstr->Core.enmOpCode = kKmkCcExpInstr_SearchAndReplacePlainVariable;
            pInstr->pszName = strcache2_add(&variable_strcache, pchName, cchName2);

            /* Figure out the search pattern, unquoting percent chars.. */
            psz = (char *)kmk_cc_block_byte_alloc(ppBlockTail, cchSearch + 2);
            psz[0] = '%';
            memcpy(psz + 1, pchColon + 1, cchSearch);
            psz[1 + cchSearch] = '\0';
            pchPct = find_percent(psz + 1); /* also performs unquoting */
            if (pchPct)
            {
                pInstr->pszSearchPattern    = psz + 1;
                pInstr->offPctSearchPattern = (uint32_t)(pchPct - psz - 1);
            }
            else
            {
                pInstr->pszSearchPattern    = psz;
                pInstr->offPctSearchPattern = 0;
            }

            /* Figure out the replacement pattern, unquoting percent chars.. */
            if (cchReplace == 0)
            {
                pInstr->pszReplacePattern    = "%";
                pInstr->offPctReplacePattern = 0;
            }
            else
            {
                psz = (char *)kmk_cc_block_byte_alloc(ppBlockTail, cchReplace + 2);
                psz[0] = '%';
                memcpy(psz + 1, pchEqual + 1, cchReplace);
                psz[1 + cchReplace] = '\0';
                pchPct = find_percent(psz + 1); /* also performs unquoting */
                if (pchPct)
                {
                    pInstr->pszReplacePattern    = psz + 1;
                    pInstr->offPctReplacePattern = (uint32_t)(pchPct - psz - 1);
                }
                else
                {
                    pInstr->pszReplacePattern    = psz;
                    pInstr->offPctReplacePattern = 0;
                }
            }

            /* Note down where the next instruction is after realigning the allocator. */
            kmk_cc_block_realign(ppBlockTail);
            pInstr->pNext = (PKMKCCEXPCORE)kmk_cc_block_get_next_ptr(*ppBlockTail);
        }
    }
}


/**
 * Emits a kKmkCcExpInstr_CopyString.
 *
 * @param   ppBlockTail         Pointer to the allocator tail pointer.
 * @param   pchStr              The string to emit (ASSUMED presistent thru-out
 *                              the program life time).
 * @param   cchStr              The number of chars to copy. If zero, nothing
 *                              will be emitted.
 */
static void kmk_cc_exp_emit_copy_string(PKMKCCBLOCK *ppBlockTail, const char *pchStr, uint32_t cchStr)
{
    if (cchStr > 0)
    {
        PKMKCCEXPCOPYSTRING pInstr = (PKMKCCEXPCOPYSTRING)kmk_cc_block_alloc_exp(ppBlockTail, sizeof(*pInstr));
        pInstr->Core.enmOpCode = kKmkCcExpInstr_CopyString;
        pInstr->cchCopy = cchStr;
        pInstr->pachSrc = pchStr;
    }
}


/**
 * String expansion compilation function common to both normal and sub programs.
 *
 * @returns 0 on success, non-zero on failure.
 * @param   ppBlockTail         Pointer to the allocator tail pointer.
 * @param   pchStr              The expression to compile.
 * @param   cchStr              The length of the expression to compile.
 */
static int kmk_cc_exp_compile_common(PKMKCCBLOCK *ppBlockTail, const char *pchStr, uint32_t cchStr)
{
    /*
     * Process the string.
     */
    while (cchStr > 0)
    {
        /* Look for dollar sign, marks variable expansion or dollar-escape. */
        int         rc;
        const char *pchDollar = memchr(pchStr, '$', cchStr);
        if (pchDollar)
        {
            /*
             * Check for multiple dollar chars.
             */
            uint32_t offDollar = (uint32_t)(pchDollar - pchStr);
            uint32_t cDollars  = 1;
            while (   offDollar + cDollars < cchStr
                   && pchStr[offDollar + cDollars] == '$')
                cDollars++;

            /*
             * Emit a string copy for any preceeding stuff, including half of
             * the dollars we found (dollar escape: $$ -> $).
             * (kmk_cc_exp_emit_copy_string ignore zero length strings).
             */
            kmk_cc_exp_emit_copy_string(ppBlockTail, pchStr, offDollar + cDollars / 2);
            pchStr += offDollar + cDollars;
            cchStr -= offDollar + cDollars;

            /*
             * Odd number of dollar chars means there is a variable to expand
             * or function to call.
             */
            if (cDollars & 1)
            {
                if (cchStr > 0)
                {
                    char const chOpen = *pchStr;
                    if (chOpen == '(' || chOpen == '{')
                    {
                        /* There are several alternative ways of finding the ending
                           parenthesis / braces.

                           GNU make does one thing for functions and variable containing
                           any '$' chars before the first closing char.  While for
                           variables where a closing char comes before any '$' char, a
                           simplified approach is taken.  This means that for example:

                                Given VAR=var, the expressions "$(var())" and
                                "$($(VAR)())" would be expanded differently.
                                In the first case the variable "var(" would be
                                used and in the second "var()".

                           This code will not duplicate this weird behavior, but work
                           the same regardless of whether there is a '$' char before
                           the first closing char. */
                        make_function_ptr_t pfnFunction;
                        const char         *pszFunction;
                        unsigned char       cMaxArgs;
                        unsigned char       cMinArgs;
                        char                fExpandArgs;
                        char const          chClose   = chOpen == '(' ? ')' : '}';
                        char                ch        = 0;
                        uint32_t            cchName   = 0;
                        uint32_t            cDepth    = 1;
                        uint32_t            cMaxDepth = 1;
                        cDollars = 0;

                        pchStr++;
                        cchStr--;

                        /* First loop: Identify potential function calls and dynamic expansion. */
                        KMK_CC_ASSERT(!func_char_map[chOpen]);
                        KMK_CC_ASSERT(!func_char_map[chClose]);
                        KMK_CC_ASSERT(!func_char_map['$']);
                        while (cchName < cchStr)
                        {
                            ch = pchStr[cchName];
                            if (!func_char_map[(int)ch])
                                break;
                            cchName++;
                        }

                        if (   cchName >= MIN_FUNCTION_LENGTH
                            && cchName <= MAX_FUNCTION_LENGTH
                            && (isblank(ch) || ch == chClose || cchName == cchStr)
                            && (pfnFunction = lookup_function_for_compiler(pchStr, cchName, &cMinArgs, &cMaxArgs,
                                                                           &fExpandArgs, &pszFunction)) != NULL)
                        {
                            /*
                             * It's a function invocation, we should count parameters while
                             * looking for the end.
                             * Note! We use cchName for the length of the argument list.
                             */
                            uint32_t cArgs = 1;
                            if (ch != chClose)
                            {
                                /* Skip leading spaces before the first arg. */
                                cchName++;
                                while (cchName < cchStr && isblank((unsigned char)pchStr[cchName]))
                                    cchName++;

                                pchStr += cchName;
                                cchStr -= cchName;
                                cchName = 0;

                                while (cchName < cchStr)
                                {
                                    ch = pchStr[cchName];
                                    if (ch == ',')
                                    {
                                        if (cDepth == 1)
                                            cArgs++;
                                    }
                                    else if (ch == chClose)
                                    {
                                        if (!--cDepth)
                                            break;
                                    }
                                    else if (ch == chOpen)
                                    {
                                        if (++cDepth > cMaxDepth)
                                            cMaxDepth = cDepth;
                                    }
                                    else if (ch == '$')
                                        cDollars++;
                                    cchName++;
                                }
                            }
                            else
                            {
                                pchStr += cchName;
                                cchStr -= cchName;
                                cchName = 0;
                            }
                            if (cArgs < cMinArgs)
                            {
                                fatal(NULL, _("Function '%.*s' takes a minimum of %d arguments: %d given"),
                                      pszFunction, (int)cMinArgs, (int)cArgs);
                                return -1; /* not reached */
                            }
                            if (cDepth != 0)
                            {
                                fatal(NULL, chOpen == '('
                                      ? _("Missing closing parenthesis calling '%s'") : _("Missing closing braces calling '%s'"),
                                      pszFunction);
                                return -1; /* not reached */
                            }
                            if (cMaxDepth > 16 && fExpandArgs)
                            {
                                fatal(NULL, _("Too many levels of nested function arguments expansions: %s"), pszFunction);
                                return -1; /* not reached */
                            }
                            if (!fExpandArgs || cDollars == 0)
                                kmk_cc_exp_emit_plain_function(ppBlockTail, pszFunction, pchStr, cchName,
                                                               cArgs, chOpen, chClose, pfnFunction, cMaxArgs);
                            else
                            {
                                rc = kmk_cc_exp_emit_dyn_function(ppBlockTail, pszFunction, pchStr, cchName,
                                                                  cArgs, chOpen, chClose, pfnFunction, cMaxArgs);
                                if (rc != 0)
                                    return rc;
                            }
                        }
                        else
                        {
                            /*
                             * Variable, find the end while checking whether anything needs expanding.
                             */
                            if (ch == chClose)
                                cDepth = 0;
                            else if (cchName < cchStr)
                            {
                                if (ch != '$')
                                {
                                    /* Second loop: Look for things that needs expanding. */
                                    while (cchName < cchStr)
                                    {
                                        ch = pchStr[cchName];
                                        if (ch == chClose)
                                        {
                                            if (!--cDepth)
                                                break;
                                        }
                                        else if (ch == chOpen)
                                        {
                                            if (++cDepth > cMaxDepth)
                                                cMaxDepth = cDepth;
                                        }
                                        else if (ch == '$')
                                            break;
                                        cchName++;
                                    }
                                }
                                if (ch == '$')
                                {
                                    /* Third loop: Something needs expanding, just find the end. */
                                    cDollars = 1;
                                    cchName++;
                                    while (cchName < cchStr)
                                    {
                                        ch = pchStr[cchName];
                                        if (ch == chClose)
                                        {
                                            if (!--cDepth)
                                                break;
                                        }
                                        else if (ch == chOpen)
                                        {
                                            if (++cDepth > cMaxDepth)
                                                cMaxDepth = cDepth;
                                        }
                                        cchName++;
                                    }
                                }
                            }
                            if (cDepth > 0) /* After warning, we just assume they're all there. */
                                error(NULL, chOpen == '(' ? _("Missing closing parenthesis ") : _("Missing closing braces"));
                            if (cMaxDepth >= 16)
                            {
                                fatal(NULL, _("Too many levels of nested variable expansions: '%.*s'"), (int)cchName + 2, pchStr - 1);
                                return -1; /* not reached */
                            }
                            if (cDollars == 0)
                                kmk_cc_exp_emit_plain_variable_maybe_sr(ppBlockTail, pchStr, cchName);
                            else
                            {
                                rc = kmk_cc_exp_emit_dyn_variable(ppBlockTail, pchStr, cchName);
                                if (rc != 0)
                                    return rc;
                            }
                        }
                        pchStr += cchName + 1;
                        cchStr -= cchName + (cDepth == 0);
                    }
                    else
                    {
                        /* Single character variable name. */
                        kmk_cc_exp_emit_plain_variable_maybe_sr(ppBlockTail, pchStr, 1);
                        pchStr++;
                        cchStr--;
                    }
                }
                else
                {
                    error(NULL, _("Unexpected end of string after $"));
                    break;
                }
            }
        }
        else
        {
            /*
             * Nothing more to expand, the remainder is a simple string copy.
             */
            kmk_cc_exp_emit_copy_string(ppBlockTail, pchStr, cchStr);
            break;
        }
    }

    /*
     * Emit final instruction.
     */
    kmk_cc_exp_emit_return(ppBlockTail);
    return 0;
}


/**
 * Initializes string expansion program statistics.
 * @param   pStats              Pointer to the statistics structure to init.
 */
static void kmk_cc_exp_stats_init(PKMKCCEXPSTATS pStats)
{
    pStats->cchAvg = 0;
}


/**
 * Compiles a string expansion subprogram.
 *
 * The caller typically make a call to kmk_cc_block_get_next_ptr after this
 * function returns to figure out where to continue executing.
 *
 * @returns 0 on success, non-zero on failure.
 * @param   ppBlockTail         Pointer to the allocator tail pointer.
 * @param   pchStr              Pointer to the string to compile an expansion
 *                              program for (ASSUMED to be valid for the
 *                              lifetime of the program).
 * @param   cchStr              The length of the string to compile. Expected to
 *                              be at least on char long.
 * @param   pSubProg            The subprogram structure to initialize.
 */
static int kmk_cc_exp_compile_subprog(PKMKCCBLOCK *ppBlockTail, const char *pchStr, uint32_t cchStr, PKMKCCEXPSUBPROG pSubProg)
{
    KMK_CC_ASSERT(cchStr > 0);
    pSubProg->pFirstInstr = (PKMKCCEXPCORE)kmk_cc_block_get_next_ptr(*ppBlockTail);
    kmk_cc_exp_stats_init(&pSubProg->Stats);
    return kmk_cc_exp_compile_common(ppBlockTail, pchStr, cchStr);
}


/**
 * Compiles a string expansion program.
 *
 * @returns Pointer to the program on success, NULL on failure.
 * @param   pchStr              Pointer to the string to compile an expansion
 *                              program for (ASSUMED to be valid for the
 *                              lifetime of the program).
 * @param   cchStr              The length of the string to compile. Expected to
 *                              be at least on char long.
 */
static PKMKCCEXPPROG kmk_cc_exp_compile(const char *pchStr, uint32_t cchStr)
{
    /*
     * Estimate block size, allocate one and initialize it.
     */
    PKMKCCEXPPROG   pProg;
    PKMKCCBLOCK     pBlock;
    pProg = kmk_cc_block_alloc_first(&pBlock, sizeof(*pProg),
                                     (kmk_cc_count_dollars(pchStr, cchStr) + 4)  * 8);
    if (pProg)
    {
        int rc = 0;

        pProg->pBlockTail   = pBlock;
        pProg->pFirstInstr  = (PKMKCCEXPCORE)kmk_cc_block_get_next_ptr(pBlock);
        kmk_cc_exp_stats_init(&pProg->Stats);
        pProg->cRefs        = 1;
#ifdef KMK_CC_STRICT
        pProg->uInputHash   = kmk_cc_debug_string_hash_n(0, pchStr, cchStr);
#endif

        /*
         * Join forces with the subprogram compilation code.
         */
        if (kmk_cc_exp_compile_common(&pProg->pBlockTail, pchStr, cchStr) == 0)
        {
#ifdef KMK_CC_WITH_STATS
            pBlock = pProg->pBlockTail;
            if (!pBlock->pNext)
                g_cSingleBlockExpProgs++;
            else if (!pBlock->pNext->pNext)
                g_cTwoBlockExpProgs++;
            else
                g_cMultiBlockExpProgs++;
            for (; pBlock; pBlock = pBlock->pNext)
            {
                g_cBlocksAllocatedExpProgs++;
                g_cbAllocatedExpProgs += pBlock->cbBlock;
                g_cbUnusedMemExpProgs += pBlock->cbBlock - pBlock->offNext;
            }
#endif
            return pProg;
        }
        kmk_cc_block_free_list(pProg->pBlockTail);
    }
    return NULL;
}


/**
 * Updates the recursive_without_dollar member of a variable structure.
 *
 * This avoid compiling string expansion programs without only a CopyString
 * instruction.  By setting recursive_without_dollar to 1, code calling
 * kmk_cc_compile_variable_for_expand and kmk_exec_expand_to_var_buf will
 * instead treat start treating it as a simple variable, which is faster.
 *
 * @returns The updated recursive_without_dollar value.
 * @param   pVar        Pointer to the variable.
 */
static int kmk_cc_update_variable_recursive_without_dollar(struct variable *pVar)
{
    int fValue;
    KMK_CC_ASSERT(pVar->recursive_without_dollar == 0);

    if (memchr(pVar->value, '$', pVar->value_length))
        fValue = -1;
    else
        fValue = 1;
    pVar->recursive_without_dollar = fValue;

    return fValue;
}


/**
 * Compiles a variable for string expansion.
 *
 * @returns Pointer to the string expansion program on success, NULL if no
 *          program was created.
 * @param   pVar        Pointer to the variable.
 */
struct kmk_cc_expandprog *kmk_cc_compile_variable_for_expand(struct variable *pVar)
{
    KMK_CC_ASSERT(strlen(pVar->value) == pVar->value_length);
    KMK_CC_ASSERT(!pVar->expandprog);
    KMK_CC_ASSERT(pVar->recursive_without_dollar <= 0);

    if (   !pVar->expandprog
        && pVar->recursive)
    {
        if (   pVar->recursive_without_dollar < 0
            || (   pVar->recursive_without_dollar == 0
                && kmk_cc_update_variable_recursive_without_dollar(pVar) < 0) )
        {
            pVar->expandprog = kmk_cc_exp_compile(pVar->value, pVar->value_length);
            g_cVarForExpandCompilations++;
        }
    }
    return pVar->expandprog;
}


/**
 * String expansion execution worker for outputting a variable.
 *
 * @returns The new variable buffer position.
 * @param   pVar        The variable to reference.
 * @param   pchDst      The current variable buffer position.
 */
static char *kmk_exec_expand_worker_reference_variable(struct variable *pVar, char *pchDst)
{
    if (pVar->value_length > 0)
    {
        if (!pVar->recursive || IS_VARIABLE_RECURSIVE_WITHOUT_DOLLAR(pVar))
            pchDst = variable_buffer_output(pchDst, pVar->value, pVar->value_length);
        else
            pchDst = reference_recursive_variable(pchDst, pVar);
    }
    else if (pVar->append)
        pchDst = reference_recursive_variable(pchDst, pVar);
    return pchDst;
}


/**
 * Executes a stream string expansion instructions, outputting to the current
 * varaible buffer.
 *
 * @returns The new variable buffer position.
 * @param   pInstrCore      The instruction to start executing at.
 * @param   pchDst          The current variable buffer position.
 */
static char *kmk_exec_expand_instruction_stream_to_var_buf(PKMKCCEXPCORE pInstrCore, char *pchDst)
{
    for (;;)
    {
        switch (pInstrCore->enmOpCode)
        {
            case kKmkCcExpInstr_CopyString:
            {
                PKMKCCEXPCOPYSTRING pInstr = (PKMKCCEXPCOPYSTRING)pInstrCore;
                pchDst = variable_buffer_output(pchDst, pInstr->pachSrc, pInstr->cchCopy);

                pInstrCore = &(pInstr + 1)->Core;
                break;
            }

            case kKmkCcExpInstr_PlainVariable:
            {
                PKMKCCEXPPLAINVAR pInstr = (PKMKCCEXPPLAINVAR)pInstrCore;
                struct variable  *pVar = lookup_variable_strcached(pInstr->pszName);
                if (pVar)
                    pchDst = kmk_exec_expand_worker_reference_variable(pVar, pchDst);
                else
                    warn_undefined(pInstr->pszName, strcache2_get_len(&variable_strcache, pInstr->pszName));

                pInstrCore = &(pInstr + 1)->Core;
                break;
            }

            case kKmkCcExpInstr_DynamicVariable:
            {
                PKMKCCEXPDYNVAR  pInstr = (PKMKCCEXPDYNVAR)pInstrCore;
                struct variable *pVar;
                uint32_t         cchName;
                char            *pszName = kmk_exec_expand_subprog_to_tmp(&pInstr->Subprog, &cchName);
                char            *pszColon = (char *)memchr(pszName, ':', cchName);
                char            *pszEqual;
                if (   pszColon == NULL
                    || (pszEqual = (char *)memchr(pszColon + 1, '=', &pszName[cchName] - pszColon - 1)) == NULL
                    || pszEqual == pszColon + 1)
                {
                    pVar = lookup_variable(pszName, cchName);
                    if (pVar)
                        pchDst = kmk_exec_expand_worker_reference_variable(pVar, pchDst);
                    else
                        warn_undefined(pszName, cchName);
                }
                else if (pszColon != pszName)
                {
                    /*
                     * Oh, we have to do search and replace. How tedious.
                     * Since the variable name is a temporary buffer, we can transform
                     * the strings into proper search and replacement patterns directly.
                     */
                    pVar = lookup_variable(pszName, pszColon - pszName);
                    if (pVar)
                    {
                        char const *pszExpandedVarValue = pVar->recursive ? recursively_expand(pVar) : pVar->value;
                        char       *pszSearchPat  = pszColon + 1;
                        char       *pszReplacePat = pszEqual + 1;
                        const char *pchPctSearchPat;
                        const char *pchPctReplacePat;

                        *pszEqual = '\0';
                        pchPctSearchPat = find_percent(pszSearchPat);
                        pchPctReplacePat = find_percent(pszReplacePat);

                        if (!pchPctReplacePat)
                        {
                            if (pszReplacePat[-2] != '\0') /* On the offchance that a pct was unquoted by find_percent. */
                            {
                                memmove(pszName + 1, pszSearchPat, pszReplacePat - pszSearchPat);
                                if (pchPctSearchPat)
                                    pchPctSearchPat -= pszSearchPat - &pszName[1];
                                pszSearchPat = &pszName[1];
                            }
                            pchPctReplacePat = --pszReplacePat;
                            *pszReplacePat = '%';
                        }

                        if (!pchPctSearchPat)
                        {
                            pchPctSearchPat = --pszSearchPat;
                            *pszSearchPat = '%';
                        }

                        pchDst = patsubst_expand_pat(pchDst, pszExpandedVarValue,
                                                     pszSearchPat, pszReplacePat,
                                                     pchPctSearchPat, pchPctReplacePat);

                        if (pVar->recursive)
                            free((void *)pszExpandedVarValue);
                    }
                    else
                        warn_undefined(pszName, pszColon - pszName);
                }
                free(pszName);

                pInstrCore = pInstr->pNext;
                break;
            }


            case kKmkCcExpInstr_SearchAndReplacePlainVariable:
            {
                PKMKCCEXPSRPLAINVAR pInstr = (PKMKCCEXPSRPLAINVAR)pInstrCore;
                struct variable    *pVar = lookup_variable_strcached(pInstr->pszName);
                if (pVar)
                {
                    char const *pszExpandedVarValue = pVar->recursive ? recursively_expand(pVar) : pVar->value;
                    pchDst = patsubst_expand_pat(pchDst,
                                                 pszExpandedVarValue,
                                                 pInstr->pszSearchPattern,
                                                 pInstr->pszReplacePattern,
                                                 &pInstr->pszSearchPattern[pInstr->offPctSearchPattern],
                                                 &pInstr->pszReplacePattern[pInstr->offPctReplacePattern]);
                    if (pVar->recursive)
                        free((void *)pszExpandedVarValue);
                }
                else
                    warn_undefined(pInstr->pszName, strcache2_get_len(&variable_strcache, pInstr->pszName));

                pInstrCore = pInstr->pNext;
                break;
            }

            case kKmkCcExpInstr_PlainFunction:
            {
                PKMKCCEXPPLAINFUNC pInstr = (PKMKCCEXPPLAINFUNC)pInstrCore;
                uint32_t iArg;
                if (!pInstr->FnCore.fDirty)
                {
#ifdef KMK_CC_STRICT
                    uint32_t uCrcBefore = 0;
                    uint32_t uCrcAfter = 0;
                    iArg = pInstr->FnCore.cArgs;
                    while (iArg-- > 0)
                        uCrcBefore = kmk_cc_debug_string_hash(uCrcBefore, pInstr->apszArgs[iArg]);
#endif

                    pchDst = pInstr->FnCore.pfnFunction(pchDst, (char **)&pInstr->apszArgs[0], pInstr->FnCore.pszFuncName);

#ifdef KMK_CC_STRICT
                    iArg = pInstr->FnCore.cArgs;
                    while (iArg-- > 0)
                        uCrcAfter = kmk_cc_debug_string_hash(uCrcAfter, pInstr->apszArgs[iArg]);
                    KMK_CC_ASSERT(uCrcBefore == uCrcAfter);
#endif
                }
                else
                {
                    char **papszShadowArgs = xmalloc((pInstr->FnCore.cArgs * 2 + 1) * sizeof(papszShadowArgs[0]));
                    char **papszArgs = &papszShadowArgs[pInstr->FnCore.cArgs];

                    iArg = pInstr->FnCore.cArgs;
                    papszArgs[iArg] = NULL;
                    while (iArg-- > 0)
                        papszArgs[iArg] = papszShadowArgs[iArg] = xstrdup(pInstr->apszArgs[iArg]);

                    pchDst = pInstr->FnCore.pfnFunction(pchDst, (char **)&pInstr->apszArgs[0], pInstr->FnCore.pszFuncName);

                    iArg = pInstr->FnCore.cArgs;
                    while (iArg-- > 0)
                        free(papszShadowArgs[iArg]);
                    free(papszShadowArgs);
                }

                pInstrCore = pInstr->FnCore.pNext;
                break;
            }

            case kKmkCcExpInstr_DynamicFunction:
            {
                PKMKCCEXPDYNFUNC pInstr = (PKMKCCEXPDYNFUNC)pInstrCore;
                char           **papszArgsShadow = xmalloc( (pInstr->FnCore.cArgs * 2 + 1) * sizeof(char *));
                char           **papszArgs = &papszArgsShadow[pInstr->FnCore.cArgs];
                uint32_t         iArg;

                if (!pInstr->FnCore.fDirty)
                {
#ifdef KMK_CC_STRICT
                    uint32_t    uCrcBefore = 0;
                    uint32_t    uCrcAfter = 0;
#endif
                    iArg = pInstr->FnCore.cArgs;
                    papszArgs[iArg] = NULL;
                    while (iArg-- > 0)
                    {
                        char *pszArg;
                        if (pInstr->aArgs[iArg].fSubprog)
                            pszArg = kmk_exec_expand_subprog_to_tmp(&pInstr->aArgs[iArg].u.Subprog, NULL);
                        else
                            pszArg = (char *)pInstr->aArgs[iArg].u.Plain.psz;
                        papszArgsShadow[iArg] = pszArg;
                        papszArgs[iArg]       = pszArg;
#ifdef KMK_CC_STRICT
                        uCrcBefore = kmk_cc_debug_string_hash(uCrcBefore, pszArg);
#endif
                    }
                    pchDst = pInstr->FnCore.pfnFunction(pchDst, papszArgs, pInstr->FnCore.pszFuncName);

                    iArg = pInstr->FnCore.cArgs;
                    while (iArg-- > 0)
                    {
#ifdef KMK_CC_STRICT
                        KMK_CC_ASSERT(papszArgsShadow[iArg] == papszArgs[iArg]);
                        uCrcAfter = kmk_cc_debug_string_hash(uCrcAfter, papszArgsShadow[iArg]);
#endif
                        if (pInstr->aArgs[iArg].fSubprog)
                            free(papszArgsShadow[iArg]);
                    }
                    KMK_CC_ASSERT(uCrcBefore == uCrcAfter);
                }
                else
                {
                    iArg = pInstr->FnCore.cArgs;
                    papszArgs[iArg] = NULL;
                    while (iArg-- > 0)
                    {
                        char *pszArg;
                        if (pInstr->aArgs[iArg].fSubprog)
                            pszArg = kmk_exec_expand_subprog_to_tmp(&pInstr->aArgs[iArg].u.Subprog, NULL);
                        else
                            pszArg = xstrdup(pInstr->aArgs[iArg].u.Plain.psz);
                        papszArgsShadow[iArg] = pszArg;
                        papszArgs[iArg]       = pszArg;
                    }

                    pchDst = pInstr->FnCore.pfnFunction(pchDst, papszArgs, pInstr->FnCore.pszFuncName);

                    iArg = pInstr->FnCore.cArgs;
                    while (iArg-- > 0)
                        free(papszArgsShadow[iArg]);
                }
                free(papszArgsShadow);

                pInstrCore = pInstr->FnCore.pNext;
                break;
            }

            case kKmkCcExpInstr_Jump:
            {
                PKMKCCEXPJUMP pInstr = (PKMKCCEXPJUMP)pInstrCore;
                pInstrCore = pInstr->pNext;
                break;
            }

            case kKmkCcExpInstr_Return:
                return pchDst;

            default:
                fatal(NULL, _("Unknown string expansion opcode: %d (%#x)"),
                      (int)pInstrCore->enmOpCode, (int)pInstrCore->enmOpCode);
                return NULL;
        }
    }
}


/**
 * Updates the string expansion statistics.
 *
 * @param   pStats              The statistics structure to update.
 * @param   cchResult           The result lenght.
 */
void kmk_cc_exp_stats_update(PKMKCCEXPSTATS pStats, uint32_t cchResult)
{
    /*
     * The average is simplified and not an exact average for every
     * expansion that has taken place.
     */
    pStats->cchAvg = (pStats->cchAvg * 7 + cchResult) / 8;
}


/**
 * Execute a string expansion subprogram, outputting to a new heap buffer.
 *
 * @returns Pointer to the output buffer (hand to free when done).
 * @param   pSubProg          The subprogram to execute.
 * @param   pcchResult        Where to return the size of the result. Optional.
 */
static char *kmk_exec_expand_subprog_to_tmp(PKMKCCEXPSUBPROG pSubProg, uint32_t *pcchResult)
{
    char           *pchOldVarBuf;
    unsigned int    cbOldVarBuf;
    char           *pchDst;
    char           *pszResult;
    uint32_t        cchResult;

    /*
     * Temporarily replace the variable buffer while executing the instruction
     * stream for this subprogram.
     */
    pchDst = install_variable_buffer_with_hint(&pchOldVarBuf, &cbOldVarBuf,
                                               pSubProg->Stats.cchAvg ? pSubProg->Stats.cchAvg + 32 : 256);

    pchDst = kmk_exec_expand_instruction_stream_to_var_buf(pSubProg->pFirstInstr, pchDst);

    /* Ensure that it's terminated. */
    pchDst = variable_buffer_output(pchDst, "\0", 1) - 1;

    /* Grab the result buffer before restoring the previous one. */
    pszResult = variable_buffer;
    cchResult = (uint32_t)(pchDst - pszResult);
    if (pcchResult)
        *pcchResult = cchResult;
    kmk_cc_exp_stats_update(&pSubProg->Stats, cchResult);

    variable_buffer = pchOldVarBuf;
    variable_buffer_length = cbOldVarBuf;

    return pszResult;
}


/**
 * Execute a string expansion program, outputting to the current variable
 * buffer.
 *
 * @returns New variable buffer position.
 * @param   pProg               The program to execute.
 * @param   pchDst              The current varaible buffer position.
 */
static char *kmk_exec_expand_prog_to_var_buf(PKMKCCEXPPROG pProg, char *pchDst)
{
    uint32_t cchResult;
    uint32_t offStart = (uint32_t)(pchDst - variable_buffer);

    if (pProg->Stats.cchAvg >= variable_buffer_length - offStart)
        pchDst = ensure_variable_buffer_space(pchDst, offStart + pProg->Stats.cchAvg + 32);

    KMK_CC_ASSERT(pProg->cRefs > 0);
    pProg->cRefs++;

    pchDst = kmk_exec_expand_instruction_stream_to_var_buf(pProg->pFirstInstr, pchDst);

    pProg->cRefs--;
    KMK_CC_ASSERT(pProg->cRefs > 0);

    cchResult = (uint32_t)(pchDst - variable_buffer);
    KMK_CC_ASSERT(cchResult >= offStart);
    cchResult -= offStart;
    kmk_cc_exp_stats_update(&pProg->Stats, cchResult);
    g_cVarForExpandExecs++;

    return pchDst;
}


/**
 * Expands a variable into a variable buffer using its expandprog.
 *
 * @returns The new variable buffer position.
 * @param   pVar        Pointer to the variable.  Must have a program.
 * @param   pchDst      Pointer to the current variable buffer position.
 */
char *kmk_exec_expand_to_var_buf(struct variable *pVar, char *pchDst)
{
    KMK_CC_ASSERT(pVar->expandprog);
    KMK_CC_ASSERT(pVar->expandprog->uInputHash == kmk_cc_debug_string_hash(0, pVar->value));
    return kmk_exec_expand_prog_to_var_buf(pVar->expandprog, pchDst);
}





/*
 *
 * Makefile evaluation programs.
 * Makefile evaluation programs.
 * Makefile evaluation programs.
 *
 */
/*#define KMK_CC_EVAL_ENABLE*/


/**
 * Compiles a variable direct evaluation as is, setting v->evalprog on success.
 *
 * @returns Pointer to the program on success, NULL if no program was created.
 * @param   pVar        Pointer to the variable.
 */
struct kmk_cc_evalprog   *kmk_cc_compile_variable_for_eval(struct variable *pVar)
{
    return NULL;
}


/**
 * Compiles a makefile for
 *
 * @returns Pointer to the program on success, NULL if no program was created.
 * @param   pVar        Pointer to the variable.
 */
struct kmk_cc_evalprog   *kmk_cc_compile_file_for_eval(FILE *pFile, const char *pszFilename)
{
#ifdef KMK_CC_EVAL_ENABLE
    /*
     * Read the entire file into a zero terminate memory buffer.
     */
    size_t      cchContent = 0;
    char       *pszContent = NULL;
    struct stat st;
    if (!fstat(fileno(pFile), &st))
    {
        if (   st.st_size > (off_t)16*1024*1024
            && st.st_size < 0)
            fatal(NULL, _("Makefile too large to compile: %ld bytes (%#lx) - max 16MB"), (long)st.st_size, (long)st.st_size);
        cchContent = (size_t)st.st_size;
        pszContent = (char *)xmalloc(cchContent + 1);

        cchContent = fread(pszContent, 1, cchContent, pFile);
        if (ferror(pFile))
            fatal(NULL, _("Read error: %s"), strerror(errno));
    }
    else
    {
        size_t cbAllocated = 2048;
        do
        {
            cbAllocated *= 2;
            if (cbAllocated > 16*1024*1024)
                fatal(NULL, _("Makefile too large to compile: max 16MB"));
            pszContent = (char *)xrealloc(pszContent, cbAllocated);
            cchContent += fread(&pszContent[cchContent], 1, cbAllocated - 1 - cchContent, pFile);
            if (ferror(pFile))
                fatal(NULL, _("Read error: %s"), strerror(errno));
        } while (!feof(pFile));
    }
    pszContent[cchContent] = '\0';

    /*
     * Call common function to do the compilation.
     */
    //kmk_cc_eval_compile_common()

    free(pszContent);
    return NULL;
#else
    return NULL;
#endif
}


/**
 * Equivalent of eval_buffer, only it's using the evalprog of the variable.
 *
 * @param   pVar        Pointer to the variable. Must have a program.
 */
void kmk_exec_eval_variable(struct variable *pVar)
{
    KMK_CC_ASSERT(pVar->evalprog);
    assert(0);
}


/**
 * Worker for eval_makefile.
 *
 * @param   pEvalProg   The program pointer.
 */
void kmk_exec_eval_file(struct kmk_cc_evalprog *pEvalProg)
{
    KMK_CC_ASSERT(pEvalProg);
    assert(0);
}



/*
 *
 * Program destruction hooks.
 * Program destruction hooks.
 * Program destruction hooks.
 *
 */


/**
 * Called when a variable with expandprog or/and evalprog changes.
 *
 * @param   pVar        Pointer to the variable.
 */
void  kmk_cc_variable_changed(struct variable *pVar)
{
    PKMKCCEXPPROG pProg = pVar->expandprog;

    KMK_CC_ASSERT(pVar->evalprog || pProg);

#if 0
    if (pVar->evalprog)
    {
        kmk_cc_block_free_list(pVar->evalprog->pBlockTail);
        pVar->evalprog = NULL;
    }
#endif

    if (pProg)
    {
        if (pProg->cRefs == 1)
            kmk_cc_block_free_list(pProg->pBlockTail);
        else
            fatal(NULL, _("Modifying a variable (%s) while its expansion program is running is not supported"), pVar->name);
        pVar->expandprog = NULL;
    }
}


/**
 * Called when a variable with expandprog or/and evalprog is deleted.
 *
 * @param   pVar        Pointer to the variable.
 */
void  kmk_cc_variable_deleted(struct variable *pVar)
{
    PKMKCCEXPPROG pProg = pVar->expandprog;

    KMK_CC_ASSERT(pVar->evalprog || pProg);

#if 0
    if (pVar->evalprog)
    {
        kmk_cc_block_free_list(pVar->evalprog->pBlockTail);
        pVar->evalprog = NULL;
    }
#endif

    if (pProg)
    {
        if (pProg->cRefs == 1)
            kmk_cc_block_free_list(pProg->pBlockTail);
        else
            fatal(NULL, _("Deleting a variable (%s) while its expansion program is running is not supported"), pVar->name);
        pVar->expandprog = NULL;
    }
}







#endif /* CONFIG_WITH_COMPILER */

