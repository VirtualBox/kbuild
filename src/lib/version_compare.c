/* $Id: version_compare.c 3551 2022-01-29 02:57:33Z knut.osmundsen@oracle.com $ */
/** @file
 * version_compare - version compare.
 */

/*
 * Copyright (c) 2020 knut st. osmundsen <bird-kBuild-spamxx@anduin.net>
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
#include "version_compare.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>


/**
 * Simple quantification of the pre-release designations (alpha2, beta1, rc1).
 *
 * @returns ~0U if not a pre-release designation, lesser values if it is.
 * @note    Case is ignored.
 */
static const char *check_release_type(char ch, const char *psz, unsigned *puValue)
{
    const char * const pszStart = psz;
    *puValue = ~0U;
    switch (ch)
    {
        default:
            return psz;

        case 'r':
        case 'R':
            ch = *psz++;
            if (ch != 'c' && ch != 'C')
                return pszStart;
            *puValue = ~0U/4 * 2;
            break;
        case 'b':
        case 'B':
            ch = *psz++;
            if (ch != 'e' && ch != 'E')
                return pszStart;
            ch = *psz++;
            if (ch != 't' && ch != 'T')
                return pszStart;
            ch = *psz++;
            if (ch != 'a' && ch != 'A')
                return pszStart;
            *puValue = ~0U/4;
            break;
        case 'a':
        case 'A':
            ch = *psz++;
            if (ch != 'l' && ch != 'L')
                return pszStart;
            ch = *psz++;
            if (ch != 'p' && ch != 'P')
                return pszStart;
            ch = *psz++;
            if (ch != 'h' && ch != 'H')
                return pszStart;
            ch = *psz++;
            if (ch != 'a' && ch != 'A')
                return pszStart;
            *puValue = 0;
            break;
    }

    /* The next must be an non-alpha character, if a digit we add it to the value.  */
    ch = *psz;
    if (isdigit(ch))
    {
        long int lSub = strtol(psz, (char **)&psz, 10);
        if (lSub >= ~0U / 4)
            lSub = ~0U / 4 - 1;
        *puValue += (unsigned)lSub;
    }
    else if (isalpha(ch))
        return pszStart;
    return psz;
}


/**
 * Deals with returns, mainly from the string compare part.
 */
static int compare_failed(char ch1, char ch2)
{
    if (ch1 == '~')
        return -1;
    if (ch2 == '~')
        return 1;
    if (ch1 == '\0' || ch1 == '/') /* treat '/' similar to '\0' to deal with the v14.2/ vs v14.2.11.9/ case.  */
        return -1;
    if (ch2 == '\0' || ch2 == '/')
        return 1;
    if (isdigit(ch1))
        return -1;
    if (isdigit(ch2))
        return 1;
    if (isalpha(ch1))
        return isalpha(ch2) ? (int)ch1 - (int)ch2 : -1;
    if (isalpha(ch2))
        return 1;
    return (int)ch1 - (int)ch2;
}


int version_compare(const char *psz1, const char *psz2)
{
    for (;;)
    {
        int diff;

        /* Work non-digits:  */
        char ch1 = *psz1++;
        char ch2 = *psz2++;
        for (;;)
        {
            if (ch1 == ch2)
            {
                if (ch1 != '\0')
                {
                    if (isdigit(ch1))
                        break;
                    ch1 = *psz1++;
                    ch2 = *psz2++;
                }
                else
                    return 0;
            }
            else if (isdigit(ch1) && isdigit(ch2))
                break;
            else
                return compare_failed(ch1, ch2);
        }

        /* Skip leading zeros */
        while (ch1 == '0')
            ch1 = *psz1++;

        while (ch2 == '0')
            ch2 = *psz2++;

        /* Compare digits. */
        for (diff = 0;;)
        {
            if (isdigit(ch1))
            {
                if (isdigit(ch2))
                {
                    if (diff == 0)
                        diff = (int)ch1 - (int)ch2;
                    ch1 = *psz1++;
                    ch2 = *psz2++;
                }
                else
                    return 1; /* The number in psz1 is longer and therefore larger. */
            }
            else if (isdigit(ch2))
                return -1;    /* The number in psz1 is shorter and therefore smaller. */
            else if (diff != 0)
                return diff;
            else
                break;
        }

        /* Neither ch1 nor ch2 is a digit at this point, but complete the
           comparisons of the two before looping.  We check for alpha, beta, rc
           suffixes here (mainly to correctly order 1.2.3r4567890 after 1.2.3rc1) */
        {
            unsigned uType1 = ~0;
            unsigned uType2 = ~0;
            psz1 = check_release_type(ch1, psz1, &uType1);
            psz2 = check_release_type(ch2, psz2, &uType2);
            if (uType1 != uType2)
                return uType1 < uType2 ? -1 : 1;
            if (ch1 != ch2 && uType1 == ~0U)
                return compare_failed(ch1, ch2);
            if (ch1 == '\0')
                return 0;
        }
    }
}


