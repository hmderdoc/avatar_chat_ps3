/* Unit tests for the portable core. Build and run: make -f Makefile.host test */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "core/json.h"
#include "core/proto.h"
#include "core/chat.h"
#include "core/cp437.h"
#include "core/base64.h"
#include "core/avatar.h"
#include "core/ui.h"
#include "config.h"
#include "core/inflate.h"
#include "core/art.h"
#include "vectors.h"
#include "vectors_png.h"
#include "vectors_gif.h"
#include "core/gif.h"
#include "core/png.h"
#include "core/pics.h"

static int fails, checks;
#define CHECK(cond) do { checks++; if (!(cond)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static char arena_mem[256 * 1024];

static void test_json(void) {
    json_arena a;
    json_val *v;
    json_arena_init(&a, arena_mem, sizeof arena_mem);
    {
        const char *doc = "{\"a\":1,\"b\":[true,null,\"x\\u00e9\\ud83d\\ude00\"],\"c\":{\"d\":-2.5},\"e\":\"q\\\"\\\\\\n\"}";
        v = json_parse(doc, strlen(doc), &a);
    }
    CHECK(v && v->type == J_OBJECT && v->len == 4);
    CHECK(json_int(json_get(v, "a"), 0) == 1);
    CHECK(json_index(json_get(v, "b"), 0)->boolean == 1);
    CHECK(json_index(json_get(v, "b"), 1)->type == J_NULL);
    CHECK(strcmp(json_str(json_index(json_get(v, "b"), 2), ""), "x\xc3\xa9\xf0\x9f\x98\x80") == 0);
    CHECK(json_get(json_get(v, "c"), "d")->num == -2.5);
    CHECK(strcmp(json_str(json_get(v, "e"), ""), "q\"\\\n") == 0);
    CHECK(json_get(v, "zz") == NULL);
    CHECK(json_parse("{\"a\":}", 6, &a) == NULL);
    CHECK(json_parse("[1,2", 4, &a) == NULL);
    CHECK(json_parse("1761000000000", 13, &a) != NULL);
    v = json_parse("1761000000000", 13, &a);
    CHECK(v && v->is_int && v->inum == 1761000000000LL);
    {
        char out[64];
        size_t n = json_escape("a\"b\\c\n\x01\xc3\xa9\xff", 10, out, sizeof out);
        CHECK(strcmp(out, "\"a\\\"b\\\\c\\n\\u0001\xc3\xa9?\"") == 0);
        CHECK(n == strlen(out));
    }
}

static int lines_seen;
static char last_line[256];
static void on_line(void *ctx, const char *line, size_t n) {
    (void)ctx;
    lines_seen++;
    snprintf(last_line, sizeof last_line, "%.*s", (int)n, line);
}

static void test_framing(void) {
    static proto_reader r;
    proto_reader_init(&r);
    lines_seen = 0;
    proto_feed(&r, "{\"a\":1}\r\n{\"b\"", 13, on_line, NULL);
    CHECK(lines_seen == 1 && strcmp(last_line, "{\"a\":1}") == 0);
    proto_feed(&r, ":2}\n\n{\"c\":3}\r\n", 14, on_line, NULL);
    CHECK(lines_seen == 3 && strcmp(last_line, "{\"c\":3}") == 0);
    {
        /* An oversize line is dropped, the next one survives. */
        static char big[PROTO_MAX_PACKET + 10];
        memset(big, 'x', sizeof big);
        proto_feed(&r, big, sizeof big, on_line, NULL);
        proto_feed(&r, "\n{\"d\":4}\n", 9, on_line, NULL);
        CHECK(lines_seen == 4 && strcmp(last_line, "{\"d\":4}") == 0);
        CHECK(r.overflow == 1);
    }
}

static void test_builders(void) {
    char out[4096], nick[256];
    json_arena a;
    proto_packet pk;
    size_t n;
    json_arena_init(&a, arena_mem, sizeof arena_mem);
    n = proto_subscribe(out, sizeof out, "channels.main.messages", "SiX", "PS3");
    CHECK(n > 0 && strcmp(out, "{\"scope\":\"chat\",\"func\":\"QUERY\",\"oper\":\"SUBSCRIBE\",\"location\":\"channels.main.messages\",\"nick\":\"SiX\",\"system\":\"PS3\",\"timeout\":-1}\r\n") == 0);
    CHECK(proto_nick_json(nick, sizeof nick, "SiX", "PS3", "QUJD") > 0);
    CHECK(strcmp(nick, "{\"name\":\"SiX\",\"host\":\"PS3\",\"avatar\":\"QUJD\"}") == 0);
    n = proto_message(out, sizeof out, "WRITE", "channels.main.messages", nick, "hi \"there\"", 1761000000000LL);
    CHECK(n > 0);
    CHECK(proto_parse(out, n - 2, &a, &pk));
    CHECK(strcmp(pk.oper, "WRITE") == 0 && strcmp(pk.location, "channels.main.messages") == 0);
    CHECK(strcmp(json_str(json_get(pk.data, "str"), ""), "hi \"there\"") == 0);
    CHECK(json_int(json_get(pk.data, "time"), 0) == 1761000000000LL);
    CHECK(json_int(json_get(json_parse(out, n - 2, &a), "lock"), 0) == 2);
    n = proto_slice(out, sizeof out, "channels.main.history", -40);
    CHECK(strstr(out, "\"data\":{\"start\":-40}") && strstr(out, "\"lock\":1"));
    n = proto_pong(out, sizeof out, 5);
    CHECK(strcmp(out, "{\"scope\":\"SOCKET\",\"func\":\"PONG\",\"data\":5}\r\n") == 0);
    CHECK(proto_message(out, 16, "WRITE", "x", nick, "too small", 1) == 0);
}

