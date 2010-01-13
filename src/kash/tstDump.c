/* $Id: tstDump.c 2376 2010-01-13 01:45:49Z knut.osmundsen@oracle.com $ */
/** @file
 * tstDump - dump inherited file handle information on Windows.
 */

/*
 * Copyright (c) 2010  knut st. osmundsen <bird-kBuild-spamix@anduin.net>
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

/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
#include <Windows.h>
#include <stdio.h>


int main()
{
    STARTUPINFO Info;
    GetStartupInfo(&Info);

    printf("tst: hStdInput =%p / %p\n", Info.hStdInput,  GetStdHandle(STD_INPUT_HANDLE));
    printf("tst: hStdOutput=%p / %p\n", Info.hStdOutput, GetStdHandle(STD_OUTPUT_HANDLE));
    printf("tst: hStdError =%p / %p\n", Info.hStdError,  GetStdHandle(STD_ERROR_HANDLE));
    printf("tst: cbReserved2=%d (%#x) lpReserved2=%p\n",
           Info.cbReserved2, Info.cbReserved2, Info.lpReserved2);

    if (Info.cbReserved2 > sizeof(int) && Info.lpReserved2)
    {
        int             count = *(int *)Info.lpReserved2;
        unsigned char  *paf   = (unsigned char *)Info.lpReserved2 + sizeof(int);
        HANDLE         *pah   = (HANDLE *)(paf + count);
        int             i;

        printf("tst: count=%d\n", count);
        for (i = 0; i < count; i++)
        {
            if (paf[i] == 0 && pah[i] == INVALID_HANDLE_VALUE)
                continue;

            printf("tst: #%02d: native=%#p flags=%#x", i, pah[i], paf[i]);
            if (paf[i] & 0x01)  printf(" FOPEN");
            if (paf[i] & 0x02)  printf(" FEOFLAG");
            if (paf[i] & 0x02)  printf(" FEOFLAG");
            if (paf[i] & 0x04)  printf(" FCRLF");
            if (paf[i] & 0x08)  printf(" FPIPE");
            if (paf[i] & 0x10)  printf(" FNOINHERIT");
            if (paf[i] & 0x20)  printf(" FAPPEND");
            if (paf[i] & 0x40)  printf(" FDEV");
            if (paf[i] & 0x80)  printf(" FTEXT");
            printf("\n");
        }
    }

    return 0;
}

