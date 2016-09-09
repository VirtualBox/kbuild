/* $Id: msc_buffered_printf.c 2907 2016-09-09 22:33:26Z knut.osmundsen@oracle.com $ */
/** @file
 * printf, vprintf, fprintf, puts, fputs console optimizations for Windows/MSC.
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
#include <Windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <io.h>
#include <conio.h>
#include <malloc.h>

#undef printf
#undef vprintf
#undef fprintf
#undef puts
#undef fputs
#pragma warning(disable: 4273) /* inconsistent dll linkage*/

extern size_t maybe_con_fwrite(void const *pvBuf, size_t cbUnit, size_t cUnits, FILE *pFile);


/**
 * Replaces printf for MSC to speed up console output.
 *
 * @returns chars written on success, -1 and errno on failure.
 * @param   pszFormat           The format string.
 * @param   ...                 Format arguments.
 */
__declspec(dllexport)
int __cdecl printf(const char *pszFormat, ...)
{
    int cchRet;
    va_list va;
    va_start(va, pszFormat);
    cchRet = vprintf(pszFormat, va);
    va_end(va);
    return cchRet;
}


/**
 * Replaces vprintf for MSC to speed up console output.
 *
 * @returns chars written on success, -1 and errno on failure.
 * @param   pszFormat           The format string.
 * @param   va                  Format arguments.
 */
__declspec(dllexport)
int __cdecl vprintf(const char *pszFormat, va_list va)
{
    /*
     * If it's a TTY, try format into a stack buffer and output using our
     * console optimized fwrite wrapper.
     */
    if (*pszFormat != '\0')
    {
        int fd = fileno(stdout);
        if (fd >= 0)
        {
            if (isatty(fd))
            {
                char szTmp[8192];
                va_list va2 = va;
                int cchRet = vsnprintf(szTmp, sizeof(szTmp), pszFormat, va2);
                if (cchRet >= sizeof(szTmp) - 1)
                    return (int)maybe_con_fwrite(szTmp, cchRet, 1, stdout);
            }
        }
    }

    /*
     * Fallback.
     */
    return vfprintf(stdout, pszFormat, va);
}


/**
 * Replaces fprintf for MSC to speed up console output.
 *
 * @returns chars written on success, -1 and errno on failure.
 * @param   pFile               The output file/stream.
 * @param   pszFormat           The format string.
 * @param   va                  Format arguments.
 */
__declspec(dllexport)
int __cdecl fprintf(FILE *pFile, const char *pszFormat, ...)
{
    va_list va;
    int cchRet;

    /*
     * If it's a TTY, try format into a stack buffer and output using our
     * console optimized fwrite wrapper.
     */
    if (*pszFormat != '\0')
    {
        int fd = fileno(pFile);
        if (fd >= 0)
        {
            if (isatty(fd))
            {
                char szTmp[8192];
                va_start(va, pszFormat);
                cchRet = vsnprintf(szTmp, sizeof(szTmp), pszFormat, va);
                va_end(va);
                if (cchRet >= sizeof(szTmp) - 1)
                    return (int)maybe_con_fwrite(szTmp, cchRet, 1, pFile);
            }
        }
    }

    /*
     * Fallback.
     */
    va_start(va, pszFormat);
    cchRet = vfprintf(pFile, pszFormat, va);
    va_end(va);
    return cchRet;
}


/**
 * Replaces puts for MSC to speed up console output.
 *
 * @returns Units written; 0 & errno on failure.
 * @param   pszString           The string to write. (newline is appended)
 */
__declspec(dllexport)
int __cdecl puts(const char *pszString)
{
    size_t cchString = strlen(pszString);
    size_t cch;

    /*
     * If it's a TTY, we convert it to a wide char string with a newline
     * appended right here.  Going thru maybe_con_fwrite is just extra
     * buffering due to the added newline.
     */
    if (*pszString != '\0')
    {
        int fd = fileno(stdout);
        if (fd >= 0)
        {
            if (isatty(fd))
            {
                HANDLE hCon = (HANDLE)_get_osfhandle(fd);
                if (   hCon != INVALID_HANDLE_VALUE
                    && hCon != NULL)
                {
                    /* We need to append a newline, so we can just as well do the conversion here. */
                    size_t   cwcTmp  = cchString * 2 + 16 + 2;
                    wchar_t *pawcTmp = (wchar_t *)malloc(cwcTmp * sizeof(wchar_t));
                    if (pawcTmp)
                    {
                        int           cwcToWrite;
                        static UINT s_uConsoleCp = 0;
                        if (s_uConsoleCp == 0)
                            s_uConsoleCp = GetConsoleCP();

                        cwcToWrite = MultiByteToWideChar(s_uConsoleCp, 0 /*dwFlags*/, pszString, (int)cchString, pawcTmp,
                                                         (int)(cwcTmp - 2));
                        if (cwcToWrite > 0)
                        {
                            int rc;

                            pawcTmp[cwcToWrite++] = '\n';
                            pawcTmp[cwcToWrite]   = '\0';

                            /* Let the CRT do the rest.  At least the Visual C++ 2010 CRT
                               sources indicates _cputws will do the right thing we want.  */
                            fflush(stdout);
                            rc = _cputws(pawcTmp);
                            free(pawcTmp);
                            return rc;
                        }
                        free(pawcTmp);
                    }
                }
            }
        }
    }

    /*
     * Fallback.
     */
    cch = fwrite(pszString, cchString, 1, stdout);
    if (cch == cchString)
    {
        if (putc('\n', stdout) != EOF)
            return 0;
    }
    return -1;
}


/**
 * Replaces puts for MSC to speed up console output.
 *
 * @returns Units written; 0 & errno on failure.
 * @param   pszString           The string to write (no newline added).
 * @param   pFile               The output file.
 */
__declspec(dllexport)
int __cdecl fputs(const char *pszString, FILE *pFile)
{
    size_t cchString = strlen(pszString);
    size_t cch = maybe_con_fwrite(pszString, cchString, 1, stdout);
    if (cch == cchString)
        return 0;
    return -1;
}



void * const __imp_printf  = (void *)(uintptr_t)printf;
void * const __imp_vprintf = (void *)(uintptr_t)vprintf;
void * const __imp_fprintf = (void *)(uintptr_t)fprintf;
void * const __imp_puts    = (void *)(uintptr_t)puts;
void * const __imp_fputs   = (void *)(uintptr_t)fputs;

