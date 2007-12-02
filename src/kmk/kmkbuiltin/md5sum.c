/* $Id: md5sum.c 1329 2007-12-02 20:49:33Z knut.osmundsen@oracle.com $ */
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
#include <fcntl.h>
#ifdef _MSC_VER
# include <io.h>
#else
# include <unistd.h>
#endif
#include <sys/stat.h>
#include "err.h"
#include "kmkbuiltin.h"
#include "../../lib/md5.h"

/*#define MD5SUM_USE_STDIO*/


/**
 * Prints the usage and return 1.
 */
static int usage(FILE *pOut)
{
    fprintf(pOut,
            "usage: md5sum [-bt] file(s)\n"
            "   or: md5sum [-btwq] -c list-file(s)\n"
            "   or: md5sum [-btq] -C MD5 file\n"
            "\n"
            " -c, --check       Check MD5 and files found in the specified list file(s).\n"
            "                   The default is to compute MD5 sums of the specified files\n"
            "                   and print them to stdout in list form.\n"
            " -C, --check-file  This is followed by an MD5 sum and the file to check.\n"
            " -b, --binary      Read files in binary mode. (default)\n"
            " -t, --text        Read files in text mode.\n"
            " -p, --progress    Show progress indicator on large files.\n"
            " -q, --status      Be quiet.\n"
            " -w, --warn        Ignored. Always warn, unless quiet.\n"
            " -h, --help        This usage info.\n"
            " -v, --version     Show version information and exit.\n"
            );
    return 1;
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
 * Opens the specified file for md5 sum calculation.
 *
 * @returns Opaque pointer on success, NULL and errno on failure.
 * @param   pszFilename     The filename.
 * @param   fText           Whether text or binary mode should be used.
 */
static void *open_file(const char *pszFilename, unsigned fText)
{
#if defined(MD5SUM_USE_STDIO)
    FILE *pFile;

    errno = 0;
    pFile = fopen(pszFilename, fText ? "r" : "rb");
    if (!pFile && errno == EINVAL && !fText)
        pFile = fopen(pszFilename, "r");
    return pFile;

#else
    int fd;
    int fFlags;

    /* figure out the appropriate flags. */
    fFlags = O_RDONLY;
#ifdef O_SEQUENTIAL
    fFlags |= _O_SEQUENTIAL;
#elif defined(_O_SEQUENTIAL)
    fFlags |= _O_SEQUENTIAL;
#endif
#ifdef _O_BINARY
    if (!fText)     fFlags |= _O_BINARY;
#elif defined(_O_BINARY)
    if (!fText)     fFlags |= _O_BINARY;
#endif
#ifdef O_TEXT
    if (fText)      fFlags |= O_TEXT;
#elif defined(O_TEXT)
    if (fText)      fFlags |= _O_TEXT;
#endif

    errno = 0;
    fd = open(pszFilename, fFlags, 0755);
    if (fd >= 0)
    {
        int *pFd = malloc(sizeof(*pFd));
        if (pFd)
        {
            *pFd = fd;
            return pFd;
        }
        close(fd);
        errno = ENOMEM;
    }

    return NULL;
#endif
}


/**
 * Closes a file opened by open_file.
 *
 * @param   pvFile          The opaque pointer returned by open_file.
 */
static void close_file(void *pvFile)
{
#if defined(MD5SUM_USE_STDIO)
    fclose((FILE *)pvFile);
#else
    close(*(int *)pvFile);
    free(pvFile);
#endif
}


/**
 * Reads from a file opened by open_file.
 *
 * @returns Number of bytes read on success.
 *          0 on EOF.
 *          Negated errno on read error.
 * @param   pvFile          The opaque pointer returned by open_file.
 * @param   pvBuf           Where to put the number of read bytes.
 * @param   cbBuf           The max number of bytes to read.
 *                          Must be less than a INT_MAX.
 */
static int read_file(void *pvFile, void *pvBuf, size_t cbBuf)
{
#if defined(MD5SUM_USE_STDIO)
    int cb;

    errno = 0;
    cb = (int)fread(pvBuf, 1, cbBuf, (FILE *)pvFile);
    if (cb >= 0)
        return (int)cb;
    if (!errno)
        return -EINVAL;
    return -errno;
#else
    int cb;

    errno = 0;
    cb = (int)read(*(int *)pvFile, pvBuf, (int)cbBuf);
    if (cb >= 0)
        return (int)cb;
    if (!errno)
        return -EINVAL;
    return -errno;
#endif
}


/**
 * Gets the size of the file.
 * This is informational and not necessarily 100% accurate.
 *
 * @returns File size.
 * @param   pvFile          The opaque pointer returned by open_file
 */
static double size_file(void *pvFile)
{
#if defined(_MSC_VER)
    __int64 cb;
# if defined(MD5SUM_USE_STDIO)
    cb = _filelengthi64(fileno((FILE *)pvFile));
# else
    cb = _filelengthi64(*(int *)pvFile);
# endif
    if (cb >= 0)
        return (double)cb;

#elif defined(MD5SUM_USE_STDIO)
    struct stat st;
    if (!fstat(fileno((FILE *)pvFile), &st))
        return st.st_size;

#else
    struct stat st;
    if (!fstat(*(int *)pvFile, &st))
        return st.st_size;
#endif
    return 1024;
}


/**
 * Calculates the md5sum of the sepecified file stream.
 *
 * @returns errno on failure, 0 on success.
 * @param   pvFile      The file stream.
 * @param   pDigest     Where to store the MD5 digest.
 * @param   fProgress   Whether to show a progress bar.
 */
static int calc_md5sum(void *pvFile, unsigned char pDigest[16], unsigned fProgress)
{
    int cb;
    int rc = 0;
    char abBuf[32*1024];
    struct MD5Context Ctx;
    unsigned uPercent = 0;
    double cbFile = 0.0;
    double off = 0.0;

    if (fProgress)
    {
        cbFile = size_file(pvFile);
        if (cbFile < 1024*1024)
            fProgress = 0;
    }

    MD5Init(&Ctx);
    for (;;)
    {
        /* process a chunk. */
        cb = read_file(pvFile, abBuf, sizeof(abBuf));
        if (cb > 0)
            MD5Update(&Ctx, (unsigned char *)&abBuf[0], cb);
        else if (!cb)
            break;
        else
        {
            rc = -cb;
            break;
        }

        /* update the progress indicator. */
        if (fProgress)
        {
            unsigned uNewPercent;
            off += cb;
            uNewPercent = (unsigned)((off / cbFile) * 100);
            if (uNewPercent != uPercent)
            {
                if (uPercent)
                    printf("\b\b\b\b");
                printf("%3d%%", uNewPercent);
                fflush(stdout);
                uPercent = uNewPercent;
            }
        }
    }
    MD5Final(pDigest, &Ctx);

    if (fProgress)
        printf("\b\b\b\b    \b\b\b\b");

    return rc;
}


/**
 * Checks the if the specified digest matches the digest of the file stream.
 *
 * @returns 0 on match, -1 on mismatch, errno value (positive) on failure.
 * @param   pvFile      The file stream.
 * @param   Digest      The MD5 digest.
 * @param   fProgress   Whether to show an progress indicator on large files.
 */
static int check_md5sum(void *pvFile, unsigned char Digest[16], unsigned fProgress)
{
    unsigned char DigestFile[16];
    int rc;

    rc = calc_md5sum(pvFile, DigestFile, fProgress);
    if (!rc)
        rc = memcmp(Digest, DigestFile, 16) ? -1 : 0;
    return rc;
}


/**
 * Checks if the specified file matches the given MD5 digest.
 *
 * @returns 0 if it matches, 1 if it doesn't or an error occurs.
 * @param   pszFilename The name of the file to check.
 * @param   pszDigest   The MD5 digest string.
 * @param   fText       Whether to open the file in text or binary mode.
 * @param   fQuiet      Whether to go about this in a quiet fashion or not.
 * @param   fProgress   Whether to show an progress indicator on large files.
 */
static int check_one_file(const char *pszFilename, const char *pszDigest, unsigned fText, unsigned fQuiet, unsigned fProgress)
{
    unsigned char Digest[16];
    int rc;

    rc = string_to_digest(pszDigest, Digest);
    if (!rc)
    {
        void *pvFile;

        pvFile = open_file(pszFilename, fText);
        if (pvFile)
        {
            if (!fQuiet)
                fprintf(stdout, "%s: ", pszFilename);
            rc = check_md5sum(pvFile, Digest, fProgress);
            close_file(pvFile);
            if (!fQuiet)
            {
                fprintf(stdout, "%s\n", !rc ? "OK" : rc < 0 ? "FAILURE" : "ERROR");
                fflush(stdout);
                if (rc > 0)
                    errx(1, "Error reading '%s': %s", pszFilename, strerror(rc));
            }
            if (rc)
                rc = 1;
        }
        else
        {
            if (!fQuiet)
                errx(1, "Failed to open '%s': %s", pszFilename, strerror(errno));
            rc = 1;
        }
    }
    else
    {
        errx(1, "Malformed MD5 digest '%s'!", pszDigest);
        errx(1, "                      %*s^", rc - 1, "");
        rc = 1;
    }

    return rc;
}


/**
 * Checks the specified md5.lst file.
 *
 * @returns 0 if all checks out file, 1 if one or more fails or there are read errors.
 * @param   pszFilename     The name of the file.
 * @param   fText           The default mode, text or binary. Only used when fBinaryTextOpt is true.
 * @param   fBinaryTextOpt  Whether a -b or -t option was specified and should be used.
 * @param   fQuiet          Whether to be quiet.
 * @param   fProgress   Whether to show an progress indicator on large files.
 */
static int check_files(const char *pszFilename, int fText, int fBinaryTextOpt, int fQuiet, unsigned fProgress)
{
    int rc = 0;
    FILE *pFile;

    /*
     * Try open the md5.lst file and process it line by line.
     */
    pFile = fopen(pszFilename, "r");
    if (pFile)
    {
        int iLine = 0;
        char szLine[8192];
        while (fgets(szLine, sizeof(szLine), pFile))
        {
            const char *pszDigest;
            int fLineText;
            char *psz;
            int rc2;

            iLine++;
            psz = szLine;

            /* leading blanks */
            while (*psz == ' ' || *psz == '\t' || *psz == '\n')
                psz++;

            /* skip blank or comment lines. */
            if (!*psz || *psz == '#' || *psz == ';' || *psz == '/')
                continue;

            /* remove the trailing newline. */
            rc2 = (int)strlen(psz);
            if (psz[rc2 - 1] == '\n')
                psz[rc2 - (rc2 >= 2 && psz[rc2 - 2] == '\r' ? 2 : 1)] = '\0';

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
                    unsigned char Digest[16];

                    /* the rest is filename. */
                    pszFilename = psz;

                    /*
                     * Do the job.
                     */
                    rc2 = string_to_digest(pszDigest, Digest);
                    if (!rc2)
                    {
                        void *pvFile = open_file(pszFilename, fLineText);
                        if (pvFile)
                        {
                            if (!fQuiet)
                                fprintf(stdout, "%s: ", pszFilename);
                            rc2 = check_md5sum(pvFile, Digest, fProgress);
                            close_file(pvFile);
                            if (!fQuiet)
                            {
                                fprintf(stdout, "%s\n", !rc2 ? "OK" : rc2 < 0 ? "FAILURE" : "ERROR");
                                fflush(stdout);
                                if (rc2 > 0)
                                    errx(1, "Error reading '%s': %s", pszFilename, strerror(rc2));
                            }
                            if (rc2)
                                rc = 1;
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
                        errx(1, "%s (%d): Ignoring malformed digest '%s' (digest)", pszFilename, iLine, pszDigest);
                        errx(1, "%s (%d):                            %*s^", pszFilename, iLine, rc2 - 1, "");
                    }
                }
                else if (!fQuiet)
                    errx(1, "%s (%d): Ignoring malformed line!", pszFilename, iLine);
            }
            else if (!fQuiet)
                errx(1, "%s (%d): Ignoring malformed line!", pszFilename, iLine);
        } /* while more lines */

        fclose(pFile);
    }
    else
    {
        errx(1, "Failed to open '%s': %s", pszFilename, strerror(errno));
        rc = 1;
    }

    return rc;
}


