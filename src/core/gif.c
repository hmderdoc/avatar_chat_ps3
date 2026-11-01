#include "gif.h"
#include <string.h>
#include <stdio.h>

int gif_is(const unsigned char *d, size_t n) { return n >= 6 && memcmp(d, "GIF8", 4) == 0 && (d[4] == '7' || d[4] == '9') && d[5] == 'a'; }

int gif_size(const unsigned char *d, size_t n, int *w, int *h) {
    if (!gif_is(d, n) || n < 13) return 0;
    *w = d[6] | (d[7] << 8);
    *h = d[8] | (d[9] << 8);
    return *w > 0 && *h > 0 && *w <= 16384 && *h <= 16384;
}

/* LZW tables: prefix, suffix and first-byte per code, 4096 codes. */
size_t gif_scratch(void) { return 4096 * 4 + 4096; }

static void fill_bg(unsigned char *rgb, int w, int h, const unsigned char *pal, int bg, int has_pal, int transparent) {
    unsigned char r = 0, g = 0, b = 0;
    size_t i;
    if (has_pal && !transparent) { r = pal[bg * 3]; g = pal[bg * 3 + 1]; b = pal[bg * 3 + 2]; }
    for (i = 0; i < (size_t)w * h; i++) { rgb[i * 3] = r; rgb[i * 3 + 1] = g; rgb[i * 3 + 2] = b; }
}

typedef struct { const unsigned char *d; size_t n, pos; unsigned char sub[256]; int sublen, subpos; unsigned long bits; int nbits; int eod; } lzw_in;

static int lzw_byte(lzw_in *in) {
    if (in->subpos >= in->sublen) {
        if (in->pos >= in->n) { in->eod = 1; return -1; }
        in->sublen = in->d[in->pos++];
        if (in->sublen == 0 || in->pos + (size_t)in->sublen > in->n) { in->eod = 1; return -1; }
        memcpy(in->sub, in->d + in->pos, (size_t)in->sublen);
        in->pos += (size_t)in->sublen;
        in->subpos = 0;
    }
    return in->sub[in->subpos++];
}

static int lzw_code(lzw_in *in, int size) {
    int c;
    while (in->nbits < size) {
        int b = lzw_byte(in);
        if (b < 0) return -1;
        in->bits |= (unsigned long)b << in->nbits;
        in->nbits += 8;
    }
    c = (int)(in->bits & ((1UL << size) - 1));
    in->bits >>= size;
    in->nbits -= size;
    return c;
}

