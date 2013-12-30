/* $Id: kbuild.h 2717 2013-12-30 00:58:43Z knut.osmundsen@oracle.com $ */
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

/** @name kBuild objects
 * @{ */
struct kbuild_eval_data;
struct kbuild_object;

extern struct kbuild_eval_data *g_pTopKbEvalData;


/** Special return value indicating variable name isn't an accessor. */
#define KOBJ_NOT_KBUILD_ACCESSOR    ( (struct kbuild_object *)~(size_t)0 )

/** Special lookup_kbuild_object_variable return value. */
#define VAR_NOT_KBUILD_ACCESSOR     ( (struct variable *)~(size_t)0 )

struct variable    *lookup_kbuild_object_variable_accessor(const char *pchName, size_t cchName);
int                 is_kbuild_object_variable_accessor(const char *pchName, size_t cchName);
struct variable    *try_define_kbuild_object_variable_via_accessor(const char *pszName, size_t cchName,
                                                                   const char *pszValue, size_t cchValue, int fDuplicateValue,
                                                                   enum variable_origin enmOrigin, int fRecursive,
                                                                   struct floc const *pFileLoc);
struct variable    *define_kbuild_object_variable_in_top_obj(const char *pszName, size_t cchName,
                                                             const char *pszValue, size_t cchValue, int fDuplicateValue,
                                                             enum variable_origin enmOrigin, int fRecursive,
                                                             struct floc const *pFileLoc);
struct variable    *kbuild_object_variable_pre_append(const char *pchName, size_t cchName,
                                                      const char *pchValue, size_t cchValue, int fSimpleValue,
                                                      enum variable_origin enmOrigin, int fAppend,
                                                      const struct floc *pFileLoc);
int                 eval_kbuild_read_hook(struct kbuild_eval_data **kdata, const struct floc *flocp,
                                          const char *word, size_t wlen, const char *line, const char *eos, int ignoring);
void                print_kbuild_data_base(void);
void                print_kbuild_define_stats(void);
void                init_kbuild_object(void);
/** @} */

#endif

