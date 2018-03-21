/* $Id: kmkbuiltin.c 3169 2018-03-21 11:27:47Z knut.osmundsen@oracle.com $ */
/** @file
 * kMk Builtin command execution.
 */

/*
 * Copyright (c) 2005-2010 knut st. osmundsen <bird-kBuild-spamx@anduin.net>
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
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <assert.h>
#include <sys/stat.h>
#ifdef _MSC_VER
# include <io.h>
#endif
#if defined(KBUILD_OS_WINDOWS) && defined(CONFIG_NEW_WIN_CHILDREN)
# include "makeint.h"
# include "job.h"
# include "w32/winchildren.h"
#endif
#include "kmkbuiltin/err.h"
#include "kmkbuiltin.h"

#ifndef _MSC_VER
extern char **environ;
#endif


int kmk_builtin_command(const char *pszCmd, struct child *pChild, char ***ppapszArgvToSpawn, pid_t *pPidSpawned)
{
    int         argc;
    char      **argv;
    int         rc;
    char       *pszzCmd;
    char       *pszDst;
    int         fOldStyle = 0;

    /*
     * Check and skip the prefix.
     */
    if (strncmp(pszCmd, "kmk_builtin_", sizeof("kmk_builtin_") - 1))
    {
        fprintf(stderr, "kmk_builtin: Invalid command prefix '%s'!\n", pszCmd);
        return 1;
    }

    /*
     * Parse arguments.
     */
    rc      = 0;
    argc    = 0;
    argv    = NULL;
    pszzCmd = pszDst = (char *)strdup(pszCmd);
    if (!pszDst)
    {
        fprintf(stderr, "kmk_builtin: out of memory. argc=%d\n", argc);
        return 1;
    }
    do
    {
        const char * const pszSrcStart = pszCmd;
        char ch;
        char chQuote;

        /*
         * Start new argument.
         */
        if (!(argc % 16))
        {
            void *pv = realloc(argv, sizeof(char *) * (argc + 17));
            if (!pv)
            {
                fprintf(stderr, "kmk_builtin: out of memory. argc=%d\n", argc);
                rc = 1;
                break;
            }
            argv = (char **)pv;
        }
        argv[argc++] = pszDst;
        argv[argc]   = NULL;

        if (!fOldStyle)
        {
            /*
             * Process the next argument, bourne style.
             */
            chQuote = 0;
            ch = *pszCmd++;
            do
            {
                /* Unquoted mode? */
                if (chQuote == 0)
                {
                    if (ch != '\'' && ch != '"')
                    {
                        if (!isspace(ch))
                        {
                            if (ch != '\\')
                                *pszDst++ = ch;
                            else
                            {
                                ch = *pszCmd++;
                                if (ch)
                                    *pszDst++ = ch;
                                else
                                {
                                    fprintf(stderr, "kmk_builtin: Incomplete escape sequence in argument %d: %s\n",
                                            argc, pszSrcStart);
                                    rc = 1;
                                    break;
                                }
                            }
                        }
                        else
                            break;
                    }
                    else
                        chQuote = ch;
                }
                /* Quoted mode */
                else if (ch != chQuote)
                {
                    if (   ch != '\\'
                        || chQuote == '\'')
                        *pszDst++ = ch;
                    else
                    {
                        ch = *pszCmd++;
                        if (ch)
                        {
                            if (   ch != '\\'
                                && ch != '"'
                                && ch != '`'
                                && ch != '$'
                                && ch != '\n')
                                *pszDst++ = '\\';
                            *pszDst++ = ch;
                        }
                        else
                        {
                            fprintf(stderr, "kmk_builtin: Unbalanced quote in argument %d: %s\n", argc, pszSrcStart);
                            rc = 1;
                            break;
                        }
                    }
                }
                else
                    chQuote = 0;
            } while ((ch = *pszCmd++) != '\0');
        }
        else
        {
            /*
             * Old style in case we ever need it.
             */
            ch = *pszCmd++;
            if (ch != '"' && ch != '\'')
            {
                do
                    *pszDst++ = ch;
                while ((ch = *pszCmd++) != '\0' && !isspace(ch));
            }
            else
            {
                chQuote = ch;
                for (;;)
                {
                    char *pszEnd = strchr(pszCmd, chQuote);
                    if (pszEnd)
                    {
                        fprintf(stderr, "kmk_builtin: Unbalanced quote in argument %d: %s\n", argc, pszSrcStart);
                        rc = 1;
                        break;
                    }
                    memcpy(pszDst, pszCmd, pszEnd - pszCmd);
                    pszDst += pszEnd - pszCmd;
                    if (pszEnd[1] != chQuote)
                        break;
                    *pszDst++ = chQuote;
                }
            }
        }
        *pszDst++ = '\0';

        /*
         * Skip argument separators (IFS=space() for now).  Check for EOS.
         */
        if (ch != 0)
            while ((ch = *pszCmd) && isspace(ch))
                pszCmd++;
        if (ch == 0)
            break;
    } while (rc == 0);

    /*
     * Execute the command if parsing was successful.
     */
    if (rc == 0)
        rc = kmk_builtin_command_parsed(argc, argv, pChild, ppapszArgvToSpawn, pPidSpawned);

    /* clean up and return. */
    free(argv);
    free(pszzCmd);
    return rc;
}


