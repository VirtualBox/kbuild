/* $Id: kShell.h 19 2002-10-17 23:02:18Z knut.osmundsen@oracle.com $
 *
 * kShell - Public interface.
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

#ifndef __kShell_h__
#define __kShell_h__

/*
 * Return codes.
 */
#define KSHELL_ERROR_SYNTAX_ERROR           1700
#define KSHELL_ERROR_NOT_ENOUGHT_MEMORY     1708
#define KSHELL_ERROR_PROGRAM_NOT_FOUND      1742
#define KSHELL_ERROR_COMMAND_TOO_LONG       1743

/*
 * Functions.
 */
int     kshellInit(int fVerbose);
void    kshellTerm(void);
int     kshellExecute(const char *pszCmd);
int     kshellInteractive(void);


#endif
