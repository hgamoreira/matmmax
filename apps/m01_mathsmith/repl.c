/* MathSmith M01: a line-by-line front end for the engine. Reads lines from the keyboard (or a redirected file), prints the results.
   `MATHSMTH` starts it; `quit` ends it. The full-screen version comes in M02. */

#include <stdio.h>
#include <string.h>
#include <time.h>
#include "engine.h"
#include "version.h"
#include "mat.h"

#ifdef __WATCOMC__
#include <bios.h>
#include <float.h>
#include <io.h>
#include "plotdos.h"
#include "ui.h"
#ifndef MS_NO_AI
#include "askdos.h"
#endif

/* Esc stops a long calculation: the matrix loops and the numerical tools ask now and then. It reads the keyboard through the BIOS: DOS's
   own kbhit/getch read the standard input, which a script redirected from a file would lose to this check. */
static int plot_hook(void *ctx, const PlotSpec *spec)
{
    (void)ctx;
    if (plotdos_show(spec) != 0) {
        printf("error: this display cannot show graphics (it needs the CGA 640x200 mode)\n");
    }
    return 0;
}

static int esc_pressed(void)
{
    while (_bios_keybrd(_KEYBRD_READY)) {
        if ((_bios_keybrd(_KEYBRD_READ) & 0xFF) == 27) {
            return 1;
        }
    }
    return 0;
}
#endif

static void sink(void *ctx, const char *text)
{
    (void)ctx;
    fputs(text, stdout);
}

int main(int argc, char **argv)
{
    char line[300];
    MlSession *s;
    int rc = ML_OK;
    clock_t t0;
    double seconds;

#ifdef __WATCOMC__
    _control87(MCW_EM, MCW_EM);                 /* no floating-point exceptions: 1/0 gives Inf, sqrt of -1 NaN (the engine checks) */
#ifdef MS_486
    _control87(PC_53, MCW_PC);                  /* the 486's maths chip works in 64 bits of mantissa: round to a double after every operation, as the Toshiba's
                                                   library and the Linux tests do, so that the answers are the same digit for digit (XD86) */
#endif
    mat_poll_hook = esc_pressed;
#endif
#ifdef __WATCOMC__
    {
        int force_lines = argc > 1 && (argv[1][0] == '/' || argv[1][0] == '-') && (argv[1][1] == 'L' || argv[1][1] == 'l');

#ifdef MS_486
        int choose_model = argc > 1 && (argv[1][0] == '/' || argv[1][0] == '-') && (argv[1][1] == 'M' || argv[1][1] == 'm');

        if (choose_model && isatty(0) && isatty(1)) {              /* the menu's "Model": only the choice of the AI model */
            if (ui_run_model() == 0) {
                return 0;
            }
        }
#endif
        if (!force_lines && isatty(0) && isatty(1)) {              /* a person at the keyboard: the full screen */
            if (ui_run() == 0) {
                return 0;
            }
        }
    }
#else
    (void)argc;
    (void)argv;
#endif
    s = ml_open(sink, NULL);
    if (s == NULL) {
        printf("out of memory\n");
        return 1;
    }
#ifdef __WATCOMC__
    ml_set_plot_hook(s, plot_hook, NULL);
#ifndef MS_NO_AI
    ml_set_ask_hook(s, askdos_run, NULL);
#endif
#endif
    printf("MathSmith " MS_VERSION " (line mode) - type help for help, quit to leave, Esc stops a long calculation\n");
    while (rc != ML_QUIT) {
        printf(rc == ML_MORE ? "   " : ">> ");               /* three spaces while a block (if, for, while, function) is open */
        fflush(stdout);
        if (fgets(line, sizeof(line), stdin) == NULL) {
            break;
        }
        line[strcspn(line, "\r\n")] = '\0';
        t0 = clock();
        rc = ml_eval(s, line);
        seconds = (double)(clock() - t0) / (double)CLOCKS_PER_SEC;
        if (seconds >= 1.0) {
            printf("(%.1f s)\n", seconds);          /* slow lines say how slow: the 8086 has no maths chip */
        }
    }
    ml_close(s);
    return 0;
}
