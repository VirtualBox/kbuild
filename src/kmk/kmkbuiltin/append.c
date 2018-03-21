/* $Id: append.c 3171 2018-03-21 13:26:36Z knut.osmundsen@oracle.com $ */
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
# include "makeint.h"
# include "filedef.h"
# include "variable.h"
#else
# include "config.h"
#endif
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif
#ifdef _MSC_VER
# include <io.h>
#endif
#ifdef HAVE_ALLOCA_H
# include <alloca.h>
#endif
#include "err.h"
#include "kmkbuiltin.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#define STR_TUPLE(a_sz)     a_sz, (sizeof(a_sz) - 1)

/** No-inherit open flag.   */
#ifdef _O_NOINHERIT
# define MY_O_NOINHERIT     _O_NOINHERIT
#elif defined(O_NOINHERIT)
# define MY_O_NOINHERIT     O_NOINHERIT
#elif defined(O_CLOEXEC)
# define MY_O_NOINHERIT     O_CLOEXEC
#else
# define MY_O_NOINHERIT     0
#endif

/** Binary mode open flag. */
#ifdef _O_BINARY
# define MY_O_BINARY        _O_BINARY
#elif defined(O_BINARY)
# define MY_O_BINARY        O_BINARY
#else
# define MY_O_BINARY        0
#endif


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Append output buffer.
 */
typedef struct KMKBUILTINAPPENDBUF
{
    /** Buffer pointer. */
    char   *pszBuf;
    /** The buffer allocation size. */
    size_t  cbBuf;
    /** The current buffer offset. */
    size_t  offBuf;
    /** Set if we ran out of memory. */
    int     fOutOfMemory;
} KMKBUILTINAPPENDBUF;


/**
 * Appends a substring to the output buffer.
 *
 * @param   pBuf        The output buffer.
 * @param   pch         The substring pointer.
 * @param   cch         The substring length.
 */
static void write_to_buf(KMKBUILTINAPPENDBUF *pBuf, const char *pch, size_t cch)
{
    size_t const offCur = pBuf->offBuf;
    size_t       offNew = offCur + cch;

    if (offNew >= pBuf->cbBuf)
    {
        size_t cbNew = offNew + 1 + 256;
        void  *pvNew;
        cbNew = (cbNew + 511) & ~(size_t)511;
        pvNew = realloc(pBuf->pszBuf, cbNew);
        if (pvNew)
            pBuf->pszBuf = (char *)pvNew;
        else
        {
            free(pBuf->pszBuf);
            pBuf->pszBuf = NULL;
            pBuf->cbBuf  = 0;
            pBuf->fOutOfMemory = 1;
            return;
        }
    }

    memcpy(&pBuf->pszBuf[offCur], pch, cch);
    pBuf->pszBuf[offNew] = '\0';
    pBuf->offBuf = offNew;
}

/**
 * Adds a string to the output buffer.
 *
 * @param   pBuf        The output buffer.
 * @param   psz         The string.
 */
static void string_to_buf(KMKBUILTINAPPENDBUF *pBuf, const char *psz)
{
    write_to_buf(pBuf, psz, strlen(psz));
}


/**
 * Prints the usage and return 1.
 */
static int kmk_builtin_append_usage(const char *arg0, FILE *pf)
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
            "  -i  look for --insert-command=trg and --insert-variable=var. [builtin only]\n"
            "  -n  Insert a newline between the strings.\n"
            "  -N  Suppress the trailing newline.\n"
            "  -t  Truncate the file instead of appending\n"
            "  -v  Output the value(s) for specified variable(s). [builtin only]\n"
            ,
            arg0, arg0, arg0);
    return 1;
}

/**
 * Appends text to a textfile, creating the textfile if necessary.
 */
int kmk_builtin_append(int argc, char **argv, char **envp)
{
    const char *pszFilename;
    int rc = 88;
    int i;
    int fFirst;
    int fNewline = 0;
    int fNoTrailingNewline = 0;
    int fTruncate = 0;
    int fDefine = 0;
    int fVariables = 0;
    int fCommands = 0;
#ifndef kmk_builtin_append
    int fLookForInserts = 0;
#endif
#if defined(KBUILD_OS_WINDOWS) || defined(KBUILD_OS_OS2)
    static const char s_szNewLine[] = "\r\n";
#else
    static const char s_szNewLine[] = "\n";
#endif
    KMKBUILTINAPPENDBUF OutBuf = { NULL, 0, 0, 0 };

    g_progname = argv[0];

    /*
     * Parse options.
     */
    i = 1;
    while (i < argc
       &&  argv[i][0] == '-'
       &&  argv[i][1] != '\0' /* '-' is a file */
       &&  strchr("-cdinNtv", argv[i][1]) /* valid option char */
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
                            return kmk_builtin_append_usage(argv[0], stderr);
                        }
#ifndef kmk_builtin_append
                        fCommands = 1;
                        break;
#else
                        errx(1, "Option '-c' isn't supported in external mode.");
                        return kmk_builtin_append_usage(argv[0], stderr);
#endif
                    case 'd':
                        if (fVariables)
                        {
                            errx(1, "Option '-d' must come before '-v'!");
                            return kmk_builtin_append_usage(argv[0], stderr);
                        }
                        fDefine = 1;
                        break;
                    case 'i':
                        if (fVariables || fCommands)
                        {
                            errx(1, fVariables ? "Option '-i' clashes with '-v'." : "Option '-i' clashes with '-c'.");
                            return kmk_builtin_append_usage(argv[0], stderr);
                        }
#ifndef kmk_builtin_append
                        fLookForInserts = 1;
                        break;
#else
                        errx(1, "Option '-C' isn't supported in external mode.");
                        return kmk_builtin_append_usage(argv[0], stderr);
#endif
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
                            return kmk_builtin_append_usage(argv[0], stderr);
                        }
