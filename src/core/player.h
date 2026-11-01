/* Audio playback of a downloaded track: the platform decodes (adec) and
 * owns the output port; this keeps a FIFO between them so decoding runs a
 * little ahead of the hardware and a slow decoder never stalls the UI. */
#ifndef AC_PLAYER_H
#define AC_PLAYER_H
#include <stddef.h>

#define PLAYER_FIFO_FRAMES (48000 * 4)   /* four seconds at the highest rate */

typedef struct adec adec;   /* a decoder, see platform.h */

typedef struct {
    adec *dec;
    int rate, channels;
    short *fifo;             /* interleaved frames, a ring */
    int head, count;         /* frames */
    int done;                /* decoder finished; drain and stop */
    char name[96];
    long long frames_out;    /* source frames handed to the output */
} player;

/* Fill the FIFO from the decoder up to about two seconds. Returns -1 on a
 * decoder error. */
int player_fill(player *p, int (*pull)(adec *, short *, int));
/* Hand up to want frames to out; returns frames copied. */
int player_take(player *p, short *out, int want);
int player_empty(const player *p);

#endif
