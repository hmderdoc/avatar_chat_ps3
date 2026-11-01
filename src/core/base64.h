#ifndef AC_BASE64_H
#define AC_BASE64_H
#include <stddef.h>
/* Standard alphabet with '=' padding. Encode returns the length written
 * (needs 4*ceil(n/3)+1 bytes of cap, NUL-terminates). Decode returns the
 * byte count, or -1 on bad input or when it does not fit. */
size_t base64_encode(const unsigned char *in, size_t n, char *out, size_t cap);
int base64_decode(const char *in, size_t n, unsigned char *out, size_t cap);
#endif
