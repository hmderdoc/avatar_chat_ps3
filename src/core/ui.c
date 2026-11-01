#include "ui.h"
#include "cp437.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* Colours (CGA attribute nibbles). */
#define C_HEADER     0x1F  /* white on blue */
#define C_HEADER_DIM 0x1B  /* cyan on blue */
#define C_TEXT       0x07
#define C_NAME       0x0B
#define C_NAME_SELF  0x0A
#define C_NAME_PM    0x0D
#define C_TEXT_PM    0x05
#define C_META       0x08
#define C_NOTICE     0x08
#define C_NOTICE_DOT 0x03
#define C_INPUT      0x0F
#define C_PROMPT     0x0E
#define C_STATUS     0x70
#define C_STATUS_HOT 0x74

#define GUTTER (AVATAR_W + 2)   /* avatar plus a gap */

/* xterm's 256 colours: the CGA sixteen, the 6x6x6 cube, 24 greys. */
const unsigned int ui_palette[256] = {
    0x000000, 0x0000AA, 0x00AA00, 0x00AAAA, 0xAA0000, 0xAA00AA, 0xAA5500, 0xAAAAAA,
    0x555555, 0x5555FF, 0x55FF55, 0x55FFFF, 0xFF5555, 0xFF55FF, 0xFFFF55, 0xFFFFFF,
#define L(v) ((v) == 0 ? 0 : 55 + 40 * (v))
#define CUBE(r, g, b) ((unsigned)L(r) << 16 | (unsigned)L(g) << 8 | (unsigned)L(b))
#define ROW(r, g) CUBE(r, g, 0), CUBE(r, g, 1), CUBE(r, g, 2), CUBE(r, g, 3), CUBE(r, g, 4), CUBE(r, g, 5)
#define PLANE(r) ROW(r, 0), ROW(r, 1), ROW(r, 2), ROW(r, 3), ROW(r, 4), ROW(r, 5)
    PLANE(0), PLANE(1), PLANE(2), PLANE(3), PLANE(4), PLANE(5),
#undef L
#undef CUBE
#undef ROW
#undef PLANE
#define G(i) ((unsigned)(8 + 10 * (i)) * 0x010101u)
    G(0), G(1), G(2), G(3), G(4), G(5), G(6), G(7), G(8), G(9), G(10), G(11),
    G(12), G(13), G(14), G(15), G(16), G(17), G(18), G(19), G(20), G(21), G(22), G(23),
#undef G
};

unsigned int ui_rgb(unsigned char idx) { return ui_palette[idx]; }
#define GROUP_MS (2 * 60000LL)

void ui_init(ui *u) {
    memset(u, 0, sizeof *u);
    u->mode = UI_ASK_NICK;
    u->dirty = 1;
    u->view_art = -1;
}

void ui_set_status(ui *u, long long now_ms, long long for_ms, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(u->status, sizeof u->status, fmt, ap);
    va_end(ap);
    u->status_until = for_ms > 0 ? now_ms + for_ms : 0;
    u->dirty = 1;
}

/* ---- drawing primitives --------------------------------------------- */

static void put_rgb(screen *s, int x, int y, unsigned char ch, unsigned int fg, unsigned int bg) {
    cell *c;
    if (x < 0 || y < 0 || x >= s->cols || y >= s->rows) return;
    c = &s->cells[y * s->cols + x];
    c->ch = ch;
    c->fg = fg;
    c->bg = bg;
}

/* CGA attribute: fg low nibble, bg high nibble. */
static void put(screen *s, int x, int y, unsigned char ch, unsigned char attr) {
    put_rgb(s, x, y, ch, ui_palette[attr & 15], ui_palette[(attr >> 4) & 7]);
}

static void fill_row(screen *s, int y, unsigned char ch, unsigned char attr) {
    int x;
    for (x = 0; x < s->cols; x++) put(s, x, y, ch, attr);
}

/* Draw UTF-8 text (converted to CP437); returns the x after it. Not named
 * text(): on the PPU ELFv1 ABI a function f gets a code symbol .f, and
 * .text is taken. */
static int draw_text(screen *s, int x, int y, const char *utf8, unsigned char attr, int maxw) {
    unsigned char tmp[UI_MAX_COLS];
    size_t n = cp437_from_utf8(utf8, strlen(utf8), tmp, sizeof tmp), i;
    if (maxw >= 0 && (int)n > maxw) n = (size_t)(maxw > 0 ? maxw : 0);
    for (i = 0; i < n; i++) put(s, x + (int)i, y, tmp[i], attr);
    return x + (int)n;
}

static int raw(screen *s, int x, int y, const unsigned char *bytes, int n, unsigned char attr) {
    int i;
    for (i = 0; i < n; i++) put(s, x + i, y, bytes[i], attr);
    return x + n;
}

