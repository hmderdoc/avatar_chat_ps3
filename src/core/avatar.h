/* 10x6 CP437 avatars: 120 bytes of (char, attr) pairs, row-major, the same
 * format as Synchronet's avatar_lib.js and the universal door (AVATARS.md). */
#ifndef AC_AVATAR_H
#define AC_AVATAR_H
#include <stddef.h>

#define AVATAR_W 10
#define AVATAR_H 6
#define AVATAR_BYTES 120

/* 1 when the 120 bytes pass the shared validation rule. */
int avatar_valid(const unsigned char a[AVATAR_BYTES]);

/* Decode a base64 payload into out; 1 on success (exactly 120 valid bytes). */
int avatar_from_base64(const char *b64, size_t n, unsigned char out[AVATAR_BYTES]);

/* Pick the i-th avatar out of a .bin collection (N x 120 bytes, optional
 * SAUCE trailer). Returns the number of avatars in the file, or 0; fills
 * out when 0 <= i < count. */
int avatar_from_collection(const unsigned char *data, size_t n, int i, unsigned char out[AVATAR_BYTES]);

#endif
