/* MathSmith M08: the full-screen front end. A status line, the history of what was typed and answered, the line being typed, a row of keys.
   DOS only (it uses the screen layer of the other programs). When the input is redirected from a file, repl.c does the line-by-line mode. */

#include <bios.h>
#include <dos.h>
#include <io.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "editor.h"
#include "engine.h"
#include "mat.h"
#include "plotdos.h"
#include "msattr.h"
#include "screen.h"
#include "ui.h"
#include "version.h"
#ifndef MS_NO_AI
#include "askdos.h"
#endif
#ifdef MS_486
#include "picker.h"
#endif

#ifdef MS_486
#define HIST_MAX 1000                           /* the 486 has the memory; MSASK.EXE runs in the conventional memory, which the extender leaves free */
#else
#define HIST_MAX 160                            /* 82 bytes a line: the memory is needed by MSASK.EXE (XD79) */
#endif
#define TEXT_ROWS (SCR_ROWS - 3)                /* rows 1..22 on 25 rows, 1..47 on the VGA's 50 */
#define ROW_STATUS 0
#define ROW_INPUT (SCR_ROWS - 2)
#define ROW_KEYS (SCR_ROWS - 1)
#define EDIT_MAX 250
#define RECALL_MAX 24
#define PROMPT_W 3

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
#define K_F1 0x3B00
#define K_F2 0x3C00
#define K_F3 0x3D00
#define K_F4 0x3E00
#define K_F5 0x3F00
#define K_F6 0x4000
#define K_F10 0x4400

typedef struct {
    char text[SCR_COLS + 1];
    unsigned char attr;
} HLine;

static HLine *hist;
static int hhead, hcount;                       /* the oldest line, and how many there are */
static int back;                                /* lines scrolled back from the bottom (0: the newest is in view) */
static char partial[SCR_COLS + 1];              /* an output line not finished yet */
static int plen;
static unsigned char partial_attr;
static MlSession *sess;
static int more;                                /* a block is open: the prompt shows three spaces */
static char status_note[40];                    /* "working ..." or the time of the last line */
static char editbuf[EDIT_MAX + 1];
static int editlen, editpos, editscroll;
static char *recall[RECALL_MAX];
static int nrecall;
static clock_t last_draw;

static void redraw(void);
#ifdef MS_486
static char prefill[80];                        /* put in the line being typed when read_line starts (a command that was completed by the picker) */
#endif

static void hist_push(const char *text, unsigned char attr)
{
    int slot;

    if (hcount < HIST_MAX) {
        slot = (hhead + hcount) % HIST_MAX;
        ++hcount;
    } else {
        slot = hhead;
        hhead = (hhead + 1) % HIST_MAX;
    }
    strncpy(hist[slot].text, text, SCR_COLS);
    hist[slot].text[SCR_COLS] = '\0';
    hist[slot].attr = attr;
}

static void flush_partial(void)
{
    partial[plen] = '\0';
    hist_push(partial, partial_attr);
    plen = 0;
}

/* What the engine prints arrives here a piece at a time. */
static void ui_sink(void *ctx, const char *text)
{
    (void)ctx;
    for (; *text != '\0'; ++text) {
        char c = *text;

        if (c == '\f') {
            hcount = 0;
            hhead = 0;
            plen = 0;
            continue;
        }
        if (c == '\r') {
            continue;
        }
        if (c == '\n') {
            flush_partial();
            continue;
        }
        if (plen == 0) {
            partial_attr = ATTR_NORMAL;
        }
        if (c == '\t') {
            c = ' ';
        }
        partial[plen++] = c;
        if (plen == 5 && strncmp(partial, "error", 5) == 0) {
            partial_attr = ATTR_ERRLINE;
        }
        if (plen == 7 && strncmp(partial, "warning", 7) == 0) {
            partial_attr = ATTR_ERRLINE;
        }
        if (plen >= SCR_COLS - 1) {
            flush_partial();
        }
    }
    back = 0;
    if (clock() - last_draw > CLOCKS_PER_SEC / 5) {        /* show progress now and then while a long line works */
        redraw();
    }
}

