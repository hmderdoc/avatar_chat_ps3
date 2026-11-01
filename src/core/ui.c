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
#define GROUP_MS (2 * 60000LL)

void ui_init(ui *u) {
    memset(u, 0, sizeof *u);
    u->mode = UI_ASK_NICK;
    u->dirty = 1;
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

static void put(screen *s, int x, int y, unsigned char ch, unsigned char attr) {
    if (x < 0 || y < 0 || x >= s->cols || y >= s->rows) return;
    s->cells[y * s->cols + x].ch = ch;
    s->cells[y * s->cols + x].attr = attr;
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
                    char tmp[24];
                    snprintf(tmp, sizeof tmp, "%4s ", age);
                    x = draw_text(s, x, y, tmp, C_META, -1);
                    x = draw_text(s, x, y, m->name, name_attr, -1);
                    put(s, x, y, m->kind == MSG_PRIVATE ? '>' : ':', C_META);
                }
                raw(s, indent, y, m->text + segs[k].start, segs[k].len, text_attr);
            }
        }
        return;
    }

    /* Avatar mode: gutter on the left, name line, then the texts. */
    {
        int x, width = cols - GUTTER - 1;
        char age[24];
        for (k = 0; k < AVATAR_H; k++) {
            int yy = y0 + k;
            if (!ROW_OK(yy)) continue;
            if (m0->has_avatar) avatar_row(s, 1, yy, m0->avatar, k);
            else placeholder_row(s, 1, yy, m0->name, k);
        }
        if (ROW_OK(y)) {
            x = draw_text(s, GUTTER, y, m0->name, name_attr, width);
            if (m0->host[0]) {
                x = draw_text(s, x, y, " @", C_META, -1);
                x = draw_text(s, x, y, m0->host, C_META, cols - x - 8);
            }
            age_text(age, sizeof age, chat_msg_at(c, b->last)->time, now);
            if (age[0]) draw_text(s, cols - 1 - (int)strlen(age), y, age, C_META, -1);
        }
        y++;
        for (i = b->first; i <= b->last; i++) {
            const chat_msg *m = chat_msg_at(c, i);
            int n = ui_wrap(m->text, m->textlen, width, segs, MAX_SEGS);
            for (k = 0; k < n; k++, y++)
                if (ROW_OK(y)) raw(s, GUTTER, y, m->text + segs[k].start, segs[k].len, text_attr);
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

/* ---- the whole screen ------------------------------------------------ */

void ui_render(ui *u, const chat *c, screen *s, long long now_ms, const char *conn) {
    int y, x, cols = s->cols, rows = s->rows, input_rows, in_y, status_y;
    char tmp[200];

    if (cols > UI_MAX_COLS) cols = s->cols = UI_MAX_COLS;
    if (rows > UI_MAX_ROWS) rows = s->rows = UI_MAX_ROWS;
    for (y = 0; y < rows; y++) fill_row(s, y, ' ', C_TEXT);

    /* Header */
    fill_row(s, 0, ' ', C_HEADER);
    x = draw_text(s, 1, 0, "Avatar Chat", C_HEADER, -1);
    x = draw_text(s, x, 0, "  #", C_HEADER_DIM, -1);
    x = draw_text(s, x, 0, c->channel, C_HEADER, -1);
    snprintf(tmp, sizeof tmp, "  %d user%s", c->users, c->users == 1 ? "" : "s");
    x = draw_text(s, x, 0, tmp, C_HEADER_DIM, -1);
    snprintf(tmp, sizeof tmp, "%s  %s ", c->nick[0] ? c->nick : "", conn ? conn : "");
    draw_text(s, cols - 1 - (int)strlen(tmp), 0, tmp, C_HEADER_DIM, -1);

    /* Input: one or two rows depending on length. */
    input_rows = (u->inlen + 2) / (cols - 1) + 1;
    if (input_rows > 3) input_rows = 3;
    status_y = rows - 1;
    in_y = status_y - input_rows;

    /* Transcript between header and input. */
    transcript(u, c, s, 1, in_y - 1, now_ms);

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
            draw_text(s, 1, status_y, "Type a nickname (letters, digits, - _ space; up to 25) and press Enter.", C_STATUS, cols - 2);
        } else {
            snprintf(tmp, sizeof tmp, "Enter sends  /help commands  PgUp/PgDn scroll  %s  Start or /quit exits  %s",
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

int ui_key(ui *u, chat *c, int key, long long now_ms, char *nick_out, size_t nick_cap) {
    (void)now_ms;
    if (key == KEY_NONE) return 0;
    u->dirty = 1;
    switch (key) {
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
        if (u->mode == UI_CHAT) chat_notice(c, "Commands: /join <room>  /who  /rooms  /msg <name> <text>  /r <text>  /compact  /quit");
        return 1;
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
