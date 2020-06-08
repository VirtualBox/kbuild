/* $Id: kDepObj.c 3364 2020-06-08 19:29:42Z knut.osmundsen@oracle.com $ */
/** @file
 * kDepObj - Extract dependency information from an object file.
 */

/*
 * Copyright (c) 2007-2010 knut st. osmundsen <bird-kBuild-spamx@anduin.net>
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

/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
#define MSCFAKES_NO_WINDOWS_H
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <stdarg.h>
#if !defined(_MSC_VER)
# include <unistd.h>
#else
# include <io.h>
typedef intptr_t ssize_t;
#endif
#include "k/kDefs.h"
#include "k/kTypes.h"
#include "k/kLdrFmts/pe.h"
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif
#include "kDep.h"
#include "err.h"
#include "kmkbuiltin.h"


/*******************************************************************************
*   Defined Constants And Macros                                               *
*******************************************************************************/
#if 0
# define dprintf(a)             printf a
# define dump(pb, cb, offBase)  depHexDump(pb,cb,offBase)
# define WITH_DPRINTF
#else
# define dprintf(a)             do {} while (0)
# define dump(pb, cb, offBase)  do {} while (0)
# undef  WITH_DPRINTF
#endif

/** @name OMF defines
 * @{ */
#define KDEPOMF_THEADR          0x80
#define KDEPOMF_LHEADR          0x82
#define KDEPOMF_COMENT          0x88
#define KDEPOMF_CMTCLS_DEPENDENCY   0xe9
#define KDEPOMF_CMTCLS_DBGTYPE      0xa1
#define KDEPOMF_LINNUM          0x94
#define KDEPOMF_LINNUM32        0x95
/** @} */


/*******************************************************************************
*   Structures and Typedefs                                                    *
*******************************************************************************/
/** @name OMF Structures
 * @{ */
#pragma pack(1)
/** OMF record header. */
typedef struct KDEPOMFHDR
{
    /** The record type. */
    KU8     bType;
    /** The size of the record, excluding this header. */
    KU16    cbRec;
} KDEPOMFHDR;
typedef KDEPOMFHDR *PKDEPOMFHDR;
typedef const KDEPOMFHDR *PCKDEPOMFHDR;

/** OMF string. */
typedef struct KDEPOMFSTR
{
    KU8     cch;
    char    ach[1];
} KDEPOMFSTR;
typedef KDEPOMFSTR *PKDEPOMFSTR;
typedef const KDEPOMFSTR *PCKDEPOMFSTR;

/** THEADR/LHEADR. */
typedef struct KDEPOMFTHEADR
{
    KDEPOMFHDR  Hdr;
    KDEPOMFSTR  Name;
} KDEPOMFTHEADR;
typedef KDEPOMFTHEADR *PKDEPOMFTHEADR;
typedef const KDEPOMFTHEADR *PCKDEPOMFTHEADR;

/** Dependency File. */
typedef struct KDEPOMFDEPFILE
{
    KDEPOMFHDR  Hdr;
    KU8         fType;
    KU8         bClass;
    KU16        wDosTime;
    KU16        wDosDate;
    KDEPOMFSTR  Name;
} KDEPOMFDEPFILE;
typedef KDEPOMFDEPFILE *PKDEPOMFDEPFILE;
typedef const KDEPOMFDEPFILE *PCKDEPOMFDEPFILE;

#pragma pack()
/** @} */


/** @name COFF Structures
 * @{ */
#pragma pack(1)

typedef struct KDEPCVSYMHDR
{
    /** The record size minus the size field. */
    KU16        cb;
    /** The record type. */
    KU16        uType;
} KDEPCVSYMHDR;
typedef KDEPCVSYMHDR *PKDEPCVSYMHDR;
typedef const KDEPCVSYMHDR *PCKDEPCVSYMHDR;

/** @name Selection of KDEPCVSYMHDR::uType values.
 * @{ */
#define K_CV8_S_MSTOOL      KU16_C(0x1116)
/** @} */

typedef struct KDEPCV8SYMHDR
{
    /** The record type. */
    KU32        uType;
    /** The record size minus the size field. */
    KU32        cb;
} KDEPCV8SYMHDR;
typedef KDEPCV8SYMHDR *PKDEPCV8SYMHDR;
typedef const KDEPCV8SYMHDR *PCKDEPCV8SYMHDR;

/** @name Known KDEPCV8SYMHDR::uType Values.
 * @{ */
#define K_CV8_SYMBOL_INFO   KU32_C(0x000000f1)
#define K_CV8_LINE_NUMBERS  KU32_C(0x000000f2)
#define K_CV8_STRING_TABLE  KU32_C(0x000000f3)
#define K_CV8_SOURCE_FILES  KU32_C(0x000000f4)
#define K_CV8_COMDAT_XXXXX  KU32_C(0x000000f5) /**< no idea about the format... */
/** @} */

#pragma pack()
/** @} */

/**
 * Globals.
 */
typedef struct KDEPOBJGLOBALS
{
    /** The command execution context. */
    PKMKBUILTINCTX pCtx;
    /** Core instance. */
    DEPGLOBALS Core;
    /** The file.    */
    const char *pszFile;
} KDEPOBJGLOBALS;
/** Pointer to kDepObj globals. */
typedef KDEPOBJGLOBALS *PKDEPOBJGLOBALS;



/**
 * Prints an error message.
 *
 * @returns rc.
 * @param   pThis       kObjDep instance data.
 * @param   rc          The return code, for making one line return statements.
 * @param   pszFormat   The message format string.
 * @param   ...         Format arguments.
 * @todo    Promote this to kDep.c.
 */
static int kDepErr(PKDEPOBJGLOBALS pThis, int rc, const char *pszFormat, ...)
{
    char szMsg[2048];
    va_list va;
    va_start(va, pszFormat);
    vsnprintf(szMsg, sizeof(szMsg) - 1, pszFormat, va);
    va_end(va);
    szMsg[sizeof(szMsg) - 1] = '\0';

    if (pThis->pszFile)
        warnx(pThis->pCtx, "%s: error: %s", pThis->pszFile, szMsg);
    else
        errx(pThis->pCtx, rc, "%s", szMsg);
    return rc;
}


