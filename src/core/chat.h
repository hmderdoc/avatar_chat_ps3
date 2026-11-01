/* Chat session state, independent of sockets and screens. The platform
 * feeds it inbound lines and user input; it emits outbound packets through
 * a callback and keeps a ring of messages for the UI to draw. */
#ifndef AC_CHAT_H
#define AC_CHAT_H

#include <stddef.h>
#include "proto.h"
#include "avatar.h"

#define CHAT_RING     200
#define CHAT_TEXT     1024
#define CHAT_NAME     26   /* 25 chars + NUL, the shared nick limit */
#define CHAT_HOST     41
#define CHAT_CHANNEL  33
#define CHAT_PEERS    64
#define CHAT_ROSTER   512

typedef enum { MSG_CHAT, MSG_NOTICE, MSG_PRIVATE } msg_kind;

typedef struct {
    msg_kind kind;
    char name[CHAT_NAME];          /* CP437 */
    char host[CHAT_HOST];          /* CP437; the sender's board */
    unsigned char text[CHAT_TEXT]; /* CP437, not NUL-terminated */
    int textlen;
    unsigned char avatar[AVATAR_BYTES];
    int has_avatar;
    long long time;                /* unix ms */
    int self;                      /* sent by us */
} chat_msg;

typedef struct {
    void *ctx;
    void (*send)(void *ctx, const char *bytes, size_t n);  /* queue bytes to the socket */
    long long (*now_ms)(void *ctx);
} chat_io;

typedef struct {
    char name[CHAT_NAME];
    char host[CHAT_HOST];
    char mailbox[96];   /* a standalone web user's name@origin, else "" */
} chat_peer;

typedef struct chat {
    chat_io io;

    /* Identity: what rides on every message we send. */
    char nick[CHAT_NAME];
    char host[CHAT_HOST];
    char channel[CHAT_CHANNEL];
    unsigned char avatar[AVATAR_BYTES];
    int has_avatar;
    char avatar_b64[AVATAR_BYTES * 2];
    char nick_json[512];
    int history_count;

    int connected;
    unsigned seq;              /* bumps whenever something visible changed */
    int users;                 /* last WHO count for the channel */
    char roster[CHAT_ROSTER];  /* "alice, bob (BBS)" */

    chat_msg ring[CHAT_RING];
    int head, count;

    struct { long long time; unsigned hash; } sent[8];
    int sent_i;

    chat_peer peers[CHAT_PEERS];
    int npeers;
    char last_pm_from[CHAT_NAME];

    long long last_rx_ms, last_ping_ms;
    int want_quit;
    int toggle_compact;        /* UI hint set by /compact, cleared by the UI */
} chat;

void chat_init(chat *c, const chat_io *io);

/* name/host are UTF-8 (ASCII in practice); avatar may be NULL. */
void chat_set_identity(chat *c, const char *nick, const char *host, const unsigned char *avatar120);
void chat_set_channel(chat *c, const char *channel);

/* Socket lifecycle. chat_connected sends the join sequence (subscribe to
 * the room and our mailbox, read history, ask who's here). */
void chat_connected(chat *c);
void chat_disconnected(chat *c, const char *why);

/* One inbound line (a JSON packet without its line ending). */
void chat_feed_line(chat *c, const char *line, size_t n);

/* The user pressed Enter on a CP437 line: a message or a /command. */
void chat_input(chat *c, const unsigned char *line, size_t n);

/* Periodic housekeeping: our own keepalive PINGs. */
void chat_tick(chat *c);

/* Local notice (UTF-8 printf). */
void chat_notice(chat *c, const char *fmt, ...);

/* Messages oldest (0) to newest (count-1). */
const chat_msg *chat_msg_at(const chat *c, int i);

/* Switch rooms: leave the current one, join another, reload history. */
void chat_join(chat *c, const char *channel);

#endif
