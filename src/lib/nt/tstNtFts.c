

/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#ifndef USE_OLD_FTS
# include "fts-nt.h"
#else
# include "kmkbuiltin/ftsfake.h"
#endif
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>


static int usage(const char *argv0)
{
    printf("usage: %s [options] <dirs & files>\n", argv0);
    printf("\n"
           "options:\n"
           "  -d, --see-dot\n"
           "    FTS_SEEDOT\n"
           "  -p, --physical\n"
           "    FTS_PHYSICAL\n"
           "  -l, --logical\n"
           "    FTS_LOGICAL\n"
           "  -H, --dereference-command-line\n"
           "    FTS_COMFOLLOW\n"
           "  -L, --dereference\n"
           "    Follow symbolic links while scanning directories.\n"
           "  -P, --no-dereference\n"
           "    Do not follow symbolic links while scanning directories.\n"
           "  -c, --no-chdir\n"
           "    FTS_NOCHDIR\n"
           "  -s, --no-stat\n"
           "    FTS_NOSTAT\n"
           "  -x, --one-file-system\n"
           "    FTS_XDEV\n"
           "  -q, --quiet\n"
           "    Quiet operation, no output.\n"
           "  -v, --verbose\n"
           "    Verbose operation (default).\n"
           );
    return 0;
}


int main(int argc, char **argv)
{
    FTS        *pFts;
    int         i;
    int         rcExit       = 0;
    int         cVerbosity   = 1;
    int         fFollowLinks = 0;
    int         fFtsFlags    = 0;
    unsigned    fDoneOptions = 0;
    unsigned    cFtsArgs     = 0;
    char const **papszFtsArgs = calloc(argc + 1, sizeof(char *));

    /*
     * Parse options and heap up non-options.
     */
    for (i = 1; i < argc; i++)
    {
        const char *pszArg = argv[i];
        if (*pszArg == '-' && !fDoneOptions)
        {
            char chOpt = *++pszArg;
            pszArg++;
            if (chOpt == '-')
            {
                if (!chOpt)
                {
                    fDoneOptions = 1;
                    continue;
                }
                if (strcmp(pszArg, "help") == 0)
                    chOpt = 'h';
                else if (strcmp(pszArg, "version") == 0)
                    chOpt = 'V';
                else if (strcmp(pszArg, "see-dot") == 0)
                    chOpt = 'd';
                else if (strcmp(pszArg, "physical") == 0)
                    chOpt = 'p';
                else if (strcmp(pszArg, "logical") == 0)
                    chOpt = 'l';
                else if (strcmp(pszArg, "dereference-command-line") == 0)
                    chOpt = 'H';
                else if (strcmp(pszArg, "no-chdir") == 0)
                    chOpt = 'c';
                else if (strcmp(pszArg, "no-stat") == 0)
                    chOpt = 's';
                else if (strcmp(pszArg, "one-file-system") == 0)
                    chOpt = 'x';
                else if (strcmp(pszArg, "quiet") == 0)
                    chOpt = 'q';
                else if (strcmp(pszArg, "verbose") == 0)
                    chOpt = 'v';
                else if (strcmp(pszArg, "no-ansi") == 0)
                    chOpt = 'w';
                else
                {
                    fprintf(stderr, "syntax error: Unknown option: %s (%s)\n", argv[i], pszArg);
                    return 2;
                }
                pszArg = "";
            }
            do
            {
                switch (chOpt)
                {
                    case '?':
                    case 'h':
                        return usage(argv[0]);
                    case 'V':
                        printf("v0.0.0\n");
                        return 0;

                    case 'd':
                        fFtsFlags |= FTS_SEEDOT;
                        break;
                    case 'l':
                        fFtsFlags |= FTS_LOGICAL;
                        break;
                    case 'p':
                        fFtsFlags |= FTS_PHYSICAL;
                        break;
                    case 'H':
                        fFtsFlags |= FTS_COMFOLLOW;
                        break;
                    case 'c':
                        fFtsFlags |= FTS_NOCHDIR;
                        break;
                    case 's':
                        fFtsFlags |= FTS_NOSTAT;
                        break;
                    case 'x':
                        fFtsFlags |= FTS_XDEV;
                        break;
                    case 'w':
                        fFtsFlags |= FTS_NO_ANSI;
                        break;
                    case 'L':
                        fFollowLinks = 1;
                        break;
                    case 'P':
                        fFollowLinks = 0;
                        break;

                    case 'q':
                        cVerbosity = 0;
                        break;
                    case 'v':
                        cVerbosity++;
                        break;

                    default:
                        fprintf(stderr, "syntax error: Unknown option: -%c (%s)\n", chOpt, argv[i]);
                        return 2;
                }
                chOpt = *pszArg++;
            } while (chOpt != '\0');
        }
        else
            papszFtsArgs[cFtsArgs++] = pszArg;
    }

#ifdef USE_OLD_FTS
    if (papszFtsArgs[0] == NULL)
    {
        fprintf(stderr, "Nothing to do\n");
        return 1;
    }
#endif

    /*
     * Do the traversal.
     */
    errno = 0;
    pFts = fts_open((char **)papszFtsArgs, fFtsFlags, NULL /*pfnCompare*/);
    if (pFts)
    {
        for (;;)
        {
            FTSENT *pFtsEnt = fts_read(pFts);
            if (pFtsEnt)
            {
                const char *pszState;
                switch (pFtsEnt->fts_info)
                {
                    case FTS_D:         pszState = "D"; break;
                    case FTS_DC:        pszState = "DC"; break;
                    case FTS_DEFAULT:   pszState = "DEFAULT"; break;
                    case FTS_DNR:       pszState = "DNR"; break;
                    case FTS_DOT:       pszState = "DOT"; break;
                    case FTS_DP:        pszState = "DP"; break;
                    case FTS_ERR:       pszState = "ERR"; break;
                    case FTS_F:         pszState = "F"; break;
                    case FTS_INIT:      pszState = "INIT"; break;
                    case FTS_NS:        pszState = "NS"; break;
                    case FTS_NSOK:      pszState = "NSOK"; break;
                    case FTS_SL:        pszState = "SL"; break;
                    case FTS_SLNONE:    pszState = "SLNONE"; break;
                    default:
                        pszState = "Invalid";
                        rcExit = 1;
                        break;
                }

                if (cVerbosity > 0)
                {
                    if (fFtsFlags & FTS_NO_ANSI)
                        printf("%8s %ls\n", pszState, pFtsEnt->fts_wcsaccpath);
                    else
                        printf("%8s %s\n", pszState, pFtsEnt->fts_accpath);
                }
                if (   pFtsEnt->fts_info == FTS_SL
                    && pFtsEnt->fts_number == 0
                    && fFollowLinks
                    && (   (fFtsFlags & FTS_COMFOLLOW)
                        || pFtsEnt->fts_level > FTS_ROOTLEVEL) ) {
                    pFtsEnt->fts_number++;
                    fts_set(pFts, pFtsEnt, FTS_FOLLOW);
                }
            }
            else
            {
                if (errno != 0)
                {
                    fprintf(stderr, "fts_read failed: errno=%d\n", errno);
                    rcExit = 1;
                }
                break;
            }
        } /* enum loop */

        errno = 0;
        i = fts_close(pFts);
        if (i != 0)
        {
            fprintf(stderr, "fts_close failed: errno=%d\n", errno);
            rcExit = 1;
        }
    }
    else
    {
        fprintf(stderr, "fts_open failed: errno=%d (cFtsArgs=%u)\n", errno, cFtsArgs);
        rcExit = 1;
    }

    return rcExit;
}
