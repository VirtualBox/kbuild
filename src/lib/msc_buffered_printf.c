/* $Id: msc_buffered_printf.c 3669 2025-02-22 01:31:14Z knut.osmundsen@oracle.com $ */
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
#include <locale.h>
#include "console.h"

#pragma warning(disable: 4273) /* inconsistent dll linkage*/

#ifndef KWORKER
# define DLL_IMPORT __declspec(dllexport)
#else
# define DLL_IMPORT
#endif



#if _MSC_VER >= 1400
typedef int (__cdecl *PFN_STDIO_COMMON_VFPRINTF_T)(unsigned __int64, FILE *, const char *, _locale_t, va_list);

static PFN_STDIO_COMMON_VFPRINTF_T g_pfnFallback_vfprintf = NULL;

DLL_IMPORT
int __cdecl __stdio_common_vfprintf(unsigned __int64 fOptions, FILE *pFile, const char *pszFormat, _locale_t hLocale, va_list va)
{
    /* 
     * Make sure we've got the fallback function before we start.
     */
    PFN_STDIO_COMMON_VFPRINTF_T pfnFallback = g_pfnFallback_vfprintf;
    if (g_pfnFallback_vfprintf)
    { /* likely */ }
    else
    {
        HANDLE hmodCrt = GetModuleHandleW(L"api-ms-win-crt-stdio-l1-1-0.dll");
        if (!hmodCrt)
        {
            hmodCrt = GetModuleHandleW(L"ucrtbase.dll");
            if (!hmodCrt)
            {
                hmodCrt = LoadLibraryW(L"api-ms-win-crt-stdio-l1-1-0.dll");
                if (!hmodCrt)
                {
                    static char const s_szMsg[] = "fatal error! Failed to load the UCRT DLL and therefore no __stdio_common_vfprintf fallback!\r\n";
                    DWORD cbIgn = 0;
                    WriteFile(GetStdHandle(STD_ERROR_HANDLE ), s_szMsg, sizeof(s_szMsg) - 1, &cbIgn, NULL);
                    TerminateProcess(GetCurrentProcess(), 998);
                }
            }
        }

        pfnFallback = (PFN_STDIO_COMMON_VFPRINTF_T)GetProcAddress(hmodCrt, "__stdio_common_vfprintf");
        if (!pfnFallback)
        {
            static char const s_szMsg[] = "fatal error! Failed resolve __stdio_common_vfprintf in the UCRT DLL!\r\n";
            DWORD cbIgn = 0;
            WriteFile(GetStdHandle(STD_ERROR_HANDLE ), s_szMsg, sizeof(s_szMsg) - 1, &cbIgn, NULL);
            TerminateProcess(GetCurrentProcess(), 997);
        }

        g_pfnFallback_vfprintf = pfnFallback;
    }

    /*
     * If it's a TTY, try format into a stack buffer and output using our
     * console optimized fwrite wrapper.
     */
    if (*pszFormat != '\0')
    {
        if (hLocale == NULL)
        {
            int fd = fileno(pFile);
            if (fd >= 0)
            {
                if (is_console(fd))
                {
                    char *pszTmp = (char *)alloca(16384);
                    va_list va2 = va;
                    int cchRet = vsnprintf(pszTmp, 16384, pszFormat, va2);
                    if (cchRet < 16384 - 1)
                        return (int)maybe_con_fwrite(pszTmp, cchRet, 1, pFile);
                }
            }
        }
    }

    /*
     * Fallback.
     */
# ifdef DEBUG_STDOUT_CLOSE_ISSUE
    extern void my_check_stdout(const char *pszWhere);
    if (pFile == stdout)
        my_check_stdout("__stdio_common_vfprintf/entry");
    int rcRet = g_pfnFallback_vfprintf(fOptions, pFile, pszFormat, hLocale, va);
    if (pFile == stdout)
        my_check_stdout("__stdio_common_vfprintf/exit");
    return rcRet;
# else
    return g_pfnFallback_vfprintf(fOptions, pFile, pszFormat, hLocale, va);
# endif
}


void * const __imp___stdio_common_vfprintf  = (void *)(uintptr_t)__stdio_common_vfprintf;


#else /* < _MSC_VER <= 1400 */

# undef printf
# undef vprintf
# undef fprintf
# undef puts
# undef fputs

/**
 * Replaces printf for MSC to speed up console output.
 *
 * @returns chars written on success, -1 and errno on failure.
 * @param   pszFormat           The format string.
 * @param   ...                 Format arguments.
 */
DLL_IMPORT
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
DLL_IMPORT
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
            if (is_console(fd))
            {
                char *pszTmp = (char *)alloca(16384);
                va_list va2 = va;
                int cchRet = vsnprintf(pszTmp, 16384, pszFormat, va2);
                if (cchRet < 16384 - 1)
                    return (int)maybe_con_fwrite(pszTmp, cchRet, 1, stdout);
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
DLL_IMPORT
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
            if (is_console(fd))
            {
                char *pszTmp = (char *)alloca(16384);
                if (pszTmp)
                {
                    va_start(va, pszFormat);
                    cchRet = vsnprintf(pszTmp, 16384, pszFormat, va);
                    va_end(va);
                    if (cchRet < 16384 - 1)
                        return (int)maybe_con_fwrite(pszTmp, cchRet, 1, pFile);
                }
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
DLL_IMPORT
int __cdecl puts(const char *pszString)
{
    /*
     * If it's a TTY, we convert it to a wide char string with a newline
     * appended right here.  Going thru maybe_con_fwrite is just extra
     * buffering due to the added newline.
     */
    size_t cchString = strlen(pszString);
    size_t cch;
    if (cchString > 0 && cchString < INT_MAX / 2)
    {
        int fd = fileno(stdout);
        if (fd >= 0)
        {
            if (is_console(fd))
            {
                HANDLE hCon = (HANDLE)_get_osfhandle(fd);
                if (   hCon != INVALID_HANDLE_VALUE
                    && hCon != NULL)
                {
                    wchar_t  awcBuf[1024];
                    wchar_t *pawcBuf;
                    wchar_t *pawcBufFree = NULL;
                    size_t   cwcBuf      = cchString * 2 + 16 + 1; /* +1 for added newline */
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
                                                             pszString, (int)cchString,
                                                             pawcBuf, (int)(cwcBuf - 1));
                        if (cwcToWrite > 0)
                        {
                            int rc;
                            pawcBuf[cwcToWrite++] = '\n';
                            pawcBuf[cwcToWrite]   = '\0';

                            /* Let the CRT do the rest.  At least the Visual C++ 2010 CRT
                               sources indicates _cputws will do the right thing.  */
                            fflush(stdout);
                            rc = _cputws(pawcBuf);
                            if (pawcBufFree)
                                free(pawcBufFree);
                            if (rc >= 0)
                                return 0;
                            return -1;
                        }
                        free(pawcBufFree);
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
DLL_IMPORT
int __cdecl fputs(const char *pszString, FILE *pFile)
{
    size_t cchString = strlen(pszString);
    size_t cch = maybe_con_fwrite(pszString, cchString, 1, pFile);
    if (cch == cchString)
        return 0;
    return -1;
}



void * const __imp_printf  = (void *)(uintptr_t)printf;
void * const __imp_vprintf = (void *)(uintptr_t)vprintf;
void * const __imp_fprintf = (void *)(uintptr_t)fprintf;
void * const __imp_puts    = (void *)(uintptr_t)puts;
void * const __imp_fputs   = (void *)(uintptr_t)fputs;

#endif /* Old MSC */
