/* $Id: mscfakes.h 805 2007-01-25 00:56:27Z bird $ */
/** @file
 *
 * Unix fakes for Solaris.
 *
 * Copyright (c) 2005-2007 knut st. osmundsen <bird-kBuild-spam@anduin.net>
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with This program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#ifndef __solfakes_h__
#define __solfakes_h__
#ifdef __sun__

#include "getopt.h"
#define _PATH_DEVNULL "/dev/null"
#define ALLPERMS 0000777
#define lutimes(path, tvs) utimes(path, tvs)
#define lchmod(path, mod) chmod(path, mod)
#define MAX(a,b) ((a) >= (b) ? (a) : (b))
#ifndef USHRT_MAX
# define USHRT_MAX 65535
#endif

#endif /* __sun__ */
#endif /* __solfakes_h__ */

