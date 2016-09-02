/* $Id: tst-1-c1xx-xcpt.cpp 2865 2016-09-02 22:17:13Z knut.osmundsen@oracle.com $ */

/*
 * kWorker testcase.
 *
 * This is a testcase sitched together from bits of iprt/cdefs.h,
 * iprt/assert.h and VBox/vmm/hm_vmx.h.
 *
 * It triggers an 0xc0000005 exception (#PF) via RT_BF_ASSERT_COMPILE_CHECKS,
 * guess this is due to deep preprocessor expansion nesting (around 32 levels).
 *
 * This doesn't work if we haven't got the exception handling right,
 * like the RtlAddFunctionTable and RtlDeleteFunctionTable calls.
 *
 */

/* glue */
#define UINT32_C(x) x##U
#define UINT32_MAX  0xffffffffU


/** @file
 * IPRT - Common C and C++ definitions.
 */

/*
 * Copyright (C) 2006-2016 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 *
 * The contents of this file may alternatively be used under the terms
 * of the Common Development and Distribution License Version 1.0
 * (CDDL) only, as it comes in the "COPYING.CDDL" file of the
 * VirtualBox OSE distribution, in which case the provisions of the
 * CDDL are applicable instead of those of the GPL.
 *
 * You may elect to license modified versions of this file under the
 * terms and conditions of either the GPL or the CDDL or both.
 */

/** @def RT_EXPAND_2
 * Helper for RT_EXPAND. */
#define RT_EXPAND_2(a_Expr)     a_Expr
/** @def RT_EXPAND
 * Returns the expanded expression.
 * @param   a_Expr              The expression to expand. */
#define RT_EXPAND(a_Expr)       RT_EXPAND_2(a_Expr)


/** @def RT_UNPACK_CALL
 * Unpacks the an argument list inside an extra set of parenthesis and turns it
 * into a call to @a a_Fn.
 *
 * @param   a_Fn        Function/macro to call.
 * @param   a_Args      Parameter list in parenthesis.
 */
#define RT_UNPACK_CALL(a_Fn, a_Args) a_Fn a_Args


/** @def RT_UNPACK_ARGS
 * Returns the arguments without parenthesis.
 *
 * @param   ...         Parameter list in parenthesis.
 * @remarks Requires RT_COMPILER_SUPPORTS_VA_ARGS.
 */
# define RT_UNPACK_ARGS(...)    __VA_ARGS__

/** @def RT_COUNT_VA_ARGS_HLP
 * Helper for RT_COUNT_VA_ARGS that picks out the argument count from
 * RT_COUNT_VA_ARGS_REV_SEQ. */
#define RT_COUNT_VA_ARGS_HLP( \
    c69, c68, c67, c66, c65, c64, c63, c62, c61, c60, \
    c59, c58, c57, c56, c55, c54, c53, c52, c51, c50, \
    c49, c48, c47, c46, c45, c44, c43, c42, c41, c40, \
    c39, c38, c37, c36, c35, c34, c33, c32, c31, c30, \
    c29, c28, c27, c26, c25, c24, c23, c22, c21, c20, \
    c19, c18, c17, c16, c15, c14, c13, c12, c11, c10, \
     c9,  c8,  c7,  c6,  c5,  c4,  c3,  c2,  c1, cArgs, ...) cArgs
/** Argument count sequence. */
#define RT_COUNT_VA_ARGS_REV_SEQ \
     69,  68,  67,  66,  65,  64,  63,  62,  61,  60, \
     59,  58,  57,  56,  55,  54,  53,  52,  51,  50, \
     49,  48,  47,  46,  45,  44,  43,  42,  41,  40, \
     39,  38,  37,  36,  35,  34,  33,  32,  31,  30, \
     29,  28,  27,  26,  25,  24,  23,  22,  21,  20, \
     19,  18,  17,  16,  15,  14,  13,  12,  11,  10, \
      9,   8,   7,   6,   5,   4,   3,   2,   1,   0
