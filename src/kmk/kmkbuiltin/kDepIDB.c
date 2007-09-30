/* $Id: kDepIDB.c 1162 2007-09-30 03:55:36Z knut.osmundsen@oracle.com $ */
/** @file
 *
 * kDepIDB - Extract dependency information from a MS Visual C++ .idb file.
 *
 * Copyright (c) 2006-2007 knut st. osmundsen <bird-kBuild-spam@anduin.net>
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
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#if !defined(_MSC_VER)
# include <stdint.h>
#else
 typedef unsigned char  uint8_t;
 typedef unsigned short uint16_t;
 typedef unsigned int   uint32_t;
#endif
/*#include "kDep.h"*/
#include "../../lib/kDep.h"
#include "kmkbuiltin.h"

#define OFFSETOF(type, member)  ( (int)(size_t)(void *)&( ((type *)(void *)0)->member) )

/*#define DEBUG*/
#ifdef DEBUG
# define dprintf(a)             printf a
# define dump(pb, cb, offBase)  hexdump(pb,cb,offBase)
#else
# define dprintf(a)             do {} while (0)
# define dump(pb, cb, offBase)  do {} while (0)
#endif


/*******************************************************************************
*   Global Variables                                                           *
*******************************************************************************/
/** the executable name. */
static const char *argv0 = "";

#ifdef DEBUG
/**
 * Performs a hexdump.
 */
static void hexdump(const uint8_t *pb, size_t cb, size_t offBase)
{
    static const char   szHex[16] = "0123456789abcdef";

    const unsigned      cchWidth = 16;
    size_t              off = 0;
    while (off < cb)
    {
        unsigned i;
        printf("%s%0*x %04x:", off ? "\n" : "", sizeof(pb) * 2, offBase + off, off);
        for (i = 0; i < cchWidth && off + i < cb ; i++)
            printf(off + i < cb ? !(i & 7) && i ? "-%02x" : " %02x" : "   ", pb[i]);

        while (i++ < cchWidth)
                printf("   ");
        printf(" ");

        for (i = 0; i < cchWidth && off + i < cb; i++)
        {
            const uint8_t u8 = pb[i];
            printf("%c", u8 < 127 && u8 >= 32 ? u8 : '.', 1);
        }
        off += cchWidth;
        pb  += cchWidth;
    }
    printf("\n");
}
#endif

/**
 * Scans a stream (chunk of data really) for dependencies.
 *
 * @returns 0 on success.
 * @returns !0 on failure.
 * @param   pbStream        The stream bits.
 * @param   cbStream        The size of the stream.
 * @param   pszPrefix       The dependency prefix.
 * @param   cchPrefix       The size of the prefix.
 */
static int ScanStream(uint8_t *pbStream, size_t cbStream, const char *pszPrefix, size_t cchPrefix)
{
    const uint8_t  *pbCur = pbStream;
    size_t          cbLeft = cbStream;
    register char   chFirst = *pszPrefix;
    while (cbLeft > cchPrefix + 2)
    {
        if (    *pbCur != chFirst
            ||  memcmp(pbCur, pszPrefix, cchPrefix))
        {
            pbCur++;
            cbLeft--;
        }
        else
        {
            size_t cchDep;
            pbCur += cchPrefix;
            cchDep = strlen(pbCur);
            depAdd(pbCur, cchDep);
            dprintf(("%05x: '%s'\n", pbCur - pbStream, pbCur));

            pbCur += cchDep;
            cbLeft -= cchDep + cchPrefix;
        }
    }

    return 0;
}



/**
 * Reads the file specified by the pInput file stream into memory.
 * The size of the file is returned in *pcbFile if specified.
 * The returned pointer should be freed by free().
 */