#ifdef __386__
/* The largest block the DOS extender can still give, in KB (DPMI function 0500h: the first doubleword of the buffer). */
static unsigned free_kb(void)
{
    union REGS r;
    struct SREGS sr;
    unsigned long info[12];

    memset(info, 0xFF, sizeof info);
    segread(&sr);
    r.w.ax = 0x0500;
    r.x.edi = (unsigned long)info;
    int386x(0x31, &r, &r, &sr);
    return (unsigned)(info[0] / 1024UL);
}
#else
static unsigned free_kb(void)
{
    unsigned seg = 0;

    _dos_allocmem(0xFFFF, &seg);                        /* fails, and says how many paragraphs are free */
    return (unsigned)(seg / 64);
}
#endif

static void draw_status(void)
{
    char line[SCR_COLS + 1], note[48];
    int nvar, nfunc, digits;

    ml_info(sess, &nvar, &nfunc, &digits);
    sprintf(note, "%s", status_note);
    sprintf(line, " MathSmith " MS_VERSION "   vars %d  funcs %d  mem %uK  %s  %.30s", nvar, nfunc, free_kb(), digits == 4 ? "short" : "long", note);
    scr_clear_row(ROW_STATUS, ATTR_REVERSE);
    scr_put(ROW_STATUS, 0, line, ATTR_REVERSE);
}

static void draw_history(void)
{
    int total = hcount + (plen > 0 ? 1 : 0), first, r;
    char line[SCR_COLS + 1];

    if (back > total - TEXT_ROWS) {
        back = total - TEXT_ROWS;
    }
    if (back < 0) {
        back = 0;
    }
    first = total - back - TEXT_ROWS;
    for (r = 0; r < TEXT_ROWS; ++r) {
        int idx = first + r;

        scr_clear_row(1 + r, ATTR_NORMAL);
        if (idx < 0 || idx >= total) {
            continue;
        }
        if (idx < hcount) {
            HLine *h = &hist[(hhead + idx) % HIST_MAX];

            scr_put(1 + r, 0, h->text, h->attr);
        } else {
            partial[plen] = '\0';
            scr_put(1 + r, 0, partial, partial_attr);
        }
    }
    if (back > 0) {
        sprintf(line, " [%d lines back] ", back);
        scr_put(1, SCR_COLS - (int)strlen(line), line, ATTR_REVERSE);
    }
}

/* How many screen rows the line being typed needs: it wraps at the edge of the screen and grows upwards over the bottom of the history. */
static int input_rows(void)
{
    return (PROMPT_W + editlen) / SCR_COLS + 1;
}

static int drawn_rows = 1;                       /* the rows the last draw_input used: when it changes, the history under them needs drawing again */

static void draw_input(void)
{
    char line[SCR_COLS + 1];
    int rows = input_rows(), first = ROW_INPUT - (rows - 1), r, from, n, width;

    for (r = 0; r < rows; ++r) {
        width = r == 0 ? SCR_COLS - PROMPT_W : SCR_COLS;
        from = r == 0 ? 0 : r * SCR_COLS - PROMPT_W;       /* the first character of the line shown on this row */
        scr_clear_row(first + r, ATTR_NORMAL);
        if (r == 0) {
            scr_put(first, 0, more ? "   " : ">> ", ATTR_ECHO);
        }
        n = editlen - from;
        if (n > width) {
            n = width;
        }
        if (n > 0) {
            memcpy(line, editbuf + from, (size_t)n);
            line[n] = '\0';
            scr_put(first + r, r == 0 ? PROMPT_W : 0, line, ATTR_ECHO);
        }
    }
    scr_cursor(first + (PROMPT_W + editpos) / SCR_COLS, (PROMPT_W + editpos) % SCR_COLS);
    drawn_rows = rows;
}

