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

/* On-screen keyboard. plat_osk_open shows it with a title and the text
 * to edit (CP437); 0 on success, -1 when there is none (host build).
 * plat_osk_poll then returns 0 while it is open, 1 with the entered text
 * in out (CP437, not NUL-terminated; *n set), or -1 when cancelled. The
 * caller keeps drawing frames while it is open. */
int plat_osk_open(const char *title, const unsigned char *text, int n);
int plat_osk_poll(unsigned char *out, int cap, int *n);
/* 1 while the keyboard is showing. */
int plat_osk_active(void);
/* 1 once the system reported the dialog as loaded (so a request that
 * never shows can be told apart from one in use). */
int plat_osk_shown(void);
/* Tear down a dialog that never appeared or is no longer wanted. */
void plat_osk_abort(void);

long long plat_now_ms(void);
void plat_sleep_ms(int ms);

/* Networking. net_connect returns a socket or -1 (err gets a message). */
int plat_net_connect(const char *host, int port, char *err, size_t errcap);
/* >0 bytes read, 0 nothing available, -1 closed or failed. */
int plat_net_recv(int fd, char *buf, size_t cap);
/* 0 on success, -1 on failure. Blocks until all is written. */
int plat_net_send_all(int fd, const char *buf, size_t n);
void plat_net_close(int fd);

/* Downloads for pictures: one at a time. plat_fetch_start begins a GET of
 * url with a byte cap; -1 when the platform can't (no HTTPS support, bad
 * URL), with err set. plat_fetch_poll returns 0 while in progress, 1 done
 * (data/n valid until the next start), -1 failed (err set). */
int plat_fetch_start(const char *url, size_t max_bytes, char *err, size_t errcap);
int plat_fetch_poll(const unsigned char **data, size_t *n, char *err, size_t errcap);
void plat_fetch_cancel(void);
/* Whether HTTPS certificates are checked (needs cacert.pem next to the app). */
void plat_set_ssl_verify(int on);

/* Decode an image file (PNG, JPEG, ... as the platform supports) into a
 * malloc'd packed RGB buffer; 1 on success. The caller frees *rgb. */
int plat_decode_image(const unsigned char *data, size_t n, unsigned char **rgb, int *w, int *h, char *err, size_t errcap);

/* Audio decoding. adec_open takes a whole downloaded file (kept alive by
 * the caller until adec_close) and a name hint for the format; NULL with
 * err set when the platform can't decode it. adec_pull copies up to max
 * interleaved 16-bit frames; 0 at the end, -1 on error. */
typedef struct adec adec;
adec *adec_open(const unsigned char *data, size_t n, const char *name, char *err, size_t errcap);
void adec_info(adec *d, int *rate, int *channels);
int adec_pull(adec *d, short *out, int max_frames);
void adec_close(adec *d);

/* Audio output. plat_audio_open starts the device for a source rate and
 * channel count (the platform resamples); plat_audio_space is how many
 * source frames it can take right now; plat_audio_write queues frames
 * (returns how many it took); plat_audio_close stops. */
int plat_audio_open(int rate, int channels);
int plat_audio_space(void);
int plat_audio_write(const short *pcm, int frames);
void plat_audio_close(void);

void plat_log(const char *fmt, ...);

#endif