void *ReadFileIntoMemory(FILE *pInput, size_t *pcbFile)
{
    void       *pvFile;
    long        cbFile;
    int         rc = 0;

    /*
     * Figure out file size.
     */
    if (    fseek(pInput, 0, SEEK_END) < 0
        ||  (cbFile = ftell(pInput)) < 0
        ||  fseek(pInput, 0, SEEK_SET))
    {
        fprintf(stderr, "%s: error: Failed to determin file size.\n", argv0);
        return NULL;
    }
    if (pcbFile)
        *pcbFile = cbFile;

    /*
     * Allocate memory and read the file.
     */
    pvFile = malloc(cbFile + 1);
    if (pvFile)
    {
        if (fread(pvFile, cbFile, 1, pInput))
        {
            ((uint8_t *)pvFile)[cbFile] = '\0';
            return pvFile;
        }
        fprintf(stderr, "%s: error: Failed to read %ld bytes.\n", argv0, cbFile);
        free(pvFile);
    }
    else
        fprintf(stderr, "%s: error: Failed to allocate %ld bytes (file mapping).\n", argv0, cbFile);
    return NULL;
}



///////////////////////////////////////////////////////////////////////////////
//
//
//  P D B   7 . 0
//
//
///////////////////////////////////////////////////////////////////////////////

/** A PDB 7.0 Page number. */
typedef uint32_t PDB70PAGE;
/** Pointer to a PDB 7.0 Page number. */
typedef PDB70PAGE *PPDB70PAGE;

/**
 * A PDB 7.0 stream.
 */
typedef struct PDB70STREAM
{
    /** The size of the stream. */
    uint32_t    cbStream;
} PDB70STREAM, *PPDB70STREAM;


/** The PDB 7.00 signature. */
#define PDB_SIGNATURE_700 "Microsoft C/C++ MSF 7.00\r\n\x1A" "DS\0\0"
/**
 * The PDB 7.0 header.
 */
typedef struct PDB70HDR
{
    /** The signature string. */
    uint8_t     szSignature[sizeof(PDB_SIGNATURE_700)];
    /** The page size. */
    uint32_t    cbPage;
    /** The start page. */
    PDB70PAGE   iStartPage;
    /** The number of pages in the file. */
    PDB70PAGE   cPages;
    /** The root stream directory. */
    uint32_t    cbRoot;
    /** Unknown function, always 0. */
    uint32_t    u32Reserved;
    /** The page index of the root page table. */
    PDB70PAGE   iRootPages;
} PDB70HDR, *PPDB70HDR;

/**
 * The PDB 7.0 root directory.
 */
typedef struct PDB70ROOT
{
    /** The number of streams */
    uint32_t    cStreams;
    /** Array of streams. */
    PDB70STREAM aStreams[1];
    /* uint32_t aiPages[] */
} PDB70ROOT, *PPDB70ROOT;

/**
 * The PDB 7.0 name stream (#1) header.
 */
typedef struct PDB70NAMES
{
    /** The structure version. */
    uint32_t        Version;
    /** Timestamp.  */
    uint32_t        TimeStamp;
    /** Unknown. */
    uint32_t        Unknown1;
    /** GUID. */
    uint32_t        u32Guid[4];
    /** The size of the following name table. */
    uint32_t        cbNames;
    /** The name table. */
    char            szzNames[1];
} PDB70NAMES, *PPDB70NAMES;

/** The version / magic of the names structure. */
#define PDB70NAMES_VERSION  20000404


static int Pdb70ValidateHeader(PPDB70HDR pHdr, size_t cbFile)
{
    if (pHdr->cbPage * pHdr->cPages != cbFile)
    {
        fprintf(stderr, "%s: error: Bad PDB 2.0 header - cbPage * cPages != cbFile.\n", argv0);
        return 1;
    }
    if (pHdr->iStartPage >= pHdr->cPages && pHdr->iStartPage <= 0)
    {
        fprintf(stderr, "%s: error: Bad PDB 2.0 header - iStartPage=%u cPages=%u.\n", argv0,
                pHdr->iStartPage, pHdr->cPages);
        return 1;
    }
    if (pHdr->iRootPages >= pHdr->cPages && pHdr->iRootPages <= 0)
    {
        fprintf(stderr, "%s: error: Bad PDB 2.0 header - iRootPages=%u cPage=%u.\n", argv0,
                pHdr->iStartPage, pHdr->cPages);
        return 1;
    }
    return 0;
}

static size_t Pdb70Align(PPDB70HDR pHdr, size_t cb)
{
    if (cb == ~(uint32_t)0 || !cb)
        return 0;
    return ((cb + pHdr->cbPage - 1) / pHdr->cbPage) * pHdr->cbPage;
}