static void draw_keys(void)
{
    scr_clear_row(ROW_KEYS, ATTR_KEYS);
#ifdef MS_NO_AI
    scr_put(ROW_KEYS, 0, " F1 Help F2 Vars F3 Run F4 Clear F5 Plot PgUp/Dn Up/Dn Recall Esc Stop F10 Quit", ATTR_KEYS);
#else
    scr_put(ROW_KEYS, 0, " F1 Help F2 Vars F3 Run F4 Clear F5 Plot F6 Why PgUp/Dn Up/Dn Recall F10 Quit", ATTR_KEYS);
#endif
}

static void redraw(void)
{
    scr_begin_frame();
    draw_status();
    draw_history();
    draw_input();
    draw_keys();
    scr_end_frame();
    last_draw = clock();
}

/* While a line is being worked on: Esc stops it. Other keys typed meanwhile are dropped. It looks at the real keyboard only: the keys a test
   injects (XTKEYS) are for the editing loop, not for this. */
static int ui_poll(void)
{
    while (_bios_keybrd(_KEYBRD_READY)) {
        if ((_bios_keybrd(_KEYBRD_READ) & 0xFF) == 27) {
            return 1;
        }
    }
    return 0;
}

/* A plot is ready: show it full screen, then come back to the text screen (the history gets a note, or the reason it could not be shown). */
static int ui_plot(void *ctx, const PlotSpec *spec)
{
    char note[SCR_COLS + 1];

    (void)ctx;
    if (plotdos_show(spec) == 0) {
        sprintf(note, "(plot shown: %d curve%s; F5 shows it again)", spec->nc, spec->nc == 1 ? "" : "s");
        hist_push(note, ATTR_NORMAL);
    } else {
        hist_push("error: this display cannot show graphics (it needs the CGA 640x200 mode)", ATTR_ERRLINE);
    }
    return 0;
}

#ifndef MS_NO_AI
/* The assistant needs the whole console for the helper's messages: the screen is cleared for it, and redrawn by the next read_line. */
static int ui_ask(void *ctx, int mode)
{
    int rc;

    scr_clear();
    scr_cursor(0, 0);
    printf("MathSmith assistant (MSASK.EXE)\n\n");
    fflush(stdout);
    rc = askdos_run(ctx, mode);
    scr_flush_keys();                                    /* whatever was typed while it worked */
    return rc;
}
#endif

static void remember(const char *line)
{
    if (line[0] == '\0' || (nrecall > 0 && strcmp(recall[nrecall - 1], line) == 0)) {
        return;
    }
    if (nrecall == RECALL_MAX) {
        free(recall[0]);
        memmove(recall, recall + 1, (RECALL_MAX - 1) * sizeof(char *));
        --nrecall;
    }
    recall[nrecall] = (char *)malloc(strlen(line) + 1);
    if (recall[nrecall] != NULL) {
        strcpy(recall[nrecall], line);
        ++nrecall;
    }
}

static void set_edit(const char *text)
{
    strncpy(editbuf, text, EDIT_MAX);
    editbuf[EDIT_MAX] = '\0';
    editlen = (int)strlen(editbuf);
    editpos = editlen;
}