/* One row of a 10x6 avatar. */
static void avatar_row(screen *s, int x, int y, const unsigned char *av, int r) {
    int c;
    for (c = 0; c < AVATAR_W; c++)
        put(s, x + c, y, av[(r * AVATAR_W + c) * 2], av[(r * AVATAR_W + c) * 2 + 1] & 0x7F);
}

/* One row of the placeholder for a sender with no avatar: a frame with
 * their initial. */
static void placeholder_row(screen *s, int x, int y, const char *name, int r) {
    int c;
    for (c = 0; c < AVATAR_W; c++) {
        unsigned char ch, attr = C_META;
        if (r == 0) ch = c == 0 ? 0xDA : c == AVATAR_W - 1 ? 0xBF : 0xC4;
        else if (r == AVATAR_H - 1) ch = c == 0 ? 0xC0 : c == AVATAR_W - 1 ? 0xD9 : 0xC4;
        else if (c == 0 || c == AVATAR_W - 1) ch = 0xB3;
        else if (r == AVATAR_H / 2 - 1 && c == AVATAR_W / 2 - 1) { ch = (unsigned char)(name[0] ? name[0] : '?'); attr = C_TEXT; }
        else ch = ' ';
        put(s, x + c, y, ch, attr);
    }
}

/* The handle in its own colours, one per letter where the sender set one. */
static int draw_name_only(screen *s, int x, int y, const chat_msg *m, unsigned char attr) {
    int i;
    for (i = 0; m->name[i]; i++) {
        unsigned int fg = (i < m->ncolors && m->colors[i]) ? m->colors[i] : ui_palette[attr & 15];
        put_rgb(s, x + i, y, (unsigned char)m->name[i], fg, ui_palette[(attr >> 4) & 7]);
    }
    return x + i;
}

/* Cells the name and its tag take. */
static int name_width(const chat_msg *m) {
    return (int)strlen(m->name) + (m->tag[0] ? 1 + (int)strlen(m->tag) : 0);
}

/* Name plus the tag after it, as the door and the web page show it. */
static int draw_name(screen *s, int x, int y, const chat_msg *m, unsigned char attr) {
    x = draw_name_only(s, x, y, m, attr);
    if (m->tag[0]) {
        unsigned int fg = m->tag_fg ? m->tag_fg : ui_palette[C_META & 15];
        unsigned int bg = m->tag_bg ? m->tag_bg : ui_palette[0];
        int i;
        x++;
        for (i = 0; m->tag[i]; i++) put_rgb(s, x + i, y, (unsigned char)m->tag[i], fg, bg);
        x += i;
    }
    return x;
}

/* ---- wrapping -------------------------------------------------------- */

int ui_wrap(const unsigned char *t, int n, int width, ui_seg *segs, int max) {
    int count = 0, pos = 0;
    if (width < 1) width = 1;
    if (n <= 0) { segs[0].start = 0; segs[0].len = 0; return 1; }
    while (pos < n && count < max) {
        int end = pos + width, cut;
        if (end >= n) { segs[count].start = pos; segs[count].len = n - pos; count++; break; }
        cut = end;
        while (cut > pos && t[cut] != ' ') cut--;
        if (cut == pos) cut = end;           /* one long word: hard break */
        segs[count].start = pos;
        segs[count].len = cut - pos;
        count++;
        pos = cut;
        while (pos < n && t[pos] == ' ') pos++;
    }
    return count ? count : 1;
}

/* ---- transcript layout ----------------------------------------------- */

typedef struct {
    int first, last;   /* message indices, inclusive */
    int height;        /* lines, including the blank separator */
} block;

#define MAX_SEGS 64

static void age_text(char *out, size_t cap, long long then, long long now) {
    long long d = (now - then) / 1000;
    if (then <= 0 || now <= 0 || d < 0) { out[0] = 0; return; }
    if (d < 60) snprintf(out, cap, "now");
    else if (d < 3600) snprintf(out, cap, "%lldm", d / 60);
    else if (d < 86400) snprintf(out, cap, "%lldh", d / 3600);
    else snprintf(out, cap, "%lldd", d / 86400);
}

static int same_group(const chat_msg *a, const chat_msg *b) {
    return a->kind == b->kind && a->kind != MSG_NOTICE && a->self == b->self &&
           strcmp(a->name, b->name) == 0 && strcmp(a->host, b->host) == 0 &&
           b->time - a->time < GROUP_MS && b->time >= a->time;
}

/* Rows a message's pictures take: a picture's cell rows when it is
 * ready, else one line of status. */
static int pic_rows(const chat *c, const chat_msg *m, int width) {
    int i, rows = 0;
    for (i = 0; i < m->npics; i++) {
        const pic *p = pics_find((pic_cache *)&c->pics, m->pics[i]);
        if (!p) continue;
        rows += p->state == PIC_READY ? p->rows : 1;
    }
    (void)width;
    return rows;
}