static size_t Pdb70Pages(PPDB70HDR pHdr, size_t cb)
{
    if (cb == ~(uint32_t)0 || !cb)
        return 0;
    return (cb + pHdr->cbPage - 1) / pHdr->cbPage;
}

static void *Pdb70AllocAndRead(PPDB70HDR pHdr, size_t cb, PPDB70PAGE paiPageMap)
{
    const size_t    cbPage = pHdr->cbPage;
    size_t          cPages = Pdb70Pages(pHdr, cb);
    uint8_t *pbBuf = malloc(cPages * cbPage + 1);
    if (pbBuf)
    {
        size_t iPage = 0;
        while (iPage < cPages)
        {
            size_t off = paiPageMap[iPage];
            if (off < pHdr->cPages)
            {
                off *= cbPage;
                memcpy(pbBuf + iPage * cbPage, (uint8_t *)pHdr + off, cbPage);
                dump(pbBuf + iPage * cbPage, iPage + 1 < cPages ? cbPage : cb % cbPage, off);
            }
            else
            {
                fprintf(stderr, "%s: warning: Invalid page index %u (max %u)!\n", argv0,
                        (unsigned)off, pHdr->cPages);
                memset(pbBuf + iPage * cbPage, 0, cbPage);
            }

            iPage++;
        }
        pbBuf[cPages * cbPage] = '\0';
    }
    else
        fprintf(stderr, "%s: error: failed to allocate %u bytes\n", argv0, cPages * cbPage + 1);
    return pbBuf;
}

static PPDB70ROOT Pdb70AllocAndReadRoot(PPDB70HDR pHdr)
{
    /*
     * The tricky bit here is to find the right length. Really?
     * (Todo: Check if we can just use the stream #0 size..)
     */
    PPDB70PAGE piPageMap = (uint32_t *)((uint8_t *)pHdr + pHdr->iRootPages * pHdr->cbPage);
    PPDB70ROOT pRoot = Pdb70AllocAndRead(pHdr, pHdr->cbRoot, piPageMap);
    if (pRoot)
    {
#if 1
        /* This stuff is probably unnecessary: */
        /* size = stream header + array of stream. */
        size_t cb = OFFSETOF(PDB70ROOT, aStreams[pRoot->cStreams]);
        free(pRoot);
        pRoot = Pdb70AllocAndRead(pHdr, cb, piPageMap);
        if (pRoot)
        {
            /* size += page tables. */
            unsigned iStream = pRoot->cStreams;
            while (iStream-- > 0)
                if (pRoot->aStreams[iStream].cbStream != ~(uint32_t)0)
                    cb += Pdb70Pages(pHdr, pRoot->aStreams[iStream].cbStream) * sizeof(PDB70PAGE);
            free(pRoot);
            pRoot = Pdb70AllocAndRead(pHdr, cb, piPageMap);
            if (pRoot)
            {
                /* validate? */
                return pRoot;
            }
        }
#else
        /* validate? */
        return pRoot;
#endif
    }
    return NULL;
}

static void *Pdb70AllocAndReadStream(PPDB70HDR pHdr, PPDB70ROOT pRoot, unsigned iStream, size_t *pcbStream)
{
    const size_t    cbStream = pRoot->aStreams[iStream].cbStream;
    PPDB70PAGE      paiPageMap;
    if (    iStream >= pRoot->cStreams
        ||  cbStream == ~(uint32_t)0)
    {
        fprintf(stderr, "%s: error: Invalid stream %d\n", iStream);
        return NULL;
    }

    paiPageMap = (PPDB70PAGE)&pRoot->aStreams[pRoot->cStreams];
    while (iStream-- > 0)
        if (pRoot->aStreams[iStream].cbStream != ~(uint32_t)0)
            paiPageMap += Pdb70Pages(pHdr, pRoot->aStreams[iStream].cbStream);

    if (pcbStream)
        *pcbStream = cbStream;
    return Pdb70AllocAndRead(pHdr, cbStream, paiPageMap);
}

