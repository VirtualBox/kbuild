/* $Id: kmkbuiltin.c 3040 2017-05-10 13:07:10Z knut.osmundsen@oracle.com $ */
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


int kmk_builtin_command_parsed(int argc, char **argv, struct child *pChild, char ***ppapszArgvToSpawn, pid_t *pPidSpawned)
{
    const char *pszCmd = argv[0];
    int         iUmask;
    int         rc;

    /*
     * Check and skip the prefix.
     */
    if (strncmp(pszCmd, "kmk_builtin_", sizeof("kmk_builtin_") - 1))
    {
        fprintf(stderr, "kmk_builtin: Invalid command prefix '%s'!\n", pszCmd);
        return 1;
    }
    pszCmd += sizeof("kmk_builtin_") - 1;

    /*
     * String switch on the command (frequent stuff at the top).
     */
    iUmask = umask(0);
    umask(iUmask);
    if (!strcmp(pszCmd, "append"))
        rc = kmk_builtin_append(argc, argv, environ);
    else if (!strcmp(pszCmd, "printf"))
        rc = kmk_builtin_printf(argc, argv, environ);
    else if (!strcmp(pszCmd, "echo"))
        rc = kmk_builtin_echo(argc, argv, environ);
    else if (!strcmp(pszCmd, "install"))
        rc = kmk_builtin_install(argc, argv, environ);
    else if (!strcmp(pszCmd, "kDepIDB"))
        rc = kmk_builtin_kDepIDB(argc, argv, environ);
#ifdef KBUILD_OS_WINDOWS
    else if (!strcmp(pszCmd, "kSubmit"))
        rc = kmk_builtin_kSubmit(argc, argv, environ, pChild, pPidSpawned);
#endif
    else if (!strcmp(pszCmd, "mkdir"))
        rc = kmk_builtin_mkdir(argc, argv, environ);
    else if (!strcmp(pszCmd, "mv"))
        rc = kmk_builtin_mv(argc, argv, environ);
    else if (!strcmp(pszCmd, "redirect"))
        rc = kmk_builtin_redirect(argc, argv, environ, pChild, pPidSpawned);
    else if (!strcmp(pszCmd, "rm"))
        rc = kmk_builtin_rm(argc, argv, environ);
    else if (!strcmp(pszCmd, "rmdir"))
        rc = kmk_builtin_rmdir(argc, argv, environ);
    else if (!strcmp(pszCmd, "test"))
        rc = kmk_builtin_test(argc, argv, environ, ppapszArgvToSpawn);
    /* rarely used commands: */
    else if (!strcmp(pszCmd, "kDepObj"))
        rc = kmk_builtin_kDepObj(argc, argv, environ);
    else if (!strcmp(pszCmd, "chmod"))
        rc = kmk_builtin_chmod(argc, argv, environ);
    else if (!strcmp(pszCmd, "cp"))
        rc = kmk_builtin_cp(argc, argv, environ);
    else if (!strcmp(pszCmd, "expr"))
        rc = kmk_builtin_expr(argc, argv, environ);
    else if (!strcmp(pszCmd, "ln"))
        rc = kmk_builtin_ln(argc, argv, environ);
    else if (!strcmp(pszCmd, "md5sum"))
        rc = kmk_builtin_md5sum(argc, argv, environ);
    else if (!strcmp(pszCmd, "cmp"))
        rc = kmk_builtin_cmp(argc, argv, environ);
    else if (!strcmp(pszCmd, "cat"))
        rc = kmk_builtin_cat(argc, argv, environ);
    else if (!strcmp(pszCmd, "sleep"))
        rc = kmk_builtin_sleep(argc, argv, environ);
    else if (!strcmp(pszCmd, "dircache"))
#ifdef KBUILD_OS_WINDOWS
        rc = kmk_builtin_dircache(argc, argv, environ);
#else
        rc = 0;
#endif
    else
    {
        fprintf(stderr, "kmk_builtin: Unknown command '%s'!\n", pszCmd);
        return 1;
    }

    /*
     * Cleanup.
     */
    g_progname = "kmk";                 /* paranoia, make sure it's not pointing at a freed argv[0]. */
    umask(iUmask);


    /*
     * If we've executed a conditional test or something that wishes to execute
     * some child process, check if the child is a kmk_builtin thing. We recurse
     * here, both because I'm lazy and because it's easier to debug a problem then
     * (the call stack shows what's been going on).
     */
    if (    !rc
        &&  *ppapszArgvToSpawn
        &&  !strncmp(**ppapszArgvToSpawn, "kmk_builtin_", sizeof("kmk_builtin_") - 1))
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

    return rc;
}

