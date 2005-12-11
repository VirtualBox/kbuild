/* $Id: kDepPre.c 343 2005-12-11 04:02:21Z knut.osmundsen@oracle.com $ */
/** @file
 *
 * kDepPre - Dependency Generator using Precompiler output.
 *
 * Copyright (c) 2005 knut st. osmundsen <bird@innotek.de>
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


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <limits.h>
#include <sys/stat.h>
#ifdef __WIN32__
# include <windows.h>
#endif
#if !defined(__WIN32__) && !defined(__OS2__)
# include <dirent.h>
#endif
#ifndef __WIN32__
# include <unistd.h>
#endif

#ifdef HAVE_FGETC_UNLOCKED
# define FGETC(s)   getc_unlocked(s)
#else
# define FGETC(s)   fgetc(s)
#endif

#ifdef NEED_ISBLANK
# define isblank(ch) ( (unsigned char)(ch) == ' ' || (unsigned char)(ch) == '\t' )
#endif




/*******************************************************************************
*   Structures and Typedefs                                                    *
*******************************************************************************/
/** A dependency. */
typedef struct DEP
{
    /** Next dependency in the list. */
    struct DEP *pNext;
    /** The filename hash. */
    unsigned    uHash;
    /** The length of the filename. */
    size_t      cchFilename;
    /** The filename. */
    char        szFilename[4];
} DEP, *PDEP;


/*******************************************************************************
*   Global Variables                                                           *
*******************************************************************************/
/** List of dependencies. */
static PDEP g_pDeps = NULL;


/*******************************************************************************
*   Internal Functions                                                         *
*******************************************************************************/
static PDEP depAdd(const char *pszFilename, size_t cchFilename);
static void depOptimize(int fFixCase);
static void depPrint(FILE *pOutput);
static void depPrintStubs(FILE *pOutput);


#ifdef __WIN32__
/**
 * Corrects the case of a path.
 * Expects a fullpath!
 *
 * @param   pszPath     Pointer to the path, both input and output.
 *                      The buffer must be able to hold one more byte than the string length.
 */