/* Draw a message's pictures starting at row y, left-aligned at x (or
 * right-aligned ending at xend when mine); returns the next row. */
static int draw_pics(const chat *c, screen *s, const chat_msg *m, int x, int xend, int mine, int y, int top, int bottom) {
    int i, r, cx;
    for (i = 0; i < m->npics; i++) {
        pic *p = pics_find((pic_cache *)&c->pics, m->pics[i]);
        if (!p) continue;
        if (p->state != PIC_READY) {
            const char *name = strrchr(m->pics[i], '/');
            char line[160];
            name = name ? name + 1 : m->pics[i];
            if (p->state == PIC_FAILED) snprintf(line, sizeof line, "[picture %.40s: %s]", name, p->why);
            else snprintf(line, sizeof line, "[loading picture %.40s...]", name);
            if (y >= top && y <= bottom) {
                int len = (int)strlen(line), lx = mine ? xend - len : x;
                if (lx < 0) lx = 0;
                draw_text(s, lx, y, line, C_META, xend - lx);
            }
            y++;
            continue;
        }
        for (r = 0; r < p->rows; r++, y++) {
            int lx = mine ? xend - p->cols : x;
            if (y < top || y > bottom) continue;
            for (cx = 0; cx < p->cols; cx++)
                put_rgb(s, lx + cx, y, 0xDF, p->top[r * p->cols + cx], p->bot[r * p->cols + cx]);
        }
    }
    return y;
}

/* Height of a block in the current mode. */
static int block_height(const ui *u, const chat *c, int first, int last, int cols) {
    ui_seg segs[MAX_SEGS];
    int i, lines = 0, width;
    const chat_msg *m0 = chat_msg_at(c, first);
    if (m0->kind == MSG_NOTICE) {
        width = cols - 3;
        return ui_wrap(m0->text, m0->textlen, width, segs, MAX_SEGS);
    }
    if (u->compact) {
        for (i = first; i <= last; i++) {
            const chat_msg *m = chat_msg_at(c, i);
            width = cols - (5 + (int)strlen(m->name) + 2);
            if (width < 10) width = 10;
            lines += ui_wrap(m->text, m->textlen, width, segs, MAX_SEGS);
        }
        return lines;
    }
    width = cols - GUTTER - 1;
    lines = 1; /* name line */
    for (i = first; i <= last; i++) {
        const chat_msg *m = chat_msg_at(c, i);
        lines += ui_wrap(m->text, m->textlen, width, segs, MAX_SEGS);
        lines += pic_rows(c, m, width);
    }
    if (lines < AVATAR_H) lines = AVATAR_H;
    return lines + 1; /* separator */
}

/* Draw one block whose top line is at screen row y0 (may be off-screen);
 * only rows within [top, bottom] are written. */
