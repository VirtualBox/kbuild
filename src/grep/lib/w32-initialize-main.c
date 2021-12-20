


#include "config.h"
#include <corecrt_startup.h>


void w32_initialize_main(int *pcArgs, char ***ppapszArgs)
{
    if (!getenv("KMK_GREP_NO_EXPANSION"))
    {
        _configure_narrow_argv(_crt_argv_expanded_arguments);
        *pcArgs = __argc;
        *ppapszArgs = __argv;
    }
}