/**
 * Gets an index from the data.
 *
 * @returns The index, KU16_MAX on buffer underflow.
 * @param   puData      The current data stream position (in/out).
 * @param   pcbLeft     Number of bytes left (in/out).
 */
static KU16 kDepObjOMFGetIndex(KPCUINT *puData, KU16 *pcbLeft)
{
    KU16 u16;

    if (*pcbLeft >= 1 && *pcbLeft != KU16_MAX)
    {
        *pcbLeft -= 1;
        u16 = *puData->pb++;
        if (u16 & KU16_C(0x80))
        {
            if (*pcbLeft >= 1)
            {
                *pcbLeft -= 1;
                u16 = ((u16 & KU16_C(0x7f)) << 8) | *puData->pb++;
            }
            else
                u16 = KU16_MAX;
        }
    }
    else
        u16 = KU16_MAX;
    return u16;
}


/**
 * Parses the OMF file.
 *
 * @returns 0 on success, 1 on failure, 2 if no dependencies was found.
 * @param   pThis       The kDepObj instance data.
 * @param   pbFile      The start of the file.
 * @param   cbFile      The file size.
 */
int kDepObjOMFParse(PKDEPOBJGLOBALS pThis, const KU8 *pbFile, KSIZE cbFile)
{
    PCKDEPOMFHDR    pHdr        = (PCKDEPOMFHDR)pbFile;
    KSIZE           cbLeft      = cbFile;
    char            uDbgType    = 0; /* H or C */
    KU8             uDbgVer     = KU8_MAX;
    KU32            iSrc        = 0;
    KU32            iMaybeSrc   = 0;
    KU8             uLinNumType = KU8_MAX;
    KU16            cLinNums    = 0;
    KU32            cLinFiles   = 0;
    KU32            iLinFile    = 0;

    /*
     * Iterate thru the records until we hit the end or an invalid one.
     */
    while (   cbLeft >= sizeof(*pHdr)
           && cbLeft >= pHdr->cbRec + sizeof(*pHdr))
    {
        KPCUINT     uData;
        uData.pv = pHdr + 1;

        /* process selected record types. */
        dprintf(("%#07" KUPTR_PRI ": %#04x %#05x\n", (const KU8*)pHdr - pbFile, pHdr->bType, pHdr->cbRec));
        switch (pHdr->bType)
        {
            /*
             * The T/L Header contains the source name. When emitting CodeView 4
             * and earlier (like masm and watcom does), all includes used by the
             * line number tables have their own THEADR record.
             */
            case KDEPOMF_THEADR:
            case KDEPOMF_LHEADR:
            {
                PCKDEPOMFTHEADR pTHeadr = (PCKDEPOMFTHEADR)pHdr;
                if (1 + pTHeadr->Name.cch + 1 != pHdr->cbRec)
                    return kDepErr(pThis, 1, "%#07x - Bad %cHEADR record, length mismatch.",
                                   (const KU8*)pHdr - pbFile, pHdr->bType == KDEPOMF_THEADR ? 'T' : 'L');
                if (    (   pTHeadr->Name.cch > 2
                         && pTHeadr->Name.ach[pTHeadr->Name.cch - 2] == '.'
                         && (   pTHeadr->Name.ach[pTHeadr->Name.cch - 1] == 'o'
                             || pTHeadr->Name.ach[pTHeadr->Name.cch - 1] == 'O'))
                    ||  (   pTHeadr->Name.cch > 4
                         && pTHeadr->Name.ach[pTHeadr->Name.cch - 4] == '.'
                         && (   pTHeadr->Name.ach[pTHeadr->Name.cch - 3] == 'o'
                             || pTHeadr->Name.ach[pTHeadr->Name.cch - 3] == 'O')
                         && (   pTHeadr->Name.ach[pTHeadr->Name.cch - 2] == 'b'
                             || pTHeadr->Name.ach[pTHeadr->Name.cch - 2] == 'B')
                         && (   pTHeadr->Name.ach[pTHeadr->Name.cch - 1] == 'j'
                             || pTHeadr->Name.ach[pTHeadr->Name.cch - 1] == 'J'))
                   )
                    dprintf(("%cHEADR: %.*s [ignored]\n", pHdr->bType == KDEPOMF_THEADR ? 'T' : 'L', pTHeadr->Name.cch, pTHeadr->Name.ach));
                else
                {
                    dprintf(("%cHEADR: %.*s\n", pHdr->bType == KDEPOMF_THEADR ? 'T' : 'L', pTHeadr->Name.cch, pTHeadr->Name.ach));
                    depAdd(&pThis->Core, pTHeadr->Name.ach, pTHeadr->Name.cch);
                    iMaybeSrc++;
                }
                uLinNumType = KU8_MAX;
                break;
            }

            case KDEPOMF_COMENT:
            {
                KU8 uClass;

                if (pHdr->cbRec < 2 + 1)
                    return kDepErr(pThis, 1, "%#07x - Bad COMMENT record, too small.", (const KU8*)pHdr - pbFile);
                if (uData.pb[0] & 0x3f)
                    return kDepErr(pThis, 1, "%#07x - Bad COMMENT record, reserved flags set.", (const KU8*)pHdr - pbFile);
                uClass = uData.pb[1];
                uData.pb += 2;
                switch (uClass)
                {
                    /*
                     * Borland dependency file comment (famously used by wmake and Watcom).
                     */
                    case KDEPOMF_CMTCLS_DEPENDENCY:
                    {
                        PCKDEPOMFDEPFILE pDep = (PCKDEPOMFDEPFILE)pHdr;
                        if (K_OFFSETOF(KDEPOMFDEPFILE, Name.ach[pDep->Name.cch]) + 1 != pHdr->cbRec + sizeof(*pHdr))
                        {
                            /* Empty record indicates the end of the dependency files,
                               no need to go on. */
                            if (pHdr->cbRec == 2 + 1)
                                return 0;
                            return kDepErr(pThis, 1, "%#07lx - Bad DEPENDENCY FILE record, length mismatch. (%u/%u)",
                                           (long)((const KU8 *)pHdr - pbFile),
                                           K_OFFSETOF(KDEPOMFDEPFILE, Name.ach[pDep->Name.cch]) + 1,
                                           (unsigned)(pHdr->cbRec + sizeof(*pHdr)));
                        }
                        depAdd(&pThis->Core, pDep->Name.ach, pDep->Name.cch);
                        iSrc++;
                        break;
                    }

                    /*
                     * Pick up the debug type so we can parse the LINNUM records.
                     */
                    case KDEPOMF_CMTCLS_DBGTYPE:
                        if (pHdr->cbRec < 2 + 3 + 1)
                            break; /* ignore, Borland used this for something else apparently. */
                        if (    !(uData.pb[1] == 'C' && uData.pb[2] == 'V')
                            &&  !(uData.pb[1] == 'H' && uData.pb[2] == 'L'))
                        {
                            dprintf(("Unknown debug type: %c%c (%u)\n", uData.pb[1], uData.pb[2], uData.pb[0]));
                            break;
                        }
                        uDbgType = uData.pb[1];
                        uDbgVer  = uData.pb[0];
                        dprintf(("Debug Type %s ver %u\n", uDbgType == 'H' ? "HLL" : "CodeView", uDbgVer));
                        break;

                }
                break; /* COMENT */
            }

            /*
             * LINNUM + THEADR == sigar.
             */
            case KDEPOMF_LINNUM:
                if (uDbgType == 'C')
                    iMaybeSrc |= KU32_C(0x80000000);
                dprintf(("LINNUM:\n"));
                break;

            /*
             * The HLL v4 and v6 file names table will include all files when present, which
             * is perfect for generating dependencies.
             */
            case KDEPOMF_LINNUM32:
                if (    uDbgType == 'H'
                    &&  uDbgVer >= 3
                    &&  uDbgVer <= 6)
                {
                    /* skip two indexes (group & segment) */
                    KU16 cbRecLeft = pHdr->cbRec - 1;
                    KU16 uGrp = kDepObjOMFGetIndex(&uData, &cbRecLeft);
                    KU16 uSeg = kDepObjOMFGetIndex(&uData, &cbRecLeft);
                    if (uSeg == KU16_MAX)
                        return kDepErr(pThis, 1, "%#07lx - Bad LINNUM32 record", (long)((const KU8 *)pHdr - pbFile));
                    K_NOREF(uGrp);

                    if (uLinNumType == KU8_MAX)
                    {
#ifdef WITH_DPRINTF
                        static const char * const s_apsz[5] =
                        {
                            "source file", "listing file", "source & listing file", "file names table", "path table"
                        };
#endif
                        KU16 uLine;
                        KU8  uReserved;
                        KU16 uSeg2;
                        KU32 cbLinNames;

                        if (cbRecLeft < 2+1+1+2+2+4)
                            return kDepErr(pThis, 1, "%#07lx - Bad LINNUM32 record, too short", (long)((const KU8 *)pHdr - pbFile));
                        cbRecLeft  -= 2+1+1+2+2+4;
                        uLine       = *uData.pu16++;
                        uLinNumType = *uData.pu8++;
                        uReserved   = *uData.pu8++;  K_NOREF(uReserved);
                        cLinNums    = *uData.pu16++; K_NOREF(cLinNums);
                        uSeg2       = *uData.pu16++; K_NOREF(uSeg2);
                        cbLinNames  = *uData.pu32++; K_NOREF(cbLinNames);

                        dprintf(("LINNUM32: uGrp=%#x uSeg=%#x uSeg2=%#x uLine=%#x (MBZ) uReserved=%#x\n",
                                 uGrp, uSeg, uSeg2, uLine, uReserved));
                        dprintf(("LINNUM32: cLinNums=%#x (%u) cbLinNames=%#x (%u) uLinNumType=%#x (%s)\n",
                                 cLinNums, cLinNums, cbLinNames, cbLinNames, uLinNumType,
                                 uLinNumType < K_ELEMENTS(s_apsz) ? s_apsz[uLinNumType] : "??"));

                        if (uLine != 0)
                            return kDepErr(pThis, 1, "%#07lx - Bad LINNUM32 record, line %#x (MBZ)", (long)((const KU8 *)pHdr - pbFile), uLine);
                        cLinFiles = iLinFile = KU32_MAX;
                        if (   uLinNumType == 3 /* file names table */
                            || uLinNumType == 4 /* path table */)
                            cLinNums = 0; /* no line numbers */
                        else if (uLinNumType > 4)
                            return kDepErr(pThis, 1, "%#07lx - Bad LINNUM32 record, type %#x unknown", (long)((const KU8 *)pHdr - pbFile), uLinNumType);
                    }
                    else
                        dprintf(("LINNUM32: uGrp=%#x uSeg=%#x\n", uGrp, uSeg));


                    /* Skip file numbers (we parse them to follow the stream correctly). */
                    if (uLinNumType != 3 && uLinNumType != 4)
                    {
                        static const KU16 s_acbTypes[3] = { 2+2+4, 4+4+4, 2+2+4+4+4 };
                        KU16              cbEntry = s_acbTypes[uLinNumType];

                        while (cLinNums && cbRecLeft)
                        {
                            if (cbRecLeft < cbEntry)
                                return kDepErr(pThis, 1, "%#07lx - Bad LINNUM32 record, incomplete line entry", (long)((const KU8 *)pHdr - pbFile));

                            switch (uLinNumType)
                            {
                                case 0: /* source file */
                                    dprintf((" Line %6" KU16_PRI " of file %2" KU16_PRI " at %#010" KX32_PRI "\n",
                                             uData.pu16[0], uData.pu16[1], uData.pu32[1]));
                                    break;
                                case 1: /* listing file */
                                    dprintf((" Line %6" KU32_PRI ", statement %6" KU32_PRI " at %#010" KX32_PRI "\n",
                                             uData.pu32[0], uData.pu32[1], uData.pu32[2]));
                                    break;
                                case 2: /* source & listing file */
                                    dprintf((" Line %6" KU16_PRI " of file %2" KU16_PRI ", listning line %6" KU32_PRI ", statement %6" KU32_PRI " at %#010" KX32_PRI "\n",
                                             uData.pu16[0], uData.pu16[1], uData.pu32[1], uData.pu32[2], uData.pu32[3]));
                                    break;
                            }
                            uData.pb += cbEntry;
                            cbRecLeft -= cbEntry;
                            cLinNums--;
                        }

                        /* If at end of the announced line number entiries, we may find a file names table
                           here (who is actually emitting this?). */
                        if (!cLinNums)
                        {
                            uLinNumType = cbRecLeft > 0 ? 3 : KU8_MAX;
                            dprintf(("End-of-line-numbers; uLinNumType=%u cbRecLeft=%#x\n", uLinNumType, cbRecLeft));
                        }
                    }

                    if (uLinNumType == 3 || uLinNumType == 4)
                    {
                        /* Read the file/path table header (first time only). */
                        if (cLinFiles == KU32_MAX && iLinFile == KU32_MAX)
                        {
                            KU32 iFirstCol;
                            KU32 cCols;

                            if (cbRecLeft < 4+4+4)
                                return kDepErr(pThis, 1, "%#07lx - Bad LINNUM32 record, incomplete file/path table header", (long)((const KU8 *)pHdr - pbFile));
                            cbRecLeft -= 4+4+4;

                            iFirstCol = *uData.pu32++;  K_NOREF(iFirstCol);
                            cCols     = *uData.pu32++;  K_NOREF(cCols);
                            cLinFiles = *uData.pu32++;
                            dprintf(("%s table header: cLinFiles=%#" KX32_PRI " (%" KU32_PRI ") iFirstCol=%" KU32_PRI " cCols=%" KU32_PRI"\n",
                                     uLinNumType == 3 ? "file names" : "path", cLinFiles, cLinFiles, iFirstCol, cCols));
                            if (cLinFiles == KU32_MAX)
                                return kDepErr(pThis, 1, "%#07lx - Bad LINNUM32 record, too many file/path table entries.", (long)((const KU8 *)pHdr - pbFile));
                            iLinFile = 0;
                        }

                        /* Parse the file names / path table. */
                        while (iLinFile < cLinFiles && cbRecLeft)
                        {
                            KU16 cbName = *uData.pb++;
                            if (cbRecLeft < 1 + cbName)
                                return kDepErr(pThis, 1, "%#07lx - Bad LINNUM32 record, file/path table entry too long.", (long)((const KU8 *)pHdr - pbFile));
                            iLinFile++;
                            dprintf(("#%" KU32_PRI": %.*s\n", iLinFile, cbName, uData.pch));
                            if (uLinNumType == 3)
                            {
                                depAdd(&pThis->Core, uData.pch, cbName);
                                iSrc++;
                            }
                            cbRecLeft -= 1 + cbName;
                            uData.pb += cbName;
                        }

                        /* The end? */
                        if (iLinFile == cLinFiles)
                        {
                            uLinNumType = KU8_MAX;
                            dprintf(("End-of-file/path-table; cbRecLeft=%#x\n", cbRecLeft));
                        }
                    }
                }
                else
                    dprintf(("LINNUM32: Unknown or unsupported format\n"));
                break;

        }

        /* advance */
        cbLeft -= pHdr->cbRec + sizeof(*pHdr);
        pHdr = (PCKDEPOMFHDR)((const KU8 *)(pHdr + 1) + pHdr->cbRec);
    }

    if (cbLeft)
        return kDepErr(pThis, 1, "%#07x - Unexpected EOF. cbLeft=%#x", (const KU8*)pHdr - pbFile, cbLeft);

    if (iSrc == 0 && iMaybeSrc <= 1)
    {
        dprintf(("kDepObjOMFParse: No cylindrical smoking thing: iSrc=0 iMaybeSrc=%#" KX32_PRI"\n", iMaybeSrc));
        return 2;
    }
    dprintf(("kDepObjOMFParse: iSrc=%" KU32_PRI " iMaybeSrc=%#" KX32_PRI "\n", iSrc, iMaybeSrc));
    return 0;
}