/** This is for zero arguments. At least Visual C++ requires it. */
#define RT_COUNT_VA_ARGS_PREFIX_RT_NOTHING       RT_COUNT_VA_ARGS_REV_SEQ
/**
 * Counts the number of arguments given to the variadic macro.
 *
 * Max is 69.
 *
 * @returns Number of arguments in the ellipsis
 * @param   ...     Arguments to count.
 * @remarks Requires RT_COMPILER_SUPPORTS_VA_ARGS.
 */
#define RT_COUNT_VA_ARGS(...) \
      RT_UNPACK_CALL(RT_COUNT_VA_ARGS_HLP, (RT_COUNT_VA_ARGS_PREFIX_ ## __VA_ARGS__ ## RT_NOTHING, \
                                            RT_COUNT_VA_ARGS_REV_SEQ))
/** @def RT_CONCAT
 * Concatenate the expanded arguments without any extra spaces in between.
 *
 * @param   a       The first part.
 * @param   b       The second part.
 */
#define RT_CONCAT(a,b)              RT_CONCAT_HLP(a,b)
/** RT_CONCAT helper, don't use.  */
#define RT_CONCAT_HLP(a,b)          a##b

/** @def RT_CONCAT3
 * Concatenate the expanded arguments without any extra spaces in between.
 *
 * @param   a       The 1st part.
 * @param   b       The 2nd part.
 * @param   c       The 3rd part.
 */
#define RT_CONCAT3(a,b,c)           RT_CONCAT3_HLP(a,b,c)
/** RT_CONCAT3 helper, don't use.  */
#define RT_CONCAT3_HLP(a,b,c)       a##b##c

/** Bit field compile time check helper
 * @internal */
#define RT_BF_CHECK_DO_XOR_MASK(a_uLeft, a_RightPrefix, a_FieldNm)  ((a_uLeft) ^ RT_CONCAT3(a_RightPrefix, a_FieldNm, _MASK))
/** Bit field compile time check helper
 * @internal */
#define RT_BF_CHECK_DO_OR_MASK(a_uLeft, a_RightPrefix, a_FieldNm)   ((a_uLeft) | RT_CONCAT3(a_RightPrefix, a_FieldNm, _MASK))
/** Bit field compile time check helper
 * @internal */
#define RT_BF_CHECK_DO_1ST_MASK_BIT(a_uLeft, a_RightPrefix, a_FieldNm) \
    ((a_uLeft) && ( (RT_CONCAT3(a_RightPrefix, a_FieldNm, _MASK) >> RT_CONCAT3(a_RightPrefix, a_FieldNm, _SHIFT)) & 1U ) )
/** Used to check that a bit field mask does not start too early.
 * @internal */
#define RT_BF_CHECK_DO_MASK_START(a_uLeft, a_RightPrefix, a_FieldNm) \
    (   (a_uLeft) \
     && (   RT_CONCAT3(a_RightPrefix, a_FieldNm, _SHIFT) == 0 \
         || (  (  (   ((RT_CONCAT3(a_RightPrefix, a_FieldNm, _MASK) >> RT_CONCAT3(a_RightPrefix, a_FieldNm, _SHIFT)) & 1U) \
                   << RT_CONCAT3(a_RightPrefix, a_FieldNm, _SHIFT)) /* => single bit mask, correct type */ \
                - 1U) /* => mask of all bits below the field */ \
             & RT_CONCAT3(a_RightPrefix, a_FieldNm, _MASK)) == 0 ) )
/** @name Bit field compile time check recursion workers.
 * @internal
 * @{  */
#define RT_BF_CHECK_DO_1(a_DoThis, a_uLeft, a_RightPrefix, f1) \
    a_DoThis(a_uLeft, a_RightPrefix, f1)
#define RT_BF_CHECK_DO_2(a_DoThis, a_uLeft, a_RightPrefix,                                        f1, f2) \
    RT_BF_CHECK_DO_1(a_DoThis,  RT_BF_CHECK_DO_1(a_DoThis, a_uLeft, a_RightPrefix,f1), a_RightPrefix, f2)
#define RT_BF_CHECK_DO_3(a_DoThis, a_uLeft, a_RightPrefix,                                        f1, f2, f3) \
    RT_BF_CHECK_DO_2(a_DoThis,  RT_BF_CHECK_DO_1(a_DoThis, a_uLeft, a_RightPrefix,f1), a_RightPrefix, f2, f3)
