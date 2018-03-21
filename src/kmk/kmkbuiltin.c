/* $Id: kmkbuiltin.c 3168 2018-03-21 00:04:56Z knut.osmundsen@oracle.com $ */
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
static struct KMKBUILTINENTRY
{
    const char *pszName;
    size_t      cchName;
    union
    {
        uintptr_t uPfn;
#define FN_SIG_MAIN             0
        int (* pfnMain)(int argc, char **argv, char **envp);
#define FN_SIG_MAIN_SPAWNS      1
        int (* pfnMainSpawns)(int argc, char **argv, char **envp, struct child *pChild, pid_t *pPid);
#define FN_SIG_MAIN_TO_SPAWN    2
        int (* pfnMainToSpawn)(int argc, char **argv, char **envp, char ***ppapszArgvToSpawn);
    } u;
    size_t      uFnSignature : 8;
    size_t      fMpSafe : 1;
    size_t      fNeedEnv : 1;
} const g_aBuiltins[] =
{
#define BUILTIN_ENTRY(a_fn, a_uFnSignature, fMpSafe, fNeedEnv) \
    { &(#a_fn)[12], sizeof(#a_fn) - 12 - 1, \
       (uintptr_t)a_fn,                     a_uFnSignature,  fMpSafe, fNeedEnv }

    /* More frequently used commands: */
    BUILTIN_ENTRY(kmk_builtin_append,       FN_SIG_MAIN,            0, 0),
    BUILTIN_ENTRY(kmk_builtin_printf,       FN_SIG_MAIN,            0, 0),
    BUILTIN_ENTRY(kmk_builtin_echo,         FN_SIG_MAIN,            0, 0),
    BUILTIN_ENTRY(kmk_builtin_install,      FN_SIG_MAIN,            0, 0),
    BUILTIN_ENTRY(kmk_builtin_kDepObj,      FN_SIG_MAIN,            0, 0),
#ifdef KBUILD_OS_WINDOWS
    BUILTIN_ENTRY(kmk_builtin_kSubmit,      FN_SIG_MAIN_SPAWNS,     0, 0),
#endif
    BUILTIN_ENTRY(kmk_builtin_mkdir,        FN_SIG_MAIN,            0, 0),
    BUILTIN_ENTRY(kmk_builtin_mv,           FN_SIG_MAIN,            0, 0),
    BUILTIN_ENTRY(kmk_builtin_redirect,     FN_SIG_MAIN_SPAWNS,     0, 0),
    BUILTIN_ENTRY(kmk_builtin_rm,           FN_SIG_MAIN,            0, 0),
    BUILTIN_ENTRY(kmk_builtin_rmdir,        FN_SIG_MAIN,            0, 0),
    BUILTIN_ENTRY(kmk_builtin_test,         FN_SIG_MAIN_TO_SPAWN,   0, 0),
    /* Less frequently used commands: */
    BUILTIN_ENTRY(kmk_builtin_kDepIDB,      FN_SIG_MAIN,            0, 0),
    BUILTIN_ENTRY(kmk_builtin_chmod,        FN_SIG_MAIN,            0, 0),
    BUILTIN_ENTRY(kmk_builtin_cp,           FN_SIG_MAIN,            0, 0),
    BUILTIN_ENTRY(kmk_builtin_expr,         FN_SIG_MAIN,            0, 0),
    BUILTIN_ENTRY(kmk_builtin_ln,           FN_SIG_MAIN,            0, 0),
    BUILTIN_ENTRY(kmk_builtin_md5sum,       FN_SIG_MAIN,            0, 0),
    BUILTIN_ENTRY(kmk_builtin_cmp,          FN_SIG_MAIN,            0, 0),
    BUILTIN_ENTRY(kmk_builtin_cat,          FN_SIG_MAIN,            0, 0),
    BUILTIN_ENTRY(kmk_builtin_touch,        FN_SIG_MAIN,            0, 0),
    BUILTIN_ENTRY(kmk_builtin_sleep,        FN_SIG_MAIN,            0, 0),
    BUILTIN_ENTRY(kmk_builtin_dircache,     FN_SIG_MAIN,            0, 0),
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
        size_t cchCmd;
        char ch0;
        int  cLeft;

        pszCmd += sizeof(s_szPrefix) - 1;

        /*
         * Look up the builtin command in the table.
         */
        cchCmd  = strlen(pszCmd);
        ch0     = *pszCmd;
        pEntry  = &g_aBuiltins[0];
        cLeft   = sizeof(g_aBuiltins) / sizeof(g_aBuiltins[0]);
        while (cLeft-- > 0)
            if (   *pEntry->pszName != ch0
                || pEntry->cchName != cchCmd
                || memcmp(pEntry->pszName, pszCmd, cchCmd) != 0)
                pEntry++;
            else
            {
                int rc;
#if defined(KBUILD_OS_WINDOWS) && CONFIG_NEW_WIN_CHILDREN
                if (pEntry->fMpSafe)
                {
                    rc = 98;
                }
                else
#endif
                {
                    char **envp = environ; /** @todo fixme? */

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

