/* M07: runs MSASK.EXE for the engine's assistant commands (decision XD75). The helper is another program because the network stack does not fit
   in MATHSMTH.EXE's data segment; the two talk through two small files in the current folder, which the engine writes and reads. The helper's exit code: 0 answered,
   1 failed (the reason is in ASKOUT.TXT), 2 stopped with Esc. The environment variable MSASK_PROG names another program to run (the tests use it).
   MSASK.EXE needs about 230 KB: before it runs, the memory MathSmith's heap holds but does not use is given back to DOS. */
#include <dos.h>
#include <errno.h>
#include <malloc.h>
#include <process.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "askdos.h"
#ifdef MS_XT
#include "swapexec.h"
#endif

#define OUT_NAME "ASKOUT.TXT"
#define NEED_KB 235                       /* measured in the emulator (XD79): started from the full screen, MSASK.EXE runs with 238 KB free and not with 233 KB */

static void say_reason(const char *text)
{
    FILE *f = fopen(OUT_NAME, "w");

    if (f != NULL) {
        fprintf(f, "%s\n", text);
        fclose(f);
    }
}

int askdos_run(void *ctx, int mode)
{
    const char *prog = getenv("MSASK_PROG");
    char reason[120];
    unsigned seg = 0;
    int rc;

    (void)ctx;
    (void)mode;
    if (prog == NULL || prog[0] == '\0') {
        prog = "MSASK";
    }
    _heapshrink();                                           /* freed heap segments go back to DOS */
    _dos_allocmem(0xFFFF, &seg);                             /* fails, and says how many paragraphs are free */
#ifdef MS_XT
    if (seg / 64 < NEED_KB) {                                /* the XT: MathSmith's memory goes to EMS while the helper runs (XD97) */
        char name[80], why[100];

        strncpy(name, prog, sizeof name - 5);
        name[sizeof name - 5] = '\0';
        if (strchr(name, '.') == NULL) {
            strcat(name, ".EXE");
        }
        printf("Only %u KB free: MathSmith's memory goes to EMS while the helper runs...\n", seg / 64);
        fflush(stdout);
        rc = swap_spawn(name, why);
        if (rc == -1) {
            say_reason(why);
            return 1;
        }
        return rc == 0 || rc == 2 ? rc : 1;
    }
#endif
    if (seg / 64 < NEED_KB) {
        sprintf(reason, "not enough free memory to run MSASK.EXE: it needs about %u KB, %u KB are free", NEED_KB, seg / 64);
        say_reason(reason);
        return 1;
    }
    fflush(stdout);                                          /* what MathSmith printed so far comes before the helper's messages */
    rc = spawnlp(P_WAIT, prog, prog, NULL);
    if (rc == -1) {
        if (errno == ENOMEM) {
            strcpy(reason, "not enough free memory to run MSASK.EXE");
        } else {
            strcpy(reason, "cannot run MSASK.EXE (it must be in this folder or on the PATH)");
        }
        say_reason(reason);
        return 1;
    }
    return rc == 0 || rc == 2 ? rc : 1;
}
