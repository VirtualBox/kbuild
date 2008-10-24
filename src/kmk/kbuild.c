/* $Id: kbuild.c 1932 2008-10-24 20:22:22Z knut.osmundsen@oracle.com $ */
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

/* No GNU coding style here! */


/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
#include "make.h"
#include "filedef.h"
#include "variable.h"
#include "dep.h"
#include "debug.h"
#ifdef WINDOWS32
# include "pathstuff.h"
# include <Windows.h>
#endif
#if defined(__APPLE__)
# include <mach-o/dyld.h>
#endif

#include "kbuild.h"

#include <assert.h>


/*******************************************************************************
*   Defined Constants And Macros                                               *
*******************************************************************************/
/** Helper for passing a string constant to kbuild_get_variable_n. */
#define ST(strconst) strconst, sizeof(strconst) - 1

#if 1
# define my_memcpy(dst, src, len) \
    do { \
        if (len > 8) \
            memcpy(dst, src, len); \
        else \
            switch (len) \
            { \
                case 8: dst[7] = src[7]; \
                case 7: dst[6] = src[6]; \
                case 6: dst[5] = src[5]; \
                case 5: dst[4] = src[4]; \
                case 4: dst[3] = src[3]; \
                case 3: dst[2] = src[2]; \
                case 2: dst[1] = src[1]; \
                case 1: dst[0] = src[0]; \
                case 0: break; \
            } \
    } while (0)
#elif defined(__GNUC__)
# define my_memcpy __builtin_memcpy
#elif defined(_MSC_VER)
# pragma instrinic(memcpy)
# define my_memcpy memcpy
#endif


/*******************************************************************************
*   Global Variables                                                           *
*******************************************************************************/
/** The argv[0] passed to main. */
static const char *g_pszExeName;
/** The initial working directory. */
static char *g_pszInitialCwd;


/**
 * Initialize kBuild stuff.
 *
 * @param   argc    Number of arguments to main().
 * @param   argv    The main() argument vector.
 */
void init_kbuild(int argc, char **argv)
{
    int rc;
    PATH_VAR(szTmp);

    /*
     * Get the initial cwd for use in my_abspath.
     */
#ifdef WINDOWS32
    if (getcwd_fs(szTmp, GET_PATH_MAX) != 0)
#else
    if (getcwd(szTmp, GET_PATH_MAX) != 0)
#endif
        g_pszInitialCwd = xstrdup(szTmp);
    else
        fatal(NILF, _("getcwd failed"));

    /*
     * Determin the executable name.
     */
    rc = -1;
#if defined(__APPLE__)
    {
        const char *pszImageName = _dyld_get_image_name(0);
        if (pszImageName)
        {
            size_t cchImageName = strlen(pszImageName);
            if (cchImageName < GET_PATH_MAX)
            {
                memcpy(szTmp, pszImageName, cchImageName + 1);
                rc = 0;
            }
        }
    }

#elif defined(__FreeBSD__)
    rc = readlink("/proc/curproc/file", szTmp, GET_PATH_MAX - 1);
    if (rc < 0 || rc == GET_PATH_MAX - 1)
        rc = -1;
    else
        szTmp[rc] = '\0';

#elif defined(__gnu_linux__) /** @todo find proper define... */
    rc = readlink("/proc/self/exe", szTmp, GET_PATH_MAX - 1);
    if (rc < 0 || rc == GET_PATH_MAX - 1)
        rc = -1;
    else
        szTmp[rc] = '\0';

#elif defined(__OS2__)
    _execname(szTmp, GET_PATH_MAX);
    rc = 0;

#elif defined(__sun__)
    {
        char szTmp2[64];
        snprintf(szTmp2, sizeof(szTmp2), "/proc/%d/path/a.out", getpid());
        rc = readlink(szTmp2, szTmp, GET_PATH_MAX - 1);
        if (rc < 0 || rc == GET_PATH_MAX - 1)
            rc = -1;
        else
            szTmp[rc] = '\0';
    }

#elif defined(WINDOWS32)
    if (GetModuleFileName(GetModuleHandle(NULL), szTmp, GET_PATH_MAX))
        rc = 0;

#endif

#if !defined(__OS2__) && !defined(WINDOWS32)
    /* fallback, try use the path to locate the binary. */
    if (   rc < 0
        && access(argv[0], X_OK))
    {
        size_t cchArgv0 = strlen(argv[0]);
        const char *pszPath = getenv("PATH");
        char *pszCopy = xstrdup(pszPath ? pszPath : ".");
        char *psz = pszCopy;
        while (*psz)
        {
            size_t cch;
            char *pszEnd = strchr(psz, PATH_SEPARATOR_CHAR);
            if (!pszEnd)
                pszEnd = strchr(psz, '\0');
            cch = pszEnd - psz;
            if (cch + cchArgv0 + 2 <= GET_PATH_MAX)
            {
                memcpy(szTmp, psz, cch);
                szTmp[cch] = '/';
                memcpy(&szTmp[cch + 1], argv[0], cchArgv0 + 1);
                if (!access(szTmp, X_OK))
                {
                    rc = 0;
                    break;
                }
            }

            /* next */
            psz = pszEnd;
            while (*psz == PATH_SEPARATOR_CHAR)
               psz++;
        }
        free(pszCopy);
    }
#endif

    if (rc < 0)
        g_pszExeName = argv[0];
    else
        g_pszExeName = xstrdup(szTmp);

    (void)argc;
}


/**
 * Wrapper that ensures correct starting_directory.
 */
static char *my_abspath(const char *pszIn, char *pszOut)
{
    char *pszSaved, *pszRet;

    pszSaved = starting_directory;
    starting_directory = g_pszInitialCwd;
    pszRet = abspath(pszIn, pszOut);
    starting_directory = pszSaved;

    return pszRet;
}


/**
 * Determin the KBUILD_PATH value.
 *
 * @returns Pointer to static a buffer containing the value (consider it read-only).
 */
const char *get_kbuild_path(void)
{
    static const char *s_pszPath = NULL;
    if (!s_pszPath)
    {
        PATH_VAR(szTmpPath);
        const char *pszEnvVar = getenv("KBUILD_PATH");
        if (    !pszEnvVar
            ||  !my_abspath(pszEnvVar, szTmpPath))
        {
            const char *pszEnvVar = getenv("PATH_KBUILD");
            if (    !pszEnvVar
                ||  !my_abspath(pszEnvVar, szTmpPath))
            {
#ifdef KBUILD_PATH
                return s_pszPath = KBUILD_PATH;
#else
                /* $(abspath $(KBUILD_BIN_PATH)/../..)*/
                size_t cch = strlen(get_kbuild_bin_path());
                char *pszTmp2 = alloca(cch + sizeof("/../.."));
                strcat(strcpy(pszTmp2, get_kbuild_bin_path()), "/../..");
                if (!my_abspath(pszTmp2, szTmpPath))
                    fatal(NILF, _("failed to determin KBUILD_PATH"));
#endif
            }
        }
        s_pszPath = xstrdup(szTmpPath);
    }
    return s_pszPath;
}


/**
 * Determin the KBUILD_BIN_PATH value.
 *
 * @returns Pointer to static a buffer containing the value (consider it read-only).
 */
const char *get_kbuild_bin_path(void)
{
    static const char *s_pszPath = NULL;
    if (!s_pszPath)
    {
        PATH_VAR(szTmpPath);

        const char *pszEnvVar = getenv("KBUILD_BIN_PATH");
        if (    !pszEnvVar
            ||  !my_abspath(pszEnvVar, szTmpPath))
        {
            const char *pszEnvVar = getenv("PATH_KBUILD_BIN");
            if (    !pszEnvVar
                ||  !my_abspath(pszEnvVar, szTmpPath))
            {
#ifdef KBUILD_PATH
                return s_pszPath = KBUILD_BIN_PATH;
#else
                /* $(abspath $(dir $(ARGV0)).) */
                size_t cch = strlen(g_pszExeName);
                char *pszTmp2 = alloca(cch + sizeof("."));
                char *pszSep = pszTmp2 + cch - 1;
                memcpy(pszTmp2, g_pszExeName, cch);
# ifdef HAVE_DOS_PATHS
                while (pszSep >= pszTmp2 && *pszSep != '/' && *pszSep != '\\' && *pszSep != ':')
# else
                while (pszSep >= pszTmp2 && *pszSep != '/')
# endif
                    pszSep--;
                if (pszSep >= pszTmp2)
                  strcpy(pszSep + 1, ".");
                else
                  strcpy(pszTmp2, ".");

                if (!my_abspath(pszTmp2, szTmpPath))
                    fatal(NILF, _("failed to determin KBUILD_BIN_PATH (pszTmp2=%s szTmpPath=%s)"), pszTmp2, szTmpPath);
#endif /* !KBUILD_PATH */
            }
        }
        s_pszPath = xstrdup(szTmpPath);
    }
    return s_pszPath;
}


/**
 * Determin the location of default kBuild shell.
 *
 * @returns Pointer to static a buffer containing the location (consider it read-only).
 */
const char *get_default_kbuild_shell(void)
{
    static char *s_pszDefaultShell = NULL;
    if (!s_pszDefaultShell)
    {
#if defined(__OS2__) || defined(_WIN32) || defined(WINDOWS32)
        static const char s_szShellName[] = "/kmk_ash.exe";
#else
        static const char s_szShellName[] = "/kmk_ash";
#endif
        const char *pszBin = get_kbuild_bin_path();
        size_t cchBin = strlen(pszBin);
        s_pszDefaultShell = xmalloc(cchBin + sizeof(s_szShellName));
        memcpy(s_pszDefaultShell, pszBin, cchBin);
        memcpy(&s_pszDefaultShell[cchBin], s_szShellName, sizeof(s_szShellName));
    }
    return s_pszDefaultShell;
}

#ifdef KMK_HELPERS

/**
 * Applies the specified default path to any relative paths in *ppsz.
 *
 * @param   pDefPath        The default path.
 * @param   ppsz            Pointer to the string pointer. If we expand anything, *ppsz
 *                          will be replaced and the caller is responsible for calling free() on it.
 * @param   pcch            IN: *pcch contains the current string length.
 *                          OUT: *pcch contains the new string length.
 * @param   pcchAlloc       *pcchAlloc contains the length allocated for the string. Can be NULL.
 * @param   fCanFree        Whether *ppsz should be freed when we replace it.
 */
