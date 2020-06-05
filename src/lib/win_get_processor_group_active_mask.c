/* $Id: win_get_processor_group_active_mask.c 3356 2020-06-05 02:09:14Z knut.osmundsen@oracle.com $ */
/** @file
 * win_get_processor_group_active_mask - Helper.
 */

/*
 * Copyright (c) 2020 knut st. osmundsen <bird-kBuild-spamxx@anduin.net>
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

/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include <string.h>
#include <Windows.h>
#include "win_get_processor_group_active_mask.h"


KAFFINITY win_get_processor_group_active_mask(unsigned iGroup)
{
    union
    {
        SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX     Info;
        SYSTEM_INFO                                 SysInfo;
        char                                        ab[8192];
    } uBuf;
    typedef BOOL (WINAPI *PFNGETLOGICALPROCESSORINFORMATIONEX)(LOGICAL_PROCESSOR_RELATIONSHIP,
                                                               SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX *, DWORD *);
    static PFNGETLOGICALPROCESSORINFORMATIONEX volatile s_pfnGetLogicalProcessorInformationEx = NULL;
    static HMODULE                             volatile s_hmodKernel32 = NULL;
    PFNGETLOGICALPROCESSORINFORMATIONEX pfnGetLogicalProcessorInformationEx = s_pfnGetLogicalProcessorInformationEx;
    if (!pfnGetLogicalProcessorInformationEx)
    {
        if (!s_hmodKernel32)
        {
            HMODULE hmodKernel32 = GetModuleHandleW(L"KERNEL32.DLL");
            s_hmodKernel32 = hmodKernel32;
            s_pfnGetLogicalProcessorInformationEx
                = pfnGetLogicalProcessorInformationEx
                = (PFNGETLOGICALPROCESSORINFORMATIONEX)GetProcAddress(hmodKernel32, "GetLogicalProcessorInformationEx");
        }
    }

    SetLastError(0);
    if (pfnGetLogicalProcessorInformationEx)
    {
        DWORD cbBuf;
        memset(&uBuf, 0, sizeof(uBuf));
        uBuf.Info.Size = cbBuf = sizeof(uBuf);
        if (pfnGetLogicalProcessorInformationEx(RelationGroup, &uBuf.Info, &cbBuf))
        {
            SetLastError(0);
            if (iGroup < uBuf.Info.Group.MaximumGroupCount)
                return uBuf.Info.Group.GroupInfo[iGroup].ActiveProcessorMask;
        }
    }
    else if (iGroup == 0)
    {
        GetSystemInfo(&uBuf.SysInfo);
        return uBuf.SysInfo.dwActiveProcessorMask;
    }
    return 0;
}

