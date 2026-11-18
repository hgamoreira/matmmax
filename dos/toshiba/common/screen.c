#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <bios.h>
#include <dos.h>
#include <i86.h>
#include "screen.h"

static unsigned short __far *screen;
static unsigned char saved_mode;
static unsigned short saved_cursor_shape;
static unsigned short *back;            /* the frame being drawn, while framing */
static int framing;
unsigned long scr_cells_written;

/* Where drawing goes: the frame in memory, or the screen itself. */
static unsigned short __far *canvas(void)
{
    return framing ? (unsigned short __far *)back : screen;
}

void scr_begin_frame(void)
{
    if (back == NULL) {
        back = (unsigned short *)malloc(SCR_ROWS * SCR_COLS * sizeof back[0]);
    }
    if (back != NULL) {
        _fmemcpy((void __far *)back, screen, SCR_ROWS * SCR_COLS * sizeof back[0]);
        framing = 1;
    }
}

void scr_end_frame(void)
{
    int row;

    if (!framing) {
        return;
    }
    framing = 0;
    for (row = 0; row < SCR_ROWS; ++row) {
        unsigned short __far *shown = screen + row * SCR_COLS;
        unsigned short __far *drawn = (unsigned short __far *)back + row * SCR_COLS;

        if (_fmemcmp(shown, drawn, SCR_COLS * sizeof shown[0]) != 0) {
            _fmemcpy(shown, drawn, SCR_COLS * sizeof shown[0]);
            scr_cells_written += SCR_COLS;
        }
    }
}

/* DOS restores INT 23h from the PSP when the program ends, so this only
   needs installing: Ctrl+Break must not skip the screen restore. */
static void __interrupt __far ignore_ctrl_break(void)
{
}

/* INT 11h equipment bits 4-5 = 3: monochrome adapter (see apps/00_smoke
   for why this and not the video mode number). */
static int monochrome_adapter(void)
{
    union REGS r;
    int86(0x11, &r, &r);
    return ((r.x.ax >> 4) & 3) == 3;
}

void scr_flush_keys(void)
{
    while (_bios_keybrd(_KEYBRD_READY)) {
        _bios_keybrd(_KEYBRD_READ);
    }
}

int scr_init(void)
{
    union REGS r;
    int mono;

    r.h.ah = 0x0F;
    int86(0x10, &r, &r);
    saved_mode = r.h.al & 0x7F;
    mono = monochrome_adapter() || saved_mode == 7;
    if (r.h.ah != SCR_COLS || (!mono && saved_mode != 2 && saved_mode != 3)) {
        printf("This program needs an 80-column text mode (current mode %u).\n", saved_mode);
        return 1;
    }
    screen = (unsigned short __far *)MK_FP(mono ? 0xB000 : 0xB800, 0);

    _dos_setvect(0x23, ignore_ctrl_break);
    r.h.ah = 0x03;
    r.h.bh = 0;
    int86(0x10, &r, &r);
    saved_cursor_shape = r.x.cx;

    scr_flush_keys();
    return 0;
}

int scr_ready(void)
{
    return screen != NULL;
}

void scr_done(void)
{
    union REGS r;
    r.h.ah = 0x00;
    r.h.al = saved_mode;
    int86(0x10, &r, &r);
    r.h.ah = 0x01;
    r.x.cx = saved_cursor_shape;
    int86(0x10, &r, &r);
}

void scr_clear_row(int row, unsigned char attr)
{
    unsigned short __far *cell = canvas() + row * SCR_COLS;
    int col;
    for (col = 0; col < SCR_COLS; ++col) {
        *cell++ = ((unsigned short)attr << 8) | ' ';
    }
    if (!framing) {
        scr_cells_written += SCR_COLS;
    }
}

void scr_clear(void)
{
    int row;
    for (row = 0; row < SCR_ROWS; ++row) {
        scr_clear_row(row, ATTR_NORMAL);
    }
}

void scr_put(int row, int col, const char *text, unsigned char attr)
{
    unsigned short __far *cell = canvas() + row * SCR_COLS + col;
    unsigned short hi = (unsigned short)attr << 8;
    int room = SCR_COLS - col, written = 0;

    while (written < room && text[written] != '\0') {
        *cell++ = hi | (unsigned char)text[written];
        ++written;
    }
    if (!framing) {
        scr_cells_written += (unsigned long)written;
    }
}

void scr_move_rows(int from, int to, int count)
{
    _fmemmove(canvas() + to * SCR_COLS, canvas() + from * SCR_COLS, (size_t)count * SCR_COLS * sizeof(unsigned short));
    if (!framing) {
        scr_cells_written += (unsigned long)count * SCR_COLS;
    }
}

void scr_put_centred(int row, const char *text, unsigned char attr)
{
    scr_put(row, (SCR_COLS - (int)strlen(text)) / 2, text, attr);
}

void scr_cursor(int row, int col)
{
    union REGS r;
    r.h.ah = 0x02;
    r.h.bh = 0;
    r.h.dh = (unsigned char)row;
    r.h.dl = (unsigned char)col;
    int86(0x10, &r, &r);
}

/* Off-screen rather than a "disable" bit in the cursor shape: MDA's
   6845 setup does not honour that bit reliably. */
void scr_hide_cursor(void)
{
    scr_cursor(SCR_ROWS, 0);
}