static void
kbuild_apply_defpath(struct variable *pDefPath, char **ppsz, unsigned int *pcch, int *pcchAlloc, int fCanFree)
{
    const char *pszIterator;
    const char *pszInCur;
    unsigned int cchInCur;
    unsigned int cRelativePaths;

    /*
     * The first pass, count the relative paths.
     */
    cRelativePaths = 0;
    pszIterator = *ppsz;
    while ((pszInCur = find_next_token(&pszIterator, &cchInCur)))
    {
        /* is relative? */
#ifdef HAVE_DOS_PATHS
        if (pszInCur[0] != '/' && pszInCur[0] != '\\' && (cchInCur < 2 || pszInCur[1] != ':'))
#else
        if (pszInCur[0] != '/')
#endif
            cRelativePaths++;
    }

    /*
     * The second pass construct the new string.
     */
    if (cRelativePaths)
    {
        const size_t cchOut = *pcch + cRelativePaths * (pDefPath->value_length + 1) + 1;
        char *pszOut = xmalloc(cchOut);
        char *pszOutCur = pszOut;
        const char *pszInNextCopy = *ppsz;

        cRelativePaths = 0;
        pszIterator = *ppsz;
        while ((pszInCur = find_next_token(&pszIterator, &cchInCur)))
        {
            /* is relative? */
#ifdef HAVE_DOS_PATHS
            if (pszInCur[0] != '/' && pszInCur[0] != '\\' && (cchInCur < 2 || pszInCur[1] != ':'))
#else
            if (pszInCur[0] != '/')
#endif
            {
                PATH_VAR(szAbsPathIn);
                PATH_VAR(szAbsPathOut);

                if (pDefPath->value_length + cchInCur + 1 >= GET_PATH_MAX)
                    continue;

                /* Create the abspath input. */
                memcpy(szAbsPathIn, pDefPath->value, pDefPath->value_length);
                szAbsPathIn[pDefPath->value_length] = '/';
                memcpy(&szAbsPathIn[pDefPath->value_length + 1], pszInCur, cchInCur);
                szAbsPathIn[pDefPath->value_length + 1 + cchInCur] = '\0';

                if (abspath(szAbsPathIn, szAbsPathOut) != NULL)
                {
                    const size_t cchAbsPathOut = strlen(szAbsPathOut);
                    assert(cchAbsPathOut <= pDefPath->value_length + 1 + cchInCur);

                    /* copy leading input */
                    if (pszInCur != pszInNextCopy)
                    {
                        const size_t cchCopy = pszInCur - pszInNextCopy;
                        memcpy(pszOutCur, pszInNextCopy, cchCopy);
                        pszOutCur += cchCopy;
                    }
                    pszInNextCopy = pszInCur + cchInCur;

                    /* copy out the abspath. */
                    memcpy(pszOutCur, szAbsPathOut, cchAbsPathOut);
                    pszOutCur += cchAbsPathOut;
                }
            }
        }
        /* the final copy (includes the nil). */
        cchInCur = *ppsz + *pcch - pszInNextCopy;
        memcpy(pszOutCur, pszInNextCopy, cchInCur);
        pszOutCur += cchInCur;
        *pszOutCur = '\0';
        assert((size_t)(pszOutCur - pszOut) < cchOut);

        /* set return values */
        if (fCanFree)
            free(*ppsz);
        *ppsz = pszOut;
        *pcch = pszOutCur - pszOut;
        if (pcchAlloc)
            *pcchAlloc = cchOut;
    }
}

/**
 * Gets a variable that must exist.
 * Will cause a fatal failure if the variable doesn't exist.
 *
 * @returns Pointer to the variable.
 * @param   pszName     The variable name.
 * @param   cchName     The name length.
 */
MY_INLINE struct variable *
kbuild_get_variable_n(const char *pszName, size_t cchName)
{
#ifndef NDEBUG
    int i;
#endif
    struct variable *pVar = lookup_variable(pszName, cchName);
    if (!pVar)
        fatal(NILF, _("variable `%.*s' isn't defined!"), (int)cchName, pszName);
    if (pVar->recursive)
        fatal(NILF, _("variable `%.*s' is defined as `recursive' instead of `simple'!"), (int)cchName, pszName);
#ifndef NDEBUG
    i = strlen(pVar->value);
    if (i != pVar->value_length)
    {
        printf("%d != %d %s\n", pVar->value_length, i, pVar->name);
# ifdef _MSC_VER
        __debugbreak();
# endif
        assert(0);
    }
#endif
    return pVar;
}


#if 0 /* unused */
/**
 * Gets a variable that must exist.
 * Will cause a fatal failure if the variable doesn't exist.
 *
 * @returns Pointer to the variable.
 * @param   pszName     The variable name.
 */
static struct variable *
kbuild_get_variable(const char *pszName)
{
    return kbuild_get_variable_n(pszName, strlen(pszName));
}
#endif


/**
 * Gets a variable that must exist and can be recursive.
 * Will cause a fatal failure if the variable doesn't exist.
 *
 * @returns Pointer to the variable.
 * @param   pszName     The variable name.
 */
static struct variable *
kbuild_get_recursive_variable(const char *pszName)
{
#ifndef NDEBUG
    int i;
#endif
    struct variable *pVar = lookup_variable(pszName, strlen(pszName));
    if (!pVar)
        fatal(NILF, _("variable `%s' isn't defined!"), pszName);
#ifndef NDEBUG
    i = strlen(pVar->value);
    if (i != pVar->value_length)
    {
        printf("%d != %d %s\n", pVar->value_length, i, pVar->name);
# ifdef _MSC_VER
        __debugbreak();
# endif
        assert(0);
    }
#endif
    return pVar;
}


/**
 * Gets a variable that doesn't have to exit, but if it does can be recursive.
 *
 * @returns Pointer to the variable.
 *          NULL if not found.
 * @param   pszName     The variable name.
 */
static struct variable *
kbuild_query_recursive_variable(const char *pszName)
{
#ifndef NDEBUG
    int i;
#endif
    struct variable *pVar = lookup_variable(pszName, strlen(pszName));
    if (pVar)
    {
#ifndef NDEBUG
        i = strlen(pVar->value);
        if (i != pVar->value_length)
        {
            printf("%d != %d %s\n", pVar->value_length, i, pVar->name);
# ifdef _MSC_VER
            __debugbreak();
# endif
            assert(0);
        }
#endif
    }
    return pVar;
}


/**
 * Converts the specified variable into a 'simple' one.
 * @returns pVar.
 * @param   pVar        The variable.
 */
static struct variable *
kbuild_simplify_variable(struct variable *pVar)
{
    if (memchr(pVar->value, '$', pVar->value_length))
    {
        unsigned int value_len;
        char *pszExpanded = allocated_variable_expand_2(pVar->value, pVar->value_length, &value_len);
#ifdef CONFIG_WITH_RDONLY_VARIABLE_VALUE
        if (pVar->rdonly_val)
            pVar->rdonly_val = 0;
        else
#endif
            free(pVar->value);
        assert (pVar->origin != o_automatic);
        pVar->value = pszExpanded;
        pVar->value_length = value_len;
        pVar->value_alloc_len = value_len + 1;
    }
    pVar->recursive = 0;
    return pVar;
}


/**
 * Looks up a variable.
 * The value_length field is valid upon successful return.
 *
 * @returns Pointer to the variable. NULL if not found.
 * @param   pszName     The variable name.
 * @param   cchName     The name length.
 */
MY_INLINE struct variable *
kbuild_lookup_variable_n(const char *pszName, size_t cchName)
{
    struct variable *pVar = lookup_variable(pszName, cchName);
    if (pVar)
    {
#ifndef NDEBUG
        int i = strlen(pVar->value);
        if (i != pVar->value_length)
        {
            printf("%d != %d %s\n", pVar->value_length, i, pVar->name);
# ifdef _MSC_VER
            __debugbreak();
# endif
            assert(0);
        }
#endif
        /* Make sure the variable is simple, convert it if necessary. */
        if (pVar->recursive)
            kbuild_simplify_variable(pVar);
    }
    return pVar;
}


/**
 * Looks up a variable.
 * The value_length field is valid upon successful return.
 *
 * @returns Pointer to the variable. NULL if not found.
 * @param   pszName     The variable name.
 */
MY_INLINE struct variable *
kbuild_lookup_variable(const char *pszName)
{
    return kbuild_lookup_variable_n(pszName, strlen(pszName));
}


/**
 * Looks up a variable and applies default a path to all relative paths.
 * The value_length field is valid upon successful return.
 *
 * @returns Pointer to the variable. NULL if not found.
 * @param   pDefPath    The default path.
 * @param   pszName     The variable name.
 * @param   cchName     The name length.
 */
MY_INLINE struct variable *
kbuild_lookup_variable_defpath_n(struct variable *pDefPath, const char *pszName, size_t cchName)
{
    struct variable *pVar = kbuild_lookup_variable_n(pszName, cchName);
    if (pVar && pDefPath)
    {
        assert(pVar->origin != o_automatic);
#ifdef CONFIG_WITH_RDONLY_VARIABLE_VALUE
        assert(!pVar->rdonly_val);
#endif
        kbuild_apply_defpath(pDefPath, &pVar->value, (unsigned int *)&pVar->value_length, &pVar->value_alloc_len, 1);
    }
    return pVar;
}


/**
 * Looks up a variable and applies default a path to all relative paths.
 * The value_length field is valid upon successful return.
 *
 * @returns Pointer to the variable. NULL if not found.
 * @param   pDefPath    The default path.
 * @param   pszName     The variable name.
 */
MY_INLINE struct variable *
kbuild_lookup_variable_defpath(struct variable *pDefPath, const char *pszName)
{
    struct variable *pVar = kbuild_lookup_variable(pszName);
    if (pVar && pDefPath)
    {
        assert(pVar->origin != o_automatic);
#ifdef CONFIG_WITH_RDONLY_VARIABLE_VALUE
        assert(!pVar->rdonly_val);
#endif
        kbuild_apply_defpath(pDefPath, &pVar->value, (unsigned int *)&pVar->value_length, &pVar->value_alloc_len, 1);
    }
    return pVar;
}


/**
 * Gets the first defined property variable.
 */
