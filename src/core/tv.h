/* The TV lounge: a telnetvision consumer. A room is "tuned" to a feed by a
 * [TVTUNER|host|port|channel] marker; the client connects to the fanout
 * service and shows its frames (RGB, two pixels per cell) in place of the
 * transcript. Wire format (telnetvision/wire.py): u32 big-endian length +
 * payload; payload[0] is the type. */
#ifndef AC_TV_H
#define AC_TV_H
#include <stddef.h>

#define TV_MAX_COLS 160
#define TV_MAX_ROWS 60
#define TV_MAX_MSG (4u * 1024u * 1024u)

typedef struct {
    int cols, rows;
    char caption[128];           /* CP437 */
    unsigned int top[TV_MAX_COLS * TV_MAX_ROWS];
    unsigned int bot[TV_MAX_COLS * TV_MAX_ROWS];
    unsigned long frames;        /* frames received */
} tv_frame;

typedef struct {
    unsigned char *buf;          /* message assembly, TV_MAX_MSG */
    size_t len;
    size_t need;                 /* 0: waiting for the length */
    tv_frame frame;
    /* The newest audio chunk, handed to the player: a sound file (WAV)
     * the broadcaster cut, see the door's apcaudio. */
    const unsigned char *audio;
    size_t audio_n;
    unsigned int audio_rate;
    unsigned char audio_codec, audio_channels;
    int have_audio;
    unsigned long dropped;
} tv_consumer;

/* buf must hold TV_MAX_MSG bytes. */
void tv_init(tv_consumer *t, unsigned char *buf);
/* The HELLO_CONSUMER message for a channel; returns its length. */
size_t tv_hello(unsigned char *out, size_t cap, const char *channel, int want_audio);
/* Feed received bytes. Returns the number of new frames decoded; sets
 * have_audio when an AUDIO message arrived (valid until the next feed). */
int tv_feed(tv_consumer *t, const unsigned char *data, size_t n);

/* [TVTUNER|host|port|channel] and [TVTUNER|off]. Returns 1 for a tune (host,
 * port, channel filled), 2 for off, 0 if not a tuner marker. */
int tv_parse_marker(const char *s, char *host, size_t hcap, int *port, char *channel, size_t ccap);

#endif