/* Edits a line until Enter. Returns 1 for a line, or the key code (F1 ... F6, F10) that asks for something else, as a negative number. */
static int read_line(void)
{
    int rpos = nrecall;                                  /* where Up/Down are in the remembered lines */
    int full = 1;                                        /* the whole screen needs drawing, not just the line being typed */
    unsigned key;

    editbuf[0] = '\0';
    editlen = editpos = editscroll = 0;
#ifdef MS_486
    if (prefill[0] != '\0') {
        set_edit(prefill);
        prefill[0] = '\0';
    }
#endif
    for (;;) {
        if (full || input_rows() != drawn_rows) {
            redraw();
            full = 0;
        } else {
            draw_input();                                /* a key press changes only this row: cheap on an 8086 */
        }
        key = scr_getkey();
        if (key == KEY_ENTER) {
            return 1;
        }
        if (key == K_F1 || key == K_F2 || key == K_F3 || key == K_F4 || key == K_F5 || key == K_F6 || key == K_F10) {
            return -(int)key;
        }
        switch (key) {
        case KEY_ESC:
            editbuf[0] = '\0';
            editlen = editpos = 0;
            break;
        case K_BACKSPACE:
            if (editpos > 0) {
                memmove(editbuf + editpos - 1, editbuf + editpos, (size_t)(editlen - editpos + 1));
                --editpos;
                --editlen;
            }
            break;
        case K_DEL:
            if (editpos < editlen) {
                memmove(editbuf + editpos, editbuf + editpos + 1, (size_t)(editlen - editpos));
                --editlen;
            }
            break;
        case K_LEFT:
            if (editpos > 0) {
                --editpos;
            }
            break;
        case K_RIGHT:
            if (editpos < editlen) {
                ++editpos;
            }
            break;
        case K_HOME:
            editpos = 0;
            break;
        case K_END:
            editpos = editlen;
            break;
        case K_UP:
            if (rpos > 0) {
                --rpos;
                set_edit(recall[rpos]);
            }
            break;
        case K_DOWN:
            if (rpos < nrecall) {
                ++rpos;
                if (rpos == nrecall) {
                    editbuf[0] = '\0';
                    editlen = editpos = 0;
                } else {
                    set_edit(recall[rpos]);
                }
            }
            break;
        case K_PGUP:
            back += TEXT_ROWS / 2;
            full = 1;
            continue;
        case K_PGDN:
            back -= TEXT_ROWS / 2;
            full = 1;
            continue;
        default:
            if ((key & 0xFF) >= 0x20 && (key & 0xFF) < 0x7F && editlen < EDIT_MAX) {
                memmove(editbuf + editpos + 1, editbuf + editpos, (size_t)(editlen - editpos + 1));
                editbuf[editpos++] = (char)(key & 0xFF);
                ++editlen;
            } else {
                continue;
            }
            break;
        }
        if (back != 0) {
            back = 0;
            full = 1;
        }
    }
}

static char pending_edit[40];                            /* `edit name` was typed: the editor opens when the line is done */

static void ui_edit(void *ctx, const char *name)
{
    (void)ctx;
    strncpy(pending_edit, name, sizeof(pending_edit) - 1);
    pending_edit[sizeof(pending_edit) - 1] = '\0';
}

static int run_line(const char *line);

/* After a script, an explanation or a fix: the history is on the screen; any key goes back to the editor, Esc to the prompt. Returns 1 for the editor. */
static int back_to_editor(void)
{
    unsigned key;

    strcpy(status_note, "any key: editor, Esc: prompt");
    redraw();
    scr_flush_keys();
    key = scr_getkey();
    status_note[0] = '\0';
    return key != 0x011B;
}

/* The editor, and what it asks for: it saves the file and returns, the work is done here (where the history is), and the editor is entered again
   at the same place, so that no editor buffer is in memory while MSASK.EXE runs. */
static void edit_session(const char *name)
{
    EdRequest r;
    char cmd[320], file[40], text[100], shown[160];
    int act, line, rc;

    memset(&r, 0, sizeof(r));
    editor_path(name, r.path);
    r.line = 1;
    for (;;) {
        act = editor_run(&r);
        if (act == ED_FAILED) {
            sprintf(shown, "error: cannot edit %.20s: %.50s", r.path, r.message);
            hist_push(shown, ATTR_ERRLINE);
            break;
        }
        r.apply = EDA_NONE;
        r.message[0] = '\0';
        ml_file_changed(sess, r.path);                   /* functions read from it before are read again */
        if (act == ED_QUIT) {
            break;
        }
#ifdef MS_486
        if (act == ED_OPEN) {                            /* F8: another script, chosen from the list */
            editor_path(r.instruction, r.path);
            r.line = 1;
            continue;
        }
#endif
        r.error[0] = '\0';
        if (act == ED_RUN) {
            sprintf(cmd, "run %s", r.path);
            rc = run_line(cmd);
            if (rc == ML_ERROR) {
                line = ml_last_error(sess, text, sizeof(text), file, sizeof(file));
                strncpy(r.error, text, sizeof(r.error) - 1);
                if (line > 0 && stricmp(file, r.path) == 0) {
                    r.line = (unsigned)line;
                    sprintf(r.message, "error in line %d: %.50s", line, text);
                } else if (line > 0) {
                    sprintf(r.message, "error in %.12s, line %d: %.40s", file, line, text);
                } else {
                    sprintf(r.message, "error: %.70s", text);
                }
            }
        } else if (act == ED_EXPLAIN) {
            sprintf(cmd, "explain %s", r.path);
            run_line(cmd);
        } else if (act == ED_FIX) {
            if (r.instruction[0] != '\0') {
                sprintf(cmd, "fix %s \"%s\"", r.path, r.instruction);
            } else {
                sprintf(cmd, "fix %s", r.path);
            }
            if (run_line(cmd) == ML_OK) {
                r.apply = EDA_REPLACE;
            }
        } else if (act == ED_CHANGE) {
            sprintf(cmd, "change %s \"%s\"", r.path, r.instruction);
            if (run_line(cmd) == ML_OK) {
                r.apply = EDA_REPLACE;
            }
        } else if (act == ED_WRITE) {
            sprintf(cmd, "code %s", r.instruction);
            if (run_line(cmd) == ML_OK) {
                r.apply = EDA_INSERT;
            }
        }
        if (!back_to_editor()) {
            break;
        }
    }
}

