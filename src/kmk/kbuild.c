/* $Id: kbuild.c 3425 2020-08-21 12:45:06Z knut.osmundsen@oracle.com $ */
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

/* No GNU coding style here! */

/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
#define NO_MEMCOPY_HACK
#include "makeint.h"
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
#if defined(__FreeBSD__)
# include <dlfcn.h>
# include <sys/link_elf.h>
#endif

#include "kbuild.h"
#include "k/kDefs.h"

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
                case 8: dst[7] = src[7]; /* fall thru */ \
                case 7: dst[6] = src[6]; /* fall thru */ \
                case 6: dst[5] = src[5]; /* fall thru */ \
                case 5: dst[4] = src[4]; /* fall thru */ \
                case 4: dst[3] = src[3]; /* fall thru */ \
                case 3: dst[2] = src[2]; /* fall thru */ \
                case 2: dst[1] = src[1]; /* fall thru */ \
                case 1: dst[0] = src[0]; /* fall thru */ \
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
        O(fatal, NILF, _("getcwd failed"));

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
    {
        rc = -1;
# if 0 /* doesn't work because l_name isn't always absolute, it's just argv0 from exec or something. */
        /* /proc is optional, try rtdl. */
        void *hExe = dlopen(NULL, 0);
        rc = -1;
        if (hExe)
        {
            struct link_map const *pLinkMap = 0;
            if (dlinfo(hExe, RTLD_DI_LINKMAP, &pLinkMap) == 0)
            {
                const char *pszImageName = pLinkMap->l_name;
                size_t cchImageName = strlen(pszImageName);
                if (cchImageName < GET_PATH_MAX)
                {
                    memcpy(szTmp, pszImageName, cchImageName + 1);
                    rc = 0;
                }
            }

        }
# endif
    }
    else
        szTmp[rc] = '\0';

#elif defined(__gnu_linux__) || defined(__linux__)
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
        snprintf(szTmp2, sizeof(szTmp2), "/proc/%ld/path/a.out", (long)getpid());
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
            pszEnvVar = getenv("PATH_KBUILD");
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
                    O(fatal, NILF, _("failed to determin KBUILD_PATH"));
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
            pszEnvVar = getenv("PATH_KBUILD_BIN");
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
                    OSS(fatal, NILF, _("failed to determin KBUILD_BIN_PATH (pszTmp2=%s szTmpPath=%s)"), pszTmp2, szTmpPath);
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
kbuild_apply_defpath(struct variable *pDefPath, char **ppsz, unsigned int *pcch, unsigned int *pcchAlloc, int fCanFree)
{
    unsigned int cchInCur;
    unsigned int cchMaxRelative = 0;
    const char *pszInCur;

    /*
     * The first pass, count the relative paths.
     */
    const char *pszIterator = *ppsz;
    const char * const pszEos = pszIterator + *pcch;
    unsigned int cRelativePaths = 0;
    assert(*pszEos == '\0');
    while ((pszInCur = find_next_file_token(&pszIterator, pszEos, &cchInCur)) != NULL)
    {
        /* is relative? */
#ifdef HAVE_DOS_PATHS
        if (pszInCur[0] != '/' && pszInCur[0] != '\\' && (cchInCur < 2 || pszInCur[1] != ':'))
#else
        if (pszInCur[0] != '/')
#endif
        {
            cRelativePaths++;
            if (cchInCur > cchMaxRelative)
                cchMaxRelative = cchInCur;
        }
    }

    /*
     * The second pass construct the new string.
     */
    if (cRelativePaths)
    {
        size_t const cchAbsPathBuf = MAX(GET_PATH_MAX, pDefPath->value_length + cchInCur + 1 + 16);
        char *pszAbsPathOut = (char *)alloca(cchAbsPathBuf);
        char *pszAbsPathIn  = (char *)alloca(cchAbsPathBuf);
        size_t cchAbsDefPath;
        size_t cchOut;
        char *pszOut;
        char *pszOutCur;
        const char *pszInNextCopy = *ppsz;

        /* make defpath absolute and have a trailing slash first. */
        if (abspath(pDefPath->value, pszAbsPathIn) == NULL)
            memcpy(pszAbsPathIn, pDefPath->value, pDefPath->value_length);
        cchAbsDefPath = strlen(pszAbsPathIn);
#ifdef HAVE_DOS_PATHS
        if (pszAbsPathIn[cchAbsDefPath - 1] != '/' && pszAbsPathIn[cchAbsDefPath - 1] != '\\')
#else
        if (pszAbsPathIn[cchAbsDefPath - 1] != '/')
#endif
            pszAbsPathIn[cchAbsDefPath++] = '/';

        cchOut = *pcch + cRelativePaths * cchAbsDefPath + 1;
        pszOutCur = pszOut = xmalloc(cchOut);

        cRelativePaths = 0;
        pszIterator = *ppsz;
        while ((pszInCur = find_next_file_token(&pszIterator, pszEos, &cchInCur)))
        {
            /* is relative? */
#ifdef HAVE_DOS_PATHS
            if (pszInCur[0] != '/' && pszInCur[0] != '\\' && (cchInCur < 2 || pszInCur[1] != ':'))
#else
            if (pszInCur[0] != '/')
#endif
            {
                const char *pszToCopy;
                size_t      cchToCopy;

                /* Create the abspath input. */
                memcpy(&pszAbsPathIn[cchAbsDefPath], pszInCur, cchInCur);
                pszAbsPathIn[cchAbsDefPath + cchInCur] = '\0';

                pszToCopy = abspath(pszAbsPathIn, pszAbsPathOut);
                if (!pszToCopy)
                    pszToCopy = pszAbsPathIn;

                /* copy leading input */
                if (pszInCur != pszInNextCopy)
                {
                    const size_t cchCopy = pszInCur - pszInNextCopy;
                    memcpy(pszOutCur, pszInNextCopy, cchCopy);
                    pszOutCur += cchCopy;
                }
                pszInNextCopy = pszInCur + cchInCur;

                /* copy out the abspath. */
                cchToCopy = strlen(pszToCopy);
                assert(cchToCopy <= cchAbsDefPath + cchInCur);
                memcpy(pszOutCur, pszToCopy, cchToCopy);
                pszOutCur += cchToCopy;
            }
            /* else: Copy absolute paths as bulk when we hit then next relative one or the end. */
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
    struct variable *pVar = lookup_variable(pszName, cchName);
    if (!pVar)
        fatal(NILF, cchName, _("variable `%.*s' isn't defined!"), (int)cchName, pszName);
    if (pVar->recursive)
        fatal(NILF, cchName, _("variable `%.*s' is defined as `recursive' instead of `simple'!"), (int)cchName, pszName);

    MY_ASSERT_MSG(strlen(pVar->value) == pVar->value_length,
                  ("%u != %u %.*s\n", pVar->value_length, (unsigned int)strlen(pVar->value), (int)cchName, pVar->name));
    return pVar;
}


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
    struct variable *pVar = lookup_variable(pszName, strlen(pszName));
    if (!pVar)
        OS(fatal, NILF, _("variable `%s' isn't defined!"), pszName);

    MY_ASSERT_MSG(strlen(pVar->value) == pVar->value_length,
                  ("%u != %u %s\n", pVar->value_length, (unsigned int)strlen(pVar->value), pVar->name));
    return pVar;
}


/**
 * Gets a variable that doesn't have to exit, but if it does can be recursive.
 *
 * @returns Pointer to the variable.
 *          NULL if not found.
 * @param   pszName     The variable name. Doesn't need to be terminated.
 * @param   cchName     The name length.
 */
static struct variable *
kbuild_query_recursive_variable_n(const char *pszName, size_t cchName)
{
    struct variable *pVar = lookup_variable(pszName, cchName);
    MY_ASSERT_MSG(!pVar || strlen(pVar->value) == pVar->value_length,
                  ("%u != %u %.*s\n", pVar->value_length, (unsigned int)strlen(pVar->value), (int)cchName, pVar->name));
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
    return kbuild_query_recursive_variable_n(pszName, strlen(pszName));
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
        assert(pVar->origin != o_automatic);
        pVar->value = pszExpanded;
        pVar->value_length = value_len;
        pVar->value_alloc_len = value_len + 1;
    }
    pVar->recursive = 0;
    VARIABLE_CHANGED(pVar);
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
        MY_ASSERT_MSG(strlen(pVar->value) == pVar->value_length,
                      ("%u != %u %.*s\n", pVar->value_length, (unsigned int)strlen(pVar->value), (int)cchName, pVar->name));

        /* Make sure the variable is simple, convert it if necessary.
           Note! Must NOT do this for the dynamic INCS of sdks/ReorderCompilerIncs.kmk */
        if (!pVar->recursive)
        { /* likely */ }
        else if (   cchName < sizeof("SDK_ReorderCompilerIncs_INCS.") - 1U
                 || pszName[0] != 'S'
                 || pszName[4] != 'R'
                 || memcmp(pszName, "SDK_ReorderCompilerIncs_INCS.", sizeof("SDK_ReorderCompilerIncs_INCS.") - 1U) == 0)
            kbuild_simplify_variable(pVar);
    }
    return pVar;
}


/**
 * Looks up an non-empty variable when simplified and spaces skipped.
 *
 * This handy when emulating $(firstword )/$(lastword ) behaviour.
 *
 * @returns Pointer to the variable. NULL if not found.
 * @param   pszName     The variable name.
 * @param   cchName     The name length.
 */
