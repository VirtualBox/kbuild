/* $Id: mainmain.c 48 2003-04-05 03:22:01Z knut.osmundsen@oracle.com $
 *
 * The real mail entry point for kMk.
 *
 * Copyright (c) 2003 knut st. osmundsen <bird@anduin.net>
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
#include <kLib/kString.h>
#include <kLib/kPath.h>


/*******************************************************************************
*   External Functions                                                         *
*******************************************************************************/
extern main_kMk(int argc, char **argv);
extern main_kShell(int argc, char **argv);
extern main_kDepend(int argc, char **argv);


/**
 * The real main of the merged tool suite (all in one).
 *
 * There is two ways the tool can be made work different, either thru
 * it's name or thru the first argument.
 */
int main(int argc, char **argv)
{
    int     rc = 16;
    char *  psz;

    /* @todo Debug build must register exception handler here. */

    /*
     * Which tool is this?
     * Give argument preference and check the first.
     */
    if (argc > 1 && !kStrCmp(argv[1], "--kShell"))
        rc = main_kShell(argc, argv);
    #if 0
    else if (argc > 1 && !kStrCmp(argv[1], "--kDepend"))
        rc = main_kDepend(argc, argv);
    #endif
    else if (argc > 1 && !kStrCmp(argv[1], "--kMk"))
        rc = main_kDepend(argc, argv);
    else
    {
        /* check name */
        psz = kPathName(argv[0], NULL, 0);
        if (!kStrNICmp(psz, "kShell", 6) && (!psz[6] || psz[6] == '.'))
            rc = main_kShell(argc, argv);
        #if 0
        else if (!kStrNICmp(psz, "kDepend", 7) && (!psz[7] || psz[7] == '.'))
            rc = main_kDepend(argc, argv);
        #endif
        else /* kMk is default */
            rc = main_kMk(argc, argv);
    }

    /* @todo Debug build must unregister exception handler here. */

    return rc;
}
