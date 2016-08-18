/* $Id: kWorker.c 2831 2016-08-18 22:10:08Z knut.osmundsen@oracle.com $ */
/** @file
 * kWorker - experimental process reuse worker for Windows.
 */

/*
 * Copyright (c) 2016 knut st. osmundsen <bird-kBuild-spamx@anduin.net>
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
#include <k/kHlp.h>
#include <k/kLdr.h>

#include <stdio.h>
#include <intrin.h>
#include <setjmp.h>

#include <nt/ntstat.h>
/* lib/nt_fullpath.c */
extern void nt_fullpath(const char *pszPath, char *pszFull, size_t cchFull);
#include <Windows.h>


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
typedef enum KWLOCATION
{
    KWLOCATION_INVALID = 0,
    KWLOCATION_EXE_DIR,
    KWLOCATION_IMPORTER_DIR,
    KWLOCATION_SYSTEM32
} KWLOCATION;

typedef enum KWMODSTATE
{
    KWMODSTATE_INVALID = 0,
    KWMODSTATE_NEEDS_BITS,
    KWMODSTATE_NEEDS_INIT,
    KWMODSTATE_BEING_INITED,
    KWMODSTATE_INIT_FAILED,
    KWMODSTATE_READY,
} KWMODSTATE;

typedef struct KWMODULE *PKWMODULE;
typedef struct KWMODULE
{
    /** Pointer to the next image. */
    PKWMODULE           pNext;
    /** The normalized path to the image. */
    const char         *pszPath;
    /** The hash of the program path. */
    KU32                uHashPath;
    /** Number of references. */
    KU32                cRefs;
    /** The offset of the filename in pszPath. */
    KU16                offFilename;
    /** Set if executable. */
    KBOOL               fExe;
    /** Set if native module entry. */
    KBOOL               fNative;
    /** Loader module handle. */
    PKLDRMOD            pLdrMod;

    union
    {
        /** Data for a manually loaded image. */
        struct
        {
            /** The of the loaded image bits. */
            size_t              cbImage;
            /** Where we load the image. */
            void               *pvLoad;
            /** Virgin copy of the image. */
            void               *pvCopy;
            /** Ldr pvBits argument.  This is NULL till we've successfully resolved
             *  the imports. */
            void               *pvBits;
            /** The state. */
            KWMODSTATE          enmState;
            /** Number of imported modules. */
            size_t              cImpMods;
            /** Import array (variable size). */
            PKWMODULE           apImpMods[1];
        } Manual;
    } u;
} KWMODULE;

typedef enum KWTOOLTYPE
{
    KWTOOLTYPE_INVALID = 0,
    KWTOOLTYPE_SANDBOXED,
    KWTOOLTYPE_WATCOM,
    KWTOOLTYPE_EXEC,
    KWTOOLTYPE_END
} KWTOOLTYPE;

typedef struct KWTOOL *PKWTOOL;
typedef struct KWTOOL
{
    /** Pointer to the next in the hash collision chain. */
    PKWTOOL             pNext;
    /** The normalized path to the program. */
    const char         *pszPath;
    /** The hash of the program path. */
    KU32                uHashPath;
    /** The kind of tool. */
    KWTOOLTYPE          enmType;

    union
    {
        struct
        {
            /** The executable. */
            PKWMODULE   pExe;
        } Sandboxed;
    } u;
} KWTOOL;


typedef struct KWSANDBOX *PKWSANDBOX;
typedef struct KWSANDBOX
{
    /** The tool currently running in the sandbox. */
    PKWTOOL     pTool;
    /** Jump buffer. */
    jmp_buf     JmpBuf;
    /** The thread ID of the main thread (owner of JmpBuf). */
    DWORD       idMainThread;
    /** The exit code in case of longjmp.   */
    int         rcExitCode;


} KWSANDBOX;

/** Replacement function entry. */
typedef struct KWREPLACEMENTFUNCTION
{
    /** The function name. */
    const char *pszFunction;
    /** The length of the function name. */
    KSIZE       cchFunction;
    /** The module name (optional). */
    const char *pszModule;
    /** The replacement function. */
    KUPTR       pfnReplacement;
} KWREPLACEMENTFUNCTION;
typedef KWREPLACEMENTFUNCTION const *PCKWREPLACEMENTFUNCTION;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** The currently active sandbox. */
static PKWSANDBOX   g_pSandbox;

/** Module hash table. */
static PKWMODULE    g_apModules[127];

/** Tool hash table. */
static PKWTOOL      g_apTools[63];

/** Create a larget BSS blob that with help of /IMAGEBASE:0x10000 should
 * cover the default executable link address of 0x400000. */