static void draw_block(const ui *u, const chat *c, screen *s, const block *b, int y0, int top, int bottom, long long now) {
    ui_seg segs[MAX_SEGS];
    const chat_msg *m0 = chat_msg_at(c, b->first);
    int i, k, y = y0, cols = s->cols;
    unsigned char name_attr, text_attr;

#define ROW_OK(yy) ((yy) >= top && (yy) <= bottom)

    if (m0->kind == MSG_NOTICE) {
        int n = ui_wrap(m0->text, m0->textlen, cols - 3, segs, MAX_SEGS);
        for (k = 0; k < n; k++, y++) {
            if (!ROW_OK(y)) continue;
            if (k == 0) put(s, 1, y, 0x07, C_NOTICE_DOT);
            raw(s, 3, y, m0->text + segs[k].start, segs[k].len, C_NOTICE);
        }
        return;
    }

    name_attr = m0->kind == MSG_PRIVATE ? C_NAME_PM : m0->self ? C_NAME_SELF : C_NAME;
    text_attr = m0->kind == MSG_PRIVATE ? C_TEXT_PM : C_TEXT;

    if (u->compact) {
        for (i = b->first; i <= b->last; i++) {
            const chat_msg *m = chat_msg_at(c, i);
            char age[24];
            int indent = 5 + (int)strlen(m->name) + 2, n, width = cols - indent;
            if (width < 10) { width = 10; indent = cols - 10; }
            n = ui_wrap(m->text, m->textlen, width, segs, MAX_SEGS);
            age_text(age, sizeof age, m->time, now);
            for (k = 0; k < n; k++, y++) {
                if (!ROW_OK(y)) continue;
                if (k == 0) {
                    int x = 0;
                    char tmp[40];
                    snprintf(tmp, sizeof tmp, "%4s ", age);
                    x = draw_text(s, x, y, tmp, C_META, -1);
                    x = draw_name_only(s, x, y, m, name_attr);
                    put(s, x, y, m->kind == MSG_PRIVATE ? '>' : ':', C_META);
                }
                raw(s, indent, y, m->text + segs[k].start, segs[k].len, text_attr);
            }
            y = draw_pics(c, s, m, indent, cols - 1, 0, y, top, bottom);
        }
        return;
    }

    /* Avatar mode: the gutter on the left for others, on the right for our
     * own lines (the door's layout), then the name line and the texts. */
    {
        int width = cols - GUTTER - 1, mine = m0->self, gx = mine ? cols - 1 - AVATAR_W : 1, tx = mine ? 1 : GUTTER;
        char age[24];
        for (k = 0; k < AVATAR_H; k++) {
            int yy = y0 + k;
            if (!ROW_OK(yy)) continue;
            if (m0->has_avatar) avatar_row(s, gx, yy, m0->avatar, k);
            else placeholder_row(s, gx, yy, m0->name, k);
        }
        if (ROW_OK(y)) {
            age_text(age, sizeof age, chat_msg_at(c, b->last)->time, now);
            if (mine) {
                /* Right-aligned: "age  name @host" ending at the gutter. */
                int nlen = name_width(m0), hlen = m0->host[0] ? 2 + (int)strlen(m0->host) : 0, x;
                x = gx - 1 - nlen - hlen;
                if (x < tx + (int)strlen(age) + 2) x = tx + (int)strlen(age) + 2;
                x = draw_name(s, x, y, m0, name_attr);
                if (m0->host[0]) {
                    x = draw_text(s, x, y, " @", C_META, -1);
                    draw_text(s, x, y, m0->host, C_META, gx - 1 - x);
                }
                if (age[0]) draw_text(s, tx, y, age, C_META, -1);
            } else {
                int x = draw_name(s, GUTTER, y, m0, name_attr);
                if (m0->host[0]) {
                    x = draw_text(s, x, y, " @", C_META, -1);
                    draw_text(s, x, y, m0->host, C_META, cols - x - 8);
                }
                if (age[0]) draw_text(s, cols - 1 - (int)strlen(age), y, age, C_META, -1);
            }
        }
        y++;
        for (i = b->first; i <= b->last; i++) {
            const chat_msg *m = chat_msg_at(c, i);
            int n = ui_wrap(m->text, m->textlen, width, segs, MAX_SEGS);
            for (k = 0; k < n; k++, y++) {
                if (!ROW_OK(y)) continue;
                if (mine) raw(s, gx - 1 - segs[k].len, y, m->text + segs[k].start, segs[k].len, text_attr);
                else raw(s, GUTTER, y, m->text + segs[k].start, segs[k].len, text_attr);
            }
            y = draw_pics(c, s, m, mine ? tx : GUTTER, mine ? gx - 1 : cols - 1, mine, y, top, bottom);
        }
    }
#undef ROW_OK
}

static int transcript(ui *u, const chat *c, screen *s, int top, int bottom, long long now) {
    static block blocks[CHAT_RING];
    int nb = 0, i, total = 0, view = bottom - top + 1, y;
    /* Group messages into blocks, oldest first. */
    for (i = 0; i < c->count; i++) {
        const chat_msg *m = chat_msg_at(c, i);
        if (nb > 0 && m->kind != MSG_NOTICE && !u->compact) {
            block *b = &blocks[nb - 1];
            const chat_msg *prev = chat_msg_at(c, b->last);
            if (same_group(prev, m) && b->last - b->first < 20) { b->last = i; continue; }
        }
        blocks[nb].first = blocks[nb].last = i;
        nb++;
    }
    for (i = 0; i < nb; i++) {
        blocks[i].height = block_height(u, c, blocks[i].first, blocks[i].last, s->cols);
        total += blocks[i].height;
    }
    if (u->scroll > total - view) u->scroll = total - view;
    if (u->scroll < 0) u->scroll = 0;
    /* Walk newest to oldest; the newest block's last line sits at bottom + scroll. */
    y = bottom + u->scroll;
    for (i = nb - 1; i >= 0 && y >= top; i--) {
        int y0 = y - blocks[i].height + 1;
        if (y0 <= bottom) draw_block(u, c, s, &blocks[i], y0, top, bottom, now);
        y = y0 - 1;
    }
    return total;
}

/* Columns a picture may use in the current layout. */
int ui_pic_cols(const ui *u, int cols) {
    int w = u->compact ? cols - 12 : cols - GUTTER - 1;
    if (w > PIC_MAX_COLS) w = PIC_MAX_COLS;
    return w < 8 ? 8 : w;
}

static void insert_char(ui *u, unsigned char ch);

/* ---- built-in pad keyboard --------------------------------------------- */

/* Four rows of keys; the last row holds the specials. */
static const char *kb_rows_lower[4] = { "1234567890-=", "qwertyuiop[]", "asdfghjkl;'\\", "zxcvbnm,./" };
static const char *kb_rows_upper[4] = { "!@#$%^&*()_+", "QWERTYUIOP{}", "ASDFGHJKL:\"|", "ZXCVBNM<>?" };
#define KB_SPECIALS 5   /* space, backspace, shift, send, close */
static const char *kb_special_names[KB_SPECIALS] = { "space", "bksp", "shift", "send", "close" };

