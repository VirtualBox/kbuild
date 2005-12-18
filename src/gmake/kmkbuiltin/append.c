/* $Id: append.c 370 2005-12-18 03:48:02Z knut.osmundsen@oracle.com $ */
/** @file
 *
 * kMk Builtin command - append text to file.
 *
 * Copyright (c) 2005 knut st. osmundsen <bird@anduin.net>
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

/**
 * Appends text to a textfile, creating the textfile if necessary.
 */
int kmk_builtin_append(int argc, char **argv, char **envp)
{
    int i;
    FILE *pFile;

    g_progname = argv[0];

    /*
     * Open the output file.
     */
    if (argc <= 1)
    {
        errx(1, "missing filename!");
        fprintf(stderr, "usage: append file [string ...]\n");
        return 1;
    }
    pFile = fopen(argv[1], "a");
    if (!pFile)
        return err(1, "failed to open '%s'.", argv[1]);

    /*
     * Append the argument strings to the file
     */
    for (i = 2; i < argc; i++)
    {
        const char *psz = argv[i];
        size_t cch = strlen(psz);
        if (i > 2)
            fputc(' ', pFile);
        fwrite(psz, 1, cch, pFile);
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

