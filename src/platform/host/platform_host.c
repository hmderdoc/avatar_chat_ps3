/* Host platform: a POSIX terminal and BSD sockets. For developing and
 * testing the client without a PS3; it also works as a plain Linux/macOS
 * client. The terminal must show UTF-8. */
#define _GNU_SOURCE
#include "../../platform.h"
#include "../../core/cp437.h"
#include "../../core/png.h"
#include "../../core/gif.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <termios.h>
#include <time.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include "../../core/pics.h"

static struct termios g_saved;
static int g_raw;
static volatile sig_atomic_t g_quit;
static char g_base[512] = "./";
static FILE *g_log;

static void on_signal(int sig) { (void)sig; g_quit = 1; }

void plat_set_argv0(const char *argv0) { (void)argv0; }

int plat_init(const plat_options *opt) {
    struct termios t;
    const char *env;
    (void)opt;
    env = getenv("AVATAR_CHAT_DIR");
    if (env && *env) snprintf(g_base, sizeof g_base, "%s%s", env, env[strlen(env) - 1] == '/' ? "" : "/");
    if (!g_log && getenv("AVATAR_CHAT_LOG")) g_log = fopen(getenv("AVATAR_CHAT_LOG"), "a");
    if (g_raw) return 0;
    if (!isatty(STDIN_FILENO)) {
        /* Allowed (tests drive us through a pty normally), but no raw mode. */
    } else {
        tcgetattr(STDIN_FILENO, &g_saved);
        t = g_saved;
        t.c_lflag &= ~(ICANON | ECHO | ISIG | IEXTEN);
        t.c_iflag &= ~(IXON | ICRNL | INLCR);
        t.c_oflag &= ~(OPOST);
        t.c_cc[VMIN] = 0;
        t.c_cc[VTIME] = 0;
        tcsetattr(STDIN_FILENO, TCSANOW, &t);
    }
    g_raw = 1;
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    signal(SIGPIPE, SIG_IGN);
    fputs("\x1b[?1049h\x1b[?25l\x1b[2J", stdout);
    fflush(stdout);
    return 0;
}

void plat_shutdown(void) {
    if (!g_raw) return;
    fputs("\x1b[0m\x1b[?25h\x1b[?1049l", stdout);
    fflush(stdout);
    if (isatty(STDIN_FILENO)) tcsetattr(STDIN_FILENO, TCSANOW, &g_saved);
    g_raw = 0;
}

const char *plat_base_dir(void) { return g_base; }

void plat_screen_size(int *cols, int *rows) {
    struct winsize ws;
    *cols = 80; *rows = 25;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0 && ws.ws_row > 0) {
        *cols = ws.ws_col;
        *rows = ws.ws_row;
    }
    if (*cols > UI_MAX_COLS) *cols = UI_MAX_COLS;
    if (*rows > UI_MAX_ROWS) *rows = UI_MAX_ROWS;
    if (*cols < 40) *cols = 40;
    if (*rows < 10) *rows = 10;
}

void plat_present(const screen *s) {
    static char out[UI_MAX_COLS * UI_MAX_ROWS * 48];
    size_t n = 0;
    int y, x;
    unsigned int last_fg = 0xFFFFFFFFu, last_bg = 0xFFFFFFFFu;
    n += (size_t)snprintf(out + n, sizeof out - n, "\x1b[H");
    for (y = 0; y < s->rows; y++) {
        if (y) n += (size_t)snprintf(out + n, sizeof out - n, "\r\n");
        for (x = 0; x < s->cols; x++) {
            const cell *c = &s->cells[y * s->cols + x];
            char u[4];
            size_t k;
            if (c->fg != last_fg || c->bg != last_bg) {
                n += (size_t)snprintf(out + n, sizeof out - n, "\x1b[38;2;%u;%u;%u;48;2;%u;%u;%um",
                                      (c->fg >> 16) & 255, (c->fg >> 8) & 255, c->fg & 255,
                                      (c->bg >> 16) & 255, (c->bg >> 8) & 255, c->bg & 255);
                last_fg = c->fg;
                last_bg = c->bg;
            }
            k = cp437_byte_to_utf8(c->ch, u);
            if (n + k + 64 > sizeof out) break;
            memcpy(out + n, u, k);
            n += k;
        }
    }
    if (s->cur_x >= 0 && s->cur_y >= 0)
        n += (size_t)snprintf(out + n, sizeof out - n, "\x1b[%d;%dH\x1b[?25h", s->cur_y + 1, s->cur_x + 1);
    else
        n += (size_t)snprintf(out + n, sizeof out - n, "\x1b[?25l");
    fwrite(out, 1, n, stdout);
    fflush(stdout);
}

