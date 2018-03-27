/* $Id: kmkbuiltin.h 3195 2018-03-27 18:09:23Z knut.osmundsen@oracle.com $ */
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
#include <fcntl.h>

/* For the GNU/hurd weirdo. */
#ifndef PATH_MAX
# ifdef MAXPATHLEN
#  define PATH_MAX  MAXPATHLEN
# else
#  define PATH_MAX  4096
# endif
#endif
#ifndef MAXPATHLEN
# define MAXPATHLEN PATH_MAX
#endif

/** This is for telling fopen() to get a close-on-exec handle.
 * @todo glibc 2.7+ and recent cygwin supports 'e' for doing this. */
#ifndef KMK_FOPEN_NO_INHERIT_MODE
# ifdef _MSC_VER
#  define KMK_FOPEN_NO_INHERIT_MODE "N"
# else
#  define KMK_FOPEN_NO_INHERIT_MODE ""
# endif
#endif

/** This is for telling open() to open to return a close-on-exec descriptor. */
#ifdef _O_NOINHERIT
# define KMK_OPEN_NO_INHERIT        _O_NOINHERIT
#elif defined(O_NOINHERIT)
# define KMK_OPEN_NO_INHERIT        O_NOINHERIT
#elif defined(O_CLOEXEC)
# define KMK_OPEN_NO_INHERIT        O_CLOEXEC
#else
# define KMK_OPEN_NO_INHERIT        0
#endif


#include "kbuild_version.h"
#ifndef KMK_BUILTIN_STANDALONE
# include "../output.h"
#endif

struct child;
int kmk_builtin_command(const char *pszCmd, struct child *pChild, char ***ppapszArgvToSpawn, pid_t *pPidSpawned);
int kmk_builtin_command_parsed(int argc, char **argv, struct child *pChild, char ***ppapszArgvToSpawn, pid_t *pPidSpawned);


/**
 * KMK built-in command execution context.
 */
typedef struct KMKBUILTINCTX
{
    /** The program name to use in error messages. */
    const char *pszProgName;
    /** The KMK output synchronizer.   */
    struct output *pOut;
#if defined(KBUILD_OS_WINDOWS) && !defined(KMK_BUILTIN_STANDALONE)
    /** Pointer to the worker thread, if we're on one. */
    void *pvWorker;
#endif
} KMKBUILTINCTX;
/** Pointer to kmk built-in command execution context. */
typedef KMKBUILTINCTX *PKMKBUILTINCTX;

/**
 * kmk built-in command entry.
 */
typedef struct KMKBUILTINENTRY
{
    union
    {
        struct
        {
            char    cch;
            char    sz[15];
        } s;
        size_t      cchAndStart;
    } uName;
    union
    {
        uintptr_t uPfn;
#define FN_SIG_MAIN             0
        int (* pfnMain)(int argc, char **argv, char **envp, PKMKBUILTINCTX pCtx);
#define FN_SIG_MAIN_SPAWNS      1
        int (* pfnMainSpawns)(int argc, char **argv, char **envp, PKMKBUILTINCTX pCtx, struct child *pChild, pid_t *pPid);
#define FN_SIG_MAIN_TO_SPAWN    2
        int (* pfnMainToSpawn)(int argc, char **argv, char **envp, PKMKBUILTINCTX pCtx, char ***ppapszArgvToSpawn);
    } u;
    size_t      uFnSignature : 8;
    size_t      fMtSafe : 1;            /**< Safe for multi threaded execution. */
    size_t      fNeedEnv : 1;           /**< Needs the (target) enviornment. */
} KMKBUILTINENTRY;
/** Pointer to kmk built-in command entry. */
typedef KMKBUILTINENTRY const *PCKMKBUILTINENTRY;