#pragma section("DefLdBuf", write, execute, read)
__declspec(allocate("DefLdBuf"))
static KU8          g_abDefLdBuf[16*1024*1024];

/* Further down. */
extern KWREPLACEMENTFUNCTION const g_aSandboxReplacements[];
extern KU32                  const g_cSandboxReplacements;


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static FNKLDRMODGETIMPORT kwLdrModuleGetImportCallback;
static int kwLdrModuleResolveAndLookup(const char *pszName, PKWMODULE pExe, PKWMODULE pImporter, PKWMODULE *ppMod);


/**
 * Normalizes the path so we get a consistent hash.
 *
 * @returns status code.
 * @param   pszPath             The path.
 * @param   pszNormPath         The output buffer.
 * @param   cbNormPath          The size of the output buffer.
 */
static int kwPathNormalize(const char *pszPath, char *pszNormPath, size_t cbNormPath)
{
    char *pchSlash;
    nt_fullpath(pszPath, pszNormPath, cbNormPath);

    pchSlash = kHlpStrChr(pszNormPath, '/');
    while (pchSlash)
    {
        *pchSlash = '\\';
        pchSlash = kHlpStrChr(pchSlash + 1, '/');
    }

    return 0;
}


/**
 * Hashes a string.
 *
 * @returns 32-bit string hash.
 * @param   pszString           String to hash.
 */
static KU32 kwStrHash(const char *pszString)
{
    /* This algorithm was created for sdbm (a public-domain reimplementation of
       ndbm) database library. it was found to do well in scrambling bits,
       causing better distribution of the keys and fewer splits. it also happens
       to be a good general hashing function with good distribution. the actual
       function is hash(i) = hash(i - 1) * 65599 + str[i]; what is included below
       is the faster version used in gawk. [there is even a faster, duff-device
       version] the magic constant 65599 was picked out of thin air while
       experimenting with different constants, and turns out to be a prime.
       this is one of the algorithms used in berkeley db (see sleepycat) and
       elsewhere. */
    KU32 uHash = 0;
    KU32 uChar;
    while ((uChar = (unsigned char)*pszString++) != 0)
        uHash = uChar + (uHash << 6) + (uHash << 16) - uHash;
    return uHash;
}




/**
 * Retains a new reference to the given module
 * @returns pMod
 * @param   pMod                The module to retain.
 */
static PKWMODULE kwLdrModuleRetain(PKWMODULE pMod)
{
    kHlpAssert(pMod->cRefs > 0);
    kHlpAssert(pMod->cRefs < 64);
    pMod->cRefs++;
    return pMod;
}


/**
 * Releases a module reference.
 *
 * @param   pMod                The module to release.
 */
static void kwLdrModuleRelease(PKWMODULE pMod)
{
    if (--pMod->cRefs == 0)
    {
        /* Unlink it. */
        if (!pMod->fExe)
        {
            PKWMODULE pPrev = NULL;
            unsigned  idx   = pMod->uHashPath % K_ELEMENTS(g_apModules);
            if (g_apModules[idx] == pMod)
                g_apModules[idx] = pMod->pNext;
            else
            {
                PKWMODULE pPrev = g_apModules[idx];
                kHlpAssert(pPrev != NULL);
                while (pPrev->pNext != pMod)
                {
                    pPrev = pPrev->pNext;
                    kHlpAssert(pPrev != NULL);
                }
                pPrev->pNext = pMod->pNext;
            }
        }

        /* Release import modules. */
        if (!pMod->fNative)
        {
            KSIZE idx = pMod->u.Manual.cImpMods;
            while (idx-- > 0)
            {
                kwLdrModuleRelease(pMod->u.Manual.apImpMods[idx]);
                pMod->u.Manual.apImpMods[idx] = NULL;
            }
        }

        /* Free our resources. */
        kLdrModClose(pMod->pLdrMod);
        pMod->pLdrMod = NULL;

        if (!pMod->fNative)
        {
            kHlpPageFree(pMod->u.Manual.pvCopy, pMod->u.Manual.cbImage);
            kHlpPageFree(pMod->u.Manual.pvLoad, pMod->u.Manual.cbImage);
        }

        kHlpFree(pMod);
    }
    else
        kHlpAssert(pMod->cRefs < 64);
}


/**
 * Links the module into the module hash table.
 *
 * @returns pMod
 * @param   pMod                The module to link.
 */
static PKWMODULE kwLdrModuleLink(PKWMODULE pMod)
{
    unsigned idx = pMod->uHashPath % K_ELEMENTS(g_apModules);
    pMod->pNext = g_apModules[idx];
    g_apModules[idx] = pMod;
    return pMod;
}


