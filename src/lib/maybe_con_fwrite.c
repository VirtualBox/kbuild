/* $Id: maybe_con_fwrite.c 2900 2016-09-09 14:42:06Z knut.osmundsen@oracle.com $ */
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
     * If it's a TTY, do our own conversion to wide char and
     * call WriteConsoleW directly.
     */
    if (   cbUnit > 0
        && cUnits > 0
        && (pFile == stdout || pFile == stderr))
    {
        int fd = fileno(pFile);
        if (fd >= 0)
        {
            if (isatty(fd))
            {
                HANDLE hCon = (HANDLE)_get_osfhandle(fd);
                if (   hCon != INVALID_HANDLE_VALUE
                    && hCon != NULL)
                {
                    size_t   cbToWrite = cbUnit * cUnits;
                    size_t   cwcTmp    = cbToWrite * 2 + 16;
                    wchar_t *pawcTmp   = (wchar_t *)malloc(cwcTmp * sizeof(wchar_t));
                    if (pawcTmp)
                    {
                        int           cwcToWrite;
                        static UINT s_uConsoleCp = 0;
                        if (s_uConsoleCp == 0)
                            s_uConsoleCp = GetConsoleCP();

                        cwcToWrite = MultiByteToWideChar(s_uConsoleCp, 0 /*dwFlags*/, pvBuf, (int)cbToWrite, pawcTmp, (int)(cwcTmp - 1));
                        if (cwcToWrite > 0)
                        {
                            /* Let the CRT do the rest.  At least the Visual C++ 2010 CRT
                               sources indicates _cputws will do the right thing we want.  */
                            pawcTmp[cwcToWrite] = '\0';
                            fflush(pFile);
                            if (_cputws(pawcTmp) >= 0)
                                return cUnits;
                            return 0;
                        }
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
