

/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
#include <string.h>
#include <locale.h>
#include "shinstance.h"


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
extern void shfork_resume(void *cur, void *base, void *limit);

/* called by shforkA-win.asm: */
void *shfork_maybe_forked(int argc, char **argv, char **envp);
extern int shfork_body(uintptr_t stack_ptr);


/***
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
        shheap_init();
        return (char *)sh_malloc(NULL, 1*1024*1024) + 1*1024*1024;
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
    }
    return (void *)ptr;
}

/***
 * Create the child process making sure it inherits all our handles,
 * copy of the forkable heap and kick it off.
 *
 * Called by shfork_do_it() in shforkA-win.asm.
 *
 * @returns child pid on success, -1 and errno on failure.
 * @param   stack_ptr       The stack address at which the guest is suppost to resume.
 */
int shfork_body(uintptr_t stack_ptr)
{
    errno = ENOSYS;
    return -1;
}