#define RT_BF_CHECK_DO_4(a_DoThis, a_uLeft, a_RightPrefix,                                        f1, f2, f3, f4) \
    RT_BF_CHECK_DO_3(a_DoThis,  RT_BF_CHECK_DO_1(a_DoThis, a_uLeft, a_RightPrefix,f1), a_RightPrefix, f2, f3, f4)
#define RT_BF_CHECK_DO_5(a_DoThis, a_uLeft, a_RightPrefix,                                        f1, f2, f3, f4, f5) \
    RT_BF_CHECK_DO_4(a_DoThis,  RT_BF_CHECK_DO_1(a_DoThis, a_uLeft, a_RightPrefix,f1), a_RightPrefix, f2, f3, f4, f5)
#define RT_BF_CHECK_DO_6(a_DoThis, a_uLeft, a_RightPrefix,                                        f1, f2, f3, f4, f5, f6) \
    RT_BF_CHECK_DO_5(a_DoThis,  RT_BF_CHECK_DO_1(a_DoThis, a_uLeft, a_RightPrefix,f1), a_RightPrefix, f2, f3, f4, f5, f6)
#define RT_BF_CHECK_DO_7(a_DoThis, a_uLeft, a_RightPrefix,                                        f1, f2, f3, f4, f5, f6, f7) \
    RT_BF_CHECK_DO_6(a_DoThis,  RT_BF_CHECK_DO_1(a_DoThis, a_uLeft, a_RightPrefix,f1), a_RightPrefix, f2, f3, f4, f5, f6, f7)
#define RT_BF_CHECK_DO_8(a_DoThis, a_uLeft, a_RightPrefix,                                        f1, f2, f3, f4, f5, f6, f7, f8) \
    RT_BF_CHECK_DO_7(a_DoThis,  RT_BF_CHECK_DO_1(a_DoThis, a_uLeft, a_RightPrefix,f1), a_RightPrefix, f2, f3, f4, f5, f6, f7, f8)
#define RT_BF_CHECK_DO_9(a_DoThis, a_uLeft, a_RightPrefix,                                        f1, f2, f3, f4, f5, f6, f7, f8, f9) \
    RT_BF_CHECK_DO_8(a_DoThis,  RT_BF_CHECK_DO_1(a_DoThis, a_uLeft, a_RightPrefix,f1), a_RightPrefix, f2, f3, f4, f5, f6, f7, f8, f9)
#define RT_BF_CHECK_DO_10(a_DoThis, a_uLeft, a_RightPrefix,                                       f1, f2, f3, f4, f5, f6, f7, f8, f9, f10) \
    RT_BF_CHECK_DO_9(a_DoThis,  RT_BF_CHECK_DO_1(a_DoThis, a_uLeft, a_RightPrefix,f1), a_RightPrefix, f2, f3, f4, f5, f6, f7, f8, f9, f10)
#define RT_BF_CHECK_DO_11(a_DoThis, a_uLeft, a_RightPrefix,                                       f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11) \
    RT_BF_CHECK_DO_10(a_DoThis, RT_BF_CHECK_DO_1(a_DoThis, a_uLeft, a_RightPrefix,f1), a_RightPrefix, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11)
#define RT_BF_CHECK_DO_12(a_DoThis, a_uLeft, a_RightPrefix,                                       f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12) \
    RT_BF_CHECK_DO_11(a_DoThis, RT_BF_CHECK_DO_1(a_DoThis, a_uLeft, a_RightPrefix,f1), a_RightPrefix, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12)
#define RT_BF_CHECK_DO_13(a_DoThis, a_uLeft, a_RightPrefix,                                       f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13) \
    RT_BF_CHECK_DO_12(a_DoThis, RT_BF_CHECK_DO_1(a_DoThis, a_uLeft, a_RightPrefix,f1), a_RightPrefix, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13)
#define RT_BF_CHECK_DO_14(a_DoThis, a_uLeft, a_RightPrefix,                                       f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14) \
    RT_BF_CHECK_DO_13(a_DoThis, RT_BF_CHECK_DO_1(a_DoThis, a_uLeft, a_RightPrefix,f1), a_RightPrefix, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14)
