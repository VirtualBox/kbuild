

/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
#define _XOPEN_SOURCE
#define _BSD_SOURCE
#include <sys/time.h>
#include <sys/stat.h>
#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <errno.h>


static void SigAlaramHandler(int iSig)
{
    /* ignore */
    (void)iSig;
}


int main(int argc, char **argv)
{
    struct itimerval TmrVal;
    void (*rcSig)(int);
    int i;
    int rc;

    /*
     * Set up the timer signal.
     */
    rcSig = bsd_signal(SIGALRM, SigAlaramHandler);
    if (rcSig == SIG_ERR)
    {
        fprintf(stderr, "bsd_signal failed: %s\n", strerror(errno));
        return 1;
    }
    if (argc == 2) /* testing... */
        siginterrupt(SIGALRM, 1);

    memset(&TmrVal, '\0', sizeof(TmrVal));
    TmrVal.it_interval.tv_sec  = TmrVal.it_value.tv_sec  = 0;
    TmrVal.it_interval.tv_usec = TmrVal.it_value.tv_usec = 1000;
    rc = setitimer(ITIMER_REAL, &TmrVal, NULL);
    if (rc != 0)
    {
        fprintf(stderr, "setitimer failed: %s\n", strerror(errno));
        return 1;
    }

    /*
     * Do path related stuff.
     */
    for (i = 0; i < 100*1000*1000; i++)
    {
        struct stat St;
        rc = stat(argv[0], &St);
        if (rc != 0)
        {
            printf("iteration %d: stat: %u\n", i, errno);
            break;
        }
    }

    if (rc)
        printf("No EINTR in %d iterations - system is working nicely!\n", i);

    TmrVal.it_interval.tv_sec  = TmrVal.it_value.tv_sec  = 0;
    TmrVal.it_interval.tv_usec = TmrVal.it_value.tv_usec = 0;
    setitimer(ITIMER_REAL, &TmrVal, NULL);

    return rc ? 1 : 0;
}

