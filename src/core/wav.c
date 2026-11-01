#include "wav.h"
#include <string.h>
#include <stdio.h>

static unsigned le16(const unsigned char *p) { return p[0] | (p[1] << 8); }
static unsigned long le32(const unsigned char *p) { return (unsigned long)p[0] | ((unsigned long)p[1] << 8) | ((unsigned long)p[2] << 16) | ((unsigned long)p[3] << 24); }

int wav_is(const unsigned char *d, size_t n) { return n >= 12 && memcmp(d, "RIFF", 4) == 0 && memcmp(d + 8, "WAVE", 4) == 0; }

int wav_open(wav_reader *r, const unsigned char *d, size_t n, char *err, size_t errcap) {
    size_t pos = 12;
    int have_fmt = 0;
    memset(r, 0, sizeof *r);
    if (!wav_is(d, n)) { snprintf(err, errcap, "not a WAV"); return 0; }
    while (pos + 8 <= n) {
        unsigned long len = le32(d + pos + 4);
        const unsigned char *body = d + pos + 8;
        if (len > n - pos - 8) len = n - pos - 8;
        if (memcmp(d + pos, "fmt ", 4) == 0 && len >= 16) {
            unsigned fmt = le16(body);
            r->channels = (int)le16(body + 2);
            r->rate = (int)le32(body + 4);
            r->bits = (int)le16(body + 14);
            if (fmt == 0xFFFE && len >= 26) fmt = le16(body + 24); /* extensible: the sub-format's first word */
            if (fmt != 1 || (r->bits != 8 && r->bits != 16) || r->channels < 1 || r->channels > 2 || r->rate < 8000 || r->rate > 192000) {
                snprintf(err, errcap, "unsupported WAV (%u-bit format %u)", r->bits, fmt);
                return 0;
            }
            have_fmt = 1;
        } else if (memcmp(d + pos, "data", 4) == 0 && have_fmt) {
            r->pcm = body;
            r->frames = len / ((size_t)r->channels * (size_t)(r->bits / 8));
            return 1;
        }
        pos += 8 + len + (len & 1);
    }
    snprintf(err, errcap, "WAV has no data");
    return 0;
}

int wav_pull(wav_reader *r, short *out, int max) {
    int i, c;
    size_t left = r->frames - r->pos;
    if ((size_t)max > left) max = (int)left;
    for (i = 0; i < max; i++)
        for (c = 0; c < r->channels; c++) {
            size_t k = (r->pos + (size_t)i) * (size_t)r->channels + (size_t)c;
            if (r->bits == 8) out[i * r->channels + c] = (short)(((int)r->pcm[k] - 128) << 8);
            else out[i * r->channels + c] = (short)le16(r->pcm + k * 2);
        }
    r->pos += (size_t)max;
    return max;
}
