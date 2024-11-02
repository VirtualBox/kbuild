/* $Id: err.c 3636 2024-11-02 01:52:45Z knut.osmundsen@oracle.com $ */
/** @file
 * Override err.h so we get the program name right.
 */

/*
 * Copyright (c) 2005-2016 knut st. osmundsen <bird-kBuild-spamx@anduin.net>
 *
 * This file is part of kBuild.
 *
 * kBuild is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * kBuild is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with kBuild.  If not, see <http://www.gnu.org/licenses/>
 *
 */

/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
#ifdef HAVE_CONFIG_H
# include "config.h"
# ifdef HAVE_STDLIB_H
#  include <stdlib.h>
# endif
# ifdef HAVE_STDINT_H
#  include <stdint.h>
# endif
#else
# include <stdlib.h>
# if _MSC_VER < 1400
#  define snprintf _snprintf
# endif
#endif
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include "err.h"
#if !defined(KMK_BUILTIN_STANDALONE) && !defined(KWORKER)
# include "../output.h"
#endif

#ifdef KBUILD_OS_WINDOWS
/* This is a trick to speed up console output on windows. */
# include "console.h"
# undef fwrite
# define fwrite maybe_con_fwrite
#endif

int err(PKMKBUILTINCTX pCtx, int eval, const char *fmt, ...)
{
    /*
     * We format into a buffer and pass that onto output.c or fwrite.
     */
    int     error = errno;
    char   *pszToFree = NULL;
    char    szMsgStack[4096];
    char   *pszMsg = szMsgStack;
    size_t  cbMsg = sizeof(szMsgStack);
    for (;;)
    {
        int cchMsg = snprintf(pszMsg, cbMsg, "%s: error: ", pCtx->pszProgName);
        if (cchMsg < (int)cbMsg - 1 && cchMsg > 0)
        {
            int cchMsg2;
            va_list va;
            va_start(va, fmt);
            cchMsg += cchMsg2 = vsnprintf(&pszMsg[cchMsg], cbMsg - cchMsg, fmt, va);
            va_end(va);

            if (   cchMsg < (int)cbMsg - 1
                && cchMsg2 >= 0)
            {
                cchMsg += cchMsg2 = snprintf(&pszMsg[cchMsg], cbMsg - cchMsg, ": %s\n", strerror(error));
                if (   cchMsg < (int)cbMsg - 1
                    && cchMsg2 >= 0)
                {
#if !defined(KMK_BUILTIN_STANDALONE) && !defined(KWORKER)
                    if (pCtx->pOut)
                        output_write_text(pCtx->pOut, 1 /*is_err*/, pszMsg, cchMsg);
                    else
#endif
                    {
                        fflush(stdout);
                        fwrite(pszMsg, cchMsg, 1, stderr);
                        fflush(stderr); /* paranoia */
                    }
                    if (pszToFree)
                        free(pszToFree);
                    errno = error;
                    return eval;
                }
            }
        }

        /* double the buffer size and retry */
        if (pszToFree)
            free(pszToFree);
        cbMsg *= 2;
        pszToFree = malloc(cbMsg);
        if (!pszToFree)
        {
            fprintf(stderr, "out of memory!\n");
            errno = error;
            return eval;
        }
    }
}


int errx(PKMKBUILTINCTX pCtx, int eval, const char *fmt, ...)
{
    /*
     * We format into a buffer and pass that onto output.c or fwrite.
     */
    char   *pszToFree = NULL;
    char    szMsgStack[4096];
    char   *pszMsg = szMsgStack;
    size_t  cbMsg = sizeof(szMsgStack);
    for (;;)
    {
        int cchMsg = snprintf(pszMsg, cbMsg, "%s: error: ", pCtx->pszProgName);
        if (cchMsg < (int)cbMsg - 1 && cchMsg > 0)
        {
            int cchMsg2;
            va_list va;
            va_start(va, fmt);
            cchMsg += cchMsg2 = vsnprintf(&pszMsg[cchMsg], cbMsg - cchMsg, fmt, va);
            va_end(va);

            if (   cchMsg < (int)cbMsg - 2
                && cchMsg2 >= 0)
            {
                /* ensure newline */
                if (pszMsg[cchMsg - 1] != '\n')
                {
                    pszMsg[cchMsg++] = '\n';
                    pszMsg[cchMsg] = '\0';
                }

#if !defined(KMK_BUILTIN_STANDALONE) && !defined(KWORKER)
                if (pCtx->pOut)
                    output_write_text(pCtx->pOut, 1 /*is_err*/, pszMsg, cchMsg);
                else
#endif
                {
                    fflush(stdout);
                    fwrite(pszMsg, cchMsg, 1, stderr);
                    fflush(stderr); /* paranoia */
                }
                if (pszToFree)
                    free(pszToFree);
                return eval;
            }
        }

        /* double the buffer size and retry */
        if (pszToFree)
            free(pszToFree);
        cbMsg *= 2;
        pszToFree = malloc(cbMsg);
        if (!pszToFree)
        {
            fprintf(stderr, "out of memory!\n");
            return eval;
        }
    }
}

