

/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
#define _BSD_SOURCE
#define _GNU_SOURCE
#include <sys/time.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>


/*******************************************************************************
*   Global Variables                                                           *
*******************************************************************************/
/** The number of signals. */
static volatile long    g_cSigs = 0;
/** Number of signals received on threads other than the main one. */
static volatile long    g_cSigsOther = 0;
/** Whether to shutdown or not. */
static volatile int     g_fShutdown = 0;
/** The handle of the main thread. */
static pthread_t        g_hMainThread;


static void SigHandler(int iSig)
{
    g_cSigs++;
    if (pthread_self() != g_hMainThread)
        g_cSigsOther++;

    (void)iSig;
}


static void NanoSleep(unsigned long cNanoSecs)
{
    struct timespec Ts;
    Ts.tv_sec  = 0;
    Ts.tv_nsec = cNanoSecs;
    nanosleep(&Ts, NULL);
}


static void *ThreadProc(void *pvIgnored)
{
    int volatile i = 0;
    while (!g_fShutdown)
    {
//        NanoSleep(850);
        if (g_fShutdown)
            break;

        pthread_kill(g_hMainThread, SIGALRM);
        for (i = 6666; i > 0; i--) 
            /* nothing */;
    }
    return NULL;
}


int main(int argc, char **argv)
{
    void (*rcSig)(int);
    pthread_t hThread;
    char szName[1024];
    int i;
    int rc;

    /*
     * Set up the signal handlers.
     */
    rcSig = bsd_signal(SIGALRM, SigHandler);
    if (rcSig != SIG_ERR)
        rcSig = bsd_signal(SIGCHLD, SigHandler);
    if (rcSig == SIG_ERR)
    {
        fprintf(stderr, "bsd_signal failed: %s\n", strerror(errno));
        return 1;
    }
    if (argc == 2) /* testing... */
    {
        siginterrupt(SIGALRM, 1);
        siginterrupt(SIGCHLD, 1);
    }

    /*
     * Kick off a thread that will signal us like there was no tomorrow.
     */
    g_hMainThread = pthread_self();
    rc = pthread_create(&hThread, NULL, ThreadProc, NULL);
    if (rc != 0)
    {
        fprintf(stderr, "pthread_create failed: %s\n", strerror(rc));
        return 1;
    }

    /*
     * Do path related stuff.
     */
    snprintf(szName, sizeof(szName), "%s-test2", argv[0]);
    for (i = 0; i < 100*1000*1000; i++)
    {
        struct stat St;
        int fd;

        rc = stat(argv[0], &St);
        if (rc == 0 || errno != EINTR)
            rc = stat(szName, &St);
        if (errno == EINTR && rc != 0)
        {
            printf("iteration %d: stat: %u\n", i, errno);
            break;
        }
        
        fd = open(szName, O_CREAT | O_RDWR, 0666);
        if (errno == EINTR && fd < 0)
        {
            printf("iteration %d: open: %u\n", i, errno);
            break;
        }
        close(fd);
        rc = unlink(szName);
        if (errno == EINTR && rc != 0)
        {
            printf("iteration %d: unlink: %u\n", i, errno);
            break;
        }
        
        /* Show progress info */
        if ((i % 100000) == 0)
        {
            printf(".");
            if ((i % 1000000) == 0)
                printf("[%d/%ld/%ld]\n", i, g_cSigs, g_cSigsOther);
            fflush(stdout);
        }
    }

    g_fShutdown = 1;
    if (rc)
        printf("No EINTR in %d iterations - system is working nicely!\n", i);
    NanoSleep(10000000);

    return rc ? 1 : 0;
}

