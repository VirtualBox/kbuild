/* $Id: kDep.h 393 2006-01-13 00:42:10Z knut.osmundsen@oracle.com $ */
/** @file
 *
 * kDep - Common Dependency Managemnt Code. 
 *
 * Copyright (c) 2004-2006 knut st. osmundsen <bird@innotek.de>
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


#ifndef __kDep_h__
#define __kDep_h__

/** A dependency. */
typedef struct DEP
{
    /** Next dependency in the list. */
    struct DEP *pNext;
    /** The filename hash. */
    unsigned    uHash;
    /** The length of the filename. */
    size_t      cchFilename;
    /** The filename. */
    char        szFilename[4];
} DEP, *PDEP;


extern PDEP depAdd(const char *pszFilename, size_t cchFilename);
extern void depOptimize(int fFixCase);
extern void depPrint(FILE *pOutput);
extern void depPrintStubs(FILE *pOutput);

#endif