void ui_open_pad_keyboard(ui *u) {
    u->kb_open = 1;
    u->kb_row = 1;
    u->kb_col = 0;
    u->dirty = 1;
}

static int kb_row_len(const ui *u, int row) {
    return row < 4 ? (int)strlen((u->kb_shift ? kb_rows_upper : kb_rows_lower)[row]) : KB_SPECIALS;
}

/* Height of the keyboard panel in rows (border, 4 key rows, specials). */
#define KB_ROWS 7

static void render_pad_keyboard(ui *u, screen *s, int y0) {
    int r, c, cols = s->cols, x0;
    const char **rows = u->kb_shift ? kb_rows_upper : kb_rows_lower;
    int widest = 12 * 3 + 2;
    x0 = (cols - widest) / 2;
    if (x0 < 0) x0 = 0;
    for (r = 0; r < KB_ROWS; r++) fill_row(s, y0 + r, ' ', 0x70);
    draw_text(s, x0, y0, " Pad keyboard: D-pad moves, Cross types, Square backspace, Triangle space, Start sends, Circle closes ", 0x7F, cols - x0);
    for (r = 0; r < 4; r++) {
        int n = (int)strlen(rows[r]);
        for (c = 0; c < n; c++) {
            int sel = u->kb_row == r && u->kb_col == c;
            put(s, x0 + 1 + c * 3, y0 + 1 + r, ' ', sel ? 0x4F : 0x70);
            put(s, x0 + 2 + c * 3, y0 + 1 + r, (unsigned char)rows[r][c], sel ? 0x4F : 0x70);
            put(s, x0 + 3 + c * 3, y0 + 1 + r, ' ', sel ? 0x4F : 0x70);
        }
    }
    {
        int x = x0 + 1;
        for (c = 0; c < KB_SPECIALS; c++) {
            int sel = u->kb_row == 4 && u->kb_col == c;
            char lab[12];
            snprintf(lab, sizeof lab, " %s ", kb_special_names[c]);
            if (c == 2 && u->kb_shift) snprintf(lab, sizeof lab, " SHIFT ");
            x = draw_text(s, x, y0 + 5, lab, sel ? 0x4F : 0x70, -1) + 1;
        }
    }
}

/* Keys while the pad keyboard is up. Returns 2 when the line should be
 * sent (the caller runs the Enter path), 1 when handled, 0 if not ours. */
static int pad_keyboard_key(ui *u, int key) {
    switch (key) {
    case KEY_UP: u->kb_row = (u->kb_row + 4) % 5; break;
    case KEY_DOWN: u->kb_row = (u->kb_row + 1) % 5; break;
    case KEY_LEFT: case KEY_ROOM_PREV: u->kb_col--; break;
    case KEY_RIGHT: case KEY_ROOM_NEXT: u->kb_col++; break;
    case KEY_PAD_SQUARE: case KEY_VIEW_ART: case KEY_BACKSPACE:
        if (u->incur > 0) { memmove(u->input + u->incur - 1, u->input + u->incur, (size_t)(u->inlen - u->incur)); u->incur--; u->inlen--; }
        break;
    case KEY_OSK: insert_char(u, ' '); break;             /* Triangle: space */
    case KEY_PAD_L1: case KEY_PAD_R1: u->kb_shift = !u->kb_shift; break;
    case KEY_ESC: u->kb_open = 0; break;                  /* Circle */
    case KEY_QUIT: u->kb_open = 0; return 2;              /* Start: send */
    case KEY_ENTER: u->kb_open = 0; return 2;
    case KEY_PAD_ENTER:                                   /* Cross: the key under the cursor */
        if (u->kb_row < 4) {
            const char *row = (u->kb_shift ? kb_rows_upper : kb_rows_lower)[u->kb_row];
            insert_char(u, (unsigned char)row[u->kb_col]);
            if (u->kb_shift) u->kb_shift = 0;
        } else switch (u->kb_col) {
            case 0: insert_char(u, ' '); break;
            case 1: if (u->incur > 0) { memmove(u->input + u->incur - 1, u->input + u->incur, (size_t)(u->inlen - u->incur)); u->incur--; u->inlen--; } break;
            case 2: u->kb_shift = !u->kb_shift; break;
            case 3: u->kb_open = 0; return 2;
            case 4: u->kb_open = 0; break;
            default: break;
        }
        break;
    case KEY_PGUP: case KEY_PGDN: u->kb_shift = !u->kb_shift; break;   /* L1/R1 arrive as page keys */
    case KEY_TOGGLE_COMPACT: case KEY_PAD_L2: case KEY_PAD_R2:
        break; /* swallowed while the keyboard is up */
    default:
        if (key >= 0x20 && key <= 0xFF) { insert_char(u, (unsigned char)key); break; } /* a USB keyboard still works */
        return 0;
    }
    {
        int n = kb_row_len(u, u->kb_row);
        if (u->kb_col < 0) u->kb_col = n - 1;
        if (u->kb_col >= n) u->kb_col = 0;
    }
    return 1;
}

