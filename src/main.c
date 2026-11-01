/* Avatar Chat for the PlayStation 3: a native client for the BBS chat the
 * avatar_chat doors share. One loop: read the socket, read the keys, draw. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "platform.h"
#include "config.h"
#include "core/chat.h"
#include "core/ui.h"
#include "core/proto.h"
#include "core/avatar.h"
#include "core/player.h"

static chat g_chat;
static ui g_ui;
static screen g_screen;
static proto_reader g_reader;
static int g_sock = -1;
static long long g_next_connect;
static int g_retry;
static char g_conn[32] = "offline";

static void io_send(void *ctx, const char *bytes, size_t n) {
    (void)ctx;
    if (g_sock < 0) return;
    if (plat_net_send_all(g_sock, bytes, n) < 0) {
        plat_net_close(g_sock);
        g_sock = -1;
        chat_disconnected(&g_chat, "send failed");
    }
}

static long long io_now(void *ctx) { (void)ctx; return plat_now_ms(); }

static void on_line(void *ctx, const char *line, size_t n) {
    (void)ctx;
    chat_feed_line(&g_chat, line, n);
}

/* ---- audio: /play downloads a track, then decodes a little ahead of the port */
static struct {
    int state;                 /* 0 idle, 1 downloading, 2 playing */
    pic *dl;
    unsigned char *data;
    size_t n;
    player pl;
    char url[PIC_URL];
} g_audio;

static void audio_stop(const char *why) {
    if (g_audio.state == 2) {
        plat_audio_close();
        if (g_audio.pl.dec) adec_close(g_audio.pl.dec);
        g_audio.pl.dec = NULL;
    }
    free(g_audio.data);
    g_audio.data = NULL;
    free(g_audio.pl.fifo);
    g_audio.pl.fifo = NULL;
    if (g_audio.state && why) chat_notice(&g_chat, "%s %s", why, g_audio.pl.name);
    g_audio.state = 0;
    g_audio.dl = NULL;
}

static void audio_start(const char *url) {
    const char *name = strrchr(url, '/');
    audio_stop(NULL);
    snprintf(g_audio.url, sizeof g_audio.url, "%s", url);
    snprintf(g_audio.pl.name, sizeof g_audio.pl.name, "%.95s", name ? name + 1 : url);
    g_audio.dl = pics_get(&g_chat.pics, url, FETCH_AUDIO);
    if (g_audio.dl->state == PIC_FAILED) g_audio.dl->state = PIC_QUEUED;
    g_audio.dl->veiled = 0;
    g_audio.state = 1;
    chat_notice(&g_chat, "Fetching %s ...", g_audio.pl.name);
}

/* Called when a FETCH_AUDIO download completes (data valid until the next fetch). */
static void audio_downloaded(const unsigned char *data, size_t n) {
    char err[80];
    int rate = 0, channels = 0;
    if (g_audio.state != 1) return;
    g_audio.data = malloc(n);
    if (!g_audio.data) { audio_stop("Out of memory for"); return; }
    memcpy(g_audio.data, data, n);
    g_audio.n = n;
    g_audio.pl.dec = adec_open(g_audio.data, n, g_audio.pl.name, err, sizeof err);
    if (!g_audio.pl.dec) { chat_notice(&g_chat, "Can't play %s: %s", g_audio.pl.name, err); audio_stop(NULL); return; }
    adec_info(g_audio.pl.dec, &rate, &channels);
    if (rate < 8000 || rate > 192000 || channels < 1 || channels > 2) { adec_close(g_audio.pl.dec); g_audio.pl.dec = NULL; chat_notice(&g_chat, "Can't play %s: odd format", g_audio.pl.name); audio_stop(NULL); return; }
    g_audio.pl.rate = rate;
    g_audio.pl.channels = channels;
    g_audio.pl.head = g_audio.pl.count = 0;
    g_audio.pl.done = 0;
    g_audio.pl.frames_out = 0;
    g_audio.pl.fifo = malloc((size_t)PLAYER_FIFO_FRAMES * channels * sizeof(short));
    if (!g_audio.pl.fifo || plat_audio_open(rate, channels) != 0) { chat_notice(&g_chat, "No audio output."); audio_stop(NULL); return; }
    g_audio.state = 2;
    chat_notice(&g_chat, "Playing %s (%d Hz, %s). /stop stops.", g_audio.pl.name, rate, channels == 2 ? "stereo" : "mono");
    plat_log("audio: playing %s %d Hz x%d", g_audio.pl.name, rate, channels);
}