/**
 * Creates a module using the native loader.
 *
 * @returns Module w/ 1 reference on success, NULL on failure.
 * @param   pszPath             The normalized path to the module.
 * @param   uHashPath           The module path hash.
 */
static PKWMODULE kwLdrModuleCreateNative(const char *pszPath, KU32 uHashPath)
{
    /*
     * Open the module and check the type.
     */
    PKLDRMOD pLdrMod;
    int rc = kLdrModOpenNative(pszPath, &pLdrMod);
    if (rc == 0)
    {
        /*
         * Create the entry.
         */
        KSIZE     cbPath = kHlpStrLen(pszPath) + 1;
        PKWMODULE pMod   = (PKWMODULE)kHlpAllocZ(sizeof(*pMod) + cbPath);
        if (pMod)
        {
            pMod->pszPath       = (char *)kHlpMemCopy(pMod + 1, pszPath, cbPath);
            pMod->uHashPath     = uHashPath;
            pMod->cRefs         = 1;
            pMod->offFilename   = (KU16)(kHlpGetFilename(pszPath) - pszPath);
            pMod->fExe          = K_FALSE;
            pMod->fNative       = K_TRUE;
            pMod->pLdrMod       = pLdrMod;
            return kwLdrModuleLink(pMod);
        }
        //kLdrModClose(pLdrMod);
    }
    return NULL;
}


/**
 * Creates a module using the our own loader.
 *
 * @returns Module w/ 1 reference on success, NULL on failure.
 * @param   pszPath             The normalized path to the module.
 * @param   uHashPath           The module path hash.
 * @param   fExe                K_TRUE if this is an executable image, K_FALSE
 *                              if not.  Executable images does not get entered
 *                              into the global module table.
 * @param   pExeMod             The executable module of the process (for
 *                              resolving imports).  NULL if fExe is set.
 */
static PKWMODULE kwLdrModuleCreateNonNative(const char *pszPath, KU32 uHashPath, KBOOL fExe, PKWMODULE pExeMod)
{
    /*
     * Open the module and check the type.
     */
    PKLDRMOD pLdrMod;
    int rc = kLdrModOpen(pszPath, 0 /*fFlags*/, (KCPUARCH)K_ARCH, &pLdrMod);
    if (rc == 0)
    {
        switch (pLdrMod->enmType)
        {
            case KLDRTYPE_EXECUTABLE_FIXED:
            case KLDRTYPE_EXECUTABLE_RELOCATABLE:
            case KLDRTYPE_EXECUTABLE_PIC:
                if (!fExe)
                    rc = KERR_GENERAL_FAILURE;
                break;

            case KLDRTYPE_SHARED_LIBRARY_RELOCATABLE:
            case KLDRTYPE_SHARED_LIBRARY_PIC:
            case KLDRTYPE_SHARED_LIBRARY_FIXED:
                if (fExe)
                    rc = KERR_GENERAL_FAILURE;
                break;

            default:
                rc = KERR_GENERAL_FAILURE;
                break;
        }
        if (rc == 0)
        {
            KI32 cImports = kLdrModNumberOfImports(pLdrMod, NULL /*pvBits*/);
            if (cImports >= 0)
            {
                /*
                 * Create the entry.
                 */
                KSIZE     cbPath = kHlpStrLen(pszPath) + 1;
                PKWMODULE pMod   = (PKWMODULE)kHlpAllocZ(sizeof(*pMod) + cbPath + sizeof(pMod) * cImports);
                if (pMod)
                {
                    KBOOL fFixed;

                    pMod->cRefs         = 1;
                    pMod->offFilename   = (KU16)(kHlpGetFilename(pszPath) - pszPath);
                    pMod->fExe          = fExe;
                    pMod->fNative       = K_FALSE;
                    pMod->pLdrMod       = pLdrMod;
                    pMod->u.Manual.cImpMods = (KU32)cImports;
                    pMod->pszPath       = (char *)kHlpMemCopy(&pMod->u.Manual.apImpMods[cImports + 1], pszPath, cbPath);

                    /*
                     * Figure out where to load it and get memory there.
                     */
                    fFixed = pLdrMod->enmType == KLDRTYPE_EXECUTABLE_FIXED
                          || pLdrMod->enmType == KLDRTYPE_SHARED_LIBRARY_FIXED;
                    pMod->u.Manual.pvLoad = fFixed ? (void *)(KUPTR)pLdrMod->aSegments[0].LinkAddress : NULL;
                    pMod->u.Manual.cbImage = kLdrModSize(pLdrMod);
                    if (   !fFixed
                        || (KUPTR)pMod->u.Manual.pvLoad - (KUPTR)g_abDefLdBuf >= sizeof(g_abDefLdBuf)
                        || sizeof(g_abDefLdBuf) - (KUPTR)pMod->u.Manual.pvLoad - (KUPTR)g_abDefLdBuf < pMod->u.Manual.cbImage)
                        rc = kHlpPageAlloc(&pMod->u.Manual.pvLoad, pMod->u.Manual.cbImage, KPROT_EXECUTE_READWRITE, fFixed);
                    if (rc == 0)
                    {
                        rc = kHlpPageAlloc(&pMod->u.Manual.pvCopy, pMod->u.Manual.cbImage, KPROT_READWRITE, K_FALSE);
                        if (rc == 0)
                        {

                            KI32 iImp;

                            /*
                             * Link the module (unless it's an executable image) and process the imports.
                             */
                            if (!fExe)
                                kwLdrModuleLink(pMod);

                            for (iImp = 0; iImp < cImports; iImp++)
                            {
                                char szName[1024];
                                rc = kLdrModGetImport(pMod->pLdrMod, NULL /*pvBits*/, iImp, szName, sizeof(szName));
                                if (rc == 0)
                                {
                                    rc = kwLdrModuleResolveAndLookup(szName, pExeMod, pMod, &pMod->u.Manual.apImpMods[iImp]);
                                    if (rc == 0)
                                        continue;
                                }
                                break;
                            }

                            if (rc == 0)
                            {
                                rc = kLdrModGetBits(pLdrMod, pMod->u.Manual.pvCopy, (KUPTR)pMod->u.Manual.pvLoad,
                                                    kwLdrModuleGetImportCallback, pMod);
                                if (rc == 0)
                                {
                                    pMod->u.Manual.pvBits = pMod->u.Manual.pvCopy;
                                    pMod->u.Manual.enmState = KWMODSTATE_NEEDS_BITS;
                                    return pMod;
                                }
                            }

                            kwLdrModuleRelease(pMod);
                            return NULL;

                            //kHlpPageFree(pMod->u.Manual.pvCopy, pMod->u.Manual.cbImage);
                        }
                        kHlpPageFree(pMod->u.Manual.pvLoad, pMod->u.Manual.cbImage);
                    }
                }
            }
        }
        kLdrModClose(pLdrMod);
    }
    return NULL;
}


