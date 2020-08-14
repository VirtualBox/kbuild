/*
 * Fake strsignal (for Windows/MSC).
 */

#include "shinstance.h" /* for MSC */
#include <string.h>

const char *strsignal(int iSig)
{
    if (iSig < NSIG)
	return sys_signame[iSig];
    return NULL;
}

