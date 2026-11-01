#include "chat.h"
#include "cp437.h"
#include "base64.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

#define OUT_CAP 8192
#define ARENA_CAP (768 * 1024)

static char out_buf[OUT_CAP];
static char arena_mem[ARENA_CAP];

static long long now_ms(chat *c) { return c->io.now_ms ? c->io.now_ms(c->io.ctx) : 0; }

static void emit(chat *c, size_t n) {
    if (n > 0 && c->io.send) c->io.send(c->io.ctx, out_buf, n);
}

static int ieq(const char *a, const char *b) {
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
        a++; b++;
    }
    return *a == *b;
}

static void copy_cp437(char *dst, size_t cap, const char *utf8) {
    size_t n = cp437_from_utf8(utf8 ? utf8 : "", utf8 ? strlen(utf8) : 0, (unsigned char *)dst, cap - 1);
    dst[n] = 0;
}

static void loc_messages(char *out, size_t cap, const char *name) { snprintf(out, cap, "channels.%s.messages", name); }
static void loc_history(char *out, size_t cap, const char *name) { snprintf(out, cap, "channels.%s.history", name); }

void chat_init(chat *c, const chat_io *io) {
    memset(c, 0, sizeof *c);
    c->io = *io;
    c->history_count = 40;
    c->want_art = -1;
    c->show_pics = 1;
    pics_init(&c->pics);
    strcpy(c->channel, "main");
    strcpy(c->joined[0], "main");
    c->njoined = 1;
}

/* Picture links in a message: remember them and queue the fetches. */
static void take_pics(chat *c, chat_msg *m, const char *utf8) {
    int i;
    if (!c->show_pics) return;
    m->npics = (unsigned char)pics_find_links((const unsigned char *)utf8, (int)strlen(utf8), m->pics, 3);
    for (i = 0; i < m->npics; i++) pics_get(&c->pics, m->pics[i]);
}

static int room_index(const chat *c, const char *name) {
    int i;
    for (i = 0; i < c->njoined; i++)
        if (ieq(c->joined[i], name)) return i;
    return -1;
}

int chat_current_room(const chat *c) { return room_index(c, c->channel); }

chat_art *chat_art_at(chat *c, int i) {
    if (i < 0 || i >= c->nart) return NULL;
    return &c->arts[(c->art_head + i) % CHAT_ARTS];
}

int chat_unviewed_art(const chat *c) {
    int i, n = 0;
    for (i = 0; i < c->nart; i++)
        if (!c->arts[(c->art_head + i) % CHAT_ARTS].viewed) n++;
    return n;
}

static chat_art *art_push(chat *c) {
    chat_art *a;
    if (c->nart < CHAT_ARTS) a = &c->arts[(c->art_head + c->nart++) % CHAT_ARTS];
    else { a = &c->arts[c->art_head]; c->art_head = (c->art_head + 1) % CHAT_ARTS; }
    memset(a, 0, sizeof *a);
    c->seq++;
    return a;
}

static void build_nick_json(chat *c) {
    proto_nick_json(c->nick_json, sizeof c->nick_json, c->nick, c->host, c->has_avatar ? c->avatar_b64 : NULL);
}

void chat_set_identity(chat *c, const char *nick, const char *host, const unsigned char *avatar120) {
    snprintf(c->nick, sizeof c->nick, "%s", nick ? nick : "");
    snprintf(c->host, sizeof c->host, "%s", host ? host : "");
    c->has_avatar = 0;
    if (avatar120 && avatar_valid(avatar120)) {
        memcpy(c->avatar, avatar120, AVATAR_BYTES);
        base64_encode(c->avatar, AVATAR_BYTES, c->avatar_b64, sizeof c->avatar_b64);
        c->has_avatar = 1;
    }
    build_nick_json(c);
}

void chat_set_channel(chat *c, const char *channel) {
    snprintf(c->channel, sizeof c->channel, "%s", channel && *channel ? channel : "main");
    if (room_index(c, c->channel) < 0) {
        if (c->njoined >= CHAT_ROOMS) c->njoined = CHAT_ROOMS - 1;
        snprintf(c->joined[c->njoined], CHAT_CHANNEL, "%s", c->channel);
        c->unread[c->njoined] = c->mentioned[c->njoined] = 0;
        c->njoined++;
    }
}

/* ---- ring ------------------------------------------------------------ */

