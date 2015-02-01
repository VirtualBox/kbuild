#ifdef CONFIG_WITH_COMPILER
/* $Id: kmk_cc_exec.c 2769 2015-02-01 00:44:30Z knut.osmundsen@oracle.com $ */
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

/**
 * String expansion statistics.
 */
typedef struct KMKCCEXPSTATS
{
    /** Max expanded size. */
    uint32_t                    cchMax;
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
} KMKCCEXPANDINSTR;

/** Instruction core. */
typedef struct kmk_cc_exp_core
{
    /** The instruction opcode number (KMKCCEXPANDINSTR). */
    KMKCCEXPANDINSTR        enmOpCode;
} KMKCCEXPCORE;
typedef KMKCCEXPCORE *PKMKCCEXPCORE;

/**
 * String expansion sub program.
 */
typedef struct kmk_cc_exp_subprog
{
    /** Pointer to the first instruction. */
    PKMKCCEXPCORE           pFirstInstr;
    /** Statistics. */
    KMKCCEXPSTATS           Stats;
} KMKCCEXPSUBPROG;
typedef KMKCCEXPSUBPROG *PKMKCCEXPSUBPROG;

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
    /** Where to continue after this instruction.  (This is necessary since the
     * instructions of the subprogram are emitted after this instruction.) */
    PKMKCCEXPCORE           pNext;
    /** The subprogram that will give us the variable name. */
    KMKCCEXPSUBPROG         SubProg;
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
    uint32_t                cArgs;
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
    char *                 (*pfnFunction)(char *pchDst, char **papszArgs, const char *pszFuncName);
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
    KMKCCEXPFUNCCORE        Core;
    /** Variable sized argument list (cArgs + 1 in length, last entry is NULL).
     * The string pointers are to memory following this instruction, to memory in
     * the next block or to memory in the variable / makefile we're working on
     * (if zero terminated appropriately). */
    const char             *apszArgs[1];
} KMKCCEXPPLAINFUNC;
typedef KMKCCEXPPLAINFUNC *PKMKCCEXPPLAINFUNC;
/** Calculates the size of an KMKCCEXPPLAINFUNC with a_cArgs. */
#define KMKCCEXPPLAINFUNC_SIZE(a_cArgs)  (sizeof(KMKCCEXPFUNCCORE) + (a_cArgs + 1) * sizeof(const char *))

/**
 * Instruction format for kKmkCcExpInstr_DynamicFunction.
 */
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
/** Calculates the size of an KMKCCEXPPLAINFUNC with a_cArgs. */
#define KMKCCEXPDYNFUNC_SIZE(a_cArgs)  (  sizeof(KMKCCEXPFUNCCORE) \
                                          + (a_cArgs) * sizeof(((PKMKCCEXPDYNFUNC)(uintptr_t)42)->aArgs[0]) )

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
} KMKCCEXPPROG;
/** Pointer to a string expansion program. */
typedef KMKCCEXPPROG *PKMKCCEXPPROG;


/*******************************************************************************
*   Global Variables                                                           *
*******************************************************************************/


/*******************************************************************************
*   Internal Functions                                                         *
*******************************************************************************/
static int kmk_cc_exp_compile_subprog(PKMKCCBLOCK *ppBlockTail, const char *pchStr, uint32_t cchStr, PKMKCCEXPSUBPROG pSubProg);
static char *kmk_exec_expand_subprog_to_tmp(PKMKCCEXPSUBPROG pSubProg, uint32_t *pcch);


/**
 * Initializes global variables for the 'compiler'.
 */
