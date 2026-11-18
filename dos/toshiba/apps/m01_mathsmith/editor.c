/* M08: MathSmith's script editor, see editor.h. The text is WordSmith's gap buffer (wstext.c, a line is never wrapped before 250 columns, the
   screen scrolls sideways), loaded and saved through wsio.c. The screen is rows 0 (status), 1-23 (text) and 24 (keys, or a message). */

#include <ctype.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "editor.h"
#include "version.h"
#include "msattr.h"
#ifdef MS_486
#include "picker.h"
#endif
#include "screen.h"
#include "wsio.h"
#include "wstext.h"

#define ROW_TEXT 1
#define ED_ROWS (SCR_ROWS - 2)
#define ROW_KEYS (SCR_ROWS - 1)
#define VIS SCR_COLS

#if ED_ROWS > WS_MAX_ROWS
#error the text window has more rows than WsText's line caches hold: build with -DWS_MAX_ROWS=<rows> (M11 found this the hard way)
#endif
#define HSTEP 8
#define LINE_MAX 250
#define TEXT_MAX 16000u                    /* a script is small; the floppy and the assistant (4000 characters) agree */
#define TAB_STOP 4
#define CHUNK 512

#define K_ESC 0x011B
#define K_ENTER 0x1C0D
#define K_BACKSPACE 0x0E08
#define K_TAB 0x0F09
#define K_UP 0x4800
#define K_DOWN 0x5000
#define K_LEFT 0x4B00
#define K_RIGHT 0x4D00
#define K_HOME 0x4700
#define K_END 0x4F00
#define K_PGUP 0x4900
#define K_PGDN 0x5100
#define K_DEL 0x5300
#define K_CTRL_LEFT 0x7300
#define K_CTRL_RIGHT 0x7400
#define K_CTRL_HOME 0x7700
#define K_CTRL_END 0x7500
#define K_F2 0x3C00
#define K_F3 0x3D00
#define K_F4 0x3E00
#define K_F5 0x3F00
#define K_F6 0x4000
#define K_F7 0x4100
#define K_F8 0x4200
#define K_F10 0x4400
#define K_ALT_Z 0x2C00

static WsText text;
static char *lines;                        /* ED_ROWS rows of LINE_MAX + 1 characters, as ws_render fills them */
static char *snapshot;                     /* the text before the assistant's last change (Alt+Z), or NULL */
static unsigned snapshot_len;
static char path[64];
static char message[80];
static int message_error;
static int dirty;
static int hscroll;
static unsigned cache_pos, cache_line;     /* the line number of position cache_pos: moving the cursor only counts the lines in between */
static char last_find[40];
static int repaint;                        /* a box covered the text: the next draw writes all the rows again */
static int readonly;                       /* editor_view: the help text, which cannot be changed */
static const char *view_title;
static unsigned view_lines;
static int line_width = LINE_MAX;          /* the text's wrap width: LINE_MAX for a script, narrower for the viewer */

static void say(const char *msg, int error)
{
    strncpy(message, msg, sizeof(message) - 1);
    message[sizeof(message) - 1] = '\0';
    message_error = error;
}

/* The line (1 = first) the cursor is on. */
static unsigned cursor_line(void)
{
    unsigned p = cache_pos;

    while (p < text.cur) {
        if (ws_at(&text, p) == '\n') {
            ++cache_line;
        }
        ++p;
    }
    while (p > text.cur) {
        --p;
        if (ws_at(&text, p) == '\n') {
            --cache_line;
        }
    }
    cache_pos = p;
    return cache_line;
}

static void forget_lines(void)
{
    cache_pos = 0;
    cache_line = 1;
}

static void goto_line(unsigned line)
{
    unsigned pos = 0, len = ws_len(&text), at = 1;

    while (at < line && pos < len) {
        if (ws_at(&text, pos) == '\n') {
            ++at;
        }
        ++pos;
    }
    ws_goto(&text, pos);
}

