/* MathSmith plotting (M06): see plot.h. */

#include <math.h>
#include <stdio.h>
#include <string.h>
#include "plot.h"

static int finite(double v)
{
    return v == v && v - v == 0.0;
}

#ifdef MS_486
static unsigned char cur_colour = PLOT_COL_FRAME;            /* the colour put_pixel draws in */
#define SET_COLOUR(c) (cur_colour = (unsigned char)(c))

static void put_pixel(unsigned char *bits, int x, int y)
{
    if (x >= 0 && x < PLOT_W && y >= 0 && y < PLOT_H) {
        bits[y * PLOT_STRIDE + x] = cur_colour;
    }
}
#else
#define SET_COLOUR(c) ((void)0)

static void put_pixel(unsigned char *bits, int x, int y)
{
    if (x >= 0 && x < PLOT_W && y >= 0 && y < PLOT_H) {
        bits[y * PLOT_STRIDE + (x >> 3)] |= (unsigned char)(0x80 >> (x & 7));
    }
}
#endif

/* A line between two pixels with a pattern: 'on' pixels, then 'off' pixels (off 0: solid). */
static void draw_line(unsigned char *bits, int x0, int y0, int x1, int y1, int on, int off)
{
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, dy = y1 > y0 ? y1 - y0 : y0 - y1;
    int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx - dy, phase = 0, e2;

    for (;;) {
        if (off == 0 || phase < on) {
            put_pixel(bits, x0, y0);
        }
        if (off != 0) {
            phase = (phase + 1) % (on + off);
        }
        if (x0 == x1 && y0 == y1) {
            break;
        }
        e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x0 += sx;
        }
        if (e2 < dx) {
            err += dx;
            y0 += sy;
        }
    }
}

/* Liang-Barsky: clips the segment to the frame; 0 if nothing is left. */
static int clip(double *x0, double *y0, double *x1, double *y1)
{
    double t0 = 0.0, t1 = 1.0, dx = *x1 - *x0, dy = *y1 - *y0;
    double p[4], q[4];
    int i;

    p[0] = -dx; q[0] = *x0 - PLOT_X0;
    p[1] = dx;  q[1] = PLOT_X1 - *x0;
    p[2] = -dy; q[2] = *y0 - PLOT_Y0;
    p[3] = dy;  q[3] = PLOT_Y1 - *y0;
    for (i = 0; i < 4; ++i) {
        if (p[i] == 0.0) {
            if (q[i] < 0.0) {
                return 0;
            }
        } else {
            double r = q[i] / p[i];

            if (p[i] < 0.0) {
                if (r > t1) {
                    return 0;
                }
                if (r > t0) {
                    t0 = r;
                }
            } else {
                if (r < t0) {
                    return 0;
                }
                if (r < t1) {
                    t1 = r;
                }
            }
        }
    }
    *x1 = *x0 + t1 * dx;
    *y1 = *y0 + t1 * dy;
    *x0 += t0 * dx;
    *y0 += t0 * dy;
    return 1;
}

static int round_int(double v)
{
    return (int)floor(v + 0.5);
}

static void marker_at(unsigned char *bits, int x, int y, int marker)
{
    int k;

    if (x < PLOT_X0 - 2 || x > PLOT_X1 + 2 || y < PLOT_Y0 - 2 || y > PLOT_Y1 + 2) {
        return;
    }
    switch (marker) {
    case MARK_POINT:
        put_pixel(bits, x, y);
        put_pixel(bits, x + 1, y);
        put_pixel(bits, x, y + 1);
        put_pixel(bits, x + 1, y + 1);
        break;
    case MARK_CIRCLE:
        for (k = -1; k <= 1; ++k) {
            put_pixel(bits, x + k, y - 2);
            put_pixel(bits, x + k, y + 2);
            put_pixel(bits, x - 2, y + k);
            put_pixel(bits, x + 2, y + k);
        }
        break;
    case MARK_PLUS:
        for (k = -2; k <= 2; ++k) {
            put_pixel(bits, x + k, y);
            put_pixel(bits, x, y + k);
        }
        break;
    case MARK_CROSS:
        for (k = -2; k <= 2; ++k) {
            put_pixel(bits, x + k, y + k);
            put_pixel(bits, x + k, y - k);
        }
        break;
    case MARK_STAR:                                      /* a plus and a cross */
        marker_at(bits, x, y, MARK_PLUS);
        marker_at(bits, x, y, MARK_CROSS);
        break;
    }
}

