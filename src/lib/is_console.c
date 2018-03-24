/* $Id: is_console.c 3188 2018-03-24 15:32:26Z knut.osmundsen@oracle.com $ */
/** @file
 * is_console - checks if a file descriptor is the console.
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include "console.h"
#ifdef KBUILD_OS_WINDOWS
# include <Windows.h>
#endif
#ifdef _MSC_VER
# include <io.h>
#else
# include <unistd.h>
#endif


#ifdef KBUILD_OS_WINDOWS
/**
 * Checks if @a hHandle is a console handle.
 * @returns 1 if it is, 0 if not.
 */
int is_console_handle(intptr_t hHandle)
{
    DWORD fMode;
    if (GetConsoleMode((HANDLE)hHandle, &fMode))
        return 1;
    return 0;
}
#endif

/**
 * Checks if @a fd is a console handle.
 * @returns 1 if it is, 0 if not.
 */
int is_console(int fd)
{
#ifdef KBUILD_OS_WINDOWS
    intptr_t hNative = _get_osfhandle(fd);
    if (hNative != (intptr_t)INVALID_HANDLE_VALUE)
        return is_console_handle(hNative);
    return 0;
#else
    return isatty(fd);
#endif
}

