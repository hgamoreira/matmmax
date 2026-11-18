#include <string.h>
#include "wstext.h"

unsigned ws_len(const WsText *t)
{
    return t->size - (t->gend - t->gap);
}

unsigned ws_free(const WsText *t)
{
    return t->gend - t->gap;
}

char ws_at(const WsText *t, unsigned pos)
{
    return t->buf[pos < t->gap ? pos : pos + (t->gend - t->gap)];
}

/* The characters from logical position pos on, as far as they are in one piece (up to the gap, or the
   end of the buffer): returns where they are and sets *n. */
static const char *run_from(const WsText *t, unsigned pos, unsigned *n)
{
    if (pos < t->gap) {
        *n = t->gap - pos;
        return t->buf + pos;
    }
    *n = ws_len(t) - pos;
    return t->buf + t->gend + (pos - t->gap);
}

void ws_copy(const WsText *t, unsigned from, unsigned to, char *out)
{
    while (from < to) {
        unsigned n;
        const char *p = run_from(t, from, &n);

        if (n > to - from) {
            n = to - from;
        }
        memcpy(out, p, n);
        out += n;
        from += n;
    }
}

void ws_init(WsText *t, char *buf, unsigned size, int rows, int width)
{
    memset(t, 0, sizeof *t);
    t->buf = buf;
    t->size = size;
    t->gend = size;
    t->rows = rows;
    t->width = width;
}

void ws_set(WsText *t, const char *text, unsigned n)
{
    memmove(t->buf + t->size - n, text, n);        /* the text goes to the far end: the gap is before it */
    t->gap = 0;
    t->gend = t->size - n;
    t->cur = 0;
    t->top = 0;
    t->want = 0;
    t->want_ok = 1;
    ++t->version;
}

/* Moves the gap so that it starts at the logical position pos. */
static void move_gap(WsText *t, unsigned pos)
{
    unsigned n;

    if (pos < t->gap) {
        n = t->gap - pos;
        memmove(t->buf + t->gend - n, t->buf + pos, n);
        t->gap = pos;
        t->gend -= n;
    } else if (pos > t->gap) {
        n = pos - t->gap;
        memmove(t->buf + t->gap, t->buf + t->gend, n);
        t->gap += n;
        t->gend += n;
    }
}

/* --- lines --- */

/* The start of the paragraph that contains pos: after the last newline before it. Scans backwards through
   the (at most two) pieces of the buffer. */
static unsigned par_start(const WsText *t, unsigned pos)
{
    const char *base;
    unsigned n, floor;

    while (pos > 0) {
        if (pos > t->gap) {
            base = t->buf + t->gend;
            n = pos - t->gap;
            floor = t->gap;
        } else {
            base = t->buf;
            n = pos;
            floor = 0;
        }
        while (n > 0 && base[n - 1] != '\n') {
            --n;
        }
        if (n > 0) {
            return floor + n;
        }
        pos = floor;
    }
    return 0;
}

/* The slow way, for a line that crosses the gap: one character at a time. */
static unsigned line_next_slow(const WsText *t, unsigned ls, unsigned width, unsigned *end, int *last)
{
    unsigned len = ws_len(t), i, brk = 0;
    int have_brk = 0;

    for (i = ls; i <= ls + width && i < len; ++i) {
        char c = ws_at(t, i);

        if (c == '\n') {
            *end = i;
            *last = 0;
            return i + 1;
        }
        if (c == ' ' && i > ls) {
            brk = i;
            have_brk = 1;
        }
    }
    *last = 0;
    if (len - ls <= width) {
        *end = len;
        *last = 1;
        return len;
    }
    if (have_brk) {
        *end = brk;
        return brk + 1;
    }
    *end = ls + width;
    return ls + width;
}

/* The line that starts at ls, wrapped at `width`: *end is where its text ends, the result is where the
   next line starts, and *last is 1 if the text ends with this line (the position at the end of the
   text is in it). The window of width + 1 characters is searched with memchr when it is in one piece. */
static unsigned line_next(const WsText *t, unsigned ls, unsigned width, unsigned *end, int *last)
{
    unsigned len = ws_len(t), avail = len - ls, window = width + 1, n, k;
    const char *p, *nl;

    if (avail < window) {
        window = avail;
    }
    p = run_from(t, ls, &n);
    if (n < window) {
        return line_next_slow(t, ls, width, end, last);
    }
    *last = 0;
    nl = window > 0 ? (const char *)memchr(p, '\n', window) : NULL;
    if (nl != NULL) {
        *end = ls + (unsigned)(nl - p);
        return *end + 1;
    }
    if (avail <= width) {
        *end = len;
        *last = 1;
        return len;
    }
    for (k = width; k >= 1; --k) {                 /* the last space in (ls, ls + width] */
        if (p[k] == ' ') {
            *end = ls + k;
            return ls + k + 1;
        }
    }
    *end = ls + width;
    return ls + width;
}

static unsigned line_start(const WsText *t, unsigned pos)
{
    unsigned ls = par_start(t, pos), end, nx;
    int last;

    for (;;) {
        nx = line_next(t, ls, (unsigned)t->width, &end, &last);
        if (last || nx > pos) {
            return ls;
        }
        ls = nx;
    }
}

