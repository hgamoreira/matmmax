/* X03 WordSmith/XT: the text engine, pure C (no DOS, no screen), so that it is unit-tested on Linux.

   The text is a gap buffer: the characters before the cursor, a gap, the characters after it. The gap
   moves only when something is typed or deleted (lazily), so moving the cursor around costs nothing.
   Paragraphs end with one LF. A paragraph is word-wrapped on the fly at `width` columns:

   - a line holds at most `width` characters; a newline within the first width+1 characters ends it;
   - otherwise the line breaks after the last space (not the first character) found within the first
     width+1 characters, and that space is not shown: it belongs to the end of the line;
   - with no space there, the line breaks hard after `width` characters. The cursor can then not be at the
     end of that line (that position is the start of the next one): End and Up/Down stop at its last
     character.

   Positions are logical indexes into the text (0 .. length). The cursor is at a position; a line's
   "start" is the position of its first character. */
#ifndef XT_WSTEXT_H
#define XT_WSTEXT_H

/* The most rows a text window can have (the line caches are this big): 25 for the XT; the VGA editor builds with 30. */
#ifndef WS_MAX_ROWS
#define WS_MAX_ROWS 25
#endif

typedef struct {
    char *buf;           /* the buffer, `size` bytes */
    unsigned size;
    unsigned gap, gend;  /* the gap is buf[gap .. gend) */
    unsigned cur;        /* the cursor */
    unsigned top;        /* the start of the first line shown */
    unsigned want;       /* the column Up and Down try to keep, if want_ok (else it is the current column) */
    int rows;            /* lines shown */
    int width;           /* wrap width */
    int want_ok;
    /* What the last ws_render laid out, so that moving the cursor inside the screen needs no new layout:
       valid only while the text (version) and the top line are the same. */
    unsigned version;    /* changes with every edit */
    int cache_ok;
    unsigned cache_version, cache_top;
    char *cache_lines;   /* the buffer the lines were written to */
    int cache_n;         /* lines laid out */
    unsigned cache_start[WS_MAX_ROWS + 1];   /* where each line starts; [n] is where the next would start */
    int cache_last;      /* 1 if the text ends within the lines laid out */
    unsigned char cache_len[WS_MAX_ROWS];    /* the length of each line's text */
} WsText;

void ws_init(WsText *t, char *buf, unsigned size, int rows, int width);
unsigned ws_len(const WsText *t);
unsigned ws_free(const WsText *t);
char ws_at(const WsText *t, unsigned pos);
/* Copies text[from .. to) into out (to - from bytes). */
void ws_copy(const WsText *t, unsigned from, unsigned to, char *out);
/* Throws the text away and puts `n` bytes in (they fit: n <= size); the cursor goes to 0. */
void ws_set(WsText *t, const char *text, unsigned n);

/* Editing. ws_insert returns 0 (and does nothing) when the buffer is full. */
int ws_insert(WsText *t, char c);
void ws_backspace(WsText *t);
void ws_delete(WsText *t);

/* Moving. */
void ws_left(WsText *t);
void ws_right(WsText *t);
void ws_up(WsText *t);
void ws_down(WsText *t);
void ws_home(WsText *t);
void ws_end(WsText *t);
void ws_word_left(WsText *t);
void ws_word_right(WsText *t);
void ws_page_up(WsText *t);        /* rows - 1 lines up */
void ws_page_down(WsText *t);
void ws_text_start(WsText *t);
/* The cursor to position pos (at most the end of the text). */
void ws_goto(WsText *t, unsigned pos);
void ws_text_end(WsText *t);

/* Column of the cursor in its line (0 .. width). */
unsigned ws_column(const WsText *t);
/* The column Up and Down try to keep (worked out when first needed). */
unsigned ws_want(WsText *t);

/* Moves `top` as little as possible so that the cursor's line is on screen. */
void ws_scroll(WsText *t);
/* Scrolls, then fills lines[r * (width + 1)] with the text of line r (r < rows; NUL-terminated, empty
   below the end of the text) and returns where the cursor is on screen. Use the same `lines` buffer every
   time and do not change it: while the text and the top line are unchanged and the cursor is on the lines
   already laid out, the call only finds the cursor (no layout, `lines` as it was). */
int ws_render(WsText *t, char *lines, int *cursor_row, int *cursor_col);
/* ws_render returns 1 if it laid the lines out again, 0 if `lines` is as it was (only the cursor was looked up).
   The length of line r's text after a ws_render: */
unsigned ws_line_length(const WsText *t, int r);

/* Line after line from `pos` (a line start) at an other wrap width, for printing: copies the line's
   text to out (at least width + 1 bytes), sets *last to 1 if the text ends with it, and returns the
   start of the next line. */
unsigned ws_next_line(const WsText *t, unsigned pos, int width, char *out, int *last);

#endif