/* Typing: keeps the line count right without a new scan. */
static int type_char(char c)
{
    unsigned line = cursor_line();

    if (!ws_insert(&text, c)) {
        say("The text is full (16 KB).", 1);
        return 0;
    }
    dirty = 1;
    cache_pos = text.cur;
    cache_line = c == '\n' ? line + 1 : line;
    return 1;
}

static void backspace(void)
{
    char c;
    unsigned line;

    if (text.cur == 0) {
        return;
    }
    line = cursor_line();
    c = ws_at(&text, text.cur - 1);
    ws_backspace(&text);
    dirty = 1;
    cache_pos = text.cur;
    cache_line = c == '\n' ? line - 1 : line;
}

static void delete_char(void)
{
    if (text.cur < ws_len(&text)) {
        cursor_line();
        ws_delete(&text);
        dirty = 1;
    }
}

/* Enter: a new line with the blanks the line had up to the cursor. */
static void new_line(void)
{
    unsigned start = text.cur, n = 0, i;

    while (start > 0 && ws_at(&text, start - 1) != '\n') {
        --start;
    }
    while (start + n < text.cur && ws_at(&text, start + n) == ' ') {
        ++n;
    }
    if (!type_char('\n')) {
        return;
    }
    for (i = 0; i < n; ++i) {
        if (!type_char(' ')) {
            return;
        }
    }
}

static void tab(void)
{
    unsigned n = TAB_STOP - ws_column(&text) % TAB_STOP;

    while (n-- > 0) {
        if (!type_char(' ')) {
            return;
        }
    }
}

/* --- drawing --- */

static void put_row(int row, const char *s, unsigned char attr)
{
    char t[SCR_COLS + 1];

    strncpy(t, s, SCR_COLS);                           /* a longer text must not run over into the next row */
    t[SCR_COLS] = '\0';
    scr_clear_row(row, attr);
    scr_put(row, 0, t, attr);
}

static void draw(void)
{
    char bar[160], row[VIS + 1];
    int crow, ccol, r, h, laid_out;
    unsigned n;

    laid_out = ws_render(&text, lines, &crow, &ccol);
    h = hscroll;                                           /* the sideways scroll keeps the cursor in view */
    if (ccol < h + (h > 0 ? 1 : 0)) {
        h = ccol > 0 ? ((ccol - 1) / HSTEP) * HSTEP : 0;
    } else if (ccol >= h + VIS - 1) {
        h = ((ccol - (VIS - 1)) / HSTEP + 1) * HSTEP;
    }
    scr_begin_frame();
    if (laid_out || h != hscroll || repaint) {
        repaint = 0;
        for (r = 0; r < ED_ROWS; ++r) {
            const char *line = lines + (size_t)r * (line_width + 1);

            n = (unsigned)strlen(line);
            scr_clear_row(ROW_TEXT + r, ATTR_NORMAL);
            if (n > (unsigned)h) {
                n -= (unsigned)h;
                if (n > VIS) {
                    n = VIS;
                }
                memcpy(row, line + h, n);
                row[n] = '\0';
                scr_put(ROW_TEXT + r, 0, row, ATTR_NORMAL);
            }
        }
    }
    hscroll = h;
    if (readonly) {
        unsigned first = cursor_line() - (unsigned)crow, last = first + ED_ROWS - 1;

        sprintf(bar, " %s (read only)   Lines %u-%u of %u", view_title, first, last < view_lines ? last : view_lines, view_lines);
    } else {
        sprintf(bar, " %s%s   Ln %u  Col %d   %u of %u characters", path, dirty ? " *" : "", cursor_line(), ccol + 1, ws_len(&text), TEXT_MAX);
    }
    put_row(0, bar, ATTR_REVERSE);
    if (message[0] != '\0') {
        put_row(ROW_KEYS, message, message_error ? ATTR_REVERSE : ATTR_BRIGHT);
    } else if (readonly) {
        put_row(ROW_KEYS, " Arrows, PgUp, PgDn, Ctrl+Home, Ctrl+End move   F3 Find   F4 Line   Esc Close", ATTR_KEYS);
    } else {
#ifdef MS_NO_AI
        put_row(ROW_KEYS, " F2 Save   F7 Save as   F3 Find   F4 Line   F5 Run   Esc Quit", ATTR_KEYS);
#elif defined(MS_486)
        put_row(ROW_KEYS, " F2 Save F3 Find F4 Line F5 Run F6 Assist F7 Save as F8 Open Alt+Z Undo Esc Quit", ATTR_KEYS);
#else
        put_row(ROW_KEYS, " F2 Save F7 Save as F3 Find F4 Line F5 Run F6 Assistant Alt+Z Undo Esc Quit", ATTR_KEYS);
#endif
    }
    scr_end_frame();
    scr_cursor(ROW_TEXT + crow, ccol - hscroll);
}