/**
 * Checks if this file is an OMF file or not.
 *
 * @returns K_TRUE if it's OMF, K_FALSE otherwise.
 *
 * @param   pb      The start of the file.
 * @param   cb      The file size.
 */
KBOOL kDepObjOMFTest(const KU8 *pbFile, KSIZE cbFile)
{
    PCKDEPOMFTHEADR pHdr = (PCKDEPOMFTHEADR)pbFile;

    if (cbFile <= sizeof(*pHdr))
        return K_FALSE;
    if (    pHdr->Hdr.bType != KDEPOMF_THEADR
        &&  pHdr->Hdr.bType != KDEPOMF_LHEADR)
        return K_FALSE;
    if (pHdr->Hdr.cbRec + sizeof(pHdr->Hdr) >= cbFile)
        return K_FALSE;
    if (pHdr->Hdr.cbRec != 1 + pHdr->Name.cch + 1)
        return K_FALSE;

    return K_TRUE;
}


/**
 * Parses a CodeView 8 symbol section.
 *
 * @returns 0 on success, 1 on failure, 2 if no dependencies was found.
 * @param   pThis       The kDepObj instance data.
 * @param   pbSyms      Pointer to the start of the symbol section.
 * @param   cbSyms      Size of the symbol section.
 */
int kDepObjCOFFParseCV8SymbolSection(PKDEPOBJGLOBALS pThis, const KU8 *pbSyms, KU32 cbSyms)
{
    char const *    pchStrTab  = NULL;
    KU32            cbStrTab   = 0;
    KPCUINT         uSrcFiles  = {0};
    KU32            cbSrcFiles = 0;
    KU32            off        = 4;
    KU32            iSrc       = 0;

    if (cbSyms < 16)
        return 1;

    /*
     * The parsing loop.
     */
    while (off < cbSyms)
    {
        PCKDEPCV8SYMHDR pHdr = (PCKDEPCV8SYMHDR)(pbSyms + off);
        KPCUINT         uData;
        KU32            cbData;
        uData.pv = pHdr + 1;

        if (off + sizeof(*pHdr) >= cbSyms)
        {
            kDepErr(pThis, 1, "CV symbol table entry at %08" KX32_PRI " is too long; cbSyms=%#" KX32_PRI "",
                    off, cbSyms);
            return 1; /* FIXME */
        }

        cbData = pHdr->cb;
        if (off + cbData + sizeof(*pHdr) > cbSyms)
        {
            kDepErr(pThis, 1, "CV symbol table entry at %08" KX32_PRI " is too long; cbData=%#" KX32_PRI " cbSyms=%#" KX32_PRI,
                    off, cbData, cbSyms);
            return 1; /* FIXME */
        }

        /* If the size is 0, assume it covers the rest of the section. VC++ 2003 has
           been observed doing thing. */
        if (!cbData)
            cbData = cbSyms - off;

        switch (pHdr->uType)
        {
            case K_CV8_SYMBOL_INFO:
                dprintf(("%06" KX32_PRI " %06" KX32_PRI ": Symbol Info\n", off, cbData));
                /*dump(uData.pb, cbData, 0);*/
                break;

            case K_CV8_LINE_NUMBERS:
                dprintf(("%06" KX32_PRI " %06" KX32_PRI ": Line numbers\n", off, cbData));
                /*dump(uData.pb, cbData, 0);*/
                break;

            case K_CV8_STRING_TABLE:
                dprintf(("%06" KX32_PRI " %06" KX32_PRI ": String table\n", off, cbData));
                if (pchStrTab)
                    warnx(pThis->pCtx, "%s: warning: Found yet another string table!", pThis->pszFile);
                pchStrTab = uData.pch;
                cbStrTab = cbData;
                /*dump(uData.pb, cbData, 0);*/
                break;

            case K_CV8_SOURCE_FILES:
                dprintf(("%06" KX32_PRI " %06" KX32_PRI ": Source files\n", off, cbData));
                if (uSrcFiles.pb)
                    warnx(pThis->pCtx, "%s: warning: Found yet another source files table!", pThis->pszFile);
                uSrcFiles = uData;
                cbSrcFiles = cbData;
                /*dump(uData.pb, cbData, 0);*/
                break;

            case K_CV8_COMDAT_XXXXX:
                dprintf(("%06" KX32_PRI " %06" KX32_PRI ": 0xf5 Unknown COMDAT stuff\n", off, cbData));
                /*dump(uData.pb, cbData, 0);*/
                break;

            default:
                dprintf(("%06" KX32_PRI " %06" KX32_PRI ": Unknown type %#" KX32_PRI "\n",
                         off, cbData, pHdr->uType));
                dump(uData.pb, cbData, 0);
                break;
        }

        /* next */
        cbData = (cbData + 3) & ~KU32_C(3);
        off += cbData + sizeof(*pHdr);
    }

    /*
     * Did we find any source files and strings?
     */
    if (!pchStrTab || !uSrcFiles.pv)
    {
        dprintf(("kDepObjCOFFParseCV8SymbolSection: No cylindrical smoking thing: pchStrTab=%p uSrcFiles.pv=%p\n", pchStrTab, uSrcFiles.pv));
        return 2;
    }

    /*
     * Iterate the source file table.
     */
    off = 0;
    while (off < cbSrcFiles)
    {
        KU32        offFile;
        const char *pszFile;
        KSIZE       cchFile;
        KU16        u16Type;
        KPCUINT     uSrc;
        KU32        cbSrc;

        /*
         * Validate and parse the entry (variable length record are fun).
         */
        if (off + 8 > cbSrcFiles)
            return kDepErr(pThis, 1, "CV source file entry at %08" KX32_PRI " is too long; cbSrcFiles=%#" KX32_PRI,
                           off, cbSrcFiles);
        uSrc.pb = uSrcFiles.pb + off;
        u16Type = uSrc.pu16[2];
        switch (u16Type)
        {
            case 0x0110:    cbSrc = 6 + 16 + 2; break;  /* MD5 */
            case 0x0214:    cbSrc = 6 + 20 + 2; break;  /* SHA1 */ /** @todo check this */
            case 0x0320:    cbSrc = 6 + 32 + 2; break;  /* SHA-256 */
            default:        cbSrc = 6 + 0  + 2; break;
        }
        if (off + cbSrc > cbSrcFiles)
            return kDepErr(pThis, 1, "CV source file entry at %08" KX32_PRI " is too long; cbSrc=%#" KX32_PRI " cbSrcFiles=%#" KX32_PRI,
                           off, cbSrc, cbSrcFiles);

        offFile = *uSrc.pu32;
        if (offFile > cbStrTab)
            return kDepErr(pThis, 1, "CV source file entry at %08" KX32_PRI " is out side the string table; offFile=%#" KX32_PRI " cbStrTab=%#" KX32_PRI,
                    off, offFile, cbStrTab);
        pszFile = pchStrTab + offFile;
        cchFile = strlen(pszFile);
        if (cchFile == 0)
            return kDepErr(pThis, 1, "CV source file entry at %08" KX32_PRI " has an empty file name; offFile=%#x" KX32_PRI,
                           off, offFile);

        /*
         * Display the result and add it to the dependency database.
         */
        depAdd(&pThis->Core, pszFile, cchFile);
#ifdef WITH_DPRINTF
        dprintf(("#%03" KU32_PRI ": ", iSrc));
        {
            KU32 off = 6;
            for (;off < cbSrc - 2; off++)
                dprintf(("%02" KX8_PRI, uSrc.pb[off]));
            if (cbSrc == 6)
                dprintf(("type=%#06" KX16_PRI, u16Type));
            dprintf((" '%s'\n", pszFile));
        }
#endif


        /* next */
        iSrc++;
        off += cbSrc;
    }

    if (iSrc == 0)
    {
        dprintf(("kDepObjCOFFParseCV8SymbolSection: No cylindrical smoking thing: iSrc=0\n"));
        return 2;
    }
    dprintf(("kDepObjCOFFParseCV8SymbolSection: iSrc=%" KU32_PRI "\n", iSrc));
    return 0;
}


