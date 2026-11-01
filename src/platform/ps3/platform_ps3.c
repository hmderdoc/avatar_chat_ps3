/* PS3 platform (PSL1GHT): RSX framebuffer with a software-drawn CP437
 * font, USB keyboard through io/kb, pad through io/pad for the few
 * non-text keys, and lv2 BSD sockets. GameOS only: no terminal, so every
 * cell is painted by hand into the display buffer and flipped. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <malloc.h>
#include <errno.h>
#include <sys/time.h>

#include <ppu-lv2.h>
#include <sys/process.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <net/net.h>
#include <net/poll.h>
#include <rsx/rsx.h>
#include <sysutil/video.h>
#include <sysutil/sysutil.h>
#include <sysutil/osk.h>
#include <sys/memory.h>
#include <io/pad.h>
#include <io/kb.h>

#include "../../platform.h"
#include "../../core/cp437.h"
#include "../../core/font_cp437.h"

SYS_PROCESS_PARAM(1001, 0x100000)

#define CB_SIZE   0x100000
#define HOST_SIZE (32 * 1024 * 1024)
#define NBUF 2

typedef struct {
    u32 *ptr;
    u32 offset;
} fb;

static gcmContextData *g_ctx;
static videoResolution g_res;
static fb g_fb[NBUF];
static int g_cur;
static u32 g_pitch;
static int g_scale = 2;
static int g_margin_x, g_margin_y, g_cols, g_rows;
static volatile int g_quit;
static char g_base[256] = "/dev_hdd0/game/AVCH00001/USRDIR/";
static plat_options g_opt;
static FILE *g_log;

void plat_set_argv0(const char *argv0) {
    /* "/dev_hdd0/game/AVCH00001/USRDIR/EBOOT.BIN" -> its directory. */
    const char *slash;
    size_t n;
    if (!argv0 || argv0[0] != '/') return;
    slash = strrchr(argv0, '/');
    n = (size_t)(slash - argv0) + 1;
    if (n >= sizeof g_base) return;
    memcpy(g_base, argv0, n);
    g_base[n] = 0;
}

/* ---- on-screen keyboard ------------------------------------------------ */

#define OSK_MAX 1000
static sys_mem_container_t g_osk_container;
static int g_osk_have_container;
static u16 g_osk_title[64], g_osk_start[OSK_MAX + 1], g_osk_result[OSK_MAX + 1];
static int g_osk_active;       /* dialog up */
static int g_osk_outcome;      /* 0 none yet, 1 text in g_osk_result, -1 cancelled */
static int g_osk_len;

static void osk_finished(void) {
    oskCallbackReturnParam ret;
    memset(&ret, 0, sizeof ret);
    ret.res = OSK_OK;
    ret.len = OSK_MAX;
    ret.str = g_osk_result;
    memset(g_osk_result, 0, sizeof g_osk_result);
    if (oskUnloadAsync(&ret) != 0) { g_osk_outcome = -1; plat_log("oskUnloadAsync failed"); return; }
    if (ret.res == OSK_OK && ret.len > 0) { g_osk_len = ret.len > OSK_MAX ? OSK_MAX : ret.len; g_osk_outcome = 1; }
    else if (ret.res == OSK_NO_TEXT) { g_osk_len = 0; g_osk_outcome = 1; }
    else g_osk_outcome = -1;
    plat_log("osk result res=%u len=%d", (unsigned)ret.res, (int)ret.len);
}

static void sysutil_cb(u64 status, u64 param, void *userdata) {
    (void)param; (void)userdata;
    switch (status) {
    case SYSUTIL_EXIT_GAME: g_quit = 1; break;
    case SYSUTIL_OSK_LOADED: plat_log("osk loaded"); break;
    case SYSUTIL_OSK_DONE: osk_finished(); break;
    case SYSUTIL_OSK_INPUT_CANCELED: plat_log("osk cancelled"); break;
    case SYSUTIL_OSK_UNLOADED: g_osk_active = 0; plat_log("osk unloaded"); break;
    default: break;
    }
}

static void to_utf16(u16 *out, size_t cap, const unsigned char *cp437, int n, const char *ascii) {
    size_t o = 0;
    int i;
    if (ascii) for (i = 0; ascii[i] && o + 1 < cap; i++) out[o++] = (u16)(unsigned char)ascii[i];
    else for (i = 0; i < n && o + 1 < cap; i++) out[o++] = (u16)cp437_to_unicode[cp437[i]];
    out[o] = 0;
}

