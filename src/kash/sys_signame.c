/*
 * Fake sys_signame.
 */

#include "shinstance.h" /* for MSC */
#include <string.h>
#include <stdio.h>

static char sys_signame_initialized = 0;
char sys_signame[NSIG][16];

void init_sys_signame(void)
{
	unsigned i;
	if (sys_signame_initialized)
		return;
	for (i = 0; i < NSIG; ++i)
		sprintf(sys_signame[i], "%d", i);
#define SET_SIG_STR(sig) strcpy(sys_signame[SIG##sig], #sig);
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
	sys_signame_initialized = 1;
}

#if defined(_MSC_VER)
const char *strsignal(int iSig)
{
    if (!sys_signame_initialized)
	init_sys_signame();
    if (iSig < NSIG)
	return sys_signame(iSig);
    return NULL;
}
#endif

