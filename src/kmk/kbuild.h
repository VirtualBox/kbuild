/* $Id: kbuild.h 2549 2011-11-09 01:22:04Z knut.osmundsen@oracle.com $ */
/** @file
 * kBuild specific make functionality.
 */

/*
 * Copyright (c) 2006-2010 knut st. osmundsen <bird-kBuild-spamx@anduin.net>
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

struct kbuild_eval_data;
extern struct kbuild_eval_data *g_pTopKbDef;
struct variable_set *get_top_kbuild_variable_set(void);
char *kbuild_prefix_variable(const char *pszName, unsigned int *pcchName);

int eval_kbuild_define(struct kbuild_eval_data **kdata, const struct floc *flocp,
                       const char *word, unsigned int wlen, const char *line, const char *eos, int ignoring);
int eval_kbuild_endef(struct kbuild_eval_data **kdata, const struct floc *flocp,
                      const char *word, unsigned int wlen, const char *line, const char *eos, int ignoring);

void print_kbuild_data_base(void);
void print_kbuild_define_stats(void);

#endif

