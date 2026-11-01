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
    if (net_send_all(g_sock, bytes, n) < 0) {
        net_close(g_sock);
        g_sock = -1;
        chat_disconnected(&g_chat, "send failed");
    }
}

static long long io_now(void *ctx) { (void)ctx; return plat_now_ms(); }

static void on_line(void *ctx, const char *line, size_t n) {
    (void)ctx;
    chat_feed_line(&g_chat, line, n);
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
    g_sock = net_connect(cfg->host, cfg->port, err, sizeof err);
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
            while (budget-- > 0 && (got = net_recv(g_sock, buf, sizeof buf)) != 0) {
                if (got < 0) {
                    net_close(g_sock);
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

        nick[0] = 0;
        while ((key = plat_poll_key()) != KEY_NONE) {
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

    if (g_sock >= 0) net_close(g_sock);
    plat_shutdown();
    return 0;
}
