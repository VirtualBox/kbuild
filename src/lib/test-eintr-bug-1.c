

/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
//#define _XOPEN_SOURCE
//#define _BSD_SOURCE
#include <sys/time.h>
#include <sys/stat.h>
#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <errno.h>


volatile unsigned long g_cInts = 0;

static void SigAlaramHandler(int iSig)
{
    g_cInts++;
    (void)iSig;
}


int main(int argc, char **argv)
{
    struct itimerval TmrVal;
    void (*rcSig)(int);
    int i;
    int rc;
    char szName[256];

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
    TmrVal.it_interval.tv_usec = TmrVal.it_value.tv_usec = 1;
    rc = setitimer(ITIMER_REAL, &TmrVal, NULL);
    if (rc != 0)
    {
        fprintf(stderr, "setitimer failed: %s\n", strerror(errno));
        return 1;
    }
    printf("interval %d.%06d\n", (int)TmrVal.it_interval.tv_sec, (int)TmrVal.it_interval.tv_usec);

    /*
     * Do path related stuff.
     */
    snprintf(szName, sizeof(szName), "%s/fooled/you", argv[0]);
    for (i = 0; i < 100*1000*1000; i++)
    {
        struct stat St;
        rc = stat(argv[0], &St);
        if (rc == 0)
            rc = stat(szName, &St);
        if (rc != 0 && errno == EINTR)
        {
            printf("iteration %d: stat: %s (%u)\n", i, strerror(errno), errno);
            break;
        }
        if ((i % 100000) == 0)
        {
            printf("."); 
            if ((i % 1000000) == 0)
                printf("[%u/%lu]", i, g_cInts);
            fflush(stdout);
        }
    }

    if (!rc)
        printf("No EINTR in %d iterations - system is working nicely!\n", i);

    TmrVal.it_interval.tv_sec  = TmrVal.it_value.tv_sec  = 0;
    TmrVal.it_interval.tv_usec = TmrVal.it_value.tv_usec = 0;
    setitimer(ITIMER_REAL, &TmrVal, NULL);

    return rc ? 1 : 0;
}