int plat_osk_open(const char *title, const unsigned char *text, int n) {
    oskParam param;
    oskInputFieldInfo info;
    s32 rc;
    if (g_osk_active) return 0;
    if (!g_osk_have_container) {
        if (sysMemContainerCreate(&g_osk_container, 7 * 1024 * 1024) != 0) { plat_log("osk: no memory container"); return -1; }
        g_osk_have_container = 1;
    }
    to_utf16(g_osk_title, sizeof g_osk_title / sizeof g_osk_title[0], NULL, 0, title);
    to_utf16(g_osk_start, sizeof g_osk_start / sizeof g_osk_start[0], text, n, NULL);
    memset(&param, 0, sizeof param);
    param.allowedPanels = OSK_PANEL_TYPE_DEFAULT;
    param.firstViewPanel = OSK_PANEL_TYPE_DEFAULT;
    param.controlPoint.x = 0;
    param.controlPoint.y = 0;
    param.prohibitFlags = OSK_PROHIBIT_RETURN;
    memset(&info, 0, sizeof info);
    info.message = g_osk_title;
    info.startText = g_osk_start;
    info.maxLength = OSK_MAX;
    oskSetInitialInputDevice(OSK_DEVICE_PAD);
    oskSetKeyLayoutOption(OSK_10KEY_PANEL | OSK_FULLKEY_PANEL);
    oskSetLayoutMode(OSK_LAYOUTMODE_HORIZONTAL_ALIGN_CENTER | OSK_LAYOUTMODE_VERTICAL_ALIGN_BOTTOM);
    g_osk_outcome = 0;
    g_osk_len = 0;
    rc = oskLoadAsync(g_osk_container, &param, &info);
    if (rc != 0) { plat_log("oskLoadAsync failed: %d", (int)rc); return -1; }
    g_osk_active = 1;
    return 0;
}

int plat_osk_poll(unsigned char *out, int cap, int *n) {
    int i, o = 0;
    *n = 0;
    sysUtilCheckCallback();
    if (g_osk_outcome == 0) return g_osk_active ? 0 : -1;
    if (g_osk_outcome < 0) { g_osk_outcome = 0; return -1; }
    for (i = 0; i < g_osk_len && o < cap; i++) {
        unsigned long cp = g_osk_result[i];
        unsigned char b;
        if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < g_osk_len) { cp = 0x10000 + ((cp - 0xD800) << 10) + (g_osk_result[i + 1] - 0xDC00); i++; }
        b = cp437_from_unicode(cp);
        if (b) out[o++] = b;
    }
    *n = o;
    g_osk_outcome = 0;
    return 1;
}

int plat_osk_active(void) { return g_osk_active; }

static void wait_flip(void) {
    while (gcmGetFlipStatus() != 0) usleep(200);
    gcmResetFlipStatus();
}

static void flip(int buffer) {
    gcmSetFlip(g_ctx, buffer);
    rsxFlushBuffer(g_ctx);
    gcmSetWaitFlip(g_ctx);
}

static int init_video(void) {
    videoState state;
    videoConfiguration vconfig;
    void *host_addr = memalign(1024 * 1024, HOST_SIZE);
    int i;
    if (!host_addr) return -1;
    g_ctx = rsxInit(CB_SIZE, HOST_SIZE, host_addr);
    if (!g_ctx) return -1;
    if (videoGetState(0, 0, &state) != 0 || state.state != 0) return -1;
    if (videoGetResolution(state.displayMode.resolution, &g_res) != 0) return -1;
    memset(&vconfig, 0, sizeof vconfig);
    vconfig.resolution = state.displayMode.resolution;
    vconfig.format = VIDEO_BUFFER_FORMAT_XRGB;
    vconfig.pitch = g_res.width * sizeof(u32);
    vconfig.aspect = state.displayMode.aspect;
    wait_flip();
    if (videoConfigure(0, &vconfig, NULL, 0) != 0) return -1;
    if (videoGetState(0, 0, &state) != 0) return -1;
    gcmSetFlipMode(GCM_FLIP_VSYNC);
    g_pitch = g_res.width * sizeof(u32);
    for (i = 0; i < NBUF; i++) {
        g_fb[i].ptr = (u32 *)rsxMemalign(64, g_pitch * g_res.height);
        if (!g_fb[i].ptr) return -1;
        if (rsxAddressToOffset(g_fb[i].ptr, &g_fb[i].offset) != 0) return -1;
        if (gcmSetDisplayBuffer(i, g_fb[i].offset, g_pitch, g_res.width, g_res.height) != 0) return -1;
        memset(g_fb[i].ptr, 0, g_pitch * g_res.height);
    }
    gcmResetFlipStatus();
    flip(1);
    g_cur = 0;
    return 0;
}

