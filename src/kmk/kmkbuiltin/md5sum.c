/* $Id: md5sum.c 1101 2007-09-22 22:01:37Z knut.osmundsen@oracle.com $ */
/** @file
 * md5sum.
 */

/*
 * Copyright (c) 2007 knut st. osmundsen <bird-src-spam@anduin.net>
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#include <string.h>
#include <stdio.h>
#include <errno.h>
#include "err.h"
#include "kmkbuiltin.h"
#include "../../lib/md5.h"



/**
 * Prints the usage and return 1.
 */
static int usage(void)
{
    fprintf(stderr,
            "usage: md5sum [-bt] file [string ...]\n"
            "   or: md5sum [-cbtwq] file\n");
    return 1;
}


/**
 * Prints the version string.
 * @returns 0
 */
static int version(void)
{
#ifdef kmk_builtin_md5sum
    fprintf(stdout, "kmk_md5sum (kBuild) %d.%d.%d\n"
                    "Copyright (c) 2007 knut st. osmundsen\n",
            KBUILD_VERSION_MAJOR, KBUILD_VERSION_MINOR, KBUILD_VERSION_PATCH);
#else
    fprintf(stdout, "kmk_builtin_md5sum (kBuild) %d.%d.%d\n"
                    "Copyright (c) 2007 knut st. osmundsen\n",
            KBUILD_VERSION_MAJOR, KBUILD_VERSION_MINOR, KBUILD_VERSION_PATCH);
#endif
    return 0;
}


/**
 * Makes a string out of the given digest.
 *
 * @param   pDigest     The MD5 digest.
 * @param   pszDigest   Where to put the digest string. Must be able to
 *                      hold at least 33 bytes.
 */
static void digest_to_string(unsigned char pDigest[16], char *pszDigest)
{
    unsigned i;
    for (i = 0; i < 16; i++)
    {
        static char s_achDigits[17] = "0123456789abcdef";
        pszDigest[i*2]     = s_achDigits[(pDigest[i] >> 4)];
        pszDigest[i*2 + 1] = s_achDigits[(pDigest[i] & 0xf)];
    }
    pszDigest[i*2] = '\0';
}


/**
 * Attempts to convert a string to a MD5 digest.
 *
 * @returns 0 on success, 1-based position of the failure first error.
 * @param   pszDigest   The string to interpret.
 * @param   pDigest     Where to put the MD5 digest.
 */
static int string_to_digest(const char *pszDigest, unsigned char pDigest[16])
{
    unsigned i;
    unsigned iBase = 1;

    /* skip blanks */
    while (     *pszDigest == ' '
           ||   *pszDigest == '\t'
           ||   *pszDigest == '\n'
           ||   *pszDigest == '\r')
        pszDigest++, iBase++;

    /* convert the digits. */
    memset(pDigest, 0, 16);
    for (i = 0; i < 32; i++, pszDigest++)
    {
        int iDigit;
        if (*pszDigest >= '0' && *pszDigest <= '9')
            iDigit = *pszDigest - '0';
        else if (*pszDigest >= 'a' && *pszDigest <= 'f')
            iDigit = *pszDigest - 'a' + 10;
        else if (*pszDigest >= 'A' && *pszDigest <= 'F')
            iDigit = *pszDigest - 'A' + 10;
        else
            return i + iBase;
        if (i & 1)
            pDigest[i >> 1] |= iDigit;
        else
            pDigest[i >> 1] |= iDigit << 4;
    }

    /* the rest of the string must now be blanks. */
    while (     *pszDigest == ' '
           ||   *pszDigest == '\t'
           ||   *pszDigest == '\n'
           ||   *pszDigest == '\r')
        pszDigest++, i++;

    return *pszDigest ? i + iBase : 0;
}


/**
 * Calculates the md5sum of the sepecified file stream.
 *
 * @returns errno on failure, 0 on success.
 * @param   pFile       The file stream.
 * @param   pDigest     Where to store the MD5 digest.
 */
static int calc_md5sum(FILE *pFile, unsigned char pDigest[16])
{
    int rc  = 0;
    int cb;
    char abBuf[16384];
    struct MD5Context Ctx;

    MD5Init(&Ctx);
    for (;;)
    {
        errno = 0;
        cb = (int)fread(abBuf, 1, sizeof(abBuf), pFile);
        if (cb > 0)
            MD5Update(&Ctx, abBuf, cb);
        else if (cb == 0)
            break;
        else
        {
            rc = errno;
            if (!rc)
                rc = EINVAL;
            break;
        }
    }
    MD5Final(pDigest, &Ctx);

    return rc;
}