/** Implements FNKLDRMODGETIMPORT, used by kwLdrModuleCreate. */
static int kwLdrModuleGetImportCallback(PKLDRMOD pMod, KU32 iImport, KU32 iSymbol, const char *pchSymbol, KSIZE cchSymbol,
                                        const char *pszVersion, PKLDRADDR puValue, KU32 *pfKind, void *pvUser)
{
    PKWMODULE pCurMod = (PKWMODULE)pvUser;
    PKWMODULE pImpMod = pCurMod->u.Manual.apImpMods[iImport];
    int rc;
    K_NOREF(pMod);

    if (pImpMod->fNative)
        rc = kLdrModQuerySymbol(pImpMod->pLdrMod, NULL /*pvBits*/, KLDRMOD_BASEADDRESS_MAP,
                                iSymbol, pchSymbol, cchSymbol, pszVersion,
                                NULL /*pfnGetForwarder*/, NULL /*pvUSer*/,
                                puValue, pfKind);
    else
        rc = kLdrModQuerySymbol(pImpMod->pLdrMod, pImpMod->u.Manual.pvBits, (KUPTR)pImpMod->u.Manual.pvLoad,
                                iSymbol, pchSymbol, cchSymbol, pszVersion,
                                NULL /*pfnGetForwarder*/, NULL /*pvUSer*/,
                                puValue, pfKind);
    if (rc == 0)
    {
        KU32 i = g_cSandboxReplacements;
        while (i-- > 0)
            if (   g_aSandboxReplacements[i].cchFunction == cchSymbol
                && kHlpMemComp(g_aSandboxReplacements[i].pszFunction, pchSymbol, cchSymbol) == 0)
            {
                if (   !g_aSandboxReplacements[i].pszModule
                    || kHlpStrICompAscii(g_aSandboxReplacements[i].pszModule, &pImpMod->pszPath[pImpMod->offFilename]) == 0)
                {
                    printf("replacing %s!%s\n", &pImpMod->pszPath[pImpMod->offFilename], g_aSandboxReplacements[i].pszFunction);
                    *puValue = g_aSandboxReplacements[i].pfnReplacement;
                }
            }
    }

    printf("iImport=%u (%s) %*.*s rc=%d\n", iImport, &pImpMod->pszPath[pImpMod->offFilename], cchSymbol, cchSymbol, pchSymbol, rc);
    return rc;

}