void kmk_cc_init(void)
{
}


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

    assert(((cbFirst + sizeof(void *) - 1) & (sizeof(void *) - 1)) == 0);

    /*
     * Turn the hint into a block size.
     */
    if (cbHint <= 512)
        cbBlock = 512;
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
        assert(pBlockTail->cbBlock - pBlockTail->offNext >= sizeof(KMKCCEXPJUMP));
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

    assert(cbLeft >= sizeof(KMKCCEXPJUMP));
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
    uint32_t cbBlock = pOldBlock->cbBlock;
    while (cbBlock - sizeof(KMKCCEXPJUMP) - sizeof(*pNewBlock) < cb)
        cbBlock *= 2;

    /* Allocate and initialize the block it with the new instruction already accounted for. */
    pNewBlock = (PKMKCCBLOCK)xmalloc(cbBlock);
    pNewBlock->cbBlock = cbBlock;
    pNewBlock->offNext = sizeof(*pNewBlock) + cb;
    pNewBlock->pNext   = pOldBlock;
    *ppBlockTail = pNewBlock;

    pRet = (PKMKCCEXPCORE)(pNewBlock + 1);

    /* Emit jump. */
    pJump = (PKMKCCEXPJUMP)((char *)pOldBlock + pOldBlock->offNext);
    pJump->Core.enmOpCode = kKmkCcExpInstr_Jump;
    pJump->pNext = pRet;
    pOldBlock->offNext += sizeof(*pJump);
    assert(pOldBlock->offNext <= pOldBlock->cbBlock);

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

    assert(cbLeft >= sizeof(KMKCCEXPJUMP));
    assert(((cb + sizeof(void *) - 1) & (sizeof(void *) - 1)) == 0 || cb == sizeof(KMKCCEXPCORE));

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


/**
 * Emits a kKmkCcExpInstr_Return.
 *
 * @returns 0 on success, non-zero on failure.
 * @param   ppBlockTail         Pointer to the allocator tail pointer.
 */
static int kmk_cc_exp_emit_return(PKMKCCBLOCK *ppBlockTail)
{
    PKMKCCEXPCORE pCore = kmk_cc_block_alloc_exp(ppBlockTail, sizeof(*pCore));
    pCore->enmOpCode = kKmkCcExpInstr_Return;
    return 0;
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
    pInstr->Core.Core.enmOpCode = kKmkCcExpInstr_DynamicFunction;
    pInstr->Core.cArgs          = cActualArgs;
    pInstr->Core.pfnFunction    = pfnFunction;
    pInstr->Core.pszFuncName    = pszFunction;

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
                assert(cDepth > 0);
                if (cDepth > 0)
                    cDepth--;
            }
            else if (ch == chOpen)
                cDepth++;
            else if (ch == ',' && cDepth == 0)
                break;
            else if (ch == '$')
                fDollar = 1;
            cchThisArg++;
        }

        pInstr->aArgs[iArg].fPlain = fDollar;
        if (fDollar)
        {
            /* Compile it. */
            int rc;
            kmk_cc_block_realign(ppBlockTail);
            rc = kmk_cc_exp_compile_subprog(ppBlockTail, pchArgs, cchThisArg, &pInstr->aArgs[iArg].u.SubProg);
            if (rc != 0)
                return rc;
        }
        else
        {
            /* Duplicate it. */
            pInstr->aArgs[iArg].u.Plain.pszArg = kmk_cc_block_strdup(ppBlockTail, pchArgs, cchThisArg);
        }
        iArg++;
        if (ch != ',')
            break;
        pchArgs += cchThisArg + 1;
        cchArgs -= cchThisArg + 1;
    }
    assert(iArg == cActualArgs);

    /*
     * Realign the allocator and take down the address of the next instruction.
     */
    kmk_cc_block_realign(ppBlockTail);
    pInstr->Core.pNext = (PKMKCCEXPCORE)kmk_cc_block_get_next_ptr(*ppBlockTail);
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
static int kmk_cc_exp_emit_plain_function(PKMKCCBLOCK *ppBlockTail, const char *pszFunction,
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
    pInstr->Core.Core.enmOpCode = kKmkCcExpInstr_PlainFunction;
    pInstr->Core.cArgs          = cActualArgs;
    pInstr->Core.pfnFunction    = pfnFunction;
    pInstr->Core.pszFuncName    = pszFunction;

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
                assert(cDepth > 0);
                if (cDepth > 0)
                    cDepth--;
            }
            else if (ch == chOpen)
                cDepth++;
            else if (ch == ',' && cDepth == 0)
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

    assert(iArg == cActualArgs);
    pInstr->apszArgs[iArg] = NULL;

    /*
     * Realign the allocator and take down the address of the next instruction.
     */
    kmk_cc_block_realign(ppBlockTail);
    pInstr->Core.pNext = (PKMKCCEXPCORE)kmk_cc_block_get_next_ptr(*ppBlockTail);
    return 0;
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
    assert(cchNameExpr > 0);

    pInstr = (PKMKCCEXPDYNVAR)kmk_cc_block_alloc_exp(ppBlockTail, sizeof(*pInstr));
    pInstr->Core.enmOpCode = kKmkCcExpInstr_DynamicVariable;

    rc = kmk_cc_exp_compile_subprog(ppBlockTail, pchNameExpr, cchNameExpr, &pInstr->SubProg);

    pInstr->pNext = (PKMKCCEXPCORE)kmk_cc_block_get_next_ptr(*ppBlockTail);
    return rc;
}