static void pump_audio(void) {
    static short out[8192 * 2];
    if (g_chat.want_stop) { g_chat.want_stop = 0; if (g_audio.state) audio_stop("Stopped"); else chat_notice(&g_chat, "Nothing is playing."); }
    if (g_chat.want_play[0]) { audio_start(g_chat.want_play); g_chat.want_play[0] = 0; }
    if (g_audio.state != 2) return;
    if (player_fill(&g_audio.pl, adec_pull) < 0) { audio_stop("Decoding failed for"); return; }
    {
        int space = plat_audio_space(), want = space < 8192 ? space : 8192, got;
        if (want > 0) {
            got = player_take(&g_audio.pl, out, want);
            if (got > 0) plat_audio_write(out, got);
        }
    }
    if (g_audio.pl.done && player_empty(&g_audio.pl)) audio_stop("Finished");
}

/* One picture download at a time, polled from the loop. */
static long long g_osk_opened;
static pic *g_fetching;
static long long g_fetch_started;
#define PIC_MAX_BYTES (5u * 1024u * 1024u)
#define PIC_TIMEOUT_MS 15000

static void pump_pictures(long long now) {
    char err[64];
    if (!g_fetching) {
        pic *p = pics_next_queued(&g_chat.pics);
        if (!p) return;
        if (p->veiled) return; /* waits for /nsfw */
        if (plat_fetch_start(p->url, p->kind == FETCH_PAGE ? 256u * 1024u : p->kind == FETCH_AUDIO ? 20u * 1024u * 1024u : PIC_MAX_BYTES, err, sizeof err) != 0) {
            pics_fail(p, err);
            g_chat.seq++;
            if (p->kind == FETCH_AUDIO && g_audio.dl == p) { chat_notice(&g_chat, "Can't fetch %s: %s", g_audio.pl.name, err); audio_stop(NULL); }
            return;
        }
        p->state = PIC_LOADING;
        g_fetching = p;
        g_fetch_started = now;
        plat_log("picture: fetching %s", p->url);
        return;
    }
    {
        const unsigned char *data = NULL;
        size_t n = 0;
        int rc = plat_fetch_poll(&data, &n, err, sizeof err);
        if (rc == 0) {
            if (now - g_fetch_started > PIC_TIMEOUT_MS) { plat_fetch_cancel(); pics_fail(g_fetching, "timed out"); g_fetching = NULL; g_chat.seq++; }
            return;
        }
        if (rc < 0) {
            pics_fail(g_fetching, err);
            plat_log("fetch: %s", err);
            if (g_fetching->kind == FETCH_AUDIO && g_audio.dl == g_fetching) { chat_notice(&g_chat, "Can't fetch %s: %s", g_audio.pl.name, err); audio_stop(NULL); }
        } else if (g_fetching->kind == FETCH_AUDIO) {
            g_fetching->state = PIC_READY;
            if (g_audio.dl == g_fetching) audio_downloaded(data, n);
        } else if (g_fetching->kind == FETCH_PAGE) {
            pics_parse_page(g_fetching, (const char *)data, n);
            g_fetching->state = PIC_READY;
            plat_log("card: %s | %s", g_fetching->site, g_fetching->title);
        } else {
            unsigned char *rgb = NULL;
            int w = 0, h = 0;
            if (plat_decode_image(data, n, &rgb, &w, &h, err, sizeof err)) {
                pics_set_rgb(g_fetching, rgb, w, h, ui_pic_cols(&g_ui, g_screen.cols > 0 ? g_screen.cols : 80));
                free(rgb);
                plat_log("picture: %dx%d -> %dx%d cells", w, h, g_fetching->cols, g_fetching->rows);
            } else {
                pics_fail(g_fetching, err);
                plat_log("picture: decode: %s", err);
            }
        }
        g_fetching = NULL;
        g_chat.seq++;
    }
}