/* One line of input on the bottom row. Returns 1 on Enter. */
static int prompt(const char *label, char *buf, int max)
{
    int ok, col = (int)strlen(label) + 1;

    put_row(ROW_KEYS, label, ATTR_BRIGHT);
    scr_dump_to_env("XTDUMPEDIT");                   /* test hook: the screen with the question */
    ok = scr_input(ROW_KEYS, col, buf, max, ATTR_UNDERLINE);
    message[0] = '\0';
    return ok;
}

#ifndef MS_NO_AI
/* A longer text to type (what the assistant is asked): a box of the bottom rows, wrapped at the screen's edge, up to max characters (234 at most). The
   arrows, Home, End, Backspace and Del edit it; Enter accepts, Esc cancels. Returns 1 on Enter. */
#define BOX_W SCR_COLS
#define BOX_ROWS 3
static int prompt_long(const char *label, char *buf, int max)
{
    int len = (int)strlen(buf), pos, top = ROW_KEYS - BOX_ROWS - 1, r, n;
    unsigned key;
    char row[BOX_W + 1], count[BOX_W + 1];

    if (max > BOX_W * BOX_ROWS) {
        max = BOX_W * BOX_ROWS;
    }
    if (len > max) {
        len = max;
        buf[len] = '\0';
    }
    pos = len;
    repaint = 1;
    for (;;) {
        put_row(top, label, ATTR_REVERSE);
        for (r = 0; r < BOX_ROWS; ++r) {
            for (n = 0; n < BOX_W && r * BOX_W + n < len; ++n) {
                row[n] = buf[r * BOX_W + n];
            }
            row[n] = '\0';
            scr_clear_row(top + 1 + r, ATTR_NORMAL);
            scr_put(top + 1 + r, 0, row, ATTR_BRIGHT);
        }
        sprintf(count, " Enter: ok   Esc: cancel   %d of %d characters", len, max);
        put_row(ROW_KEYS, count, ATTR_NORMAL);
        scr_cursor(top + 1 + pos / BOX_W, pos % BOX_W);
        scr_dump_to_env("XTDUMPEDIT");               /* test hook: the screen with the box */
        key = scr_getkey();
        if (key == K_ENTER) {
            message[0] = '\0';
            return 1;
        }
        if (key == K_ESC) {
            message[0] = '\0';
            buf[0] = '\0';
            return 0;
        }
        switch (key) {
        case K_LEFT: if (pos > 0) { --pos; } break;
        case K_RIGHT: if (pos < len) { ++pos; } break;
        case K_UP: pos = pos >= BOX_W ? pos - BOX_W : pos; break;
        case K_DOWN: pos = pos + BOX_W <= len ? pos + BOX_W : len; break;
        case K_HOME: pos = 0; break;
        case K_END: pos = len; break;
        case K_BACKSPACE:
            if (pos > 0) {
                memmove(buf + pos - 1, buf + pos, (size_t)(len - pos + 1));
                --pos;
                --len;
            }
            break;
        case K_DEL:
            if (pos < len) {
                memmove(buf + pos, buf + pos + 1, (size_t)(len - pos));
                --len;
            }
            break;
        default:
            if ((key & 0xFF) >= 0x20 && (key & 0xFF) < 0x7F && len < max) {
                memmove(buf + pos + 1, buf + pos, (size_t)(len - pos + 1));
                buf[pos++] = (char)(key & 0xFF);
                ++len;
            }
            break;
        }
    }
}
#endif

