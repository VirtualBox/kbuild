/* $Id: get_codepage.c 3546 2022-01-29 02:37:06Z knut.osmundsen@oracle.com $ */
/** @file
 * get_codepage - Gets the current codepage (as per CRT).
 */

/*
 * Copyright (c) 2016-2021 knut st. osmundsen <bird-kBuild-spamx@anduin.net>
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
#include "console.h"
#include <Windows.h>
#include <locale.h>
#if _MSC_VER < 1900
_CRTIMP UINT __cdecl ___lc_codepage_func(void);
#endif


/**
 * Returns the active codepage as per the CRT.
 *
 * @returns Code page.
 */
unsigned get_crt_codepage(void)
{
    /* We use the CRT internal function ___lc_codepage_func for getting
       the codepage.  It was made public/official in UCRT. */
    unsigned uCodepage = ___lc_codepage_func();
    if (uCodepage == 0)
        uCodepage = GetACP();
    return uCodepage;
}


/**
 * Returns GetACP().
 */
unsigned get_ansi_codepage(void)
{
    return GetACP();
}

