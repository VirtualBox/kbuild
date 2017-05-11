/* $Id: kWorkerTlsXxxK.c 3042 2017-05-11 10:23:12Z knut.osmundsen@oracle.com $ */
/** @file
 * kWorkerTlsXxxK - Loader TLS allocation hack DLL.
 */

/*
 * Copyright (c) 2017 knut st. osmundsen <bird-kBuild-spamx@anduin.net>
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include <windows.h>


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
typedef void KWLDRTLSALLOCATIONHOOK(void *hDll, ULONG idxTls, PIMAGE_TLS_CALLBACK *ppfnTlsCallback);


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
__declspec(dllexport) void __stdcall DummyTlsCallback(void *hDll, DWORD dwReason, void *pvContext);


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** The TLS pointer array. The 2nd entry is NULL and serve to terminate the array.
 * The first entry can be used by kWorker if it needs to. */
__declspec(dllexport) PIMAGE_TLS_CALLBACK g_apfnTlsCallbacks[2] = { DummyTlsCallback, NULL };

/**
 * The TLS index.
 */
__declspec(dllexport) ULONG               g_idxTls              = ~(ULONG)0;

/**
 * Initialization data.
 */
static char const g_abDummy[TLS_SIZE] = {0x42};

/**
 * The TLS directory entry.  Not possible to get more than one from the linker
 * and probably also the loader doesn't want more than one anyway.
 */
#pragma section(".rdata$T", long, read)
__declspec(allocate(".rdata$T")) const IMAGE_TLS_DIRECTORY _tls_used =
{
    (ULONG_PTR)&g_abDummy,
    (ULONG_PTR)&g_abDummy + sizeof(g_abDummy),
    (ULONG_PTR)&g_idxTls,
    (ULONG_PTR)&g_apfnTlsCallbacks,
    0, /* This SizeOfZeroFill bugger doesn't work on w10/amd64 from what I can tell! */
    IMAGE_SCN_ALIGN_32BYTES
};


/*
 * This is just a dummy TLS callback function.
 * We'll be replacing g_apfnTlsCallbacks[0] from kWorker.c after loading it.
 *
 * Note! W10 doesn't seem to want to process the TLS directory if the DLL
 *       doesn't have any imports (to snap).
 */
__declspec(dllexport) void __stdcall DummyTlsCallback(void *hDll, DWORD dwReason, void *pvContext)
{
    (void)hDll; (void)dwReason; (void)pvContext;
    if (dwReason == DLL_PROCESS_ATTACH)
    {
        HMODULE hModExe = (HMODULE)(ULONG_PTR)KWORKER_BASE;
        KWLDRTLSALLOCATIONHOOK *pfnHook = (KWLDRTLSALLOCATIONHOOK *)GetProcAddress(hModExe, "kwLdrTlsAllocationHook");
        if (pfnHook)
        {
            pfnHook(hDll, g_idxTls, &g_apfnTlsCallbacks[0]);
            return;
        }
        __debugbreak();
    }
}


/*
 * Dummy DLL entry point to avoid dragging in unnecessary CRT stuff. kWorkerTls1K!_tls_index
 */
BOOL __stdcall DummyDllEntry(void *hDll, DWORD dwReason, void *pvContext)
{
    (void)hDll; (void)dwReason; (void)pvContext;
    return TRUE;
}