static void test_cp437(void) {
    unsigned char out[32];
    char u[64];
    size_t n = cp437_from_utf8("A\xc3\xa9\xe2\x96\x88\xe2\x82\xac\t\x01z", 13, out, sizeof out);
    CHECK(n == 6 && out[0] == 'A' && out[1] == 0x82 && out[2] == 0xDB && out[3] == '?' && out[4] == ' ' && out[5] == 'z');
    n = cp437_to_utf8(out, 3, u, sizeof u);
    CHECK(strcmp(u, "A\xc3\xa9\xe2\x96\x88") == 0 && n == 6);
    n = cp437_from_utf8("\xff\xfe", 2, out, sizeof out);
    CHECK(n == 2 && out[0] == '?' && out[1] == '?');
}

static void test_avatar(void) {
    unsigned char av[AVATAR_BYTES], got[AVATAR_BYTES];
    char b64[256];
    static unsigned char coll[AVATAR_BYTES * 2 + 128];
    int i;
    for (i = 0; i < AVATAR_BYTES; i += 2) { av[i] = 0xB1; av[i + 1] = 0x1E; }
    CHECK(avatar_valid(av));
    base64_encode(av, AVATAR_BYTES, b64, sizeof b64);
    CHECK(strlen(b64) == 160);
    CHECK(avatar_from_base64(b64, strlen(b64), got) && memcmp(av, got, AVATAR_BYTES) == 0);
    av[3] = 0x9E; /* blink */
    CHECK(!avatar_valid(av));
    av[3] = 0x1E; av[0] = 0x1B;
    CHECK(!avatar_valid(av));
    av[0] = 0xB1;
    /* Collection with a SAUCE trailer. */
    memcpy(coll, av, AVATAR_BYTES);
    memcpy(coll + AVATAR_BYTES, av, AVATAR_BYTES);
    coll[AVATAR_BYTES * 2 - 1] = 0x4F;
    memset(coll + AVATAR_BYTES * 2, 0, 128);
    memcpy(coll + AVATAR_BYTES * 2, "SAUCE00", 7);
    CHECK(avatar_from_collection(coll, sizeof coll, 1, got) == 2 && got[AVATAR_BYTES - 1] == 0x4F);
    CHECK(avatar_from_collection(coll, sizeof coll, 5, got) == 2);
}

/* A chat with a captured outbox. */
static char outbox[16384];
static size_t outlen;
static long long fake_now = 1761000000000LL;
static void cap_send(void *ctx, const char *b, size_t n) { (void)ctx; if (outlen + n < sizeof outbox) { memcpy(outbox + outlen, b, n); outlen += n; outbox[outlen] = 0; } }
static long long cap_now(void *ctx) { (void)ctx; return fake_now; }
static void feed(chat *c, const char *line) { chat_feed_line(c, line, strlen(line)); }
static const char *last_text(const chat *c, char *buf, size_t cap) {
    const chat_msg *m = chat_msg_at(c, c->count - 1);
    if (!m) return "";
    cp437_to_utf8(m->text, (size_t)m->textlen, buf, cap);
    return buf;
}

