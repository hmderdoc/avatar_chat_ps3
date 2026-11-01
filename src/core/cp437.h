/* CP437 <-> UTF-8. The wire carries UTF-8 (JSON); the screen, the font and
 * avatars are CP437 bytes, as on every BBS client of this chat. */
#ifndef AC_CP437_H
#define AC_CP437_H
#include <stddef.h>

extern const unsigned short cp437_to_unicode[256];

/* Convert UTF-8 text to CP437 bytes. Characters CP437 lacks become '?',
 * control characters (except TAB, which becomes a space) are dropped.
 * Returns the number of bytes written (at most cap). */
size_t cp437_from_utf8(const char *s, size_t n, unsigned char *out, size_t cap);

/* Convert CP437 bytes to UTF-8. Needs up to 3 bytes per input byte plus a
 * NUL. Returns the length written. 0x00 becomes a space. */
size_t cp437_to_utf8(const unsigned char *s, size_t n, char *out, size_t cap);

/* One CP437 byte to UTF-8; returns the byte count (1-3), writes no NUL. */
size_t cp437_byte_to_utf8(unsigned char b, char out[4]);

#endif