/* ---- keys: a small escape-sequence decoder ----------------------------- */

static unsigned char kbuf[64];
static int klen;

static int fill_keys(int wait_ms) {
    fd_set r;
    struct timeval tv;
    int got;
    if (klen >= (int)sizeof kbuf) return 0;
    FD_ZERO(&r);
    FD_SET(STDIN_FILENO, &r);
    tv.tv_sec = 0;
    tv.tv_usec = wait_ms * 1000;
    if (select(STDIN_FILENO + 1, &r, NULL, NULL, &tv) <= 0) return 0;
    got = (int)read(STDIN_FILENO, kbuf + klen, sizeof kbuf - (size_t)klen);
    if (got <= 0) {
        if (got == 0) g_quit = 1; /* stdin closed */
        return 0;
    }
    klen += got;
    return got;
}

static void consume(int n) {
    memmove(kbuf, kbuf + n, (size_t)(klen - n));
    klen -= n;
}

/* Decode one UTF-8 character from the front of kbuf into a CP437 key. */
static int take_utf8(void) {
    unsigned char c = kbuf[0];
    int need = c < 0x80 ? 1 : (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : (c & 0xF8) == 0xF0 ? 4 : 1;
    unsigned char out[4];
    size_t n;
    if (klen < need) fill_keys(20);
    if (klen < need) need = klen;
    n = cp437_from_utf8((const char *)kbuf, (size_t)need, out, sizeof out);
    consume(need);
    return n ? out[0] : KEY_NONE;
}

int plat_poll_key(void) {
    if (klen == 0 && fill_keys(0) == 0) return KEY_NONE;
    if (klen == 0) return KEY_NONE;
    {
        unsigned char c = kbuf[0];
        if (c == 0x1b) {
            if (klen == 1) fill_keys(30);
            if (klen == 1) { consume(1); return KEY_ESC; }
            if (kbuf[1] == '[' || kbuf[1] == 'O') {
                int i = 2, num = 0;
                while (i < klen && kbuf[i] >= '0' && kbuf[i] <= '9') num = num * 10 + (kbuf[i++] - '0');
                if (i < klen && kbuf[i] == ';') { i++; while (i < klen && kbuf[i] >= '0' && kbuf[i] <= '9') i++; }
                if (i >= klen) { fill_keys(30); if (i >= klen) { consume(klen); return KEY_NONE; } }
                {
                    unsigned char f = kbuf[i];
                    consume(i + 1);
                    switch (f) {
                    case 'A': return KEY_UP;
                    case 'B': return KEY_DOWN;
                    case 'C': return KEY_RIGHT;
                    case 'D': return KEY_LEFT;
                    case 'H': return KEY_HOME;
                    case 'F': return KEY_END;
                    case 'P': return KEY_F1;
                    case 'Q': return KEY_F2;
                    case 'R': return KEY_F3;
                    case '~':
                        switch (num) {
                        case 1: case 7: return KEY_HOME;
                        case 4: case 8: return KEY_END;
                        case 3: return KEY_DELETE;
                        case 5: return KEY_PGUP;
                        case 6: return KEY_PGDN;
                        case 11: return KEY_F1;
                        case 12: return KEY_F2;
                        case 13: return KEY_F3;
                        default: return KEY_NONE;
                        }
                    default: return KEY_NONE;
                    }
                }
            }
            consume(1);
            return KEY_ESC;
        }
        if (c == '\r' || c == '\n') { consume(1); return KEY_ENTER; }
        if (c == 0x7f || c == 0x08) { consume(1); return KEY_BACKSPACE; }
        if (c == '\t') { consume(1); return KEY_TAB; }
        if (c == 0x03) { consume(1); return KEY_QUIT; }
        if (c == 0x0c) { consume(1); return KEY_TOGGLE_COMPACT; } /* Ctrl-L */
        if (c == 0x0e) { consume(1); return KEY_ROOM_NEXT; }      /* Ctrl-N */
        if (c == 0x10) { consume(1); return KEY_ROOM_PREV; }      /* Ctrl-P */
        if (c < 0x20) { consume(1); return KEY_NONE; }
        return take_utf8();
    }
}

int plat_quit_requested(void) { return g_quit; }

/* A terminal has a keyboard; there is no on-screen one here. */
int plat_osk_open(const char *title, const unsigned char *text, int n) { (void)title; (void)text; (void)n; return -1; }
int plat_osk_poll(unsigned char *out, int cap, int *n) { (void)out; (void)cap; *n = 0; return -1; }
int plat_osk_active(void) { return 0; }
int plat_osk_shown(void) { return 0; }
void plat_osk_abort(void) {}

long long plat_now_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (long long)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

void plat_sleep_ms(int ms) {
    /* Sleep on stdin so a key wakes us early. */
    fd_set r;
    struct timeval tv;
    FD_ZERO(&r);
    FD_SET(STDIN_FILENO, &r);
    tv.tv_sec = 0;
    tv.tv_usec = ms * 1000;
    select(STDIN_FILENO + 1, &r, NULL, NULL, &tv);
}

/* ---- sockets ----------------------------------------------------------- */

int plat_net_connect(const char *host, int port, char *err, size_t errcap) {
    struct addrinfo hints, *res = NULL, *ai;
    char portstr[16];
    int fd = -1, rc;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    snprintf(portstr, sizeof portstr, "%d", port);
    rc = getaddrinfo(host, portstr, &hints, &res);
    if (rc != 0) { snprintf(err, errcap, "%s", gai_strerror(rc)); return -1; }
    for (ai = res; ai; ai = ai->ai_next) {
        struct timeval tv;
        fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0) continue;
        tv.tv_sec = 10; tv.tv_usec = 0;
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
        if (connect(fd, ai->ai_addr, ai->ai_addrlen) == 0) break;
        snprintf(err, errcap, "%s", strerror(errno));
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    if (fd < 0) { if (!*err) snprintf(err, errcap, "no address"); return -1; }
    {
        int one = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
    }
    return fd;
}

int plat_net_recv(int fd, char *buf, size_t cap) {
    fd_set r;
    struct timeval tv;
    ssize_t got;
    FD_ZERO(&r);
    FD_SET(fd, &r);
    tv.tv_sec = 0; tv.tv_usec = 0;
    if (select(fd + 1, &r, NULL, NULL, &tv) <= 0) return 0;
    got = recv(fd, buf, cap, 0);
    if (got == 0) return -1;
    if (got < 0) return (errno == EAGAIN || errno == EINTR) ? 0 : -1;
    return (int)got;
}

int plat_net_send_all(int fd, const char *buf, size_t n) {
    while (n > 0) {
        ssize_t w = send(fd, buf, n, 0);
        if (w < 0) { if (errno == EINTR) continue; return -1; }
        buf += w;
        n -= (size_t)w;
    }
    return 0;
}

void plat_net_close(int fd) { close(fd); }

/* ---- pictures: a small HTTP/1.1 client, plain http only on the host ------ */

static struct {
    int fd;
    int active;
    unsigned char *buf;
    size_t len, cap, max;
    int header_done;
    size_t body_off;
    long content_len;    /* -1 unknown */
    int chunked;
    int redirects;
    char url[PIC_URL];
} g_fetch;

static int parse_url(const char *url, char *host, size_t hcap, int *port, char *path, size_t pcap) {
    const char *p, *slash, *colon;
    size_t hl;
    if (strncasecmp(url, "http://", 7) != 0) return -1;
    p = url + 7;
    slash = strchr(p, '/');
    hl = slash ? (size_t)(slash - p) : strlen(p);
    if (hl == 0 || hl >= hcap) return -1;
    memcpy(host, p, hl);
    host[hl] = 0;
    *port = 80;
    colon = strchr(host, ':');
    if (colon) { *port = atoi(colon + 1); host[colon - host] = 0; }
    snprintf(path, pcap, "%s", slash ? slash : "/");
    return 0;
}

static int fetch_begin(const char *url, char *err, size_t errcap) {
    char host[256], path[1024], req[1600];
    int port;
    if (parse_url(url, host, sizeof host, &port, path, sizeof path) != 0) { snprintf(err, errcap, "only http:// here"); return -1; }
    g_fetch.fd = plat_net_connect(host, port, err, errcap);
    if (g_fetch.fd < 0) return -1;
    snprintf(req, sizeof req, "GET %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: avatar_chat_ps3/0.1\r\nAccept: image/*\r\nConnection: close\r\n\r\n", path, host);
    if (plat_net_send_all(g_fetch.fd, req, strlen(req)) != 0) { plat_net_close(g_fetch.fd); g_fetch.fd = -1; snprintf(err, errcap, "send failed"); return -1; }
    g_fetch.len = 0;
    g_fetch.header_done = 0;
    g_fetch.content_len = -1;
    g_fetch.chunked = 0;
    snprintf(g_fetch.url, sizeof g_fetch.url, "%s", url);
    return 0;
}

int plat_fetch_start(const char *url, size_t max_bytes, char *err, size_t errcap) {
    if (g_fetch.active) plat_fetch_cancel();
    if (!g_fetch.buf || g_fetch.cap < max_bytes + 65536) {
        free(g_fetch.buf);
        g_fetch.cap = max_bytes + 65536;
        g_fetch.buf = malloc(g_fetch.cap);
        if (!g_fetch.buf) { snprintf(err, errcap, "no memory"); return -1; }
    }
    g_fetch.max = max_bytes;
    g_fetch.redirects = 0;
    if (fetch_begin(url, err, errcap) != 0) return -1;
    g_fetch.active = 1;
    return 0;
}

void plat_fetch_cancel(void) {
    if (g_fetch.fd >= 0) plat_net_close(g_fetch.fd);
    g_fetch.fd = -1;
    g_fetch.active = 0;
}

/* Undo chunked transfer coding in place; returns the body length or -1. */
static long dechunk(unsigned char *b, size_t n) {
    size_t r = 0, w = 0;
    while (r < n) {
        char *end;
        unsigned long len = strtoul((char *)b + r, &end, 16);
        unsigned char *nl = memchr(b + r, '\n', n - r);
        if (!nl) return -1;
        r = (size_t)(nl - b) + 1;
        if (len == 0) return (long)w;
        if (r + len > n) return -1;
        memmove(b + w, b + r, len);
        w += len;
        r += len;
        while (r < n && (b[r] == '\r' || b[r] == '\n')) r++;
        (void)end;
    }
    return -1;
}

static int fetch_finish(const unsigned char **data, size_t *n, char *err, size_t errcap) {
    char *hdr = (char *)g_fetch.buf;
    int status;
    char *line_end = strstr(hdr, "\r\n");
    plat_fetch_cancel();
    if (!g_fetch.header_done || !line_end) { snprintf(err, errcap, "bad reply"); return -1; }
    status = atoi(hdr + 9);
    if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308) {
        char *loc = strcasestr(hdr, "\nLocation:");
        if (loc && g_fetch.redirects < 3) {
            char next[PIC_URL];
            size_t k = 0;
            loc += 10;
            while (*loc == ' ') loc++;
            while (*loc && *loc != '\r' && *loc != '\n' && k + 1 < sizeof next) next[k++] = *loc++;
            next[k] = 0;
            g_fetch.redirects++;
            if (fetch_begin(next, err, errcap) != 0) return -1;
            g_fetch.active = 1;
            return 0;
        }
        snprintf(err, errcap, "too many redirects");
        return -1;
    }
    if (status != 200) { snprintf(err, errcap, "HTTP %d", status); return -1; }
    {
        unsigned char *body = g_fetch.buf + g_fetch.body_off;
        size_t blen = g_fetch.len - g_fetch.body_off;
        if (g_fetch.chunked) {
            long l = dechunk(body, blen);
            if (l < 0) { snprintf(err, errcap, "bad chunking"); return -1; }
            blen = (size_t)l;
        }
        *data = body;
        *n = blen;
    }
    return 1;
}

