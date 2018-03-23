/* $Id: winchildren.c 3182 2018-03-23 00:41:55Z knut.osmundsen@oracle.com $ */
/** @file
 * Child process creation and management for kmk.
 */

/*
 * Copyright (c) 2018 knut st. osmundsen <bird-kBuild-spamx@anduin.net>
 *
 * This file is part of kBuild.
 *
 * kBuild is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * kBuild is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with kBuild.  If not, see <http://www.gnu.org/licenses/>
 *
 */

/* No GNU coding style here atm, convert if upstreamed. */

/** @page pg_win_children   Windows child process creation and managment
 *
 * This new implementation aims at addressing the following:
 *
 *      1. Speed up process creation by doing the expensive CreateProcess call
 *         in a worker thread.
 *
 *      2. No 64 process limit imposed by WaitForMultipleObjects.
 *
 *      3. Better distribute jobs among processor groups.
 *
 *      4. Offloading more expensive kmkbuiltin operations to worker threads,
 *         making the main thread focus on managing child processes.
 *
 *      5. Output synchronization using reusable pipes [not yet implemented].
 *
 *
 * To be quite honest, the first item (CreateProcess expense) didn't occur to me
 * at first and was more of a sideeffect discovered along the way.  A test
 * rebuilding IPRT went from 4m52s to 3m19s on a 8 thread system.
 *
 * The 2nd and 3rd goals are related to newer build servers that have lots of
 * CPU threads and various Windows NT (aka NT OS/2 at the time) design choices
 * made in the late 1980ies.
 *
 * WaitForMultipleObjects does not support waiting for more than 64 objects,
 * unlike poll and select.  This is just something everyone ends up having to
 * work around in the end.
 *
 * Affinity masks are uintptr_t sized, so 64-bit hosts can only manage 64
 * processors and 32-bit only 32.  Workaround was introduced with Windows 7
 * (IIRC) and is called processor groups.  The CPU threads are grouped into 1 or
 * more groups of up to 64 processors.  Processes are generally scheduled to a
 * signle processor group at first, but threads may be changed to be scheduled
 * on different groups.  This code will try distribute children evenly among the
 * processor groups, using a very simple algorithm (see details in code).
 *
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include "../makeint.h"
#include "../job.h"
#include "../debug.h"
#include "../kmkbuiltin.h"
#include "winchildren.h"

#include <Windows.h>
#include <Winternl.h>
#include <assert.h>
#include <process.h>
#include <intrin.h>

#include "nt/nt_child_inject_standard_handles.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#define MKWINCHILD_MAX_PATH     1024

/** Checks the UTF-16 environment variable pointed to is the PATH. */
#define IS_PATH_ENV_VAR(a_cwcVar, a_pwszVar) \
    (   (a_cwcVar) >= 5 \
     &&  (a_pwszVar)[4] == L'=' \
     && ((a_pwszVar)[0] == L'P' || (a_pwszVar)[0] == L'p') \
     && ((a_pwszVar)[1] == L'A' || (a_pwszVar)[1] == L'a') \
     && ((a_pwszVar)[2] == L'T' || (a_pwszVar)[2] == L't') \
     && ((a_pwszVar)[3] == L'H' || (a_pwszVar)[3] == L'h') )


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Child process type.
 */
typedef enum WINCHILDTYPE
{
    WINCHILDTYPE_INVALID = 0,
    /** Normal child process. */
    WINCHILDTYPE_PROCESS,
#ifdef KMK
    /** kmkbuiltin command. */
    WINCHILDTYPE_BUILT_IN,
    /** kmkbuiltin_append result write out. */
    WINCHILDTYPE_APPEND,
    /** kSubmit job. */
    WINCHILDTYPE_SUBMIT,
    /** kmk_redirect job. */
    WINCHILDTYPE_REDIRECT,
#endif
    /** End of valid child types. */
    WINCHILDTYPE_END
} WINCHILDTYPE;


/** Pointer to a windows child process. */
typedef struct WINCHILD *PWINCHILD;
/**
 * Windows child process.
 */
typedef struct WINCHILD
{
    /** Magic / eyecatcher (WINCHILD_MAGIC). */
    ULONG                   uMagic;
    /** Child type. */
    WINCHILDTYPE            enmType;
    /** Pointer to the next child process. */
    PWINCHILD               pNext;
    /** The pid for this child. */
    pid_t                   pid;
    /** The make child structure associated with this child. */
    struct child           *pMkChild;

    /** The process exit code. */
    int                     iExitCode;
    /** Kill signal, in case we or someone else killed it. */
    int                     iSignal;
    /** Set if core was dumped. */
    int                     fCoreDumped;

    /** Type specific data. */
    union
    {
        /** Data for WINCHILDTYPE_PROCESS.   */
        struct
        {
            /** Argument vector (single allocation, strings following array). */
            char          **papszArgs;
            /** Length of the argument strings. */
            size_t          cbArgsStrings;
            /** Environment vector.  Only a copy if fEnvIsCopy is set. */
            char          **papszEnv;
            /** If we made a copy of the environment, this is the size of the
             * strings and terminator string (not in array).  This is done to
             * speed up conversion, since MultiByteToWideChar can handle '\0'. */
            size_t          cbEnvStrings;
            /** The make shell to use (copy). */
            char           *pszShell;
            /** Handle to use for standard out. */
            HANDLE          hStdOut;
            /** Handle to use for standard out. */
            HANDLE          hStdErr;
            /** Whether to close hStdOut after creating the process.  */
            BOOL            fCloseStdOut;
            /** Whether to close hStdErr after creating the process.  */
            BOOL            fCloseStdErr;

            /** Child process handle. */
            HANDLE          hProcess;
        } Process;

        /** Data for WINCHILDTYPE_BUILT_IN.   */
        struct
        {
            /** The built-in command. */
            PCKMKBUILTINENTRY pBuiltIn;
            /** Number of arguments. */
            int             cArgs;
            /** Argument vector (single allocation, strings following array). */
            char          **papszArgs;
            /** Environment vector.  Only a copy if fEnvIsCopy is set. */
            char          **papszEnv;
        } BuiltIn;

        /** Data for WINCHILDTYPE_APPEND.   */
        struct
        {
            /** The filename. */
            char           *pszFilename;
            /** How much to append. */
            size_t          cbAppend;
            /** What to append. */
            char           *pszAppend;
            /** Whether to truncate the file. */
            int             fTruncate;
        } Append;

        /** Data for WINCHILDTYPE_SUBMIT.   */
        struct
        {
            /** The event we're to wait on (hooked up to a pipe) */
            HANDLE          hEvent;
            /** Parameter for the cleanup callback. */
            void           *pvSubmitWorker;
        } Submit;

        /** Data for WINCHILDTYPE_REDIRECT.   */
        struct
        {
            /** Child process handle. */
            HANDLE          hProcess;
        } Redirect;
    } u;

} WINCHILD;
/** WINCHILD::uMagic value. */
#define WINCHILD_MAGIC      0xbabebabeU


/**
 * Data for a windows childcare worker thread.
 *
 * We use one worker thread per child, reusing the threads when possible.
 *
 * This setup helps avoid the 64-bit handle with the WaitForMultipleObject API.
 *
 * It also helps using all CPUs on systems with more than one CPU group
 * (typically systems with more than 64 CPU threads or/and multiple sockets, or
 * special configs).
 *
 * This helps facilitates using pipes for collecting output child rather
 * than temporary files.  Pipes doesn't involve NTFS and can easily be reused.
 *
 * Finally, kBuild specific, this allows running kmkbuiltin_xxxx commands in
 * threads.
 */
typedef struct WINCHILDCAREWORKER
{
    /** Magic / eyecatcher (WINCHILDCAREWORKER_MAGIC). */
    ULONG                   uMagic;
    /** The processor group for this worker. */
    unsigned int            iProcessorGroup;
    /** The thread ID. */
    unsigned int            tid;
    /** The thread handle. */
    HANDLE                  hThread;
    /** The event the thread is idling on. */
    HANDLE                  hEvtIdle;
    /** Pointer to the current child. */
    PWINCHILD volatile      pCurChild;
    /** List of children pending execution on this worker.
     * This is updated atomitically just like g_pTailCompletedChildren.  */
    PWINCHILD volatile      pTailTodoChildren;
    /** TRUE if idle, FALSE if not. */
    long volatile           fIdle;
} WINCHILDCAREWORKER;
/** Pointer to a childcare worker thread.   */
typedef WINCHILDCAREWORKER *PWINCHILDCAREWORKER;
/** WINCHILD::uMagic value. */
#define WINCHILDCAREWORKER_MAGIC    0xdad0dad0U


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** Whether it's initialized or not. */
static BOOL                 g_fInitialized = FALSE;
/** Set when we're shutting down everything. */
static BOOL volatile        g_fShutdown = FALSE;
/** Event used to wait for children. */
static HANDLE               g_hEvtWaitChildren = INVALID_HANDLE_VALUE;
/** Number of childcare workers currently in g_papChildCareworkers. */
static unsigned             g_cChildCareworkers = 0;
/** Maximum number of childcare workers in g_papChildCareworkers. */
static unsigned             g_cChildCareworkersMax = 0;
/** Pointer to childcare workers. */
static PWINCHILDCAREWORKER *g_papChildCareworkers = NULL;
/** The group index for the worker allocator.
 * This is ever increasing and must be modded by g_cProcessorGroups. */
static unsigned             g_idxProcessorGroupAllocator = 0;
/** The processor in group index for the worker allocator. */
static unsigned             g_idxProcessorInGroupAllocator = 0;
/** Number of processor groups in the system.   */
static unsigned             g_cProcessorGroups = 1;
/** Array detailing how many active processors there are in each group. */
static unsigned const      *g_pacProcessorsInGroup = &g_cProcessorGroups;
/** Kernel32!GetActiveProcessorGroupCount */
static WORD (WINAPI        *g_pfnGetActiveProcessorGroupCount)(VOID);
/** Kernel32!GetActiveProcessorCount */
static DWORD (WINAPI       *g_pfnGetActiveProcessorCount)(WORD);
/** Kernel32!SetThreadGroupAffinity */
static BOOL (WINAPI        *g_pfnSetThreadGroupAffinity)(HANDLE, CONST GROUP_AFFINITY *, GROUP_AFFINITY *);
/** NTDLL!NtQueryInformationProcess */
static NTSTATUS (NTAPI     *g_pfnNtQueryInformationProcess)(HANDLE, PROCESSINFOCLASS, PVOID, ULONG, PULONG);
/** Set if the windows host is 64-bit. */
static BOOL                 g_f64BitHost = (K_ARCH_BITS == 64);
/** Windows version info.
 * @note Putting this before the volatile stuff, hoping to keep it in a
 *       different cache line than the static bits above. */
static OSVERSIONINFOA       g_VersionInfo = { sizeof(g_VersionInfo), 4, 0, 1381, VER_PLATFORM_WIN32_NT, {0} };

/** Children that has been completed.
 * This is updated atomically, pushing completed children in LIFO fashion
 * (thus 'tail'), then hitting g_hEvtWaitChildren if head. */
static PWINCHILD volatile   g_pTailCompletedChildren = NULL;

/** Number of idle pending children.
 * This is updated before g_hEvtWaitChildren is signalled. */
static unsigned volatile    g_cPendingChildren = 0;

/** Number of idle childcare worker threads. */
static unsigned volatile    g_cIdleChildcareWorkers = 0;
/** Index of the last idle child careworker (just a hint). */
static unsigned volatile    g_idxLastChildcareWorker = 0;

#ifdef WITH_RW_LOCK
/** RW lock for serializing kmkbuiltin_redirect and CreateProcess. */
static SRWLOCK              g_RWLock;
#endif


#if K_ARCH_BITS == 32 && !defined(_InterlockedCompareExchangePointer)
/** _InterlockedCompareExchangePointer is missing? (VS2010) */
K_INLINE void *_InterlockedCompareExchangePointer(void * volatile *ppvDst, void *pvNew, void *pvOld)
{
    return (void *)_InterlockedCompareExchange((long volatile *)ppvDst, (intptr_t)pvNew, (intptr_t)pvOld);
}
#endif


/**
 * Initializes the windows child module.
 *
 * @param   cJobSlots           The number of job slots.
 */
