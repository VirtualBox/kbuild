/* $Id: append.c 348 2005-12-11 05:13:13Z knut.osmundsen@oracle.com $ */
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
#include <errno.h>
#include "kmkbuiltin.h"

/**
 * Appends text to a textfile, creating the textfile if necessary.
 */
int kmk_builtin_append(int argc, char **argv, char **envp)
{
    int i;
    FILE *pFile;

    /*
     * Open the output file.
     */
    if (argc <= 1)
    {
        fprintf(stderr, "append: missing filename!\n");
        fprintf(stderr, "usage: append file [string ...]\n");
        return 1;
    }
    pFile = fopen(argv[1], "a");
    if (!pFile)
    {
        fprintf(stderr, "append: failed to open '%s': %s\n", argv[1], strerror(errno));
        return 1;
    }

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
        fprintf(stderr, "append: error writing to '%s'!\n", argv[1]);
        fclose(pFile);
        return 1;
    }
    if (fclose(pFile))
    {
        fprintf(stderr, "append: failed to fclose '%s': %s\n", argv[1], strerror(errno));
        return 1;
    }
    return 0;
}

