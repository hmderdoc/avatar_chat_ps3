#include "player.h"
#include <stdlib.h>
#include <string.h>

int player_fill(player *p, int (*pull)(adec *, short *, int)) {
    short tmp[4096 * 2];
    int target = p->rate * 2;
    if (target > PLAYER_FIFO_FRAMES - 4096) target = PLAYER_FIFO_FRAMES - 4096;
    while (!p->done && p->count < target) {
        int room = PLAYER_FIFO_FRAMES - p->count, chunk = room < 4096 ? room : 4096, got, i;
        if (chunk <= 0) break;
        got = pull(p->dec, tmp, chunk);
        if (got < 0) return -1;
        if (got == 0) { p->done = 1; break; }
        for (i = 0; i < got; i++) {
            int slot = (p->head + p->count + i) % PLAYER_FIFO_FRAMES, c;
            for (c = 0; c < p->channels; c++) p->fifo[slot * p->channels + c] = tmp[i * p->channels + c];
        }
        p->count += got;
    }
    return 0;
}

int player_take(player *p, short *out, int want) {
    int i, c;
    if (want > p->count) want = p->count;
    for (i = 0; i < want; i++) {
        int slot = (p->head + i) % PLAYER_FIFO_FRAMES;
        for (c = 0; c < p->channels; c++) out[i * p->channels + c] = p->fifo[slot * p->channels + c];
    }
    p->head = (p->head + want) % PLAYER_FIFO_FRAMES;
    p->count -= want;
    p->frames_out += want;
    return want;
}

int player_empty(const player *p) { return p->count == 0; }