/* The start of the line n lines before the line that starts at ls (the first line if there are fewer). The
   lines of a paragraph are found by walking it once from its start, so going back many lines costs the length
   of the paragraphs crossed, not that much for every line. */
static unsigned back_lines(const WsText *t, unsigned ls, int n)
{
    unsigned ring[WS_MAX_ROWS + 1], p, cur, end, nx;
    int count, last;

    while (n > 0 && ls > 0) {
        p = par_start(t, ls - 1);
        count = 0;
        for (cur = p; cur < ls; cur = nx) {
            ring[count % (WS_MAX_ROWS + 1)] = cur;
            ++count;
            nx = line_next(t, cur, (unsigned)t->width, &end, &last);
        }
        if (count >= n) {
            return n <= WS_MAX_ROWS ? ring[(count - n) % (WS_MAX_ROWS + 1)] : p;
        }
        n -= count;
        ls = p;
    }
    return ls;
}

/* The last position the cursor can have on the line that starts at ls: the end of its text, except on a line
   that was broken hard at the full width, where that position is the start of the next line (so one less). */
static unsigned line_cursor_end(const WsText *t, unsigned ls)
{
    unsigned end, nx;
    int last;

    nx = line_next(t, ls, (unsigned)t->width, &end, &last);
    if (!last && nx == end && end > ls) {
        return end - 1;
    }
    return end;
}

unsigned ws_column(const WsText *t)
{
    return t->cur - line_start(t, t->cur);
}

/* --- editing --- */

int ws_insert(WsText *t, char c)
{
    if (t->gap == t->gend) {
        return 0;
    }
    move_gap(t, t->cur);
    t->buf[t->gap++] = c;
    ++t->cur;
    ++t->version;
    t->want_ok = 0;
    return 1;
}

void ws_backspace(WsText *t)
{
    if (t->cur == 0) {
        return;
    }
    move_gap(t, t->cur);
    --t->gap;
    --t->cur;
    ++t->version;
    t->want_ok = 0;
}

void ws_delete(WsText *t)
{
    if (t->cur >= ws_len(t)) {
        return;
    }
    move_gap(t, t->cur);
    ++t->gend;
    ++t->version;
    t->want_ok = 0;
}

/* --- moving --- */

unsigned ws_want(WsText *t)
{
    if (!t->want_ok) {                      /* nothing but this and Up/Down has moved the cursor since */
        t->want = ws_column(t);
        t->want_ok = 1;
    }
    return t->want;
}

void ws_left(WsText *t)
{
    if (t->cur > 0) {
        --t->cur;
    }
    t->want_ok = 0;
}

void ws_right(WsText *t)
{
    if (t->cur < ws_len(t)) {
        ++t->cur;
    }
    t->want_ok = 0;
}

/* Puts the cursor on the line that starts at ls, as far right as `want` allows. */
static void place(WsText *t, unsigned ls, unsigned want)
{
    unsigned e = line_cursor_end(t, ls);

    t->cur = ls + (want < e - ls ? want : e - ls);
}

void ws_up(WsText *t)
{
    unsigned want = ws_want(t), ls = line_start(t, t->cur);

    if (ls == 0) {
        return;
    }
    place(t, back_lines(t, ls, 1), want);
}

void ws_down(WsText *t)
{
    unsigned want = ws_want(t), ls = line_start(t, t->cur), end, nx;
    int last;

    nx = line_next(t, ls, (unsigned)t->width, &end, &last);
    if (last) {
        return;
    }
    place(t, nx, want);
}

void ws_home(WsText *t)
{
    t->cur = line_start(t, t->cur);
    t->want = 0;
    t->want_ok = 1;
}

void ws_end(WsText *t)
{
    t->cur = line_cursor_end(t, line_start(t, t->cur));
    t->want_ok = 0;
}

void ws_word_left(WsText *t)
{
    while (t->cur > 0 && (ws_at(t, t->cur - 1) == ' ' || ws_at(t, t->cur - 1) == '\n')) {
        --t->cur;
    }
    while (t->cur > 0 && ws_at(t, t->cur - 1) != ' ' && ws_at(t, t->cur - 1) != '\n') {
        --t->cur;
    }
    t->want_ok = 0;
}

void ws_word_right(WsText *t)
{
    unsigned len = ws_len(t);

    while (t->cur < len && ws_at(t, t->cur) != ' ' && ws_at(t, t->cur) != '\n') {
        ++t->cur;
    }
    while (t->cur < len && (ws_at(t, t->cur) == ' ' || ws_at(t, t->cur) == '\n')) {
        ++t->cur;
    }
    t->want_ok = 0;
}

void ws_page_up(WsText *t)
{
    unsigned want = ws_want(t), ls = line_start(t, t->cur);

    if (ls > 0) {
        place(t, back_lines(t, ls, t->rows - 1), want);
    }
}

void ws_page_down(WsText *t)
{
    unsigned want = ws_want(t), ls = line_start(t, t->cur), end, nx;
    int i, last;

    for (i = 0; i < t->rows - 1; ++i) {
        nx = line_next(t, ls, (unsigned)t->width, &end, &last);
        if (last) {
            break;
        }
        ls = nx;
    }
    place(t, ls, want);
}

