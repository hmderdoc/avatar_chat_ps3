#ifndef SHIM_ARPA_INET_H
#define SHIM_ARPA_INET_H
#include <netinet/in.h>
int inet_aton(const char *, struct in_addr *);
#endif