/**
 * Parses the OMF file.
 *
 * @returns 0 on success, 1 on failure, 2 if no dependencies was found.
 * @param   pThis       The kDepObj instance data.
 * @param   pbFile      The start of the file.
 * @param   cbFile      The file size.
 */
int kDepObjCOFFParse(PKDEPOBJGLOBALS pThis, const KU8 *pbFile, KSIZE cbFile)
{
    IMAGE_FILE_HEADER const            *pFileHdr   = (IMAGE_FILE_HEADER const *)pbFile;
    ANON_OBJECT_HEADER_BIGOBJ const    *pBigObjHdr = (ANON_OBJECT_HEADER_BIGOBJ const *)pbFile;
    IMAGE_SECTION_HEADER const         *paSHdrs;
    KU32                                cSHdrs;
    unsigned                            iSHdr;
    KPCUINT                             u;
    KBOOL                               fDebugS    = K_FALSE;
    int                                 rcRet      = 2;
    int                                 rc;

    if (   pBigObjHdr->Sig1 == 0
        && pBigObjHdr->Sig2 == KU16_MAX)
    {
        paSHdrs = (IMAGE_SECTION_HEADER const *)(pBigObjHdr + 1);
        cSHdrs  = pBigObjHdr->NumberOfSections;
    }
    else
    {
        paSHdrs = (IMAGE_SECTION_HEADER const *)((KU8 const *)(pFileHdr + 1) + pFileHdr->SizeOfOptionalHeader);
        cSHdrs  = pFileHdr->NumberOfSections;
    }


    dprintf(("COFF file!\n"));

    for (iSHdr = 0; iSHdr < cSHdrs; iSHdr++)
    {
        if (    !memcmp(paSHdrs[iSHdr].Name, ".debug$S", sizeof(".debug$S") - 1)
            &&  paSHdrs[iSHdr].SizeOfRawData > 4)
        {
            u.pb = pbFile + paSHdrs[iSHdr].PointerToRawData;
            dprintf(("CV symbol table: version=%x\n", *u.pu32));
            if (*u.pu32 == 0x000000004)
                rc = kDepObjCOFFParseCV8SymbolSection(pThis, u.pb, paSHdrs[iSHdr].SizeOfRawData);
            else
                rc = 2;
            dprintf(("rc=%d\n", rc));
            if (rcRet == 2)
                rcRet = rc;
            if (rcRet != 2)
                return rc;
            fDebugS = K_TRUE;
        }
        dprintf(("#%d: %.8s\n", iSHdr, paSHdrs[iSHdr].Name));
    }

    /* If we found no dependencies but did find a .debug$S section, check if
       this is a case where the compile didn't emit any because there is no
       code in this compilation unit. */
    if (rcRet == 2)
    {
        if (fDebugS)
        {
            for (iSHdr = 0; iSHdr < cSHdrs; iSHdr++)
                if (!memcmp(paSHdrs[iSHdr].Name, ".text", sizeof(".text") - 1))
                    return kDepErr(pThis, 2, "%s: no dependencies (has text).", pThis->pszFile);
            warnx(pThis->pCtx, "%s: no dependencies, but also no text, so probably (mostly) harmless.", pThis->pszFile);
            return 0;
        }
        kDepErr(pThis, 2, "%s: no dependencies.", pThis->pszFile);
    }

    return rcRet;
}


