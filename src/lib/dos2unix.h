/* $Id: dos2unix.h 3114 2017-10-29 18:02:04Z knut.osmundsen@oracle.com $ */
/** @file
 * dos2unix - Line ending conversion routines.
 */

/*
 * Copyright (c) 2017 knut st. osmundsen <bird-kBuild-spamx@anduin.net>
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

#ifndef ___lib_dos2unix_h___
#define ___lib_dos2unix_h___

#include <k/kTypes.h>

#define DOS2UNIX_STYLE_NONE     0x00
#define DOS2UNIX_STYLE_DOS      0x01
#define DOS2UNIX_STYLE_UNIX     0x02
#define DOS2UNIX_STYLE_MIXED    0x03
#define DOS2UNIX_STYLE_MASK     0x03
#define DOS2UNIX_F_BINARY       0x80 /**< Probably a binary file. */

int dos2unix_analyze_file(const char *pszFilename, KU32 *pfStyle, KSIZE *pcDosEols, KSIZE *pcUnixEols);
int dos2unix_analyze_fd(int fd, KU32 *pfStyle, KSIZE *pcDosEols, KSIZE *pcUnixEols);

KBOOL dos2unix_convert_to_unix(const char *pchSrc, KSIZE cchSrc, char *pchDst, KSIZE *pcchDst);
KBOOL dos2unix_convert_to_dos(const char *pchSrc, KSIZE cchSrc, char *pchDst, KSIZE *pcchDst);

#endif