/**
 * Emits either a kKmkCcExpInstr_PlainVariable or
 * kKmkCcExpInstr_SearchAndReplacePlainVariable instruction.
 *
 * @returns 0 on success, non-zero on failure.
 * @param   ppBlockTail         Pointer to the allocator tail pointer.
 * @param   pchName             The name of the variable.  (Does not need to be
 *                              valid beyond the call.)
 * @param   cchName             The length of the variable name. If zero,
 *                              nothing will be emitted.
 */
static int kmk_cc_exp_emit_plain_variable_maybe_sr(PKMKCCBLOCK *ppBlockTail, const char *pchName, uint32_t cchName)
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
    return 0;
}


/**
 * Emits a kKmkCcExpInstr_CopyString.
 *
 * @returns 0 on success, non-zero on failure.
 * @param   ppBlockTail         Pointer to the allocator tail pointer.
 * @param   pchStr              The string to emit (ASSUMED presistent thru-out
 *                              the program life time).
 * @param   cchStr              The number of chars to copy. If zero, nothing
 *                              will be emitted.
 */
static int kmk_cc_exp_emit_copy_string(PKMKCCBLOCK *ppBlockTail, const char *pchStr, uint32_t cchStr)
{
    if (cchStr > 0)
    {
        PKMKCCEXPCOPYSTRING pInstr = (PKMKCCEXPCOPYSTRING)kmk_cc_block_alloc_exp(ppBlockTail, sizeof(*pInstr));
        pInstr->Core.enmOpCode = kKmkCcExpInstr_CopyString;
        pInstr->cchCopy = cchStr;
        pInstr->pachSrc = pchStr;
    }
    return 0;
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
            rc = kmk_cc_exp_emit_copy_string(ppBlockTail, pchStr, offDollar + cDollars / 2);
            if (rc != 0)
                return rc;
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
                        assert(!func_char_map[chOpen]); assert(!func_char_map[chClose]); assert(!func_char_map['$']);
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
                                rc = kmk_cc_exp_emit_plain_function(ppBlockTail, pszFunction, pchStr, cchName,
                                                                    cArgs, chOpen, chClose, pfnFunction, cMaxArgs);
                            else
                                rc = kmk_cc_exp_emit_dyn_function(ppBlockTail, pszFunction, pchStr, cchName,
                                                                  cArgs, chOpen, chClose, pfnFunction, cMaxArgs);
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
                                rc = kmk_cc_exp_emit_plain_variable_maybe_sr(ppBlockTail, pchStr, cchName);
                            else
                                rc = kmk_cc_exp_emit_dyn_variable(ppBlockTail, pchStr, cchName);
                        }
                        pchStr += cchName + 1;
                        cchStr -= cchName + (cDepth == 0);
                    }
                    else
                    {
                        /* Single character variable name. */
                        rc = kmk_cc_exp_emit_plain_variable_maybe_sr(ppBlockTail, pchStr, 1);
                        pchStr++;
                        cchStr--;
                    }
                    if (rc != 0)
                        return rc;
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
            rc = kmk_cc_exp_emit_copy_string(ppBlockTail, pchStr, cchStr);
            if (rc != 0)
                return rc;
            break;
        }
    }

    /*
     * Emit final instruction.
     */
    return kmk_cc_exp_emit_return(ppBlockTail);
}


