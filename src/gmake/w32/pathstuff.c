/* Path conversion for Windows pathnames.
Copyright (C) 1996, 1997, 1998, 1999, 2000, 2001, 2002, 2003, 2004, 2005,
2006 Free Software Foundation, Inc.
This file is part of GNU Make.

GNU Make is free software; you can redistribute it and/or modify it under the
terms of the GNU General Public License as published by the Free Software
Foundation; either version 2, or (at your option) any later version.

GNU Make is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
A PARTICULAR PURPOSE.  See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with
GNU Make; see the file COPYING.  If not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.  */

#include <Windows.h> /* bird */
#include <stdio.h>   /* bird */
#include <string.h>
#include <stdlib.h>
#include "make.h"
#include "pathstuff.h"

/*
 * Convert delimiter separated vpath to Canonical format.
 */
char *
convert_vpath_to_windows32(char *Path, char to_delim)
{
    char *etok;            /* token separator for old Path */

	/*
	 * Convert all spaces to delimiters. Note that pathnames which
	 * contain blanks get trounced here. Use 8.3 format as a workaround.
	 */
	for (etok = Path; etok && *etok; etok++)
		if (isblank ((unsigned char) *etok))
			*etok = to_delim;

	return (convert_Path_to_windows32(Path, to_delim));
}

/*
 * Convert delimiter separated path to Canonical format.
 */
char *
convert_Path_to_windows32(char *Path, char to_delim)
{
    char *etok;            /* token separator for old Path */
    char *p;            /* points to element of old Path */

    /* is this a multi-element Path ? */
    for (p = Path, etok = strpbrk(p, ":;");
         etok;
         etok = strpbrk(p, ":;"))
        if ((etok - p) == 1) {
            if (*(etok - 1) == ';' ||
                *(etok - 1) == ':') {
                etok[-1] = to_delim;
                etok[0] = to_delim;
                p = ++etok;
                continue;    /* ignore empty bucket */
            } else if (!isalpha ((unsigned char) *p)) {
                /* found one to count, handle things like '.' */
                *etok = to_delim;
                p = ++etok;
            } else if ((*etok == ':') && (etok = strpbrk(etok+1, ":;"))) {
                /* found one to count, handle drive letter */
                *etok = to_delim;
                p = ++etok;
            } else
                /* all finished, force abort */
                p += strlen(p);
        } else {
            /* found another one, no drive letter */
            *etok = to_delim;
            p = ++etok;
	}

    return Path;
}

/* 
 * Corrects the case of a path.
 * Expects a fullpath!
 * Added by bird for the $(abspath ) function and w32ify
 */
void 
w32_fixcase(char *pszPath)
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

/*
 * Convert to forward slashes. Resolve to full pathname optionally
 */
char *
w32ify(char *filename, int resolve)
{
    static char w32_path[FILENAME_MAX];
    char *p;

    if (resolve)
        _fullpath(w32_path, filename, sizeof (w32_path));
    else
        strncpy(w32_path, filename, sizeof (w32_path));

    w32_fixcase(w32_path); /* bird */

    for (p = w32_path; p && *p; p++)
        if (*p == '\\')
            *p = '/';

    return w32_path;
}

char *
getcwd_fs(char* buf, int len)
{
	char *p = getcwd(buf, len);

	if (p) {
		char *q = w32ify(buf, 0);
		strncpy(buf, q, len);
	}

	return p;
}

/* 
 * Workaround for directory names with trailing slashes. 
 * Added by bird reasons stated.
 */
int 
stat(const char *path, struct stat *st)
{
    int rc = _stat(path, (struct _stat *)st);
    if (    rc != 0
        &&  errno == ENOENT
        &&  *path != '\0')
      {
        char *slash = strchr(path, '\0') - 1;
        if (*slash == '/' || *slash == '\\')
          {
            size_t len_path = slash - path + 1;
            char *tmp = alloca(len_path + 4);
            memcpy(tmp, path, len_path);
            tmp[len_path] = '.';
            tmp[len_path + 1] = '\0';
            errno = 0;
            rc = _stat(tmp, (struct _stat *)st);
            if (    rc == 0
                &&  !S_ISDIR(st->st_mode))
              {
                errno = ENOTDIR;
                rc = -1;
              }
          }
      }
    return rc;
}

