/* $Id: kShell.c 23 2002-11-22 04:00:46Z knut.osmundsen@oracle.com $
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

/** @design             kShell (Micro Shell)
 *
 * The micro shell provides the basic shell functionality kBuild need - no more,
 * no less. It is intended to be as simple as possible.
 *
 * The shell commands are case sensitive - all lowercase.
 *
 * The shell environment variables are case sensitive or insensitive according to
 * host os.
 *
 *
 *
 * @subsection         Command Separators
 *
 * There is one command separator '&&'. This works like splitting the command line
 * into several makefile lines. This splitting isn't done by the micro shell but
 * the makefile interpreter.
 *
 * You might thing this is limiting, but no, you can use all the makefile command
 * prefixes.
 *
 *
 *
 * @subsection          Path Component Separator (/)
 *
 * The shell uses '/' as path component separator.
 * For host OSes  with the notion of drive letters or similar, ':' is
 * used to separate the drive letter and the path.
 *
 *
 *
 * @subsection          UNC paths
 *
 * For host OSes which supports UNC paths these are supported but for the chdir
 * command.
 *
 * The Path Component Separator is still '/' for UNC paths.
 *
 *
 *
 * @subsection          Wildchars
 *
 * '*' and '?' are accepted as wildchars.
 *
 * '*' means 0 or more characters. <br>
 * '?' means 1 character.
 *
 * When the term 'pattern' is use in command description this means that
 * wildchars are accepted.
 *
 *
 *
 * @subsection          Quoting
 *
 * Use double quotes (") to quote filenames or executables containing spaces.
 *
 *
 *
 * @subsection          Execute Program
 *
 * If the first, possibly quoted, word of a commandline if not found as an
 * internal command will be tried executed. If no path it will be searched
 * for in the PATH environment variable.
 *
 *
 *
 * @subsection          Commands
 *
 * This section will describe the commands implemented by the shell.
 *
 *
 *
 * @subsubsection       copy
 * Copies one or more files to a target file or directory.
 *
 * <b>Syntax: copy <source file pattern> [more sources] <target> </b>
 *
 * Specify one or more source file patterns.
 *
 * Specify exactly one target. The target may be a directory or a file.
 * If it's a file and multiple source files specified either thru pattern or
 * multiple source file specifications, the target file will be a copy of the
 * last one.
 *
 * The command fails if a source file isn't found. It also fails on read or
 * write errors.
 *
 *
 *
 * @subsubsection       copytree
 * Copies one or more files to a target file or directory.
 *
 * <b>Syntax: copytree <source directory> <target directory> </b>
 *
 * Specify exactly one source directory.
 *
 * Specify exactly one target directory. The target directory path will be
 * created if doesn't exist.
 *
 * The command fails if source directory isn't found. It also fails on read or
 * write errors.
 *
 *
 *
 * @subsubsection       rm
 * Deletes one or more files.
 *
 * <b>Syntax: rm [file pattern] [more files] </b>
 *
 * Specify 0 or more file patterns for deletion.
 *
 * This command fails if it cannot delete a file. It will not fail if a file
 * doesn't exist. It will neither fail if no files are specified.
 *
 *
 *
 * @subsubsection       rmtree
 * Deletes one or more directory trees.
 *
 * <b>Syntax: rmtree [directory pattern] [directories] </b>
 *
 * Specify 0 or more directory patterns for deletion.
 *
 * This command fails if it cannot delete a file or directory. It will not fail
 * if a directory doesn't exist. It will neither fail if no files are specified.
 *
 *
 *
 * @subsubsection       chdir
 * Changes the current directory.
 *
 * This updates the .CWD macro to the new current directory path.
 *
 * <b>Syntax: chdir <directory> </b>
 *
 *
 *
 * @subsubsection       mkdir
 * Create directory.
 *
 * <b>Syntax:  mkdir <directory> </b>
 *
 * Specify one directory to create.
 *
 *
 *
 * @subsubsection       rmdir
 * Remove directory.
 *
 * <b>Syntax: rmdir <directory> </b>
 *
 * Specify one directory to remove. The directory must be empty.
 *
 * This command failes if directory isn't empty. It will not fail if
 * the directory doesn't exist.
 *
 *
 *
 * @subsubsection       set
 * Set environment variable.
 *
 * <b>Syntax: set <envvar>=<value> </b>
 *
 *
 *
 * @subsubsection       unset
 * Unset enviornment variable(s).
 *
 * <b>Syntax: unset <envvar pattern> [more envvars] </b>
 *
 * Specify on or more environment variable patterns.
 *
 *
 *
 * @subsubsection       pushenv
 * Pushes a set of environment variables onto the environment stack. The
 * variables can later be popped back using the popenv command.
 *
 * If '*' is specified as pattern the complete enviornment is pushed and
 * when popped it will <b>replace</b> the enviornment.
 *
 * <b>Syntax: pushenv <envvar pattern> [more envvars] </b>
 * <b>Syntax: pushenv * </b>
 *
 *
 *
 * @subsubsection       popenv
 * Pop a set of environment variables from the environment stack. If a '*'
 * push was done, we'll replace the enviornment with the variables poped off
 * the stack.
 *
 * <b>Syntax: popenv </b>
 *
 *
 *
 */


