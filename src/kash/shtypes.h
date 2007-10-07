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
# include <io.h> /* intptr_t and uintptr_t */
typedef signed char     int8_t;
typedef unsigned char   uint8_t;
typedef short           int16_t;
typedef unsigned short  uint16_t;
typedef int             int32_t;
typedef unsigned int    uint32_t;
typedef _int64          int64_t;
typedef unsigned _int64 uint64_t;

#define INT16_C(c)      (c)
#define INT32_C(c)      (c)
#define INT64_C(c)      (c ## LL)

#define UINT8_C(c)      (c)
#define UINT16_C(c)     (c)
#define UINT32_C(c)     (c ## U)
#define UINT64_C(c)     (c ## ULL)

#define INTMAX_C(c)     (c ## LL)
#define UINTMAX_C(c)    (c ## ULL)

#undef  INT8_MIN
#define INT8_MIN        (-0x7f-1)
#undef  INT16_MIN
#define INT16_MIN       (-0x7fff-1)
#undef  INT32_MIN
#define INT32_MIN       (-0x7fffffff-1)
#undef  INT64_MIN
#define INT64_MIN       (-0x7fffffffffffffffLL-1)

#undef  INT8_MAX
#define INT8_MAX        0x7f
#undef  INT16_MAX
#define INT16_MAX       0x7fff
#undef  INT32_MAX
#define INT32_MAX       0x7fffffff
#undef  INT64_MAX
#define INT64_MAX       0x7fffffffffffffffLL

#undef  UINT8_MAX
#define UINT8_MAX       0xff
#undef  UINT16_MAX
#define UINT16_MAX      0xffff
#undef  UINT32_MAX
#define UINT32_MAX      0xffffffffU
#undef  UINT64_MAX
#define UINT64_MAX      0xffffffffffffffffULL

typedef int             pid_t;
typedef unsigned short  uid_t;
typedef unsigned short  gid_t;
typedef int             mode_t;
typedef intptr_t        ssize_t;

#else
# include <stdint.h>
#endif

struct shinstance;
typedef struct shinstance shinstance;

#endif

