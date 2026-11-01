/* Pictures linked in chat: a cache of downloaded images reduced to
 * half-block cells (two pixels per cell, top and bottom), as the door
 * draws them on a truecolor terminal. The platform fetches and decodes;
 * this keeps the state and the scaled result. */
#ifndef AC_PICS_H
#define AC_PICS_H
#include <stddef.h>

#define PIC_MAX      24      /* cache entries */
#define PIC_URL      512
#define PIC_MAX_COLS 148
#define PIC_MAX_ROWS 10      /* cells; 20 pixel rows */

typedef enum { PIC_EMPTY, PIC_QUEUED, PIC_LOADING, PIC_READY, PIC_FAILED } pic_state;

typedef struct {
    char url[PIC_URL];
    pic_state state;
    int cols, rows;                       /* cells */
    int src_w, src_h;                     /* the original's pixels */
    unsigned int top[PIC_MAX_COLS * PIC_MAX_ROWS];  /* 0x00RRGGBB per half cell */
    unsigned int bot[PIC_MAX_COLS * PIC_MAX_ROWS];
    char why[48];                         /* PIC_FAILED reason */
    unsigned long long used;              /* for eviction */
} pic;

typedef struct {
    pic items[PIC_MAX];
    unsigned long long tick;
} pic_cache;

void pics_init(pic_cache *pc);
/* The entry for url, created as PIC_QUEUED when new (evicting the least
 * recently used entry that is not loading). Never NULL. */
pic *pics_get(pic_cache *pc, const char *url);
/* Look up without creating; NULL when absent. */
pic *pics_find(pic_cache *pc, const char *url);
/* The next queued entry to download, or NULL. */
pic *pics_next_queued(pic_cache *pc);
/* Reduce packed 8-bit RGB (w*h*3) to at most max_cols x PIC_MAX_ROWS
 * cells, keeping the aspect ratio, and mark the entry ready. */
void pics_set_rgb(pic *p, const unsigned char *rgb, int w, int h, int max_cols);
void pics_fail(pic *p, const char *why);

/* 1 when url looks like a picture link (by extension, as the door decides). */
int pics_is_image_url(const char *url, size_t n);
/* Find up to max http(s) picture links in text; returns how many. Each
 * out[i] is NUL-terminated and at most PIC_URL-1 long. */
int pics_find_links(const unsigned char *text, int n, char out[][PIC_URL], int max);

#endif