/**
 * Checks the if the specified digest matches the digest of the file stream.
 *
 * @returns 0 on match, -1 on mismatch, errno value (positive) on failure.
 * @param   pFile   The file stream.
 * @param   Digest  The MD5 digest.
 */
static int check_md5sum(FILE *pFile, unsigned char Digest[16])
{
    unsigned char DigestFile[16];
    int rc;

    rc = calc_md5sum(pFile, DigestFile);
    if (!rc)
        rc = memcmp(Digest, DigestFile, 16) ? -1 : 0;
    return rc;
}


/**
 * Opens the specified file for md5 sum calculation.
 *
 * @returns File stream on success, NULL and errno on failure.
 * @param   pszFilename     The filename.
 * @param   fText           Whether text or binary mode should be used.
 */
static FILE *open_file(const char *pszFilename, unsigned fText)
{
    FILE *pFile;

    errno = 0;
    pFile = fopen(pszFilename, fText ? "r" : "rb");
    if (!pFile && errno == EINVAL && !fText)
        pFile = fopen(pszFilename, "r");
    return pFile;
}


/**
 * md5sum, calculates and checks the md5sum of files.
 * Somewhat similar to the GNU coreutil md5sum command.
 */
int kmk_builtin_md5sum(int argc, char **argv, char **envp)
{
    int i;
    int rc = 0;
    int fText = 0;
    int fBinaryTextOpt = 0;
    int fQuiet = 0;
    int fNewLine = 0;
    int fChecking = 0;
    int fNoMoreOptions = 0;
    unsigned char Digest[16];
    const char *pszFilename;
    const char *pszDigest;
    char szDigest[36];
    FILE *pFile;
    int rc2;

    g_progname = argv[0];

    /*
     * Print usage if no arguments.
     */
    if (argc <= 1)
        return usage();

    /*
     * Process the arguments, FIFO style.
     */
    i = 1;
    while (i < argc)
    {
        char *psz = argv[i];
        if (!fNoMoreOptions && psz[0] == '-' && psz[1] == '-' && !psz[2])
            fNoMoreOptions = 1;
        else if (*psz == '-' && !fNoMoreOptions)
        {
            psz++;
            /* convert long options for gnu just for fun */
            if (*psz == '-')
            {
                if (!strcmp(psz, "-binary"))
                    psz = "b";
                else if (!strcmp(psz, "-text"))
                    psz = "t";
                else if (!strcmp(psz, "-check"))
                    psz = "c";
                else if (!strcmp(psz, "-check-this"))
                    psz = "C";
                else if (!strcmp(psz, "-status"))
                    psz = "q";
                else if (!strcmp(psz, "-warn"))
                    psz = "w";
                else if (!strcmp(psz, "-help"))
                    psz = "h";
                else if (!strcmp(psz, "-version"))
                    psz = "v";
            }

            /* short options */
            do
            {
                switch (*psz)
                {
                    case 'c':
                        fChecking = 1;
                        break;

                    case 'b':
                        fText = 0;
                        fBinaryTextOpt = 1;
                        break;

                    case 't':
                        fText = 1;
                        fBinaryTextOpt = 1;
                        break;

                    case 'q':
                        fQuiet = 1;
                        break;

                    case 'w':
                        /* ignored */
                        break;

                    case 'v':
                        return version();

                    /*
                     * -C md5 file
                     */
                    case 'C':
                    {
                        if (psz[1])
                            pszDigest = &psz[1];
                        else if (i + 1 < argc)
                            pszDigest = argv[++i];
                        else
                        {
                            errx(1, "'-C' is missing the MD5 sum!");
                            return 1;
                        }
                        rc2 = string_to_digest(pszDigest, Digest);
                        if (rc2)
                        {
                            errx(1, "Malformed MD5 digest '%s'!", pszDigest);
                            errx(1, "                      %*s^", rc2 - 1, "");
                            return 1;
                        }

                        if (i + 1 < argc)
                            pszFilename = argv[++i];
                        else
                        {
                            errx(1, "'-C' is missing the filename!");
                            return 1;
                        }
                        pFile = open_file(pszFilename, fText);
                        if (pFile)
                        {
                            rc2 = check_md5sum(pFile, Digest);
                            if (!fQuiet)
                            {
                                if (rc2 <= 0)
                                {
                                    fprintf(stdout, "%s: %s\n", pszFilename, !rc2 ? "OK" : "FAILURE");
                                    fflush(stdout);
                                }
                                else
                                    errx(1, "Error reading '%s': %s", pszFilename, strerror(rc));
                            }
                            if (rc2)
                                rc = 1;
                            fclose(pFile);
                        }
                        else
                        {
                            if (!fQuiet)
                                errx(1, "Failed to open '%s': %s", pszFilename, strerror(errno));
                            rc = 1;
                        }
                        psz = "\0";
                        break;
                    }

                    default:
                        errx(1, "Invalid option '%c'! (%s)", *psz, argv[i]);
                        return usage();
                }
            } while (*++psz);
        }
        else if (fChecking)
        {
            pFile = fopen(argv[i], "r");
            if (pFile)
            {
                int iLine = 0;
                char szLine[8192];
                while (fgets(szLine, sizeof(szLine), pFile))
                {
                    int fLineText;
                    char *psz = szLine;
                    iLine++;

                    /* leading blanks */
                    while (*psz == ' ' || *psz == '\t' || *psz == '\n')
                        psz++;

                    /* skip blank or comment lines. */
                    if (!*psz || *psz == '#' || *psz == ';' || *psz == '/')
                        continue;

                    /* remove the trailing newline. */
                    rc2 = (int)strlen(psz);
                    if (psz[rc2 - 1] == '\n')
                        psz[rc2 - 1] = '\0';

                    /* skip to the end of the digest and terminate it. */
                    pszDigest = psz;
                    while (*psz != ' ' && *psz != '\t' && *psz)
                        psz++;
                    if (*psz)
                    {
                        *psz++ = '\0';

                        /* blanks */
                        while (*psz == ' ' || *psz == '\t' || *psz == '\n')
                            psz++;

                        /* check for binary asterix */
                        if (*psz != '*')
                            fLineText = fBinaryTextOpt ? fText : 0;
                        else
                        {
                            fLineText = 0;
                            psz++;
                        }
                        if (*psz)
                        {
                            /* the rest is filename. */
                            pszFilename = psz;

                            /*
                             * Do the job.
                             */
                            rc2 = string_to_digest(pszDigest, Digest);
                            if (!rc2)
                            {
                                FILE *pFile2 = open_file(pszFilename, fLineText);
                                if (pFile2)
                                {
                                    rc2 = check_md5sum(pFile2, Digest);
                                    if (!fQuiet)
                                    {
                                        if (rc2 <= 0)
                                        {
                                            fprintf(stdout, "%s: %s\n", pszFilename, !rc2 ? "OK" : "FAILURE");
                                            fflush(stdout);
                                        }
                                        else
                                            errx(1, "Error reading '%s': %s", pszFilename, strerror(rc));
                                    }
                                    if (rc2)
                                        rc = 1;
                                    fclose(pFile2);
                                }
                                else
                                {
                                    if (!fQuiet)
                                        errx(1, "Failed to open '%s': %s", pszFilename, strerror(errno));
                                    rc = 1;
                                }
                            }
                            else if (!fQuiet)
                            {
                                errx(1, "%s (%d): Ignoring malformed digest '%s' (digest)", argv[i], iLine, pszDigest);
                                errx(1, "%s (%d):                            %*s^", argv[i], iLine, rc2 - 1, "");
                            }
                        }
                        else if (!fQuiet)
                            errx(1, "%s (%d): Ignoring malformed line!", argv[i], iLine);
                    }
                    else if (!fQuiet)
                        errx(1, "%s (%d): Ignoring malformed line!", argv[i], iLine);
                }

                fclose(pFile);
            }
            else
            {
                errx(1, "Failed to open '%s': %s", argv[i], strerror(errno));
                rc = 1;
            }
        }
        else
        {
            /*
             * Calcuate and print the MD5 sum for one file.
             */
            pFile = open_file(argv[i], fText);
            if (pFile)
            {
                rc2 = calc_md5sum(pFile, Digest);
                if (!rc2)
                {
                    digest_to_string(Digest, szDigest);
                    fprintf(stdout, "%s %s%s\n", szDigest, fText ? "" : "*", argv[i]);
                    fflush(stdout);
                }
                else
                {
                    if (!fQuiet)
                        errx(1, "Failed to open '%s': %s", argv[i], strerror(rc));
                    rc = 1;
                }
                fclose(pFile);
            }
            else
            {
                if (!fQuiet)
                    errx(1, "Failed to open '%s': %s", argv[i], strerror(errno));
                rc = 1;
            }
        }
        i++;
    }

    return rc;
}


