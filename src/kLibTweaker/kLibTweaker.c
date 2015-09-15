/* $Id: kLibTweaker.c 2791 2015-09-15 22:57:44Z knut.osmundsen@oracle.com $ */
/** @file
 * kLibTweaker - Import library tweaker for windows.
 */

/*
 * Copyright (c) 2007-2015 knut st. osmundsen <bird-kBuild-spamx@anduin.net>
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
#if 0
# define ELECTRIC_HEAP
# include "../kmk/electric.h"
# include "../kmk/electric.c"
#endif
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <errno.h>
#include <assert.h>

#include "k/kLdrFmts/pe.h"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Microsoft import library archive header.
 *
 * This has the same size as a COFF header, which is probably not a coincidence.
 */
typedef struct COFFIMPLIBHDR
{
    KU16            uSig1;    /* 0 */
    KU16            uSig2;    /* 0xffff */
    KU16            uVersion; /* 0 */
    KU16            uMachine; /* IMAGE_FILE_MACHINE_I386, ... */
    KU32            uTimeDateStamp;
    KU32            cbData;
    KU16            uOrdinalOrHint;
    KU16            uFlags;
} COFFIMPLIBHDR;

/**
 * COFF symbol.
 *
 * This one has an odd size and will cause misaligned accesses on platforms
 * which cares about such.
 */
#pragma pack(1)
typedef struct COFFSYMBOL
{
    union
    {
        char        e_name[8];
        struct
        {
            KU32    e_zeros;    /**< Zero to distinguish it from ascii name. */
            KU32    e_offset;   /**< String table offset. */
        } e;
    } e;
    KU32            e_value;
    KI16            e_scnum;
    KU16            e_type;
    KU8             e_sclass;
    KU8             e_numaux;
} COFFSYMBOL;
#pragma pack()

/**
 * Archive file header.
 */
typedef struct ARCHFILEHDR
{
    char        achName[16];
    char        achModtime[12];
    char        achOwnerId[6];
    char        achGroupId[6];
    char        achMode[8];
    char        achSize[10];
    char        achMagic[2];
} ARCHFILEHDR;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** Whether verbose output is enabled. */
static unsigned g_cVerbosityLevel = 0;
/** What to prefix the errors with. */
static char g_szErrorPrefix[128];


void FatalMsg(const char *pszFormat, ...)
{
    va_list va;

    if (g_szErrorPrefix[0])
        fprintf(stderr, "%s - fatal error: ", g_szErrorPrefix);
    else
        fprintf(stderr, "fatal error: ");

    va_start(va, pszFormat);
    vfprintf(stderr, pszFormat, va);
    va_end(va);
}


void FatalDie(const char *pszFormat, ...)
{
    va_list va;

    if (g_szErrorPrefix[0])
        fprintf(stderr, "%s - fatal error: ", g_szErrorPrefix);
    else
        fprintf(stderr, "fatal error: ");

    va_start(va, pszFormat);
    vfprintf(stderr, pszFormat, va);
    va_end(va);

    exit(1);
}


static int ErrorMsg(const char *pszFormat, ...)
{
    va_list va;

    if (g_szErrorPrefix[0])
        fprintf(stderr, "%s - error: ", g_szErrorPrefix);
    else
        fprintf(stderr, "error: ");

    va_start(va, pszFormat);
    vfprintf(stderr, pszFormat, va);
    va_end(va);

    return 1;
}


static void InfoMsg(unsigned uLevel, const char *pszFormat, ...)
{
    if (uLevel <= g_cVerbosityLevel)
    {
        va_list va;

        if (g_szErrorPrefix[0])
            fprintf(stderr, "%s - info: ", g_szErrorPrefix);
        else
            fprintf(stderr, "info: ");

        va_start(va, pszFormat);
        vfprintf(stderr, pszFormat, va);
        va_end(va);
    }
}


static void SetErrorPrefix(const char *pszPrefix, ...)
{
    int cch;
    va_list va;

    va_start(va, pszPrefix);
#if defined(_MSC_VER) || defined(__sun__)
    cch = vsprintf(g_szErrorPrefix, pszPrefix, va);
    if (cch >= sizeof(g_szErrorPrefix))
        FatalDie("Buffer overflow setting error prefix!\n");
#else
    vsnprintf(g_szErrorPrefix, sizeof(g_szErrorPrefix), pszPrefix, va);
#endif
    va_end(va);
    (void)cch;
}