static void compute_grid(void) {
    int usable_w = g_res.width - 2 * (g_res.width * g_opt.overscan_pct / 100);
    int usable_h = g_res.height - 2 * (g_res.height * g_opt.overscan_pct / 100);
    if (g_opt.scale > 0) g_scale = g_opt.scale;
    else {
        /* About 80 columns on any TV: 480p -> 1x, 720p -> 2x, 1080p -> 3x. */
        g_scale = (g_res.height + 200) / 400;
        if (g_scale < 1) g_scale = 1;
        while (g_scale > 1 && usable_w / (FONT_W * g_scale) < 64) g_scale--;
    }
    if (g_scale > 4) g_scale = 4;
    g_cols = usable_w / (FONT_W * g_scale);
    g_rows = usable_h / (FONT_H * g_scale);
    if (g_cols > UI_MAX_COLS) g_cols = UI_MAX_COLS;
    if (g_rows > UI_MAX_ROWS) g_rows = UI_MAX_ROWS;
    g_margin_x = (g_res.width - g_cols * FONT_W * g_scale) / 2;
    g_margin_y = (g_res.height - g_rows * FONT_H * g_scale) / 2;
}

int plat_init(const plat_options *opt) {
    static int once;
    padInfo padinfo;
    if (opt) g_opt = *opt;
    if (once) { compute_grid(); return 0; }
    once = 1;
    if (!g_log) g_log = fopen("/dev_hdd0/tmp/avatar_chat_ps3.log", "a");
    plat_log("--- avatar_chat_ps3 starting, base %s", g_base);
    sysUtilRegisterCallback(SYSUTIL_EVENT_SLOT0, sysutil_cb, NULL);
    if (init_video() != 0) { plat_log("video init failed"); return -1; }
    compute_grid();
    plat_log("video %ux%u, scale %d, %dx%d cells", (unsigned)g_res.width, (unsigned)g_res.height, g_scale, g_cols, g_rows);
    ioPadInit(7);
    ioKbInit(MAX_KEYBOARDS);
    memset(&padinfo, 0, sizeof padinfo);
    ioPadGetInfo(&padinfo);
    netInitialize();
    return 0;
}

void plat_shutdown(void) {
    /* Nothing to undo between the two init passes; the real teardown is
     * at exit. */
}

static void real_shutdown(void) {
    ioKbEnd();
    ioPadEnd();
    netDeinitialize();
    if (g_ctx) { gcmSetWaitFlip(g_ctx); rsxFinish(g_ctx, 1); }
}

const char *plat_base_dir(void) { return g_base; }

void plat_screen_size(int *cols, int *rows) {
    *cols = g_cols;
    *rows = g_rows;
}

static void draw_cell(u32 *buf, int cx, int cy, unsigned char ch, u32 fg, u32 bg, int cursor) {
    const unsigned char *glyph = font_cp437 + ch * FONT_H;
    int px0 = g_margin_x + cx * FONT_W * g_scale;
    int py0 = g_margin_y + cy * FONT_H * g_scale;
    int row, col, sy, sx;
    u32 stride = g_pitch / 4;
    if (cursor) { u32 t = fg; fg = bg; bg = t; }
    for (row = 0; row < FONT_H; row++) {
        unsigned bits = glyph[row];
        for (sy = 0; sy < g_scale; sy++) {
            u32 *line = buf + (u32)(py0 + row * g_scale + sy) * stride + px0;
            for (col = 0; col < FONT_W; col++) {
                u32 c = (bits & (0x80 >> col)) ? fg : bg;
                for (sx = 0; sx < g_scale; sx++) *line++ = c;
            }
        }
    }
}

void plat_present(const screen *s) {
    u32 *buf = g_fb[g_cur].ptr;
    int x, y;
    int blink_on = (plat_now_ms() / 500) & 1;
    wait_flip();
    for (y = 0; y < s->rows && y < g_rows; y++)
        for (x = 0; x < s->cols && x < g_cols; x++) {
            const cell *c = &s->cells[y * s->cols + x];
            draw_cell(buf, x, y, c->ch, c->fg, c->bg, blink_on && x == s->cur_x && y == s->cur_y);
        }
    flip(g_cur);
    g_cur = (g_cur + 1) % NBUF;
}