void fixcase(char *pszPath)
{
#define my_assert(expr) \
    do { \
        if (!(expr)) { \
            printf("my_assert: %s, file %s, line %d\npszPath=%s\npsz=%s\n", \
                   #expr, __FILE__, __LINE__, pszPath, psz); \
            __asm { __asm int 3 } \
            exit(1); \
        } \
    } while (0)

    char *psz = pszPath;
    if (*psz == '/' || *psz == '\\')
    {
        if (psz[1] == '/' || psz[1] == '\\')
        {
            /* UNC */
            my_assert(psz[1] == '/' || psz[1] == '\\');
            my_assert(psz[2] != '/' && psz[2] != '\\');

            /* skip server name */
            psz += 2;
            while (*psz != '\\' && *psz != '/')
            {
                if (!*psz)
                    return;
                *psz++ = toupper(*psz);
            }

            /* skip the share name */
            psz++;
            my_assert(*psz != '/' && *psz != '\\');
            while (*psz != '\\' && *psz != '/')
            {
                if (!*psz)
                    return;
                *psz++ = toupper(*psz);
            }
            my_assert(*psz == '/' || *psz == '\\');
            psz++;
        }
        else
        {
            /* Unix spec */
            psz++;
        }
    }
    else
    {
        /* Drive letter */
        my_assert(psz[1] == ':');
        *psz = toupper(*psz);
        my_assert(psz[0] >= 'A' && psz[0] <= 'Z');
        my_assert(psz[2] == '/' || psz[2] == '\\');
        psz += 3;
    }

    /*
     * Pointing to the first char after the unc or drive specifier.
     */
    while (*psz)
    {
        WIN32_FIND_DATA FindFileData;
        HANDLE hDir;
        char chSaved0;
        char chSaved1;
        char *pszEnd;


        /* find the end of the component. */
        pszEnd = psz;
        while (*pszEnd && *pszEnd != '/' && *pszEnd != '\\')
            pszEnd++;

        /* replace the end with "?\0" */
        chSaved0 = pszEnd[0];
        chSaved1 = pszEnd[1];
        pszEnd[0] = '?';
        pszEnd[1] = '\0';

        /* find the right filename. */
        hDir = FindFirstFile(pszPath, &FindFileData);
        pszEnd[1] = chSaved1;
        if (!hDir)
        {
            pszEnd[0] = chSaved0;
            return;
        }
        pszEnd[0] = '\0';
        while (stricmp(FindFileData.cFileName, psz))
        {
            if (!FindNextFile(hDir, &FindFileData))
            {
                pszEnd[0] = chSaved0;
                return;
            }
        }
        strcpy(psz, FindFileData.cFileName);
        pszEnd[0] = chSaved0;

        /* advance to the next component */
        if (!chSaved0)
            return;
        psz = pszEnd + 1;
        my_assert(*psz != '/' && *psz != '\\');
    }
#undef my_assert
}

/**
 * Corrects all slashes to unix slashes.
 *
 * @returns pszFilename.
 * @param   pszFilename     The filename to correct.
 */
char *fixslash(char *pszFilename)
{
    char *psz = pszFilename;
    while ((psz = strchr(psz, '\\')) != NULL)
        *psz++ = '/';
    return pszFilename;
}

#elif defined(__OS2__)

/**
 * Corrects the case of a path.
 *
 * @param   pszPath     Pointer to the path, both input and output.
 *                      The buffer must be able to hold one more byte than the string length.
 */
void fixcase(char *pszFilename)
{
    return;
}

#else

/**
 * Corrects the case of a path.
 *
 * @param   pszPath     Pointer to the path, both input and output.
 */
void fixcase(char *pszFilename)
{
    char *psz;

    /*
     * Skip the root.
     */
    psz = pszFilename;
    while (*psz == '/')
        psz++;

    /*
     * Iterate all the components.
     */
    while (*psz)
    {
        char  chSlash;
        struct stat s;
        char   *pszStart = psz;

        /*
         * Find the next slash (or end of string) and terminate the string there.
         */
        while (*psz != '/' && *psz)
            *psz++;
        chSlash = *psz;
        *psz = '\0';

        /*
         * Does this part exist?
         * If not we'll enumerate the directory and search for an case-insensitive match.
         */
        if (stat(pszFilename, &s))
        {
            struct dirent  *pEntry;
            DIR            *pDir;
            if (pszStart == pszFilename)
                pDir = opendir(*pszFilename ? pszFilename : ".");
            else
            {
                pszStart[-1] = '\0';
                pDir = opendir(pszFilename);
                pszStart[-1] = '/';
            }
            if (!pDir)
            {
                *psz = chSlash;
                break; /* giving up, if we fail to open the directory. */
            }

            while ((pEntry = readdir(pDir)) != NULL)
            {
                if (!strcasecmp(pEntry->d_name, pszStart))
                {
                    strcpy(pszStart, pEntry->d_name);
                    break;
                }
            }
            closedir(pDir);
            if (!pEntry)
            {
                *psz = chSlash;
                break;  /* giving up if not found. */
            }
        }

        /* restore the slash and press on. */
        *psz = chSlash;
        while (*psz == '/')
            psz++;
    }

    return;
}


#endif


/**
 * 'Optimizes' and corrects the dependencies.
 */
static void depOptimize(int fFixCase)
{
    /*
     * Walk the list correct the names and re-insert them.
     */
    PDEP pDepOrg = g_pDeps;
    PDEP pDep = g_pDeps;
    g_pDeps = NULL;
    for (; pDep; pDep = pDep->pNext)
    {
#ifdef __WIN32__
        char        szFilename[_MAX_PATH + 1];
#else
        char        szFilename[PATH_MAX + 1];
#endif
        char       *pszFilename;
        struct stat s;

        /*
         * Skip some fictive names like <built-in> and <command line>.
         */
        if (    pDep->szFilename[0] == '<'
            &&  pDep->szFilename[pDep->cchFilename - 1] == '>')
            continue;
        pszFilename = pDep->szFilename;

#if !defined(__OS2__) && !defined(__WIN32__)
        /*
         * Skip any drive letters from compilers running in wine.
         */
        if (pszFilename[1] == ':')
            pszFilename += 2;
#endif

        /*
         * The microsoft compilers are notoriously screwing up the casing.
         * This will screw up kmk (/ GNU Make).
         */
        if (fFixCase)
        {
#ifdef __WIN32__
            if (_fullpath(szFilename, pszFilename, sizeof(szFilename)))
                fixslash(szFilename);
            else
#endif
                strcpy(szFilename, pszFilename);
            fixcase(szFilename);
            pszFilename = szFilename;
        }

        /*
         * Check that the file exists before we start depending on it.
         */
        if (stat(pszFilename, &s))
        {
            fprintf(stderr, "kDepPre: Skipping '%s' - %s!\n", szFilename, strerror(errno));
            continue;
        }

        /*
         * Insert the corrected dependency.
         */
        depAdd(pszFilename, strlen(pszFilename));
    }

#if 0 /* waste of time */
    /*
     * Free the old ones.
     */
    while (pDepOrg)
    {
        pDep = pDepOrg;
        pDepOrg = pDepOrg->pNext;
        free(pDep);
    }
#endif
}


/**
 * Prints the dependency chain.
 *
 * @returns Pointer to the allocated dependency.
 * @param   pOutput     Output stream.
 */
static void depPrint(FILE *pOutput)
{
    PDEP pDep;
    for (pDep = g_pDeps; pDep; pDep = pDep->pNext)
        fprintf(pOutput, " \\\n\t%s", pDep->szFilename);
    fprintf(pOutput, "\n\n");
}


/**
 * Prints empty dependency stubs for all dependencies.
 */
static void depPrintStubs(FILE *pOutput)
{
    PDEP pDep;
    for (pDep = g_pDeps; pDep; pDep = pDep->pNext)
        fprintf(pOutput, "%s:\n\n", pDep->szFilename);
}


/* sdbm:
   This algorithm was created for sdbm (a public-domain reimplementation of
   ndbm) database library. it was found to do well in scrambling bits,
   causing better distribution of the keys and fewer splits. it also happens
   to be a good general hashing function with good distribution. the actual
   function is hash(i) = hash(i - 1) * 65599 + str[i]; what is included below
   is the faster version used in gawk. [there is even a faster, duff-device
   version] the magic constant 65599 was picked out of thin air while
   experimenting with different constants, and turns out to be a prime.
   this is one of the algorithms used in berkeley db (see sleepycat) and
   elsewhere. */
static unsigned sdbm(const char *str)
{
    unsigned hash = 0;
    int c;

    while ((c = *(unsigned const char *)str++))
        hash = c + (hash << 6) + (hash << 16) - hash;

    return hash;
}


/**
 * Adds a dependency.
 *
 * @returns Pointer to the allocated dependency.
 * @param   pszFilename     The filename.
 * @param   cchFilename     The length of the filename.
 */
static PDEP depAdd(const char *pszFilename, size_t cchFilename)
{
    unsigned uHash = sdbm(pszFilename);
    PDEP    pDep;
    PDEP    pDepPrev;

    /*
     * Check if we've already got this one.
     */
    pDepPrev = NULL;
    for (pDep = g_pDeps; pDep; pDepPrev = pDep, pDep = pDep->pNext)
        if (    pDep->uHash == uHash
            &&  pDep->cchFilename == cchFilename
            &&  !memcmp(pDep->szFilename, pszFilename, cchFilename))
            return pDep;

    /*
     * Add it.
     */
    pDep = (PDEP)malloc(sizeof(*pDep) + cchFilename);
    if (!pDep)
    {
        fprintf(stderr, "\nOut of memory! (requested %#x bytes)\n\n", sizeof(*pDep) + cchFilename);
        exit(1);
    }

    pDep->cchFilename = cchFilename;
    memcpy(pDep->szFilename, pszFilename, cchFilename + 1);
    pDep->uHash = uHash;

    if (pDepPrev)
    {
        pDep->pNext = pDepPrev->pNext;
        pDepPrev->pNext = pDep;
    }
    else
    {
        pDep->pNext = g_pDeps;
        g_pDeps = pDep;
    }
    return pDep;
}


/**
 * Parses the output from a preprocessor of a C-style language.
 *
 * @returns 0 on success.
 * @returns 1 or other approriate exit code on failure.
 * @param   pInput      Input stream. (probably not seekable)
 */
static int ParseCPrecompiler(FILE *pInput)
{
    enum
    {
        C_DISCOVER = 0,
        C_SKIP_LINE,
        C_PARSE_FILENAME,
        C_EOF
    } enmMode = C_DISCOVER;
    PDEP    pDep = NULL;
    int     ch;
    char    szBuf[8192];

    for (;;)
    {
        switch (enmMode)
        {
            /*
             * Start of line, need to look for '#[[:space]]*line <num> "file"' and '# <num> "file"'.
             */
            case C_DISCOVER:
                /* first find '#' */
                while ((ch = FGETC(pInput)) != EOF)
                    if (!isblank(ch))
                        break;
                if (ch == '#')
                {
                    /* skip spaces */
                    while ((ch = FGETC(pInput)) != EOF)
                        if (!isblank(ch))
                            break;

                    /* check for "line" */
                    if (ch == 'l')
                    {
                        if (    (ch = FGETC(pInput)) == 'i'
                            &&  (ch = FGETC(pInput)) == 'n'
                            &&  (ch = FGETC(pInput)) == 'e')
                        {
                            ch = FGETC(pInput);
                            if (isblank(ch))
                            {
                                /* skip spaces */
                                while ((ch = FGETC(pInput)) != EOF)
                                    if (!isblank(ch))
                                        break;
                            }
                            else
                                ch = 'x';
                        }
                        else
                            ch = 'x';
                    }

                    /* line number */
                    if (ch >= '0' && ch <= '9')
                    {
                        /* skip the number following spaces */
                        while ((ch = FGETC(pInput)) != EOF)
                            if (!isxdigit(ch))
                                break;
                        if (isblank(ch))
                        {
                            while ((ch = FGETC(pInput)) != EOF)
                                if (!isblank(ch))
                                    break;
                            /* quoted filename */
                            if (ch == '"')
                            {
                                enmMode = C_PARSE_FILENAME;
                                break;
                            }
                        }
                    }
                }
                enmMode = C_SKIP_LINE;
                break;

            /*
             * Skip past the end of the current line.
             */
            case C_SKIP_LINE:
                do
                {
                    if (    ch == '\r'
                        ||  ch == '\n')
                        break;
                } while ((ch = FGETC(pInput)) != EOF);
                enmMode = C_DISCOVER;
                break;

            /*
             * Parse the filename.
             */
            case C_PARSE_FILENAME:
            {
                /* retreive and unescape the filename. */
                char   *psz = &szBuf[0];
                while (     (ch = FGETC(pInput)) != EOF
                       &&   psz < &szBuf[sizeof(szBuf) - 1])
                {
                    if (ch == '\\')
                    {
                        ch = FGETC(pInput);
                        switch (ch)
                        {
                            case '\\': ch = '/'; break;
                            case 't':  ch = '\t'; break;
                            case 'r':  ch = '\r'; break;
                            case 'n':  ch = '\n'; break;
                            case 'b':  ch = '\b'; break;
                            default:
                                fprintf(stderr, "warning: unknown escape char '%c'\n", ch);
                                continue;

                        }
                        *psz++ = ch == '\\' ? '/' : ch;
                    }
                    else if (ch != '"')
                        *psz++ = ch;
                    else
                    {
                        size_t cchFilename = psz - &szBuf[0];
                        *psz = '\0';
                        /* compare with current dep, add & switch on mismatch. */
                        if (    !pDep
                            ||  pDep->cchFilename != cchFilename
                            ||  memcmp(pDep->szFilename, szBuf, cchFilename))
                            pDep = depAdd(szBuf, cchFilename);
                        break;
                    }
                }
                enmMode = C_SKIP_LINE;
                break;
            }

            /*
             * Handle EOF.
             */
            case C_EOF:
                if (feof(pInput))
                    return 0;
                enmMode = C_DISCOVER;
                break;
        }
        if (ch == EOF)
            enmMode = C_EOF;
    }

    return 0;
}


static void usage(const char *argv0)
{
    printf("syntax: %s [-l=c] -o <output> -t <target> [-f] [-s] < - | <filename> | -e <cmdline> >\n", argv0);
}

int main(int argc, char *argv[])
{
    int         i;

    /* Arguments. */
    int         iExec = 0;
    FILE       *pOutput = NULL;
    const char *pszOutput = NULL;
    FILE       *pInput = NULL;
    const char *pszTarget = NULL;
    int         fStubs = 0;
    int         fFixCase = 0;
    /* Argument parsing. */
    int         fInput = 0;             /* set when we've found input argument. */

    /*
     * Parse arguments.
     */
    if (argc <= 1)
    {
        usage(argv[0]);
        return 1;
    }
    for (i = 1; i < argc; i++)
    {
        if (argv[i][0] == '-')
        {
            switch (argv[i][1])
            {
                /*
                 * Output file.
                 */
                case 'o':
                {
                    pszOutput = &argv[i][2];
                    if (pOutput)
                    {
                        fprintf(stderr, "%s: syntax error: only one output file!\n", argv[0]);
                        return 1;
                    }
                    if (!*pszOutput)
                    {
                        if (++i >= argc)
                        {
                            fprintf(stderr, "%s: syntax error: The '-o' argument is missing the filename.\n", argv[0]);
                            return 1;
                        }
                        pszOutput = argv[i];
                    }
                    if (pszOutput[0] == '-' && !pszOutput[1])
                        pOutput = stdout;
                    else
                        pOutput = fopen(pszOutput, "w");
                    if (!pOutput)
                    {
                        fprintf(stderr, "%s: error: Failed to create output file '%s'.\n", argv[0], pszOutput);
                        return 1;
                    }
                    break;
                }

                /*
                 * Language spec.
                 */
                case 'l':
                {
                    const char *psz = &argv[i][2];
                    if (*psz == '=')
                        psz++;
                    if (!strcmp(psz, "c"))
                        ;
                    else
                    {
                        fprintf(stderr, "%s: error: The '%s' language is not supported.\n", argv[0], psz);
                        return 1;
                    }
                    break;
                }

                /*
                 * Target name.
                 */
                case 't':
                {
                    if (pszTarget)
                    {
                        fprintf(stderr, "%s: syntax error: only one target!\n", argv[0]);
                        return 1;
                    }
                    pszTarget = &argv[i][2];
                    if (!*pszTarget)
                    {
                        if (++i >= argc)
                        {
                            fprintf(stderr, "%s: syntax error: The '-t' argument is missing the target name.\n", argv[0]);
                            return 1;
                        }
                        pszTarget = argv[i];
                    }
                    break;
                }

                /*
                 * Exec.
                 */
                case 'e':
                {
                    if (++i >= argc)
                    {
                        fprintf(stderr, "%s: syntax error: The '-e' argument is missing the command.\n", argv[0]);
                        return 1;
                    }
                    iExec = i;
                    i = argc - 1;
                    break;
                }

                /*
                 * Pipe input.
                 */
                case '\0':
                {
                    pInput = stdin;
                    fInput = 1;
                    break;
                }

                /*
                 * Fix case.
                 */
                case 'f':
                {
                    fFixCase = 1;
                    break;
                }

                /*
                 * Generate stubs.
                 */
                case 's':
                {
                    fStubs = 1;
                    break;
                }

                /*
                 * Invalid argument.
                 */
                default:
                    fprintf(stderr, "%s: syntax error: Invalid argument '%s'.\n", argv[0], argv[i]);
                    usage(argv[0]);
                    return 1;
            }
        }
        else
        {
            pInput = fopen(argv[i], "r");
            if (!pInput)
            {
                fprintf(stderr, "%s: error: Failed to open input file '%s'.\n", argv[0], argv[i]);
                return 1;
            }
            fInput = 1;
        }

        /*
         * End of the line?
         */
        if (fInput)
        {
            if (++i < argc)
            {
                fprintf(stderr, "%s: syntax error: No arguments shall follow the input spec.\n", argv[0]);
                return 1;
            }
            break;
        }
    }

    /*
     * Got all we require?
     */
    if (!pInput && iExec <= 0)
    {
        fprintf(stderr, "%s: syntax error: No input!\n", argv[0]);
        return 1;
    }
    if (!pOutput)
    {
        fprintf(stderr, "%s: syntax error: No output!\n", argv[0]);
        return 1;
    }
    if (!pszTarget)
    {
        fprintf(stderr, "%s: syntax error: No target!\n", argv[0]);
        return 1;
    }

    /*
     * Spawn process?
     */
    if (iExec > 0)
    {
        fprintf(stderr, "%s: -e is not yet implemented!\n", argv[0]);
        return 1;
    }

    /*
     * Do the parsing.
     */
    i = ParseCPrecompiler(pInput);

    /*
     * Reap child.
     */
    if (iExec > 0)
    {
        // later
    }

    /*
     * Write the dependecy file.
     */
    if (!i)
    {
        depOptimize(fFixCase);
        fprintf(pOutput, "%s:", pszTarget);
        depPrint(pOutput);
        if (fStubs)
            depPrintStubs(pOutput);
    }

    /*
     * Close the output, delete output on failure.
     */
    if (!i && ferror(pOutput))
    {
        i = 1;
        fprintf(stderr, "%s: error: Error writing to '%s'.\n", argv[0], pszOutput);
    }
    fclose(pOutput);
    if (i)
    {
        if (unlink(pszOutput))
            fprintf(stderr, "%s: warning: failed to remove output file '%s' on failure.\n", argv[0], pszOutput);
    }

    return i;
}
