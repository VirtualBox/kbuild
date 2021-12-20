/* Added by bird - Public Domain. */

#include <windows.h>
#include <string.h>

static char g_szProgName[260] = {0};

const char *getprogname(void)
{
    if (g_szProgName[0] == '\0')
    {
        char       szName[260];
        UINT const cchName = GetModuleFileNameA(NULL, szName, sizeof(szName));
        UINT       offName = cchName;
        while (   offName > 0
               && szName[offName - 1] != '\\'
               && szName[offName - 1] != '/'
               && szName[offName - 1] != ':')
            offName--;
        memcpy(g_szProgName, &szName[offName], cchName - offName);
    }
    return g_szProgName;
}