static int Pdb70Process(uint8_t *pbFile, size_t cbFile)
{
    PPDB70HDR   pHdr = (PPDB70HDR)pbFile;
    PPDB70ROOT  pRoot;
    PPDB70NAMES pNames;
    size_t      cbStream;
    unsigned    fDone = 0;
    unsigned    iStream;
    int         rc = 0;
    dprintf(("pdb70\n"));

    /*
     * Validate the header and read the root stream.
     */
    if (Pdb70ValidateHeader(pHdr, cbFile))
        return 1;
    pRoot = Pdb70AllocAndReadRoot(pHdr);
    if (!pRoot)
        return 1;

    /*
     * The names we want are usually all found in the 'Names' stream, that is #1.
     */
    dprintf(("Reading the names stream....\n"));
    pNames = Pdb70AllocAndReadStream(pHdr, pRoot, 1, &cbStream);
    if (pNames)
    {
        dprintf(("Names: Version=%u cbNames=%u (%#x)\n", pNames->Version, pNames->cbNames, pNames->cbNames));
        if (    pNames->Version == PDB70NAMES_VERSION
            &&  pNames->cbNames > 32
            &&  pNames->cbNames + offsetof(PDB70NAMES, szzNames) <= pRoot->aStreams[1].cbStream)
        {
            /*
             * Iterate the names and add the /mr/inversedeps/ ones to the dependency list.
             */
            const char *psz = &pNames->szzNames[0];
            size_t cb = pNames->cbNames;
            size_t off = 0;
            dprintf(("0x0000 #0: %6d bytes  [root / toc]\n", pRoot->aStreams[0].cbStream));
            for (iStream = 1; cb > 0; iStream++)
            {
                int fAdded = 0;
                size_t cch = strlen(psz);
                if (   cch >= sizeof("/mr/inversedeps/")
                    && !memcmp(psz, "/mr/inversedeps/", sizeof("/mr/inversedeps/") - 1))
                {
                    depAdd(psz + sizeof("/mr/inversedeps/") - 1, cch - (sizeof("/mr/inversedeps/") - 1));
                    fAdded = 1;
                }
                dprintf(("%#06x #%d: %6d bytes  %s%s\n", off, iStream,
                         iStream < pRoot->cStreams ? pRoot->aStreams[iStream].cbStream : -1,
                         psz, fAdded ? "  [dep]" : ""));
                (void)fAdded;

                /* next */
                if (cch >= cb)
                {
                    dprintf(("warning! cch=%d cb=%d\n", cch, cb));
                    cch = cb - 1;
                }
                cb  -= cch + 1;
                psz += cch + 1;
                off += cch + 1;
            }
            rc = 0;
            fDone = 1;
        }
        else
            dprintf(("Unknown version or bad size: Version=%u cbNames=%d cbStream=%d\n",
                     pNames->Version, pNames->cbNames, cbStream));
        free(pNames);
    }

    if (!fDone)
    {
        /*
         * Iterate the streams in the root and scan their content for
         * dependencies.
         */
        rc = 0;
        for (iStream = 0; iStream < pRoot->cStreams && !rc; iStream++)
        {
            uint8_t *pbStream;
            if (    pRoot->aStreams[iStream].cbStream == ~(uint32_t)0
                ||  !pRoot->aStreams[iStream].cbStream)
                continue;
            dprintf(("Stream #%d: %#x bytes (%#x aligned)\n", iStream, pRoot->aStreams[iStream].cbStream,
                     Pdb70Align(pHdr, pRoot->aStreams[iStream].cbStream)));
            pbStream = (uint8_t *)Pdb70AllocAndReadStream(pHdr, pRoot, iStream, &cbStream);
            if (pbStream)
            {
                rc = ScanStream(pbStream, cbStream, "/mr/inversedeps/", sizeof("/mr/inversedeps/") - 1);
                free(pbStream);
            }
            else
                rc = 1;
        }
    }

    free(pRoot);
    return rc;
}



///////////////////////////////////////////////////////////////////////////////
//
//
//  P D B   2 . 0
//
//
///////////////////////////////////////////////////////////////////////////////


/** A PDB 2.0 Page number. */
typedef uint16_t PDB20PAGE;
/** Pointer to a PDB 2.0 Page number. */
typedef PDB20PAGE *PPDB20PAGE;

