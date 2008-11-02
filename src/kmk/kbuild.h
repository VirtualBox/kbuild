/* $Id: kbuild.h 2019 2008-11-02 00:21:05Z knut.osmundsen@oracle.com $ */
/** @file
 * kBuild specific make functionality.
 */

/*
 * Copyright (c) 2006-2008 knut st. osmundsen <bird-src-spam@anduin.net>
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

#ifndef ___kBuild_h
#define ___kBuild_h

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