#define RT_BF_CHECK_DO_15(a_DoThis, a_uLeft, a_RightPrefix,                                       f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15) \
    RT_BF_CHECK_DO_14(a_DoThis, RT_BF_CHECK_DO_1(a_DoThis, a_uLeft, a_RightPrefix,f1), a_RightPrefix, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15)
#define RT_BF_CHECK_DO_16(a_DoThis, a_uLeft, a_RightPrefix,                                       f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15, f16) \
    RT_BF_CHECK_DO_15(a_DoThis, RT_BF_CHECK_DO_1(a_DoThis, a_uLeft, a_RightPrefix,f1), a_RightPrefix, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15, f16)
#define RT_BF_CHECK_DO_17(a_DoThis, a_uLeft, a_RightPrefix,                                       f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15, f16, f17) \
    RT_BF_CHECK_DO_16(a_DoThis, RT_BF_CHECK_DO_1(a_DoThis, a_uLeft, a_RightPrefix,f1), a_RightPrefix, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15, f16, f17)
#define RT_BF_CHECK_DO_18(a_DoThis, a_uLeft, a_RightPrefix,                                       f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15, f16, f17, f18) \
    RT_BF_CHECK_DO_17(a_DoThis, RT_BF_CHECK_DO_1(a_DoThis, a_uLeft, a_RightPrefix,f1), a_RightPrefix, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15, f16, f17, f18)
#define RT_BF_CHECK_DO_19(a_DoThis, a_uLeft, a_RightPrefix,                                       f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15, f16, f17, f18, f19) \
    RT_BF_CHECK_DO_18(a_DoThis, RT_BF_CHECK_DO_1(a_DoThis, a_uLeft, a_RightPrefix,f1), a_RightPrefix, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15, f16, f17, f18, f19)
#define RT_BF_CHECK_DO_20(a_DoThis, a_uLeft, a_RightPrefix,                                       f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15, f16, f17, f18, f19, f20) \
    RT_BF_CHECK_DO_19(a_DoThis, RT_BF_CHECK_DO_1(a_DoThis, a_uLeft, a_RightPrefix,f1), a_RightPrefix, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15, f16, f17, f18, f19, f20)
#define RT_BF_CHECK_DO_21(a_DoThis, a_uLeft, a_RightPrefix,                                       f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15, f16, f17, f18, f19, f20, f21) \
    RT_BF_CHECK_DO_20(a_DoThis, RT_BF_CHECK_DO_1(a_DoThis, a_uLeft, a_RightPrefix,f1), a_RightPrefix, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15, f16, f17, f18, f19, f20, f21)
#define RT_BF_CHECK_DO_22(a_DoThis, a_uLeft, a_RightPrefix,                                       f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15, f16, f17, f18, f19, f20, f21, f22) \
    RT_BF_CHECK_DO_21(a_DoThis, RT_BF_CHECK_DO_1(a_DoThis, a_uLeft, a_RightPrefix,f1), a_RightPrefix, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15, f16, f17, f18, f19, f20, f21, f22)
#define RT_BF_CHECK_DO_23(a_DoThis, a_uLeft, a_RightPrefix,                                       f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15, f16, f17, f18, f19, f20, f21, f22, f23) \
    RT_BF_CHECK_DO_22(a_DoThis, RT_BF_CHECK_DO_1(a_DoThis, a_uLeft, a_RightPrefix,f1), a_RightPrefix, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15, f16, f17, f18, f19, f20, f21, f22, f23)
#define RT_BF_CHECK_DO_24(a_DoThis, a_uLeft, a_RightPrefix,                                       f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15, f16, f17, f18, f19, f20, f21, f22, f23, f24) \
    RT_BF_CHECK_DO_23(a_DoThis, RT_BF_CHECK_DO_1(a_DoThis, a_uLeft, a_RightPrefix,f1), a_RightPrefix, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15, f16, f17, f18, f19, f20, f21, f22, f23, f24)