/**
 * A PDB 2.0 stream.
 */
typedef struct PDB20STREAM
{
    /** The size of the stream. */
    uint32_t    cbStream;
    /** Some unknown value. */
    uint32_t    u32Unknown;
} PDB20STREAM, *PPDB20STREAM;

/** The PDB 2.00 signature. */
#define PDB_SIGNATURE_200 "Microsoft C/C++ program database 2.00\r\n\x1A" "JG\0"
/**
 * The PDB 2.0 header.
 */
typedef struct PDB20HDR
{
    /** The signature string. */
    uint8_t     szSignature[sizeof(PDB_SIGNATURE_200)];
    /** The page size. */
    uint32_t    cbPage;
    /** The start page - whatever that is... */
    PDB20PAGE   iStartPage;
    /** The number of pages in the file. */
    PDB20PAGE   cPages;
    /** The root stream directory. */
    PDB20STREAM RootStream;
    /** The root page table. */
    PDB20PAGE   aiRootPageMap[1];
} PDB20HDR, *PPDB20HDR;

/**
 * The PDB 2.0 root directory.
 */
typedef struct PDB20ROOT
{
    /** The number of streams */
    uint16_t    cStreams;
    /** Reserved or high part of cStreams. */
    uint16_t    u16Reserved;
    /** Array of streams. */
    PDB20STREAM aStreams[1];
} PDB20ROOT, *PPDB20ROOT;


static int Pdb20ValidateHeader(PPDB20HDR pHdr, size_t cbFile)
{
    if (pHdr->cbPage * pHdr->cPages != cbFile)
    {
        fprintf(stderr, "%s: error: Bad PDB 2.0 header - cbPage * cPages != cbFile.\n", argv0);
        return 1;
    }
    if (pHdr->iStartPage >= pHdr->cPages && pHdr->iStartPage <= 0)
    {
        fprintf(stderr, "%s: error: Bad PDB 2.0 header - cbPage * cPages != cbFile.\n", argv0);
        return 1;
    }
    return 0;
}

static size_t Pdb20Pages(PPDB20HDR pHdr, size_t cb)
{
    if (cb == ~(uint32_t)0 || !cb)
        return 0;
    return (cb + pHdr->cbPage - 1) / pHdr->cbPage;
}

static void *Pdb20AllocAndRead(PPDB20HDR pHdr, size_t cb, PPDB20PAGE paiPageMap)
{
    size_t cPages = Pdb20Pages(pHdr, cb);
    uint8_t *pbBuf = malloc(cPages * pHdr->cbPage + 1);
    if (pbBuf)
    {
        size_t iPage = 0;
        while (iPage < cPages)
        {
            size_t off = paiPageMap[iPage];
            off *= pHdr->cbPage;
            memcpy(pbBuf + iPage * pHdr->cbPage, (uint8_t *)pHdr + off, pHdr->cbPage);
            iPage++;
        }
        pbBuf[cPages * pHdr->cbPage] = '\0';
    }
    else
        fprintf(stderr, "%s: error: failed to allocate %d bytes\n", argv0, cPages * pHdr->cbPage + 1);
    return pbBuf;
}

static PPDB20ROOT Pdb20AllocAndReadRoot(PPDB20HDR pHdr)
{
    /*
     * The tricky bit here is to find the right length.
     * (Todo: Check if we can just use the stream size..)
     */
    PPDB20ROOT pRoot = Pdb20AllocAndRead(pHdr, sizeof(*pRoot), &pHdr->aiRootPageMap[0]);
    if (pRoot)
    {
        /* size = stream header + array of stream. */
        size_t cb = OFFSETOF(PDB20ROOT, aStreams[pRoot->cStreams]);
        free(pRoot);
        pRoot = Pdb20AllocAndRead(pHdr, cb, &pHdr->aiRootPageMap[0]);
        if (pRoot)
        {
            /* size += page tables. */
            unsigned iStream = pRoot->cStreams;
            while (iStream-- > 0)
                if (pRoot->aStreams[iStream].cbStream != ~(uint32_t)0)
                    cb += Pdb20Pages(pHdr, pRoot->aStreams[iStream].cbStream) * sizeof(PDB20PAGE);
            free(pRoot);
            pRoot = Pdb20AllocAndRead(pHdr, cb, &pHdr->aiRootPageMap[0]);
            if (pRoot)
            {
                /* validate? */
                return pRoot;
            }
        }
    }
    return NULL;

}

