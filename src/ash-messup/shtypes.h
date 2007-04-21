/* $Id: $ */
/** @file
 *
 * Wrapper for missing types and such.
 *
 * Copyright (c) 2007 knut st. osmundsen <bird-src-spam@anduin.net>
 *
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#ifndef ___shtypes_h___
#define ___shtypes_h___

#include <sys/types.h>
#include <stdlib.h>

#ifdef _MSC_VER
typedef signed char     int8_t;
typedef unsigned char   uint8_t;
typedef short           int16_t;
typedef unsigned short  uint16_t;
typedef int             int32_t;
typedef unsigned int    uint32_t;
typedef _int64          int64_t;
typedef unsigned _int64 uint64_t;
# if defined(__X86__) || defined(_X86_) || defined(_M_IX86)
typedef signed long     intptr_t;
typedef unsigned long   uintptr_t;
# else
typedef int64_t         intptr_t;
typedef uint64_t        uintptr_t;
# endif

#define INT16_C(c)      (c)
#define INT32_C(c)      (c)
#define INT64_C(c)      (c ## LL)

#define UINT8_C(c)      (c)
#define UINT16_C(c)     (c)
#define UINT32_C(c)     (c ## U)
#define UINT64_C(c)     (c ## ULL)

#define INTMAX_C(c)     (c ## LL)
#define UINTMAX_C(c)    (c ## ULL)

#define INT8_MIN        (-0x7f-1)
#define INT16_MIN       (-0x7fff-1)
#define INT32_MIN       (-0x7fffffff-1)
#define INT64_MIN       (-0x7fffffffffffffffLL-1)

#define INT8_MAX        0x7f
#define INT16_MAX       0x7fff
#define INT32_MAX       0x7fffffff
#define INT64_MAX       0x7fffffffffffffffLL

#define UINT8_MAX       0xff
#define UINT16_MAX      0xffff
#define UINT32_MAX      0xffffffffU
#define UINT64_MAX      0xffffffffffffffffULL

#else
# include <stdint.h>
#endif

#endif