#ifdef unused
/*
 * Convert delimiter separated pathnames (e.g. PATH) or single file pathname
 * (e.g. c:/foo, c:\bar) to NutC format. If we are handed a string that
 * _NutPathToNutc() fails to convert, just return the path we were handed
 * and assume the caller will know what to do with it (It was probably
 * a mistake to try and convert it anyway due to some of the bizarre things
 * that might look like pathnames in makefiles).
 */
char *
convert_path_to_nutc(char *path)
{
    int  count;            /* count of path elements */
    char *nutc_path;     /* new NutC path */
    int  nutc_path_len;    /* length of buffer to allocate for new path */
    char *pathp;        /* pointer to nutc_path used to build it */
    char *etok;            /* token separator for old path */
    char *p;            /* points to element of old path */
    char sep;            /* what flavor of separator used in old path */
    char *rval;

    /* is this a multi-element path ? */
    for (p = path, etok = strpbrk(p, ":;"), count = 0;
         etok;
         etok = strpbrk(p, ":;"))
        if ((etok - p) == 1) {
            if (*(etok - 1) == ';' ||
                *(etok - 1) == ':') {
                p = ++etok;
                continue;    /* ignore empty bucket */
            } else if (etok = strpbrk(etok+1, ":;"))
                /* found one to count, handle drive letter */
                p = ++etok, count++;
            else
                /* all finished, force abort */
                p += strlen(p);
        } else
            /* found another one, no drive letter */
            p = ++etok, count++;

    if (count) {
        count++;    /* x1;x2;x3 <- need to count x3 */

        /*
         * Hazard a guess on how big the buffer needs to be.
         * We have to convert things like c:/foo to /c=/foo.
         */
        nutc_path_len = strlen(path) + (count*2) + 1;
        nutc_path = xmalloc(nutc_path_len);
        pathp = nutc_path;
        *pathp = '\0';

        /*
         * Loop through PATH and convert one elemnt of the path at at
         * a time. Single file pathnames will fail this and fall
         * to the logic below loop.
         */
        for (p = path, etok = strpbrk(p, ":;");
             etok;
             etok = strpbrk(p, ":;")) {

            /* don't trip up on device specifiers or empty path slots */
            if ((etok - p) == 1)
                if (*(etok - 1) == ';' ||
                    *(etok - 1) == ':') {
                    p = ++etok;
                    continue;
                } else if ((etok = strpbrk(etok+1, ":;")) == NULL)
                    break;    /* thing found was a WINDOWS32 pathname */

            /* save separator */
            sep = *etok;

            /* terminate the current path element -- temporarily */
            *etok = '\0';

#ifdef __NUTC__
            /* convert to NutC format */
            if (_NutPathToNutc(p, pathp, 0) == FALSE) {
                free(nutc_path);
                rval = savestring(path, strlen(path));
                return rval;
            }
#else
            *pathp++ = '/';
            *pathp++ = p[0];
            *pathp++ = '=';
            *pathp++ = '/';
            strcpy(pathp, &p[2]);
#endif

            pathp += strlen(pathp);
            *pathp++ = ':';     /* use Unix style path separtor for new path */
            *pathp   = '\0'; /* make sure we are null terminaed */

            /* restore path separator */
            *etok = sep;

            /* point p to first char of next path element */
            p = ++etok;

        }
    } else {
        nutc_path_len = strlen(path) + 3;
        nutc_path = xmalloc(nutc_path_len);
        pathp = nutc_path;
        *pathp = '\0';
        p = path;
    }

    /*
      * OK, here we handle the last element in PATH (e.g. c of a;b;c)
     * or the path was a single filename and will be converted
     * here. Note, testing p here assures that we don't trip up
     * on paths like a;b; which have trailing delimiter followed by
     * nothing.
     */
    if (*p != '\0') {
#ifdef __NUTC__
        if (_NutPathToNutc(p, pathp, 0) == FALSE) {
            free(nutc_path);
            rval = savestring(path, strlen(path));
            return rval;
        }
#else
        *pathp++ = '/';
        *pathp++ = p[0];
        *pathp++ = '=';
        *pathp++ = '/';
        strcpy(pathp, &p[2]);
#endif
    } else
        *(pathp-1) = '\0'; /* we're already done, don't leave trailing : */

    rval = savestring(nutc_path, strlen(nutc_path));
    free(nutc_path);
    return rval;
}

#endif
