/* $Id: mscfakes.c 1196 2007-10-06 08:49:22Z knut.osmundsen@oracle.com $ */
/** @file
 *
 * Fake Unix stuff for MSC.
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


#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <io.h>
#include <fcntl.h>
#include <sys/times.h>
#include "err.h"
#include "mscfakes.h"
#undef mkdir


int optind = 1;


char *dirname(char *path)
{
    /** @todo later */
    return path;
}


int link(const char *pszDst, const char *pszLink)
{
    errno = ENOSYS;
    err(1, "link() is not implemented on windows!");
    return -1;
}


int mkdir_msc(const char *path, mode_t mode)
{
    int rc = mkdir(path);
    if (rc)
    {
        int len = strlen(path);
        if (len > 0 && (path[len - 1] == '/' || path[len - 1] == '\\'))
        {
            char *str = strdup(path);
            while (len > 0 && (str[len - 1] == '/' || str[len - 1] == '\\'))
                str[--len] = '\0';
            rc = mkdir(str);
            free(str);
        }
    }
    return rc;
}


static int doname(char *pszX, char *pszEnd)
{
    static char s_szChars[] = "Xabcdefghijklmnopqrstuwvxyz1234567890";
    int rc = 0;
    do
    {
        char ch;

        pszEnd++;
        ch = *(strchr(s_szChars, *pszEnd) + 1);
        if (ch)
        {
            *pszEnd = ch;
            return 0;
        }
        *pszEnd = 'a';
    } while (pszEnd != pszX);
    return 1;
}


int mkstemp(char *temp)
{
    char *pszX = strchr(temp, 'X');
    char *pszEnd = strchr(pszX, '\0');
    int cTries = 1000;
    while (--cTries > 0)
    {
        int fd;
        if (doname(pszX, pszEnd))
            return -1;
        fd = open(temp, _O_EXCL | _O_CREAT | _O_BINARY | _O_RDWR, 0777);
        if (fd >= 0)
            return fd;
    }
    return -1;
}


int symlink(const char *pszDst, const char *pszLink)
{
    errno = ENOSYS;
    err(1, "symlink() is not implemented on windows!");
    return -1;
}


#if _MSC_VER < 1400
int snprintf(char *buf, size_t size, const char *fmt, ...)
{
    int cch;
    va_list args;
    va_start(args, fmt);
    cch = vsprintf(buf, fmt, args);
    va_end(args);
    return cch;
}
#endif


int utimes(const char *pszPath, const struct timeval *paTimes)
{
    /** @todo implement me! */
    return 0;
}


int writev(int fd, const struct iovec *vector, int count)
{
    int size = 0;
    int i;
    for (i = 0; i < count; i++)
    {
        int cb = write(fd, vector[i].iov_base, vector[i].iov_len);
        if (cb < 0)
            return -1;
        size += cb;
    }
    return size;
}


int fcntl(int fd, int iCmd, ...)
{
    fprintf(stderr, "fcntl(%d, %d,..)\n", fd, iCmd);
    return 0;
}

int ioctl(int fd, unsigned long iCmd, ...)
{
    fprintf(stderr, "ioctl(%d, %d,..)\n", fd, iCmd);
    return 0;
}

int tcsetpgrp(int fd, pid_t pgrp)
{
    fprintf(stderr, "tcgetpgrp(%d, %d)\n", fd, pgrp);
    return 0;
}

pid_t tcgetpgrp(int fd)
{
    fprintf(stderr, "tcgetpgrp(%d)\n", fd);
    return 0;
}

pid_t getpgrp(void)
{
    fprintf(stderr, "getpgrp\n");
    return _getpid();
}

uid_t getuid(void)
{
    fprintf(stderr, "getuid\n");
    return 0;
}

gid_t getgid(void)
{
    fprintf(stderr, "getgid\n");
    return 0;
}

gid_t getegid(void)
{
    fprintf(stderr, "getegid\n");
    return 0;
}

int setpgid(pid_t pid, pid_t pgid)
{
    fprintf(stderr, "setpgid(%d,%d)\n", pid, pgid);
    return 0;
}

pid_t getpgid(pid_t pid)
{
    fprintf(stderr, "getpgid(%d)\n", pid);
    return 0;
}

long sysconf(int name)
{
    fprintf(stderr, "sysconf(%d)\n", name);
    return 0;
}

clock_t times(struct tms *pBuf)
{
    struct timeval tv = {0};
    if (pBuf)
    {
        pBuf->tms_utime = clock();  /* clock () * HZ / CLOCKS_PER_SEC */
        pBuf->tms_stime = pBuf->tms_cutime = pBuf->tms_cstime = 0;
    }
/*
    gettimeofday(&tv, NULL); */
    fprintf(stderr, "times(%p)\n", pBuf);
    return CLK_TCK * tv.tv_sec + (CLK_TCK * tv.tv_usec) / 1000000;
}

struct passwd *getpwnam(const char *pszName)
{
    printf("getpwnam(%s)\n",  pszName);
    return NULL;
}

int setrlimit(int iResId, const struct rlimit *pLimit)
{
    printf("setrlimit(%d,  %p)\n", iResId, pLimit);
    return -1;
}

int getrlimit(int iResId, struct rlimit *pLimit)
{
    printf("getrlimit(%d,  %p)\n", iResId, pLimit);
    return -1;
}


pid_t fork(void)
{
    fprintf(stderr, "fork()\n");
    return -1;
}

pid_t wait3(int *piStatus, int fOptions, struct rusage *pRUsage)
{
    fprintf(stderr, "wait3()\n");
    return -1;
}


/* signal stuff */

int	sigprocmask(int iOperation, const sigset_t *pNew, sigset_t *pOld)
{
    fprintf(stderr, "sigprocmask(%d, %p, %p)\n", iOperation, pNew, pOld);
    return 0;
}

int	sigemptyset(sigset_t *pSet)
{
    pSet->__bitmap[0] = 0;
    return 0;
}

int siginterrupt(int iSignalNo, int fFlag)
{
    fprintf(stderr, "siginterrupt(%d, %#x)\n", iSignalNo, fFlag);
    return 0;
}

int sigaction(int iSignalNo, const struct sigaction *pSigAct, struct sigaction *pSigActOld)
{
    fprintf(stderr, "sigaction(%d, %p, %p)\n", iSignalNo, pSigAct, pSigActOld);
    return 0;
}

int kill(pid_t pid, int iSignalNo)
{
    fprintf(stderr, "kill(%d, %d)\n", pid, iSignalNo);
    return 0;
}

int killpg(pid_t pgrp, int iSignalNo)
{
    if (pgrp < 0)
    {
        errno = EINVAL;
        return -1;
    }
    return kill(-pgrp, iSignalNo);
}

const char * const  sys_siglist[NSIG] =
{
    "0",
    "1",
    "2",
    "3",
    "4",
    "5",
    "6",
    "7",
    "8",
    "9",
    "10",
    "11",
    "12",
    "13",
    "14",
    "15",
    "16",
    "17",
    "18",
    "19",
    "20",
    "21",
    "22"
};


/* */
