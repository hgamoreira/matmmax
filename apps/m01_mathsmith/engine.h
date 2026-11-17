/* MathSmith's engine (M01): a small MATLAB-like language. A line goes in, text comes out through a sink. No screen, no files: the same
   file is built for DOS and for the Linux unit tests. */
#ifndef MS_ENGINE_H
#define MS_ENGINE_H

#include "plot.h"

typedef void (*ml_sink)(void *ctx, const char *text);     /* receives the output a piece at a time (lines end with \n) */
typedef struct MlSession MlSession;

#define ML_OK 0
#define ML_ERROR 1      /* the message was sent to the sink as "error: ..." and is in ml_error() */
#define ML_QUIT 2       /* quit or exit */
#define ML_MORE 3       /* a block (if, for, while, function) is still open: give the next line */

MlSession *ml_open(ml_sink sink, void *ctx);
void ml_close(MlSession *s);
int ml_eval(MlSession *s, const char *line);               /* one line: statements separated by ; or , */
const char *ml_error(const MlSession *s);
/* For a status line: the number of variables and of functions, and the decimals shown (4 or 8). */
void ml_info(const MlSession *s, int *nvar, int *nfunc, int *digits);

/* Plots (M06). A plot is shown when the line or script that made it ends: the hook is then called with the description; it may draw and wait for a key.
   Without a hook the engine just says that a plot was made. */
typedef int (*ml_plot_fn)(void *ctx, const PlotSpec *spec);
void ml_set_plot_hook(MlSession *s, ml_plot_fn fn, void *ctx);
int ml_show_plot(MlSession *s);                            /* shows the last plot again (F5); 0 if there is none */
int ml_plot_curves(const MlSession *s);                    /* how many curves the current plot has */
/* For tests: renders the current plot like a front end would. Returns the number of texts, or -1 if there is no plot. */
int ml_render_plot(MlSession *s, unsigned char *bits, PlotText *texts, int maxtexts, double *limits);

/* The assistant (M07): ask ..., code ..., explain name, fix name [..], change name "..", why. The engine writes the request to ASKIN.TXT (line 1 the mode: 13 ask,
   14 code, 15 explain, 16 fix, 17 why, 18 change; line 2 the instruction; the rest the context) and frees its own buffers, then calls the hook, which runs the helper
   (MSASK.EXE) and returns 0 (the answer is in ASKOUT.TXT), 1 (it failed: ASKOUT.TXT holds the reason) or 2 (the user stopped it). The engine reads ASKOUT.TXT and
   deletes both files. Without a hook the commands say the assistant is not available. */
typedef int (*ml_ask_fn)(void *ctx, int mode);
void ml_set_ask_hook(MlSession *s, ml_ask_fn fn, void *ctx);

/* The editor (M08): `edit name` asks the front end to open name.m (the hook only notes the request; the front end opens the editor when the line is done,
   so that the editor can run scripts through ml_eval). Without a hook the command says the editor needs the full screen. */
typedef void (*ml_edit_fn)(void *ctx, const char *name);
void ml_set_edit_hook(MlSession *s, ml_edit_fn fn, void *ctx);
/* The last error that was not an assistant command's: returns the line in a script (and the script's file name in `file`), or 0 (then `file` is empty) if it was
   typed at the prompt; `text` is the message without the place. */
int ml_last_error(const MlSession *s, char *text, int textmax, char *file, int filemax);
/* name.m was changed on disk: functions read from it before are forgotten and will be read again when called. */
void ml_file_changed(MlSession *s, const char *name);

#ifdef MS_486
/* The model picker (XD89): a front end that can show a list sets this hook. It gets the title, the items and the index of the one in use (or -1), and returns the
   index chosen, or -1 if the user cancelled. Without it (line mode) the `model` command prints the list. */
typedef int (*ml_pick_fn)(void *ctx, const char *title, const char *const *items, int n, int current);
void ml_set_pick_hook(MlSession *s, ml_pick_fn fn, void *ctx);
#endif

#ifdef MS_486
/* The .m files of the current folder, sorted by name (the 486's file picker and the dir command, XD88): the count, at most max. name has no .m and is lower case. */
typedef struct {
    char name[16];
    long size;
    int year, month, day;
} MlScript;
int ml_list_scripts(MlScript *items, int max);
#endif

/* The help text (lines of at most 78 characters, sections separated by blank lines), for a front end that shows it in a viewer. */
const char *ml_help_text(void);

#endif
