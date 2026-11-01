#include "inflate.h"
#include <string.h>

#define MAXBITS 15
#define MAXLCODES 286
#define MAXDCODES 30
#define FIXLCODES 288

typedef struct {
    const unsigned char *in;
    size_t inlen, inpos;
    unsigned long bitbuf;
    int bitcnt;
    unsigned char *out;
    size_t outlen, cap;
    int err;
} st;

typedef struct {
    short count[MAXBITS + 1];
    short symbol[FIXLCODES];
} huff;

static int bits(st *s, int need) {
    unsigned long val = s->bitbuf;
    while (s->bitcnt < need) {
        if (s->inpos >= s->inlen) { s->err = 1; return 0; }
        val |= (unsigned long)s->in[s->inpos++] << s->bitcnt;
        s->bitcnt += 8;
    }
    s->bitbuf = val >> need;
    s->bitcnt -= need;
    return (int)(val & ((1UL << need) - 1));
}

static int construct(huff *h, const short *length, int n) {
    short offs[MAXBITS + 1];
    int symbol, len, left;
    for (len = 0; len <= MAXBITS; len++) h->count[len] = 0;
    for (symbol = 0; symbol < n; symbol++) h->count[length[symbol]]++;
    if (h->count[0] == n) return 0;
    left = 1;
    for (len = 1; len <= MAXBITS; len++) {
        left <<= 1;
        left -= h->count[len];
        if (left < 0) return -1; /* over-subscribed */
    }
    offs[1] = 0;
    for (len = 1; len < MAXBITS; len++) offs[len + 1] = offs[len] + h->count[len];
    for (symbol = 0; symbol < n; symbol++)
        if (length[symbol] != 0) h->symbol[offs[length[symbol]]++] = (short)symbol;
    return left;
}

static int decode(st *s, const huff *h) {
    int len, code = 0, first = 0, count, index = 0;
    for (len = 1; len <= MAXBITS; len++) {
        code |= bits(s, 1);
        if (s->err) return -1;
        count = h->count[len];
        if (code - count < first) return h->symbol[index + (code - first)];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    return -1;
}

static int put(st *s, unsigned char c) {
    if (s->outlen >= s->cap) return -1;
    s->out[s->outlen++] = c;
    return 0;
}

static int stored(st *s) {
    unsigned len, nlen;
    s->bitbuf = 0;
    s->bitcnt = 0;
    if (s->inpos + 4 > s->inlen) return -1;
    len = s->in[s->inpos] | (s->in[s->inpos + 1] << 8);
    nlen = s->in[s->inpos + 2] | (s->in[s->inpos + 3] << 8);
    s->inpos += 4;
    if (len != (~nlen & 0xffff)) return -1;
    if (s->inpos + len > s->inlen || s->outlen + len > s->cap) return -1;
    memcpy(s->out + s->outlen, s->in + s->inpos, len);
    s->inpos += len;
    s->outlen += len;
    return 0;
}

static const short lbase[29] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
static const short lext[29] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
static const short dbase[30] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577 };
static const short dext[30] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };

static int codes(st *s, const huff *lencode, const huff *distcode) {
    int symbol, len;
    unsigned dist;
    do {
        symbol = decode(s, lencode);
        if (symbol < 0) return -1;
        if (symbol < 256) {
            if (put(s, (unsigned char)symbol)) return -1;
        } else if (symbol > 256) {
            symbol -= 257;
            if (symbol >= 29) return -1;
            len = lbase[symbol] + bits(s, lext[symbol]);
            symbol = decode(s, distcode);
            if (symbol < 0 || symbol >= 30) return -1;
            dist = (unsigned)dbase[symbol] + (unsigned)bits(s, dext[symbol]);
            if (s->err || dist > s->outlen) return -1;
            if (s->outlen + (size_t)len > s->cap) return -1;
            while (len--) { s->out[s->outlen] = s->out[s->outlen - dist]; s->outlen++; }
        }
        if (s->err) return -1;
    } while (symbol != 256);
    return 0;
}

static int fixed(st *s) {
    static huff lencode, distcode;
    static int built;
    if (!built) {
        short lengths[FIXLCODES];
        int symbol;
        for (symbol = 0; symbol < 144; symbol++) lengths[symbol] = 8;
        for (; symbol < 256; symbol++) lengths[symbol] = 9;
        for (; symbol < 280; symbol++) lengths[symbol] = 7;
        for (; symbol < FIXLCODES; symbol++) lengths[symbol] = 8;
        construct(&lencode, lengths, FIXLCODES);
        for (symbol = 0; symbol < MAXDCODES; symbol++) lengths[symbol] = 5;
        construct(&distcode, lengths, MAXDCODES);
        built = 1;
    }
    return codes(s, &lencode, &distcode);
}

static int dynamic(st *s) {
    static const short order[19] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };
    short lengths[MAXLCODES + MAXDCODES];
    huff lencode, distcode;
    int nlen, ndist, ncode, index, err;
    nlen = bits(s, 5) + 257;
    ndist = bits(s, 5) + 1;
    ncode = bits(s, 4) + 4;
    if (s->err || nlen > MAXLCODES || ndist > MAXDCODES) return -1;
    for (index = 0; index < ncode; index++) lengths[order[index]] = (short)bits(s, 3);
    for (; index < 19; index++) lengths[order[index]] = 0;
    if (s->err || construct(&lencode, lengths, 19) != 0) return -1;
    index = 0;
    while (index < nlen + ndist) {
        int symbol = decode(s, &lencode), len;
        if (symbol < 0) return -1;
        if (symbol < 16) lengths[index++] = (short)symbol;
        else {
            len = 0;
            if (symbol == 16) {
                if (index == 0) return -1;
                len = lengths[index - 1];
                symbol = 3 + bits(s, 2);
            } else if (symbol == 17) symbol = 3 + bits(s, 3);
            else symbol = 11 + bits(s, 7);
            if (s->err || index + symbol > nlen + ndist) return -1;
            while (symbol--) lengths[index++] = (short)len;
        }
    }
    if (lengths[256] == 0) return -1;
    err = construct(&lencode, lengths, nlen);
    if (err < 0 || (err > 0 && nlen - lencode.count[0] != 1)) return -1;
    err = construct(&distcode, lengths + nlen, ndist);
    if (err < 0 || (err > 0 && ndist - distcode.count[0] != 1)) return -1;
    return codes(s, &lencode, &distcode);
}

long inflate_raw(const unsigned char *in, size_t n, unsigned char *out, size_t cap) {
    st s;
    int last, type, err;
    memset(&s, 0, sizeof s);
    s.in = in; s.inlen = n; s.out = out; s.cap = cap;
    do {
        last = bits(&s, 1);
        type = bits(&s, 2);
        if (s.err) return -1;
        err = type == 0 ? stored(&s) : type == 1 ? fixed(&s) : type == 2 ? dynamic(&s) : -1;
        if (err) return -1;
    } while (!last);
    return (long)s.outlen;
}

long inflate_zlib(const unsigned char *in, size_t n, unsigned char *out, size_t cap) {
    if (n < 6 || (in[0] & 0x0f) != 8 || ((in[0] << 8) | in[1]) % 31 != 0 || (in[1] & 0x20)) return -1;
    return inflate_raw(in + 2, n - 2, out, cap);
}
