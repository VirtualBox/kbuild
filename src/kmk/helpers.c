/* $Id: helpers.c 51 2003-04-07 01:30:32Z knut.osmundsen@oracle.com $
 *
 * Helpers.
 *
 * Copyright (c) 2003 knut st. osmundsen <bird@anduin.net>
 *
 */

/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
#ifdef USE_KLIB
    #include <kLib/kLib.h>
#endif
#include <stdlib.h>
#include <io.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdarg.h>
#include <stdio.h>
#include <err.h>

#ifdef OS2
    #define INCL_BASE
    #include <os2.h>
#endif


#ifdef OS2
/**
 * Resolve pszFileName to a full name.
 * @return pszResolvedName on success.
 * @returns NULL if not found.
 */
char *realpath(const char *pszFileName, char *pszResolvedName)
{
    #ifdef USE_KLIB
    if (kPathCanonifyEx(pszFileName, NULL, '/', '/', pszResolvedName, KFILE_LENGTH))
        if (kPathExist(pszFileName))
            return pszResolvedName;
    #else
    if (_fullpath(pszResolvedName, pszFileName, _MAX_PATH))
    {
        struct stat s;
        if (!stat(pszResolvedName, &s))
            return pszResolvedName;
    }
    #endif
    return NULL;
}
#endif

#ifdef OS2
void    err(int flags, const char *pszFormat, ...)
{
    va_list args;
    va_start(args, pszFormat);
    vfprintf(stderr, pszFormat, args);
    va_end(args);
    fprintf(stderr, "\n");
}

void    errx(int flags, const char *pszFormat, ...)
{
    va_list args;
    va_start(args, pszFormat);
    vfprintf(stderr, pszFormat, args);
    va_end(args);
    fprintf(stderr, "\n");
}

void    warnx(const char *pszFormat, ...)
{
    va_list args;
    va_start(args, pszFormat);
    vfprintf(stderr, pszFormat, args);
    va_end(args);
    fprintf(stderr, "\n");
}

void    warn(const char *pszFormat, ...)
{
    va_list args;
    va_start(args, pszFormat);
    vfprintf(stderr, pszFormat, args);
    va_end(args);
    fprintf(stderr, "\n");
}

#endif