void warn(PKMKBUILTINCTX pCtx, const char *fmt, ...)
{
    /*
     * We format into a buffer and pass that onto output.c or fwrite.
     */
    int     error = errno;
    char   *pszToFree = NULL;
    char    szMsgStack[4096];
    char   *pszMsg = szMsgStack;
    size_t  cbMsg = sizeof(szMsgStack);
    for (;;)
    {
        int cchMsg = snprintf(pszMsg, cbMsg, "%s: ", pCtx->pszProgName);
        if (cchMsg < (int)cbMsg - 1 && cchMsg > 0)
        {
            int cchMsg2;
            va_list va;
            va_start(va, fmt);
            cchMsg += cchMsg2 = vsnprintf(&pszMsg[cchMsg], cbMsg - cchMsg, fmt, va);
            va_end(va);

            if (   cchMsg < (int)cbMsg - 1
                && cchMsg2 >= 0)
            {
                cchMsg += cchMsg2 = snprintf(&pszMsg[cchMsg], cbMsg - cchMsg, ": %s\n", strerror(error));
                if (   cchMsg < (int)cbMsg - 1
                    && cchMsg2 >= 0)
                {
#if !defined(KMK_BUILTIN_STANDALONE) && !defined(KWORKER)
                    if (pCtx->pOut)
                        output_write_text(pCtx->pOut, 1 /*is_err*/, pszMsg, cchMsg);
                    else
#endif
                    {
                        fflush(stdout);
                        fwrite(pszMsg, cchMsg, 1, stderr);
                        fflush(stderr); /* paranoia */
                    }
                    if (pszToFree)
                        free(pszToFree);
                    errno = error;
                    return;
                }
            }
        }

        /* double the buffer size and retry */
        if (pszToFree)
            free(pszToFree);
        cbMsg *= 2;
        pszToFree = malloc(cbMsg);
        if (!pszToFree)
        {
            fprintf(stderr, "out of memory!\n");
            errno = error;
            return;
        }
    }
}

void warnx(PKMKBUILTINCTX pCtx, const char *fmt, ...)
{
    /*
     * We format into a buffer and pass that onto output.c or fwrite.
     */
    char   *pszToFree = NULL;
    char    szMsgStack[4096];
    char   *pszMsg = szMsgStack;
    size_t  cbMsg = sizeof(szMsgStack);
    for (;;)
    {
        int cchMsg = snprintf(pszMsg, cbMsg, "%s: ", pCtx->pszProgName);
        if (cchMsg < (int)cbMsg - 1 && cchMsg > 0)
        {
            int cchMsg2;
            va_list va;
            va_start(va, fmt);
            cchMsg += cchMsg2 = vsnprintf(&pszMsg[cchMsg], cbMsg - cchMsg, fmt, va);
            va_end(va);

            if (   cchMsg < (int)cbMsg - 2
                && cchMsg2 >= 0)
            {
                /* ensure newline */
                if (pszMsg[cchMsg - 1] != '\n')
                {
                    pszMsg[cchMsg++] = '\n';
                    pszMsg[cchMsg] = '\0';
                }

#if !defined(KMK_BUILTIN_STANDALONE) && !defined(KWORKER)
                if (pCtx->pOut)
                    output_write_text(pCtx->pOut, 1 /*is_err*/, pszMsg, cchMsg);
                else
#endif
                {
                    fflush(stdout);
                    fwrite(pszMsg, cchMsg, 1, stderr);
                    fflush(stderr); /* paranoia */
                }
                if (pszToFree)
                    free(pszToFree);
                return;
            }
        }

        /* double the buffer size and retry */
        if (pszToFree)
            free(pszToFree);
        cbMsg *= 2;
        pszToFree = malloc(cbMsg);
        if (!pszToFree)
        {
            fprintf(stderr, "out of memory!\n");
            return;
        }
    }
}

void kmk_builtin_ctx_printf(PKMKBUILTINCTX pCtx, int fIsErr, const char *pszFormat, ...)
{
    /*
     * We format into a buffer and pass that onto output.c or fwrite.
     */
    char   *pszToFree = NULL;
    char    szMsgStack[4096];
    char   *pszMsg = szMsgStack;
    size_t  cbMsg = sizeof(szMsgStack);
    for (;;)
    {
        int cchMsg;
        va_list va;
        va_start(va, pszFormat);
        cchMsg = vsnprintf(pszMsg, cbMsg, pszFormat, va);
        va_end(va);
        if (cchMsg < (int)cbMsg - 1 && cchMsg > 0)
        {
#if !defined(KMK_BUILTIN_STANDALONE) && !defined(KWORKER)
            if (pCtx->pOut)
                output_write_text(pCtx->pOut, fIsErr, pszMsg, cchMsg);
            else
#endif
            {
                fwrite(pszMsg, cchMsg, 1, fIsErr ? stderr : stdout);
                fflush(fIsErr ? stderr : stdout);
            }
            if (pszToFree)
                free(pszToFree);
            return;
        }

        /* double the buffer size and retry */
        if (pszToFree)
            free(pszToFree);
        cbMsg *= 2;
        pszToFree = malloc(cbMsg);
        if (!pszToFree)
        {
            fprintf(stderr, "out of memory!\n");
            return;
        }
    }
}