static chat_msg *ring_push(chat *c) {
    chat_msg *m;
    int idx;
    if (c->count < CHAT_RING) {
        idx = (c->head + c->count) % CHAT_RING;
        c->count++;
    } else {
        idx = c->head;
        c->head = (c->head + 1) % CHAT_RING;
    }
    m = &c->ring[idx];
    memset(m, 0, sizeof *m);
    c->seq++;
    return m;
}

const chat_msg *chat_msg_at(const chat *c, int i) {
    if (i < 0 || i >= c->count) return NULL;
    return &c->ring[(c->head + i) % CHAT_RING];
}

void chat_notice(chat *c, const char *fmt, ...) {
    char tmp[CHAT_TEXT];
    va_list ap;
    chat_msg *m;
    va_start(ap, fmt);
    vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);
    m = ring_push(c);
    m->kind = MSG_NOTICE;
    m->time = now_ms(c);
    m->textlen = (int)cp437_from_utf8(tmp, strlen(tmp), m->text, sizeof m->text);
}

/* ---- identity helpers ------------------------------------------------ */

static unsigned hash_text(const char *s) {
    unsigned h = 2166136261u;
    while (*s) { h ^= (unsigned char)*s++; h *= 16777619u; }
    return h;
}

static void remember_sent(chat *c, long long t, const char *text) {
    c->sent[c->sent_i].time = t;
    c->sent[c->sent_i].hash = hash_text(text);
    c->sent_i = (c->sent_i + 1) % 8;
}

/* Our own message echoed back by a server that fans out to the sender. */
static int recent_self(chat *c, long long t, const char *text) {
    unsigned h = hash_text(text);
    int i;
    for (i = 0; i < 8; i++)
        if (c->sent[i].hash == h && c->sent[i].time == t) return 1;
    return 0;
}

static int same_user(const chat *c, const char *name, const char *host, const char *mailbox) {
    if (!ieq(c->nick, name) || (mailbox && *mailbox)) return 0;
    return !host || !*host || !*c->host || ieq(host, c->host);
}

/* Web mailboxes (WEB.md): honoured only when exactly name@origin(host). */
static int valid_mailbox(const char *name, const char *host, const char *mailbox) {
    char want[96];
    size_t i, n;
    if (!mailbox || !*mailbox || !name || !*name || strchr(mailbox, '.')) return 0;
    n = snprintf(want, sizeof want, "%s@", name);
    if (n >= sizeof want) return 0;
    for (i = 0; host[i] && n + 1 < sizeof want; i++) {
        char ch = host[i];
        want[n++] = (isalnum((unsigned char)ch) || ch == '_' || ch == '-') ? ch : '_';
    }
    want[n] = 0;
    return ieq(want, mailbox);
}

static void remember_peer(chat *c, const char *name, const char *host, const char *mailbox) {
    int i;
    chat_peer *p = NULL;
    if (!name || !*name) return;
    for (i = 0; i < c->npeers; i++)
        if (ieq(c->peers[i].name, name)) { p = &c->peers[i]; break; }
    if (!p) {
        if (c->npeers < CHAT_PEERS) p = &c->peers[c->npeers++];
        else p = &c->peers[(c->seq) % CHAT_PEERS]; /* evict someone */
    }
    snprintf(p->name, sizeof p->name, "%s", name);
    snprintf(p->host, sizeof p->host, "%s", host ? host : "");
    if (valid_mailbox(name, host ? host : "", mailbox)) snprintf(p->mailbox, sizeof p->mailbox, "%s", mailbox);
    else p->mailbox[0] = 0;
}

static const char *pm_target(chat *c, const char *name) {
    int i;
    for (i = 0; i < c->npeers; i++)
        if (ieq(c->peers[i].name, name)) return c->peers[i].mailbox[0] ? c->peers[i].mailbox : c->peers[i].name;
    return name;
}

/* ---- outbound -------------------------------------------------------- */

static void send_subscribe(chat *c, const char *name) {
    char loc[128];
    loc_messages(loc, sizeof loc, name);
    emit(c, proto_subscribe(out_buf, OUT_CAP, loc, c->nick, c->host));
}

static void send_unsubscribe(chat *c, const char *name) {
    char loc[128];
    loc_messages(loc, sizeof loc, name);
    emit(c, proto_unsubscribe(out_buf, OUT_CAP, loc));
}