/**
 * Checks if this file is a COFF file or not.
 *
 * @returns K_TRUE if it's COFF, K_FALSE otherwise.
 *
 * @param   pThis   The kDepObj instance data.
 * @param   pb      The start of the file.
 * @param   cb      The file size.
 */
KBOOL kDepObjCOFFTest(PKDEPOBJGLOBALS pThis, const KU8 *pbFile, KSIZE cbFile)
{
    IMAGE_FILE_HEADER const         *pFileHdr   = (IMAGE_FILE_HEADER const *)pbFile;
    ANON_OBJECT_HEADER_BIGOBJ const *pBigObjHdr = (ANON_OBJECT_HEADER_BIGOBJ const *)pbFile;
    IMAGE_SECTION_HEADER const      *paSHdrs;
    KU32                             cSHdrs;
    KU32                             iSHdr;
    KSIZE                            cbHdrs;

    if (cbFile <= sizeof(*pFileHdr))
        return K_FALSE;

    /*
     * Deal with -bigobj output first.
     */
    if (   pBigObjHdr->Sig1 == 0
        && pBigObjHdr->Sig2 == KU16_MAX)
    {
        static const KU8 s_abClsId[16] = { ANON_OBJECT_HEADER_BIGOBJ_CLS_ID_BYTES };

        paSHdrs = (IMAGE_SECTION_HEADER const *)(pBigObjHdr + 1);
        cSHdrs  = pBigObjHdr->NumberOfSections;
        cbHdrs  = sizeof(IMAGE_SECTION_HEADER) * cSHdrs;

        if (cbFile <= sizeof(*pBigObjHdr))
            return K_FALSE;

        if (pBigObjHdr->Version != 2)
            return K_FALSE;
        if (memcmp(&pBigObjHdr->ClassID[0], s_abClsId, sizeof(pBigObjHdr->ClassID)) != 0)
            return K_FALSE;

        if (   pBigObjHdr->Machine != IMAGE_FILE_MACHINE_I386
            && pBigObjHdr->Machine != IMAGE_FILE_MACHINE_AMD64
            && pBigObjHdr->Machine != IMAGE_FILE_MACHINE_ARM
            && pBigObjHdr->Machine != IMAGE_FILE_MACHINE_ARMNT
            && pBigObjHdr->Machine != IMAGE_FILE_MACHINE_ARM64
            && pBigObjHdr->Machine != IMAGE_FILE_MACHINE_EBC)
        {
            kDepErr(pThis, 1, "bigobj Machine not supported: %#x", pBigObjHdr->Machine);
            return K_FALSE;
        }
        if (pBigObjHdr->Flags != 0)
        {
            kDepErr(pThis, 1, "bigobj Flags field is non-zero: %#x", pBigObjHdr->Flags);
            return K_FALSE;
        }
        if (pBigObjHdr->SizeOfData != 0)
        {
            kDepErr(pThis, 1, "bigobj SizeOfData field is non-zero: %#x", pBigObjHdr->SizeOfData);
            return K_FALSE;
        }

        if (   pBigObjHdr->PointerToSymbolTable != 0
            && (   pBigObjHdr->PointerToSymbolTable < cbHdrs
                || pBigObjHdr->PointerToSymbolTable > cbFile))
            return K_FALSE;
        if (   pBigObjHdr->PointerToSymbolTable == 0
            && pBigObjHdr->NumberOfSymbols != 0)
            return K_FALSE;
    }
    /*
     * Look for normal COFF object.
     */
    else
    {
        paSHdrs = (IMAGE_SECTION_HEADER const *)((KU8 const *)(pFileHdr + 1) + pFileHdr->SizeOfOptionalHeader);
        cSHdrs  = pFileHdr->NumberOfSections;
        cbHdrs  = (const KU8 *)&paSHdrs[cSHdrs] - (const KU8 *)pbFile;

        if (   pFileHdr->Machine != IMAGE_FILE_MACHINE_I386
            && pFileHdr->Machine != IMAGE_FILE_MACHINE_AMD64
            && pFileHdr->Machine != IMAGE_FILE_MACHINE_ARM
            && pFileHdr->Machine != IMAGE_FILE_MACHINE_ARMNT
            && pFileHdr->Machine != IMAGE_FILE_MACHINE_ARM64
            && pFileHdr->Machine != IMAGE_FILE_MACHINE_EBC)
               return K_FALSE;

        if (pFileHdr->SizeOfOptionalHeader != 0)
            return K_FALSE; /* COFF files doesn't have an optional header */

        if (   pFileHdr->PointerToSymbolTable != 0
            && (   pFileHdr->PointerToSymbolTable < cbHdrs
                || pFileHdr->PointerToSymbolTable > cbFile))
            return K_FALSE;
        if (   pFileHdr->PointerToSymbolTable == 0
            && pFileHdr->NumberOfSymbols != 0)
            return K_FALSE;
        if (  pFileHdr->Characteristics
            & (  IMAGE_FILE_DLL
               | IMAGE_FILE_SYSTEM
               | IMAGE_FILE_UP_SYSTEM_ONLY
               | IMAGE_FILE_NET_RUN_FROM_SWAP
               | IMAGE_FILE_REMOVABLE_RUN_FROM_SWAP
               | IMAGE_FILE_EXECUTABLE_IMAGE
               | IMAGE_FILE_RELOCS_STRIPPED))
            return K_FALSE;
    }
    if (   cSHdrs <= 1
        || cSHdrs > cbFile)
        return K_FALSE;
    if (cbHdrs >= cbFile)
        return K_FALSE;

    /*
     * Check the section headers.
     */
    for (iSHdr = 0; iSHdr < cSHdrs; iSHdr++)
    {
        if (   paSHdrs[iSHdr].PointerToRawData != 0
            && (   paSHdrs[iSHdr].PointerToRawData < cbHdrs
                || paSHdrs[iSHdr].PointerToRawData >= cbFile
                || paSHdrs[iSHdr].PointerToRawData + paSHdrs[iSHdr].SizeOfRawData > cbFile))
            return K_FALSE;
        if (   paSHdrs[iSHdr].PointerToRelocations != 0
            && (   paSHdrs[iSHdr].PointerToRelocations < cbHdrs
                || paSHdrs[iSHdr].PointerToRelocations >= cbFile
                || paSHdrs[iSHdr].PointerToRelocations + paSHdrs[iSHdr].NumberOfRelocations * 10 > cbFile)) /* IMAGE_RELOCATION */
            return K_FALSE;
        if (   paSHdrs[iSHdr].PointerToLinenumbers != 0
            && (   paSHdrs[iSHdr].PointerToLinenumbers < cbHdrs
                || paSHdrs[iSHdr].PointerToLinenumbers >= cbFile
                || paSHdrs[iSHdr].PointerToLinenumbers + paSHdrs[iSHdr].NumberOfLinenumbers *  6 > cbFile)) /* IMAGE_LINENUMBER */
            return K_FALSE;
    }

    return K_TRUE;
}