int plat_fetch_poll(const unsigned char **data, size_t *n, char *err, size_t errcap) {
    int got;
    if (!g_fetch.active) { snprintf(err, errcap, "no download"); return -1; }
    got = plat_net_recv(g_fetch.fd, (char *)g_fetch.buf + g_fetch.len, g_fetch.cap - g_fetch.len - 1);
    if (got > 0) {
        g_fetch.len += (size_t)got;
        g_fetch.buf[g_fetch.len] = 0;
        if (!g_fetch.header_done) {
            char *end = strstr((char *)g_fetch.buf, "\r\n\r\n");
            if (end) {
                char *cl, *te;
                *end = 0;
                g_fetch.header_done = 1;
                g_fetch.body_off = (size_t)(end + 4 - (char *)g_fetch.buf);
                cl = strcasestr((char *)g_fetch.buf, "\nContent-Length:");
                if (cl) g_fetch.content_len = atol(cl + 16);
                te = strcasestr((char *)g_fetch.buf, "\nTransfer-Encoding:");
                if (te && strcasestr(te, "chunked")) g_fetch.chunked = 1;
                if (g_fetch.content_len > (long)g_fetch.max) { plat_fetch_cancel(); snprintf(err, errcap, "too large"); return -1; }
            }
        }
        if (g_fetch.len - g_fetch.body_off > g_fetch.max) { plat_fetch_cancel(); snprintf(err, errcap, "too large"); return -1; }
        if (g_fetch.header_done && g_fetch.content_len >= 0 && (long)(g_fetch.len - g_fetch.body_off) >= g_fetch.content_len)
            return fetch_finish(data, n, err, errcap);
        return 0;
    }
    if (got == 0) return 0;
    /* Closed: the reply is complete (Connection: close). */
    return fetch_finish(data, n, err, errcap);
}

