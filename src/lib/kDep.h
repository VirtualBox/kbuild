/* $Id: kDep.h 2955 2016-09-21 19:05:53Z knut.osmundsen@oracle.com $ */
/** @file
 * kDep - Common Dependency Managemnt Code.
 */

/*
 * Copyright (c) 2004-2013 knut st. osmundsen <bird-kBuild-spamx@anduin.net>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 * Alternatively, the content of this file may be used under the terms of the
 * GPL version 2 or later, or LGPL version 2.1 or later.
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
extern void depOptimize(int fFixCase, int fQuiet, const char *pszIgnoredExt);
extern void depPrint(FILE *pOutput);
extern void depPrintStubs(FILE *pOutput);
extern void depCleanup(void);
extern void *depReadFileIntoMemory(FILE *pInput, size_t *pcbFile, void **ppvOpaque);
extern void depFreeFileMemory(void *pvFile, void *pvOpaque);
#ifdef ___k_kTypes_h___
extern void depHexDump(const KU8 *pb, size_t cb, size_t offBase);
#endif

#endif