/**
 * Calculates the MD5 sum for one file and prints it.
 *
 * @returns 0 on success, 1 on any kind of failure.
 * @param   pszFilename     The file to process.
 * @param   fText           The mode to open the file in.
 * @param   fQuiet          Whether to be quiet or verbose about errors.
 * @param   fProgress       Whether to show an progress indicator on large files.
 */
static int md5sum_file(const char *pszFilename, unsigned fText, unsigned fQuiet, unsigned fProgress)
{
    int rc;
    void *pvFile;

    /*
     * Calcuate and print the MD5 sum for one file.
     */
    pvFile = open_file(pszFilename, fText);
    if (pvFile)
    {
        unsigned char Digest[16];
        rc = calc_md5sum(pvFile, Digest, fProgress);
        close_file(pvFile);
        if (!rc)
        {
            char szDigest[36];
            digest_to_string(Digest, szDigest);
            fprintf(stdout, "%s %s%s\n", szDigest, fText ? "" : "*", pszFilename);
            fflush(stdout);
        }
        else
        {
            if (!fQuiet)
                errx(1, "Failed to open '%s': %s", pszFilename, strerror(rc));
            rc = 1;
        }
    }
    else
    {
        if (!fQuiet)
            errx(1, "Failed to open '%s': %s", pszFilename, strerror(errno));
        rc = 1;
    }
    return rc;
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
    int fProgress = 0;
    int fNoMoreOptions = 0;

    g_progname = argv[0];

    /*
     * Print usage if no arguments.
     */
    if (argc <= 1)
        return usage(stderr);

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
                else if (!strcmp(psz, "-check-file"))
                    psz = "C";
                else if (!strcmp(psz, "-progress"))
                    psz = "p";
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

                    case 'p':
                        fProgress = 1;
                        break;

                    case 'q':
                        fQuiet = 1;
                        break;

                    case 'w':
                        /* ignored */
                        break;

                    case 'h':
                        usage(stdout);
                        return 0;

                    case 'v':
                        return kbuild_version(argv[0]);

                    /*
                     * -C md5 file
                     */
                    case 'C':
                    {
                        const char *pszFilename;
                        const char *pszDigest;

                        if (psz[1])
                            pszDigest = &psz[1];
                        else if (i + 1 < argc)
                            pszDigest = argv[++i];
                        else
                        {
                            errx(1, "'-C' is missing the MD5 sum!");
                            return 1;
                        }
                        if (i + 1 < argc)
                            pszFilename = argv[++i];
                        else
                        {
                            errx(1, "'-C' is missing the filename!");
                            return 1;
                        }

                        rc |= check_one_file(pszFilename, pszDigest, fText, fQuiet, fProgress && !fQuiet);
                        psz = "\0";
                        break;
                    }

                    default:
                        errx(1, "Invalid option '%c'! (%s)", *psz, argv[i]);
                        return usage(stderr);
                }
            } while (*++psz);
        }
        else if (fChecking)
            rc |= check_files(argv[i], fText, fBinaryTextOpt, fQuiet, fProgress && !fQuiet);
        else
            rc |= md5sum_file(argv[i], fText, fQuiet, fProgress && !fQuiet);
        i++;
    }

    return rc;
}

