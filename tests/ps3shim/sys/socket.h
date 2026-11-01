#ifndef SHIM_SYS_SOCKET_H
#define SHIM_SYS_SOCKET_H
#include <stddef.h>
#include <stdint.h>
typedef uint32_t socklen_t;
struct sockaddr { uint8_t sa_len; uint8_t sa_family; char sa_data[14]; };
#define AF_INET 2
#define SOCK_STREAM 1
int socket(int, int, int);
int connect(int, const struct sockaddr *, socklen_t);
int send(int, const void *, size_t, int);
int recv(int, void *, size_t, int);
int close(int);
#endif
