#ifdef CONFIG_WITH_COMPILER
/* $Id: kmk_cc_exec.c 2811 2016-03-12 13:26:33Z knut.osmundsen@oracle.com $ */
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
#include "k/kDefs.h"
#include "k/kTypes.h"


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
# elif defined(__GNUC__) && (defined(KBUILD_ARCH_X86) || defined(KBUILD_ARCH_AMD64))
#  define KMK_CC_ASSERT(a_TrueExpr)         do { if (!(a_TrueExpr)) __asm__ __volatile__("int3;nop"); } while (0)
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
# define KMK_CC_STATIC_ASSERT_EX(a_Expr, a_szExpl)  extern int KMK_CC_STATIC_ASSERT_EX_VAR[(a_Expr) ? 1 : 0]
extern int KMK_CC_STATIC_ASSERT_EX_VAR[1];
#endif
/** @def KMK_CC_STATIC_ASSERT
 * Compile time assertion, simple variant.
 */
#define KMK_CC_STATIC_ASSERT(a_Expr)                KMK_CC_STATIC_ASSERT_EX(a_Expr, #a_Expr)


/** Aligns a size for the block allocator. */
#define KMK_CC_BLOCK_ALIGN_SIZE(a_cb)               ( ((a_cb) + (sizeof(void *) - 1U)) & ~(uint32_t)(sizeof(void *) - 1U) )

/** How to declare a no-return function.
 * Place between scope (if any) and return type.  */
#ifdef _MSC_VER
# define KMK_CC_FN_NO_RETURN                        __declspec(noreturn)
#elif defined(__GNUC__)
# define KMK_CC_FN_NO_RETURN                        __attribute__((__noreturn__))
#endif


/** @defgroup grp_kmk_cc_evalprog Makefile Evaluation
 * @{
 */
#if 1
# define KMK_CC_EVAL_DPRINTF_UNPACK(...)            __VA_ARGS__
# define KMK_CC_EVAL_DPRINTF(a)                     fprintf(stderr, KMK_CC_EVAL_DPRINTF_UNPACK a)
#else
# define KMK_CC_EVAL_DPRINTF(a)                     do { } while (0)
#endif

/** @name KMK_CC_EVAL_QUALIFIER_XXX - Variable qualifiers.
 * @{ */
#define KMK_CC_EVAL_QUALIFIER_LOCAL         1
#define KMK_CC_EVAL_QUALIFIER_EXPORT        2
#define KMK_CC_EVAL_QUALIFIER_OVERRIDE      4
#define KMK_CC_EVAL_QUALIFIER_PRIVATE       8
/** @} */

/** Eval: Max nesting depth of makefile conditionals.
 * Affects stack usage in kmk_cc_eval_compile_worker.  */
#define KMK_CC_EVAL_MAX_IF_DEPTH            32
/** Eval: Maximum number of escaped end of line sequences to track.
 * Affects stack usage in kmk_cc_eval_compile_worker, but not the actual
 * number of consequtive escaped newlines in the input file/variable. */
#define KMK_CC_EVAL_MAX_ESC_EOLS            2

/** Minimum keyword length. */
#define KMK_CC_EVAL_KEYWORD_MIN             2
/** Maximum keyword length. */
#define KMK_CC_EVAL_KEYWORD_MAX             16

/** @name KMK_CC_EVAL_CH_XXX - flags found in g_abEvalCcChars.
 * @{ */
/** Normal character, nothing special. */
#define KMK_CC_EVAL_CH_NORMAL                       UINT16_C(0)
/** Blank character. */
#define KMK_CC_EVAL_CH_BLANK                        UINT16_C(1)
#define KMK_CC_EVAL_IS_BLANK(a_ch)                  (KMK_CC_EVAL_BM_GET(g_abEvalCcChars, a_ch) & KMK_CC_EVAL_CH_BLANK)
/** Space character. */
#define KMK_CC_EVAL_CH_SPACE                        UINT16_C(2)
#define KMK_CC_EVAL_IS_SPACE(a_ch)                  (KMK_CC_EVAL_BM_GET(g_abEvalCcChars, a_ch) & KMK_CC_EVAL_CH_SPACE)
/** Space character or potential EOL escape backslash. */
#define KMK_CC_EVAL_CH_SPACE_OR_BACKSLASH           UINT16_C(4)
#define KMK_CC_EVAL_IS_SPACE_OR_BACKSLASH(a_ch)     (KMK_CC_EVAL_BM_GET(g_abEvalCcChars, a_ch) & KMK_CC_EVAL_CH_SPACE_OR_BACKSLASH)
/** Anything we need to take notice of when parsing something could be a
 * variable name or a recipe.
 * All space characters, backslash (EOL escape), variable expansion dollar,
 * variable assignment operator chars, recipe colon and recipe percent. */
#define KMK_CC_EVAL_CH_SPACE_VAR_OR_RECIPE          UINT16_C(8)
#define KMK_CC_EVAL_IS_SPACE_VAR_OR_RECIPE(a_ch)    (KMK_CC_EVAL_BM_GET(g_abEvalCcChars, a_ch) & KMK_CC_EVAL_CH_SPACE_VAR_OR_RECIPE)
/** Dollar character (possible variable expansion). */
#define KMK_CC_EVAL_CH_DOLLAR                       UINT16_C(16)
#define KMK_CC_EVAL_IS_DOLLAR(a_ch)                 (KMK_CC_EVAL_BM_GET(g_abEvalCcChars, a_ch) & KMK_CC_EVAL_CH_DOLLAR)
/** Dollar character (possible variable expansion). */
#define KMK_CC_EVAL_CH_BACKSLASH                    UINT16_C(32)
#define KMK_CC_EVAL_IS_BACKSLASH(a_ch)              (KMK_CC_EVAL_BM_GET(g_abEvalCcChars, a_ch) & KMK_CC_EVAL_CH_BACKSLASH)
/** Possible EOL character. */
#define KMK_CC_EVAL_CH_EOL_CANDIDATE                UINT16_C(64)
#define KMK_CC_EVAL_IS_EOL_CANDIDATE(a_ch)          (KMK_CC_EVAL_BM_GET(g_abEvalCcChars, a_ch) & KMK_CC_EVAL_CH_EOL_CANDIDATE)
/** First character in a keyword. */
#define KMK_CC_EVAL_CH_1ST_IN_KEYWORD               UINT16_C(128)
#define KMK_CC_EVAL_IS_1ST_IN_KEYWORD(a_ch)         (KMK_CC_EVAL_BM_GET(g_abEvalCcChars, a_ch) & KMK_CC_EVAL_CH_1ST_IN_KEYWORD)
/** Second character in a keyword. */
#define KMK_CC_EVAL_CH_2ND_IN_KEYWORD               UINT16_C(256)
#define KMK_CC_EVAL_IS_2ND_IN_KEYWORD(a_ch)         (KMK_CC_EVAL_BM_GET(g_abEvalCcChars, a_ch) & KMK_CC_EVAL_CH_2ND_IN_KEYWORD)
/** First character in a variable qualifier keyword or 'define'. */
#define KMK_CC_EVAL_CH_1ST_IN_VARIABLE_KEYWORD      UINT16_C(512)
#define KMK_CC_EVAL_IS_1ST_IN_VARIABLE_KEYWORD(a_ch) (KMK_CC_EVAL_BM_GET(g_abEvalCcChars, a_ch) & KMK_CC_EVAL_CH_1ST_IN_VARIABLE_KEYWORD)
/** Used when parsing variable names, looking for the end of a nested
 *  variable reference.  Matches parentheses and backslash (escaped eol). */
#define KMK_CC_EVAL_CH_PAREN_OR_SLASH               UINT16_C(1024)
#define KMK_CC_EVAL_IS_PAREN_OR_SLASH(a_ch)         (KMK_CC_EVAL_BM_GET(g_abEvalCcChars, a_ch) & KMK_CC_EVAL_CH_PAREN_OR_SLASH)
/** Used when parsing ifeq/ifneq (,) sequences.
 * Matches parentheses, comma and dollar (for non-plain string detection). */
#define KMK_CC_EVAL_CH_PAREN_COMMA_OR_DOLLAR        UINT16_C(2048)
#define KMK_CC_EVAL_IS_PAREN_COMMA_OR_DOLLAR(a_ch)  (KMK_CC_EVAL_BM_GET(g_abEvalCcChars, a_ch) & KMK_CC_EVAL_CH_PAREN_COMMA_OR_DOLLAR)

/** Test of space or dollar characters. */
#define KMK_CC_EVAL_IS_SPACE_OR_DOLLAR(a_ch)        (KMK_CC_EVAL_BM_GET(g_abEvalCcChars, a_ch) & (KMK_CC_EVAL_CH_SPACE | KMK_CC_EVAL_CH_DOLLAR))
/** Test of space, dollar or backslash (possible EOL escape) characters. */
#define KMK_CC_EVAL_IS_SPACE_DOLLAR_OR_SLASH(a_ch)  (KMK_CC_EVAL_BM_GET(g_abEvalCcChars, a_ch) & (KMK_CC_EVAL_CH_SPACE | KMK_CC_EVAL_CH_DOLLAR | KMK_CC_EVAL_CH_BACKSLASH))
/** Test of space, dollar, backslash (possible EOL escape) or variable
 * assingment characters. */
#define KMK_CC_EVAL_IS_SPACE_DOLLAR_SLASH_OR_ASSIGN(a_ch)   \
    (KMK_CC_EVAL_BM_GET(g_abEvalCcChars, a_ch) & (KMK_CC_EVAL_CH_SPACE | KMK_CC_EVAL_CH_SPACE_VAR_OR_RECIPE | KMK_CC_EVAL_CH_DOLLAR))
/** @} */

/** Sets a bitmap entry.
 * @param   a_abBitmap      Typically g_abEvalCcChars.
 * @param   a_ch            The character to set.
 * @param   a_uVal          The value to OR in.  */
#define KMK_CC_EVAL_BM_OR(g_abBitmap, a_ch, a_uVal) do { (g_abBitmap)[(unsigned char)(a_ch)] |= (a_uVal); } while (0)

/** Gets a bitmap entry.
 * @returns The value corresponding to @a a_ch.
 * @param   a_abBitmap      Typically g_abEvalCcChars.
 * @param   a_ch            The character to set. */
#define KMK_CC_EVAL_BM_GET(g_abBitmap, a_ch)        ( (g_abBitmap)[(unsigned char)(a_ch)] )

/** @} */


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


/** @defgroup grp_kmk_cc_exp   String Expansion
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
    KMKCCEXPINSTR           enmOpcode;
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


/** @addtogroup grp_kmk_cc_evalprog
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

    /** export variable1 [variable2...] - KMKCCEVALVARIABLES. */
    kKmkCcEvalInstr_export,
    /** unexport variable1 [variable2...] - KMKCCEVALVARIABLES. */
    kKmkCcEvalInstr_unexport,
    /** export - KMKCCEVALCORE. */
    kKmkCcEvalInstr_export_all,
    /** unexport - KMKCCEVALCORE. */
    kKmkCcEvalInstr_unexport_all,
    /** [local|override] undefine - KMKCCEVALVARIABLES. */
    kKmkCcEvalInstr_undefine,


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
    KMKCCEVALINSTR          enmOpcode;
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
    /** Whether the 'export' qualifier was used. */
    uint8_t                 fExport;
    /** Whether the 'override' qualifier was used. */
    uint8_t                 fOverride;
    /** Whether the 'local' qualifier was used. */
    uint8_t                 fLocal;
    /** Whether the 'private' qualifier was used. */
    uint8_t                 fPrivate;
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
 * Instruction format for kKmkCcEvalInstr_export, kKmkCcEvalInstr_unexport and
 * kKmkCcEvalInstr_undefine.
 */
typedef struct kmk_cc_eval_variables
{
    /** The core instruction. */
    KMKCCEVALCORE           Core;
    /** The number of variables named in aVars. */
    uint32_t                cVars;
    /** Whether the 'local' qualifier was used (undefine only). */
    uint8_t                 fLocal;
    /** Pointer to the next instruction. */
    PKMKCCEVALCORE          pNext;
    /** The variable names.
     * Expressions will be expanded and split on space.
     * @remarks Plain text names are in variable_strcache. */
    KMKCCEXPSUBPROGORPLAIN  aVars[1];
} KMKCCEVALVARIABLES;
typedef KMKCCEVALVARIABLES *PKMKCCEVALVARIABLES;
/** Calculates the size of an KMKCCEVALVARIABLES structure for @a a_cVars. */
#define KMKCCEVALVARIABLES_SIZE(a_cVars) KMK_CC_SIZEOF_VAR_STRUCT(KMKCCEVALVARIABLES, aVars, a_cVars)

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
     * This is only to assist the compilation process. */
    struct kmk_cc_eval_if_core *pPrevCond;
    /** Pointer to the jump out of the true block, if followed by 'else'.
     * This is only to assist the compilation process. */
    PKMKCCEVALJUMP          pTrueEndJump;
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
    KMKCCEXPSUBPROG         NameSubprog;
} KMKCCEVALIFDEFDYNAMIC;
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
#define KMKCCEVALIFEXPR_SIZE(a_cchExpr) KMK_CC_BLOCK_ALIGN_SIZE(KMK_CC_SIZEOF_VAR_STRUCT(KMKCCEVALIFEXPR, szExpr, (a_cchExpr) + 1))

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
     * The KMKCCEXPSUBPROGORPLAIN::bUser member one of KMKCCEVALRECIPE_FD_XXX.
     *
     * @todo Plain text file name could be replaced by file string cache entries. */
    KMKCCEXPSUBPROGORPLAIN  aFilesAndDeps[1];
} KMKCCEVALRECIPE;
typedef KMKCCEVALRECIPE *PKMKCCEVALRECIPE;
/** Calculates the size of an KMKCCEVALRECIPE structure for @a a_cFiles
 *  files. */
#define KMKCCEVALRECIPE_SIZE(a_cFilesAndDeps) KMK_CC_SIZEOF_VAR_STRUCT(KMKCCEVALRECIPE, aFilesAndDeps, a_cFilesAndDeps)
/** @name KMKCCEVALRECIPE_FD_XXX - Values for KMKCCEVALRECIPE::aFilesAndDeps[x].bUser
 * @{  */
#define KMKCCEVALRECIPE_FD_NORMAL                   0
#define KMKCCEVALRECIPE_FD_SEC_EXP                  1
#define KMKCCEVALRECIPE_FD_SPECIAL_POSIX            2
#define KMKCCEVALRECIPE_FD_SPECIAL_SECONDEXPANSION  3
#define KMKCCEVALRECIPE_FD_SPECIAL_ONESHELL         4
/** @} */


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
    /** The name of the file containing this program. */
    const char             *pszFilename;
    /** The name of the variable containing this program, if applicable.  */
    const char             *pszVarName;
#ifdef KMK_CC_STRICT
    /** The hash of the input string.  Used to check that we get all the change
     * notifications we require. */
    uint32_t                uInputHash;
#endif
    /** Reference count. */
    uint32_t volatile       cRefs;
} KMKCCEVALPROG;
typedef KMKCCEVALPROG *PKMKCCEVALPROG;

/** @} */


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
static uint32_t g_cVarForExpandCompilations = 0;
static uint32_t g_cVarForExpandExecs = 0;
static uint32_t g_cVarForEvalCompilations = 0;
static uint32_t g_cVarForEvalExecs = 0;
static uint32_t g_cFileForEvalCompilations = 0;
static uint32_t g_cFileForEvalExecs = 0;
#ifdef KMK_CC_WITH_STATS
static uint32_t g_cBlockAllocated = 0;
static uint32_t g_cbAllocated = 0;

static uint32_t g_cBlocksAllocatedExpProgs = 0;
static uint32_t g_cbAllocatedExpProgs = 0;
static uint32_t g_cSingleBlockExpProgs = 0;
static uint32_t g_cTwoBlockExpProgs = 0;
static uint32_t g_cMultiBlockExpProgs = 0;
static uint32_t g_cbUnusedMemExpProgs = 0;

static uint32_t g_cBlocksAllocatedEvalProgs = 0;
static uint32_t g_cbAllocatedEvalProgs = 0;
static uint32_t g_cSingleBlockEvalProgs = 0;
static uint32_t g_cTwoBlockEvalProgs = 0;
static uint32_t g_cMultiBlockEvalProgs = 0;
static uint32_t g_cbUnusedMemEvalProgs = 0;

#endif

/** Generic character classification, taking an 'unsigned char' index.
 * ASSUMES unsigned char is 8-bits. */
static uint16_t g_abEvalCcChars[256];


/**
 * Makefile evaluation keywords.
 */
static const char * const g_apszEvalKeywords[] =
{
    "define",
    "export",
    "else",
    "endef",
    "endif",
    "ifdef",
    "ifndef",
    "ifeq",
    "ifneq",
    "if1of",
    "ifn1of",
    "if",
    "include",
    "includedep",
    "includedep-queue",
    "includedep-flush",
    "local",
    "override",
    "private",
    "sinclude",
    "unexport",
    "undefine",
    "vpath",
    "-include",
};


/** This is parallel to KMKCCEVALINSTR.   */
static const char * const g_apszEvalInstrNms[] =
{
    "jump",
    "assign_recursive",
    "assign_simple",
    "assign_append",
    "assign_prepend",
    "assign_if_new",
    "assign_define",
    "export",
    "unexport",
    "export_all",
    "unexport_all",
    "undefine",
    "ifdef_plain",
    "ifndef_plain",
    "ifdef_dynamic",
    "ifndef_dynamic",
    "ifeq",
    "ifneq",
    "if1of",
    "ifn1of",
    "if",
    "include",
    "include_silent",
    "includedep",
    "includedep_queue",
    "includedep_flush",
    "recipe_no_commands",
    "recipe_start_normal",
    "recipe_start_double_colon",
    "recipe_start_pattern",
    "recipe_commands",
    "recipe_end",
    "recipe_cancel_pattern",
    "vpath",
    "vpath_clear_pattern",
    "vpath_clear_all",
};

/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static int kmk_cc_exp_compile_subprog(PKMKCCBLOCK *ppBlockTail, const char *pchStr, uint32_t cchStr, PKMKCCEXPSUBPROG pSubprog);
static char *kmk_exec_expand_subprog_to_tmp(PKMKCCEXPSUBPROG pSubprog, uint32_t *pcch);


/**
 * Initializes global variables for the 'compiler'.
 */
void kmk_cc_init(void)
{
    unsigned i;

    /*
     * Initialize the bitmap.
     */
    memset(g_abEvalCcChars, 0, sizeof(g_abEvalCcChars));

    /* blank chars */
    KMK_CC_EVAL_BM_OR(g_abEvalCcChars, ' ',  KMK_CC_EVAL_CH_BLANK);
    KMK_CC_EVAL_BM_OR(g_abEvalCcChars, '\t', KMK_CC_EVAL_CH_BLANK);

    /* space chars and zero terminator. */
#define MY_SPACE_BITS KMK_CC_EVAL_CH_SPACE | KMK_CC_EVAL_CH_SPACE_OR_BACKSLASH | KMK_CC_EVAL_CH_SPACE_VAR_OR_RECIPE
    KMK_CC_EVAL_BM_OR(g_abEvalCcChars, ' ',  MY_SPACE_BITS);
    KMK_CC_EVAL_BM_OR(g_abEvalCcChars, '\t', MY_SPACE_BITS);
    KMK_CC_EVAL_BM_OR(g_abEvalCcChars, '\n', MY_SPACE_BITS | KMK_CC_EVAL_CH_EOL_CANDIDATE);
    KMK_CC_EVAL_BM_OR(g_abEvalCcChars, '\v', MY_SPACE_BITS);
    KMK_CC_EVAL_BM_OR(g_abEvalCcChars, '\f', MY_SPACE_BITS);
    KMK_CC_EVAL_BM_OR(g_abEvalCcChars, '\r', MY_SPACE_BITS | KMK_CC_EVAL_CH_EOL_CANDIDATE);
    KMK_CC_EVAL_BM_OR(g_abEvalCcChars, '\\', KMK_CC_EVAL_CH_SPACE_OR_BACKSLASH | KMK_CC_EVAL_CH_SPACE_VAR_OR_RECIPE);
#undef MY_SPACE_BITS

    /* keywords  */
    for (i = 0; i < K_ELEMENTS(g_apszEvalKeywords); i++)
    {
        size_t cch = strlen(g_apszEvalKeywords[i]);
        KMK_CC_ASSERT(cch >= KMK_CC_EVAL_KEYWORD_MIN);
        KMK_CC_ASSERT(cch <= KMK_CC_EVAL_KEYWORD_MAX);

        KMK_CC_EVAL_BM_OR(g_abEvalCcChars, g_apszEvalKeywords[i][0], KMK_CC_EVAL_CH_1ST_IN_KEYWORD);
        KMK_CC_EVAL_BM_OR(g_abEvalCcChars, g_apszEvalKeywords[i][1], KMK_CC_EVAL_CH_2ND_IN_KEYWORD);
    }
    KMK_CC_EVAL_BM_OR(g_abEvalCcChars, 'd', KMK_CC_EVAL_CH_1ST_IN_VARIABLE_KEYWORD); /* define */
    KMK_CC_EVAL_BM_OR(g_abEvalCcChars, 'e', KMK_CC_EVAL_CH_1ST_IN_VARIABLE_KEYWORD); /* export (, endef) */
    KMK_CC_EVAL_BM_OR(g_abEvalCcChars, 'l', KMK_CC_EVAL_CH_1ST_IN_VARIABLE_KEYWORD); /* local */
    KMK_CC_EVAL_BM_OR(g_abEvalCcChars, 'o', KMK_CC_EVAL_CH_1ST_IN_VARIABLE_KEYWORD); /* override */
    KMK_CC_EVAL_BM_OR(g_abEvalCcChars, 'p', KMK_CC_EVAL_CH_1ST_IN_VARIABLE_KEYWORD); /* private */
    KMK_CC_EVAL_BM_OR(g_abEvalCcChars, 'u', KMK_CC_EVAL_CH_1ST_IN_VARIABLE_KEYWORD); /* undefine, unexport */

    /* Assignment punctuation and recipe stuff. */
    KMK_CC_EVAL_BM_OR(g_abEvalCcChars, '=', KMK_CC_EVAL_CH_SPACE_VAR_OR_RECIPE);
    KMK_CC_EVAL_BM_OR(g_abEvalCcChars, ':', KMK_CC_EVAL_CH_SPACE_VAR_OR_RECIPE);

    /* For locating the end of variable expansion.  */
    KMK_CC_EVAL_BM_OR(g_abEvalCcChars, '(', KMK_CC_EVAL_CH_PAREN_OR_SLASH);
    KMK_CC_EVAL_BM_OR(g_abEvalCcChars, ')', KMK_CC_EVAL_CH_PAREN_OR_SLASH);
    KMK_CC_EVAL_BM_OR(g_abEvalCcChars, '{', KMK_CC_EVAL_CH_PAREN_OR_SLASH);
    KMK_CC_EVAL_BM_OR(g_abEvalCcChars, '}', KMK_CC_EVAL_CH_PAREN_OR_SLASH);
    KMK_CC_EVAL_BM_OR(g_abEvalCcChars, '\\', KMK_CC_EVAL_CH_PAREN_OR_SLASH);

    /* For parsing ifeq and if1of expressions. (GNU weirdly does not respect {} style function references.)  */
    KMK_CC_EVAL_BM_OR(g_abEvalCcChars, '(', KMK_CC_EVAL_CH_PAREN_COMMA_OR_DOLLAR);
    KMK_CC_EVAL_BM_OR(g_abEvalCcChars, ')', KMK_CC_EVAL_CH_PAREN_COMMA_OR_DOLLAR);
    KMK_CC_EVAL_BM_OR(g_abEvalCcChars, ',', KMK_CC_EVAL_CH_PAREN_COMMA_OR_DOLLAR);
    KMK_CC_EVAL_BM_OR(g_abEvalCcChars, '$', KMK_CC_EVAL_CH_PAREN_COMMA_OR_DOLLAR);

    /* Misc. */
    KMK_CC_EVAL_BM_OR(g_abEvalCcChars, '$',  KMK_CC_EVAL_CH_DOLLAR);
    KMK_CC_EVAL_BM_OR(g_abEvalCcChars, '\\', KMK_CC_EVAL_CH_BACKSLASH);

    /*
     * Check that the eval instruction names match up.
     */
    KMK_CC_ASSERT(strcmp(g_apszEvalInstrNms[kKmkCcEvalInstr_ifneq], "ifneq") == 0);
    KMK_CC_ASSERT(strcmp(g_apszEvalInstrNms[kKmkCcEvalInstr_vpath_clear_all], "vpath_clear_all") == 0);
}