#ifdef TEST
# include <stdio.h>

int main()
{
    static const struct
    {
        int rcExpect;
        const char *psz1, *psz2;
    } s_aTests[] =
    {
        {  0, "", "" },
        {  0, "a", "a" },
        {  0, "ab", "ab" },
        {  0, "abc", "abc" },
        {  0, "001", "1" },
        {  0, "000", "0" },
        { -1, "0", "a" },
        { -1, "0", "1" },
        { -1, "0", "9" },
        { -1, "0", "99" },
        { -1, "9", "99" },
        { -1, "98", "99" },
        {  0, "asdfasdf", "asdfasdf" },
        { -1, "asdfasdf", "asdfasdfz" },
        { +1, "asdfasdfz", "asdfasdf" },
        {  0, "a1s2d3f4", "a1s2d3f4" },
        {  0, "a01s002d003f004", "a1s2d3f4" },
        {  0, "a1s2d3f4", "a01s002d003f004" },
        {  0, "kBuild-0.099.7", "kBuild-0.99.00007" },
        { +1, "kBuild-0.099.7", "kBuild-0.99.00007rc1" },
        { +1, "kBuild-0.099.7rc2", "kBuild-0.99.7beta3" },
        { -1, "kBuild-0.099.7alpha", "kBuild-0.99.7beta3" },
        { -1, "kBuild-0.099.7alpha", "kBuild-0.99.7beta3" },
        { -1, "kBuild-0.099.7alpha", "kBuild-0.99.7alpha1" },
        {  0, "kBuild-0.099.7ALPHA1", "kBuild-0.99.7alpha1" },
        { -1, "kBuild-0.099.7BETA1", "kBuild-0.99.7rC1" },
        { -1, "kBuild-0.099", "kBuild-0.99.0" },
        { +1, "kBuild-0.099", "kBuild-0.99~" },
        { +1, "1.2.3r4567890", "1.2.3rc1" },
        { +1, "1.2.3r4567890", "1.2.3RC1" },
        { -1, "/tools/win.amd64/vcc/v14.2/Tools", "/tools/win.amd64/vcc/v14.2.11.9/Tools" },
        { -1, "/tools/win.amd64/vcc/v14.2/Tools", "/tools/win.amd64/vcc/v14.211.9/Tools" },
        { -1, "/tools/win.amd64/vcc/v14.2/Tools", "/tools/win.amd64/vcc/v14.2r2/Tools" },
        { -1, "/tools/win.amd64/vcc/v14.2/Tools", "/tools/win.amd64/vcc/v14.2-r2/Tools" },
    };
    unsigned cErrors = 0;
    unsigned i;

    for (i = 0; i < sizeof(s_aTests) / sizeof(s_aTests[0]); i++)
    {
        int rc = version_compare(s_aTests[i].psz1, s_aTests[i].psz2);
        int rcExpect = s_aTests[i].rcExpect;
        if (rc != (rcExpect < 0 ? -1 : rcExpect > 0 ? 1 : 0))
        {
            fprintf(stderr, "error: Test #%u: %d, expected %d: '%s' vs '%s'\n",
                    i, rc, rcExpect, s_aTests[i].psz1, s_aTests[i].psz2);
            cErrors++;
        }
    }

    return cErrors == 0 ? 0 : 1;
}
#endif