void ws_text_start(WsText *t)
{
    t->cur = 0;
    t->want = 0;
    t->want_ok = 1;
}

void ws_goto(WsText *t, unsigned pos)
{
    t->cur = pos < ws_len(t) ? pos : ws_len(t);
    t->want_ok = 0;
}

void ws_text_end(WsText *t)
{
    t->cur = ws_len(t);
    t->want_ok = 0;
}

/* --- the screen --- */

void ws_scroll(WsText *t)
{
    unsigned cl, ls, end, nx, len = ws_len(t);
    int d, last, k;

    if (t->cache_ok && t->cache_version == t->version && t->cache_top == t->top && t->cache_n == t->rows &&
        !t->cache_last) {
        /* The text has not changed since the last layout: its line starts are known. If the cursor is on
           one of the lines just below them, scrolling down is looking them up. */
        cl = line_start(t, t->cur);
        if (cl >= t->cache_start[t->rows]) {
            ls = t->cache_start[t->rows];
            for (d = 0; ls != cl && d < t->rows; ++d) {
                nx = line_next(t, ls, (unsigned)t->width, &end, &last);
                if (last) {
                    break;
                }
                ls = nx;
            }
            if (ls == cl && d + 1 < t->rows) {      /* cl is d lines below the next line after the screen */
                t->top = t->cache_start[d + 1];
                return;
            }
        }
    }
    if (t->top > len) {
        t->top = len;
    }
    t->top = line_start(t, t->top);                 /* an edit may have re-wrapped the line it was at */
    cl = line_start(t, t->cur);
    if (cl < t->top) {
        t->top = cl;
        return;
    }
    ls = t->top;
    d = 0;
    while (ls != cl && d < 2 * t->rows) {           /* how many lines below the top is the cursor's line? */
        nx = line_next(t, ls, (unsigned)t->width, &end, &last);
        if (last) {
            break;
        }
        ls = nx;
        ++d;
    }
    if (ls == cl && d < t->rows) {
        return;                                     /* already on screen */
    }
    if (ls == cl) {                                 /* a little below: scroll down by the difference */
        ls = t->top;
        for (k = 0; k < d - (t->rows - 1); ++k) {
            ls = line_next(t, ls, (unsigned)t->width, &end, &last);
        }
        t->top = ls;
        return;
    }
    t->top = back_lines(t, cl, t->rows - 1);        /* far below: make it the last row */
}

/* Where the cursor is among the lines of the last layout, or -1 if it is not on them. */
static int cached_row(const WsText *t)
{
    int r;

    if (t->cur < t->cache_start[0]) {
        return -1;
    }
    for (r = 0; r < t->cache_n; ++r) {
        if (t->cur < t->cache_start[r + 1] || (r == t->cache_n - 1 && t->cache_last)) {
            return r;
        }
    }
    return -1;
}

unsigned ws_line_length(const WsText *t, int r)
{
    return t->cache_len[r];
}

int ws_render(WsText *t, char *lines, int *cursor_row, int *cursor_col)
{
    unsigned ls, end, nx;
    int r, last = 0, live = 1, stride = t->width + 1;

    /* Moving the cursor within the lines already laid out needs no layout at all. */
    if (t->cache_ok && t->cache_version == t->version && t->cache_top == t->top && t->cache_lines == lines) {
        r = cached_row(t);
        if (r >= 0) {
            *cursor_row = r;
            *cursor_col = (int)(t->cur - t->cache_start[r]);
            return 0;
        }
    }
    ws_scroll(t);
    ls = t->top;
    t->cache_n = 0;
    t->cache_last = 0;
    *cursor_row = 0;
    *cursor_col = 0;
    for (r = 0; r < t->rows; ++r) {
        char *row = lines + r * stride;

        if (!live) {
            row[0] = '\0';
            t->cache_len[r] = 0;
            continue;
        }
        nx = line_next(t, ls, (unsigned)t->width, &end, &last);
        ws_copy(t, ls, end, row);
        row[end - ls] = '\0';
        t->cache_len[r] = (unsigned char)(end - ls);
        t->cache_start[r] = ls;
        t->cache_n = r + 1;
        if (last) {
            live = 0;
            t->cache_last = 1;
            t->cache_start[r + 1] = ws_len(t) + 1;
        } else {
            ls = nx;
            t->cache_start[r + 1] = nx;
        }
    }
    t->cache_ok = 1;
    t->cache_version = t->version;
    t->cache_top = t->top;
    t->cache_lines = lines;
    r = cached_row(t);
    if (r >= 0) {
        *cursor_row = r;
        *cursor_col = (int)(t->cur - t->cache_start[r]);
    }
    return 1;
}

unsigned ws_next_line(const WsText *t, unsigned pos, int width, char *out, int *last)
{
    unsigned end, nx;

    nx = line_next(t, pos, (unsigned)width, &end, last);
    ws_copy(t, pos, end, out);
    out[end - pos] = '\0';
    return nx;
}
