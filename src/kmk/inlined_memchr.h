#include <string.h>
#ifdef _MSC_VER
_inline void *
#else
static __inline__ void *
#endif
my_inline_memchr(const void *pv, int ch, register size_t cb)
{
    register const unsigned int   uch = (unsigned)ch;
    register const unsigned char *pb = (const unsigned char *)pv;
#if 1
    while (cb >= 4)
    {
        if (*pb == uch)
            return (unsigned char *)pb;
        if (pb[1] == uch)
            return (unsigned char *)pb + 1;
        if (pb[2] == uch)
            return (unsigned char *)pb + 2;
        if (pb[3] == uch)
            return (unsigned char *)pb + 3;
        cb -= 4;
        pb += 4;
    }
    switch (cb & 3)
    {
        case 0:
            break;
        case 1:
            if (*pb == uch)
                return (unsigned char *)pb;
            break;
        case 2:
            if (*pb == uch)
                return (unsigned char *)pb;
            if (pb[1] == uch)
                return (unsigned char *)pb + 1;
            break;
        case 3:
            if (*pb == uch)
                return (unsigned char *)pb;
            if (pb[1] == uch)
                return (unsigned char *)pb + 1;
            if (pb[2] == uch)
                return (unsigned char *)pb + 2;
            break;
    }

#else
    while (cb > 0)
    {
        if (*pb == uch)
            return (void *)pb;
        cb--;
        pb++;
    }
#endif
    return 0;
}

#define memchr my_inline_memchr