static struct variable *
kbuild_first_prop(struct variable *pTarget, struct variable *pSource,
                  struct variable *pTool, struct variable *pType,
                  struct variable *pBldTrg, struct variable *pBldTrgArch,
                  const char *pszPropF1, char cchPropF1,
                  const char *pszPropF2, char cchPropF2,
                  const char *pszVarName)
{
    struct variable *pVar;
    size_t cchBuf;
    char *pszBuf;
    char *psz, *psz1, *psz2, *psz3, *psz4;

    /* calc and allocate a too big name buffer. */
    cchBuf = cchPropF2 + 1
           + cchPropF1 + 1
           + pTarget->value_length + 1
           + pSource->value_length + 1
           + (pTool ? pTool->value_length + 1 : 0)
           + pType->value_length + 1
           + pBldTrg->value_length + 1
           + pBldTrgArch->value_length + 1;
    pszBuf = xmalloc(cchBuf);

#define ADD_VAR(pVar)           do { my_memcpy(psz, (pVar)->value, (pVar)->value_length); psz += (pVar)->value_length; } while (0)
#define ADD_STR(pszStr, cchStr) do { my_memcpy(psz, (pszStr), (cchStr)); psz += (cchStr); } while (0)
#define ADD_CSTR(pszStr)        do { my_memcpy(psz, pszStr, sizeof(pszStr) - 1); psz += sizeof(pszStr) - 1; } while (0)
#define ADD_CH(ch)              do { *psz++ = (ch); } while (0)

    /*
     * $(target)_$(source)_$(type)$(propf2).$(bld_trg).$(bld_trg_arch)
     */
    psz = pszBuf;
    ADD_VAR(pTarget);
    ADD_CH('_');
    ADD_VAR(pSource);
    ADD_CH('_');
    psz2 = psz;
    ADD_VAR(pType);
    ADD_STR(pszPropF2, cchPropF2);
    psz3 = psz;
    ADD_CH('.');
    ADD_VAR(pBldTrg);
    psz4 = psz;
    ADD_CH('.');
    ADD_VAR(pBldTrgArch);
    pVar = kbuild_lookup_variable_n(pszBuf, psz - pszBuf);

    /* $(target)_$(source)_$(type)$(propf2).$(bld_trg) */
    if (!pVar)
        pVar = kbuild_lookup_variable_n(pszBuf, psz4 - pszBuf);

    /* $(target)_$(source)_$(type)$(propf2) */
    if (!pVar)
        pVar = kbuild_lookup_variable_n(pszBuf, psz3 - pszBuf);

    /*
     * $(target)_$(source)_$(propf2).$(bld_trg).$(bld_trg_arch)
     */
    if (!pVar)
    {
        psz = psz2;
        ADD_STR(pszPropF2, cchPropF2);
        psz3 = psz;
        ADD_CH('.');
        ADD_VAR(pBldTrg);
        psz4 = psz;
        ADD_CH('.');
        ADD_VAR(pBldTrgArch);
        pVar = kbuild_lookup_variable_n(pszBuf, psz - pszBuf);

        /* $(target)_$(source)_$(propf2).$(bld_trg) */
        if (!pVar)
            pVar = kbuild_lookup_variable_n(pszBuf, psz4 - pszBuf);

        /* $(target)_$(source)_$(propf2) */
        if (!pVar)
            pVar = kbuild_lookup_variable_n(pszBuf, psz3 - pszBuf);
    }


    /*
     * $(source)_$(type)$(propf2).$(bld_trg).$(bld_trg_arch)
     */
    if (!pVar)
    {
        psz = pszBuf;
        ADD_VAR(pSource);
        ADD_CH('_');
        psz2 = psz;
        ADD_VAR(pType);
        ADD_STR(pszPropF2, cchPropF2);
        psz3 = psz;
        ADD_CH('.');
        ADD_VAR(pBldTrg);
        psz4 = psz;
        ADD_CH('.');
        ADD_VAR(pBldTrgArch);
        pVar = kbuild_lookup_variable_n(pszBuf, psz - pszBuf);

        /* $(source)_$(type)$(propf2).$(bld_trg) */
        if (!pVar)
            pVar = kbuild_lookup_variable_n(pszBuf, psz4 - pszBuf);

        /* $(source)_$(type)$(propf2) */
        if (!pVar)
            pVar = kbuild_lookup_variable_n(pszBuf, psz3 - pszBuf);

        /*
         * $(source)_$(propf2).$(bld_trg).$(bld_trg_arch)
         */
        if (!pVar)
        {
            psz = psz2;
            ADD_STR(pszPropF2, cchPropF2);
            psz3 = psz;
            ADD_CH('.');
            ADD_VAR(pBldTrg);
            psz4 = psz;
            ADD_CH('.');
            ADD_VAR(pBldTrgArch);
            pVar = kbuild_lookup_variable_n(pszBuf, psz - pszBuf);

            /* $(source)_$(propf2).$(bld_trg) */
            if (!pVar)
                pVar = kbuild_lookup_variable_n(pszBuf, psz4 - pszBuf);

            /* $(source)_$(propf2) */
            if (!pVar)
                pVar = kbuild_lookup_variable_n(pszBuf, psz3 - pszBuf);
        }
    }

    /*
     * $(target)_$(type)$(propf2).$(bld_trg).$(bld_trg_arch)
     */
    if (!pVar)
    {
        psz = pszBuf;
        ADD_VAR(pTarget);
        ADD_CH('_');
        psz2 = psz;
        ADD_VAR(pType);
        ADD_STR(pszPropF2, cchPropF2);
        psz3 = psz;
        ADD_CH('.');
        ADD_VAR(pBldTrg);
        psz4 = psz;
        ADD_CH('.');
        ADD_VAR(pBldTrgArch);
        pVar = kbuild_lookup_variable_n(pszBuf, psz - pszBuf);

        /* $(target)_$(type)$(propf2).$(bld_trg) */
        if (!pVar)
            pVar = kbuild_lookup_variable_n(pszBuf, psz4 - pszBuf);

        /* $(target)_$(type)$(propf2) */
        if (!pVar)
            pVar = kbuild_lookup_variable_n(pszBuf, psz3 - pszBuf);

        /* $(target)_$(propf2).$(bld_trg).$(bld_trg_arch) */
        if (!pVar)
        {
            psz = psz2;
            ADD_STR(pszPropF2, cchPropF2);
            psz3 = psz;
            ADD_CH('.');
            ADD_VAR(pBldTrg);
            psz4 = psz;
            ADD_CH('.');
            ADD_VAR(pBldTrgArch);
            pVar = kbuild_lookup_variable_n(pszBuf, psz - pszBuf);
        }

        /* $(target)_$(propf2).$(bld_trg) */
        if (!pVar)
            pVar = kbuild_lookup_variable_n(pszBuf, psz4 - pszBuf);

        /* $(target)_$(propf2) */
        if (!pVar)
            pVar = kbuild_lookup_variable_n(pszBuf, psz3 - pszBuf);
    }

    /*
     * TOOL_$(tool)_$(type)$(propf2).$(bld_trg).$(bld_trg_arch)
     */
    if (!pVar && pTool)
    {
        psz = pszBuf;
        ADD_CSTR("TOOL_");
        ADD_VAR(pTool);
        ADD_CH('_');
        psz2 = psz;
        ADD_VAR(pType);
        ADD_STR(pszPropF2, cchPropF2);
        psz3 = psz;
        ADD_CH('.');
        ADD_VAR(pBldTrg);
        psz4 = psz;
        ADD_CH('.');
        ADD_VAR(pBldTrgArch);
        pVar = kbuild_lookup_variable_n(pszBuf, psz - pszBuf);

        /* TOOL_$(tool)_$(type)$(propf2).$(bld_trg) */
        if (!pVar)
            pVar = kbuild_lookup_variable_n(pszBuf, psz4 - pszBuf);

        /* TOOL_$(tool)_$(type)$(propf2) */
        if (!pVar)
            pVar = kbuild_lookup_variable_n(pszBuf, psz3 - pszBuf);

        /* TOOL_$(tool)_$(propf2).$(bld_trg).$(bld_trg_arch) */
        if (!pVar)
        {
            psz = psz2;
            ADD_STR(pszPropF2, cchPropF2);
            psz3 = psz;
            ADD_CH('.');
            ADD_VAR(pBldTrg);
            psz4 = psz;
            ADD_CH('.');
            ADD_VAR(pBldTrgArch);
            pVar = kbuild_lookup_variable_n(pszBuf, psz - pszBuf);

            /* TOOL_$(tool)_$(propf2).$(bld_trg) */
            if (!pVar)
                pVar = kbuild_lookup_variable_n(pszBuf, psz4 - pszBuf);

            /* TOOL_$(tool)_$(propf2) */
            if (!pVar)
                pVar = kbuild_lookup_variable_n(pszBuf, psz3 - pszBuf);
        }
    }

    /*
     * $(type)$(propf1).$(bld_trg).$(bld_trg_arch)
     */
    if (!pVar)
    {
        psz = pszBuf;
        ADD_VAR(pType);
        ADD_STR(pszPropF1, cchPropF1);
        psz3 = psz;
        ADD_CH('.');
        ADD_VAR(pBldTrg);
        psz4 = psz;
        ADD_CH('.');
        ADD_VAR(pBldTrgArch);
        pVar = kbuild_lookup_variable_n(pszBuf, psz - pszBuf);

        /* $(type)$(propf1).$(bld_trg) */
        if (!pVar)
            pVar = kbuild_lookup_variable_n(pszBuf, psz4 - pszBuf);

        /* $(type)$(propf1) */
        if (!pVar)
            pVar = kbuild_lookup_variable_n(pszBuf, psz3 - pszBuf);

        /*
         * $(propf1).$(bld_trg).$(bld_trg_arch)
         */
        if (!pVar)
        {
            psz1 = pszBuf + pType->value_length;
            pVar = kbuild_lookup_variable_n(psz1, psz - psz1);

            /* $(propf1).$(bld_trg) */
            if (!pVar)
                pVar = kbuild_lookup_variable_n(psz1, psz4 - psz1);

            /* $(propf1) */
            if (!pVar)
                pVar = kbuild_lookup_variable_n(pszPropF1, cchPropF1);
        }
    }
    free(pszBuf);
#undef ADD_VAR
#undef ADD_STR
#undef ADD_CSTR
#undef ADD_CH

    if (pVar)
    {
        /* strip it */
        char *psz = pVar->value;
        char *pszEnd = psz + pVar->value_length;
        while (isblank((unsigned char)*psz))
            psz++;
        while (pszEnd > psz && isblank((unsigned char)pszEnd[-1]))
            pszEnd--;
        if (pszEnd > psz)
        {
            char chSaved = *pszEnd;
            *pszEnd = '\0';
            pVar = define_variable_vl(pszVarName, strlen(pszVarName), psz, pszEnd - psz,
                                      1 /* duplicate */, o_local, 0 /* !recursive */);
            *pszEnd = chSaved;
            if (pVar)
                return pVar;
        }
    }
    return NULL;
}


/*
_SOURCE_TOOL = $(strip $(firstword \
    $($(target)_$(source)_$(type)TOOL.$(bld_trg).$(bld_trg_arch)) \
    $($(target)_$(source)_$(type)TOOL.$(bld_trg)) \
    $($(target)_$(source)_$(type)TOOL) \
    $($(target)_$(source)_TOOL.$(bld_trg).$(bld_trg_arch)) \
    $($(target)_$(source)_TOOL.$(bld_trg)) \
    $($(target)_$(source)_TOOL) \
    $($(source)_$(type)TOOL.$(bld_trg).$(bld_trg_arch)) \
    $($(source)_$(type)TOOL.$(bld_trg)) \
    $($(source)_$(type)TOOL) \
    $($(source)_TOOL.$(bld_trg).$(bld_trg_arch)) \
    $($(source)_TOOL.$(bld_trg)) \
    $($(source)_TOOL) \
    $($(target)_$(type)TOOL.$(bld_trg).$(bld_trg_arch)) \
    $($(target)_$(type)TOOL.$(bld_trg)) \
    $($(target)_$(type)TOOL) \
    $($(target)_TOOL.$(bld_trg).$(bld_trg_arch)) \
    $($(target)_TOOL.$(bld_trg)) \
    $($(target)_TOOL) \
    $($(type)TOOL.$(bld_trg).$(bld_trg_arch)) \
    $($(type)TOOL.$(bld_trg)) \
    $($(type)TOOL) \
    $(TOOL.$(bld_trg).$(bld_trg_arch)) \
    $(TOOL.$(bld_trg)) \
    $(TOOL) ))
*/
static struct variable *
kbuild_get_source_tool(struct variable *pTarget, struct variable *pSource, struct variable *pType,
                       struct variable *pBldTrg, struct variable *pBldTrgArch, const char *pszVarName)
{
    struct variable *pVar = kbuild_first_prop(pTarget, pSource, NULL, pType, pBldTrg, pBldTrgArch,
                                              "TOOL", sizeof("TOOL") - 1,
                                              "TOOL", sizeof("TOOL") - 1,
                                              pszVarName);
    if (!pVar)
        fatal(NILF, _("no tool for source `%s' in target `%s'!"), pSource->value, pTarget->value);
    return pVar;
}


