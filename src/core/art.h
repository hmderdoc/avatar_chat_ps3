/* [BITMAP|...] art envelopes: ANSI art and images sent as a grid of CP437
 * cells with 256-colour foreground and background, the way the JS door,
 * the universal door and the web page exchange them. */
#ifndef AC_ART_H
#define AC_ART_H
#include <stddef.h>

#define ART_MAX_W 200
#define ART_MAX_H 100
#define ART_CELLS (ART_MAX_W * ART_MAX_H)

typedef struct {
    int w, h;
    unsigned char ch[ART_CELLS];
    unsigned char fg[ART_CELLS];  /* palette index 0-255 */
    unsigned char bg[ART_CELLS];
} art_image;

/* 1 if text starts like an envelope: "[BITMAP|". */
int art_is_envelope(const char *text);

/* Decode "[BITMAP|<w>|<h>|<from>|<hex of zlib(payload)>]" into img.
 * from (may be NULL) receives the name field. Returns 1 on success. */
int art_parse(const char *text, size_t n, art_image *img, char *from, size_t fromcap);

#endif