static void *Pdb20AllocAndReadStream(PPDB20HDR pHdr, PPDB20ROOT pRoot, unsigned iStream, size_t *pcbStream)
{
    size_t      cbStream = pRoot->aStreams[iStream].cbStream;
    PPDB20PAGE  paiPageMap;
    if (    iStream >= pRoot->cStreams
        ||  cbStream == ~(uint32_t)0)
    {
        fprintf(stderr, "%s: error: Invalid stream %d\n", iStream);
        return NULL;
    }

    paiPageMap = (PPDB20PAGE)&pRoot->aStreams[pRoot->cStreams];
    while (iStream-- > 0)
        if (pRoot->aStreams[iStream].cbStream != ~(uint32_t)0)
            paiPageMap += Pdb20Pages(pHdr, pRoot->aStreams[iStream].cbStream);

    if (pcbStream)
        *pcbStream = cbStream;
    return Pdb20AllocAndRead(pHdr, cbStream, paiPageMap);
}

static int Pdb20Process(uint8_t *pbFile, size_t cbFile)
{
    PPDB20HDR   pHdr = (PPDB20HDR)pbFile;
    PPDB20ROOT  pRoot;
    unsigned    iStream;
    int         rc = 0;

    /*
     * Validate the header and read the root stream.
     */
    if (Pdb20ValidateHeader(pHdr, cbFile))
        return 1;
    pRoot = Pdb20AllocAndReadRoot(pHdr);
    if (!pRoot)
        return 1;

    /*
     * Iterate the streams in the root and scan their content for
     * dependencies.
     */
    rc = 0;
    for (iStream = 0; iStream < pRoot->cStreams && !rc; iStream++)
    {
        uint8_t *pbStream;
        if (pRoot->aStreams[iStream].cbStream == ~(uint32_t)0)
            continue;
        pbStream = (uint8_t *)Pdb20AllocAndReadStream(pHdr, pRoot, iStream, NULL);
        if (pbStream)
        {
            rc = ScanStream(pbStream, pRoot->aStreams[iStream].cbStream, "/ipm/header/", sizeof("/ipm/header/") - 1);
            free(pbStream);
        }
        else
            rc = 1;
    }

    free(pRoot);
    return rc;
}


/**
 * Make an attempt at parsing a Visual C++ IDB file.
 */
static int ProcessIDB(FILE *pInput)
{
    size_t  cbFile;
    char   *pbFile;
    char   *pbHdr = PDB_SIGNATURE_200;
    int     rc = 0;

    /*
     * Read the file into memory.
     */
    pbFile = (char *)ReadFileIntoMemory(pInput, &cbFile);
    if (!pbFile)
        return 1;

    /*
     * Figure out which parser to use.
     */
    if (!memcmp(pbFile, PDB_SIGNATURE_700, sizeof(PDB_SIGNATURE_700)))
        rc = Pdb70Process(pbFile, cbFile);
    else if (!memcmp(pbFile, PDB_SIGNATURE_200, sizeof(PDB_SIGNATURE_200)))
        rc = Pdb20Process(pbFile, cbFile);
    else
    {
        fprintf(stderr, "%s: error: Doesn't recognize the header of the Visual C++ IDB file.\n", argv0);
        rc = 1;
    }

    free(pbFile);
    return rc;
}


static void usage(const char *argv0)
{
    printf("syntax: %s -o <output> -t <target> [-f] [-s] <vc idb-file>\n", argv0);
}


int kmk_builtin_kDepIDB(int argc, char *argv[])
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
            switch (argv[i][1])
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
                 * Generate stubs.
                 */
                case 's':
                {
                    fStubs = 1;
                    break;
                }

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
    i = ProcessIDB(pInput);

    /*
     * Write the dependecy file.
     */
    if (!i)
    {
        depOptimize(fFixCase);
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

    return i;
}