#ifndef ELECTRIC_HEAP
void *xmalloc(size_t cb)
{
    void *pv = malloc(cb);
    if (!pv)
        FatalDie("out of memory (%d)\n", (int)cb);
    return pv;
}


void *xrealloc(void *pvOld, size_t cb)
{
    void *pv = realloc(pvOld, cb);
    if (!pv)
        FatalDie("out of memory (%d)\n", (int)cb);
    return pv;
}


char *xstrdup(const char *pszIn)
{
    char *psz;
    if (pszIn)
    {
        psz = strdup(pszIn);
        if (!psz)
            FatalDie("out of memory (%d)\n", (int)strlen(pszIn));
    }
    else
        psz = NULL;
    return psz;
}
#endif


void *xmallocz(size_t cb)
{
    void *pv = xmalloc(cb);
    memset(pv, 0, cb);
    return pv;
}


/**
 * Adds the arguments found in the pszCmdLine string to argument vector.
 *
 * The parsing of the pszCmdLine string isn't very sophisticated, no
 * escaping or quotes.
 *
 * @param   pcArgs      Pointer to the argument counter.
 * @param   ppapszArgs  Pointer to the argument vector pointer.
 * @param   pszCmdLine  The command line to parse and append.
 * @param   pszWedgeArg Argument to put infront of anything found in pszCmdLine.
 */
static void AppendArgs(int *pcArgs, char ***ppapszArgs, const char *pszCmdLine, const char *pszWedgeArg)
{
    int i;
    int cExtraArgs;
    const char *psz;
    char **papszArgs;

    /*
     * Count the new arguments.
     */
    cExtraArgs = 0;
    psz = pszCmdLine;
    while (*psz)
    {
        while (isspace(*psz))
            psz++;
        if (!psz)
            break;
        cExtraArgs++;
        while (!isspace(*psz) && *psz)
            psz++;
    }
    if (!cExtraArgs)
        return;

    /*
     * Allocate a new vector that can hold the arguments.
     * (Reallocating might not work since the argv might not be allocated
     *  from the heap but off the stack or somewhere... )
     */
    i = *pcArgs;
    *pcArgs = i + cExtraArgs + !!pszWedgeArg;
    papszArgs = xmalloc((*pcArgs + 1) * sizeof(char *));
    *ppapszArgs = memcpy(papszArgs, *ppapszArgs, i * sizeof(char *));

    if (pszWedgeArg)
        papszArgs[i++] = xstrdup(pszWedgeArg);

    psz = pszCmdLine;
    while (*psz)
    {
        size_t cch;
        const char *pszEnd;
        while (isspace(*psz))
            psz++;
        if (!psz)
            break;
        pszEnd = psz;
        while (!isspace(*pszEnd) && *pszEnd)
            pszEnd++;

        cch = pszEnd - psz;
        papszArgs[i] = xmalloc(cch + 1);
        memcpy(papszArgs[i], psz, cch);
        papszArgs[i][cch] = '\0';

        i++;
        psz = pszEnd;
    }

    papszArgs[i] = NULL;
}




static fpos_t kLibTweakerAsciiToSize(const char *pch, size_t cch)
{
    fpos_t cb = 0;

    /* strip leading spaces. */
    while (cch > 0 && (*pch == ' ' || *pch == '\t'))
        cch--, pch++;

    /* Convert decimal to binary. */
    while (cch-- > 0)
    {
        char ch = *pch++;
        if (ch >= '0' && ch <= '9')
        {
            cb *= 10;
            cb += ch - '0';
        }
        else
            break;
    }

    return cb;
}


static int kLibMyReadAt(FILE *pFile, void *pv, size_t cb, fpos_t off, int fEofOk)
{
    if (fsetpos(pFile, &off) == 0)
    {
        size_t cbActual = fread(pv, 1, cb, pFile);
        if (cbActual == cb)
            return 0;
        if (!fEofOk || !feof(pFile))
            ErrorMsg("fread returned %#lx, expected %#lx!\n", (unsigned long)cbActual, (unsigned long)cb);
    }
    else
        ErrorMsg("seek error!\n");
    return 1;
}


static int kLibMyWriteAt(FILE *pFile, const void *pv, size_t cb, fpos_t off)
{
    if (fsetpos(pFile, &off) == 0)
    {
        size_t cbActual = fwrite(pv, 1, cb, pFile);
        if (cbActual == cb)
            return 0;
        ErrorMsg("fwrite returned %#lx, expected %#lx!\n", (unsigned long)cbActual, (unsigned long)cb);
    }
    else
        ErrorMsg("seek error!\n");
    return 1;
}