/**
 * Initializes string expansion program statistics.
 * @param   pStats              Pointer to the statistics structure to init.
 */
static void kmk_cc_exp_stats_init(PKMKCCEXPSTATS pStats)
{
    pStats->cchAvg = 0;
    pStats->cchMax = 0;
}


/**
 * Compiles a string expansion sub program.
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
 * @param   pSubProg            The sub program structure to initialize.
 */
static int kmk_cc_exp_compile_subprog(PKMKCCBLOCK *ppBlockTail, const char *pchStr, uint32_t cchStr, PKMKCCEXPSUBPROG pSubProg)
{
    assert(cchStr);
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
                                     (kmk_cc_count_dollars(pchStr, cchStr) + 8)  * 16);
    if (pProg)
    {
        int rc = 0;

        pProg->pBlockTail   = pBlock;
        pProg->pFirstInstr  = (PKMKCCEXPCORE)kmk_cc_block_get_next_ptr(pBlock);
        kmk_cc_exp_stats_init(&pProg->Stats);

        /*
         * Join forces with the sub program compilation code.
         */
        if (kmk_cc_exp_compile_common(&pProg->pBlockTail, pchStr, cchStr) == 0)
            return pProg;
        kmk_cc_block_free_list(pProg->pBlockTail);
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
    assert(!pVar->expandprog);
    if (   !pVar->expandprog
        && pVar->value_length > 0
        && pVar->recursive)
    {
        assert(strlen(pVar->value) == pVar->value_length);
#if 0 /** @todo test & debug this code. Write interpreters! */
        pVar->expandprog = kmk_cc_exp_compile(pVar->value, pVar->value_length);
#endif
    }
    return pVar->expandprog;
}


#ifndef NDEBUG
/**
 * Used to check that function arguments are left alone.
 * @returns Updated hash.
 * @param   uHash       The current hash value.
 * @param   psz         The string to hash.
 */
static uint32_t kmk_exec_debug_string_hash(uint32_t uHash, const char *psz)
{
    unsigned char ch;
    while ((ch = *(unsigned char const *)psz++) != '\0')
        uHash = (uHash << 6) + (uHash << 16) - uHash + (unsigned char)ch;
    return uHash;
}
#endif


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
        if (!pVar->recursive)
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
                char            *pszName = kmk_exec_expand_subprog_to_tmp(&pInstr->SubProg, &cchName);
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
#ifndef NDEBUG
                uint32_t uCrcBefore = 0;
                uint32_t uCrcAfter = 0;
                uint32_t iArg = pInstr->Core.cArgs;
                while (iArg-- > 0)
                    uCrcBefore = kmk_exec_debug_string_hash(uCrcBefore, pInstr->apszArgs[iArg]);
#endif

                pchDst = pInstr->Core.pfnFunction(pchDst, (char **)&pInstr->apszArgs[0], pInstr->Core.pszFuncName);

#ifndef NDEBUG
                iArg = pInstr->Core.cArgs;
                while (iArg-- > 0)
                    uCrcAfter = kmk_exec_debug_string_hash(uCrcAfter, pInstr->apszArgs[iArg]);
                assert(uCrcBefore == uCrcAfter);