MY_INLINE struct variable *
kbuild_lookup_not_empty_variable_n(const char *pszName, size_t cchName)
{
    struct variable *pVar = kbuild_lookup_variable_n(pszName, cchName);
    if (pVar && !pVar->recursive)
    {
        /*
         * Skip spaces and make sure it's non-zero.
         */
        char *psz = pVar->value;
        if (!ISSPACE(*psz))
        { /* kind of likely */ }
        else
            do
                psz++;
            while (ISSPACE(*psz));

        if (*psz)
        { /*kind of likely */ }
        else
            pVar = NULL;
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
        kbuild_apply_defpath(pDefPath, &pVar->value, &pVar->value_length, &pVar->value_alloc_len, 1);
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
        kbuild_apply_defpath(pDefPath, &pVar->value, &pVar->value_length, &pVar->value_alloc_len, 1);
    }
    return pVar;
}


/**
 * Gets the first defined property variable.
 *
 * When pBldType is given, additional property variations are consulted.
 * See _TARGET_TOOL/r2433 in footer.kmk for the target-level difference.
 * Similar extended property lookup is applied to the source part if the
 * fExtendedSource parameter is non-zero (not done yet as it'll be expensive).
 *
 * Since r3415 this function will use $(target)_2_$(type)TOOL to cache the
 * result of the target specific part of the lookup (_TARGET_TOOL).
 */
static struct variable *
kbuild_first_prop(struct variable *pTarget, struct variable *pSource,
                  struct variable *pTool, struct variable *pType,
                  struct variable *pBldTrg, struct variable *pBldTrgArch, struct variable *pBldType,
                  const char *pszPropF1, char cchPropF1,
                  const char *pszPropF2, char cchPropF2,
                  int fExtendedSource,
                  const char *pszVarName)
{
    struct variable *pVar;
    size_t cchBuf;
    char *pszBuf;
    char *psz, *psz1, *psz2, *psz3, *psz4, *pszEnd;
    int fCacheIt = 1;

    fExtendedSource = fExtendedSource && pBldType != NULL;

    /* calc and allocate a too big name buffer. */
    cchBuf = cchPropF2 + 1
           + cchPropF1 + 1
           + pTarget->value_length + 1
           + pSource->value_length + 1
           + (pTool ? pTool->value_length + 1 : 0)
           + pType->value_length + 1
           + pBldTrg->value_length + 1
           + pBldTrgArch->value_length + 1
           + (pBldType ? pBldType->value_length + 1 : 0);
    pszBuf = xmalloc(cchBuf);

#define ADD_VAR(pVar)           do { my_memcpy(psz, (pVar)->value, (pVar)->value_length); psz += (pVar)->value_length; } while (0)
#define ADD_STR(pszStr, cchStr) do { my_memcpy(psz, (pszStr), (cchStr)); psz += (cchStr); } while (0)
#define ADD_CSTR(pszStr)        do { my_memcpy(psz, pszStr, sizeof(pszStr) - 1); psz += sizeof(pszStr) - 1; } while (0)
#define ADD_CH(ch)              do { *psz++ = (ch); } while (0)

    /*
     * $(target)_$(source)_$(type)$(propf2).$(bld_trg).$(bld_trg_arch).$(bld_type)
     *   ...
     * $(target)_$(source)_$(type)$(propf2)
     */
    psz = pszBuf;
    ADD_VAR(pTarget);
    ADD_CH('_');
    ADD_VAR(pSource);
    ADD_CH('_');
    psz1 = psz;
    ADD_VAR(pType);
    ADD_STR(pszPropF2, cchPropF2);
#define DO_VARIATIONS(a_fExtended) do { \
    psz2 = psz; \
    ADD_CH('.'); \
    ADD_VAR(pBldTrg); \
    psz3 = psz; \
    ADD_CH('.'); \
    ADD_VAR(pBldTrgArch); \
    psz4 = psz; \
    if ((a_fExtended) && pBldType) \
    { \
        ADD_CH('.'); \
        ADD_VAR(pBldType); \
        pVar = kbuild_lookup_not_empty_variable_n(pszBuf, psz - pszBuf); \
        \
        /* <lead>.$(bld_trg).$(bld_trg_arch) */ \
        if (!pVar) \
            pVar = kbuild_lookup_not_empty_variable_n(pszBuf, psz4 - pszBuf); \
        \
        /* <lead>.$(bld_trg).$(bld_type) */ \
        if (!pVar) \
        { \
            psz = psz3 + 1; \
            ADD_VAR(pBldType); \
            pVar = kbuild_lookup_not_empty_variable_n(pszBuf, psz - pszBuf); \
        } \
        \
        /* <lead>.$(bld_trg_arch) */ \
        if (!pVar) \
        { \
            psz = psz2 + 1; \
            ADD_VAR(pBldTrgArch); \
            pVar = kbuild_lookup_not_empty_variable_n(pszBuf, psz - pszBuf); \
        } \
        \
        /* <lead>.$(bld_trg) */ \
        if (!pVar) \
        { \
            psz = psz2 + 1; \
            ADD_VAR(pBldTrg); \
            pVar = kbuild_lookup_not_empty_variable_n(pszBuf, psz - pszBuf); \
        } \
        \
        /* <lead>.$(bld_type) */ \
        if (!pVar) \
        { \
            psz = psz2 + 1; \
            ADD_VAR(pBldType); \
            pVar = kbuild_lookup_not_empty_variable_n(pszBuf, psz - pszBuf); \
        } \
    } \
    else \
    { \
        /* <lead>.$(bld_trg).$(bld_trg_arch) */ \
        pVar = kbuild_lookup_not_empty_variable_n(pszBuf, psz4 - pszBuf); \
        \
        /* <lead>.$(bld_trg) */ \
        if (!pVar) \
            pVar = kbuild_lookup_not_empty_variable_n(pszBuf, psz3 - pszBuf); \
    } \
    \
    /* <lead> */ \
    if (!pVar) \
        pVar = kbuild_lookup_not_empty_variable_n(pszBuf, psz2 - pszBuf); \
} while (0)
    DO_VARIATIONS(fExtendedSource);

    /*
     * $(target)_$(source)_$(propf2).$(bld_trg).$(bld_trg_arch).$(bld_type) [omit $(type) prefix to $(propf2)]
     *   ...
     * $(target)_$(source)_$(propf2)
     */
    if (!pVar)
    {
        psz = psz1; /* rewind to '$(target)_$(source)_' */
        ADD_STR(pszPropF2, cchPropF2);
        DO_VARIATIONS(fExtendedSource);
    }

    /*
     * $(source)_$(type)$(propf2).$(bld_trg).$(bld_trg_arch).$(bld_type)
     *   ...
     * $(source)_$(type)$(propf2)
     */
    if (!pVar)
    {
        psz = pszBuf;
        ADD_VAR(pSource);
        ADD_CH('_');
        psz1 = psz;
        ADD_VAR(pType);
        ADD_STR(pszPropF2, cchPropF2);
        DO_VARIATIONS(fExtendedSource);

        /*
         * $(source)_$(propf2).$(bld_trg).$(bld_trg_arch).$(bld_type)  [omit $(type) prefix to $(propf2)]
         *   ...
         * $(source)_$(propf2)
         */
        if (!pVar)
        {
            psz = psz1; /* rewind to '$(source)_' */
            ADD_STR(pszPropF2, cchPropF2);
            DO_VARIATIONS(fExtendedSource);
        }
    }

    /*
     * Check the cache: $(target)_2_$(type)$(propf2)
     */
    if (pVar)
        fCacheIt = 0;
    else if (fCacheIt)
    {
        psz = pszBuf;
        ADD_VAR(pTarget);
        ADD_STR("_2_", 3);
        ADD_VAR(pType);
        ADD_STR(pszPropF2, cchPropF2);
        pVar = kbuild_lookup_variable_n(pszBuf, psz - pszBuf);

        /* If found, this can be duplicated and returned directly. No value stripping
           needed as we defined it (or at least should have) ourselves. */
        if (pVar)
        {
            pVar = define_variable_vl(pszVarName, strlen(pszVarName), pVar->value, pVar->value_length,
                                      1 /* duplicate */, o_local, 0 /* !recursive */);
            free(pszBuf);
            return pVar;
        }
    }

    /*
     * $(target)_$(type)$(propf2).$(bld_trg).$(bld_trg_arch).$(bld_type)
     *   ...
     * $(target)_$(type)$(propf2)
     */
    if (!pVar)
    {
        psz = pszBuf;
        ADD_VAR(pTarget);
        ADD_CH('_');
        psz1 = psz;
        ADD_VAR(pType);
        ADD_STR(pszPropF2, cchPropF2);
        DO_VARIATIONS(1);

        /*
         * $(target)_$(propf2).$(bld_trg).$(bld_trg_arch).$(bld_type)  [omit $(type) prefix to $(propf2)]
         *   ...
         * $(target)_$(propf2)
         */
        if (!pVar)
        {
            psz = psz1; /* rewind to '$(target)_' */
            ADD_STR(pszPropF2, cchPropF2);
            DO_VARIATIONS(1);
        }
    }

    /*
     * TOOL_$(tool)_$(type)$(propf2).$(bld_trg).$(bld_trg_arch).$(bld_type)
     *   ...
     * TOOL_$(tool)_$(type)$(propf2)
     */
    if (!pVar && pTool)
    {
        psz = pszBuf;
        ADD_CSTR("TOOL_");
        ADD_VAR(pTool);
        ADD_CH('_');
        psz1 = psz;
        ADD_VAR(pType);
        ADD_STR(pszPropF2, cchPropF2);
        DO_VARIATIONS(1);

        /*
         * TOOL_$(tool)_$(propf2).$(bld_trg).$(bld_trg_arch).$(bld_type)  [omit $(type) prefix to $(propf2)]
         *   ...
         * TOOL_$(tool)_$(propf2)
         */
        if (!pVar)
        {
            psz = psz1; /* rewind to 'TOOL_$(tool)_' */
            ADD_STR(pszPropF2, cchPropF2);
            DO_VARIATIONS(1);
        }
    }

    /*
     * $(type)$(propf1).$(bld_trg).$(bld_trg_arch).$(bld_type)
     *   ...
     * $(type)$(propf1)
     */
    if (!pVar)
    {
        psz = pszBuf;
        ADD_VAR(pType);
        ADD_STR(pszPropF1, cchPropF1);
        DO_VARIATIONS(1);

        /*
         * $(propf1).$(bld_trg).$(bld_trg_arch).$(bld_type)
         *   ...
         * $(propf1)
         */
        if (!pVar)
        {
            psz = pszBuf;
            ADD_STR(pszPropF1, cchPropF1);
            DO_VARIATIONS(1);
        }
    }

    /*
     * Done!
     */
    if (pVar)
    {
        /* strip it */
        psz = pVar->value;
        pszEnd = psz + pVar->value_length;
        while (ISBLANK(*psz))
            psz++;
        while (pszEnd > psz && ISBLANK(pszEnd[-1]))
            pszEnd--;
        if (pszEnd > psz)
        {
            char chSaved = *pszEnd;
            *pszEnd = '\0';
            pVar = define_variable_vl(pszVarName, strlen(pszVarName), psz, pszEnd - psz,
                                      1 /* duplicate */, o_local, 0 /* !recursive */);
            *pszEnd = chSaved;
        }
        else
            pVar = NULL;
    }

    /* Cache the result if needed. */
    if (fCacheIt)
    {
        psz = pszBuf;
        ADD_VAR(pTarget);
        ADD_STR("_2_", 3);
        ADD_VAR(pType);
        ADD_STR(pszPropF2, cchPropF2);
        define_variable_vl_global(pszBuf, psz - pszBuf, pVar ? pVar->value : "", pVar ? pVar->value_length : 0,
                                  1 /* duplicate */, o_file, 0 /* !recursive */, NILF);
    }