static void test_chat(void) {
    static chat c;
    chat_io io;
    char buf[2048];
    io.ctx = NULL; io.send = cap_send; io.now_ms = cap_now;
    chat_init(&c, &io);
    chat_set_identity(&c, "SiX", "PS3", NULL);
    chat_set_channel(&c, "main");
    outlen = 0;
    chat_connected(&c);
    CHECK(strstr(outbox, "\"oper\":\"SUBSCRIBE\",\"location\":\"channels.main.messages\",\"nick\":\"SiX\",\"system\":\"PS3\""));
    CHECK(strstr(outbox, "\"location\":\"channels.SiX.messages\""));
    CHECK(strstr(outbox, "\"oper\":\"SLICE\",\"location\":\"channels.main.history\",\"data\":{\"start\":-40}"));
    CHECK(strstr(outbox, "\"oper\":\"WHO\",\"location\":\"channels.main.messages\""));

    /* History arrives: two messages, one with an avatar, one a marker. */
    feed(&c, "{\"scope\":\"chat\",\"func\":\"RESPONSE\",\"oper\":\"SLICE\",\"location\":\"channels.main.history\",\"data\":["
             "{\"nick\":{\"name\":\"alice\",\"host\":\"Future Land\"},\"str\":\"hello \\u2588 world\",\"time\":1760999000000},"
             "{\"nick\":{\"name\":\"bob\"},\"str\":\"[BITMAP|x|y]\",\"time\":1760999100000},"
             "{\"nick\":{\"name\":\"bob\"},\"str\":\"[TVTUNER|off]\",\"time\":1760999100001}]}");
    CHECK(c.count == 2); /* the connect notice + alice */
    CHECK(strcmp(last_text(&c, buf, sizeof buf), "hello \xe2\x96\x88 world") == 0);
    CHECK(strcmp(chat_msg_at(&c, 1)->host, "Future Land") == 0 && chat_msg_at(&c, 1)->kind == MSG_CHAT);

    /* WHO. */
    feed(&c, "{\"func\":\"RESPONSE\",\"oper\":\"WHO\",\"location\":\"channels.main.messages\",\"data\":[{\"id\":1,\"nick\":\"alice\",\"system\":\"Future Land\"},{\"nick\":\"SiX\",\"system\":\"PS3\"}]}");
    CHECK(c.users == 2 && strstr(last_text(&c, buf, sizeof buf), "Users in #main (2): alice (Future Land), SiX (PS3)"));

    /* A live message, a join, a leave, our own presence is silent. */
    feed(&c, "{\"func\":\"UPDATE\",\"oper\":\"WRITE\",\"location\":\"channels.main.messages\",\"data\":{\"nick\":{\"name\":\"alice\",\"host\":\"Future Land\",\"avatar\":\"" 
             "sR6xHrEesR6xHrEesR6xHrEesR6xHrEesR6xHrEesR6xHrEesR6xHrEesR6xHrEesR6xHrEesR6xHrEesR6xHrEesR6xHrEesR6xHrEesR6xHrEesR6xHrEesR6xHrEesR6xHrEesR6xHrEesR6xHrEesR6xHrEesR6xHrEesR6xHrEesR6xHrEesR6xHrEesR6xHrEesR6xHrEesR6xHrEesR6xHrEesR6xHrEesR6xHrEesR6xHrEesR6xHrEesR6xHrEesR6xHrEesR6xHrEesR6xHrEesR6xHrEesR6xHrEesR6xHrEesR6xHrEesR6xHrEesR6xHrEesR6xHrEesR6xHrEesR6xHrEesR6xHrEesR6xHrEesR6xHrEesR6xHrEesR6xHrEesR6xHrEesR6xHrEesR6xHrEesR6xHrEesR6xHrEesR6xHrEesR6xHrEesR6xHrEesR6xHrEesR6xHrEesR6xHrEesR6xHrEesR6xHrEesR6xHrEesR6xHrEesR6xHrEesR6xHrEesR6xHrEesR6xHrEesR6xHrEesR6xHrEesR6xHrEesR6xHrEesR6xHrEesR6xHrEesR6xHrEesR6xHrEesR6xHrEesR6xHrEesR6xHrEesR6xHrEesR6xHrEesR6xHrEesR6xHrEesR6xHrEesR6xHrEesR6xHrEesR6xHrEesR6xHrEesR6xHrEesR6xHrEesR6xHrEesR6xHrEesR6xHrEesR6xHrEesR6xHrEesR6xHrEesR6xHrEesR6xHrEesR6xHrEe\"},\"str\":\"hey SiX\",\"time\":1761000001000}}");
    CHECK(c.count == 4 && strcmp(last_text(&c, buf, sizeof buf), "hey SiX") == 0);
    CHECK(chat_msg_at(&c, 3)->has_avatar && chat_msg_at(&c, 3)->avatar[0] == 0xB1);
    /* Handle style: a colour per letter, a tag. */
    feed(&c, "{\"func\":\"UPDATE\",\"oper\":\"WRITE\",\"location\":\"channels.main.messages\",\"data\":{\"nick\":{\"name\":\"zed\",\"style\":{\"colors\":[\"#ff0000\",\"\",\"#0f0\",7],\"tag\":{\"text\":\"sysop\",\"fg\":\"#ffff55\",\"bg\":\"\"}}},\"str\":\"styled\",\"time\":1761000001500}}");
    CHECK(c.count == 5 && chat_msg_at(&c, 4)->ncolors == 4 && chat_msg_at(&c, 4)->colors[0] == 0xff0000 && chat_msg_at(&c, 4)->colors[1] == 0 && chat_msg_at(&c, 4)->colors[2] == 0x00ff00 && chat_msg_at(&c, 4)->colors[3] == 0);
    CHECK(strcmp(chat_msg_at(&c, 4)->tag, "sysop") == 0 && chat_msg_at(&c, 4)->tag_fg == 0xffff55 && chat_msg_at(&c, 4)->tag_bg == 0);
    c.count--; /* keep the rest of the counts below unchanged */
    feed(&c, "{\"func\":\"UPDATE\",\"oper\":\"SUBSCRIBE\",\"location\":\"channels.main.messages\",\"data\":{\"nick\":\"carol\",\"system\":\"X\"}}");
    CHECK(c.users == 3 && strcmp(last_text(&c, buf, sizeof buf), "carol is here.") == 0);
    feed(&c, "{\"func\":\"UPDATE\",\"oper\":\"UNSUBSCRIBE\",\"location\":\"channels.main.messages\",\"data\":{\"nick\":\"carol\",\"system\":\"X\"}}");
    CHECK(c.users == 2 && strcmp(last_text(&c, buf, sizeof buf), "carol has left.") == 0);
    feed(&c, "{\"func\":\"UPDATE\",\"oper\":\"SUBSCRIBE\",\"location\":\"channels.main.messages\",\"data\":{\"nick\":\"SiX\",\"system\":\"PS3\"}}");
    CHECK(c.count == 6 && c.users == 2);
    /* Another room's traffic is ignored. */
    feed(&c, "{\"func\":\"UPDATE\",\"oper\":\"WRITE\",\"location\":\"channels.other.messages\",\"data\":{\"nick\":{\"name\":\"z\"},\"str\":\"no\",\"time\":1}}");
    CHECK(c.count == 6);

    /* PING -> PONG. */
    outlen = 0;
    feed(&c, "{\"scope\":\"SOCKET\",\"func\":\"PING\",\"data\":123}");
    CHECK(strcmp(outbox, "{\"scope\":\"SOCKET\",\"func\":\"PONG\",\"data\":1761000000000}\r\n") == 0);

    /* Sending: WRITE + PUSH, local echo, server echo deduped. */
    outlen = 0;
    chat_input(&c, (const unsigned char *)"  hi all \x82 ", strlen("  hi all \x82 "));
    CHECK(strstr(outbox, "\"oper\":\"WRITE\",\"location\":\"channels.main.messages\",\"data\":{\"nick\":{\"name\":\"SiX\",\"host\":\"PS3\"},\"str\":\"hi all \xc3\xa9\",\"time\":1761000000000},\"lock\":2"));
    CHECK(strstr(outbox, "\"oper\":\"PUSH\",\"location\":\"channels.main.history\""));
    CHECK(c.count == 7 && chat_msg_at(&c, 6)->self && strcmp(last_text(&c, buf, sizeof buf), "hi all \xc3\xa9") == 0);
    feed(&c, "{\"func\":\"UPDATE\",\"oper\":\"WRITE\",\"location\":\"channels.main.messages\",\"data\":{\"nick\":{\"name\":\"SiX\",\"host\":\"PS3\"},\"str\":\"hi all \xc3\xa9\",\"time\":1761000000000}}");
    CHECK(c.count == 7);

    /* Private message in, /r out to a web user's mailbox. */
    feed(&c, "{\"func\":\"UPDATE\",\"oper\":\"WRITE\",\"location\":\"channels.SiX.messages\",\"data\":{\"nick\":{\"name\":\"dave\",\"host\":\"My BBS\",\"mailbox\":\"dave@My_BBS\"},\"str\":\"psst\",\"time\":1761000002000}}");
    CHECK(c.count == 8 && chat_msg_at(&c, 7)->kind == MSG_PRIVATE && strcmp(c.last_pm_from, "dave") == 0);
    outlen = 0;
    chat_input(&c, (const unsigned char *)"/r yes?", strlen("/r yes?"));
    CHECK(strstr(outbox, "\"location\":\"channels.dave@My_BBS.messages\""));
    CHECK(strstr(outbox, "\"location\":\"channels.dave@My_BBS.history\""));
    CHECK(strcmp(last_text(&c, buf, sizeof buf), "-> dave: yes?") == 0);
    /* A forged mailbox is ignored: the PM goes to the plain name. */
    feed(&c, "{\"func\":\"UPDATE\",\"oper\":\"WRITE\",\"location\":\"channels.main.messages\",\"data\":{\"nick\":{\"name\":\"eve\",\"host\":\"x\",\"mailbox\":\"dave@My_BBS\"},\"str\":\"hi\",\"time\":1761000003000}}");
    outlen = 0;
    chat_input(&c, (const unsigned char *)"/msg eve hello", strlen("/msg eve hello"));
    CHECK(strstr(outbox, "\"location\":\"channels.eve.messages\""));

    /* /join: subscribe, slice, who; main stays joined; ring cleared. */
    outlen = 0;
    chat_input(&c, (const unsigned char *)"/join #Lobby", strlen("/join #Lobby"));
    CHECK(!strstr(outbox, "UNSUBSCRIBE"));
    CHECK(strstr(outbox, "\"oper\":\"SUBSCRIBE\",\"location\":\"channels.Lobby.messages\""));
    CHECK(strstr(outbox, "\"location\":\"channels.Lobby.history\""));
    CHECK(strcmp(c.channel, "Lobby") == 0 && c.count == 1 && c.njoined == 2 && chat_current_room(&c) == 1);
    chat_input(&c, (const unsigned char *)"/join bad room!", strlen("/join bad room!"));
    CHECK(strcmp(c.channel, "Lobby") == 0);
    /* Traffic on main is counted, with a mention flag; viewing it clears it. */
    feed(&c, "{\"func\":\"UPDATE\",\"oper\":\"WRITE\",\"location\":\"channels.main.messages\",\"data\":{\"nick\":{\"name\":\"alice\"},\"str\":\"anyone?\",\"time\":1761000004000}}");
    feed(&c, "{\"func\":\"UPDATE\",\"oper\":\"WRITE\",\"location\":\"channels.main.messages\",\"data\":{\"nick\":{\"name\":\"alice\"},\"str\":\"hey @six!\",\"time\":1761000004001}}");
    feed(&c, "{\"func\":\"UPDATE\",\"oper\":\"WRITE\",\"location\":\"channels.main.messages\",\"data\":{\"nick\":{\"name\":\"SiX\",\"host\":\"PS3\"},\"str\":\"my own\",\"time\":1761000004002}}");
    CHECK(c.unread[0] == 2 && c.mentioned[0] == 1 && c.count == 2); /* the bad-name notice is the 2nd */
    outlen = 0;
    chat_switch(&c, 1);
    CHECK(strcmp(c.channel, "main") == 0 && c.unread[0] == 0 && c.mentioned[0] == 0 && strstr(outbox, "channels.main.history") && !strstr(outbox, "SUBSCRIBE"));
    chat_switch(&c, -1);
    CHECK(strcmp(c.channel, "Lobby") == 0);
    /* Reconnect subscribes to every joined room. */
    outlen = 0;
    chat_connected(&c);
    CHECK(strstr(outbox, "channels.main.messages\",\"nick\"") && strstr(outbox, "channels.Lobby.messages\",\"nick\""));
    /* /leave the viewed room falls back to the other one; can't leave the last. */
    outlen = 0;
    chat_input(&c, (const unsigned char *)"/leave", 6);
    CHECK(strstr(outbox, "\"oper\":\"UNSUBSCRIBE\",\"location\":\"channels.Lobby.messages\"") && strcmp(c.channel, "main") == 0 && c.njoined == 1);
    chat_input(&c, (const unsigned char *)"/leave", 6);
    CHECK(c.njoined == 1 && strcmp(c.channel, "main") == 0);

    /* ERROR shows as a notice; /quit sets the flag; keepalive ping. */
    feed(&c, "{\"func\":\"ERROR\",\"data\":{\"description\":\"slow down\"}}");
    CHECK(strcmp(last_text(&c, buf, sizeof buf), "Server: slow down") == 0);
    chat_input(&c, (const unsigned char *)"/quit", strlen("/quit"));
    CHECK(c.want_quit == 1);
    outlen = 0;
    fake_now += 61000;
    chat_tick(&c);
    CHECK(strstr(outbox, "\"func\":\"PING\""));
    outlen = 0;
    chat_tick(&c);
    CHECK(outlen == 0);
}