/* A question answered with one key; returns the key's lower-case letter, or 0 for Esc. */
static int ask_key(const char *question)
{
    unsigned key;

    put_row(ROW_KEYS, question, ATTR_BRIGHT);
    scr_dump_to_env("XTDUMPEDIT");                   /* test hook: the screen with the question */
    scr_flush_keys();
    key = scr_getkey();
    message[0] = '\0';
    if (key == K_ESC) {
        return 0;
    }
    return tolower((int)(key & 0xFF));
}

/* --- files --- */

static int file_exists(const char *p)
{
    FILE *f = fopen(p, "rb");

    if (f == NULL) {
        return 0;
    }
    fclose(f);
    return 1;
}

/* Reads a DOS file into out (outmax bytes) as text; returns its length, or -1 (message set). */
static long read_text(const char *p, char *out, unsigned outmax)
{
    FILE *f = fopen(p, "rb");
    long size;
    unsigned char *raw;
    unsigned n;
    int toobig;

    if (f == NULL) {
        return -1;
    }
    if (fseek(f, 0L, SEEK_END) != 0 || (size = ftell(f)) < 0 || fseek(f, 0L, SEEK_SET) != 0) {
        fclose(f);
        say("Read error.", 1);
        return -1;
    }
    if ((unsigned long)size > 2UL * outmax) {
        fclose(f);
        say("That file is too big for the editor (16 KB).", 1);
        return -1;
    }
    raw = (unsigned char *)malloc((size_t)size + 1);
    if (raw == NULL) {
        fclose(f);
        say("Not enough memory.", 1);
        return -1;
    }
    if ((long)fread(raw, 1, (size_t)size, f) != size || ferror(f)) {
        fclose(f);
        free(raw);
        say("Read error.", 1);
        return -1;
    }
    fclose(f);
    n = ws_import(raw, (unsigned)size, out, outmax, &toobig);
    free(raw);
    if (toobig) {
        say("That file is too big for the editor (16 KB).", 1);
        return -1;
    }
    return (long)n;
}

/* Writes the text through a temporary file; an existing file is replaced only when the new one is complete. */
static int save_file(void)
{
    char piece[CHUNK], out[2 * CHUNK];
    FILE *f;
    unsigned pos = 0, len = ws_len(&text), n, m;
    int had = file_exists(path);

    f = fopen("MSTMP.$$$", "wb");
    if (f == NULL) {
        say("Cannot create the file (disk full, write-protected?).", 1);
        return 0;
    }
    while (pos < len) {
        n = len - pos < CHUNK ? len - pos : CHUNK;
        ws_copy(&text, pos, pos + n, piece);
        m = ws_export(piece, n, out);
        if (fwrite(out, 1, m, f) != m) {
            fclose(f);
            remove("MSTMP.$$$");
            say("Write error (disk full?): not saved.", 1);
            return 0;
        }
        pos += n;
    }
    if (fclose(f) != 0) {
        remove("MSTMP.$$$");
        say("Write error (disk full?): not saved.", 1);
        return 0;
    }
    if (had) {
        remove("MSBAK.$$$");
        if (rename(path, "MSBAK.$$$") != 0) {
            remove("MSTMP.$$$");
            say("Cannot replace the old file (read-only?): not saved.", 1);
            return 0;
        }
    }
    if (rename("MSTMP.$$$", path) != 0) {
        if (had) {
            rename("MSBAK.$$$", path);
        }
        remove("MSTMP.$$$");
        say("Cannot put the file in place: not saved.", 1);
        return 0;
    }
    if (had) {
        remove("MSBAK.$$$");
    }
    dirty = 0;
    return 1;
}