/* Nice tick spacing (1, 2 or 5 times a power of ten) for about `target` intervals over lo..hi; *olo and *ohi are the limits (rounded out to ticks unless
   tight). */
static void nice_scale(double lo, double hi, int target, int tight, double *olo, double *ohi, double *step)
{
    double raw, mag, f, nf;

    if (!(hi > lo)) {                                    /* one value, or none: a range around it */
        double d = lo != 0.0 ? fabs(lo) * 0.1 : 1.0;

        lo -= d;
        hi += d;
    }
    raw = (hi - lo) / target;
    mag = pow(10.0, floor(log10(raw)));
    f = raw / mag;
    nf = f <= 1.0 ? 1.0 : (f <= 2.0 ? 2.0 : (f <= 5.0 ? 5.0 : 10.0));
    *step = nf * mag;
    if (tight) {
        *olo = lo;
        *ohi = hi;
    } else {
        *olo = floor(lo / *step + 1e-9) * *step;
        *ohi = ceil(hi / *step - 1e-9) * *step;
    }
}

static void tick_text(double v, double step, char *buf)
{
    if (fabs(v) < step * 1e-6) {
        v = 0.0;
    }
    sprintf(buf, "%.5g", v);
}

static void add_text(PlotText *texts, int *n, int max, int row, int col, const char *text)
{
    if (*n < max && row >= 0 && row < PLOT_ROWS) {
        if (col < 0) {
            col = 0;
        }
        texts[*n].row = row;
        texts[*n].col = col;
        strncpy(texts[*n].text, text, 80);
        texts[*n].text[80] = '\0';
        ++*n;
    }
}

