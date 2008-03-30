/* $Id: append.c 1440 2008-03-30 04:13:55Z knut.osmundsen@oracle.com $ */
/** @file
 *
 * kMk Builtin command - append text to file.
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
#include <stdio.h>
#include "err.h"
#include "kmkbuiltin.h"
#ifndef kmk_builtin_append
# include "make.h"
# include "filedef.h"
# include "variable.h"
#endif


/**
 * Prints the usage and return 1.
 */
static int usage(FILE *pf)
{
    fprintf(pf,
            "usage: %s [-cnv] file [string ...]\n"
            "   or: %s --version\n"
            "   or: %s --help\n",
            g_progname, g_progname, g_progname);
    return 1;
}


/**
 * Appends text to a textfile, creating the textfile if necessary.
 */
int kmk_builtin_append(int argc, char **argv, char **envp)
{
    int i;
    int fFirst;
    FILE *pFile;
    int fNewLine = 0;
#ifndef kmk_builtin_append
    int fVariables = 0;
    int fCommands = 0;
#endif

    g_progname = argv[0];

    /*
     * Parse options.
     */
    i = 1;
    while (i < argc
       &&  argv[i][0] == '-'
       &&  argv[i][1] != '\0' /* '-' is a file */
       &&  strchr("-nv", argv[i][1]) /* valid option char */
       )
    {
        char *psz = &argv[i][1];
        if (*psz != '-')
        {
            do
            {
                switch (*psz)
                {
                    case 'c':
#ifndef kmk_builtin_append
                        fCommands = 1;
                        break;
#else
                        errx(1, "Option '-c' isn't supported in external mode.");
                        return usage(stderr);
#endif
                    case 'n':
                        fNewLine = 1;
                        break;
                    case 'v':
#ifndef kmk_builtin_append
                        fVariables = 1;
                        break;
#else
                        errx(1, "Option '-v' isn't supported in external mode.");
                        return usage(stderr);
#endif
                    default:
                        errx(1, "Invalid option '%c'! (%s)", *psz, argv[i]);
                        return usage(stderr);
                }
            } while (*++psz);
        }
        else if (!strcmp(psz, "-help"))
        {
            usage(stdout);
            return 0;
        }
        else if (!strcmp(psz, "-version"))
            return kbuild_version(argv[0]);
        else
            break;
        i++;
    }

    /*
     * Open the output file.
     */
    if (i >= argc)
    {
        errx(1, "missing filename!");
        return usage(stderr);
    }
    pFile = fopen(argv[i], "a");
    if (!pFile)
        return err(1, "failed to open '%s'.", argv[i]);

    /*
     * Append the argument strings to the file
     */
    fFirst = 1;
    for (i++; i < argc; i++)
    {
        const char *psz = argv[i];
        size_t cch = strlen(psz);
        if (!fFirst)
            fputc(fNewLine ? '\n' : ' ', pFile);
#ifndef kmk_builtin_append
        if (fCommands)
        {
            char *pszOldBuf;
            unsigned cchOldBuf;
            char *pchEnd;

            install_variable_buffer(&pszOldBuf, &cchOldBuf);

            pchEnd = func_commands(variable_buffer, &argv[i], "commands");
            fwrite(variable_buffer, 1, pchEnd - variable_buffer, pFile);

            restore_variable_buffer(pszOldBuf, cchOldBuf);
        }
        else if (fVariables)
        {
            struct variable *pVar = lookup_variable(psz, cch);
            if (!pVar)
                continue;
            if (    pVar->recursive
                &&  memchr(pVar->value, '$', pVar->value_length))
            {
                char *pszExpanded = allocated_variable_expand(pVar->value);
                fwrite(pszExpanded, 1, strlen(pszExpanded), pFile);
                free(pszExpanded);
            }
            else
                fwrite(pVar->value, 1, pVar->value_length, pFile);
        }
        else
#endif
            fwrite(psz, 1, cch, pFile);
        fFirst = 0;
    }

    /*
     * Add the newline and close the file.
     */
    if (    fputc('\n', pFile) == EOF
        ||  ferror(pFile))
    {
        fclose(pFile);
        return errx(1, "error writing to '%s'!", argv[1]);
    }
    if (fclose(pFile))
        return err(1, "failed to fclose '%s'!", argv[1]);
    return 0;
}