/* Implements _SOURCE_TOOL. */
char *
func_kbuild_source_tool(char *o, char **argv, const char *pszFuncName)
{
    struct variable *pVar = kbuild_get_source_tool(kbuild_get_variable_n(ST("target")),
                                                   kbuild_get_variable_n(ST("source")),
                                                   kbuild_get_variable_n(ST("type")),
                                                   kbuild_get_variable_n(ST("bld_trg")),
                                                   kbuild_get_variable_n(ST("bld_trg_arch")),
                                                   argv[0]);
    if (pVar)
         o = variable_buffer_output(o, pVar->value, pVar->value_length);
    (void)pszFuncName;
    return o;

}


/* This has been extended a bit, it's now identical to _SOURCE_TOOL.
$(firstword \
	$($(target)_$(source)_OBJSUFF.$(bld_trg).$(bld_trg_arch))\
	$($(target)_$(source)_OBJSUFF.$(bld_trg))\
	$($(target)_$(source)_OBJSUFF)\
	$($(source)_OBJSUFF.$(bld_trg).$(bld_trg_arch))\
	$($(source)_OBJSUFF.$(bld_trg))\
	$($(source)_OBJSUFF)\
	$($(target)_OBJSUFF.$(bld_trg).$(bld_trg_arch))\
	$($(target)_OBJSUFF.$(bld_trg))\
	$($(target)_OBJSUFF)\
	$(TOOL_$(tool)_$(type)OBJSUFF.$(bld_trg).$(bld_trg_arch))\
	$(TOOL_$(tool)_$(type)OBJSUFF.$(bld_trg))\
	$(TOOL_$(tool)_$(type)OBJSUFF)\
	$(SUFF_OBJ))
*/
static struct variable *
kbuild_get_object_suffix(struct variable *pTarget, struct variable *pSource,
                         struct variable *pTool, struct variable *pType,
                         struct variable *pBldTrg, struct variable *pBldTrgArch, const char *pszVarName)
{
    struct variable *pVar = kbuild_first_prop(pTarget, pSource, pTool, pType, pBldTrg, pBldTrgArch,
                                              "SUFF_OBJ", sizeof("SUFF_OBJ") - 1,
                                              "OBJSUFF",  sizeof("OBJSUFF")  - 1,
                                              pszVarName);
    if (!pVar)
        fatal(NILF, _("no OBJSUFF attribute or SUFF_OBJ default for source `%s' in target `%s'!"), pSource->value, pTarget->value);
    return pVar;
}


/*  */
char *
func_kbuild_object_suffix(char *o, char **argv, const char *pszFuncName)
{
    struct variable *pVar = kbuild_get_object_suffix(kbuild_get_variable_n(ST("target")),
                                                     kbuild_get_variable_n(ST("source")),
                                                     kbuild_get_variable_n(ST("tool")),
                                                     kbuild_get_variable_n(ST("type")),
                                                     kbuild_get_variable_n(ST("bld_trg")),
                                                     kbuild_get_variable_n(ST("bld_trg_arch")),
                                                     argv[0]);
    if (pVar)
         o = variable_buffer_output(o, pVar->value, pVar->value_length);
    (void)pszFuncName;
    return o;

}


/*
## Figure out where to put object files.
# @param    $1      source file
# @param    $2      normalized main target
# @remark There are two major hacks here:
#           1. Source files in the output directory are translated into a gen/ subdir.
#         	2. Catch anyone specifying $(PATH_SUB_CURRENT)/sourcefile.c.
_OBJECT_BASE = $(PATH_TARGET)/$(2)/$(call no-root-slash,$(call no-drive,$(basename \
	$(patsubst $(PATH_ROOT)/%,%,$(patsubst $(PATH_SUB_CURRENT)/%,%,$(patsubst $(PATH_TARGET)/$(2)/%,gen/%,$(1)))))))
*/
static struct variable *
kbuild_get_object_base(struct variable *pTarget, struct variable *pSource, const char *pszVarName)
{
    struct variable *pPathTarget = kbuild_get_variable_n(ST("PATH_TARGET"));
    struct variable *pPathRoot   = kbuild_get_variable_n(ST("PATH_ROOT"));
    struct variable *pPathSubCur = kbuild_get_variable_n(ST("PATH_SUB_CURRENT"));
    const char *pszSrcPrefix = NULL;
    size_t      cchSrcPrefix = 0;
    size_t      cchSrc = 0;
    const char *pszSrcEnd;
    char *pszSrc;
    char *pszResult;
    char *psz;
    char *pszDot;
    size_t cch;

    /*
     * Strip the source filename of any uncessary leading path and root specs.
     */
    /* */
    if (    pSource->value_length > pPathTarget->value_length
        &&  !strncmp(pSource->value, pPathTarget->value, pPathTarget->value_length))
    {
        pszSrc = pSource->value + pPathTarget->value_length;
        pszSrcPrefix = "gen/";
        cchSrcPrefix = sizeof("gen/") - 1;
        if (    *pszSrc == '/'
            &&  !strncmp(pszSrc + 1, pTarget->value, pTarget->value_length)
            &&   (   pszSrc[pTarget->value_length + 1] == '/'
                  || pszSrc[pTarget->value_length + 1] == '\0'))
            pszSrc += 1 + pTarget->value_length;
    }
    else if (    pSource->value_length > pPathRoot->value_length
             &&  !strncmp(pSource->value, pPathRoot->value, pPathRoot->value_length))
    {
        pszSrc = pSource->value + pPathRoot->value_length;
        if (    *pszSrc == '/'
            &&  !strncmp(pszSrc + 1, pPathSubCur->value, pPathSubCur->value_length)
            &&   (   pszSrc[pPathSubCur->value_length + 1] == '/'
                  || pszSrc[pPathSubCur->value_length + 1] == '\0'))
            pszSrc += 1 + pPathSubCur->value_length;
    }
    else
        pszSrc = pSource->value;

    /* skip root specification */
#ifdef HAVE_DOS_PATHS
    if (isalpha(pszSrc[0]) && pszSrc[1] == ':')
        pszSrc += 2;
#endif
    while (*pszSrc == '/'
#ifdef HAVE_DOS_PATHS
           || *pszSrc == '\\'
#endif
           )
        pszSrc++;

    /* drop the source extension. */
    pszSrcEnd = pSource->value + pSource->value_length;
    for (;;)
    {
        pszSrcEnd--;
        if (    pszSrcEnd <= pszSrc
            ||  *pszSrcEnd == '/'
#ifdef HAVE_DOS_PATHS
            ||  *pszSrcEnd == '\\'
            ||  *pszSrcEnd == ':'
#endif
           )
        {
            pszSrcEnd = pSource->value + pSource->value_length;
            break;
        }
        if (*pszSrcEnd == '.')
            break;
    }

    /*
     * Assemble the string on the heap and define the objbase variable
     * which we then return.
     */
    cchSrc = pszSrcEnd - pszSrc;
    cch = pPathTarget->value_length
        + 1 /* slash */
        + pTarget->value_length
        + 1 /* slash */
        + cchSrcPrefix
        + cchSrc
        + 1;
    psz = pszResult = xmalloc(cch);

    memcpy(psz, pPathTarget->value, pPathTarget->value_length); psz += pPathTarget->value_length;
    *psz++ = '/';
    memcpy(psz, pTarget->value, pTarget->value_length); psz += pTarget->value_length;
    *psz++ = '/';
    if (pszSrcPrefix)
    {
        memcpy(psz, pszSrcPrefix, cchSrcPrefix);
        psz += cchSrcPrefix;
    }
    pszDot = psz;
    memcpy(psz, pszSrc, cchSrc); psz += cchSrc;
    *psz = '\0';

    /* convert '..' path elements in the source to 'dt'. */
    while ((pszDot = memchr(pszDot, '.', psz - pszDot)) != NULL)
    {
        if (    pszDot[1] == '.'
            &&  (   pszDot == psz
                 || pszDot[-1] == '/'
#ifdef HAVE_DOS_PATHS
                 || pszDot[-1] == '\\'
                 || pszDot[-1] == ':'
#endif
                )
            &&  (   !pszDot[2]
                 || pszDot[2] == '/'
#ifdef HAVE_DOS_PATHS
                 || pszDot[2] == '\\'
                 || pszDot[2] == ':'
#endif
                )
            )
        {
            *pszDot++ = 'd';
            *pszDot++ = 't';
        }
        else
            pszDot++;
    }

    /*
     * Define the variable in the current set and return it.
     */
    return define_variable_vl(pszVarName, strlen(pszVarName), pszResult, cch - 1,
                              0 /* use pszResult */, o_local, 0 /* !recursive */);
}


/* Implements _OBJECT_BASE. */
char *
func_kbuild_object_base(char *o, char **argv, const char *pszFuncName)
{
    struct variable *pVar = kbuild_get_object_base(kbuild_lookup_variable("target"),
                                                   kbuild_lookup_variable("source"),
                                                   argv[0]);
    if (pVar)
         o = variable_buffer_output(o, pVar->value, pVar->value_length);
    (void)pszFuncName;
    return o;

}


struct kbuild_sdks
{
    char *apsz[4];
    struct variable *pa;
    unsigned c;
    unsigned iGlobal;
    unsigned cGlobal;
    unsigned iTarget;
    unsigned cTarget;
    unsigned iSource;
    unsigned cSource;
    unsigned iTargetSource;
    unsigned cTargetSource;
    unsigned int cchMax;
};


/* Fills in the SDK struct (remember to free it). */
static void
kbuild_get_sdks(struct kbuild_sdks *pSdks, struct variable *pTarget, struct variable *pSource,
                struct variable *pBldType, struct variable *pBldTrg, struct variable *pBldTrgArch)
{
    unsigned i;
    unsigned j;
    size_t cchTmp, cch;
    char *pszTmp;
    unsigned cchCur;
    char *pszCur;
    const char *pszIterator;

    /** @todo rewrite this to avoid sprintf and allocated_varaible_expand_2. */

    /* basic init. */
    pSdks->cchMax = 0;
    pSdks->pa = NULL;
    pSdks->c = 0;
    i = 0;