static int kLibFillNullThunkData(FILE *pFile, fpos_t cbFile, fpos_t offFileBytes)
{
    size_t                  off;
    IMAGE_FILE_HEADER       CoffHdr;
    IMAGE_SECTION_HEADER    SecHdr;
    unsigned                iSecHdr;
    fpos_t                  offCur;
    unsigned                cbMachineWord;
    KU32                    cbStrTab;
    COFFSYMBOL             *paSymbols;
    int                     rcRet = 0;

    /*
     * Read the COFF file header and filter out unlikly files based on
     * section and symbol counts.
     */
    if (cbFile <= sizeof(IMAGE_FILE_HEADER) + sizeof(IMAGE_SECTION_HEADER) * 2 + 4)
        return 0;
    if (kLibMyReadAt(pFile, &CoffHdr, sizeof(CoffHdr), offFileBytes, 0) != 0)
        return 1;
    if (   CoffHdr.Machine != IMAGE_FILE_MACHINE_I386
        && CoffHdr.Machine != IMAGE_FILE_MACHINE_AMD64)
        return 0;
    cbMachineWord = CoffHdr.Machine == IMAGE_FILE_MACHINE_I386 ? 4 : 8;
    if (   CoffHdr.NumberOfSections == 0
        || CoffHdr.NumberOfSymbols == 0)
        return 0;
    off = sizeof(IMAGE_FILE_HEADER) + CoffHdr.NumberOfSections * sizeof(IMAGE_SECTION_HEADER);
    if ((fpos_t)off >= cbFile)
        return 0;
    if (   CoffHdr.PointerToSymbolTable >= (KU64)cbFile
        || CoffHdr.PointerToSymbolTable < off)
        return 0;

    /*
     * Search for the .idata$5 section which the thunk data is usually found in.
     */
    offCur = offFileBytes + sizeof(CoffHdr);
    for (iSecHdr = 0; iSecHdr < CoffHdr.NumberOfSections; iSecHdr++)
    {
        if (kLibMyReadAt(pFile, &SecHdr, sizeof(SecHdr), offCur, 0) != 0)
            return 1;
        InfoMsg(2, "#2: %.8s VirtualSize=%#lx\n", SecHdr.Name, SecHdr.SizeOfRawData);
        if (   !memcmp(SecHdr.Name, ".idata$5", 8)
            && SecHdr.SizeOfRawData == cbMachineWord)
            break;
        offCur += sizeof(SecHdr);
    }
    if (iSecHdr == CoffHdr.NumberOfSections)
        return 0;

    /*
     * Read in the symbo and string tables.
     */
    off = CoffHdr.PointerToSymbolTable + CoffHdr.NumberOfSymbols * sizeof(COFFSYMBOL);
    if (kLibMyReadAt(pFile, &cbStrTab, sizeof(cbStrTab), offFileBytes + off, 0) != 0)
        return 1;
    InfoMsg(2, "#2: Found COFF file header, cbStrTab=%#x; off=%#lx NumberOfSymbols=%#x PointerToSymbolTable=%#x\n",
            cbStrTab, (long)off, CoffHdr.NumberOfSymbols, CoffHdr.PointerToSymbolTable);
    if (   cbStrTab <= 4U
        || cbStrTab >= 16*1024*1024U /* 16MB */
        || (fpos_t)off + cbStrTab > cbFile)
        return 0;
    paSymbols = xmalloc(CoffHdr.NumberOfSymbols * sizeof(COFFSYMBOL) + cbStrTab + 1);
    if (kLibMyReadAt(pFile, paSymbols, CoffHdr.NumberOfSymbols * sizeof(COFFSYMBOL) + cbStrTab,
                     offFileBytes + CoffHdr.PointerToSymbolTable, 0) == 0)
    {
        char       *pchStrTab = (char *)&paSymbols[CoffHdr.NumberOfSymbols];
        unsigned    iSym;

        pchStrTab[cbStrTab] = '\0';
        pchStrTab[0] = '\0';
        pchStrTab[1] = '\0';
        pchStrTab[2] = '\0';
        pchStrTab[3] = '\0';

        for (iSym = 0; iSym < CoffHdr.NumberOfSymbols; iSym++)
        {
            static char const s_szSuffix[] = "NULL_THUNK_DATA";
            const char *pchName;
            size_t      cchName;
            if (paSymbols[iSym].e.e.e_zeros != 0)
            {
                pchName = &paSymbols[iSym].e.e_name[0];
                cchName = (char *)memchr(pchName, '\0', sizeof(paSymbols[iSym].e.e_name)) - pchName;
                if (cchName > sizeof(paSymbols[iSym].e.e_name))
                    cchName = sizeof(paSymbols[iSym].e.e_name);
            }
            else if (   paSymbols[iSym].e.e.e_offset == 0
                     || paSymbols[iSym].e.e.e_offset >= cbStrTab)
                continue;
            else
            {
                pchName = &pchStrTab[paSymbols[iSym].e.e.e_offset];
                cchName = strlen(pchName);
            }

            if (   *pchName == 0x7f
                && cchName >= sizeof(s_szSuffix)
                && memcmp(&pchName[cchName - sizeof(s_szSuffix) + 1], s_szSuffix, sizeof(s_szSuffix) - 1) == 0)
            {
                if (pchName[cchName] == '\0')
                    InfoMsg(1, "#2: Found '%s': value=%#lx\n", pchName, paSymbols[iSym].e_value);
                else
                    InfoMsg(1, "#2: Found '%.8s': value=%#lx\n", pchName, paSymbols[iSym].e_value);
                if (   paSymbols[iSym].e_scnum > 0
                    && paSymbols[iSym].e_scnum <= CoffHdr.NumberOfSections)
                {
                    if (paSymbols[iSym].e_scnum != iSecHdr + 1)
                        InfoMsg(0, "#2: '%s' in section %u, expected %u\n", pchName, paSymbols[iSym].e_scnum, iSecHdr);
                    else if (paSymbols[iSym].e_value != 0)
                        InfoMsg(0, "#2: '%s' in value %#xu, expected 0x0\n", pchName, paSymbols[iSym].e_value);
                    else if (   SecHdr.PointerToRawData < sizeof(CoffHdr) + CoffHdr.NumberOfSections * sizeof(IMAGE_SECTION_HEADER)
                             || (fpos_t)SecHdr.PointerToRawData + cbMachineWord > cbFile)
                        InfoMsg(0, "#2: Unexpected PointerToRawData value: %#x\n", SecHdr.PointerToRawData);
                    else
                    {
                        union
                        {
                            KU8  ab[8];
                            KU32 u32;
                            KU64 u64;
                        } uBuf;
                        uBuf.u64 = 0;
                        off = offFileBytes + SecHdr.PointerToRawData;
                        if (kLibMyReadAt(pFile, &uBuf, cbMachineWord, off, 0) == 0)
                        {
                            static const KU8 s_abGarbage[8] = { 0xaa, 0x99, 0x88, 0xbb,  0xbb, 0xaa, 0x88, 0x99 };
                            static const KU8 s_abZero[8]    = { 0, 0, 0, 0,  0, 0, 0, 0 };
                            if (memcmp(&uBuf, s_abZero,cbMachineWord) == 0)
                            {
                                rcRet = kLibMyWriteAt(pFile, s_abGarbage, cbMachineWord, off);
                                if (!rcRet)
                                    InfoMsg(1, "#2: Updated '%s'\n", pchName);
                            }
                            else if (memcmp(&uBuf, s_abGarbage, cbMachineWord) == 0)
                            {
                                InfoMsg(1, "#2: Already modified '%s'\n", pchName);
                                rcRet = 0;
                            }
                            else
                                rcRet = ErrorMsg(0, "#2: Unexpected '%s' data: %02x %02x %02x %02x %02x %02x %02x %02x\n",
                                                 pchName,
                                                 uBuf.ab[0], uBuf.ab[1], uBuf.ab[2], uBuf.ab[3],
                                                 uBuf.ab[4], uBuf.ab[5], uBuf.ab[6], uBuf.ab[7]);
                        }
                        break;
                    }
                }
            }
        }

    }
    else
        rcRet = 1;
    free(paSymbols);
    return rcRet;
}


