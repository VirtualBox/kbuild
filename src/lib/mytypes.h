/* $Id: mytypes.h 2019 2008-11-02 00:21:05Z knut.osmundsen@oracle.com $ */
/** @file
 * mytypes - wrapper that ensures the necessary uintXY_t types are defined.
 */

/*
 * Copyright (c) 2007-2008 knut st. osmundsen <bird-src-spam@anduin.net>
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

#ifndef ___mytypes_h__
#define ___mytypes_h__

#include <stdlib.h>
#include <sys/types.h>

#if defined(_MSC_VER)
typedef unsigned int uint32_t;
typedef signed int int32_t;
typedef unsigned char uint8_t;
typedef signed char int8_t;
#else
# include <stdint.h>
#endif

#endif