static void test_ring_wraps(void) {
    static chat c;
    chat_io io;
    int i;
    io.ctx = NULL; io.send = cap_send; io.now_ms = cap_now;
    chat_init(&c, &io);
    for (i = 0; i < CHAT_RING + 10; i++) chat_notice(&c, "n%d", i);
    CHECK(c.count == CHAT_RING);
    CHECK(chat_msg_at(&c, 0)->text[1] == '1' && chat_msg_at(&c, 0)->text[2] == '0'); /* n10 */
    CHECK(chat_msg_at(&c, CHAT_RING) == NULL);
}

static void test_wrap_and_render(void) {
    ui_seg segs[16];
    int n = ui_wrap((const unsigned char *)"the quick brown fox jumps", 25, 10, segs, 16);
    CHECK(n == 3 && segs[0].len == 9 && segs[1].start == 10 && segs[1].len == 9 && segs[2].start == 20 && segs[2].len == 5);
    n = ui_wrap((const unsigned char *)"abcdefghijklmnop", 16, 5, segs, 16);
    CHECK(n == 4 && segs[3].start == 15);
    n = ui_wrap((const unsigned char *)"", 0, 5, segs, 16);
    CHECK(n == 1 && segs[0].len == 0);
    {
        static chat c;
        static ui u;
        static screen s;
        chat_io io;
        char row[UI_MAX_COLS + 1];
        int x, found = 0, y;
        io.ctx = NULL; io.send = cap_send; io.now_ms = cap_now;
        chat_init(&c, &io);
        chat_set_identity(&c, "SiX", "PS3", NULL);
        ui_init(&u);
        u.mode = UI_CHAT;
        feed(&c, "{\"func\":\"UPDATE\",\"oper\":\"WRITE\",\"location\":\"channels.main.messages\",\"data\":{\"nick\":{\"name\":\"alice\",\"host\":\"FL\"},\"str\":\"hello there\",\"time\":1760999999000}}");
        s.cols = 80; s.rows = 25;
        ui_render(&u, &c, &s, fake_now, "online");
        for (x = 0; x < 80; x++) row[x] = (char)s.cells[x].ch;
        row[80] = 0;
        CHECK(strncmp(row, " Avatar Chat  #main  0 users", 28) == 0);
        for (y = 1; y < 22 && !found; y++) {
            for (x = 0; x < 80; x++) row[x] = (char)s.cells[y * 80 + x].ch;
            row[80] = 0;
            if (strstr(row, "alice @FL") && strstr(row, "1m")) found = y;
        }
        CHECK(found > 0);
        /* The placeholder avatar frame sits left of the name line. */
        CHECK(s.cells[found * 80 + 1].ch == 0xDA && s.cells[(found + 5) * 80 + 1].ch == 0xC0);
        for (x = 0; x < 80; x++) row[x] = (char)s.cells[(found + 1) * 80 + x].ch;
        CHECK(strstr(row, "hello there") == row + 12);
        /* Our own line: avatar frame at the right edge, text ending before it. */
        c.connected = 1;
        chat_input(&c, (const unsigned char *)"mine", 4);
        ui_render(&u, &c, &s, fake_now, "online");
        found = 0;
        for (y = 1; y < 22 && !found; y++)
            if (s.cells[y * 80 + 69].ch == 0xDA && s.cells[y * 80 + 78].ch == 0xBF) found = y;
        CHECK(found > 0);
        for (x = 0; x < 80; x++) row[x] = (char)s.cells[found * 80 + x].ch;
        row[80] = 0;
        CHECK(strstr(row, "SiX @PS3") && strstr(row, "SiX @PS3") + 8 == row + 68);
        for (x = 0; x < 80; x++) row[x] = (char)s.cells[(found + 1) * 80 + x].ch;
        CHECK(strncmp(row + 64, "mine", 4) == 0);
        c.count = 1; /* drop it again for the checks below */
        /* Typing goes to the input row; Enter sends. */
        ui_key(&u, &c, 'h', fake_now, NULL, 0);
        ui_key(&u, &c, 'i', fake_now, NULL, 0);
        CHECK(u.inlen == 2);
        ui_render(&u, &c, &s, fake_now, "online");
        CHECK(s.cells[23 * 80 + 0].ch == '>' && s.cells[23 * 80 + 2].ch == 'h' && s.cur_x == 4 && s.cur_y == 23);
        CHECK(ui_palette[0] == 0 && ui_palette[15] == 0xFFFFFF && ui_palette[16] == 0 && ui_palette[21] == 0x0000FF && ui_palette[196] == 0xFF0000 && ui_palette[231] == 0xFFFFFF && ui_palette[232] == 0x080808 && ui_palette[255] == 0xEEEEEE);
        outlen = 0;
        c.connected = 1;
        ui_key(&u, &c, KEY_ENTER, fake_now, NULL, 0);
        CHECK(u.inlen == 0 && strstr(outbox, "\"str\":\"hi\""));
        /* Compact toggle and scroll clamp. */
        ui_key(&u, &c, KEY_TOGGLE_COMPACT, fake_now, NULL, 0);
        CHECK(u.compact == 1);
        ui_key(&u, &c, KEY_PGUP, fake_now, NULL, 0);
        ui_render(&u, &c, &s, fake_now, "online");
        CHECK(u.scroll == 0); /* nothing to scroll into */
        /* Nick prompt validation. */
        ui_init(&u);
        ui_key(&u, &c, 'a', fake_now, NULL, 0);
        ui_key(&u, &c, '!', fake_now, NULL, 0);
        CHECK(u.inlen == 1);
        {
            char nick[32] = "";
            ui_key(&u, &c, KEY_ENTER, fake_now, nick, sizeof nick);
            CHECK(strcmp(nick, "a") == 0 && u.mode == UI_CHAT);
        }
        /* The built-in pad keyboard: type "hi" then send with Start. */
        ui_init(&u); u.mode = UI_CHAT; c.connected = 1; outlen = 0;
        ui_open_pad_keyboard(&u);
        CHECK(u.kb_open && u.kb_row == 1 && u.kb_col == 0);
        /* 'h' is row 2 col 5; 'i' is row 1 col 7. */
        ui_key(&u, &c, KEY_DOWN, fake_now, NULL, 0);
        { int k; for (k = 0; k < 5; k++) ui_key(&u, &c, KEY_RIGHT, fake_now, NULL, 0); }
        ui_key(&u, &c, KEY_PAD_ENTER, fake_now, NULL, 0);
        ui_key(&u, &c, KEY_UP, fake_now, NULL, 0);
        ui_key(&u, &c, KEY_RIGHT, fake_now, NULL, 0); ui_key(&u, &c, KEY_RIGHT, fake_now, NULL, 0);
        ui_key(&u, &c, KEY_PAD_ENTER, fake_now, NULL, 0);
        CHECK(u.inlen == 2 && u.input[0] == 'h' && u.input[1] == 'i');
        ui_key(&u, &c, KEY_PAD_L1, fake_now, NULL, 0);   /* shift */
        ui_key(&u, &c, KEY_PAD_ENTER, fake_now, NULL, 0); /* 'I' */
        CHECK(u.inlen == 3 && u.input[2] == 'I' && u.kb_shift == 0);
        ui_key(&u, &c, KEY_VIEW_ART, fake_now, NULL, 0);  /* Square: backspace */
        CHECK(u.inlen == 2);
        ui_key(&u, &c, KEY_LEFT, fake_now, NULL, 0);
        CHECK(u.kb_col == 6);
        { int k; for (k = 0; k < 7; k++) ui_key(&u, &c, KEY_LEFT, fake_now, NULL, 0); }
        CHECK(u.kb_col == 11);                             /* wraps to the row end */
        s.cols = 80; s.rows = 25;
        ui_render(&u, &c, &s, fake_now, "online");
        { int found_kb = 0, yy; for (yy = 0; yy < 25; yy++) if (s.cells[yy * 80 + 23].ch == 'q' || s.cells[yy * 80 + 24].ch == 'q') found_kb = 1; CHECK(found_kb); }
        ui_key(&u, &c, KEY_QUIT, fake_now, NULL, 0);      /* Start: send */
        CHECK(!u.kb_open && !u.quit && u.inlen == 0 && strstr(outbox, "\"str\":\"hi\""));
        /* The on-screen keyboard's text goes out like a typed line. */
        outlen = 0;
        c.connected = 1;
        u.osk_open = 1;
        ui_osk_result(&u, &c, (const unsigned char *)"from the osk", 12, fake_now, NULL, 0);
        CHECK(u.osk_open == 0 && u.inlen == 0 && strstr(outbox, "\"str\":\"from the osk\""));
        ui_init(&u);
        {
            char nick[32] = "";
            ui_osk_result(&u, &c, (const unsigned char *)"PadUser", 7, fake_now, nick, sizeof nick);
            CHECK(strcmp(nick, "PadUser") == 0 && u.mode == UI_CHAT);
        }
    }
}

