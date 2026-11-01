/* What a platform gives the portable core. Two implementations: PS3
 * (PSL1GHT: RSX framebuffer, USB keyboard, pad, lv2 sockets) and host
 * (POSIX terminal and sockets) for development and testing. */
#ifndef AC_PLATFORM_H
#define AC_PLATFORM_H

#include <stddef.h>
#include "core/ui.h"

typedef struct {
    int overscan_pct;   /* PS3: margin kept clear on each edge, percent */
    int scale;          /* PS3: font scale, 0 = auto */
} plat_options;

/* Call before plat_init with argv[0] (may be NULL): the PS3 derives the
 * config directory from the program's own path. */
void plat_set_argv0(const char *argv0);

int plat_init(const plat_options *opt);
void plat_shutdown(void);

/* Directory holding the config, with a trailing slash. */
const char *plat_base_dir(void);

/* Current screen size in cells (may change on the host when the terminal
 * is resized). */
void plat_screen_size(int *cols, int *rows);

/* Show a screen. */
void plat_present(const screen *s);

/* Next key or KEY_NONE. Printable keys arrive as CP437 bytes. */
int plat_poll_key(void);

/* 1 once the system asked us to quit (XMB exit, SIGINT). */
int plat_quit_requested(void);

long long plat_now_ms(void);
void plat_sleep_ms(int ms);

/* Networking. net_connect returns a socket or -1 (err gets a message). */
int net_connect(const char *host, int port, char *err, size_t errcap);
/* >0 bytes read, 0 nothing available, -1 closed or failed. */
int net_recv(int fd, char *buf, size_t cap);
/* 0 on success, -1 on failure. Blocks until all is written. */
int net_send_all(int fd, const char *buf, size_t n);
void net_close(int fd);

void plat_log(const char *fmt, ...);

#endif
