/* $Id: maybe_con_fwrite.c 3547 2022-01-29 02:39:47Z knut.osmundsen@oracle.com $ */
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
#endif


/**
 * Drop-in fwrite replacement for optimizing console output on windows.
 *
 *
 * @returns Units written; 0 & errno on failure.
 * @param   pvBuf               What to write.
 * @param   cbUnit              How much to write in each unit.
 * @param   cUnits              How many units to write.
 * @param   pFile               The file to write to.
 */
size_t maybe_con_fwrite(void const *pvBuf, size_t cbUnit, size_t cUnits, FILE *pFile)
{
#ifdef KBUILD_OS_WINDOWS
    /*
     * If it's a TTY, do our own conversion to wide char and call _cputws.
     */
    if (   cbUnit > 0
        && cUnits > 0
        && cbUnit < (unsigned)INT_MAX / 4
        && cUnits < (unsigned)INT_MAX / 4
        && (pFile == stdout || pFile == stderr))
    {
        int fd = fileno(pFile);
        if (fd >= 0)
        {
            HANDLE hCon = (HANDLE)_get_osfhandle(fd);
            if (   hCon != INVALID_HANDLE_VALUE
                && hCon != NULL)
            {
                if (is_console_handle((intptr_t)hCon))
                {
                    /* Use a stack buffer if we can, falling back on the heap for larger writes: */
                    wchar_t  awcBuf[1024];
                    wchar_t *pawcBuf;
                    wchar_t *pawcBufFree = NULL;
                    size_t   cbToWrite   = cbUnit * cUnits;
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
                            fflush(pFile);
                            rc = _cputws(pawcBuf);
                            if (pawcBufFree)
                                free(pawcBufFree);
                            if (rc >= 0)
                                return cUnits;
                            return 0;
                        }
                        free(pawcBufFree);
                    }
                }
            }
        }
    }
#endif

    /*
     * Semi regular write handling.
     */
    return fwrite(pvBuf, cbUnit, cUnits, pFile);
}