/* Runs one line of input through the engine, showing it in the history first. */
static int run_line(const char *line)
{
    char shown[SCR_COLS + 1];
    int rc;
    clock_t t0;
    double seconds;

    strcpy(shown, more ? "   " : ">> ");
    strncat(shown, line, SCR_COLS - PROMPT_W);                /* a typed line can be 250 characters: only what fits goes into the history */
    hist_push(shown, ATTR_ECHO);
    back = 0;
    if (!more && strcmp(line, "help") == 0) {              /* the help is longer than the screen: it is read in the viewer */
        editor_view("MathSmith help", ml_help_text());
        return ML_OK;
    }
    strcpy(status_note, "working... Esc stops");
    redraw();
    t0 = clock();
    rc = ml_eval(sess, line);
    seconds = (double)(clock() - t0) / (double)CLOCKS_PER_SEC;
    if (plen > 0) {
        flush_partial();                                 /* output that did not end with a new line */
    }
    more = rc == ML_MORE;
    if (seconds >= 0.5) {
        sprintf(status_note, "last %.1f s", seconds);
    } else {
        status_note[0] = '\0';
    }
    if (pending_edit[0] != '\0') {                       /* the line was `edit name` */
        char name[sizeof(pending_edit)];

        strcpy(name, pending_edit);
        pending_edit[0] = '\0';
        edit_session(name);
    }
    return rc;
}

#ifdef MS_486
static int ui_pick(void *ctx, const char *title, const char *const *items, int n, int current)
{
    (void)ctx;
    return pick_item(title, items, n, current);
}

/* `edit`, `run`, `explain`, `fix` or `change` typed with no name: the script is chosen from a list (XD88). Returns 1 if the line was one of them: then `line` is
   the whole command to run (empty if the choice was cancelled), or, for `change`, which needs the user's words as well, `prefill` holds "change name " for
   the user to finish and `line` is empty. */
static int complete_with_picker(char *line)
{
    static const char *const verbs[] = {"edit", "run", "explain", "fix", "change"};
    static const char *const titles[] = {"Edit which script?", "Run which script?", "Explain which script?", "Fix which script?", "Change which script?"};
    const char *p = line;
    char name[16];
    size_t i;
    int pick;

    while (*p == ' ') {
        ++p;
    }
    for (i = 0; i < sizeof(verbs) / sizeof(verbs[0]); ++i) {
        size_t n = strlen(verbs[i]);
        const char *q = p + n;

        if (strncmp(p, verbs[i], n) != 0) {
            continue;
        }
        while (*q == ' ') {
            ++q;
        }
        if (*q != '\0') {
            return 0;                                    /* a name or more follows: the engine does it */
        }
        pick = pick_script(titles[i], name, sizeof(name));
        if (pick < 0) {
            hist_push("no .m files in this folder", ATTR_NORMAL);
        }
        if (pick == 1 && strcmp(verbs[i], "change") == 0) {
            sprintf(prefill, "change %s ", name);
            line[0] = '\0';
        } else if (pick == 1) {
            sprintf(line, "%s %s", verbs[i], name);
        } else {
            line[0] = '\0';
        }
        return 1;
    }
    return 0;
}
#endif

