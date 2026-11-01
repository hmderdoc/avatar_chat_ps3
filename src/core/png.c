#include "png.h"
#include "inflate.h"
#include <string.h>
#include <stdio.h>

static unsigned be32(const unsigned char *p) { return ((unsigned)p[0] << 24) | ((unsigned)p[1] << 16) | ((unsigned)p[2] << 8) | p[3]; }

int png_is(const unsigned char *d, size_t n) {
    static const unsigned char sig[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
    return n >= 8 && memcmp(d, sig, 8) == 0;
}

int png_size(const unsigned char *d, size_t n, int *w, int *h) {
    if (!png_is(d, n) || n < 33 || memcmp(d + 12, "IHDR", 4) != 0) return 0;
    *w = (int)be32(d + 16);
    *h = (int)be32(d + 20);
    return *w > 0 && *h > 0 && *w <= 16384 && *h <= 16384;
}

static int channels_of(int ct) {
    switch (ct) { case 0: return 1; case 2: return 3; case 3: return 1; case 4: return 2; case 6: return 4; default: return 0; }
}

size_t png_scratch(int w, int h) {
    /* The raw (filtered) image at up to 8 bytes per pixel plus a filter
     * byte per row, which also covers the Adam7 passes together. */
    return (size_t)h * ((size_t)w * 8 + 1) + 64;
}

static int paeth(int a, int b, int c) {
    int p = a + b - c, pa = p > a ? p - a : a - p, pb = p > b ? p - b : b - p, pc = p > c ? p - c : c - p;
    return (pa <= pb && pa <= pc) ? a : pb <= pc ? b : c;
}

/* Unfilters rows in place: in points at filter-byte-prefixed rows. */
static int unfilter(unsigned char *in, int rows, size_t stride, int bpp) {
    size_t i;
    int y;
    unsigned char *prev = NULL;
    for (y = 0; y < rows; y++) {
        unsigned char *row = in + (size_t)y * (stride + 1);
        int f = row[0];
        unsigned char *cur = row + 1;
        switch (f) {
        case 0: break;
        case 1: for (i = (size_t)bpp; i < stride; i++) cur[i] += cur[i - bpp]; break;
        case 2: if (prev) for (i = 0; i < stride; i++) cur[i] += prev[i]; break;
        case 3: for (i = 0; i < stride; i++) cur[i] += (unsigned char)(((i >= (size_t)bpp ? cur[i - bpp] : 0) + (prev ? prev[i] : 0)) / 2); break;
        case 4: for (i = 0; i < stride; i++) cur[i] += (unsigned char)paeth(i >= (size_t)bpp ? cur[i - bpp] : 0, prev ? prev[i] : 0, (prev && i >= (size_t)bpp) ? prev[i - bpp] : 0); break;
        default: return 0;
        }
        prev = cur;
    }
    return 1;
}

/* Reads sample x of a row at the given bit depth and channel count. */
static unsigned sample(const unsigned char *row, int depth, int idx) {
    switch (depth) {
    case 1: return (row[idx >> 3] >> (7 - (idx & 7))) & 1;
    case 2: return (row[idx >> 2] >> ((3 - (idx & 3)) * 2)) & 3;
    case 4: return (row[idx >> 1] >> ((1 - (idx & 1)) * 4)) & 15;
    case 8: return row[idx];
    default: return row[idx * 2]; /* 16-bit: high byte */
    }
}

static void emit_pixel(unsigned char *out, int ct, int depth, const unsigned char *row, int x, const unsigned char *pal, int npal, const int *trns_gray, int ntrns, const unsigned char *trns_pal) {
    int ch = channels_of(ct);
    unsigned v[4] = { 0, 0, 0, 0 }, a = 255, i, maxv = depth == 16 ? 255 : (1u << depth) - 1;
    for (i = 0; i < (unsigned)ch; i++) v[i] = sample(row, depth, x * ch + (int)i);
    switch (ct) {
    case 0: /* grey */
        if (ntrns && trns_gray && (int)v[0] == trns_gray[0]) a = 0;
        v[0] = v[0] * 255 / maxv; v[1] = v[2] = v[0]; break;
    case 2: /* rgb */ break;
    case 3: /* palette */
        if ((int)v[0] < npal) { unsigned k = v[0]; if (trns_pal && (int)k < ntrns) a = trns_pal[k]; v[0] = pal[k * 3]; v[1] = pal[k * 3 + 1]; v[2] = pal[k * 3 + 2]; }
        else v[0] = v[1] = v[2] = 0;
        break;
    case 4: a = v[1]; v[0] = v[0] * 255 / maxv; v[1] = v[2] = v[0]; break;
    case 6: a = v[3]; break;
    default: break;
    }
    out[0] = (unsigned char)(v[0] * a / 255);
    out[1] = (unsigned char)(v[1] * a / 255);
    out[2] = (unsigned char)(v[2] * a / 255);
}

int png_decode(const unsigned char *d, size_t n, unsigned char *rgb, int w, int h,
               unsigned char *scratch, size_t scratch_n, char *err, size_t errcap) {
    int depth, ct, interlace, ch, bpp, npal = 0, ntrns = 0, trns_gray[1] = { -1 };
    const unsigned char *pal = NULL, *trns_pal = NULL;
    size_t pos = 8, idat_len = 0;
    unsigned char *zbuf, *raw;
    size_t zcap, rawcap;
    long rawlen;
    int iw, ih;
    if (!png_size(d, n, &iw, &ih) || iw != w || ih != h) { snprintf(err, errcap, "not a PNG"); return 0; }
    depth = d[24]; ct = d[25]; interlace = d[28];
    ch = channels_of(ct);
    if (!ch || (depth != 1 && depth != 2 && depth != 4 && depth != 8 && depth != 16) || d[26] || d[27]) { snprintf(err, errcap, "unsupported PNG"); return 0; }
    if (ct != 3 && ct != 0 && depth < 8) { snprintf(err, errcap, "unsupported PNG depth"); return 0; }
    rawcap = png_scratch(w, h);
    if (scratch_n < rawcap) { snprintf(err, errcap, "PNG too large"); return 0; }
    /* Compressed data is gathered at the end of the scratch area; the
     * inflated rows go at the front. A PNG's IDAT total is smaller than
     * the raw image in practice; refuse the rare one that is not. */
    raw = scratch;
    zcap = rawcap / 2;
    zbuf = scratch + rawcap - zcap;
    while (pos + 8 <= n) {
        unsigned len = be32(d + pos);
        const unsigned char *type = d + pos + 4, *body = d + pos + 8;
        if (pos + 12 + (size_t)len > n) break;
        if (memcmp(type, "PLTE", 4) == 0) { pal = body; npal = (int)(len / 3); }
        else if (memcmp(type, "tRNS", 4) == 0) {
            if (ct == 3) { trns_pal = body; ntrns = (int)len; }
            else if (ct == 0 && len >= 2) { trns_gray[0] = (body[0] << 8 | body[1]) >> (depth == 16 ? 8 : 0); ntrns = 1; }
        } else if (memcmp(type, "IDAT", 4) == 0) {
            if (idat_len + len > zcap) { snprintf(err, errcap, "PNG data too large"); return 0; }
            memcpy(zbuf + idat_len, body, len);
            idat_len += len;
        } else if (memcmp(type, "IEND", 4) == 0) break;
        pos += 12 + len;
    }
    if (idat_len == 0) { snprintf(err, errcap, "PNG has no image data"); return 0; }
    rawlen = inflate_zlib(zbuf, idat_len, raw, rawcap - zcap);
    if (rawlen < 0) { snprintf(err, errcap, "corrupt PNG data"); return 0; }
    bpp = (ch * depth + 7) / 8;
    if (!interlace) {
        size_t stride = ((size_t)w * ch * depth + 7) / 8;
        int y, x;
        if ((size_t)rawlen < (stride + 1) * (size_t)h) { snprintf(err, errcap, "short PNG data"); return 0; }
        if (!unfilter(raw, h, stride, bpp)) { snprintf(err, errcap, "bad PNG filter"); return 0; }
        for (y = 0; y < h; y++) {
            const unsigned char *row = raw + (size_t)y * (stride + 1) + 1;
            for (x = 0; x < w; x++) emit_pixel(rgb + ((size_t)y * w + x) * 3, ct, depth, row, x, pal, npal, trns_gray, ntrns, trns_pal);
        }
        return 1;
    }
    {
        /* Adam7: seven passes, each a small image of its own. */
        static const int xs[7] = { 0, 4, 0, 2, 0, 1, 0 }, ys[7] = { 0, 0, 4, 0, 2, 0, 1 };
        static const int xd[7] = { 8, 8, 4, 4, 2, 2, 1 }, yd[7] = { 8, 8, 8, 4, 4, 2, 2 };
        size_t off = 0;
        int p;
        for (p = 0; p < 7; p++) {
            int pw = (w - xs[p] + xd[p] - 1) / xd[p], ph = (h - ys[p] + yd[p] - 1) / yd[p], y, x;
            size_t stride;
            if (pw <= 0 || ph <= 0) continue;
            stride = ((size_t)pw * ch * depth + 7) / 8;
            if (off + (stride + 1) * (size_t)ph > (size_t)rawlen) { snprintf(err, errcap, "short PNG data"); return 0; }
            if (!unfilter(raw + off, ph, stride, bpp)) { snprintf(err, errcap, "bad PNG filter"); return 0; }
            for (y = 0; y < ph; y++) {
                const unsigned char *row = raw + off + (size_t)y * (stride + 1) + 1;
                for (x = 0; x < pw; x++)
                    emit_pixel(rgb + ((size_t)(ys[p] + y * yd[p]) * w + (size_t)(xs[p] + x * xd[p])) * 3, ct, depth, row, x, pal, npal, trns_gray, ntrns, trns_pal);
            }
            off += (stride + 1) * (size_t)ph;
        }
        return 1;
    }
}