#undef ADD_VAR
#undef ADD_STR
#undef ADD_CSTR
#undef ADD_CH
    free(pszBuf);
    return pVar;
}


/*
 *
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
    \ - the rest depends on pBldType, see _TARGET_TOOL and kbuild_first_prop.
    $($(type)TOOL.$(bld_trg).$(bld_trg_arch)) \
    $($(type)TOOL.$(bld_trg)) \
    $($(type)TOOL) \
    $(TOOL.$(bld_trg).$(bld_trg_arch)) \
    $(TOOL.$(bld_trg)) \
    $(TOOL) ))
*/
static struct variable *
kbuild_get_source_tool(struct variable *pTarget, struct variable *pSource, struct variable *pType,
                       struct variable *pBldTrg, struct variable *pBldTrgArch, struct variable *pBldType,
                       const char *pszVarName)
{
    struct variable *pVar = kbuild_first_prop(pTarget, pSource, NULL, pType, pBldTrg, pBldTrgArch, pBldType,
                                              "TOOL", sizeof("TOOL") - 1,
                                              "TOOL", sizeof("TOOL") - 1,
                                              0 /*fExtendedSource*/, pszVarName);
    if (!pVar)
        OSS(fatal, NILF, _("no tool for source `%s' in target `%s'!"), pSource->value, pTarget->value);
    return pVar;
}


/**
 * Helper for func_kbuild_source_tool, func_kbuild_source_one, ++.
 */
static int kbuild_version_to_int(const char *pszVersion, int fStrict)
{
    int iVer = 0;
    if (pszVersion && pszVersion[0])
    {
        switch (pszVersion[0] | (pszVersion[1] << 8))
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
                while (ISBLANK(*pszVersion))
                    pszVersion++;
                if (*pszVersion)
                {
                    char *pszEnd = NULL;
                    long lVer;
                    errno = 0;
                    lVer = strtol(pszVersion, &pszEnd, 10);
                    iVer = (int)lVer;
                    if (fStrict)
                    {
                        if (lVer == 0 && errno != 0)
                            OSN(fatal, NILF, _("invalid version argument '%s': errno=%d"), pszVersion, errno);
                        else if (iVer != (int)lVer || iVer < 0)
                            OS(fatal, NILF, _("version argument out of range '%s'"), pszVersion);
                        else if (pszEnd)
                        {
                            while (ISBLANK(*pszEnd))
                                pszEnd++;
                            if (*pszEnd)
                                OS(fatal, NILF, _("version is not numerical '%s'"), pszVersion);
                        }
                    }
                }
                break;
        }
    }
    return iVer;
}


/**
 * "kb-src-tool <varname> [ver=0]" - implements _SOURCE_TOOL.
 *
 * Since r3415 an extended set of keyword variations is used on the target, tool
 * and global properties.
 */
char *
func_kbuild_source_tool(char *o, char **argv, const char *pszFuncName)
{
    const int        iVer = kbuild_version_to_int(argv[1], 1 /*strict*/);
    struct variable *pVar = kbuild_get_source_tool(kbuild_get_variable_n(ST("target")),
                                                   kbuild_get_variable_n(ST("source")),
                                                   kbuild_get_variable_n(ST("type")),
                                                   kbuild_get_variable_n(ST("bld_trg")),
                                                   kbuild_get_variable_n(ST("bld_trg_arch")),
                                                   kbuild_get_variable_n(ST("bld_type")),
                                                   argv[0]);
    if (pVar)
         o = variable_buffer_output(o, pVar->value, pVar->value_length);
    (void)pszFuncName; (void)iVer;
    return o;
}


/**
 * Similar to _TARGET_TOOL since r3415.
 */
static struct variable *
kbuild_get_object_suffix(struct variable *pTarget, struct variable *pSource,
                         struct variable *pTool, struct variable *pType,
                         struct variable *pBldTrg, struct variable *pBldTrgArch, struct variable *pBldType,
                         const char *pszVarName)
{
    struct variable *pVar = kbuild_first_prop(pTarget, pSource, pTool, pType, pBldTrg, pBldTrgArch, pBldType,
                                              "SUFF_OBJ", sizeof("SUFF_OBJ") - 1,
                                              "OBJSUFF",  sizeof("OBJSUFF")  - 1,
                                              0 /*fExtendedSource*/, pszVarName);
    if (!pVar)
        OSS(fatal, NILF, _("no OBJSUFF attribute or SUFF_OBJ default for source `%s' in target `%s'!"),
            pSource->value, pTarget->value);
    return pVar;
}


/**
 * "kb-obj-suff <varname> [ver=0]"
 *
 * Since r3415 an extended set of keyword variations is used on the target, tool
 * and global properties.
 */
