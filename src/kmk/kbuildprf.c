#include <stdio.h>
#include <io.h>
#include <errno.h>
#include <Windows.h>

__int64 prf_now(void)
{
    return GetTickCount();
}

#undef open
int prf_open(const char *name, int of, int mask)
{
    int fd;
//    int err;
//    __int64 t = prf_now();
    fd = _open(name, of, mask);
//    err = errno;
//    t = prf_now() - t;
//    fprintf(stderr, "open(%s, %#x) -> %d/%d in %lu\n", name, of, fd, err, (long)t);
//    errno = err;
    return fd;
}

#undef close
int prf_close(int fd)
{
    int rc;
    rc = _close(fd);
    return rc;
}


#undef read
int prf_read(int fd, void *buf, long cb)
{
    int cbRead;
    cbRead = _read(fd, buf, cb);
    return cbRead;
}

#undef lseek
long prf_lseek(int fd, long off, int whence)
{
    long rc;
    rc = _lseek(fd, off, whence);
    return rc;
}