/* ---- art viewer ------------------------------------------------------ */

static void render_viewer(ui *u, chat *c, screen *s, long long now_ms) {
    chat_art *a = chat_art_at(c, u->view_art);
    int cols = s->cols, rows = s->rows, x, y, vis_w, vis_h, ox, oy;
    char tmp[200], age[24];
    if (!a) { u->view_art = -1; return; }
    a->viewed = 1;
    for (y = 0; y < rows; y++) fill_row(s, y, ' ', C_TEXT);
    fill_row(s, 0, ' ', C_HEADER);
    age_text(age, sizeof age, a->time, now_ms);
    snprintf(tmp, sizeof tmp, "Art from %s in #%s  %dx%d  %s", a->from, a->room, a->img.w, a->img.h, age);
    draw_text(s, 1, 0, tmp, C_HEADER, cols - 2);
    snprintf(tmp, sizeof tmp, "%d/%d", u->view_art + 1, c->nart);
    draw_text(s, cols - 1 - (int)strlen(tmp), 0, tmp, C_HEADER_DIM, -1);

    vis_w = cols; vis_h = rows - 2;
    if (u->art_dx > a->img.w - vis_w) u->art_dx = a->img.w - vis_w;
    if (u->art_dy > a->img.h - vis_h) u->art_dy = a->img.h - vis_h;
    if (u->art_dx < 0) u->art_dx = 0;
    if (u->art_dy < 0) u->art_dy = 0;
    ox = a->img.w < vis_w ? (vis_w - a->img.w) / 2 : 0;
    oy = a->img.h < vis_h ? (vis_h - a->img.h) / 2 : 0;
    for (y = 0; y < vis_h; y++) {
        int ay = y + u->art_dy - oy;
        if (ay < 0 || ay >= a->img.h) continue;
        for (x = 0; x < vis_w; x++) {
            int ax = x + u->art_dx - ox, k;
            if (ax < 0 || ax >= a->img.w) continue;
            k = ay * a->img.w + ax;
            put_rgb(s, x, 1 + y, a->img.ch[k], ui_palette[a->img.fg[k]], ui_palette[a->img.bg[k]]);
        }
    }
    fill_row(s, rows - 1, ' ', C_STATUS);
    snprintf(tmp, sizeof tmp, "Circle/Esc closes   Left/Right: other pieces   %s",
             (a->img.w > vis_w || a->img.h > vis_h) ? "Up/Down/PgUp/PgDn scroll" : "");
    draw_text(s, 1, rows - 1, tmp, C_STATUS, cols - 2);
    s->cur_x = s->cur_y = -1;
    u->drawn_seq = c->seq;
    u->dirty = 0;
}

/* ---- the whole screen ------------------------------------------------ */