char *
func_kbuild_object_suffix(char *o, char **argv, const char *pszFuncName)
{
    const int        iVer = kbuild_version_to_int(argv[1], 1 /*strict*/);
    struct variable *pVar = kbuild_get_object_suffix(kbuild_get_variable_n(ST("target")),
                                                     kbuild_get_variable_n(ST("source")),
                                                     kbuild_get_variable_n(ST("tool")),
                                                     kbuild_get_variable_n(ST("type")),
                                                     kbuild_get_variable_n(ST("bld_trg")),
                                                     kbuild_get_variable_n(ST("bld_trg_arch")),
                                                     kbuild_get_variable_n(ST("bld_type")),
                                                     argv[0]);
    if (pVar)
         o = variable_buffer_output(o, pVar->value, pVar->value_length);
    (void)pszFuncName; (void)iVer;
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
     * Strip the source filename of any unnecessary leading path and root specs.
     */
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


/**
 * "kb-obj-base <var> [ver]" Implements _OBJECT_BASE.
 */
char *
func_kbuild_object_base(char *o, char **argv, const char *pszFuncName)
{
    const int        iVer = kbuild_version_to_int(argv[1], 1 /*strict*/);
    struct variable *pVar = kbuild_get_object_base(kbuild_lookup_variable("target"),
                                                   kbuild_lookup_variable("source"),
                                                   argv[0]);
    if (pVar)
         o = variable_buffer_output(o, pVar->value, pVar->value_length);
    (void)pszFuncName; (void)iVer;
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

/** Per variable struct used by kbuild_collect_source_prop and helpers. */
struct kbuild_collect_variable
{
    struct variable    *pVar;
    unsigned int        cchExp;
    char               *pszExp;
};

/**
 * Helper for kbuild_collect_source_prop. 
 *  
 * Frees up any memory we've allocated for the variable values. 
 */
static struct variable *
kbuild_collect_source_prop_create_var(size_t cchTotal, int cVars, struct kbuild_collect_variable *paVars, int iDirection,
                                      const char *pszVarName, size_t cchVarName, enum variable_origin enmVarOrigin)
{
    char *pszResult;
    struct variable *pVar;
    if (!cVars || !cchTotal)
    {
        pszResult = xmalloc(1);
        *pszResult = '\0';
    }
    else
    {
        int iVar;
        char *psz = pszResult = xmalloc(cchTotal + 1);
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

    }
    if (enmVarOrigin == o_local)
        pVar = define_variable_vl(pszVarName, cchVarName, pszResult, cchTotal,
                                  0 /* take pszResult */ , enmVarOrigin, 0 /* !recursive */);
    else
        pVar = define_variable_vl_global(pszVarName, cchVarName, pszResult, cchTotal,
                                         0 /* take pszResult */ , enmVarOrigin, 0 /* !recursive */, NILF);
    return pVar;
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
    char *pszBuf, *psz, *psz2, *psz3;
    struct kbuild_collect_variable *paVars;

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
           + pBldType->value_length + 1
           + sizeof("_2_");
    pszBuf = xmalloc(cchBuf);

    /*
     * Get the variables.
     *
     * The compiler will get a heart attack when it sees this code ... ;-)
     */
    cVars = 12 * (pSdks->c + 5);
    paVars = (struct kbuild_collect_variable *)alloca(cVars * sizeof(paVars[0]));

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
            if (   !pVar->recursive \
                || IS_VARIABLE_RECURSIVE_WITHOUT_DOLLAR(pVar)) \
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

    /*
     * The first bunch part may be cached on the target already, check that first:
     */
    psz = pszBuf;
    ADD_VAR(pTarget);
    ADD_CSTR("_2_");
    ADD_VAR(pType);
    ADD_STR(pszProp, cchProp);
    ADD_CH('_');
    ADD_VAR(pBldType);
    *psz = '\0';
    pVar = kbuild_lookup_variable_n(pszBuf, psz - pszBuf);
    if (pVar)
        assert(iVar == 0);
    else
    {
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

        /*
         * Cache the result thus far (frees up anything we might've allocated above).
         */
        psz = pszBuf;
        ADD_VAR(pTarget);
        ADD_CSTR("_2_");
        ADD_VAR(pType);
        ADD_STR(pszProp, cchProp);
        ADD_CH('_');
        ADD_VAR(pBldType);
        *psz = '\0';
        assert(iVar <= cVars);
        pVar = kbuild_collect_source_prop_create_var(cchTotal, iVar, paVars, iDirection, pszBuf, psz - pszBuf, o_file);
        assert(pVar);
    }

    /* Now we use the cached target variable. */
    paVars[0].pVar   = pVar;
    paVars[0].pszExp = pVar->value;
    paVars[0].cchExp = pVar->value_length;
    cchTotal         = paVars[0].cchExp + 1;
    iVar = 1;

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

    /*
     * Construct the result value (frees up anything we might've allocated above).
     */
    assert(iVar <= cVars);
    return kbuild_collect_source_prop_create_var(cchTotal, iVar, paVars, iDirection, pszVarName, cchVarName, o_local);

#undef ADD_VAR
#undef ADD_STR
#undef ADD_CSTR
#undef ADD_CH
#undef DO_VAR_LOOKUP
#undef DO_DOUBLE_PSZ2_VARIATION
#undef DO_SINGLE_PSZ3_VARIATION
}


/* "kb-src-prop <prop> <var> <dir> [defpath] [ver]"
   get a source property. */
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
    int iVer = 0;
    int iDirection;
    if (!strcmp(argv[2], "left-to-right"))
        iDirection = 1;
    else if (!strcmp(argv[2], "right-to-left"))
        iDirection = -1;
    else
        OS(fatal, NILF, _("incorrect direction argument `%s'!"), argv[2]);
    if (argv[3])
    {
        const char *psz = argv[3];
        while (ISSPACE(*psz))
            psz++;
        if (*psz)
            pDefPath = kbuild_get_variable_n(ST("defpath"));
        if (argv[4])
            iVer = kbuild_version_to_int(argv[4], 1 /*strict*/);
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
    (void)pszFuncName; (void)iVer;
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
            OS(fatal, NULL, "whut!?! no path? result=`%s'", pszResult);
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


/**
 *
 *
 * Setup the base variables for def_target_source_c_cpp_asm_new:
 * @code

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
 * @endcode
 *
 * argv[0] is the function version. Prior to r1792 (early 0.1.5) this
 * was undefined and footer.kmk always passed an empty string.
 *
 * Version 2, as implemented in r1797, will make use of the async
 * includedep queue feature. This means the files will be read by one or
 * more background threads, leaving the eval'ing to be done later on by
 * the main thread (in snap_deps).
 *
 * Version 3, as implemented in rXXXX, will check
 * TOOL_$(tool)_COMPILE_$(type)_USES_KOBJCACHE and use
 * def_target_source_rule_v3plus_objcache if it's non-empty or
 * def_target_source_rule_v3plus if it's empty (version 2 and older will use
 * def_target_source_rule).  Version 3 will also skip defining several
 * properties that it considers legacy (todo: which).
 *
 * Version 4, as implemented in r3415, will use the extended tool resolution at
 * the target level (not source) as implemented by the _TARGET_TOOL update in
 * r2433.
 *
 * With r3415 the $(target)_2_$(type)TOOL will be used for caching purposes
 * regardless of version.
 */
char *
func_kbuild_source_one(char *o, char **argv, const char *pszFuncName)
{
    static int s_fNoCompileDepsDefined = -1;
    struct variable *pTarget    = kbuild_get_variable_n(ST("target"));
    struct variable *pSource    = kbuild_get_variable_n(ST("source"));
    struct variable *pDefPath   = kbuild_get_variable_n(ST("defpath"));
    struct variable *pType      = kbuild_get_variable_n(ST("type"));
    struct variable *pBldType   = kbuild_get_variable_n(ST("bld_type"));
    struct variable *pBldTrg    = kbuild_get_variable_n(ST("bld_trg"));
    struct variable *pBldTrgArch= kbuild_get_variable_n(ST("bld_trg_arch"));
    struct variable *pBldTrgCpu = kbuild_get_variable_n(ST("bld_trg_cpu"));
    const int        iVer       = kbuild_version_to_int(argv[0], 0 /*strict*/);
    struct variable *pTool      = kbuild_get_source_tool(pTarget, pSource, pType, pBldTrg, pBldTrgArch,
                                                         iVer >= 4 ? pBldType : NULL, "tool");
    struct variable *pOutBase   = kbuild_get_object_base(pTarget, pSource, "outbase");
    struct variable *pObjSuff   = kbuild_get_object_suffix(pTarget, pSource, pTool, pType, pBldTrg, pBldTrgArch,
                                                           iVer >= 4 ? pBldType : NULL, "objsuff");
    struct variable *pDeps, *pOrderDeps, *pDirDep, *pDep, *pVar, *pOutput, *pOutputMaybe;
#if 0 /* not used */
    struct variable *pDefs, *pIncs, *pFlags;
#endif
    struct variable *pObj       = kbuild_set_object_name_and_dep_and_dirdep_and_PATH_target_source(pTarget, pSource, pOutBase, pObjSuff, "obj", &pDep, &pDirDep);
    int fInstallOldObjsVar = 0;
    char *pszDstVar, *pszDst, *pszSrcVar, *pszSrc, *pszVal, *psz;
    char *pszSavedVarBuf;
    unsigned cchSavedVarBuf;
    size_t cch;
    struct kbuild_sdks Sdks;

    /*
     * Gather properties.
     */
    kbuild_get_sdks(&Sdks, pTarget, pSource, pBldType, pBldTrg, pBldTrgArch);

    if (pDefPath && !pDefPath->value_length)
        pDefPath = NULL;


    /*pDefs  =*/ kbuild_collect_source_prop(pTarget, pSource, pTool, &Sdks, pType, pBldType, pBldTrg, pBldTrgArch, pBldTrgCpu, NULL,
                                            ST("DEFS"),      ST("defs"),      1 /* left-to-right */);
    /*pIncs  =*/ kbuild_collect_source_prop(pTarget, pSource, pTool, &Sdks, pType, pBldType, pBldTrg, pBldTrgArch, pBldTrgCpu, pDefPath,
                                            ST("INCS"),      ST("incs"),     -1 /* right-to-left */);
    /*pFlags =*/ kbuild_collect_source_prop(pTarget, pSource, pTool, &Sdks, pType, pBldType, pBldTrg, pBldTrgArch, pBldTrgCpu, NULL,
                                            ST("FLAGS"),     ST("flags"),     1 /* left-to-right */);
    pDeps      = kbuild_collect_source_prop(pTarget, pSource, pTool, &Sdks, pType, pBldType, pBldTrg, pBldTrgArch, pBldTrgCpu, pDefPath,
                                            ST("DEPS"),      ST("deps"),      1 /* left-to-right */);
    pOrderDeps = kbuild_collect_source_prop(pTarget, pSource, pTool, &Sdks, pType, pBldType, pBldTrg, pBldTrgArch, pBldTrgCpu, pDefPath,
                                            ST("ORDERDEPS"), ST("orderdeps"), 1 /* left-to-right */);

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
        kbuild_apply_defpath(pDefPath, &pSource->value, &pSource->value_length, &pSource->value_alloc_len, 1 /* can free */);
    }

    /*
    # dependencies
    ifndef NO_COMPILE_DEPS
    _DEPFILES_INCLUDED += $(dep)
    $(if $(wildcard $(dep)),$(eval include $(dep)))
    endif
     */
    if (s_fNoCompileDepsDefined == -1)
        s_fNoCompileDepsDefined = kbuild_lookup_variable_n(ST("NO_COMPILE_DEPS")) != NULL;
    if (!s_fNoCompileDepsDefined)
    {
        pVar = kbuild_query_recursive_variable_n("_DEPFILES_INCLUDED", sizeof("_DEPFILES_INCLUDED") - 1);
        if (pVar)
        {
            if (pVar->recursive)
                pVar = kbuild_simplify_variable(pVar);
            append_string_to_variable(pVar, pDep->value, pDep->value_length, 1 /* append */);
        }
        else
            define_variable_vl_global("_DEPFILES_INCLUDED", sizeof("_DEPFILES_INCLUDED") - 1,
                                      pDep->value, pDep->value_length,
                                      1 /* duplicate_value */,
                                      o_file,
                                      0 /* recursive */,
                                      NULL /* flocp */);

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
    /** @todo Make all these local variables, if someone needs the info later it
     *        can be recalculated. (Ticket #80.) */
    cch = sizeof("TOOL_") + pTool->value_length + sizeof("_COMPILE_") + pType->value_length + sizeof("_USES_KOBJCACHE");
    if (cch < pTarget->value_length + sizeof("$(_2_OBJS)"))
        cch = pTarget->value_length + sizeof("$(_2_OBJS)");
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
    if (iVer <= 2)
        do_variable_definition_2(NILF, pszDstVar, pVar->value, pVar->value_length,
                                 !pVar->recursive, 0, o_local, f_simple, 0 /* !target_var */);
    do_variable_definition_2(NILF, "kbsrc_cmds", pVar->value, pVar->value_length,
                             !pVar->recursive, 0, o_local, f_simple, 0 /* !target_var */);

    memcpy(pszSrc, "_OUTPUT", sizeof("_OUTPUT"));
    memcpy(pszDst, "_OUTPUT_", sizeof("_OUTPUT_"));
    pVar = kbuild_get_recursive_variable(pszSrcVar);
    if (iVer <= 2)
        pOutput = do_variable_definition_2(NILF, pszDstVar, pVar->value, pVar->value_length,
                                           !pVar->recursive, 0, o_local, f_simple, 0 /* !target_var */);
    pOutput = do_variable_definition_2(NILF, "kbsrc_output", pVar->value, pVar->value_length,
                                       !pVar->recursive, 0, o_local, f_simple, 0 /* !target_var */);

    memcpy(pszSrc, "_OUTPUT_MAYBE", sizeof("_OUTPUT_MAYBE"));
    memcpy(pszDst, "_OUTPUT_MAYBE_", sizeof("_OUTPUT_MAYBE_"));
    pVar = kbuild_query_recursive_variable(pszSrcVar);
    if (pVar)
    {
        if (iVer <= 2)
            pOutputMaybe = do_variable_definition_2(NILF, pszDstVar, pVar->value, pVar->value_length,
                                                    !pVar->recursive, 0, o_local, f_simple, 0 /* !target_var */);
        pOutputMaybe = do_variable_definition_2(NILF, "kbsrc_output_maybe", pVar->value, pVar->value_length,
                                                !pVar->recursive, 0, o_local, f_simple, 0 /* !target_var */);
    }
    else
    {
        if (iVer <= 2)
            pOutputMaybe = do_variable_definition_2(NILF, pszDstVar, "", 0, 1, 0, o_local, f_simple, 0 /* !target_var */);
        pOutputMaybe = do_variable_definition_2(NILF, "kbsrc_output_maybe", "", 0, 1, 0, o_local, f_simple, 0 /* !target_var */);
    }

    memcpy(pszSrc, "_DEPEND", sizeof("_DEPEND"));
    memcpy(pszDst, "_DEPEND_", sizeof("_DEPEND_"));
    pVar = kbuild_get_recursive_variable(pszSrcVar);
    psz = pszVal = xmalloc(pVar->value_length + 1 + pDeps->value_length + 1 + pSource->value_length + 1);
    memcpy(psz, pVar->value, pVar->value_length);       psz += pVar->value_length;
    *psz++ = ' ';
    memcpy(psz, pDeps->value, pDeps->value_length);     psz += pDeps->value_length;
    *psz++ = ' ';
    memcpy(psz, pSource->value, pSource->value_length + 1);
    if (iVer <= 2)
        do_variable_definition_2(NILF, pszDstVar, pszVal, pVar->value_length + 1 + pDeps->value_length + 1 + pSource->value_length,
                                 !pVar->recursive && !pDeps->recursive && !pSource->recursive,
                                 NULL, o_local, f_simple, 0 /* !target_var */);
    do_variable_definition_2(NILF, "kbsrc_depend", pszVal, pVar->value_length + 1 + pDeps->value_length + 1 + pSource->value_length,
                             !pVar->recursive && !pDeps->recursive && !pSource->recursive,
                             pszVal, o_local, f_simple, 0 /* !target_var */);

    memcpy(pszSrc, "_DEPORD", sizeof("_DEPORD"));
    memcpy(pszDst, "_DEPORD_", sizeof("_DEPORD_"));
    pVar = kbuild_get_recursive_variable(pszSrcVar);
    psz = pszVal = xmalloc(pVar->value_length + 1 + pDirDep->value_length + 1 + pOrderDeps->value_length + 1);
    memcpy(psz, pVar->value, pVar->value_length);       psz += pVar->value_length;
    *psz++ = ' ';
    memcpy(psz, pDirDep->value, pDirDep->value_length); psz += pDirDep->value_length;
    *psz++ = ' ';
    memcpy(psz, pOrderDeps->value, pOrderDeps->value_length + 1);
    if (iVer <= 2)
        do_variable_definition_2(NILF, pszDstVar, pszVal,
                                 pVar->value_length + 1 + pDirDep->value_length + 1 + pOrderDeps->value_length,
                                 !pVar->recursive && !pDirDep->recursive && !pOrderDeps->recursive,
                                 NULL, o_local, f_simple, 0 /* !target_var */);
    do_variable_definition_2(NILF, "kbsrc_depord", pszVal,
                             pVar->value_length + 1 + pDirDep->value_length + 1 + pOrderDeps->value_length,
                             !pVar->recursive && !pDirDep->recursive && !pOrderDeps->recursive,
                             pszVal, o_local, f_simple, 0 /* !target_var */);

    /*
    _OUT_FILES      += $($(target)_$(source)_OUTPUT_) $($(target)_$(source)_OUTPUT_MAYBE_)
    */
    pVar = kbuild_get_variable_n(ST("_OUT_FILES"));
    append_string_to_variable(pVar, pOutput->value, pOutput->value_length, 1 /* append */);
    if (pOutputMaybe->value_length)
        append_string_to_variable(pVar, pOutputMaybe->value, pOutputMaybe->value_length, 1 /* append */);

    /*
    $(target)_2_OBJS += $(obj)
    */
    memcpy(pszDstVar + pTarget->value_length, "_2_OBJS", sizeof("_2_OBJS"));
    pVar = kbuild_query_recursive_variable_n(pszDstVar, pTarget->value_length + sizeof("_2_OBJS") - 1);
    fInstallOldObjsVar |= iVer <= 2 && (!pVar || !pVar->value_length);
    if (pVar)
    {
        if (pVar->recursive)
            pVar = kbuild_simplify_variable(pVar);
        append_string_to_variable(pVar, pObj->value, pObj->value_length, 1 /* append */);
    }
    else
        define_variable_vl_global(pszDstVar, pTarget->value_length + sizeof("_2_OBJS") - 1,
                                  pObj->value, pObj->value_length,
                                  1 /* duplicate_value */,
                                  o_file,
                                  0 /* recursive */,
                                  NULL /* flocp */);

    /*
     * Install legacy variable.
     */
    if (fInstallOldObjsVar)
    {
        /* $(target)_OBJS_ = $($(target)_2_OBJS)*/
        memcpy(pszDstVar + pTarget->value_length, "_OBJS_", sizeof("_OBJS_"));

        pszSrcVar[0] = '$';
        pszSrcVar[1] = '(';
        memcpy(pszSrcVar + 2, pTarget->value, pTarget->value_length);
        psz = pszSrcVar + 2 + pTarget->value_length;
        memcpy(psz, "_2_OBJS)", sizeof("_2_OBJS)"));

        define_variable_vl_global(pszDstVar, pTarget->value_length + sizeof("_OBJS_") - 1,
                                  pszSrcVar, pTarget->value_length + sizeof("$(_2_OBJS)") - 1,
                                  1 /* duplicate_value */,
                                  o_file,
                                  1 /* recursive */,
                                  NULL /* flocp */);
    }

    /*
    $(eval $(def_target_source_rule))
    */
    if (iVer > 2)
    {
        /*ifneq ($(TOOL_$(tool)_COMPILE_$(type)_USES_KOBJCACHE),)*/
        int fUsesObjCache = 0;
        memcpy(pszSrc, "_USES_KOBJCACHE", sizeof("_USES_KOBJCACHE"));
        pVar = lookup_variable(pszSrcVar, pszSrc + sizeof("_USES_KOBJCACHE") - 1 - pszSrcVar);
        if (pVar)
        {
            if (   !pVar->recursive
                || IS_VARIABLE_RECURSIVE_WITHOUT_DOLLAR(pVar))
                fUsesObjCache = pVar->value_length > 0;
            else
            {
                unsigned int cchTmp = 0;
                char *pszTmp = allocated_variable_expand_2(pVar->value, pVar->value_length, &cchTmp);
                free(pszTmp);
                fUsesObjCache = cchTmp > 0;
            }
        }
        if (!fUsesObjCache)
            pVar = kbuild_get_recursive_variable("def_target_source_rule_v3plus");
        else
            pVar = kbuild_get_recursive_variable("def_target_source_rule_v3plus_objcache");
    }
    else
        pVar = kbuild_get_recursive_variable("def_target_source_rule");
    pszVal = variable_expand_string_2(o, pVar->value, pVar->value_length, &psz);
    assert(!((size_t)pszVal & 3));

    install_variable_buffer(&pszSavedVarBuf, &cchSavedVarBuf);
    eval_buffer(pszVal, NULL, psz);
    restore_variable_buffer(pszSavedVarBuf, cchSavedVarBuf);

    kbuild_put_sdks(&Sdks);
    (void)pszFuncName;

    *pszVal = '\0';
    return pszVal;
}

/*

## Inherit one template property in a non-accumulative manner.
# @param    $(prop)     Property name
# @param    $(target)	Target name
# @todo fix the precedence order for some properties.
define def_inherit_template_one
ifdef TEMPLATE_$($(target)_TEMPLATE)_$(prop)
ifndef $(target)_$(prop)
$(target)_$(prop) := $(TEMPLATE_$($(target)_TEMPLATE)_$(prop))
endif
endif
ifdef TEMPLATE_$($(target)_TEMPLATE)_$(prop).$(bld_trg)
ifndef $(target)_$(prop).$(bld_trg)
$(target)_$(prop).$(bld_trg) := $(TEMPLATE_$($(target)_TEMPLATE)_$(prop).$(bld_trg))
endif
endif
ifdef TEMPLATE_$($(target)_TEMPLATE)_$(prop).$(bld_trg).$(bld_trg_arch)
ifndef $(target)_$(prop).$(bld_trg).$(bld_trg_arch)
$(target)_$(prop).$(bld_trg).$(bld_trg_arch) := $(TEMPLATE_$($(target)_TEMPLATE)_$(prop).$(bld_trg).$(bld_trg_arch))
endif
endif
ifdef TEMPLATE_$($(target)_TEMPLATE)_$(prop).$(bld_trg_arch)
ifndef $(target)_$(prop).$(bld_trg_arch)
$(target)_$(prop).$(bld_trg_arch) := $(TEMPLATE_$($(target)_TEMPLATE)_$(prop).$(bld_trg_arch))
endif
endif
ifdef TEMPLATE_$($(target)_TEMPLATE)_$(prop).$(bld_trg_cpu)
ifndef $(target)_$(prop).$(bld_trg_cpu)
$(target)_$(prop).$(bld_trg_cpu) := $(TEMPLATE_$($(target)_TEMPLATE)_$(prop).$(bld_trg_cpu))
endif
endif
endef

## Inherit one template property in a non-accumulative manner, deferred expansion.
# @param    1: $(prop)     Property name
# @param    2: $(target)	Target name
# @todo fix the precedence order for some properties.
# @remark this define relies on double evaluation
define def_inherit_template_one_deferred
ifdef TEMPLATE_$($(target)_TEMPLATE)_$(prop)
ifndef $(target)_$(prop)
$(target)_$(prop) = $$(TEMPLATE_$($(target)_TEMPLATE)_$(prop))
endif
endif
ifdef TEMPLATE_$($(target)_TEMPLATE)_$(prop).$(bld_trg)
ifndef $(target)_$(prop).$(bld_trg)
$(target)_$(prop).$(bld_trg) = $$(TEMPLATE_$($(target)_TEMPLATE)_$(prop).$(bld_trg))
endif
endif
ifdef TEMPLATE_$($(target)_TEMPLATE)_$(prop).$(bld_trg).$(bld_trg_arch)
ifndef $(target)_$(prop).$(bld_trg).$(bld_trg_arch)
$(target)_$(prop).$(bld_trg).$(bld_trg_arch) = $$(TEMPLATE_$($(target)_TEMPLATE)_$(prop).$(bld_trg).$(bld_trg_arch))
endif
endif
ifdef TEMPLATE_$($(target)_TEMPLATE)_$(prop).$(bld_trg_arch)
ifndef $(target)_$(prop).$(bld_trg_arch)
$(target)_$(prop).$(bld_trg_arch) = $$(TEMPLATE_$($(target)_TEMPLATE)_$(prop).$(bld_trg_arch))
endif
endif
ifdef TEMPLATE_$($(target)_TEMPLATE)_$(prop).$(bld_trg_cpu)
ifndef $(target)_$(prop).$(bld_trg_cpu)
$(target)_$(prop).$(bld_trg_cpu) = $$(TEMPLATE_$($(target)_TEMPLATE)_$(prop).$(bld_trg_cpu))
endif
endif
endef

## Inherit one acculumlative template property where the 'most significant' items are at the left end.
# @param    $(prop)     Property name
# @param    $(target)	Target name
define def_inherit_template_one_accumulate_l
ifdef TEMPLATE_$($(target)_TEMPLATE)_$(prop)
 ifeq ($$(flavor $(target)_$(prop)),simple)
 $$(evalcall2 def_simple_2_recursive,$(target)_$(prop))
 endif
$(target)_$(prop) += $$(TEMPLATE_$($(target)_TEMPLATE)_$(prop))
endif
ifdef TEMPLATE_$($(target)_TEMPLATE)_$(prop).$(KBUILD_TYPE)
 ifeq ($$(flavor $(target)_$(prop).$(KBUILD_TYPE)),simple)
 $$(evalcall2 def_simple_2_recursive,$(target)_$(prop).$(KBUILD_TYPE))
 endif
$(target)_$(prop).$(KBUILD_TYPE) += $$(TEMPLATE_$($(target)_TEMPLATE)_$(prop).$(KBUILD_TYPE))
endif
ifdef TEMPLATE_$($(target)_TEMPLATE)_$(prop).$(bld_trg)
 ifeq ($$(flavor $(target)_$(prop).$(bld_trg)),simple)
 $$(evalcall2 def_simple_2_recursive,$(target)_$(prop).$(bld_trg))
 endif
$(target)_$(prop).$(bld_trg) += $$(TEMPLATE_$($(target)_TEMPLATE)_$(prop).$(bld_trg))
endif
ifdef TEMPLATE_$($(target)_TEMPLATE)_$(prop).$(bld_trg).$(bld_trg_arch)
 ifeq ($$(flavor $(target)_$(prop).$(bld_trg).$(bld_trg_arch)),simple)
 $$(evalcall2 def_simple_2_recursive,$(target)_$(prop).$(bld_trg).$(bld_trg_arch))
 endif
$(target)_$(prop).$(bld_trg).$(bld_trg_arch) += $$(TEMPLATE_$($(target)_TEMPLATE)_$(prop).$(bld_trg).$(bld_trg_arch))
endif
ifdef TEMPLATE_$($(target)_TEMPLATE)_$(prop).$(bld_trg_cpu)
 ifeq ($$(flavor $(target)_$(prop).$(bld_trg_cpu)),simple)
 $$(evalcall2 def_simple_2_recursive,$(target)_$(prop).$(bld_trg_cpu))
 endif
$(target)_$(prop).$(bld_trg_cpu) += $$(TEMPLATE_$($(target)_TEMPLATE)_$(prop).$(bld_trg_cpu))
endif
ifdef TEMPLATE_$($(target)_TEMPLATE)_$(prop).$(bld_trg_arch)
 ifeq ($$(flavor $(target)_$(prop).$(bld_trg_arch)),simple)
 $$(evalcall2 def_simple_2_recursive,$(target)_$(prop).$(bld_trg_arch))
 endif
$(target)_$(prop).$(bld_trg_arch) += $$(TEMPLATE_$($(target)_TEMPLATE)_$(prop).$(bld_trg_arch))
endif
endef

## Inherit one acculumlative template property where the 'most significant' items are at the right end.
# @param    $(prop)     Property name
# @param    $(target)	Target name
define def_inherit_template_one_accumulate_r
ifdef TEMPLATE_$($(target)_TEMPLATE)_$(prop)
 ifeq ($$(flavor $(target)_$(prop)),simple)
 $$(evalcall2 def_simple_2_recursive,$(target)_$(prop))
 endif
$(target)_$(prop) <=$$(TEMPLATE_$($(target)_TEMPLATE)_$(prop))
endif
ifdef TEMPLATE_$($(target)_TEMPLATE)_$(prop).$(KBUILD_TYPE)
 ifeq ($$(flavor $(target)_$(prop).$(KBUILD_TYPE)),simple)
 $$(evalcall2 def_simple_2_recursive,$(target)_$(prop).$(KBUILD_TYPE))
 endif
$(target)_$(prop).$(KBUILD_TYPE) <=$$(TEMPLATE_$($(target)_TEMPLATE)_$(prop).$(KBUILD_TYPE))
endif
ifdef TEMPLATE_$($(target)_TEMPLATE)_$(prop).$(bld_trg)
 ifeq ($$(flavor $(target)_$(prop).$(bld_trg)),simple)
 $$(evalcall2 def_simple_2_recursive,$(target)_$(prop).$(bld_trg))
 endif
$(target)_$(prop).$(bld_trg) <=$$(TEMPLATE_$($(target)_TEMPLATE)_$(prop).$(bld_trg))
endif
ifdef TEMPLATE_$($(target)_TEMPLATE)_$(prop).$(bld_trg).$(bld_trg_arch)
 ifeq ($$(flavor $(target)_$(prop).$(bld_trg).$(bld_trg_arch)),simple)
 $$(evalcall2 def_simple_2_recursive,$(target)_$(prop).$(bld_trg).$(bld_trg_arch))
 endif
$(target)_$(prop).$(bld_trg).$(bld_trg_arch) <=$$(TEMPLATE_$($(target)_TEMPLATE)_$(prop).$(bld_trg).$(bld_trg_arch))
endif
ifdef TEMPLATE_$($(target)_TEMPLATE)_$(prop).$(bld_trg_cpu)
 ifeq ($$(flavor $(target)_$(prop).$(bld_trg_cpu)),simple)
 $$(evalcall2 def_simple_2_recursive,$(target)_$(prop).$(bld_trg_cpu))
 endif
$(target)_$(prop).$(bld_trg_cpu) <=$$(TEMPLATE_$($(target)_TEMPLATE)_$(prop).$(bld_trg_cpu))
endif
ifdef TEMPLATE_$($(target)_TEMPLATE)_$(prop).$(bld_trg_arch)
 ifeq ($$(flavor $(target)_$(prop).$(bld_trg_arch)),simple)
 $$(evalcall2 def_simple_2_recursive,$(target)_$(prop).$(bld_trg_arch))
 endif
$(target)_$(prop).$(bld_trg_arch) <=$$(TEMPLATE_$($(target)_TEMPLATE)_$(prop).$(bld_trg_arch))
endif
endef


## Inherit template properties for on target.
# @param    $(target)    Target name.
define def_inherit_template
# sanity check.
ifdef _$(target)_ALREADY_PROCESSED
 $(error kBuild: The target $(target) appears more than once in the target lists! Please correct the makefile(s))
endif
_$(target)_ALREADY_PROCESSED := 1

# Inherit any default template.
ifdef TEMPLATE
ifeq ($($(target)_TEMPLATE),)
$(eval $(target)_TEMPLATE:=$(TEMPLATE))
endif
endif
# Expand the template if specified.
ifneq ($($(target)_TEMPLATE),)
$(foreach prop,$(PROPS_SINGLE),$(evalval def_inherit_template_one))
$(foreach prop,$(PROPS_DEFERRED),$(eval $(def_inherit_template_one_deferred))) # exploits the 2 evaluation, so no value!
$(foreach prop,$(PROPS_ACCUMULATE_L),$(eval $(def_inherit_template_one_accumulate_l))) # += works fine without value
$(foreach prop,$(PROPS_ACCUMULATE_R),$(eval $(def_inherit_template_one_accumulate_r))) # use <= (kmk addition)
endif
endef


Invoked like this:
 $(kb-exp-tmpl 1,$(_ALL_TARGET_TARGETS),$(KBUILD_TARGET),$(KBUILD_TARGET_ARCH),$(KBUILD_TARGET_CPU),$(KBUILD_TYPE))
*/
char *
func_kbuild_expand_template(char *o, char **argv, const char *pszFuncName)
{
    const char         *pszVersion    = argv[0];
    const char         *pszBldTrg     = argv[2];
    const char         *pszBldTrgArch = argv[3];
    const char         *pszBldTrgCpu  = argv[4];
    const char         *pszBldType    = argv[5];
    size_t              cchBldTrg     = strlen(pszBldTrg);
    size_t              cchBldTrgArch = strlen(pszBldTrgArch);
    size_t              cchBldTrgCpu  = strlen(pszBldTrgCpu);
    size_t              cchBldType    = strlen(pszBldType);
    size_t              cchMaxBld     = cchBldTrg + cchBldTrgArch + cchBldTrgCpu + cchBldType; /* too big, but so what. */
    struct kbet_key
    {
        unsigned int        cch;
        char               *psz;
    }                   aKeys[6];
    unsigned int const  cKeys = 6;
    unsigned int        iKey;
    struct variable    *pDefTemplate;
    struct variable    *pProps;
    struct kbet_prop
    {
        const char         *pch;
        unsigned int        cch;
        enum kbet_prop_enum { kPropSingle, kPropDeferred, kPropAccumulateL, kPropAccumulateR }
                            enmType;
    }                  *paProps;
    unsigned int        cProps;
    unsigned int        iProp;
    size_t              cchMaxProp;
    struct variable    *pVarTrg;
    struct variable    *pVarSrc;
    const char         *pszIter;
    const char         *pszTarget;
    unsigned int        cchTarget;
    char               *pszSrc    = 0;
    char               *pszSrcRef = 0;
    char               *pszSrcBuf = 0;
    size_t              cchSrcBuf = 0;
    char               *pszTrg    = 0;
    size_t              cchTrg    = 0;

    /*
     * Validate input.
     */
    if (pszVersion[0] != '1' || pszVersion[1])
      OSS(fatal, NULL, "%s: Unsupported version `%s'", pszFuncName, pszVersion);

    if (!cchBldTrg)
      OS(fatal, NULL, "%s: missing bldtrg", pszFuncName);

    if (!cchBldTrgArch)
      OS(fatal, NULL, "%s: missing bld_trg_arch", pszFuncName);

    if (!cchBldTrgCpu)
      OS(fatal, NULL, "%s: missing bld_trg_cpu", pszFuncName);

    if (!cchBldType)
      OS(fatal, NULL, "%s: missing bld_type", pszFuncName);

    /*
     * Prepare the keywords, prepending dots for quicker copying.
     * This allows for an inner loop when processing properties, saving code
     * at the expense of a few xmallocs.
     */
    /* the first entry is empty. */
    aKeys[0].cch = 0;
    aKeys[0].psz = NULL;

    /* .$(bld_type) */
    aKeys[1].cch = cchBldType + 1;
    aKeys[1].psz = xmalloc (aKeys[1].cch + 1);
    aKeys[1].psz[0] = '.';
    memcpy(aKeys[1].psz + 1, pszBldType, cchBldType + 1);

    /* .$(bld_trg) */
    aKeys[2].cch = cchBldTrg + 1;
    aKeys[2].psz = xmalloc (aKeys[2].cch + 1);
    aKeys[2].psz[0] = '.';
    memcpy(aKeys[2].psz + 1, pszBldTrg, cchBldTrg + 1);

    /* .$(bld_trg).$(bld_trg_arch) */
    aKeys[3].cch = cchBldTrg + 1 + cchBldTrgArch + 1;
    aKeys[3].psz = xmalloc (aKeys[3].cch + 1);
    aKeys[3].psz[0] = '.';
    memcpy(aKeys[3].psz + 1, pszBldTrg, cchBldTrg);
    aKeys[3].psz[1 + cchBldTrg] = '.';
    memcpy(aKeys[3].psz + 1 + cchBldTrg + 1, pszBldTrgArch, cchBldTrgArch + 1);

    /* .$(bld_trg_cpu) */
    aKeys[4].cch = cchBldTrgCpu + 1;
    aKeys[4].psz = xmalloc (aKeys[4].cch + 1);
    aKeys[4].psz[0] = '.';
    memcpy(aKeys[4].psz + 1, pszBldTrgCpu, cchBldTrgCpu + 1);

    /* .$(bld_trg_arch) */
    aKeys[5].cch = cchBldTrgArch + 1;
    aKeys[5].psz = xmalloc (aKeys[5].cch + 1);
    aKeys[5].psz[0] = '.';
    memcpy(aKeys[5].psz + 1, pszBldTrgArch, cchBldTrgArch + 1);


    /*
     * Prepare the properties, folding them into an array.
     * This way we won't have to reparse them for each an every target, though
     * it comes at the expense of one or more heap calls.
     */
#define PROP_ALLOC_INC  128
    iProp = 0;
    cProps = PROP_ALLOC_INC;
    paProps = xmalloc(sizeof(*pProps) * cProps);

    pProps = kbuild_get_variable_n(ST("PROPS_SINGLE"));
    pszIter = pProps->value;
    while ((paProps[iProp].pch = find_next_token(&pszIter, &paProps[iProp].cch)))
    {
        paProps[iProp].enmType = kPropSingle;
        if (++iProp >= cProps)
        {
            cProps += PROP_ALLOC_INC;
            paProps = xrealloc(paProps, sizeof(*paProps) * cProps);
        }

    }

    pProps = kbuild_get_variable_n(ST("PROPS_DEFERRED"));
    pszIter = pProps->value;
    while ((paProps[iProp].pch = find_next_token(&pszIter, &paProps[iProp].cch)))
    {
        paProps[iProp].enmType = kPropDeferred;
        if (++iProp >= cProps)
        {
            cProps += PROP_ALLOC_INC;
            paProps = xrealloc(paProps, sizeof(*paProps) * cProps);
        }
    }

    pProps = kbuild_get_variable_n(ST("PROPS_ACCUMULATE_L"));
    pszIter = pProps->value;
    while ((paProps[iProp].pch = find_next_token(&pszIter, &paProps[iProp].cch)))
    {
        paProps[iProp].enmType = kPropAccumulateL;
        if (++iProp >= cProps)
        {
            cProps += PROP_ALLOC_INC;
            paProps = xrealloc(paProps, sizeof(*paProps) * cProps);
        }
    }

    pProps = kbuild_get_variable_n(ST("PROPS_ACCUMULATE_R"));
    pszIter = pProps->value;
    while ((paProps[iProp].pch = find_next_token(&pszIter, &paProps[iProp].cch)))
    {
        paProps[iProp].enmType = kPropAccumulateR;
        if (++iProp >= cProps)
        {
            cProps += PROP_ALLOC_INC;
            paProps = xrealloc(paProps, sizeof(*paProps) * cProps);
        }
    }
#undef PROP_ALLOC_INC
    cProps = iProp;

    /* find the max prop length. */
    cchMaxProp = paProps[0].cch;
    while (--iProp > 0)
        if (paProps[iProp].cch > cchMaxProp)
            cchMaxProp = paProps[iProp].cch;

    /*
     * Query and prepare (strip) the default template
     * (given by the TEMPLATE variable).
     */
    pDefTemplate = kbuild_lookup_variable_n(ST("TEMPLATE"));
    if (pDefTemplate)
    {
        if (   pDefTemplate->value_length
            && (   ISSPACE(pDefTemplate->value[0])
                || ISSPACE(pDefTemplate->value[pDefTemplate->value_length - 1])))
        {
            unsigned int off;
            if (pDefTemplate->rdonly_val)
                OS(fatal, NULL, "%s: TEMPLATE is read-only", pszFuncName);

            /* head */
            for (off = 0; ISSPACE(pDefTemplate->value[off]); off++)
                /* nothing */;
            if (off)
            {
                pDefTemplate->value_length -= off;
                memmove(pDefTemplate->value, pDefTemplate->value + off, pDefTemplate->value_length + 1);
            }

            /* tail */
            off = pDefTemplate->value_length;
            while (off > 0 && ISSPACE(pDefTemplate->value[off - 1]))
                off--;
            pDefTemplate->value_length = off;
            pDefTemplate->value[off] = '\0';

            VARIABLE_CHANGED(pDefTemplate);
        }

        if (!pDefTemplate->value_length)
            pDefTemplate = NULL;
    }

    /*
     * Iterate the target list.
     */
    pszIter = argv[1];
    while ((pszTarget = find_next_token(&pszIter, &cchTarget)))
    {
        char *pszTrgProp, *pszSrcProp;
        char *pszTrgKey, *pszSrcKey;
        struct variable *pTmpl;
        const char *pszTmpl;
        size_t cchTmpl, cchMax;

        /* resize the target buffer. */
        cchMax = cchTarget + cchMaxProp + cchMaxBld + 10;
        if (cchTrg < cchMax)
        {
            cchTrg = (cchMax + 31U) & ~(size_t)31;
            pszTrg = xrealloc(pszTrg, cchTrg);
        }

        /*
         * Query the TEMPLATE property, if not found or zero-length fall back on the default.
         */
        memcpy(pszTrg, pszTarget, cchTarget);
        pszTrgProp = pszTrg + cchTarget;
        memcpy(pszTrgProp, "_TEMPLATE", sizeof("_TEMPLATE"));
        pszTrgProp++; /* after '_'. */

        /** @todo Change this to a recursive lookup with simplification below. That
         *        will allow target_TEMPLATE = $(NO_SUCH_TEMPLATE) instead of having
         *        to use target_TEMPLATE = DUMMY */
        pTmpl = kbuild_lookup_variable_n(pszTrg, cchTarget + sizeof("_TEMPLATE") - 1);
        if (!pTmpl || !pTmpl->value_length)
        {
            if (!pDefTemplate)
                continue; /* no template */
            pszTmpl = pDefTemplate->value;
            cchTmpl = pDefTemplate->value_length;
        }
        else
        {
            pszTmpl = pTmpl->value;
            cchTmpl = pTmpl->value_length;
            while (ISSPACE(*pszTmpl))
                cchTmpl--, pszTmpl++;
            if (!cchTmpl)
                continue; /* no template */
        }

        /* resize the source buffer. */
        cchMax = sizeof("TEMPLATE_") + cchTmpl + cchMaxProp + cchMaxBld + 10 + sizeof(void *);
        if (cchSrcBuf < cchMax)
        {
            cchSrcBuf = (cchMax + 31U) & ~(size_t)31;
            pszSrcBuf = xrealloc(pszSrcBuf, cchSrcBuf);
            pszSrc = pszSrcBuf + sizeof(void *);  assert(sizeof(void *) >= 2);
            pszSrcRef = pszSrc - 2;
            pszSrcRef[0] = '$';
            pszSrcRef[1] = '(';
        }

        /* prepare the source buffer */
        memcpy(pszSrc, "TEMPLATE_", sizeof("TEMPLATE_") - 1);
        pszSrcProp = pszSrc + sizeof("TEMPLATE_") - 1;
        memcpy(pszSrcProp, pszTmpl, cchTmpl);
        pszSrcProp += cchTmpl;
        *pszSrcProp++ = '_';

        /*
         * Process properties.
         * Note! The single and deferred are handled in the same way now.
         */
#define BY_REF_LIMIT   64 /*(cchSrcVar * 4 > 64 ? cchSrcVar * 4 : 64)*/

        for (iProp = 0; iProp < cProps; iProp++)
        {
            memcpy(pszTrgProp, paProps[iProp].pch, paProps[iProp].cch);
            pszTrgKey = pszTrgProp + paProps[iProp].cch;

            memcpy(pszSrcProp, paProps[iProp].pch, paProps[iProp].cch);
            pszSrcKey = pszSrcProp + paProps[iProp].cch;

            for (iKey = 0; iKey < cKeys; iKey++)
            {
                char *pszTrgEnd;
                size_t cchSrcVar;

                /* lookup source, skip ahead if it doesn't exist. */
                memcpy(pszSrcKey, aKeys[iKey].psz, aKeys[iKey].cch);
                cchSrcVar = pszSrcKey - pszSrc + aKeys[iKey].cch;
                pszSrc[cchSrcVar] = '\0';
                pVarSrc = kbuild_query_recursive_variable_n(pszSrc, cchSrcVar);
                if (!pVarSrc)
                    continue;

                /* lookup target, skip ahead if it exists. */
                memcpy(pszTrgKey, aKeys[iKey].psz, aKeys[iKey].cch);
                pszTrgEnd = pszTrgKey + aKeys[iKey].cch;
                *pszTrgEnd = '\0';
                pVarTrg = kbuild_query_recursive_variable_n(pszTrg, pszTrgEnd - pszTrg);

                switch (paProps[iProp].enmType)
                {
                    case kPropAccumulateL:
                    case kPropAccumulateR:
                        if (pVarTrg)
                        {
                            /* Append to existing variable. If the source is recursive,
                               or we append by reference, we'll have to make sure the
                               target is recusive as well. */
                            if (    !pVarTrg->recursive
                                &&  (   pVarSrc->value_length >= BY_REF_LIMIT
                                     || pVarSrc->recursive))
                                pVarTrg->recursive = 1;

                            if (pVarSrc->value_length < BY_REF_LIMIT)
                                append_string_to_variable(pVarTrg, pVarSrc->value, pVarSrc->value_length,
                                                          paProps[iProp].enmType == kPropAccumulateL /* append */);
                            else
                            {
                                pszSrc[cchSrcVar] = ')';
                                pszSrc[cchSrcVar + 1] = '\0';
                                append_string_to_variable(pVarTrg, pszSrcRef, 2 + cchSrcVar + 1,
                                                          paProps[iProp].enmType == kPropAccumulateL /* append */);
                            }
                            break;
                        }
                        /* else: the target variable doesn't exist, create it. */
                        /* fall thru */

                    case kPropSingle:
                    case kPropDeferred:
                        if (pVarTrg)
                            continue; /* skip ahead if it already exists. */

                        /* copy the variable if its short, otherwise reference it. */
                        if (pVarSrc->value_length < BY_REF_LIMIT)
                            define_variable_vl_global(pszTrg, pszTrgEnd - pszTrg,
                                                      pVarSrc->value, pVarSrc->value_length,
                                                      1 /* duplicate_value */,
                                                      o_file,
                                                      pVarSrc->recursive,
                                                      NULL /* flocp */);
                        else
                        {
                            pszSrc[cchSrcVar] = ')';
                            pszSrc[cchSrcVar + 1] = '\0';
                            define_variable_vl_global(pszTrg, pszTrgEnd - pszTrg,
                                                      pszSrcRef, 2 + cchSrcVar + 1,
                                                      1 /* duplicate_value */,
                                                      o_file,
                                                      1 /* recursive */,
                                                      NULL /* flocp */);
                        }
                        break;

                }

            } /* foreach key */
        } /* foreach prop */
#undef BY_REF_LIMIT
    } /* foreach target */

    /*
     * Cleanup.
     */
    free(pszSrcBuf);
    free(pszTrg);
    free(paProps);
    for (iKey = 1; iKey < cKeys; iKey++)
        free(aKeys[iKey].psz);

    return o;
}

#endif /* KMK_HELPERS */

