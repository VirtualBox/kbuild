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
#if 0 /* 8-byte loop unroll */
    while (cb >= 8)
    {
        if (*pb == uch)
            return (unsigned char *)pb;
        if (pb[1] == uch)
            return (unsigned char *)pb + 1;
        if (pb[2] == uch)
            return (unsigned char *)pb + 2;
        if (pb[3] == uch)
            return (unsigned char *)pb + 3;
        if (pb[4] == uch)
            return (unsigned char *)pb + 4;
        if (pb[5] == uch)
            return (unsigned char *)pb + 5;
        if (pb[6] == uch)
            return (unsigned char *)pb + 6;
        if (pb[7] == uch)
            return (unsigned char *)pb + 7;
        cb -= 8;
        pb += 8;
    }
    switch (cb & 7)
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
        case 4:
            if (*pb == uch)
                return (unsigned char *)pb;
            if (pb[1] == uch)
                return (unsigned char *)pb + 1;
            if (pb[2] == uch)
                return (unsigned char *)pb + 2;
            if (pb[3] == uch)
                return (unsigned char *)pb + 3;
            break;
        case 5:
            if (*pb == uch)
                return (unsigned char *)pb;
            if (pb[1] == uch)
                return (unsigned char *)pb + 1;
            if (pb[2] == uch)
                return (unsigned char *)pb + 2;
            if (pb[3] == uch)
                return (unsigned char *)pb + 3;
            if (pb[4] == uch)
                return (unsigned char *)pb + 4;
            break;
        case 6:
            if (*pb == uch)
                return (unsigned char *)pb;
            if (pb[1] == uch)
                return (unsigned char *)pb + 1;
            if (pb[2] == uch)
                return (unsigned char *)pb + 2;
            if (pb[3] == uch)
                return (unsigned char *)pb + 3;
            if (pb[4] == uch)
                return (unsigned char *)pb + 4;
            if (pb[5] == uch)
                return (unsigned char *)pb + 5;
            break;
        case 7:
            if (*pb == uch)
                return (unsigned char *)pb;
            if (pb[1] == uch)
                return (unsigned char *)pb + 1;
            if (pb[2] == uch)
                return (unsigned char *)pb + 2;
            if (pb[3] == uch)
                return (unsigned char *)pb + 3;
            if (pb[4] == uch)
                return (unsigned char *)pb + 4;
            if (pb[5] == uch)
                return (unsigned char *)pb + 5;
            if (pb[6] == uch)
                return (unsigned char *)pb + 6;
            break;
    }

#elif 1 /* 4 byte loop unroll */
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

#else /* the basic loop */
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