#endif

                pInstrCore = pInstr->Core.pNext;
                break;
            }

            case kKmkCcExpInstr_DynamicFunction:
            {
                PKMKCCEXPDYNFUNC pInstr = (PKMKCCEXPDYNFUNC)pInstrCore;
                char           **papszArgsShadow = xmalloc( (pInstr->Core.cArgs * 2 + 1) * sizeof(char *));
                char           **papszArgs = &papszArgsShadow[pInstr->Core.cArgs];
                uint32_t         iArg;
#ifndef NDEBUG
                uint32_t         uCrcBefore = 0;
                uint32_t         uCrcAfter = 0;
#endif
                iArg = pInstr->Core.cArgs;
                papszArgs[iArg] = NULL;
                while (iArg-- > 0)
                {
                    char *pszArg;
                    if (pInstr->aArgs[iArg].fPlain)
                        pszArg = (char *)pInstr->aArgs[iArg].u.Plain.pszArg;
                    else
                        pszArg = kmk_exec_expand_subprog_to_tmp(&pInstr->aArgs[iArg].u.SubProg, NULL);
                    papszArgsShadow[iArg] = pszArg;
                    papszArgs[iArg]       = pszArg;
#ifndef NDEBUG
                    uCrcBefore = kmk_exec_debug_string_hash(uCrcBefore, pszArg);
#endif
                }

                pchDst = pInstr->Core.pfnFunction(pchDst, papszArgs, pInstr->Core.pszFuncName);

                iArg = pInstr->Core.cArgs;
                while (iArg-- > 0)
                {
#ifndef NDEBUG
                    assert(papszArgsShadow[iArg] == papszArgs[iArg]);
                    uCrcAfter = kmk_exec_debug_string_hash(uCrcAfter, papszArgsShadow[iArg]);
#endif
                    if (!pInstr->aArgs[iArg].fPlain)
                        free(papszArgsShadow);
                }
                assert(uCrcBefore == uCrcAfter);
                free(papszArgsShadow);

                pInstrCore = pInstr->Core.pNext;
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
     * Keep statistics on output size.  The average is simplified and not an
     * exact average for every expansion that has taken place.
     */
    if (cchResult > pStats->cchMax)
    {
        if (pStats->cchMax)
            pStats->cchAvg = cchResult;
        pStats->cchMax = cchResult;
    }
    pStats->cchAvg = (pStats->cchAvg * 7 + cchResult) / 8;
}


/**
 * Execute a string expansion sub-program, outputting to a new heap buffer.
 *
 * @returns Pointer to the output buffer (hand to free when done).
 * @param   pSubProg          The sub-program to execute.
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
     * stream for this sub program.
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

    restore_variable_buffer(pchOldVarBuf, cbOldVarBuf);

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

    pchDst = kmk_exec_expand_instruction_stream_to_var_buf(pProg->pFirstInstr, pchDst);

    cchResult = (uint32_t)(pchDst - variable_buffer);
    assert(cchResult >= offStart);
    cchResult -= offStart;
    kmk_cc_exp_stats_update(&pProg->Stats, cchResult);

    return pchDst;
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
    return kmk_exec_expand_prog_to_var_buf(pVar->expandprog, pchDst);
}


/**
 * Called when a variable with expandprog or/and evalprog changes.
 *
 * @param   pVar        Pointer to the variable.
 */
void  kmk_cc_variable_changed(struct variable *pVar)
{
    assert(pVar->evalprog || pVar->expandprog);
#if 0
    if (pVar->evalprog)
    {
        kmk_cc_block_free_list(pVar->evalprog->pBlockTail);
        pVar->evalprog = NULL;
    }
#endif
    if (pVar->expandprog)
    {
        kmk_cc_block_free_list(pVar->expandprog->pBlockTail);
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
    assert(pVar->evalprog || pVar->expandprog);
#if 0
    if (pVar->evalprog)
    {
        kmk_cc_block_free_list(pVar->evalprog->pBlockTail);
        pVar->evalprog = NULL;
    }
#endif
    if (pVar->expandprog)
    {
        kmk_cc_block_free_list(pVar->expandprog->pBlockTail);
        pVar->expandprog = NULL;
    }
}


#endif /* CONFIG_WITH_COMPILER */