/* F7: the text under another name (the new name is the file being edited from then on). */
static void save_as(void)
{
    char name[16], newpath[64], old[64];
    size_t n;

    strncpy(name, path, sizeof(name) - 1);
    name[sizeof(name) - 1] = '\0';
    n = strlen(name);
    if (n > 2 && name[n - 2] == '.' && name[n - 1] == 'm') {
        name[n - 2] = '\0';
    }
    if (!prompt("Save as (a name of up to 8 letters or digits; .m is added):", name, 8) || name[0] == '\0') {
        return;
    }
    editor_path(name, newpath);
    if (!ws_path_ok(newpath) || strchr(newpath, '\\') != NULL || strchr(newpath, '/') != NULL || strchr(newpath, ':') != NULL) {
        say("Not a name DOS accepts: 1 to 8 letters or digits.", 1);
        return;
    }
    if (stricmp(newpath, path) != 0 && file_exists(newpath)) {
        char question[160];

        sprintf(question, " %s is there already.  Replace it?  Y)es  N)o", newpath);
        if (ask_key(question) != 'y') {
            say("Not saved.", 0);
            return;
        }
    }
    strcpy(old, path);
    strcpy(path, newpath);
    if (save_file()) {
        sprintf(message, "Saved as %s.", path);
        message_error = 0;
    } else {
        strcpy(path, old);
    }
}

/* --- searching --- */

static void find(void)
{
    char what[sizeof(last_find)], chunk[CHUNK + sizeof(last_find)];
    unsigned len = ws_len(&text), m, pos, start, got, i, j, wrapped = 0;

    strcpy(what, last_find);
    if (!prompt("Find (Enter = search, Esc = cancel):", what, (int)sizeof(what) - 1) || what[0] == '\0') {
        return;
    }
    strcpy(last_find, what);
    m = (unsigned)strlen(what);
    for (i = 0; i < m; ++i) {
        what[i] = (char)tolower((unsigned char)what[i]);
    }
    start = text.cur < len ? text.cur + 1 : 0;
    pos = start;
    for (;;) {
        unsigned stop = wrapped ? (start + m - 1 < len ? start + m - 1 : len) : len;

        if (pos >= stop) {
            if (wrapped) {
                say("Not found.", 1);
                return;
            }
            wrapped = 1;
            pos = 0;
            continue;
        }
        got = stop - pos < CHUNK + m - 1 ? stop - pos : CHUNK + m - 1;
        ws_copy(&text, pos, pos + got, chunk);
        for (i = 0; i + m <= got && i < CHUNK; ++i) {
            for (j = 0; j < m && tolower((unsigned char)chunk[i + j]) == what[j]; ++j) {
            }
            if (j == m) {
                ws_goto(&text, pos + i);
                say(wrapped ? "Found (from the top)." : "Found.", 0);
                return;
            }
        }
        pos += CHUNK;
    }
}

static void go_to(void)
{
    char buf[8];
    unsigned line;

    buf[0] = '\0';
    if (!prompt("Go to line:", buf, 6) || buf[0] == '\0') {
        return;
    }
    line = (unsigned)atoi(buf);
    if (line == 0) {
        say("A line number, 1 or more.", 1);
        return;
    }
    goto_line(line);
}

/* --- the assistant's results --- */

#ifndef MS_NO_AI
static void take_snapshot(void)
{
    if (snapshot == NULL) {
        snapshot = (char *)malloc(TEXT_MAX);
    }
    if (snapshot != NULL) {
        snapshot_len = ws_len(&text);
        ws_copy(&text, 0, snapshot_len, snapshot);
    }
}
#endif

static void undo(void)
{
    if (snapshot == NULL) {
        say("Nothing to undo: Alt+Z takes back the assistant's last change.", 0);
        return;
    }
    ws_set(&text, snapshot, snapshot_len);
    free(snapshot);
    snapshot = NULL;
    forget_lines();
    dirty = 1;
    say("The assistant's change was taken back.", 0);
}

