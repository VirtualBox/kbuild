/* $Id: kmkbuiltin.c 1293 2007-12-01 03:42:50Z knut.osmundsen@oracle.com $ */
/** @file
 *
 * kMk Builtin command execution.
 *
 * Copyright (c) 2005-2007 knut st. osmundsen <bird-kBuild-spam@anduin.net>
 *
 *
 * This file is part of kBuild.
 *
 * kBuild is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * kBuild is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with kBuild; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <assert.h>
#ifdef _MSC_VER
# include <io.h>
#endif
#include "kmkbuiltin/err.h"
#include "kmkbuiltin.h"

#ifndef _MSC_VER
extern char **environ;
#endif

int kmk_builtin_command(const char *pszCmd, char ***ppapszArgvToSpawn, pid_t *pPidSpawned)
{
    int         argc;
    char      **argv;
    int         rc;

    /*
     * Check and skip the prefix.
     */
    if (strncmp(pszCmd, "kmk_builtin_", sizeof("kmk_builtin_") - 1))
    {
        printf("kmk_builtin: Invalid command prefix '%s'!\n", pszCmd);
        return 1;
    }

    /*
     * Parse arguments.
     */
    argc = 0;
    argv = NULL;
    while (*pszCmd)
    {
        const char *pszEnd;
        const char *pszNext;
        int         fEscaped = 0;
        size_t      cch;

        /*
         * Find start and end of the current command.
         */
        if (*pszCmd == '"' || *pszCmd == '\'')
        {
            pszEnd = pszCmd;
            for (;;)
            {
                pszEnd = strchr(pszEnd + 1, *pszCmd);
                if (!pszEnd)
                {
                    printf("kmk_builtin: Unbalanced quote in argument %d: %s\n", argc + 1, pszCmd);
                    while (argc--)
                        free(argv[argc]);
                    free(argv);
                    return 1;
                }
                /* two quotes -> escaped quote. */
                if (pszEnd[0] != pszEnd[1])
                    break;
                fEscaped = 1;
            }
            pszNext = pszEnd + 1;
            pszCmd++;
        }
        else
        {
            pszEnd = pszCmd;
            while (!isspace(*pszEnd) && *pszEnd)
                pszEnd++;
            pszNext = pszEnd;
        }

        /*
         * Make argument.
         */
        if (!(argc % 16))
        {
            void *pv = realloc(argv, sizeof(char *) * (argc + 17));
            if (!pv)
            {
                printf("kmk_builtin: out of memory. argc=%d\n", argc);
                break;
            }
            argv = (char **)pv;
        }
        cch = pszEnd - pszCmd;
        argv[argc] = malloc(cch + 1);
        if (!argv[argc])
        {
            printf("kmk_builtin: out of memory. argc=%d len=%d\n", argc, pszEnd - pszCmd + 1);
            break;
        }
        memcpy(argv[argc], pszCmd, cch);
        argv[argc][cch] = '\0';

        /* unescape quotes? */
        if (fEscaped)
        {
            char ch = pszCmd[-1];
            char *pszW = argv[argc];
            char *pszR = argv[argc];
            while (*pszR)
            {
                if (*pszR == ch)
                    pszR++;
                *pszW++ = *pszR++;
            }
            *pszW = '\0';
        }
        /* commit it */
        argv[++argc] = NULL;

        /*
         * Next
         */
        pszCmd = pszNext;
        if (isspace(*pszCmd) && *pszCmd)
            pszCmd++;
    }

    /*
     * Execute the command if parsing was successful.
     */
    if (!*pszCmd)
        rc = kmk_builtin_command_parsed(argc, argv, ppapszArgvToSpawn, pPidSpawned);
    else
        rc = 1;

    /* clean up and return. */
    while (argc--)
        free(argv[argc]);
    free(argv);
    return rc;
}


int kmk_builtin_command_parsed(int argc, char **argv, char ***ppapszArgvToSpawn, pid_t *pPidSpawned)
{
    const char *pszCmd = argv[0];
    int         iumask;
    int         rc;

    /*
     * Check and skip the prefix.
     */
    if (strncmp(pszCmd, "kmk_builtin_", sizeof("kmk_builtin_") - 1))
    {
        printf("kmk_builtin: Invalid command prefix '%s'!\n", pszCmd);
        return 1;
    }
    pszCmd += sizeof("kmk_builtin_") - 1;

    /*
     * String switch on the command.
     */
    iumask = umask(0);
    umask(iumask);
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
    else if (!strcmp(pszCmd, "mkdir"))
        rc = kmk_builtin_mkdir(argc, argv, environ);
    else if (!strcmp(pszCmd, "mv"))
        rc = kmk_builtin_mv(argc, argv, environ);
    /*else if (!strcmp(pszCmd, "redirect"))
        rc = kmk_builtin_redirect(argc, argv, environ, pPidSpawned);*/
    else if (!strcmp(pszCmd, "rm"))
        rc = kmk_builtin_rm(argc, argv, environ);
    else if (!strcmp(pszCmd, "rmdir"))
        rc = kmk_builtin_rmdir(argc, argv, environ);
    else if (!strcmp(pszCmd, "test"))
        rc = kmk_builtin_test(argc, argv, environ, ppapszArgvToSpawn);
    /* rarely used commands: */
    else if (!strcmp(pszCmd, "cp"))
        rc = kmk_builtin_cp(argc, argv, environ);
    else if (!strcmp(pszCmd, "ln"))
        rc = kmk_builtin_ln(argc, argv, environ);
    else if (!strcmp(pszCmd, "md5sum"))
        rc = kmk_builtin_md5sum(argc, argv, environ);
    else if (!strcmp(pszCmd, "cmp"))
        rc = kmk_builtin_cmp(argc, argv, environ);
    else if (!strcmp(pszCmd, "cat"))
        rc = kmk_builtin_cat(argc, argv, environ);
    else
    {
        printf("kmk_builtin: Unknown command '%s'!\n", pszCmd);
        return 1;
    }

    /*
     * Cleanup.
     */
    g_progname = "kmk";                 /* paranoia, make sure it's not pointing at a freed argv[0]. */
    umask(iumask);


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
        rc = kmk_builtin_command_parsed(argc_new, argv_new, ppapszArgvToSpawn, pPidSpawned);

        free(argv_new[0]);
        free(argv_new);
    }

    return rc;
}