/**
 * Clears timestamps to avoid rebuilding stuff just because the internal
 * timestamps changed in an import library.
 */
static int kLibClearTimestamps(FILE *pFile, fpos_t offFileHdr, ARCHFILEHDR *pFileHdr, fpos_t cbFile, fpos_t offFileBytes)
{
    union
    {
        IMAGE_FILE_HEADER   CoffHdr;
        COFFIMPLIBHDR       ImpLibHdr;
    } u;
    if (sizeof(u.CoffHdr) != sizeof(u.ImpLibHdr))
        FatalDie("Oops!");

    /*
     * Clear the timestamp in the library file header.
     */
    memset(pFileHdr->achModtime, '0', sizeof(pFileHdr->achModtime));
    if (kLibMyWriteAt(pFile, pFileHdr, sizeof(*pFileHdr), offFileHdr) != 0)
        return 1;

    /*
     * Clear the timestamp in the COFF header, if we find one.
     */
    if (cbFile <= sizeof(IMAGE_FILE_HEADER))
        return 0;
    if (kLibMyReadAt(pFile, &u.CoffHdr, sizeof(u.CoffHdr), offFileBytes, 0) != 0)
        return 1;

    if (   (   u.CoffHdr.Machine == IMAGE_FILE_MACHINE_I386
            || u.CoffHdr.Machine == IMAGE_FILE_MACHINE_AMD64)
        &&     sizeof(IMAGE_FILE_HEADER)
             + u.CoffHdr.NumberOfSections * sizeof(IMAGE_SECTION_HEADER)
           <= (KU64)cbFile
        && u.CoffHdr.PointerToSymbolTable <= (KU64)cbFile)
    {
        InfoMsg(1, "Found COFF file header\n");
        if (u.CoffHdr.TimeDateStamp != 0)
        {
            u.CoffHdr.TimeDateStamp = 0;
            return kLibMyWriteAt(pFile, &u.CoffHdr, sizeof(u.CoffHdr), offFileBytes);
        }
    }
    else if (   u.ImpLibHdr.uSig1    == 0
             && u.ImpLibHdr.uSig2    == 0xffff
             && u.ImpLibHdr.uVersion == 0
             && (   u.ImpLibHdr.uMachine == IMAGE_FILE_MACHINE_I386
                 || u.ImpLibHdr.uMachine == IMAGE_FILE_MACHINE_AMD64)
             && u.ImpLibHdr.cbData <= cbFile)
    {
        InfoMsg(1, "Found COFF import library header\n");
        if (u.ImpLibHdr.uTimeDateStamp)
        {
            u.ImpLibHdr.uTimeDateStamp = 0;
            return kLibMyWriteAt(pFile, &u.ImpLibHdr, sizeof(u.ImpLibHdr), offFileBytes);
        }
    }
    else
        InfoMsg(1, "CoffHdr.Machine=%#x ImpLibHdr.Machine=%#x\n", u.CoffHdr.Machine, u.ImpLibHdr.uMachine);

    return 0;
}


