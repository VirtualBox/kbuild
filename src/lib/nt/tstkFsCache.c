/* $Id: tstkFsCache.c 3380 2020-06-12 00:55:31Z knut.osmundsen@oracle.com $ */
/** @file
 * kFsCache testcase.
 */

/*
 * Copyright (c) 2020 knut st. osmundsen <bird-kBuild-spamx@anduin.net>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 * Alternatively, the content of this file may be used under the terms of the
 * GPL version 2 or later, or LGPL version 2.1 or later.
 */

/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
#include <direct.h>
#include <errno.h>
#include <process.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "kFsCache.h"

#include <windows.h>


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
static unsigned g_cErrors = 0;


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#define CHECK_RETV(a_Expr) do { \
        if (!(a_Expr)) \
        { \
            g_cErrors++; \
            fprintf(stderr, "error(%u): %s\n", __LINE__, #a_Expr);\
            return; \
        } \
    } while (0)

#define CHECK(a_Expr) do { \
        if (!(a_Expr)) \
        { \
            g_cErrors++; \
            fprintf(stderr, "error(%u): %s\n", __LINE__, #a_Expr);\
        } \
    } while (0)

static int myMkDir(const char *pszPath)
{
    if (_mkdir(pszPath) == 0)
        return 0;
    fprintf(stderr, "_mkdir(%s) -> errno=%d\n", pszPath, errno);
    return -1;
}

static int myCreateFile(const char *pszPath)
{
    FILE *pFile = fopen(pszPath, "w");
    if (pFile)
    {
        fclose(pFile);
        return 0;
    }
    fprintf(stderr, "fopen(%s,w) -> errno=%d\n", pszPath, errno);
    return -1;
}

static void test1(const char *pszWorkDir)
{
    char            szPath[4096];
    size_t          cchWorkDir = strlen(pszWorkDir);
    PKFSCACHE       pCache;
    KFSLOOKUPERROR  enmLookupError;
    PKFSOBJ         pFsObj;

    CHECK_RETV(cchWorkDir < sizeof(szPath) - 1024);
    memcpy(szPath, pszWorkDir, cchWorkDir);
    cchWorkDir += sprintf(&szPath[cchWorkDir], "\\tstkFsCache%u", _getpid());
    CHECK_RETV(myMkDir(szPath) == 0);

    pCache = kFsCacheCreate(KFSCACHE_F_MISSING_OBJECTS | KFSCACHE_F_MISSING_PATHS);
    CHECK_RETV(pCache != NULL);

    enmLookupError = (KFSLOOKUPERROR)-1;
    CHECK((pFsObj = kFsCacheLookupA(pCache, szPath, &enmLookupError)) != NULL);

#if 0
    /*
     * Accidentally left out the '\' in front of the filename, so it ended up in
     * a temp dir with almost 1000 files and that triggered a refresh issue.
     */
    /* Negative lookup followed by creation of that file. */
    enmLookupError = (KFSLOOKUPERROR)-1;
    sprintf(&szPath[cchWorkDir], "file1.txt");
    CHECK((pFsObj = kFsCacheLookupA(pCache, szPath, &enmLookupError)) != NULL);
    if (pFsObj)
        CHECK(pFsObj->bObjType == KFSOBJ_TYPE_MISSING);

    CHECK(myCreateFile(szPath) == 0);

    CHECK((pFsObj = kFsCacheLookupA(pCache, szPath, &enmLookupError)) != NULL);
    if (pFsObj)
        CHECK(pFsObj->bObjType == KFSOBJ_TYPE_MISSING);

    kFsCacheInvalidateAll(pCache);
    CHECK((pFsObj = kFsCacheLookupA(pCache, szPath, &enmLookupError)) != NULL);
    if (pFsObj)
    {
        CHECK(pFsObj->bObjType == KFSOBJ_TYPE_FILE);
        if (pFsObj->bObjType != KFSOBJ_TYPE_FILE)
            fprintf(stderr, "bObjType=%d\n", pFsObj->bObjType);
    }
#endif

    /*
     * Try emulate the temp issue above.  Seem to require several files.
     */
    szPath[cchWorkDir++] = '\\';
    sprintf(&szPath[cchWorkDir], "longfilename1.txt");
    CHECK(myCreateFile(szPath) == 0);
    sprintf(&szPath[cchWorkDir], "longfilename2.txt");
    CHECK(myCreateFile(szPath) == 0);
#if 1
    /* no file 3 */
    sprintf(&szPath[cchWorkDir], "longfilename4.txt");
    CHECK(myCreateFile(szPath) == 0);
    sprintf(&szPath[cchWorkDir], "longfilename5.txt");
    CHECK(myCreateFile(szPath) == 0);
    /* no file 6 */
    sprintf(&szPath[cchWorkDir], "longfilename7.txt");
    CHECK(myCreateFile(szPath) == 0);
#endif

    enmLookupError = (KFSLOOKUPERROR)-1;
    sprintf(&szPath[cchWorkDir], "longfilename3.txt");
    CHECK((pFsObj = kFsCacheLookupA(pCache, szPath, &enmLookupError)) != NULL);
    CHECK(pFsObj && pFsObj->bObjType == KFSOBJ_TYPE_MISSING);

    enmLookupError = (KFSLOOKUPERROR)-1;
    sprintf(&szPath[cchWorkDir], "longfilename6.txt");
    CHECK((pFsObj = kFsCacheLookupA(pCache, szPath, &enmLookupError)) != NULL);
    CHECK(pFsObj && pFsObj->bObjType == KFSOBJ_TYPE_MISSING);

Sleep(100);
    sprintf(&szPath[cchWorkDir], "longfilename3.txt");
    CHECK(myCreateFile(szPath) == 0);
    sprintf(&szPath[cchWorkDir], "longfilename6.txt");
    CHECK(myCreateFile(szPath) == 0);

    sprintf(&szPath[cchWorkDir], "longfilename3.txt");
    CHECK((pFsObj = kFsCacheLookupA(pCache, szPath, &enmLookupError)) != NULL);
    CHECK(pFsObj && pFsObj->bObjType == KFSOBJ_TYPE_MISSING);

    sprintf(&szPath[cchWorkDir], "longfilename6.txt");
    CHECK((pFsObj = kFsCacheLookupA(pCache, szPath, &enmLookupError)) != NULL);
    CHECK(pFsObj && pFsObj->bObjType == KFSOBJ_TYPE_MISSING);

    kFsCacheInvalidateAll(pCache);

    sprintf(&szPath[cchWorkDir], "longfilename3.txt");
    CHECK((pFsObj = kFsCacheLookupA(pCache, szPath, &enmLookupError)) != NULL);
    if (pFsObj)
    {
        CHECK(pFsObj->bObjType == KFSOBJ_TYPE_FILE);
        if (pFsObj->bObjType != KFSOBJ_TYPE_FILE)
            fprintf(stderr, "bObjType=%d\n", pFsObj->bObjType);
    }

    sprintf(&szPath[cchWorkDir], "longfilename6.txt");
    CHECK((pFsObj = kFsCacheLookupA(pCache, szPath, &enmLookupError)) != NULL);
    if (pFsObj)
    {
        CHECK(pFsObj->bObjType == KFSOBJ_TYPE_FILE);
        if (pFsObj->bObjType != KFSOBJ_TYPE_FILE)
            fprintf(stderr, "bObjType=%d\n", pFsObj->bObjType);
    }
}

