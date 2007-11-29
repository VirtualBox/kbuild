/* $Id: kmk_redirect.c 1271 2007-11-29 20:36:18Z knut.osmundsen@oracle.com $ */
/** @file
 *
 * kmk_redirect - Do simple program <-> file redirection.
 *
 * Copyright (c) 2007 knut st. osmundsen <bird-kBuild-spam@anduin.net>
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


/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#if defined(_MSC_VER)
# include <io.h>
# include <direct.h>
# include <process.h>
#else
# include <unistd.h>
#endif



static int usage(FILE *pOut,  const char *argv0)
{
    fprintf(pOut,
            "usage: %s [-[rwa+tb]<fd> <file>] -- <program> [args]\n"
            "   or: %s --help\n"
            "   or: %s --version\n"
            "\n"
            "The rwa+tb is like for fopen, if not specified it defaults to w+.\n"
            "The <fd> is either a number or an alias for the standard handles;\n" 
            "i - stdin, o - stdout, e - stderr.\n"
            ,
            argv0, argv0, argv0);
    return 1;
}


int main(int argc, char **argv, char **envp)
{
    int i;
    FILE *pStdErr = stderr;
    FILE *pStdOut = stdout;


    /*
     * Parse arguments.
     */
    if (argc <= 1)
        return usage(pStdErr, argv[0]);
    for (i = 1; i < argc; i++)
    {
        if (argv[i][0] == '-')
        {
            int fd;
            int fdOpened;
            int fOpen;
            char *psz = &argv[i][1];
            if (*psz == '-')
            {
                /* '--' ? */
                if (!psz[1])
                {
                    i++;
                    break;
                }

                /* convert to short. */
                if (!strcmp(psz, "-help"))
                    psz = "h";
                else if (!strcmp(psz, "-version"))
                    psz = "V";
            }

            /*
             * Deal with the obligatory help and version switches first.
             */
            if (*psz == 'h')
            {
                usage(pStdOut, argv[0]);
                return 0;
            }
            if (*psz == 'V')
            {
                printf("kmk_redirect - kBuild version %d.%d.%d\n"
                       "Copyright (C) 2007 Knut St. Osmundsen\n",
                       KBUILD_VERSION_MAJOR, KBUILD_VERSION_MINOR, KBUILD_VERSION_PATCH);
                return 0;
            }

            /* 
             * Parse a file descriptor argument.
             */

            /* mode */
            switch (*psz)
            {
                case 'r':
                    psz++;
                    if (*psz == '+')
                    {
                        fOpen = O_RDWR;
                        psz++;
                    }
                    else
                        fOpen = O_RDONLY;
                    break;

                case 'w':
                    psz++;
                    if (*psz == '+')
                    {
                        psz++;
                        fOpen = O_RDWR | O_CREAT | O_TRUNC;
                    }
                    else
                        fOpen = O_WRONLY | O_CREAT | O_TRUNC;
                    break;

                case 'a':
                    psz++;
                    if (*psz == '+')
                    {
                        psz++;
                        fOpen = O_RDWR | O_CREAT | O_APPEND;
                    }
                    else
                        fOpen = O_WRONLY | O_CREAT | O_APPEND;
                    break;

                case '+':
                    fprintf(pStdErr, "%s: syntax error: Unexpected '+' in '%s'\n", argv[0], argv[i]);
                    return 1;

                default:
                    fOpen = O_RDWR | O_CREAT | O_TRUNC;
                    break;
            }

            /* binary / text modifiers */
            switch (*psz)
            {
                case 'b':
#ifdef O_BINARY
                    fOpen |= O_BINARY;
#endif
                    psz++;
                    break;

                case 't':
#ifdef O_TEXT
                    fOpen |= O_TEXT;
#endif 
                    psz++;
                    break;

                default:
                    fOpen |= O_BINARY;
                    break;

            }

            /* convert to file descriptor number */
            switch (*psz)
            {
                case 'o':
                    fd = 1;
                    psz++;
                    break;
                        
                case 'e':
                    fd = 2;
                    psz++;
                    break;
    
                case '0':
                    if (!psz[1])
                    {
                        fd = 0;
                        psz++;
                        break;
                    }
                case '1':
                case '2':
                case '3':
                case '4':
                case '5':
                case '6':
                case '7':
                case '8':
                case '9':
                    fd = (int)strtol(psz, &psz, 0);
                    if (!fd)
                    {
                        fprintf(pStdErr, "%s: error: failed to convert '%s' to a number\n", argv[0], argv[i]);
                        return 1;
                        
                    }
                    if (fd < 0)
                    {
                        fprintf(pStdErr, "%s: error: negative fd %d (%s)\n", argv[0], fd, argv[i]);
                        return 1;
                    }
                    break;

                /*
                 * Invalid argument.
                 */
                default:
                    fprintf(pStdErr, "%s: error: failed to convert '%s' ('%s') to a file descriptor\n", argv[0], psz, argv[i]);
                    return 1;
            }

            /*
             * Check for the filename.
             */
            if (*psz)
            {
                fprintf(pStdErr, "%s: syntax error: characters following the file descriptor: '%s' ('%s')\n", argv[0], psz, argv[i]);
                return 1;
            }
            i++;
            if (i >= argc )
            {
                fprintf(pStdErr, "%s: syntax error: missing filename argument.\n", argv[0]);
                return 1;
            }
            psz = argv[i];

            /*
             * Setup the redirection.
             */
            if (fd == fileno(pStdErr))
            {
                /* 
                 * Move stderr to a new location, making it close on exec.
                 * If pStdOut has already teamed up with pStdErr, update it too.
                 */
                FILE *pNew;
                fdOpened = dup(fileno(pStdErr));
                if (fdOpened == -1)
                {
                    fprintf(pStdErr, "%s: error: failed to dup stderr (%d): %s\n", argv[0], fileno(pStdErr), strerror(errno));
                    return 1;
                }
#ifdef _MSC_VER
                /** @todo figure out how to make the handle close-on-exec. We'll simply close it for now. */
#else
                if (fcntl(channel, F_SETFD, FD_CLOEXEC) == -1)
                {
                    fprintf(pStdErr, "%s: error: failed to make stderr (%d) close-on-exec: %s\n", argv[0], fdOpened, strerror(errno));
                    return 1;
                }
#endif 

                pNew = fdopen(fdOpened, "w");
                if (!pNew)
                {
                    fprintf(pStdErr, "%s: error: failed to fdopen the new stderr (%d): %s\n", argv[0], fdOpened, strerror(errno));
                    return 1;
                }
                if (pStdOut == pStdErr)
                    pStdOut = pNew;
                pStdErr = pNew;
            }
            else if (fd == 1 && pStdOut != pStdErr)
                pStdOut = pStdErr;

            /*
             * Close and open the new file descriptor.
             */
            close(fd);
#if defined(_MSC_VER)
            if (!strcmp(psz, "/dev/null"))
                psz = (char *)"nul";
#endif
            fdOpened = open(psz, fOpen, 0666);
            if (fdOpened == -1)
            {
                fprintf(pStdErr, "%s: error: failed to open '%s' as %d: %s\n", argv[0], psz, fd, strerror(errno));
                return 1;
            }
            if (fdOpened != fd)
            {
                /* move it (dup2 returns 0 on MSC). */
                if (dup2(fdOpened, fd) == -1)
                {
                    fprintf(pStdErr, "%s: error: failed to dup '%s' as %d: %s\n", argv[0], psz, fd, strerror(errno));
                    return 1;
                }
                close(fdOpened);
            }
        }
        else
        {
            fprintf(pStdErr, "%s: syntax error: Invalid argument '%s'.\n", argv[0], argv[i]);
            return usage(pStdErr, argv[0]);
        }
    }

    /*
     * Make sure there's something to execute.
     */
    if (i >= argc)
    {
        fprintf(pStdErr, "%s: syntax error: nothing to execute!\n", argv[0]);
        return usage(pStdErr, argv[0]);
    }

#if 0/** @todo defined(_MSC_VER)
    / * 
     * We'll have to find the '--' in the commandline and pass that 
     * on to CreateProcess or spawn. Otherwise, the argument qouting 
     * is gonna be messed up.
     */
#else
# if defined(_MSC_VER) /* tmp hack. */
    if (fileno(pStdErr) != 2)
    {
        fclose(pStdErr);
        execv(argv[i], &argv[i]);
        return 1;
    }
# endif
    execv(argv[i], &argv[i]);
    fprintf(pStdErr, "%s: error: execv(%s,..) failed: %s\n", argv[0], argv[i], strerror(errno));
#endif
    return 1;
}