/**
 * Gets the main entrypoint for a module.
 *
 * @returns 0 on success, KERR on failure
 * @param   pMod                The module.
 * @param   puAddrMain          Where to return the address.
 */
static int kwLdrModuleQueryMainEntrypoint(PKWMODULE pMod, KUPTR *puAddrMain)
{
    KLDRADDR uLdrAddrMain;
    int rc = kLdrModQueryMainEntrypoint(pMod->pLdrMod,  pMod->u.Manual.pvBits, (KUPTR)pMod->u.Manual.pvLoad, &uLdrAddrMain);
    if (rc == 0)
    {
        *puAddrMain = (KUPTR)uLdrAddrMain;
        return 0;
    }
    return rc;
}


static KBOOL kwLdrModuleCanLoadNatively(const char *pszFilename, KWLOCATION enmLocation)
{
    if (enmLocation == KWLOCATION_SYSTEM32)
        return K_TRUE;
    return kHlpStrICompAscii(pszFilename, "msvcrt.dll") == 0
        || kHlpStrNICompAscii(pszFilename, "msvc", 4)   == 0;
}


/**
 * Worker for kwLdrModuleResolveAndLookup that checks out one possibility.
 *
 * If the file exists, we consult the module hash table before trying to load it
 * off the disk.
 *
 * @returns Pointer to module on success, NULL if not found, ~(KUPTR)0 on
 *          failure.
 * @param   pszPath             The name of the import module.
 * @param   enmLocation         The location we're searching.  This is used in
 *                              the heuristics for determining if we can use the
 *                              native loader or need to sandbox the DLL.
 * @param   pExe                The executable (optional).
 */
static PKWMODULE kwLdrModuleTryLoadDll(const char *pszPath, KWLOCATION enmLocation, PKWMODULE pExeMod)
{
    /*
     * Does the file exists and is it a regular file?
     */
    BirdStat_T  Stat;
    int rc = birdStatFollowLink(pszPath, &Stat);
    if (rc == 0)
    {
        if (S_ISREG(Stat.st_mode))
        {
            /*
             * Yes! Normalize it and look it up in the hash table.
             */
            char szNormPath[1024];
            rc = kwPathNormalize(pszPath, szNormPath, sizeof(szNormPath));
            if (rc == 0)
            {
                KU32 const uHashPath = kwStrHash(szNormPath);
                unsigned   idxHash   = uHashPath % K_ELEMENTS(g_apModules);
                PKWMODULE  pMod      = g_apModules[idxHash];
                if (pMod)
                {
                    do
                    {
                        if (   pMod->uHashPath == uHashPath
                            && kHlpStrComp(pMod->pszPath, szNormPath) == 0)
                            return kwLdrModuleRetain(pMod);
                        pMod = pMod->pNext;
                    } while (pMod);
                }

                /*
                 * Not in the hash table, so we have to load it from scratch.
                 */
                if (kwLdrModuleCanLoadNatively(kHlpGetFilename(szNormPath), enmLocation))
                    pMod = kwLdrModuleCreateNative(szNormPath, uHashPath);
                else
                    pMod = kwLdrModuleCreateNonNative(szNormPath, uHashPath, K_FALSE /*fExe*/, pExeMod);
                if (pMod)
                    return pMod;
                return (PKWMODULE)~(KUPTR)0;
            }
        }
    }
    return NULL;
}


/**
 * Gets a reference to the module by the given name.
 *
 * We must do the search path thing, as our hash table may multiple DLLs with
 * the same base name due to different tools version and similar.  We'll use a
 * modified search sequence, though.  No point in searching the current
 * directory for instance.
 *
 * @returns 0 on success, KERR on failure.
 * @param   pszName             The name of the import module.
 * @param   pExe                The executable (optional).
 * @param   pImporter           The module doing the importing (optional).
 * @param   ppMod               Where to return the module pointer w/ reference.
 */