void MkWinChildInit(unsigned int cJobSlots)
{
    HMODULE hmod;

    /*
     * Figure out how many childcare workers first.
     */
    static unsigned int const s_cMaxWorkers = 4096;
    unsigned cWorkers;
    if (cJobSlots >= 1 && cJobSlots < s_cMaxWorkers)
        cWorkers = cJobSlots;
    else
        cWorkers = s_cMaxWorkers;

    /*
     * Allocate the array and the child completed event object.
     */
    g_papChildCareworkers = (PWINCHILDCAREWORKER *)xcalloc(cWorkers * sizeof(g_papChildCareworkers[0]));
    g_cChildCareworkersMax = cWorkers;

    g_hEvtWaitChildren = CreateEvent(NULL, FALSE /*fManualReset*/, FALSE /*fInitialState*/, NULL /*pszName*/);
    if (!g_hEvtWaitChildren)
        fatal(NILF, INTSTR_LENGTH, _("MkWinChildInit: CreateEvent failed: %u"), GetLastError());

    /*
     * NTDLL imports that we need.
     */
    hmod = GetModuleHandleA("NTDLL.DLL");
    *(FARPROC *)&g_pfnNtQueryInformationProcess = GetProcAddress(hmod, "NtQueryInformationProcess");
    if (!g_pfnNtQueryInformationProcess)
        fatal(NILF, 0, _("MkWinChildInit: NtQueryInformationProcess not found"));

#if K_ARCH_BITS == 32
    /*
     * Initialize g_f64BitHost.
     */
    if (!IsWow64Process(GetCurrentProcess(), &g_f64BitHost))
        fatal(NILF, INTSTR_LENGTH, _("MkWinChildInit: IsWow64Process failed: %u"), GetLastError());
#elif K_ARCH_BITS == 64
    assert(g_f64BitHost);
#else
# error "K_ARCH_BITS is bad/missing"
#endif

    /*
     * Figure out how many processor groups there are.
     * For that we need to first figure the windows version.
     */
    if (!GetVersionExA(&g_VersionInfo))
    {
        DWORD uRawVer = GetVersion();
        g_VersionInfo.dwMajorVersion = uRawVer & 0xff;
        g_VersionInfo.dwMinorVersion = (uRawVer >>  8) &   0xff;
        g_VersionInfo.dwBuildNumber  = (uRawVer >> 16) & 0x7fff;
    }
    if (g_VersionInfo.dwMajorVersion >= 6)
    {
        hmod = GetModuleHandleA("KERNEL32.DLL");
        *(FARPROC *)&g_pfnGetActiveProcessorGroupCount = GetProcAddress(hmod, "GetActiveProcessorGroupCount");
        *(FARPROC *)&g_pfnGetActiveProcessorCount      = GetProcAddress(hmod, "GetActiveProcessorCount");
        *(FARPROC *)&g_pfnSetThreadGroupAffinity       = GetProcAddress(hmod, "SetThreadGroupAffinity");
        if (   g_pfnSetThreadGroupAffinity
            && g_pfnGetActiveProcessorCount
            && g_pfnGetActiveProcessorGroupCount)
        {
            unsigned int *pacProcessorsInGroup;
            unsigned      iGroup;
            g_cProcessorGroups = g_pfnGetActiveProcessorGroupCount();
            if (g_cProcessorGroups == 0)
                g_cProcessorGroups = 1;

            pacProcessorsInGroup = (unsigned int *)xmalloc(sizeof(g_pacProcessorsInGroup[0]) * g_cProcessorGroups);
            g_pacProcessorsInGroup = pacProcessorsInGroup;
            for (iGroup = 0; iGroup < g_cProcessorGroups; iGroup++)
                pacProcessorsInGroup[iGroup] = g_pfnGetActiveProcessorCount(iGroup);

            /* We shift the starting group with the make nesting level as part of
               our very simple distribution strategy. */
            g_idxProcessorGroupAllocator = makelevel;
        }
        else
        {
            g_pfnSetThreadGroupAffinity       = NULL;
            g_pfnGetActiveProcessorCount      = NULL;
            g_pfnGetActiveProcessorGroupCount = NULL;
        }
    }

#ifdef WITH_RW_LOCK
    /*
     * For serializing with standard file handle manipulation (kmkbuiltin_redirect).
     */
    InitializeSRWLock(&g_RWLock);
#endif

    /*
     * This is dead code that was thought to fix a problem observed doing
     * `tcc.exe /c "kmk |& tee bld.log"` and leading to a crash in cl.exe
     * when spawned with fInheritHandles = FALSE, see hStdErr=NULL in the
     * child.  However, it turns out this was probably caused by not clearing
     * the CRT file descriptor and handle table in the startup info.
     * Leaving the code here in case it comes in handy after all.
     */
#if 0
    {
        struct
        {
            DWORD  uStdHandle;
            HANDLE hHandle;
        } aHandles[3] = { { STD_INPUT_HANDLE, NULL }, { STD_OUTPUT_HANDLE, NULL }, { STD_ERROR_HANDLE, NULL } };
        int i;

        for (i = 0; i < 3; i++)
            aHandles[i].hHandle = GetStdHandle(aHandles[i].uStdHandle);

        for (i = 0; i < 3; i++)
            if (   aHandles[i].hHandle == NULL
                || aHandles[i].hHandle == INVALID_HANDLE_VALUE)
            {
                int fd = open("nul", _O_RDWR);
                if (fd >= 0)
                {
                    if (_dup2(fd, i) >= 0)
                    {
                        assert((HANDLE)_get_osfhandle(i) != aHandles[i].hHandle);
                        assert((HANDLE)_get_osfhandle(i) == GetStdHandle(aHandles[i].uStdHandle));
                    }
                    else
                        ONNNS(fatal, NILF, "_dup2(%d('nul'), %d) failed: %u (%s)", fd, i, errno, strerror(errno));
                    if (fd != i)
                        close(fd);
                }
                else
                    ONNS(fatal, NILF, "open(nul,RW) failed: %u (%s)", i, errno, strerror(errno));
            }
            else
            {
                int j;
                for (j = i + 1; j < 3; j++)
                    if (aHandles[j].hHandle == aHandles[i].hHandle)
                    {
                        int fd = _dup(j);
                        if (fd >= 0)
                        {
                            if (_dup2(fd, j) >= 0)
                            {
                                aHandles[j].hHandle = (HANDLE)_get_osfhandle(j);
                                assert(aHandles[j].hHandle != aHandles[i].hHandle);
                                assert(aHandles[j].hHandle == GetStdHandle(aHandles[j].uStdHandle));
                            }
                            else
                                ONNNS(fatal, NILF, "_dup2(%d, %d) failed: %u (%s)", fd, j, errno, strerror(errno));
                            if (fd != j)
                                close(fd);
                        }
                        else
                            ONNS(fatal, NILF, "_dup(%d) failed: %u (%s)", j, errno, strerror(errno));
                    }
            }
    }
#endif
}

/**
 * Used by mkWinChildcareWorkerThread() and MkWinChildWait() to get the head
 * child from a lifo (g_pTailCompletedChildren, pTailTodoChildren).
 *
 * @returns Head child.
 * @param   ppTail          Pointer to the child variable.
 * @param   pChild          Tail child.
 */
static PWINCHILD mkWinChildDequeFromLifo(PWINCHILD volatile *ppTail, PWINCHILD pChild)
{
    if (pChild->pNext)
    {
        PWINCHILD pPrev;
        do
        {
            pPrev = pChild;
            pChild = pChild->pNext;
        } while (pChild->pNext);
        pPrev->pNext = NULL;
    }
    else
    {
        PWINCHILD const pWantedChild = pChild;
        pChild = _InterlockedCompareExchangePointer(ppTail, NULL, pWantedChild);
        if (pChild != pWantedChild)
        {
            PWINCHILD pPrev;
            do
            {
                pPrev = pChild;
                pChild = pChild->pNext;
            } while (pChild->pNext);
            pPrev->pNext = NULL;
            assert(pChild == pWantedChild);
        }
    }
    return pChild;
}

/**
 * Duplicates the given UTF-16 string.
 *
 * @returns 0
 * @param   pwszSrc             The UTF-16 string to duplicate.
 * @param   cwcSrc              Length, may include the terminator.
 * @param   ppwszDst            Where to return the duplicate.
 */
static int mkWinChildDuplicateUtf16String(const WCHAR *pwszSrc, size_t cwcSrc, WCHAR **ppwszDst)
{
    size_t cb = sizeof(WCHAR) * cwcSrc;
    if (cwcSrc > 0 && pwszSrc[cwcSrc - 1] == L'\0')
        *ppwszDst = (WCHAR *)memcpy(xmalloc(cb), pwszSrc, cb);
    else
    {
        WCHAR *pwszDst = (WCHAR *)xmalloc(cb + sizeof(WCHAR));
        memcpy(pwszDst, pwszSrc, cb);
        pwszDst[cwcSrc] = L'\0';
        *ppwszDst = pwszDst;
    }
    return 0;
}

/**
 * Commmon worker for waiting on a child process and retrieving the exit code.
 *
 * @param   pWorker             The worker.
 * @param   pChild              The child.
 * @param   hProcess            The process handle.
 */
static void mkWinChildcareWorkerWaitForProcess(PWINCHILDCAREWORKER pWorker, PWINCHILD pChild, HANDLE hProcess)
{
    for (;;)
    {
        DWORD dwExitCode = -42;
        DWORD dwStatus = WaitForSingleObject(hProcess, INFINITE);
        assert(dwStatus != WAIT_FAILED);
        if (dwStatus == WAIT_OBJECT_0)
        {
            DWORD dwExitCode = -42;
            if (GetExitCodeProcess(hProcess, &dwExitCode))
            {
                pChild->iExitCode = (int)dwExitCode;
                return;
            }
        }
        else if (   dwStatus == WAIT_IO_COMPLETION
                 || dwStatus == WAIT_TIMEOUT /* whatever */)
            continue; /* however unlikely, these aren't fatal. */

        /* Something failed. */
        pChild->iExitCode = GetLastError();
        if (pChild->iExitCode == 0)
            pChild->iExitCode = -4242;
        return;
    }
}


/**
 * Closes standard handles that need closing before destruction.
 *
 * @param   pChild          The child (WINCHILDTYPE_PROCESS).
 */
static void mkWinChildcareWorkerCloseStandardHandles(PWINCHILD pChild)
{
    if (   pChild->u.Process.fCloseStdOut
        && pChild->u.Process.hStdOut != INVALID_HANDLE_VALUE)
    {
        CloseHandle(pChild->u.Process.hStdOut);
        pChild->u.Process.hStdOut      = INVALID_HANDLE_VALUE;
        pChild->u.Process.fCloseStdOut = FALSE;
    }
    if (   pChild->u.Process.fCloseStdErr
        && pChild->u.Process.hStdErr != INVALID_HANDLE_VALUE)
    {
        CloseHandle(pChild->u.Process.hStdErr);
        pChild->u.Process.hStdErr      = INVALID_HANDLE_VALUE;
        pChild->u.Process.fCloseStdErr = FALSE;
    }
}


/**
 * Does the actual process creation given.
 *
 * @returns 0 if there is anything to wait on, otherwise non-zero windows error.
 * @param   pWorker             The childcare worker.
 * @param   pChild              The child.
 * @param   pwszImageName       The image path.
 * @param   pwszCommandLine     The command line.
 * @param   pwszzEnvironment    The enviornment block.
 */