void plat_set_ssl_verify(int on) { (void)on; }

static int decode_gif(const unsigned char *data, size_t n, unsigned char **rgb, int *w, int *h, char *err, size_t errcap) {
    unsigned char *scratch = malloc(gif_scratch());
    if ((long long)*w * *h > 16000000LL) { free(scratch); snprintf(err, errcap, "image too large"); return 0; }
    *rgb = malloc((size_t)*w * *h * 3);
    if (!scratch || !*rgb) { free(scratch); free(*rgb); *rgb = NULL; snprintf(err, errcap, "no memory"); return 0; }
    if (!gif_decode(data, n, *rgb, *w, *h, scratch, gif_scratch(), err, errcap)) { free(scratch); free(*rgb); *rgb = NULL; return 0; }
    free(scratch);
    return 1;
}

int plat_decode_image(const unsigned char *data, size_t n, unsigned char **rgb, int *w, int *h, char *err, size_t errcap) {
    unsigned char *scratch;
    size_t sn;
    if (gif_size(data, n, w, h)) return decode_gif(data, n, rgb, w, h, err, errcap);
    if (!png_size(data, n, w, h)) { snprintf(err, errcap, "only PNG and GIF on the host build"); return 0; }
    if ((long long)*w * *h > 16000000LL) { snprintf(err, errcap, "image too large"); return 0; }
    sn = png_scratch(*w, *h);
    scratch = malloc(sn);
    *rgb = malloc((size_t)*w * *h * 3);
    if (!scratch || !*rgb) { free(scratch); free(*rgb); *rgb = NULL; snprintf(err, errcap, "no memory"); return 0; }
    if (!png_decode(data, n, *rgb, *w, *h, scratch, sn, err, errcap)) { free(scratch); free(*rgb); *rgb = NULL; return 0; }
    free(scratch);
    return 1;
}

void plat_log(const char *fmt, ...) {
    va_list ap;
    if (!g_log) return;
    va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fputc('\n', g_log);
    fflush(g_log);
}
