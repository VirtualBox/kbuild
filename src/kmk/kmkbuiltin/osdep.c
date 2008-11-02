/* $Id: osdep.c 2019 2008-11-02 00:21:05Z knut.osmundsen@oracle.com $ */
/** @file
 * Include all the OS dependent bits when bootstrapping.
 */

/*
 * Copyright (c) 2005-2008 knut st. osmundsen <bird-src-spam@anduin.net>
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

#include <config.h>

/** @todo replace this by proper configure.in tests. */

#if defined(_MSC_VER)
# include "mscfakes.c"
# include "fts.c"

#elif defined(__sun__)
# include "solfakes.c"
# include "fts.c"

#elif defined(__APPLE__)
# include "darwin.c"

#endif