static int mkWinChildcareWorkerCreateProcess(PWINCHILDCAREWORKER pWorker, PWINCHILD pChild, WCHAR const *pwszImageName,
                                             WCHAR const *pwszCommandLine, WCHAR const *pwszzEnvironment)
{
    PROCESS_INFORMATION ProcInfo;
    STARTUPINFOW        StartupInfo;
    DWORD               fFlags       = CREATE_UNICODE_ENVIRONMENT;
    BOOL const          fHaveHandles = pChild->u.Process.hStdErr != INVALID_HANDLE_VALUE
                                    || pChild->u.Process.hStdOut != INVALID_HANDLE_VALUE;
    BOOL                fRet;
    DWORD               dwErr;
#ifdef KMK
    extern int          process_priority;
#endif

    /*
     * Populate startup info.
     *
     * Turns out we can get away without passing TRUE for the inherit handles
     * parameter to CreateProcess when we're not using STARTF_USESTDHANDLES.
     * At least on NT, which is all worth caring about at this point + context IMO.
     *
     * Not inherting the handles is a good thing because it means we won't
     * accidentally end up with a pipe handle or such intended for a different
     * child process, potentially causing the EOF/HUP event to be delayed.
     *
     * Since the present handle inhertiance requirements only involves standard
     * output and error, we'll never set the inherit handles flag and instead
     * do manual handle duplication and planting.
     */
    memset(&StartupInfo, 0, sizeof(StartupInfo));
    StartupInfo.cb = sizeof(StartupInfo);
    GetStartupInfoW(&StartupInfo);
    StartupInfo.lpReserved2 = 0; /* No CRT file handle + descriptor info possible, sorry. */
    StartupInfo.cbReserved2 = 0;
    if (!fHaveHandles)
        StartupInfo.dwFlags &= ~STARTF_USESTDHANDLES;
    else
    {
        fFlags |= CREATE_SUSPENDED;
        StartupInfo.dwFlags &= ~STARTF_USESTDHANDLES;
    }

    /*
     * Flags.
     */
#ifdef KMK
    switch (process_priority)
    {
        case 1: fFlags |= CREATE_SUSPENDED | IDLE_PRIORITY_CLASS; break;
        case 2: fFlags |= CREATE_SUSPENDED | BELOW_NORMAL_PRIORITY_CLASS; break;
        case 3: fFlags |= CREATE_SUSPENDED | NORMAL_PRIORITY_CLASS; break;
        case 4: fFlags |= CREATE_SUSPENDED | HIGH_PRIORITY_CLASS; break;
        case 5: fFlags |= CREATE_SUSPENDED | REALTIME_PRIORITY_CLASS; break;
    }
#endif
    if (g_cProcessorGroups > 1)
        fFlags |= CREATE_SUSPENDED;

    /*
     * Try create the process.
     */
    DB(DB_JOBS, ("CreateProcessW(%ls, %ls,,, TRUE, %#x...)\n", pwszImageName, pwszCommandLine, fFlags));
    memset(&ProcInfo, 0, sizeof(ProcInfo));
#ifdef WITH_RW_LOCK
    AcquireSRWLockShared(&g_RWLock);
#endif

    fRet = CreateProcessW((WCHAR *)pwszImageName, (WCHAR *)pwszCommandLine, NULL /*pProcSecAttr*/, NULL /*pThreadSecAttr*/,
                          FALSE /*fInheritHandles*/, fFlags, (WCHAR *)pwszzEnvironment, NULL /*pwsz*/, &StartupInfo, &ProcInfo);
    dwErr = GetLastError();

#ifdef WITH_RW_LOCK
    ReleaseSRWLockShared(&g_RWLock);
#endif
    if (fRet)
        pChild->u.Process.hProcess = ProcInfo.hProcess;
    else
    {
        fprintf(stderr, "CreateProcess(%ls) failed: %u\n", pwszImageName, dwErr);
        return pChild->iExitCode = (int)dwErr;
    }

    /*
     * If the child is suspended, we've got some adjustment work to be done.
     */
    dwErr = ERROR_SUCCESS;
    if (fFlags & CREATE_SUSPENDED)
    {
        /*
         * First do handle inhertiance as that's the most complicated.
         */
        if (fHaveHandles)
        {
            char    szErrMsg[128];
            BOOL    afReplace[3] =
            { FALSE, pChild->u.Process.hStdOut != INVALID_HANDLE_VALUE, pChild->u.Process.hStdErr != INVALID_HANDLE_VALUE };
            HANDLE  ahChild[3] =
            { NULL,  pChild->u.Process.hStdOut,                         pChild->u.Process.hStdErr  };

            dwErr = nt_child_inject_standard_handles(ProcInfo.hProcess, afReplace, ahChild, szErrMsg, sizeof(szErrMsg));
            if (dwErr != 0)
                fprintf(stderr, "%s\n", szErrMsg);
        }

        /*
         * Assign processor group (ignore failure).
         */
        if (g_cProcessorGroups > 1)
        {
            GROUP_AFFINITY Affinity = { ~(ULONG_PTR)0, pWorker->iProcessorGroup, { 0, 0, 0 } };
            fRet = g_pfnSetThreadGroupAffinity(ProcInfo.hThread, &Affinity, NULL);
            assert(fRet);
        }

#ifdef KMK
        /*
         * Set priority (ignore failure).
         */
        switch (process_priority)
        {
            case 1: fRet = SetThreadPriority(ProcInfo.hThread, THREAD_PRIORITY_IDLE); break;
            case 2: fRet = SetThreadPriority(ProcInfo.hThread, THREAD_PRIORITY_BELOW_NORMAL); break;
            case 3: fRet = SetThreadPriority(ProcInfo.hThread, THREAD_PRIORITY_NORMAL); break;
            case 4: fRet = SetThreadPriority(ProcInfo.hThread, THREAD_PRIORITY_HIGHEST); break;
            case 5: fRet = SetThreadPriority(ProcInfo.hThread, THREAD_PRIORITY_TIME_CRITICAL); break;
            default: fRet = TRUE;
        }
        assert(fRet);
#endif

        /*
         * Resume the thread if the adjustments succeeded, otherwise kill it.
         */
        if (dwErr == ERROR_SUCCESS)
        {
            fRet = ResumeThread(ProcInfo.hThread);
            assert(fRet);
            if (!fRet)
            {
                dwErr = GetLastError();
                fprintf(stderr, "ResumeThread failed on child process: %u\n", dwErr);
            }
        }
        if (dwErr != ERROR_SUCCESS)
            TerminateProcess(ProcInfo.hProcess, dwErr);
    }

    /*
     * Close unnecessary handles.
     */
    mkWinChildcareWorkerCloseStandardHandles(pChild);
    CloseHandle(ProcInfo.hThread);
    return 0;
}


#define MKWCCWCMD_F_CYGWIN_SHELL    1
#define MKWCCWCMD_F_MKS_SHELL       2
#define MKWCCWCMD_F_HAVE_SH         4
#define MKWCCWCMD_F_HAVE_KASH_C     8 /**< kmk_ash -c "..." */

static int mkWinChildcareWorkerConvertCommandline(char **papszArgs, unsigned fFlags, WCHAR **ppwszCommandLine)
{
    struct ARGINFO
    {
        size_t   cchSrc;
        size_t   cwcDst;           /**< converted size w/o terminator. */
        size_t   cwcDstExtra : 24; /**< Only set with fSlowly. */
        size_t   fSlowly     : 1;
        size_t   fQuoteIt    : 1;
        size_t   fEndSlashes : 1; /**< if escapes needed for trailing backslashes. */
        size_t   fExtraSpace : 1; /**< if kash -c "" needs an extra space before the quote. */
    }     *paArgInfo;
    size_t cArgs;
    size_t i;
    size_t cwcNeeded;
    WCHAR *pwszDst;
    WCHAR *pwszCmdLine;

    /*
     * Count them first so we can allocate an info array of the stack.
     */
    cArgs = 0;
    while (papszArgs[cArgs] != NULL)
        cArgs++;
    paArgInfo = (struct ARGINFO *)alloca(sizeof(paArgInfo[0]) * cArgs);

    /*
     * Preprocess them and calculate the exact command line length.
     */
    cwcNeeded = 1;
    for (i = 0; i < cArgs; i++)
    {
        char  *pszSrc = papszArgs[i];
        size_t cchSrc = strlen(pszSrc);
        paArgInfo[i].cchSrc = cchSrc;
        if (cchSrc == 0)
        {
            /* empty needs quoting. */
            paArgInfo[i].cwcDst      = 2;
            paArgInfo[i].cwcDstExtra = 0;
            paArgInfo[i].fSlowly     = 0;
            paArgInfo[i].fQuoteIt    = 1;
            paArgInfo[i].fExtraSpace = 0;
            paArgInfo[i].fEndSlashes = 0;
        }
        else
        {
            const char *pszSpace  = memchr(pszSrc, ' ', cchSrc);
            const char *pszTab    = memchr(pszSrc, '\t', cchSrc);
            const char *pszDQuote = memchr(pszSrc, '"', cchSrc);
            const char *pszEscape = memchr(pszSrc, '\\', cchSrc);
            int cwcDst = MultiByteToWideChar(CP_ACP, 0 /*fFlags*/, pszSrc, cchSrc + 1, NULL, 0);
            if (cwcDst >= 0)
                --cwcDst;
            else
            {
                DWORD dwErr = GetLastError();
                fprintf(stderr, _("MultiByteToWideChar failed to convert argv[%u] (%s): %u\n"), i, pszSrc, dwErr);
                return dwErr;
            }
#if 0
            if (!pszSpace && !pszTab && !pszDQuote && !pszEscape)
            {
                /* no special handling needed. */
                paArgInfo[i].cwcDst      = cwcDst;
                paArgInfo[i].cwcDstExtra = 0;
                paArgInfo[i].fSlowly     = 0;
                paArgInfo[i].fQuoteIt    = 0;
                paArgInfo[i].fExtraSpace = 0;
                paArgInfo[i].fEndSlashes = 0;
            }
            else if (!pszDQuote && !pszEscape)
            {
                /* Just double quote it. */
                paArgInfo[i].cwcDst      = cwcDst + 2;
                paArgInfo[i].cwcDstExtra = 0;
                paArgInfo[i].fSlowly     = 0;
                paArgInfo[i].fQuoteIt    = 1;
                paArgInfo[i].fExtraSpace = 0;
                paArgInfo[i].fEndSlashes = 0;
            }
            else
#endif
            {
                /* Complicated, need to scan the string to figure out what to do. */
                size_t cwcDstExtra;
                int cBackslashes;
                char ch;

                paArgInfo[i].fQuoteIt    = 0;
                paArgInfo[i].fSlowly     = 1;
                paArgInfo[i].fExtraSpace = 0;
                paArgInfo[i].fEndSlashes = 0;

                cwcDstExtra  = 0;
                cBackslashes = 0;
                while ((ch = *pszSrc++) != '\0')
                {
                    switch (ch)
                    {
                        default:
                            cBackslashes = 0;
                            break;

                        case '\\':
                            cBackslashes++;
                            break;

                        case '"':
                            if (fFlags & (MKWCCWCMD_F_CYGWIN_SHELL | MKWCCWCMD_F_MKS_SHELL))
                                cwcDstExtra += 1;
                            else
                                cwcDstExtra += 1 + cBackslashes;
                            break;

                        case ' ':
                        case '\t':
                            if (!paArgInfo[i].fQuoteIt)
                            {
                                paArgInfo[i].fQuoteIt = 1;
                                cwcDstExtra += 2;
                            }
                            cBackslashes = 0;
                            break;
                    }
                }

                if (   cBackslashes > 0
                    && paArgInfo[i].fQuoteIt
                    && !(fFlags & (MKWCCWCMD_F_CYGWIN_SHELL | MKWCCWCMD_F_MKS_SHELL)))
                {
                    cwcDstExtra += cBackslashes;
                    paArgInfo[i].fEndSlashes = 1;
                }

                paArgInfo[i].cwcDst      = cwcDst + cwcDstExtra;
                paArgInfo[i].cwcDstExtra = cwcDstExtra;
            }
        }

        if (   (fFlags & MKWCCWCMD_F_HAVE_KASH_C)
            && paArgInfo[i].fQuoteIt)
        {
            paArgInfo[i].fExtraSpace = 1;
            paArgInfo[i].cwcDst++;
            paArgInfo[i].cwcDstExtra++;
        }

        cwcNeeded += (i != 0) + paArgInfo[i].cwcDst;
    }

    /*
     * Allocate the result buffer and do the actual conversion.
     */
    pwszDst = pwszCmdLine = (WCHAR *)xmalloc(sizeof(WCHAR) * cwcNeeded);
    for (i = 0; i < cArgs; i++)
    {
        char  *pszSrc = papszArgs[i];
        size_t cwcDst = paArgInfo[i].cwcDst;

        if (i != 0)
            *pwszDst++ = L' ';

        if (paArgInfo[i].fQuoteIt)
        {
            *pwszDst++ = L'"';
            cwcDst -= 2;
        }

        if (!paArgInfo[i].fSlowly)
        {
            int cwcDst2 = MultiByteToWideChar(CP_ACP, 0 /*fFlags*/, pszSrc, paArgInfo[i].cchSrc, pwszDst, cwcDst + 1);
            assert(cwcDst2 >= 0);
            pwszDst += cwcDst;
        }
        else
        {
            /* Do the conversion into the end of the output buffer, then move
               it up to where it should be char by char.  */
            size_t          cBackslashes;
            size_t          cwcLeft     = paArgInfo[i].cwcDst - paArgInfo[i].cwcDstExtra;
            WCHAR volatile *pwchSlowSrc = pwszDst + paArgInfo[i].cwcDstExtra;
            WCHAR volatile *pwchSlowDst = pwszDst;
            int cwcDst2 = MultiByteToWideChar(CP_ACP, 0 /*fFlags*/, pszSrc, paArgInfo[i].cchSrc,
                                              (WCHAR *)pwchSlowSrc, cwcLeft + 1);
            assert(cwcDst2 >= 0);

            cBackslashes = 0;
            while (cwcLeft-- > 0)
            {
                WCHAR wcSrc = *pwchSlowSrc++;
                if (wcSrc != L'\\' && wcSrc != L'"')
                    cBackslashes = 0;
                else if (wcSrc == L'\\')
                    cBackslashes++;
                else if (   (fFlags & (MKWCCWCMD_F_CYGWIN_SHELL | MKWCCWCMD_F_HAVE_SH))
                         ==           (MKWCCWCMD_F_CYGWIN_SHELL | MKWCCWCMD_F_HAVE_SH))
                    *pwchSlowDst++ = L'"'; /* cygwin: '"' instead of '\\', no escaped slashes. */
                else
                {
                    if (!(fFlags & (MKWCCWCMD_F_CYGWIN_SHELL | MKWCCWCMD_F_MKS_SHELL)))
                        cBackslashes = 1;
                    while (cBackslashes-- > 0)
                        *pwchSlowDst++ = L'\\';
                }
                *pwchSlowDst++ = wcSrc;
            }

            if (paArgInfo[i].fEndSlashes)
                while (cBackslashes-- > 0)
                    *pwchSlowDst++ = L'\\';

            pwszDst += cwcDst;
            assert(pwszDst == (WCHAR *)pwchSlowDst);
        }

        if (paArgInfo[i].fExtraSpace)
            *pwszDst++ = L' ';
        if (paArgInfo[i].fQuoteIt)
            *pwszDst++ = L'"';
    }
    *pwszDst = L'\0';
    *ppwszCommandLine = pwszCmdLine;
    return 0;
}

