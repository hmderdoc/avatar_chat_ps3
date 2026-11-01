#include "art.h"
#include "inflate.h"
#include <string.h>
#include <stdlib.h>

int art_is_envelope(const char *text) { return strncmp(text, "[BITMAP|", 8) == 0; }

static int hexval(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* The hex payload can be most of a 128 KB packet; the inflated grid is at
 * most 3 bytes per cell plus one. Static scratch, single-threaded. */
static unsigned char packed[65536];
static unsigned char raw[3 * ART_CELLS + 1];

int art_parse(const char *text, size_t n, art_image *img, char *from, size_t fromcap) {
    const char *p = text, *end = text + n, *f[5];
    size_t i, k, plen = 0;
    long rawlen;
    int rows, cells, w;
    if (n < 10 || !art_is_envelope(text) || text[n - 1] != ']') return 0;
    /* Fields: BITMAP, w, h, from, hex. */
    f[0] = p + 1;
    for (i = 1; i < 5; i++) {
        const char *bar = memchr(f[i - 1], '|', (size_t)(end - f[i - 1]));
        if (!bar) return 0;
        f[i] = bar + 1;
    }
    if (from && fromcap) {
        size_t flen = (size_t)(f[4] - 1 - f[3]);
        if (flen >= fromcap) flen = fromcap - 1;
        memcpy(from, f[3], flen);
        from[flen] = 0;
    }
    for (p = f[4]; p + 1 < end - 1 && plen < sizeof packed; p += 2) {
        int hi = hexval((unsigned char)p[0]), lo = hexval((unsigned char)p[1]);
        if (hi < 0 || lo < 0) return 0;
        packed[plen++] = (unsigned char)((hi << 4) | lo);
    }
    if (p < end - 1) return 0; /* did not fit */
    rawlen = inflate_zlib(packed, plen, raw, sizeof raw);
    if (rawlen < 4) return 0;
    rows = raw[0];
    cells = (int)((rawlen - 1) / 3);
    if (rows <= 0 || cells <= 0 || cells % rows != 0) return 0;
    w = cells / rows;
    if (w > ART_MAX_W || rows > ART_MAX_H) return 0;
    img->w = w;
    img->h = rows;
    for (k = 0; k < (size_t)cells; k++) {
        img->fg[k] = raw[1 + k];
        img->bg[k] = raw[1 + cells + k];
        img->ch[k] = raw[1 + 2 * cells + k];
        if (img->ch[k] == 0) img->ch[k] = ' ';
    }
    return 1;
}
