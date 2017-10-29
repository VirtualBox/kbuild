/* $Id: dos2unix.c 3114 2017-10-29 18:02:04Z knut.osmundsen@oracle.com $ */
/** @file
 * dos2unix - Line ending conversion routines.
 */

/*
 * Copyright (c) 2017 knut st. osmundsen <bird-kBuild-spamx@anduin.net>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 * Alternatively, the content of this file may be used under the terms of the
 * GPL version 2 or later, or LGPL version 2.1 or later.
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include "dos2unix.h"
#include <k/kDefs.h>
#include <errno.h>
#include <fcntl.h>
#if K_OS == K_OS_WINDOWS
# include <io.h>
#else
# include <unistd.h>
#endif
#include <assert.h>

#ifndef O_BINARY
# ifdef _O_BINARY
#  define O_BINARY   _O_BINARY
# else
#  define O_BINARY   0
# endif
#endif


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#define STACK_BUF_SIZE  0x20000

#define DOS2UNIX_LF     0x0a
#define DOS2UNIX_CR     0x0d



/**
 * Does a line ending analysis of the given file.
 *
 * @returns 0 on success, errno value on open or read error.
 * @param   pszFilename         The path to the file
 * @param   pfStyle             Where to return the DOS2UNIX_STYLE_XXX and
 *                              DOS2UNIX_F_XXX flags.
 * @param   pcDosEols           Where to return the number of DOS end-of-line
 *                              sequences found.  Optional.
 * @param   pcUnixEols          Where to return the number of UNIX end-of-line
 *                              sequences found.
 */
int dos2unix_analyze_file(const char *pszFilename, KU32 *pfStyle, KSIZE *pcDosEols, KSIZE *pcUnixEols)
{
    int iRet = 0;
    int fd = open(pszFilename, O_RDONLY | O_BINARY);
    if (fd >= 0)
    {
        iRet = dos2unix_analyze_fd(fd, pfStyle, pcDosEols, pcUnixEols);
        close(fd);
    }
    else
    {
        iRet = errno;
        *pfStyle = DOS2UNIX_STYLE_NONE;
        if (pcUnixEols)
            *pcUnixEols = 0;
        if (pcDosEols)
            *pcDosEols = 0;
    }
    return iRet;
}

/**
 * Does a line ending analysis of the given file descriptor.
 *
 * @returns 0 on success, errno value on open or read error.
 * @param   fd                  The file descriptor to analyze.  Caller must
 *                              place this as the desired position.
 * @param   pfStyle             Where to return the DOS2UNIX_STYLE_XXX and
 *                              DOS2UNIX_F_XXX flags.
 * @param   pcDosEols           Where to return the number of DOS end-of-line
 *                              sequences found.  Optional.
 * @param   pcUnixEols          Where to return the number of UNIX end-of-line
 *                              sequences found.
 */
int dos2unix_analyze_fd(int fd, KU32 *pfStyle, KSIZE *pcDosEols, KSIZE *pcUnixEols)
{
    KSIZE   cUnixEols  = 0;
    KSIZE   cDosEols   = 0;
    KSIZE   cLoneCrs   = 0;
    KBOOL   fPendingCr = K_FALSE;
    int     iRet       = 0;

    /*
     * Do the analysis.
     */
    *pfStyle = DOS2UNIX_STYLE_NONE;
    for (;;)
    {
        char achBuf[STACK_BUF_SIZE];
        int  cchRead = read(fd, achBuf, sizeof(achBuf));
        if (cchRead > 0)
        {
            int off = 0;
            if (fPendingCr)
            {
                if (achBuf[0] == DOS2UNIX_LF)
                {
                    off++;
                    cDosEols++;
                }
                else
                    cLoneCrs++;
                fPendingCr = K_FALSE;
            }

            while (off < cchRead)
            {
                char ch = achBuf[off++];
                if ((unsigned char)ch > (unsigned char)DOS2UNIX_CR)
                { /* likely */ }
                else if (ch == DOS2UNIX_CR)
                {
                    if (off < cchRead && achBuf[off] == DOS2UNIX_CR)
                        cDosEols++;
                    else
                    {
                        fPendingCr = K_TRUE;
                        while (off < cchRead)
                        {
                            ch = achBuf[off++];
                            if (ch != DOS2UNIX_CR)
                            {
                                if (ch == DOS2UNIX_LF)
                                    cDosEols++;
                                else
                                    cLoneCrs++;
                                fPendingCr = K_FALSE;
                                break;
                            }
                            cLoneCrs++;
                        }
                    }
                }
                else if (ch == DOS2UNIX_LF)
                    cUnixEols++;
                else if (ch == '\0')
                    *pfStyle |= DOS2UNIX_F_BINARY;
            }
        }
        else
        {
            if (cchRead < 0)
                iRet = errno;
            if (fPendingCr)
                cLoneCrs++;
            break;
        }
    }

    /*
     * Set return values.
     */
    if (cUnixEols > 0 && cDosEols == 0)
        *pfStyle |= DOS2UNIX_STYLE_UNIX;
    else if (cDosEols > 0 && cUnixEols == 0)
        *pfStyle |= DOS2UNIX_STYLE_DOS;
    else if (cDosEols != 0 && cUnixEols != 0)
        *pfStyle |= DOS2UNIX_STYLE_MIXED;
    if (pcUnixEols)
        *pcUnixEols = cUnixEols;
    if (pcDosEols)
        *pcDosEols = cDosEols;

    return iRet;
}