static int mkWinChildcareWorkerConvertCommandlineWithShell(const WCHAR *pwszShell, char **papszArgs, WCHAR **ppwszCommandLine)
{
    fprintf(stderr, "%s: not found!\n", papszArgs[0]);
    return ERROR_FILE_NOT_FOUND;
}

/**
 * Searches the environment block for the PATH variable.
 *
 * @returns Pointer to the path in the block or "." in pwszPathFallback.
 * @param   pwszzEnv            The UTF-16 environment block to search.
 * @param   pwszPathFallback    Fallback.
 */
static const WCHAR *mkWinChildcareWorkerFindPathValue(const WCHAR *pwszzEnv, WCHAR pwszPathFallback[4])
{
    while (*pwszzEnv)
    {
        size_t cwcVar = wcslen(pwszzEnv);
        if (!IS_PATH_ENV_VAR(cwcVar, pwszzEnv))
            pwszzEnv += cwcVar + 1;
        else if (cwcVar > 5)
            return &pwszzEnv[5];
        else
            break;
    }
    pwszPathFallback[0] = L'.';
    pwszPathFallback[1] = L'\0';
    return pwszPathFallback;
}

/**
 * Checks if we need to had this executable file to the shell.
 *
 * @returns TRUE if it's shell fooder, FALSE if we think windows can handle it.
 * @param   hFile               Handle to the file in question
 */
static BOOL mkWinChildcareWorkerCheckIfNeedShell(HANDLE hFile)
{
    /*
     * Read the first 512 bytes and check for an executable image header.
     */
    union
    {
        DWORD dwSignature;
        WORD  wSignature;
        BYTE  ab[128];
    } uBuf;
    DWORD cbRead;
    uBuf.dwSignature = 0;
    if (   ReadFile(hFile, &uBuf, sizeof(uBuf), &cbRead, NULL /*pOverlapped*/)
        && cbRead == sizeof(uBuf))
    {
        if (uBuf.wSignature == IMAGE_DOS_SIGNATURE)
            return FALSE;
        if (uBuf.dwSignature == IMAGE_NT_SIGNATURE)
            return FALSE;
        if (   uBuf.wSignature == IMAGE_OS2_SIGNATURE    /* NE */
            || uBuf.wSignature == 0x5d4c                 /* LX */
            || uBuf.wSignature == IMAGE_OS2_SIGNATURE_LE /* LE */)
            return FALSE;
    }
    return TRUE;
}


/**
 * Tries to locate the image file, searching the path and maybe falling back on
 * the shell in case it knows more (think cygwin with its own view of the file
 * system).
 *
 * This will also check for shell script, falling back on the shell too to
 * handle those.
 *
 * @returns 0 on success, windows error code on failure.
 * @param   pszArg0         The first argument.
 * @param   pwszSearchPath  In case mkWinChildcareWorkerConvertEnvironment
 *                          had a chance of locating the search path already.
 * @param   pwszzEnv        The environment block, in case we need to look for
 *                          the path.
 * @param   pszShell        The shell.
 * @param   ppwszImagePath  Where to return the pointer to the image path.  This
 *                          could be the shell.
 * @param   pfNeedShell     Where to return shell vs direct execution indicator.
 */
