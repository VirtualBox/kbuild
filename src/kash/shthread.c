/* $Id: shthread.c 2413 2010-09-11 17:43:04Z knut.osmundsen@oracle.com $ */
/** @file
 *
 * Shell Thread Management.
 *
 * Copyright (c) 2007-2010 knut st. osmundsen <bird-kBuild-spamx@anduin.net>
 *
 *
 * This file is part of kBuild.
 *
 * kBuild is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * kBuild is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with kBuild; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include "shthread.h"
#include "shinstance.h"
#include <assert.h>

#if K_OS == K_OS_WINDOWS
# include <Windows.h>
#elif K_OS == K_OS_OS2
# include <InnoTekLIBC/FastInfoBlocks.h>
# include <InnoTekLIBC/thread.h>
#else
# include <pthread.h>
#endif


/*******************************************************************************
*   Global Variables                                                           *
*******************************************************************************/
#if K_OS == K_OS_WINDOWS
static DWORD sh_tls = TLS_OUT_OF_INDEXES;
#elif K_OS == K_OS_OS2
static int sh_tls = -1;
#else
static int sh_tls_inited = 0;
static pthread_key_t sh_tls;
#endif


/**
 * Stores the shell instance pointer in a TLS entry.
 *
 * This will allocate the TLS entry on the first call. We assume
 * there will no be races at that time.
 *
 * @param   psh     The shell instance.
 */
void shthread_set_shell(struct shinstance *psh)
{
#if K_OS == K_OS_WINDOWS
    if (sh_tls == TLS_OUT_OF_INDEXES)
    {
        sh_tls = TlsAlloc();
        assert(sh_tls != TLS_OUT_OF_INDEXES);
    }
    if (!TlsSetValue(sh_tls, psh))
        assert(0);

#elif K_OS == K_OS_OS2
    if (sh_tls == -1)
    {
        sh_tls = __libc_TLSAlloc();
        assert(sh_tls != -1);
    }
    if (__libc_TLSSet(sh_tls, psh) == -1)
        assert(0);
#else
    if (!sh_tls_inited)
    {
        if (pthread_key_create(&sh_tls, NULL) != 0)
            assert(0);
        sh_tls_inited = 1;
    }
    if (pthread_setspecific(sh_tls, psh) != 0)
        assert(0);
#endif
}

/**
 * Get the shell instance pointer from TLS.
 *
 * @returns The shell instance.
 */
struct shinstance *shthread_get_shell(void)
{
    shinstance *psh;
#if K_OS == K_OS_WINDOWS
    psh = (shinstance *)TlsGetValue(sh_tls);
#elif K_OS == K_OS_OS2
    psh = (shinstance *)__libc_TLSGet(iTls)
#else
    psh = (shinstance *)pthread_getspecific(sh_tls);
#endif
    return psh;
}