/**
 * kmk built command.
 */
static const KMKBUILTINENTRY g_aBuiltins[] =
{
#define BUILTIN_ENTRY(a_fn, a_sz, a_uFnSignature, fMpSafe, fNeedEnv) \
    {  { { sizeof(a_sz) - 1, a_sz, } }, \
       (uintptr_t)a_fn,                                 a_uFnSignature,   fMpSafe, fNeedEnv }

    /* More frequently used commands: */
    BUILTIN_ENTRY(kmk_builtin_append,   "append",       FN_SIG_MAIN,            0, 0),
    BUILTIN_ENTRY(kmk_builtin_printf,   "printf",       FN_SIG_MAIN,            0, 0),
    BUILTIN_ENTRY(kmk_builtin_echo,     "echo",         FN_SIG_MAIN,            0, 0),
    BUILTIN_ENTRY(kmk_builtin_install,  "install",      FN_SIG_MAIN,            0, 0),
    BUILTIN_ENTRY(kmk_builtin_kDepObj,  "kDepObj",      FN_SIG_MAIN,            1, 0),
#ifdef KBUILD_OS_WINDOWS
    BUILTIN_ENTRY(kmk_builtin_kSubmit,  "kSubmit",      FN_SIG_MAIN_SPAWNS,     0, 0),
#endif
    BUILTIN_ENTRY(kmk_builtin_mkdir,    "mkdir",        FN_SIG_MAIN,            0, 0),
    BUILTIN_ENTRY(kmk_builtin_mv,       "mv",           FN_SIG_MAIN,            0, 0),
    BUILTIN_ENTRY(kmk_builtin_redirect, "redirect",     FN_SIG_MAIN_SPAWNS,     0, 1),
    BUILTIN_ENTRY(kmk_builtin_rm,       "rm",           FN_SIG_MAIN,            0, 1),
    BUILTIN_ENTRY(kmk_builtin_rmdir,    "rmdir",        FN_SIG_MAIN,            0, 0),
    BUILTIN_ENTRY(kmk_builtin_test,     "test",         FN_SIG_MAIN_TO_SPAWN,   0, 0),
    /* Less frequently used commands: */
    BUILTIN_ENTRY(kmk_builtin_kDepIDB,  "kDepIDB",      FN_SIG_MAIN,            0, 0),
    BUILTIN_ENTRY(kmk_builtin_chmod,    "chmod",        FN_SIG_MAIN,            0, 0),
    BUILTIN_ENTRY(kmk_builtin_cp,       "cp",           FN_SIG_MAIN,            0, 1),
    BUILTIN_ENTRY(kmk_builtin_expr,     "expr",         FN_SIG_MAIN,            0, 0),
    BUILTIN_ENTRY(kmk_builtin_ln,       "ln",           FN_SIG_MAIN,            0, 0),
    BUILTIN_ENTRY(kmk_builtin_md5sum,   "md5sum",       FN_SIG_MAIN,            0, 0),
    BUILTIN_ENTRY(kmk_builtin_cmp,      "cmp",          FN_SIG_MAIN,            0, 0),
    BUILTIN_ENTRY(kmk_builtin_cat,      "cat",          FN_SIG_MAIN,            0, 0),
    BUILTIN_ENTRY(kmk_builtin_touch,    "touch",        FN_SIG_MAIN,            0, 0),
    BUILTIN_ENTRY(kmk_builtin_sleep,    "sleep",        FN_SIG_MAIN,            0, 0),
    BUILTIN_ENTRY(kmk_builtin_dircache, "dircache",     FN_SIG_MAIN,            0, 0),
};


