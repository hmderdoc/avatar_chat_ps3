/* The chat wire protocol: line-delimited JSON over TCP, "\r\n" terminated,
 * the same packets Synchronet's json-client.js, the universal door and
 * avatar_chat_server exchange. See PROTOCOL.md in avatar_chat_universal. */
#ifndef AC_PROTO_H
#define AC_PROTO_H

#include <stddef.h>
#include "json.h"

#define PROTO_MAX_PACKET 131072   /* json-sock.js max_recv; the server drops bigger lines */

/* Inbound framing: feed bytes, get whole lines back (without the line end).
 * A line over PROTO_MAX_PACKET is discarded and 'overflow' counted. */
typedef struct {
    char buf[PROTO_MAX_PACKET + 1];
    size_t len;
    int skipping;
    unsigned overflow;
} proto_reader;

typedef void (*proto_line_fn)(void *ctx, const char *line, size_t n);

void proto_reader_init(proto_reader *r);
void proto_feed(proto_reader *r, const char *data, size_t n, proto_line_fn fn, void *ctx);

/* A parsed inbound packet. Strings point into the arena (valid until the
 * next parse); data is NULL when absent. */
typedef struct {
    const char *scope;
    const char *func;      /* RESPONSE, UPDATE, PING, PONG, ERROR */
    const char *oper;      /* WRITE, SUBSCRIBE, SLICE, WHO, ... (echoes the request) */
    const char *location;
    const json_val *data;
} proto_packet;

int proto_parse(const char *line, size_t n, json_arena *a, proto_packet *out);

/* Outbound builders. Each writes one complete line including "\r\n" and
 * returns its length, or 0 when it does not fit in cap. Text arguments are
 * UTF-8. nick_json is a complete JSON object for the sender, see
 * proto_nick_json. */
size_t proto_nick_json(char *out, size_t cap, const char *name, const char *host, const char *avatar_b64);
size_t proto_subscribe(char *out, size_t cap, const char *location, const char *nick, const char *system);
size_t proto_unsubscribe(char *out, size_t cap, const char *location);
size_t proto_message(char *out, size_t cap, const char *oper, const char *location,
                     const char *nick_json, const char *text, long long time_ms);
size_t proto_slice(char *out, size_t cap, const char *location, int start);
size_t proto_who(char *out, size_t cap, const char *location);
size_t proto_keys(char *out, size_t cap, const char *location);
size_t proto_pong(char *out, size_t cap, long long now_ms);
size_t proto_ping(char *out, size_t cap, long long now_ms);

#endif