static void test_inflate_and_art(void) {
    static unsigned char out[8192];
    long n;
    n = inflate_zlib(v_stored_z, sizeof v_stored_z, out, sizeof out);
    CHECK(n == V_STORED_RAWLEN && memcmp(out, v_stored_raw, (size_t)n) == 0);
    n = inflate_zlib(v_fixed_z, sizeof v_fixed_z, out, sizeof out);
    CHECK(n == V_FIXED_RAWLEN && memcmp(out, v_fixed_raw, (size_t)n) == 0);
    n = inflate_zlib(v_dyn_z, sizeof v_dyn_z, out, sizeof out);
    CHECK(n == V_DYN_RAWLEN && memcmp(out, v_dyn_raw, (size_t)n) == 0);
    CHECK(inflate_zlib(v_dyn_z, sizeof v_dyn_z, out, 100) == -1);          /* too small */
    CHECK(inflate_zlib(v_dyn_z, sizeof v_dyn_z / 2, out, sizeof out) == -1); /* truncated */
    CHECK(inflate_zlib((const unsigned char *)"nope!!", 6, out, sizeof out) == -1);
    {
        static art_image img;
        char from[32];
        CHECK(art_is_envelope(v_env) && !art_is_envelope("[TVTUNER|off]"));
        CHECK(art_parse(v_env, strlen(v_env), &img, from, sizeof from) == 1);
        CHECK(img.w == 12 && img.h == 3 && strcmp(from, "alice") == 0);
        CHECK(img.ch[0] == 'A' && img.ch[35] == 'J' && img.fg[0] == 1 && img.bg[0] == 16 && img.fg[35] == 36 && img.bg[35] == 51);
        CHECK(art_parse("[BITMAP|1|1|x|zz]", 17, &img, NULL, 0) == 0);
        CHECK(art_parse("[BITMAP|1|1|x", 13, &img, NULL, 0) == 0);
    }
    {
        /* Through the chat: a notice, a queued piece, /img opens it. */
        static chat c;
        static ui u;
        static screen s;
        chat_io io;
        char line[8192], buf[256];
        io.ctx = NULL; io.send = cap_send; io.now_ms = cap_now;
        chat_init(&c, &io);
        chat_set_identity(&c, "SiX", "PS3", NULL);
        ui_init(&u);
        u.mode = UI_CHAT;
        snprintf(line, sizeof line, "{\"func\":\"UPDATE\",\"oper\":\"WRITE\",\"location\":\"channels.main.messages\",\"data\":{\"nick\":{\"name\":\"alice\"},\"str\":\"%s\",\"time\":1761000000000}}", v_env);
        feed(&c, line);
        CHECK(c.nart == 1 && chat_unviewed_art(&c) == 1 && c.count == 1);
        CHECK(strcmp(last_text(&c, buf, sizeof buf), "alice sent art (12x3). /img or Square to view") == 0);
        chat_input(&c, (const unsigned char *)"/img", 4);
        CHECK(c.want_art == 0);
        ui_key(&u, &c, KEY_VIEW_ART, fake_now, NULL, 0);
        CHECK(u.view_art == 0);
        s.cols = 80; s.rows = 25;
        ui_render(&u, &c, &s, fake_now, "online");
        CHECK(chat_unviewed_art(&c) == 0);
        /* Centered: 12 wide in 80 -> x offset 34; 3 tall in 23 -> y offset 10, plus the header row. */
        CHECK(s.cells[11 * 80 + 34].ch == 'A' && s.cells[11 * 80 + 34].fg == ui_palette[1] && s.cells[11 * 80 + 34].bg == ui_palette[16]);
        ui_key(&u, &c, KEY_ESC, fake_now, NULL, 0);
        CHECK(u.view_art == -1);
    }
}

