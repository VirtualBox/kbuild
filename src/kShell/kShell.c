/* $Id: kShell.c 19 2002-10-17 23:02:18Z knut.osmundsen@oracle.com $
 *
 * kShell - A mini shell.
 *
 * Copyright (c) 2002 knut st. osmundsen <bird@anduin.net>
 *
 *
 * This file is part of kBuild.
 *
 * kBuild is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * kBuild is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with kBuild; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

/*******************************************************************************
*   Defined Constants And Macros                                               *
*******************************************************************************/
#define KSWORD_FLAGS_ESCAPE     1
#define KSWORD_FLAGS_QUOTED     2
#define KSWORD_FLAGS_PATTERN    4

#define KSHELL_MAX_COMMAND      4096

/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
#include "kShell.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>


/*******************************************************************************
*   Global Variables                                                           *
*******************************************************************************/
typedef struct _kshellWord
{
    int         fFlags;
    char *      pszWord;
    unsigned    cchWord;
    const char *pszWordOrg;
    unsigned    cchWordOrg;
} KSHELLWORD, *PKSHELLWORD;


typedef struct _kshellWords
{
    unsigned    cWords;
    KSHELLWORD  aWords[1];
} KSHELLWORDS, *PKSHELLWORDS;


/*******************************************************************************
*   Internal Functions                                                         *
*******************************************************************************/
PKSHELLWORDS    kshellWordsParse(const char *pszText, int cWords, PKSHELLWORDS pPrevWords);
void            kshellWordsDestroy(PKSHELLWORDS pWords);

int             kshellCmdcopy(const char *pszCmd, PKSHELLWORDS pWords);
int             kshellCmdcopytree(const char *pszCmd, PKSHELLWORDS pWords);
int             kshellCmdrm(const char *pszCmd, PKSHELLWORDS pWords);
int             kshellCmdrmtree(const char *pszCmd, PKSHELLWORDS pWords);
int             kshellCmdchdir(const char *pszCmd, PKSHELLWORDS pWords);
int             kshellCmdmkdir(const char *pszCmd, PKSHELLWORDS pWords);
int             kshellCmdrmdir(const char *pszCmd, PKSHELLWORDS pWords);
int             kshellCmdset(const char *pszCmd, PKSHELLWORDS pWords);
int             kshellCmdunset(const char *pszCmd, PKSHELLWORDS pWords);
int             kshellCmdpushenv(const char *pszCmd, PKSHELLWORDS pWords);
int             kshellCmdpopenv(const char *pszCmd, PKSHELLWORDS pWords);
int             kshellCmdecho(const char *pszCmd, PKSHELLWORDS pWords);
int             kshellCmdwrite(const char *pszCmd, PKSHELLWORDS pWords);
int             kshellCmdExecuteProgram(const char *pszCmd, PKSHELLWORDS pWords);



/**
 * Initiate the shell.
 * Allow us to initiate globals.
 *
 * @returns 0 on success.
 * @returns error code on error.
 * @param   fVerbose    If set banner will be printed.
 */
int     kshellInit(int fVerbose)
{
    if (fVerbose)
    {
        printf("\n"
               "kShell v0.0.0\n"
               "Copyright 2002 knut st. osmundsen <bird@anduin.net>\n"
               "\n");
    }
    return 0;
}


/**
 * Terminate the shell.
 * Allow us to cleanup stuff.
 */
void    kshellTerm(void)
{
    return;
}


/**
 * Start interactive shell interface.
 * This reads commands from stdin till 'exit' is encountered  or end-of-file.
 *
 * @returns returncode of last command.
 */
int     kshellInteractive(void)
{
    static char szCmd[KSHELL_MAX_COMMAND];
    int         rc = 0;

    printf("kShell>");
    fflush(stdout);
    while (fgets(&szCmd[0], sizeof(szCmd), stdin))
    {
        char *pszEnd = &szCmd[strlen(&szCmd[0]) - 1];
        while (pszEnd >= &szCmd[0] && (*pszEnd == '\n' || *pszEnd == '\r'))
            *pszEnd-- = '\0';

        if (!strcmp(&szCmd[0], "exit"))
            break;

        rc = kshellExecute(&szCmd[0]);
        printf("kShell(rc=%d)>", rc);
        fflush(stdout);
    }

    return rc;
}

/**
 * Execute a shell command.
 * @returns 0 on success if command.
 * @returns Error code if command failed.
 * @returns Return code from program.
 * @returns 1742 (KSHELL_ERROR_PROGRAM_NOT_FOUND) if program wasn't found.
 * @returns 1743 (KSHELL_ERROR_COMMAND_TOO_LONG) if program commandline was too long.
 * @param   pszCmd  Command or program to execute.
 */
