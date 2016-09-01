/* $Id: kDepObj.c 2856 2016-09-01 02:42:08Z knut.osmundsen@oracle.com $ */
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
#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <stdarg.h>
#ifdef HAVE_ALLOCA_H
# include <alloca.h>
#endif
#if !defined(_MSC_VER)
# include <unistd.h>
#else
# include <io.h>
#endif
#include "k/kDefs.h"
#include "k/kTypes.h"
#include "k/kLdrFmts/pe.h"
#include "kDep.h"
#include "kmkbuiltin.h"


/*******************************************************************************
*   Defined Constants And Macros                                               *
*******************************************************************************/
/*#define DEBUG*/
#ifdef DEBUG
# define dprintf(a)             printf a
# define dump(pb, cb, offBase)  depHexDump(pb,cb,offBase)
#else
# define dprintf(a)             do {} while (0)
# define dump(pb, cb, offBase)  do {} while (0)
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


/*******************************************************************************
*   Global Variables                                                           *
*******************************************************************************/
/** the executable name. */
static const char *argv0 = "";
static const char *g_pszFile = NULL;


/**
 * Prints an error message.
 *
 * @returns rc.
 * @param   rc          The return code, for making one line return statements.
 * @param   pszFormat   The message format string.
 * @param   ...         Format arguments.
 * @todo    Promote this to kDep.c.
 */