#define RT_BF_CHECK_DO_25(a_DoThis, a_uLeft, a_RightPrefix,                                       f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15, f16, f17, f18, f19, f20, f21, f22, f23, f24, f25) \
    RT_BF_CHECK_DO_24(a_DoThis, RT_BF_CHECK_DO_1(a_DoThis, a_uLeft, a_RightPrefix,f1), a_RightPrefix, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15, f16, f17, f18, f19, f20, f21, f22, f23, f24, f25)
#define RT_BF_CHECK_DO_26(a_DoThis, a_uLeft, a_RightPrefix,                                       f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15, f16, f17, f18, f19, f20, f21, f22, f23, f24, f25, f26) \
    RT_BF_CHECK_DO_25(a_DoThis, RT_BF_CHECK_DO_1(a_DoThis, a_uLeft, a_RightPrefix,f1), a_RightPrefix, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15, f16, f17, f18, f19, f20, f21, f22, f23, f24, f25, f26)
#define RT_BF_CHECK_DO_27(a_DoThis, a_uLeft, a_RightPrefix,                                       f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15, f16, f17, f18, f19, f20, f21, f22, f23, f24, f25, f26, f27) \
    RT_BF_CHECK_DO_26(a_DoThis, RT_BF_CHECK_DO_1(a_DoThis, a_uLeft, a_RightPrefix,f1), a_RightPrefix, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15, f16, f17, f18, f19, f20, f21, f22, f23, f24, f25, f26, f27)
#define RT_BF_CHECK_DO_28(a_DoThis, a_uLeft, a_RightPrefix,                                       f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15, f16, f17, f18, f19, f20, f21, f22, f23, f24, f25, f26, f27, f28) \
    RT_BF_CHECK_DO_27(a_DoThis, RT_BF_CHECK_DO_1(a_DoThis, a_uLeft, a_RightPrefix,f1), a_RightPrefix, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15, f16, f17, f18, f19, f20, f21, f22, f23, f24, f25, f26, f27, f28)
#define RT_BF_CHECK_DO_29(a_DoThis, a_uLeft, a_RightPrefix,                                       f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15, f16, f17, f18, f19, f20, f21, f22, f23, f24, f25, f26, f27, f28, f29) \
    RT_BF_CHECK_DO_28(a_DoThis, RT_BF_CHECK_DO_1(a_DoThis, a_uLeft, a_RightPrefix,f1), a_RightPrefix, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15, f16, f17, f18, f19, f20, f21, f22, f23, f24, f25, f26, f27, f28, f29)
#define RT_BF_CHECK_DO_30(a_DoThis, a_uLeft, a_RightPrefix,                                       f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15, f16, f17, f18, f19, f20, f21, f22, f23, f24, f25, f26, f27, f28, f29, f30) \
    RT_BF_CHECK_DO_29(a_DoThis, RT_BF_CHECK_DO_1(a_DoThis, a_uLeft, a_RightPrefix,f1), a_RightPrefix, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15, f16, f17, f18, f19, f20, f21, f22, f23, f24, f25, f26, f27, f28, f29, f30)
#define RT_BF_CHECK_DO_31(a_DoThis, a_uLeft, a_RightPrefix,                                       f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15, f16, f17, f18, f19, f20, f21, f22, f23, f24, f25, f26, f27, f28, f29, f30, f31) \
    RT_BF_CHECK_DO_30(a_DoThis, RT_BF_CHECK_DO_1(a_DoThis, a_uLeft, a_RightPrefix,f1), a_RightPrefix, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15, f16, f17, f18, f19, f20, f21, f22, f23, f24, f25, f26, f27, f28, f29, f30, f31)
#define RT_BF_CHECK_DO_32(a_DoThis, a_uLeft, a_RightPrefix,                                       f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15, f16, f17, f18, f19, f20, f21, f22, f23, f24, f25, f26, f27, f28, f29, f30, f31, f32) \
    RT_BF_CHECK_DO_31(a_DoThis, RT_BF_CHECK_DO_1(a_DoThis, a_uLeft, a_RightPrefix,f1), a_RightPrefix, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15, f16, f17, f18, f19, f20, f21, f22, f23, f24, f25, f26, f27, f28, f29, f30, f31, f32)
