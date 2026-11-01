#ifndef SHIM_ERRNO_H
#define SHIM_ERRNO_H
extern int errno;
#define EAGAIN 11
#define EWOULDBLOCK EAGAIN
#endif