int plot_render(const PlotSpec *spec, unsigned char *bits, PlotText *texts, int maxtexts, double *limits)
{
    double xmin = 0.0, xmax = 0.0, ymin = 0.0, ymax = 0.0, xlo, xhi, ylo, yhi, xstep, ystep, v;
    int have = 0, i, c, n = 0, len;
    char buf[40];

    memset(bits, 0, PLOT_BYTES);
    for (c = 0; c < spec->nc; ++c) {
        for (i = 0; i < spec->c[c].n; ++i) {
            double x = spec->c[c].x[i], y = spec->c[c].y[i];

            if (!finite(x) || !finite(y)) {
                continue;
            }
            if (!have) {
                xmin = xmax = x;
                ymin = ymax = y;
                have = 1;
            } else {
                if (x < xmin) { xmin = x; }
                if (x > xmax) { xmax = x; }
                if (y < ymin) { ymin = y; }
                if (y > ymax) { ymax = y; }
            }
        }
    }
    nice_scale(xmin, xmax, 5, spec->xlim_set == 2, &xlo, &xhi, &xstep);
    nice_scale(ymin, ymax, 6, spec->ylim_set == 2, &ylo, &yhi, &ystep);
    if (spec->xlim_set == 1) {
        xlo = spec->xlim[0];
        xhi = spec->xlim[1];
        nice_scale(xlo, xhi, 5, 1, &xlo, &xhi, &xstep);
    }
    if (spec->ylim_set == 1) {
        ylo = spec->ylim[0];
        yhi = spec->ylim[1];
        nice_scale(ylo, yhi, 6, 1, &ylo, &yhi, &ystep);
    }
    if (limits != NULL) {
        limits[0] = xlo;
        limits[1] = xhi;
        limits[2] = ylo;
        limits[3] = yhi;
    }

    /* the frame */
    SET_COLOUR(PLOT_COL_FRAME);
    for (i = PLOT_X0; i <= PLOT_X1; ++i) {
        put_pixel(bits, i, PLOT_Y0);
        put_pixel(bits, i, PLOT_Y1);
    }
    for (i = PLOT_Y0; i <= PLOT_Y1; ++i) {
        put_pixel(bits, PLOT_X0, i);
        put_pixel(bits, PLOT_X1, i);
    }
    /* ticks, grid and their numbers */
    for (v = ceil(xlo / xstep - 1e-9) * xstep; v <= xhi + xstep * 1e-9; v += xstep) {
        int px = round_int(PLOT_X0 + (v - xlo) / (xhi - xlo) * (PLOT_X1 - PLOT_X0));

        if (px < PLOT_X0 || px > PLOT_X1) {
            continue;
        }
        for (i = 0; i < 4; ++i) {
            put_pixel(bits, px, PLOT_Y1 - i);
            put_pixel(bits, px, PLOT_Y0 + i);
        }
        if (spec->grid) {
            SET_COLOUR(PLOT_COL_GRID);
            for (i = PLOT_Y0; i <= PLOT_Y1; i += 4) {
                put_pixel(bits, px, i);
            }
            SET_COLOUR(PLOT_COL_FRAME);
        }
        tick_text(v, xstep, buf);
        len = (int)strlen(buf);
        add_text(texts, &n, maxtexts, (PLOT_Y1 + 1) / PLOT_CELL_H, px / 8 - len / 2 > 80 - len ? 80 - len : px / 8 - len / 2, buf);
    }
    for (v = ceil(ylo / ystep - 1e-9) * ystep; v <= yhi + ystep * 1e-9; v += ystep) {
        int py = round_int(PLOT_Y1 - (v - ylo) / (yhi - ylo) * (PLOT_Y1 - PLOT_Y0));

        if (py < PLOT_Y0 || py > PLOT_Y1) {
            continue;
        }
        for (i = 0; i < 4; ++i) {
            put_pixel(bits, PLOT_X0 + i, py);
            put_pixel(bits, PLOT_X1 - i, py);
        }
        if (spec->grid) {
            SET_COLOUR(PLOT_COL_GRID);
            for (i = PLOT_X0; i <= PLOT_X1; i += 4) {
                put_pixel(bits, i, py);
            }
            SET_COLOUR(PLOT_COL_FRAME);
        }
        tick_text(v, ystep, buf);
        len = (int)strlen(buf);
        add_text(texts, &n, maxtexts, py / PLOT_CELL_H, 9 - len, buf);
    }
    /* the curves */
    for (c = 0; c < spec->nc; ++c) {
        const PlotCurve *cv = &spec->c[c];
        int on = 1, off = 0, havep = 0;
        double px0 = 0.0, py0 = 0.0;

#ifdef MS_486
        static const unsigned char curve_colour[PLOT_MAXCURVES] = { 14, 11, 10, 12, 13, 9 };   /* yellow, cyan, green, red, magenta, blue */

        SET_COLOUR(cv->colour != 0 ? cv->colour : curve_colour[c % PLOT_MAXCURVES]);
#endif
        if (cv->line == LINE_DASHED) {
            on = 5;
            off = 3;
        } else if (cv->line == LINE_DOTTED) {
            on = 1;
            off = 3;
        }
        for (i = 0; i < cv->n; ++i) {
            double x = cv->x[i], y = cv->y[i], px, py;

            if (!finite(x) || !finite(y)) {
                havep = 0;
                continue;
            }
            px = PLOT_X0 + (x - xlo) / (xhi - xlo) * (PLOT_X1 - PLOT_X0);
            py = PLOT_Y1 - (y - ylo) / (yhi - ylo) * (PLOT_Y1 - PLOT_Y0);
            if (cv->marker != MARK_NONE && px > -1e6 && px < 1e6 && py > -1e6 && py < 1e6) {
                marker_at(bits, round_int(px), round_int(py), cv->marker);
            }
            if (cv->line != LINE_NONE && havep) {
                double ax = px0, ay = py0, bx = px, by = py;

                if (fabs(ax) < 1e9 && fabs(ay) < 1e9 && fabs(bx) < 1e9 && fabs(by) < 1e9 && clip(&ax, &ay, &bx, &by)) {
                    draw_line(bits, round_int(ax), round_int(ay), round_int(bx), round_int(by), on, off);
                }
            }
            px0 = px;
            py0 = py;
            havep = 1;
        }
    }
    /* title and labels */
    if (spec->title[0] != '\0') {
        len = (int)strlen(spec->title);
        add_text(texts, &n, maxtexts, 0, (80 - len) / 2, spec->title);
    }
    if (spec->ylabel[0] != '\0') {
        add_text(texts, &n, maxtexts, 1, 0, spec->ylabel);
    }
    if (spec->xlabel[0] != '\0') {
        len = (int)strlen(spec->xlabel);
        add_text(texts, &n, maxtexts, (PLOT_Y1 + 1) / PLOT_CELL_H + 1, (80 - len) / 2, spec->xlabel);
    }
    return n;
}