#define RT_BF_CHECK_DO_33(a_DoThis, a_uLeft, a_RightPrefix,                                       f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15, f16, f17, f18, f19, f20, f21, f22, f23, f24, f25, f26, f27, f28, f29, f30, f31, f32, f33) \
    RT_BF_CHECK_DO_32(a_DoThis, RT_BF_CHECK_DO_1(a_DoThis, a_uLeft, a_RightPrefix,f1), a_RightPrefix, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15, f16, f17, f18, f19, f20, f21, f22, f23, f24, f25, f26, f27, f28, f29, f30, f31, f32, f33)
/** @} */

/** @def RT_BF_ASSERT_COMPILE_CHECKS
 * Emits a series of AssertCompile statements checking that the bit-field
 * declarations doesn't overlap, has holes, and generally makes some sense.
 *
 * This requires variadic macros because its too much to type otherwise.
 */
#define RT_BF_ASSERT_COMPILE_CHECKS(a_Prefix, a_uZero, a_uCovered, a_Fields) \
    AssertCompile(RT_BF_CHECK_DO_N(RT_BF_CHECK_DO_OR_MASK,     a_uZero, a_Prefix, RT_UNPACK_ARGS a_Fields ) == a_uCovered); \
    AssertCompile(RT_BF_CHECK_DO_N(RT_BF_CHECK_DO_XOR_MASK, a_uCovered, a_Prefix, RT_UNPACK_ARGS a_Fields ) == 0); \
    AssertCompile(RT_BF_CHECK_DO_N(RT_BF_CHECK_DO_1ST_MASK_BIT,   true, a_Prefix, RT_UNPACK_ARGS a_Fields ) == true); \
    AssertCompile(RT_BF_CHECK_DO_N(RT_BF_CHECK_DO_MASK_START,     true, a_Prefix, RT_UNPACK_ARGS a_Fields ) == true)
/** Bit field compile time check helper
 * @internal */
#define RT_BF_CHECK_DO_N(a_DoThis, a_uLeft, a_RightPrefix, ...) \
        RT_UNPACK_CALL(RT_CONCAT(RT_BF_CHECK_DO_, RT_EXPAND(RT_COUNT_VA_ARGS(__VA_ARGS__))), (a_DoThis, a_uLeft, a_RightPrefix, __VA_ARGS__))


/** @file
 * IPRT - Assertions.
 */

/*
 * Copyright (C) 2006-2016 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 *
 * The contents of this file may alternatively be used under the terms
 * of the Common Development and Distribution License Version 1.0
 * (CDDL) only, as it comes in the "COPYING.CDDL" file of the
 * VirtualBox OSE distribution, in which case the provisions of the
 * CDDL are applicable instead of those of the GPL.
 *
 * You may elect to license modified versions of this file under the
 * terms and conditions of either the GPL or the CDDL or both.
 */

/** @def AssertCompile
 * Asserts that a C++0x compile-time expression is true. If it's not break the
 * build.
 * @param   expr    Expression which should be true.
 */
#define AssertCompile(expr)    static_assert(!!(expr), #expr)




/** @file
 * HM - VMX Structures and Definitions. (VMM)
 */

/*
 * Copyright (C) 2006-2016 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 *
 * The contents of this file may alternatively be used under the terms
 * of the Common Development and Distribution License Version 1.0
 * (CDDL) only, as it comes in the "COPYING.CDDL" file of the
 * VirtualBox OSE distribution, in which case the provisions of the
 * CDDL are applicable instead of those of the GPL.
 *
 * You may elect to license modified versions of this file under the
 * terms and conditions of either the GPL or the CDDL or both.
 */

/** Address calculation scaling field (powers of two). */
#define VMX_XDTR_INSINFO_SCALE_SHIFT                            0
#define VMX_XDTR_INSINFO_SCALE_MASK                             UINT32_C(0x00000003)
/** Bits 2 thru 6 are undefined. */
#define VMX_XDTR_INSINFO_UNDEF_2_6_SHIFT                        2
#define VMX_XDTR_INSINFO_UNDEF_2_6_MASK                         UINT32_C(0x0000007c)
/** Address size, only 0(=16), 1(=32) and 2(=64) are defined.
 * @remarks anyone's guess why this is a 3 bit field...  */