/**
 * Prints stats (for kmk -p).
 */
void kmk_cc_print_stats(void)
{
#ifdef KMK_CC_WITH_STATS
    uint32_t const cEvalCompilations = g_cFileForEvalCompilations + g_cVarForEvalCompilations;
#endif

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
    puts("");
#endif
    printf(_("# Variables compiled for string eval:      %6u\n"), g_cVarForEvalCompilations);
    printf(_("# Variables string eval runs:              %6u\n"), g_cVarForEvalExecs);
    printf(_("# String evals runs per compile:           %6u\n"), g_cVarForEvalExecs / g_cVarForEvalCompilations);
    printf(_("# Files compiled:                          %6u\n"), g_cFileForEvalCompilations);
    printf(_("# Files runs:                              %6u\n"), g_cFileForEvalExecs);
    printf(_("# Files eval runs per compile:             %6u\n"), g_cFileForEvalExecs / g_cFileForEvalCompilations);
#ifdef KMK_CC_WITH_STATS
    printf(_("#         Single alloc block eval progs:   %6u (%u%%)\n"
             "#            Two alloc block eval progs:   %6u (%u%%)\n"
             "#  Three or more alloc block eval progs:   %6u (%u%%)\n"
             ),
           g_cSingleBlockEvalProgs, (uint32_t)((uint64_t)g_cSingleBlockEvalProgs * 100 / cEvalCompilations),
           g_cTwoBlockEvalProgs,    (uint32_t)((uint64_t)g_cTwoBlockEvalProgs    * 100 / cEvalCompilations),
           g_cMultiBlockEvalProgs,  (uint32_t)((uint64_t)g_cMultiBlockEvalProgs  * 100 / cEvalCompilations));
    printf(_("# Total amount of memory for eval progs: %8u bytes\n"
             "#                                    in:   %6u blocks\n"
             "#                        avg block size:   %6u bytes\n"
             "#                         unused memory: %8u bytes (%u%%)\n"
             "#           avg unused memory per block:   %6u bytes\n"
             "\n"),
           g_cbAllocatedEvalProgs, g_cBlocksAllocatedEvalProgs, g_cbAllocatedEvalProgs / g_cBlocksAllocatedEvalProgs,
           g_cbUnusedMemEvalProgs, (uint32_t)((uint64_t)g_cbUnusedMemEvalProgs * 100 / g_cbAllocatedEvalProgs),
           g_cbUnusedMemEvalProgs / g_cBlocksAllocatedEvalProgs);
    puts("");
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
    KMK_CC_ASSERT(cbFirst <= 128);

    /*
     * Turn the hint into a block size.
     */
    cbHint += cbFirst;
    if (cbHint <= 512)
    {
        if (cbHint <= 256)
        {
            if (cbFirst <= 64)
                cbBlock = 128;
            else
                cbBlock = 256;
        }
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
    if (pBlockTail->offNext & (sizeof(void *) - 1U))
    {
        pBlockTail->offNext = KMK_CC_BLOCK_ALIGN_SIZE(pBlockTail->offNext);
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
    uint32_t    cbLeft     = pBlockTail->cbBlock - pBlockTail->offNext;

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
    KMK_CC_ASSERT(((size_t)pRet & (sizeof(void *) - 1)) == 0);

    /* Emit jump. */
    pJump = (PKMKCCEXPJUMP)((char *)pOldBlock + pOldBlock->offNext);
    pJump->Core.enmOpcode = kKmkCcExpInstr_Jump;
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
        KMK_CC_ASSERT(((size_t)pRet & (sizeof(void *) - 1)) == 0);
        return pRet;
    }
    return kmk_cc_block_alloc_exp_grow(ppBlockTail, cb);
}


/**
 * Grows the allocation with another block, makefile evaluation program case.
 *
 * @returns Pointer to a makefile evaluation instruction core.
 * @param   ppBlockTail         Pointer to the allocator tail pointer.
 * @param   cb                  The number of bytes to allocate.
 */
static PKMKCCEVALCORE kmk_cc_block_alloc_eval_grow(PKMKCCBLOCK *ppBlockTail, uint32_t cb)
{
    PKMKCCBLOCK     pOldBlock = *ppBlockTail;
    PKMKCCBLOCK     pNewBlock;
    PKMKCCEVALCORE  pRet;
    PKMKCCEVALJUMP  pJump;

    /* Figure the block size. */
    uint32_t cbBlock = !pOldBlock->pNext ? 128 : pOldBlock->cbBlock;
    while (cbBlock - sizeof(KMKCCEVALJUMP) - sizeof(*pNewBlock) < cb)
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

    pRet = (PKMKCCEVALCORE)(pNewBlock + 1);

    /* Emit jump. */
    pJump = (PKMKCCEVALJUMP)((char *)pOldBlock + pOldBlock->offNext);
    pJump->Core.enmOpcode = kKmkCcEvalInstr_jump;
    pJump->pNext = pRet;
    pOldBlock->offNext += sizeof(*pJump);
    KMK_CC_ASSERT(pOldBlock->offNext <= pOldBlock->cbBlock);

    return pRet;
}


/**
 * Allocates a makefile evaluation instruction of size @a cb.
 *
 * @returns Pointer to a makefile evaluation instruction core.
 * @param   ppBlockTail         Pointer to the allocator tail pointer.
 * @param   cb                  The number of bytes to allocate.
 */
static PKMKCCEVALCORE kmk_cc_block_alloc_eval(PKMKCCBLOCK *ppBlockTail, uint32_t cb)
{
    PKMKCCBLOCK pBlockTail = *ppBlockTail;
    uint32_t    cbLeft = pBlockTail->cbBlock - pBlockTail->offNext;

    KMK_CC_ASSERT(cbLeft >= sizeof(KMKCCEVALJUMP));
    KMK_CC_ASSERT( (cb & (sizeof(void *) - 1)) == 0 );

    if (cbLeft >= cb + sizeof(KMKCCEVALJUMP))
    {
        PKMKCCEVALCORE pRet = (PKMKCCEVALCORE)((char *)pBlockTail + pBlockTail->offNext);
        pBlockTail->offNext += cb;
        return pRet;
    }
    return kmk_cc_block_alloc_eval_grow(ppBlockTail, cb);
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
    pCore->enmOpcode = kKmkCcExpInstr_Return;
    kmk_cc_block_realign(ppBlockTail);
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
    pInstr->FnCore.Core.enmOpcode = kKmkCcExpInstr_DynamicFunction;
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
    pInstr->FnCore.Core.enmOpcode = kKmkCcExpInstr_PlainFunction;
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
    pInstr->Core.enmOpcode = kKmkCcExpInstr_DynamicVariable;

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
            pInstr->Core.enmOpcode = kKmkCcExpInstr_PlainVariable;
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
            pInstr->Core.enmOpcode = kKmkCcExpInstr_SearchAndReplacePlainVariable;
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
        pInstr->Core.enmOpcode = kKmkCcExpInstr_CopyString;
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
                        KMK_CC_ASSERT(!func_char_map[(unsigned char)chOpen]);
                        KMK_CC_ASSERT(!func_char_map[(unsigned char)chClose]);
                        KMK_CC_ASSERT(!func_char_map[(unsigned char)'$']);
                        while (cchName < cchStr)
                        {
                            ch = pchStr[cchName];
                            if (!func_char_map[(unsigned char)ch])
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
                                fatal(NULL, _("Function '%s' takes a minimum of %d arguments: %d given"),
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
 * @param   pSubprog            The subprogram structure to initialize.
 */
static int kmk_cc_exp_compile_subprog(PKMKCCBLOCK *ppBlockTail, const char *pchStr, uint32_t cchStr, PKMKCCEXPSUBPROG pSubprog)
{
    KMK_CC_ASSERT(cchStr > 0);
    pSubprog->pFirstInstr = (PKMKCCEXPCORE)kmk_cc_block_get_next_ptr(*ppBlockTail);
    kmk_cc_exp_stats_init(&pSubprog->Stats);
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
        switch (pInstrCore->enmOpcode)
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
                      (int)pInstrCore->enmOpcode, (int)pInstrCore->enmOpcode);
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
 * @param   pSubprog          The subprogram to execute.
 * @param   pcchResult        Where to return the size of the result. Optional.
 */
static char *kmk_exec_expand_subprog_to_tmp(PKMKCCEXPSUBPROG pSubprog, uint32_t *pcchResult)
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
                                               pSubprog->Stats.cchAvg ? pSubprog->Stats.cchAvg + 32 : 256);

    pchDst = kmk_exec_expand_instruction_stream_to_var_buf(pSubprog->pFirstInstr, pchDst);

    /* Ensure that it's terminated. */
    pchDst = variable_buffer_output(pchDst, "\0", 1) - 1;

    /* Grab the result buffer before restoring the previous one. */
    pszResult = variable_buffer;
    cchResult = (uint32_t)(pchDst - pszResult);
    if (pcchResult)
        *pcchResult = cchResult;
    kmk_cc_exp_stats_update(&pSubprog->Stats, cchResult);

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

static size_t kmk_cc_eval_detect_eol_style(char *pchFirst, char *pchSecond, const char *pszContent, size_t cchContent)
{
    /* Look for LF first. */
    const char *pszTmp = (const char *)memchr(pszContent, '\n', cchContent);
    if (pszTmp)
    {
        /* CRLF? */
        if (pszTmp != pszContent && pszTmp[-1] == '\r')
        {
            *pchFirst = '\r';
            *pchSecond = '\n';
            return 2;
        }

        /* No, LF or LFCR. (pszContent is zero terminated, so no bounds checking necessary.) */
        *pchFirst = '\n';
        if (pszTmp[1] != '\r')
        {
            *pchSecond = 0;
            return 1;
        }
        *pchSecond = '\r';
        return 2;
    }

    /* Probably no EOLs here. */
    if (memchr(pszContent, '\r', cchContent) == NULL)
    {
        *pchSecond = *pchFirst = 0;
        return 0;
    }

    /* kind of unlikely */
    *pchFirst  = '\r';
    *pchSecond = 0;
    return 1;
}


#if 0
/**
 * Checks whether we've got an EOL escape sequence or not.
 *
 * @returns non-zero if escaped EOL, 0 if not (i.e. actual EOL).
 * @param   pszContent          The string pointer @a offEol is relative to.
 * @param   offEol              The offset of the first EOL char.
 */
static unsigned kmk_cc_eval_is_eol_escape_seq(const char *pszContent, size_t offEol)
{
    /* The caller has already checked out two backslashes. */
    size_t offFirstBackslash = offEol;
    KMK_CC_ASSERT(offFirstBackslash >= 2);
    offFirstBackslash -= 2;

    /* Find the first backslash. */
    while (offFirstBackslash > 0 && pszContent[offFirstBackslash - 1] == '\\')
        offFirstBackslash--;

    /* Odd number -> escaped EOL; Even number -> real EOL; */
    return (offEol - offFirstBackslash) & 1;
}
#endif



typedef enum kmk_cc_eval_token
{
    /** Invalid token . */
    kKmkCcEvalToken_Invalid = 0,

    /** Assignment: '=' */
    kKmkCcEvalToken_AssignRecursive,
    /** Assignment: ':=' */
    kKmkCcEvalToken_AssignSimple,
    /** Assignment: '+=' */
    kKmkCcEvalToken_AssignAppend,
    /** Assignment: '<=' */
    kKmkCcEvalToken_AssignPrepend,
    /** Assignment: '?=' */
    kKmkCcEvalToken_AssignIfNew,
    /** Assignment: 'define' */
    kKmkCcEvalToken_define,
    /** Unassignment: 'undefine' */
    kKmkCcEvalToken_undefine,

    /* Assignment modifier: 'local'  */
    kKmkCcEvalToken_local,
    /* Assignment modifier: 'override' */
    kKmkCcEvalToken_override,
    /* Assignment modifier: 'private' (target variable not inh by deps) */
    kKmkCcEvalToken_private,
    /* Assignment modifier / other variable thing: 'export' */
    kKmkCcEvalToken_export,
    /* Other variable thing: 'unexport' */
    kKmkCcEvalToken_unexport,

    kKmkCcEvalToken_ifdef,
    kKmkCcEvalToken_ifndef,
    kKmkCcEvalToken_ifeq,
    kKmkCcEvalToken_ifneq,
    kKmkCcEvalToken_if1of,
    kKmkCcEvalToken_ifn1of,
    kKmkCcEvalToken_if,
    kKmkCcEvalToken_else,
    kKmkCcEvalToken_endif,

    kKmkCcEvalToken_include,
    kKmkCcEvalToken_include_silent,
    kKmkCcEvalToken_includedep,
    kKmkCcEvalToken_includedep_queue,
    kKmkCcEvalToken_includedep_flush,

    kKmkCcEvalToken_colon,
    kKmkCcEvalToken_double_colon,
    kKmkCcEvalToken_plus,
    kKmkCcEvalToken_plus_maybe,

    kKmkCcEvalToken_vpath,

    /** Plain word. */
    kKmkCcEvalToken_WordPlain,
    /** Word that maybe in need of expanding. */
    kKmkCcEvalToken_WordWithDollar,

    kKmkCcEvalToken_End
} KMKCCEVALTOKEN;

/**
 * A tokenized word.
 */
typedef struct kmk_cc_eval_word
{
    /** The token word (lexeme).   */
    const char         *pchWord;
    /** The length of the word (lexeme). */
    uint32_t            cchWord;
    /** The token classification. */
    KMKCCEVALTOKEN      enmToken;
} KMKCCEVALWORD;
typedef KMKCCEVALWORD *PKMKCCEVALWORD;
typedef KMKCCEVALWORD const *PCKMKCCEVALWORD;


/**
 * Escaped end-of-line sequence in the current line.
 */
typedef struct KMKCCEVALESCEOL
{
    /** Offset at which the EOL escape sequence starts for a non-command line. */
    size_t              offEsc;
    /** Offset of the newline sequence. */
    size_t              offEol;
} KMKCCEVALESCEOL;
typedef KMKCCEVALESCEOL *PKMKCCEVALESCEOL;


/**
 * String copy segment.
 */
typedef struct KMKCCEVALSTRCPYSEG
{
    /** The start. */
    const char         *pchSrc;
    /** The number of chars to copy and whether to prepend space.
     * Negative values indicates that we should prepend a space. */
    ssize_t             cchSrcAndPrependSpace;
} KMKCCEVALSTRCPYSEG;
typedef KMKCCEVALSTRCPYSEG *PKMKCCEVALSTRCPYSEG;
typedef KMKCCEVALSTRCPYSEG const *PCKMKCCEVALSTRCPYSEG;


typedef struct KMKCCEVALCOMPILER
{
    /** Pointer to the KMKCCEVALPROG::pBlockTail member.  */
    PKMKCCBLOCK        *ppBlockTail;

    /** @name Line parsing state.
     * @{ */
    /** Offset of newline escape sequences in the current line.
     * This is only applicable if cEscEols is not zero.  */
    PKMKCCEVALESCEOL    paEscEols;
    /** The number of number of paEscEols entries we've allocated. */
    unsigned            cEscEolsAllocated;
    /** Number of escaped EOLs (line count - 1). */
    unsigned            cEscEols;
    /** The paEscEols entry corresponding to the current parsing location.
     * Still to be seen how accurate this can be made to be. */
    unsigned            iEscEol;

    /** The current line number (for error handling / debugging). */
    unsigned            iLine;
    /** The start offset of the current line. */
    size_t              offLine;
    /** Length of the current line, sans the final EOL and comments. */
    size_t              cchLine;
    /** Length of the current line, sans the final EOL but with comments. */
    size_t              cchLineWithComments;

    /** The first char in an EOL sequence.
     * We ASSUMES that this char won't appear in any other sequence in the file,
     * thus skipping matching any subsequent chars. */
    char                chFirstEol;
    /** The second char in an EOL sequence, if applicable. */
    char                chSecondEol;

    /** The length of the EOL sequence. */
    size_t              cchEolSeq;
    /** The minimum length of an esacped EOL sequence (cchEolSeq + 1). */
    size_t              cchEscEolSeq;

    /** String copy segments. */
    PKMKCCEVALSTRCPYSEG paStrCopySegs;
    /** The number of segments that has been prepared. */
    unsigned            cStrCopySegs;
    /** The number of segments we've allocated. */
    unsigned            cStrCopySegsAllocated;
    /** @} */


    /** @name Recipe state.
     * @{ */
    /** Set if we're working on a recipe. */
    PKMKCCEVALRECIPE    pRecipe;
    /** Set for ignoring recipes without targets (Sun OS 4 Make). */
    uint8_t             fNoTargetRecipe;
    /** The command prefix character. */
    char                chCmdPrefix;
    /** @} */

    /** @name Tokenzied words.
     * @{ */
    uint32_t            cWords;
    uint32_t            cWordsAllocated;
    PKMKCCEVALWORD      paWords;
    /** @} */

    /** @name Conditionals.
     * @{ */
    /** Current conditional stack depth. */
    unsigned            cIfs;
    /** The conditional directive stack. */
    PKMKCCEVALIFCORE    apIfs[KMK_CC_EVAL_MAX_IF_DEPTH];
    /** @} */

    /** The program being compiled. */
    PKMKCCEVALPROG      pEvalProg;
    /** Pointer to the content. */
    const char         *pszContent;
    /** The amount of input to parse. */
    size_t              cchContent;
} KMKCCEVALCOMPILER;
typedef KMKCCEVALCOMPILER *PKMKCCEVALCOMPILER;


static void kmk_cc_eval_init_compiler(PKMKCCEVALCOMPILER pCompiler, PKMKCCEVALPROG pEvalProg, unsigned iLine,
                                      const char *pszContent, size_t cchContent)
{
    pCompiler->ppBlockTail      = &pEvalProg->pBlockTail;

    pCompiler->pRecipe          = NULL;
    pCompiler->fNoTargetRecipe  = 0;
    pCompiler->chCmdPrefix      = cmd_prefix;

    pCompiler->cWordsAllocated  = 0;
    pCompiler->paWords          = NULL;

    pCompiler->cEscEolsAllocated = 0;
    pCompiler->paEscEols        = NULL;
    pCompiler->iLine            = iLine;

    pCompiler->cStrCopySegsAllocated = 0;
    pCompiler->paStrCopySegs    = NULL;

    pCompiler->cIfs             = 0;

    pCompiler->pEvalProg        = pEvalProg;
    pCompiler->pszContent       = pszContent;
    pCompiler->cchContent       = cchContent;

    /* Detect EOL style. */
    pCompiler->cchEolSeq        = kmk_cc_eval_detect_eol_style(&pCompiler->chFirstEol, &pCompiler->chSecondEol,
                                                               pszContent, cchContent);
    pCompiler->cchEscEolSeq     = 1 + pCompiler->cchEolSeq;
}


static void kmk_cc_eval_delete_compiler(PKMKCCEVALCOMPILER pCompiler)
{
    if (pCompiler->paWords)
        free(pCompiler->paWords);
    if (pCompiler->paEscEols)
        free(pCompiler->paEscEols);
}

static void KMK_CC_FN_NO_RETURN kmk_cc_eval_fatal(PKMKCCEVALCOMPILER pCompiler, const char *pchWhere, const char *pszMsg, ...)
{
    va_list  va;
    unsigned iLine = pCompiler->iLine;

    log_working_directory(1);

    /*
     * If we have a pointer location, use it to figure out the exact line and column.
     */
    if (pchWhere)
    {
        size_t   offLine = pCompiler->offLine;
        size_t   off     = pchWhere - pCompiler->pszContent;
        unsigned i       = 0;
        while (   i   < pCompiler->cEscEols
               && off > pCompiler->paEscEols[i].offEol)
        {
            offLine = pCompiler->paEscEols[i].offEol + 1 + pCompiler->cchEolSeq;
            iLine++;
            i++;
        }
        KMK_CC_ASSERT(off <= pCompiler->cchContent);

        if (pCompiler->pEvalProg->pszVarName)
            fprintf(stderr, "%s:%u:%u: *** fatal parsing error in %s: ",
                    pCompiler->pEvalProg->pszFilename, iLine, (unsigned)(off - offLine), pCompiler->pEvalProg->pszVarName);
        else
            fprintf(stderr, "%s:%u:%u: *** fatal parsing error: ",
                    pCompiler->pEvalProg->pszFilename, iLine, (unsigned)(off - offLine));
    }
    else if (pCompiler->pEvalProg->pszVarName)
        fprintf(stderr, "%s:%u: *** fatal parsing error in %s: ",
                pCompiler->pEvalProg->pszFilename, iLine, pCompiler->pEvalProg->pszVarName);
    else
        fprintf(stderr, "%s:%u: *** fatal parsing error: ",
                pCompiler->pEvalProg->pszFilename, iLine);

    /*
     * Print the message and die.
     */
    va_start(va, pszMsg);
    vfprintf(stderr, pszMsg, va);
    va_end(va);
    fputs(".  Stop.\n", stderr);

    for (;;)
        die(2);
}


static KMK_CC_FN_NO_RETURN void
kmk_cc_eval_fatal_eol(PKMKCCEVALCOMPILER pCompiler, const char *pchEol, unsigned iLine, size_t offLine)
{
    pCompiler->iLine   = iLine;
    pCompiler->offLine = offLine;

    for (;;)
        kmk_cc_eval_fatal(pCompiler, pchEol, "Missing 2nd EOL character: found %#x instead of %#x\n",
                                   pchEol, pCompiler->chSecondEol);
}


static void kmk_cc_eval_warn(PKMKCCEVALCOMPILER pCompiler, const char *pchWhere, const char *pszMsg, ...)
{
    /** @todo warnings.   */
    (void)pchWhere;
    (void)pCompiler;
    (void)pszMsg;
}


/**
 * Compiles a string expansion subprogram.
 *
 * @param   pCompiler   The compiler state.
 * @param   pszExpr     The expression to compile.
 * @param   cchExpr     The length of the expression.
 * @param   pSubprog    The subprogram to compile.
 */
static void kmk_cc_eval_compile_string_exp_subprog(PKMKCCEVALCOMPILER pCompiler, const char *pszExpr, size_t cchExpr,
                                                   PKMKCCEXPSUBPROG pSubprog)
{
    int rc = kmk_cc_exp_compile_subprog(pCompiler->ppBlockTail, pszExpr, cchExpr, pSubprog);
    if (rc == 0)
        return;
    kmk_cc_eval_fatal(pCompiler, NULL, "String expansion compile error");
}


/**
 * Initializes a subprogam or plain operand structure.
 *
 * @param   pCompiler   The compiler state.
 * @param   pOperand    The subprogram or plain structure to init.
 * @param   pszString   The string.
 * @param   cchString   The length of the string.
 * @param   fPlain      Whether it's plain or not.  If not, we'll compile it.
 */
static void kmk_cc_eval_init_subprogram_or_plain(PKMKCCEVALCOMPILER pCompiler, PKMKCCEXPSUBPROGORPLAIN pOperand,
                                                 const char *pszString, size_t cchString, int fPlain)
{
    pOperand->fPlainIsInVarStrCache  = 0;
    pOperand->bUser                  = 0;
    pOperand->bUser2                 = 0;
    pOperand->fSubprog               = fPlain;
    if (fPlain)
    {
        pOperand->u.Plain.cch = cchString;
        pOperand->u.Plain.psz = pszString;
    }
    else
        kmk_cc_eval_compile_string_exp_subprog(pCompiler, pszString, cchString, &pOperand->u.Subprog);
}

/**
 * Initializes an array of subprogram-or-plain (spp) operands from a word array.
 *
 * The words will be duplicated and the caller must therefore call
 * kmk_cc_block_realign() when done (it's not done here as the caller may
 * initialize several string operands and we don't want any unnecessary
 * fragmentation).
 *
 * @param   pCompiler   The compiler state.
 * @param   cWords      The number of words to copy.
 * @param   paSrc       The source words.
 * @param   paDst       The destination subprogram-or-plain array.
 */
static void kmk_cc_eval_init_spp_array_from_duplicated_words(PKMKCCEVALCOMPILER pCompiler, unsigned cWords,
                                                             PKMKCCEVALWORD paSrc, PKMKCCEXPSUBPROGORPLAIN paDst)
{
    unsigned i;
    for (i = 0; i < cWords; i++)
    {
        const char *pszCopy = kmk_cc_block_strdup(pCompiler->ppBlockTail, paSrc[i].pchWord, paSrc[i].cchWord);
        paDst[i].fPlainIsInVarStrCache  = 0;
        paDst[i].bUser                  = 0;
        paDst[i].bUser2                 = 0;
        if (paSrc[i].enmToken == kKmkCcEvalToken_WordWithDollar)
        {
            paDst[i].fSubprog    = 1;
            kmk_cc_eval_compile_string_exp_subprog(pCompiler, pszCopy, paSrc[i].cchWord, &paDst[i].u.Subprog);
        }
        else
        {
            paDst[i].fSubprog    = 0;
            paDst[i].u.Plain.cch = paSrc[i].cchWord;
            paDst[i].u.Plain.psz = pszCopy;
        }
        KMK_CC_EVAL_DPRINTF(("  %s\n", pszCopy));
    }
}



/** @name KMK_CC_WORD_COMP_CONST_XXX - Optimal(/insane) constant work matching.
 * @{
 */
#if (defined(KBUILD_ARCH_X86) || defined(KBUILD_ARCH_AMD64)) /* Unaligned access is reasonably cheap. */ \
 && !defined(GCC_ADDRESS_SANITIZER)
# define KMK_CC_WORD_COMP_CONST_2(a_pchLine, a_pszWord) \
        (   *(uint16_t const *)(a_pchLine)     == *(uint16_t const *)(a_pszWord) )
# define KMK_CC_WORD_COMP_CONST_3(a_pchLine, a_pszWord) \
        (   *(uint16_t const *)(a_pchLine)     == *(uint16_t const *)(a_pszWord) \
         && (a_pchLine)[2]                     == (a_pszWord)[2] )
# define KMK_CC_WORD_COMP_CONST_4(a_pchLine, a_pszWord) \
        (   *(uint32_t const *)(a_pchLine)     == *(uint32_t const *)(a_pszWord) )
# define KMK_CC_WORD_COMP_CONST_5(a_pchLine, a_pszWord) \
        (   *(uint32_t const *)(a_pchLine)     == *(uint32_t const *)(a_pszWord) \
         && (a_pchLine)[4]                     == (a_pszWord)[4] )
# define KMK_CC_WORD_COMP_CONST_6(a_pchLine, a_pszWord) \
        (   *(uint32_t const *)(a_pchLine)     == *(uint32_t const *)(a_pszWord) \
         && ((uint16_t const *)(a_pchLine))[2] == ((uint32_t const *)(a_pszWord))[2] )
# define KMK_CC_WORD_COMP_CONST_7(a_pchLine, a_pszWord) \
        (   *(uint32_t const *)(a_pchLine)     == *(uint32_t const *)(a_pszWord) \
         && ((uint16_t const *)(a_pchLine))[2] == ((uint32_t const *)(a_pszWord))[2] \
         && (a_pchLine)[6]                     == (a_pszWord)[6] )
# define KMK_CC_WORD_COMP_CONST_8(a_pchLine, a_pszWord) \
        (   *(uint64_t const *)(a_pchLine)     == *(uint64_t const *)(a_pszWord) )
# define KMK_CC_WORD_COMP_CONST_10(a_pchLine, a_pszWord) \
        (   *(uint64_t const *)(a_pchLine)     == *(uint64_t const *)(a_pszWord) \
         && ((uint16_t const *)(a_pchLine))[4] == ((uint16_t const *)(a_pszWord))[4] )
# define KMK_CC_WORD_COMP_CONST_16(a_pchLine, a_pszWord) \
        (   *(uint64_t const *)(a_pchLine)     == *(uint64_t const *)(a_pszWord) \
         && ((uint64_t const *)(a_pchLine))[1] == ((uint64_t const *)(a_pszWord))[1] )
#else
# define KMK_CC_WORD_COMP_CONST_2(a_pchLine, a_pszWord) \
        (   (a_pchLine)[0] == (a_pszWord)[0] \
         && (a_pchLine)[1] == (a_pszWord)[1] )
# define KMK_CC_WORD_COMP_CONST_3(a_pchLine, a_pszWord) \
        (   (a_pchLine)[0] == (a_pszWord)[0] \
         && (a_pchLine)[1] == (a_pszWord)[1] \
         && (a_pchLine)[2] == (a_pszWord)[2] )
# define KMK_CC_WORD_COMP_CONST_4(a_pchLine, a_pszWord) \
        (   (a_pchLine)[0] == (a_pszWord)[0] \
         && (a_pchLine)[1] == (a_pszWord)[1] \
         && (a_pchLine)[2] == (a_pszWord)[2] \
         && (a_pchLine)[3] == (a_pszWord)[3] )
# define KMK_CC_WORD_COMP_CONST_5(a_pchLine, a_pszWord) \
        (   (a_pchLine)[0] == (a_pszWord)[0] \
         && (a_pchLine)[1] == (a_pszWord)[1] \
         && (a_pchLine)[2] == (a_pszWord)[2] \
         && (a_pchLine)[3] == (a_pszWord)[3] \
         && (a_pchLine)[4] == (a_pszWord)[4] )
# define KMK_CC_WORD_COMP_CONST_6(a_pchLine, a_pszWord) \
        (   (a_pchLine)[0] == (a_pszWord)[0] \
         && (a_pchLine)[1] == (a_pszWord)[1] \
         && (a_pchLine)[2] == (a_pszWord)[2] \
         && (a_pchLine)[3] == (a_pszWord)[3] \
         && (a_pchLine)[4] == (a_pszWord)[4] \
         && (a_pchLine)[5] == (a_pszWord)[5] )
# define KMK_CC_WORD_COMP_CONST_7(a_pchLine, a_pszWord) \
        (   (a_pchLine)[0] == (a_pszWord)[0] \
         && (a_pchLine)[1] == (a_pszWord)[1] \
         && (a_pchLine)[2] == (a_pszWord)[2] \
         && (a_pchLine)[3] == (a_pszWord)[3] \
         && (a_pchLine)[4] == (a_pszWord)[4] \
         && (a_pchLine)[5] == (a_pszWord)[5] \
         && (a_pchLine)[6] == (a_pszWord)[6] )
# define KMK_CC_WORD_COMP_CONST_8(a_pchLine, a_pszWord) \
        (   (a_pchLine)[0] == (a_pszWord)[0] \
         && (a_pchLine)[1] == (a_pszWord)[1] \
         && (a_pchLine)[2] == (a_pszWord)[2] \
         && (a_pchLine)[3] == (a_pszWord)[3] \
         && (a_pchLine)[4] == (a_pszWord)[4] \
         && (a_pchLine)[5] == (a_pszWord)[5] \
         && (a_pchLine)[6] == (a_pszWord)[6] \
         && (a_pchLine)[7] == (a_pszWord)[7] )
# define KMK_CC_WORD_COMP_CONST_10(a_pchLine, a_pszWord) \
        (   (a_pchLine)[0] == (a_pszWord)[0] \
         && (a_pchLine)[1] == (a_pszWord)[1] \
         && (a_pchLine)[2] == (a_pszWord)[2] \
         && (a_pchLine)[3] == (a_pszWord)[3] \
         && (a_pchLine)[4] == (a_pszWord)[4] \
         && (a_pchLine)[5] == (a_pszWord)[5] \
         && (a_pchLine)[6] == (a_pszWord)[6] \
         && (a_pchLine)[7] == (a_pszWord)[7] \
         && (a_pchLine)[8] == (a_pszWord)[8] \
         && (a_pchLine)[9] == (a_pszWord)[9] )
# define KMK_CC_WORD_COMP_CONST_16(a_pchLine, a_pszWord) \
        (   (a_pchLine)[0] == (a_pszWord)[0] \
         && (a_pchLine)[1] == (a_pszWord)[1] \
         && (a_pchLine)[2] == (a_pszWord)[2] \
         && (a_pchLine)[3] == (a_pszWord)[3] \
         && (a_pchLine)[4] == (a_pszWord)[4] \
         && (a_pchLine)[5] == (a_pszWord)[5] \
         && (a_pchLine)[6] == (a_pszWord)[6] \
         && (a_pchLine)[7] == (a_pszWord)[7] \
         && (a_pchLine)[8] == (a_pszWord)[8] \
         && (a_pchLine)[9] == (a_pszWord)[9] \
         && (a_pchLine)[10] == (a_pszWord)[10] \
         && (a_pchLine)[11] == (a_pszWord)[11] \
         && (a_pchLine)[12] == (a_pszWord)[12] \
         && (a_pchLine)[13] == (a_pszWord)[13] \
         && (a_pchLine)[14] == (a_pszWord)[14] \
         && (a_pchLine)[15] == (a_pszWord)[15])
#endif

/** See if the given string match a constant string. */
#define KMK_CC_STRCMP_CONST(a_pchLeft, a_cchLeft, a_pszConst, a_cchConst) \
    (   (a_cchLeft) == (a_cchConst) \
     && KMK_CC_WORD_COMP_CONST_##a_cchConst(a_pchLeft, a_pszConst) )

/** See if a starting of a given length starts with a constant word. */
#define KMK_CC_EVAL_WORD_COMP_IS_EOL(a_pCompiler, a_pchLine, a_cchLine) \
    (   (a_cchLine) == 0 \
     || KMK_CC_EVAL_IS_SPACE((a_pchLine)[0]) \
     || ((a_pchLine)[0] == '\\' && (a_pchLine)[1] == (a_pCompiler)->chFirstEol) ) \

/** See if a starting of a given length starts with a constant word. */
#define KMK_CC_EVAL_WORD_COMP_CONST(a_pCompiler, a_pchLine, a_cchLine, a_pszWord, a_cchWord) \
    (    (a_cchLine) >= (a_cchWord) \
      && (   (a_cchLine) == (a_cchWord) \
          || KMK_CC_EVAL_IS_SPACE((a_pchLine)[a_cchWord]) \
          || ((a_pchLine)[a_cchWord] == '\\' && (a_pchLine)[(a_cchWord) + 1] == (a_pCompiler)->chFirstEol) ) \
      && KMK_CC_WORD_COMP_CONST_##a_cchWord(a_pchLine, a_pszWord) )
/** @} */


/**
 * Checks if a_ch is a space after a word.
 *
 * Since there is always a terminating zero, the user can safely access a char
 * beyond @a a_cchLeft.  However, that byte isn't necessarily a zero terminator
 * character, so we have to check @a a_cchLeft whether we're at the end of the
 * parsing input string.
 *
 * @returns true / false.
 * @param   a_pCompiler     The compiler instance data.
 * @param   a_ch            The character to inspect.
 * @param   a_ch2           The character following it, in case of escaped EOL.
 * @param   a_cchLeft       The number of chars left to parse (from @a a_ch).
 */
#define KMK_CC_EVAL_IS_SPACE_AFTER_WORD(a_pCompiler, a_ch, a_ch2, a_cchLeft) \
    (   a_cchLeft == 0 \
     || KMK_CC_EVAL_IS_SPACE(a_ch) \
     || ((a_ch) == '\\' && (a_ch2) == (a_pCompiler)->chFirstEol) )


/**
 * Common path for space skipping worker functions when escaped EOLs may be
 * involed.
 *
 * @returns Points to the first non-space character or end of input.
 * @param   pchWord         The current position. There is some kind of char
 * @param   cchLeft         The current number of chars left to parse in the
 *                          current line.
 * @param   pcchLeft        Where to store the updated @a cchLeft value.
 * @param   pCompiler       The compiler instance data.
 */
static const char *kmk_cc_eval_skip_spaces_with_esc_eol(const char *pchWord, size_t cchLeft, size_t *pcchLeft,
                                                        PKMKCCEVALCOMPILER pCompiler)
{
    /*
     * Skip further spaces. We unrolls 4 loops here.
     * ASSUMES cchEscEolSeq is either 2 or 3!
     */
    KMK_CC_ASSERT(pCompiler->cchEscEolSeq == 2 || pCompiler->cchEscEolSeq == 3);
    KMK_CC_ASSERT(pCompiler->iEscEol < pCompiler->cEscEols);
    while (cchLeft >= 4)
    {
        /* First char. */
        char ch = pchWord[0];
        if (KMK_CC_EVAL_IS_SPACE(ch))
        { /* maybe likely */ }
        else if (   ch == '\\'
                 && pchWord[1] == pCompiler->chFirstEol)
        {
            pchWord += pCompiler->cchEscEolSeq;
            cchLeft -= pCompiler->cchEscEolSeq;
            pCompiler->iEscEol++;
            continue;
        }
        else
        {
            *pcchLeft = cchLeft;
            return pchWord;
        }

        /* Second char. */
        ch = pchWord[1];
        if (KMK_CC_EVAL_IS_SPACE(ch))
        { /* maybe likely */ }
        else if (   ch == '\\'
                 && pchWord[2] == pCompiler->chFirstEol)
        {
            pchWord += 1 + pCompiler->cchEscEolSeq;
            cchLeft -= 1 + pCompiler->cchEscEolSeq;
            pCompiler->iEscEol++;
            continue;
        }
        else
        {
            *pcchLeft = cchLeft - 1;
            return pchWord + 1;
        }

        /* Third char. */
        ch = pchWord[2];
        if (KMK_CC_EVAL_IS_SPACE(ch))
        { /* maybe likely */ }
        else if (   ch == '\\'
                 && pchWord[3] == pCompiler->chFirstEol
                 && cchLeft >= 2 + pCompiler->cchEscEolSeq)
        {
            pchWord += 2 + pCompiler->cchEscEolSeq;
            cchLeft -= 2 + pCompiler->cchEscEolSeq;
            pCompiler->iEscEol++;
            continue;
        }
        else
        {
            *pcchLeft = cchLeft - 2;
            return pchWord + 2;
        }

        /* Third char. */
        ch = pchWord[3];
        if (KMK_CC_EVAL_IS_SPACE(ch))
        {
            pchWord += 4;
            cchLeft -= 4;
        }
        else if (   ch == '\\'
                 && cchLeft >= 3 + pCompiler->cchEscEolSeq
                 && pchWord[4] == pCompiler->chFirstEol)
        {
            pchWord += 3 + pCompiler->cchEscEolSeq;
            cchLeft -= 3 + pCompiler->cchEscEolSeq;
            pCompiler->iEscEol++;
        }
        else
        {
            *pcchLeft = cchLeft - 3;
            return pchWord + 3;
        }
    }

    /*
     * Simple loop for the final three chars.
     */
    while (cchLeft > 0)
    {
        /* First char. */
        char ch = *pchWord;
        if (KMK_CC_EVAL_IS_SPACE(ch))
        {
            pchWord += 1;
            cchLeft -= 1;
        }
        else if (   ch == '\\'
                 && cchLeft > pCompiler->cchEolSeq
                 && pchWord[1] == pCompiler->chFirstEol)
        {
            pchWord += pCompiler->cchEscEolSeq;
            cchLeft -= pCompiler->cchEscEolSeq;
            pCompiler->iEscEol++;
        }
        else
            break;
    }

    *pcchLeft = cchLeft;
    return pchWord;
}


/**
 * Common path for space skipping worker functions when no escaped EOLs need
 * considering.
 *
 * @returns Points to the first non-space character or end of input.
 * @param   pchWord         The current position. There is some kind of char
 * @param   cchLeft         The current number of chars left to parse in the
 *                          current line.
 * @param   pcchLeft        Where to store the updated @a cchLeft value.
 * @param   pCompiler       The compiler instance data.
 */
static const char *kmk_cc_eval_skip_spaces_without_esc_eol(const char *pchWord, size_t cchLeft, size_t *pcchLeft,
                                                           PKMKCCEVALCOMPILER pCompiler)
{
    /*
     * 4x loop unroll.
     */
    while (cchLeft >= 4)
    {
        if (KMK_CC_EVAL_IS_SPACE(pchWord[0]))
        {
            if (KMK_CC_EVAL_IS_SPACE(pchWord[1]))
            {
                if (KMK_CC_EVAL_IS_SPACE(pchWord[2]))
                {
                    if (KMK_CC_EVAL_IS_SPACE(pchWord[3]))
                    {
                        pchWord += 4;
                        cchLeft -= 4;
                    }
                    else
                    {
                        *pcchLeft = cchLeft - 3;
                        return pchWord + 3;
                    }
                }
                else
                {
                    *pcchLeft = cchLeft - 2;
                    return pchWord + 2;
                }
            }
            else
            {
                *pcchLeft = cchLeft - 1;
                return pchWord + 1;
            }
        }
        else
        {
            *pcchLeft = cchLeft;
            return pchWord;
        }
    }

    /*
     * The last 3. Not entirely sure if this yield good code.
     */
    switch (cchLeft & 3)
    {
        case 3:
            if (!KMK_CC_EVAL_IS_SPACE(*pchWord))
                break;
            pchWord++;
            cchLeft--;
        case 2:
            if (!KMK_CC_EVAL_IS_SPACE(*pchWord))
                break;
            pchWord++;
            cchLeft--;
        case 1:
            if (!KMK_CC_EVAL_IS_SPACE(*pchWord))
                break;
            pchWord++;
            cchLeft--;
        case 0:
            break;
    }

    *pcchLeft = cchLeft;
    return pchWord;
}


/**
 * Used to skip spaces after a word.
 *
 * We ASSUME that the first char is a space or that we've reached the end of the
 * string (a_cchLeft == 0).
 *
 * @param   a_pCompiler     The compiler instance data.
 * @param   a_pchWord       The current input position, this will be moved to
 *                          the start of the next word or end of the input.
 * @param   a_cchLeft       The number of chars left to parse.  This will be
 *                          updated.
 */
#define KMK_CC_EVAL_SKIP_SPACES_AFTER_WORD(a_pCompiler, a_pchWord, a_cchLeft) \
    do { \
        /* Skip the first char which is known to be a space, end of line or end of input. */ \
        if ((a_cchLeft) > 0) \
        { \
            char const chSkipBlanksFirst = *(a_pchWord); \
            KMK_CC_ASSERT(KMK_CC_EVAL_IS_SPACE_AFTER_WORD(a_pCompiler, chSkipBlanksFirst, (a_pchWord)[1], a_cchLeft)); \
            if (chSkipBlanksFirst != '\\') \
            { \
                (a_pchWord) += 1; \
                (a_cchLeft) -= 1; \
                \
                /* Another space or escaped EOL? Then there are probably more then, so call worker function. */ \
                if ((a_cchLeft) > 0) \
                { \
                    char const chSkipBlanksSecond = *(a_pchWord); \
                    if (KMK_CC_EVAL_IS_SPACE_OR_BACKSLASH(chSkipBlanksSecond)) \
                        (a_pchWord) = kmk_cc_eval_skip_spaces_after_word_slow(a_pchWord, &(a_cchLeft), \
                                                                              chSkipBlanksSecond, a_pCompiler); \
                } \
            } \
            else /* escape sequences can be complicated. */ \
                (a_pchWord) = kmk_cc_eval_skip_spaces_after_word_slow(a_pchWord, &(a_cchLeft), \
                                                                      chSkipBlanksFirst, a_pCompiler); \
        } \
    } while (0)

/**
 * The slow path of KMK_CC_EVAL_SKIP_SPACES_AFTER_WORD.
 *
 * This is called to handles escaped EOL sequences, as these can involve
 * multiple backslashes and therefore doesn't led themselves well to inlined
 * code.
 *
 * The other case this is used for is to handle more than once space, since it's
 * likely that when there are two there might be more.  No point in inlining
 * that, better do some loop unrolling instead.
 *
 * @returns Points to the first non-space character or end of input.
 * @param   pchWord             The current position. There is some kind of char
 * @param   pcchLeft            Pointer to the cchLeft variable, this is both
 *                              input and output.
 * @param   ch                  The current character.
 * @param   pCompiler           The compiler instance data.
 */
static const char *kmk_cc_eval_skip_spaces_after_word_slow(const char *pchWord, size_t *pcchLeft, char ch,
                                                           PKMKCCEVALCOMPILER pCompiler)
{
    size_t cchLeft = *pcchLeft;

    /*
     * It's all very simple when we don't have to consider escaped EOLs.
     */
    if (pCompiler->iEscEol >= pCompiler->cEscEols)
    {
        if (ch != '\\')
        {
            pchWord += 1;
            cchLeft -= 1;
        }
        else
            return pchWord;
        return kmk_cc_eval_skip_spaces_without_esc_eol(pchWord, cchLeft, pcchLeft, pCompiler);
    }

    /*
     * Skip the pending space or EOL found by the caller.  We need to
     * confirm the EOL.
     *
     * Note! We only need to care about simple backslash+EOL sequences here
     *       since we're either at the end of a validated word, or we've already
     *       skipped one space.  In the former case, someone else has already
     *       validated the escape esequence, in the latter case multiple
     *       backslashes would indicate a new word that that we should return.
     */
    if (ch != '\\')
    {
        pchWord += 1;
        cchLeft -= 1;
    }
    else if (   cchLeft >= pCompiler->cchEscEolSeq
             && pchWord[1] == pCompiler->chFirstEol)
    {
        KMK_CC_ASSERT(pCompiler->cchEolSeq == 1 || pchWord[2] == pCompiler->chSecondEol);
        pchWord += pCompiler->cchEscEolSeq;
        cchLeft -= pCompiler->cchEscEolSeq;
        pCompiler->iEscEol++;

        if (pCompiler->iEscEol < pCompiler->cEscEols)
        { /* likely */ }
        else return kmk_cc_eval_skip_spaces_without_esc_eol(pchWord, cchLeft, pcchLeft, pCompiler);
    }
    else
        return pchWord;
    return kmk_cc_eval_skip_spaces_with_esc_eol(pchWord, cchLeft, pcchLeft, pCompiler);
}


/**
 * Skip zero or more spaces.
 *
 * This macro deals with a single space, if there are more or we're hittin some
 * possible escaped EOL sequence, work is deferred to a worker function.
 *
 * @param   a_pCompiler The compiler state.
 * @param   a_pchWord   The current input position. Advanced past spaces.
 * @param   a_cchLeft   The amount of input left to parse. Will be updated.
 */
#define KMK_CC_EVAL_SKIP_SPACES(a_pCompiler, a_pchWord, a_cchLeft) \
    do { \
        if ((a_cchLeft) > 0) \
        { \
            char chSkipSpaces = *(a_pchWord); \
            if (KMK_CC_EVAL_IS_SPACE_OR_BACKSLASH(chSkipSpaces)) \
            { \
                if (chSkipSpaces != '\\') \
                { \
                    (a_pchWord) += 1; \
                    (a_cchLeft) -= 1; \
                    chSkipSpaces = *(a_pchWord); \
                    if (KMK_CC_EVAL_IS_SPACE_OR_BACKSLASH(chSkipSpaces)) \
                        (a_pchWord) = kmk_cc_eval_skip_spaces_slow(a_pchWord, &(a_cchLeft), chSkipSpaces, a_pCompiler); \
                } \
                else \
                    (a_pchWord) = kmk_cc_eval_skip_spaces_slow(a_pchWord, &(a_cchLeft), chSkipSpaces, a_pCompiler); \
            } \
        } \
    } while (0)


/**
 * Worker for KMK_CC_EVAL_SKIP_SPACES.
 *
 * @returns Points to the first non-space character or end of input.
 * @param   pchWord             The current position. There is some kind of char
 * @param   pcchLeft            Pointer to the cchLeft variable, this is both
 *                              input and output.
 * @param   ch                  The current character.
 * @param   pCompiler           The compiler instance data.
 */
static const char *kmk_cc_eval_skip_spaces_slow(const char *pchWord, size_t *pcchLeft, char ch, PKMKCCEVALCOMPILER pCompiler)
{
    size_t cchLeft = *pcchLeft;
#ifdef KMK_CC_STRICT
    size_t offWordCcStrict = pchWord - pCompiler->pszContent;
#endif
    KMK_CC_ASSERT(cchLeft > 0);
    KMK_CC_ASSERT(cchLeft <= pCompiler->cchLine);
    KMK_CC_ASSERT(*pchWord == ch);
    KMK_CC_ASSERT(KMK_CC_EVAL_IS_SPACE_OR_BACKSLASH(ch));
    KMK_CC_ASSERT(offWordCcStrict >= pCompiler->offLine);
    KMK_CC_ASSERT(offWordCcStrict < pCompiler->offLine + pCompiler->cchLine);
    KMK_CC_ASSERT(   pCompiler->iEscEol >= pCompiler->cEscEols
                  || offWordCcStrict <= pCompiler->paEscEols[pCompiler->iEscEol].offEsc);
    KMK_CC_ASSERT(   pCompiler->iEscEol >= pCompiler->cEscEols
                  || pCompiler->iEscEol == 0
                  || offWordCcStrict >= pCompiler->paEscEols[pCompiler->iEscEol - 1].offEol + pCompiler->cchEolSeq);

    /*
     * If we don't need to consider escaped EOLs, things are much much simpler.
     */
    if (pCompiler->iEscEol >= pCompiler->cEscEols)
    {
        if (ch != '\\')
        {
            pchWord++;
            cchLeft--;
        }
        else
            return pchWord;
        return kmk_cc_eval_skip_spaces_without_esc_eol(pchWord, cchLeft, pcchLeft, pCompiler);
    }

    /*
     * Possible escaped EOL complications.
     */
    if (ch != '\\')
    {
        pchWord++;
        cchLeft--;
    }
    else
    {
        size_t          cchSkip;
        size_t          offWord;
        unsigned        iEscEol = pCompiler->iEscEol;
        if (iEscEol >= pCompiler->cEscEols)
            return pchWord;

        offWord = pchWord - pCompiler->pszContent;
        if (offWord < pCompiler->paEscEols[iEscEol].offEsc)
            return pchWord;
        KMK_CC_ASSERT(offWord == pCompiler->paEscEols[iEscEol].offEsc);

        cchSkip  = pCompiler->paEscEols[iEscEol].offEol + pCompiler->cchEolSeq - offWord;
        pchWord += cchSkip;
        cchLeft -= cchSkip;
        pCompiler->iEscEol = ++iEscEol;

        if (iEscEol < pCompiler->cEscEols)
        { /* likely */ }
        else return kmk_cc_eval_skip_spaces_without_esc_eol(pchWord, cchLeft, pcchLeft, pCompiler);
    }
    return kmk_cc_eval_skip_spaces_with_esc_eol(pchWord, cchLeft, pcchLeft, pCompiler);
}


/**
 * Skips to the end of a variable name.
 *
 * This may advance pCompiler->iEscEol.
 *
 * @returns Pointer to the first char after the variable name.
 * @param   pCompiler   The compiler state.
 * @param   pchWord     The current position. Must be at the start of the
 *                      variable name.
 * @param   cchLeft     The number of chars left to parse in the current line.
 * @param   pcchLeft    The to store the updated count of characters left to
 *                      parse.
 * @param   pfPlain     Where to store the plain variable name indicator.
 *                      Returns 0 if plain, and 1 if there are variable
 *                      references in it.
 */
static const char *kmk_cc_eval_skip_var_name(PKMKCCEVALCOMPILER pCompiler, const char *pchWord, size_t cchLeft,
                                             size_t *pcchLeft, int *pfPlain)
{
    const char * const  pszContent = pCompiler->pszContent;
    size_t              off        = pchWord - pszContent;
    size_t const        offLineEnd = off + cchLeft;
    int                 fPlain     = 1;
    unsigned            iEscEol    = pCompiler->iEscEol;

    /* Check our expectations. */
    KMK_CC_ASSERT(cchLeft);
    KMK_CC_ASSERT(!KMK_CC_EVAL_IS_SPACE(*pchWord));
    KMK_CC_ASSERT(iEscEol <= pCompiler->cEscEols);
    KMK_CC_ASSERT(   iEscEol >= pCompiler->cEscEols
                  || off < pCompiler->paEscEols[iEscEol].offEol);
    KMK_CC_ASSERT(off >= (iEscEol == 0 ? pCompiler->offLine : pCompiler->paEscEols[iEscEol - 1].offEol + pCompiler->cchEolSeq));

    /*
     * The outer loop parses plain text.  Variable expansion ($) is handled
     * by an inner loop.
     */
    while (off < offLineEnd)
    {
        char ch = pszContent[off];
        if (!KMK_CC_EVAL_IS_SPACE_DOLLAR_OR_SLASH(ch))
            off++;
        else if (KMK_CC_EVAL_IS_SPACE(ch))
            break;
        else if (ch == '$')
        {
            off++;
            if (off < offLineEnd)
            {
                char const chOpen = pszContent[off];
                if (chOpen == '(' || chOpen == '{')
                {
                    /*
                     * Got a $(VAR) or ${VAR} to deal with here.  This may
                     * include nested variable references and span multiple
                     * lines (at least for function calls).
                     *
                     * We scan forward till we've found the corresponding
                     * closing parenthesis, considering any open parentheses
                     * of the same kind as worth counting, even if there are
                     * no dollar preceeding them, just like GNU make does.
                     */
                    size_t const offStart = off - 1;
                    char const   chClose  = chOpen == '(' ? ')' : '}';
                    unsigned     cOpen    = 1;
                    off++;
                    for (;;)
                    {
                        if (off < offLineEnd)
                        {
                            ch = pszContent[off];
                            if (!(KMK_CC_EVAL_IS_PAREN_OR_SLASH(ch)))
                                off++;
                            else
                            {
                                off++;
                                if (ch == chClose)
                                {
                                    if (--cOpen == 0)
                                        break;
                                }
                                else if (ch == chOpen)
                                    cOpen++;
                                else if (   ch == '\\'
                                         && iEscEol < pCompiler->cEscEols
                                         && off == pCompiler->paEscEols[iEscEol].offEsc)
                                {
                                    off = pCompiler->paEscEols[iEscEol].offEol + pCompiler->cchEolSeq;
                                    pCompiler->iEscEol = ++iEscEol;
                                }
                            }
                        }
                        else if (cOpen == 1)
                            kmk_cc_eval_fatal(pCompiler, &pszContent[offStart],
                                              "Variable reference is missing '%c'", chClose);
                        else
                            kmk_cc_eval_fatal(pCompiler, &pszContent[offStart],
                                              "%u variable references are missing '%c'", cOpen, chClose);
                    }
                }
                /* Single char variable name. */
                else if (!KMK_CC_EVAL_IS_SPACE(chOpen))
                {  /* likely */ }
                else
                    kmk_cc_eval_fatal(pCompiler, &pszContent[off], "Expected variable name after '$', not end of line");
            }
            else
                kmk_cc_eval_fatal(pCompiler, &pszContent[off], "Expected variable name after '$', not end of line");
            fPlain = 0;
        }
        /* Deal with potential escaped EOL. */
        else if (   ch != '\\'
                 || iEscEol >= pCompiler->cEscEols
                 || off != pCompiler->paEscEols[iEscEol].offEsc )
            off++;
        else
            break;
    }

    *pcchLeft = offLineEnd - off;
    *pfPlain  = fPlain;
    return &pszContent[off];
}


#if 0  /* unused atm */
/**
 * Prepares for copying a command line.
 *
 * The current version of this code will not modify any of the paEscEols
 * entries, unlike our kmk_cc_eval_prep_normal_line sibling function.
 *
 * @returns The number of chars that will be copied by
 *          kmk_cc_eval_copy_prepped_command_line().
 * @param   pCompiler   The compiler instance data.
 * @param   pchLeft     Pointer to the first char to copy from the current line.
 *                      This does not have to the start of a word.
 * @param   cchLeft     The number of chars left on the current line starting at
 *                      @a pchLeft.
 */
static size_t kmk_cc_eval_prep_command_line(PKMKCCEVALCOMPILER pCompiler, const char * const pchLeft, size_t cchLeft)
{
    size_t          cchRet;
    unsigned        iEscEol  = pCompiler->iEscEol;
    unsigned const  cEscEols = pCompiler->cEscEols;

    KMK_CC_ASSERT(cchLeft > 0);
    KMK_CC_ASSERT(iEscEol <= cEscEols);

    if (iEscEol >= cEscEols)
    {
        /*
         * No escaped EOLs left, dead simple.
         */
        cchRet = cchLeft;
    }
    else
    {
        /*
         * Compared to the normal prepping of a line, this is actually
         * really simple.  We need to account for two kind of conversions:
         *      - One leading tab is skipped after escaped EOL.
         *      - Convert EOL to LF.
         */
        const char * const  pszContent = pCompiler->pszContent;
        size_t       const  cchEolSeq  = pCompiler->cchEolSeq;

#ifdef KMK_CC_STRICT
        size_t const offLeft   = pchLeft - pszContent;
        KMK_CC_ASSERT(offLeft + cchLeft <= pCompiler->offLine + pCompiler->cchLine);
        KMK_CC_ASSERT(offLeft + cchLeft <= pCompiler->cchContent);
        KMK_CC_ASSERT(offLeft <  pCompiler->paEscEols[iEscEol].offEsc);
        KMK_CC_ASSERT(offLeft >= (iEscEol ? pCompiler->paEscEols[cEscEols - 1].offEol + pCompiler->cchEolSeq : pCompiler->offLine));
#endif

        cchRet = cchLeft;
        if (cchEolSeq > 1)
            cchRet -= (cchEolSeq - 1) * cEscEols;
        do
        {
            if (pszContent[pCompiler->paEscEols[cchEolSeq].offEol])
                cchRet--;
            iEscEol++;
        } while (iEscEol < cEscEols);
    }
    return cchRet;
}


/**
 * Copies a command line to the buffer @a pszDst points to.
 *
 * Must only be used immediately after kmk_cc_eval_prep_command_line().
 *
 * @returns
 * @param   pCompiler   The compiler instance data.
 * @param   pchLeft     Pointer to the first char to copy from the current line.
 *                      This does not have to the start of a word.
 * @param   cchPrepped  The return value of kmk_cc_eval_prep_command_line().
 * @param   pszDst      The destination buffer, must be at least @a cchPrepped
 *                      plus one (terminator) char big.
 */
static void kmk_cc_eval_copy_prepped_command_line(PKMKCCEVALCOMPILER pCompiler, const char *pchLeft,
                                                  size_t cchPrepped, char *pszDst)
{
    unsigned        iEscEol  = pCompiler->iEscEol;
    unsigned const  cEscEols = pCompiler->cEscEols;
    if (iEscEol >= cEscEols)
    {
        /* Single line. */
        memcpy(pszDst, pchLeft, cchPrepped);
        pszDst[cchPrepped] = '\0';
    }
    else
    {
        /* Multiple lines with normalized EOL and maybe one stripped leading TAB. */
        char * const        pszDstStart = pszDst;
        const char * const  pszContent  = pCompiler->pszContent;
        size_t const        cchEolSeq   = pCompiler->cchEolSeq;
        size_t              offLeft     = pchLeft - pCompiler->pszContent;
        size_t              cchCopy;

        do
        {
            size_t offEol = pCompiler->paEscEols[iEscEol].offEsc;
            cchCopy = offEol - offLeft;
            KMK_CC_ASSERT(offEol >= offLeft);

            memcpy(pszDst, &pszContent[offLeft], cchCopy);
            pszDst += cchCopy;
            *pszDst += '\n';

            offLeft = offEol + cchEolSeq;
            if (pszContent[offLeft] == '\t')
                offLeft++;
        } while (iEscEol < cEscEols);

        cchCopy = cchPrepped - (pszDst - pszDstStart);
        KMK_CC_ASSERT(cchCopy <= cchPrepped);
        memcpy(pszDst, &pszContent[offLeft], cchCopy);
        pszDst += cchCopy;

        *pszDst = '\0';
        KMK_CC_ASSERT(pszDst == &pszDstStart[cchPrepped]);
    }
}
#endif /* unused atm */


/**
 * Helper for ensuring that we've got sufficient number of words allocated.
 */
#define KMK_CC_EVAL_ENSURE_WORDS(a_pCompiler, a_cRequiredWords) \
    do { \
        if ((a_cRequiredWords) < (a_pCompiler)->cWordsAllocated) \
        { /* likely */ } \
        else \
        { \
            unsigned cEnsureWords = ((a_cRequiredWords) + 3 /*15*/) & ~(unsigned)3/*15*/; \
            KMK_CC_ASSERT((a_cRequiredWords) < 0x8000); \
            (a_pCompiler)->paWords = (PKMKCCEVALWORD)xmalloc(cEnsureWords * sizeof((a_pCompiler)->paWords)[0]); \
        } \
    } while (0)

/**
 * Parses the remainder of the line into simple words.
 *
 * The resulting words are classified as either kKmkCcEvalToken_WordPlain or
 * kKmkCcEvalToken_WordWithDollar.
 *
 * @returns Number of words.
 * @param   pCompiler   The compiler state.
 * @param   pchWord     Where to start, we expect this to be at a word.
 * @param   cchLeft     The number of chars left to parse on this line.
 *                      This is expected to be non-zero.
 */
static unsigned kmk_cc_eval_parse_words(PKMKCCEVALCOMPILER pCompiler, const char *pchWord, size_t cchLeft)
{
    unsigned iEscEol  = pCompiler->iEscEol;
    unsigned cEscEols = pCompiler->cEscEols;
    unsigned cWords   = 0;

    /* Precoditions. */
    KMK_CC_ASSERT(cchLeft > 0);
    KMK_CC_ASSERT(!KMK_CC_EVAL_IS_SPACE(*pchWord));

    /*
     * If we don't have to deal with escaped EOLs, the find-end-of word search
     * becomes a little bit simpler.  Since this function will be used a lot
     * for simple lines with single words, this could maybe save a nano second
     * or two.
     */
    if (iEscEol >= cEscEols)
    {
        do
        {
            size_t          cchSkipAfter = 0;
            size_t          cchWord      = 1;
            KMKCCEVALTOKEN  enmToken     = kKmkCcEvalToken_WordPlain;

            /* Find the end of the current word. */
            while (cchWord < cchLeft)
            {
                char ch = pchWord[cchWord];
                if (!KMK_CC_EVAL_IS_SPACE_OR_DOLLAR(ch))
                { /* likely */ }
                else if (ch == '$')
                    enmToken = kKmkCcEvalToken_WordWithDollar;
                else
                    break;
                cchWord++;
            }

            /* Add the word. */
            KMK_CC_EVAL_ENSURE_WORDS(pCompiler, cWords + 1);
            pCompiler->paWords[cWords].pchWord  = pchWord;
            pCompiler->paWords[cWords].cchWord  = cchWord;
            pCompiler->paWords[cWords].enmToken = enmToken;
            cWords++;

            /* Skip the work and any trailing blanks. */
            cchWord += cchSkipAfter;
            pchWord += cchWord;
            cchLeft -= cchWord;
            KMK_CC_EVAL_SKIP_SPACES_AFTER_WORD(pCompiler, pchWord, cchLeft);
        } while (cchLeft > 0);
    }
    /*
     * Have to deal with escaped EOLs.
     */
    else
    {
        const char *pszContent = pCompiler->pszContent;
        do
        {
            size_t          cchSkipAfter = 0;
            size_t          cchWord      = 1;
            KMKCCEVALTOKEN  enmToken     = kKmkCcEvalToken_WordPlain;

            /* Find the end of the current word. */
            while (cchWord < cchLeft)
            {
                char ch = pchWord[cchWord];
                if (!KMK_CC_EVAL_IS_SPACE_DOLLAR_OR_SLASH(ch))
                { /* likely */ }
                else if (ch == '$')
                    enmToken = kKmkCcEvalToken_WordWithDollar;
                else if (ch != '\\')
                    break;
                else if ((size_t)(&pchWord[cchWord] - pszContent) == pCompiler->paEscEols[iEscEol].offEsc)
                {
                    cchSkipAfter = pCompiler->paEscEols[iEscEol].offEol - pCompiler->paEscEols[iEscEol].offEsc
                                 + pCompiler->cchEolSeq;
                    iEscEol++;
                    break;
                }
                cchWord++;
            }

            /* Add the word. */
            KMK_CC_EVAL_ENSURE_WORDS(pCompiler, cWords + 1);
            pCompiler->paWords[cWords].pchWord  = pchWord;
            pCompiler->paWords[cWords].cchWord  = cchWord;
            pCompiler->paWords[cWords].enmToken = enmToken;
            cWords++;

            /* Skip the work and any trailing blanks. */
            cchWord += cchSkipAfter;
            pchWord += cchWord;
            cchLeft -= cchWord;
            KMK_CC_EVAL_SKIP_SPACES_AFTER_WORD(pCompiler, pchWord, cchLeft);
        } while (cchLeft > 0);
    }
    pCompiler->cWords = cWords;
    return cWords;
}




/**
 * Gather string from segments and optional space insertion trick.
 *
 * @param   pszDst          The destination buffer.
 * @param   paSegs          The source segments.
 * @param   cSegs           The number of segments.
 * @param   cchDstPrepped   The size of pszDst, excluding the terminator.
 */
static void kmk_cc_eval_strcpyv(char *pszDst, PCKMKCCEVALSTRCPYSEG paSegs, unsigned cSegs, size_t cchDstPrepped)
{
    const char *pszDstStart = pszDst;
    unsigned    iSeg = 0;
    while (iSeg < cSegs)
    {
        size_t cchToCopy;
        if (paSegs[iSeg].cchSrcAndPrependSpace >= 0)
            cchToCopy = paSegs[iSeg].cchSrcAndPrependSpace;
        else
        {
            cchToCopy = -paSegs[iSeg].cchSrcAndPrependSpace;
            *pszDst++ = ' ';
        }

        memcpy(pszDst, paSegs[iSeg].pchSrc, cchToCopy);
        pszDst += cchToCopy;

        iSeg++;
    }
    *pszDst = '\0';
    KMK_CC_ASSERT(pszDst == &pszDstStart[cchDstPrepped]); K_NOREF(pszDstStart); K_NOREF(cchDstPrepped);
}


/**
 * Allocate a byte buffer and ocpy the prepared string segments into it.
 *
 * The caller must call kmk_cc_block_realign!
 *
 * @returns Pointer to the duplicated string.
 * @param   pCompiler       The compiler instance data.
 * @param   cchPrepped      The length of the prepped string segments.
 */
static char *kmk_cc_eval_strdup_prepped(PKMKCCEVALCOMPILER pCompiler, size_t cchPrepped)
{
    char *pszCopy = kmk_cc_block_byte_alloc(pCompiler->ppBlockTail, cchPrepped + 1);
    kmk_cc_eval_strcpyv(pszCopy, pCompiler->paStrCopySegs, pCompiler->cStrCopySegs, cchPrepped);
    return pszCopy;
}


/**
 * Strip trailing spaces from prepped copy
 *
 * @param   paSegs          The segments to strip trailing chars from.
 * @param   pcSegs          The number of segments (in/out).
 * @param   pcchDstPrepped  The total number of chars prepped (in/out).
 */
static void kmk_cc_eval_strip_right_v(PKMKCCEVALSTRCPYSEG paSegs, unsigned *pcSegs, size_t *pcchDstPrepped)
{
    /*
     * Work our way thru the segments, from the end obviously.
     */
    size_t   cchDstPrepped = *pcchDstPrepped;
    unsigned cSegs         = *pcSegs;
    while (cSegs > 0)
    {
        unsigned    iSeg   = cSegs - 1;
        const char *pszSrc = paSegs[iSeg].pchSrc;
        size_t      cchSrc = paSegs[iSeg].cchSrcAndPrependSpace >= 0
                           ? paSegs[iSeg].cchSrcAndPrependSpace : -paSegs[iSeg].cchSrcAndPrependSpace;
        if (cchSrc)
        {
            /*
             * Check for trailing spaces.
             */
            size_t cchSrcOrg;
            if (!KMK_CC_EVAL_IS_SPACE(pszSrc[cchSrc - 1]))
            {
                /* Special case: No trailing spaces at all. No need to update
                                 input/output variables. */
                if (cSegs == *pcSegs)
                    return;
                break;
            }

            /* Skip the rest of the trailing spaces. */
            cchSrcOrg = cchSrc;
            do
                cchSrc--;
            while (cchSrc > 0 && KMK_CC_EVAL_IS_SPACE(pszSrc[cchSrc - 1]));

            if (cchSrc > 0)
            {
                /*
                 * There are non-space chars in this segment. So, update the
                 * segment and total char count and we're done.
                 */
                cchDstPrepped -= cchSrcOrg - cchSrc;
                if (paSegs[iSeg].cchSrcAndPrependSpace < 0)
                    paSegs[iSeg].cchSrcAndPrependSpace = -(ssize_t)cchSrc;
                else
                    paSegs[iSeg].cchSrcAndPrependSpace = cchSrc;
                break;
            }

            /*
             * Skip the whole segment.
             */
            cchDstPrepped -= cchSrcOrg + (paSegs[iSeg].cchSrcAndPrependSpace < 0);
        }
        cSegs--;
    }
    *pcchDstPrepped = cchDstPrepped;
    *pcSegs         = cSegs;
}

/**
 * Helper for ensuring that we've got sufficient number of string copy segments.
 */
#define KMK_CC_EVAL_ENSURE_STRCOPY_SEGS(a_pCompiler, a_cRequiredSegs) \
    do { \
        if ((a_cRequiredSegs) < (a_pCompiler)->cStrCopySegsAllocated) \
        { /* likely */ } \
        else \
        { \
            unsigned cEnsureSegs = ((a_cRequiredSegs) + 3 /*15*/) & ~(unsigned)3/*15*/; \
            KMK_CC_ASSERT((a_cRequiredSegs) < 0x8000); \
            (a_pCompiler)->paStrCopySegs = (PKMKCCEVALSTRCPYSEG)xmalloc(cEnsureSegs * sizeof((a_pCompiler)->paStrCopySegs)[0]); \
        } \
    } while (0)


/**
 * Prepares for copying a normal line, extended version.
 *
 * This does not assume that we start on a word, it can handle any starting
 * character.  It can also prepare partial copies.
 *
 * In addition to the returned information, this will store instruction in
 * paEscEols for the following kmk_cc_eval_strcpyv() call.
 *
 * This will advance pCompiler->iEscEol, so that it's possible to use the common
 * macros and helpers for parsing what comes afterwards.
 *
 * @returns The number of chars that will be copied by kmk_cc_eval_strcpyv().
 * @param   pCompiler               The compiler instance data.
 * @param   pchWord                 Pointer to the first char to copy from the
 *                                  current line. This must be the start of a
 *                                  word.
 * @param   cchLeft                 The number of chars left on the current line
 *                                  starting at @a pchWord.
 */
static size_t kmk_cc_eval_prep_normal_line_ex(PKMKCCEVALCOMPILER pCompiler, const char * const pchWord, size_t cchLeft)
{
    size_t          cchRet;
    unsigned        iEscEol  = pCompiler->iEscEol;
    unsigned const  cEscEols = pCompiler->cEscEols;

    KMK_CC_ASSERT(iEscEol <= cEscEols);

    if (cchLeft > 0)
    {
        /*
         * If there are no escaped EOLs left, just copy exactly
         * what was passed in.
         */
        if (iEscEol >= cEscEols)
        {
            KMK_CC_EVAL_ENSURE_STRCOPY_SEGS(pCompiler, 1);
            pCompiler->cStrCopySegs = 1;
            pCompiler->paStrCopySegs[0].pchSrc = pchWord;
            pCompiler->paStrCopySegs[0].cchSrcAndPrependSpace = cchRet = cchLeft;
        }
        /*
         * Ok, we have to deal with escaped EOLs and do the proper
         * replacement of escaped newlines with space.  The deal is that we
         * collaps all whitespace before and after one or more newlines into a
         * single space.  (FreeBSD make does this differently, by the by.)
         */
        else
        {
            const char * const  pszContent    = pCompiler->pszContent;
            size_t              offWord       = pchWord - pCompiler->pszContent;
            size_t const        offLineEnd    = offWord + cchLeft;             /* Note! Not necessarily end of line.*/
            size_t              offEsc;
            size_t              fPendingSpace = 0;
            unsigned            cSegs         = 0;
            size_t              cchSeg;

            /* Go nuts checking our preconditions here. */
            KMK_CC_ASSERT(offWord >= pCompiler->offLine);
            KMK_CC_ASSERT(offWord + cchLeft <= pCompiler->offLine + pCompiler->cchLine);
            KMK_CC_ASSERT(offWord + cchLeft <= pCompiler->cchContent);
            KMK_CC_ASSERT(offWord <= pCompiler->paEscEols[iEscEol].offEsc);
            KMK_CC_ASSERT(offWord >= (iEscEol ? pCompiler->paEscEols[cEscEols - 1].offEol + pCompiler->cchEolSeq
                                              : pCompiler->offLine));
            KMK_CC_ASSERT(offWord <  offLineEnd);

            /* Make sure we've got more than enough segments to fill in. */
            KMK_CC_EVAL_ENSURE_STRCOPY_SEGS(pCompiler, cEscEols - iEscEol + 2);

            /*
             * All but the last line.
             */
            cchRet = 0;
            do
            {
                KMK_CC_ASSERT(offWord < offLineEnd);
                offEsc = pCompiler->paEscEols[iEscEol].offEsc;
                if (offWord < offEsc)
                {
                    /* Strip trailing spaces. */
                    while (offEsc > offWord && KMK_CC_EVAL_IS_SPACE(pszContent[offEsc - 1]))
                        offEsc--;
                    cchSeg = offEsc - offWord;
                    if (cchSeg)
                    {
                        /* Add segment. */
                        pCompiler->paStrCopySegs[cSegs].pchSrc = &pszContent[offWord];
                        if (offEsc < offLineEnd)
                        {
                            pCompiler->paStrCopySegs[cSegs].cchSrcAndPrependSpace = fPendingSpace
                                                                                  ? -(ssize_t)cchSeg : (ssize_t)cchSeg;
                            cchRet       += cchSeg + fPendingSpace;
                            cSegs        += 1;
                            fPendingSpace = 1;
                        }
                        else
                        {
                            cchSeg = offLineEnd - offWord;
                            pCompiler->paStrCopySegs[cSegs].cchSrcAndPrependSpace = fPendingSpace
                                                                                  ? -(ssize_t)cchSeg : (ssize_t)cchSeg;
                            pCompiler->cStrCopySegs = cSegs + 1;
                            pCompiler->iEscEol      = iEscEol;
                            return cchRet + cchSeg + fPendingSpace;
                        }
                    }
                }
                else
                    KMK_CC_ASSERT(offWord == offEsc);

                /* Next line. */
                offWord = pCompiler->paEscEols[iEscEol].offEol + pCompiler->cchEolSeq;
                iEscEol++;

                /* Strip leading spaces. */
                while (offWord < offLineEnd && KMK_CC_EVAL_IS_SPACE(pszContent[offWord]))
                    offWord++;
                if (offWord >= offLineEnd)
                {
                    pCompiler->cStrCopySegs = cSegs;
                    pCompiler->iEscEol      = iEscEol;
                    return cchRet;
                }
            } while (iEscEol < cEscEols);

            /*
             * The last line.
             */
            cchSeg      = offLineEnd - offWord;
            cchRet     += cchSeg;
            pCompiler->paStrCopySegs[cSegs].pchSrc                = &pszContent[offWord];
            pCompiler->paStrCopySegs[cSegs].cchSrcAndPrependSpace = fPendingSpace
                                                                  ? -(ssize_t)cchSeg : (ssize_t)cchSeg;
            pCompiler->cStrCopySegs = cSegs + 1;
            pCompiler->iEscEol      = iEscEol;
        }
    }
    /*
     * Odd case: Nothing to copy.
     */
    else
    {
        cchRet = 0;
        pCompiler->cStrCopySegs = 0;
    }
    return cchRet;
}


/**
 * Prepares for copying a normal line, from the given position all the way to
 * the end.
 *
 * In addition to the returned information, this will store instruction in
 * paStrCopySegs and cSTrCopySeg for the following kmk_cc_eval_strcpyv() call.
 *
 * @returns The number of chars that will be copied by kmk_cc_eval_strcpyv().
 * @param   pCompiler               The compiler instance data.
 * @param   pchWord                 Pointer to the first char to copy from the
 *                                  current line. This must be the start of a
 *                                  word.
 * @param   cchLeft                 The number of chars left on the current line
 *                                  starting at @a pchWord.
 */
static size_t kmk_cc_eval_prep_normal_line(PKMKCCEVALCOMPILER pCompiler, const char * const pchWord, size_t cchLeft)
{
    size_t          cchRet;
    unsigned        iEscEol  = pCompiler->iEscEol;
    unsigned const  cEscEols = pCompiler->cEscEols;

    KMK_CC_ASSERT(cchLeft > 0);
    KMK_CC_ASSERT(!KMK_CC_EVAL_IS_SPACE(*pchWord)); /* The fact that we're standing at a word, is exploited below. */
    KMK_CC_ASSERT(iEscEol <= cEscEols);

    /*
     * If there are no escaped EOLs left, just copy what was specified,
     * optionally sans any trailing spaces.
     */
    if (iEscEol >= cEscEols)
    {
        cchRet = cchLeft;

        KMK_CC_EVAL_ENSURE_STRCOPY_SEGS(pCompiler, 1);
        pCompiler->cStrCopySegs = 1;
        pCompiler->paStrCopySegs[0].pchSrc = pchWord;
        pCompiler->paStrCopySegs[0].cchSrcAndPrependSpace = cchRet;
    }
    /*
     * Ok, we have to deal with escaped EOLs and do the proper
     * replacement of escaped newlines with space.  The deal is that we
     * collaps all whitespace before and after one or more newlines into a
     * single space.  (FreeBSD make does this differently, by the by.)
     */
    else
    {
        const char *pszContent = pCompiler->pszContent;
        size_t      offWord    = pchWord - pCompiler->pszContent;
        size_t      offEsc;
        size_t      fPendingSpace;
        size_t      cchSeg;
        unsigned    cSegs      = 0;

        /* Go nuts checking our preconditions here. */
        KMK_CC_ASSERT(offWord >= pCompiler->offLine);
        KMK_CC_ASSERT(offWord + cchLeft <= pCompiler->offLine + pCompiler->cchLine);
        KMK_CC_ASSERT(offWord + cchLeft <= pCompiler->cchContent);
        KMK_CC_ASSERT(offWord <  pCompiler->paEscEols[iEscEol].offEsc);
        KMK_CC_ASSERT(offWord >= (iEscEol ? pCompiler->paEscEols[iEscEol - 1].offEol + pCompiler->cchEolSeq : pCompiler->offLine));

        /* Make sure we've got more than enough segments to fill in. */
        KMK_CC_EVAL_ENSURE_STRCOPY_SEGS(pCompiler, cEscEols - iEscEol + 2);

        /*
         * First line - We're at the start of a word, so no left stripping needed.
         */
        offEsc = pCompiler->paEscEols[iEscEol].offEsc;
        KMK_CC_ASSERT(offEsc > offWord);
        while (KMK_CC_EVAL_IS_SPACE(pszContent[offEsc - 1]))
            offEsc--;
        KMK_CC_ASSERT(offEsc > offWord);

        fPendingSpace = 1;
        cchRet        = offEsc - offWord;
        pCompiler->paStrCopySegs[cSegs].cchSrcAndPrependSpace = cchRet;
        pCompiler->paStrCopySegs[cSegs].pchSrc                = pchWord;
        cSegs++;

        offWord = pCompiler->paEscEols[iEscEol].offEol + pCompiler->cchEolSeq;
        iEscEol++;

        /*
         * All but the last line.
         */
        while (iEscEol < cEscEols)
        {
            offEsc = pCompiler->paEscEols[iEscEol].offEsc;

            /* Strip leading spaces. */
            while (offWord < offEsc && KMK_CC_EVAL_IS_SPACE(pszContent[offWord]))
                offWord++;

            if (offWord < offEsc)
            {
                /* Strip trailing spaces. */
                while (KMK_CC_EVAL_IS_SPACE(pszContent[offEsc - 1]))
                    offEsc--;
                cchSeg = offEsc - offWord;
                pCompiler->paStrCopySegs[cSegs].cchSrcAndPrependSpace = fPendingSpace ? -(ssize_t)cchSeg : (ssize_t)cchSeg;
                cchRet += cchSeg + fPendingSpace;
                pCompiler->paStrCopySegs[cSegs].pchSrc                = &pszContent[offWord];
                cSegs  += 1;
                fPendingSpace = 1;
            }

            /* Next. */
            offWord = pCompiler->paEscEols[iEscEol].offEol + pCompiler->cchEolSeq;
            iEscEol++;
        }

        /*
         * Final line. We must calculate the end of line offset our selves here.
         */
        offEsc = &pchWord[cchLeft] - pszContent;
        while (offWord < offEsc && KMK_CC_EVAL_IS_SPACE(pszContent[offWord]))
            offWord++;

        if (offWord < offEsc)
        {
            cchSeg = offEsc - offWord;
            pCompiler->paStrCopySegs[cSegs].cchSrcAndPrependSpace = fPendingSpace ? -(ssize_t)cchSeg : (ssize_t)cchSeg;
            cchRet += cchSeg + fPendingSpace;
            pCompiler->paStrCopySegs[cSegs].pchSrc                = &pszContent[offWord];
            cSegs  += 1;
        }

        pCompiler->cStrCopySegs = cSegs;
    }
    return cchRet;
}


/**
 * Common worker for all kmk_cc_eval_do_if*() functions.
 *
 * @param   pCompiler   The compiler state.
 * @param   pIfCore     The new IF statement.
 * @param   fInElse     Set if this is an 'else if' (rather than just 'if').
 */
static void kmk_cc_eval_do_if_core(PKMKCCEVALCOMPILER pCompiler, PKMKCCEVALIFCORE pIfCore, int fInElse)
{
    unsigned iIf = pCompiler->cIfs;
    if (!fInElse)
    {
        /* Push an IF statement. */
        if (iIf < KMK_CC_EVAL_MAX_IF_DEPTH)
        {
            pCompiler->cIfs = iIf + 1;
            pCompiler->apIfs[iIf] = pIfCore;
            pIfCore->pPrevCond = NULL;
        }
        else
            kmk_cc_eval_fatal(pCompiler, NULL, "Too deep IF nesting");
    }
    else if (iIf > 0)
    {
        /* Link an IF statement. */
        iIf--;
        pIfCore->pPrevCond    = pCompiler->apIfs[iIf];
        pCompiler->apIfs[iIf] = pIfCore;
    }
    else
        kmk_cc_eval_fatal(pCompiler, NULL, "'else if' without 'if'");
    pIfCore->pNextTrue    = (PKMKCCEVALCORE)kmk_cc_block_get_next_ptr(*pCompiler->ppBlockTail);
    pIfCore->pNextFalse   = NULL; /* This is set by else or endif. */
    pIfCore->pTrueEndJump = NULL; /* This is set by else or endif. */
}


/**
 * Deals with 'if expr' and 'else if expr' statements.
 *
 * @returns 1 to indicate we've handled a keyword (see
 *          kmk_cc_eval_try_handle_keyword).
 * @param   pCompiler   The compiler state.
 * @param   pchWord     First char after 'if'.
 * @param   cchLeft     The number of chars left to parse on this line.
 * @param   fInElse     Set if this is an 'else if' (rather than just 'if').
 */
static int kmk_cc_eval_do_if(PKMKCCEVALCOMPILER pCompiler, const char *pchWord, size_t cchLeft, int fInElse)
{
    KMK_CC_EVAL_SKIP_SPACES_AFTER_WORD(pCompiler, pchWord, cchLeft);
    if (cchLeft)
    {
        PKMKCCEVALIFEXPR pInstr;
        size_t           cchExpr = kmk_cc_eval_prep_normal_line(pCompiler, pchWord, cchLeft);
        kmk_cc_eval_strip_right_v(pCompiler->paStrCopySegs, &pCompiler->cStrCopySegs, &cchExpr);

        pInstr = (PKMKCCEVALIFEXPR)kmk_cc_block_alloc_eval(pCompiler->ppBlockTail, KMKCCEVALIFEXPR_SIZE(cchExpr));
        kmk_cc_eval_strcpyv(pInstr->szExpr, pCompiler->paStrCopySegs, pCompiler->cStrCopySegs, cchExpr);
        pInstr->cchExpr = cchExpr;
        pInstr->IfCore.Core.enmOpcode = kKmkCcEvalInstr_if;
        pInstr->IfCore.Core.iLine     = pCompiler->iLine;
        kmk_cc_eval_do_if_core(pCompiler, &pInstr->IfCore, fInElse);
    }
    else
        kmk_cc_eval_fatal(pCompiler, pchWord, "Expected expression after 'if' directive");
    return 1;
}


/**
 * Deals with 'ifdef var', 'ifndef var', 'else ifdef var' and 'else ifndef var'
 * statements.
 *
 * @returns 1 to indicate we've handled a keyword (see
 *          kmk_cc_eval_try_handle_keyword).
 * @param   pCompiler       The compiler state.
 * @param   pchWord         First char after 'if[n]def'.
 * @param   cchLeft         The number of chars left to parse on this line.
 * @param   fInElse         Set if this is an 'else if' (rather than just 'if').
 * @param   fPositiveStmt   Set if 'ifdef', clear if 'ifndef'.
 */
static int kmk_cc_eval_do_ifdef(PKMKCCEVALCOMPILER pCompiler, const char *pchWord, size_t cchLeft, int fInElse, int fPositiveStmt)
{
    KMK_CC_EVAL_SKIP_SPACES_AFTER_WORD(pCompiler, pchWord, cchLeft);
    if (cchLeft)
    {
        /*
         * Skip to the end of the variable name.
         */
        unsigned const      iSavedEscEol = pCompiler->iEscEol;
        const char * const  pchVarNm     = pchWord;
        int                 fPlain;
/** @todo this isn't quite right. It is a variable name, correct. However, it
 *        doesn't need to subscribe entirely to the rules of a variable name.
 *        Just find the end of the word, taking variable refs into account,
 *        and consider it what we need. */
        pchWord = kmk_cc_eval_skip_var_name(pCompiler, pchWord, cchLeft, &cchLeft, &fPlain);
        KMK_CC_ASSERT(pCompiler->iEscEol == iSavedEscEol || !fPlain);
        if (fPlain)
        {
            size_t const            cchVarNm = pchWord - pchVarNm;
            PKMKCCEVALIFDEFPLAIN    pInstr;
            pInstr = (PKMKCCEVALIFDEFPLAIN)kmk_cc_block_alloc_eval(pCompiler->ppBlockTail, sizeof(*pInstr));
            pInstr->IfCore.Core.enmOpcode = fPositiveStmt ? kKmkCcEvalInstr_ifdef_plain : kKmkCcEvalInstr_ifndef_plain;
            pInstr->IfCore.Core.iLine     = pCompiler->iLine;
            pInstr->pszName = strcache2_add(&variable_strcache, pchVarNm, cchVarNm);
            kmk_cc_eval_do_if_core(pCompiler, &pInstr->IfCore, fInElse);
        }
        else
        {
            PKMKCCEVALIFDEFDYNAMIC  pInstr;
            size_t const            cchVarNm = pchWord - pchVarNm;
            size_t                  cchCopy;
            char                   *pszCopy;
            pCompiler->iEscEol = iSavedEscEol;
            cchCopy = kmk_cc_eval_prep_normal_line(pCompiler, pchVarNm, cchVarNm);

            pInstr = (PKMKCCEVALIFDEFDYNAMIC)kmk_cc_block_alloc_eval(pCompiler->ppBlockTail, sizeof(*pInstr));

            /** @todo Make the subprogram embed necessary strings. */
            pszCopy = kmk_cc_eval_strdup_prepped(pCompiler, cchCopy);
            kmk_cc_block_realign(pCompiler->ppBlockTail);

            pInstr->IfCore.Core.enmOpcode = fPositiveStmt ? kKmkCcEvalInstr_ifdef_dynamic : kKmkCcEvalInstr_ifndef_dynamic;
            pInstr->IfCore.Core.iLine     = pCompiler->iLine;
            kmk_cc_eval_compile_string_exp_subprog(pCompiler, pszCopy, cchCopy, &pInstr->NameSubprog);

            kmk_cc_eval_do_if_core(pCompiler, &pInstr->IfCore, fInElse);
        }

        /*
         * Make sure there is nothing following the variable name.
         */
        KMK_CC_EVAL_SKIP_SPACES_AFTER_WORD(pCompiler, pchWord, cchLeft);
        if (cchLeft)
            kmk_cc_eval_fatal(pCompiler, pchWord, "Bogus stuff after 'if%sdef' variable name", fPositiveStmt ? "" : "n");
    }
    else
        kmk_cc_eval_fatal(pCompiler, pchWord, "Expected expression after 'if' directive");
    return 1;
}


/**
 * Deals with 'ifeq (a,b)', 'ifeq "a" "b"', 'ifneq (a,b)', 'ifneq "a" "b"',
 * 'else ifeq (a,b)', 'else ifeq "a" "b"', 'else ifneq (a,b)' and
 * 'else ifneq "a" "b"' statements.
 *
 * @returns 1 to indicate we've handled a keyword (see
 *          kmk_cc_eval_try_handle_keyword).
 * @param   pCompiler       The compiler state.
 * @param   pchWord         First char after 'if[n]eq'.
 * @param   cchLeft         The number of chars left to parse on this line.
 * @param   fInElse         Set if this is an 'else if' (rather than just 'if').
 * @param   fPositiveStmt   Set if 'ifeq', clear if 'ifneq'.
 */
static int kmk_cc_eval_do_ifeq(PKMKCCEVALCOMPILER pCompiler, const char *pchWord, size_t cchLeft, int fInElse, int fPositiveStmt)
{
    KMK_CC_EVAL_SKIP_SPACES_AFTER_WORD(pCompiler, pchWord, cchLeft);
    if (cchLeft)
    {
        /*
         * There are two forms:
         *
         *   ifeq (string1, string2)
         *   ifeq "string1" 'string2'
         *
         */
        const char * const  pchEnd = &pchWord[cchLeft];
        PKMKCCEVALIFEQ      pInstr = (PKMKCCEVALIFEQ)kmk_cc_block_alloc_eval(pCompiler->ppBlockTail, sizeof(*pInstr));

        struct
        {
            char           *pszCopy;
            size_t          cchCopy;
            int             fPlain;
        } Left, Right;

        char ch = *pchWord;
        if (ch == '(')
        {
            int      cCounts;
            size_t   off;

            /*
             * The left side ends with a comma.  We respect parentheses, but
             * not curly brackets.
             */

            /* Skip the parenthesis. */
            pchWord++;
            cchLeft--;

            /* Find the comma, checking for non-plainness. */
            cCounts = 0;
            Left.fPlain = 1;
            for (off = 0; off < cchLeft; off++)
            {
                ch = pchWord[off];
                if (!KMK_CC_EVAL_IS_PAREN_COMMA_OR_DOLLAR(ch))
                { /* likely */ }
                else if (ch == '$')
                    Left.fPlain = 0;
                else if (ch == '(')
                    cCounts++;
                else if (ch == ')')
                    cCounts--; /** @todo warn if it goes negative. */
                else if (ch == ',' && cCounts == 0)
                    break;
                else
                    KMK_CC_ASSERT(cCounts > 0);
            }
            if (ch == ',' && cCounts == 0) { /* likely */ }
            else kmk_cc_eval_fatal(pCompiler, &pchWord[off], "Expected ',' before end of line");

            /* Copy out the string. */
            Left.cchCopy = kmk_cc_eval_prep_normal_line_ex(pCompiler, pchWord, off);
            kmk_cc_eval_strip_right_v(pCompiler->paStrCopySegs, &pCompiler->cStrCopySegs, &Left.cchCopy);
            Left.pszCopy = kmk_cc_eval_strdup_prepped(pCompiler, Left.cchCopy);

            /* Skip past the comma and any following spaces. */
            pchWord += off + 1;
            cchLeft -= off + 1;
            if (   cchLeft /** @todo replace with straight 'isspace' that takes escaped EOLs into account. */
                && KMK_CC_EVAL_IS_SPACE_AFTER_WORD(pCompiler, pchWord[0], pchWord[1], cchLeft))
                KMK_CC_EVAL_SKIP_SPACES_AFTER_WORD(pCompiler, pchWord, cchLeft);

            /*
             * Ditto for the right side, only it ends with a closing parenthesis.
             */
            cCounts = 1;
            Right.fPlain = 1;
            for (off = 0; off < cchLeft; off++)
            {
                ch = pchWord[off];
                if (!KMK_CC_EVAL_IS_PAREN_COMMA_OR_DOLLAR(ch))
                { /* likely */ }
                else if (ch == '$')
                    Right.fPlain = 0;
                else if (ch == '(')
                    cCounts++;
                else if (ch == ')')
                {
                    if (--cCounts == 0)
                        break;
                }
                else
                    KMK_CC_ASSERT(cCounts > 0 || ch == ',');
            }
            if (ch == ')' && cCounts == 0) { /* likely */ }
            else kmk_cc_eval_fatal(pCompiler, &pchWord[off], "Expected ')' before end of line");

            /* Copy out the string. */
            Right.cchCopy = kmk_cc_eval_prep_normal_line_ex(pCompiler, pchWord, off);
            kmk_cc_eval_strip_right_v(pCompiler->paStrCopySegs, &pCompiler->cStrCopySegs, &Right.cchCopy);
            Right.pszCopy = kmk_cc_eval_strdup_prepped(pCompiler, Right.cchCopy);

            /* Skip past the parenthesis. */
            pchWord += off + 1;
            cchLeft -= off + 1;
        }
        else if (ch == '"' || ch == '\'')
        {
            const char *pchTmp;

            /*
             * Quoted left side.
             */
            /* Skip leading quote. */
            pchWord++;
            cchLeft--;

            /* Locate the end quote. */
            pchTmp = (const char *)memchr(pchWord, ch, cchLeft);
            if (pchTmp) { /* likely */ }
            else kmk_cc_eval_fatal(pCompiler, pchWord - 1, "Unbalanced quote in first if%seq string", fPositiveStmt ? "" : "n");

            Left.cchCopy = kmk_cc_eval_prep_normal_line_ex(pCompiler, pchWord, pchTmp - pchWord);
            Left.pszCopy = kmk_cc_eval_strdup_prepped(pCompiler, Left.cchCopy);
            Left.fPlain  = memchr(Left.pszCopy, '$', Left.cchCopy) == NULL;

            /* skip end quote */
            pchWord  = pchTmp + 1;
            cchLeft  = pchEnd - pchWord;

            /* Skip anything inbetween the left and right hand side (not mandatory). */
            if (   cchLeft /** @todo replace with straight 'isspace' that takes escaped EOLs into account. */
                && KMK_CC_EVAL_IS_SPACE_AFTER_WORD(pCompiler, pchWord[0], pchWord[1], cchLeft))
                KMK_CC_EVAL_SKIP_SPACES_AFTER_WORD(pCompiler, pchWord, cchLeft);

            /*
             * Quoted right side.
             */
            if (   cchLeft > 0
                && ( (ch = *pchWord) != '"' || ch == '\'') )
            {
                /* Skip leading quote. */
                pchWord++;
                cchLeft--;

                /* Locate the end quote. */
                pchTmp = (const char *)memchr(pchWord, ch, cchLeft);
                if (pchTmp) { /* likely */ }
                else kmk_cc_eval_fatal(pCompiler, pchWord - 1, "Unbalanced quote in second if%seq string", fPositiveStmt ? "" : "n");

                Right.cchCopy = kmk_cc_eval_prep_normal_line_ex(pCompiler, pchWord, pchTmp - pchWord);
                Right.pszCopy = kmk_cc_eval_strdup_prepped(pCompiler, Right.cchCopy);
                Right.fPlain  = memchr(Right.pszCopy, '$', Right.cchCopy) == NULL;

                /* skip end quote */
                pchWord  = pchTmp + 1;
                cchLeft  = pchEnd - pchWord;
            }
            else
                kmk_cc_eval_fatal(pCompiler, pchWord, "Expected a second quoted string for 'if%seq'",
                                  fPositiveStmt ? "" : "n");
        }
        else
            kmk_cc_eval_fatal(pCompiler, pchWord, "Expected parentheses or quoted string after 'if%seq'",
                              fPositiveStmt ? "" : "n");
        kmk_cc_block_realign(pCompiler->ppBlockTail);

        /*
         * Initialize the instruction.
         */
        pInstr->IfCore.Core.enmOpcode = fPositiveStmt ? kKmkCcEvalInstr_ifeq : kKmkCcEvalInstr_ifneq;
        pInstr->IfCore.Core.iLine     = pCompiler->iLine;
        kmk_cc_eval_init_subprogram_or_plain(pCompiler, &pInstr->Left, Left.pszCopy, Left.cchCopy, Left.fPlain);
        kmk_cc_eval_init_subprogram_or_plain(pCompiler, &pInstr->Right, Right.pszCopy, Right.cchCopy, Right.fPlain);
        kmk_cc_eval_do_if_core(pCompiler, &pInstr->IfCore, fInElse);

        /*
         * Make sure there is nothing following the variable name.
         */
        KMK_CC_EVAL_SKIP_SPACES_AFTER_WORD(pCompiler, pchWord, cchLeft);
        if (cchLeft)
            kmk_cc_eval_fatal(pCompiler, pchWord, "Bogus stuff after 'if%sdef' variable name", fPositiveStmt ? "" : "n");
    }
    else
        kmk_cc_eval_fatal(pCompiler, pchWord, "Expected expression after 'if' directive");
    return 1;
}


/**
 * Deals with 'if1of (set-a,set-b)', 'ifn1of (set-a,set-b)',
 * 'else if1of (set-a,set-b)' and 'else ifn1of (set-a,set-b)' statements.
 *
 * @returns 1 to indicate we've handled a keyword (see
 *          kmk_cc_eval_try_handle_keyword).
 * @param   pCompiler       The compiler state.
 * @param   pchWord         First char after 'if[n]1of'.
 * @param   cchLeft         The number of chars left to parse on this line.
 * @param   fInElse         Set if this is an 'else if' (rather than just 'if').
 * @param   fPositiveStmt   Set if 'if1of', clear if 'ifn1of'.
 */
static int kmk_cc_eval_do_if1of(PKMKCCEVALCOMPILER pCompiler, const char *pchWord, size_t cchLeft, int fInElse, int fPositiveStmt)
{
    KMK_CC_EVAL_SKIP_SPACES_AFTER_WORD(pCompiler, pchWord, cchLeft);
    if (cchLeft)
    {
        /*
         * This code is (currently) very similar to kmk_cc_eval_do_ifeq.
         * However, we may want to add hashing optimizations of plain text,
         * and we don't want to support the quoted form as it is not necessary
         * and may interfere with support for quoted words later on.
         */
        PKMKCCEVALIF1OF     pInstr = (PKMKCCEVALIF1OF)kmk_cc_block_alloc_eval(pCompiler->ppBlockTail, sizeof(*pInstr));

        struct
        {
            char           *pszCopy;
            size_t          cchCopy;
            int             fPlain;
        } Left, Right;

        char ch = *pchWord;
        if (ch == '(')
        {
            int      cCounts;
            size_t   off;

            /*
             * The left side ends with a comma.  We respect parentheses, but
             * not curly brackets.
             */

            /* Skip the parenthesis. */
            pchWord++;
            cchLeft--;

            /* Find the comma, checking for non-plainness. */
            cCounts = 0;
            Left.fPlain = 1;
            for (off = 0; off < cchLeft; off++)
            {
                ch = pchWord[off];
                if (!KMK_CC_EVAL_IS_PAREN_COMMA_OR_DOLLAR(ch))
                { /* likely */ }
                else if (ch == '$')
                    Left.fPlain = 0;
                else if (ch == '(')
                    cCounts++;
                else if (ch == ')')
                    cCounts--; /** @todo warn if it goes negative. */
                else if (ch == ',' && cCounts == 0)
                    break;
                else
                    KMK_CC_ASSERT(cCounts > 0);
            }
            if (ch == ',' && cCounts == 0) { /* likely */ }
            else kmk_cc_eval_fatal(pCompiler, &pchWord[off], "Expected ',' before end of line");

            /* Copy out the string. */
            Left.cchCopy = kmk_cc_eval_prep_normal_line_ex(pCompiler, pchWord, off);
            kmk_cc_eval_strip_right_v(pCompiler->paStrCopySegs, &pCompiler->cStrCopySegs, &Left.cchCopy);
            Left.pszCopy = kmk_cc_eval_strdup_prepped(pCompiler, Left.cchCopy);

            /* Skip past the comma and any following spaces. */
            pchWord += off + 1;
            cchLeft -= off + 1;
            if (   cchLeft /** @todo replace with straight 'isspace' that takes escaped EOLs into account. */
                && KMK_CC_EVAL_IS_SPACE_AFTER_WORD(pCompiler, pchWord[0], pchWord[1], cchLeft))
                KMK_CC_EVAL_SKIP_SPACES_AFTER_WORD(pCompiler, pchWord, cchLeft);

            /*
             * Ditto for the right side, only it ends with a closing parenthesis.
             */
            cCounts = 1;
            Right.fPlain = 1;
            for (off = 0; off < cchLeft; off++)
            {
                ch = pchWord[off];
                if (!KMK_CC_EVAL_IS_PAREN_COMMA_OR_DOLLAR(ch))
                { /* likely */ }
                else if (ch == '$')
                    Right.fPlain = 0;
                else if (ch == '(')
                    cCounts++;
                else if (ch == ')')
                {
                    if (--cCounts == 0)
                        break;
                }
                else
                    KMK_CC_ASSERT(cCounts > 0 || ch == ',');
            }
            if (ch == ')' && cCounts == 0) { /* likely */ }
            else kmk_cc_eval_fatal(pCompiler, &pchWord[off], "Expected ')' before end of line");

            /* Copy out the string. */
            Right.cchCopy = kmk_cc_eval_prep_normal_line_ex(pCompiler, pchWord, off);
            kmk_cc_eval_strip_right_v(pCompiler->paStrCopySegs, &pCompiler->cStrCopySegs, &Right.cchCopy);
            Right.pszCopy = kmk_cc_eval_strdup_prepped(pCompiler, Right.cchCopy);

            /* Skip past the parenthesis. */
            pchWord += off + 1;
            cchLeft -= off + 1;
        }
        else
            kmk_cc_eval_fatal(pCompiler, pchWord, "Expected parentheses after 'if%s1of'", fPositiveStmt ? "" : "n");
        kmk_cc_block_realign(pCompiler->ppBlockTail);

        /*
         * Initialize the instruction.
         */
        pInstr->IfCore.Core.enmOpcode = fPositiveStmt ? kKmkCcEvalInstr_if1of : kKmkCcEvalInstr_ifn1of;
        pInstr->IfCore.Core.iLine     = pCompiler->iLine;
        kmk_cc_eval_init_subprogram_or_plain(pCompiler, &pInstr->Left, Left.pszCopy, Left.cchCopy, Left.fPlain);
        kmk_cc_eval_init_subprogram_or_plain(pCompiler, &pInstr->Right, Right.pszCopy, Right.cchCopy, Right.fPlain);
        kmk_cc_eval_do_if_core(pCompiler, &pInstr->IfCore, fInElse);

        /*
         * Make sure there is nothing following the variable name.
         */
        KMK_CC_EVAL_SKIP_SPACES_AFTER_WORD(pCompiler, pchWord, cchLeft);
        if (cchLeft)
            kmk_cc_eval_fatal(pCompiler, pchWord, "Bogus stuff after 'if%s1of' variable name", fPositiveStmt ? "" : "n");
    }
    else
        kmk_cc_eval_fatal(pCompiler, pchWord, "Expected expression after 'if' directive");
    return 1;
}


/**
 * Deals with 'else' and 'else ifxxx' statements.
 *
 * @returns 1 to indicate we've handled a keyword (see
 *          kmk_cc_eval_try_handle_keyword).
 * @param   pCompiler   The compiler state.
 * @param   pchWord     First char after 'define'.
 * @param   cchLeft     The number of chars left to parse on this line.
 */
static int kmk_cc_eval_do_else(PKMKCCEVALCOMPILER pCompiler, const char *pchWord, size_t cchLeft)
{
    /*
     * There must be an 'if' on the stack.
     */
    unsigned iIf = pCompiler->cIfs;
    if (iIf > 0)
    {
        PKMKCCEVALIFCORE pIfCore = pCompiler->apIfs[--iIf];
        if (!pIfCore->pTrueEndJump)
        {
            /* Emit a jump instruction that will take us from the 'True' block to the 'endif'. */
            PKMKCCEVALJUMP  pInstr = (PKMKCCEVALJUMP)kmk_cc_block_alloc_eval(pCompiler->ppBlockTail, sizeof(*pInstr));
            pInstr->Core.enmOpcode = kKmkCcEvalInstr_jump;
            pInstr->Core.iLine     = pCompiler->iLine;
            pInstr->pNext          = NULL;
            pIfCore->pTrueEndJump  = pInstr;

            /* The next instruction is the first in the 'False' block of the current 'if'.
               Should this be an 'else if', this will be the 'if' instruction emitted below. */
            pIfCore->pNextFalse    = (PKMKCCEVALCORE)kmk_cc_block_get_next_ptr(*pCompiler->ppBlockTail);
        }
        else if (iIf == 0)
            kmk_cc_eval_fatal(pCompiler, pchWord, "2nd 'else' for 'if' at line %u", pIfCore->Core.iLine);
        else
            kmk_cc_eval_fatal(pCompiler, pchWord, "2nd 'else' in a row - missing 'endif' for 'if' at line %u?",
                              pIfCore->Core.iLine);
    }
    else
        kmk_cc_eval_fatal(pCompiler, pchWord, "'else' without 'if'");

    /*
     * Check for 'else ifxxx'. There can be nothing else following an else.
     */
    KMK_CC_EVAL_SKIP_SPACES_AFTER_WORD(pCompiler, pchWord, cchLeft);
    if (cchLeft)
    {
        if (   cchLeft > 2
            && KMK_CC_WORD_COMP_CONST_2(pchWord, "if"))
        {
            pchWord += 2;
            cchLeft -= 2;

            if (KMK_CC_EVAL_WORD_COMP_IS_EOL(pCompiler, pchWord, cchLeft))
                return kmk_cc_eval_do_if(pCompiler, pchWord, cchLeft, 1 /* in else */);

            if (KMK_CC_EVAL_WORD_COMP_CONST(pCompiler, pchWord, cchLeft, "eq", 2))
                return kmk_cc_eval_do_ifeq( pCompiler, pchWord + 2, cchLeft - 2, 1 /* in else */, 1 /* positive */);

            if (KMK_CC_EVAL_WORD_COMP_CONST(pCompiler, pchWord, cchLeft, "def", 3))
                return kmk_cc_eval_do_ifdef(pCompiler, pchWord + 3, cchLeft - 3, 1 /* in else */, 1 /* positive */);

            if (KMK_CC_EVAL_WORD_COMP_CONST(pCompiler, pchWord, cchLeft, "neq", 3))
                return kmk_cc_eval_do_ifeq( pCompiler, pchWord + 3, cchLeft - 3, 1 /* in else */, 0 /* positive */);

            if (KMK_CC_EVAL_WORD_COMP_CONST(pCompiler, pchWord, cchLeft, "1of", 3))
                return kmk_cc_eval_do_if1of(pCompiler, pchWord + 3, cchLeft - 3, 1 /* in else */, 1 /* positive */);

            if (KMK_CC_EVAL_WORD_COMP_CONST(pCompiler, pchWord, cchLeft, "ndef", 4))
                return kmk_cc_eval_do_ifdef(pCompiler, pchWord + 4, cchLeft - 4, 1 /* in else */, 0 /* positive */);

            if (KMK_CC_EVAL_WORD_COMP_CONST(pCompiler, pchWord, cchLeft, "n1of", 4))
                return kmk_cc_eval_do_if1of(pCompiler, pchWord + 4, cchLeft - 4, 1 /* in else */, 0 /* positive */);

            pchWord -= 2;
            cchLeft += 2;
        }
        kmk_cc_eval_fatal(pCompiler, pchWord, "Bogus stuff after 'else'");
    }

    return 1;
}


/**
 * Deals with the 'endif' statement.
 *
 * @returns 1 to indicate we've handled a keyword (see
 *          kmk_cc_eval_try_handle_keyword).
 * @param   pCompiler   The compiler state.
 * @param   pchWord     First char after 'define'.
 * @param   cchLeft     The number of chars left to parse on this line.
 */
static int kmk_cc_eval_do_endif(PKMKCCEVALCOMPILER pCompiler, const char *pchWord, size_t cchLeft)
{
    /*
     * There must be an 'if' on the stack.  We'll POP it.
     */
    unsigned iIf = pCompiler->cIfs;
    if (iIf > 0)
    {
        PKMKCCEVALCORE   pNextInstr;
        PKMKCCEVALIFCORE pIfCore = pCompiler->apIfs[--iIf];
        pCompiler->cIfs = iIf; /* POP! */

        /* Update the jump targets for all IFs at this level. */
        pNextInstr = (PKMKCCEVALCORE)kmk_cc_block_get_next_ptr(*pCompiler->ppBlockTail);
        do
        {
            if (pIfCore->pTrueEndJump)
            {
                /* Make the true block jump here, to the 'endif'. The false block is already here. */
                pIfCore->pTrueEndJump->pNext = pNextInstr;
                KMK_CC_ASSERT(pIfCore->pNextFalse);
            }
            else
            {
                /* No 'else'. The false-case jump here, to the 'endif'. */
                KMK_CC_ASSERT(!pIfCore->pNextFalse);
                pIfCore->pNextFalse = pNextInstr;
            }

            pIfCore = pIfCore->pPrevCond;
        } while (pIfCore);
    }
    else
        kmk_cc_eval_fatal(pCompiler, pchWord, "'endif' without 'if'");

    /*
     * There shouldn't be anything trailing an 'endif'.
     */
    KMK_CC_EVAL_SKIP_SPACES_AFTER_WORD(pCompiler, pchWord, cchLeft);
    if (!cchLeft) { /* likely */ }
    else kmk_cc_eval_fatal(pCompiler, pchWord, "Bogus stuff after 'else'");

    return 1;
}


/**
 * Parses a 'include file...', 'sinclude file...', '-include file...',
 * 'includedep file...', 'includedep-queue file...' and
 * 'includedep-flush file...'
 *
 * @returns 1 to indicate we've handled a keyword (see
 *          kmk_cc_eval_try_handle_keyword).
 * @param   pCompiler   The compiler state.
 * @param   pchWord     First char after the include directive.
 * @param   cchLeft     The number of chars left to parse on this line.
 * @param   enmOpcode   The opcode for the include directive we're parsing.
 */
static int kmk_cc_eval_do_include(PKMKCCEVALCOMPILER pCompiler, const char *pchWord, size_t cchLeft, KMKCCEVALINSTR enmOpcode)
{
    KMK_CC_EVAL_SKIP_SPACES_AFTER_WORD(pCompiler, pchWord, cchLeft);
    if (cchLeft)
    {
        /*
         * Split what's left up into words.
         */
        unsigned cWords = kmk_cc_eval_parse_words(pCompiler, pchWord, cchLeft);
        KMK_CC_EVAL_DPRINTF(("%s: cWords=%d\n", g_apszEvalInstrNms[enmOpcode], cWords));
        if (cWords)
        {
            PKMKCCEVALINCLUDE pInstr = (PKMKCCEVALINCLUDE)kmk_cc_block_alloc_eval(pCompiler->ppBlockTail,
                                                                                  KMKCCEVALINCLUDE_SIZE(cWords));
            pInstr->Core.enmOpcode = enmOpcode;
            pInstr->Core.iLine     = pCompiler->iLine;
            pInstr->cFiles         = cWords;
            kmk_cc_eval_init_spp_array_from_duplicated_words(pCompiler, cWords, pCompiler->paWords, pInstr->aFiles);
            kmk_cc_block_realign(pCompiler->ppBlockTail);
        }
        else
            KMK_CC_ASSERT(0);
    }
    else
        KMK_CC_EVAL_DPRINTF(("%s: include without args\n", g_apszEvalInstrNms[enmOpcode]));
    return 1;
}


static int kmk_cc_eval_do_vpath(PKMKCCEVALCOMPILER pCompiler, const char *pchWord, size_t cchLeft)
{
    kmk_cc_eval_fatal(pCompiler, NULL, "vpath directive is not implemented\n");
    return 1;
}


static void kmk_cc_eval_handle_command(PKMKCCEVALCOMPILER pCompiler, const char *pchWord, size_t cchLeft)
{
    kmk_cc_eval_fatal(pCompiler, pchWord, "command handling not implemented yet");
}


static int kmk_cc_eval_handle_recipe_cont_colon(PKMKCCEVALCOMPILER pCompiler, const char *pchWord0, size_t cchWord0,
                                                const char *pchColon, size_t cchLeft, unsigned fQualifiers)
{
    kmk_cc_eval_fatal(pCompiler, pchWord0, "recipe handling not implemented yet (#1)");
    return 1;
}


static int kmk_cc_eval_handle_recipe_cont_2nd_word(PKMKCCEVALCOMPILER pCompiler, const char *pchWord0, size_t cchWord0,
                                                   const char *pchWord, size_t cchLeft, unsigned fQualifiers)
{
    kmk_cc_eval_fatal(pCompiler, pchWord, "recipe handling not implemented yet (#2)");
    return 1;
}


static void kmk_cc_eval_handle_recipe(PKMKCCEVALCOMPILER pCompiler, const char *pszEqual, const char *pchWord, size_t cchLeft)
{
    kmk_cc_eval_fatal(pCompiler, pchWord, "recipe handling not implemented yet (#3)");
}

static void kmk_cc_eval_end_of_recipe(PKMKCCEVALCOMPILER pCompiler)
{
    if (pCompiler->pRecipe)
    {
        /** @todo do stuff here. */
    }
}


/**
 * Common worker for handling export (non-assign), undefine and unexport.
 *
 * For instructions using the KMKCCEVALVARIABLES structure.
 *
 * @returns 1 to indicate we've handled a keyword (see
 *          kmk_cc_eval_try_handle_keyword).
 * @param   pCompiler   The compiler state.
 * @param   pchWord     First non-space chare after the keyword.
 * @param   cchLeft     The number of chars left to parse on this line.
 * @param   fQualifiers The qualifiers.
 */
static int kmk_cc_eval_do_with_variable_list(PKMKCCEVALCOMPILER pCompiler, const char *pchWord, size_t cchLeft,
                                             KMKCCEVALINSTR enmOpcode, unsigned fQualifiers)
{
    if (cchLeft)
    {
        /*
         * Parse the variable name list.  GNU make is using normal word
         * handling here, so we can share code with the include directives.
         */
        unsigned cWords = kmk_cc_eval_parse_words(pCompiler, pchWord, cchLeft);
        KMK_CC_EVAL_DPRINTF(("%s: cWords=%d\n", g_apszEvalInstrNms[enmOpcode], cWords));
        if (cWords)
        {
            PKMKCCEVALVARIABLES pInstr = (PKMKCCEVALVARIABLES)kmk_cc_block_alloc_eval(pCompiler->ppBlockTail,
                                                                                      KMKCCEVALVARIABLES_SIZE(cWords));
            pInstr->Core.enmOpcode = enmOpcode;
            pInstr->Core.iLine     = pCompiler->iLine;
            pInstr->cVars         = cWords;
            kmk_cc_eval_init_spp_array_from_duplicated_words(pCompiler, cWords, pCompiler->paWords, pInstr->aVars);
            kmk_cc_block_realign(pCompiler->ppBlockTail);
        }
        else
            KMK_CC_ASSERT(0);
    }
    /* else: NOP */
    return 1;
}


/**
 * Parses a '[qualifiers] undefine variable [..]' expression.
 *
 * A 'undefine' directive is final, any qualifiers must preceed it.  So, we just
 * have to extract the variable names now.
 *
 * @returns 1 to indicate we've handled a keyword (see
 *          kmk_cc_eval_try_handle_keyword).
 * @param   pCompiler   The compiler state.
 * @param   pchWord     First char after 'define'.
 * @param   cchLeft     The number of chars left to parse on this line.
 * @param   fQualifiers The qualifiers.
 */
static int kmk_cc_eval_do_var_undefine(PKMKCCEVALCOMPILER pCompiler, const char *pchWord, size_t cchLeft, unsigned fQualifiers)
{
    KMK_CC_EVAL_SKIP_SPACES_AFTER_WORD(pCompiler, pchWord, cchLeft);
    if (!cchLeft)
        kmk_cc_eval_fatal(pCompiler, pchWord, "undefine requires a variable name");

    /** @todo GNU make doesn't actually do the list thing for undefine, it seems
     *        to assume everything after it is a single variable...  Going with
     *        simple common code for now. */
    return kmk_cc_eval_do_with_variable_list(pCompiler, pchWord, cchLeft, kKmkCcEvalInstr_undefine, fQualifiers);
}


/**
 * Parses a '[qualifiers] unexport variable [..]' expression.
 *
 * A 'unexport' directive is final, any qualifiers must preceed it.  So, we just
 * have to extract the variable names now.
 *
 * @returns 1 to indicate we've handled a keyword (see
 *          kmk_cc_eval_try_handle_keyword).
 * @param   pCompiler   The compiler state.
 * @param   pchWord     First char after 'define'.
 * @param   cchLeft     The number of chars left to parse on this line.
 * @param   fQualifiers The qualifiers.
 */
static int kmk_cc_eval_do_var_unexport(PKMKCCEVALCOMPILER pCompiler, const char *pchWord, size_t cchLeft, unsigned fQualifiers)
{
    PKMKCCEVALCORE pInstr;

    /*
     * Join paths with undefine and export, unless it's an unexport all directive.
     */
    KMK_CC_EVAL_SKIP_SPACES_AFTER_WORD(pCompiler, pchWord, cchLeft);
    if (cchLeft)
        return kmk_cc_eval_do_with_variable_list(pCompiler, pchWord, cchLeft, kKmkCcEvalInstr_unexport, fQualifiers);

    /*
     * We're unexporting all variables.
     */
    pInstr = kmk_cc_block_alloc_eval(pCompiler->ppBlockTail, sizeof(*pInstr));
    pInstr->enmOpcode = kKmkCcEvalInstr_unexport_all;
    pInstr->iLine     = pCompiler->iLine;
    return 1;
}


/**
 * Parses a 'define variable' expression.
 *
 * A 'define' directive is final, any qualifiers must preceed it.  So, we just
 * have to extract the variable name now, well and find the corresponding
 * 'endef'.
 *
 * @returns 1 to indicate we've handled a keyword (see
 *          kmk_cc_eval_try_handle_keyword).
 * @param   pCompiler   The compiler state.
 * @param   pchWord     First char after 'define'.
 * @param   cchLeft     The number of chars left to parse on this line.
 * @param   fQualifiers The qualifiers.
 */
static int kmk_cc_eval_do_var_define(PKMKCCEVALCOMPILER pCompiler, const char *pchWord, size_t cchLeft, unsigned fQualifiers)
{

    KMK_CC_EVAL_SKIP_SPACES_AFTER_WORD(pCompiler, pchWord, cchLeft);
    kmk_cc_eval_fatal(pCompiler, pchWord, "define handling not implemented yet");
    return 1;
}


static int kmk_cc_eval_handle_assignment_or_recipe(PKMKCCEVALCOMPILER pCompiler, const char *pchWord, size_t cchLeft,
                                                   unsigned fQualifiers)
{
    /*
     * We're currently at a word which may or may not be a variable name
     * followed by an assignment operator, alternatively it must be a recipe.
     * We need to figure this out and deal with it in the most efficient
     * manner as this is a very common occurence.
     */
    unsigned const  iEscEolVarNm = pCompiler->iEscEol;
    int             fPlainVarNm  = 1;
    const char     *pchVarNm     = pchWord;
    size_t          cchVarNm;
    size_t          cch = 0;
    char            ch;

    /*
     * The variable name.  Complicate by there being no requirement of a space
     * preceeding the assignment operator, as well as that the variable name
     * may include variable references with spaces (function++) in them.
     */
    for (;;)
    {
        if (cch < cchLeft)
        { /*likely*/ }
        else
            kmk_cc_eval_fatal(pCompiler, &pchWord[cch], "Neither recipe nor variable assignment");

        ch = pchWord[cch];
        if (!KMK_CC_EVAL_IS_SPACE_DOLLAR_SLASH_OR_ASSIGN(ch))
            cch++;
        /* Space? */
        else if (KMK_CC_EVAL_IS_SPACE(ch))
        {
            cchVarNm = cch;
            pchWord += cch;
            cchLeft -= cch;
            KMK_CC_EVAL_SKIP_SPACES_AFTER_WORD(pCompiler, pchWord, cchLeft);
            break;
        }
        /* Variable expansion may contain spaces, so handle specially. */
        else if (ch == '$')
        {
            cch++;
            if (cch < cchLeft)
            {
                char const chOpen = pchWord[cch];
                if (chOpen == '(' || chOpen == '{')
                {
                    /*
                     * Got a $(VAR) or ${VAR} to deal with here.  This may
                     * include nested variable references and span multiple
                     * lines (at least for function calls).
                     *
                     * We scan forward till we've found the corresponding
                     * closing parenthesis, considering any open parentheses
                     * of the same kind as worth counting, even if there are
                     * no dollar preceeding them, just like GNU make does.
                     */
                    size_t const cchStart = cch - 1;
                    char const   chClose  = chOpen == '(' ? ')' : '}';
                    unsigned     cOpen    = 1;
                    cch++;
                    for (;;)
                    {
                        if (cch < cchLeft)
                        {
                            ch = pchWord[cch];
                            if (!(KMK_CC_EVAL_IS_PAREN_OR_SLASH(ch)))
                                cch++;
                            else
                            {
                                cch++;
                                if (ch == chClose)
                                {
                                    if (--cOpen == 0)
                                        break;
                                }
                                else if (ch == chOpen)
                                    cOpen++;
                                else if (   ch == '\\'
                                         && pCompiler->iEscEol < pCompiler->cEscEols
                                         &&    (size_t)(&pchWord[cch] - pCompiler->pszContent)
                                            == pCompiler->paEscEols[pCompiler->iEscEol].offEsc)
                                {
                                    cch += pCompiler->paEscEols[pCompiler->iEscEol].offEol
                                         - pCompiler->paEscEols[pCompiler->iEscEol].offEsc
                                         + pCompiler->cchEolSeq;
                                    pCompiler->iEscEol++;
                                }
                            }
                        }
                        else if (cOpen == 1)
                            kmk_cc_eval_fatal(pCompiler, &pchWord[cchStart], "Variable reference is missing '%c'", chClose);
                        else
                            kmk_cc_eval_fatal(pCompiler, &pchWord[cchStart],
                                              "%u variable references are missing '%c'", cOpen, chClose);
                    }
                }
                /* Single char variable name. */
                else if (!KMK_CC_EVAL_IS_SPACE(chOpen))
                {  /* likely */ }
                else
                    kmk_cc_eval_fatal(pCompiler, &pchWord[cch], "Expected variable name after '$', not end of line");
            }
            else
                kmk_cc_eval_fatal(pCompiler, &pchWord[cch], "Neither recipe nor variable assignment");
            fPlainVarNm = 0;
        }
        /* Check out potential recipe. */
        else if (ch == ':')
        {
            if (   cch + 1 < cchLeft
                && pchWord[cch + 1] != '=')
            {
                cchVarNm = cch;
                pchWord += cch;
                cchLeft -= cch;
                break;
            }
#ifdef HAVE_DOS_PATHS
            /* Don't confuse the first colon in:
                    C:/Windows/System32/Kernel32.dll: C:/Windows/System32/NtDll.dll
               for a recipe, it is only the second one which counts. */
            else if (   cch == 1
                     && isalpha((unsigned char)pchWord[0]))
                cch++;
#endif
            else
                return kmk_cc_eval_handle_recipe_cont_colon(pCompiler, pchWord, cch, pchWord + cch, cchLeft - cch, fQualifiers);
        }
        /* Check out assignment operator. */
        else if (ch == '=')
        {
            if (cch)
            {
                char chPrev = pchWord[cch - 1];
                if (chPrev == ':' || chPrev == '+' || chPrev == '?' || chPrev == '<')
                    cch--;
                cchVarNm = cch;
                pchWord += cch;
                cchLeft -= cch;
                break;
            }
            else
                kmk_cc_eval_fatal(pCompiler, pchWord, "Empty variable name.");
        }
        /* Check out potential escaped EOL sequence. */
        else if (ch == '\\')
        {
            unsigned const iEscEol = pCompiler->iEscEol;
            if (iEscEol >= pCompiler->cEscEols)
                cch++;
            else
            {
                size_t offCur = &pchWord[cch] - pCompiler->pszContent;
                if (offCur < pCompiler->paEscEols[iEscEol].offEol)
                    cch++;
                else
                {
                    cchVarNm = cch;
                    KMK_CC_ASSERT(offCur == pCompiler->paEscEols[iEscEol].offEol);
                    cch = pCompiler->paEscEols[iEscEol].offEol + pCompiler->cchEolSeq - offCur;
                    pCompiler->iEscEol = iEscEol + 1;
                    pchWord += cch;
                    cchLeft -= cch;
                    KMK_CC_EVAL_SKIP_SPACES(pCompiler, pchWord, cchLeft);
                    break;
                }
            }
        }
        else
            KMK_CC_ASSERT(0);
    }

    /*
     * Check for assignment operator.
     */
    if (cchLeft)
    {
        size_t              cchValue;
        PKMKCCEVALASSIGN    pInstr;
        KMKCCEVALINSTR      enmOpCode;
        int                 fPlainValue;
        char               *pszValue;

        ch = *pchWord;
        if (ch == '=')
        {
            enmOpCode = kKmkCcEvalInstr_assign_recursive;
            pchWord++;
            cchLeft--;
        }
        else if (cchLeft >= 2 && pchWord[1] == '=')
        {
            if (ch == ':')
                enmOpCode = kKmkCcEvalInstr_assign_simple;
            else if (ch == '+')
                enmOpCode = kKmkCcEvalInstr_assign_append;
            else if (ch == '<')
                enmOpCode = kKmkCcEvalInstr_assign_prepend;
            else if (ch == '?')
                enmOpCode = kKmkCcEvalInstr_assign_if_new;
            else
                return kmk_cc_eval_handle_recipe_cont_2nd_word(pCompiler, pchVarNm, cchVarNm, pchWord, cchLeft, fQualifiers);
            pchWord += 2;
            cchLeft -= 2;
        }
        else
            return kmk_cc_eval_handle_recipe_cont_2nd_word(pCompiler, pchVarNm, cchVarNm, pchWord, cchLeft, fQualifiers);

        /*
         * Skip leading spaces, if any and prep the value for copying.
         */
        KMK_CC_EVAL_SKIP_SPACES(pCompiler, pchWord, cchLeft);
        cchValue    = kmk_cc_eval_prep_normal_line(pCompiler, pchWord, cchLeft);
        fPlainValue = memchr(pchWord, '$', cchLeft) == NULL;


        /*
         * Emit the instruction.
         */
        kmk_cc_eval_end_of_recipe(pCompiler);

        pInstr = (PKMKCCEVALASSIGN)kmk_cc_block_alloc_eval(pCompiler->ppBlockTail, sizeof(*pInstr));
        pInstr->Core.enmOpcode = enmOpCode;
        pInstr->Core.iLine     = pCompiler->iLine;
        pInstr->fExport        = (fQualifiers & KMK_CC_EVAL_QUALIFIER_EXPORT)   != 0;
        pInstr->fOverride      = (fQualifiers & KMK_CC_EVAL_QUALIFIER_OVERRIDE) != 0;
        pInstr->fPrivate       = (fQualifiers & KMK_CC_EVAL_QUALIFIER_PRIVATE)  != 0;
        pInstr->fLocal         = (fQualifiers & KMK_CC_EVAL_QUALIFIER_LOCAL)    != 0;

        /* We copy the value before messing around with the variable name since
           we have to do more iEolEsc saves & restores the other way around. */
        pszValue = kmk_cc_eval_strdup_prepped(pCompiler, cchValue);
        if (fPlainVarNm)
            pchVarNm = strcache2_add(&variable_strcache, pchVarNm, cchVarNm);
        else
        {
            pCompiler->iEscEol = iEscEolVarNm;
            cchVarNm = kmk_cc_eval_prep_normal_line_ex(pCompiler, pchVarNm, cchVarNm);
            pchVarNm = kmk_cc_eval_strdup_prepped(pCompiler, cchVarNm);
        }
        kmk_cc_block_realign(pCompiler->ppBlockTail);
        KMK_CC_EVAL_DPRINTF(("%s: '%s' '%s'\n", g_apszEvalInstrNms[enmOpCode], pchVarNm, pszValue));

        kmk_cc_eval_init_subprogram_or_plain(pCompiler, &pInstr->Variable, pchVarNm, cchVarNm, fPlainVarNm);
        kmk_cc_eval_init_subprogram_or_plain(pCompiler, &pInstr->Value, pszValue, cchValue, fPlainValue);

        pInstr->pNext = (PKMKCCEVALCORE)kmk_cc_block_get_next_ptr(*pCompiler->ppBlockTail);
    }
    else
        kmk_cc_eval_fatal(pCompiler, pchWord, "Neither recipe nor variable assignment");
    return 1;
}


/**
 * Parses a 'local [override] variable = value', 'local define variable', and
 * 'local undefine variable [...]' expressions.
 *
 * The 'local' directive must be first and it does not permit any qualifiers at
 * the moment.  Should any be added later, they will have to come after 'local'.
 *
 * @returns 1 to indicate we've handled a keyword (see
 *          kmk_cc_eval_try_handle_keyword).
 * @param   pCompiler   The compiler state.
 * @param   pchWord     First char after 'local'.
 * @param   cchLeft     The number of chars left to parse on this line.
 * @param   fQualifiers The qualifiers.
 */
static int kmk_cc_eval_do_var_local(PKMKCCEVALCOMPILER pCompiler, const char *pchWord, size_t cchLeft)
{
    KMK_CC_EVAL_SKIP_SPACES_AFTER_WORD(pCompiler, pchWord, cchLeft);
    if (cchLeft)
    {
        /*
         * Check for 'local define' and 'local undefine'
         */
        if (KMK_CC_EVAL_WORD_COMP_CONST(pCompiler, pchWord, cchLeft, "define", 6))   /* final */
            return kmk_cc_eval_do_var_define(pCompiler, pchWord + 6, cchLeft + 6, KMK_CC_EVAL_QUALIFIER_LOCAL);
        if (KMK_CC_EVAL_WORD_COMP_CONST(pCompiler, pchWord, cchLeft, "undefine", 8))   /* final */
            return kmk_cc_eval_do_var_undefine(pCompiler, pchWord + 8, cchLeft + 8, KMK_CC_EVAL_QUALIFIER_LOCAL);

        /*
         * Simpler to just join paths with the rest here, even if we could
         * probably optimize the parsing a little if we liked.
         */
        return kmk_cc_eval_handle_assignment_or_recipe(pCompiler, pchWord, cchLeft, KMK_CC_EVAL_QUALIFIER_LOCAL);
    }
    kmk_cc_eval_fatal(pCompiler, pchWord, "Expected variable name, assignment operator and value after 'local'");
    return 1;
}


/**
 * We've found one variable qualification keyword, now continue parsing and see
 * if this is some kind of variable assignment expression or not.
 *
 * @returns 1 if variable assignment, 0 if not.
 * @param   pCompiler   The compiler state.
 * @param   pchWord     First char after the first qualifier.
 * @param   cchLeft     The number of chars left to parse on this line.
 * @param   fQualifiers The qualifier.
 */
static int kmk_cc_eval_try_handle_var_with_keywords(PKMKCCEVALCOMPILER pCompiler, const char *pchWord, size_t cchLeft,
                                                    unsigned fQualifiers)
{
    for (;;)
    {
        KMK_CC_EVAL_SKIP_SPACES_AFTER_WORD(pCompiler, pchWord, cchLeft);
        if (cchLeft)
        {
            char ch = *pchWord;
            if (KMK_CC_EVAL_IS_1ST_IN_VARIABLE_KEYWORD(ch))
            {
                if (KMK_CC_EVAL_WORD_COMP_CONST(pCompiler, pchWord, cchLeft, "define", 6))   /* final */
                    return kmk_cc_eval_do_var_define(pCompiler, pchWord + 6, cchLeft - 6, fQualifiers);

                if (KMK_CC_EVAL_WORD_COMP_CONST(pCompiler, pchWord, cchLeft, "undefine", 8)) /* final */
                    return kmk_cc_eval_do_var_undefine(pCompiler, pchWord + 8, cchLeft -86, fQualifiers);

                if (KMK_CC_EVAL_WORD_COMP_CONST(pCompiler, pchWord, cchLeft, "unexport", 8)) /* final */
                    return kmk_cc_eval_do_var_unexport(pCompiler, pchWord + 8, cchLeft - 8, fQualifiers);

                if (KMK_CC_EVAL_WORD_COMP_CONST(pCompiler, pchWord, cchLeft, "export", 6))
                {
                    if (!(fQualifiers & KMK_CC_EVAL_QUALIFIER_EXPORT))
                        fQualifiers |= KMK_CC_EVAL_QUALIFIER_EXPORT;
                    else
                        kmk_cc_eval_warn(pCompiler, pchWord, "'export' qualifier repeated");
                    pchWord += 6;
                    cchLeft -= 6;
                    continue;
                }

                if (KMK_CC_EVAL_WORD_COMP_CONST(pCompiler, pchWord, cchLeft, "override", 8))
                {
                    if (!(fQualifiers & KMK_CC_EVAL_QUALIFIER_OVERRIDE))
                        fQualifiers |= KMK_CC_EVAL_QUALIFIER_OVERRIDE;
                    else
                        kmk_cc_eval_warn(pCompiler, pchWord, "'override' qualifier repeated");
                    pchWord += 8;
                    cchLeft -= 8;
                    continue;
                }

                if (KMK_CC_EVAL_WORD_COMP_CONST(pCompiler, pchWord, cchLeft, "private", 7))
                {
                    if (!(fQualifiers & KMK_CC_EVAL_QUALIFIER_PRIVATE))
                        fQualifiers |= KMK_CC_EVAL_QUALIFIER_PRIVATE;
                    else
                        kmk_cc_eval_warn(pCompiler, pchWord, "'private' qualifier repeated");
                    pchWord += 7;
                    cchLeft -= 7;
                    continue;
                }
            }

            /*
             * Not a keyword, likely variable name followed by an assignment
             * operator and a value.  Do a rough check for the assignment operator
             * and join paths with the unqualified assignment handling code.
             */
            {
                const char *pchEqual = (const char *)memchr(pchWord, '=', cchLeft);
                if (pchEqual)
                    return kmk_cc_eval_handle_assignment_or_recipe(pCompiler, pchWord, cchLeft, fQualifiers);
            }
            return 0;
        }
        else
            kmk_cc_eval_fatal(pCompiler, NULL,
                              "Expected assignment operator or variable directive after variable qualifier(s)\n");
    }
}


/**
 * Parses 'export [variable]' and 'export [qualifiers] variable = value'
 * expressions.
 *
 * When we find the 'export' directive at the start of a line, we need to
 * continue parsing with till we can tell the difference between the two forms.
 *
 * @returns 1 to indicate we've handled a keyword (see
 *          kmk_cc_eval_try_handle_keyword).
 * @param   pCompiler   The compiler state.
 * @param   pchWord     First char after 'define'.
 * @param   cchLeft     The number of chars left to parse on this line.
 */
static int kmk_cc_eval_handle_var_export(PKMKCCEVALCOMPILER pCompiler, const char *pchWord, size_t cchLeft)
{
    KMK_CC_EVAL_SKIP_SPACES_AFTER_WORD(pCompiler, pchWord, cchLeft);

    if (cchLeft)
    {
        unsigned iSavedEscEol;
        unsigned cWords;

        /*
         * We need to figure out whether this is an assignment or a export statement,
         * in the latter case join paths with 'export' and 'undefine'.
         */
        const char *pchEqual = (const char *)memchr(pchWord, '=', cchLeft);
        if (!pchEqual)
            return kmk_cc_eval_do_with_variable_list(pCompiler, pchWord, cchLeft, kKmkCcEvalInstr_export, 0 /*fQualifiers*/);

        /*
         * Found an '=', could be an assignment.  Let's take the easy way out
         * and just parse the whole statement into words like we would do if
         * it wasn't an assignment, and then check the words out for
         * assignment keywords and operators.
         */
        iSavedEscEol = pCompiler->iEscEol;
        cWords       = kmk_cc_eval_parse_words(pCompiler, pchWord, cchLeft);
        if (cWords)
        {
            PKMKCCEVALVARIABLES pInstr;
            PKMKCCEVALWORD pWord = pCompiler->paWords;
            unsigned       iWord = 0;
            while (iWord < cWords)
            {
                /* Trailing assignment operator or terminal assignment directive ('undefine'
                   and 'unexport' makes no sense here but GNU make ignores that). */
                if (   (   pWord->cchWord > 1
                        && pWord->pchWord[pWord->cchWord - 1] == '=')
                    || KMK_CC_STRCMP_CONST(pWord->pchWord, pWord->cchWord, "define", 6)
                    || KMK_CC_STRCMP_CONST(pWord->pchWord, pWord->cchWord, "undefine", 8)
                    || KMK_CC_STRCMP_CONST(pWord->pchWord, pWord->cchWord, "unexport", 8) )
                {
                    pCompiler->iEscEol = iSavedEscEol;
                    return kmk_cc_eval_try_handle_var_with_keywords(pCompiler, pchWord, cchLeft, KMK_CC_EVAL_QUALIFIER_EXPORT);
                }

                /* If not a variable assignment qualifier, it must be a variable name
                   followed by an assignment operator. */
                if (iWord + 1 < cWords)
                {
                    if (   !KMK_CC_STRCMP_CONST(pWord->pchWord, pWord->cchWord, "export", 6)
                        && !KMK_CC_STRCMP_CONST(pWord->pchWord, pWord->cchWord, "private", 7)
                        && !KMK_CC_STRCMP_CONST(pWord->pchWord, pWord->cchWord, "override", 8))
                    {
                        pWord++;
                        if (   pWord->cchWord > 0
                            && (   pWord->pchWord[0] == '='
                                || (   pWord->cchWord > 1
                                    && pWord->pchWord[1] == '='
                                    && (   pWord->pchWord[0] == ':'
                                        || pWord->pchWord[0] == '+'
                                        || pWord->pchWord[0] == '?'
                                        || pWord->pchWord[0] == '<') ) ) )
                        {
                            pCompiler->iEscEol = iSavedEscEol;
                            return kmk_cc_eval_try_handle_var_with_keywords(pCompiler, pchWord, cchLeft,
                                                                            KMK_CC_EVAL_QUALIFIER_EXPORT);
                        }
                        break;
                    }
                }
                else
                    break;
                /* next */
                pWord++;
                iWord++;
            }

            /*
             * It's not an assignment.
             * (This is the same as kmk_cc_eval_do_with_variable_list does.)
             */
            pInstr = (PKMKCCEVALVARIABLES)kmk_cc_block_alloc_eval(pCompiler->ppBlockTail, KMKCCEVALVARIABLES_SIZE(cWords));
            pInstr->Core.enmOpcode = kKmkCcEvalInstr_export;
            pInstr->Core.iLine     = pCompiler->iLine;
            pInstr->cVars          = cWords;
            kmk_cc_eval_init_spp_array_from_duplicated_words(pCompiler, cWords, pCompiler->paWords, pInstr->aVars);
            kmk_cc_block_realign(pCompiler->ppBlockTail);
        }
        else
            KMK_CC_ASSERT(0);
    }
    else
    {
        /*
         * We're exporting all variables.
         */
        PKMKCCEVALCORE pInstr = kmk_cc_block_alloc_eval(pCompiler->ppBlockTail, sizeof(*pInstr));
        pInstr->enmOpcode = kKmkCcEvalInstr_export_all;
        pInstr->iLine     = pCompiler->iLine;
    }
    return 1;
}


/**
 * When entering this function we know that the first two character in the first
 * word both independently occurs in keywords.
 *
 * @returns 1 if make directive or qualified variable assignment, 0 if neither.
 * @param   pCompiler   The compiler state.
 * @param   ch          The first char.
 * @param   pchWord     Pointer to the first word.
 * @param   cchLeft     Number of characters left to parse starting at
 *                      @a cchLeft.
 */
int kmk_cc_eval_try_handle_keyword(PKMKCCEVALCOMPILER pCompiler, char ch, const char *pchWord, size_t cchLeft)
{
    unsigned iSavedEscEol = pCompiler->iEscEol;

    KMK_CC_ASSERT(cchLeft >= 2);
    KMK_CC_ASSERT(ch == pchWord[0]);
    KMK_CC_ASSERT(KMK_CC_EVAL_IS_1ST_IN_KEYWORD(pchWord[0]));
    KMK_CC_ASSERT(KMK_CC_EVAL_IS_2ND_IN_KEYWORD(pchWord[1]));

    /*
     * If it's potentially a variable related keyword, check that out first.
     */
    if (KMK_CC_EVAL_IS_1ST_IN_VARIABLE_KEYWORD(ch))
    {
        if (KMK_CC_EVAL_WORD_COMP_CONST(pCompiler, pchWord, cchLeft, "local", 5))
            return kmk_cc_eval_do_var_local(pCompiler, pchWord + 5, cchLeft - 5);
        if (KMK_CC_EVAL_WORD_COMP_CONST(pCompiler, pchWord, cchLeft, "define", 6))
            return kmk_cc_eval_do_var_define(pCompiler, pchWord + 6, cchLeft - 6, 0);
        if (KMK_CC_EVAL_WORD_COMP_CONST(pCompiler, pchWord, cchLeft, "export", 6))
            return kmk_cc_eval_handle_var_export(pCompiler, pchWord + 6, cchLeft - 6);
        if (KMK_CC_EVAL_WORD_COMP_CONST(pCompiler, pchWord, cchLeft, "undefine", 8))
            return kmk_cc_eval_do_var_undefine(pCompiler, pchWord + 8, cchLeft - 8, 0);
        if (KMK_CC_EVAL_WORD_COMP_CONST(pCompiler, pchWord, cchLeft, "unexport", 8))
            return kmk_cc_eval_do_var_unexport(pCompiler, pchWord + 8, cchLeft - 8, 0);
        if (KMK_CC_EVAL_WORD_COMP_CONST(pCompiler, pchWord, cchLeft, "override", 8))
        {
            if (kmk_cc_eval_try_handle_var_with_keywords(pCompiler, pchWord + 8, cchLeft - 8, KMK_CC_EVAL_QUALIFIER_OVERRIDE))
                return 1;
            pCompiler->iEscEol = iSavedEscEol;
        }
        else if (KMK_CC_EVAL_WORD_COMP_CONST(pCompiler, pchWord, cchLeft, "private", 7))
        {
            if (kmk_cc_eval_try_handle_var_with_keywords(pCompiler, pchWord + 7, cchLeft - 7, KMK_CC_EVAL_QUALIFIER_PRIVATE))
                return 1;
            pCompiler->iEscEol = iSavedEscEol;
        }
    }

    /*
     * Check out the other keywords.
     */
    if (ch == 'i') /* Lots of directives starting with 'i'. */
    {
        char ch2 = pchWord[1];
        pchWord += 2;
        cchLeft -= 2;

        /* 'if...' */
        if (ch2 == 'f')
        {
            if (KMK_CC_EVAL_WORD_COMP_IS_EOL(pCompiler, pchWord, cchLeft))
                return kmk_cc_eval_do_if(pCompiler, pchWord, cchLeft, 0 /* in else */);

            if (KMK_CC_EVAL_WORD_COMP_CONST(pCompiler, pchWord, cchLeft, "eq", 2))
                return kmk_cc_eval_do_ifeq( pCompiler, pchWord + 2, cchLeft - 2, 0 /* in else */, 1 /* positive */);

            if (KMK_CC_EVAL_WORD_COMP_CONST(pCompiler, pchWord, cchLeft, "def", 3))
                return kmk_cc_eval_do_ifdef(pCompiler, pchWord + 3, cchLeft - 3, 0 /* in else */, 1 /* positive */);

            if (KMK_CC_EVAL_WORD_COMP_CONST(pCompiler, pchWord, cchLeft, "neq", 3))
                return kmk_cc_eval_do_ifeq( pCompiler, pchWord + 3, cchLeft - 3, 0 /* in else */, 0 /* positive */);

            if (KMK_CC_EVAL_WORD_COMP_CONST(pCompiler, pchWord, cchLeft, "1of", 3))
                return kmk_cc_eval_do_if1of(pCompiler, pchWord + 3, cchLeft - 3, 0 /* in else */, 1 /* positive */);

            if (KMK_CC_EVAL_WORD_COMP_CONST(pCompiler, pchWord, cchLeft, "ndef", 4))
                return kmk_cc_eval_do_ifdef(pCompiler, pchWord + 4, cchLeft - 4, 0 /* in else */, 0 /* positive */);

            if (KMK_CC_EVAL_WORD_COMP_CONST(pCompiler, pchWord, cchLeft, "n1of", 4))
                return kmk_cc_eval_do_if1of(pCompiler, pchWord + 4, cchLeft - 4, 0 /* in else */, 0 /* positive */);
        }
        /* include... */
        else if (ch2 == 'n' && cchLeft >= 5 && KMK_CC_WORD_COMP_CONST_5(pchWord, "clude") ) /* 'in...' */
        {
            pchWord += 5;
            cchLeft -= 5;
            if (KMK_CC_EVAL_WORD_COMP_IS_EOL(pCompiler, pchWord, cchLeft))
                return kmk_cc_eval_do_include(pCompiler, pchWord, cchLeft, kKmkCcEvalInstr_include);
            if (cchLeft >= 3 && KMK_CC_WORD_COMP_CONST_3(pchWord, "dep"))
            {
                pchWord += 3;
                cchLeft -= 3;
                if (KMK_CC_EVAL_WORD_COMP_IS_EOL(pCompiler, pchWord, cchLeft))
                    return kmk_cc_eval_do_include(pCompiler, pchWord, cchLeft, kKmkCcEvalInstr_includedep);
                if (KMK_CC_EVAL_WORD_COMP_CONST(pCompiler, pchWord, cchLeft, "-queue", 6))
                    return kmk_cc_eval_do_include(pCompiler, pchWord + 6, cchLeft - 6, kKmkCcEvalInstr_includedep_queue);
                if (KMK_CC_EVAL_WORD_COMP_CONST(pCompiler, pchWord, cchLeft, "-flush", 6))
                    return kmk_cc_eval_do_include(pCompiler, pchWord + 6, cchLeft - 6, kKmkCcEvalInstr_includedep_flush);
            }
        }
    }
    else if (ch == 'e') /* A few directives starts with 'e'. */
    {
        if (KMK_CC_EVAL_WORD_COMP_CONST(pCompiler, pchWord, cchLeft, "else", 4))
            return kmk_cc_eval_do_else(pCompiler, pchWord + 4, cchLeft - 4);
        if (KMK_CC_EVAL_WORD_COMP_CONST(pCompiler, pchWord, cchLeft, "endif", 5))
            return kmk_cc_eval_do_endif(pCompiler, pchWord + 5, cchLeft - 5);
        /* export and endef are handled elsewhere, though stray endef's may end up here... */
        KMK_CC_ASSERT(!KMK_CC_EVAL_WORD_COMP_CONST(pCompiler, pchWord, cchLeft, "export", 6));

    }
    else /* the rest. */
    {
        if (   KMK_CC_EVAL_WORD_COMP_CONST(pCompiler, pchWord, cchLeft, "sinclude", 8)
            || KMK_CC_EVAL_WORD_COMP_CONST(pCompiler, pchWord, cchLeft, "-include", 8))
            return kmk_cc_eval_do_include(pCompiler, pchWord + 8, cchLeft - 8, kKmkCcEvalInstr_include_silent);
        if (KMK_CC_EVAL_WORD_COMP_CONST(pCompiler, pchWord, cchLeft, "vpath", 5))
            return kmk_cc_eval_do_vpath(pCompiler, pchWord + 5, cchLeft - 5);

        KMK_CC_ASSERT(!KMK_CC_EVAL_WORD_COMP_CONST(pCompiler, pchWord, cchLeft, "local", 5));
        KMK_CC_ASSERT(!KMK_CC_EVAL_WORD_COMP_CONST(pCompiler, pchWord, cchLeft, "define", 6));
        KMK_CC_ASSERT(!KMK_CC_EVAL_WORD_COMP_CONST(pCompiler, pchWord, cchLeft, "private", 7));
        KMK_CC_ASSERT(!KMK_CC_EVAL_WORD_COMP_CONST(pCompiler, pchWord, cchLeft, "override", 8));
        KMK_CC_ASSERT(!KMK_CC_EVAL_WORD_COMP_CONST(pCompiler, pchWord, cchLeft, "unexport", 8));
        KMK_CC_ASSERT(!KMK_CC_EVAL_WORD_COMP_CONST(pCompiler, pchWord, cchLeft, "undefine", 8));
    }

    pCompiler->iEscEol = iSavedEscEol;
    return 0;
}




static int kmk_cc_eval_compile_worker(PKMKCCEVALPROG pEvalProg, const char *pszContent, size_t cchContent, unsigned iLine)
{
    const char *pchTmp;

    /*
     * Compiler state.
     */
    KMKCCEVALCOMPILER   Compiler;
    kmk_cc_eval_init_compiler(&Compiler, pEvalProg, iLine, pszContent, cchContent);
    KMK_CC_EVAL_DPRINTF(("\nkmk_cc_eval_compile_worker - begin (%s/%s/%d)\n", pEvalProg->pszFilename, pEvalProg->pszVarName, iLine));

    {
        /*
         * Line state.
         */
        size_t              cchLine;                                /* The length of the current line (w/o comments). */
        size_t              offNext = 0;                            /* The offset of the next line. */
        size_t              off     = 0;                            /* The offset into pszContent of the current line. */

        /* Try for some register/whatever optimzations. */
        int const           chFirstEol = Compiler.chFirstEol;
        size_t const        cchEolSeq  = Compiler.cchEolSeq;

        /*
         * Process input lines.
         *
         * The code here concerns itself with getting the next line in an efficient
         * manner, very basic classification and trying out corresponding handlers.
         * The real work is done in the handlers.
         */
        while (offNext < cchContent)
        {
            size_t offFirstWord;

            /*
             * Find the end of the next line.
             */
            KMK_CC_ASSERT(off == offNext);

            /* Simple case: No escaped EOL, nor the end of the input. */
            pchTmp = (const char *)memchr(&pszContent[offNext], chFirstEol, cchContent - offNext);
            if (   pchTmp
                && (   &pszContent[offNext] == pchTmp
                    || pchTmp[-1] != '\\') )
            {
                if (   cchEolSeq == 1
                    || pchTmp[1] == Compiler.chSecondEol)
                {
                    /* Frequent: Blank line. */
                    if (&pszContent[offNext] == pchTmp)
                    {
                        KMK_CC_EVAL_DPRINTF(("#%03u: <empty>\n", Compiler.iLine));
                        Compiler.iLine++;
                        off = offNext += cchEolSeq;
                        continue;
                    }
                    if (pszContent[offNext] == '#')
                    {
                        KMK_CC_EVAL_DPRINTF(("#%03u: <comment>\n", Compiler.iLine));
                        Compiler.iLine++;
                        offNext = pchTmp - pszContent;
                        off = offNext += cchEolSeq;
                        continue;
                    }

                    offNext  = pchTmp - pszContent;
                    cchLine  = offNext - off;

                    offFirstWord = off;
                    while (offFirstWord < offNext && KMK_CC_EVAL_IS_SPACE(pszContent[offFirstWord]))
                        offFirstWord++;

                    offNext += cchEolSeq;
                    Compiler.cEscEols = 0;
                    Compiler.iEscEol  = 0;
                }
                else
                    kmk_cc_eval_fatal_eol(&Compiler, pchTmp, Compiler.iLine, off);
            }
            /* The complicated, less common cases. */
            else
            {
                Compiler.cEscEols = 0;
                Compiler.iEscEol  = 0;
                offFirstWord = offNext;
                for (;;)
                {
                    if (offFirstWord == offNext)
                    {
                        size_t offEol = off + cchLine;
                        while (offFirstWord < offEol && KMK_CC_EVAL_IS_SPACE(pszContent[offFirstWord]))
                            offFirstWord++;
                    }

                    if (pchTmp)
                    {
                        if (   cchEolSeq == 1
                            || pchTmp[1] == Compiler.chSecondEol)
                        {
                            size_t offEsc;
                            if (offFirstWord != offNext)
                                offNext = pchTmp - pszContent;
                            else
                            {
                                offNext = pchTmp - pszContent;
                                while (offFirstWord < offNext && KMK_CC_EVAL_IS_SPACE(pszContent[offFirstWord]))
                                    offFirstWord++;
                            }


                            /* Is it an escape sequence? */
                            if (   !offNext
                                || pchTmp[-1] != '\\')
                            {
                                cchLine  = offNext - off;
                                offNext += cchEolSeq;
                                break;
                            }
                            if (offNext < 2 || pchTmp[-2] != '\\')
                                offEsc = offNext - 1;
                            else
                            {
                                /* Count how many backslashes there are. Must be odd number to be an escape
                                   sequence.  Normally we keep half of them, except for command lines.  */
                                size_t cSlashes = 2;
                                while (offNext >= cSlashes && pchTmp[0 - cSlashes] == '\\')
                                    cSlashes--;
                                if (!(cSlashes & 1))
                                {
                                    cchLine  = offNext - off;
                                    offNext += cchEolSeq;
                                    break;
                                }
                                offEsc = offNext - (cSlashes >> 1);
                            }

                            /* Record it. */
                            if (Compiler.cEscEols < Compiler.cEscEolsAllocated) { /* likely */ }
                            else
                            {
                                KMK_CC_ASSERT(Compiler.cEscEols == Compiler.cEscEolsAllocated);
                                Compiler.cEscEolsAllocated = Compiler.cEscEolsAllocated
                                                           ? Compiler.cEscEolsAllocated * 2 : 2;
                                Compiler.paEscEols = (PKMKCCEVALESCEOL)xrealloc(Compiler.paEscEols,
                                                                                  Compiler.cEscEolsAllocated
                                                                                * sizeof(Compiler.paEscEols[0]));
                            }
                            Compiler.paEscEols[Compiler.cEscEols].offEsc = offEsc;
                            Compiler.paEscEols[Compiler.cEscEols].offEol = offNext;
                            Compiler.cEscEols++;

                            /* Advance. */
                            offNext += cchEolSeq;
                            if (offFirstWord == offEsc)
                            {
                                offFirstWord = offNext;
                                Compiler.iEscEol++;
                            }
                        }
                        else
                            kmk_cc_eval_fatal_eol(&Compiler, pchTmp, Compiler.iLine, off);
                    }
                    else
                    {
                        /* End of input. Happens only once per compilation, nothing to optimize for. */
                        if (offFirstWord == offNext)
                            while (offFirstWord < cchContent && KMK_CC_EVAL_IS_SPACE(pszContent[offFirstWord]))
                                offFirstWord++;
                        offNext = cchContent;
                        cchLine = cchContent - off;
                        break;
                    }
                    pchTmp = (const char *)memchr(&pszContent[offNext], chFirstEol, cchContent - offNext);
                }
            }
            KMK_CC_ASSERT(offNext       <= cchContent);
            KMK_CC_ASSERT(offNext       >= off + cchLine);
            KMK_CC_ASSERT(off + cchLine <= cchContent && cchLine <= cchContent);
            KMK_CC_ASSERT(offFirstWord  <= off + cchLine);
            KMK_CC_ASSERT(offFirstWord  >= off);
            KMK_CC_ASSERT(pszContent[offFirstWord] != ' ' && pszContent[offFirstWord] != '\t');

            KMK_CC_EVAL_DPRINTF(("#%03u: %*.*s\n", Compiler.iLine, (int)cchLine, (int)cchLine, &pszContent[off]));

            /*
             * Skip blank lines.
             */
            if (offFirstWord < off + cchLine)
            {
                /*
                 * Command? Ignore command prefix if no open recipe (SunOS 4 behavior).
                 */
                if (   pszContent[off] == Compiler.chCmdPrefix
                    && (Compiler.pRecipe || Compiler.fNoTargetRecipe))
                {
                    if (!Compiler.fNoTargetRecipe)
                        kmk_cc_eval_handle_command(&Compiler, &pszContent[off], cchLine);
                }
                /*
                 * Since it's not a command line, we can now skip comment lines
                 * even with a tab indentation.  If it's not a comment line, we
                 * tentatively strip any trailing comment.
                 */
                else if (pszContent[offFirstWord] != '#')
                {
                    const char *pchWord = &pszContent[offFirstWord];
                    size_t      cchLeft = off + cchLine - offFirstWord;
                    char        ch;

                    Compiler.cchLineWithComments =  cchLine;
                    pchTmp = (const char *)memchr(pchWord, '#', cchLeft);
                    if (pchTmp)
                    {
                        cchLeft = pchTmp - pchWord;
                        cchLine = pchTmp - &pszContent[off];
                    }
                    Compiler.cchLine = cchLine;
                    Compiler.offLine = off;

                    /*
                     * If not a directive or variable qualifier, it's either a variable
                     * assignment or a recipe.
                     */
                    ch = *pchWord;
                    if (   !KMK_CC_EVAL_IS_1ST_IN_KEYWORD(ch)
                        || !KMK_CC_EVAL_IS_2ND_IN_KEYWORD(pchWord[1])
                        || !kmk_cc_eval_try_handle_keyword(&Compiler, ch, pchWord, cchLeft) )
                    {
                        pchTmp = (const char *)memchr(pchWord, '=', cchLeft);
                        if (pchTmp)
                            kmk_cc_eval_handle_assignment_or_recipe(&Compiler, pchWord, cchLeft, 0 /*fQualifiers*/);
                        else
                            kmk_cc_eval_handle_recipe(&Compiler, pchTmp, pchWord, cchLeft);
                    }
                    /* else: handled a keyword expression */
                }
            }

            /*
             * Advance to the next line.
             */
            off             = offNext;
            Compiler.iLine += Compiler.cEscEols + 1;
        }
    }

    /*
     * Check whether
     */

    kmk_cc_eval_delete_compiler(&Compiler);
    KMK_CC_EVAL_DPRINTF(("kmk_cc_eval_compile_worker - done (%s/%s)\n\n", pEvalProg->pszFilename, pEvalProg->pszVarName));
    return 0;
}



static PKMKCCEVALPROG kmk_cc_eval_compile(const char *pszContent, size_t cchContent,
                                          const char *pszFilename, unsigned iLine, const char *pszVarName)
{
    /*
     * Estimate block size, allocate one and initialize it.
     */
    PKMKCCEVALPROG  pEvalProg;
    PKMKCCBLOCK     pBlock;
    pEvalProg = kmk_cc_block_alloc_first(&pBlock, sizeof(*pEvalProg), cchContent / 32); /** @todo adjust */
    if (pEvalProg)
    {
        pEvalProg->pBlockTail   = pBlock;
        pEvalProg->pFirstInstr  = (PKMKCCEVALCORE)kmk_cc_block_get_next_ptr(pBlock);
        pEvalProg->pszFilename  = pszFilename ? pszFilename : "<unknown>";
        pEvalProg->pszVarName   = pszVarName;
        pEvalProg->cRefs        = 1;
#ifdef KMK_CC_STRICT
        pEvalProg->uInputHash   = kmk_cc_debug_string_hash_n(0, pszContent, cchContent);
#endif

        /*
         * Do the actual compiling.
         */
#ifdef CONFIG_WITH_EVAL_COMPILER
        if (kmk_cc_eval_compile_worker(pEvalProg, pszContent, cchContent, iLine) == 0)
#else
        if (0)
#endif
        {
#ifdef KMK_CC_WITH_STATS
            pBlock = pEvalProg->pBlockTail;
            if (!pBlock->pNext)
                g_cSingleBlockEvalProgs++;
            else if (!pBlock->pNext->pNext)
                g_cTwoBlockEvalProgs++;
            else
                g_cMultiBlockEvalProgs++;
            for (; pBlock; pBlock = pBlock->pNext)
            {
                g_cBlocksAllocatedEvalProgs++;
                g_cbAllocatedEvalProgs += pBlock->cbBlock;
                g_cbUnusedMemEvalProgs += pBlock->cbBlock - pBlock->offNext;
            }
#endif
            return pEvalProg;
        }
        kmk_cc_block_free_list(pEvalProg->pBlockTail);
    }
    return NULL;
}


/**
 * Compiles a variable direct evaluation as is, setting v->evalprog on success.
 *
 * @returns Pointer to the program on success, NULL if no program was created.
 * @param   pVar        Pointer to the variable.
 */
struct kmk_cc_evalprog   *kmk_cc_compile_variable_for_eval(struct variable *pVar)
{
    PKMKCCEVALPROG pEvalProg = pVar->evalprog;
    if (!pEvalProg)
    {
#ifdef CONFIG_WITH_EVAL_COMPILER
        pEvalProg = kmk_cc_eval_compile(pVar->value, pVar->value_length,
                                        pVar->fileinfo.filenm, pVar->fileinfo.lineno, pVar->name);
        pVar->evalprog = pEvalProg;
#endif
        g_cVarForEvalCompilations++;
    }
    return pEvalProg;
}


/**
 * Compiles a makefile for
 *
 * @returns Pointer to the program on success, NULL if no program was created.
 * @param   pVar        Pointer to the variable.
 */
struct kmk_cc_evalprog   *kmk_cc_compile_file_for_eval(FILE *pFile, const char *pszFilename)
{
    PKMKCCEVALPROG  pEvalProg;

    /*
     * Read the entire file into a zero terminate memory buffer.
     */
    size_t          cchContent = 0;
    char           *pszContent = NULL;
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
    pEvalProg = kmk_cc_eval_compile(pszContent, cchContent, pszFilename, 1, NULL /*pszVarName*/);
    g_cFileForEvalCompilations++;

    free(pszContent);
    if (!pEvalProg)
        fseek(pFile, 0, SEEK_SET);
    return pEvalProg;
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

    if (pVar->evalprog)
    {
        kmk_cc_block_free_list(pVar->evalprog->pBlockTail);
        pVar->evalprog = NULL;
    }

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

    if (pVar->evalprog)
    {
        kmk_cc_block_free_list(pVar->evalprog->pBlockTail);
        pVar->evalprog = NULL;
    }

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

