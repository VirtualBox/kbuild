#ifdef CONFIG_WITH_COMPILER
/* $Id: kmk_cc_exec.c 2767 2015-01-30 03:33:33Z knut.osmundsen@oracle.com $ */
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


/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
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


/*******************************************************************************
*   Structures and Typedefs                                                    *
*******************************************************************************/
/**
 * Block of expand instructions.
 *
 * To avoid wasting space on "next" pointers, as well as a lot of time walking
 * these chains when destroying programs, we work with blocks of instructions.
 */
typedef struct kmk_cc_exp_block
{
    /** The pointer to the next block (LIFO). */
    struct kmk_cc_exp_block     *pNext;
    /** The size of this block. */
    uint32_t                     cbBlock;
} KMKCCEXPBLOCK;
typedef KMKCCEXPBLOCK *PKMKCCEXPBLOCK;

/** Expansion instructions. */
typedef enum KMKCCEXPINSTR
{
    /** Copy a plain string. */
    kKmkCcExpInstr_CopyString = 0,
    /** Insert an expanded variable value, which name we already know.  */
    kKmkCcExpInstr_PlainVariable,
    /** Insert an expanded variable value, the name is dynamic (sub prog). */
    kKmkCcExpInstr_DynamicVariable,
    /** Insert the output of function that requires no argument expansion. */
    kKmkCcExpInstr_PlainFunction,
    /** Insert the output of function that requires dynamic expansion of one ore
     * more arguments. */
    kKmkCcExpInstr_DynamicFunction,
    /** Jump to a new instruction block. */
    kKmkCcExpInstr_Jump,
    /** We're done, return.  Has no specific structure. */
    kKmkCcExpInstr_Done,
    /** The end of valid instructions (exclusive). */
    kKmkCcExpInstr_End
} KMKCCEXPANDINSTR;

/** Instruction core. */
typedef struct kmk_cc_exp_core
{
    /** The instruction opcode number (KMKCCEXPANDINSTR). */
    KMKCCEXPANDINSTR        enmOpCode;
} KMKCCEXPCORE;
typedef KMKCCEXPCORE *PKMKCCEXPCORE;

typedef struct kmk_cc_exp_subprog
{
    /** Pointer to the first instruction. */
    PKMKCCEXPCORE           pFirstInstr;
    /** Max expanded size. */
    uint32_t                cbMax;
} KMKCCEXPSUBPROG;
typedef KMKCCEXPSUBPROG *PKMKCCEXPSUBPROG;

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

typedef struct kmk_cc_exp_plain_variable
{
    /** The core instruction. */
    KMKCCEXPCORE            Core;
    /** The variable strcache entry for this variable. */
    struct strcache2_entry *pNameEntry;
} KMKCCEXPPLAINVAR;
typedef KMKCCEXPPLAINVAR *PKMKCCEXPPLAINVAR;

typedef struct kmk_cc_exp_dynamic_variable
{
    /** The core instruction. */
    KMKCCEXPCORE            Core;
    /** Where to continue after this instruction.  This is necessary since the
     * subprogram is allocated after us in the instruction block.  Since the sub
     * program is of variable size, we don't even know if we're still in the same
     * instruction block.  So, we include a jump here. */
    PKMKCCEXPCORE           pNext;
    /** The subprogram that will give us the variable name. */
    KMKCCEXPSUBPROG         SubProg;
} KMKCCEXPDYNVAR;
typedef KMKCCEXPDYNVAR *PKMKCCEXPDYNVAR;

typedef struct kmk_cc_exp_function_core
{
    /** The core instruction. */
    KMKCCEXPCORE            Core;
    /** Number of arguments. */
    uint8_t                 cArgs;
    /** Where to continue after this instruction.  This is necessary since the
     * instruction is of variable size and we don't even know if we're still in the
     * same instruction block.  So, we include a jump here. */
    PKMKCCEXPCORE           pNext;
    /**
     * Pointer to the function table entry.
     *
     * @returns New variable buffer position.
     * @param   pchDst      Current variable buffer position.
     * @param   papszArgs   Pointer to a NULL terminated array of argument strings.
     * @param   pszFuncName The name of the function being called.
     */
    char *                 (*pfnFunction)(char *pchDst, char **papszArgs, const char *pszFuncName);
    /** Pointer to the function name in the variable string cache. */
    const char             *pszFuncName;
} KMKCCEXPFUNCCORE;
typedef KMKCCEXPFUNCCORE *PKMKCCEXPFUNCCORE;