    /* determin required tmp variable name space. */
    cchTmp = sizeof("$(__SDKS) $(__SDKS.) $(__SDKS.) $(__SDKS.) $(__SDKS..)")
           + (pTarget->value_length + pSource->value_length) * 5
           + pBldType->value_length
           + pBldTrg->value_length
           + pBldTrgArch->value_length
           + pBldTrg->value_length + pBldTrgArch->value_length;
    pszTmp = alloca(cchTmp);

    /* the global sdks. */
    pSdks->iGlobal = i;
    pSdks->cGlobal = 0;
    cch = sprintf(pszTmp, "$(SDKS) $(SDKS.%s) $(SDKS.%s) $(SDKS.%s) $(SDKS.%s.%s)",
                  pBldType->value,
                  pBldTrg->value,
                  pBldTrgArch->value,
                  pBldTrg->value, pBldTrgArch->value);
    pszIterator = pSdks->apsz[0] = allocated_variable_expand_2(pszTmp, cch, NULL);
    while ((pszCur = find_next_token(&pszIterator, &cchCur)) != 0)
        pSdks->cGlobal++;
    i += pSdks->cGlobal;

    /* the target sdks.*/
    pSdks->iTarget = i;
    pSdks->cTarget = 0;
    cch = sprintf(pszTmp, "$(%s_SDKS) $(%s_SDKS.%s) $(%s_SDKS.%s) $(%s_SDKS.%s) $(%s_SDKS.%s.%s)",
                  pTarget->value,
                  pTarget->value, pBldType->value,
                  pTarget->value, pBldTrg->value,
                  pTarget->value, pBldTrgArch->value,
                  pTarget->value, pBldTrg->value, pBldTrgArch->value);
    pszIterator = pSdks->apsz[1] = allocated_variable_expand_2(pszTmp, cch, NULL);
    while ((pszCur = find_next_token(&pszIterator, &cchCur)) != 0)
        pSdks->cTarget++;
    i += pSdks->cTarget;

    /* the source sdks.*/
    pSdks->iSource = i;
    pSdks->cSource = 0;
    cch = sprintf(pszTmp, "$(%s_SDKS) $(%s_SDKS.%s) $(%s_SDKS.%s) $(%s_SDKS.%s) $(%s_SDKS.%s.%s)",
                  pSource->value,
                  pSource->value, pBldType->value,
                  pSource->value, pBldTrg->value,
                  pSource->value, pBldTrgArch->value,
                  pSource->value, pBldTrg->value, pBldTrgArch->value);
    pszIterator = pSdks->apsz[2] = allocated_variable_expand_2(pszTmp, cch, NULL);
    while ((pszCur = find_next_token(&pszIterator, &cchCur)) != 0)
        pSdks->cSource++;
    i += pSdks->cSource;

    /* the target + source sdks. */
    pSdks->iTargetSource = i;
    pSdks->cTargetSource = 0;
    cch = sprintf(pszTmp, "$(%s_%s_SDKS) $(%s_%s_SDKS.%s) $(%s_%s_SDKS.%s) $(%s_%s_SDKS.%s) $(%s_%s_SDKS.%s.%s)",
                  pTarget->value, pSource->value,
                  pTarget->value, pSource->value, pBldType->value,
                  pTarget->value, pSource->value, pBldTrg->value,
                  pTarget->value, pSource->value, pBldTrgArch->value,
                  pTarget->value, pSource->value, pBldTrg->value, pBldTrgArch->value);
    assert(cch < cchTmp); (void)cch;
    pszIterator = pSdks->apsz[3] = allocated_variable_expand_2(pszTmp, cch, NULL);
    while ((pszCur = find_next_token(&pszIterator, &cchCur)) != 0)
        pSdks->cTargetSource++;
    i += pSdks->cTargetSource;

    pSdks->c = i;
    if (!i)
        return;

    /*
     * Allocate the variable array and create the variables.
     */
    pSdks->pa = (struct variable *)xmalloc(sizeof(pSdks->pa[0]) * i);
    memset(pSdks->pa, 0, sizeof(pSdks->pa[0]) * i);
    for (i = j = 0; j < sizeof(pSdks->apsz) / sizeof(pSdks->apsz[0]); j++)
    {
        pszIterator = pSdks->apsz[j];
        while ((pszCur = find_next_token(&pszIterator, &cchCur)) != 0)
        {
            pSdks->pa[i].value = pszCur;
            pSdks->pa[i].value_length = cchCur;
            i++;
        }
    }
    assert(i == pSdks->c);

    /* terminate them (find_next_token won't work if we terminate them in the previous loop). */
    while (i-- > 0)
    {
        pSdks->pa[i].value[pSdks->pa[i].value_length] = '\0';

        /* calc the max variable length too. */
        if (pSdks->cchMax < (unsigned int)pSdks->pa[i].value_length)
            pSdks->cchMax = pSdks->pa[i].value_length;
    }
}


/* releases resources allocated in the kbuild_get_sdks. */
static void
kbuild_put_sdks(struct kbuild_sdks *pSdks)
{
    unsigned j;
    for (j = 0; j < sizeof(pSdks->apsz) / sizeof(pSdks->apsz[0]); j++)
        free(pSdks->apsz[j]);
    free(pSdks->pa);
}