#ifndef MS_NO_AI
/* ASK.M (the assistant's answer) as text in a new buffer, or NULL (message set). */
static char *read_answer(long *n)
{
    char *buf = (char *)malloc(TEXT_MAX);

    if (buf == NULL) {
        say("Not enough memory.", 1);
        return NULL;
    }
    *n = read_text("ask.m", buf, TEXT_MAX);
    if (*n < 0) {
        if (message[0] == '\0') {
            say("ask.m is not there: the assistant gave nothing.", 1);
        }
        free(buf);
        return NULL;
    }
    return buf;
}

static void apply_replace(void)
{
    long n;
    char *answer = read_answer(&n);

    if (answer == NULL) {
        return;
    }
    draw();
    if (ask_key(" Replace the text with the assistant's version (ask.m)?  Y)es  N)o") == 'y') {
        take_snapshot();
        ws_set(&text, answer, (unsigned)n);
        forget_lines();
        dirty = 1;
        say("The new version is in. Alt+Z takes it back; F2 saves.", 0);
    } else {
        say("The text is unchanged (the assistant's version is still in ask.m).", 0);
    }
    free(answer);
}

static void apply_insert(void)
{
    long n, i;
    char *answer = read_answer(&n);
    unsigned start;

    if (answer == NULL) {
        return;
    }
    take_snapshot();
    start = text.cur;
    while (start > 0 && ws_at(&text, start - 1) != '\n') {
        --start;
    }
    ws_goto(&text, start);
    for (i = 0; i < n; ++i) {
        if (!ws_insert(&text, answer[i])) {
            say("The text is full: the new code was cut. Alt+Z takes it back.", 1);
            break;
        }
    }
    if (n > 0 && answer[n - 1] != '\n') {
        ws_insert(&text, '\n');
    }
    forget_lines();
    dirty = 1;
    if (message[0] == '\0') {
        say("The new code is in. Alt+Z takes it back; F2 saves.", 0);
    }
    free(answer);
}
#endif

