/* $Id: maybe_con_printf.c 2903 2016-09-09 17:38:50Z knut.osmundsen@oracle.com $ */
/** @file
 * maybe_con_write - Optimized console output on windows.
 */

/*
 * Copyright (c) 2016 knut st. osmundsen <bird-kBuild-spamx@anduin.net>
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#ifdef KBUILD_OS_WINDOWS
# include <windows.h>
#endif
#include <errno.h>
#include <stdio.h>
#ifdef _MSC_VER
# include <io.h>
# include <conio.h>
#endif



/**
 * Drop-in printf replacement for optimizing console output on windows.
 *
 *
 * @returns Number of bytes written.
 * @param   pszFormat   The format string.
 * @param   ...         The format arguments.
 */
int maybe_con_printf(const char *pszFormat, ...)
{
    va_list va;
    int rc;

#ifdef KBUILD_OS_WINDOWS
    /*
     * Just try format it into a stack buffer.
     */
    extern size_t maybe_con_fwrite(void const *, size_t, size_t, FILE *);
    char szTmp[16384];
    va_start(va, pszFormat);
    rc = vsnprintf(szTmp, sizeof(szTmp), pszFormat, va);
    va_end(va);
    if (rc > 0)
    {
        size_t cchRet = maybe_con_fwrite(szTmp, 1, rc, stdout);
        return cchRet > 0 ? (int)cchRet : -1;
    }
#endif

    /*
     * call the 'v' function.
     */
    va_start(va, pszFormat);
    rc = vfprintf(stdout, pszFormat, va);
    va_end(va);
    return rc;
}

