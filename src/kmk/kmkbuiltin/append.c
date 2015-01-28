/* $Id: append.c 2757 2015-01-28 13:02:35Z knut.osmundsen@oracle.com $ */
/** @file
 * kMk Builtin command - append text to file.
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
#ifndef kmk_builtin_append
# include "make.h"
# include "filedef.h"
# include "variable.h"
#else
# include "config.h"
#endif
#include <string.h>
#include <stdio.h>
#ifdef HAVE_ALLOCA_H
# include <alloca.h>
#endif
#include "err.h"
#include "kmkbuiltin.h"


/**
 * Prints the usage and return 1.
 */
static int usage(FILE *pf)
{
    fprintf(pf,
            "usage: %s [-dcnNtv] file [string ...]\n"
            "   or: %s --version\n"
            "   or: %s --help\n"
            "\n"
            "Options:\n"
            "  -d  Enclose the output in define ... endef, taking the name from\n"
            "      the first argument following the file name.\n"
            "  -c  Output the command for specified target(s). [builtin only]\n"
            "  -n  Insert a newline between the strings.\n"
            "  -N  Suppress the trailing newline.\n"
            "  -t  Truncate the file instead of appending\n"
            "  -v  Output the value(s) for specified variable(s). [builtin only]\n"
            ,
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
    int iFile;
    FILE *pFile;
    int fNewline = 0;
    int fNoTrailingNewline = 0;
    int fTruncate = 0;
    int fDefine = 0;
    int fVariables = 0;
    int fCommands = 0;

    g_progname = argv[0];

    /*
     * Parse options.
     */
    i = 1;
    while (i < argc
       &&  argv[i][0] == '-'
       &&  argv[i][1] != '\0' /* '-' is a file */
       &&  strchr("-cdnNtv", argv[i][1]) /* valid option char */
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
                        if (fVariables)
                        {
                            errx(1, "Option '-c' clashes with '-v'.");
                            return usage(stderr);
                        }
#ifndef kmk_builtin_append
                        fCommands = 1;
                        break;
#else
                        errx(1, "Option '-c' isn't supported in external mode.");
                        return usage(stderr);
#endif
                    case 'd':
                        if (fVariables)
                        {
                            errx(1, "Option '-d' must come before '-v'!");
                            return usage(stderr);
                        }
                        fDefine = 1;
                        break;
                    case 'n':
                        fNewline = 1;
                        break;
                    case 'N':
                        fNoTrailingNewline = 1;
                        break;
                    case 't':
                        fTruncate = 1;
                        break;
                    case 'v':
                        if (fCommands)
                        {
                            errx(1, "Option '-v' clashes with '-c'.");
                            return usage(stderr);
                        }
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

    if (i + fDefine >= argc)
    {
        if (i <= argc)
            errx(1, "missing filename!");
        else
            errx(1, "missing define name!");
        return usage(stderr);
    }

    /*
     * Open the output file.
     */
    iFile = i;
    pFile = fopen(argv[i], fTruncate ? "w" : "a");
    if (!pFile)
        return err(1, "failed to open '%s'", argv[i]);

    /*
     * Start define?
     */
    if (fDefine)
    {
        i++;
        fprintf(pFile, "define %s\n", argv[i]);
    }

    /*
     * Append the argument strings to the file
     */
    fFirst = 1;
    for (i++; i < argc; i++)
    {
        const char *psz = argv[i];
        size_t cch = strlen(psz);
        if (!fFirst)
            fputc(fNewline ? '\n' : ' ', pFile);
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
     * End the define?
     */
    if (fDefine)
    {
        if (fFirst)
            fwrite("\nendef", 1, sizeof("\nendef") - 1, pFile);
        else
            fwrite("endef", 1, sizeof("endef") - 1, pFile);
    }

    /*
     * Add the final newline (unless supressed) and close the file.
     */
    if (    (   !fNoTrailingNewline
             && fputc('\n', pFile) == EOF)
        ||  ferror(pFile))
    {
        fclose(pFile);
        return errx(1, "error writing to '%s'!", argv[iFile]);
    }
    if (fclose(pFile))
        return err(1, "failed to fclose '%s'!", argv[iFile]);
    return 0;
}