#ifdef MS_NO_AI
/* F6: there is no assistant in this version. */
static int assistant(EdRequest *r)
{
    (void)r;
    say("The assistant is not in this version of MathSmith.", 1);
    return -2;
}
#else
/* F6: the assistant. Returns an ED_ result, or -2 to go on editing. */
static int assistant(EdRequest *r)
{
    int k;

    k = ask_key(" Assistant:  E)xplain  F)ix  C)hange as I ask  W)rite code here  (Esc: back)");
    if (k != 'e' && k != 'f' && k != 'c' && k != 'w') {
        return -2;
    }
#ifdef MS_486
    if (ws_len(&text) > 3980 && k != 'w') {
        say("The assistant reads 3980 characters at most: this script is longer.", 1);
#else
    if (ws_len(&text) > 4000 && k != 'w') {
        say("The assistant reads 4000 characters at most: this script is longer.", 1);
#endif
        return -2;
    }
    r->instruction[0] = '\0';
    if (k == 'f') {
        strncpy(r->instruction, r->error, sizeof(r->instruction) - 1);
        if (!prompt_long(" What is wrong?  (Enter with nothing: find the mistakes yourself)", r->instruction, 199)) {
            return -2;
        }
    } else if (k == 'c') {
        if (!prompt_long(" Change how?  (for example: make it print the result)", r->instruction, 199) || r->instruction[0] == '\0') {
            return -2;
        }
    } else if (k == 'w') {
        if (!prompt_long(" Write what?  (describe the code: it goes in at the cursor's line)", r->instruction, 199) || r->instruction[0] == '\0') {
            return -2;
        }
    }
    if (dirty && !save_file()) {
        return -2;
    }
    return k == 'e' ? ED_EXPLAIN : k == 'f' ? ED_FIX : k == 'c' ? ED_CHANGE : ED_WRITE;
}
#endif

/* --- the main loop --- */

/* Returns an ED_ result, or -2 to go on. */
static int deny(void)
{
    say("Read only: this text cannot be changed.", 1);
    return -2;
}

#ifdef MS_486
/* F8: another script, chosen from the list of this folder (XD88). Returns ED_OPEN (r->instruction is its name) or -2 to go on editing. */
static int open_other(EdRequest *r)
{
    char name[16];
    int k, pick;

    if (readonly) {
        return -2;
    }
    pick = pick_script("Open which script?", name, sizeof(name));
    repaint = 1;
    if (pick < 0) {
        say("There are no .m files in this folder.", 0);
    }
    if (pick != 1) {
        return -2;
    }
    if (dirty) {
        k = ask_key(" The script was changed.  S)ave and open the other  D)iscard the changes  (Esc: stay)");
        if (k == 's') {
            if (!save_file()) {
                return -2;
            }
        } else if (k != 'd') {
            return -2;
        }
    }
    strcpy(r->instruction, name);
    return ED_OPEN;
}
#endif

static int handle(unsigned key, EdRequest *r)
{
    char ch;

    message[0] = '\0';
    if (readonly) {
        switch (key) {
        case K_F2: case K_F5: case K_F6: case K_F7: case K_ALT_Z: case K_ENTER: case K_TAB: case K_BACKSPACE: case K_DEL:
            return deny();
        case 0x3B00:                                   /* F1 closes the help, as it opened it */
            return ED_QUIT;
        case K_PGDN:                                   /* a page is a page of the screen, not of the cursor */
            ws_goto(&text, text.top);
            ws_page_down(&text);
            ws_page_down(&text);
            return -2;
        case K_PGUP:
            ws_goto(&text, text.top);
            ws_page_up(&text);
            return -2;
        default:
            if ((key & 0xFF) >= 0x20 && (key & 0xFF) < 0x7F) {
                return deny();
            }
            break;
        }
    }
    switch (key) {
    case K_F2:
        if (save_file()) {
            say("Saved.", 0);
        }
        return -2;
    case K_F7: save_as(); return -2;
#ifdef MS_486
    case K_F8: return open_other(r);
#endif
    case K_F3: find(); return -2;
    case K_F4: go_to(); return -2;
    case K_F5:
        if (dirty && !save_file()) {
            return -2;
        }
        return ED_RUN;
    case K_F6: return assistant(r);
    case K_ALT_Z: undo(); return -2;
    case K_ESC:
    case K_F10:
        if (dirty) {
            int k = ask_key(" The script was changed.  S)ave and leave  D)iscard the changes  (Esc: stay)");

            if (k == 's') {
                return save_file() ? ED_QUIT : -2;
            }
            return k == 'd' ? ED_QUIT : -2;
        }
        return ED_QUIT;
    case K_ENTER: new_line(); return -2;
    case K_TAB: tab(); return -2;
    case K_BACKSPACE: backspace(); return -2;
    case K_DEL: delete_char(); return -2;
    case K_LEFT: ws_left(&text); return -2;
    case K_RIGHT: ws_right(&text); return -2;
    case K_UP: ws_up(&text); return -2;
    case K_DOWN: ws_down(&text); return -2;
    case K_HOME: ws_home(&text); return -2;
    case K_END: ws_end(&text); return -2;
    case K_CTRL_LEFT: ws_word_left(&text); return -2;
    case K_CTRL_RIGHT: ws_word_right(&text); return -2;
    case K_PGUP: ws_page_up(&text); return -2;
    case K_PGDN: ws_page_down(&text); return -2;
    case K_CTRL_HOME: ws_text_start(&text); return -2;
    case K_CTRL_END: ws_text_end(&text); return -2;
    default:
        ch = (char)(key & 0xFF);
        if (ch >= 0x20 && ch < 0x7F) {
            type_char(ch);
        }
        return -2;
    }
}

void editor_path(const char *name, char *out)
{
    size_t n = strlen(name), i;

    strncpy(out, name, 60);
    out[60] = '\0';
    n = strlen(out);
    if (!(n > 2 && out[n - 2] == '.' && (out[n - 1] == 'm' || out[n - 1] == 'M'))) {
        strcat(out, ".m");
    }
    for (i = 0; out[i] != '\0'; ++i) {
        out[i] = (char)tolower((unsigned char)out[i]);
    }
}

int editor_run(EdRequest *r)
{
    char *buf;
    long n;
    int result;
    int existed;

    readonly = 0;
    line_width = LINE_MAX;
    if (!ws_path_ok(r->path)) {
        strcpy(r->message, "not a name DOS accepts");
        return ED_FAILED;
    }
    lines = (char *)malloc((size_t)ED_ROWS * (LINE_MAX + 1));
    buf = (char *)malloc(TEXT_MAX);
    snapshot = NULL;
    if (lines == NULL || buf == NULL) {
        free(lines);
        free(buf);
        strcpy(r->message, "Not enough memory for the editor.");
        return ED_FAILED;
    }
    message[0] = '\0';
    message_error = 0;
    strcpy(path, r->path);
    ws_init(&text, buf, TEXT_MAX, ED_ROWS, LINE_MAX);
    existed = file_exists(path);
    if (existed) {
        n = read_text(path, buf, TEXT_MAX);
        if (n < 0) {
            strcpy(r->message, message);
            free(lines);
            free(buf);
            return ED_FAILED;
        }
        ws_set(&text, buf, (unsigned)n);
    }
    dirty = 0;
    hscroll = 0;
    last_find[0] = '\0';
    forget_lines();
    if (r->line > 1) {
        goto_line(r->line);
    } else {
        ws_text_start(&text);
    }
    if (r->message[0] != '\0') {
        say(r->message, 1);
    } else if (!existed) {
        say("New file.", 0);
    }
#ifndef MS_NO_AI
    if (r->apply == EDA_REPLACE) {
        apply_replace();
    } else if (r->apply == EDA_INSERT) {
        apply_insert();
    }
#endif
    scr_flush_keys();
    for (;;) {
        draw();
        scr_dump_to_env("XTDUMPEDIT");               /* test hook: the screen before each key */
        result = handle(scr_getkey(), r);
        if (result != -2) {
            break;
        }
    }
    r->line = cursor_line();
    strcpy(r->path, path);                             /* F7 may have changed the name */
    free(lines);
    free(buf);
    free(snapshot);
    snapshot = NULL;
    _heapshrink();                                   /* the editor's memory goes back to DOS: MSASK.EXE needs it */
    return result;
}

/* A text to read, not to change (the help): wrapped at 78 columns, the arrows, PgUp and PgDn, find and go to line work; Esc, F10 or F1 close it. */
void editor_view(const char *title, const char *content)
{
    char *buf;
    unsigned n = (unsigned)strlen(content), i;
    EdRequest none;

    if (n > TEXT_MAX) {
        n = TEXT_MAX;
    }
    readonly = 1;
    line_width = 78;
    lines = (char *)malloc((size_t)ED_ROWS * (line_width + 1));
    buf = (char *)malloc(TEXT_MAX);
    snapshot = NULL;
    if (lines == NULL || buf == NULL) {
        free(lines);
        free(buf);
        readonly = 0;
        return;
    }
    memset(&none, 0, sizeof(none));
    message[0] = '\0';
    message_error = 0;
    view_title = title;
    view_lines = 1;
    for (i = 0; i < n; ++i) {
        if (content[i] == '\n' && i + 1 < n) {
            ++view_lines;
        }
    }
    ws_init(&text, buf, TEXT_MAX, ED_ROWS, line_width);
    ws_set(&text, content, n);
    ws_text_start(&text);
    dirty = 0;
    hscroll = 0;
    last_find[0] = '\0';
    forget_lines();
    scr_flush_keys();
    for (;;) {
        draw();
        scr_dump_to_env("XTDUMPEDIT");
        if (handle(scr_getkey(), &none) != -2) {
            break;
        }
    }
    free(lines);
    free(buf);
    readonly = 0;
    line_width = LINE_MAX;
    _heapshrink();
}
