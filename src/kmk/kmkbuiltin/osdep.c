/* $Id: osdep.c 2656 2012-09-10 20:39:16Z knut.osmundsen@oracle.com $ */
/** @file
 * Include all the OS dependent bits when bootstrapping.
 */

/*
 * Copyright (c) 2005-2010 knut st. osmundsen <bird-kBuild-spamx@anduin.net>
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

#elif defined(__OpenBSD__)
# include "openbsd.c"

#elif defined(__HAIKU__)
# include "haikufakes.c"

#endif