int kmk_builtin_command_parsed(int argc, char **argv, struct child *pChild, char ***ppapszArgvToSpawn, pid_t *pPidSpawned)
{
    /*
     * Check and skip the prefix.
     */
    static const char s_szPrefix[] = "kmk_builtin_";
    const char *pszCmd = argv[0];
    if (strncmp(pszCmd, s_szPrefix, sizeof(s_szPrefix) - 1) == 0)
    {
        struct KMKBUILTINENTRY const *pEntry;
        size_t cchAndStart;
        int    cLeft;

        pszCmd += sizeof(s_szPrefix) - 1;

        /*
         * Calc the length and start word to avoid calling memcmp/strcmp on each entry.
         */
#if K_ARCH_BITS != 64 && K_ARCH_BITS != 32
# error "PORT ME!"
#endif
        cchAndStart = strlen(pszCmd);
#if K_ENDIAN == K_ENDIAN_BIG
        cchAndStart <<= K_ARCH_BITS - 8;
        switch (cchAndStart)
        {
            default:                                   /* fall thru */
# if K_ARCH_BITS >= 64
            case 7: cchAndStart |= (size_t)pszCmd[6] << (K_ARCH_BITS - 56); /* fall thru */
            case 6: cchAndStart |= (size_t)pszCmd[5] << (K_ARCH_BITS - 48); /* fall thru */
            case 5: cchAndStart |= (size_t)pszCmd[4] << (K_ARCH_BITS - 40); /* fall thru */
            case 4: cchAndStart |= (size_t)pszCmd[3] << (K_ARCH_BITS - 32); /* fall thru */
# endif
            case 3: cchAndStart |= (size_t)pszCmd[2] << (K_ARCH_BITS - 24); /* fall thru */
            case 2: cchAndStart |= (size_t)pszCmd[1] << (K_ARCH_BITS - 16); /* fall thru */
            case 1: cchAndStart |= (size_t)pszCmd[0] << (K_ARCH_BITS -  8); /* fall thru */
            case 0: break;
        }
#else
        switch (cchAndStart)
        {
            default:                                   /* fall thru */
# if K_ARCH_BITS >= 64
            case 7: cchAndStart |= (size_t)pszCmd[6] << 56; /* fall thru */
            case 6: cchAndStart |= (size_t)pszCmd[5] << 48; /* fall thru */
            case 5: cchAndStart |= (size_t)pszCmd[4] << 40; /* fall thru */
            case 4: cchAndStart |= (size_t)pszCmd[3] << 32; /* fall thru */
# endif
            case 3: cchAndStart |= (size_t)pszCmd[2] << 24; /* fall thru */
            case 2: cchAndStart |= (size_t)pszCmd[1] << 16; /* fall thru */
            case 1: cchAndStart |= (size_t)pszCmd[0] <<  8; /* fall thru */
            case 0: break;
        }
#endif

        /*
         * Look up the builtin command in the table.
         */
        pEntry  = &g_aBuiltins[0];
        cLeft   = sizeof(g_aBuiltins) / sizeof(g_aBuiltins[0]);
        while (cLeft-- > 0)
            if (   pEntry->uName.cchAndStart != cchAndStart
                || (   pEntry->uName.s.cch >= sizeof(cchAndStart)
                    && memcmp(pEntry->uName.s.sz, pszCmd, pEntry->uName.s.cch) != 0) )
                pEntry++;
            else
            {
                /*
                 * That's a match!
                 */
                int rc;
#if defined(KBUILD_OS_WINDOWS) && defined(CONFIG_NEW_WIN_CHILDREN)
                if (pEntry->fMpSafe)
                    rc = MkWinChildCreateBuiltIn(pEntry, argc, argv, pEntry->fNeedEnv ? pChild->environment : NULL,
                                                 pChild, pPidSpawned);
                else
#endif
                {
                    char **envp = pChild->environment ? pChild->environment : environ;

                    /*
                     * Call the worker function, making sure to preserve umask.
                     */
                    int const iUmask = umask(0);        /* save umask */
                    umask(iUmask);

                    if (pEntry->uFnSignature == FN_SIG_MAIN)
                        rc = pEntry->u.pfnMain(argc, argv, envp);
                    else if (pEntry->uFnSignature == FN_SIG_MAIN_SPAWNS)
                        rc = pEntry->u.pfnMainSpawns(argc, argv, envp, pChild, pPidSpawned);
                    else if (pEntry->uFnSignature == FN_SIG_MAIN_TO_SPAWN)
                    {
                        /*
                         * When we got something to execute, check if the child is a kmk_builtin thing.
                         * We recurse here, both because I'm lazy and because it's easier to debug a
                         * problem then (the call stack shows what's been going on).
                         */
                        rc = pEntry->u.pfnMainToSpawn(argc, argv, envp, ppapszArgvToSpawn);
                        if (   !rc
                            && *ppapszArgvToSpawn
                            && !strncmp(**ppapszArgvToSpawn, s_szPrefix, sizeof(s_szPrefix) - 1))
                        {
                            char **argv_new = *ppapszArgvToSpawn;
                            int argc_new = 1;
                            while (argv_new[argc_new])
                              argc_new++;

                            assert(argv_new[0] != argv[0]);
                            assert(!*pPidSpawned);

                            *ppapszArgvToSpawn = NULL;
                            rc = kmk_builtin_command_parsed(argc_new, argv_new, pChild, ppapszArgvToSpawn, pPidSpawned);

                            free(argv_new[0]);
                            free(argv_new);
                        }
                    }
                    else
                        rc = 99;
                    g_progname = "kmk";                 /* paranoia, make sure it's not pointing at a freed argv[0]. */
                    umask(iUmask);                      /* restore it */
                }
                return rc;
            }

        /*
         * No match! :-(
         */
        fprintf(stderr, "kmk_builtin: Unknown command '%s%s'!\n", s_szPrefix, pszCmd);
    }
    else
        fprintf(stderr, "kmk_builtin: Invalid command prefix '%s'!\n", pszCmd);
    return 1;
}

#ifndef KBUILD_OS_WINDOWS
/** Dummy. */
int kmk_builtin_dircache(int argc, char **argv, char **envp)
{
    (void)argc; (void)argv; (void)envp;
    return 0;
}
#endif

