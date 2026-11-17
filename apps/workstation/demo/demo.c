/* Launcher test program: owns its prompt/arena, never touches MathSmith. */
#include <string.h>
#include "demo.h"

int wk_demo_run(WkAppContext *c)
{
    char *line;
    size_t used = 0;
    int previous_cr = 0, overflow = 0;
    if (!c || !c->memory || c->memory_size < WK_DEMO_MEMORY) return WK_APP_ERROR;
    line = (char *)c->memory;
    c->io.write(c->io.ctx, "Demo program. Text is echoed here, not evaluated.\n");
    c->io.write(c->io.ctx, "exit returns; Esc cancels; fail tests an error return.\n");
    if (*c->arguments) {
        c->io.write(c->io.ctx, "Arguments: ");
        c->io.write(c->io.ctx, c->arguments);
        c->io.write(c->io.ctx, "\n");
    }
    c->io.write(c->io.ctx, "demo> ");
    for (;;) {
        int ch = c->io.read_key(c->io.ctx);
        char echo[2];
        if (ch < 0) return WK_APP_ERROR;
        if (ch == '\n' && previous_cr) { previous_cr = 0; continue; }
        previous_cr = ch == '\r';
        if (ch == 27 || ch == 3) return WK_APP_CANCEL;
        if (ch == '\r' || ch == '\n') {
            c->io.write(c->io.ctx, "\n");
            if (overflow) c->io.write(c->io.ctx, "Demo line too long; discarded.\n");
            else {
                line[used] = '\0';
                if (!strcmp(line, "exit")) return WK_APP_OK;
                if (!strcmp(line, "fail")) {
                    c->io.write(c->io.ctx, "Demo requested an error return.\n");
                    return WK_APP_ERROR;
                }
                c->io.write(c->io.ctx, "Demo echo: ");
                c->io.write(c->io.ctx, line);
                c->io.write(c->io.ctx, "\n");
            }
            used = 0;
            overflow = 0;
            c->io.write(c->io.ctx, "demo> ");
        } else if ((ch == '\b' || ch == 127) && !overflow) {
            if (used) { --used; c->io.write(c->io.ctx, "\b \b"); }
        } else if (ch >= 32 && ch <= 126 && !overflow) {
            if (used < 127) {
                line[used++] = (char)ch;
                echo[0] = (char)ch;
                echo[1] = '\0';
                c->io.write(c->io.ctx, echo);
            } else overflow = 1;
        }
    }
}
