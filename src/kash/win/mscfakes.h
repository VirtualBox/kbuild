/* $Id: mscfakes.h 1207 2007-10-07 17:09:24Z knut.osmundsen@oracle.com $ */
/** @file
 *
 * Unix fakes for MSC.
 *
 * Copyright (c) 2005-2007 knut st. osmundsen <bird-kBuild-spam@anduin.net>
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with This program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#ifndef __mscfakes_h__
#define __mscfakes_h__
#ifdef _MSC_VER

#include "shtypes.h"
#define setmode setmode_msc
#include <sys/cdefs.h>
#include <io.h>
#include <direct.h>
#include <time.h>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <process.h>
#undef setmode
//#include "getopt.h"

#if !defined(__GNUC__) && !defined(__attribute__)
#define __attribute__(a)
#endif

#define S_ISDIR(m)  (((m) & _S_IFMT) == _S_IFDIR)
#define S_ISREG(m)  (((m) & _S_IFMT) == _S_IFREG)
#define S_ISLNK(m)  0
#define	S_IRWXU (_S_IREAD | _S_IWRITE | _S_IEXEC)
#define	S_IXUSR _S_IEXEC
#define	S_IWUSR _S_IWRITE
#define	S_IRUSR _S_IREAD
#define S_IRWXG 0000070
#define S_IRGRP	0000040
#define S_IWGRP	0000020
#define S_IXGRP 0000010
#define S_IRWXO 0000007
#define S_IROTH	0000004
#define S_IWOTH	0000002
#define S_IXOTH 0000001
#define	S_ISUID 0004000
#define	S_ISGID 0002000
#define ALLPERMS 0000777

#define PATH_MAX _MAX_PATH
#define MAXPATHLEN _MAX_PATH

#define EX_OK 0
#define EX_OSERR 1
#define EX_NOUSER 1
#define EX_USAGE 1

#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

#define F_OK 0
#define X_OK 1
#define W_OK 2
#define R_OK 4

#define EFTYPE EINVAL

#define _PATH_DEVNULL "/dev/null"

#define MAX(a,b) ((a) >= (b) ? (a) : (b))

#ifndef timerisset
struct timeval
{
    long tv_sec;
    long tv_usec;
};
#endif

struct iovec
{
    char *iov_base;
    size_t iov_len;
};


#define chown(path, uid, gid) 0         /** @todo implement fchmod! */
char *dirname(char *path);
#define fsync(fd)  0
#define fchown(fd,uid,gid) 0
#define fchmod(fd, mode) 0              /** @todo implement fchmod! */
#define lstat(path, s) stat(path, s)
#define lchmod(path, mod) chmod(path, mod)
#define lchown(path, uid, gid) chown(path, uid, gid)
#define lutimes(path, tvs) utimes(path, tvs)
int link(const char *pszDst, const char *pszLink);
int mkdir_msc(const char *path, mode_t mode);
#define mkdir(path, mode) mkdir_msc(path, mode)
#define mkfifo(path, mode) -1
#define mknod(path, mode, devno) -1
#define pipe(v) _pipe(v,0,0)
int mkstemp(char *temp);
#define readlink(link, buf, size) -1
#define reallocf(old, size) realloc(old, size)
#define strcasecmp stricmp
#define strncasecmp strnicmp
#if _MSC_VER < 1400
int snprintf(char *buf, size_t size, const char *fmt, ...);
#else
#define snprintf _snprintf
#endif
size_t strlcpy(char *, const char *, size_t);
int symlink(const char *pszDst, const char *pszLink);
int utimes(const char *pszPath, const struct timeval *paTimes);
int writev(int fd, const struct iovec *vector, int count);
#define	F_DUPFD	0
#define F_GETFD 1
#define F_SETFD 2
#define F_GETFL 3
#define F_SETFL 4
#define	FD_CLOEXEC 1
#define O_NONBLOCK 0 /// @todo
#define EWOULDBLOCK 512
int fcntl(int, int, ...);
int ioctl(int, unsigned long, ...);
pid_t tcgetpgrp(int fd);


/* signal hacks */
#include <signal.h>
typedef struct sigset
{
    unsigned long __bitmap[1];
} sigset_t;
typedef void __sighandler_t(int);
typedef	void __siginfohandler_t(int, struct __siginfo *, void *);
typedef __sighandler_t *sig_t; /** BSD 4.4 type. */
struct sigaction
{
    union
    {
        __siginfohandler_t *__sa_sigaction;
        __sighandler_t     *__sa_handler;
    }  __sigaction_u;
    sigset_t    sa_mask;
    int         sa_flags;
};
#define sa_handler      __sigaction_u.__sa_handler
#define sa_sigaction    __sigaction_u.__sa_sigaction

int	sigprocmask(int, const sigset_t *, sigset_t *);
#define SIG_BLOCK           1
#define SIG_UNBLOCK         2
#define SIG_SETMASK         3

#define SIGTTIN 19
#define SIGTSTP 18
#define SIGTTOU 17
#define SIGCONT 20
#define SIGPIPE 12
#define SIGQUIT 9
#define SIGHUP  5

extern const char *const sys_siglist[NSIG]; /* NSIG == 23 */

int	kill(pid_t, int);
int	sigaction(int, const struct sigaction *, struct sigaction *);
//int	sigaddset(sigset_t *, int);
//int	sigdelset(sigset_t *, int);
int	sigemptyset(sigset_t *);
//int	sigfillset(sigset_t *);
//int	sigismember(const sigset_t *, int);
//int	sigpending(sigset_t *);
//int	sigsuspend(const sigset_t *);
//int	sigwait(const sigset_t *, int *);
int	siginterrupt(int, int);
#undef setmode
void *  setmode(const char *p);
mode_t  getmode(const void *bbox, mode_t omode);

#endif /* _MSC_VER */
#endif