static int mkWinChildcareWorkerFindImage(char const *pszArg0, WCHAR *pwszSearchPath, WCHAR const *pwszzEnv,
                                         const char *pszShell, WCHAR **ppwszImagePath, BOOL *pfNeedShell)
{
    /** @todo Slap a cache on this code. We usually end up executing the same
     *        stuff over and over again (e.g. compilers, linkers, etc).
     *        Hitting the file system is slow on windows. */

    /*
     * Convert pszArg0 to unicode so we can work directly on that.
     */
    WCHAR     wszArg0[MKWINCHILD_MAX_PATH + 4]; /* +4 for painless '.exe' appending */
    DWORD     dwErr;
    size_t    cbArg0  = strlen(pszArg0) + 1;
    int const cwcArg0 = MultiByteToWideChar(CP_ACP, 0 /*fFlags*/, pszArg0, cbArg0, wszArg0, MKWINCHILD_MAX_PATH);
    if (cwcArg0 > 0)
    {
        HANDLE hFile = INVALID_HANDLE_VALUE;
        WCHAR  wszPathBuf[MKWINCHILD_MAX_PATH + 4]; /* +4 for painless '.exe' appending */
        int    cwc;

        /*
         * If there isn't an .exe suffix, we may have to add one.
         * Also we ASSUME that .exe suffixes means no hash bang detection needed.
         */
        int const fHasExeSuffix = cwcArg0 > CSTRLEN(".exe")
                               &&  wszArg0[cwcArg0 - 4] == '.'
                               && (wszArg0[cwcArg0 - 3] == L'e' || wszArg0[cwcArg0 - 3] == L'E')
                               && (wszArg0[cwcArg0 - 2] == L'x' || wszArg0[cwcArg0 - 2] == L'X')
                               && (wszArg0[cwcArg0 - 1] == L'e' || wszArg0[cwcArg0 - 1] == L'E');

        /*
         * If there isn't any path specified, we need to search the PATH env.var.
         */
        int const fHasPath =  wszArg0[1] == L':'
                           || wszArg0[0] == L'\\'
                           || wszArg0[0] == L'/'
                           || wmemchr(wszArg0, L'/', cwcArg0)
                           || wmemchr(wszArg0, L'\\', cwcArg0);

        /* Before we do anything, flip UNIX slashes to DOS ones. */
        WCHAR *pwc = wszArg0;
        while ((pwc = wcschr(pwc, L'/')) != NULL)
            *pwc++ = L'\\';

        /* Don't need to set this all the time... */
        *pfNeedShell = FALSE;

        /*
         * If any kind of path is specified in arg0, we will not search the
         * PATH env.var and can limit ourselves to maybe slapping a .exe on to it.
         */
        if (fHasPath)
        {
            /*
             * If relative to a CWD, turn it into an absolute one.
             */
            unsigned  cwcPath  = cwcArg0;
            WCHAR    *pwszPath = wszArg0;
            if (   *pwszPath != L'\\'
                && (pwszPath[1] != ':' || pwszPath[2] != L'\\') )
            {
                DWORD cwcAbsPath = GetFullPathNameW(wszArg0, MKWINCHILD_MAX_PATH, wszPathBuf, NULL);
                if (cwcAbsPath > 0)
                {
                    cwcAbsPath = cwcPath + 1; /* include terminator, like MultiByteToWideChar does. */
                    pwszPath = wszPathBuf;
                }
            }

            /*
             * If there is an exectuable path, we only need to check that it exists.
             */
            if (fHasExeSuffix)
            {
                DWORD dwAttribs = GetFileAttributesW(pwszPath);
                if (dwAttribs != INVALID_FILE_ATTRIBUTES)
                    return mkWinChildDuplicateUtf16String(pwszPath, cwcPath + 4, ppwszImagePath);
            }
            else
            {
                /*
                 * No suffix, so try open it first to see if it's shell fooder.
                 * Otherwise, append a .exe suffix and check if it exists.
                 */
                hFile = CreateFileW(pwszPath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE | FILE_SHARE_WRITE,
                                    NULL /*pSecAttr*/, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
                if (hFile != INVALID_HANDLE_VALUE)
                {
                    *pfNeedShell = mkWinChildcareWorkerCheckIfNeedShell(hFile);
                    CloseHandle(hFile);
                    if (!*pfNeedShell)
                        return mkWinChildDuplicateUtf16String(pwszPath, cwcPath, ppwszImagePath);
                }
                /* Append the .exe suffix and check if it exists. */
                else
                {
                    DWORD dwAttribs;
                    pwszPath[cwcPath - 1] = L'.';
                    pwszPath[cwcPath    ] = L'e';
                    pwszPath[cwcPath + 1] = L'x';
                    pwszPath[cwcPath + 2] = L'e';
                    pwszPath[cwcPath + 3] = L'\0';
                    dwAttribs = GetFileAttributesW(pwszPath);
                    if (dwAttribs != INVALID_FILE_ATTRIBUTES)
                        return mkWinChildDuplicateUtf16String(pwszPath, cwcPath + 4, ppwszImagePath);
                }
            }
        }
        /*
         * No path, need to search the PATH env.var. for the executable, maybe
         * adding an .exe suffix while do so if that is missing.
         */
        else
        {
            BOOL  fSearchedCwd = FALSE;
            WCHAR wszPathFallback[4];
            if (!pwszSearchPath)
                pwszSearchPath = (WCHAR *)mkWinChildcareWorkerFindPathValue(pwszzEnv, wszPathFallback);

            for (;;)
            {
                size_t cwcCombined;

                /*
                 * Find the end of the current PATH component.
                 */
                size_t cwcSkip;
                WCHAR  wcEnd;
                size_t cwcComponent = 0;
                WCHAR  wc;
                while ((wc = pwszSearchPath[cwcComponent]) != L'\0')
                {
                    if (wc != ';' && wc != ':')
                    { /* likely */ }
                    else if (wc == ';')
                        break;
                    else if (cwcComponent != (pwszSearchPath[cwcComponent] != L'"' ? 1 : 2))
                        break;
                   cwcComponent++;
                }
                wcEnd = wc;

                /* Trim leading spaces and double quotes. */
                while (   cwcComponent > 0
                       && ((wc = *pwszSearchPath) == L'"' || wc == L' ' || wc == L'\t'))
                {
                    pwszSearchPath++;
                    cwcComponent--;
                }
                cwcSkip = cwcComponent;

                /* Trim trailing spaces & double quotes. */
                while (   cwcComponent > 0
                       && ((wc = pwszSearchPath[cwcComponent - 1]) == L'"' || wc == L' ' || wc == L'\t'))
                    cwcComponent--;

                /*
                 * Skip empty components.  Join the component and the filename, making sure to
                 * resolve any CWD relative stuff first.
                 */
                cwcCombined = cwcComponent + 1 + cwcArg0;
                if (cwcComponent > 0 && cwcCombined <= MKWINCHILD_MAX_PATH)
                {
                    DWORD dwAttribs;

                    /* Copy the component into wszPathBuf, maybe abspath'ing it. */
                    DWORD  cwcAbsPath = 0;
                    if (   *pwszSearchPath != L'\\'
                        && (pwszSearchPath[1] != ':' || pwszSearchPath[2] != L'\\') )
                    {
                        /* To save an extra buffer + copying, we'll temporarily modify the PATH
                           value in our converted UTF-16 environment block.  */
                        WCHAR const wcSaved = pwszSearchPath[cwcComponent];
                        pwszSearchPath[cwcComponent] = L'\0';
                        cwcAbsPath = GetFullPathNameW(pwszSearchPath, MKWINCHILD_MAX_PATH, wszPathBuf, NULL);
                        pwszSearchPath[cwcComponent] = wcSaved;
                        if (cwcAbsPath > 0 && cwcAbsPath + 1 + cwcArg0 <= MKWINCHILD_MAX_PATH)
                            cwcCombined = cwcAbsPath + 1 + cwcArg0;
                        else
                            cwcAbsPath = 0;
                    }
                    if (cwcAbsPath == 0)
                    {
                        memcpy(wszPathBuf, pwszSearchPath, cwcComponent * sizeof(WCHAR));
                        cwcAbsPath = cwcComponent;
                    }

                    /* Append the filename. */
                    if ((wc = wszPathBuf[cwcAbsPath - 1]) == L'\\' || wc == L'/' || wc == L':')
                    {
                        memcpy(&wszPathBuf[cwcAbsPath], wszArg0, cwcArg0 * sizeof(WCHAR));
                        cwcCombined--;
                    }
                    else
                    {
                        wszPathBuf[cwcAbsPath] = L'\\';
                        memcpy(&wszPathBuf[cwcAbsPath + 1], wszArg0, cwcArg0 * sizeof(WCHAR));
                    }
                    assert(wszPathBuf[cwcCombined - 1] == L'\0');

                    /* DOS slash conversion */
                    pwc = wszPathBuf;
                    while ((pwc = wcschr(pwc, L'/')) != NULL)
                        *pwc++ = L'\\';

                    /*
                     * Search with exe suffix first.
                     */
                    if (!fHasExeSuffix)
                    {
                        wszPathBuf[cwcCombined - 1] = L'.';
                        wszPathBuf[cwcCombined    ] = L'e';
                        wszPathBuf[cwcCombined + 1] = L'x';
                        wszPathBuf[cwcCombined + 2] = L'e';
                        wszPathBuf[cwcCombined + 3] = L'\0';
                    }
                    dwAttribs = GetFileAttributesW(wszPathBuf);
                    if (   dwAttribs != INVALID_FILE_ATTRIBUTES
                        && !(dwAttribs & FILE_ATTRIBUTE_DIRECTORY))
                        return mkWinChildDuplicateUtf16String(wszPathBuf, cwcCombined + (fHasExeSuffix ? 0 : 4), ppwszImagePath);
                    if (!fHasExeSuffix)
                    {
                        wszPathBuf[cwcCombined - 1] = L'\0';

                        /*
                         * Check if the file exists w/o the added '.exe' suffix.  If it does,
                         * we need to check if we can pass it to CreateProcess or need the shell.
                         */
                        hFile = CreateFileW(wszPathBuf, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE | FILE_SHARE_WRITE,
                                            NULL /*pSecAttr*/, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
                        if (hFile != INVALID_HANDLE_VALUE)
                        {
                            *pfNeedShell = mkWinChildcareWorkerCheckIfNeedShell(hFile);
                            CloseHandle(hFile);
                            if (!*pfNeedShell)
                                return mkWinChildDuplicateUtf16String(wszPathBuf, cwcCombined, ppwszImagePath);
                            break;
                        }
                    }
                }

                /*
                 * Advance to the next component.
                 */
                if (wcEnd != '\0')
                    pwszSearchPath += cwcSkip + 1;
                else if (fSearchedCwd)
                    break;
                else
                {
                    fSearchedCwd = TRUE;
                    wszPathFallback[0] = L'.';
                    wszPathFallback[1] = L'\0';
                    pwszSearchPath = wszPathFallback;
                }
            }
        }

        /*
         * We need the shell.  It will take care of finding/reporting missing
         * image files and such.
         */
        *pfNeedShell = TRUE;
        if (pszShell)
        {
            cwc = MultiByteToWideChar(CP_ACP, 0 /*fFlags*/, pszShell, strlen(pszShell) + 1, wszPathBuf, MKWINCHILD_MAX_PATH);
            if (cwc > 0)
                return mkWinChildDuplicateUtf16String(wszPathBuf, cwc, ppwszImagePath);
            dwErr = GetLastError();
            fprintf(stderr, _("MultiByteToWideChar failed to convert shell (%s): %u\n"), pszShell, dwErr);
        }
        else
        {
            fprintf(stderr, "%s: not found!\n", pszArg0);
            dwErr = ERROR_FILE_NOT_FOUND;
        }
    }
    else
    {
        dwErr = GetLastError();
        fprintf(stderr, _("MultiByteToWideChar failed to convert argv[0] (%s): %u\n"), pszArg0, dwErr);
    }
    return dwErr == ERROR_INSUFFICIENT_BUFFER ? ERROR_FILENAME_EXCED_RANGE : dwErr;
}

/**
 * Creates the environment block.
 *
 * @returns 0 on success, windows error code on failure.
 * @param   papszEnv        The environment vector to convert.
 * @param   cbEnvStrings    The size of the environment strings, iff they are
 *                          sequential in a block.  Otherwise, zero.
 * @param   ppwszEnv        Where to return the pointer to the environment
 *                          block.
 * @param   ppwszSearchPath Where to return the pointer to the path value
 *                          within the environment block.  This will not be set
 *                          if cbEnvStrings is non-zero, more efficient to let
 *                          mkWinChildcareWorkerFindImage() search when needed.
 */
static int mkWinChildcareWorkerConvertEnvironment(char **papszEnv, size_t cbEnvStrings,
                                                  WCHAR **ppwszEnv, WCHAR const **ppwszSearchPath)
{
    DWORD  dwErr;
    int    cwcRc;
    int    cwcDst;
    WCHAR *pwszzDst;

    *ppwszSearchPath = NULL;

    /*
     * We've got a little optimization here with help from mkWinChildCopyStringArray.
     */
    if (cbEnvStrings)
    {
        cwcDst = cbEnvStrings + 32;
        pwszzDst = (WCHAR *)xmalloc(cwcDst * sizeof(WCHAR));
        cwcRc = MultiByteToWideChar(CP_ACP, 0 /*fFlags*/, papszEnv[0], cbEnvStrings, pwszzDst, cwcDst);
        if (cwcRc != 0)
        {
            *ppwszEnv = pwszzDst;
            return 0;
        }

        /* Resize the allocation and try again. */
        dwErr = GetLastError();
        if (dwErr == ERROR_INSUFFICIENT_BUFFER)
        {
            cwcRc = MultiByteToWideChar(CP_ACP, 0 /*fFlags*/, papszEnv[0], cbEnvStrings, NULL, 0);
            if (cwcRc > 0)
                cwcDst = cwcRc + 32;
            else
                cwcDst *= 2;
            pwszzDst = (WCHAR *)xrealloc(pwszzDst, cwcDst);
            cwcRc = MultiByteToWideChar(CP_ACP, 0 /*fFlags*/, papszEnv[0], cbEnvStrings, pwszzDst, cwcDst);
            if (cwcRc != 0)
            {
                *ppwszEnv = pwszzDst;
                return 0;
            }
            dwErr = GetLastError();
        }
        fprintf(stderr, _("MultiByteToWideChar failed to convert environment block: %u\n"), dwErr);
    }
    /*
     * Need to convert it string by string.
     */
    else
    {
        size_t offPathValue = ~(size_t)0;
        size_t offDst;

        /*
         * Estimate the size first.
         */
        size_t      cEnvVars;
        size_t      cwcDst = 32;
        size_t      iVar   = 0;
        const char *pszSrc;
        while ((pszSrc = papszEnv[iVar]) != NULL)
        {
            cwcDst += strlen(pszSrc) + 1;
            iVar++;
        }
        cEnvVars = iVar;

        /* Allocate estimated WCHARs and convert the variables one by one, reallocating
           the block as needed. */
        pwszzDst = (WCHAR *)xmalloc(cwcDst * sizeof(WCHAR));
        cwcDst--; /* save one wchar for the terminating empty string. */
        offDst = 0;
        for (iVar = 0; iVar < cEnvVars; iVar++)
        {
            size_t       cwcLeft = cwcDst - offDst;
            size_t const cbSrc   = strlen(pszSrc = papszEnv[iVar]) + 1;
            assert(cwcDst >= offDst);


            cwcRc = MultiByteToWideChar(CP_ACP, 0 /*fFlags*/, pszSrc, cbSrc, &pwszzDst[offDst], cwcLeft);
            if (cwcRc > 0)
            { /* likely */ }
            else
            {
                dwErr = GetLastError();
                if (dwErr == ERROR_INSUFFICIENT_BUFFER)
                {
                    /* Need more space.  So, calc exacly how much and resize the block accordingly. */
                    size_t cbSrc2 = cbSrc;
                    size_t iVar2  = iVar;
                    cwcLeft = 1;
                    for (;;)
                    {
                        size_t cwcRc2 = MultiByteToWideChar(CP_ACP, 0 /*fFlags*/, pszSrc, cbSrc, NULL, 0);
                        if (cwcRc2 > 0)
                            cwcLeft += cwcRc2;
                        else
                            cwcLeft += cbSrc * 4;

                        /* advance */
                        iVar2++;
                        if (iVar2 >= cEnvVars)
                            break;
                        pszSrc = papszEnv[iVar2];
                        cbSrc2 = strlen(pszSrc) + 1;
                    }
                    pszSrc = papszEnv[iVar];

                    /* Grow the allocation and repeat the conversion. */
                    if (offDst + cwcLeft > cwcDst + 1)
                    {
                        cwcDst   = offDst + cwcLeft;
                        pwszzDst = (WCHAR *)xrealloc(pwszzDst, cwcDst * sizeof(WCHAR));
                        cwcDst--; /* save one wchar for the terminating empty string. */
                        cwcRc = MultiByteToWideChar(CP_ACP, 0 /*fFlags*/, pszSrc, cbSrc, &pwszzDst[offDst], cwcLeft - 1);
                        if (cwcRc <= 0)
                            dwErr = GetLastError();
                    }
                }
                if (cwcRc <= 0)
                {
                    fprintf(stderr, _("MultiByteToWideChar failed to convert environment string #%u (%s): %u\n"),
                            iVar, pszSrc, dwErr);
                    free(pwszzDst);
                    return dwErr;
                }
            }

            /* Look for the PATH. */
            if (   offPathValue == ~(size_t)0
                && IS_PATH_ENV_VAR(cwcRc, &pwszzDst[offDst]) )
                offPathValue = offDst + 4 + 1;

            /* Advance. */
            offDst += cwcRc;
        }
        pwszzDst[offDst++] = '\0';

        if (offPathValue != ~(size_t)0)
            *ppwszSearchPath = &pwszzDst[offPathValue];
        *ppwszEnv = pwszzDst;
        return 0;
    }
    free(pwszzDst);
    return dwErr;
}

/**
 * Childcare worker: handle regular process.
 *
 * @param   pWorker             The worker.
 * @param   pChild              The kSubmit child.
 */
static void mkWinChildcareWorkerThreadHandleProcess(PWINCHILDCAREWORKER pWorker, PWINCHILD pChild)
{
    WCHAR  *pwszSearchPath   = NULL;
    WCHAR  *pwszzEnvironment = NULL;
    WCHAR  *pwszCommandLine  = NULL;
    WCHAR  *pwszImageName    = NULL;
    BOOL    fNeedShell       = FALSE;
    int     rc;

    /*
     * First we convert the environment so we get the PATH we need to
     * search for the executable.
     */
    rc = mkWinChildcareWorkerConvertEnvironment(pChild->u.Process.papszEnv ? pChild->u.Process.papszEnv : environ,
                                                pChild->u.Process.cbEnvStrings,
                                                &pwszzEnvironment, &pwszSearchPath);
    /*
     * Find the executable and maybe checking if it's a shell script, then
     * convert it to a command line.
     */
    if (rc == 0)
        rc = mkWinChildcareWorkerFindImage(pChild->u.Process.papszArgs[0], pwszSearchPath, pwszzEnvironment,
                                           pChild->u.Process.pszShell, &pwszImageName, &fNeedShell);
    if (rc == 0)
    {
        if (!fNeedShell)
            rc = mkWinChildcareWorkerConvertCommandline(pChild->u.Process.papszArgs, 0 /*fFlags*/, &pwszCommandLine);
        else
            rc = mkWinChildcareWorkerConvertCommandlineWithShell(pwszImageName, pChild->u.Process.papszArgs, &pwszCommandLine);

        /*
         * Create the child process.
         */
        if (rc == 0)
        {
            rc = mkWinChildcareWorkerCreateProcess(pWorker, pChild, pwszImageName, pwszCommandLine, pwszzEnvironment);
            if (rc == 0)
            {
                /*
                 * Wait for the child to complete.
                 */
                mkWinChildcareWorkerWaitForProcess(pWorker, pChild, pChild->u.Process.hProcess);
            }
            else
                pChild->iExitCode = rc;
        }
        else
            pChild->iExitCode = rc;
    }
    else
        pChild->iExitCode = rc;
    free(pwszCommandLine);
    free(pwszImageName);
    free(pwszzEnvironment);

    /* In case we failed, we must make sure the child end of pipes
       used by $(shell no_such_command.exe) are closed, otherwise
       the main thread will be stuck reading the parent end.  */
    mkWinChildcareWorkerCloseStandardHandles(pChild);
}

#ifdef KMK

/**
 * Childcare worker: handle builtin command.
 *
 * @param   pWorker             The worker.
 * @param   pChild              The kSubmit child.
 */
static void mkWinChildcareWorkerThreadHandleBuiltIn(PWINCHILDCAREWORKER pWorker, PWINCHILD pChild)
{
    PCKMKBUILTINENTRY pBuiltIn = pChild->u.BuiltIn.pBuiltIn;
    if (pBuiltIn->uFnSignature == FN_SIG_MAIN)
        pChild->iExitCode = pBuiltIn->u.pfnMain(pChild->u.BuiltIn.cArgs, pChild->u.BuiltIn.papszArgs, pChild->u.BuiltIn.papszEnv);
    else if (pBuiltIn->uFnSignature == FN_SIG_MAIN_SPAWNS)
        pChild->iExitCode = pBuiltIn->u.pfnMainSpawns(pChild->u.BuiltIn.cArgs, pChild->u.BuiltIn.papszArgs,
                                                      pChild->u.BuiltIn.papszEnv, pChild->pMkChild, NULL);
    else
    {
        assert(0);
        pChild->iExitCode = 98;
    }
}

/**
 * Childcare worker: handle append write-out.
 *
 * @param   pWorker             The worker.
 * @param   pChild              The kSubmit child.
 */
static void mkWinChildcareWorkerThreadHandleAppend(PWINCHILDCAREWORKER pWorker, PWINCHILD pChild)
{
    int fd = open(pChild->u.Append.pszFilename,
                  pChild->u.Append.fTruncate
                  ? O_WRONLY | O_TRUNC  | O_CREAT | _O_NOINHERIT | _O_BINARY
                  : O_WRONLY | O_APPEND | O_CREAT | _O_NOINHERIT | _O_BINARY,
                  0666);
    if (fd >= 0)
    {
        ssize_t cbWritten = write(fd, pChild->u.Append.pszAppend, pChild->u.Append.cbAppend);
        if (cbWritten == (ssize_t)pChild->u.Append.cbAppend)
        {
            if (close(fd) >= 0)
            {
                pChild->iExitCode = 0;
                return;
            }
            fprintf(stderr, "kmk_builtin_append: close failed on '%s': %u (%s)\n",
                    pChild->u.Append.pszFilename, errno, strerror(errno));
        }
        else
            fprintf(stderr, "kmk_builtin_append: error writing %lu bytes to on '%s': %u (%s)\n",
                    pChild->u.Append.cbAppend, pChild->u.Append.pszFilename, errno, strerror(errno));
        close(fd);
    }
    else
        fprintf(stderr, "kmk_builtin_append: error opening '%s': %u (%s)\n",
                pChild->u.Append.pszFilename, errno, strerror(errno));
    pChild->iExitCode = 1;
}

/**
 * Childcare worker: handle kSubmit job.
 *
 * @param   pWorker             The worker.
 * @param   pChild              The kSubmit child.
 */
static void mkWinChildcareWorkerThreadHandleSubmit(PWINCHILDCAREWORKER pWorker, PWINCHILD pChild)
{
    void *pvSubmitWorker = pChild->u.Submit.pvSubmitWorker;
    for (;;)
    {
        int   iExitCode = -42;
        int   iSignal   = -1;
        DWORD dwStatus  = WaitForSingleObject(pChild->u.Submit.hEvent, INFINITE);
        assert(dwStatus != WAIT_FAILED);

        if (kSubmitSubProcGetResult((intptr_t)pvSubmitWorker, &iExitCode, &iSignal) == 0)
        {
            pChild->iExitCode = iExitCode;
            pChild->iSignal   = iSignal;
            /* Cleanup must be done on the main thread. */
            return;
        }

        if (pChild->iSignal != 0)
            kSubmitSubProcKill((intptr_t)pvSubmitWorker, pChild->iSignal);
    }
}

/**
 * Childcare worker: handle kmk_redirect process.
 *
 * @param   pWorker             The worker.
 * @param   pChild              The redirect child.
 */
static void mkWinChildcareWorkerThreadHandleRedirect(PWINCHILDCAREWORKER pWorker, PWINCHILD pChild)
{
    mkWinChildcareWorkerWaitForProcess(pWorker, pChild, pChild->u.Redirect.hProcess);
}

#endif /* KMK */

/**
 * Childcare worker thread.
 *
 * @returns 0
 * @param   pvUser              The worker instance.
 */
static unsigned int __stdcall mkWinChildcareWorkerThread(void *pvUser)
{
    PWINCHILDCAREWORKER pWorker = (PWINCHILDCAREWORKER)pvUser;
    assert(pWorker->uMagic == WINCHILDCAREWORKER_MAGIC);

    /*
     * Adjust process group if necessary.
     */
    if (g_cProcessorGroups > 1)
    {
        GROUP_AFFINITY Affinity = { ~(ULONG_PTR)0, pWorker->iProcessorGroup, { 0, 0, 0 } };
        BOOL fRet = g_pfnSetThreadGroupAffinity(GetCurrentThread(), &Affinity, NULL);
        assert(fRet); (void)fRet;
    }

    /*
     * Work loop.
     */
    while (!g_fShutdown)
    {
        /*
         * Try go idle.
         */
        PWINCHILD pChild = pWorker->pTailTodoChildren;
        if (!pChild)
        {
            _InterlockedExchange(&pWorker->fIdle, TRUE);
            pChild = pWorker->pTailTodoChildren;
            if (!pChild)
            {
                DWORD dwStatus;

                _InterlockedIncrement((long *)&g_cIdleChildcareWorkers);
                dwStatus = WaitForSingleObject(pWorker->hEvtIdle, INFINITE);
                _InterlockedExchange(&pWorker->fIdle, FALSE);
                _InterlockedDecrement((long *)&g_cIdleChildcareWorkers);

                assert(dwStatus != WAIT_FAILED);
                if (dwStatus == WAIT_FAILED)
                    Sleep(20);

                pChild = pWorker->pTailTodoChildren;
            }
            else
                _InterlockedExchange(&pWorker->fIdle, FALSE);
        }
        if (pChild)
        {
            /*
             * We got work to do.  First job is to deque the job.
             */
            pChild = mkWinChildDequeFromLifo(&pWorker->pTailTodoChildren, pChild);
            assert(pChild);
            if (pChild)
            {
                PWINCHILD pTailExpect;

                switch (pChild->enmType)
                {
                    case WINCHILDTYPE_PROCESS:
                        mkWinChildcareWorkerThreadHandleProcess(pWorker, pChild);
                        break;
#ifdef KMK
                    case WINCHILDTYPE_BUILT_IN:
                        mkWinChildcareWorkerThreadHandleBuiltIn(pWorker, pChild);
                        break;
                    case WINCHILDTYPE_APPEND:
                        mkWinChildcareWorkerThreadHandleAppend(pWorker, pChild);
                        break;
                    case WINCHILDTYPE_SUBMIT:
                        mkWinChildcareWorkerThreadHandleSubmit(pWorker, pChild);
                        break;
                    case WINCHILDTYPE_REDIRECT:
                        mkWinChildcareWorkerThreadHandleRedirect(pWorker, pChild);
                        break;
#endif
                    default:
                        assert(0);
                }

                /*
                 * Move the child to the completed list.
                 */
                pTailExpect = NULL;
                for (;;)
                {
                    PWINCHILD pTailActual;
                    pChild->pNext = pTailExpect;
                    pTailActual = _InterlockedCompareExchangePointer(&g_pTailCompletedChildren, pChild, pTailExpect);
                    if (pTailActual != pTailExpect)
                        pTailExpect = pTailActual;
                    else
                    {
                        _InterlockedDecrement(&g_cPendingChildren);
                        if (pTailExpect)
                            break;
                        if (SetEvent(g_hEvtWaitChildren))
                            break;
                        fprintf(stderr, "SetEvent(g_hEvtWaitChildren=%p) failed: %u\n", g_hEvtWaitChildren, GetLastError());
                        break;
                    }
                }
            }
        }
    }

    _endthreadex(0);
    return 0;
}

/**
 * Creates another childcare worker.
 *
 * @returns The new worker, if we succeeded.
 */
static PWINCHILDCAREWORKER mkWinChildcareCreateWorker(void)
{
    PWINCHILDCAREWORKER pWorker = (PWINCHILDCAREWORKER)xcalloc(sizeof(*pWorker));
    pWorker->uMagic = WINCHILDCAREWORKER_MAGIC;
    pWorker->hEvtIdle = CreateEvent(NULL, FALSE /*fManualReset*/, FALSE /*fInitialState*/, NULL /*pszName*/);
    if (pWorker->hEvtIdle)
    {
        /* Before we start the thread, assign it to a processor group. */
        if (g_cProcessorGroups > 1)
        {
            unsigned int cMaxInGroup;
            unsigned int cInGroup;
            unsigned int iGroup = g_idxProcessorGroupAllocator % g_cProcessorGroups;
            pWorker->iProcessorGroup = iGroup;

            /* Advance.  We employ a very simple strategy that does 50% in
               each group for each group cycle.  Odd processor counts are
               caught in odd group cycles.  The init function selects the
               starting group based on make nesting level to avoid stressing
               out the first group. */
            cInGroup = ++g_idxProcessorInGroupAllocator;
            cMaxInGroup = g_pacProcessorsInGroup[iGroup];
            if (   !(cMaxInGroup & 1)
                || !((g_idxProcessorGroupAllocator / g_cProcessorGroups) & 1))
                cMaxInGroup /= 2;
            else
                cMaxInGroup = cMaxInGroup / 2 + 1;
            if (cInGroup >= cMaxInGroup)
            {
                g_idxProcessorInGroupAllocator = 0;
                g_idxProcessorGroupAllocator++;
            }
        }

        /* Try start the thread. */
        pWorker->hThread = (HANDLE)_beginthreadex(NULL, 0 /*cbStack*/, mkWinChildcareWorkerThread, pWorker,
                                                  0, &pWorker->tid);
        if (pWorker->hThread != NULL)
        {
            g_papChildCareworkers[g_cChildCareworkers++] = pWorker;
            return pWorker;
        }
        CloseHandle(pWorker->hEvtIdle);
    }
    pWorker->uMagic = ~WINCHILDCAREWORKER_MAGIC;
    free(pWorker);
    return NULL;
}

/**
 * Helper for copying argument and environment vectors.
 *
 * @returns Single alloc block copy.
 * @param   papszSrc    The source vector.
 * @param   pcbStrings  Where to return the size of the strings & terminator.
 */
static char **mkWinChildCopyStringArray(char **papszSrc, size_t *pcbStrings)
{
    const char *psz;
    char      **papszDstArray;
    char       *pszDstStr;
    size_t      i;

    /* Calc sizes first. */
    size_t      cbStrings = 1; /* (one extra for terminator string) */
    size_t      cStrings = 0;
    while ((psz = papszSrc[cStrings]) != NULL)
    {
        cbStrings += strlen(psz) + 1;
        cStrings++;
    }
    *pcbStrings = cbStrings;

    /* Allocate destination. */
    papszDstArray = (char **)xmalloc(cbStrings + (cStrings + 1) * sizeof(papszDstArray[0]));
    pszDstStr = (char *)&papszDstArray[cStrings + 1];

    /* Copy it. */
    for (i = 0; i < cStrings; i++)
    {
        size_t cbString = strlen(papszSrc[i]) + 1;
        papszDstArray[i] = (char *)memcpy(pszDstStr, papszSrc[i], cbString);
        pszDstStr += cbString;
    }
    *pszDstStr = '\0';
    assert(&pszDstStr[1] - papszDstArray[0] == cbStrings);
    papszDstArray[i] = NULL;
    return papszDstArray;
}

/**
 * Allocate and init a WINCHILD.
 *
 * @returns The new windows child structure.
 * @param   enmType         The child type.
 */
static PWINCHILD mkWinChildNew(WINCHILDTYPE enmType)
{
    PWINCHILD pChild = xcalloc(sizeof(*pChild));
    pChild->enmType     = enmType;
    pChild->fCoreDumped = 0;
    pChild->iSignal     = 0;
    pChild->iExitCode   = 222222;
    pChild->uMagic      = WINCHILD_MAGIC;
    pChild->pid         = (intptr_t)pChild;
    return pChild;
}

/**
 * Destructor for WINCHILD.
 *
 * @param   pChild              The child structure to destroy.
 */
static void mkWinChildDelete(PWINCHILD pChild)
{
    assert(pChild->uMagic == WINCHILD_MAGIC);
    pChild->uMagic = ~WINCHILD_MAGIC;

    switch (pChild->enmType)
    {
        case WINCHILDTYPE_PROCESS:
        {
            if (pChild->u.Process.papszArgs)
            {
                free(pChild->u.Process.papszArgs);
                pChild->u.Process.papszArgs = NULL;
            }
            if (pChild->u.Process.cbEnvStrings && pChild->u.Process.papszEnv)
            {
                free(pChild->u.Process.papszEnv);
                pChild->u.Process.papszEnv = NULL;
            }
            if (pChild->u.Process.pszShell)
            {
                free(pChild->u.Process.pszShell);
                pChild->u.Process.pszShell = NULL;
            }
            if (pChild->u.Process.hProcess)
            {
                CloseHandle(pChild->u.Process.hProcess);
                pChild->u.Process.hProcess = NULL;
            }
            mkWinChildcareWorkerCloseStandardHandles(pChild);
            break;
        }

#ifdef KMK
        case WINCHILDTYPE_BUILT_IN:
            if (pChild->u.BuiltIn.papszArgs)
            {
                free(pChild->u.BuiltIn.papszArgs);
                pChild->u.BuiltIn.papszArgs = NULL;
            }
            if (pChild->u.BuiltIn.papszEnv)
            {
                free(pChild->u.BuiltIn.papszEnv);
                pChild->u.BuiltIn.papszEnv = NULL;
            }
            break;

        case WINCHILDTYPE_APPEND:
            if (pChild->u.Append.pszFilename)
            {
                free(pChild->u.Append.pszFilename);
                pChild->u.Append.pszFilename = NULL;
            }
            if (pChild->u.Append.pszAppend)
            {
                free(pChild->u.Append.pszAppend);
                pChild->u.Append.pszAppend = NULL;
            }
            break;

        case WINCHILDTYPE_SUBMIT:
            if (pChild->u.Submit.pvSubmitWorker)
            {
                kSubmitSubProcCleanup((intptr_t)pChild->u.Submit.pvSubmitWorker);
                pChild->u.Submit.pvSubmitWorker = NULL;
            }
            break;

        case WINCHILDTYPE_REDIRECT:
            if (pChild->u.Redirect.hProcess)
            {
                CloseHandle(pChild->u.Redirect.hProcess);
                pChild->u.Redirect.hProcess = NULL;
            }
            break;
#endif /* KMK */

        default:
            assert(0);
    }

    free(pChild);
}

/**
 * Queues the child with a worker, creating new workers if necessary.
 *
 * @returns 0 on success, windows error code on failure (child destroyed).
 * @param   pChild          The child.
 * @param   pPid            Where to return the PID (optional).
 */
static int mkWinChildPushToCareWorker(PWINCHILD pChild, pid_t *pPid)
{
    PWINCHILDCAREWORKER pWorker = NULL;
    PWINCHILD pOldChild;
    PWINCHILD pCurChild;

    /*
     * There are usually idle workers around, except for at the start.
     */
    if (g_cIdleChildcareWorkers > 0)
    {
        /*
         * Try the idle hint first and move forward from it.
         */
        unsigned int const cWorkers = g_cChildCareworkers;
        unsigned int       iHint    = g_idxLastChildcareWorker;
        unsigned int       i;
        for (i = iHint; i < cWorkers; i++)
        {
            PWINCHILDCAREWORKER pPossibleWorker = g_papChildCareworkers[i];
            if (pPossibleWorker->fIdle)
            {
                pWorker = pPossibleWorker;
                break;
            }
        }
        if (!pWorker)
        {
            /* Scan from the start. */
            if (iHint > cWorkers)
                iHint = cWorkers;
            for (i = 0; i < iHint; i++)
            {
                PWINCHILDCAREWORKER pPossibleWorker = g_papChildCareworkers[i];
                if (pPossibleWorker->fIdle)
                {
                    pWorker = pPossibleWorker;
                    break;
                }
            }
        }
    }
    if (!pWorker)
    {
        /*
         * Try create more workers if we haven't reached the max yet.
         */
        if (g_cChildCareworkers < g_cChildCareworkersMax)
            pWorker = mkWinChildcareCreateWorker();

        /*
         * Queue it with an existing worker.  Look for one without anthing extra scheduled.
         */
        if (!pWorker)
        {
            unsigned int i = g_cChildCareworkers;
            if (i == 0)
                fatal(NILF, 0, _("Failed to create worker threads for managing child processes!\n"));
            pWorker = g_papChildCareworkers[--i];
            if (pWorker->pTailTodoChildren)
                while (i-- > 0)
                {
                    PWINCHILDCAREWORKER pPossibleWorker = g_papChildCareworkers[i];
                    if (!pPossibleWorker->pTailTodoChildren)
                    {
                        pWorker = pPossibleWorker;
                        break;
                    }
                }
        }
    }

    /*
     * Do the queueing.
     */
    pOldChild = NULL;
    for (;;)
    {
        pChild->pNext = pOldChild;
        pCurChild = _InterlockedCompareExchangePointer((void **)&pWorker->pTailTodoChildren, pChild, pOldChild);
        if (pCurChild == pOldChild)
        {
            DWORD volatile dwErr;
            _InterlockedIncrement(&g_cPendingChildren);
            if (   !pWorker->fIdle
                || SetEvent(pWorker->hEvtIdle))
            {
                *pPid = pChild->pid;
                return 0;
            }

            _InterlockedDecrement(&g_cPendingChildren);
            dwErr = GetLastError();
            assert(0);
            mkWinChildDelete(pChild);
            return dwErr ? dwErr : -1;
        }
        pOldChild = pCurChild;
    }
}

/**
 * Creates a regular child process (job.c).
 *
 * Will copy the information and push it to a childcare thread that does the
 * actual process creation.
 *
 * @returns 0 on success, windows status code on failure.
 * @param   papszArgs           The arguments.
 * @param   papszEnv            The environment (optional).
 * @param   pszShell            The SHELL variable value (optional).
 * @param   pMkChild            The make child structure (optional).
 * @param   pPid                Where to return the pid.
 */
int MkWinChildCreate(char **papszArgs, char **papszEnv, const char *pszShell, struct child *pMkChild, pid_t *pPid)
{
    PWINCHILD pChild = mkWinChildNew(WINCHILDTYPE_PROCESS);
    pChild->pMkChild = pMkChild;

    pChild->u.Process.papszArgs = mkWinChildCopyStringArray(papszArgs, &pChild->u.Process.cbArgsStrings);
    if (   !papszEnv
        || !pMkChild
        || pMkChild->environment == papszEnv)
    {
        pChild->u.Process.cbEnvStrings = 0;
        pChild->u.Process.papszEnv = papszEnv;
    }
    else
        pChild->u.Process.papszEnv = mkWinChildCopyStringArray(papszEnv, &pChild->u.Process.cbEnvStrings);
    if (pszShell)
        pChild->u.Process.pszShell = xstrdup(pszShell);
    pChild->u.Process.hStdOut = INVALID_HANDLE_VALUE;
    pChild->u.Process.hStdErr = INVALID_HANDLE_VALUE;

    return mkWinChildPushToCareWorker(pChild, pPid);
}

/**
 * Creates a chile process with a pipe hooked up to stdout.
 *
 * @returns 0 on success, non-zero on failure.
 * @param   papszArgs       The argument vector.
 * @param   papszEnv        The environment vector (optional).
 * @param   fdErr           File descriptor to hook up to stderr.
 * @param   pPid            Where to return the pid.
 * @param   pfdReadPipe     Where to return the read end of the pipe.
 */
int MkWinChildCreateWithStdOutPipe(char **papszArgs, char **papszEnv, int fdErr, pid_t *pPid, int *pfdReadPipe)
{
    /*
     * Create the pipe.
     */
    HANDLE hReadPipe;
    HANDLE hWritePipe;
    if (CreatePipe(&hReadPipe, &hWritePipe, NULL, 0 /* default size */))
    {
        if (SetHandleInformation(hWritePipe, HANDLE_FLAG_INHERIT /* clear */ , HANDLE_FLAG_INHERIT /*set*/))
        {
            int fdReadPipe = _open_osfhandle((intptr_t)hReadPipe, O_RDONLY);
            if (fdReadPipe >= 0)
            {
                PWINCHILD pChild;
                int rc;

                /*
                 * Get a handle for fdErr.  Ignore failure.
                 */
                HANDLE hStdErr = INVALID_HANDLE_VALUE;
                if (fdErr >= 0)
                {
                    HANDLE hNative = (HANDLE)_get_osfhandle(fdErr);
                    if (!DuplicateHandle(GetCurrentProcess(), hNative, GetCurrentProcess(),
                                         &hStdErr, 0 /*DesiredAccess*/, TRUE /*fInherit*/, DUPLICATE_SAME_ACCESS))
                    {
                        ONN(error, NILF, _("DuplicateHandle failed on stderr descriptor (%u): %u\n"), fdErr, GetLastError());
                        hStdErr = INVALID_HANDLE_VALUE;
                    }
                }

                /*
                 * Push it off to the worker thread.
                 */
                pChild = mkWinChildNew(WINCHILDTYPE_PROCESS);
                pChild->u.Process.papszArgs = mkWinChildCopyStringArray(papszArgs, &pChild->u.Process.cbArgsStrings);
                pChild->u.Process.papszEnv  = mkWinChildCopyStringArray(papszEnv ? papszEnv : environ,
                                                                        &pChild->u.Process.cbEnvStrings);
                //if (pszShell)
                //    pChild->u.Process.pszShell = xstrdup(pszShell);
                pChild->u.Process.hStdOut   = hWritePipe;
                pChild->u.Process.hStdErr   = hStdErr;
                pChild->u.Process.fCloseStdErr = TRUE;
                pChild->u.Process.fCloseStdOut = TRUE;

                rc = mkWinChildPushToCareWorker(pChild, pPid);
                if (rc == 0)
                    *pfdReadPipe = fdReadPipe;
                else
                {
                    ON(error, NILF, _("mkWinChildPushToCareWorker failed on pipe: %d\n"), rc);
                    close(fdReadPipe);
                    *pfdReadPipe = -1;
                    *pPid = -1;
                }
                return rc;
            }

            ON(error, NILF, _("_open_osfhandle failed on pipe: %u\n"), errno);
        }
        else
            ON(error, NILF, _("SetHandleInformation failed on pipe: %u\n"), GetLastError());
        if (hReadPipe != INVALID_HANDLE_VALUE)
            CloseHandle(hReadPipe);
        CloseHandle(hWritePipe);
    }
    else
        ON(error, NILF, _("CreatePipe failed: %u\n"), GetLastError());
    *pfdReadPipe = -1;
    *pPid = -1;
    return -1;
}

#ifdef KMK

/**
 * Interface used by kmkbuiltin.c for executing builtin commands on threads.
 *
 * @returns 0 on success, windows status code on failure.
 * @param   pBuiltIn        The kmk built-in command entry.
 * @param   cArgs           The number of arguments in papszArgs.
 * @param   papszArgs       The argument vector.
 * @param   papszEnv        The environment vector, optional.
 * @param   pMkChild        The make child structure.
 * @param   pPid            Where to return the pid.
 */
int MkWinChildCreateBuiltIn(PCKMKBUILTINENTRY pBuiltIn, int cArgs, char **papszArgs, char **papszEnv,
                            struct child *pMkChild, pid_t *pPid)
{
    size_t    cbIgnored;
    PWINCHILD pChild = mkWinChildNew(WINCHILDTYPE_BUILT_IN);
    pChild->pMkChild            = pMkChild;
    pChild->u.BuiltIn.pBuiltIn  = pBuiltIn;
    pChild->u.BuiltIn.cArgs     = cArgs;
    pChild->u.BuiltIn.papszArgs = mkWinChildCopyStringArray(papszArgs, &cbIgnored);
    pChild->u.BuiltIn.papszEnv  = papszEnv ? mkWinChildCopyStringArray(papszEnv, &cbIgnored) : NULL;
    return mkWinChildPushToCareWorker(pChild, pPid);
}

/**
 * Interface used by append.c for do the slow file system part.
 *
 * This will append the given buffer to the specified file and free the buffer.
 *
 * @returns 0 on success, windows status code on failure.
 *
 * @param   pszFilename     The name of the file to append to.
 * @param   ppszAppend      What to append.  The pointer pointed to is set to
 *                          NULL once we've taken ownership of the buffer and
 *                          promise to free it.
 * @param   cbAppend        How much to append.
 * @param   fTruncate       Whether to truncate the file before appending to it.
 * @param   pMkChild        The make child structure.
 * @param   pPid            Where to return the pid.
 */
int MkWinChildCreateAppend(const char *pszFilename, char **ppszAppend, size_t cbAppend, int fTruncate,
                           struct child *pMkChild, pid_t *pPid)
{
    size_t    cbFilename = strlen(pszFilename) + 1;
    PWINCHILD pChild     = mkWinChildNew(WINCHILDTYPE_APPEND);
    pChild->pMkChild            = pMkChild;
    pChild->u.Append.fTruncate  = fTruncate;
    pChild->u.Append.pszAppend  = *ppszAppend;
    pChild->u.Append.cbAppend   = cbAppend;
    pChild->u.Append.pszFilename = (char *)memcpy(xmalloc(cbFilename), pszFilename, cbFilename);
    *ppszAppend = NULL;
    return mkWinChildPushToCareWorker(pChild, pPid);
}

/**
 * Interface used by kSubmit.c for registering stuff to wait on.
 *
 * @returns 0 on success, windows status code on failure.
 * @param   hEvent          The event object handle to wait on.
 * @param   pvSubmitWorker  The argument to pass back to kSubmit to clean up.
 * @param   pPid            Where to return the pid.
 */
int MkWinChildCreateSubmit(intptr_t hEvent, void *pvSubmitWorker, pid_t *pPid)
{
    PWINCHILD pChild = mkWinChildNew(WINCHILDTYPE_SUBMIT);
    pChild->u.Submit.hEvent         = (HANDLE)hEvent;
    pChild->u.Submit.pvSubmitWorker = pvSubmitWorker;
    return mkWinChildPushToCareWorker(pChild, pPid);
}

/**
 * Interface used by redirect.c for registering stuff to wait on.
 *
 * @returns 0 on success, windows status code on failure.
 * @param   hProcess        The process object to wait on.
 * @param   pPid            Where to return the pid.
 */
int MkWinChildCreateRedirect(intptr_t hProcess, pid_t *pPid)
{
    PWINCHILD pChild = mkWinChildNew(WINCHILDTYPE_REDIRECT);
    pChild->u.Redirect.hProcess = (HANDLE)hProcess;
    return mkWinChildPushToCareWorker(pChild, pPid);
}

#endif /* CONFIG_NEW_WIN_CHILDREN */

/**
 * Interface used to kill process when processing Ctrl-C and fatal errors.
 *
 * @returns 0 on success, -1+errno on error.
 * @param   pid                 The process to kill (PWINCHILD).
 * @param   iSignal             What to kill it with.
 * @param   pMkChild            The make child structure for validation.
 */
int MkWinChildKill(pid_t pid, int iSignal, struct child *pMkChild)
{
    PWINCHILD pChild = (PWINCHILD)pid;
    if (pChild)
    {
        assert(pChild->uMagic == WINCHILD_MAGIC);
        if (pChild->uMagic == WINCHILD_MAGIC)
        {
            switch (pChild->enmType)
            {
                case WINCHILDTYPE_PROCESS:
                    assert(pChild->pMkChild == pMkChild);
                    TerminateProcess(pChild->u.Process.hProcess, DBG_TERMINATE_PROCESS);
                    pChild->iSignal = iSignal;
                    break;
#ifdef KMK
                case WINCHILDTYPE_SUBMIT:
                {
                    pChild->iSignal = iSignal;
                    SetEvent(pChild->u.Submit.hEvent);
                    break;
                }

                case WINCHILDTYPE_REDIRECT:
                    TerminateProcess(pChild->u.Redirect.hProcess, DBG_TERMINATE_PROCESS);
                    pChild->iSignal = iSignal;
                    break;

                case WINCHILDTYPE_BUILT_IN:
                    break;

#endif /* KMK */
                default:
                    assert(0);
            }
        }
    }
    return -1;
}

/**
 * Wait for a child process to complete
 *
 * @returns 0 on success, windows error code on failure.
 * @param   fBlock          Whether to block.
 * @param   pPid            Where to return the pid if a child process
 *                          completed.  This is set to zero if none.
 * @param   piExitCode      Where to return the exit code.
 * @param   piSignal        Where to return the exit signal number.
 * @param   pfCoreDumped    Where to return the core dumped indicator.
 * @param   ppMkChild       Where to return the associated struct child pointer.
 */
int MkWinChildWait(int fBlock, pid_t *pPid, int *piExitCode, int *piSignal, int *pfCoreDumped, struct child **ppMkChild)
{
    PWINCHILD pChild;

    *pPid         = 0;
    *piExitCode   = -222222;
    *pfCoreDumped = 0;
    *ppMkChild    = NULL;

    /*
     * Wait if necessary.
     */
    if (fBlock && !g_pTailCompletedChildren && g_cPendingChildren > 0)
    {
        DWORD dwStatus = WaitForSingleObject(g_hEvtWaitChildren, INFINITE);
        if (dwStatus == WAIT_FAILED)
            return (int)GetLastError();
    }

    /*
     * Try unlink the last child in the LIFO.
     */
    pChild = g_pTailCompletedChildren;
    if (!pChild)
        return 0;
    pChild = mkWinChildDequeFromLifo(&g_pTailCompletedChildren, pChild);
    assert(pChild);

    /*
     * Set return values and ditch the child structure.
     */
    *pPid         = pChild->pid;
    *piExitCode   = pChild->iExitCode;
    *pfCoreDumped = pChild->fCoreDumped;
    *ppMkChild    = pChild->pMkChild;
    switch (pChild->enmType)
    {
        case WINCHILDTYPE_PROCESS:
            break;
#ifdef KMK
        case WINCHILDTYPE_BUILT_IN:
        case WINCHILDTYPE_APPEND:
        case WINCHILDTYPE_SUBMIT:
        case WINCHILDTYPE_REDIRECT:
            break;
#endif /* KMK */
        default:
            assert(0);
    }
    mkWinChildDelete(pChild);

#ifdef KMK
    /* Flush the volatile directory cache. */
    dir_cache_invalid_after_job();
#endif
    return 0;
}

/**
 * Get the child completed event handle.
 *
 * Needed when w32os.c is waiting for a job token to become available, given
 * that completed children is the typical source of these tokens (esp. for kmk).
 *
 * @returns Zero if completed children, event handle if waiting is required.
 */
intptr_t MkWinChildGetCompleteEventHandle(void)
{
    /* We don't return the handle if we've got completed children.  This
       is a safe guard against being called twice in a row without any
       MkWinChildWait call inbetween. */
    if (!g_pTailCompletedChildren)
        return (intptr_t)g_hEvtWaitChildren;
    return 0;
}

/**
 * Emulate execv() for restarting kmk after one ore more makefiles has been
 * made.
 *
 * Does not return.
 *
 * @param   papszArgs           The arguments.
 * @param   papszEnv            The environment.
 */
void MkWinChildReExecMake(char **papszArgs, char **papszEnv)
{
    PROCESS_INFORMATION     ProcInfo;
    STARTUPINFOW            StartupInfo;
    WCHAR                  *pwszCommandLine;
    WCHAR                  *pwszzEnvironment;
    WCHAR                  *pwszPathIgnored;
    int                     rc;

    /*
     * Get the executable name.
     */
    WCHAR wszImageName[MKWINCHILD_MAX_PATH];
    DWORD cwcImageName = GetModuleFileNameW(GetModuleHandle(NULL), wszImageName, MKWINCHILD_MAX_PATH);
    if (cwcImageName == 0)
        ON(fatal, NILF, _("MkWinChildReExecMake: GetModuleFileName failed: %u\n"), GetLastError());

    /*
     * Create the command line and environment.
     */
    rc = mkWinChildcareWorkerConvertCommandline(papszArgs, 0 /*fFlags*/, &pwszCommandLine);
    if (rc != 0)
        ON(fatal, NILF, _("MkWinChildReExecMake: mkWinChildcareWorkerConvertCommandline failed: %u\n"), rc);

    rc = mkWinChildcareWorkerConvertEnvironment(papszEnv ? papszEnv : environ, 0 /*cbEnvStrings*/,
                                                &pwszzEnvironment, &pwszPathIgnored);
    if (rc != 0)
        ON(fatal, NILF, _("MkWinChildReExecMake: mkWinChildcareWorkerConvertEnvironment failed: %u\n"), rc);


    /*
     * Fill out the startup info and try create the process.
     */
    memset(&ProcInfo, 0, sizeof(ProcInfo));
    memset(&StartupInfo, 0, sizeof(StartupInfo));
    StartupInfo.cb = sizeof(StartupInfo);
    GetStartupInfoW(&StartupInfo);
    if (!CreateProcessW(wszImageName, pwszCommandLine, NULL /*pProcSecAttr*/, NULL /*pThreadSecAttr*/,
                        TRUE /*fInheritHandles*/, CREATE_UNICODE_ENVIRONMENT, pwszzEnvironment, NULL /*pwsz*/,
                        &StartupInfo, &ProcInfo))
        ON(fatal, NILF, _("MkWinChildReExecMake: CreateProcessW failed: %u\n"), GetLastError());
    CloseHandle(ProcInfo.hThread);

    /*
     * Wait for it to complete and forward the status code to our parent.
     */
    for (;;)
    {
        DWORD dwExitCode = -2222;
        DWORD dwStatus = WaitForSingleObject(ProcInfo.hProcess, INFINITE);
        if (   dwStatus == WAIT_IO_COMPLETION
            || dwStatus == WAIT_TIMEOUT /* whatever */)
            continue; /* however unlikely, these aren't fatal. */

        /* Get the status code and terminate. */
        if (dwStatus == WAIT_OBJECT_0)
        {
            if (!GetExitCodeProcess(ProcInfo.hProcess, &dwExitCode))
            {
                ON(fatal, NILF, _("MkWinChildReExecMake: GetExitCodeProcess failed: %u\n"), GetLastError());
                dwExitCode = -2222;
            }
        }
        else if (dwStatus)
            dwExitCode = dwStatus;

        CloseHandle(ProcInfo.hProcess);
        for (;;)
            exit(dwExitCode);
    }
}

#ifdef WITH_RW_LOCK
/** Serialization with kmkbuiltin_redirect. */
void MkWinChildExclusiveAcquire(void)
{
    AcquireSRWLockExclusive(&g_RWLock);
}

/** Serialization with kmkbuiltin_redirect. */
void MkWinChildExclusiveRelease(void)
{
    ReleaseSRWLockExclusive(&g_RWLock);
}
#endif /* WITH_RW_LOCK */

/**
 * Implementation of the CLOSE_ON_EXEC macro.
 *
 * @returns errno value.
 * @param   fd          The file descriptor to hide from children.
 */
int MkWinChildUnrelatedCloseOnExec(int fd)
{
    if (fd >= 0)
    {
        HANDLE hNative = (HANDLE)_get_osfhandle(fd);
        if (hNative != INVALID_HANDLE_VALUE && hNative != NULL)
        {
            if (SetHandleInformation(hNative, HANDLE_FLAG_INHERIT /*clear*/ , 0 /*set*/))
                return 0;
        }
        return errno;
    }
    return EINVAL;
}