/* this kind of stuff:

defs        := $(kb-src-exp defs)
	$(TOOL_$(tool)_DEFS)\
	$(TOOL_$(tool)_DEFS.$(bld_type))\
	$(TOOL_$(tool)_DEFS.$(bld_trg))\
	$(TOOL_$(tool)_DEFS.$(bld_trg_arch))\
	$(TOOL_$(tool)_DEFS.$(bld_trg).$(bld_trg_arch))\
	$(TOOL_$(tool)_DEFS.$(bld_trg_cpu))\
	$(TOOL_$(tool)_$(type)DEFS)\
	$(TOOL_$(tool)_$(type)DEFS.$(bld_type))\
	$(foreach sdk, $(SDKS.$(bld_trg)) \
				   $(SDKS.$(bld_trg).$(bld_trg_arch)) \
				   $(SDKS.$(bld_type)) \
				   $(SDKS),\
		$(SDK_$(sdk)_DEFS)\
		$(SDK_$(sdk)_DEFS.$(bld_type))\
		$(SDK_$(sdk)_DEFS.$(bld_trg))\
		$(SDK_$(sdk)_DEFS.$(bld_trg_arch))\
		$(SDK_$(sdk)_DEFS.$(bld_trg).$(bld_trg_arch))\
		$(SDK_$(sdk)_DEFS.$(bld_trg_cpu))\
		$(SDK_$(sdk)_$(type)DEFS)\
		$(SDK_$(sdk)_$(type)DEFS.$(bld_type))\
		$(SDK_$(sdk)_$(type)DEFS.$(bld_trg))\
		$(SDK_$(sdk)_$(type)DEFS.$(bld_trg_arch))\
		$(SDK_$(sdk)_$(type)DEFS.$(bld_trg).$(bld_trg_arch))\
		$(SDK_$(sdk)_$(type)DEFS.$(bld_trg_cpu)))\
	$(DEFS)\
	$(DEFS.$(bld_type))\
	$(DEFS.$(bld_trg))\
	$(DEFS.$(bld_trg_arch))\
	$(DEFS.$(bld_trg).$(bld_trg_arch))\
	$(DEFS.$(bld_trg_cpu))\
	$($(type)DEFS)\
	$($(type)DEFS.$(bld_type))\
	$($(type)DEFS.$(bld_trg))\
	$($(type)DEFS.$(bld_trg_arch))\
	$($(type)DEFS.$(bld_trg).$(bld_trg_arch))\
	$($(type)DEFS.$(bld_trg_cpu))\
	$(foreach sdk, $($(target)_SDKS.$(bld_trg)) \
				   $($(target)_SDKS.$(bld_trg).$(bld_trg_arch)) \
				   $($(target)_SDKS.$(bld_type)) \
				   $($(target)_SDKS),\
		$(SDK_$(sdk)_DEFS)\
		$(SDK_$(sdk)_DEFS.$(bld_type))\
		$(SDK_$(sdk)_DEFS.$(bld_trg))\
		$(SDK_$(sdk)_DEFS.$(bld_trg_arch))\
		$(SDK_$(sdk)_DEFS.$(bld_trg).$(bld_trg_arch))\
		$(SDK_$(sdk)_DEFS.$(bld_trg_cpu))\
		$(SDK_$(sdk)_$(type)DEFS)\
		$(SDK_$(sdk)_$(type)DEFS.$(bld_type))\
		$(SDK_$(sdk)_$(type)DEFS.$(bld_trg))\
		$(SDK_$(sdk)_$(type)DEFS.$(bld_trg_arch))\
		$(SDK_$(sdk)_$(type)DEFS.$(bld_trg).$(bld_trg_arch))\
		$(SDK_$(sdk)_$(type)DEFS.$(bld_trg_cpu)))\
	$($(target)_DEFS)\
	$($(target)_DEFS.$(bld_type))\
	$($(target)_DEFS.$(bld_trg))\
	$($(target)_DEFS.$(bld_trg_arch))\
	$($(target)_DEFS.$(bld_trg).$(bld_trg_arch))\
	$($(target)_DEFS.$(bld_trg_cpu))\
	$($(target)_$(type)DEFS)\
	$($(target)_$(type)DEFS.$(bld_type))\
	$($(target)_$(type)DEFS.$(bld_trg))\
	$($(target)_$(type)DEFS.$(bld_trg_arch))\
	$($(target)_$(type)DEFS.$(bld_trg).$(bld_trg_arch))\
	$($(target)_$(type)DEFS.$(bld_trg_cpu))\
	$(foreach sdk, $($(source)_SDKS.$(bld_trg)) \
				   $($(source)_SDKS.$(bld_trg).$(bld_trg_arch)) \
				   $($(source)_SDKS.$(bld_type)) \
				   $($(source)_SDKS),\
		$(SDK_$(sdk)_DEFS)\
		$(SDK_$(sdk)_DEFS.$(bld_type))\
		$(SDK_$(sdk)_DEFS.$(bld_trg))\
		$(SDK_$(sdk)_DEFS.$(bld_trg_arch))\
		$(SDK_$(sdk)_DEFS.$(bld_trg).$(bld_trg_arch))\
		$(SDK_$(sdk)_DEFS.$(bld_trg_cpu))\
		$(SDK_$(sdk)_$(type)DEFS)\
		$(SDK_$(sdk)_$(type)DEFS.$(bld_type))\
		$(SDK_$(sdk)_$(type)DEFS.$(bld_trg))\
		$(SDK_$(sdk)_$(type)DEFS.$(bld_trg_arch))\
		$(SDK_$(sdk)_$(type)DEFS.$(bld_trg).$(bld_trg_arch))\
		$(SDK_$(sdk)_$(type)DEFS.$(bld_trg_cpu)))\
	$($(source)_DEFS)\
	$($(source)_DEFS.$(bld_type))\
	$($(source)_DEFS.$(bld_trg))\
	$($(source)_DEFS.$(bld_trg_arch))\
	$($(source)_DEFS.$(bld_trg).$(bld_trg_arch))\
	$($(source)_DEFS.$(bld_trg_cpu))\
	$($(source)_$(type)DEFS)\
	$($(source)_$(type)DEFS.$(bld_type))\
	$($(source)_$(type)DEFS.$(bld_trg))\
	$($(source)_$(type)DEFS.$(bld_trg_arch))\
	$($(source)_$(type)DEFS.$(bld_trg).$(bld_trg_arch))\
	$($(source)_$(type)DEFS.$(bld_trg_cpu))\
	$(foreach sdk, $($(target)_$(source)_SDKS.$(bld_trg)) \
				   $($(target)_$(source)_SDKS.$(bld_trg).$(bld_trg_arch)) \
				   $($(target)_$(source)_SDKS.$(bld_type)) \
				   $($(target)_$(source)_SDKS),\
		$(SDK_$(sdk)_DEFS)\
		$(SDK_$(sdk)_DEFS.$(bld_type))\
		$(SDK_$(sdk)_DEFS.$(bld_trg))\
		$(SDK_$(sdk)_DEFS.$(bld_trg_arch))\
		$(SDK_$(sdk)_DEFS.$(bld_trg).$(bld_trg_arch))\
		$(SDK_$(sdk)_DEFS.$(bld_trg_cpu))\
		$(SDK_$(sdk)_$(type)DEFS)\
		$(SDK_$(sdk)_$(type)DEFS.$(bld_type))\
		$(SDK_$(sdk)_$(type)DEFS.$(bld_trg))\
		$(SDK_$(sdk)_$(type)DEFS.$(bld_trg_arch))\
		$(SDK_$(sdk)_$(type)DEFS.$(bld_trg).$(bld_trg_arch))\
		$(SDK_$(sdk)_$(type)DEFS.$(bld_trg_cpu)))\
	$($(target)_$(source)_DEFS)\
	$($(target)_$(source)_DEFS.$(bld_type))\
	$($(target)_$(source)_DEFS.$(bld_trg))\
	$($(target)_$(source)_DEFS.$(bld_trg_arch))\
	$($(target)_$(source)_DEFS.$(bld_trg).$(bld_trg_arch))\
	$($(target)_$(source)_DEFS.$(bld_trg_cpu))\
	$($(target)_$(source)_$(type)DEFS)\
	$($(target)_$(source)_$(type)DEFS.$(bld_type))\
	$($(target)_$(source)_$(type)DEFS.$(bld_trg))\
	$($(target)_$(source)_$(type)DEFS.$(bld_trg_arch))\
	$($(target)_$(source)_$(type)DEFS.$(bld_trg).$(bld_trg_arch))\
	$($(target)_$(source)_$(type)DEFS.$(bld_trg_cpu))
*/
static struct variable *
kbuild_collect_source_prop(struct variable *pTarget, struct variable *pSource,
                           struct variable *pTool, struct kbuild_sdks *pSdks,
                           struct variable *pType, struct variable *pBldType,
                           struct variable *pBldTrg, struct variable *pBldTrgArch, struct variable *pBldTrgCpu,
                           struct variable *pDefPath,
                           const char *pszProp, size_t cchProp,
                           const char *pszVarName, size_t cchVarName,
                           int iDirection)
{
    struct variable *pVar;
    unsigned iSdk, iSdkEnd;
    int cVars, iVar;
    size_t cchTotal, cchBuf;
    char *pszResult, *pszBuf, *psz, *psz2, *psz3;
    struct
    {
        struct variable    *pVar;
        unsigned int        cchExp;
        char               *pszExp;
    } *paVars;

    assert(iDirection == 1 || iDirection == -1);

    /*
     * Calc and allocate a too big name buffer.
     */
    cchBuf = cchProp + 1
           + pTarget->value_length + 1
           + pSource->value_length + 1
           + pSdks->cchMax + 1
           + (pTool ? pTool->value_length + 1 : 0)
           + pType->value_length + 1
           + pBldTrg->value_length + 1
           + pBldTrgArch->value_length + 1
           + pBldTrgCpu->value_length + 1
           + pBldType->value_length + 1;
    pszBuf = xmalloc(cchBuf);

    /*
     * Get the variables.
     *
     * The compiler will get a heart attack when it sees this code ... ;-)
     */
    cVars = 12 * (pSdks->c + 5);
    paVars = alloca(cVars * sizeof(paVars[0]));

    iVar = 0;
    cchTotal = 0;

#define ADD_VAR(pVar)           do { my_memcpy(psz, (pVar)->value, (pVar)->value_length); psz += (pVar)->value_length; } while (0)
#define ADD_STR(pszStr, cchStr) do { my_memcpy(psz, (pszStr), (cchStr)); psz += (cchStr); } while (0)
#define ADD_CSTR(pszStr)        do { my_memcpy(psz, pszStr, sizeof(pszStr) - 1); psz += sizeof(pszStr) - 1; } while (0)
#define ADD_CH(ch)              do { *psz++ = (ch); } while (0)
#define DO_VAR_LOOKUP() \
    do { \
        pVar = kbuild_lookup_variable_n(pszBuf, psz - pszBuf); \
        if (pVar) \
        { \
            paVars[iVar].pVar = pVar; \
            if (    !pVar->recursive \
                ||  !memchr(pVar->value, '$', pVar->value_length)) \
            { \
                paVars[iVar].pszExp = pVar->value; \
                paVars[iVar].cchExp = pVar->value_length; \
                if (pDefPath && paVars[iVar].cchExp) \
                    kbuild_apply_defpath(pDefPath, &paVars[iVar].pszExp, &paVars[iVar].cchExp, NULL, 0); \
                if (paVars[iVar].cchExp) \
                { \
                    cchTotal += paVars[iVar].cchExp + 1; \
                    iVar++; \
                } \
            } \
            else \
            { \
                paVars[iVar].pszExp = allocated_variable_expand_2(pVar->value, pVar->value_length, &paVars[iVar].cchExp); \
                if (pDefPath && paVars[iVar].cchExp) \
                    kbuild_apply_defpath(pDefPath, &paVars[iVar].pszExp, &paVars[iVar].cchExp, NULL, 1); \
                if (paVars[iVar].cchExp) \
                { \
                    cchTotal += paVars[iVar].cchExp + 1; \
                    iVar++; \
                } \
                else \
                    free(paVars[iVar].pszExp); \
            } \
        } \
    } while (0)
#define DO_SINGLE_PSZ3_VARIATION() \
    do {                           \
       DO_VAR_LOOKUP();            \
                                   \
       ADD_CH('.');                \
       psz3 = psz;                 \
       ADD_VAR(pBldType);          \
       DO_VAR_LOOKUP();            \
                                   \
       psz = psz3;                 \
       ADD_VAR(pBldTrg);           \
       DO_VAR_LOOKUP();            \
                                   \
       psz = psz3;                 \
       ADD_VAR(pBldTrgArch);       \
       DO_VAR_LOOKUP();            \
                                   \
       psz = psz3;                 \
       ADD_VAR(pBldTrg);           \
       ADD_CH('.');                \
       ADD_VAR(pBldTrgArch);       \
       DO_VAR_LOOKUP();            \
                                   \
       psz = psz3;                 \
       ADD_VAR(pBldTrgCpu);        \
       DO_VAR_LOOKUP();            \
    } while (0)

#define DO_DOUBLE_PSZ2_VARIATION() \
    do {                           \
       psz2 = psz;                 \
       ADD_STR(pszProp, cchProp);  \
       DO_SINGLE_PSZ3_VARIATION(); \
                                   \
       /* add prop before type */  \
       psz = psz2;                 \
       ADD_VAR(pType);             \
       ADD_STR(pszProp, cchProp);  \
       DO_SINGLE_PSZ3_VARIATION(); \
    } while (0)

    /* the tool (lowest priority). */
    psz = pszBuf;
    ADD_CSTR("TOOL_");
    ADD_VAR(pTool);
    ADD_CH('_');
    DO_DOUBLE_PSZ2_VARIATION();


    /* the global sdks. */
    iSdkEnd = iDirection == 1 ? pSdks->iGlobal + pSdks->cGlobal : pSdks->iGlobal - 1;
    for (iSdk = iDirection == 1 ? pSdks->iGlobal : pSdks->iGlobal + pSdks->cGlobal - 1;
         iSdk != iSdkEnd;
         iSdk += iDirection)
    {
        struct variable *pSdk = &pSdks->pa[iSdk];
        psz = pszBuf;
        ADD_CSTR("SDK_");
        ADD_VAR(pSdk);
        ADD_CH('_');
        DO_DOUBLE_PSZ2_VARIATION();
    }

    /* the globals. */
    psz = pszBuf;
    DO_DOUBLE_PSZ2_VARIATION();


    /* the target sdks. */
    iSdkEnd = iDirection == 1 ? pSdks->iTarget + pSdks->cTarget : pSdks->iTarget - 1;
    for (iSdk = iDirection == 1 ? pSdks->iTarget : pSdks->iTarget + pSdks->cTarget - 1;
         iSdk != iSdkEnd;
         iSdk += iDirection)
    {
        struct variable *pSdk = &pSdks->pa[iSdk];
        psz = pszBuf;
        ADD_CSTR("SDK_");
        ADD_VAR(pSdk);
        ADD_CH('_');
        DO_DOUBLE_PSZ2_VARIATION();
    }

    /* the target. */
    psz = pszBuf;
    ADD_VAR(pTarget);
    ADD_CH('_');
    DO_DOUBLE_PSZ2_VARIATION();

    /* the source sdks. */
    iSdkEnd = iDirection == 1 ? pSdks->iSource + pSdks->cSource : pSdks->iSource - 1;
    for (iSdk = iDirection == 1 ? pSdks->iSource : pSdks->iSource + pSdks->cSource - 1;
         iSdk != iSdkEnd;
         iSdk += iDirection)
    {
        struct variable *pSdk = &pSdks->pa[iSdk];
        psz = pszBuf;
        ADD_CSTR("SDK_");
        ADD_VAR(pSdk);
        ADD_CH('_');
        DO_DOUBLE_PSZ2_VARIATION();
    }

    /* the source. */
    psz = pszBuf;
    ADD_VAR(pSource);
    ADD_CH('_');
    DO_DOUBLE_PSZ2_VARIATION();

    /* the target + source sdks. */
    iSdkEnd = iDirection == 1 ? pSdks->iTargetSource + pSdks->cTargetSource : pSdks->iTargetSource - 1;
    for (iSdk = iDirection == 1 ? pSdks->iTargetSource : pSdks->iTargetSource + pSdks->cTargetSource - 1;
         iSdk != iSdkEnd;
         iSdk += iDirection)
    {
        struct variable *pSdk = &pSdks->pa[iSdk];
        psz = pszBuf;
        ADD_CSTR("SDK_");
        ADD_VAR(pSdk);
        ADD_CH('_');
        DO_DOUBLE_PSZ2_VARIATION();
    }

    /* the target + source. */
    psz = pszBuf;
    ADD_VAR(pTarget);
    ADD_CH('_');
    ADD_VAR(pSource);
    ADD_CH('_');
    DO_DOUBLE_PSZ2_VARIATION();

    free(pszBuf);

    assert(iVar <= cVars);
    cVars = iVar;

    /*
     * Construct the result value.
     */
    if (!cVars || !cchTotal)
        pVar = define_variable_vl(pszVarName, cchVarName, "", 0,
                                  1 /* duplicate value */ , o_local, 0 /* !recursive */);
    else
    {
        psz = pszResult = xmalloc(cchTotal + 1);
        if (iDirection == 1)
        {
            for (iVar = 0; iVar < cVars; iVar++)
            {
                my_memcpy(psz, paVars[iVar].pszExp, paVars[iVar].cchExp);
                psz += paVars[iVar].cchExp;
                *psz++ = ' ';
                if (paVars[iVar].pszExp != paVars[iVar].pVar->value)
                    free(paVars[iVar].pszExp);
            }
        }
        else
        {
            iVar = cVars;
            while (iVar-- > 0)
            {
                my_memcpy(psz, paVars[iVar].pszExp, paVars[iVar].cchExp);
                psz += paVars[iVar].cchExp;
                *psz++ = ' ';
                if (paVars[iVar].pszExp != paVars[iVar].pVar->value)
                    free(paVars[iVar].pszExp);
            }

        }
        assert(psz != pszResult);
        assert(cchTotal == (size_t)(psz - pszResult));
        psz[-1] = '\0';
        cchTotal--;

        pVar = define_variable_vl(pszVarName, cchVarName, pszResult, cchTotal,
                                  0 /* take pszResult */ , o_local, 0 /* !recursive */);
    }

    return pVar;

#undef ADD_VAR
#undef ADD_STR
#undef ADD_CSTR
#undef ADD_CH
#undef DO_VAR_LOOKUP
#undef DO_DOUBLE_PSZ2_VARIATION
#undef DO_SINGLE_PSZ3_VARIATION
}


