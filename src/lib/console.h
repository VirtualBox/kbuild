/* $Id: console.h 3547 2022-01-29 02:39:47Z knut.osmundsen@oracle.com $ */
/** @file
 * console related functions.
 */

/*
 * Copyright (c) 2016-2018 knut st. osmundsen <bird-kBuild-spamx@anduin.net>
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

#ifndef ___lib_console_h___
#define ___lib_console_h___

#include <stdio.h>
#ifdef _MSC_VER
# include <io.h>
# ifndef ssize_t
typedef intptr_t ssize_t;
# endif
#else
# include <unistd.h>
#endif
#ifdef KBUILD_OS_WINDOWS
# include "get_codepage.h"
#endif


#ifdef KBUILD_OS_WINDOWS
extern int      is_console_handle(intptr_t hHandle);
#endif
extern int      is_console(int fd);
extern ssize_t  maybe_con_write(int fd, void const *pvBuf, size_t cbToWrite);
extern size_t   maybe_con_fwrite(void const *pvBuf, size_t cbUnit, size_t cUnits, FILE *pFile);
#endif