static int load_avatar(const app_config *cfg, unsigned char out[AVATAR_BYTES]) {
    char path[512];
    static unsigned char data[64 * 1024];
    FILE *f;
    size_t n;
    if (!cfg->avatar_file[0]) return 0;
    if (cfg->avatar_file[0] == '/') snprintf(path, sizeof path, "%s", cfg->avatar_file);
    else snprintf(path, sizeof path, "%s%s", plat_base_dir(), cfg->avatar_file);
    f = fopen(path, "rb");
    if (!f) return 0;
    n = fread(data, 1, sizeof data, f);
    fclose(f);
    if (avatar_from_collection(data, n, cfg->avatar_index, out) <= 0) return 0;
    return avatar_valid(out);
}

static void try_connect(app_config *cfg) {
    char err[128];
    snprintf(g_conn, sizeof g_conn, "connecting");
    g_ui.dirty = 1;
    plat_screen_size(&g_screen.cols, &g_screen.rows);
    ui_render(&g_ui, &g_chat, &g_screen, plat_now_ms(), g_conn);
    plat_present(&g_screen);
    g_sock = plat_net_connect(cfg->host, cfg->port, err, sizeof err);
    if (g_sock < 0) {
        long long wait = 5000LL * (g_retry < 6 ? (1 << g_retry) : 64);
        if (wait > 120000) wait = 120000;
        g_retry++;
        chat_notice(&g_chat, "Can't reach %s:%d (%s). Retrying in %llds.", cfg->host, cfg->port, err, wait / 1000);
        g_next_connect = plat_now_ms() + wait;
        snprintf(g_conn, sizeof g_conn, "offline");
        return;
    }
    g_retry = 0;
    proto_reader_init(&g_reader);
    snprintf(g_conn, sizeof g_conn, "online");
    plat_log("joining #%s as %s", g_chat.channel, g_chat.nick);
    chat_connected(&g_chat);
    plat_log("join packets sent");
}