#ifdef MS_486
static int model_only;                                   /* MATHVGA /MODEL: choose the model for the AI (the PS/1's menu entry), then leave */

int ui_run_model(void)
{
    model_only = 1;
    return ui_run();
}
#endif

int ui_run(void)
{
    int got, rc = ML_OK;
    char file[60], cmd[80];

    if (scr_init() != 0) {
        return -1;                                       /* not an 80-column text screen: the caller uses the line mode */
    }
    hist = (HLine *)malloc((size_t)HIST_MAX * sizeof(HLine));
    sess = ml_open(ui_sink, NULL);
    if (hist == NULL || sess == NULL) {
        scr_done();
        printf("out of memory\n");
        return 1;
    }
    mat_poll_hook = ui_poll;
    ml_set_plot_hook(sess, ui_plot, NULL);
#ifndef MS_NO_AI
    ml_set_ask_hook(sess, ui_ask, NULL);
#endif
    ml_set_edit_hook(sess, ui_edit, NULL);
#ifdef MS_486
    ml_set_pick_hook(sess, ui_pick, NULL);
#endif
#ifdef MS_486
    if (!model_only)
#endif
    hist_push("MathSmith " MS_VERSION " - type help, or press F1. Esc stops a long calculation.", ATTR_BRIGHT);
#ifdef MS_486
    if (model_only) {                                    /* the menu's Model: no MathSmith banner or prompt, and back to the menu as soon as the choice is made */
        scr_clear();
        rc = ml_eval(sess, "model");
        if (rc != ML_OK) {                               /* the reason (no network ...) stays on the screen until a key */
            strcpy(status_note, "any key: menu");
            redraw();
            scr_flush_keys();
            (void)scr_getkey();
        }
        rc = ML_QUIT;
    }
#endif
    while (rc != ML_QUIT) {
        got = read_line();
        if (got == 1) {
            char line[EDIT_MAX + 1];

            strcpy(line, editbuf);
#ifdef MS_486
            if (complete_with_picker(line) && line[0] == '\0') {      /* edit, run, explain, fix or change with no name: choose the script from a list */
                continue;                                    /* cancelled, or `change` waits for the user's words (prefill) */
            }
#endif
            remember(line);
            rc = run_line(line);
        } else if (got == -(int)K_F1) {
            rc = run_line("help");
        } else if (got == -(int)K_F2) {
            rc = run_line("who");
        } else if (got == -(int)K_F5) {
            if (!ml_show_plot(sess)) {
                hist_push("no plot to show yet", ATTR_NORMAL);
            }
        } else if (got == -(int)K_F6) {
            rc = run_line("why");
        } else if (got == -(int)K_F4) {
            hcount = 0;
            hhead = 0;
            back = 0;
        } else if (got == -(int)K_F3) {
#ifdef MS_486
            int pick = pick_script("Run which script?", file, sizeof(file));

            if (pick == 1) {
                sprintf(cmd, "run %s", file);
                rc = run_line(cmd);
            } else if (pick < 0) {
                hist_push("no .m files in this folder", ATTR_NORMAL);
            }
#else
            file[0] = '\0';
            scr_clear_row(ROW_INPUT, ATTR_NORMAL);
            scr_put(ROW_INPUT, 0, "Run which file (name, without .m)? ", ATTR_BRIGHT);
            if (scr_input(ROW_INPUT, 36, file, 20, ATTR_BRIGHT) && file[0] != '\0') {
                sprintf(cmd, "run %s", file);
                rc = run_line(cmd);
            }
#endif
        } else {
            rc = ML_QUIT;
        }
    }
    redraw();
    scr_dump_if_requested();
    mat_poll_hook = NULL;
    ml_close(sess);
    scr_done();
    return 0;
}
