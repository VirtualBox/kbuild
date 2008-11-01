/* $Id: kbuild.h 2011 2008-11-01 14:15:50Z knut.osmundsen@oracle.com $ */
/** @file
 *
 * kBuild specific make functionality.
 *
 * Copyright (c) 2006-2007 knut st. osmundsen <bird-kBuild-spam@anduin.net>
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

#ifndef __kBuild_h__
#define __kBuild_h__

char *func_kbuild_source_tool(char *o, char **argv, const char *pszFuncName);
char *func_kbuild_object_base(char *o, char **argv, const char *pszFuncName);
char *func_kbuild_object_suffix(char *o, char **argv, const char *pszFuncName);
char *func_kbuild_source_prop(char *o, char **argv, const char *pszFuncName);
char *func_kbuild_source_one(char *o, char **argv, const char *pszFuncName);
char *func_kbuild_expand_template(char *o, char **argv, const char *pszFuncName);

void init_kbuild(int argc, char **argv);
const char *get_kbuild_path(void);
const char *get_kbuild_bin_path(void);
const char *get_default_kbuild_shell(void);

#endif
