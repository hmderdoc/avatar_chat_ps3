#include "json.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

void json_arena_init(json_arena *a, char *mem, size_t cap) {
    a->mem = mem;
    a->cap = cap;
    a->used = 0;
}

static void *arena_alloc(json_arena *a, size_t n) {
    size_t aligned = (a->used + 7) & ~(size_t)7;
    if (aligned + n > a->cap) return NULL;
    a->used = aligned + n;
    return a->mem + aligned;
}

typedef struct {
    const char *p, *end;
    json_arena *a;
    int depth;
} parser;

static void skip_ws(parser *ps) {
    while (ps->p < ps->end && (*ps->p == ' ' || *ps->p == '\t' || *ps->p == '\n' || *ps->p == '\r')) ps->p++;
}

static json_val *new_val(parser *ps, json_type t) {
    json_val *v = (json_val *)arena_alloc(ps->a, sizeof *v);
    if (!v) return NULL;
    memset(v, 0, sizeof *v);
    v->type = t;
    return v;
}

static int hexval(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static size_t put_utf8(unsigned long cp, char *out) {
    if (cp < 0x80) { out[0] = (char)cp; return 1; }
    if (cp < 0x800) { out[0] = (char)(0xC0 | (cp >> 6)); out[1] = (char)(0x80 | (cp & 0x3F)); return 2; }
    if (cp < 0x10000) {
        out[0] = (char)(0xE0 | (cp >> 12)); out[1] = (char)(0x80 | ((cp >> 6) & 0x3F)); out[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    out[0] = (char)(0xF0 | (cp >> 18)); out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((cp >> 6) & 0x3F)); out[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}

static int read_hex4(parser *ps, unsigned long *out) {
    unsigned long v = 0;
    int i;
    if (ps->end - ps->p < 4) return 0;
    for (i = 0; i < 4; i++) {
        int h = hexval((unsigned char)ps->p[i]);
        if (h < 0) return 0;
        v = (v << 4) | (unsigned long)h;
    }
    ps->p += 4;
    *out = v;
    return 1;
}

/* Parses a string at ps->p (which points at the opening quote) into the
 * arena. Returns the decoded string or NULL. */
static char *parse_string(parser *ps, size_t *outlen) {
    const char *q;
    size_t maxlen;
    char *dst, *w;
    if (ps->p >= ps->end || *ps->p != '"') return NULL;
    ps->p++;
    /* The decoded form is never longer than the encoded one. Find the end
     * first so we can size the allocation. */
    q = ps->p;
    while (q < ps->end && *q != '"') {
        if (*q == '\\') { q++; if (q >= ps->end) return NULL; }
        q++;
    }
    if (q >= ps->end) return NULL;
    maxlen = (size_t)(q - ps->p);
    dst = (char *)arena_alloc(ps->a, maxlen + 1);
    if (!dst) return NULL;
    w = dst;
    while (ps->p < q) {
        unsigned char c = (unsigned char)*ps->p;
        if (c == '\\') {
            ps->p++;
            c = (unsigned char)*ps->p++;
            switch (c) {
            case '"': *w++ = '"'; break;
            case '\\': *w++ = '\\'; break;
            case '/': *w++ = '/'; break;
            case 'b': *w++ = '\b'; break;
            case 'f': *w++ = '\f'; break;
            case 'n': *w++ = '\n'; break;
            case 'r': *w++ = '\r'; break;
            case 't': *w++ = '\t'; break;
            case 'u': {
                unsigned long cp;
                if (!read_hex4(ps, &cp)) return NULL;
                if (cp >= 0xD800 && cp <= 0xDBFF && ps->end - ps->p >= 6 && ps->p[0] == '\\' && ps->p[1] == 'u') {
                    unsigned long lo;
                    const char *save = ps->p;
                    ps->p += 2;
                    if (read_hex4(ps, &lo) && lo >= 0xDC00 && lo <= 0xDFFF) {
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    } else {
                        ps->p = save;
                        cp = 0xFFFD;
                    }
                } else if (cp >= 0xD800 && cp <= 0xDFFF) {
                    cp = 0xFFFD;
                }
                w += put_utf8(cp, w);
                break;
            }
            default:
                return NULL;
            }
        } else {
            *w++ = (char)c;
            ps->p++;
        }
    }
    ps->p = q + 1; /* past the closing quote */
    *w = 0;
    *outlen = (size_t)(w - dst);
    return dst;
}

static json_val *parse_value(parser *ps);

static json_val *parse_array(parser *ps) {
    json_val *arr = new_val(ps, J_ARRAY), *last = NULL;
    if (!arr) return NULL;
    ps->p++; /* [ */
    skip_ws(ps);
    if (ps->p < ps->end && *ps->p == ']') { ps->p++; return arr; }
    for (;;) {
        json_val *v;
        skip_ws(ps);
        v = parse_value(ps);
        if (!v) return NULL;
        if (last) last->next = v; else arr->first = v;
        last = v;
        arr->len++;
        skip_ws(ps);
        if (ps->p >= ps->end) return NULL;
        if (*ps->p == ',') { ps->p++; continue; }
        if (*ps->p == ']') { ps->p++; return arr; }
        return NULL;
    }
}

static json_val *parse_object(parser *ps) {
    json_val *obj = new_val(ps, J_OBJECT), *last = NULL;
    if (!obj) return NULL;
    ps->p++; /* { */
    skip_ws(ps);
    if (ps->p < ps->end && *ps->p == '}') { ps->p++; return obj; }
    for (;;) {
        char *key;
        size_t klen;
        json_val *v;
        skip_ws(ps);
        key = parse_string(ps, &klen);
        if (!key) return NULL;
        skip_ws(ps);
        if (ps->p >= ps->end || *ps->p != ':') return NULL;
        ps->p++;
        skip_ws(ps);
        v = parse_value(ps);
        if (!v) return NULL;
        v->key = key;
        if (last) last->next = v; else obj->first = v;
        last = v;
        obj->len++;
        skip_ws(ps);
        if (ps->p >= ps->end) return NULL;
        if (*ps->p == ',') { ps->p++; continue; }
        if (*ps->p == '}') { ps->p++; return obj; }
        return NULL;
    }
}

static int match_lit(parser *ps, const char *lit) {
    size_t n = strlen(lit);
    if ((size_t)(ps->end - ps->p) >= n && memcmp(ps->p, lit, n) == 0) { ps->p += n; return 1; }
    return 0;
}

static json_val *parse_number(parser *ps) {
    const char *start = ps->p;
    char buf[64];
    size_t n;
    json_val *v;
    int is_int = 1;
    if (ps->p < ps->end && *ps->p == '-') ps->p++;
    while (ps->p < ps->end && ((*ps->p >= '0' && *ps->p <= '9') || *ps->p == '.' || *ps->p == 'e' || *ps->p == 'E' || *ps->p == '+' || *ps->p == '-')) {
        if (*ps->p == '.' || *ps->p == 'e' || *ps->p == 'E') is_int = 0;
        ps->p++;
    }
    n = (size_t)(ps->p - start);
    if (n == 0 || n >= sizeof buf) return NULL;
    memcpy(buf, start, n);
    buf[n] = 0;
    v = new_val(ps, J_NUMBER);
    if (!v) return NULL;
    v->num = strtod(buf, NULL);
    if (is_int && n <= 19) {
        v->inum = strtoll(buf, NULL, 10);
        v->is_int = 1;
    } else {
        v->inum = (long long)v->num;
    }
    return v;
}

static json_val *parse_value(parser *ps) {
    json_val *v;
    if (ps->depth > 64) return NULL;
    skip_ws(ps);
    if (ps->p >= ps->end) return NULL;
    switch (*ps->p) {
    case '{': ps->depth++; v = parse_object(ps); ps->depth--; return v;
    case '[': ps->depth++; v = parse_array(ps); ps->depth--; return v;
    case '"': {
        size_t len;
        char *s;
        v = new_val(ps, J_STRING);
        if (!v) return NULL;
        s = parse_string(ps, &len);
        if (!s) return NULL;
        v->str = s;
        v->len = len;
        return v;
    }
    case 't': if (!match_lit(ps, "true")) return NULL; v = new_val(ps, J_BOOL); if (v) v->boolean = 1; return v;
    case 'f': if (!match_lit(ps, "false")) return NULL; v = new_val(ps, J_BOOL); return v;
    case 'n': if (!match_lit(ps, "null")) return NULL; return new_val(ps, J_NULL);
    default:
        if (*ps->p == '-' || (*ps->p >= '0' && *ps->p <= '9')) return parse_number(ps);
        return NULL;
    }
}

json_val *json_parse(const char *text, size_t n, json_arena *a) {
    parser ps;
    json_val *v;
    ps.p = text;
    ps.end = text + n;
    ps.a = a;
    ps.depth = 0;
    v = parse_value(&ps);
    if (!v) return NULL;
    skip_ws(&ps);
    if (ps.p != ps.end) return NULL;
    return v;
}

const json_val *json_get(const json_val *obj, const char *key) {
    const json_val *c;
    if (!obj || obj->type != J_OBJECT) return NULL;
    for (c = obj->first; c; c = c->next)
        if (c->key && strcmp(c->key, key) == 0) return c;
    return NULL;
}

const json_val *json_index(const json_val *arr, size_t i) {
    const json_val *c;
    if (!arr || arr->type != J_ARRAY) return NULL;
    for (c = arr->first; c && i > 0; c = c->next, i--) {}
    return c;
}

const char *json_str(const json_val *v, const char *dflt) {
    if (!v || v->type != J_STRING) return dflt;
    return v->str;
}

long long json_int(const json_val *v, long long dflt) {
    if (!v || v->type != J_NUMBER) return dflt;
    return v->is_int ? v->inum : (long long)v->num;
}

static void putc_cap(char *out, size_t cap, size_t *n, char c) {
    if (*n < cap) out[*n] = c;
    (*n)++;
}

static void puts_cap(char *out, size_t cap, size_t *n, const char *s) {
    while (*s) putc_cap(out, cap, n, *s++);
}

size_t json_escape(const char *s, size_t len, char *out, size_t cap) {
    size_t n = 0, i = 0;
    static const char hex[] = "0123456789abcdef";
    putc_cap(out, cap, &n, '"');
    while (i < len) {
        unsigned char c = (unsigned char)s[i];
        if (c == '"') { puts_cap(out, cap, &n, "\\\""); i++; }
        else if (c == '\\') { puts_cap(out, cap, &n, "\\\\"); i++; }
        else if (c == '\n') { puts_cap(out, cap, &n, "\\n"); i++; }
        else if (c == '\r') { puts_cap(out, cap, &n, "\\r"); i++; }
        else if (c == '\t') { puts_cap(out, cap, &n, "\\t"); i++; }
        else if (c < 0x20 || c == 0x7F) {
            char u[7] = "\\u00";
            u[4] = hex[c >> 4]; u[5] = hex[c & 15]; u[6] = 0;
            puts_cap(out, cap, &n, u);
            i++;
        } else if (c < 0x80) { putc_cap(out, cap, &n, (char)c); i++; }
        else {
            /* Validate the UTF-8 sequence; copy it whole or replace it. */
            size_t need = (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : (c & 0xF8) == 0xF0 ? 4 : 0;
            size_t k;
            int ok = need > 0 && i + need <= len;
            for (k = 1; ok && k < need; k++)
                if (((unsigned char)s[i + k] & 0xC0) != 0x80) ok = 0;
            if (ok) {
                for (k = 0; k < need; k++) putc_cap(out, cap, &n, s[i + k]);
                i += need;
            } else {
                putc_cap(out, cap, &n, '?');
                i++;
            }
        }
    }
    putc_cap(out, cap, &n, '"');
    if (n < cap) out[n] = 0; else if (cap) out[cap - 1] = 0;
    return n;
}
