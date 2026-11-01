#ifndef SHIM_NET_SELECT_H
#define SHIM_NET_SELECT_H
#include <sys/time.h>
#ifndef FD_SETSIZE
typedef struct { unsigned long bits[32]; } fd_set;
#define FD_ZERO(s) do { int i_; for (i_ = 0; i_ < 32; i_++) (s)->bits[i_] = 0; } while (0)
#define FD_SET(fd, s) ((s)->bits[(fd) / 32] |= 1ul << ((fd) % 32))
#define FD_ISSET(fd, s) ((s)->bits[(fd) / 32] & (1ul << ((fd) % 32)))
#endif
int select(int, fd_set *, fd_set *, fd_set *, struct timeval *);
#endif