int main(int argc, char **argv) {
    app_config cfg;
    plat_options opt;
    chat_io io;
    char path[512];
    unsigned char avatar[AVATAR_BYTES];
    int has_avatar;
    long long last_draw = 0;

    config_defaults(&cfg);
    plat_set_argv0(argc > 0 ? argv[0] : NULL);
    memset(&opt, 0, sizeof opt);
    if (plat_init(&opt) != 0) return 1;
    snprintf(path, sizeof path, "%savatar_chat_ps3.ini", plat_base_dir());
    config_load(&cfg, path);
    opt.overscan_pct = cfg.overscan_pct;
    opt.scale = cfg.scale;
    plat_shutdown();
    if (plat_init(&opt) != 0) return 1;

    io.ctx = NULL;
    io.send = io_send;
    io.now_ms = io_now;
    chat_init(&g_chat, &io);
    g_chat.history_count = cfg.history;
    g_chat.show_pics = cfg.images;
    g_chat.show_cards = cfg.cards;
    snprintf(g_chat.nsfw_keywords, sizeof g_chat.nsfw_keywords, "%s", cfg.nsfw_keywords);
    plat_set_ssl_verify(cfg.ssl_verify);
    chat_set_channel(&g_chat, cfg.channel);
    has_avatar = load_avatar(&cfg, avatar);
    chat_set_identity(&g_chat, cfg.nick, cfg.system, has_avatar ? avatar : NULL);

    ui_init(&g_ui);
    g_ui.compact = cfg.compact;
    chat_notice(&g_chat, "Avatar Chat for PS3. Server %s:%d, room #%s.%s", cfg.host, cfg.port, cfg.channel,
                has_avatar ? "" : " No avatar.bin found, so you have no avatar.");
    if (cfg.nick[0]) {
        g_ui.mode = UI_CHAT;
        g_next_connect = 0;
    } else {
        chat_notice(&g_chat, "Pick a nickname to get started.");
        g_next_connect = -1; /* after the nick */
    }

    while (!g_ui.quit && !plat_quit_requested()) {
        long long now = plat_now_ms();
        int key, dirty = 0;
        char nick[32];

        if (g_sock < 0 && g_next_connect >= 0 && now >= g_next_connect && g_ui.mode == UI_CHAT) {
            try_connect(&cfg);
            dirty = 1;
        }
        if (g_sock >= 0) {
            char buf[4096];
            int got;
            int budget = 32;
            while (budget-- > 0 && (got = plat_net_recv(g_sock, buf, sizeof buf)) != 0) {
                if (got < 0) {
                    plat_net_close(g_sock);
                    g_sock = -1;
                    chat_disconnected(&g_chat, "connection lost");
                    g_next_connect = now + 3000;
                    snprintf(g_conn, sizeof g_conn, "offline");
                    break;
                }
                plat_log("recv %d bytes", got);
                proto_feed(&g_reader, buf, (size_t)got, on_line, NULL);
            }
            chat_tick(&g_chat);
        }
        pump_pictures(now);
        pump_audio();

        nick[0] = 0;
        if (g_ui.osk_open) {
            unsigned char text[UI_INPUT_MAX];
            int n = 0, rc = plat_osk_poll(text, sizeof text, &n);
            if (rc == 1) dirty |= ui_osk_result(&g_ui, &g_chat, text, n, now, nick, sizeof nick);
            else if (rc < 0) { g_ui.osk_open = 0; dirty = 1; }
            else if (!plat_osk_shown() && now - g_osk_opened > 6000) {
                /* The dialog never came up: give up on it for this run and
                 * use the built-in keyboard instead. */
                plat_osk_abort();
                g_ui.osk_open = 0;
                cfg.osk = 2;
                plat_log("system keyboard did not appear in 6 s; using the built-in one");
                ui_set_status(&g_ui, now, 4000, "System keyboard did not appear; using the built-in one.");
                ui_open_pad_keyboard(&g_ui);
            }
            dirty = 1; /* keep flipping frames under the dialog */
        }
        while ((key = plat_poll_key()) != KEY_NONE) {
            if (g_ui.osk_open) continue; /* the dialog has the pad */
            if (!g_ui.kb_open && (key == KEY_OSK || (key == KEY_PAD_ENTER && g_ui.inlen == 0 && g_ui.view_art < 0))) {
                const char *title = g_ui.mode == UI_ASK_NICK ? "Your nickname" : "Say something";
                if (cfg.osk == 1 && plat_osk_open(title, g_ui.input, g_ui.inlen) == 0) {
                    g_ui.osk_open = 1;
                    g_osk_opened = now;
                    ui_set_status(&g_ui, now, 4000, "Opening the system keyboard...");
                } else {
                    if (cfg.osk == 1) { plat_log("system keyboard unavailable; using the built-in one"); cfg.osk = 2; }
                    ui_open_pad_keyboard(&g_ui);
                }
                dirty = 1;
                continue;
            }
            if (key == KEY_PAD_ENTER) key = KEY_ENTER;
            dirty |= ui_key(&g_ui, &g_chat, key, now, nick, sizeof nick);
            if (nick[0]) {
                chat_set_identity(&g_chat, nick, cfg.system, has_avatar ? avatar : NULL);
                snprintf(cfg.nick, sizeof cfg.nick, "%s", nick);
                g_next_connect = 0;
                nick[0] = 0;
            }
        }
        if (g_chat.want_quit) break;

        if (dirty || g_ui.dirty || g_ui.drawn_seq != g_chat.seq || now - last_draw >= 1000) {
            plat_screen_size(&g_screen.cols, &g_screen.rows);
            ui_render(&g_ui, &g_chat, &g_screen, now, g_conn);
            plat_present(&g_screen);
            last_draw = now;
        } else {
            plat_sleep_ms(16);
        }
    }

    audio_stop(NULL);
    if (g_sock >= 0) plat_net_close(g_sock);
    plat_shutdown();
    return 0;
}
