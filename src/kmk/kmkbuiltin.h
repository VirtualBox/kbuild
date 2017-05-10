/* $Id: kmkbuiltin.h 3039 2017-05-10 10:55:51Z knut.osmundsen@oracle.com $ */
/** @file
 * kMk Builtin command handling.
 */

/*
 * Copyright (c) 2005-2016 knut st. osmundsen <bird-kBuild-spamx@anduin.net>
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

#ifndef ___kmk_kmkbuiltin_h___
#define ___kmk_kmkbuiltin_h___

#ifdef _MSC_VER
# ifndef pid_t /* see config.h.win */
#  define pid_t intptr_t /* Note! sub_proc.c needs it to be pointer sized. */
# endif
#else
# include <sys/types.h>
#endif

#include "kbuild_version.h"

struct child;
int kmk_builtin_command(const char *pszCmd, struct child *pChild, char ***ppapszArgvToSpawn, pid_t *pPidSpawned);
int kmk_builtin_command_parsed(int argc, char **argv, struct child *pChild, char ***ppapszArgvToSpawn, pid_t *pPidSpawned);

extern int kmk_builtin_append(int argc, char **argv, char **envp);
extern int kmk_builtin_cp(int argc, char **argv, char **envp);
extern int kmk_builtin_cat(int argc, char **argv, char **envp);
extern int kmk_builtin_chmod(int argc, char **argv, char **envp);
extern int kmk_builtin_cmp(int argc, char **argv, char **envp);
extern int kmk_builtin_dircache(int argc, char **argv, char **envp);
extern int kmk_builtin_echo(int argc, char **argv, char **envp);
extern int kmk_builtin_expr(int argc, char **argv, char **envp);
extern int kmk_builtin_install(int argc, char **argv, char **envp);
extern int kmk_builtin_ln(int argc, char **argv, char **envp);
extern int kmk_builtin_md5sum(int argc, char **argv, char **envp);
extern int kmk_builtin_mkdir(int argc, char **argv, char **envp);
extern int kmk_builtin_mv(int argc, char **argv, char **envp);
extern int kmk_builtin_printf(int argc, char **argv, char **envp);
extern int kmk_builtin_redirect(int argc, char **argv, char **envp, struct child *pChild, pid_t *pPidSpawned);
extern int kmk_builtin_rm(int argc, char **argv, char **envp);
extern int kmk_builtin_rmdir(int argc, char **argv, char **envp);
extern int kmk_builtin_sleep(int argc, char **argv, char **envp);
extern int kmk_builtin_test(int argc, char **argv, char **envp
#ifndef kmk_builtin_test
                            , char ***ppapszArgvSpawn
#endif
                            );
#ifdef KBUILD_OS_WINDOWS
extern int kmk_builtin_kSubmit(int argc, char **argv, char **envp, struct child *pChild, pid_t *pPidSpawned);
extern int kSubmitSubProcGetResult(intptr_t pvUser, int *prcExit, int *piSigNo);
extern int kSubmitSubProcKill(intptr_t pvUser, int iSignal);
extern void kSubmitSubProcCleanup(intptr_t pvUser);
#endif
extern int kmk_builtin_kDepIDB(int argc, char **argv, char **envp);
extern int kmk_builtin_kDepObj(int argc, char **argv, char **envp);

extern char *kmk_builtin_func_printf(char *o, char **argv, const char *funcname);

/* common-env-and-cwd-opt.c: */
extern int kBuiltinOptEnvSet(char ***ppapszEnv, unsigned *pcEnvVars, unsigned *pcAllocatedEnvVars,
                             int cVerbosity, const char *pszValue);
extern int kBuiltinOptEnvAppend(char ***ppapszEnv, unsigned *pcEnvVars, unsigned *pcAllocatedEnvVars,
                                int cVerbosity, const char *pszValue);
extern int kBuiltinOptEnvPrepend(char ***ppapszEnv, unsigned *pcEnvVars, unsigned *pcAllocatedEnvVars,
                                 int cVerbosity, const char *pszValue);
extern int kBuiltinOptEnvUnset(char **papszEnv, unsigned *pcEnvVars, int cVerbosity, const char *pszVarToRemove);
extern int kBuiltinOptChDir(char *pszCwd, size_t cbCwdBuf, const char *pszValue);

#endif