/* get a source property. */
char *
func_kbuild_source_prop(char *o, char **argv, const char *pszFuncName)
{
    struct variable *pTarget = kbuild_get_variable_n(ST("target"));
    struct variable *pSource = kbuild_get_variable_n(ST("source"));
    struct variable *pDefPath = NULL;
    struct variable *pType = kbuild_get_variable_n(ST("type"));
    struct variable *pTool = kbuild_get_variable_n(ST("tool"));
    struct variable *pBldType = kbuild_get_variable_n(ST("bld_type"));
    struct variable *pBldTrg = kbuild_get_variable_n(ST("bld_trg"));
    struct variable *pBldTrgArch = kbuild_get_variable_n(ST("bld_trg_arch"));
    struct variable *pBldTrgCpu = kbuild_get_variable_n(ST("bld_trg_cpu"));
    struct variable *pVar;
    struct kbuild_sdks Sdks;
    int iDirection;
    if (!strcmp(argv[2], "left-to-right"))
        iDirection = 1;
    else if (!strcmp(argv[2], "right-to-left"))
        iDirection = -1;
    else
        fatal(NILF, _("incorrect direction argument `%s'!"), argv[2]);
    if (argv[3])
    {
        const char *psz = argv[3];
        while (isspace(*psz))
            psz++;
        if (*psz)
            pDefPath = kbuild_get_variable_n(ST("defpath"));
    }

    kbuild_get_sdks(&Sdks, pTarget, pSource, pBldType, pBldTrg, pBldTrgArch);

    pVar = kbuild_collect_source_prop(pTarget, pSource, pTool, &Sdks, pType,
                                      pBldType, pBldTrg, pBldTrgArch, pBldTrgCpu,
                                      pDefPath,
                                      argv[0], strlen(argv[0]),
                                      argv[1], strlen(argv[1]),
                                      iDirection);
    if (pVar)
         o = variable_buffer_output(o, pVar->value, pVar->value_length);

    kbuild_put_sdks(&Sdks);
    (void)pszFuncName;
    return o;
}


/*
dep     := $(obj)$(SUFF_DEP)
obj     := $(outbase)$(objsuff)
dirdep  := $(call DIRDEP,$(dir $(outbase)))
PATH_$(target)_$(source) := $(patsubst %/,%,$(dir $(outbase)))
*/
static struct variable *
kbuild_set_object_name_and_dep_and_dirdep_and_PATH_target_source(struct variable *pTarget, struct variable *pSource,
                                                                 struct variable *pOutBase, struct variable *pObjSuff,
                                                                 const char *pszVarName, struct variable **ppDep,
                                                                 struct variable **ppDirDep)
{
    struct variable *pDepSuff = kbuild_get_variable_n(ST("SUFF_DEP"));
    struct variable *pObj;
    size_t cch = pOutBase->value_length + pObjSuff->value_length + pDepSuff->value_length + 1;
    char *pszResult = alloca(cch);
    char *pszName, *psz;

    /*
     * dep.
     */
    psz = pszResult;
    memcpy(psz, pOutBase->value, pOutBase->value_length);   psz += pOutBase->value_length;
    memcpy(psz, pObjSuff->value, pObjSuff->value_length);   psz += pObjSuff->value_length;
    memcpy(psz, pDepSuff->value, pDepSuff->value_length + 1);
    *ppDep = define_variable_vl("dep", 3, pszResult, cch - 1, 1 /*dup*/, o_local, 0 /* !recursive */);

    /*
     * obj
     */
    *psz = '\0';
    pObj = define_variable_vl(pszVarName, strlen(pszVarName), pszResult, psz - pszResult,
                              1/* dup */, o_local, 0 /* !recursive */);

    /*
     * PATH_$(target)_$(source) - this is global!
     */
    /* calc variable name. */
    cch = sizeof("PATH_")-1 + pTarget->value_length + sizeof("_")-1 + pSource->value_length;
    psz = pszName = alloca(cch + 1);
    memcpy(psz, "PATH_", sizeof("PATH_") - 1);          psz += sizeof("PATH_") - 1;
    memcpy(psz, pTarget->value, pTarget->value_length); psz += pTarget->value_length;
    *psz++ = '_';
    memcpy(psz, pSource->value, pSource->value_length + 1);

    /* strip off the filename. */
    psz = pszResult + pOutBase->value_length;
    for (;;)
    {
        psz--;
        if (psz <= pszResult)
            fatal(NULL, "whut!?! no path? result=`%s'", pszResult);
#ifdef HAVE_DOS_PATHS
        if (*psz == ':')
        {
            psz++;
            break;
        }
#endif
        if (    *psz == '/'
#ifdef HAVE_DOS_PATHS
            ||  *psz == '\\'
#endif
           )
        {
            while (     psz - 1 > pszResult
                   &&   psz[-1] == '/'
#ifdef HAVE_DOS_PATHS
                   ||   psz[-1] == '\\'
#endif
                  )
                psz--;
#ifdef HAVE_DOS_PATHS
            if (psz == pszResult + 2 && pszResult[1] == ':')
                psz++;
#endif
            break;
        }
    }
    *psz = '\0';

    /* set global variable */
    define_variable_vl_global(pszName, cch, pszResult, psz - pszResult, 1/*dup*/, o_file, 0 /* !recursive */, NILF);

    /*
     * dirdep
     */
    if (    psz[-1] != '/'
#ifdef HAVE_DOS_PATHS
        &&  psz[-1] != '\\'
        &&  psz[-1] != ':'
#endif
       )
    {
        *psz++ = '/';
        *psz = '\0';
    }
    *ppDirDep = define_variable_vl("dirdep", 6, pszResult, psz - pszResult, 1/*dup*/, o_local, 0 /* !recursive */);

    return pObj;
}


