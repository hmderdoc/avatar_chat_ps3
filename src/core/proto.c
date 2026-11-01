#include "proto.h"

#include <stdio.h>
#include <string.h>

void proto_reader_init(proto_reader *r) {
    r->len = 0;
    r->skipping = 0;
    r->overflow = 0;
}

void proto_feed(proto_reader *r, const char *data, size_t n, proto_line_fn fn, void *ctx) {
    size_t i;
    for (i = 0; i < n; i++) {
        char c = data[i];
        if (c == '\n') {
            if (r->skipping) {
                r->skipping = 0;
                r->len = 0;
                continue;
            }
            while (r->len > 0 && (r->buf[r->len - 1] == '\r')) r->len--;
            if (r->len > 0) {
                r->buf[r->len] = 0;
                fn(ctx, r->buf, r->len);
            }
            r->len = 0;
            continue;
        }
        if (r->skipping) continue;
        if (r->len >= PROTO_MAX_PACKET) {
            r->skipping = 1;
            r->overflow++;
            r->len = 0;
            continue;
        }
        r->buf[r->len++] = c;
    }
}

int proto_parse(const char *line, size_t n, json_arena *a, proto_packet *out) {
    const json_val *root;
    a->used = 0;
    root = json_parse(line, n, a);
    if (!root || root->type != J_OBJECT) return 0;
    out->scope = json_str(json_get(root, "scope"), "");
    out->func = json_str(json_get(root, "func"), "");
    out->oper = json_str(json_get(root, "oper"), "");
    out->location = json_str(json_get(root, "location"), "");
    out->data = json_get(root, "data");
    return 1;
}

/* A tiny bounded string builder. */
typedef struct { char *out; size_t cap, n; int bad; } sb;

static void sb_raw(sb *b, const char *s) {
    size_t k = strlen(s);
    if (b->n + k >= b->cap) { b->bad = 1; return; }
    memcpy(b->out + b->n, s, k);
    b->n += k;
    b->out[b->n] = 0;
}

static void sb_str(sb *b, const char *s) {
    size_t k;
    if (!s) s = "";
    k = json_escape(s, strlen(s), b->out + b->n, b->cap - b->n);
    if (b->n + k >= b->cap) { b->bad = 1; return; }
    b->n += k;
}

static void sb_int(sb *b, long long v) {
    char tmp[32];
    snprintf(tmp, sizeof tmp, "%lld", v);
    sb_raw(b, tmp);
}

static size_t sb_end(sb *b) {
    sb_raw(b, "\r\n");
    if (b->bad) { if (b->cap) b->out[0] = 0; return 0; }
    return b->n;
}

static void sb_begin(sb *b, char *out, size_t cap) {
    b->out = out; b->cap = cap; b->n = 0; b->bad = cap == 0;
    if (cap) out[0] = 0;
}

size_t proto_nick_json(char *out, size_t cap, const char *name, const char *host, const char *avatar_b64) {
    sb b;
    sb_begin(&b, out, cap);
    sb_raw(&b, "{\"name\":"); sb_str(&b, name);
    if (host && *host) { sb_raw(&b, ",\"host\":"); sb_str(&b, host); }
    if (avatar_b64 && *avatar_b64) { sb_raw(&b, ",\"avatar\":"); sb_str(&b, avatar_b64); }
    sb_raw(&b, "}");
    return b.bad ? 0 : b.n;
}

size_t proto_subscribe(char *out, size_t cap, const char *location, const char *nick, const char *system) {
    sb b;
    sb_begin(&b, out, cap);
    sb_raw(&b, "{\"scope\":\"chat\",\"func\":\"QUERY\",\"oper\":\"SUBSCRIBE\",\"location\":");
    sb_str(&b, location);
    if (nick && *nick) { sb_raw(&b, ",\"nick\":"); sb_str(&b, nick); }
    if (system && *system) { sb_raw(&b, ",\"system\":"); sb_str(&b, system); }
    sb_raw(&b, ",\"timeout\":-1}");
    return sb_end(&b);
}

size_t proto_unsubscribe(char *out, size_t cap, const char *location) {
    sb b;
    sb_begin(&b, out, cap);
    sb_raw(&b, "{\"scope\":\"chat\",\"func\":\"QUERY\",\"oper\":\"UNSUBSCRIBE\",\"location\":");
    sb_str(&b, location);
    sb_raw(&b, ",\"timeout\":-1}");
    return sb_end(&b);
}

size_t proto_message(char *out, size_t cap, const char *oper, const char *location,
                     const char *nick_json, const char *text, long long time_ms) {
    sb b;
    sb_begin(&b, out, cap);
    sb_raw(&b, "{\"scope\":\"chat\",\"func\":\"QUERY\",\"oper\":"); sb_str(&b, oper);
    sb_raw(&b, ",\"location\":"); sb_str(&b, location);
    sb_raw(&b, ",\"data\":{\"nick\":"); sb_raw(&b, nick_json);
    sb_raw(&b, ",\"str\":"); sb_str(&b, text);
    sb_raw(&b, ",\"time\":"); sb_int(&b, time_ms);
    sb_raw(&b, "},\"lock\":2,\"timeout\":-1}");
    return sb_end(&b);
}

size_t proto_slice(char *out, size_t cap, const char *location, int start) {
    sb b;
    sb_begin(&b, out, cap);
    sb_raw(&b, "{\"scope\":\"chat\",\"func\":\"QUERY\",\"oper\":\"SLICE\",\"location\":");
    sb_str(&b, location);
    sb_raw(&b, ",\"data\":{\"start\":"); sb_int(&b, start);
    sb_raw(&b, "},\"lock\":1,\"timeout\":-1}");
    return sb_end(&b);
}

size_t proto_who(char *out, size_t cap, const char *location) {
    sb b;
    sb_begin(&b, out, cap);
    sb_raw(&b, "{\"scope\":\"chat\",\"func\":\"QUERY\",\"oper\":\"WHO\",\"location\":");
    sb_str(&b, location);
    sb_raw(&b, ",\"timeout\":-1}");
    return sb_end(&b);
}

size_t proto_keys(char *out, size_t cap, const char *location) {
    sb b;
    sb_begin(&b, out, cap);
    sb_raw(&b, "{\"scope\":\"chat\",\"func\":\"QUERY\",\"oper\":\"KEYS\",\"location\":");
    sb_str(&b, location);
    sb_raw(&b, ",\"lock\":1,\"timeout\":-1}");
    return sb_end(&b);
}

size_t proto_pong(char *out, size_t cap, long long now_ms) {
    sb b;
    sb_begin(&b, out, cap);
    sb_raw(&b, "{\"scope\":\"SOCKET\",\"func\":\"PONG\",\"data\":"); sb_int(&b, now_ms); sb_raw(&b, "}");
    return sb_end(&b);
}

size_t proto_ping(char *out, size_t cap, long long now_ms) {
    sb b;
    sb_begin(&b, out, cap);
    sb_raw(&b, "{\"scope\":\"SOCKET\",\"func\":\"PING\",\"data\":"); sb_int(&b, now_ms); sb_raw(&b, "}");
    return sb_end(&b);
}