/* ---- input --------------------------------------------------------------- */

static int kb_ready[MAX_KEYBOARDS];
static int queue[64];
static int qhead, qlen;

static void push_key(int k) {
    if (qlen >= 64 || k == KEY_NONE) return;
    queue[(qhead + qlen) % 64] = k;
    qlen++;
}

static int pop_key(void) {
    int k;
    if (qlen == 0) return KEY_NONE;
    k = queue[qhead];
    qhead = (qhead + 1) % 64;
    qlen--;
    return k;
}

static int raw_to_key(u16 code) {
    switch (code & 0xFF) {
    case KB_RAWKEY_ENTER: case KB_RAWKEY_KPAD_ENTER: return KEY_ENTER;
    case KB_RAWKEY_BS: return KEY_BACKSPACE;
    case KB_RAWKEY_DELETE: return KEY_DELETE;
    case KB_RAWKEY_ESCAPE: return KEY_ESC;
    case KB_RAWKEY_TAB: return KEY_TAB;
    case KB_RAWKEY_UP_ARROW: return KEY_UP;
    case KB_RAWKEY_DOWN_ARROW: return KEY_DOWN;
    case KB_RAWKEY_LEFT_ARROW: return KEY_LEFT;
    case KB_RAWKEY_RIGHT_ARROW: return KEY_RIGHT;
    case KB_RAWKEY_PAGE_UP: return KEY_PGUP;
    case KB_RAWKEY_PAGE_DOWN: return KEY_PGDN;
    case KB_RAWKEY_HOME: return KEY_HOME;
    case KB_RAWKEY_END: return KEY_END;
    case KB_RAWKEY_F1: return KEY_F1;
    case KB_RAWKEY_F2: return KEY_F2;
    case KB_RAWKEY_F3: return KEY_F3;
    default: return KEY_NONE;
    }
}

static void poll_keyboard(void) {
    KbInfo info;
    KbData data;
    int i, k;
    if (ioKbGetInfo(&info) != 0) return;
    for (i = 0; i < MAX_KEYBOARDS; i++) {
        if (!info.status[i]) { kb_ready[i] = 0; continue; }
        if (!kb_ready[i]) {
            /* A keyboard just appeared: ask for ASCII with the raw codes
             * for the keys that have none, one event per press. */
            ioKbSetCodeType(i, KB_CODETYPE_ASCII);
            ioKbSetReadMode(i, KB_RMODE_INPUTCHAR);
            kb_ready[i] = 1;
        }
        if (ioKbRead(i, &data) != 0 || data.nb_keycode <= 0) continue;
        for (k = 0; k < data.nb_keycode; k++) {
            u16 code = data.keycode[k];
            if (code == 0) continue;
            if (code & KB_RAWDAT) { push_key(raw_to_key(code)); continue; }
            switch (code) {
            case 0x0A: case 0x0D: push_key(KEY_ENTER); break;
            case 0x08: case 0x7F: push_key(KEY_BACKSPACE); break;
            case 0x1B: push_key(KEY_ESC); break;
            case 0x09: push_key(KEY_TAB); break;
            default:
                if (code >= 0x20 && code < 0x7F) push_key(code);
                else if (code >= 0x80 && code <= 0xFF) {
                    /* Latin-1 from the keyboard; map through Unicode to CP437. */
                    char u[2]; unsigned char out[2];
                    u[0] = (char)(0xC0 | (code >> 6)); u[1] = (char)(0x80 | (code & 0x3F));
                    if (cp437_from_utf8(u, 2, out, 1) == 1) push_key(out[0]);
                }
                break;
            }
        }
    }
}

