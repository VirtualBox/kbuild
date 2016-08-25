/* $Id: quote_argv.h 2838 2016-08-25 21:46:25Z knut.osmundsen@oracle.com $ */
/** @file
 * quote_argv - Correctly quote argv for spawn, windows specific.
 */

/*
 * Copyright (c) 2010 knut st. osmundsen <bird-kBuild-spamx@anduin.net>
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


#ifndef ___quote_argv_h___
#define ___quote_argv_h___

#include "mytypes.h"
extern void quote_argv(int argc, char **argv, int fWatcomBrainDamage, int fFreeOrLeak);

#endif

