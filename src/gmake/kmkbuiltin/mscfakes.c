/* $Id: mscfakes.c 370 2005-12-18 03:48:02Z knut.osmundsen@oracle.com $ */
/** @file
 *
 * Fake Unix stuff for MSC.
 *
 * Copyright (c) 2005 knut st. osmundsen <bird@innotek.de>
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with This program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */


#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <io.h>
#include <fcntl.h>
#include "err.h"
#include "mscfakes.h"
#undef mkdir


char *dirname(char *path)
{         
    /** @todo later */
    return path;
}


int link(const char *pszDst, const char *pszLink)
{
    errno = ENOSYS;
    err(1, "link() is not implemented on windows!");
    return -1;
}


int mkdir_msc(const char *path, mode_t mode)
{
    int rc = mkdir(path);
    if (rc)
    {
        int len = strlen(path);
        if (len > 0 && (path[len - 1] == '/' || path[len - 1] == '\\'))
        {
            char *str = strdup(path);
            while (len > 0 && (str[len - 1] == '/' || str[len - 1] == '\\'))
                str[--len] = '\0';
            rc = mkdir(str);
            free(str);
        }
    }
    return rc;
}


static int doname(char *pszX, char *pszEnd)
{
    static char s_szChars[] = "Xabcdefghijklmnopqrstuwvxyz1234567890";
    int rc = 0;
    do
    {
        char ch;

        pszEnd++;
        ch = *(strchr(s_szChars, *pszEnd) + 1);
        if (ch)
        {
            *pszEnd = ch;
            return 0;
        }
        *pszEnd = 'a';
    } while (pszEnd != pszX);
    return 1;
}


int mkstemp(char *temp)
{
    char *pszX = strchr(temp, 'X');
    char *pszEnd = strchr(pszX, '\0');
    int cTries = 1000;
    while (--cTries > 0)
    {
        int fd;
        if (doname(pszX, pszEnd))
            return -1;
        fd = open(temp, _O_EXCL | _O_CREAT | _O_BINARY | _O_RDWR, 0777);
        if (fd >= 0)
            return fd;
    }
    return -1;
}


int symlink(const char *pszDst, const char *pszLink)
{
    errno = ENOSYS;
    err(1, "symlink() is not implemented on windows!");
    return -1;
}


#if _MSC_VER < 1400
int snprintf(char *buf, size_t size, const char *fmt, ...)
{
    int cch;
    va_list args;
    va_start(args, fmt);
    cch = vsprintf(buf, fmt, args);
    va_end(args);
    return cch;
}
#endif 


int utimes(const char *pszPath, const struct timeval *paTimes)
{
    /** @todo implement me! */
    return 0;
}


int writev(int fd, const struct iovec *vector, int count)
{
    int size = 0;
    int i;
    for (i = 0; i < count; i++)
    {
        int cb = write(fd, vector[i].iov_base, vector[i].iov_len);
        if (cb < 0)
            return -1;
        size += cb;
    }
    return size;
}