static void send_slice(chat *c, const char *name, int n) {
    char loc[128];
    loc_history(loc, sizeof loc, name);
    emit(c, proto_slice(out_buf, OUT_CAP, loc, -n));
}

static void send_who(chat *c, const char *name) {
    char loc[128];
    loc_messages(loc, sizeof loc, name);
    emit(c, proto_who(out_buf, OUT_CAP, loc));
}

void chat_connected(chat *c) {
    c->connected = 1;
    c->last_rx_ms = now_ms(c);
    c->last_ping_ms = c->last_rx_ms;
    chat_notice(c, "Connected. Joining #%s ...", c->channel);
    {
        int i;
        for (i = 0; i < c->njoined; i++) send_subscribe(c, c->joined[i]);
    }
    if (c->nick[0]) send_subscribe(c, c->nick);
    if (c->history_count > 0) send_slice(c, c->channel, c->history_count);
    send_who(c, c->channel);
}

void chat_disconnected(chat *c, const char *why) {
    if (c->connected || why) chat_notice(c, "Disconnected%s%s", why && *why ? ": " : ".", why ? why : "");
    c->connected = 0;
    c->users = 0;
    c->roster[0] = 0;
}

static int clean_room_name(chat *c, const char *channel, char *name, size_t cap) {
    size_t i, n = 0;
    for (i = 0; channel[i] && n + 1 < cap; i++) {
        char ch = channel[i];
        if (ch == '#' && n == 0) continue;
        if (isalnum((unsigned char)ch) || ch == '_' || ch == '-') name[n++] = ch;
        else { chat_notice(c, "Room names are letters, digits, - and _."); return -1; }
    }
    name[n] = 0;
    return n > 0;
}

/* Make name the viewed room: clear the transcript and load it. */
static void view_room(chat *c, const char *name) {
    int idx;
    snprintf(c->channel, sizeof c->channel, "%s", name);
    idx = room_index(c, name);
    if (idx >= 0) { c->unread[idx] = 0; c->mentioned[idx] = 0; }
    c->count = 0;
    c->head = 0;
    c->users = 0;
    c->roster[0] = 0;
    c->seq++;
    if (c->connected) {
        chat_notice(c, "Joining #%s ...", c->channel);
        if (c->history_count > 0) send_slice(c, c->channel, c->history_count);
        send_who(c, c->channel);
    } else {
        chat_notice(c, "Will join #%s when connected.", c->channel);
    }
}

void chat_join(chat *c, const char *channel) {
    char name[CHAT_CHANNEL];
    int idx;
    {
        int rc = clean_room_name(c, channel, name, sizeof name);
        if (rc < 0) return;
        if (rc == 0) { chat_notice(c, "Usage: /join <room>"); return; }
    }
    if (ieq(name, c->channel)) { chat_notice(c, "You are already in #%s.", c->channel); return; }
    idx = room_index(c, name);
    if (idx < 0) {
        if (c->njoined >= CHAT_ROOMS) {
            chat_notice(c, "You are in %d rooms already; /leave one first.", CHAT_ROOMS);
            return;
        }
        snprintf(c->joined[c->njoined], CHAT_CHANNEL, "%s", name);
        c->unread[c->njoined] = c->mentioned[c->njoined] = 0;
        c->njoined++;
        if (c->connected) send_subscribe(c, name);
    } else {
        snprintf(name, sizeof name, "%s", c->joined[idx]); /* the joined spelling */
    }
    view_room(c, name);
}

void chat_leave(chat *c, const char *channel) {
    char name[CHAT_CHANNEL];
    int idx, i;
    if (!channel || !*channel) channel = c->channel;
    if (clean_room_name(c, channel, name, sizeof name) <= 0) return;
    idx = room_index(c, name);
    if (idx < 0) { chat_notice(c, "You are not in #%s.", name); return; }
    if (c->njoined == 1) { chat_notice(c, "That is your only room; /join another first."); return; }
    if (c->connected) send_unsubscribe(c, c->joined[idx]);
    for (i = idx; i + 1 < c->njoined; i++) {
        strcpy(c->joined[i], c->joined[i + 1]);
        c->unread[i] = c->unread[i + 1];
        c->mentioned[i] = c->mentioned[i + 1];
    }
    c->njoined--;
    c->seq++;
    if (ieq(name, c->channel)) view_room(c, c->joined[idx < c->njoined ? idx : c->njoined - 1]);
    else chat_notice(c, "Left #%s.", name);
}

