/* $Id: gdllshell.c 199 2004-12-17 04:51:58Z knut.osmundsen@oracle.com $ */
/** @file
 *
 * dllshell://reference implementation.
 *
 * Copyright (c) 2003 knut st. osmundsen <bird-srcspam@anduin.net>
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
#include <process.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>


/**
 * Asynchronusly execute a command. The argument argv and envp together
 * with the current filehandle configuration is what the command should
 * should work within.
 *
 * @returns pid or fake pid of the command.
 *          Fake pid means a pid number out of the normal pid range, but
 *          still unique for sometime.
 * @param   argv    Argument vector.
 * @param   envp    Environment vector.
 * @param   status  Where to store the status.
 * @param   done    Where to flag that the command is completed in one
 *                  or another manner.
 */
pid_t _System spawn_command(char **argv, char **envp, int *status, char *done)
{
    printf("gdllshell: hello\n");
    return 0;
}
