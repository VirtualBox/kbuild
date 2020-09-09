/* $Id: shtypes.h 3438 2020-09-09 20:01:39Z knut.osmundsen@oracle.com $ */
/** @file
 * Wrapper for missing types and such.
 */

/*
 * Copyright (c) 2007-2010 knut st. osmundsen <bird-kBuild-spamx@anduin.net>
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

#include "k/kTypes.h" /* Use these, not the ones below. */

#include <sys/types.h>
#include <stdlib.h>
#ifdef __HAIKU__
# include <posix/signal.h> /* silly */
#elif !defined(_MSC_VER)
# include <sys/signal.h>
#endif

#ifdef _MSC_VER
typedef signed char     int8_t;
typedef unsigned char   uint8_t;
typedef short           int16_t;
typedef unsigned short  uint16_t;
typedef int             int32_t;
typedef unsigned int    uint32_t;
typedef _int64          int64_t;
typedef unsigned _int64 uint64_t;
# if _MSC_VER >= 1400
#  include <io.h> /* intptr_t and uintptr_t */
# else
typedef KIPTR           intptr_t;
typedef KUPTR           uintptr_t;
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

#ifdef _MSC_VER
typedef uint32_t shsigset_t;
#else
typedef sigset_t shsigset_t;
#endif

typedef void (*shsig_t)(shinstance *, int);
typedef struct shsigaction
{
    shsig_t     sh_handler;
    shsigset_t  sh_mask;
    int         sh_flags;
} shsigaction_t;

/* SH_NORETURN_1 must be both on prototypes and definitions, while
   SH_NORETURN_2 should at least be on the prototype. */
#ifdef _MSC_VER
# define SH_NORETURN_1 __declspec(noreturn)
# define SH_NORETURN_2
#else
# define SH_NORETURN_1
# define SH_NORETURN_2 __attribute__((__noreturn__))
#endif

/** @name Extra wide pid_t so we can safely add a sub-pid to the top.
 * @{ */
#ifndef SH_FORKED_MODE
typedef KI64 shpid;
# define SHPID_MAKE(pid, tid)       ((shpid)(KU32)(pid) | (shpid)(KU32)(tid) << 32)
# define SHPID_GET_PID(shpid)       ((pid_t)(KU32)(shpid))
# define SHPID_GET_TID(shpid)       ((pid_t)((shpid) >> 32))
# define SHPID_PRI                  KI64_PRI
#else
typedef pid_t shpid;
# define SHPID_GET_PID(shpid)       (shpid)
# define SHPID_PRI                  KI32_PRI
#endif
/** @}  */

#endif

