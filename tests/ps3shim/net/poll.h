#ifndef SHIM_NET_POLL_H
#define SHIM_NET_POLL_H
struct pollfd { int fd; short events; short revents; };
#define POLLIN 0x0001
#define POLLERR 0x0008
#define POLLHUP 0x0010
#define POLLNVAL 0x0020
int poll(struct pollfd *, unsigned, int);
#endif
