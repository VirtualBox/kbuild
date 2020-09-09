

/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
#include <string.h>
#include <locale.h>
#include <assert.h>
#include "shinstance.h"
#include <Windows.h>

/*******************************************************************************
*   Defined Constants And Macros                                               *
*******************************************************************************/
/** The stack size. This is also defined in shforkA-win.asm. */
#define SHFORK_STACK_SIZE (1*1024*1024)


/*******************************************************************************
*   Global Variables                                                           *
*******************************************************************************/
static void *g_stack_base = 0;
static void *g_stack_limit = 0;


/*******************************************************************************
*   Internal Functions                                                         *
*******************************************************************************/
static void *shfork_string_to_ptr(const char *str, const char *argv0, const char *what);

/* in shforkA-win.asm: */
extern pid_t shfork_do_it(shinstance *psh);
extern void shfork_resume(void *cur, void *base, void *limit);

/* called by shforkA-win.asm: */
void *shfork_maybe_forked(int argc, char **argv, char **envp);
extern int shfork_body(shinstance *psh, void *stack_ptr);
extern void init_syntax(void);


/**
 * Called by shforkA-win.asm to check whether we're a forked child
 * process or not.
 *
 * In the former case we will resume execution at the fork resume
 * point. In the latter we'll allocate a new stack of the forkable
 * heap and return it to the caller so real_main() in main.c can be
 * invoked on it.
 *
 * @returns Stack or not at all.
 * @param   argc    Argument count.
 * @param   argv    Argument vector.
 * @param   envp    Environment vector.
 */
void *shfork_maybe_forked(int argc, char **argv, char **envp)
{
    void *stack_ptr;

    /*
     * Are we actually forking?
     */
    if (    argc != 8
        ||  strcmp(argv[1], "--!forked!--")
        ||  strcmp(argv[2], "--stack-address")
        ||  strcmp(argv[4], "--stack-base")
        ||  strcmp(argv[6], "--stack-limit"))
    {
        char *stack;
        shheap_init(NULL);
        g_stack_limit = stack = (char *)sh_malloc(NULL, SHFORK_STACK_SIZE);
        g_stack_base = stack += SHFORK_STACK_SIZE;
        return stack;
    }

    /*
     * Do any init that needs to be done before resuming the
     * fork() call.
     */
    setlocale(LC_ALL, "");

    /*
     * Convert the stack addresses.
     */
    stack_ptr     = shfork_string_to_ptr(argv[3], argv[0], "--stack-address");
    g_stack_base  = shfork_string_to_ptr(argv[5], argv[0], "--stack-base");
    g_stack_limit = shfork_string_to_ptr(argv[7], argv[0], "--stack-limit");
    assert((uintptr_t)stack_ptr < (uintptr_t)g_stack_base);
    assert((uintptr_t)stack_ptr > (uintptr_t)g_stack_limit);

    /*
     * Switch stack and jump to the fork resume point.
     */
    shfork_resume(stack_ptr, g_stack_base, g_stack_limit);
    /* (won't get here) */
    return NULL;
}

/***
 * Converts a string into a pointer.
 *
 * @returns Pointer.
 * @param   argv0   The program name in case of error.
 * @param   str     The string to convert.
 */
static void *shfork_string_to_ptr(const char *str, const char *argv0, const char *what)
{
    const char *start = str;
    intptr_t ptr = 0;
    if (str[0] == '0' && (str[1] == 'x' || str[1] == 'X'))
        str += 2;
    while (*str)
    {
        unsigned digit;
        switch (*str)
        {
            case '0':            digit =   0; break;
            case '1':            digit =   1; break;
            case '2':            digit =   2; break;
            case '3':            digit =   3; break;
            case '4':            digit =   4; break;
            case '5':            digit =   5; break;
            case '6':            digit =   6; break;
            case '7':            digit =   7; break;
            case '8':            digit =   8; break;
            case '9':            digit =   9; break;
            case 'a': case 'A':  digit = 0xa; break;
            case 'b': case 'B':  digit = 0xb; break;
            case 'c': case 'C':  digit = 0xc; break;
            case 'd': case 'D':  digit = 0xd; break;
            case 'e': case 'E':  digit = 0xe; break;
            case 'f': case 'F':  digit = 0xf; break;
            default:
                fprintf(stderr, "%s: fatal error: Invalid %s '%s'\n", argv0, what, start);
                exit(2);
        }
        ptr <<= 4;
        ptr |= digit;
        str++;
    }
    return (void *)ptr;
}

/**
 * Do the fork.
 * @returns same as fork().
 * @param   psh             The shell that's forking.
 */
int shfork_do(shinstance *psh)
{
    /* save globals */
    void *pheap_head = shheap_get_head();
    pid_t pid = shfork_do_it(psh);
    if (pid == 0)
    {
        /* reinit stuff, only the heap is copied! */
        shthread_set_shell(psh);
        shheap_init(pheap_head);
        setlocale(LC_ALL, "");
        init_syntax();
    }
    return pid;
}

