/* M07 MSASK.EXE: MathSmith's link to the AI on the PC (decision XD75). A separate program so that MATHSMTH.EXE does not grow.

   MathSmith writes ASKIN.TXT:  line 1 = the mode (13-18, host/writer.py), line 2 = the instruction (up to 240 characters,
   may be empty), the rest = the context (the file, the last error ...). This program asks the PC server and writes the answer to
   ASKOUT.TXT as it arrives. Exit code: 0 = the answer is in ASKOUT.TXT; 1 = it failed, ASKOUT.TXT holds the reason (one line);
   2 = stopped with Esc. The settings are MSASK.CFG (else MATHSMTH.CFG, else NETFILES.CFG), as for the other programs. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <bios.h>
#include <i86.h>
#include "cfg.h"
#include "netget.h"
#include "nfproto.h"
#include "ticks.h"
#include "wstext.h"
#include "wsnet.h"

#define IN_NAME  "ASKIN.TXT"
#define OUT_NAME "ASKOUT.TXT"
#define CONTEXT_MAX 4000u                 /* the most context sent: MathSmith sends 4000 at most (the serial link carries about 1 KB/s at 19200) */
#define LINE_MAX 250

static Config config;
static FILE *out;
static unsigned long answer_chars;

static int cancelled(void)
{
    int esc = 0;

    while (_bios_keybrd(_KEYBRD_READY)) {
        if ((_bios_keybrd(_KEYBRD_READ) & 0xFF) == 27) {
            esc = 1;
        }
    }
    return esc;
}

static void progress(const char *text)
{
    printf("\r%-70.70s", text);
    fflush(stdout);
}

static void pause_a_moment(void)
{
    unsigned long start = bios_ticks();

    while (bios_ticks() - start < 4) {
    }
}

/* The answer's next piece goes straight to the file; the characters MathSmith cannot show become '?'. */
static int put(const unsigned char *data, unsigned n)
{
    unsigned i;
    unsigned char c;

    for (i = 0; i < n; ++i) {
        c = data[i];
        if (c == '\t') {
            c = ' ';
        } else if (c != '\n' && (c < 0x20 || c > 0x7E)) {
            c = '?';
        }
        if (fputc(c, out) == EOF) {
            return 0;
        }
        ++answer_chars;
    }
    return 1;
}

static const WnHooks hooks = { progress, cancelled, put, pause_a_moment };

/* Ends the run: a failure leaves only the reason in ASKOUT.TXT. */
static int finish(int code, const char *why)
{
    if (code != 0) {
        if (out != NULL) {
            fclose(out);
        }
        out = fopen(OUT_NAME, "w");
        if (out != NULL) {
            fprintf(out, "%s\n", why);
            fclose(out);
        }
        printf("\r%-70s\n", why);
    } else if (out != NULL) {
        fclose(out);
        printf("\r%-70s\n", "Done.");
    }
    net_stop();
    return code;
}

/* Reads one line (without its end) of at most max characters; the rest of a longer line is dropped. Returns 0 at the end of the file. */
static int read_line(FILE *f, char *buf, int max)
{
    int c, n = 0;

    for (;;) {
        c = fgetc(f);
        if (c == EOF) {
            buf[n] = '\0';
            return n > 0;
        }
        if (c == '\n') {
            buf[n] = '\0';
            return 1;
        }
        if (c != '\r' && n < max - 1) {
            buf[n++] = (char)c;
        }
    }
}

int main(void)
{
    char head[40], instruction[NF_AI_ILEN_MAX + 2];
    char *buf;
    FILE *in;
    WsText ctx;
    unsigned n = 0;
    int mode, c, rc;
    unsigned long got = 0;
    char why[100];

    cfg_defaults(&config);
    cfg_load(&config, "MSASK.CFG");
    if (!cfg_ip_is_set(config.server)) {
        cfg_load(&config, "MATHSMTH.CFG");
    }
    if (!cfg_ip_is_set(config.server)) {
        cfg_load(&config, "NETFILES.CFG");
    }
    config.mtcp_fields = getenv("MTCPCFG") != NULL ? cfg_load_mtcp(&config, getenv("MTCPCFG")) : -1;
    cfg_finish(&config);

    in = fopen(IN_NAME, "rb");
    if (in == NULL || !read_line(in, head, sizeof head)) {
        return finish(1, "MSASK: no " IN_NAME " to send");
    }
    mode = atoi(head);
    if (mode < 13 || mode > 18) {
        fclose(in);
        return finish(1, "MSASK: ASKIN.TXT has a bad mode on its first line");
    }
    read_line(in, instruction, sizeof instruction);
    if (strlen(instruction) > NF_AI_ILEN_MAX) {
        instruction[NF_AI_ILEN_MAX] = '\0';
    }
    buf = (char *)malloc(CONTEXT_MAX + 1);
    if (buf == NULL || !wn_init()) {
        fclose(in);
        return finish(1, "MSASK: not enough memory");
    }
    while (n < CONTEXT_MAX && (c = fgetc(in)) != EOF) {
        if (c != '\r') {
            buf[n++] = (char)c;
        }
    }
    fclose(in);
    ws_init(&ctx, buf, CONTEXT_MAX + 1, 1, LINE_MAX);
    ws_set(&ctx, buf, n);

    if (!cfg_ip_is_set(config.server)) {
        return finish(1, "MSASK: no server address: put server=a.b.c.d in MSASK.CFG");
    }
    if (!cfg_ip_is_set(config.ip)) {
        return finish(1, "MSASK: no IP address: put ip=a.b.c.d in MSASK.CFG");
    }
    progress("Starting the network...");
    rc = net_start(&config);
    if (rc != NF_OK) {
        if (rc == NF_NET_NOSTACK && net_start_error()[0] != '\0') {
            sprintf(why, "Network did not start: %.60s", net_start_error());
        } else {
            sprintf(why, "%.90s", nf_message(rc));
        }
        return finish(1, why);
    }
    wn_set_transport(net_get, net_post);

    out = fopen(OUT_NAME, "w");
    if (out == NULL) {
        return finish(1, "MSASK: cannot write " OUT_NAME);
    }
    rc = wn_ai(mode, instruction, &ctx, 0, n, &hooks, &got);
    if (rc == WN_CANCELLED) {
        return finish(2, "Stopped.");
    }
    if (rc != NF_OK) {
        sprintf(why, "%.90s", wn_message(rc));
        return finish(1, why);
    }
    if (answer_chars == 0) {
        return finish(1, "The AI wrote nothing.");
    }
    return finish(0, "");
}
