/* Text-cell screen and the chat layout drawn into it. A platform turns a
 * screen into pixels (PS3 framebuffer) or terminal escapes (host build). */
#ifndef AC_UI_H
#define AC_UI_H

#include "chat.h"

#define UI_MAX_COLS 160
#define UI_MAX_ROWS 60
#define UI_INPUT_MAX 1000

/* A cell carries real colours (0x00RRGGBB): the PS3 framebuffer is 24-bit,
 * so handle colours, 256-colour art and pictures all draw exactly. The CGA
 * attribute helpers map through ui_palette. */
typedef struct {
    unsigned char ch;    /* CP437 */
    unsigned int fg, bg; /* 0x00RRGGBB */
} cell;

/* 0-15 CGA, 16-231 the 6x6x6 cube, 232-255 greys: xterm's 256 colours. */
extern const unsigned int ui_palette[256];
unsigned int ui_rgb(unsigned char idx);

typedef struct {
    int cols, rows;
    int cur_x, cur_y;    /* text cursor; -1 to hide */
    cell cells[UI_MAX_COLS * UI_MAX_ROWS];
} screen;

/* Key codes handed to ui_key: CP437 bytes 0x20-0xFF, or these. */
enum {
    KEY_NONE = 0,
    KEY_ENTER = 0x1000, KEY_BACKSPACE, KEY_DELETE, KEY_ESC, KEY_TAB,
    KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT, KEY_PGUP, KEY_PGDN, KEY_HOME, KEY_END,
    KEY_F1, KEY_F2, KEY_F3,
    KEY_TOGGLE_COMPACT,  /* pad Select */
    KEY_VIEW_ART,        /* pad Square, F3: open the art viewer */
    KEY_ROOM_NEXT, KEY_ROOM_PREV,  /* pad Right/Left, Ctrl-N/Ctrl-P: other joined rooms */
    KEY_QUIT             /* pad Start, window close */
};

typedef enum { UI_ASK_NICK, UI_CHAT } ui_mode;

typedef struct {
    ui_mode mode;
    int compact;
    int scroll;                 /* transcript lines scrolled up from the bottom */
    unsigned char input[UI_INPUT_MAX];
    int inlen, incur;
    char status[160];           /* transient status text (UTF-8) */
    long long status_until;
    int quit;
    unsigned drawn_seq;
    int dirty;
    /* Art viewer: which piece is open (oldest = 0), -1 for none, and how
     * far it is scrolled when larger than the screen. */
    int view_art, art_dx, art_dy;
} ui;

void ui_init(ui *u);
void ui_set_status(ui *u, long long now_ms, long long for_ms, const char *fmt, ...);

/* Returns 1 when the screen needs redrawing. nick_out receives the name
 * entered in UI_ASK_NICK mode (then mode becomes UI_CHAT). */
int ui_key(ui *u, chat *c, int key, long long now_ms, char *nick_out, size_t nick_cap);

/* Lay the chat out into s (s->cols/rows set by the platform first). conn is
 * a short connection state label, e.g. "online" or "connecting". */
void ui_render(ui *u, const chat *c, screen *s, long long now_ms, const char *conn);

/* Word-wrap CP437 text to width: fills up to max segments with (start,len).
 * Returns the segment count (at least 1 for empty text). Shared with tests. */
typedef struct { int start, len; } ui_seg;
int ui_wrap(const unsigned char *text, int n, int width, ui_seg *segs, int max);

#endif
