/* $Id: kDepObj.c 2263 2009-01-23 00:22:47Z knut.osmundsen@oracle.com $ */
/** @file
 * kDepObj - Extract dependency information from an object file.
 */

/*
 * Copyright (c) 2007-2009 knut st. osmundsen <bird-kBuild-spamix@anduin.net>
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
#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#ifdef HAVE_ALLOCA_H
# include <alloca.h>
#endif
#if !defined(_MSC_VER)
# include <unistd.h>
#else
# include <io.h>
#endif
#include "../../lib/k/kDefs.h"
#include "../../lib/k/kTypes.h"
#include "../../lib/kDep.h"
#include "kmkbuiltin.h"


/*******************************************************************************
*   Defined Constants And Macros                                               *
*******************************************************************************/
/*#define DEBUG*/
#define DEBUG
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
#define KDEPOMF_COMMENT         0x88
#define KDEPOMF_CMTCLS_DEPENDENCY  0xe9
/** @} */


/*******************************************************************************
*   Structures and Typedefs                                                    *
*******************************************************************************/
/** @name OMF structure
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


/*******************************************************************************
*   Global Variables                                                           *
*******************************************************************************/
/** the executable name. */
static const char *argv0 = "";


/**
 * Parses the OMF file.
 *
 * @returns 0 on success, 1 on failure.
 * @param   pbFile      The start of the file.
 * @param   cbFile      The file size.
 */
int kDepObjOMFParse(const KU8 *pbFile, KSIZE cbFile)
{
    PCKDEPOMFHDR    pHdr = (PCKDEPOMFHDR)pbFile;
    KSIZE           cbLeft = cbFile;


    /*
     * Iterate thru the records until we hit the end or an invalid one.
     */
    while (   cbLeft >= sizeof(*pHdr)
           && cbLeft >= pHdr->cbRec + sizeof(*pHdr))
    {
        /* process selected record types. */
        dprintf(("%#07x: %#04x %#05x\n", (const KU8*)pHdr - pbFile, pHdr->bType, pHdr->cbRec));
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
                {
                    fprintf(stderr, "%s: error: %#07x - Bad %cHEADR record, length mismatch.\n",
                            argv0, (const KU8*)pHdr - pbFile, pHdr->bType == KDEPOMF_THEADR ? 'T' : 'L');
                    return 1;
                }
                depAdd(pTHeadr->Name.ach, pTHeadr->Name.cch);
                break;
            }

            case KDEPOMF_COMMENT:
                if (pHdr->cbRec < 2)
                {
                    fprintf(stderr, "%s: error: %#07x - Bad COMMENT record, too small.\n",
                            argv0, (const KU8*)pHdr - pbFile);
                    return 1;
                }
                if (((const KU8 *)(pHdr+1))[0] & 0x3f)
                {
                    fprintf(stderr, "%s: error: %#07x - Bad COMMENT record, reserved flags set.\n",
                            argv0, (const KU8*)pHdr - pbFile);
                    return 1;
                }
                switch (((const KU8 *)(pHdr+1))[1])
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

                            fprintf(stderr, "%s: error: %#07x - Bad DEPENDENCY FILE record, length mismatch. (%d/%d)\n",
                                    argv0, (const KU8*)pHdr - pbFile,
                                    K_OFFSETOF(KDEPOMFDEPFILE, Name.ach[pDep->Name.cch]) + 1,
                                    pHdr->cbRec + sizeof(*pHdr));
                            return 1;

                        }
                        depAdd(pDep->Name.ach, pDep->Name.cch);
                        break;
                    }

                    /** @todo Check for class A1 and pick up the debug info type (HL/CV(/DX)). */
                }
                break;
            /** @todo add support for HLL line number segments and stuff. */
        }

        /* advance */
        cbLeft -= pHdr->cbRec + sizeof(*pHdr);
        pHdr = (PCKDEPOMFHDR)((const KU8 *)(pHdr + 1) + pHdr->cbRec);
    }

    if (cbLeft)
    {
        fprintf(stderr, "%s: error: %#07x - Unexpected EOF. cbLeft=%#x\n",
                argv0, (const KU8*)pHdr - pbFile, cbLeft);
        return 1;
    }
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

    if (cbFile < sizeof(*pHdr))
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