int gif_decode(const unsigned char *d, size_t n, unsigned char *rgb, int w, int h,
               unsigned char *scratch, size_t scratch_n, char *err, size_t errcap) {
    int gw, gh, flags, has_gpal, gpal_n, bg, transparent = -1;
    const unsigned char *gpal = NULL;
    size_t pos;
    if (!gif_size(d, n, &gw, &gh) || gw != w || gh != h) { snprintf(err, errcap, "not a GIF"); return 0; }
    if (scratch_n < gif_scratch()) { snprintf(err, errcap, "no scratch"); return 0; }
    flags = d[10];
    bg = d[11];
    has_gpal = flags & 0x80;
    gpal_n = 2 << (flags & 7);
    pos = 13;
    if (has_gpal) { gpal = d + pos; pos += (size_t)gpal_n * 3; }
    if (pos > n) { snprintf(err, errcap, "short GIF"); return 0; }
    for (;;) {
        unsigned char block;
        if (pos >= n) { snprintf(err, errcap, "GIF has no image"); return 0; }
        block = d[pos++];
        if (block == 0x3B) { snprintf(err, errcap, "GIF has no image"); return 0; }
        if (block == 0x21) { /* extension */
            unsigned char label;
            if (pos >= n) { snprintf(err, errcap, "short GIF"); return 0; }
            label = d[pos++];
            if (label == 0xF9 && pos + 6 <= n && d[pos] == 4) {
                if (d[pos + 1] & 1) transparent = d[pos + 4];
            }
            while (pos < n && d[pos] != 0) pos += 1 + d[pos];
            pos++;
            continue;
        }
        if (block != 0x2C) { snprintf(err, errcap, "bad GIF block"); return 0; }
        {
            int ix, iy, iw, ih, iflags, interlaced, has_lpal, lpal_n, min_code, clear, eoi, size, avail, old, first, x, y, pass;
            const unsigned char *pal = gpal;
            unsigned short *prefix = (unsigned short *)scratch;
            unsigned char *suffix = scratch + 4096 * 2, *firstb = scratch + 4096 * 3, *stack = scratch + 4096 * 4;
            lzw_in in;
            static const int ipass_start[4] = { 0, 4, 2, 1 }, ipass_step[4] = { 8, 8, 4, 2 };
            if (pos + 9 > n) { snprintf(err, errcap, "short GIF"); return 0; }
            ix = d[pos] | (d[pos + 1] << 8); iy = d[pos + 2] | (d[pos + 3] << 8);
            iw = d[pos + 4] | (d[pos + 5] << 8); ih = d[pos + 6] | (d[pos + 7] << 8);
            iflags = d[pos + 8];
            pos += 9;
            interlaced = iflags & 0x40;
            has_lpal = iflags & 0x80;
            lpal_n = 2 << (iflags & 7);
            if (has_lpal) { pal = d + pos; pos += (size_t)lpal_n * 3; }
            if (!pal) { snprintf(err, errcap, "GIF has no palette"); return 0; }
            if (pos >= n) { snprintf(err, errcap, "short GIF"); return 0; }
            fill_bg(rgb, w, h, gpal, bg, has_gpal && bg < gpal_n, transparent >= 0);
            min_code = d[pos++];
            if (min_code < 2 || min_code > 11) { snprintf(err, errcap, "bad GIF code size"); return 0; }
            clear = 1 << min_code; eoi = clear + 1;
            size = min_code + 1; avail = clear + 2; old = -1; first = 0;
            memset(&in, 0, sizeof in);
            in.d = d; in.n = n; in.pos = pos;
            x = 0; y = 0; pass = 0;
            {
                int code, sp;
                int pal_n = has_lpal ? lpal_n : gpal_n;
                int row = interlaced ? ipass_start[0] : 0;
                for (code = 0; code < clear; code++) { prefix[code] = 0xFFFF; suffix[code] = firstb[code] = (unsigned char)code; }
                while ((code = lzw_code(&in, size)) >= 0) {
                    int cur, c;
                    if (code == clear) { size = min_code + 1; avail = clear + 2; old = -1; continue; }
                    if (code == eoi) break;
                    if (old < 0) { cur = code; }
                    else if (code < avail) cur = code;
                    else if (code == avail) cur = old; /* KwKwK */
                    else { snprintf(err, errcap, "bad GIF code"); return 0; }
                    /* Walk the chain into the stack. */
                    sp = 0;
                    c = cur;
                    if (code == avail) { stack[sp++] = (unsigned char)first; c = old; }
                    while (c != 0xFFFF && c >= clear + 2 && sp < 4095) { stack[sp++] = suffix[c]; c = prefix[c]; }
                    if (c != 0xFFFF) stack[sp++] = suffix[c];
                    first = stack[sp - 1];
                    if (old >= 0 && avail < 4096) {
                        prefix[avail] = (unsigned short)old;
                        suffix[avail] = (unsigned char)first;
                        firstb[avail] = (unsigned char)first;
                        avail++;
                        if (avail == (1 << size) && size < 12) size++;
                    }
                    old = code;
                    while (sp > 0) {
                        int idx = stack[--sp];
                        if (y < ih) {
                            int px = ix + x, py = iy + row;
                            if (px >= 0 && px < w && py >= 0 && py < h && idx != transparent && idx < pal_n) {
                                unsigned char *o = rgb + ((size_t)py * w + px) * 3;
                                o[0] = pal[idx * 3]; o[1] = pal[idx * 3 + 1]; o[2] = pal[idx * 3 + 2];
                            }
                            if (++x >= iw) {
                                x = 0;
                                y++;
                                if (interlaced) {
                                    row += ipass_step[pass];
                                    while (row >= ih && pass < 3) { pass++; row = ipass_start[pass]; }
                                } else row++;
                            }
                        }
                    }
                }
            }
            if (y == 0 && x == 0) { snprintf(err, errcap, "empty GIF"); return 0; }
            return 1;
        }
    }
}
