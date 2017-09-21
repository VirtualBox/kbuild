/* $Id: ntutimes.h 3060 2017-09-21 15:11:07Z knut.osmundsen@oracle.com $ */
/** @file
 * MSC + NT utimes and lutimes.
 */

/*
 * Copyright (c) 2005-2017 knut st. osmundsen <bird-kBuild-spamx@anduin.net>
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

#ifndef ___nt_ntutimes_h
#define ___nt_ntutimes_h

#include "nttypes.h"

int birdUtimes(const char *pszFile, BirdTimeVal_T paTimes[2]);
int birdLUtimes(const char *pszFile, BirdTimeVal_T paTimes[2]);

#undef  utimes
#define utimes(a_pszPath, a_paTimes)    birdUtimes(a_pszPath, a_paTimes)
#undef  lutimes
#define lutimes(a_pszPath, a_paTimes)   birdLUtimes(a_pszPath, a_paTimes)

#endif