static void test_png_and_pics(void) {
    static unsigned char scratch[65536], rgb[4 * 4 * 3];
    char err[64];
    int w, h;
    CHECK(png_size(png_rgb, sizeof png_rgb, &w, &h) && w == 2 && h == 2);
    CHECK(png_decode(png_rgb, sizeof png_rgb, rgb, 2, 2, scratch, sizeof scratch, err, sizeof err));
    CHECK(rgb[0] == 255 && rgb[1] == 0 && rgb[2] == 0 && rgb[3] == 0 && rgb[4] == 255 && rgb[5] == 0);
    CHECK(rgb[6] == 0 && rgb[7] == 0 && rgb[8] == 255 && rgb[9] == 255 && rgb[10] == 255 && rgb[11] == 255);
    CHECK(png_size(png_pal, sizeof png_pal, &w, &h) && png_decode(png_pal, sizeof png_pal, rgb, 3, 1, scratch, sizeof scratch, err, sizeof err));
    CHECK(rgb[0] == 10 && rgb[1] == 20 && rgb[2] == 30 && rgb[3] == 0 && rgb[4] == 0 && rgb[5] == 0 && rgb[6] == 70);
    CHECK(png_size(png_g16, sizeof png_g16, &w, &h) && png_decode(png_g16, sizeof png_g16, rgb, 2, 1, scratch, sizeof scratch, err, sizeof err));
    CHECK(rgb[0] == 0x80 && rgb[3] == 0xff);
    CHECK(png_size(png_rgba, sizeof png_rgba, &w, &h) && png_decode(png_rgba, sizeof png_rgba, rgb, 2, 2, scratch, sizeof scratch, err, sizeof err));
    /* row0: avg filter from nothing -> px0 = (200,100,50,a255); px1 = px0/2 -> (100,50,25,a127) -> composited (49,24,12) */
    CHECK(rgb[0] == 200 && rgb[1] == 100 && rgb[2] == 50 && rgb[3] == 49 && rgb[4] == 24 && rgb[5] == 12);
    CHECK(png_size(png_il, sizeof png_il, &w, &h) && png_decode(png_il, sizeof png_il, rgb, 4, 4, scratch, sizeof scratch, err, sizeof err));
    {
        int i, ok = 1;
        for (i = 0; i < 16; i++) if (rgb[i * 3] != i) ok = 0;
        CHECK(ok);
    }
    CHECK(!png_decode(png_rgb, sizeof png_rgb - 20, rgb, 2, 2, scratch, sizeof scratch, err, sizeof err));
    CHECK(!png_size((const unsigned char *)"GIF89a", 6, &w, &h));
    /* Links. */
    {
        char links[3][PIC_URL];
        const char *t = "see https://x.org/a/cat.PNG?x=1, and http://y/dog.jpg. not http://z/page.html https://q/w.webp";
        int n = pics_find_links((const unsigned char *)t, (int)strlen(t), links, 3);
        CHECK(n == 3 && strcmp(links[0], "https://x.org/a/cat.PNG?x=1") == 0 && strcmp(links[1], "http://y/dog.jpg") == 0 && strcmp(links[2], "https://q/w.webp") == 0);
        CHECK(!pics_is_image_url("ftp://a/b.png", 13) && pics_is_image_url("http://a/b.jpeg#f", 17));
    }
    /* Scaling: a 400x100 image into 40 columns keeps the aspect: 40x10 half-rows = 5 cell rows. */
    {
        static pic_cache pc;
        static unsigned char big[400 * 100 * 3];
        pic *p;
        int i;
        for (i = 0; i < 400 * 100; i++) { big[i * 3] = (unsigned char)(i % 400 < 200 ? 255 : 0); big[i * 3 + 1] = 0; big[i * 3 + 2] = (unsigned char)((i / 400) < 50 ? 255 : 0); }
        pics_init(&pc);
        p = pics_get(&pc, "http://h/i.png");
        CHECK(p && p->state == PIC_QUEUED && pics_next_queued(&pc) == p && pics_find(&pc, "http://h/i.png") == p);
        pics_set_rgb(p, big, 400, 100, 40);
        CHECK(p->state == PIC_READY && p->cols == 40 && p->rows == 5);
        CHECK(p->top[0] == 0xFF00FF && p->bot[4 * 40 + 39] == 0x000000 && p->top[3 * 40 + 0] == 0xFF0000 && p->bot[1 * 40 + 20] == 0x0000FF);
        /* The cache evicts the least recently used ready entry. */
        for (i = 0; i < PIC_MAX + 2; i++) { char u[64]; snprintf(u, sizeof u, "http://h/%d.png", i); pics_get(&pc, u); }
        CHECK(pics_find(&pc, "http://h/i.png") == NULL && pics_find(&pc, "http://h/25.png") != NULL);
    }
}

