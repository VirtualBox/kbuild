/* $Id: solfakes.h 3213 2018-03-30 21:03:28Z knut.osmundsen@oracle.com $ */
/** @file
 * Unix fakes for Solaris.
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

#ifndef ___solfakes_h
#define ___solfakes_h
#ifdef __sun__

#include <stdarg.h>
#include <sys/types.h>
#ifndef FAKES_NO_GETOPT_H
# include "getopt.h"
#endif

#define _PATH_DEVNULL "/dev/null"
#define ALLPERMS 0000777
#define lutimes(path, tvs) utimes(path, tvs)
#define lchmod sol_lchmod
#define MAX(a,b) ((a) >= (b) ? (a) : (b))
#ifndef USHRT_MAX
# define USHRT_MAX 65535
#endif

int vasprintf(char **strp, const char *fmt, va_list va);
int asprintf(char **strp, const char *fmt, ...);
int sol_lchmod(const char *pszPath, mode_t mode);

#endif /* __sun__ */
#endif /* !___solfakes_h */

