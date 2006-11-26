#ifndef __sys_wait_h__
#define __sys_wait_h__

#include "mscfakes.h"

#define	WNOHANG		1	/* Don't hang in wait. */
#define	WUNTRACED	2	/* Tell about stopped, untraced children. */
#define	WCONTINUED	4	/* Report a job control continued process. */
#define	_W_INT(w)	(*(int *)&(w))	/* Convert union wait to int. */
#define	WCOREFLAG	0200

#define	_WSTATUS(x)	(_W_INT(x) & 0177)
#define	_WSTOPPED	0177		/* _WSTATUS if process is stopped */
#define	WIFSTOPPED(x)	(_WSTATUS(x) == _WSTOPPED)
#define	WSTOPSIG(x)	(_W_INT(x) >> 8)
#define	WIFSIGNALED(x)	(_WSTATUS(x) != 0 && !WIFSTOPPED(x) && !WIFCONTINUED(x)) /* bird: made GLIBC tests happy. */
#define	WTERMSIG(x)	(_WSTATUS(x))
#define	WIFEXITED(x)	(_WSTATUS(x) == 0)
#define	WEXITSTATUS(x)	(_W_INT(x) >> 8)
#define	WIFCONTINUED(x)	(x == 0x13)	/* 0x13 == SIGCONT */
#define	WCOREDUMP(x)	(_W_INT(x) & WCOREFLAG)
#define	W_EXITCODE(ret, sig)	((ret) << 8 | (sig))
#define	W_STOPCODE(sig)		((sig) << 8 | _WSTOPPED)

#endif 
