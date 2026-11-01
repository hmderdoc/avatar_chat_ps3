/* A small DEFLATE / zlib decoder (RFC 1950/1951) for the chat's art
 * envelopes and PNG data. No allocation. */
#ifndef AC_INFLATE_H
#define AC_INFLATE_H
#include <stddef.h>

/* Inflate a raw DEFLATE stream. Returns the output length, or -1 on a
 * corrupt stream or when out is too small. */
long inflate_raw(const unsigned char *in, size_t n, unsigned char *out, size_t cap);

/* The same for a zlib stream (2-byte header, Adler-32 trailer, which is
 * not checked). */
long inflate_zlib(const unsigned char *in, size_t n, unsigned char *out, size_t cap);

#endif