void ui_render(ui *u, const chat *c, screen *s, long long now_ms, const char *conn) {
    int y, x, cols = s->cols, rows = s->rows, input_rows, in_y, status_y;
    char tmp[200];

    if (cols > UI_MAX_COLS) cols = s->cols = UI_MAX_COLS;
    if (rows > UI_MAX_ROWS) rows = s->rows = UI_MAX_ROWS;
    if (u->view_art >= 0) { render_viewer(u, (chat *)c, s, now_ms); return; }
    for (y = 0; y < rows; y++) fill_row(s, y, ' ', C_TEXT);

    /* Header: the joined rooms, the viewed one bright, unread counts. */
    fill_row(s, 0, ' ', C_HEADER);
    x = draw_text(s, 1, 0, "Avatar Chat ", C_HEADER, -1);
    {
        int i;
        for (i = 0; i < c->njoined; i++) {
            int here = strcmp(c->joined[i], c->channel) == 0;
            x = draw_text(s, x, 0, " #", here ? C_HEADER : C_HEADER_DIM, -1);
            x = draw_text(s, x, 0, c->joined[i], here ? C_HEADER : C_HEADER_DIM, -1);
            if (!here && c->unread[i] > 0) {
                snprintf(tmp, sizeof tmp, "(%d)", c->unread[i]);
                x = draw_text(s, x, 0, tmp, c->mentioned[i] ? 0x1E : 0x1A, -1);
            }
        }
    }
    snprintf(tmp, sizeof tmp, "  %d user%s", c->users, c->users == 1 ? "" : "s");
    x = draw_text(s, x, 0, tmp, C_HEADER_DIM, -1);
    snprintf(tmp, sizeof tmp, "%s  %s ", c->nick[0] ? c->nick : "", conn ? conn : "");
    draw_text(s, cols - 1 - (int)strlen(tmp), 0, tmp, C_HEADER_DIM, -1);

    /* Input: one or two rows depending on length. */
    input_rows = (u->inlen + 2) / (cols - 1) + 1;
    if (input_rows > 3) input_rows = 3;
    status_y = rows - 1;
    in_y = status_y - input_rows;

    /* Transcript between header and input (and the pad keyboard, if up). */
    transcript(u, c, s, 1, in_y - 1 - (u->kb_open ? KB_ROWS : 0), now_ms);
    if (u->kb_open) render_pad_keyboard(u, s, in_y - KB_ROWS);

    /* Input line(s). */
    {
        int i, px = 2, py = in_y, w = cols - 1;
        const char *prompt = u->mode == UI_ASK_NICK ? "Nick: " : "> ";
        int plen = (int)strlen(prompt);
        for (i = 0; i < input_rows; i++) fill_row(s, in_y + i, ' ', C_TEXT);
        draw_text(s, 0, in_y, prompt, C_PROMPT, -1);
        px = plen;
        s->cur_x = px; s->cur_y = py;
        for (i = 0; i < u->inlen; i++) {
            if (px >= w) { px = plen; py++; if (py >= status_y) break; }
            put(s, px, py, u->input[i], C_INPUT);
            px++;
            if (i + 1 == u->incur) { s->cur_x = px; s->cur_y = py; }
            if (px >= w && i + 1 == u->incur) { s->cur_x = plen; s->cur_y = py + 1 < status_y ? py + 1 : py; }
        }
        if (u->incur == 0) { s->cur_x = plen; s->cur_y = in_y; }
    }

    /* Status bar. */
    fill_row(s, status_y, ' ', C_STATUS);
    if (u->status[0] && (u->status_until == 0 || now_ms < u->status_until)) {
        draw_text(s, 1, status_y, u->status, C_STATUS_HOT, cols - 2);
    } else {
        u->status[0] = 0;
        if (u->mode == UI_ASK_NICK) {
            draw_text(s, 1, status_y, "Type a nickname (letters, digits, - _ space; up to 25) and press Enter. Pad: Triangle or Cross opens the keyboard.", C_STATUS, cols - 2);
        } else {
            int unviewed = chat_unviewed_art(c);
            if (unviewed > 0)
                snprintf(tmp, sizeof tmp, "%d new art piece%s: Square or /img to view   /help commands   Start or /quit exits",
                         unviewed, unviewed == 1 ? "" : "s");
            else
                snprintf(tmp, sizeof tmp, "Enter sends  Triangle: keyboard  /help commands  L1/R1 scroll  %s  Start quits  %s",
                         u->compact ? "Select: avatars" : "Select: compact",
                         u->scroll > 0 ? "[scrolled]" : "");
            draw_text(s, 1, status_y, tmp, C_STATUS, cols - 2);
        }
    }
    u->drawn_seq = c->seq;
    u->dirty = 0;
}

/* ---- keys ------------------------------------------------------------ */

static void insert_char(ui *u, unsigned char ch) {
    if (u->inlen >= UI_INPUT_MAX) return;
    memmove(u->input + u->incur + 1, u->input + u->incur, (size_t)(u->inlen - u->incur));
    u->input[u->incur++] = ch;
    u->inlen++;
}

static int valid_nick_char(unsigned char ch) {
    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' || ch == ' ';
}

static void open_art(ui *u, chat *c, int index) {
    if (index < 0 || index >= c->nart) return;
    u->view_art = index;
    u->art_dx = u->art_dy = 0;
    u->dirty = 1;
}

int ui_osk_result(ui *u, chat *c, const unsigned char *text, int n, long long now_ms, char *nick_out, size_t nick_cap) {
    u->osk_open = 0;
    u->dirty = 1;
    if (n < 0) n = 0;
    if (n > UI_INPUT_MAX) n = UI_INPUT_MAX;
    memcpy(u->input, text, (size_t)n);
    u->inlen = u->incur = n;
    if (n == 0) return 1;
    return ui_key(u, c, KEY_ENTER, now_ms, nick_out, nick_cap);
}

/* Keys while a piece of art is open. */
static int viewer_key(ui *u, chat *c, int key) {
    switch (key) {
    case KEY_ESC: case KEY_ENTER: case KEY_VIEW_ART: case KEY_F3: u->view_art = -1; return 1;
    case KEY_LEFT: if (u->view_art > 0) open_art(u, c, u->view_art - 1); return 1;
    case KEY_RIGHT: if (u->view_art + 1 < c->nart) open_art(u, c, u->view_art + 1); return 1;
    case KEY_UP: u->art_dy -= 1; return 1;
    case KEY_DOWN: u->art_dy += 1; return 1;
    case KEY_PGUP: u->art_dy -= 10; return 1;
    case KEY_PGDN: u->art_dy += 10; return 1;
    case KEY_HOME: u->art_dx -= 10; return 1;
    case KEY_END: u->art_dx += 10; return 1;
    case KEY_QUIT: u->quit = 1; return 1;
    default: return 1;
    }
}

