/* $Id: quoted_spawn.h 2409 2010-06-16 23:46:56Z knut.osmundsen@oracle.com $ */
/** @file
 * quote_spawn - Correctly Quote The _spawnvp arguments, windows specific.
 */

/*
 * Copyright (c) 2010 knut st. osmundsen <bird-kBuild-spamix@anduin.net>
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


#ifndef ___quoted_spawn_h___
#define ___quoted_spawn_h___

#include "mytypes.h"
intptr_t quoted_spawnvp(int fMode, const char *pszExecPath, const char * const *papszArgs);

#endif

