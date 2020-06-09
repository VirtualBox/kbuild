/* $Id: kWorkerTlsXxxK.c 3366 2020-06-09 23:53:39Z knut.osmundsen@oracle.com $ */
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
typedef void KWLDRTLSCALLBACK(void *hDll, DWORD dwReason, void *pvContext, void *pvWorkerModule);
typedef KWLDRTLSCALLBACK *PKWLDRTLSCALLBACK;
typedef PKWLDRTLSCALLBACK KWLDRTLSALLOCATIONHOOK(void *hDll, ULONG idxTls, char *pabInitData, void **ppvWorkerModule);


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
__declspec(dllexport) void __stdcall DummyTlsCallback(void *hDll, DWORD dwReason, void *pvContext);


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** The TLS pointer array. The 2nd entry is NULL and serve to terminate the array.
 * The first entry can be used by kWorker if it needs to. */
__declspec(dllexport) PIMAGE_TLS_CALLBACK   g_apfnTlsCallbacks[2]   = { DummyTlsCallback, NULL };

/**
 * The TLS index.
 */
__declspec(dllexport) ULONG                 g_idxTls                = ~(ULONG)0;

/**
 * Callback context.
 */
__declspec(dllexport) void                 *g_pvWorkerModule        = NULL;

/**
 * Regular callback method (returned by kwLdrTlsAllocationHook).
 */
__declspec(dllexport) PKWLDRTLSCALLBACK     g_pfnWorkerCallback     = NULL;



/**
 * Initialization data.
 * kWorker will copy the init data of the target DLL here.
 */
static char g_abInitData[TLS_SIZE] = {0};

/**
 * The TLS directory entry.  Not possible to get more than one from the linker
 * and probably also the loader doesn't want more than one anyway.
 */
#pragma section(".rdata$T", long, read)
__declspec(allocate(".rdata$T")) const IMAGE_TLS_DIRECTORY _tls_used =
{
    (ULONG_PTR)&g_abInitData,
    (ULONG_PTR)&g_abInitData + sizeof(g_abInitData),
    (ULONG_PTR)&g_idxTls,
    (ULONG_PTR)&g_apfnTlsCallbacks,
    0, /* This SizeOfZeroFill bugger doesn't work on w10/amd64 from what I can tell! */
    IMAGE_SCN_ALIGN_32BYTES
};


/**
 * Just a dummy callback function in case the allocation hook gambit fails below
 * (see KWLDRTLSCALLBACK).
 */
static void DummyWorkerCallback(void *hDll, DWORD dwReason, void *pvContext, void *pvWorkerModule)
{
    (void)hDll; (void)dwReason; (void)pvContext; (void)pvWorkerModule;
}


/*
 * This is just a dummy TLS callback function.
 * We'll be replacing g_apfnTlsCallbacks[0] from kWorker.c after loading it.
 *
 * Note! W10 doesn't seem to want to process the TLS directory if the DLL
 *       doesn't have any imports (to snap).
 */
__declspec(dllexport) void __stdcall DummyTlsCallback(void *hDll, DWORD dwReason, void *pvContext)
{
    if (g_pfnWorkerCallback)
        g_pfnWorkerCallback(hDll, dwReason, pvContext, g_pvWorkerModule);
    else
    {
        g_pfnWorkerCallback = DummyWorkerCallback;
        if (dwReason == DLL_PROCESS_ATTACH)
        {
            HMODULE hModExe = GetModuleHandleW(NULL);
            KWLDRTLSALLOCATIONHOOK *pfnHook = (KWLDRTLSALLOCATIONHOOK *)GetProcAddress(hModExe, "kwLdrTlsAllocationHook");
            if (pfnHook)
                g_pfnWorkerCallback = pfnHook(hDll, g_idxTls, g_abInitData, &g_pvWorkerModule);
            else
                __debugbreak();
        }
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