/**
 * Create the child process making sure it inherits all our handles,
 * copy of the forkable heap and kick it off.
 *
 * Called by shfork_do_it() in shforkA-win.asm.
 *
 * @returns child pid on success, -1 and errno on failure.
 * @param   psh             The shell that's forking.
 * @param   stack_ptr       The stack address at which the guest is suppost to resume.
 */
int shfork_body(shinstance *psh, void *stack_ptr)
{
    PROCESS_INFORMATION ProcInfo;
    STARTUPINFO StrtInfo;
    intptr_t hndls[3];
    char szExeName[1024];
    char szCmdLine[1024+256];
    DWORD cch;
    int rc = 0;

    assert((uintptr_t)stack_ptr < (uintptr_t)g_stack_base);
    assert((uintptr_t)stack_ptr > (uintptr_t)g_stack_limit);

    /*
     * Mark all handles inheritable and get the three standard handles.
     */
    shfile_fork_win(&psh->fdtab, 1 /* set */, &hndls[0]);

    /*
     * Create the process.
     */
    cch = GetModuleFileName(GetModuleHandle(NULL), szExeName, sizeof(szExeName));
    if (cch > 0)
    {
#if 0 /* quoting the program name doesn't seems to be working :/ */
        szCmdLine[0] = '"';
        memcpy(&szCmdLine[1], szExeName, cch);
        szCmdLine[++cch] = '"';
#else
        memcpy(&szCmdLine[0], szExeName, cch);
#endif
        cch += sprintf(&szCmdLine[cch], " --!forked!-- --stack-address %p --stack-base %p --stack-limit %p",
                       stack_ptr, g_stack_base, g_stack_limit);
        szCmdLine[cch+1] = '\0';
        TRACE2((NULL, "shfork_body: szCmdLine=%s\n", szCmdLine));

        memset(&StrtInfo, '\0', sizeof(StrtInfo)); /* just in case. */
        StrtInfo.cb = sizeof(StrtInfo);
        StrtInfo.lpReserved = NULL;
        StrtInfo.lpDesktop = NULL;
        StrtInfo.lpTitle = NULL;
        StrtInfo.dwX = 0;
        StrtInfo.dwY = 0;
        StrtInfo.dwXSize = 0;
        StrtInfo.dwYSize = 0;
        StrtInfo.dwXCountChars = 0;
        StrtInfo.dwYCountChars = 0;
        StrtInfo.dwFillAttribute = 0;
        StrtInfo.dwFlags = STARTF_USESTDHANDLES;;
        StrtInfo.wShowWindow = 0;
        StrtInfo.cbReserved2 = 0;
        StrtInfo.lpReserved2 = NULL;
        StrtInfo.hStdInput  = (HANDLE)hndls[0];
        StrtInfo.hStdOutput = (HANDLE)hndls[1];
        StrtInfo.hStdError  = (HANDLE)hndls[2];
        if (CreateProcess(szExeName,
                          szCmdLine,
                          NULL,         /* pProcessAttributes */
                          NULL,         /* pThreadAttributes */
                          TRUE,         /* bInheritHandles */
                          CREATE_SUSPENDED,
                          NULL,         /* pEnvironment */
                          NULL,         /* pCurrentDirectory */
                          &StrtInfo,
                          &ProcInfo))
        {
            /*
             * Copy the memory to the child.
             */
            rc = shheap_fork_copy_to_child(ProcInfo.hProcess);
            if (!rc)
            {
                if (ResumeThread(ProcInfo.hThread) != (DWORD)-1)
                {
                    rc = sh_add_child(psh, ProcInfo.dwProcessId, ProcInfo.hProcess, K_TRUE);
                    if (!rc)
                        rc = (int)ProcInfo.dwProcessId;
                }
                else
                {
                    DWORD dwErr = GetLastError();
                    fprintf(stderr, "shfork: ResumeThread() -> %d\n", dwErr);
                    errno = EINVAL;
                    rc = -1;
                }
            }
            if (rc == -1)
            {
                TerminateProcess(ProcInfo.hProcess, 127);
                /* needed?: ResumeThread(ProcInfo.hThread); */
                CloseHandle(ProcInfo.hProcess);
            }
            CloseHandle(ProcInfo.hThread);
        }
        else
        {
            DWORD dwErr = GetLastError();
            fprintf(stderr, "shfork: CreateProcess(%s) -> %d\n", szExeName, dwErr);
            errno = EINVAL;
            rc = -1;
        }
    }
    else
    {
        DWORD dwErr = GetLastError();
        fprintf(stderr, "shfork: GetModuleFileName() -> %d\n", dwErr);
        errno = EINVAL;
        rc = -1;
    }

    /*
     * Restore the handle inherit property.
     */
    shfile_fork_win(&psh->fdtab, 0 /* restore */, NULL);

    return rc;
}