static int kwLdrModuleResolveAndLookup(const char *pszName, PKWMODULE pExe, PKWMODULE pImporter, PKWMODULE *ppMod)
{
    KSIZE const cchName = kHlpStrLen(pszName);
    char        szPath[1024];
    PKWMODULE   pMod = NULL;


    /* The import path. */
    if (pMod == NULL && pImporter != NULL)
    {
        if (pImporter->offFilename + cchName >= sizeof(szPath))
            return KERR_BUFFER_OVERFLOW;
        kHlpMemCopy(kHlpMemPCopy(szPath, pImporter->pszPath, pImporter->offFilename), pszName, cchName + 1);
        pMod = kwLdrModuleTryLoadDll(szPath, KWLOCATION_IMPORTER_DIR, pExe);
    }

    /* Application directory first. */
    if (pMod == NULL && pExe != NULL && pExe != pImporter)
    {
        if (pExe->offFilename + cchName >= sizeof(szPath))
            return KERR_BUFFER_OVERFLOW;
        kHlpMemCopy(kHlpMemPCopy(szPath, pExe->pszPath, pExe->offFilename), pszName, cchName + 1);
        pMod = kwLdrModuleTryLoadDll(szPath, KWLOCATION_EXE_DIR, pExe);
    }

    /* The windows directory. */
    if (pMod == NULL)
    {
        UINT cchDir = GetSystemDirectoryA(szPath, sizeof(szPath));
        if (   cchDir <= 2
            || cchDir + 1 + cchName >= sizeof(szPath))
            return KERR_BUFFER_OVERFLOW;
        szPath[cchDir++] = '\\';
        kHlpMemCopy(&szPath[cchDir], pszName, cchName + 1);
        pMod = kwLdrModuleTryLoadDll(szPath, KWLOCATION_SYSTEM32, pExe);
    }

    /* Return. */
    if (pMod != NULL && pMod != (PKWMODULE)~(KUPTR)0)
    {
        *ppMod = pMod;
        return 0;
    }
    *ppMod = NULL;
    return KERR_GENERAL_FAILURE;
}



/**
 * Creates a tool entry and inserts it.
 *
 * @returns Pointer to the tool entry.  NULL on failure.
 * @param   pszTool             The normalized path to the tool.
 * @param   uHashPath           The hash of the tool path.
 * @param   idxHashTab          The hash table index of the tool.
 */
static PKWTOOL kwToolEntryCreate(const char *pszTool, KU32 uHashPath, unsigned idxHashTab)
{
    KSIZE   cbTool = kHlpStrLen(pszTool) + 1;
    PKWTOOL pTool  = (PKWTOOL)kHlpAllocZ(sizeof(*pTool) + cbTool);
    if (pTool)
    {
        pTool->pszPath   = (char *)kHlpMemCopy(pTool + 1, pszTool, cbTool);
        pTool->uHashPath = uHashPath;
        pTool->enmType   = KWTOOLTYPE_SANDBOXED;

        pTool->u.Sandboxed.pExe = kwLdrModuleCreateNonNative(pszTool, uHashPath, K_TRUE /*fExe*/, NULL);
        if (!pTool->u.Sandboxed.pExe)
            pTool->enmType = KWTOOLTYPE_EXEC;

        /* Link the tool. */
        pTool->pNext = g_apTools[idxHashTab];
        g_apTools[idxHashTab] = pTool;
        return pTool;
    }
    return NULL;
}


/**
 * Looks up the given tool, creating a new tool table entry if necessary.
 *
 * @returns Pointer to the tool entry.  NULL on failure.
 * @param   pszExe              The executable for the tool (not normalized).
 */
static PKWTOOL kwToolLookup(const char *pszExe)
{
    /*
     * Normalize the path and look up the tool in the g_apTools hash table.
     */
    char szNormPath[4096];
    int rc = kwPathNormalize(pszExe, szNormPath, sizeof(szNormPath));
    if (rc == 0)
    {
        KU32     uHashPath = kwStrHash(szNormPath);
        unsigned idxHash   = uHashPath % K_ELEMENTS(g_apTools);
        PKWTOOL  pTool     = g_apTools[idxHash];
        if (pTool)
        {
            do
            {
                if (   pTool->uHashPath == uHashPath
                    && kHlpStrComp(pTool->pszPath, szNormPath) == 0)
                    return pTool;
                pTool = pTool->pNext;
            } while (pTool);
        }

        /*
         * Not found, create new entry.
         */
       return kwToolEntryCreate(szNormPath, uHashPath, idxHash);
    }
    return NULL;
}



/*
 *
 * Kernel32 API replacements.
 * Kernel32 API replacements.
 * Kernel32 API replacements.
 *
 */

/** ExitProcess replacement.  */
static void WINAPI kwSandbox_Kernel32_ExitProcess(UINT uExitCode)
{
    if (g_pSandbox->idMainThread == GetCurrentThreadId())
    {
        g_pSandbox->rcExitCode = (int)uExitCode;
        longjmp(g_pSandbox->JmpBuf, 1);
    }
    __debugbreak();
}

