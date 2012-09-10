/* $Id: haikufakes.h 2656 2012-09-10 20:39:16Z knut.osmundsen@oracle.com $ */
/** @file
 * Unix/BSD fakes for Haiku.
 */

/*
 * Copyright (c) 2005-2012 knut st. osmundsen <bird-kBuild-spamx@anduin.net>
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

#ifndef ___haikufakes_h
#define ___haikufakes_h

#define EX_OK 0
#define EX_OSERR 1
#define EX_NOUSER 1
#define EX_USAGE 1
#define EX_TEMPFAIL 1
#define EX_SOFTWARE 1

#define lutimes(path, tvs) utimes(path, tvs)
#define lchmod             haiku_lchmod

extern int haiku_lchmod(const char *pszPath, mode_t mode);

#endif

