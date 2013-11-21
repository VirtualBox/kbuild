/* $Id: tstNtStat.c 2702 2013-11-21 00:11:08Z knut.osmundsen@oracle.com $ */
/** @file
 * Manual lstat/stat testcase.
 */

/*
 * Copyright (c) 2013 knut st. osmundsen <bird-kBuild-spamx@anduin.net>
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
#include <stdio.h>
#include <errno.h>
#include "ntstat.h"


static int IsLeapYear(int iYear)
{
    return iYear % 4 == 0
        && (   iYear % 100 != 0
            || iYear % 400 == 0);
}

static int DaysInMonth(int iYear, int iMonth)
{
    switch (iMonth)
    {
        case 1:
        case 3:
        case 5:
        case 7:
        case 8:
        case 10:
        case 12:
            return 31;
        case 4:
        case 6:
        case 9:
        case 11:
            return 30;
        case 2:
            return IsLeapYear(iYear) ? 29 : 28;

        default:
            *(void **)(size_t)iMonth = 0; /* crash! */
            return 0;
    }
}


static char *FormatTimeSpec(char *pszBuf, BirdTimeSpec_T const *pTimeSpec)
{
    if (pTimeSpec->tv_sec >= 0)
    {
        int     iYear  = 1970;
        int     iMonth = 1;
        int     iDay   = 1;
        int     iHour  = 0;
        int     iMin   = 0;
        int     iSec   = 0;
        __int64 cSecs  = pTimeSpec->tv_sec;

        /* lazy bird approach, find date, day by day */
        while (cSecs >= 24*3600)
        {
            if (   iDay < 28
                || iDay < DaysInMonth(iYear, iMonth))
                iDay++;
            else
            {
                if (iMonth < 12)
                    iMonth++;
                else
                {
                    iYear++;
                    iMonth = 1;
                }
                iDay = 1;
            }
            cSecs -= 24*3600;
        }

        iHour  = (int)cSecs / 3600;
        cSecs %= 3600;
        iMin   = (int)cSecs / 60;
        iSec   = (int)cSecs % 60;

        sprintf(pszBuf, "%04d-%02d-%02dT%02u:%02u:%02u.%09u (%I64d.%09u)",
                iYear, iMonth, iDay, iHour, iMin, iSec, pTimeSpec->tv_nsec,
                pTimeSpec->tv_sec, pTimeSpec->tv_nsec);
    }
    else
        sprintf(pszBuf, "%I64d.%09u (before 1970-01-01)", pTimeSpec->tv_sec, pTimeSpec->tv_nsec);
    return pszBuf;
}


int main(int argc, char **argv)
{
    int rc = 0;
    int i;

    for (i = 1; i < argc; i++)
    {
        struct stat st;
        if (lstat(argv[i], &st) == 0)
        {
            char szBuf[256];
            printf("%s:\n", argv[i]);
            printf("  st_mode:          %o\n", st.st_mode);
            printf("  st_dirsymlink:    %d\n", st.st_dirsymlink);
            printf("  st_size:          %I64u (%#I64x)\n", st.st_size, st.st_size);
            printf("  st_atim:          %s\n", FormatTimeSpec(szBuf, &st.st_atim));
            printf("  st_mtim:          %s\n", FormatTimeSpec(szBuf, &st.st_mtim));
            printf("  st_ctim:          %s\n", FormatTimeSpec(szBuf, &st.st_ctim));
            printf("  st_birthtim:      %s\n", FormatTimeSpec(szBuf, &st.st_birthtim));
            printf("  st_ino:           %#I64x\n", st.st_ino);
            printf("  st_dev:           %#I64x\n", st.st_dev);
            printf("  st_nlink:         %u\n", st.st_nlink);
            printf("  st_rdev:          %#x\n", st.st_rdev);
            printf("  st_uid:           %d\n", st.st_uid);
            printf("  st_gid:           %d\n", st.st_gid);
            printf("  st_blksize:       %d (%#x)\n", st.st_blksize, st.st_blksize);
            printf("  st_blocks:        %I64u (%#I64x)\n", st.st_blocks, st.st_blocks);
        }
        else
        {
            fprintf(stderr, "stat failed on '%s': errno=%u\n", argv[i], errno);
            rc = 1;
        }
    }
    return rc;
}

