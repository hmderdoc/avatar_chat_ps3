/* Chat session state, independent of sockets and screens. The platform
 * feeds it inbound lines and user input; it emits outbound packets through
 * a callback and keeps a ring of messages for the UI to draw. */
#ifndef AC_CHAT_H
#define AC_CHAT_H

#include <stddef.h>
#include "proto.h"
#include "avatar.h"
#include "art.h"

#define CHAT_RING     200
#define CHAT_TEXT     1024
#define CHAT_NAME     26   /* 25 chars + NUL, the shared nick limit */
#define CHAT_HOST     41
#define CHAT_CHANNEL  33
#define CHAT_PEERS    64
#define CHAT_ROSTER   512
#define CHAT_ARTS     8    /* art pieces kept for the viewer */
#define CHAT_ROOMS    8    /* rooms joined at once */

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
    /* Handle style (nick.style): a colour per letter of the name, 0 = the
     * default colour; and a short tag drawn after it. */
    unsigned int colors[CHAT_NAME];
    unsigned char ncolors;
    char tag[13];
    unsigned int tag_fg, tag_bg;
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

typedef struct {
    art_image img;
    char from[CHAT_NAME];   /* CP437 */
    char room[CHAT_CHANNEL];
    long long time;
    int viewed;
} chat_art;

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

    /* Rooms we stay subscribed to besides the viewed one (channel). Their
     * traffic is counted, not shown, until they are viewed. */
    char joined[CHAT_ROOMS][CHAT_CHANNEL];
    int unread[CHAT_ROOMS];
    int mentioned[CHAT_ROOMS];  /* an unread line named us */
    int njoined;

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

    /* Art received ([BITMAP|...]), newest last; a ring of CHAT_ARTS. */
    chat_art arts[CHAT_ARTS];
    int art_head, nart;
    int want_art;              /* UI hint: open the viewer on art index n (oldest = 0); -1 none */
} chat;

/* Art i, oldest first (0 .. nart-1), or NULL. */
chat_art *chat_art_at(chat *c, int i);
/* How many pieces have not been opened in the viewer. */
int chat_unviewed_art(const chat *c);

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

/* View a room: joins it if needed (subscribe) and loads its history. The
 * room viewed before stays joined; chat_leave drops one. */
void chat_join(chat *c, const char *channel);
void chat_leave(chat *c, const char *channel);
/* View the next (+1) or previous (-1) joined room. */
void chat_switch(chat *c, int dir);
/* Index of the viewed room in joined[], or -1. */
int chat_current_room(const chat *c);

#endif
