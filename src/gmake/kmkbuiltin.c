/* $Id: kmkbuiltin.c 225 2005-02-09 08:31:14Z knut.osmundsen@oracle.com $ */
/** @file
 *
 * kMk Builtin command execution.
 *
 * Copyright (c) 2005 knut st. osmundsen <bird@innotek.de>
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
#include "kmkbuiltin.h"

int kmk_builtin_cp(char **argv)
{
    return 1;
}

int kmk_builtin_command(char **argv)
{
    const char *pszCmd = argv[0];
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
    if (!strcmp(pszCmd, "cp"))
        rc = kmk_builtin_cp(argv);
    //else if (!strcmp(pszCmd, "chmod"))
    //    rc = kmk_builtin_chmod(argv);
    //else if (!strcmp(pszCmd, "mkdir"))
    //    rc = kmk_builtin_mkdir(argv);
    //else if (!strcmp(pszCmd, "mv"))
    //    rc = kmk_builtin_mv(argv);
    //else if (!strcmp(pszCmd, "rm"))
    //    rc = kmk_builtin_rm(argv);
    //else if (!strcmp(pszCmd, "rmdir"))
    //    rc = kmk_builtin_rmdir(argv);
    else
    {
        printf("kmk_builtin: Unknown command '%s'!\n", pszCmd);
        return 1;
    }
    return rc;
}

