/* Showing a plot on a DOS machine with a CGA-compatible display (M06): see plotdos.h. */

#include <bios.h>
#include <dos.h>
#include <i86.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "plotdos.h"
#include "screen.h"


static int video_mode(void)
{
    union REGS r;

    r.h.ah = 0x0F;
    int86(0x10, &r, &r);
    return r.h.al & 0x7F;
}

static void set_mode(int mode)
{
    union REGS r;

    r.h.ah = 0x00;
    r.h.al = (unsigned char)mode;
    int86(0x10, &r, &r);
}

/* Prints text in the graphics mode through the BIOS (it draws each letter from its own font into the 8x8 cell). */
static void put_text(int row, int col, const char *text)
{
    union REGS r;

    for (; *text != '\0' && col < 80; ++text, ++col) {
        r.h.ah = 0x02;
        r.h.bh = 0;
        r.h.dh = (unsigned char)row;
        r.h.dl = (unsigned char)col;
        int86(0x10, &r, &r);
        r.h.ah = 0x09;
        r.h.al = (unsigned char)*text;
        r.h.bh = 0;
        r.h.bl = 1;
        r.x.cx = 1;
        int86(0x10, &r, &r);
    }
}

int plotdos_show(const PlotSpec *spec)
{
    int old_mode = video_mode(), n, row, i;
    unsigned char __far *vram = (unsigned char __far *)MK_FP(0xB800, 0);
    const char *dump;
    unsigned char *bits;                                    /* the bitmap and the texts are made in the far heap: the near data segment is full */
    PlotText *texts;

    bits = (unsigned char *)malloc(PLOT_BYTES);
    texts = (PlotText *)malloc(PLOT_MAXTEXTS * sizeof(PlotText));
    if (bits == NULL || texts == NULL) {
        free(bits);
        free(texts);
        return -2;
    }
    set_mode(6);
    if (video_mode() != 6) {
        set_mode(old_mode);
        free(bits);
        free(texts);
        return -1;
    }
    n = plot_render(spec, bits, texts, PLOT_MAXTEXTS, NULL);
    for (row = 0; row < PLOT_H; ++row) {                    /* mode 6 keeps the even rows at B800:0000 and the odd rows at B800:2000 */
        _fmemcpy(vram + (row & 1 ? 0x2000 : 0) + (row >> 1) * PLOT_STRIDE, bits + row * PLOT_STRIDE, PLOT_STRIDE);
    }
    for (i = 0; i < n; ++i) {
        put_text(texts[i].row, texts[i].col, texts[i].text);
    }
    put_text(24, 0, "press a key");
    dump = getenv("XTPLOTDUMP");
    if (dump != NULL) {                                     /* test hook: the video memory as it is */
        FILE *f = fopen(dump, "wb");

        if (f != NULL) {
            unsigned char chunk[256];
            unsigned off;

            for (off = 0; off < 0x4000; off += sizeof(chunk)) {
                _fmemcpy((void __far *)chunk, vram + off, sizeof(chunk));
                fwrite(chunk, 1, sizeof(chunk), f);
            }
            fclose(f);
        }
    }
    scr_getkey();
    set_mode(old_mode);
    free(bits);
    free(texts);
    return 0;
}
