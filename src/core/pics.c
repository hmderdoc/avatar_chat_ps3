#include "pics.h"
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>

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

pic *pics_get(pic_cache *pc, const char *url, fetch_kind kind) {
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
    victim->kind = kind;
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

int pics_is_audio_url(const char *url, size_t n) {
    static const char *exts[] = { ".mp3", ".ogg", ".oga", ".flac", ".wav" };
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

int pics_find_links(const unsigned char *text, int n, char out[][PIC_URL], int max, int (*accept)(const char *, size_t)) {
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
        if (!accept || accept((const char *)text + start, (size_t)len)) {
            memcpy(out[found], text + start, (size_t)len);
            out[found][len] = 0;
            found++;
        }
    }
    return found;
}

/* ---- link cards ---------------------------------------------------------- */

/* Fold one code point to printable ASCII, or -1 to drop it. */
static int fold_cp(unsigned long cp) {
    static const char *lat = "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYTsaaaaaaaceeeeiiiidnooooo/ouuuuyty";
    if (cp >= 32 && cp < 127) return (int)cp;
    if (cp < 32 || cp == 127) return ' ';
    switch (cp) {
    case 0x2018: case 0x2019: case 0x201A: return '\'';
    case 0x201C: case 0x201D: case 0x201E: return '"';
    case 0x2013: case 0x2014: case 0x2212: return '-';
    case 0x2026: return '.';
    case 0x00A0: case 0x2009: case 0x200A: case 0x202F: case 0x3000: return ' ';
    case 0x2022: case 0x00B7: return '*';
    default:
        if (cp >= 0xC0 && cp <= 0xFF) return lat[cp - 0xC0];
        return -1;
    }
}

/* Decode the entities pages use and fold the text to printable ASCII with
 * runs of whitespace collapsed, the way the door's cards do. */
static void fold_text(const char *in, size_t n, char *out, size_t cap) {
    size_t i = 0, o = 0;
    int space = 1;
    while (i < n && o + 1 < cap) {
        unsigned char c = (unsigned char)in[i];
        int ch;
        if (c == '&') {
            static const struct { const char *e; unsigned long cp; } ents[] = { { "&amp;", '&' }, { "&lt;", '<' }, { "&gt;", '>' }, { "&quot;", '"' }, { "&apos;", '\'' }, { "&nbsp;", ' ' }, { "&ndash;", 0x2013 }, { "&mdash;", 0x2014 }, { "&hellip;", 0x2026 }, { "&lsquo;", 0x2018 }, { "&rsquo;", 0x2019 }, { "&ldquo;", 0x201C }, { "&rdquo;", 0x201D } };
            size_t k;
            int hit = 0;
            ch = '&';
            for (k = 0; k < sizeof ents / sizeof ents[0]; k++) {
                size_t el = strlen(ents[k].e);
                if (i + el <= n && ieq_n(in + i, ents[k].e, el)) { ch = fold_cp(ents[k].cp); i += el; hit = 1; break; }
            }
            if (!hit) {
                if (i + 2 < n && in[i + 1] == '#') {
                    int hex = in[i + 2] == 'x' || in[i + 2] == 'X';
                    char *end;
                    long v = strtol(in + i + 2 + hex, &end, hex ? 16 : 10);
                    if (end && end > in + i + 2 + hex && *end == ';' && v > 0) { ch = fold_cp((unsigned long)v); i = (size_t)(end - in) + 1; hit = 1; }
                }
                if (!hit) i++;
            }
        } else if (c >= 0x80) {
            size_t need = (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : (c & 0xF8) == 0xF0 ? 4 : 1;
            unsigned long cp;
            size_t k;
            if (need == 1 || i + need > n) { i++; continue; }
            cp = c & (0xFF >> (need + 1));
            for (k = 1; k < need; k++) cp = (cp << 6) | ((unsigned char)in[i + k] & 0x3F);
            i += need;
            ch = fold_cp(cp);
        } else { ch = fold_cp(c); i++; }
        if (ch < 0) continue;
        if (ch == ' ') { if (space) continue; space = 1; }
        else space = 0;
        out[o++] = (char)ch;
    }
    while (o > 0 && out[o - 1] == ' ') o--;
    out[o] = 0;
}

/* Value of the attribute named attr inside tag [tag, tagend). */
static int attr_value(const char *tag, const char *tagend, const char *attr, const char **val, size_t *vlen) {
    const char *p = tag;
    size_t al = strlen(attr);
    while (p + al < tagend) {
        if (ieq_n(p, attr, al) && (p == tag || !isalnum((unsigned char)p[-1]))) {
            const char *q = p + al;
            while (q < tagend && *q == ' ') q++;
            if (q < tagend && *q == '=') {
                q++;
                while (q < tagend && *q == ' ') q++;
                if (q < tagend && (*q == '"' || *q == '\'')) {
                    const char *e = memchr(q + 1, *q, (size_t)(tagend - q - 1));
                    if (!e) return 0;
                    *val = q + 1; *vlen = (size_t)(e - q - 1);
                    return 1;
                }
                *val = q;
                while (q < tagend && *q != ' ' && *q != '>') q++;
                *vlen = (size_t)(q - *val);
                return 1;
            }
        }
        p++;
    }
    return 0;
}

void pics_parse_page(pic *p, const char *html, size_t n) {
    const char *s = html, *end = html + n;
    char og_title[256] = "", og_desc[400] = "", og_site[96] = "", tw_title[256] = "", tw_desc[400] = "", title[256] = "", desc[400] = "";
    if (n > 192 * 1024) { n = 192 * 1024; end = html + n; } /* the door parses the first 192 KB */
    p->site[0] = p->title[0] = p->desc[0] = p->audio[0] = 0;
    while (s < end) {
        const char *lt = memchr(s, '<', (size_t)(end - s)), *gt;
        if (!lt) break;
        gt = memchr(lt, '>', (size_t)(end - lt));
        if (!gt) break;
        if (gt - lt > 5 && ieq_n(lt, "<meta", 5)) {
            const char *name, *content;
            size_t nl, cl;
            if ((attr_value(lt, gt, "property", &name, &nl) || attr_value(lt, gt, "name", &name, &nl)) && attr_value(lt, gt, "content", &content, &cl)) {
#define IS(k) (nl == strlen(k) && ieq_n(name, k, nl))
                if (IS("og:title") && !og_title[0]) fold_text(content, cl, og_title, sizeof og_title);
                else if (IS("og:description") && !og_desc[0]) fold_text(content, cl, og_desc, sizeof og_desc);
                else if (IS("og:site_name") && !og_site[0]) fold_text(content, cl, og_site, sizeof og_site);
                else if (IS("twitter:title") && !tw_title[0]) fold_text(content, cl, tw_title, sizeof tw_title);
                else if (IS("twitter:description") && !tw_desc[0]) fold_text(content, cl, tw_desc, sizeof tw_desc);
                else if (IS("description") && !desc[0]) fold_text(content, cl, desc, sizeof desc);
                else if ((IS("og:audio") || IS("og:audio:secure_url") || IS("og:audio:url")) && !p->audio[0] && cl < sizeof p->audio) {
                    size_t k;
                    for (k = 0; k < cl; k++) p->audio[k] = content[k];
                    p->audio[cl] = 0;
                    if (!(ieq_n(p->audio, "http://", 7) || ieq_n(p->audio, "https://", 8))) p->audio[0] = 0;
                }
#undef IS
            }
        } else if (gt - lt >= 6 && ieq_n(lt, "<title", 6) && !title[0]) {
            const char *close = NULL, *q = gt + 1;
            while (q < end) {
                const char *c2 = memchr(q, '<', (size_t)(end - q));
                if (!c2) break;
                if (end - c2 >= 8 && ieq_n(c2, "</title>", 8)) { close = c2; break; }
                q = c2 + 1;
            }
            if (close) fold_text(gt + 1, (size_t)(close - gt - 1), title, sizeof title);
        }
        s = gt + 1;
    }
    snprintf(p->title, sizeof p->title, "%s", og_title[0] ? og_title : tw_title[0] ? tw_title : title);
    snprintf(p->desc, sizeof p->desc, "%s", og_desc[0] ? og_desc : tw_desc[0] ? tw_desc : desc);
    snprintf(p->site, sizeof p->site, "%s", og_site);
    if (!p->site[0]) {
        /* The host, as the door shows when a page names no site. */
        const char *h = strstr(p->url, "://"), *e;
        size_t hl;
        if (h) {
            h += 3;
            if (ieq_n(h, "www.", 4)) h += 4;
            e = h;
            while (*e && *e != '/' && *e != ':' && *e != '?') e++;
            hl = (size_t)(e - h);
            if (hl >= sizeof p->site) hl = sizeof p->site - 1;
            memcpy(p->site, h, hl);
            p->site[hl] = 0;
        }
    }
}

/* ---- NSFW ------------------------------------------------------------------ */

static const char *nsfw_default =
    "nude,nudes,nudity,naked,nsfw,topless,sex,sexy,sexual,porn,porno,xxx,erotic,erotica,boobs,breasts,tits,"
    "nipples,nipple,genitals,penis,vagina,pussy,dick,cock,orgy,lingerie,thong,stripper,striptease,undressed,undressing";

static int word_in_list(const char *w, size_t wl, const char *list) {
    const char *p = list;
    while (*p) {
        const char *e = p;
        size_t kl;
        while (*e && *e != ',') e++;
        kl = (size_t)(e - p);
        while (kl > 0 && p[0] == ' ') { p++; kl--; }
        while (kl > 0 && p[kl - 1] == ' ') kl--;
        if (kl == wl && ieq_n(p, w, wl)) return 1;
        p = *e ? e + 1 : e;
    }
    return 0;
}

int pics_looks_nsfw(const char *text, const char *keywords) {
    char buf[1200];
    size_t i = 0, o = 0, n = strlen(text);
    if (!keywords) keywords = nsfw_default;
    if (!*keywords || strcmp(keywords, "none") == 0) return 0;
    /* Percent-decode and lower-case into buf. */
    while (i < n && o + 1 < sizeof buf) {
        unsigned char c = (unsigned char)text[i];
        if (c == '%' && i + 2 < n && isxdigit((unsigned char)text[i + 1]) && isxdigit((unsigned char)text[i + 2])) {
            char h[3] = { text[i + 1], text[i + 2], 0 };
            c = (unsigned char)strtol(h, NULL, 16);
            i += 3;
        } else i++;
        buf[o++] = (char)tolower(c);
    }
    buf[o] = 0;
    for (i = 0; i < o;) {
        size_t start;
        while (i < o && !isalnum((unsigned char)buf[i])) i++;
        start = i;
        while (i < o && isalnum((unsigned char)buf[i])) i++;
        if (i > start && word_in_list(buf + start, i - start, keywords)) return 1;
    }
    return 0;
}