static int usage(int rcExit)
{
    printf("usage: tstkFsCache [--workdir dir]\n"
           "\n"
           "Test program of the kFsCache.  May leave stuff behind in the work\n"
           "directory requiring manual cleanup.\n"
           );
    return rcExit;
}

int main(int argc, char **argv)
{
    const char *pszWorkDir = NULL;
    int i;

    /*
     * Parse arguments.
     */
    for (i = 1; i < argc; i++)
    {
        if (argv[i][0] == '-')
        {
            const char *pszValue;
            const char *psz = &argv[i][1];
            char chOpt;
            chOpt = *psz++;
            if (chOpt == '-')
            {
                /* Convert long to short option. */
                if (!strcmp(psz, "workdir"))
                    chOpt = 'w';
                else if (!strcmp(psz, "help"))
                    chOpt = '?';
                else if (!strcmp(psz, "version"))
                    chOpt = 'V';
                else
                    return usage(2);
                psz = "";
            }

            /*
             * Requires value?
             */
            switch (chOpt)
            {
                case 'w':
                    if (*psz)
                        pszValue = psz;
                    else if (++i < argc)
                        pszValue = argv[i];
                    else
                    {
                        fprintf(stderr, "The '-%c' option takes a value.\n", chOpt);
                        return 2;
                    }
                    break;

                default:
                    pszValue = NULL;
                    break;
            }

            switch (chOpt)
            {
                case 'w':
                    pszWorkDir = pszValue;
                    break;

                case '?':
                    return usage(0);
                case 'V':
                    printf("0.0.0\n");
                    return 0;

                /*
                 * Invalid argument.
                 */
                default:
                    fprintf(stderr, "syntax error: Invalid option '%s'.\n", argv[i]);
                    return 2;
            }
        }
        else
        {
            fprintf(stderr, "syntax error: Invalid argument '%s'.\n", argv[i]);
            return 2;
        }
    }

    /*
     * Resolve defaults.
     */
    if (!pszWorkDir)
    {
        pszWorkDir = getenv("TEMP");
        if (!pszWorkDir)
            pszWorkDir = ".";
    }

    /*
     * Do the testing.
     */
    test1(pszWorkDir);

    if (!g_cErrors)
        printf("Success!\n");
    else
        printf("Failed - %u errors!\n", g_cErrors);
    return g_cErrors == 0 ? 0 : 1;
}


