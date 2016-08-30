/* $Id: nt_fullpath.h 2849 2016-08-30 14:28:46Z knut.osmundsen@oracle.com $ */
/** @file
 * fixcase - fixes the case of paths, windows specific.
 */

/*
 * Copyright (c) 2004-2016 knut st. osmundsen <bird-kBuild-spamx@anduin.net>
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

#ifndef ___lib_nt_fullpath_h___
#define ___lib_nt_fullpath_h___

#ifdef __cpluslus
extern "C"
#endif

extern void nt_fullpath(const char *pszPath, char *pszFull, size_t cchFull);
extern void nt_fullpath_cached(const char *pszPath, char *pszFull, size_t cchFull);


#ifdef __cpluslus
}
#endif

#endif

