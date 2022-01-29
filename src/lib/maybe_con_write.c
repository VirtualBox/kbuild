/* $Id: maybe_con_write.c 3547 2022-01-29 02:39:47Z knut.osmundsen@oracle.com $ */
/** @file
 * maybe_con_write - Optimized console output on windows.
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
# include <windows.h>
#endif
#include <errno.h>
#ifdef _MSC_VER
# include <conio.h>
typedef unsigned int to_write_t;
#else
typedef size_t to_write_t;
#endif


/**
 * Drop-in write replacement for optimizing console output on windows.
 *
 * @returns Number of bytes written, -1 + errno on failure.
 * @param   fd                  The file descript to write to.
 * @param   pvBuf               What to write.
 * @param   cbToWrite           How much to write.
 */
ssize_t maybe_con_write(int fd, void const *pvBuf, size_t cbToWrite)
{
    ssize_t cbWritten;

#ifdef KBUILD_OS_WINDOWS
    /*
     * If it's a TTY, do our own conversion to wide char and
     * call WriteConsoleW directly.
     */
    if (cbToWrite > 0 && cbToWrite < INT_MAX / 2)
    {
        HANDLE hCon = (HANDLE)_get_osfhandle(fd);
        if (   hCon != INVALID_HANDLE_VALUE
            && hCon != NULL)
        {
            if (is_console_handle((intptr_t)hCon))
            {
                wchar_t  awcBuf[1024];
                wchar_t *pawcBuf;
                wchar_t *pawcBufFree = NULL;
                size_t   cwcBuf      = cbToWrite * 2 + 16;
                if (cwcBuf < sizeof(awcBuf) / sizeof(awcBuf[0]))
                {
                    pawcBuf = awcBuf;
                    cwcBuf  = sizeof(awcBuf) / sizeof(awcBuf[0]);
                }
                else
                    pawcBufFree = pawcBuf = (wchar_t *)malloc(cwcBuf * sizeof(wchar_t));
                if (pawcBuf)
                {
                    int cwcToWrite = MultiByteToWideChar(get_crt_codepage(), 0 /*dwFlags*/,
                                                         pvBuf, (int)cbToWrite,
                                                         pawcBuf, (int)(cwcBuf - 1));
                    if (cwcToWrite > 0)
                    {
                        int rc;
                        pawcBuf[cwcToWrite] = '\0';

                        /* Let the CRT do the rest.  At least the Visual C++ 2010 CRT
                           sources indicates _cputws will do the right thing.  */
                        rc = _cputws(pawcBuf);
                        if (pawcBufFree)
                            free(pawcBufFree);
                        if (rc >= 0)
                            return cbToWrite;
                        return -1;
                    }
                    free(pawcBufFree);
                }
            }
        }
    }
#endif

    /*
     * Semi regular write handling.
     */
    cbWritten = write(fd, pvBuf, (to_write_t)cbToWrite);
    if (cbWritten == (ssize_t)cbToWrite)
    { /* likely */ }
    else if (cbWritten >= 0 || errno == EINTR)
    {
        if (cbWritten < 0)
            cbWritten = 0;
        while (cbWritten < (ssize_t)cbToWrite)
        {
            ssize_t cbThis = write(fd, (char *)pvBuf + cbWritten, (to_write_t)(cbToWrite - cbWritten));
            if (cbThis >= 0)
                cbWritten += cbThis;
            else if (errno != EINTR)
                return -1;
        }
    }
    return cbWritten;
}