/** ExitProcess replacement.  */
static BOOL WINAPI kwSandbox_Kernel32_TerminateProcess(HANDLE hProcess, UINT uExitCode)
{
    if (hProcess == GetCurrentProcess())
        kwSandbox_Kernel32_ExitProcess(uExitCode);
__debugbreak();
    return TerminateProcess(hProcess, uExitCode);
}



/*
 *
 * MS Visual C++ CRT replacements.
 * MS Visual C++ CRT replacements.
 * MS Visual C++ CRT replacements.
 *
 */

/** Normal CRT exit(). */
static void __cdecl kwSandbox_msvcrt_exit(int rcExitCode)
{
    fprintf(stderr, "kwSandbox_msvcrt_exit\n");
    kwSandbox_Kernel32_ExitProcess(rcExitCode);
}


/** Quick CRT _exit(). */
static void __cdecl kwSandbox_msvcrt__exit(int rcExitCode)
{
    /* Quick. */
    fprintf(stderr, "kwSandbox_msvcrt__exit\n");
    kwSandbox_Kernel32_ExitProcess(rcExitCode);
}


/** Return to caller CRT _cexit(). */
static void __cdecl kwSandbox_msvcrt__cexit(int rcExitCode)
{
    fprintf(stderr, "kwSandbox_msvcrt__cexit\n");
    kwSandbox_Kernel32_ExitProcess(rcExitCode);
}


/** Quick return to caller CRT _c_exit(). */
static void __cdecl kwSandbox_msvcrt__c_exit(int rcExitCode)
{
    fprintf(stderr, "kwSandbox_msvcrt__c_exit\n");
    kwSandbox_Kernel32_ExitProcess(rcExitCode);
}


/** Runtime error and exit. */
static void __cdecl kwSandbox_msvcrt__amsg_exit(int iMsgNo)
{
    fprintf(stderr, "\nRuntime error #%u!\n", iMsgNo);
    kwSandbox_Kernel32_ExitProcess(255);
}


/** The CRT internal __getmainargs() API. */
static int __cdecl kwSandbox_msvcrt___getmainargs(int *pargc, char ***pargv, char ***penvp,
                                                  int dowildcard, /*_startupinfo*/ void *startinfo)
{
    /** @todo startinfo points at a newmode (setmode) value.   */

    *pargc = 2;
    *pargv = (char **)kHlpAllocZ(sizeof(char *) * 3);
    (*pargv)[0] = "nasm.exe";
    (*pargv)[1] = "-h";
    *penvp = (char **)kHlpAllocZ(sizeof(char *) * 1);
    return 0;
}


/**
 * Functions that needs replacing for sandboxed execution.
 */
KWREPLACEMENTFUNCTION const g_aSandboxReplacements[] =
{
#define TUPLE(a_sz) a_sz, sizeof(a_sz) - 1
    { TUPLE("ExitProcess"),            NULL,       (KUPTR)kwSandbox_Kernel32_ExitProcess },
    { TUPLE("TerminateProcess"),       NULL,       (KUPTR)kwSandbox_Kernel32_TerminateProcess },
    { TUPLE("exit"),                   NULL,       (KUPTR)kwSandbox_msvcrt_exit },
    { TUPLE("_exit"),                  NULL,       (KUPTR)kwSandbox_msvcrt__exit },
    { TUPLE("_cexit"),                 NULL,       (KUPTR)kwSandbox_msvcrt__cexit },
    { TUPLE("_c_exit"),                NULL,       (KUPTR)kwSandbox_msvcrt__c_exit },
    { TUPLE("_amsg_exit"),             NULL,       (KUPTR)kwSandbox_msvcrt__amsg_exit },
    { TUPLE("__getmainargs"),          NULL,       (KUPTR)kwSandbox_msvcrt___getmainargs},
};
/** Number of entries in g_aReplacements. */
KU32 const                  g_cSandboxReplacements = K_ELEMENTS(g_aSandboxReplacements);


/**
 * Used by kwSandboxExec to reset the state of the module tree.
 *
 * This is done recursively.
 *
 * @param   pMod                The root of the tree to consider.
 */
static void kwSandboxResetModuleState(PKWMODULE pMod)
{
    if (   !pMod->fNative
        && pMod->u.Manual.enmState != KWMODSTATE_NEEDS_BITS)
    {
        KSIZE iImp;
        pMod->u.Manual.enmState = KWMODSTATE_NEEDS_BITS;
        iImp = pMod->u.Manual.cImpMods;
        while (iImp-- > 0)
            kwSandboxResetModuleState(pMod->u.Manual.apImpMods[iImp]);
    }
}


/**
 * Does module initialization starting at @a pMod.
 *
 * This is initially used on the executable.  Later it is used by the
 * LoadLibrary interceptor.
 *
 * @returns 0 on success, error on failure.
 * @param   pMod                The module to initialize.
 */