typedef struct kmk_cc_exp_plain_function
{
    /** The bits comment to both plain and dynamic functions. */
    KMKCCEXPFUNCCORE        Core;
    /** Variable sized argument list (cArgs + 1 in length, last entry is NULL).
     * The string pointers are to memory following this instruction, to memory in
     * the next block or to memory in the variable / makefile we're working on
     * (if zero terminated appropriately). */
    const char             *apszArgs[1];
} KMKCCEXPPLAINFUNC;
typedef KMKCCEXPPLAINFUNC *PKMKCCEXPPLAINFUNC;

typedef struct kmk_cc_exp_dyn_function
{
    /** The bits comment to both plain and dynamic functions. */
    KMKCCEXPFUNCCORE        Core;
    /** Variable sized argument list (cArgs + 1 in length, last entry is NULL).
     * The string pointers are to memory following this instruction, to memory in
     * the next block or to memory in the variable / makefile we're working on
     * (if zero terminated appropriately). */
    struct
    {
        /** Set if plain string argument, clear if sub program. */
        uint8_t             fPlain;
        union
        {
            /** Sub program for expanding this argument. */
            KMKCCEXPSUBPROG     SubProg;
            struct
            {
                /** Pointer to the plain argument string.
                 * This is allocated in the same manner as the
                 * string pointed to by KMKCCEXPPLAINFUNC::apszArgs. */
                const char      *pszArg;
            } Plain;
        } u;
    }                       aArgs[1];
} KMKCCEXPDYNFUNC;
typedef KMKCCEXPDYNFUNC *PKMKCCEXPDYNFUNC;

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
    PKMKCCEXPCORE   pFirstInstr;
    /** List of blocks for this program (LIFO). */
    PKMKCCEXPBLOCK  pBlockTail;
    /** Max expanded size. */
    uint32_t        cbMax;
} KMKCCEXPANDPROG;
/** Pointer to a string expansion program. */
typedef KMKCCEXPANDPROG *PKMKCCEXPANDPROG;


/*******************************************************************************
*   Global Variables                                                           *
*******************************************************************************/


/**
 * Initializes global variables for the 'compiler'.
 */
void kmk_cc_init(void)
{
}


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
 * Compiles a variable for string expansion.
 *
 * @returns Pointer to the string expansion program on success, NULL if no
 *          program was created.
 * @param   pVar        Pointer to the variable.
 */
struct kmk_cc_expandprog *kmk_cc_compile_variable_for_expand(struct variable *pVar)
{
    assert(!pVar->evalprog);

    //memchr()




    return NULL;
}


/**
 * Equivalent of eval_buffer, only it's using the evalprog of the variable.
 *
 * @param   pVar        Pointer to the variable. Must have a program.
 */
void kmk_exec_evalval(struct variable *pVar)
{
    assert(pVar->evalprog);
    assert(0);
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
    assert(pVar->expandprog);
    assert(0);
    return pchDst;
}


/**
 * Called when a variable with expandprog or/and evalprog changes.
 *
 * @param   pVar        Pointer to the variable.
 */
void  kmk_cc_variable_changed(struct variable *pVar)
{
    assert(pVar->evalprog || pVar->expandprog);
}


/**
 * Called when a variable with expandprog or/and evalprog is deleted.
 *
 * @param   pVar        Pointer to the variable.
 */
void  kmk_cc_variable_deleted(struct variable *pVar)
{
    assert(pVar->evalprog || pVar->expandprog);
}


#endif /* CONFIG_WITH_COMPILER */

