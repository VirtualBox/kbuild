/* $Id: kDep.h 2243 2009-01-10 02:24:02Z knut.osmundsen@oracle.com $ */
/** @file
 * kDep - Common Dependency Managemnt Code.
 */

/*
 * Copyright (c) 2004-2009 knut st. osmundsen <bird-kBuild-spamix@anduin.net>
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

#ifndef ___kDep_h
#define ___kDep_h

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
extern void depOptimize(int fFixCase, int fQuiet);
extern void depPrint(FILE *pOutput);
extern void depPrintStubs(FILE *pOutput);
extern void depCleanup(void);

#endif

