#include "pics.h"
#include <string.h>
#include <stdio.h>
#include <ctype.h>

void pics_init(pic_cache *pc) { memset(pc, 0, sizeof *pc); }

pic *pics_find(pic_cache *pc, const char *url) {
    int i;
    for (i = 0; i < PIC_MAX; i++)
        if (pc->items[i].state != PIC_EMPTY && strcmp(pc->items[i].url, url) == 0) {
            pc->items[i].used = ++pc->tick;
            return &pc->items[i];
        }
    return NULL;
}

pic *pics_get(pic_cache *pc, const char *url) {
    pic *p = pics_find(pc, url), *victim = NULL;
    int i;
    if (p) return p;
    for (i = 0; i < PIC_MAX; i++) {
        pic *q = &pc->items[i];
        if (q->state == PIC_EMPTY) { victim = q; break; }
        if (q->state != PIC_LOADING && (!victim || q->used < victim->used)) victim = q;
    }
    if (!victim) victim = &pc->items[0];
    memset(victim, 0, sizeof *victim);
    snprintf(victim->url, sizeof victim->url, "%s", url);
    victim->state = PIC_QUEUED;
    victim->used = ++pc->tick;
    return victim;
}

pic *pics_next_queued(pic_cache *pc) {
    int i;
    pic *best = NULL;
    for (i = 0; i < PIC_MAX; i++)
        if (pc->items[i].state == PIC_QUEUED && (!best || pc->items[i].used < best->used)) best = &pc->items[i];
    return best;
}

void pics_fail(pic *p, const char *why) {
    p->state = PIC_FAILED;
    snprintf(p->why, sizeof p->why, "%s", why ? why : "failed");
}

void pics_set_rgb(pic *p, const unsigned char *rgb, int w, int h, int max_cols) {
    /* Half cells are square (8x8 px of a cell's 8x16), so the target grid
     * is cols x 2*rows samples at one aspect. */
    int max_rows2 = PIC_MAX_ROWS * 2, cols, rows2, rows, cx, cy;
    double sx, sy, scale;
    if (max_cols > PIC_MAX_COLS) max_cols = PIC_MAX_COLS;
    if (max_cols < 1) max_cols = 1;
    if (w <= 0 || h <= 0) { pics_fail(p, "empty image"); return; }
    sx = (double)max_cols / w;
    sy = (double)max_rows2 / h;
    scale = sx < sy ? sx : sy;
    if (scale > 1.0) scale = 1.0;
    cols = (int)(w * scale + 0.5); if (cols < 1) cols = 1;
    rows2 = (int)(h * scale + 0.5); if (rows2 < 1) rows2 = 1;
    rows = (rows2 + 1) / 2;
    p->cols = cols; p->rows = rows; p->src_w = w; p->src_h = h;
    for (cy = 0; cy < rows2; cy++) {
        int y0 = (int)((long long)cy * h / rows2), y1 = (int)((long long)(cy + 1) * h / rows2), y, x;
        if (y1 <= y0) y1 = y0 + 1;
        if (y1 > h) y1 = h;
        for (cx = 0; cx < cols; cx++) {
            int x0 = (int)((long long)cx * w / cols), x1 = (int)((long long)(cx + 1) * w / cols);
            unsigned long r = 0, g = 0, b = 0, cnt = 0;
            unsigned int rgbv;
            if (x1 <= x0) x1 = x0 + 1;
            if (x1 > w) x1 = w;
            for (y = y0; y < y1; y++)
                for (x = x0; x < x1; x++) {
                    const unsigned char *px = rgb + ((size_t)y * w + x) * 3;
                    r += px[0]; g += px[1]; b += px[2]; cnt++;
                }
            if (!cnt) cnt = 1;
            rgbv = (unsigned int)((r / cnt) << 16 | (g / cnt) << 8 | (b / cnt));
            if (cy & 1) p->bot[(cy / 2) * cols + cx] = rgbv; else p->top[(cy / 2) * cols + cx] = rgbv;
        }
    }
    if (rows2 & 1) { /* an odd last row: the bottom half repeats the top */
        for (cx = 0; cx < cols; cx++) p->bot[(rows - 1) * cols + cx] = p->top[(rows - 1) * cols + cx];
    }
    p->state = PIC_READY;
}

static int ieq_n(const char *a, const char *b, size_t n) {
    size_t i;
    for (i = 0; i < n; i++) if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i])) return 0;
    return 1;
}

int pics_is_image_url(const char *url, size_t n) {
    static const char *exts[] = { ".png", ".jpg", ".jpeg", ".gif", ".webp", ".bmp" };
    size_t end = n, i;
    if (n < 8) return 0;
    if (!(ieq_n(url, "http://", 7) || ieq_n(url, "https://", 8))) return 0;
    for (i = 0; i < n; i++) if (url[i] == '?' || url[i] == '#') { end = i; break; }
    for (i = 0; i < sizeof exts / sizeof exts[0]; i++) {
        size_t k = strlen(exts[i]);
        if (end >= k && ieq_n(url + end - k, exts[i], k)) return 1;
    }
    return 0;
}

int pics_find_links(const unsigned char *text, int n, char out[][PIC_URL], int max) {
    int i = 0, found = 0;
    while (i < n && found < max) {
        int start, len;
        if (!(i + 7 <= n && (ieq_n((const char *)text + i, "http://", 7) || (i + 8 <= n && ieq_n((const char *)text + i, "https://", 8))))) { i++; continue; }
        start = i;
        while (i < n && text[i] > ' ' && text[i] != '"' && text[i] != '<' && text[i] != '>') i++;
        len = i - start;
        /* Trailing punctuation belongs to the sentence. */
        while (len > 0 && strchr(".,;:!?)]}'", text[start + len - 1])) len--;
        if (len >= PIC_URL) len = PIC_URL - 1;
        if (pics_is_image_url((const char *)text + start, (size_t)len)) {
            memcpy(out[found], text + start, (size_t)len);
            out[found][len] = 0;
            found++;
        }
    }
    return found;
}