int     kshellExecute(const char *pszCmd)
{
#define MAX_WORDS (~0)
    static struct _kshellCommands
    {
        const char *pszCmd;
        unsigned    cWords;             /* Number of words including the command it self. */
        int       (*pfnCmd)(const char *, PKSHELLWORDS);
    } aCmds[] =
    {
        {"copy",        MAX_WORDS,  kshellCmdcopy},
        {"copytree",    3,          kshellCmdcopytree},
        {"rm",          MAX_WORDS,  kshellCmdrm},
        {"rmtree",      MAX_WORDS,  kshellCmdrmtree},
        {"chdir",       2,          kshellCmdchdir},
        {"mkdir",       MAX_WORDS,  kshellCmdmkdir},
        {"rmdir",       MAX_WORDS,  kshellCmdrmdir},
        {"set",         1,          kshellCmdset},
        {"unset",       MAX_WORDS,  kshellCmdunset},
        {"pushenv",     MAX_WORDS,  kshellCmdpushenv},
        {"popenv",      1,          kshellCmdpopenv},
        {"echo",        2,          kshellCmdecho},
        {"write",       2,          kshellCmdwrite},

        /* last entry */
        {"",            1,          kshellCmdExecuteProgram}
    };
#undef MAX_WORDS

    PKSHELLWORDS    pWords;
    int             i;
    int             rc;


    /*
     * Parse out the first word.
     */
    pWords = kshellWordsParse(pszCmd, 1, NULL);
    if (!pWords)
        return KSHELL_ERROR_NOT_ENOUGHT_MEMORY;
    if (!pWords->cWords)
        return 0;


    /*
     * Look for command.
     * Note! the last entry is the default command (execute program).
     */
    for (i = 0; i < (sizeof(aCmds) / sizeof(aCmds[0])) - 1; i++)
    {
        if (!strcmp(aCmds[i].pszCmd, pWords->aWords[0].pszWord))
            break;
    }


    /*
     * Execute command.
     */
    if (aCmds[i].cWords > 1)
    {
        pWords = kshellWordsParse(pszCmd, aCmds[i].cWords, pWords);
        if (!pWords)
            return KSHELL_ERROR_NOT_ENOUGHT_MEMORY;
    }
    rc = aCmds[i].pfnCmd(pszCmd, pWords);


    return rc;
}


/**
 * Parses words out of a string.
 *
 * @returns Pointer to a words structure.
 *          This must be destroy calling kshellWordsDestroy().
 * @returns NULL on failure (out of memory).
 * @param   pszText     Text string to parse.
 * @param   cWords      Number of words to parse. Will stop after cWords.
 * @param   pPrevWords  Pointer to structur of previosly parse words from pszText.
 *                      Use this to continue parsing a string. The pPrevWords
 *                      structure will be destroyed.
 *                      If NULL we'll start from the begining.
 */