static int kLibTweakerDoIt(const char *pszLib, int fClearTimestamps, int fFillNullThunkData)
{
    int   rcRet = 0;
    FILE *pFile = fopen(pszLib, "r+b");
    if (pFile)
    {
        /*
         * Read the header.
         */
        static char s_szMagic[] = "!<arch>\n";
        union
        {
            char                ab[1024];
            IMAGE_FILE_HEADER   CoffHdr;
        } uBuf;
        if (   fread(uBuf.ab, 1, sizeof(s_szMagic) - 1, pFile) == sizeof(s_szMagic) - 1
            && memcmp(uBuf.ab, s_szMagic, sizeof(s_szMagic) - 1) == 0)
        {
            fpos_t offFileHdr = sizeof(s_szMagic) - 1;
            while (!feof(pFile))
            {
                ARCHFILEHDR     FileHdr;
                if (kLibMyReadAt(pFile, &FileHdr, sizeof(FileHdr), offFileHdr, 1) != 0)
                {
                    if (feof(pFile))
                        break;
                    rcRet = ErrorMsg("failed reading the file header (offset %ld)\n", (long)offFileHdr);
                    break;
                }
                if (   FileHdr.achMagic[0] == 0x60
                    && FileHdr.achMagic[1] == 0x0a)
                {
                    fpos_t const offFileBytes = offFileHdr + sizeof(FileHdr);

                    /*
                     * Convert the size from decimal to binary as we need it to skip to
                     * the next file header.
                     */
                    fpos_t const cb = kLibTweakerAsciiToSize(FileHdr.achSize, sizeof(FileHdr.achSize));
                    InfoMsg(1, "Found header at %#lx: cbFile=%#lx, bytes at %#lx\n",
                            (unsigned long)offFileHdr, (unsigned long)cb, (unsigned long)offFileBytes);

                    /*
                     * Make the requested changes.
                     */
                    if (fClearTimestamps)
                        rcRet |= kLibClearTimestamps(pFile, offFileHdr, &FileHdr, cb, offFileBytes);

                    if (fFillNullThunkData)
                        rcRet |= kLibFillNullThunkData(pFile, cb, offFileBytes);

                    /*
                     * Skip to the next header.
                     */
                    offFileHdr = offFileBytes + ((cb + 1) & ~(fpos_t)1);
                }
                else
                    rcRet = ErrorMsg("invalid file header magic (offset %ld)\n", (long)offFileHdr);
            }
        }
        else
            rcRet = ErrorMsg("Didn't find '!<arch>\\n' magic in '%s' (or read error)\n", pszLib);

        if (fclose(pFile) != 0)
            rcRet = ErrorMsg("Error closing '%s'\n");
    }
    else
        rcRet = ErrorMsg("Failed to open '%s' for read+write\n", pszLib);
    return rcRet;
}