/* Test hook: when the environment variable XTKEYS names a file, its words are keys delivered before the real
   keyboard is looked at again: four hex digits are a BIOS key code (scan << 8 | ascii; Alt+A is 1E00), "-"
   is "no key" for one scr_poll_key call (scr_getkey skips it). DOSBox-X's AUTOTYPE cannot type Alt combinations
   or time a key press; this can. */
#define INJECT_MAX 1024
static unsigned inject_keys[INJECT_MAX];
static int inject_count, inject_next, inject_loaded;

#ifdef XTKEYS_RUNS
/* CodeSmith for the XT (-DXTKEYS_RUNS, as common/vgascreen.c has it): the file may hold the keys of several runs of the program, separated
   by the word "==": the run that takes them is counted in the file named by XTKEYSN (CodeSmith restarts itself through a batch file between builds). */
static int inject_run(void)
{
    const char *path = getenv("XTKEYSN");
    FILE *f;
    int n = 0;

    if (path == NULL) {
        return 0;
    }
    if ((f = fopen(path, "r")) != NULL) {
        if (fscanf(f, "%d", &n) != 1) {
            n = 0;
        }
        fclose(f);
    }
    if ((f = fopen(path, "w")) != NULL) {
        fprintf(f, "%d\n", n + 1);
        fclose(f);
    }
    return n;
}
#endif

static void inject_load(void)
{
    const char *path = getenv("XTKEYS");
    FILE *f;
    int c, digits = 0;
    unsigned value = 0;
#ifdef XTKEYS_RUNS
    int run = 0, mine;
#endif

    inject_loaded = 1;
    if (path == NULL || (f = fopen(path, "rb")) == NULL) {
        return;
    }
#ifdef XTKEYS_RUNS
    mine = inject_run();
#endif
    do {                                   /* words of hex digits or "-", separated by anything else */
        c = fgetc(f);
#ifdef XTKEYS_RUNS
        if (c == '=') {                    /* "==" starts the next run */
            if ((c = fgetc(f)) == '=') {
                ++run;
            }
            digits = 0;
            value = 0;
            continue;
        }
#endif
        if (c == '-' && digits == 0) {
            if (
#ifdef XTKEYS_RUNS
                run == mine &&
#endif
                inject_count < INJECT_MAX) {
                inject_keys[inject_count++] = 0;
            }
        } else if ((c >= '0' && c <= '9') || ((c | 0x20) >= 'a' && (c | 0x20) <= 'f')) {
            value = (value << 4) | (unsigned)(c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10);
            ++digits;
        } else {
            if (digits > 0 &&
#ifdef XTKEYS_RUNS
                run == mine &&
#endif
                inject_count < INJECT_MAX) {
                inject_keys[inject_count++] = value;
            }
            digits = 0;
            value = 0;
        }
    } while (c != EOF);
    fclose(f);
}

/* 1 and *key set (0 for "no key") if an injected word is left. */
static int inject_take(unsigned *key)
{
    if (!inject_loaded) {
        inject_load();
    }
    if (inject_next >= inject_count) {
        return 0;
    }
    *key = inject_keys[inject_next++];
    return 1;
}

unsigned scr_getkey(void)
{
    unsigned key;

    while (inject_take(&key)) {
        if (key != 0) {
            return key;
        }
    }
    return _bios_keybrd(_KEYBRD_READ);
}

unsigned scr_poll_key(void)
{
    unsigned key;

    if (inject_take(&key)) {
        return key;
    }
    return _bios_keybrd(_KEYBRD_READY) ? _bios_keybrd(_KEYBRD_READ) : 0;
}

static int input_loop(int row, int col, char *buf, int max, unsigned char attr, unsigned stop_key)
{
    int len = (int)strlen(buf);
    unsigned key;
    int i;

    for (;;) {
        for (i = 0; i < max; ++i) {
            scr_put(row, col + i, " ", attr);
        }
        scr_put(row, col, buf, attr);
        scr_cursor(row, col + len);
        key = scr_getkey();
        if (key == KEY_ENTER) {
            scr_hide_cursor();
            return 1;
        }
        if (key == KEY_ESC) {
            scr_hide_cursor();
            return 0;
        }
        if (stop_key != 0 && key == stop_key) {
            scr_hide_cursor();
            return 2;
        }
        if ((key & 0xFF) == 0x08) {
            if (len > 0) {
                buf[--len] = '\0';
            }
        } else if ((key & 0xFF) >= 0x20 && (key & 0xFF) < 0x7F && len < max) {
            buf[len++] = (char)(key & 0xFF);
            buf[len] = '\0';
        }
    }
}

int scr_input(int row, int col, char *buf, int max, unsigned char attr)
{
    return input_loop(row, col, buf, max, attr, 0);
}

int scr_input_stop(int row, int col, char *buf, int max, unsigned char attr, unsigned stop_key)
{
    return input_loop(row, col, buf, max, attr, stop_key);
}

void scr_dump_to_env(const char *variable)
{
    static unsigned short copy[SCR_ROWS * SCR_COLS];
    const char *path = getenv(variable);
    FILE *f;
    if (path == NULL) {
        return;
    }
    _fmemcpy(copy, screen, sizeof copy);
    f = fopen(path, "wb");
    if (f != NULL) {
        fwrite(copy, 1, sizeof copy, f);
        fclose(f);
    }
}

void scr_dump_if_requested(void)
{
    scr_dump_to_env("XTDUMP");
}