/**
 * Converts a buffer to unix line (LF) endings.
 *
 * @retval  K_TRUE if pending CR.  The caller must handle this case.
 * @retval  K_FALSE if no pending CR.
 *
 * @param   pchSrc          The input buffer.
 * @param   cchSrc          Number of characters to convert from the input
 *                          buffer.
 * @param   pchDst          The output buffer.  This must be at least as big as
 *                          the input.  It is okay if this overlaps with the
 *                          source buffer, as long as this is at the same or a
 *                          lower address.
 * @param   pcchDst         Where to return the number of characters in the
 *                          output buffer.
 */
KBOOL dos2unix_convert_to_unix(const char *pchSrc, KSIZE cchSrc, char *pchDst, KSIZE *pcchDst)
{
    KSIZE offDst = 0;
    while (cchSrc-- > 0)
    {
        char ch = *pchSrc++;
        if ((unsigned char)ch != (unsigned char)DOS2UNIX_CR)
            pchDst[offDst++] = ch;
        else if (cchSrc > 0 && *pchSrc == DOS2UNIX_LF)
        {
            pchDst[offDst++] = DOS2UNIX_LF;
            cchSrc--;
            pchSrc++;
        }
        else if (cchSrc == 0)
        {
            *pcchDst = offDst;
            return K_TRUE;
        }
        else
            pchDst[offDst++] = ch;
    }

    *pcchDst = offDst;
    return K_FALSE;
}


/**
 * Converts a buffer to DOS (CRLF) endings.
 *
 * @retval  K_TRUE if pending CR.  The caller must handle this case.
 * @retval  K_FALSE if no pending CR.
 *
 * @param   pchSrc          The input buffer.
 * @param   cchSrc          Number of characters to convert from the input
 *                          buffer.
 * @param   pchDst          The output buffer.  This must be at least _twice_ as
 *                          big as the input.  It is okay if the top half of the
 *                          buffer overlaps with the source buffer.
 * @param   pcchDst         Where to return the number of characters in the
 *                          output buffer.
 */
KBOOL dos2unix_convert_to_dos(const char *pchSrc, KSIZE cchSrc, char *pchDst, KSIZE *pcchDst)
{
    KSIZE offDst = 0;
    while (cchSrc-- > 0)
    {
        char ch = *pchSrc++;
        if ((unsigned char)ch > (unsigned char)DOS2UNIX_CR)
            pchDst[offDst++] = ch;
        else if (ch == DOS2UNIX_CR)
        {
            /* We treat CR kind of like an escape character. */
            do
            {
                if (cchSrc > 0)
                {
                    pchDst[offDst++] = ch;
                    cchSrc--;
                    ch = *pchSrc++;
                }
                else
                {
                    *pcchDst = offDst;
                    return K_TRUE;
                }
            } while (ch == DOS2UNIX_CR);
            pchDst[offDst++] = ch;
        }
        else if (ch == DOS2UNIX_LF)
        {
            pchDst[offDst++] = DOS2UNIX_CR;
            pchDst[offDst++] = DOS2UNIX_LF;
        }
        else
            pchDst[offDst++] = ch;
    }

    *pcchDst = offDst;
    return K_FALSE;
}