PKSHELLWORDS    kshellWordsParse(const char *pszText, int cWords, PKSHELLWORDS pPrevWords)
{
    PKSHELLWORDS pWords = pPrevWords;

    /*
     * If previous work done, skip to end of that.
     */
    if (pPrevWords && pPrevWords->cWords)
    {
        pszText = pPrevWords->aWords[pPrevWords->cWords - 1].pszWordOrg
                    + pPrevWords->aWords[pPrevWords->cWords - 1].cchWordOrg;
        cWords -= pPrevWords->cWords;
    }

    /*
     * Parse loop
     */
    while (cWords > 0)
    {
        KSHELLWORD  word = {0,0,0,0,0};
        char        chEnd = ' ';

        /*
         * Skip blanks to find start of word.
         */
        while (*pszText == ' ' || *pszText == '\t')
            pszText++;
        if (!*pszText)
            break;
        word.pszWordOrg = pszText;

        /*
         * Quoted?
         */
        if (*pszText == '"')
        {
            pszText++;
            word.fFlags |= KSWORD_FLAGS_QUOTED;
            chEnd = '"';
        }

        /*
         * Find end of word and look for escape and pattern characters.
         */
        while (*pszText != '\0' && *pszText != chEnd)
        {
            if (*pszText == '\\')
            {
                word.fFlags |= KSWORD_FLAGS_ESCAPE;
                pszText++;
            }
            if (*pszText == '*' || *pszText == '?')
                word.fFlags |= KSWORD_FLAGS_PATTERN;
            pszText++;
        }
        if (word.fFlags & KSWORD_FLAGS_QUOTED)
            pszText++;
        word.cchWordOrg = pszText - word.pszWordOrg;

        /*
         * Make a copy of the word and unescape (if required).
         */
        word.pszWord = malloc(word.cchWordOrg + 1);
        if (!word.pszWord)
        {
            kshellWordsDestroy(pWords);
            return NULL;
        }

        if (word.fFlags & KSWORD_FLAGS_ESCAPE)
        {
            int         cch = word.cchWordOrg;
            const char *pszSrc = word.pszWordOrg;
            char *      pszTrg = word.pszWord;
            while (cch)
            {
                if (*pszSrc == '\\')
                    pszSrc++;
                *pszTrg++ = *pszSrc++;
            }
            word.cchWord = pszTrg - word.pszWord;
            *pszTrg = '\0';
        }
        else
        {
            if (word.fFlags & KSWORD_FLAGS_QUOTED)
            {
                word.cchWord = word.cchWordOrg - 2;
                memcpy(word.pszWord, word.pszWordOrg + 1, word.cchWord);
            }
            else
            {
                word.cchWord = word.cchWordOrg;
                memcpy(word.pszWord, word.pszWordOrg, word.cchWord);
            }
            word.pszWord[word.cchWord] = '\0';
        }


        /*
         * Add to words structure.
         */
        if (!pWords || ((pWords->cWords + 1) % 32))
        {
            void *pv = realloc(pWords, sizeof(KSHELLWORDS) + ((pWords ? pWords->cWords : 0) + 32) * sizeof(KSHELLWORD));
            if (!pv)
            {
                kshellWordsDestroy(pWords);
                return NULL;
            }
            if (pWords)
                pWords = pv;
            else
            {
                pWords = pv;
                pWords->cWords = 0;
            }
        }
        pWords->aWords[pWords->cWords++] = word;
    }

    return pWords;
}


/**
 * Destroys a words structure freeing it's memory.
 * @param   pWords  Pointer to words structure to destroy.
 */
void            kshellWordsDestroy(PKSHELLWORDS pWords)
{
    if (pWords)
    {
        int i;

        for (i = 0; i < pWords->cWords; i++)
        {
            if (pWords->aWords[i].pszWord)
            {
                free(pWords->aWords[i].pszWord);
                pWords->aWords[i].pszWord = NULL;
            }
        }

        pWords->cWords = 0;
        free(pWords);
    }
}


/**
 * Execute program.
 *
 * @returns program return code.
 * @returns 1742 (KSHELL_ERROR_PROGRAM_NOT_FOUND) if program wasn't found.
 * @returns 1743 (KSHELL_ERROR_COMMAND_TOO_LONG) if program commandline was too long.
 *
 * @param   pszCmd  Pointer to commandline.
 * @param   pWords  Pointer to 1st word in pszCmd.
 */
int             kshellCmdExecuteProgram(const char *pszCmd, PKSHELLWORDS pWords)
{
    return -1;
}


/*
 *
 * The commands are documented externally.
 * (Bad idea btw!)
 *
 */


int             kshellCmdcopy(const char *pszCmd, PKSHELLWORDS pWords)
{
    return -1;
}


int             kshellCmdcopytree(const char *pszCmd, PKSHELLWORDS pWords)
{
    return -1;
}


int             kshellCmdrm(const char *pszCmd, PKSHELLWORDS pWords)
{
    return -1;
}


int             kshellCmdrmtree(const char *pszCmd, PKSHELLWORDS pWords)
{
    return -1;
}


int             kshellCmdchdir(const char *pszCmd, PKSHELLWORDS pWords)
{
    return -1;
}


int             kshellCmdmkdir(const char *pszCmd, PKSHELLWORDS pWords)
{
    return -1;
}


int             kshellCmdrmdir(const char *pszCmd, PKSHELLWORDS pWords)
{
    return -1;
}


int             kshellCmdset(const char *pszCmd, PKSHELLWORDS pWords)
{
    return -1;
}


int             kshellCmdunset(const char *pszCmd, PKSHELLWORDS pWords)
{
    return -1;
}


int             kshellCmdpushenv(const char *pszCmd, PKSHELLWORDS pWords)
{
    return -1;
}


int             kshellCmdpopenv(const char *pszCmd, PKSHELLWORDS pWords)
{
    return -1;
}


int             kshellCmdecho(const char *pszCmd, PKSHELLWORDS pWords)
{
    return -1;
}


int             kshellCmdwrite(const char *pszCmd, PKSHELLWORDS pWords)
{
    return -1;
}

