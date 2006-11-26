#ifndef __sys_times_h__
#define __sys_times_h__
#include <time.h>

struct tms
{
    clock_t tms_utime;      /**< User mode CPU time. */
    clock_t tms_stime;      /**< Kernel mode CPU time. */
    clock_t tms_cutime;     /**< User mode CPU time for waited for children. */
    clock_t tms_cstime;     /**< Kernel mode CPU time for waited for children. */
};

clock_t times(struct tms *);

#endif 