int plot_parse_style(const char *s, int *line, int *marker)
{
    int have_line = 0;

    *line = LINE_SOLID;
    *marker = MARK_NONE;

    for (; *s != '\0'; ++s) {
        switch (*s) {
        case '-':
            if (s[1] == '-') {
                *line = LINE_DASHED;
                ++s;
            } else if (s[1] == '.') {
                *line = LINE_DASHED;
                ++s;
            } else {
                *line = LINE_SOLID;
            }
            have_line = 1;
            break;
        case ':':
            *line = LINE_DOTTED;
            have_line = 1;
            break;
        case '.': *marker = MARK_POINT; break;
        case 'o': *marker = MARK_CIRCLE; break;
        case '+': *marker = MARK_PLUS; break;
        case '*': *marker = MARK_STAR; break;
        case 'x': *marker = MARK_CROSS; break;
        case 'r': case 'g': case 'b': case 'c': case 'm': case 'y': case 'k': case 'w':
            break;
        default:
            return 0;
        }
    }
    if (*marker != MARK_NONE && !have_line) {
        *line = LINE_NONE;
    }
    return 1;
}

#ifdef MS_486
int plot_style_colour(const char *s)
{
    for (; *s != '\0'; ++s) {
        switch (*s) {
        case 'r': return 12;
        case 'g': return 10;
        case 'b': return 9;
        case 'c': return 11;
        case 'm': return 13;
        case 'y': return 14;
        case 'k': return 8;
        case 'w': return 15;
        default: break;
        }
    }
    return 0;
}
#else
/* --- the plot as characters (the XT) --- */

static int ones(unsigned char b)
{
    int n = 0;

    while (b != 0) {
        n += b & 1;
        b >>= 1;
    }
    return n;
}

/* The character for a cell, from which of its quadrants are on: bit 3 top left, 2 top right, 1 bottom left, 0 bottom right. */
static unsigned char quadrant_char(int q)
{
    static const unsigned char table[16] = {
        ' ',  0xDC, 0xDC, 0xDC,         /* none, BR and BL (the lower half: a line stays joined), BL+BR */
        0xDF, 0xDE, 0xDB, 0xDB,         /* TR (the upper half), TR+BR (right half), TR+BL (diagonal), three */
        0xDF, 0xDB, 0xDD, 0xDB,         /* TL, TL+BR (diagonal), TL+BL (left half), three */
        0xDF, 0xDB, 0xDB, 0xDB          /* TL+TR (upper half), three, three, all */
    };

    return table[q];
}

void plot_text_image(const unsigned char *bits, const PlotText *texts, int n, unsigned char *cells)
{
    int row, col, y, i, k, q, count[4];

    for (row = 0; row < PLOT_TEXT_ROWS; ++row) {
        for (col = 0; col < PLOT_TEXT_COLS; ++col) {
            count[0] = count[1] = count[2] = count[3] = 0;
            for (y = 0; y < 8; ++y) {
                unsigned char byte = bits[(row * 8 + y) * PLOT_STRIDE + col];

                count[(y >> 2) * 2] += ones((unsigned char)(byte >> 4));           /* the left four pixels */
                count[(y >> 2) * 2 + 1] += ones((unsigned char)(byte & 0x0F));     /* the right four */
            }
            q = (count[0] >= 3 ? 8 : 0) | (count[1] >= 3 ? 4 : 0) | (count[2] >= 3 ? 2 : 0) | (count[3] >= 3 ? 1 : 0);
            cells[row * PLOT_TEXT_COLS + col] = q != 0 ? quadrant_char(q) : (count[0] + count[1] + count[2] + count[3]) > 0 ? 0xFA : ' ';   /* faint ink (a dotted grid): a dot */
        }
    }
    for (i = 0; i < n; ++i) {
        for (k = 0; texts[i].text[k] != '\0' && texts[i].col + k < PLOT_TEXT_COLS; ++k) {
            if (texts[i].row >= 0 && texts[i].row < PLOT_TEXT_ROWS) {
                cells[texts[i].row * PLOT_TEXT_COLS + texts[i].col + k] = (unsigned char)texts[i].text[k];
            }
        }
    }
}
#endif
