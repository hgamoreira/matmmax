/* MathSmith plotting (M06): a plot description and a portable renderer. The renderer makes a 640x200 one-bit-per-pixel bitmap (80 bytes a row, the
   leftmost pixel in the top bit) and a list of text items placed on the 80x25 character grid (8x8 pixel cells); a front end puts the bitmap on the
   screen and prints the texts. Plain C89, no I/O. */
#ifndef MS_PLOT_H
#define MS_PLOT_H

#ifdef MS_486
/* The 486's VGA (M11, decision XD86): 640x480 in 16 colours, one byte per pixel (the colour, 0 = background), texts on a grid of 8x16 cells (80x30). */
#define PLOT_W 640
#define PLOT_H 480
#define PLOT_STRIDE 640
#define PLOT_CELL_H 16
#define PLOT_ROWS 30
#else
#define PLOT_W 640
#define PLOT_H 200
#define PLOT_STRIDE 80
#define PLOT_CELL_H 8
#define PLOT_ROWS 25
#endif
#define PLOT_BYTES (PLOT_STRIDE * PLOT_H)
#define PLOT_MAXCURVES 6
#define PLOT_MAXPOINTS 6000                     /* in all the curves together */
#define PLOT_MAXTEXTS 40

/* The frame the curves are drawn in (pixels, inclusive). */
#ifdef MS_486
#define PLOT_X0 72
#define PLOT_X1 631
#define PLOT_Y0 32
#define PLOT_Y1 431
/* The colours of the VGA's default palette: the frame, the grid, the curves in order (a style letter r g b c m y k w picks one), the texts. */
#define PLOT_COL_FRAME 7
#define PLOT_COL_GRID 8
#define PLOT_COL_TEXT 15
#else
#define PLOT_X0 72
#define PLOT_X1 631
#define PLOT_Y0 16
#define PLOT_Y1 175
#endif

#define LINE_NONE 0
#define LINE_SOLID 1
#define LINE_DASHED 2
#define LINE_DOTTED 3
#define MARK_NONE 0
#define MARK_POINT 1
#define MARK_CIRCLE 2
#define MARK_PLUS 3
#define MARK_STAR 4
#define MARK_CROSS 5

typedef struct {
    int n;
    const double *x, *y;
    int line, marker;
#ifdef MS_486
    int colour;                                 /* 0: by the curve's number; else a VGA colour 1-15 (a style letter) */
#endif
} PlotCurve;

typedef struct {
    PlotCurve c[PLOT_MAXCURVES];
    int nc;
    char title[81], xlabel[81], ylabel[81];
    int grid;
    int xlim_set, ylim_set;                     /* 1: the limits below are given; 2: "tight": the data's own limits */
    double xlim[2], ylim[2];
} PlotSpec;

typedef struct {
    int row, col;                               /* character cell of the first letter */
    char text[81];
} PlotText;

/* Draws the plot into bits (PLOT_BYTES, cleared here) and fills texts (up to maxtexts). Returns the number of texts. limits (4 numbers: xlo xhi ylo yhi,
   or NULL) gets the axis limits used. A plot with no curves still draws its frame. */
int plot_render(const PlotSpec *spec, unsigned char *bits, PlotText *texts, int maxtexts, double *limits);

/* A picture of a rendered plot for a text screen (the IBM XT's monochrome display has no graphics): cells gets 25 rows of 80 characters (PLOT_TEXT_BYTES, code
   page 437). Each cell is the 8x8 pixels of the bitmap seen as four quadrants of 4x4 pixels, a quadrant being "on" when 3 or more of its 16 pixels are set (a line is
   4 or more); the cell is drawn with the half blocks or the full block so that a curve stays joined, a dot where there is only faint ink (a dotted grid), a space where
   there is none, and the texts are written over it. Plain C89. */
#define PLOT_TEXT_ROWS PLOT_ROWS
#define PLOT_TEXT_COLS 80
#define PLOT_TEXT_BYTES (PLOT_TEXT_ROWS * PLOT_TEXT_COLS)
void plot_text_image(const unsigned char *bits, const PlotText *texts, int n, unsigned char *cells);

/* Parses a style such as "r--", "o", "-.b", "k:" (colour letters are ignored). Returns 0 if a character is not part of a style. */
int plot_parse_style(const char *s, int *line, int *marker);
#ifdef MS_486
/* The VGA colour a style's letter names (r g b c m y k w: light red, green, blue, cyan, magenta, yellow, dark grey, white), or 0 if it has none. */
int plot_style_colour(const char *s);
#endif

#endif
