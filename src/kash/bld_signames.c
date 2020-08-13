
#include "shinstance.h" /* for MSC */
#include <string.h>
#include <stdio.h>

int main(int argc, char **argv)
{
    char    aszSigName[NSIG][16];
    FILE   *pFile;
    int     i;

    if (argc != 2 || *argv[1] == '\0')
    {
        fprintf(stderr, "syntax error: Expected exactly one parameter, the output-file!\n");
        return 2;
    }

    /*
     * Populate the name array.
     */
    strcpy(aszSigName[0], "Signal 0");
    for (i = 1; i < NSIG; ++i)
        sprintf(aszSigName[i], "%i", i);

#define SET_SIG_STR(sig) strcpy(aszSigName[SIG##sig], #sig);
#ifdef SIGHUP
    SET_SIG_STR(HUP);
#endif
#ifdef SIGINT
    SET_SIG_STR(INT);
#endif
#ifdef SIGQUIT
    SET_SIG_STR(QUIT);
#endif
#ifdef SIGILL
    SET_SIG_STR(ILL);
#endif
#ifdef SIGTRAP
    SET_SIG_STR(TRAP);
#endif
#ifdef SIGABRT
    SET_SIG_STR(ABRT);
#endif
#ifdef SIGIOT
    SET_SIG_STR(IOT);
#endif
#ifdef SIGBUS
    SET_SIG_STR(BUS);
#endif
#ifdef SIGFPE
    SET_SIG_STR(FPE);
#endif
#ifdef SIGKILL
    SET_SIG_STR(KILL);
#endif
#ifdef SIGUSR1
    SET_SIG_STR(USR1);
#endif
#ifdef SIGSEGV
    SET_SIG_STR(SEGV);
#endif
#ifdef SIGUSR2
    SET_SIG_STR(USR2);
#endif
#ifdef SIGPIPE
    SET_SIG_STR(PIPE);
#endif
#ifdef SIGALRM
    SET_SIG_STR(ALRM);
#endif
#ifdef SIGTERM
    SET_SIG_STR(TERM);
#endif
#ifdef SIGSTKFLT
    SET_SIG_STR(STKFLT);
#endif
#ifdef SIGCHLD
    SET_SIG_STR(CHLD);
#endif
#ifdef SIGCONT
    SET_SIG_STR(CONT);
#endif
#ifdef SIGSTOP
    SET_SIG_STR(STOP);
#endif
#ifdef SIGTSTP
    SET_SIG_STR(TSTP);
#endif
#ifdef SIGTTIN
    SET_SIG_STR(TTIN);
#endif
#ifdef SIGTTOU
    SET_SIG_STR(TTOU);
#endif
#ifdef SIGURG
    SET_SIG_STR(URG);
#endif
#ifdef SIGXCPU
    SET_SIG_STR(XCPU);
#endif
#ifdef SIGXFSZ
    SET_SIG_STR(XFSZ);
#endif
#ifdef SIGVTALRM
    SET_SIG_STR(VTALRM);
#endif
#ifdef SIGPROF
    SET_SIG_STR(PROF);
#endif
#ifdef SIGWINCH
    SET_SIG_STR(WINCH);
#endif
#ifdef SIGIO
    SET_SIG_STR(IO);
#endif
#ifdef SIGPWR
    SET_SIG_STR(PWR);
#endif
#ifdef SIGSYS
    SET_SIG_STR(SYS);
#endif
#ifdef SIGBREAK
    SET_SIG_STR(BREAK);
#endif
#undef SET_SIG_STR

    /*
     * Write out the list.
     */
    pFile = fopen(argv[1], "w");
    if (!pFile)
    {
        fprintf(stderr, "error: failed to open '%s' for writing\n", argv[1]);
        return 1;
    }
    fputs("/* autogenerate */\n"
          "\n"
          "#include \"shinstance.h\"\n"
          "\n"
          "const char * const sys_signame[NSIG] = \n"
          "{\n"
          , pFile);
    for (i = 0; i < NSIG; i++)
        fprintf(pFile, "    \"%s\",\n", aszSigName[i]);
    fputs("};\n", pFile);

    if (fclose(pFile) != 0)
    {
        fprintf(stderr, "error: error writing/closing '%s' after writing it\n", argv[1]);
        return 1;
    }
    return 0;
}