/*******************************************************************************
*   Defined Constants And Macros                                               *
*******************************************************************************/
#define KSWORD_FLAGS_ESCAPE     1
#define KSWORD_FLAGS_QUOTED     2
#define KSWORD_FLAGS_PATTERN    4

#define KSHELL_MAX_COMMAND      4096

/**
 * Test if this is an escapable character or not.
 */
#define KSHELL_ESCAPABLE(ch)    (   (ch) == '"' \
                                 || (ch) == '\'' \
                                 || (ch) == '`' \
                                 || (ch) == 'ï' \
                                 )

/**
 * Test if this is a quote character or not.
 */
#define KSHELL_QUOTE(ch)        (   (ch) == '"' \
                                 || (ch) == '\'' \
                                 || (ch) == '`' \
                                 || (ch) == 'ï' \
                                 )

/**
 * Test if this is a wildchar character or not.
 */
#define KSHELL_WILDCHAR(ch)     ( (ch) == '*' || (ch) == '?' )



/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
#include "kShell.h"
#include <kLib/kLib.h>
#include <kLib/kString.h>

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
*   Global Variables                                                           *
*******************************************************************************/
static const char *pszkshellCurDir = NULL;

/*******************************************************************************
*   Internal Functions                                                         *
*******************************************************************************/
PKSHELLWORDS    kshellWordsParse(const char *pszText, int cWords, PKSHELLWORDS pPrevWords);
void            kshellWordsDestroy(PKSHELLWORDS pWords);

int             kshellSyntaxError(const char *pszCmd, const char *pszMessage);
int             kshellError(const char *pszCmd, const char *pszMessage);