void chat_switch(chat *c, int dir) {
    int idx = room_index(c, c->channel);
    if (c->njoined < 2) return;
    if (idx < 0) idx = 0;
    idx = (idx + (dir < 0 ? -1 : 1) + c->njoined) % c->njoined;
    view_room(c, c->joined[idx]);
}

static void send_text_to(chat *c, const char *dest, const char *utf8, int is_pm, const char *shown_as) {
    char loc[128];
    long long t = now_ms(c);
    chat_msg *m;
    loc_messages(loc, sizeof loc, dest);
    emit(c, proto_message(out_buf, OUT_CAP, "WRITE", loc, c->nick_json, utf8, t));
    loc_history(loc, sizeof loc, dest);
    emit(c, proto_message(out_buf, OUT_CAP, "PUSH", loc, c->nick_json, utf8, t));
    remember_sent(c, t, utf8);
    m = ring_push(c);
    m->kind = is_pm ? MSG_PRIVATE : MSG_CHAT;
    m->self = 1;
    m->time = t;
    copy_cp437(m->name, sizeof m->name, c->nick);
    copy_cp437(m->host, sizeof m->host, c->host);
    if (c->has_avatar) { memcpy(m->avatar, c->avatar, AVATAR_BYTES); m->has_avatar = 1; }
    if (is_pm) {
        char tmp[CHAT_TEXT];
        snprintf(tmp, sizeof tmp, "-> %s: %s", shown_as, utf8);
        m->textlen = (int)cp437_from_utf8(tmp, strlen(tmp), m->text, sizeof m->text);
    } else {
        m->textlen = (int)cp437_from_utf8(utf8, strlen(utf8), m->text, sizeof m->text);
        take_pics(c, m, utf8);
    }
}

static void help(chat *c) {
    chat_notice(c, "Commands: /join <room>  /leave [room]  /next  /prev  /rooms  /who  /msg <name> <text>  /r <text>  /img [N]  /compact  /quit");
}

void chat_input(chat *c, const unsigned char *line, size_t n) {
    char utf8[CHAT_TEXT * 3 + 1];
    char *p;
    while (n > 0 && line[n - 1] == ' ') n--;
    while (n > 0 && line[0] == ' ') { line++; n--; }
    if (n == 0) return;
    if (n > 1000) n = 1000;
    cp437_to_utf8(line, n, utf8, sizeof utf8);
    p = utf8;
    if (*p != '/') {
        if (!c->connected) { chat_notice(c, "Not connected."); return; }
        send_text_to(c, c->channel, utf8, 0, NULL);
        return;
    }
    p++;
    if (ieq(p, "quit") || ieq(p, "exit") || ieq(p, "q")) { c->want_quit = 1; return; }
    if (ieq(p, "help") || ieq(p, "?")) { help(c); return; }
    if (ieq(p, "compact")) { c->toggle_compact = 1; return; }
    if (ieq(p, "img") || strncmp(p, "img ", 4) == 0) {
        int n = c->nart;
        if (n == 0) { chat_notice(c, "No art has been sent here yet."); return; }
        if (p[3] == ' ') {
            int k = atoi(p + 4);
            if (k < 1 || k > n) { chat_notice(c, "/img N picks one of the last %d pieces, 1 = newest.", n); return; }
            c->want_art = n - k;
        } else {
            int i;
            c->want_art = n - 1;
            for (i = 0; i < n; i++) if (!chat_art_at(c, i)->viewed) { c->want_art = i; break; }
        }
        return;
    }
    if (ieq(p, "who")) {
        if (!c->connected) { chat_notice(c, "Not connected."); return; }
        send_who(c, c->channel);
        return;
    }
    if (ieq(p, "rooms")) {
        char list[CHAT_TEXT];
        size_t n = 0;
        int i;
        list[0] = 0;
        for (i = 0; i < c->njoined; i++) {
            int k = snprintf(list + n, sizeof list - n, "%s#%s%s", n ? ", " : "", c->joined[i],
                             ieq(c->joined[i], c->channel) ? " (here)" : "");
            if (k < 0 || n + (size_t)k >= sizeof list) break;
            n += (size_t)k;
            if (c->unread[i] > 0) {
                k = snprintf(list + n, sizeof list - n, " [%d new]", c->unread[i]);
                if (k < 0 || n + (size_t)k >= sizeof list) break;
                n += (size_t)k;
            }
        }
        chat_notice(c, "Joined: %s", list);
        if (!c->connected) { chat_notice(c, "Not connected."); return; }
        emit(c, proto_keys(out_buf, OUT_CAP, "public_channels"));
        return;
    }
    if (ieq(p, "leave") || ieq(p, "part")) { chat_leave(c, NULL); return; }
    if (strncmp(p, "leave ", 6) == 0 || strncmp(p, "part ", 5) == 0) {
        p = strchr(p, ' ') + 1;
        while (*p == ' ') p++;
        chat_leave(c, p);
        return;
    }
    if (ieq(p, "next")) { chat_switch(c, 1); return; }
    if (ieq(p, "prev")) { chat_switch(c, -1); return; }
    if (strncmp(p, "join ", 5) == 0 || strncmp(p, "j ", 2) == 0) {
        p = strchr(p, ' ') + 1;
        while (*p == ' ') p++;
        chat_join(c, p);
        return;
    }
    if (strncmp(p, "msg ", 4) == 0 || strncmp(p, "m ", 2) == 0 || strncmp(p, "private ", 8) == 0) {
        char name[CHAT_NAME];
        size_t k = 0;
        p = strchr(p, ' ') + 1;
        while (*p == ' ') p++;
        while (*p && *p != ' ' && k + 1 < sizeof name) name[k++] = *p++;
        name[k] = 0;
        while (*p == ' ') p++;
        if (!k || !*p) { chat_notice(c, "Usage: /msg <name> <text>"); return; }
        if (!c->connected) { chat_notice(c, "Not connected."); return; }
        send_text_to(c, pm_target(c, name), p, 1, name);
        return;
    }
    if (strncmp(p, "r ", 2) == 0) {
        p += 2;
        while (*p == ' ') p++;
        if (!c->last_pm_from[0]) { chat_notice(c, "Nobody has messaged you yet."); return; }
        if (!*p) { chat_notice(c, "Usage: /r <text>"); return; }
        if (!c->connected) { chat_notice(c, "Not connected."); return; }
        send_text_to(c, pm_target(c, c->last_pm_from), p, 1, c->last_pm_from);
        return;
    }
    chat_notice(c, "Unknown command. /help lists them.");
}

