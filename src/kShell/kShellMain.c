/* $Id: kShellMain.c 19 2002-10-17 23:02:18Z knut.osmundsen@oracle.com $
 *
 * kShell - Mainprogram (intented for testing)
 *
 * Copyright (c) 2002 knut st. osmundsen <bird@anduin.net>
 *
 *
 * This file is part of kBuild.
 *
 * kBuild is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * kBuild is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with kBuild; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */


/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
#include "kShell.h"
#include <string.h>


/**
 * Simple main program which will execute any arguments or
 * start in interactive mode if no parameters.
 */
int main(int argc, char **argv)
{
    static char szCmd[4096];
    int         argi;
    int         rc;

    /*
     * init the shell.
     */
    rc = kshellInit(1);
    if (rc)
        return rc;


    /*
     * If any arguments we'll execute them as a command.
     */
    if (argc > 1)
    {
        for (argi = 2, strcpy(&szCmd[0], argv[1]); argi < argc; argi++)
            strcat(strcat(&szCmd[0], " "), argv[argi]);
        rc = kshellExecute(szCmd);
    }

    /*
     * Interactive mode.
     */
    else
    {
        rc = kshellInteractive();
    }


    /*
     * Terminate the shell and exit.
     */
    kshellTerm();

    return rc;
}