int ui_key(ui *u, chat *c, int key, long long now_ms, char *nick_out, size_t nick_cap) {
    (void)now_ms;
    if (key == KEY_NONE) return 0;
    u->dirty = 1;
    if (u->view_art >= 0) return viewer_key(u, c, key);
    if (u->kb_open) {
        int rc = pad_keyboard_key(u, key);
        if (rc == 1) return 1;
        if (rc == 2) key = KEY_ENTER;
        else return 1;
    }
    switch (key) {
    case KEY_PAD_SQUARE: case KEY_PAD_L1: case KEY_PAD_R1: case KEY_PAD_L2: case KEY_PAD_R2:
        return 1;
    case KEY_VIEW_ART:
    case KEY_F3: {
        int i, pick = c->nart - 1;
        if (c->nart == 0) { ui_set_status(u, now_ms, 3000, "No art has been sent here yet."); return 1; }
        for (i = 0; i < c->nart; i++) if (!chat_art_at(c, i)->viewed) { pick = i; break; }
        open_art(u, c, pick);
        return 1;
    }
    case KEY_QUIT:
        u->quit = 1;
        return 1;
    case KEY_ESC:
        /* Clears the line only; quitting is Start, Ctrl-C or /quit, so a
         * stray Escape never drops someone out of the chat. */
        u->inlen = u->incur = 0;
        u->scroll = 0;
        return 1;
    case KEY_TOGGLE_COMPACT:
    case KEY_F2:
        u->compact = !u->compact;
        u->scroll = 0;
        return 1;
    case KEY_F1:
        if (u->mode == UI_CHAT) chat_notice(c, "Commands: /join <room>  /who  /rooms  /msg <name> <text>  /r <text>  /img [N]  /compact  /quit");
        return 1;
    case KEY_ROOM_NEXT: chat_switch(c, 1); u->scroll = 0; return 1;
    case KEY_ROOM_PREV: chat_switch(c, -1); u->scroll = 0; return 1;
    case KEY_PGUP: u->scroll += 5; return 1;
    case KEY_PGDN: u->scroll -= 5; if (u->scroll < 0) u->scroll = 0; return 1;
    case KEY_UP: u->scroll += 1; return 1;
    case KEY_DOWN: u->scroll -= 1; if (u->scroll < 0) u->scroll = 0; return 1;
    case KEY_LEFT: if (u->incur > 0) u->incur--; return 1;
    case KEY_RIGHT: if (u->incur < u->inlen) u->incur++; return 1;
    case KEY_HOME: u->incur = 0; return 1;
    case KEY_END: u->incur = u->inlen; return 1;
    case KEY_BACKSPACE:
        if (u->incur > 0) {
            memmove(u->input + u->incur - 1, u->input + u->incur, (size_t)(u->inlen - u->incur));
            u->incur--; u->inlen--;
        }
        return 1;
    case KEY_DELETE:
        if (u->incur < u->inlen) {
            memmove(u->input + u->incur, u->input + u->incur + 1, (size_t)(u->inlen - u->incur - 1));
            u->inlen--;
        }
        return 1;
    case KEY_TAB:
        return 1;
    case KEY_OSK:
        return 1; /* the main loop opens it; nothing to do in the core */
    case KEY_ENTER:
        if (u->mode == UI_ASK_NICK) {
            int i, n = u->inlen;
            while (n > 0 && u->input[n - 1] == ' ') n--;
            for (i = 0; i < n; i++) if (!valid_nick_char(u->input[i])) break;
            if (n == 0 || i < n || n > 25) {
                ui_set_status(u, now_ms, 4000, "Letters, digits, spaces, - and _ only, up to 25.");
                return 1;
            }
            if (nick_out && nick_cap) {
                size_t k = (size_t)n < nick_cap - 1 ? (size_t)n : nick_cap - 1;
                memcpy(nick_out, u->input, k);
                nick_out[k] = 0;
            }
            u->mode = UI_CHAT;
            u->inlen = u->incur = 0;
            return 1;
        }
        chat_input(c, u->input, (size_t)u->inlen);
        u->inlen = u->incur = 0;
        u->scroll = 0;
        if (c->toggle_compact) { c->toggle_compact = 0; u->compact = !u->compact; }
        if (c->want_art >= 0) { open_art(u, c, c->want_art); c->want_art = -1; }
        if (c->want_quit) u->quit = 1;
        return 1;
    default:
        if (key >= 0x20 && key <= 0xFF) {
            if (u->mode == UI_ASK_NICK && !valid_nick_char((unsigned char)key)) return 1;
            insert_char(u, (unsigned char)key);
            return 1;
        }
        return 0;
    }
}
