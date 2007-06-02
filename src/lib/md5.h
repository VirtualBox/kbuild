#ifndef MD5_H
#define MD5_H

#include "mytypes.h"

struct MD5Context {
        uint32_t buf[4];
        uint32_t bits[2];
        unsigned char in[64];
};

void MD5Init(struct MD5Context *);
void MD5Update(struct MD5Context *, const unsigned char *, unsigned);
void MD5Final(unsigned char digest[16], struct MD5Context *);
void MD5Transform(uint32_t buf[4], uint32_t in[16]);

#endif /* !MD5_H */