extern int kmk_builtin_append(int argc, char **argv, char **envp, PKMKBUILTINCTX pCtx, struct child *pChild, pid_t *pPidSpawned);
extern int kmk_builtin_cp(int argc, char **argv, char **envp, PKMKBUILTINCTX pCtx);
extern int kmk_builtin_cat(int argc, char **argv, char **envp, PKMKBUILTINCTX pCtx);
extern int kmk_builtin_chmod(int argc, char **argv, char **envp, PKMKBUILTINCTX pCtx);
extern int kmk_builtin_cmp(int argc, char **argv, char **envp, PKMKBUILTINCTX pCtx);
extern int kmk_builtin_dircache(int argc, char **argv, char **envp, PKMKBUILTINCTX pCtx);
extern int kmk_builtin_echo(int argc, char **argv, char **envp, PKMKBUILTINCTX pCtx);
extern int kmk_builtin_expr(int argc, char **argv, char **envp, PKMKBUILTINCTX pCtx);
extern int kmk_builtin_install(int argc, char **argv, char **envp, PKMKBUILTINCTX pCtx);
extern int kmk_builtin_ln(int argc, char **argv, char **envp, PKMKBUILTINCTX pCtx);
extern int kmk_builtin_md5sum(int argc, char **argv, char **envp, PKMKBUILTINCTX pCtx);
extern int kmk_builtin_mkdir(int argc, char **argv, char **envp, PKMKBUILTINCTX pCtx);
extern int kmk_builtin_mv(int argc, char **argv, char **envp, PKMKBUILTINCTX pCtx);
extern int kmk_builtin_printf(int argc, char **argv, char **envp, PKMKBUILTINCTX pCtx);
extern int kmk_builtin_redirect(int argc, char **argv, char **envp, PKMKBUILTINCTX pCtx, struct child *pChild, pid_t *pPidSpawned);
extern int kmk_builtin_rm(int argc, char **argv, char **envp, PKMKBUILTINCTX pCtx);
extern int kmk_builtin_rmdir(int argc, char **argv, char **envp, PKMKBUILTINCTX pCtx);
extern int kmk_builtin_sleep(int argc, char **argv, char **envp, PKMKBUILTINCTX pCtx);
extern int kmk_builtin_test(int argc, char **argv, char **envp, PKMKBUILTINCTX pCtx, char ***ppapszArgvSpawn);
extern int kmk_builtin_touch(int argc, char **argv, char **envp, PKMKBUILTINCTX pCtx);
#ifdef KBUILD_OS_WINDOWS
extern int kmk_builtin_kSubmit(int argc, char **argv, char **envp, PKMKBUILTINCTX pCtx, struct child *pChild, pid_t *pPidSpawned);
extern int kSubmitSubProcGetResult(intptr_t pvUser, int *prcExit, int *piSigNo);
extern int kSubmitSubProcKill(intptr_t pvUser, int iSignal);
extern void kSubmitSubProcCleanup(intptr_t pvUser);
#endif
extern int kmk_builtin_kDepIDB(int argc, char **argv, char **envp, PKMKBUILTINCTX pCtx);
extern int kmk_builtin_kDepObj(int argc, char **argv, char **envp, PKMKBUILTINCTX pCtx);

extern char *kmk_builtin_func_printf(char *o, char **argv, const char *funcname);

/* common-env-and-cwd-opt.c: */
extern int kBuiltinOptEnvSet(PKMKBUILTINCTX pCtx, char ***ppapszEnv, unsigned *pcEnvVars, unsigned *pcAllocatedEnvVars,
                             int cVerbosity, const char *pszValue);
extern int kBuiltinOptEnvAppend(PKMKBUILTINCTX pCtx, char ***ppapszEnv, unsigned *pcEnvVars, unsigned *pcAllocatedEnvVars,
                                int cVerbosity, const char *pszValue);
extern int kBuiltinOptEnvPrepend(PKMKBUILTINCTX pCtx, char ***ppapszEnv, unsigned *pcEnvVars, unsigned *pcAllocatedEnvVars,
                                 int cVerbosity, const char *pszValue);
extern int kBuiltinOptEnvUnset(PKMKBUILTINCTX pCtx, char ***ppapszEnv, unsigned *pcEnvVars, unsigned *pcAllocatedEnvVars,
                               int cVerbosity, const char *pszVarToRemove);
extern int kBuiltinOptEnvZap(PKMKBUILTINCTX pCtx, char ***ppapszEnv, unsigned *pcEnvVars, unsigned *pcAllocatedEnvVars,
                             int cVerbosity);
extern void kBuiltinOptEnvCleanup(char ***ppapszEnv, unsigned cEnvVars, unsigned *pcAllocatedEnvVars);
extern int kBuiltinOptChDir(PKMKBUILTINCTX pCtx, char *pszCwd, size_t cbCwdBuf, const char *pszValue);

#ifdef CONFIG_WITH_KMK_BUILTIN_STATS
extern void kmk_builtin_print_stats(FILE *pOutput, const char *pszPrefix);
#endif

#endif