/* setup the base variables for def_target_source_c_cpp_asm_new:

X := $(kb-src-tool tool)
x := $(kb-obj-base outbase)
x := $(kb-obj-suff objsuff)
obj     := $(outbase)$(objsuff)
PATH_$(target)_$(source) := $(patsubst %/,%,$(dir $(outbase)))

x := $(kb-src-prop DEFS,defs,left-to-right)
x := $(kb-src-prop INCS,incs,right-to-left)
x := $(kb-src-prop FLAGS,flags,right-to-left)

x := $(kb-src-prop DEPS,deps,left-to-right)
dirdep  := $(call DIRDEP,$(dir $(outbase)))
dep     := $(obj)$(SUFF_DEP)
*/
char *
func_kbuild_source_one(char *o, char **argv, const char *pszFuncName)
{
    static int s_fNoCompileCmdsDepsDefined = -1;
    struct variable *pTarget    = kbuild_get_variable_n(ST("target"));
    struct variable *pSource    = kbuild_get_variable_n(ST("source"));
    struct variable *pDefPath   = kbuild_get_variable_n(ST("defpath"));
    struct variable *pType      = kbuild_get_variable_n(ST("type"));
    struct variable *pBldType   = kbuild_get_variable_n(ST("bld_type"));
    struct variable *pBldTrg    = kbuild_get_variable_n(ST("bld_trg"));
    struct variable *pBldTrgArch= kbuild_get_variable_n(ST("bld_trg_arch"));
    struct variable *pBldTrgCpu = kbuild_get_variable_n(ST("bld_trg_cpu"));
    struct variable *pTool      = kbuild_get_source_tool(pTarget, pSource, pType, pBldTrg, pBldTrgArch, "tool");
    struct variable *pOutBase   = kbuild_get_object_base(pTarget, pSource, "outbase");
    struct variable *pObjSuff   = kbuild_get_object_suffix(pTarget, pSource, pTool, pType, pBldTrg, pBldTrgArch, "objsuff");
    struct variable *pDefs, *pIncs, *pFlags, *pDeps, *pOrderDeps, *pDirDep, *pDep, *pVar, *pOutput, *pOutputMaybe;
    struct variable *pObj       = kbuild_set_object_name_and_dep_and_dirdep_and_PATH_target_source(pTarget, pSource, pOutBase, pObjSuff, "obj", &pDep, &pDirDep);
    char *pszDstVar, *pszDst, *pszSrcVar, *pszSrc, *pszVal, *psz;
    char *pszSavedVarBuf;
    unsigned cchSavedVarBuf;
    size_t cch;
    struct kbuild_sdks Sdks;
    int iVer;

    /*
     * argv[0] is the function version. Prior to r1792 (early 0.1.5) this
     * was undefined and footer.kmk always passed an empty string.
     *
     * Version 2, as implemented in r1797, will make use of the async
     * includedep queue feature. This means the files will be read by one or
     * more background threads, leaving the eval'ing to be done later on by
     * the main thread (in snap_deps).
     */
    if (!argv[0][0])
        iVer = 0;
    else
        switch (argv[0][0] | (argv[0][1] << 8))
        {
            case '2': iVer = 2; break;
            case '3': iVer = 3; break;
            case '4': iVer = 4; break;
            case '5': iVer = 5; break;
            case '6': iVer = 6; break;
            case '7': iVer = 7; break;
            case '8': iVer = 8; break;
            case '9': iVer = 9; break;
            case '0': iVer = 0; break;
            case '1': iVer = 1; break;
            default:
                iVer = 0;
                psz = argv[0];
                while (isblank((unsigned char)*psz))
                    psz++;
                if (*psz)
                    iVer = atoi(psz);
                break;
        }

    /*
     * Gather properties.
     */
    kbuild_get_sdks(&Sdks, pTarget, pSource, pBldType, pBldTrg, pBldTrgArch);

    if (pDefPath && !pDefPath->value_length)
        pDefPath = NULL;
    pDefs      = kbuild_collect_source_prop(pTarget, pSource, pTool, &Sdks, pType, pBldType, pBldTrg, pBldTrgArch, pBldTrgCpu, NULL,
                                            ST("DEFS"),  ST("defs"), 1/* left-to-right */);
    pIncs      = kbuild_collect_source_prop(pTarget, pSource, pTool, &Sdks, pType, pBldType, pBldTrg, pBldTrgArch, pBldTrgCpu, pDefPath,
                                            ST("INCS"),  ST("incs"), -1/* right-to-left */);
    pFlags     = kbuild_collect_source_prop(pTarget, pSource, pTool, &Sdks, pType, pBldType, pBldTrg, pBldTrgArch, pBldTrgCpu, NULL,
                                            ST("FLAGS"), ST("flags"), 1/* left-to-right */);
    pDeps      = kbuild_collect_source_prop(pTarget, pSource, pTool, &Sdks, pType, pBldType, pBldTrg, pBldTrgArch, pBldTrgCpu, pDefPath,
                                            ST("DEPS"),  ST("deps"), 1/* left-to-right */);
    pOrderDeps = kbuild_collect_source_prop(pTarget, pSource, pTool, &Sdks, pType, pBldType, pBldTrg, pBldTrgArch, pBldTrgCpu, pDefPath,
                                            ST("ORDERDEPS"), ST("orderdeps"), 1/* left-to-right */);

    /*
     * If we've got a default path, we must expand the source now.
     * If we do this too early, "<source>_property = stuff" won't work becuase
     * our 'source' value isn't what the user expects.
     */
    if (pDefPath)
    {
        /** @todo assert(pSource->origin != o_automatic);  We're changing 'source'
         *        from the foreach loop!  */
#ifdef CONFIG_WITH_RDONLY_VARIABLE_VALUE
        assert(!pSource->rdonly_val);
#endif
        kbuild_apply_defpath(pDefPath, &pSource->value, (unsigned int *)&pSource->value_length, &pSource->value_alloc_len, 1 /* can free */);
    }

    /*
    # dependencies
    ifndef NO_COMPILE_CMDS_DEPS
    _DEPFILES_INCLUDED += $(dep)
    $(if $(wildcard $(dep)),$(eval include $(dep)))
    endif
     */
    if (s_fNoCompileCmdsDepsDefined == -1)
        s_fNoCompileCmdsDepsDefined = kbuild_lookup_variable_n(ST("NO_COMPILE_CMDS_DEPS")) != NULL;
    if (!s_fNoCompileCmdsDepsDefined)
    {
        do_variable_definition_2(NILF, "_DEPFILES_INCLUDED", pDep->value, pDep->value_length,
                                 !pDep->recursive, 0, o_file, f_append, 0 /* !target_var */);
        eval_include_dep(pDep->value, NILF, iVer >= 2 ? incdep_queue : incdep_read_it);
    }

    /*
    # call the tool
    $(target)_$(source)_CMDS_   := $(TOOL_$(tool)_COMPILE_$(type)_CMDS)
    $(target)_$(source)_OUTPUT_ := $(TOOL_$(tool)_COMPILE_$(type)_OUTPUT)
    $(target)_$(source)_OUTPUT_MAYBE_ := $(TOOL_$(tool)_COMPILE_$(type)_OUTPUT_MAYBE)
    $(target)_$(source)_DEPEND_ := $(TOOL_$(tool)_COMPILE_$(type)_DEPEND) $(deps) $(source)
    $(target)_$(source)_DEPORD_ := $(TOOL_$(tool)_COMPILE_$(type)_DEPORD) $(dirdep)
    */
    cch = sizeof("TOOL_") + pTool->value_length + sizeof("_COMPILE_") + pType->value_length + sizeof("_OUTPUT_MAYBE");
    psz = pszSrcVar = alloca(cch);
    memcpy(psz, "TOOL_", sizeof("TOOL_") - 1);          psz += sizeof("TOOL_") - 1;
    memcpy(psz, pTool->value, pTool->value_length);     psz += pTool->value_length;
    memcpy(psz, "_COMPILE_", sizeof("_COMPILE_") - 1);  psz += sizeof("_COMPILE_") - 1;
    memcpy(psz, pType->value, pType->value_length);     psz += pType->value_length;
    pszSrc = psz;

    cch = pTarget->value_length + 1 + pSource->value_length + sizeof("_OUTPUT_MAYBE_");
    psz = pszDstVar = alloca(cch);
    memcpy(psz, pTarget->value, pTarget->value_length); psz += pTarget->value_length;
    *psz++ = '_';
    memcpy(psz, pSource->value, pSource->value_length); psz += pSource->value_length;
    pszDst = psz;

    memcpy(pszSrc, "_CMDS", sizeof("_CMDS"));
    memcpy(pszDst, "_CMDS_", sizeof("_CMDS_"));
    pVar = kbuild_get_recursive_variable(pszSrcVar);
    do_variable_definition_2(NILF, pszDstVar, pVar->value, pVar->value_length,
                             !pVar->recursive, 0, o_file, f_simple, 0 /* !target_var */);

    memcpy(pszSrc, "_OUTPUT", sizeof("_OUTPUT"));
    memcpy(pszDst, "_OUTPUT_", sizeof("_OUTPUT_"));
    pVar = kbuild_get_recursive_variable(pszSrcVar);
    pOutput = do_variable_definition_2(NILF, pszDstVar, pVar->value, pVar->value_length,
                                       !pVar->recursive, 0, o_file, f_simple, 0 /* !target_var */);

    memcpy(pszSrc, "_OUTPUT_MAYBE", sizeof("_OUTPUT_MAYBE"));
    memcpy(pszDst, "_OUTPUT_MAYBE_", sizeof("_OUTPUT_MAYBE_"));
    pVar = kbuild_query_recursive_variable(pszSrcVar);
    if (pVar)
        pOutputMaybe = do_variable_definition_2(NILF, pszDstVar, pVar->value, pVar->value_length,
                                                !pVar->recursive, 0, o_file, f_simple, 0 /* !target_var */);
    else
        pOutputMaybe = do_variable_definition_2(NILF, pszDstVar, "", 0, 1, 0, o_file, f_simple, 0 /* !target_var */);

    memcpy(pszSrc, "_DEPEND", sizeof("_DEPEND"));
    memcpy(pszDst, "_DEPEND_", sizeof("_DEPEND_"));
    pVar = kbuild_get_recursive_variable(pszSrcVar);
    psz = pszVal = xmalloc(pVar->value_length + 1 + pDeps->value_length + 1 + pSource->value_length + 1);
    memcpy(psz, pVar->value, pVar->value_length);       psz += pVar->value_length;
    *psz++ = ' ';
    memcpy(psz, pDeps->value, pDeps->value_length);     psz += pDeps->value_length;
    *psz++ = ' ';
    memcpy(psz, pSource->value, pSource->value_length + 1);
    do_variable_definition_2(NILF, pszDstVar, pszVal, pVar->value_length + 1 + pDeps->value_length + 1 + pSource->value_length,
                             !pVar->recursive && !pDeps->recursive && !pSource->recursive,
                             pszVal, o_file, f_simple, 0 /* !target_var */);

    memcpy(pszSrc, "_DEPORD", sizeof("_DEPORD"));
    memcpy(pszDst, "_DEPORD_", sizeof("_DEPORD_"));
    pVar = kbuild_get_recursive_variable(pszSrcVar);
    psz = pszVal = xmalloc(pVar->value_length + 1 + pDirDep->value_length + 1 + pOrderDeps->value_length + 1);
    memcpy(psz, pVar->value, pVar->value_length);       psz += pVar->value_length;
    *psz++ = ' ';
    memcpy(psz, pDirDep->value, pDirDep->value_length); psz += pDirDep->value_length;
    *psz++ = ' ';
    memcpy(psz, pOrderDeps->value, pOrderDeps->value_length + 1);
    do_variable_definition_2(NILF, pszDstVar, pszVal,
                             pVar->value_length + 1 + pDirDep->value_length + 1 + pOrderDeps->value_length,
                             !pVar->recursive && !pDirDep->recursive && !pOrderDeps->recursive,
                             pszVal, o_file, f_simple, 0 /* !target_var */);

    /*
    _OUT_FILES      += $($(target)_$(source)_OUTPUT_) $($(target)_$(source)_OUTPUT_MAYBE_)
    */
    /** @todo use append? */
    pVar = kbuild_get_variable_n(ST("_OUT_FILES"));
    psz = pszVal = xmalloc(pVar->value_length + 1 + pOutput->value_length + 1 + pOutputMaybe->value_length + 1);
    memcpy(psz, pVar->value, pVar->value_length); psz += pVar->value_length;
    *psz++ = ' ';
    memcpy(psz, pOutput->value, pOutput->value_length); psz += pOutput->value_length;
    *psz++ = ' ';
    memcpy(psz, pOutputMaybe->value, pOutputMaybe->value_length + 1);
    do_variable_definition_2(NILF, "_OUT_FILES", pszVal,
                             pVar->value_length + 1 + pOutput->value_length + 1 + pOutputMaybe->value_length,
                             !pVar->recursive && !pOutput->recursive && !pOutputMaybe->recursive,
                             pszVal, o_file, f_simple, 0 /* !target_var */);

    /*
    $(target)_OBJS_ += $(obj)
    */
    memcpy(pszDstVar + pTarget->value_length, "_OBJS_", sizeof("_OBJS_"));
    do_variable_definition_2(NILF, pszDstVar, pObj->value, pObj->value_length,
                             !pObj->recursive, 0, o_file, f_append, 0 /* !target_var */);

    /*
    $(eval $(def_target_source_rule))
    */
    pVar = kbuild_get_recursive_variable("def_target_source_rule");
    pszVal = variable_expand_string_2 (o, pVar->value, pVar->value_length, &psz);
    assert (!((size_t)pszVal & 3));

    install_variable_buffer(&pszSavedVarBuf, &cchSavedVarBuf);
    eval_buffer(pszVal, psz);
    restore_variable_buffer(pszSavedVarBuf, cchSavedVarBuf);

    kbuild_put_sdks(&Sdks);
    (void)pszFuncName;

    *pszVal = '\0';
    return pszVal;
}


#endif /* KMK_HELPERS */

