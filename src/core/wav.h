/* RIFF/WAVE: 8- and 16-bit PCM, any rate, mono or stereo, as 16-bit
 * interleaved frames. */
#ifndef AC_WAV_H
#define AC_WAV_H
#include <stddef.h>

typedef struct {
    const unsigned char *pcm;  /* the data chunk */
    size_t frames;
    int rate, channels, bits;
    size_t pos;                /* frames consumed */
} wav_reader;

int wav_is(const unsigned char *data, size_t n);
/* 1 on success. */
int wav_open(wav_reader *r, const unsigned char *data, size_t n, char *err, size_t errcap);
/* Copies up to max frames as 16-bit interleaved; returns frames, 0 at end. */
int wav_pull(wav_reader *r, short *out, int max);

#endif