static void test_gif(void) {
    static unsigned char scratch[20480], rgb[64 * 64 * 3];
    char err[64];
    int w, h, x, y, ok = 1;
    CHECK(gif_size(gif_small, sizeof gif_small, &w, &h) && w == 4 && h == 2);
    CHECK(gif_decode(gif_small, sizeof gif_small, rgb, 4, 2, scratch, sizeof scratch, err, sizeof err));
    CHECK(rgb[0] == 255 && rgb[4] == 255 && rgb[8] == 255 && rgb[9] == 0 && rgb[10] == 0 && rgb[11] == 0);
    CHECK(rgb[12] == 0 && rgb[13] == 0 && rgb[14] == 0 && rgb[17] == 255 && rgb[19] == 255 && rgb[21] == 255 && rgb[22] == 0);
    CHECK(gif_size(gif_il, sizeof gif_il, &w, &h) && gif_decode(gif_il, sizeof gif_il, rgb, 8, 8, scratch, sizeof scratch, err, sizeof err));
    for (y = 0; y < 8; y++) for (x = 0; x < 8; x++) if (rgb[(y * 8 + x) * 3] != (y % 2 ? 200 : 10)) ok = 0;
    CHECK(ok);
    CHECK(gif_size(gif_big, sizeof gif_big, &w, &h) && gif_decode(gif_big, sizeof gif_big, rgb, 64, 64, scratch, sizeof scratch, err, sizeof err));
    ok = 1;
    for (y = 0; y < 64; y++) for (x = 0; x < 64; x++) if (rgb[(y * 64 + x) * 3] != 1 + 3 * ((x * y) % 3)) ok = 0;
    CHECK(ok);
    CHECK(!gif_decode(gif_big, sizeof gif_big / 3, rgb, 64, 64, scratch, sizeof scratch, err, sizeof err) || 1); /* truncated: no crash */
    CHECK(!gif_is(png_rgb, sizeof png_rgb));
}

static void test_config(void) {
    app_config cfg;
    FILE *f = fopen("build-host/test.ini", "w");
    config_defaults(&cfg);
    CHECK(strcmp(cfg.host, "futureland.today") == 0 && cfg.port == 10088 && strcmp(cfg.system, "PS3") == 0);
    if (f) {
        fputs("; comment\nhost = bbs.example.org\nport=10089\nnick = SiX\n[ui]\ncompact = on\noverscan = 99\nbogus = 1\n", f);
        fclose(f);
        CHECK(config_load(&cfg, "build-host/test.ini") == 1);
        CHECK(strcmp(cfg.host, "bbs.example.org") == 0 && cfg.port == 10089 && strcmp(cfg.nick, "SiX") == 0 && cfg.compact == 1 && cfg.overscan_pct == 15);
    }
    CHECK(config_load(&cfg, "build-host/missing.ini") == 0);
}

int main(void) {
    test_json();
    test_framing();
    test_builders();
    test_cp437();
    test_avatar();
    test_chat();
    test_ring_wraps();
    test_wrap_and_render();
    test_inflate_and_art();
    test_png_and_pics();
    test_gif();
    test_config();
    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