void chat_tick(chat *c) {
    long long t = now_ms(c);
    if (!c->connected) return;
    if (t - c->last_ping_ms >= 60000) {
        c->last_ping_ms = t;
        emit(c, proto_ping(out_buf, OUT_CAP, t));
    }
}

/* ---- inbound --------------------------------------------------------- */

static int is_marker(const char *s, const char *prefix) {
    return strncmp(s, prefix, strlen(prefix)) == 0;
}

static int word_byte(int c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'); }

/* The web chat's rule: our name as a whole word, optional leading @,
 * ignoring case. */
static int mentions_me(const chat *c, const char *text) {
    size_t n = strlen(c->nick), i, tl;
    char low[CHAT_NAME];
    if (n == 0) return 0;
    for (i = 0; i < n; i++) low[i] = (char)tolower((unsigned char)c->nick[i]);
    low[n] = 0;
    tl = strlen(text);
    for (i = 0; i + n <= tl; i++) {
        size_t k;
        for (k = 0; k < n; k++) if (tolower((unsigned char)text[i + k]) != low[k]) break;
        if (k < n) continue;
        if ((i == 0 || !word_byte(tolower((unsigned char)text[i - 1]))) && (i + n == tl || !word_byte(tolower((unsigned char)text[i + n])))) return 1;
    }
    return 0;
}

/* "#rrggbb" or "#rgb" -> 0x00RRGGBB; 0 when absent or malformed (which is
 * also what "" means: the default colour). */
static unsigned int parse_hex_color(const char *s) {
    unsigned int v = 0;
    int i, n;
    if (!s || s[0] != '#') return 0;
    n = (int)strlen(s + 1);
    if (n != 6 && n != 3) return 0;
    for (i = 1; s[i]; i++) {
        int c = s[i], d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else return 0;
        v = (v << 4) | (unsigned)d;
        if (n == 3) v = (v << 4) | (unsigned)d;
    }
    return v == 0 ? 0x010101 : v; /* pure black still counts as a colour */
}

static void take_style(chat_msg *m, const json_val *style) {
    const json_val *colors = json_get(style, "colors"), *tag = json_get(style, "tag"), *e;
    int i = 0;
    m->ncolors = 0;
    m->tag[0] = 0;
    if (!style || style->type != J_OBJECT) return;
    if (colors && colors->type == J_ARRAY) {
        for (e = colors->first; e && i < CHAT_NAME - 1; e = e->next, i++)
            m->colors[i] = e->type == J_STRING ? parse_hex_color(e->str) : 0;
        m->ncolors = (unsigned char)i;
    }
    if (tag && tag->type == J_OBJECT) {
        const char *text = json_str(json_get(tag, "text"), "");
        size_t n = cp437_from_utf8(text, strlen(text), (unsigned char *)m->tag, sizeof m->tag - 1);
        m->tag[n] = 0;
        m->tag_fg = parse_hex_color(json_str(json_get(tag, "fg"), ""));
        m->tag_bg = parse_hex_color(json_str(json_get(tag, "bg"), ""));
    }
}

/* Appends one wire message to the ring; live=1 for an UPDATE, 0 for history. */
static void take_message(chat *c, const json_val *msg, int private_box, int live) {
    const json_val *nick = json_get(msg, "nick");
    const char *name = json_str(json_get(nick, "name"), "");
    const char *host = json_str(json_get(nick, "host"), "");
    const char *mailbox = json_str(json_get(nick, "mailbox"), "");
    const char *avatar = json_str(json_get(nick, "avatar"), "");
    const char *str = json_str(json_get(msg, "str"), "");
    long long t = json_int(json_get(msg, "time"), 0);
    chat_msg *m;
    int self;

    remember_peer(c, name, host, mailbox);
    self = same_user(c, name, host, mailbox);
    if (self && live && recent_self(c, t, str)) return;

    if (is_marker(str, "[TVTUNER|")) return;
    if (art_is_envelope(str)) {
        static art_image tmp;
        char from[CHAT_NAME];
        if (art_parse(str, strlen(str), &tmp, from, sizeof from)) {
            chat_art *a = art_push(c);
            a->img = tmp;
            a->time = t;
            a->viewed = !live;
            copy_cp437(a->from, sizeof a->from, *name ? name : from);
            snprintf(a->room, sizeof a->room, "%s", private_box ? c->nick : c->channel);
            chat_notice(c, "%s sent art (%dx%d). %s to view", a->from, tmp.w, tmp.h,
                        live ? "/img or Square" : "/img");
        } else if (live) {
            chat_notice(c, "%s sent an image this client can't decode.", name);
        }
        return;
    }
    m = ring_push(c);
    m->kind = private_box ? MSG_PRIVATE : MSG_CHAT;
    m->self = self;
    m->time = t;
    copy_cp437(m->name, sizeof m->name, name);
    copy_cp437(m->host, sizeof m->host, host);
    m->textlen = (int)cp437_from_utf8(str, strlen(str), m->text, sizeof m->text);
    if (*avatar && avatar_from_base64(avatar, strlen(avatar), m->avatar)) m->has_avatar = 1;
    take_style(m, json_get(nick, "style"));
    take_pics(c, m, str);
    if (private_box && !self) snprintf(c->last_pm_from, sizeof c->last_pm_from, "%s", name);
}

/* "channels.<name>.<leaf>" -> name and leaf; 0 when not that shape. */
static int split_location(const char *loc, char *name, size_t ncap, const char **leaf) {
    const char *p, *q;
    size_t k;
    if (strncmp(loc, "channels.", 9) != 0) return 0;
    p = loc + 9;
    q = strchr(p, '.');
    if (!q) return 0;
    k = (size_t)(q - p);
    if (k == 0 || k >= ncap) return 0;
    memcpy(name, p, k);
    name[k] = 0;
    *leaf = q + 1;
    return 1;
}

static void handle_update(chat *c, const proto_packet *pk) {
    char name[CHAT_CHANNEL + CHAT_NAME];
    const char *leaf;
    int private_box;
    if (!split_location(pk->location, name, sizeof name, &leaf)) return;
    if (strcmp(leaf, "messages") != 0) return;
    private_box = ieq(name, c->nick);
    if (!private_box && !ieq(name, c->channel)) {
        /* A joined room we are not looking at: count it. */
        int idx = room_index(c, name);
        if (idx >= 0 && ieq(pk->oper, "WRITE") && pk->data && pk->data->type == J_OBJECT) {
            const char *str = json_str(json_get(pk->data, "str"), "");
            const json_val *nick = json_get(pk->data, "nick");
            if (!same_user(c, json_str(json_get(nick, "name"), ""), json_str(json_get(nick, "host"), ""),
                           json_str(json_get(nick, "mailbox"), "")) && !is_marker(str, "[TVTUNER|")) {
                c->unread[idx]++;
                if (mentions_me(c, str)) c->mentioned[idx] = 1;
                c->seq++;
            }
        }
        return;
    }

    if (ieq(pk->oper, "WRITE")) {
        if (pk->data && pk->data->type == J_OBJECT) take_message(c, pk->data, private_box, 1);
    } else if (ieq(pk->oper, "SUBSCRIBE") || ieq(pk->oper, "UNSUBSCRIBE")) {
        const char *who = json_str(json_get(pk->data, "nick"), "");
        const char *sys = json_str(json_get(pk->data, "system"), "");
        int joined = ieq(pk->oper, "SUBSCRIBE");
        if (private_box || !*who) return;
        if (ieq(who, c->nick) && (!*sys || !*c->host || ieq(sys, c->host))) return;
        chat_notice(c, joined ? "%s is here." : "%s has left.", who);
        if (joined) c->users++; else if (c->users > 0) c->users--;
    }
}

static void handle_response(chat *c, const proto_packet *pk) {
    char name[CHAT_CHANNEL + CHAT_NAME];
    const char *leaf = "";
    if (ieq(pk->oper, "SLICE")) {
        const json_val *m;
        int shown = 0;
        if (!split_location(pk->location, name, sizeof name, &leaf) || strcmp(leaf, "history") != 0) return;
        if (!ieq(name, c->channel) || !pk->data || pk->data->type != J_ARRAY) return;
        for (m = pk->data->first; m; m = m->next) {
            if (m->type != J_OBJECT) continue;
            take_message(c, m, 0, 0);
            shown++;
        }
        if (shown == 0) chat_notice(c, "No history in #%s yet.", c->channel);
    } else if (ieq(pk->oper, "WHO")) {
        const json_val *e;
        size_t n = 0;
        int count = 0;
        if (!pk->data || pk->data->type != J_ARRAY) return;
        c->roster[0] = 0;
        for (e = pk->data->first; e; e = e->next) {
            const char *who = e->type == J_STRING ? e->str : json_str(json_get(e, "nick"), "");
            const char *sys = e->type == J_STRING ? "" : json_str(json_get(e, "system"), "");
            int k;
            if (!*who) continue;
            count++;
            k = snprintf(c->roster + n, sizeof c->roster - n, "%s%s%s%s%s", n ? ", " : "", who,
                         *sys ? " (" : "", sys, *sys ? ")" : "");
            if (k < 0 || n + (size_t)k >= sizeof c->roster) { c->roster[n] = 0; break; }
            n += (size_t)k;
        }
        c->users = count;
        chat_notice(c, "Users in #%s (%d): %s", c->channel, count, c->roster[0] ? c->roster : "nobody");
    } else if (ieq(pk->oper, "KEYS")) {
        char list[CHAT_TEXT];
        size_t n = 0;
        const json_val *e;
        if (!pk->data) { chat_notice(c, "No public rooms are registered."); return; }
        list[0] = 0;
        for (e = pk->data->first; e; e = e->next) {
            const char *room = e->type == J_STRING ? e->str : e->key;
            int k;
            if (!room || !*room) continue;
            k = snprintf(list + n, sizeof list - n, "%s#%s", n ? ", " : "", room);
            if (k < 0 || n + (size_t)k >= sizeof list) break;
            n += (size_t)k;
        }
        chat_notice(c, "Rooms: %s", list[0] ? list : "none registered");
    }
}

void chat_feed_line(chat *c, const char *line, size_t n) {
    json_arena a;
    proto_packet pk;
    json_arena_init(&a, arena_mem, ARENA_CAP);
    c->last_rx_ms = now_ms(c);
    if (!proto_parse(line, n, &a, &pk)) return;
    if (ieq(pk.func, "PING")) {
        emit(c, proto_pong(out_buf, OUT_CAP, now_ms(c)));
    } else if (ieq(pk.func, "UPDATE")) {
        handle_update(c, &pk);
    } else if (ieq(pk.func, "RESPONSE")) {
        handle_response(c, &pk);
    } else if (ieq(pk.func, "ERROR")) {
        chat_notice(c, "Server: %s", json_str(json_get(pk.data, "description"), "error"));
    }
}
