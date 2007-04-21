#include "mscfakes.h"
#include "getopt.h"
#include "process.h"

#define _SC_CLK_TCK 0x101
long _sysconf(int);
pid_t fork(void);
#define getpid _getpid

uid_t getuid(void);
uid_t geteuid(void);
gid_t getgid(void);
gid_t getegid(void);

