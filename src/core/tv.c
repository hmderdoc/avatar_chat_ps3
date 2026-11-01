#include "tv.h"
#include "cp437.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define MSG_HELLO_CONSUMER 0x02
#define MSG_FRAME 0x10
#define MSG_AUDIO 0x11
#define CAP_AUDIO 0x01

void tv_init(tv_consumer *t, unsigned char *buf) {
    memset(t, 0, sizeof *t);
    t->buf = buf;
}

size_t tv_hello(unsigned char *out, size_t cap, const char *channel, int want_audio) {
    size_t cl = strlen(channel), pl = 2 + cl + (want_audio ? 1 : 0);
    if (cl > 255 || cap < 4 + pl) return 0;
    out[0] = (unsigned char)(pl >> 24); out[1] = (unsigned char)(pl >> 16); out[2] = (unsigned char)(pl >> 8); out[3] = (unsigned char)pl;
    out[4] = MSG_HELLO_CONSUMER;
    out[5] = (unsigned char)cl;
    memcpy(out + 6, channel, cl);
    if (want_audio) out[6 + cl] = CAP_AUDIO;
    return 4 + pl;
}

static unsigned be16(const unsigned char *p) { return ((unsigned)p[0] << 8) | p[1]; }
static unsigned long be32(const unsigned char *p) { return ((unsigned long)p[0] << 24) | ((unsigned long)p[1] << 16) | ((unsigned long)p[2] << 8) | p[3]; }

static int take_frame(tv_consumer *t, const unsigned char *p, size_t n) {
    int cols, rows, x, y;
    size_t caplen, need;
    const unsigned char *px;
    if (n < 9) return 0;
    cols = (int)be16(p + 1);
    rows = (int)be16(p + 3);
    caplen = be16(p + 7);
    if (cols <= 0 || rows <= 0 || 9 + caplen > n) return 0;
    px = p + 9 + caplen;
    need = (size_t)2 * rows * cols * 3;
    if (n - 9 - caplen < need) return 0;
    {
        size_t k = cp437_from_utf8((const char *)p + 9, caplen, (unsigned char *)t->frame.caption, sizeof t->frame.caption - 1);
        t->frame.caption[k] = 0;
    }
    /* Keep at most TV_MAX_COLS x TV_MAX_ROWS of it (the centre). */
    {
        int ox = cols > TV_MAX_COLS ? (cols - TV_MAX_COLS) / 2 : 0, oy = rows > TV_MAX_ROWS ? (rows - TV_MAX_ROWS) / 2 : 0;
        int w = cols > TV_MAX_COLS ? TV_MAX_COLS : cols, h = rows > TV_MAX_ROWS ? TV_MAX_ROWS : rows;
        for (y = 0; y < h; y++)
            for (x = 0; x < w; x++) {
                const unsigned char *a = px + ((size_t)(2 * (y + oy)) * cols + (x + ox)) * 3;
                const unsigned char *b = px + ((size_t)(2 * (y + oy) + 1) * cols + (x + ox)) * 3;
                t->frame.top[y * w + x] = ((unsigned)a[0] << 16) | ((unsigned)a[1] << 8) | a[2];
                t->frame.bot[y * w + x] = ((unsigned)b[0] << 16) | ((unsigned)b[1] << 8) | b[2];
            }
        t->frame.cols = w;
        t->frame.rows = h;
    }
    t->frame.frames++;
    return 1;
}

static void take_audio(tv_consumer *t, const unsigned char *p, size_t n) {
    const size_t hdr = 1 + 1 + 1 + 4 + 4 + 8 + 4;
    if (n < hdr) return;
    t->audio_codec = p[1];
    t->audio_channels = p[2];
    t->audio_rate = (unsigned)be32(p + 3);
    t->audio = p + hdr;
    t->audio_n = n - hdr;
    t->have_audio = 1;
}

int tv_feed(tv_consumer *t, const unsigned char *data, size_t n) {
    int frames = 0;
    t->have_audio = 0;
    while (n > 0) {
        if (t->need == 0) {
            /* Gather the 4-byte length. */
            size_t take = 4 - t->len;
            if (take > n) take = n;
            memcpy(t->buf + t->len, data, take);
            t->len += take; data += take; n -= take;
            if (t->len < 4) break;
            t->need = be32(t->buf);
            t->len = 0;
            if (t->need == 0 || t->need > TV_MAX_MSG) { t->dropped++; t->need = 0; return -1; } /* protocol error */
            continue;
        }
        {
            size_t take = t->need - t->len;
            if (take > n) take = n;
            memcpy(t->buf + t->len, data, take);
            t->len += take; data += take; n -= take;
            if (t->len < t->need) break;
            if (t->buf[0] == MSG_FRAME) frames += take_frame(t, t->buf, t->len);
            else if (t->buf[0] == MSG_AUDIO) take_audio(t, t->buf, t->len);
            t->need = 0;
            t->len = 0;
            if (t->have_audio) break; /* hand the chunk over before it is overwritten */
        }
    }
    return frames;
}

int tv_parse_marker(const char *s, char *host, size_t hcap, int *port, char *channel, size_t ccap) {
    const char *p, *q;
    size_t k;
    if (strncmp(s, "[TVTUNER|", 9) != 0) return 0;
    p = s + 9;
    k = strlen(p);
    if (k == 0 || p[k - 1] != ']') return 0;
    if (strncasecmp(p, "off]", 4) == 0) return 2;
    q = strchr(p, '|');
    if (!q || (size_t)(q - p) >= hcap) return 0;
    memcpy(host, p, (size_t)(q - p)); host[q - p] = 0;
    p = q + 1;
    *port = atoi(p);
    q = strchr(p, '|');
    if (!q || *port <= 0 || *port > 65535) return 0;
    p = q + 1;
    k = strlen(p) - 1; /* without ] */
    if (k == 0 || k >= ccap) return 0;
    memcpy(channel, p, k); channel[k] = 0;
    while (host[0] == ' ') memmove(host, host + 1, strlen(host));
    return 1;
}