static void poll_pad(void) {
    static u32 prev;
    padInfo info;
    padData pad;
    int i;
    u32 now = 0;
    if (ioPadGetInfo(&info) != 0) return;
    for (i = 0; i < MAX_PADS; i++) {
        if (!info.status[i]) continue;
        if (ioPadGetData(i, &pad) != 0) continue;
        if (pad.BTN_START) now |= 1;
        if (pad.BTN_SELECT) now |= 2;
        if (pad.BTN_UP) now |= 4;
        if (pad.BTN_DOWN) now |= 8;
        if (pad.BTN_L1) now |= 16;
        if (pad.BTN_R1) now |= 32;
        if (pad.BTN_CIRCLE) now |= 64;
        if (pad.BTN_CROSS) now |= 128;
        if (pad.BTN_TRIANGLE) now |= 256;
        if (pad.BTN_SQUARE) now |= 512;
        if (pad.BTN_LEFT) now |= 1024;
        if (pad.BTN_RIGHT) now |= 2048;
    }
    {
        u32 pressed = now & ~prev;
        prev = now;
        if (pressed & 1) push_key(KEY_QUIT);
        if (pressed & 2) push_key(KEY_TOGGLE_COMPACT);
        if (pressed & 4) push_key(KEY_UP);
        if (pressed & 8) push_key(KEY_DOWN);
        if (pressed & 16) push_key(KEY_PGUP);
        if (pressed & 32) push_key(KEY_PGDN);
        if (pressed & 64) push_key(KEY_ESC);
        if (pressed & 128) push_key(KEY_PAD_ENTER);
        if (pressed & 256) push_key(KEY_OSK);
        if (pressed & 512) push_key(KEY_VIEW_ART);
        if (pressed & 1024) push_key(KEY_ROOM_PREV);
        if (pressed & 2048) push_key(KEY_ROOM_NEXT);
    }
}

int plat_poll_key(void) {
    sysUtilCheckCallback();
    if (qlen == 0) {
        poll_keyboard();
        poll_pad();
    }
    return pop_key();
}

int plat_quit_requested(void) {
    sysUtilCheckCallback();
    return g_quit;
}

long long plat_now_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (long long)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

void plat_sleep_ms(int ms) { usleep((useconds_t)ms * 1000); }

/* ---- sockets ------------------------------------------------------------- */

int net_connect(const char *host, int port, char *err, size_t errcap) {
    struct sockaddr_in sa;
    int fd;
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons((u16)port);
    if (inet_aton(host, &sa.sin_addr) == 0) {
        struct hostent *he = gethostbyname(host);
        if (!he || !he->h_addr_list || !he->h_addr_list[0]) {
            snprintf(err, errcap, "can't resolve host");
            return -1;
        }
        memcpy(&sa.sin_addr, he->h_addr_list[0], sizeof sa.sin_addr);
    }
    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { snprintf(err, errcap, "socket: %d", errno); plat_log("socket failed: %d errno %d", fd, errno); return -1; }
    plat_log("connecting fd=%d to %s:%d", fd, inet_ntoa(sa.sin_addr), port);
    if (connect(fd, (struct sockaddr *)&sa, sizeof sa) < 0) {
        snprintf(err, errcap, "connect: %d", errno);
        plat_log("connect failed: errno %d", errno);
        close(fd);
        return -1;
    }
    plat_log("connected fd=%d", fd);
    return fd;
}

/* Readiness through poll(), not select(): lv2's select takes a 128-byte
 * sys_net_fd_set, and a smaller C-library fd_set on the stack gets
 * overwritten when the kernel writes the result back. pollfd is the same
 * 8 bytes everywhere. */
int net_recv(int fd, char *buf, size_t cap) {
    struct pollfd p;
    int rc, got;
    p.fd = fd;
    p.events = POLLIN;
    p.revents = 0;
    rc = poll(&p, 1, 0);
    if (rc < 0) { plat_log("poll(%d) failed: %d errno %d", fd, rc, errno); return -1; }
    if (rc == 0 || !(p.revents & (POLLIN | POLLHUP | POLLERR | POLLNVAL))) return 0;
    got = recv(fd, buf, cap, 0);
    if (got == 0) { plat_log("recv(%d): peer closed", fd); return -1; }
    if (got < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
        plat_log("recv(%d) failed: %d errno %d", fd, got, errno);
        return -1;
    }
    return got;
}

int net_send_all(int fd, const char *buf, size_t n) {
    while (n > 0) {
        int w = send(fd, buf, n, 0);
        if (w < 0) { plat_log("send(%d, %u bytes) failed: %d errno %d", fd, (unsigned)n, w, errno); return -1; }
        if (w == 0) { plat_log("send(%d) wrote nothing", fd); return -1; }
        buf += w;
        n -= (size_t)w;
    }
    return 0;
}

void net_close(int fd) { close(fd); }

void plat_log(const char *fmt, ...) {
    va_list ap;
    if (!g_log) return;
    va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fputc('\n', g_log);
    fflush(g_log);
}

/* main() is in src/main.c; it calls plat_shutdown, which keeps the RSX up
 * for a second init pass. Tear everything down when the process ends. */
__attribute__((destructor)) static void at_exit_teardown(void) { real_shutdown(); }