#define VMX_XDTR_INSINFO_ADDR_SIZE_SHIFT                        7
#define VMX_XDTR_INSINFO_ADDR_SIZE_MASK                         UINT32_C(0x00000380)
/** Bit 10 is defined as zero. */
#define VMX_XDTR_INSINFO_ZERO_10_SHIFT                          10
#define VMX_XDTR_INSINFO_ZERO_10_MASK                           UINT32_C(0x00000400)
/** Operand size, either (1=)32-bit or (0=)16-bit, but get this, it's undefined
 * for exits from 64-bit code as the operand size there is fixed. */
#define VMX_XDTR_INSINFO_OP_SIZE_SHIFT                          11
#define VMX_XDTR_INSINFO_OP_SIZE_MASK                           UINT32_C(0x00000800)
/** Bits 12 thru 14 are undefined. */
#define VMX_XDTR_INSINFO_UNDEF_12_14_SHIFT                      12
#define VMX_XDTR_INSINFO_UNDEF_12_14_MASK                       UINT32_C(0x00007000)
/** Applicable segment register (X86_SREG_XXX values). */
#define VMX_XDTR_INSINFO_SREG_SHIFT                             15
#define VMX_XDTR_INSINFO_SREG_MASK                              UINT32_C(0x00038000)
/** Index register (X86_GREG_XXX values). Undefined if HAS_INDEX_REG is clear. */
#define VMX_XDTR_INSINFO_INDEX_REG_SHIFT                        18
#define VMX_XDTR_INSINFO_INDEX_REG_MASK                         UINT32_C(0x003c0000)
/** Is VMX_XDTR_INSINFO_INDEX_REG_XXX valid (=1) or not (=0). */
#define VMX_XDTR_INSINFO_HAS_INDEX_REG_SHIFT                    22
#define VMX_XDTR_INSINFO_HAS_INDEX_REG_MASK                     UINT32_C(0x00400000)
/** Base register (X86_GREG_XXX values). Undefined if HAS_BASE_REG is clear. */
#define VMX_XDTR_INSINFO_BASE_REG_SHIFT                         23
#define VMX_XDTR_INSINFO_BASE_REG_MASK                          UINT32_C(0x07800000)
/** Is VMX_XDTR_INSINFO_BASE_REG_XXX valid (=1) or not (=0). */
#define VMX_XDTR_INSINFO_HAS_BASE_REG_SHIFT                     27
#define VMX_XDTR_INSINFO_HAS_BASE_REG_MASK                      UINT32_C(0x08000000)
/** The instruction identity (VMX_XDTR_INSINFO_II_XXX values) */
#define VMX_XDTR_INSINFO_INSTR_ID_SHIFT                         28
#define VMX_XDTR_INSINFO_INSTR_ID_MASK                          UINT32_C(0x30000000)
#define VMX_XDTR_INSINFO_II_SGDT                                0 /**< Instruction ID: SGDT */
#define VMX_XDTR_INSINFO_II_SIDT                                1 /**< Instruction ID: SIDT */
#define VMX_XDTR_INSINFO_II_LGDT                                2 /**< Instruction ID: LGDT */
#define VMX_XDTR_INSINFO_II_LIDT                                3 /**< Instruction ID: LIDT */
/** Bits 30 & 31 are undefined. */
#define VMX_XDTR_INSINFO_UNDEF_30_31_SHIFT                      30
#define VMX_XDTR_INSINFO_UNDEF_30_31_MASK                       UINT32_C(0xc0000000)
RT_BF_ASSERT_COMPILE_CHECKS(VMX_XDTR_INSINFO_, UINT32_C(0), UINT32_MAX,
                            (SCALE, UNDEF_2_6, ADDR_SIZE, ZERO_10, OP_SIZE, UNDEF_12_14, SREG, INDEX_REG, HAS_INDEX_REG,
                             BASE_REG, HAS_BASE_REG, INSTR_ID, UNDEF_30_31));