/**
 * Read the file into memory and parse it.
 */
static int kDepObjProcessFile(PKDEPOBJGLOBALS pThis, FILE *pInput)
{
    size_t      cbFile;
    KU8        *pbFile;
    void       *pvOpaque;
    int         rc = 0;

    /*
     * Read the file into memory.
     */
    pbFile = (KU8 *)depReadFileIntoMemory(pInput, &cbFile, &pvOpaque);
    if (!pbFile)
        return 1;

    /*
     * See if it's an OMF file, then process it.
     */
    if (kDepObjOMFTest(pbFile, cbFile))
        rc = kDepObjOMFParse(pThis, pbFile, cbFile);
    else if (kDepObjCOFFTest(pThis, pbFile, cbFile))
        rc = kDepObjCOFFParse(pThis, pbFile, cbFile);
    else
        rc = kDepErr(pThis, 1, "Doesn't recognize the header of the OMF/COFF file.");

    depFreeFileMemory(pbFile, pvOpaque);
    return rc;
}


static void kDebObjUsage(PKMKBUILTINCTX pCtx, int fIsErr)
{
    kmk_builtin_ctx_printf(pCtx, fIsErr,
                           "usage: %s -o <output> -t <target> [-fqs] [-e <ignore-ext>] <OMF or COFF file>\n"
                           "   or: %s --help\n"
                           "   or: %s --version\n",
                           pCtx->pszProgName, pCtx->pszProgName, pCtx->pszProgName);
}


