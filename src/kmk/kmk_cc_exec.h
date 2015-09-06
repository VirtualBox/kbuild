/* $Id: kmk_cc_exec.h 2788 2015-09-06 15:43:10Z knut.osmundsen@oracle.com $ */
/** @file
 * kmk_cc - Make "Compiler".
 */

/*
 * Copyright (c) 2015 knut st. osmundsen <bird-kBuild-spamx@anduin.net>
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

#ifndef ___kmk_cc_and_exech
#define ___kmk_cc_and_exech
#ifdef CONFIG_WITH_COMPILER

#include <stdio.h>


void  kmk_cc_init(void);
void  kmk_cc_print_stats(void);

struct variable;
extern struct kmk_cc_expandprog *kmk_cc_compile_variable_for_expand(struct variable *pVar);
extern struct kmk_cc_evalprog   *kmk_cc_compile_variable_for_eval(struct variable *pVar);
extern struct kmk_cc_evalprog   *kmk_cc_compile_file_for_eval(FILE *pFile, const char *pszFilename);
extern char *kmk_exec_expand_to_var_buf(struct variable *pVar, char *pchDst);
extern void kmk_exec_eval_file(struct kmk_cc_evalprog *pProg);
extern void kmk_exec_eval_variable(struct variable *pVar);
extern void kmk_cc_variable_changed(struct variable *pVar);
extern void kmk_cc_variable_deleted(struct variable *pVar);


#endif /* CONFIG_WITH_COMPILER */
#endif