/**
 * Prints a syntax error and returns the appropriate exit code
 *
 * @returns approriate exit code.
 * @param   pszFormat   The syntax error message.
 * @param   ...         Message args.
 */
static int SyntaxError(const char *pszFormat, ...)
{
    va_list va;
    fprintf(stderr, "kObjCache: syntax error: ");
    va_start(va, pszFormat);
    vfprintf(stderr, pszFormat, va);
    va_end(va);
    return 1;
}


/**
 * Prints the usage.
 * @returns 0.
 */
static int usage(FILE *pOut)
{
    fprintf(pOut,
            "syntax: kLibTweaker [-v|--verbose] [--clear-timestamps] <lib>\n"
            "\n");
    return 0;
}


int main(int argc, char **argv)
{
    char *psz;
    int i;

    int fClearTimestamps = 0;
    int fFillNullThunkData = 0;
    const char *pszLib = NULL;

    SetErrorPrefix("kLibTweaker");

    /*
     * Arguments passed in the environmnet?
     */
    psz = getenv("KLIBTWEAKER_OPTS");
    if (psz)
        AppendArgs(&argc, &argv, psz, NULL);

/** @todo Add the capability to produce import/stub libraries from ELF shared
 * objects that we can use while linking and break up linking dependencies
 * (i.e. not relink everything just because something in VBoxRT change that
 * didn't make any difference to the symbols it exports). */

    /*
     * Parse the arguments.
     */
    if (argc <= 1)
        return usage(stderr) + 1;
    for (i = 1; i < argc; i++)
    {
        if (!strcmp(argv[i], "--clear-timestamps"))
            fClearTimestamps = 1;
        else if (!strcmp(argv[i], "--fill-null_thunk_data"))
            fFillNullThunkData = 1;
        /* Standard stuff: */
        else if (!strcmp(argv[i], "--help"))
            return usage(stderr);
        else if (!strcmp(argv[i], "-v") || !strcmp(argv[i], "--verbose"))
            g_cVerbosityLevel++;
        else if (!strcmp(argv[i], "-q") || !strcmp(argv[i], "--quiet"))
            g_cVerbosityLevel = 0;
        else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "-?")
              || !strcmp(argv[i], "/h") || !strcmp(argv[i], "/?") || !strcmp(argv[i], "/help"))
            return usage(stdout);
        else if (!strcmp(argv[i], "-V") || !strcmp(argv[i], "--version"))
        {
            printf("kLibTweaker - kBuild version %d.%d.%d ($Revision: 2791 $)\n"
                   "Copyright (c) 2007-2015 knut st. osmundsen\n",
                   KBUILD_VERSION_MAJOR, KBUILD_VERSION_MINOR, KBUILD_VERSION_PATCH);
            return 0;
        }
        else if (!strcmp(argv[i], "--"))
        {
            i++;
            if (i == argc)
                return SyntaxError("No library given!\n");
            if (i + 1 != argc || pszLib)
                return SyntaxError("Only one library can be tweaked at a time!\n");
            pszLib = argv[i];
            break;
        }
        else if (argv[i][0] == '-')
            return SyntaxError("Doesn't grok '%s'!\n", argv[i]);
        else if (!pszLib)
            pszLib = argv[i];
        else
            return SyntaxError("Only one library can be tweaked at a time!\n");
    }
    if (!pszLib)
        return SyntaxError("No library given!\n");

    return kLibTweakerDoIt(pszLib, fClearTimestamps, fFillNullThunkData);
}