int             kshellCmd_copy(const char *pszCmd, PKSHELLWORDS pWords);
int             kshellCmd_copytree(const char *pszCmd, PKSHELLWORDS pWords);
int             kshellCmd_sync(const char *pszCmd, PKSHELLWORDS pWords);
int             kshellCmd_synctree(const char *pszCmd, PKSHELLWORDS pWords);
int             kshellCmd_rm(const char *pszCmd, PKSHELLWORDS pWords);
int             kshellCmd_rmtree(const char *pszCmd, PKSHELLWORDS pWords);
int             kshellCmd_chdir(const char *pszCmd, PKSHELLWORDS pWords);
int             kshellCmd_mkdir(const char *pszCmd, PKSHELLWORDS pWords);
int             kshellCmd_rmdir(const char *pszCmd, PKSHELLWORDS pWords);
int             kshellCmd_set(const char *pszCmd, PKSHELLWORDS pWords);
int             kshellCmd_unset(const char *pszCmd, PKSHELLWORDS pWords);
int             kshellCmd_pushenv(const char *pszCmd, PKSHELLWORDS pWords);
int             kshellCmd_popenv(const char *pszCmd, PKSHELLWORDS pWords);
int             kshellCmd_echo(const char *pszCmd, PKSHELLWORDS pWords);
int             kshellCmd_write(const char *pszCmd, PKSHELLWORDS pWords);
int             kshellCmd_ExecuteProgram(const char *pszCmd, PKSHELLWORDS pWords);



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
        {"copy",        MAX_WORDS,  kshellCmd_copy},
        {"copytree",    3,          kshellCmd_copytree},
        {"sync",        MAX_WORDS,  kshellCmd_sync},
        {"synctree",    3,          kshellCmd_synctree},
        {"rm",          MAX_WORDS,  kshellCmd_rm},
        {"rmtree",      MAX_WORDS,  kshellCmd_rmtree},
        {"chdir",       2,          kshellCmd_chdir},
        {"mkdir",       MAX_WORDS,  kshellCmd_mkdir},
        {"rmdir",       MAX_WORDS,  kshellCmd_rmdir},
        {"set",         1,          kshellCmd_set},
        {"unset",       MAX_WORDS,  kshellCmd_unset},
        {"pushenv",     MAX_WORDS,  kshellCmd_pushenv},
        {"popenv",      1,          kshellCmd_popenv},
        {"echo",        2,          kshellCmd_echo},
        {"write",       2,          kshellCmd_write},

        /* last entry */
        {"",            1,          kshellCmd_ExecuteProgram}
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
    while (cWords-- > 0)
    {
        KSHELLWORD  word = {0,0,0,0,0};
        char        chEnd = ' ';
        char        ch;

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
         * Any possible quote!
         */
        if (KSHELL_QUOTE(*pszText))
        {
            chEnd = *pszText++;
            word.fFlags |= KSWORD_FLAGS_QUOTED;
        }


        /*
         * Find end of word and look for escape and pattern characters.
         * We escape by doubling the character, not by slashing!
         */
        while ((ch = *pszText) != '\0' && (ch != chEnd || pszText[1] == chEnd))
        {
            if (ch == pszText[1] && KSHELL_ESCAPABLE(ch))
            {
                word.fFlags |= KSWORD_FLAGS_ESCAPE;
                pszText++;
            }
            if (KSHELL_WILDCHAR(ch))
                word.fFlags |= KSWORD_FLAGS_PATTERN;
            pszText++;
        }
        if (word.fFlags & KSWORD_FLAGS_QUOTED)
            pszText++;
        word.cchWordOrg = pszText - word.pszWordOrg;


        /*
         * Make a copy of the word and unescape (if needed).
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
                char ch;
                if ((ch = *pszSrc) == pszSrc[1] && KSHELL_ESCAPABLE(ch))
                    pszSrc++;
                *pszTrg++ = ch;
                pszSrc++;
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
 * Display an syntax message.
 * @returns KSHELL_ERROR_SYNTAX_ERROR
 * @param   pszCmd      The command name.
 * @param   pszMessage  Message text.
 */
int             kshellSyntaxError(const char *pszCmd, const char *pszMessage)
{
    fflush(stdout);
    fprintf(stderr, "Syntax error while executing command '%s': %s\n", pszCmd, pszMessage);
    return KSHELL_ERROR_SYNTAX_ERROR;
}


/**
 * Display an generic message.
 * @returns KSHELL_ERROR_SYNTAX_ERROR
 * @param   pszCmd      The command name.
 * @param   pszMessage  Message text.
 */
int             kshellError(const char *pszCmd, const char *pszMessage)
{
    fflush(stdout);
    fprintf(stderr, "Error while executing command '%s': %s\n", pszCmd, pszMessage);
    return -1;
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
int             kshellCmd_ExecuteProgram(const char *pszCmd, PKSHELLWORDS pWords)
{
    return -1;
}


/*
 *
 * The commands are documented externally.
 * (Bad idea btw!)
 *
 */


int             kshellCmd_copy(const char *pszCmd, PKSHELLWORDS pWords)
{
    return -1;
}


int             kshellCmd_copytree(const char *pszCmd, PKSHELLWORDS pWords)
{
    return -1;
}


int             kshellCmd_sync(const char *pszCmd, PKSHELLWORDS pWords)
{
    return -1;
}


int             kshellCmd_synctree(const char *pszCmd, PKSHELLWORDS pWords)
{
    return -1;
}


int             kshellCmd_rm(const char *pszCmd, PKSHELLWORDS pWords)
{
    return -1;
}


int             kshellCmd_rmtree(const char *pszCmd, PKSHELLWORDS pWords)
{
    return -1;
}


int             kshellCmd_chdir(const char *pszCmd, PKSHELLWORDS pWords)
{
    return -1;
}


int             kshellCmd_mkdir(const char *pszCmd, PKSHELLWORDS pWords)
{
    return -1;
}


int             kshellCmd_rmdir(const char *pszCmd, PKSHELLWORDS pWords)
{
    return -1;
}


int             kshellCmd_set(const char *pszCmd, PKSHELLWORDS pWords)
{
    return -1;
}


int             kshellCmd_unset(const char *pszCmd, PKSHELLWORDS pWords)
{
    return -1;
}


int             kshellCmd_pushenv(const char *pszCmd, PKSHELLWORDS pWords)
{
    return -1;
}


int             kshellCmd_popenv(const char *pszCmd, PKSHELLWORDS pWords)
{
    return -1;
}


/** @subsubsection      echo
 * Prints a message to stdout.
 *
 * <b>Syntax: echo <level> <message>
 *
 * Level is verbosity level of the message. This is compared with the
 * KBUILD_MSG_LEVEL environment variable. The message is suppressed if the
 * level is lower that KBUILD_MSG_LEVEL.
 *
 * The message is printed word for word normalize with a single space between
 * the words. It's therefore a good thing to quote the message.
 *
 * The message can be empty. Then a blank line will be printed.
 */
int             kshellCmd_echo(const char *pszCmd, PKSHELLWORDS pWords)
{
    int         rc = KSHELL_ERROR_SYNTAX_ERROR;

    /*
     * Get the message level from the message.
     */
    if (pWords->cWords >= 2)
    {
        unsigned uMsgLevel = kStrToUnsigned(pWords->aWords[1].pszWord, -2);
        if (uMsgLevel != -2)
        {
            if (uMsgLevel <= kEnvGetUnsigned("KBUILD_MSG_LEVEL", 0))
            {
                /* output all the words forcing one space separation */
                pWords = kshellWordsParse(pszCmd, -1, pWords);
                if (pWords)
                {
                    int i;
                    for (i = 2; i < pWords->cWords; i++)
                        fwrite(pWords->aWords[i].pszWord, pWords->aWords[i].cchWord, 1, stdout);
                }

                /* new line */
                fputc('\n', stdout);
                fflush(stdout);
            }
        }
        else
            kshellSyntaxError("echo", "invalid message level!");
    }
    else
        kshellSyntaxError("echo", "requires at least one argument!");

    return -1;
}


int             kshellCmd_write(const char *pszCmd, PKSHELLWORDS pWords)
{
    return -1;
}