int kmk_builtin_kDepObj(int argc, char **argv, char **envp, PKMKBUILTINCTX pCtx)
{
    int             i;
    KDEPOBJGLOBALS  This;

    /* Arguments. */
    FILE       *pOutput = NULL;
    const char *pszOutput = NULL;
    FILE       *pInput = NULL;
    const char *pszTarget = NULL;
    int         fStubs = 0;
    int         fFixCase = 0;
    const char *pszIgnoreExt = NULL;
    /* Argument parsing. */
    int         fInput = 0;             /* set when we've found input argument. */
    int         fQuiet = 0;

    /* Init instance data.   */
    This.pCtx = pCtx;
    This.pszFile = NULL;

    /*
     * Parse arguments.
     */
    if (argc <= 1)
    {
        kDebObjUsage(pCtx, 0);
        return 1;
    }
    for (i = 1; i < argc; i++)
    {
        if (argv[i][0] == '-')
        {
            const char *pszValue;
            const char *psz = &argv[i][1];
            char chOpt;
            chOpt = *psz++;
            if (chOpt == '-')
            {
                /* Convert long to short option. */
                if (!strcmp(psz, "quiet"))
                    chOpt = 'q';
                else if (!strcmp(psz, "help"))
                    chOpt = '?';
                else if (!strcmp(psz, "version"))
                    chOpt = 'V';
                else
                {
                    errx(pCtx, 2, "Invalid argument '%s'.", argv[i]);
                    kDebObjUsage(pCtx, 1);
                    return 2;
                }
                psz = "";
            }

            /*
             * Requires value?
             */
            switch (chOpt)
            {
                case 'o':
                case 't':
                case 'e':
                    if (*psz)
                        pszValue = psz;
                    else if (++i < argc)
                        pszValue = argv[i];
                    else
                        return errx(pCtx, 2, "The '-%c' option takes a value.", chOpt);
                    break;

                default:
                    pszValue = NULL;
                    break;
            }


            switch (chOpt)
            {
                /*
                 * Output file.
                 */
                case 'o':
                {
                    if (pOutput)
                        return errx(pCtx, 2, "only one output file!");
                    pszOutput = pszValue;
                    if (pszOutput[0] == '-' && !pszOutput[1])
                        pOutput = stdout;
                    else
                        pOutput = fopen(pszOutput, "w" KMK_FOPEN_NO_INHERIT_MODE);
                    if (!pOutput)
                        return err(pCtx, 1, "Failed to create output file '%s'", pszOutput);
                    break;
                }

                /*
                 * Target name.
                 */
                case 't':
                {
                    if (pszTarget)
                        return errx(pCtx, 2, "only one target!");
                    pszTarget = pszValue;
                    break;
                }

                /*
                 * Fix case.
                 */
                case 'f':
                {
                    fFixCase = 1;
                    break;
                }

                /*
                 * Quiet.
                 */
                case 'q':
                {
                    fQuiet = 1;
                    break;
                }

                /*
                 * Generate stubs.
                 */
                case 's':
                {
                    fStubs = 1;
                    break;
                }

                /*
                 * Extension to ignore.
                 */
                case 'e':
                {
                    if (pszIgnoreExt)
                        return errx(pCtx, 2, "The '-e' option can only be used once!");
                    pszIgnoreExt = pszValue;
                    break;
                }

                /*
                 * The mandatory version & help.
                 */
                case '?':
                    kDebObjUsage(pCtx, 0);
                    return 0;
                case 'V':
                case 'v':
                    return kbuild_version(argv[0]);

                /*
                 * Invalid argument.
                 */
                default:
                    errx(pCtx, 2, "Invalid argument '%s'.", argv[i]);
                    kDebObjUsage(pCtx, 1);
                    return 2;
            }
        }
        else
        {
            pInput = fopen(argv[i], "rb" KMK_FOPEN_NO_INHERIT_MODE);
            if (!pInput)
                return err(pCtx, 1, "Failed to open input file '%s'", argv[i]);
            This.pszFile = argv[i];
            fInput = 1;
        }

        /*
         * End of the line?
         */
        if (fInput)
        {
            if (++i < argc)
                return errx(pCtx, 2, "No arguments shall follow the input spec.");
            break;
        }
    }

    /*
     * Got all we require?
     */
    if (!pInput)
        return errx(pCtx, 2, "No input!");
    if (!pOutput)
        return errx(pCtx, 2, "No output!");
    if (!pszTarget)
        return errx(pCtx, 2, "No target!");

    /*
     * Do the parsing.
     */
    depInit(&This.Core);
    i = kDepObjProcessFile(&This, pInput);
    fclose(pInput);

    /*
     * Write the dependecy file.
     */
    if (!i)
    {
        depOptimize(&This.Core, fFixCase, fQuiet, pszIgnoreExt);
        depPrintTargetWithDeps(&This.Core, pOutput, pszTarget, 1 /*fEscapeTarget*/);
        if (fStubs)
            depPrintStubs(&This.Core, pOutput);
    }

    /*
     * Close the output, delete output on failure.
     */
    if (!i && ferror(pOutput))
        i = errx(pCtx, 1, "Error writing to '%s'", pszOutput);
    fclose(pOutput);
    if (i)
    {
        if (unlink(pszOutput))
            warn(pCtx, "warning: failed to remove output file '%s' on failure.", pszOutput);
    }

    depCleanup(&This.Core);
    return i;
}

#ifdef KMK_BUILTIN_STANDALONE
int main(int argc, char **argv, char **envp)
{
    KMKBUILTINCTX Ctx = { "kDepObj", NULL };
    return kmk_builtin_kDepObj(argc, argv, envp, &Ctx);
}
#endif