#ifndef kmk_builtin_append
                        fVariables = 1;
                        break;
#else
                        errx(1, "Option '-v' isn't supported in external mode.");
                        return kmk_builtin_append_usage(argv[0], stderr);
#endif
                    default:
                        errx(1, "Invalid option '%c'! (%s)", *psz, argv[i]);
                        return kmk_builtin_append_usage(argv[0], stderr);
                }
            } while (*++psz);
        }
        else if (!strcmp(psz, "-help"))
        {
            kmk_builtin_append_usage(argv[0], stdout);
            return 0;
        }
        else if (!strcmp(psz, "-version"))
            return kbuild_version(argv[0]);
        else
            break;
        i++;
    }

    /*
     * Take down the filename.
     */
    if (i + fDefine < argc)
        pszFilename = argv[i++];
    else
    {
        if (i <= argc)
            errx(1, "missing filename!");
        else
            errx(1, "missing define name!");
        return kmk_builtin_append_usage(argv[0], stderr);
    }

    /* Start of no-return zone! */

    /*
     * Start define?
     */
    if (fDefine)
    {
        write_to_buf(&OutBuf, STR_TUPLE("define "));
        string_to_buf(&OutBuf, argv[i]);
        write_to_buf(&OutBuf, STR_TUPLE(s_szNewLine));
        i++;
    }

    /*
     * Append the argument strings to the file
     */
    fFirst = 1;
    for (; i < argc; i++)
    {
        const char *psz = argv[i];
        size_t cch = strlen(psz);
        if (!fFirst)
        {
            if (fNewline)
                write_to_buf(&OutBuf, STR_TUPLE(s_szNewLine));
            else
                write_to_buf(&OutBuf, STR_TUPLE(" "));
        }
#ifndef kmk_builtin_append
        if (fCommands)
        {
            char *pszOldBuf;
            unsigned cchOldBuf;
            char *pchEnd;

            install_variable_buffer(&pszOldBuf, &cchOldBuf);

            pchEnd = func_commands(variable_buffer, &argv[i], "commands");
            write_to_buf(&OutBuf, variable_buffer, pchEnd - variable_buffer);

            restore_variable_buffer(pszOldBuf, cchOldBuf);
        }
        else if (fVariables)
        {
            struct variable *pVar = lookup_variable(psz, cch);
            if (!pVar)
                continue;
            if (   !pVar->recursive
                || IS_VARIABLE_RECURSIVE_WITHOUT_DOLLAR(pVar))
                write_to_buf(&OutBuf, pVar->value, pVar->value_length);
            else
            {
                char *pszExpanded = allocated_variable_expand(pVar->value);
                string_to_buf(&OutBuf, pszExpanded);
                free(pszExpanded);
            }
        }
        else if (fLookForInserts && strncmp(psz, "--insert-command=", 17) == 0)
        {
            char *pszOldBuf;
            unsigned cchOldBuf;
            char *pchEnd;

            install_variable_buffer(&pszOldBuf, &cchOldBuf);

            psz += 17;
            pchEnd = func_commands(variable_buffer, (char **)&psz, "commands");
            write_to_buf(&OutBuf, variable_buffer, pchEnd - variable_buffer);

            restore_variable_buffer(pszOldBuf, cchOldBuf);
        }
        else if (fLookForInserts && strncmp(psz, "--insert-variable=", 18) == 0)
        {
            struct variable *pVar = lookup_variable(psz + 18, cch);
            if (!pVar)
                continue;
            if (   !pVar->recursive
                || IS_VARIABLE_RECURSIVE_WITHOUT_DOLLAR(pVar))
                write_to_buf(&OutBuf, pVar->value, pVar->value_length);
            else
            {
                char *pszExpanded = allocated_variable_expand(pVar->value);
                string_to_buf(&OutBuf, pszExpanded);
                free(pszExpanded);
            }
        }
        else
#endif
            write_to_buf(&OutBuf, psz, cch);
        fFirst = 0;
    }

    /*
     * End the define?
     */
    if (fDefine)
    {
        if (fFirst)
            write_to_buf(&OutBuf, STR_TUPLE(s_szNewLine));
        write_to_buf(&OutBuf, STR_TUPLE("endef"));
    }

    /*
     * Add final newline (unless supressed) and check for errors.
     */
    if (!fNoTrailingNewline)
        write_to_buf(&OutBuf, STR_TUPLE(s_szNewLine));

    /*
     * Write the buffer (unless we ran out of heap already).
     */
    if (!OutBuf.fOutOfMemory)
    {
        int fd = open(pszFilename,
                      fTruncate ? O_WRONLY | O_TRUNC  | O_CREAT | MY_O_NOINHERIT | MY_O_BINARY
                                : O_WRONLY | O_APPEND | O_CREAT | MY_O_NOINHERIT | MY_O_BINARY,
                      0666);
        if (fd >= 0)
        {
            ssize_t cbWritten = write(fd, OutBuf.pszBuf, OutBuf.offBuf);
            if (cbWritten == (ssize_t)OutBuf.offBuf)
                rc = 0;
            else
                rc = err(1, "error writing %lu bytes to '%s'", (unsigned long)OutBuf.offBuf, pszFilename);
            if (close(fd) < 0)
                rc = err(1, "error closing '%s'", pszFilename);
        }
        else
            rc = err(1, "failed to open '%s'", pszFilename);
        free(OutBuf.pszBuf);
    }
    else
        rc = errx(1, "out of memory!");
    return rc;
}

