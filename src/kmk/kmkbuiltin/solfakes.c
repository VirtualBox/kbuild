/* $Id: $ */
/** @file
 *
 * Fake Unix stuff for Solaris.
 *
 * Copyright (c) 2005-2007 knut st. osmundsen <bird-kBuild-spam@anduin.net>
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


#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include "solfakes.h"


int asprintf(char **strp, const char *fmt, ...)
{
    int rc;
    va_list va;
    va_start(va, fmt);
    rc = vasprintf(strp, fmt, va);
    va_end(va);
    return rc;
}


int vasprintf(char **strp, const char *fmt, va_list va)
{
    int rc;
    char *psz;
    size_t cb = 1024;

    *strp = NULL;
    for (;;)
    {
        va_list va2;

        psz = malloc(cb);
        if (!psz)
            return -1;

#ifdef va_copy
        va_copy(va2, va);
        rc = snprintf(psz, cb, fmt, va2);
        va_end(va2);
#else
        va2 = va;
        rc = snprintf(psz, cb, fmt, va2);
#endif
        if (rc < 0 || (size_t)rc < cb)
            break;
        cb *= 2;
        free(psz);
    }

    *strp = psz;
    return rc;
}