static int kDepErr(int rc, const char *pszFormat, ...)
{
    va_list va;
    const char *psz;
    const char *pszName = argv0;

    fflush(stdout);

    /* The message prefix. */
    while ((psz = strpbrk(pszName, "/\\:")) != NULL)
        pszName = psz + 1;

    if (g_pszFile)
        fprintf(stderr, "%s: %s: error: ", pszName, g_pszFile);
    else
        fprintf(stderr, "%s: error: ", pszName);

    /* The message. */
    va_start(va, pszFormat);
    vfprintf(stderr, pszFormat, va);
    va_end(va);

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
 * @param   pbFile      The start of the file.
 * @param   cbFile      The file size.
 */
int kDepObjOMFParse(const KU8 *pbFile, KSIZE cbFile)
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
                    return kDepErr(1, "%#07x - Bad %cHEADR record, length mismatch.\n",
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
                    depAdd(pTHeadr->Name.ach, pTHeadr->Name.cch);
                    iMaybeSrc++;
                }
                uLinNumType = KU8_MAX;
                break;
            }

            case KDEPOMF_COMENT:
            {
                KU8 uClass;

                if (pHdr->cbRec < 2 + 1)
                    return kDepErr(1, "%#07x - Bad COMMENT record, too small.\n", (const KU8*)pHdr - pbFile);
                if (uData.pb[0] & 0x3f)
                    return kDepErr(1, "%#07x - Bad COMMENT record, reserved flags set.\n", (const KU8*)pHdr - pbFile);
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
                            return kDepErr(1, "%#07lx - Bad DEPENDENCY FILE record, length mismatch. (%u/%u)\n",
                                           (long)((const KU8 *)pHdr - pbFile),
                                           K_OFFSETOF(KDEPOMFDEPFILE, Name.ach[pDep->Name.cch]) + 1,
                                           (unsigned)(pHdr->cbRec + sizeof(*pHdr)));
                        }
                        depAdd(pDep->Name.ach, pDep->Name.cch);
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
                        return kDepErr(1, "%#07lx - Bad LINNUM32 record\n", (long)((const KU8 *)pHdr - pbFile));

                    if (uLinNumType == KU8_MAX)
                    {
                        static const char * const s_apsz[5] =
                        {
                            "source file", "listing file", "source & listing file", "file names table", "path table"
                        };
                        KU16 uLine;
                        KU8  uReserved;
                        KU16 uSeg2;
                        KU32 cbLinNames;

                        if (cbRecLeft < 2+1+1+2+2+4)
                            return kDepErr(1, "%#07lx - Bad LINNUM32 record, too short\n", (long)((const KU8 *)pHdr - pbFile));
                        cbRecLeft  -= 2+1+1+2+2+4;
                        uLine       = *uData.pu16++;
                        uLinNumType = *uData.pu8++;
                        uReserved   = *uData.pu8++;
                        cLinNums    = *uData.pu16++;
                        uSeg2       = *uData.pu16++;
                        cbLinNames  = *uData.pu32++;

                        dprintf(("LINNUM32: uGrp=%#x uSeg=%#x uSeg2=%#x uLine=%#x (MBZ) uReserved=%#x\n",
                                 uGrp, uSeg, uSeg2, uLine, uReserved));
                        dprintf(("LINNUM32: cLinNums=%#x (%u) cbLinNames=%#x (%u) uLinNumType=%#x (%s)\n",
                                 cLinNums, cLinNums, cbLinNames, cbLinNames, uLinNumType,
                                 uLinNumType < K_ELEMENTS(s_apsz) ? s_apsz[uLinNumType] : "??"));
                        if (uLine != 0)
                            return kDepErr(1, "%#07lx - Bad LINNUM32 record, line %#x (MBZ)\n", (long)((const KU8 *)pHdr - pbFile), uLine);
                        cLinFiles = iLinFile = KU32_MAX;
                        if (   uLinNumType == 3 /* file names table */
                            || uLinNumType == 4 /* path table */)
                            cLinNums = 0; /* no line numbers */
                        else if (uLinNumType > 4)
                            return kDepErr(1, "%#07lx - Bad LINNUM32 record, type %#x unknown\n", (long)((const KU8 *)pHdr - pbFile), uLinNumType);
                    }
                    else
                        dprintf(("LINNUM32: uGrp=%#x uSeg=%#x\n", uGrp, uSeg));


                    /* Skip file numbers (we parse them to follow the stream correctly). */
                    if (uLinNumType != 3 && uLinNumType != 4)
                    {
                        static const unsigned s_acbTypes[3] = { 2+2+4, 4+4+4, 2+2+4+4+4 };
                        unsigned              cbEntry = s_acbTypes[uLinNumType];

                        while (cLinNums && cbRecLeft)
                        {
                            if (cbRecLeft < cbEntry)
                                return kDepErr(1, "%#07lx - Bad LINNUM32 record, incomplete line entry\n", (long)((const KU8 *)pHdr - pbFile));

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
                                return kDepErr(1, "%#07lx - Bad LINNUM32 record, incomplete file/path table header\n", (long)((const KU8 *)pHdr - pbFile));
                            cbRecLeft -= 4+4+4;

                            iFirstCol = *uData.pu32++;
                            cCols     = *uData.pu32++;
                            cLinFiles = *uData.pu32++;
                            dprintf(("%s table header: cLinFiles=%#" KX32_PRI " (%" KU32_PRI ") iFirstCol=%" KU32_PRI " cCols=%" KU32_PRI"\n",
                                     uLinNumType == 3 ? "file names" : "path", cLinFiles, cLinFiles, iFirstCol, cCols));
                            if (cLinFiles == KU32_MAX)
                                return kDepErr(1, "%#07lx - Bad LINNUM32 record, too many file/path table entries.\n", (long)((const KU8 *)pHdr - pbFile));
                            iLinFile = 0;
                        }

                        /* Parse the file names / path table. */
                        while (iLinFile < cLinFiles && cbRecLeft)
                        {
                            int cbName = *uData.pb++;
                            if (cbRecLeft < 1 + cbName)
                                return kDepErr(1, "%#07lx - Bad LINNUM32 record, file/path table entry too long.\n", (long)((const KU8 *)pHdr - pbFile));
                            iLinFile++;
                            dprintf(("#%" KU32_PRI": %.*s\n", iLinFile, cbName, uData.pch));
                            if (uLinNumType == 3)
                            {
                                depAdd(uData.pch, cbName);
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
        return kDepErr(1, "%#07x - Unexpected EOF. cbLeft=%#x\n", (const KU8*)pHdr - pbFile, cbLeft);

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
 * @param   pbSyms      Pointer to the start of the symbol section.
 * @param   cbSyms      Size of the symbol section.
 */
int kDepObjCOFFParseCV8SymbolSection(const KU8 *pbSyms, KSIZE cbSyms)
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
            fprintf(stderr, "%s: CV symbol table entry at %08" KX32_PRI " is too long; cbSyms=%#" KSIZE_PRI "\n",
                    argv0, off, cbSyms);
            return 1; /* FIXME */
        }

        cbData = pHdr->cb;
        if (off + cbData + sizeof(*pHdr) > cbSyms)
        {
            fprintf(stderr, "%s: CV symbol table entry at %08" KX32_PRI " is too long; cbData=%#" KX32_PRI " cbSyms=%#" KSIZE_PRI "\n",
                    argv0, off, cbData, cbSyms);
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
                    fprintf(stderr, "%s: warning: Found yet another string table!\n", argv0);
                pchStrTab = uData.pch;
                cbStrTab = cbData;
                /*dump(uData.pb, cbData, 0);*/
                break;

            case K_CV8_SOURCE_FILES:
                dprintf(("%06" KX32_PRI " %06" KX32_PRI ": Source files\n", off, cbData));
                if (uSrcFiles.pb)
                    fprintf(stderr, "%s: warning: Found yet another source files table!\n", argv0);
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
        {
            fprintf(stderr, "%s: CV source file entry at %08" KX32_PRI " is too long; cbSrcFiles=%#" KX32_PRI "\n",
                    argv0, off, cbSrcFiles);
            return 1;
        }
        uSrc.pb = uSrcFiles.pb + off;
        u16Type = uSrc.pu16[2];
        cbSrc = u16Type == 0x0110 ? 6 + 16 + 2 : 6 + 2;
        if (off + cbSrc > cbSrcFiles)
        {
            fprintf(stderr, "%s: CV source file entry at %08" KX32_PRI " is too long; cbSrc=%#" KX32_PRI " cbSrcFiles=%#" KX32_PRI "\n",
                    argv0, off, cbSrc, cbSrcFiles);
            return 1;
        }

        offFile = *uSrc.pu32;
        if (offFile > cbStrTab)
        {
            fprintf(stderr, "%s: CV source file entry at %08" KX32_PRI " is out side the string table; offFile=%#" KX32_PRI " cbStrTab=%#" KX32_PRI "\n",
                    argv0, off, offFile, cbStrTab);
            return 1;
        }
        pszFile = pchStrTab + offFile;
        cchFile = strlen(pszFile);
        if (cchFile == 0)
        {
            fprintf(stderr, "%s: CV source file entry at %08" KX32_PRI " has an empty file name; offFile=%#x" KX32_PRI "\n",
                    argv0, off, offFile);
            return 1;
        }

        /*
         * Display the result and add it to the dependency database.
         */
        depAdd(pszFile, cchFile);
        if (u16Type == 0x0110)
            dprintf(("#%03" KU32_PRI ": {todo-md5-todo} '%s'\n",
                     iSrc, pszFile));
        else
            dprintf(("#%03" KU32_PRI ": type=%#06" KX16_PRI " '%s'\n", iSrc, u16Type, pszFile));


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
 * @param   pbFile      The start of the file.
 * @param   cbFile      The file size.
 */
int kDepObjCOFFParse(const KU8 *pbFile, KSIZE cbFile)
{
    IMAGE_FILE_HEADER const    *pFileHdr = (IMAGE_FILE_HEADER const *)pbFile;
    IMAGE_SECTION_HEADER const *paSHdrs  = (IMAGE_SECTION_HEADER const *)((KU8 const *)(pFileHdr + 1) + pFileHdr->SizeOfOptionalHeader);
    unsigned                    cSHdrs   = pFileHdr->NumberOfSections;
    unsigned                    iSHdr;
    KPCUINT                     u;
    int                         rcRet    = 2;
    int                         rc;

    printf("COFF file!\n");

    for (iSHdr = 0; iSHdr < cSHdrs; iSHdr++)
    {
        if (    !memcmp(paSHdrs[iSHdr].Name, ".debug$S", sizeof(".debug$S") - 1)
            &&  paSHdrs[iSHdr].SizeOfRawData > 4)
        {
            u.pb = pbFile + paSHdrs[iSHdr].PointerToRawData;
            dprintf(("CV symbol table: version=%x\n", *u.pu32));
            if (*u.pu32 == 0x000000004)
                rc = kDepObjCOFFParseCV8SymbolSection(u.pb, paSHdrs[iSHdr].SizeOfRawData);
            else
                rc = 2;
            dprintf(("rc=%d\n", rc));
            if (rcRet == 2)
                rcRet = rc;
            if (rcRet != 2)
                return rc;
        }
        printf("#%d: %.8s\n", iSHdr, paSHdrs[iSHdr].Name);
    }
    return rcRet;
}


/**
 * Checks if this file is a COFF file or not.
 *
 * @returns K_TRUE if it's COFF, K_FALSE otherwise.
 *
 * @param   pb      The start of the file.
 * @param   cb      The file size.
 */
KBOOL kDepObjCOFFTest(const KU8 *pbFile, KSIZE cbFile)
{
    IMAGE_FILE_HEADER const    *pFileHdr = (IMAGE_FILE_HEADER const *)pbFile;
    IMAGE_SECTION_HEADER const *paSHdrs  = (IMAGE_SECTION_HEADER const *)((KU8 const *)(pFileHdr + 1) + pFileHdr->SizeOfOptionalHeader);
    unsigned                    cSHdrs   = pFileHdr->NumberOfSections;
    unsigned                    iSHdr;
    KSIZE                       cbHdrs   = (const KU8 *)&paSHdrs[cSHdrs] - (const KU8 *)pbFile;

    if (cbFile <= sizeof(*pFileHdr))
        return K_FALSE;
    if (    pFileHdr->Machine != IMAGE_FILE_MACHINE_I386
        &&  pFileHdr->Machine != IMAGE_FILE_MACHINE_AMD64)
        return K_FALSE;
    if (pFileHdr->SizeOfOptionalHeader != 0)
        return K_FALSE; /* COFF files doesn't have an optional header */

    if (    pFileHdr->NumberOfSections <= 1
        ||  pFileHdr->NumberOfSections > cbFile)
        return K_FALSE;

    if (cbHdrs >= cbFile)
        return K_FALSE;

    if (    pFileHdr->PointerToSymbolTable != 0
        &&  (   pFileHdr->PointerToSymbolTable < cbHdrs
             || pFileHdr->PointerToSymbolTable > cbFile))
        return K_FALSE;
    if (    pFileHdr->PointerToSymbolTable == 0
        &&  pFileHdr->NumberOfSymbols != 0)
        return K_FALSE;
    if (    pFileHdr->Characteristics
        &   (  IMAGE_FILE_DLL
             | IMAGE_FILE_SYSTEM
             | IMAGE_FILE_UP_SYSTEM_ONLY
             | IMAGE_FILE_NET_RUN_FROM_SWAP
             | IMAGE_FILE_REMOVABLE_RUN_FROM_SWAP
             | IMAGE_FILE_EXECUTABLE_IMAGE
             | IMAGE_FILE_RELOCS_STRIPPED))
        return K_FALSE;

    for (iSHdr = 0; iSHdr < cSHdrs; iSHdr++)
    {
        if (    paSHdrs[iSHdr].PointerToRawData != 0
            &&  (   paSHdrs[iSHdr].PointerToRawData < cbHdrs
                 || paSHdrs[iSHdr].PointerToRawData >= cbFile
                 || paSHdrs[iSHdr].PointerToRawData + paSHdrs[iSHdr].SizeOfRawData > cbFile))
            return K_FALSE;
        if (    paSHdrs[iSHdr].PointerToRelocations != 0
            &&  (   paSHdrs[iSHdr].PointerToRelocations < cbHdrs
                 || paSHdrs[iSHdr].PointerToRelocations >= cbFile
                 || paSHdrs[iSHdr].PointerToRelocations + paSHdrs[iSHdr].NumberOfRelocations * 10 > cbFile)) /* IMAGE_RELOCATION */
            return K_FALSE;
        if (    paSHdrs[iSHdr].PointerToLinenumbers != 0
            &&  (   paSHdrs[iSHdr].PointerToLinenumbers < cbHdrs
                 || paSHdrs[iSHdr].PointerToLinenumbers >= cbFile
                 || paSHdrs[iSHdr].PointerToLinenumbers + paSHdrs[iSHdr].NumberOfLinenumbers *  6 > cbFile)) /* IMAGE_LINENUMBER */
            return K_FALSE;
    }

    return K_TRUE;
}


/**
 * Read the file into memory and parse it.
 */
static int kDepObjProcessFile(FILE *pInput)
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
        rc = kDepObjOMFParse(pbFile, cbFile);
    else if (kDepObjCOFFTest(pbFile, cbFile))
        rc = kDepObjCOFFParse(pbFile, cbFile);
    else
    {
        fprintf(stderr, "%s: error: Doesn't recognize the header of the OMF file.\n", argv0);
        rc = 1;
    }

    depFreeFileMemory(pbFile, pvOpaque);
    return rc;
}


static void usage(const char *a_argv0)
{
    printf("usage: %s -o <output> -t <target> [-fqs] <OMF-file>\n"
           "   or: %s --help\n"
           "   or: %s --version\n",
           a_argv0, a_argv0, a_argv0);
}


int kmk_builtin_kDepObj(int argc, char *argv[], char **envp)
{
    int         i;

    /* Arguments. */
    FILE       *pOutput = NULL;
    const char *pszOutput = NULL;
    FILE       *pInput = NULL;
    const char *pszTarget = NULL;
    int         fStubs = 0;
    int         fFixCase = 0;
    /* Argument parsing. */
    int         fInput = 0;             /* set when we've found input argument. */
    int         fQuiet = 0;

    argv0 = argv[0];

    /*
     * Parse arguments.
     */
    if (argc <= 1)
    {
        usage(argv[0]);
        return 1;
    }
    for (i = 1; i < argc; i++)
    {
        if (argv[i][0] == '-')
        {
            const char *psz = &argv[i][1];
            if (*psz == '-')
            {
                if (!strcmp(psz, "-quiet"))
                    psz = "q";
                else if (!strcmp(psz, "-help"))
                    psz = "?";
                else if (!strcmp(psz, "-version"))
                    psz = "V";
            }

            switch (*psz)
            {
                /*
                 * Output file.
                 */
                case 'o':
                {
                    pszOutput = &argv[i][2];
                    if (pOutput)
                    {
                        fprintf(stderr, "%s: syntax error: only one output file!\n", argv[0]);
                        return 1;
                    }
                    if (!*pszOutput)
                    {
                        if (++i >= argc)
                        {
                            fprintf(stderr, "%s: syntax error: The '-o' argument is missing the filename.\n", argv[0]);
                            return 1;
                        }
                        pszOutput = argv[i];
                    }
                    if (pszOutput[0] == '-' && !pszOutput[1])
                        pOutput = stdout;
                    else
                        pOutput = fopen(pszOutput, "w");
                    if (!pOutput)
                    {
                        fprintf(stderr, "%s: error: Failed to create output file '%s'.\n", argv[0], pszOutput);
                        return 1;
                    }
                    break;
                }

                /*
                 * Target name.
                 */
                case 't':
                {
                    if (pszTarget)
                    {
                        fprintf(stderr, "%s: syntax error: only one target!\n", argv[0]);
                        return 1;
                    }
                    pszTarget = &argv[i][2];
                    if (!*pszTarget)
                    {
                        if (++i >= argc)
                        {
                            fprintf(stderr, "%s: syntax error: The '-t' argument is missing the target name.\n", argv[0]);
                            return 1;
                        }
                        pszTarget = argv[i];
                    }
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
                 * The mandatory version & help.
                 */
                case '?':
                    usage(argv[0]);
                    return 0;
                case 'V':
                case 'v':
                    return kbuild_version(argv[0]);

                /*
                 * Invalid argument.
                 */
                default:
                    fprintf(stderr, "%s: syntax error: Invalid argument '%s'.\n", argv[0], argv[i]);
                    usage(argv[0]);
                    return 1;
            }
        }
        else
        {
            pInput = fopen(argv[i], "rb");
            if (!pInput)
            {
                fprintf(stderr, "%s: error: Failed to open input file '%s'.\n", argv[0], argv[i]);
                return 1;
            }
            fInput = 1;
        }

        /*
         * End of the line?
         */
        if (fInput)
        {
            if (++i < argc)
            {
                fprintf(stderr, "%s: syntax error: No arguments shall follow the input spec.\n", argv[0]);
                return 1;
            }
            break;
        }
    }

    /*
     * Got all we require?
     */
    if (!pInput)
    {
        fprintf(stderr, "%s: syntax error: No input!\n", argv[0]);
        return 1;
    }
    if (!pOutput)
    {
        fprintf(stderr, "%s: syntax error: No output!\n", argv[0]);
        return 1;
    }
    if (!pszTarget)
    {
        fprintf(stderr, "%s: syntax error: No target!\n", argv[0]);
        return 1;
    }

    /*
     * Do the parsing.
     */
    i = kDepObjProcessFile(pInput);
    fclose(pInput);

    /*
     * Write the dependecy file.
     */
    if (!i)
    {
        depOptimize(fFixCase, fQuiet);
        fprintf(pOutput, "%s:", pszTarget);
        depPrint(pOutput);
        if (fStubs)
            depPrintStubs(pOutput);
    }

    /*
     * Close the output, delete output on failure.
     */
    if (!i && ferror(pOutput))
    {
        i = 1;
        fprintf(stderr, "%s: error: Error writing to '%s'.\n", argv[0], pszOutput);
    }
    fclose(pOutput);
    if (i)
    {
        if (unlink(pszOutput))
            fprintf(stderr, "%s: warning: failed to remove output file '%s' on failure.\n", argv[0], pszOutput);
    }

    depCleanup();
    return i;
}