static int kwSandboxInitModuleTree(PKWMODULE pMod)
{
    int rc = 0;
    if (!pMod->fNative)
    {
        /* Need to copy bits? */
        if (pMod->u.Manual.enmState == KWMODSTATE_NEEDS_BITS)
        {
            kHlpMemCopy(pMod->u.Manual.pvLoad, pMod->u.Manual.pvCopy, pMod->u.Manual.cbImage);
            pMod->u.Manual.enmState = KWMODSTATE_NEEDS_INIT;
        }

        if (pMod->u.Manual.enmState == KWMODSTATE_NEEDS_INIT)
        {
            /* Must do imports first, but mark our module as being initialized to avoid
               endless recursion should there be a dependency loop. */
            KSIZE iImp;
            pMod->u.Manual.enmState = KWMODSTATE_BEING_INITED;

            for (iImp = 0; iImp < pMod->u.Manual.cImpMods; iImp++)
            {
                rc = kwSandboxInitModuleTree(pMod->u.Manual.apImpMods[iImp]);
                if (rc != 0)
                    return rc;
            }

            rc = kLdrModCallInit(pMod->pLdrMod, pMod->u.Manual.pvLoad, (KUPTR)pMod->u.Manual.pvLoad);
            if (rc == 0)
                pMod->u.Manual.enmState = KWMODSTATE_READY;
            else
                pMod->u.Manual.enmState = KWMODSTATE_INIT_FAILED;
        }
    }
    return rc;
}


static void *kwSandboxGetProcessEnvironmentBlock(void)
{
#if K_ARCH == K_ARCH_X86_32
    return (void *)__readfsdword(0x030 /* offset of ProcessEnvironmentBlock in TEB */);
#elif K_ARCH == K_ARCH_AMD64
    return (void *)__readgsqword(0x060 /* offset of ProcessEnvironmentBlock in TEB */);
#else
# error "Port me!"
#endif
}


static int kwSandboxExec(PKWTOOL pTool, const char *pszCmdLine, int *prcExitCode)
{
    int rc;
    KWSANDBOX Sandbox;

    /*
     * Initialize the sandbox environment.
     */
    Sandbox.pTool = pTool;
    Sandbox.rcExitCode = *prcExitCode = 256;
    Sandbox.idMainThread = GetCurrentThreadId();
    g_pSandbox = &Sandbox;

    /*
     * Do module initialization.
     */
    kwSandboxResetModuleState(pTool->u.Sandboxed.pExe);
    rc = kwSandboxInitModuleTree(pTool->u.Sandboxed.pExe);
    if (rc == 0)
    {
        /*
         * Call the main function.
         */
        KUPTR uAddrMain;
        rc = kwLdrModuleQueryMainEntrypoint(pTool->u.Sandboxed.pExe, &uAddrMain);
        if (rc == 0)
        {
            int rcExitCode;
            int (*pfnWin64Entrypoint)(void *pvPeb, void *, void *, void *);
            *(KUPTR *)&pfnWin64Entrypoint = uAddrMain;

                __try
                {
                    if (setjmp(Sandbox.JmpBuf) == 0)
                    {
                        *(KU64*)(Sandbox.JmpBuf) = 0; /** @todo find other way to prevent longjmp from doing unwind! */
                        rcExitCode = pfnWin64Entrypoint(kwSandboxGetProcessEnvironmentBlock, NULL, NULL, NULL);
                    }
                    else
                        rcExitCode = Sandbox.rcExitCode;
                }
                __except (EXCEPTION_EXECUTE_HANDLER)
                {
                    rcExitCode = 512;
                }
            *prcExitCode = rcExitCode;
        }
    }
    return rc;
}


static int kwExecCmdLine(const char *pszExe, const char *pszCmdLine)
{
    int rc;
    PKWTOOL pTool = kwToolLookup(pszExe);
    if (pTool)
    {
        int rc;
        int rcExitExit;
        switch (pTool->enmType)
        {
            case KWTOOLTYPE_SANDBOXED:
                rc = kwSandboxExec(pTool, pszCmdLine, &rcExitExit);
                break;

            default:
                kHlpAssertFailed();
            case KWTOOLTYPE_WATCOM:
            case KWTOOLTYPE_EXEC:
                rc = 2;
                break;
        }
    }
    else
        rc = 1;
    return rc;
}


int main(int argc, char **argv)
{
    int rc = kwExecCmdLine(argv[1], argv[2]);
    rc = kwExecCmdLine(argv[1], argv[2]);
    return rc;
}

