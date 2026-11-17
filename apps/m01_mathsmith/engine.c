/* MathSmith's engine (M01): see engine.h. Plain C89. Values are all matrices of doubles (a scalar is 1x1); the parser evaluates as it
   reads, so there is no syntax tree. */

#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "engine.h"
#include "version.h"
#include "linalg.h"
#ifdef MS_FFT
#include "fft.h"
#endif
#include "mat.h"
#include "plot.h"

#ifdef MS_486
#define MAXVARS 200                     /* the 486 (M11): room for bigger programs */
#else
#define MAXVARS 40
#endif
#define NAMELEN 32
#define MAXARGS 12
#define LINE_WIDTH 80
#define PI 3.14159265358979323846
#ifdef MS_486
#define MAXFUNCS 60
#else
#define MAXFUNCS 12
#endif
#define MAXPARAMS 3
#define BODYLEN 160
#define MAXBIND 16
#define MAX_DEPTH 8
#ifdef MS_486
#define MAX_EVALS 500000L               /* the 486 runs ten times as many in the time the 8086 takes for 20000 */
#else
#define MAX_EVALS 20000L
#endif

typedef struct {
    char name[NAMELEN];
    int np;
    char param[MAXPARAMS][NAMELEN];
    char body[BODYLEN];
    int ffp1;                                   /* 1 + the index of a file function this stands for (0: none) */
} UserFn;

/* The variables of one workspace: the main one, or one function call's own. */
typedef struct {
    int nvar;
    struct {
        char name[NAMELEN];
        Mat *m;
    } var[MAXVARS];
} Frame;

/* One statement of a program: the text without its ; or , and whether a ; ended it (the result is not shown). */
typedef struct {
    char *text;
    int quiet;
    int line;
} Stmt;

/* A linked program: for each statement its keyword, and for blocks where the matching end and the next clause are. */
typedef struct {
    Stmt *st;
    int n;
    int *endof;
    int *next;
    unsigned char *kw;
} Prog;

#define MAXFFN 16
#define MAXFPARAMS 6
#define MAX_FDEPTH 12

typedef struct {
    char name[NAMELEN];
    int np;
    char param[MAXFPARAMS][NAMELEN];
    char out[NAMELEN];                          /* the output variable, empty if none */
    Stmt *own;                                  /* the body's statements, owned */
    Prog body;
} FileFn;

struct MlSession {
    ml_sink sink;
    void *ctx;
    char err[100];
    int digits;                                 /* decimals shown: 4 (format short) or 8 (format long) */
    Frame glob;
    Frame *cur;                                 /* the workspace in use */
    unsigned long seed;
    UserFn fn[MAXFUNCS];                        /* functions made with f = @(x) ... */
    int nfn;
    struct {
        char name[NAMELEN];
        const Mat *m;                           /* not owned: the caller's argument */
    } bind[MAXBIND];                            /* parameters of the functions being run */
    int nbind;
    int depth;
    long evals;                                 /* function evaluations in this line */
    int warn_integral;
    struct {
        struct {
            double *x, *y;
            int n, line, marker;
#ifdef MS_486
            int colour;
#endif
        } c[PLOT_MAXCURVES];
        int nc;
        long total;
        char title[81], xlabel[81], ylabel[81];
        int grid, hold, xlim_set, ylim_set, dirty;
        double xlim[2], ylim[2];
    } plot;
    ml_plot_fn plot_hook;
    void *plot_ctx;
    ml_ask_fn ask_hook;                         /* the assistant (M07): the front end runs MSASK.EXE */
    void *ask_ctx;
#ifdef MS_486
    ml_pick_fn pick_hook;                       /* the model picker (XD89): the front end shows a list and returns the choice */
    void *pick_ctx;
    char model[64];                             /* the Ollama model the user chose ("" = the PC's own), kept in MSMODEL.CFG */
    int model_loaded;
#endif
    ml_edit_fn edit_hook;                       /* the editor (M08): the front end opens the script */
    void *edit_ctx;
    char why_text[100];                         /* the last error without its place, for the editor's status line */
    char last_line[240];                        /* the last line typed that was not an assistant command, for why */
    char why_msg[240];                          /* the last error, as it was shown */
    char why_file[NAMELEN + 12];                /* where it happened (a script), else empty */
    int why_line;
    int assisting;                              /* the line now running is an assistant command (its errors are not the ones why explains) */
    int want_out;                               /* how many results the statement [a,b] = f(...) asks the call for (1 normally) */
    Mat *extra[3];                              /* the results after the first, made by the call */
    int n_extra;
    int novalue;                                /* the statement made no value (disp, fprintf, tic) */
    clock_t tic_at;
    int bindbase;                               /* parameters of anonymous functions below this are not visible */
    FileFn ffn[MAXFFN];                         /* functions from files or typed over several lines */
    int nffn;
    int fdepth;                                 /* calls of file functions in progress */
    Frame *frames[MAX_FDEPTH + 1];              /* the variable tables of calls, made once and reused (zeroing a table costs more than a call) */
    int sdepth;                                 /* scripts running */
    Stmt *pend;                                 /* statements of a block typed over several lines */
    int npend, cappend, pdepth;
    int errline;                                /* where an error happened inside a program (0: not in one) */
    char errfile[NAMELEN + 12];
    const char *curfile;
};

typedef struct {
    MlSession *s;
    const char *p;                              /* the cursor */
    int brack;                                  /* inside [ ] but not inside ( ): spaces separate elements */
    int endtop;
    struct {
        int r, c, k, n;                         /* the matrix being indexed, which argument, how many arguments */
    } endctx[4];
} Parser;

static Mat *call_ffn(Parser *P, int ffi, Mat **argv, int n);
static char *find_comment(char *p);
static Mat *fast_expr(Parser *P, int mask);
static int fast_store(Parser *P, Mat *tm, const char *paren, const char *eq);
static void ffn_remove(MlSession *s, const char *name);
static int run_file(MlSession *s, const char *name, int functions_only);
static int find_ffn(const MlSession *s, const char *name);
static int autoload_function(MlSession *s, const char *name);

static void emit(MlSession *s, const char *text)
{
    if (s->sink != NULL) {
        s->sink(s->ctx, text);
    }
}

static void set_err(MlSession *s, const char *fmt, ...)
{
    va_list ap;
    char tmp[200];

    if (s->err[0] != '\0') {
        return;                                 /* the first error is the real one */
    }
    va_start(ap, fmt);
    vsprintf(tmp, fmt, ap);
    va_end(ap);
    tmp[sizeof(s->err) - 1] = '\0';
    strcpy(s->err, tmp);
}

/* NaN and Inf are tested and made on the bits of the IEEE double (little-endian), not with comparisons or 0/0: the DOS build's
   floating-point library does not give NaN for 0/0 and does not compare NaNs as IEEE does (seen in the emulator, 2026-10-16). */
static int exponent_all_ones(const unsigned char *b)
{
    return (b[7] & 0x7F) == 0x7F && (b[6] & 0xF0) == 0xF0;
}

static int is_nan(double x)
{
    unsigned char b[8];

    memcpy(b, &x, 8);
    return exponent_all_ones(b) && ((b[6] & 0x0F) || b[0] || b[1] || b[2] || b[3] || b[4] || b[5]);
}

static int is_inf(double x)
{
    unsigned char b[8];

    memcpy(b, &x, 8);
    return exponent_all_ones(b) && !((b[6] & 0x0F) || b[0] || b[1] || b[2] || b[3] || b[4] || b[5]);
}

static int is_int(double x) { return !is_nan(x) && !is_inf(x) && floor(x) == x; }

static double make_special(int nan, int negative)
{
    unsigned char b[8];
    double x;

    memset(b, 0, 8);
    b[6] = nan ? 0xF8 : 0xF0;
    b[7] = negative ? 0xFF : 0x7F;
    memcpy(&x, b, 8);
    return x;
}

static double make_nan(void) { return make_special(1, 0); }
static double make_inf(void) { return make_special(0, 0); }

/* --- variables --- */

static int find_var(const MlSession *s, const char *name)
{
    int i;

    for (i = 0; i < s->cur->nvar; ++i) {
        if (s->cur->var[i].name[0] == name[0] && strcmp(s->cur->var[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

static int find_func(const MlSession *s, const char *name)
{
    int i;

    for (i = 0; i < s->nfn; ++i) {
        if (s->fn[i].name[0] == name[0] && strcmp(s->fn[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

static void clear_func(MlSession *s, const char *name)
{
    int i = find_func(s, name);

    if (i >= 0) {
        for (; i + 1 < s->nfn; ++i) {
            s->fn[i] = s->fn[i + 1];
        }
        --s->nfn;
    }
}

/* Takes ownership of m. */
static int put_var(MlSession *s, const char *name, Mat *m)
{
    int i = find_var(s, name);

    if (i < 0) {
        if (s->cur->nvar >= MAXVARS) {
            mat_free(m);
            set_err(s, "too many variables (%d)", MAXVARS);
            return 0;
        }
        i = s->cur->nvar++;
        strcpy(s->cur->var[i].name, name);
        s->cur->var[i].m = NULL;
    }
    mat_free(s->cur->var[i].m);
    s->cur->var[i].m = m;
    return 1;
}

/* Takes ownership of m. A variable replaces a function of the same name. */
static int set_var(MlSession *s, const char *name, Mat *m)
{
    clear_func(s, name);
    return put_var(s, name, m);
}

static void clear_var(MlSession *s, const char *name)
{
    int i = find_var(s, name);

    clear_func(s, name);
    if (i >= 0) {
        mat_free(s->cur->var[i].m);
        for (; i + 1 < s->cur->nvar; ++i) {
            s->cur->var[i] = s->cur->var[i + 1];
        }
        --s->cur->nvar;
    }
}

/* --- display --- */

static void fmt_scalar(char *buf, double x, int digits)
{
    if (x == 0.0) {
        x = 0.0;                                /* no "-0" */
    }
    if (is_nan(x)) {
        strcpy(buf, "NaN");
    } else if (is_inf(x)) {
        strcpy(buf, x < 0 ? "-Inf" : "Inf");
    } else if (is_int(x) && fabs(x) < 1e9) {
        sprintf(buf, "%.0f", x);
    } else if (fabs(x) >= 1e5 || (x != 0.0 && fabs(x) < 1e-3)) {
        sprintf(buf, "%.*e", digits, x);
    } else {
        sprintf(buf, "%.*f", digits, x);
    }
}

/* Prints a value: "name = ..." for a variable, or just the value when name is NULL (disp). */
static void show_value(MlSession *s, const char *name, const Mat *m)
{
    char line[260], cell[40];
    long n = MAT_N(m), i;
    int allint = 1, plain = 1, width, intd = 1, per, c0, c1, j, rr;
    double big = 0.0;

    if (m->kind == MAT_CHAR) {
        if (m->r <= 1) {
            line[0] = '\0';
            if (name != NULL) {
                sprintf(line, "%s = ", name);
            }
            for (i = 0; i < n && strlen(line) < sizeof(line) - 3; ++i) {
                char one[2];

                one[0] = (char)(int)m->d[i];
                one[1] = '\0';
                strcat(line, one);
            }
            strcat(line, "\n");
            emit(s, line);
        } else {
            if (name != NULL) {
                sprintf(line, "%s =\n", name);
                emit(s, line);
            }
            for (rr = 0; rr < m->r; ++rr) {
                line[0] = '\0';
                for (j = 0; j < m->c && j < (int)sizeof(line) - 3; ++j) {
                    char one[2];

                    one[0] = (char)(int)MAT_AT(m, rr, j);
                    one[1] = '\0';
                    strcat(line, one);
                }
                strcat(line, "\n");
                emit(s, line);
            }
        }
        return;
    }
    if (n == 0) {
        if (name != NULL) {
            sprintf(line, "%s = []\n", name);
            emit(s, line);
        }
        return;
    }
    if (n == 1) {
        fmt_scalar(cell, m->d[0], s->digits);
        if (name != NULL) {
            sprintf(line, "%s = %s\n", name, cell);
        } else {
            sprintf(line, "%s\n", cell);
        }
        emit(s, line);
        return;
    }
    for (i = 0; i < n; ++i) {
        double x = m->d[i];

        if (is_nan(x) || is_inf(x)) {
            continue;
        }
        if (!is_int(x) || fabs(x) >= 1e9) {
            allint = 0;
        }
        if (fabs(x) > big) {
            big = fabs(x);
        }
        if (x != 0.0 && (fabs(x) >= 1e5 || fabs(x) < 1e-3)) {
            plain = 0;
        }
    }
    while (big >= 10.0) {
        big /= 10.0;
        ++intd;
    }
    if (allint) {
        width = intd + 4;                       /* sign and three spaces */
    } else if (plain) {
        width = intd + s->digits + 4;
    } else {
        width = s->digits + 9;
    }
    if (name != NULL) {
        sprintf(line, "%s =\n", name);
        emit(s, line);
    }
    per = LINE_WIDTH / width;
    if (per < 1) {
        per = 1;
    }
    for (c0 = 0; c0 < m->c; c0 += per) {
        c1 = c0 + per < m->c ? c0 + per : m->c;
        if (m->c > per) {
            if (c1 - c0 == 1) {
                sprintf(line, " Column %d:\n", c0 + 1);
            } else {
                sprintf(line, " Columns %d through %d:\n", c0 + 1, c1);
            }
            emit(s, line);
        }
        for (rr = 0; rr < m->r; ++rr) {
            line[0] = '\0';
            for (j = c0; j < c1; ++j) {
                double x = MAT_AT(m, rr, j);
                char one[60];

                if (x == 0.0) {
                    x = 0.0;                    /* no "-0.0000" */
                }
                if (is_nan(x) || is_inf(x)) {
                    fmt_scalar(cell, x, s->digits);
                } else if (allint) {
                    sprintf(cell, "%.0f", x);
                } else if (x == 0.0) {
                    strcpy(cell, "0");          /* an exact zero is a plain 0, as in MATLAB */
                } else if (plain) {
                    sprintf(cell, "%.*f", s->digits, x);
                } else {
                    sprintf(cell, "%.*e", s->digits, x);
                }
                sprintf(one, "%*s", width, cell);
                strcat(line, one);
            }
            strcat(line, "\n");
            emit(s, line);
        }
    }
}

/* --- parsing helpers --- */

static int skip_ws(Parser *P)
{
    int had = 0;

    while (*P->p == ' ' || *P->p == '\t') {
        ++P->p;
        had = 1;
    }
    return had;
}

/* The same without the result, with no call: it runs dozens of times a statement. */
#define SKIPWS(P) while (*(P)->p == ' ' || *(P)->p == '\t') { ++(P)->p; }

static Mat *parse_expr(Parser *P);
static Mat *parse_additive(Parser *P);
static Mat *parse_unary(Parser *P);

static Mat *need(Parser *P, Mat *m)
{
    if (m == NULL && P->s->err[0] == '\0') {
        if (mat_aborted) {
            set_err(P->s, "interrupted");
        } else {
            set_err(P->s, "out of memory or matrix larger than %d elements", MAT_MAX_ELEMS);
        }
    }
    return m;
}

/* Broadcasting element-wise operation. op: '+' '-' '*' '/' '^' 'm' (mod) 'r' (rem) */
static double apply(int op, double x, double y)
{
    switch (op) {
    case '+': return x + y;
    case '-': return x - y;
    case '*': return x * y;
    case '/':
        if (y == 0.0) {
            return (is_nan(x) || x == 0.0) ? make_nan() : make_special(0, x < 0.0);
        }
        return x / y;
    case '^': return pow(x, y);
    case 'm':
        if (y == 0.0) {
            return x;
        }
        return x - floor(x / y) * y;
    case 'r':
        if (y == 0.0) {
            return x;
        }
        return x - (x / y < 0.0 ? ceil(x / y) : floor(x / y)) * y;
    case '=': return x == y ? 1.0 : 0.0;
    case '!': return x != y ? 1.0 : 0.0;
    case '<': return x < y ? 1.0 : 0.0;
    case '>': return x > y ? 1.0 : 0.0;
    case 'l': return x <= y ? 1.0 : 0.0;
    case 'g': return x >= y ? 1.0 : 0.0;
    case '&': return (x != 0.0 && y != 0.0) ? 1.0 : 0.0;
    case '|': return (x != 0.0 || y != 0.0) ? 1.0 : 0.0;
    case 'M': return x > y ? x : y;
    case 'N': return x < y ? x : y;
    }
    return make_nan();
}

static Mat *elementwise(Parser *P, int op, const Mat *a, const Mat *b)
{
    int r, c, i, j;
    Mat *m;

    if (a->r == 1 && a->c == 1 && b->r == 1 && b->c == 1) {                     /* two numbers: the common case */
        m = need(P, mat_scalar(apply(op, a->d[0], b->d[0])));
        if (m != NULL && (op == '=' || op == '!' || op == '<' || op == '>' || op == 'l' || op == 'g' || op == '&' || op == '|')) {
            m->kind = MAT_BOOL;
        }
        return m;
    }
    if ((a->r != b->r && a->r != 1 && b->r != 1) || (a->c != b->c && a->c != 1 && b->c != 1)) {
        set_err(P->s, "matrix dimensions must agree (%dx%d and %dx%d)", a->r, a->c, b->r, b->c);
        return NULL;
    }
    r = a->r > b->r ? a->r : b->r;
    c = a->c > b->c ? a->c : b->c;
    if (a->r == 0 || b->r == 0) {
        r = 0;
    }
    if (a->c == 0 || b->c == 0) {
        c = 0;
    }
    m = need(P, mat_new(r, c));
    if (m == NULL) {
        return NULL;
    }
    for (i = 0; i < r; ++i) {
        for (j = 0; j < c; ++j) {
            MAT_AT(m, i, j) = apply(op, MAT_AT(a, a->r == 1 ? 0 : i, a->c == 1 ? 0 : j), MAT_AT(b, b->r == 1 ? 0 : i, b->c == 1 ? 0 : j));
        }
    }
    if (op == '=' || op == '!' || op == '<' || op == '>' || op == 'l' || op == 'g' || op == '&' || op == '|') {
        m->kind = MAT_BOOL;
    }
    return m;
}

static const char *solve_message(int status)
{
    if (mat_aborted) {
        return "interrupted";
    }
    switch (status) {
    case MAT_DIMS: return "matrix dimensions must agree";
    case MAT_SINGULAR: return "matrix is singular to working precision";
    case MAT_UNDER: return "fewer equations than unknowns is not supported yet";
    }
    return "out of memory";
}

static Mat *do_solve(Parser *P, const Mat *a, const Mat *b)
{
    int st;
    Mat *x;

    if (a->r == 1 && a->c == 1) {
        return elementwise(P, '/', b, a);
    }
    x = mat_solve(a, b, &st);
    if (x == NULL) {
        set_err(P->s, "%s", solve_message(st));
    }
    return x;
}

static Mat *do_divide(Parser *P, const Mat *a, const Mat *b)
{
    Mat *bt, *at, *x, *r;

    if (b->r == 1 && b->c == 1) {
        return elementwise(P, '/', a, b);
    }
    bt = need(P, mat_transpose(b));
    at = need(P, mat_transpose(a));
    if (bt == NULL || at == NULL) {
        mat_free(bt);
        mat_free(at);
        return NULL;
    }
    x = do_solve(P, bt, at);
    mat_free(bt);
    mat_free(at);
    if (x == NULL) {
        return NULL;
    }
    r = need(P, mat_transpose(x));
    mat_free(x);
    return r;
}

static Mat *do_multiply(Parser *P, const Mat *a, const Mat *b)
{
    Mat *m;

    if ((a->r == 1 && a->c == 1) || (b->r == 1 && b->c == 1)) {
        return elementwise(P, '*', a, b);
    }
    if (a->c != b->r) {
        set_err(P->s, "inner matrix dimensions must agree (%dx%d times %dx%d)", a->r, a->c, b->r, b->c);
        return NULL;
    }
    m = mat_mul(a, b);
    return need(P, m);
}

static Mat *do_power(Parser *P, const Mat *a, const Mat *b)
{
    Mat *result, *base, *t;
    long k;
    int negative;

    if (a->r == 1 && a->c == 1 && b->r == 1 && b->c == 1) {
        return need(P, mat_scalar(pow(a->d[0], b->d[0])));
    }
    if (!(b->r == 1 && b->c == 1) || a->r != a->c) {
        set_err(P->s, "^ needs a square matrix and a scalar integer power (use .^ for element-wise)");
        return NULL;
    }
    if (!is_int(b->d[0]) || fabs(b->d[0]) > 1000.0) {
        set_err(P->s, "matrix power must be an integer");
        return NULL;
    }
    k = (long)b->d[0];
    negative = k < 0;
    if (negative) {
        int st;

        base = mat_inverse(a, &st);
        if (base == NULL) {
            set_err(P->s, "%s", solve_message(st));
            return NULL;
        }
        k = -k;
    } else {
        base = need(P, mat_copy(a));
        if (base == NULL) {
            return NULL;
        }
    }
    result = need(P, mat_new(a->r, a->r));
    if (result == NULL) {
        mat_free(base);
        return NULL;
    }
    {
        int i;

        for (i = 0; i < a->r; ++i) {
            MAT_AT(result, i, i) = 1.0;
        }
    }
    while (k > 0 && result != NULL) {
        if (k & 1) {
            t = need(P, mat_mul(result, base));
            mat_free(result);
            result = t;
        }
        k >>= 1;
        if (k > 0 && result != NULL) {
            t = need(P, mat_mul(base, base));
            mat_free(base);
            base = t;
            if (base == NULL) {
                mat_free(result);
                return NULL;
            }
        }
    }
    mat_free(base);
    return result;
}

/* --- concatenation --- */

/* Both are consumed. An empty (0x0) operand is skipped. */
static Mat *hcat(Parser *P, Mat *a, Mat *b)
{
    Mat *m;
    int i, j;

    if (a == NULL || b == NULL) {
        mat_free(a);
        mat_free(b);
        return NULL;
    }
    if (MAT_N(a) == 0 && a->r == 0) {
        mat_free(a);
        return b;
    }
    if (MAT_N(b) == 0 && b->r == 0) {
        mat_free(b);
        return a;
    }
    if (a->r != b->r) {
        set_err(P->s, "horizontal dimensions are not consistent (%d rows and %d rows)", a->r, b->r);
        mat_free(a);
        mat_free(b);
        return NULL;
    }
    m = need(P, mat_new(a->r, a->c + b->c));
    if (m != NULL) {
        m->kind = (a->kind == MAT_CHAR || b->kind == MAT_CHAR) ? MAT_CHAR : MAT_NUM;
        for (i = 0; i < a->r; ++i) {
            for (j = 0; j < a->c; ++j) {
                MAT_AT(m, i, j) = MAT_AT(a, i, j);
            }
            for (j = 0; j < b->c; ++j) {
                MAT_AT(m, i, a->c + j) = MAT_AT(b, i, j);
            }
        }
    }
    mat_free(a);
    mat_free(b);
    return m;
}

static Mat *vcat(Parser *P, Mat *a, Mat *b)
{
    Mat *m;
    int i, j;

    if (a == NULL || b == NULL) {
        mat_free(a);
        mat_free(b);
        return NULL;
    }
    if (MAT_N(a) == 0 && a->r == 0) {
        mat_free(a);
        return b;
    }
    if (MAT_N(b) == 0 && b->r == 0) {
        mat_free(b);
        return a;
    }
    if (a->c != b->c) {
        set_err(P->s, "vertical dimensions are not consistent (%d columns and %d columns)", a->c, b->c);
        mat_free(a);
        mat_free(b);
        return NULL;
    }
    m = need(P, mat_new(a->r + b->r, a->c));
    if (m != NULL) {
        m->kind = (a->kind == MAT_CHAR || b->kind == MAT_CHAR) ? MAT_CHAR : MAT_NUM;
        for (j = 0; j < a->c; ++j) {
            for (i = 0; i < a->r; ++i) {
                MAT_AT(m, i, j) = MAT_AT(a, i, j);
            }
            for (i = 0; i < b->r; ++i) {
                MAT_AT(m, a->r + i, j) = MAT_AT(b, i, j);
            }
        }
    }
    mat_free(a);
    mat_free(b);
    return m;
}

/* --- indexing --- */

typedef struct {
    int all;                                    /* the lone colon */
    Mat *v;                                     /* otherwise the values */
} Arg;

static void free_args(Arg *a, int n)
{
    int i;

    for (i = 0; i < n; ++i) {
        mat_free(a[i].v);
        a[i].v = NULL;
    }
}

/* Converts one argument to zero-based indices in an int array of `count` entries; `limit` is the size of that dimension (0: no limit,
   for assignment). Returns the array (free it) or NULL. */
static int *indices(Parser *P, const Arg *a, int dim_size, int *count, int limit_check)
{
    int *ix, i;
    long n;

    if (a->all) {
        *count = dim_size;
        ix = (int *)malloc((size_t)(dim_size > 0 ? dim_size : 1) * sizeof(int));
        if (ix == NULL) {
            need(P, NULL);
            return NULL;
        }
        for (i = 0; i < dim_size; ++i) {
            ix[i] = i;
        }
        return ix;
    }
    n = MAT_N(a->v);
    if (a->v->kind == MAT_BOOL) {                 /* a mask: the places where it is true, down the columns */
        int ii, jj, k = 0;

        ix = (int *)malloc((size_t)(n > 0 ? n : 1) * sizeof(int));
        if (ix == NULL) {
            need(P, NULL);
            return NULL;
        }
        for (jj = 0; jj < a->v->c; ++jj) {
            for (ii = 0; ii < a->v->r; ++ii) {
                if (MAT_AT(a->v, ii, jj) != 0.0) {
                    long pos = (long)jj * a->v->r + ii;

                    if (limit_check && pos >= (long)dim_size) {
                        set_err(P->s, "the mask is longer than the matrix dimension %d", dim_size);
                        free(ix);
                        return NULL;
                    }
                    ix[k++] = (int)pos;
                }
            }
        }
        *count = k;
        return ix;
    }
    *count = (int)n;
    ix = (int *)malloc((size_t)(n > 0 ? n : 1) * sizeof(int));
    if (ix == NULL) {
        need(P, NULL);
        return NULL;
    }
    for (i = 0; i < (int)n; ++i) {
        double x = a->v->d[i];

        if (!is_int(x) || x < 1.0) {
            set_err(P->s, "subscript indices must be positive integers (got %g)", x);
            free(ix);
            return NULL;
        }
        if (limit_check && x > (double)dim_size) {
            set_err(P->s, "index %.0f exceeds the matrix dimension %d", x, dim_size);
            free(ix);
            return NULL;
        }
        ix[i] = (int)x - 1;
    }
    return ix;
}

static Mat *index_value(Parser *P, const Mat *src, Arg *a, int n)
{
    Mat *m = NULL;
    int *ri = NULL, *ci = NULL, nr, nc, i, j;

    if (n == 1) {
        long total = MAT_N(src);

        ri = indices(P, &a[0], (int)total, &nr, 1);
        if (ri == NULL) {
            return NULL;
        }
        if (a[0].all) {
            m = need(P, mat_new(nr, 1));
        } else if (a[0].v->kind == MAT_BOOL) {
            m = need(P, src->r == 1 ? mat_new(1, nr) : mat_new(nr, 1));
        } else if ((src->r == 1 || src->c == 1) && (a[0].v->r == 1 || a[0].v->c == 1)) {
            m = need(P, src->r == 1 && src->c != 1 ? mat_new(1, nr) : (src->c == 1 && src->r != 1 ? mat_new(nr, 1) : mat_new(a[0].v->r, a[0].v->c)));
        } else {
            m = need(P, mat_new(a[0].v->r, a[0].v->c));
        }
        if (m != NULL) {
            m->kind = src->kind;
            for (i = 0; i < nr; ++i) {
                int row = ri[i] % (src->r > 0 ? src->r : 1), col = ri[i] / (src->r > 0 ? src->r : 1);

                m->d[i] = MAT_AT(src, row, col);          /* linear order is down the columns, as in MATLAB */
            }
        }
        free(ri);
        return m;
    }
    ri = indices(P, &a[0], src->r, &nr, 1);
    ci = ri != NULL ? indices(P, &a[1], src->c, &nc, 1) : NULL;
    if (ri != NULL && ci != NULL) {
        m = need(P, mat_new(nr, nc));
        if (m != NULL) {
            m->kind = src->kind;
            for (i = 0; i < nr; ++i) {
                for (j = 0; j < nc; ++j) {
                    MAT_AT(m, i, j) = MAT_AT(src, ri[i], ci[j]);
                }
            }
        }
    }
    free(ri);
    free(ci);
    return m;
}

/* A(idx) = [] : removes elements (the result is a row, or a column if A was one), or A(i,:) = [] / A(:,j) = [] removes whole rows or columns.
   Takes the matrix (consumed); returns the new one or NULL. */
static Mat *delete_elements(Parser *P, Mat *cur, Arg *a, int n)
{
    int *ri = NULL, *ci = NULL, nr = 0, nc = 0, i, j, k;
    char *gone = NULL;
    Mat *m = NULL;

    if (n == 1) {
        long total = MAT_N(cur);

        ri = indices(P, &a[0], (int)total, &nr, 1);
        if (ri == NULL) {
            mat_free(cur);
            return NULL;
        }
        gone = (char *)calloc((size_t)(total > 0 ? total : 1), 1);
        if (gone == NULL) {
            free(ri);
            mat_free(cur);
            need(P, NULL);
            return NULL;
        }
        for (i = 0; i < nr; ++i) {
            gone[ri[i]] = 1;
        }
        for (i = 0, k = 0; i < (int)total; ++i) {
            if (!gone[i]) {
                ++k;
            }
        }
        m = need(P, cur->c == 1 && cur->r != 1 ? mat_new(k, 1) : mat_new(k > 0 || total == 0 ? 1 : 1, k));
        if (m != NULL) {
            m->kind = cur->kind;
            for (i = 0, k = 0; i < (int)total; ++i) {
                if (!gone[i]) {
                    m->d[k++] = MAT_AT(cur, i % cur->r, i / cur->r);   /* in column order */
                }
            }
        }
        free(gone);
        free(ri);
        mat_free(cur);
        return m;
    }
    ri = indices(P, &a[0], cur->r, &nr, 1);
    ci = ri != NULL ? indices(P, &a[1], cur->c, &nc, 1) : NULL;
    if (ri == NULL || ci == NULL) {
        free(ri);
        free(ci);
        mat_free(cur);
        return NULL;
    }
    if (a[0].all || nr == cur->r) {                       /* whole columns */
        gone = (char *)calloc((size_t)(cur->c > 0 ? cur->c : 1), 1);
        if (gone == NULL) {
            free(ri);
            free(ci);
            mat_free(cur);
            need(P, NULL);
            return NULL;
        }
        for (j = 0; j < nc; ++j) {
            gone[ci[j]] = 1;
        }
        for (j = 0, k = 0; j < cur->c; ++j) {
            if (!gone[j]) {
                ++k;
            }
        }
        m = need(P, mat_new(cur->r, k));
        if (m != NULL) {
            m->kind = cur->kind;
            for (j = 0, k = 0; j < cur->c; ++j) {
                if (!gone[j]) {
                    for (i = 0; i < cur->r; ++i) {
                        MAT_AT(m, i, k) = MAT_AT(cur, i, j);
                    }
                    ++k;
                }
            }
        }
    } else if (a[1].all || nc == cur->c) {                 /* whole rows */
        gone = (char *)calloc((size_t)(cur->r > 0 ? cur->r : 1), 1);
        if (gone == NULL) {
            free(ri);
            free(ci);
            mat_free(cur);
            need(P, NULL);
            return NULL;
        }
        for (i = 0; i < nr; ++i) {
            gone[ri[i]] = 1;
        }
        for (i = 0, k = 0; i < cur->r; ++i) {
            if (!gone[i]) {
                ++k;
            }
        }
        m = need(P, mat_new(k, cur->c));
        if (m != NULL) {
            m->kind = cur->kind;
            for (i = 0, k = 0; i < cur->r; ++i) {
                if (!gone[i]) {
                    for (j = 0; j < cur->c; ++j) {
                        MAT_AT(m, k, j) = MAT_AT(cur, i, j);
                    }
                    ++k;
                }
            }
        }
    } else {
        set_err(P->s, "A(i,j) = [] can only remove whole rows or whole columns");
    }
    free(gone);
    free(ri);
    free(ci);
    mat_free(cur);
    return m;
}

/* A(args) = rhs. Takes the variable's matrix (may be NULL = new), returns the new matrix (the old one is freed) or NULL on error. */
static Mat *assign_index(Parser *P, Mat *old, Arg *a, int n, const Mat *rhs)
{
    Mat *cur = old, *grown;
    int *ri = NULL, *ci = NULL, nr, nc, i, j, newr, newc, k;
    long count;

    int kind;

    if (cur == NULL) {
        cur = need(P, mat_new(0, 0));
        if (cur == NULL) {
            return NULL;
        }
    }
    kind = cur->kind == MAT_CHAR ? MAT_CHAR : (MAT_N(cur) == 0 ? rhs->kind : MAT_NUM);
    if (n == 1) {
        long maxi = -1;
        long total = MAT_N(cur);

        ri = indices(P, &a[0], (int)total, &nr, 0);
        if (ri == NULL) {
            mat_free(cur);
            return NULL;
        }
        for (i = 0; i < nr; ++i) {
            if (ri[i] > maxi) {
                maxi = ri[i];
            }
        }
        newr = cur->r;
        newc = cur->c;
        if (maxi >= total) {
            if (cur->r == 0 && cur->c == 0) {
                newr = 1;
                newc = (int)maxi + 1;
            } else if (cur->r == 1) {
                newc = (int)maxi + 1;
            } else if (cur->c == 1) {
                newr = (int)maxi + 1;
            } else {
                set_err(P->s, "A(I) = X: this would grow the matrix in an ambiguous direction");
                free(ri);
                mat_free(cur);
                return NULL;
            }
        }
        count = nr;
        if (!(rhs->r == 1 && rhs->c == 1) && MAT_N(rhs) != count) {
            set_err(P->s, "A(I) = X: X must have the same number of elements as I (%ld and %ld)", MAT_N(rhs), count);
            free(ri);
            mat_free(cur);
            return NULL;
        }
        grown = cur;
        if (newr != cur->r || newc != cur->c) {
            grown = need(P, mat_new(newr, newc));
            if (grown == NULL) {
                free(ri);
                mat_free(cur);
                return NULL;
            }
            for (i = 0; i < cur->r; ++i) {
                for (j = 0; j < cur->c; ++j) {
                    MAT_AT(grown, i, j) = MAT_AT(cur, i, j);
                }
            }
            mat_free(cur);
        }
        for (k = 0; k < nr; ++k) {
            int row = ri[k] % grown->r, col = ri[k] / grown->r;

            MAT_AT(grown, row, col) = rhs->r == 1 && rhs->c == 1 ? rhs->d[0] : rhs->d[k];
        }
        free(ri);
        grown->kind = kind;
        return grown;
    }
    ri = indices(P, &a[0], cur->r, &nr, 0);
    ci = ri != NULL ? indices(P, &a[1], cur->c, &nc, 0) : NULL;
    if (ri == NULL || ci == NULL) {
        free(ri);
        free(ci);
        mat_free(cur);
        return NULL;
    }
    newr = cur->r;
    newc = cur->c;
    for (i = 0; i < nr; ++i) {
        if (ri[i] + 1 > newr) {
            newr = ri[i] + 1;
        }
    }
    for (j = 0; j < nc; ++j) {
        if (ci[j] + 1 > newc) {
            newc = ci[j] + 1;
        }
    }
    if (a[0].all && cur->r == 0) {                /* A(:,j) = column on an empty A takes the rows of the right-hand side */
        newr = rhs->r;
        free(ri);
        nr = rhs->r;
        ri = (int *)malloc((size_t)(nr > 0 ? nr : 1) * sizeof(int));
        for (i = 0; ri != NULL && i < nr; ++i) {
            ri[i] = i;
        }
    }
    if (a[1].all && cur->c == 0) {
        newc = rhs->c;
        free(ci);
        nc = rhs->c;
        ci = (int *)malloc((size_t)(nc > 0 ? nc : 1) * sizeof(int));
        for (j = 0; ci != NULL && j < nc; ++j) {
            ci[j] = j;
        }
    }
    if (ri == NULL || ci == NULL) {
        free(ri);
        free(ci);
        mat_free(cur);
        need(P, NULL);
        return NULL;
    }
    if (!(rhs->r == 1 && rhs->c == 1) && !(rhs->r == nr && rhs->c == nc)) {
        set_err(P->s, "A(I,J) = X: X must be %dx%d to fit (it is %dx%d)", nr, nc, rhs->r, rhs->c);
        free(ri);
        free(ci);
        mat_free(cur);
        return NULL;
    }
    grown = cur;
    if (newr != cur->r || newc != cur->c) {
        grown = need(P, mat_new(newr, newc));
        if (grown == NULL) {
            free(ri);
            free(ci);
            mat_free(cur);
            return NULL;
        }
        for (i = 0; i < cur->r; ++i) {
            for (j = 0; j < cur->c; ++j) {
                MAT_AT(grown, i, j) = MAT_AT(cur, i, j);
            }
        }
        mat_free(cur);
    }
    for (i = 0; i < nr; ++i) {
        for (j = 0; j < nc; ++j) {
            MAT_AT(grown, ri[i], ci[j]) = rhs->r == 1 && rhs->c == 1 ? rhs->d[0] : MAT_AT(rhs, i, j);
        }
    }
    free(ri);
    free(ci);
    grown->kind = kind;
    return grown;
}

/* Counts the top-level, comma-separated arguments of the parenthesis that starts at p (p points just after the '('). */
static int count_args(const char *p)
{
    int depth = 1, n = 1;
    const char *q = p;

    while (*q == ' ' || *q == '\t') {
        ++q;
    }
    if (*q == ')') {
        return 0;
    }
    for (; *p != '\0' && depth > 0; ++p) {
        if (*p == '(' || *p == '[') {
            ++depth;
        } else if (*p == ')' || *p == ']') {
            --depth;
        } else if (*p == ',' && depth == 1) {
            ++n;
        }
    }
    return n;
}

/* Parses "a, b, c)" (the '(' is already consumed): fills args; returns the count or -1. `ctx` is the matrix whose dimensions `end` means
   (NULL for a function call: `end` then keeps the enclosing meaning). */
static int parse_arglist(Parser *P, Arg *args, const Mat *ctx)
{
    int n = 0, total = count_args(P->p), saved_brack = P->brack, pushed = 0;
    const char *q;

    if (ctx != NULL && P->endtop < 4) {
        P->endctx[P->endtop].r = ctx->r;
        P->endctx[P->endtop].c = ctx->c;
        P->endctx[P->endtop].n = total;
        ++P->endtop;
        pushed = 1;
    }
    P->brack = 0;
    for (;;) {
        SKIPWS(P);
        if (*P->p == ')') {
            ++P->p;
            break;
        }
        if (n >= MAXARGS) {
            set_err(P->s, "too many arguments");
            goto fail;
        }
        if (pushed) {
            P->endctx[P->endtop - 1].k = n;
        }
        q = P->p;
        if (*q == ':') {
            ++q;
            while (*q == ' ' || *q == '\t') {
                ++q;
            }
            if (*q == ',' || *q == ')') {
                args[n].all = 1;
                args[n].v = NULL;
                ++n;
                P->p = q;
                SKIPWS(P);
                if (*P->p == ',') {
                    ++P->p;
                }
                continue;
            }
        }
        args[n].all = 0;
        args[n].v = fast_expr(P, 3);
        if (args[n].v == NULL) {
            args[n].v = parse_expr(P);
        }
        if (args[n].v == NULL) {
            goto fail;
        }
        ++n;
        SKIPWS(P);
        if (*P->p == ',') {
            ++P->p;
        } else if (*P->p != ')') {
            set_err(P->s, "expected , or ) in the argument list");
            goto fail;
        }
    }
    P->brack = saved_brack;
    if (pushed) {
        --P->endtop;
    }
    return n;
fail:
    free_args(args, n);
    P->brack = saved_brack;
    if (pushed) {
        --P->endtop;
    }
    return -1;
}

/* --- built-in functions --- */

typedef Mat *(*BuiltinFn)(Parser *P, Mat **a, int n);

static int scalar_arg(Parser *P, const Mat *m, const char *fname, double *out)
{
    if (m->r != 1 || m->c != 1) {
        set_err(P->s, "%s needs a scalar here", fname);
        return 0;
    }
    *out = m->d[0];
    return 1;
}

static Mat *map1(Parser *P, const Mat *a, double (*f)(double))
{
    Mat *m = need(P, mat_new(a->r, a->c));
    long i;

    for (i = 0; m != NULL && i < MAT_N(a); ++i) {
        m->d[i] = f(a->d[i]);
    }
    return m;
}

static double f_sqrt(double x) { return x < 0.0 ? make_nan() : sqrt(x); }
static double f_log(double x) { return x < 0.0 ? make_nan() : (x == 0.0 ? make_special(0, 1) : log(x)); }
static double f_log10(double x) { return x < 0.0 ? make_nan() : (x == 0.0 ? make_special(0, 1) : log10(x)); }
static double f_asin(double x) { return fabs(x) > 1.0 ? make_nan() : asin(x); }
static double f_acos(double x) { return fabs(x) > 1.0 ? make_nan() : acos(x); }
static double f_abs(double x) { return fabs(x); }
static double f_fix(double x) { return x < 0.0 ? ceil(x) : floor(x); }
static double f_round(double x) { return x < 0.0 ? -floor(-x + 0.5) : floor(x + 0.5); }
static double f_sign(double x) { return x > 0.0 ? 1.0 : (x < 0.0 ? -1.0 : 0.0); }
static double f_sin(double x) { return sin(x); }
static double f_cos(double x) { return cos(x); }
static double f_tan(double x) { return tan(x); }
static double f_atan(double x) { return atan(x); }
static double f_exp(double x) { return exp(x); }
static double f_floor(double x) { return floor(x); }
static double f_ceil(double x) { return ceil(x); }

static Mat *b_sqrt(Parser *P, Mat **a, int n) { (void)n; return map1(P, a[0], f_sqrt); }
static Mat *b_log(Parser *P, Mat **a, int n) { (void)n; return map1(P, a[0], f_log); }
static Mat *b_log10(Parser *P, Mat **a, int n) { (void)n; return map1(P, a[0], f_log10); }
static Mat *b_asin(Parser *P, Mat **a, int n) { (void)n; return map1(P, a[0], f_asin); }
static Mat *b_acos(Parser *P, Mat **a, int n) { (void)n; return map1(P, a[0], f_acos); }
static Mat *b_abs(Parser *P, Mat **a, int n) { (void)n; return map1(P, a[0], f_abs); }
static Mat *b_fix(Parser *P, Mat **a, int n) { (void)n; return map1(P, a[0], f_fix); }
static Mat *b_round(Parser *P, Mat **a, int n) { (void)n; return map1(P, a[0], f_round); }
static Mat *b_sign(Parser *P, Mat **a, int n) { (void)n; return map1(P, a[0], f_sign); }
static Mat *b_sin(Parser *P, Mat **a, int n) { (void)n; return map1(P, a[0], f_sin); }
static Mat *b_cos(Parser *P, Mat **a, int n) { (void)n; return map1(P, a[0], f_cos); }
static Mat *b_tan(Parser *P, Mat **a, int n) { (void)n; return map1(P, a[0], f_tan); }
static Mat *b_atan(Parser *P, Mat **a, int n) { (void)n; return map1(P, a[0], f_atan); }
static Mat *b_exp(Parser *P, Mat **a, int n) { (void)n; return map1(P, a[0], f_exp); }
static Mat *b_floor(Parser *P, Mat **a, int n) { (void)n; return map1(P, a[0], f_floor); }
static Mat *b_ceil(Parser *P, Mat **a, int n) { (void)n; return map1(P, a[0], f_ceil); }

static Mat *b_atan2(Parser *P, Mat **a, int n)
{
    Mat *m;
    long i;

    (void)n;
    if (a[0]->r != a[1]->r || a[0]->c != a[1]->c) {
        set_err(P->s, "atan2 needs arguments of the same size");
        return NULL;
    }
    m = need(P, mat_new(a[0]->r, a[0]->c));
    for (i = 0; m != NULL && i < MAT_N(m); ++i) {
        m->d[i] = atan2(a[0]->d[i], a[1]->d[i]);
    }
    return m;
}

static Mat *b_mod(Parser *P, Mat **a, int n) { (void)n; return elementwise(P, 'm', a[0], a[1]); }
static Mat *b_rem(Parser *P, Mat **a, int n) { (void)n; return elementwise(P, 'r', a[0], a[1]); }

/* sum, prod, mean, max, min, cumsum: down the columns of a matrix, over the whole of a vector. */
static Mat *reduce(Parser *P, const Mat *a, int kind)
{
    Mat *m;
    int i, j, vec = (a->r == 1 || a->c == 1);
    int cols = vec ? 1 : a->c, len = vec ? (int)MAT_N(a) : a->r;

    if (kind == 'c') {                              /* cumsum keeps the shape */
        m = need(P, mat_new(a->r, a->c));
        if (m == NULL) {
            return NULL;
        }
        if (vec) {
            double s = 0.0;

            for (i = 0; i < (int)MAT_N(a); ++i) {
                s += a->d[i];
                m->d[i] = s;
            }
        } else {
            for (j = 0; j < a->c; ++j) {
                double s = 0.0;

                for (i = 0; i < a->r; ++i) {
                    s += MAT_AT(a, i, j);
                    MAT_AT(m, i, j) = s;
                }
            }
        }
        return m;
    }
    if (MAT_N(a) == 0) {
        return need(P, mat_scalar(kind == '*' ? 1.0 : (kind == '+' ? 0.0 : make_nan())));
    }
    m = need(P, mat_new(1, cols));
    if (m == NULL) {
        return NULL;
    }
    for (j = 0; j < cols; ++j) {
        double acc = 0.0;

        for (i = 0; i < len; ++i) {
            double x = vec ? a->d[i] : MAT_AT(a, i, j);

            if (i == 0) {
                acc = x;
            } else if (kind == '+' || kind == 'a') {
                acc += x;
            } else if (kind == '*') {
                acc *= x;
            } else if (kind == '>') {
                if (x > acc) {
                    acc = x;
                }
            } else if (kind == '<') {
                if (x < acc) {
                    acc = x;
                }
            }
        }
        m->d[j] = kind == 'a' ? acc / len : acc;
    }
    return m;
}

static Mat *b_cumsum(Parser *P, Mat **a, int n) { (void)n; return reduce(P, a[0], 'c'); }

static Mat *fill(Parser *P, Mat **a, int n, double value, const char *fname)
{
    double r, c;
    Mat *m;
    long i;

    if (n == 0) {
        return need(P, mat_scalar(value));
    }
    if (!scalar_arg(P, a[0], fname, &r)) {
        return NULL;
    }
    c = r;
    if (n > 1 && !scalar_arg(P, a[1], fname, &c)) {
        return NULL;
    }
    if (r < 0 || c < 0 || !is_int(r) || !is_int(c)) {
        set_err(P->s, "%s needs non-negative integer sizes", fname);
        return NULL;
    }
    m = need(P, mat_new((int)r, (int)c));
    for (i = 0; m != NULL && i < MAT_N(m); ++i) {
        m->d[i] = value;
    }
    return m;
}

static Mat *b_zeros(Parser *P, Mat **a, int n) { return fill(P, a, n, 0.0, "zeros"); }
static Mat *b_ones(Parser *P, Mat **a, int n) { return fill(P, a, n, 1.0, "ones"); }

static Mat *b_eye(Parser *P, Mat **a, int n)
{
    Mat *m = fill(P, a, n, 0.0, "eye");
    int i;

    for (i = 0; m != NULL && i < m->r && i < m->c; ++i) {
        MAT_AT(m, i, i) = 1.0;
    }
    return m;
}

static Mat *b_rand(Parser *P, Mat **a, int n)
{
    Mat *m = fill(P, a, n, 0.0, "rand");
    long i;

    for (i = 0; m != NULL && i < MAT_N(m); ++i) {
        P->s->seed = P->s->seed * 1103515245UL + 12345UL;
        m->d[i] = (double)((P->s->seed >> 8) & 0xFFFFUL) / 65536.0;
    }
    return m;
}

static Mat *b_linspace(Parser *P, Mat **a, int n)
{
    double lo, hi, cnt = 100.0;
    Mat *m;
    int i, k;

    if (!scalar_arg(P, a[0], "linspace", &lo) || !scalar_arg(P, a[1], "linspace", &hi) || (n > 2 && !scalar_arg(P, a[2], "linspace", &cnt))) {
        return NULL;
    }
    if (cnt < 1.0 || !is_int(cnt)) {
        set_err(P->s, "linspace needs a count of 1 or more");
        return NULL;
    }
    k = (int)cnt;
    m = need(P, mat_new(1, k));
    for (i = 0; m != NULL && i < k; ++i) {
        m->d[i] = k == 1 ? hi : lo + (hi - lo) * i / (k - 1);
    }
    return m;
}

static Mat *b_length(Parser *P, Mat **a, int n)
{
    (void)n;
    return need(P, mat_scalar(MAT_N(a[0]) == 0 ? 0.0 : (a[0]->r > a[0]->c ? a[0]->r : a[0]->c)));
}

static Mat *b_numel(Parser *P, Mat **a, int n) { (void)n; return need(P, mat_scalar((double)MAT_N(a[0]))); }

static Mat *b_transpose(Parser *P, Mat **a, int n) { (void)n; return need(P, mat_transpose(a[0])); }

static Mat *b_det(Parser *P, Mat **a, int n)
{
    int st;
    double d = mat_det(a[0], &st);

    (void)n;
    if (st != MAT_OK) {
        set_err(P->s, "%s", st == MAT_DIMS ? "det needs a square matrix" : (mat_aborted ? "interrupted" : "out of memory"));
        return NULL;
    }
    return need(P, mat_scalar(d));
}

static Mat *b_inv(Parser *P, Mat **a, int n)
{
    int st;
    Mat *m = mat_inverse(a[0], &st);

    (void)n;
    if (m == NULL) {
        set_err(P->s, "%s", st == MAT_DIMS ? "inv needs a square matrix" : solve_message(st));
    }
    return m;
}

static Mat *b_trace(Parser *P, Mat **a, int n)
{
    double t = 0.0;
    int i;

    (void)n;
    if (a[0]->r != a[0]->c) {
        set_err(P->s, "trace needs a square matrix");
        return NULL;
    }
    for (i = 0; i < a[0]->r; ++i) {
        t += MAT_AT(a[0], i, i);
    }
    return need(P, mat_scalar(t));
}

static Mat *b_rank(Parser *P, Mat **a, int n)
{
    int r = mat_rank(a[0]);

    (void)n;
    if (r < 0) {
        need(P, NULL);
        return NULL;
    }
    return need(P, mat_scalar(r));
}

static Mat *b_diag(Parser *P, Mat **a, int n)
{
    Mat *m;
    int i, k;

    (void)n;
    if (a[0]->r == 1 || a[0]->c == 1) {
        k = (int)MAT_N(a[0]);
        m = need(P, mat_new(k, k));
        for (i = 0; m != NULL && i < k; ++i) {
            MAT_AT(m, i, i) = a[0]->d[i];
        }
        return m;
    }
    k = a[0]->r < a[0]->c ? a[0]->r : a[0]->c;
    m = need(P, mat_new(k, 1));
    for (i = 0; m != NULL && i < k; ++i) {
        m->d[i] = MAT_AT(a[0], i, i);
    }
    return m;
}

static Mat *b_dot(Parser *P, Mat **a, int n)
{
    double s = 0.0;
    long i;

    (void)n;
    if (MAT_N(a[0]) != MAT_N(a[1])) {
        set_err(P->s, "dot needs vectors of the same length");
        return NULL;
    }
    for (i = 0; i < MAT_N(a[0]); ++i) {
        s += a[0]->d[i] * a[1]->d[i];
    }
    return need(P, mat_scalar(s));
}

static Mat *b_reshape(Parser *P, Mat **a, int n)
{
    double r, c;
    Mat *m;
    long i;

    (void)n;
    if (!scalar_arg(P, a[1], "reshape", &r) || !scalar_arg(P, a[2], "reshape", &c)) {
        return NULL;
    }
    if (r < 0 || c < 0 || !is_int(r) || !is_int(c) || (long)r * (long)c != MAT_N(a[0])) {
        set_err(P->s, "reshape: the number of elements must not change");
        return NULL;
    }
    m = need(P, mat_new((int)r, (int)c));
    for (i = 0; m != NULL && i < MAT_N(m); ++i) {                 /* column order, as in MATLAB */
        int sr = (int)(i % a[0]->r), sc = (int)(i / a[0]->r), dr = (int)(i % (int)r), dc = (int)(i / (int)r);

        MAT_AT(m, dr, dc) = MAT_AT(a[0], sr, sc);
    }
    return m;
}


#ifdef __WATCOMC__
#pragma code_seg("ENG_LIB", "CODE")
#endif

static Mat *bool_result(Parser *P, int v)
{
    Mat *m = need(P, mat_scalar(v ? 1.0 : 0.0));

    if (m != NULL) {
        m->kind = MAT_BOOL;
    }
    return m;
}

static Mat *make_string(Parser *P, const char *text)
{
    size_t n = strlen(text), i;
    Mat *m = need(P, mat_new(n > 0 ? 1 : 0, (int)n));

    for (i = 0; m != NULL && i < n; ++i) {
        m->d[i] = (double)(unsigned char)text[i];
    }
    if (m != NULL) {
        m->kind = MAT_CHAR;
    }
    return m;
}

/* A statement that makes no value (disp, fprintf, tic): an empty matrix that the statement runner throws away. */
static Mat *no_value(Parser *P)
{
    P->s->novalue = 1;
    return need(P, mat_new(0, 0));
}

static int is_true(const Mat *m)
{
    long i;

    if (MAT_N(m) == 0) {
        return 0;
    }
    for (i = 0; i < MAT_N(m); ++i) {
        if (m->d[i] == 0.0) {
            return 0;
        }
    }
    return 1;
}


static Mat *any_all(Parser *P, const Mat *a, int all)
{
    Mat *m;
    int i, j, vec = (a->r == 1 || a->c == 1);
    int cols = vec ? 1 : a->c, len = vec ? (int)MAT_N(a) : a->r;

    m = need(P, mat_new(1, cols));
    if (m == NULL) {
        return NULL;
    }
    m->kind = MAT_BOOL;
    for (j = 0; j < cols; ++j) {
        int acc = all ? 1 : 0;

        for (i = 0; i < len; ++i) {
            double x = vec ? a->d[i] : MAT_AT(a, i, j);

            if (all && x == 0.0) {
                acc = 0;
            }
            if (!all && x != 0.0) {
                acc = 1;
            }
        }
        m->d[j] = acc;
    }
    return m;
}

static Mat *b_any(Parser *P, Mat **a, int n) { (void)n; return any_all(P, a[0], 0); }
static Mat *b_all(Parser *P, Mat **a, int n) { (void)n; return any_all(P, a[0], 1); }
static Mat *b_isempty(Parser *P, Mat **a, int n) { (void)n; return bool_result(P, MAT_N(a[0]) == 0); }

static Mat *b_find(Parser *P, Mat **a, int n)
{
    long count = 0, i, k = 0;
    int j, r, c;
    Mat *m;

    (void)n;
    for (i = 0; i < MAT_N(a[0]); ++i) {
        if (a[0]->d[i] != 0.0) {
            ++count;
        }
    }
    m = need(P, a[0]->r == 1 && a[0]->c != 1 ? mat_new(1, (int)count) : mat_new((int)count, count > 0 ? 1 : 0));
    if (m == NULL) {
        return NULL;
    }
    for (j = 0; j < a[0]->c; ++j) {                    /* down the columns, as in MATLAB */
        for (r = 0; r < a[0]->r; ++r) {
            if (MAT_AT(a[0], r, j) != 0.0) {
                c = (int)k++;
                m->d[c] = (double)j * a[0]->r + r + 1;
            }
        }
    }
    return m;
}

static void shell_sort(double *v, long n)
{
    long gap, i, j;
    double t;

    for (gap = n / 2; gap > 0; gap /= 2) {
        for (i = gap; i < n; ++i) {
            t = v[i];
            for (j = i; j >= gap && v[j - gap] > t; j -= gap) {
                v[j] = v[j - gap];
            }
            v[j] = t;
        }
    }
}

static Mat *b_tic(Parser *P, Mat **a, int n)
{
    (void)a;
    (void)n;
    P->s->tic_at = clock();
    return no_value(P);
}

static Mat *b_toc(Parser *P, Mat **a, int n)
{
    (void)a;
    (void)n;
    return need(P, mat_scalar((double)(clock() - P->s->tic_at) / (double)CLOCKS_PER_SEC));
}

static Mat *b_disp(Parser *P, Mat **a, int n)
{
    (void)n;
    show_value(P->s, NULL, a[0]);
    return no_value(P);
}

static Mat *b_isequal_str(Parser *P, Mat **a, int n)
{
    long i;

    (void)n;
    if (a[0]->kind != MAT_CHAR || a[1]->kind != MAT_CHAR || a[0]->r != a[1]->r || a[0]->c != a[1]->c) {
        return bool_result(P, 0);
    }
    for (i = 0; i < MAT_N(a[0]); ++i) {
        if (a[0]->d[i] != a[1]->d[i]) {
            return bool_result(P, 0);
        }
    }
    return bool_result(P, 1);
}

static void num_to_text(double x, char *buf)
{
    if (is_nan(x) || is_inf(x)) {
        fmt_scalar(buf, x, 4);
    } else if (is_int(x) && fabs(x) < 1e15) {
        sprintf(buf, "%.0f", x);
    } else {
        int digits = (int)floor(log10(fabs(x) > 1e-300 ? fabs(x) : 1.0)) + 5;

        if (digits < 5) {
            digits = 5;
        }
        if (digits > 16) {
            digits = 16;
        }
        sprintf(buf, "%.*g", digits, x);
    }
}

static Mat *b_num2str(Parser *P, Mat **a, int n)
{
    char cell[40], line[400];
    int i, j, w = 0;
    Mat *m;
    long len;

    (void)n;
    if (a[0]->kind == MAT_CHAR) {
        return need(P, mat_copy(a[0]));
    }
    if (MAT_N(a[0]) == 1) {
        num_to_text(a[0]->d[0], cell);
        return make_string(P, cell);
    }
    for (i = 0; i < a[0]->r; ++i) {
        for (j = 0; j < a[0]->c; ++j) {
            num_to_text(MAT_AT(a[0], i, j), cell);
            if ((int)strlen(cell) > w) {
                w = (int)strlen(cell);
            }
        }
    }
    len = (long)a[0]->c * (w + 2) - 2;
    if (len < 0 || len > 380) {
        set_err(P->s, "num2str: the result would be too long");
        return NULL;
    }
    m = need(P, mat_new(a[0]->r, (int)len));
    if (m == NULL) {
        return NULL;
    }
    m->kind = MAT_CHAR;
    for (i = 0; i < a[0]->r; ++i) {
        line[0] = '\0';
        for (j = 0; j < a[0]->c; ++j) {
            char one[60];

            num_to_text(MAT_AT(a[0], i, j), cell);
            sprintf(one, "%*s%s", j > 0 ? 2 : 0, "", "");
            strcat(line, one);
            sprintf(one, "%*s", w, cell);
            strcat(line, one);
        }
        for (j = 0; j < (int)len; ++j) {
            MAT_AT(m, i, j) = (double)(unsigned char)line[j];
        }
    }
    return m;
}

/* --- printf-style formatting: sprintf, fprintf and error --- */

#define FMT_MAX 590
#define OUT_MAX 2000

typedef struct {
    Mat **a;
    int n, ai;
    long ei;
} Cursor;

static long items_in(const Mat *m)
{
    return m->kind == MAT_CHAR ? (MAT_N(m) > 0 ? 1 : 0) : MAT_N(m);
}

static void cursor_skip(Cursor *c)
{
    while (c->ai < c->n && c->ei >= items_in(c->a[c->ai])) {
        ++c->ai;
        c->ei = 0;
    }
}

static int cursor_more(Cursor *c)
{
    cursor_skip(c);
    return c->ai < c->n;
}

static int c_is_string(const Cursor *c)
{
    return c->a[c->ai]->kind == MAT_CHAR;
}

static long count_items(Mat **a, int n)
{
    long total = 0;
    int i;

    for (i = 0; i < n; ++i) {
        total += items_in(a[i]);
    }
    return total;
}

static void unescape(const char *in, char *out)
{
    int j = 0;

    for (; *in != '\0'; ++in) {
        if (*in == '\\' && in[1] != '\0') {
            ++in;
            switch (*in) {
            case 'n': out[j++] = '\n'; break;
            case 't': out[j++] = '\t'; break;
            case 'r': out[j++] = '\r'; break;
            case 'a': out[j++] = '\a'; break;
            case '\\': out[j++] = '\\'; break;
            case '\'': out[j++] = '\''; break;
            case '"': out[j++] = '"'; break;
            default: out[j++] = '\\'; out[j++] = *in; break;
            }
        } else {
            out[j++] = *in;
        }
    }
    out[j] = '\0';
}

static int put_text(Parser *P, char *out, int *len, const char *text)
{
    size_t n = strlen(text);

    if ((size_t)*len + n >= OUT_MAX) {
        set_err(P->s, "the formatted text is longer than %d characters", OUT_MAX);
        return 0;
    }
    memcpy(out + *len, text, n + 1);
    *len += (int)n;
    return 1;
}

/* Formats a[0] with the values of a[1..n-1] into out (OUT_MAX + 1 bytes). Returns 0 with an error set. */
static int format_text(Parser *P, Mat **a, int n, char *out)
{
    char raw[FMT_MAX + 1], fmt[FMT_MAX + 1], spec[40], piece[760], text[FMT_MAX + 1];
    int fl, i, j, k, len = 0, used, more_pass;
    long total;
    Cursor cur;

    if (a[0]->kind != MAT_CHAR || MAT_N(a[0]) > FMT_MAX) {
        set_err(P->s, "the format must be a string of at most %d characters", FMT_MAX);
        return 0;
    }
    fl = (int)MAT_N(a[0]);
    for (i = 0; i < fl; ++i) {
        raw[i] = (char)(int)a[0]->d[i];
    }
    raw[fl] = '\0';
    unescape(raw, fmt);
    fl = (int)strlen(fmt);
    out[0] = '\0';
    cur.a = a + 1;
    cur.n = n - 1;
    cur.ai = 0;
    cur.ei = 0;
    total = count_items(a + 1, n - 1);
    do {
        used = 0;
        for (i = 0; i < fl; ++i) {
            char conv, one[2];
            int isstr, havenum = 0;
            double v = 0.0;

            if (fmt[i] != '%') {
                one[0] = fmt[i];
                one[1] = '\0';
                if (!put_text(P, out, &len, one)) {
                    return 0;
                }
                continue;
            }
            if (fmt[i + 1] == '%') {
                if (!put_text(P, out, &len, "%")) {
                    return 0;
                }
                ++i;
                continue;
            }
            j = i + 1;
            k = 0;
            spec[k++] = '%';
            while (fmt[j] != '\0' && strchr("-+ 0#", fmt[j]) != NULL && k < 8) {
                spec[k++] = fmt[j++];
            }
            while (isdigit((unsigned char)fmt[j]) && k < 12) {
                spec[k++] = fmt[j++];
            }
            if (fmt[j] == '.') {
                spec[k++] = fmt[j++];
                while (isdigit((unsigned char)fmt[j]) && k < 18) {
                    spec[k++] = fmt[j++];
                }
            }
            spec[k] = '\0';
            conv = fmt[j];
            if (conv == '\0') {
                put_text(P, out, &len, spec);
                i = j - 1;
                continue;
            }
            if (!cursor_more(&cur)) {
                if (total == 0) {
                    i = j;                       /* no values at all: the conversions print nothing */
                    continue;
                }
                return 1;                        /* the values ran out in the middle of the format: stop here */
            }
            isstr = c_is_string(&cur);
            if (isstr) {
                const Mat *sm = cur.a[cur.ai];
                int t;

                for (t = 0; t < (int)MAT_N(sm) && t < FMT_MAX; ++t) {
                    text[t] = (char)(int)sm->d[t];
                }
                text[t] = '\0';
                ++cur.ei;
            } else {
                v = cur.a[cur.ai]->d[cur.ei++];
                havenum = 1;
            }
            ++used;
            if (strchr("diuxXoc", conv) != NULL || strchr("fFeEgG", conv) != NULL || conv == 's') {
                if (isstr) {
                    strcat(spec, "s");
                    sprintf(piece, spec, text);
                } else if (conv == 's') {
                    char num[40];

                    num_to_text(v, num);
                    strcat(spec, "s");
                    sprintf(piece, spec, num);
                } else if (is_nan(v) || is_inf(v)) {
                    char num[40];

                    fmt_scalar(num, v, 4);
                    k = (int)strlen(spec);
                    spec[k] = 's';
                    spec[k + 1] = '\0';
                    sprintf(piece, "%s", num);
                } else if (conv == 'c') {
                    strcat(spec, "c");
                    sprintf(piece, spec, (int)v);
                } else if (strchr("diu", conv) != NULL) {
                    if (is_int(v) && fabs(v) < 2e9) {
                        strcat(spec, "ld");
                        sprintf(piece, spec, (long)v);
                    } else {
                        strcat(spec, "g");
                        sprintf(piece, spec, v);
                    }
                } else if (strchr("xXo", conv) != NULL) {
                    char c2[3];

                    c2[0] = 'l';
                    c2[1] = conv;
                    c2[2] = '\0';
                    strcat(spec, c2);
                    sprintf(piece, spec, (long)v);
                } else {
                    char c1[2];

                    c1[0] = conv;
                    c1[1] = '\0';
                    strcat(spec, c1);
                    sprintf(piece, spec, v);
                }
                if (!put_text(P, out, &len, piece)) {
                    return 0;
                }
            } else {
                set_err(P->s, "unknown conversion %%%c in the format", conv);
                return 0;
            }
            (void)havenum;
            i = j;
        }
        more_pass = used > 0 && cursor_more(&cur);
    } while (more_pass);
    return 1;
}

static Mat *b_sprintf(Parser *P, Mat **a, int n)
{
    static char out[OUT_MAX + 1];

    if (!format_text(P, a, n, out)) {
        return NULL;
    }
    return make_string(P, out);
}

static Mat *b_fprintf(Parser *P, Mat **a, int n)
{
    static char out[OUT_MAX + 1];

    if (!format_text(P, a, n, out)) {
        return NULL;
    }
    emit(P->s, out);
    return no_value(P);
}

static Mat *b_error(Parser *P, Mat **a, int n)
{
    static char out[OUT_MAX + 1];

    if (!format_text(P, a, n, out)) {
        return NULL;
    }
    set_err(P->s, "%s", out);
    return NULL;
}


/* --- M05: several results, sums along a dimension, statistics, polynomials, eigenvalues, singular values --- */

static Mat *reduce_dim(Parser *P, const Mat *a, int kind, int dim)
{
    Mat *m;
    int i, j;

    if (dim != 1 && dim != 2) {
        set_err(P->s, "the dimension must be 1 (down the columns) or 2 (along the rows)");
        return NULL;
    }
    m = need(P, dim == 1 ? mat_new(1, a->c) : mat_new(a->r, 1));
    if (m == NULL) {
        return NULL;
    }
    for (j = 0; j < (dim == 1 ? a->c : a->r); ++j) {
        int len = dim == 1 ? a->r : a->c;
        double acc = kind == '*' ? 1.0 : 0.0;

        for (i = 0; i < len; ++i) {
            double x = dim == 1 ? MAT_AT(a, i, j) : MAT_AT(a, j, i);

            acc = kind == '*' ? acc * x : acc + x;
        }
        m->d[j] = kind == 'a' ? (len > 0 ? acc / len : make_nan()) : acc;
    }
    return m;
}

static Mat *b_sum(Parser *P, Mat **a, int n)
{
    return n == 2 ? reduce_dim(P, a[0], '+', (int)a[1]->d[0]) : reduce(P, a[0], '+');
}

static Mat *b_prod(Parser *P, Mat **a, int n)
{
    return n == 2 ? reduce_dim(P, a[0], '*', (int)a[1]->d[0]) : reduce(P, a[0], '*');
}

static Mat *b_mean(Parser *P, Mat **a, int n)
{
    return n == 2 ? reduce_dim(P, a[0], 'a', (int)a[1]->d[0]) : reduce(P, a[0], 'a');
}

static Mat *no_extra(Parser *P)
{
    P->s->n_extra = 0;
    return NULL;
}

/* max and min: of a list, down the columns, of two arrays, and with [m, i] = max(v) the place of the first one too. */
static Mat *max_min(Parser *P, Mat **a, int n, int is_max)
{
    const Mat *x = a[0];
    int vec, cols, len, i, j, best;
    Mat *m, *idx;

    if (n == 2) {
        return elementwise(P, is_max ? 'M' : 'N', a[0], a[1]);
    }
    if (P->s->want_out < 2) {
        return reduce(P, a[0], is_max ? '>' : '<');
    }
    if (MAT_N(x) == 0) {
        set_err(P->s, "%s of an empty matrix has no place", is_max ? "max" : "min");
        return NULL;
    }
    vec = x->r == 1 || x->c == 1;
    cols = vec ? 1 : x->c;
    len = vec ? (int)MAT_N(x) : x->r;
    m = need(P, mat_new(1, cols));
    idx = need(P, mat_new(1, cols));
    if (m == NULL || idx == NULL) {
        mat_free(m);
        mat_free(idx);
        return no_extra(P);
    }
    for (j = 0; j < cols; ++j) {
        best = 0;
        for (i = 1; i < len; ++i) {
            double v = vec ? x->d[i] : MAT_AT(x, i, j), b = vec ? x->d[best] : MAT_AT(x, best, j);

            if (is_max ? v > b : v < b) {
                best = i;
            }
        }
        m->d[j] = vec ? x->d[best] : MAT_AT(x, best, j);
        idx->d[j] = best + 1;
    }
    P->s->extra[0] = idx;
    P->s->n_extra = 1;
    return m;
}

static Mat *b_max(Parser *P, Mat **a, int n) { return max_min(P, a, n, 1); }
static Mat *b_min(Parser *P, Mat **a, int n) { return max_min(P, a, n, 0); }

static Mat *b_size(Parser *P, Mat **a, int n)
{
    Mat *m;
    double dim;

    if (n == 2) {
        if (!scalar_arg(P, a[1], "size", &dim)) {
            return NULL;
        }
        if (dim != 1.0 && dim != 2.0) {
            set_err(P->s, "size(A,dim): dim must be 1 or 2");
            return NULL;
        }
        return need(P, mat_scalar(dim == 1.0 ? a[0]->r : a[0]->c));
    }
    if (P->s->want_out >= 2) {                                 /* [r, c] = size(A) */
        m = need(P, mat_scalar(a[0]->r));
        P->s->extra[0] = need(P, mat_scalar(a[0]->c));
        P->s->n_extra = P->s->extra[0] != NULL ? 1 : 0;
        return m;
    }
    m = need(P, mat_new(1, 2));
    if (m != NULL) {
        m->d[0] = a[0]->r;
        m->d[1] = a[0]->c;
    }
    return m;
}

/* Sorts v (n numbers) up, carrying the places along; equal numbers keep their order. */
static void shell_sort_idx(double *v, double *ix, long n)
{
    long gap, i, j;
    double t, ti;

    for (gap = n / 2; gap > 0; gap /= 2) {
        for (i = gap; i < n; ++i) {
            t = v[i];
            ti = ix[i];
            for (j = i; j >= gap && (v[j - gap] > t || (v[j - gap] == t && ix[j - gap] > ti)); j -= gap) {
                v[j] = v[j - gap];
                ix[j] = ix[j - gap];
            }
            v[j] = t;
            ix[j] = ti;
        }
    }
}

static Mat *b_sort(Parser *P, Mat **a, int n)
{
    Mat *m = need(P, mat_copy(a[0])), *idx = NULL;
    int j, i;
    double *col = NULL, *ci = NULL;

    (void)n;
    if (m == NULL) {
        return NULL;
    }
    if (P->s->want_out >= 2) {
        idx = need(P, mat_new(m->r, m->c));
        if (idx == NULL) {
            mat_free(m);
            return no_extra(P);
        }
    }
    if (m->r == 1 || m->c == 1) {
        if (idx != NULL) {
            for (i = 0; i < (int)MAT_N(m); ++i) {
                idx->d[i] = i + 1;
            }
            shell_sort_idx(m->d, idx->d, MAT_N(m));
        } else {
            shell_sort(m->d, MAT_N(m));
        }
    } else {
        col = (double *)malloc((size_t)m->r * sizeof(double));
        ci = (double *)malloc((size_t)m->r * sizeof(double));
        if (col == NULL || ci == NULL) {
            free(col);
            free(ci);
            mat_free(m);
            mat_free(idx);
            need(P, NULL);
            return no_extra(P);
        }
        for (j = 0; j < m->c; ++j) {
            for (i = 0; i < m->r; ++i) {
                col[i] = MAT_AT(m, i, j);
                ci[i] = i + 1;
            }
            if (idx != NULL) {
                shell_sort_idx(col, ci, m->r);
            } else {
                shell_sort(col, m->r);
            }
            for (i = 0; i < m->r; ++i) {
                MAT_AT(m, i, j) = col[i];
                if (idx != NULL) {
                    MAT_AT(idx, i, j) = ci[i];
                }
            }
        }
        free(col);
        free(ci);
    }
    if (idx != NULL) {
        P->s->extra[0] = idx;
        P->s->n_extra = 1;
    }
    return m;
}

/* median, std, var, diff, cumprod */
static Mat *columnwise(Parser *P, const Mat *a, int kind)
{
    int vec = (a->r == 1 || a->c == 1), cols = vec ? 1 : a->c, len = vec ? (int)MAT_N(a) : a->r, i, j;
    Mat *m;
    double *col;

    if (len == 0) {
        return need(P, mat_scalar(make_nan()));
    }
    m = need(P, mat_new(1, cols));
    col = (double *)malloc((size_t)len * sizeof(double));
    if (m == NULL || col == NULL) {
        mat_free(m);
        free(col);
        need(P, NULL);
        return NULL;
    }
    for (j = 0; j < cols; ++j) {
        for (i = 0; i < len; ++i) {
            col[i] = vec ? a->d[i] : MAT_AT(a, i, j);
        }
        if (kind == 'm') {                                       /* median */
            shell_sort(col, len);
            m->d[j] = len % 2 ? col[len / 2] : 0.5 * (col[len / 2 - 1] + col[len / 2]);
        } else {                                                 /* standard deviation or variance, with n - 1 */
            double mean = 0.0, ss = 0.0;

            for (i = 0; i < len; ++i) {
                mean += col[i];
            }
            mean /= len;
            for (i = 0; i < len; ++i) {
                ss += (col[i] - mean) * (col[i] - mean);
            }
            ss = len > 1 ? ss / (len - 1) : 0.0;
            m->d[j] = kind == 's' ? sqrt(ss) : ss;
        }
    }
    free(col);
    return m;
}

static Mat *b_median(Parser *P, Mat **a, int n) { (void)n; return columnwise(P, a[0], 'm'); }
static Mat *b_std(Parser *P, Mat **a, int n) { (void)n; return columnwise(P, a[0], 's'); }
static Mat *b_var(Parser *P, Mat **a, int n) { (void)n; return columnwise(P, a[0], 'v'); }

static Mat *b_diff(Parser *P, Mat **a, int n)
{
    const Mat *x = a[0];
    Mat *m;
    int i, j;

    (void)n;
    if (x->r == 1 || x->c == 1) {
        long len = MAT_N(x);

        m = need(P, x->r == 1 ? mat_new(1, len > 0 ? (int)len - 1 : 0) : mat_new(len > 0 ? (int)len - 1 : 0, 1));
        for (i = 0; m != NULL && i + 1 < (int)len; ++i) {
            m->d[i] = x->d[i + 1] - x->d[i];
        }
        return m;
    }
    m = need(P, mat_new(x->r - 1, x->c));
    for (j = 0; m != NULL && j < x->c; ++j) {
        for (i = 0; i + 1 < x->r; ++i) {
            MAT_AT(m, i, j) = MAT_AT(x, i + 1, j) - MAT_AT(x, i, j);
        }
    }
    return m;
}

static Mat *b_cumprod(Parser *P, Mat **a, int n)
{
    Mat *m = need(P, mat_copy(a[0]));
    int i, j;

    (void)n;
    if (m == NULL) {
        return NULL;
    }
    if (m->r == 1 || m->c == 1) {
        for (i = 1; i < (int)MAT_N(m); ++i) {
            m->d[i] *= m->d[i - 1];
        }
        return m;
    }
    for (j = 0; j < m->c; ++j) {
        for (i = 1; i < m->r; ++i) {
            MAT_AT(m, i, j) *= MAT_AT(m, i - 1, j);
        }
    }
    return m;
}

/* polyfit(x, y, d): the coefficients (highest power first, as a row) of the degree-d polynomial that fits the points best. */
static Mat *b_polyfit(Parser *P, Mat **a, int n)
{
    double dd;
    int d, i, j, st;
    long len = MAT_N(a[0]);
    Mat *vand, *rhs, *sol, *row;

    (void)n;
    if (!scalar_arg(P, a[2], "polyfit", &dd) || dd < 0.0 || !is_int(dd) || dd > 20.0) {
        set_err(P->s, "polyfit: the degree must be a whole number from 0 to 20");
        return NULL;
    }
    d = (int)dd;
    if (len != MAT_N(a[1]) || len < d + 1) {
        set_err(P->s, "polyfit needs x and y of the same length, at least degree + 1 points");
        return NULL;
    }
    vand = need(P, mat_new((int)len, d + 1));
    rhs = need(P, mat_new((int)len, 1));
    if (vand == NULL || rhs == NULL) {
        mat_free(vand);
        mat_free(rhs);
        return NULL;
    }
    for (i = 0; i < (int)len; ++i) {
        double p = 1.0;

        for (j = d; j >= 0; --j) {
            MAT_AT(vand, i, j) = p;
            p *= a[0]->d[i];
        }
        rhs->d[i] = a[1]->d[i];
    }
    sol = mat_solve(vand, rhs, &st);
    mat_free(vand);
    mat_free(rhs);
    if (sol == NULL) {
        set_err(P->s, "polyfit: the points do not determine a polynomial of degree %d", d);
        return NULL;
    }
    row = need(P, mat_transpose(sol));
    mat_free(sol);
    return row;
}

static Mat *b_polyval(Parser *P, Mat **a, int n)
{
    Mat *m = need(P, mat_new(a[1]->r, a[1]->c));
    long i, k;

    (void)n;
    for (i = 0; m != NULL && i < MAT_N(a[1]); ++i) {
        double v = 0.0;

        for (k = 0; k < MAT_N(a[0]); ++k) {
            v = v * a[1]->d[i] + a[0]->d[k];
        }
        m->d[i] = v;
    }
    return m;
}

static Mat *b_trapz(Parser *P, Mat **a, int n)
{
    const Mat *y = a[n - 1], *x = n == 2 ? a[0] : NULL;
    double sum = 0.0;
    long i;

    if (x != NULL && MAT_N(x) != MAT_N(y)) {
        set_err(P->s, "trapz needs x and y of the same length");
        return NULL;
    }
    for (i = 0; i + 1 < MAT_N(y); ++i) {
        sum += (x != NULL ? x->d[i + 1] - x->d[i] : 1.0) * 0.5 * (y->d[i] + y->d[i + 1]);
    }
    return need(P, mat_scalar(sum));
}

static Mat *make_diag(Parser *P, const double *w, int n)
{
    Mat *m = need(P, mat_new(n, n));
    int i;

    for (i = 0; m != NULL && i < n; ++i) {
        MAT_AT(m, i, i) = w[i];
    }
    return m;
}

static const char *la_message(int rc)
{
    return rc == LA_NOCONV ? "the iteration did not converge" : (mat_aborted ? "interrupted" : "out of memory");
}

static int all_finite(const Mat *a)
{
    long i;

    for (i = 0; i < MAT_N(a); ++i) {
        if (is_nan(a->d[i]) || is_inf(a->d[i])) {
            return 0;
        }
    }
    return 1;
}

static int check_square(Parser *P, const Mat *a, const char *what)
{
    if (a->r != a->c || a->kind == MAT_CHAR) {
        set_err(P->s, "%s needs a square matrix of numbers", what);
        return 0;
    }
    if (!all_finite(a)) {
        set_err(P->s, "%s: the matrix has NaN or Inf in it", what);
        return 0;
    }
    return 1;
}

/* eig: the eigenvalues as a column when they are all real; [V, D] = eig(A) also the vectors. */
static Mat *b_eig(Parser *P, Mat **a, int n)
{
    const Mat *A = a[0];
    int N = A->r, want = P->s->want_out, i, j, rc;
    double *w, *wi = NULL;
    Mat *V = NULL, *out;

    (void)n;
    if (!check_square(P, A, "eig")) {
        return NULL;
    }
    if (N == 0) {
        return need(P, mat_new(0, 0));
    }
    w = (double *)malloc((size_t)N * sizeof(double));
    wi = (double *)malloc((size_t)N * sizeof(double));
    if (w == NULL || wi == NULL) {
        free(w);
        free(wi);
        need(P, NULL);
        return NULL;
    }
    if (want >= 2) {
        V = need(P, mat_new(N, N));
    }
    if (la_is_symmetric(A)) {
        rc = la_symmetric(A, w, V);
    } else {
        rc = la_eigenvalues(A, w, wi);
        for (i = 0; rc == LA_OK && i < N; ++i) {
            if (fabs(wi[i]) > 1e-9 * (1.0 + fabs(w[i]))) {
                set_err(P->s, "eig: the matrix has complex eigenvalues (%g %+gi, ...): use eigc(A), which gives real and imaginary parts", w[i], wi[i]);
                rc = -1;
            }
        }
        for (j = 0; rc == LA_OK && V != NULL && j < N; ++j) {
            double *col = (double *)malloc((size_t)N * sizeof(double));

            if (col == NULL) {
                rc = LA_MEMORY;
                break;
            }
            rc = la_eigenvector(A, w[j], col);
            for (i = 0; rc == LA_OK && i < N; ++i) {
                MAT_AT(V, i, j) = col[i];
            }
            free(col);
        }
    }
    if (rc != LA_OK) {
        if (rc > 0) {
            set_err(P->s, "eig: %s", la_message(rc));
        }
        free(w);
        free(wi);
        mat_free(V);
        return no_extra(P);
    }
    if (want >= 2) {
        out = V;
        P->s->extra[0] = make_diag(P, w, N);
        P->s->n_extra = P->s->extra[0] != NULL ? 1 : 0;
    } else {
        out = need(P, mat_new(N, 1));
        for (i = 0; out != NULL && i < N; ++i) {
            out->d[i] = w[i];
        }
    }
    free(w);
    free(wi);
    return out;
}

/* eigc: all the eigenvalues of a real matrix, complex ones included, as two columns [real imaginary]. */
static Mat *b_eigc(Parser *P, Mat **a, int n)
{
    const Mat *A = a[0];
    int N = A->r, i, rc;
    double *w, *wi;
    Mat *out;

    (void)n;
    if (!check_square(P, A, "eigc")) {
        return NULL;
    }
    if (N == 0) {
        return need(P, mat_new(0, 2));
    }
    w = (double *)malloc((size_t)N * sizeof(double));
    wi = (double *)malloc((size_t)N * sizeof(double));
    if (w == NULL || wi == NULL) {
        free(w);
        free(wi);
        need(P, NULL);
        return NULL;
    }
    if (la_is_symmetric(A)) {
        rc = la_symmetric(A, w, NULL);
        for (i = 0; i < N; ++i) {
            wi[i] = 0.0;
        }
    } else {
        rc = la_eigenvalues(A, w, wi);
    }
    if (rc != LA_OK) {
        set_err(P->s, "eigc: %s", la_message(rc));
        free(w);
        free(wi);
        return NULL;
    }
    out = need(P, mat_new(N, 2));
    for (i = 0; out != NULL && i < N; ++i) {
        MAT_AT(out, i, 0) = w[i];
        MAT_AT(out, i, 1) = wi[i];
    }
    free(w);
    free(wi);
    return out;
}

/* The economy-size SVD of any matrix: U is m x k, V is n x k, k = min(m, n). s is made here (free it). */
static int svd_economy(Parser *P, const Mat *A, Mat **U, double **s, Mat **V)
{
    int m = A->r, nn = A->c, k = m < nn ? m : nn, tr = m < nn, rc;
    Mat *work, *Uw, *Vw;
    double *sv;

    if (A->kind == MAT_CHAR || !all_finite(A)) {
        set_err(P->s, "svd needs a matrix of finite numbers");
        return 0;
    }
    work = need(P, tr ? mat_transpose(A) : mat_copy(A));
    Uw = need(P, mat_new(work != NULL ? work->r : 0, k));
    Vw = need(P, mat_new(k, k));
    sv = (double *)malloc((size_t)(k > 0 ? k : 1) * sizeof(double));
    if (work == NULL || Uw == NULL || Vw == NULL || sv == NULL) {
        mat_free(work);
        mat_free(Uw);
        mat_free(Vw);
        free(sv);
        need(P, NULL);
        return 0;
    }
    rc = k == 0 ? LA_OK : la_svd(work, Uw, sv, Vw);
    mat_free(work);
    if (rc != LA_OK) {
        set_err(P->s, "svd: %s", la_message(rc));
        mat_free(Uw);
        mat_free(Vw);
        free(sv);
        return 0;
    }
    *U = tr ? Vw : Uw;                                    /* for a wide matrix the roles swap */
    *V = tr ? Uw : Vw;
    *s = sv;
    return 1;
}

static Mat *b_svd(Parser *P, Mat **a, int n)
{
    Mat *U, *V, *out;
    double *s;
    int k = a[0]->r < a[0]->c ? a[0]->r : a[0]->c, i;

    (void)n;
    if (!svd_economy(P, a[0], &U, &s, &V)) {
        return no_extra(P);
    }
    if (P->s->want_out >= 3) {
        out = U;
        P->s->extra[0] = make_diag(P, s, k);
        P->s->extra[1] = V;
        P->s->n_extra = 2;
        free(s);
        return out;
    }
    mat_free(U);
    mat_free(V);
    out = need(P, mat_new(k, 1));
    for (i = 0; out != NULL && i < k; ++i) {
        out->d[i] = s[i];
    }
    free(s);
    return out;
}

static Mat *b_cond(Parser *P, Mat **a, int n)
{
    Mat *U, *V;
    double *s, c;
    int k = a[0]->r < a[0]->c ? a[0]->r : a[0]->c;

    (void)n;
    if (k == 0) {
        return need(P, mat_scalar(0.0));
    }
    if (!svd_economy(P, a[0], &U, &s, &V)) {
        return NULL;
    }
    c = s[k - 1] > 0.0 ? s[0] / s[k - 1] : make_inf();
    mat_free(U);
    mat_free(V);
    free(s);
    return need(P, mat_scalar(c));
}

static Mat *b_pinv(Parser *P, Mat **a, int n)
{
    Mat *U, *V, *out;
    double *s, tol;
    int m = a[0]->r, nn = a[0]->c, k = m < nn ? m : nn, i, j, t;

    (void)n;
    if (k == 0) {
        return need(P, mat_new(nn, m));
    }
    if (!svd_economy(P, a[0], &U, &s, &V)) {
        return NULL;
    }
    tol = (m > nn ? m : nn) * s[0] * 2.220446049250313e-16;
    out = need(P, mat_new(nn, m));
    for (i = 0; out != NULL && i < nn; ++i) {
        for (j = 0; j < m; ++j) {
            double v = 0.0;

            for (t = 0; t < k; ++t) {
                if (s[t] > tol) {
                    v += MAT_AT(V, i, t) * MAT_AT(U, j, t) / s[t];
                }
            }
            MAT_AT(out, i, j) = v;
        }
    }
    mat_free(U);
    mat_free(V);
    free(s);
    return out;
}

/* norm(x), norm(x, p) with p = 1, 2, Inf, any other number for vectors, or 'fro'. For a matrix the 2-norm is the largest singular value. */
static Mat *b_norm(Parser *P, Mat **a, int n)
{
    const Mat *x = a[0];
    double p = 2.0, v = 0.0;
    int fro = 0, vec = x->r == 1 || x->c == 1;
    long i;
    int r, c;

    if (n == 2) {
        if (a[1]->kind == MAT_CHAR) {
            char word[8];
            int k;

            for (k = 0; k < (int)MAT_N(a[1]) && k < 7; ++k) {
                word[k] = (char)tolower((int)a[1]->d[k]);
            }
            word[k] = '\0';
            if (strcmp(word, "fro") == 0) {
                fro = 1;
            } else if (strcmp(word, "inf") == 0) {
                p = make_inf();
            } else {
                set_err(P->s, "norm: the second argument is a number, Inf, or 'fro'");
                return NULL;
            }
        } else if (!scalar_arg(P, a[1], "norm", &p)) {
            return NULL;
        }
    }
    if (MAT_N(x) == 0) {
        return need(P, mat_scalar(0.0));
    }
    if (fro || (vec && p == 2.0)) {
        for (i = 0; i < MAT_N(x); ++i) {
            v += x->d[i] * x->d[i];
        }
        return need(P, mat_scalar(sqrt(v)));
    }
    if (vec) {
        if (is_inf(p)) {
            for (i = 0; i < MAT_N(x); ++i) {
                if (fabs(x->d[i]) > v) {
                    v = fabs(x->d[i]);
                }
            }
        } else if (p >= 1.0) {
            for (i = 0; i < MAT_N(x); ++i) {
                v += pow(fabs(x->d[i]), p);
            }
            v = pow(v, 1.0 / p);
        } else {
            set_err(P->s, "norm: p must be 1 or more");
            return NULL;
        }
        return need(P, mat_scalar(v));
    }
    if (p == 1.0) {                                      /* the largest column sum */
        for (c = 0; c < x->c; ++c) {
            double sum = 0.0;

            for (r = 0; r < x->r; ++r) {
                sum += fabs(MAT_AT(x, r, c));
            }
            if (sum > v) {
                v = sum;
            }
        }
        return need(P, mat_scalar(v));
    }
    if (is_inf(p)) {                                     /* the largest row sum */
        for (r = 0; r < x->r; ++r) {
            double sum = 0.0;

            for (c = 0; c < x->c; ++c) {
                sum += fabs(MAT_AT(x, r, c));
            }
            if (sum > v) {
                v = sum;
            }
        }
        return need(P, mat_scalar(v));
    }
    if (p == 2.0) {                                      /* the largest singular value */
        Mat *U, *V;
        double *s;

        if (!svd_economy(P, x, &U, &s, &V)) {
            return NULL;
        }
        v = s[0];
        mat_free(U);
        mat_free(V);
        free(s);
        return need(P, mat_scalar(v));
    }
    set_err(P->s, "norm of a matrix: p must be 1, 2, Inf or 'fro'");
    return NULL;
}

/* The roots of the polynomial p (highest power first) as the eigenvalues of its companion matrix. */
static Mat *roots_of(Parser *P, const Mat *p, int complex_ok)
{
    long len = MAT_N(p), first = 0, last;
    int deg, zeros, i, rc, N;
    Mat *comp, *out;
    double *wr, *wi;

    while (first < len && p->d[first] == 0.0) {
        ++first;
    }
    last = len;
    while (last > first && p->d[last - 1] == 0.0) {
        --last;
    }
    zeros = (int)(len - last);                           /* roots at zero */
    deg = (int)(last - first) - 1;
    if (deg < 0) {
        return need(P, mat_new(0, complex_ok ? 2 : 1));
    }
    N = deg + zeros;
    wr = (double *)malloc((size_t)(N > 0 ? N : 1) * sizeof(double));
    wi = (double *)malloc((size_t)(N > 0 ? N : 1) * sizeof(double));
    if (wr == NULL || wi == NULL) {
        free(wr);
        free(wi);
        need(P, NULL);
        return NULL;
    }
    for (i = 0; i < N; ++i) {
        wr[i] = wi[i] = 0.0;
    }
    if (deg >= 1) {
        comp = need(P, mat_new(deg, deg));
        if (comp == NULL) {
            free(wr);
            free(wi);
            return NULL;
        }
        for (i = 0; i < deg; ++i) {
            comp->d[i] = -p->d[first + 1 + i] / p->d[first];
            if (i + 1 < deg) {
                MAT_AT(comp, i + 1, i) = 1.0;
            }
        }
        rc = la_eigenvalues(comp, wr, wi);
        mat_free(comp);
        if (rc != LA_OK) {
            set_err(P->s, "roots: %s", la_message(rc));
            free(wr);
            free(wi);
            return NULL;
        }
    }
    for (i = 1; i < N; ++i) {                              /* all the roots in order: real part, then imaginary part */
        double re = wr[i], im = wi[i];
        int j2;

        for (j2 = i - 1; j2 >= 0 && (wr[j2] > re || (wr[j2] == re && wi[j2] > im)); --j2) {
            wr[j2 + 1] = wr[j2];
            wi[j2 + 1] = wi[j2];
        }
        wr[j2 + 1] = re;
        wi[j2 + 1] = im;
    }
    out = need(P, mat_new(N, complex_ok ? 2 : 1));
    for (i = 0; out != NULL && i < N; ++i) {
        if (!complex_ok && fabs(wi[i]) > 1e-9 * (1.0 + fabs(wr[i]))) {
            set_err(P->s, "roots: the polynomial has complex roots (%g %+gi, ...): use rootsc(p)", wr[i], wi[i]);
            mat_free(out);
            out = NULL;
            break;
        }
        MAT_AT(out, i, 0) = wr[i];
        if (complex_ok) {
            MAT_AT(out, i, 1) = wi[i];
        }
    }
    free(wr);
    free(wi);
    return out;
}

static Mat *b_roots(Parser *P, Mat **a, int n) { (void)n; return roots_of(P, a[0], 0); }
static Mat *b_rootsc(Parser *P, Mat **a, int n) { (void)n; return roots_of(P, a[0], 1); }

#ifdef MS_FFT
/* --- the Fourier transform (XD100, the Linux build only). There are no complex numbers in the language, so a complex list is two columns [real imaginary], as in
   eigc and rootsc. fft(x) and ifft(X) take a real vector (any orientation) or such a two-column matrix and always give n by 2; cabs and cangle turn [re im] into
   the magnitude and the phase (a column). --- */
static Mat *fourier(Parser *P, const Mat *x, int inverse, const char *name)
{
    long n, i;
    double *re, *im;
    Mat *out;
    int two_columns, symmetric = 0;

    if (x->kind == MAT_CHAR || MAT_N(x) < 1) {
        set_err(P->s, "%s needs numbers, not %s", name, x->kind == MAT_CHAR ? "text" : "an empty matrix");
        return NULL;
    }
    two_columns = x->r > 1 && x->c == 2;
    if (!two_columns && x->r != 1 && x->c != 1) {
        set_err(P->s, "%s needs a vector, or two columns [real imaginary] (got %dx%d)", name, x->r, x->c);
        return NULL;
    }
    n = two_columns ? x->r : MAT_N(x);
#ifdef FFT_POW2_ONLY
    if ((n & (n - 1)) != 0) {                                /* the 16-bit machines do radix 2 only (XD102): say so before anything is allocated */
        long next = 1;

        while (next < n) {
            next <<= 1;
        }
        set_err(P->s, "%s needs a length that is a power of 2 (got %ld): pad the list with zeros, for example to %ld", name, n, next);
        return NULL;
    }
#endif
    out = need(P, mat_new((int)n, 2));
    if (out == NULL) {
        return NULL;
    }
    re = (double *)malloc((size_t)n * sizeof(double));
    im = (double *)malloc((size_t)n * sizeof(double));
    if (re == NULL || im == NULL) {
        free(re);
        free(im);
        mat_free(out);
        need(P, NULL);
        return NULL;
    }
    for (i = 0; i < n; ++i) {
        re[i] = two_columns ? MAT_AT(x, i, 0) : x->d[i];
        im[i] = two_columns ? MAT_AT(x, i, 1) : 0.0;
    }
    if (inverse) {
        /* A spectrum with F[n-k] = conj(F[k]) (to 1e-10 of its largest value) is that of a real list: the result is then exactly real, not real plus rounding noise (as
           MATLAB does), so that ifft(fft(x)) prints as x. */
        double big = 0.0, dev = 0.0;

        for (i = 0; i < n; ++i) {
            double m = fabs(re[i]) + fabs(im[i]), d = fabs(re[i] - re[(n - i) % n]) + fabs(im[i] + im[(n - i) % n]);

            big = m > big ? m : big;
            dev = d > dev ? d : dev;
        }
        symmetric = big > 0.0 && dev <= 1e-10 * big;
    }
    if (fft_transform(re, im, n, inverse) != 0) {
        free(re);
        free(im);
        mat_free(out);
        need(P, NULL);
        return NULL;
    }
    if (symmetric) {
        for (i = 0; i < n; ++i) {
            im[i] = 0.0;
        }
    } else if (!inverse && !two_columns) {
        /* the transform of a real list is conjugate-symmetric: make it exactly so (the rounding noise of the last bit is the same in both halves) */
        im[0] = 0.0;
        for (i = 1; i < (n + 1) / 2; ++i) {
            re[n - i] = re[i];
            im[n - i] = -im[i];
        }
        if (n % 2 == 0) {
            im[n / 2] = 0.0;
        }
    }
    for (i = 0; i < n; ++i) {
        MAT_AT(out, i, 0) = re[i];
        MAT_AT(out, i, 1) = im[i];
    }
    free(re);
    free(im);
    return out;
}

static Mat *polar(Parser *P, const Mat *x, int phase, const char *name)
{
    Mat *out;
    int i;

    if (x->kind == MAT_CHAR || x->c != 2 || x->r < 1) {
        set_err(P->s, "%s needs two columns [real imaginary] (got %dx%d)", name, x->r, x->c);
        return NULL;
    }
    out = need(P, mat_new(x->r, 1));
    for (i = 0; out != NULL && i < x->r; ++i) {
        double re = MAT_AT(x, i, 0), im = MAT_AT(x, i, 1), big = fabs(re) > fabs(im) ? fabs(re) : fabs(im), small = fabs(re) > fabs(im) ? fabs(im) : fabs(re);

        out->d[i] = phase ? atan2(im, re) : (big == 0.0 ? 0.0 : big * sqrt(1.0 + (small / big) * (small / big)));     /* no overflow in re*re */
    }
    return out;
}

static Mat *b_fft(Parser *P, Mat **a, int n) { (void)n; return fourier(P, a[0], 0, "fft"); }
static Mat *b_ifft(Parser *P, Mat **a, int n) { (void)n; return fourier(P, a[0], 1, "ifft"); }
static Mat *b_cabs(Parser *P, Mat **a, int n) { (void)n; return polar(P, a[0], 0, "cabs"); }
static Mat *b_cangle(Parser *P, Mat **a, int n) { (void)n; return polar(P, a[0], 1, "cangle"); }
#endif


/* --- plots (M06) --- */

static void plot_reset(MlSession *s, int keep_hold)
{
    int i, hold = s->plot.hold;

    for (i = 0; i < s->plot.nc; ++i) {
        free(s->plot.c[i].x);
        free(s->plot.c[i].y);
    }
    memset(&s->plot, 0, sizeof(s->plot));
    if (keep_hold) {
        s->plot.hold = hold;
    }
}

static void plot_spec(const MlSession *s, PlotSpec *spec)
{
    int i;

    memset(spec, 0, sizeof(*spec));
    for (i = 0; i < s->plot.nc; ++i) {
        spec->c[i].n = s->plot.c[i].n;
        spec->c[i].x = s->plot.c[i].x;
        spec->c[i].y = s->plot.c[i].y;
        spec->c[i].line = s->plot.c[i].line;
        spec->c[i].marker = s->plot.c[i].marker;
#ifdef MS_486
        spec->c[i].colour = s->plot.c[i].colour;
#endif
    }
    spec->nc = s->plot.nc;
    strcpy(spec->title, s->plot.title);
    strcpy(spec->xlabel, s->plot.xlabel);
    strcpy(spec->ylabel, s->plot.ylabel);
    spec->grid = s->plot.grid;
    spec->xlim_set = s->plot.xlim_set;
    spec->ylim_set = s->plot.ylim_set;
    spec->xlim[0] = s->plot.xlim[0];
    spec->xlim[1] = s->plot.xlim[1];
    spec->ylim[0] = s->plot.ylim[0];
    spec->ylim[1] = s->plot.ylim[1];
}

void ml_set_plot_hook(MlSession *s, ml_plot_fn fn, void *ctx)
{
    s->plot_hook = fn;
    s->plot_ctx = ctx;
}

int ml_plot_curves(const MlSession *s)
{
    return s->plot.nc;
}

int ml_show_plot(MlSession *s)
{
    static PlotSpec spec;

    if (s->plot.nc == 0 || s->plot_hook == NULL) {
        return 0;
    }
    plot_spec(s, &spec);
    s->plot.dirty = 0;
    s->plot_hook(s->plot_ctx, &spec);
    return 1;
}

int ml_render_plot(MlSession *s, unsigned char *bits, PlotText *texts, int maxtexts, double *limits)
{
    static PlotSpec spec;

    if (s->plot.nc == 0) {
        return -1;
    }
    plot_spec(s, &spec);
    return plot_render(&spec, bits, texts, maxtexts, limits);
}

/* One curve from n points of x and y (copied). */
#ifdef MS_486
static int plot_colour_now;                     /* the colour letter of the style that is being parsed (the 486's VGA plots); plot_add_curve keeps it */
#endif

static int plot_add_curve(Parser *P, const double *x, const double *y, int n, int line, int marker)
{
    MlSession *s = P->s;
    int i;
    double *cx, *cy;

    if (s->plot.nc >= PLOT_MAXCURVES) {
        set_err(s, "plot: at most %d curves at once", PLOT_MAXCURVES);
        return 0;
    }
    if (s->plot.total + n > PLOT_MAXPOINTS) {
        set_err(s, "plot: at most %d points in all the curves", PLOT_MAXPOINTS);
        return 0;
    }
    cx = (double *)malloc((size_t)(n > 0 ? n : 1) * sizeof(double));
    cy = (double *)malloc((size_t)(n > 0 ? n : 1) * sizeof(double));
    if (cx == NULL || cy == NULL) {
        free(cx);
        free(cy);
        need(P, NULL);
        return 0;
    }
    for (i = 0; i < n; ++i) {
        cx[i] = x != NULL ? x[i] : i + 1;
        cy[i] = y[i];
    }
    s->plot.c[s->plot.nc].x = cx;
    s->plot.c[s->plot.nc].y = cy;
    s->plot.c[s->plot.nc].n = n;
    s->plot.c[s->plot.nc].line = line;
    s->plot.c[s->plot.nc].marker = marker;
#ifdef MS_486
    s->plot.c[s->plot.nc].colour = plot_colour_now;
#endif
    ++s->plot.nc;
    s->plot.total += n;
    return 1;
}

static int text_of(const Mat *m, char *buf, int max)
{
    int i, n = (int)MAT_N(m);

    if (n >= max) {
        n = max - 1;
    }
    for (i = 0; i < n; ++i) {
        buf[i] = (char)(int)m->d[i];
    }
    buf[n] = '\0';
    return 1;
}

/* plot(y), plot(x,y), plot(x,y,'style'), plot(x1,y1,x2,y2,...): columns of a matrix are curves. */
static Mat *b_plot(Parser *P, Mat **a, int n)
{
    MlSession *s = P->s;
    int i = 0, line, marker, j;

    if (!s->plot.hold) {
        plot_reset(s, 1);
    }
    while (i < n) {
        const Mat *x = NULL, *y = a[i];
        char style[24];

        if (y->kind == MAT_CHAR) {
            set_err(s, "plot: a style such as 'r--' must come after the data it is for");
            return NULL;
        }
        ++i;
        if (i < n && a[i]->kind != MAT_CHAR) {
            x = y;
            y = a[i];
            ++i;
        }
        line = LINE_SOLID;
        marker = MARK_NONE;
#ifdef MS_486
        plot_colour_now = 0;
#endif
        if (i < n && a[i]->kind == MAT_CHAR) {
            text_of(a[i], style, (int)sizeof(style));
#ifdef MS_486
            plot_colour_now = plot_style_colour(style);
#endif
            if (!plot_parse_style(style, &line, &marker)) {
                set_err(s, "plot: '%s' is not a line style (use - -- : and the markers . o + * x)", style);
                return NULL;
            }
            ++i;
        }
        if (MAT_N(y) == 0) {
            set_err(s, "plot: nothing to plot (an empty matrix)");
            return NULL;
        }
        if (y->r > 1 && y->c > 1) {                                /* the columns of y against x (or against 1, 2, 3, ...) */
            for (j = 0; j < y->c; ++j) {
                double *col = (double *)malloc((size_t)y->r * sizeof(double)), *xc = NULL;
                int r, ok;

                xc = x != NULL ? (double *)malloc((size_t)y->r * sizeof(double)) : NULL;
                if (col == NULL || (x != NULL && xc == NULL)) {
                    free(col);
                    free(xc);
                    need(P, NULL);
                    return NULL;
                }
                if (x != NULL && ((x->r > 1 && x->c > 1) ? (x->r != y->r || x->c != y->c) : (MAT_N(x) != y->r))) {
                    free(col);
                    free(xc);
                    set_err(s, "plot: x must have as many numbers as y has rows (%d)", y->r);
                    return NULL;
                }
                for (r = 0; r < y->r; ++r) {
                    col[r] = MAT_AT(y, r, j);
                    if (x != NULL) {
                        xc[r] = (x->r > 1 && x->c > 1) ? MAT_AT(x, r, j) : x->d[r];
                    }
                }
                ok = plot_add_curve(P, xc, col, y->r, line, marker);
                free(col);
                free(xc);
                if (!ok) {
                    return NULL;
                }
            }
        } else {
            if (x != NULL && MAT_N(x) != MAT_N(y)) {
                set_err(s, "plot: x and y must have the same number of points (%ld and %ld)", MAT_N(x), MAT_N(y));
                return NULL;
            }
            if (!plot_add_curve(P, x != NULL ? x->d : NULL, y->d, (int)MAT_N(y), line, marker)) {
                return NULL;
            }
        }
    }
    s->plot.dirty = 1;
    return no_value(P);
}

static int plot_need(Parser *P, const char *what)
{
    if (P->s->plot.nc == 0) {
        set_err(P->s, "%s: there is no plot yet (plot something first)", what);
        return 0;
    }
    return 1;
}

static Mat *label_to(Parser *P, Mat **a, char *dest, const char *what)
{
    if (!plot_need(P, what)) {
        return NULL;
    }
    if (a[0]->kind != MAT_CHAR && MAT_N(a[0]) > 0) {
        set_err(P->s, "%s needs a text, in quotes", what);
        return NULL;
    }
    text_of(a[0], dest, 81);
    P->s->plot.dirty = 1;
    return no_value(P);
}

static Mat *b_title(Parser *P, Mat **a, int n) { (void)n; return label_to(P, a, P->s->plot.title, "title"); }
static Mat *b_xlabel(Parser *P, Mat **a, int n) { (void)n; return label_to(P, a, P->s->plot.xlabel, "xlabel"); }
static Mat *b_ylabel(Parser *P, Mat **a, int n) { (void)n; return label_to(P, a, P->s->plot.ylabel, "ylabel"); }

/* on, off or nothing (toggle) from a word or a text argument; -1 if it is something else. */
static int on_off(const char *word, int current)
{
    if (word[0] == '\0') {
        return !current;
    }
    if (strcmp(word, "on") == 0) {
        return 1;
    }
    if (strcmp(word, "off") == 0) {
        return 0;
    }
    return -1;
}

/* hold, grid, axis, clf, shg by name with an optional word; shared by the commands (hold on) and the functions (hold('on')). Returns 1 if all is well. */
static int plot_command(Parser *P, const char *name, const char *word)
{
    MlSession *s = P->s;
    int v;

    if (strcmp(name, "clf") == 0) {
        plot_reset(s, 1);
        return 1;
    }
    if (strcmp(name, "shg") == 0) {
        if (!plot_need(P, "shg")) {
            return 0;
        }
        s->plot.dirty = 1;
        return 1;
    }
    if (strcmp(name, "hold") == 0) {
        v = on_off(word, s->plot.hold);
        if (v < 0) {
            set_err(s, "hold: use hold on or hold off");
            return 0;
        }
        s->plot.hold = v;
        return 1;
    }
    if (strcmp(name, "grid") == 0) {
        v = on_off(word, s->plot.grid);
        if (v < 0) {
            set_err(s, "grid: use grid on or grid off");
            return 0;
        }
        s->plot.grid = v;
        if (s->plot.nc > 0) {
            s->plot.dirty = 1;
        }
        return 1;
    }
    if (strcmp(name, "axis") == 0) {
        if (strcmp(word, "auto") == 0) {
            s->plot.xlim_set = s->plot.ylim_set = 0;
        } else if (strcmp(word, "tight") == 0) {
            s->plot.xlim_set = s->plot.ylim_set = 2;
        } else {
            set_err(s, "axis: use axis([xmin xmax ymin ymax]), axis tight or axis auto");
            return 0;
        }
        if (s->plot.nc > 0) {
            s->plot.dirty = 1;
        }
        return 1;
    }
    return 0;
}

static Mat *plot_function(Parser *P, Mat **a, int n, const char *name)
{
    char word[16];

    word[0] = '\0';
    if (n > 0 && a[0]->kind == MAT_CHAR) {
        text_of(a[0], word, (int)sizeof(word));
    } else if (n > 0 && strcmp(name, "axis") == 0) {                 /* axis([xmin xmax ymin ymax]) */
        if (MAT_N(a[0]) != 4 || !(a[0]->d[0] < a[0]->d[1]) || !(a[0]->d[2] < a[0]->d[3])) {
            set_err(P->s, "axis needs [xmin xmax ymin ymax] with min below max");
            return NULL;
        }
        P->s->plot.xlim[0] = a[0]->d[0];
        P->s->plot.xlim[1] = a[0]->d[1];
        P->s->plot.ylim[0] = a[0]->d[2];
        P->s->plot.ylim[1] = a[0]->d[3];
        P->s->plot.xlim_set = P->s->plot.ylim_set = 1;
        if (P->s->plot.nc > 0) {
            P->s->plot.dirty = 1;
        }
        return no_value(P);
    }
    if (!plot_command(P, name, word)) {
        return NULL;
    }
    return no_value(P);
}

static Mat *b_hold(Parser *P, Mat **a, int n) { return plot_function(P, a, n, "hold"); }
static Mat *b_grid(Parser *P, Mat **a, int n) { return plot_function(P, a, n, "grid"); }
static Mat *b_axis(Parser *P, Mat **a, int n) { return plot_function(P, a, n, "axis"); }
static Mat *b_clf(Parser *P, Mat **a, int n) { return plot_function(P, a, n, "clf"); }
static Mat *b_shg(Parser *P, Mat **a, int n) { return plot_function(P, a, n, "shg"); }

static Mat *limit_to(Parser *P, Mat **a, int which)
{
    const char *name = which ? "ylim" : "xlim";

    if (MAT_N(a[0]) != 2 || !(a[0]->d[0] < a[0]->d[1])) {
        set_err(P->s, "%s needs [min max] with min below max", name);
        return NULL;
    }
    if (which) {
        P->s->plot.ylim[0] = a[0]->d[0];
        P->s->plot.ylim[1] = a[0]->d[1];
        P->s->plot.ylim_set = 1;
    } else {
        P->s->plot.xlim[0] = a[0]->d[0];
        P->s->plot.xlim[1] = a[0]->d[1];
        P->s->plot.xlim_set = 1;
    }
    if (P->s->plot.nc > 0) {
        P->s->plot.dirty = 1;
    }
    return no_value(P);
}

static Mat *b_xlim(Parser *P, Mat **a, int n) { (void)n; return limit_to(P, a, 0); }
static Mat *b_ylim(Parser *P, Mat **a, int n) { (void)n; return limit_to(P, a, 1); }

static const struct {
    const char *name;
    int min, max;
    BuiltinFn fn;
} builtins[] = {
    {"sqrt", 1, 1, b_sqrt}, {"log", 1, 1, b_log}, {"log10", 1, 1, b_log10}, {"asin", 1, 1, b_asin}, {"acos", 1, 1, b_acos},
    {"abs", 1, 1, b_abs}, {"fix", 1, 1, b_fix}, {"round", 1, 1, b_round}, {"sign", 1, 1, b_sign}, {"sin", 1, 1, b_sin},
    {"cos", 1, 1, b_cos}, {"tan", 1, 1, b_tan}, {"atan", 1, 1, b_atan}, {"exp", 1, 1, b_exp}, {"floor", 1, 1, b_floor},
    {"ceil", 1, 1, b_ceil}, {"atan2", 2, 2, b_atan2}, {"mod", 2, 2, b_mod}, {"rem", 2, 2, b_rem}, {"sum", 1, 2, b_sum},
    {"prod", 1, 2, b_prod}, {"mean", 1, 2, b_mean}, {"max", 1, 2, b_max}, {"min", 1, 2, b_min}, {"cumsum", 1, 1, b_cumsum},
    {"zeros", 0, 2, b_zeros}, {"ones", 0, 2, b_ones}, {"eye", 0, 2, b_eye}, {"rand", 0, 2, b_rand},
    {"linspace", 2, 3, b_linspace}, {"size", 1, 2, b_size}, {"length", 1, 1, b_length}, {"numel", 1, 1, b_numel},
    {"transpose", 1, 1, b_transpose}, {"det", 1, 1, b_det}, {"inv", 1, 1, b_inv}, {"trace", 1, 1, b_trace},
    {"rank", 1, 1, b_rank}, {"diag", 1, 1, b_diag}, {"norm", 1, 2, b_norm}, {"dot", 2, 2, b_dot}, {"reshape", 3, 3, b_reshape},
    {"any", 1, 1, b_any}, {"all", 1, 1, b_all}, {"find", 1, 1, b_find}, {"isempty", 1, 1, b_isempty}, {"sort", 1, 1, b_sort},
    {"tic", 0, 0, b_tic}, {"toc", 0, 0, b_toc}, {"disp", 1, 1, b_disp}, {"num2str", 1, 1, b_num2str}, {"strcmp", 2, 2, b_isequal_str},
    {"sprintf", 1, MAXARGS, b_sprintf}, {"fprintf", 1, MAXARGS, b_fprintf}, {"error", 1, MAXARGS, b_error},
    {"plot", 1, MAXARGS, b_plot}, {"title", 1, 1, b_title}, {"xlabel", 1, 1, b_xlabel}, {"ylabel", 1, 1, b_ylabel}, {"hold", 0, 1, b_hold},
    {"grid", 0, 1, b_grid}, {"axis", 1, 1, b_axis}, {"clf", 0, 0, b_clf}, {"shg", 0, 0, b_shg}, {"xlim", 1, 1, b_xlim}, {"ylim", 1, 1, b_ylim},
    {"eig", 1, 1, b_eig}, {"eigc", 1, 1, b_eigc}, {"svd", 1, 1, b_svd}, {"cond", 1, 1, b_cond}, {"pinv", 1, 1, b_pinv},
    {"median", 1, 1, b_median}, {"std", 1, 1, b_std}, {"var", 1, 1, b_var}, {"diff", 1, 1, b_diff}, {"cumprod", 1, 1, b_cumprod},
    {"polyfit", 3, 3, b_polyfit}, {"polyval", 2, 2, b_polyval}, {"trapz", 1, 2, b_trapz}, {"roots", 1, 1, b_roots}, {"rootsc", 1, 1, b_rootsc},
#ifdef MS_FFT
    {"fft", 1, 1, b_fft}, {"ifft", 1, 1, b_ifft}, {"cabs", 1, 1, b_cabs}, {"cangle", 1, 1, b_cangle},
#endif
    {NULL, 0, 0, NULL}
};

static int builtin_index(const char *name)
{
    int i;

    for (i = 0; builtins[i].name != NULL; ++i) {
        if (builtins[i].name[0] == name[0] && strcmp(builtins[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

#ifdef __WATCOMC__
#pragma code_seg("ENG_TOOLS", "CODE")
#endif

/* --- user functions and the numerical tools --- */

static const Mat *lookup_value_s(const MlSession *s, const char *name)
{
    int i;

    for (i = s->nbind - 1; i >= s->bindbase; --i) {
        if (s->bind[i].name[0] == name[0] && strcmp(s->bind[i].name, name) == 0) {
            return s->bind[i].m;
        }
    }
    i = find_var(s, name);
    return i >= 0 ? s->cur->var[i].m : NULL;
}

static const Mat *lookup_value(const Parser *P, const char *name)
{
    return lookup_value_s(P->s, name);
}

static void fn_text(const UserFn *f, char *buf)
{
    int i;

    strcpy(buf, "@(");
    for (i = 0; i < f->np; ++i) {
        if (i > 0) {
            strcat(buf, ",");
        }
        strcat(buf, f->param[i]);
    }
    strcat(buf, ") ");
    strcat(buf, f->body);
}

/* @name for a built-in: a function of its own arguments. */
static void alias_builtin(UserFn *f, const char *name, int bi)
{
    int i, np = builtins[bi].min > 0 ? builtins[bi].min : 1;
    static const char *names[MAXPARAMS] = {"x", "y", "z"};

    if (np > MAXPARAMS) {
        np = MAXPARAMS;
    }
    f->name[0] = '\0';
    f->np = np;
    strcpy(f->body, name);
    strcat(f->body, "(");
    for (i = 0; i < np; ++i) {
        strcpy(f->param[i], names[i]);
        if (i > 0) {
            strcat(f->body, ",");
        }
        strcat(f->body, names[i]);
    }
    strcat(f->body, ")");
}

static int read_name(Parser *P, char *name)
{
    int len = 0;

    while (isalnum((unsigned char)P->p[len]) || P->p[len] == '_') {
        if (len >= NAMELEN - 1) {
            set_err(P->s, "name too long");
            return 0;
        }
        name[len] = P->p[len];
        ++len;
    }
    name[len] = '\0';
    P->p += len;
    return len;
}

/* P->p is at the '@'. Reads @(a,b) body, or @name; the body ends at a ; or , outside brackets, or at a ) that closes an outer call. */
static int define_function(Parser *P, UserFn *f)
{
    char name[NAMELEN];
    const char *q;
    int depth = 0, len, fi, bi;

    ++P->p;
    SKIPWS(P);
    memset(f, 0, sizeof(*f));
    if (isalpha((unsigned char)*P->p) || *P->p == '_') {
        if (!read_name(P, name)) {
            return 0;
        }
        fi = find_func(P->s, name);
        if (fi >= 0) {
            *f = P->s->fn[fi];
            f->name[0] = '\0';
            return 1;
        }
        fi = find_ffn(P->s, name);
        if (fi < 0 && builtin_index(name) < 0 && autoload_function(P->s, name)) {
            fi = find_ffn(P->s, name);
        }
        if (fi >= 0) {
            f->np = P->s->ffn[fi].np;
            f->ffp1 = fi + 1;
            return 1;
        }
        bi = builtin_index(name);
        if (bi >= 0) {
            alias_builtin(f, name, bi);
            return 1;
        }
        set_err(P->s, "'%s' is not a function", name);
        return 0;
    }
    if (*P->p != '(') {
        set_err(P->s, "expected ( after @");
        return 0;
    }
    ++P->p;
    for (;;) {
        SKIPWS(P);
        if (*P->p == ')') {
            ++P->p;
            break;
        }
        if (!(isalpha((unsigned char)*P->p) || *P->p == '_')) {
            set_err(P->s, "expected a parameter name in @(...)");
            return 0;
        }
        if (f->np >= MAXPARAMS) {
            set_err(P->s, "a function takes at most %d parameters", MAXPARAMS);
            return 0;
        }
        if (!read_name(P, f->param[f->np])) {
            return 0;
        }
        ++f->np;
        SKIPWS(P);
        if (*P->p == ',') {
            ++P->p;
        } else if (*P->p != ')') {
            set_err(P->s, "expected , or ) in the parameter list");
            return 0;
        }
    }
    SKIPWS(P);
    for (q = P->p; *q != '\0'; ++q) {
        if (*q == '(' || *q == '[') {
            ++depth;
        } else if (*q == ')' || *q == ']') {
            if (depth == 0) {
                break;
            }
            --depth;
        } else if ((*q == ';' || *q == ',') && depth == 0) {
            break;
        }
    }
    len = (int)(q - P->p);
    while (len > 0 && (P->p[len - 1] == ' ' || P->p[len - 1] == '\t')) {
        --len;
    }
    if (len == 0) {
        set_err(P->s, "the function body is empty");
        return 0;
    }
    if (len >= BODYLEN) {
        set_err(P->s, "the function body is too long (%d characters at most)", BODYLEN - 1);
        return 0;
    }
    memcpy(f->body, P->p, (size_t)len);
    f->body[len] = '\0';
    P->p = q;
    return 1;
}

/* A function wanted by a tool: @(x) expr, @name, or the name of a function (or of a built-in). */
static int parse_fnref(Parser *P, UserFn *f)
{
    char name[NAMELEN];
    int fi, bi;

    SKIPWS(P);
    if (*P->p == '@') {
        return define_function(P, f);
    }
    if (isalpha((unsigned char)*P->p) || *P->p == '_') {
        if (!read_name(P, name)) {
            return 0;
        }
        fi = find_func(P->s, name);
        if (fi >= 0) {
            *f = P->s->fn[fi];
            return 1;
        }
        fi = find_ffn(P->s, name);
        if (fi < 0 && builtin_index(name) < 0 && autoload_function(P->s, name)) {
            fi = find_ffn(P->s, name);
        }
        if (fi >= 0) {
            memset(f, 0, sizeof(*f));
            f->np = P->s->ffn[fi].np;
            f->ffp1 = fi + 1;
            return 1;
        }
        bi = builtin_index(name);
        if (bi >= 0) {
            alias_builtin(f, name, bi);
            return 1;
        }
    }
    set_err(P->s, "the first argument must be a function: @(x) expression, @name, or the name of a function");
    return 0;
}

static Mat *call_fn(Parser *P, const UserFn *f, Mat **argv, int n)
{
    MlSession *s = P->s;
    Parser Q;
    Mat *r;
    int i, base;

    if (f->ffp1 > 0) {                                   /* a function from a file */
        if (++s->evals > MAX_EVALS) {
            set_err(s, "too many function evaluations (%ld)", MAX_EVALS);
            return NULL;
        }
        return call_ffn(P, f->ffp1 - 1, argv, n);
    }

    if (n != f->np) {
        set_err(s, "this function takes %d argument%s (%d given)", f->np, f->np == 1 ? "" : "s", n);
        return NULL;
    }
    if (s->depth >= MAX_DEPTH) {
        set_err(s, "functions are nested too deep (does a function call itself?)");
        return NULL;
    }
    if (s->nbind + n > MAXBIND) {
        set_err(s, "too many nested function parameters");
        return NULL;
    }
    if (++s->evals > MAX_EVALS) {
        set_err(s, "too many function evaluations (%ld): the function may not be integrable or have no root", MAX_EVALS);
        return NULL;
    }
    if (mat_check_abort()) {
        set_err(s, "interrupted");
        return NULL;
    }
    base = s->nbind;
    for (i = 0; i < n; ++i) {
        strcpy(s->bind[base + i].name, f->param[i]);
        s->bind[base + i].m = argv[i];
    }
    s->nbind += n;
    ++s->depth;
    Q.s = s;
    Q.p = f->body;
    Q.brack = 0;
    Q.endtop = 0;
    r = fast_expr(&Q, 1);
    if (r == NULL) {
        r = parse_expr(&Q);
    }
    if (r != NULL) {
        SKIPWS(&Q);
        if (*Q.p != '\0') {
            set_err(s, "unexpected '%c' in the function body", *Q.p);
            mat_free(r);
            r = NULL;
        }
    }
    s->nbind = base;
    --s->depth;
    return r;
}

/* f(x) for one number; the function must give one number back. */
static int eval_scalar(Parser *P, const UserFn *f, double x, double *out)
{
    Mat *arg = need(P, mat_scalar(x)), *r;
    Mat *av[1];

    if (arg == NULL) {
        return 0;
    }
    av[0] = arg;
    r = call_fn(P, f, av, 1);
    mat_free(arg);
    if (r == NULL) {
        return 0;
    }
    if (MAT_N(r) != 1) {
        set_err(P->s, "the function must give one number for one number (it gave %dx%d)", r->r, r->c);
        mat_free(r);
        return 0;
    }
    *out = r->d[0];
    mat_free(r);
    return 1;
}

static int eval_finite(Parser *P, const UserFn *f, double x, double *out)
{
    if (!eval_scalar(P, f, x, out)) {
        return 0;
    }
    if (is_nan(*out) || is_inf(*out)) {
        set_err(P->s, "the function is not finite at x = %g", x);
        return 0;
    }
    return 1;
}

static int scalar_value(Parser *P, const Mat *m, const char *what, double *out)
{
    if (MAT_N(m) != 1) {
        set_err(P->s, "%s must be a single number", what);
        return 0;
    }
    *out = m->d[0];
    return 1;
}

/* integral: adaptive Gauss-Kronrod 7/15. The nodes and weights are the standard ones (checked: the 15-point rule is exact for polynomials
   of degree 22 and the 7-point rule for degree 13). */
static const double xgk[8] = {0.991455371120812639206854697526329, 0.949107912342758524526189684047851, 0.864864423359769072789712788640926,
                              0.741531185599394439863864773280788, 0.586087235467691130294144838258730, 0.405845151377397166906606412076961,
                              0.207784955007898467600689403773245, 0.0};
static const double wgk[8] = {0.022935322010529224963732008058970, 0.063092092629978553290700663189204, 0.104790010322250183839876322541518,
                              0.140653259715525918745189590510238, 0.169004726639267902826583426598550, 0.190350578064785409913256402421014,
                              0.204432940075298892414161999234649, 0.209482141084727828012999174891714};
static const double wg[4] = {0.129484966168869693270611432679082, 0.279705391489276667901467771423780, 0.381830050505118944950369775488975,
                             0.417959183673469387755102040816327};

static int gk15(Parser *P, const UserFn *f, double a, double b, double *value, double *err)
{
    double c = 0.5 * (a + b), h = 0.5 * (b - a), fc, f1, f2, resk, resg;
    int j;

    if (!eval_finite(P, f, c, &fc)) {
        return 0;
    }
    resg = fc * wg[3];
    resk = fc * wgk[7];
    for (j = 0; j < 7; ++j) {
        double dx = h * xgk[j];

        if (!eval_finite(P, f, c - dx, &f1) || !eval_finite(P, f, c + dx, &f2)) {
            return 0;
        }
        resk += wgk[j] * (f1 + f2);
        if (j & 1) {
            resg += wg[j / 2] * (f1 + f2);
        }
    }
    *value = resk * h;
    *err = fabs((resk - resg) * h);
    return 1;
}

static int adapt(Parser *P, const UserFn *f, double a, double b, double tol, int depth, double *sum)
{
    double k, err, m;

    if (!gk15(P, f, a, b, &k, &err)) {
        return 0;
    }
    if (err <= tol || depth >= 14) {
        if (err > tol) {
            P->s->warn_integral = 1;                  /* gave up on this piece: a singularity, or too much wiggle */
        }
        *sum += k;
        return 1;
    }
    m = 0.5 * (a + b);
    return adapt(P, f, a, m, 0.5 * tol, depth + 1, sum) && adapt(P, f, m, b, 0.5 * tol, depth + 1, sum);
}

static Mat *t_integral(Parser *P, const UserFn *f, Mat **a, int n)
{
    double lo, hi, k0, e0, tol, sum = 0.0;

    (void)n;
    if (!scalar_value(P, a[0], "the lower limit", &lo) || !scalar_value(P, a[1], "the upper limit", &hi)) {
        return NULL;
    }
    if (is_nan(lo) || is_nan(hi) || is_inf(lo) || is_inf(hi)) {
        set_err(P->s, "integral needs finite limits");
        return NULL;
    }
    if (lo == hi) {
        return need(P, mat_scalar(0.0));
    }
    if (!gk15(P, f, lo, hi, &k0, &e0)) {
        return NULL;
    }
    tol = 1e-10 * fabs(k0);
    if (tol < 1e-13) {
        tol = 1e-13;
    }
    if (e0 <= tol) {
        return need(P, mat_scalar(k0));
    }
    P->s->warn_integral = 0;
    if (!adapt(P, f, lo, hi, tol, 0, &sum)) {
        return NULL;
    }
    if (P->s->warn_integral) {
        emit(P->s, "warning: integral did not converge on every piece (a singularity or a very wiggly function?): the result is approximate\n");
    }
    return need(P, mat_scalar(sum));
}

/* fzero: Brent's method (after Numerical Recipes' zbrent). */
static Mat *t_fzero(Parser *P, const UserFn *f, Mat **a, int n)
{
    double lo, hi, flo, fhi, c, fc, d = 0.0, e = 0.0, p, q, r, s, tol1, xm, x0, dx;
    int iter, i;

    (void)n;
    if (MAT_N(a[0]) == 2) {
        lo = a[0]->d[0];
        hi = a[0]->d[1];
        if (!eval_finite(P, f, lo, &flo) || !eval_finite(P, f, hi, &fhi)) {
            return NULL;
        }
    } else if (MAT_N(a[0]) == 1) {
        x0 = a[0]->d[0];
        if (!eval_finite(P, f, x0, &flo)) {
            return NULL;
        }
        if (flo == 0.0) {
            return need(P, mat_scalar(x0));
        }
        dx = x0 == 0.0 ? 0.1 : 0.1 * fabs(x0);
        lo = x0;
        hi = x0;
        fhi = flo;
        for (i = 0; i < 60; ++i) {
            double xs[2], fs[2];
            int j, found = 0;

            xs[0] = x0 + dx;
            xs[1] = x0 - dx;
            for (j = 0; j < 2 && !found; ++j) {
                if (!eval_finite(P, f, xs[j], &fs[j])) {
                    return NULL;
                }
                if ((fs[j] < 0.0) != (flo < 0.0) || fs[j] == 0.0) {
                    hi = xs[j];
                    fhi = fs[j];
                    found = 1;
                }
            }
            if (found) {
                break;
            }
            dx *= 1.6;
        }
        if (lo == hi) {
            set_err(P->s, "fzero: no sign change found near %g (try fzero(f,[a b]) with a bracket)", x0);
            return NULL;
        }
    } else {
        set_err(P->s, "fzero needs a starting number or a bracket [a b]");
        return NULL;
    }
    if (flo == 0.0) {
        return need(P, mat_scalar(lo));
    }
    if (fhi == 0.0) {
        return need(P, mat_scalar(hi));
    }
    if ((flo < 0.0) == (fhi < 0.0)) {
        set_err(P->s, "fzero: the function has the same sign at both ends of [%g %g]", lo, hi);
        return NULL;
    }
    c = hi;
    fc = fhi;
    {
        double aa = lo, fa = flo, bb = hi, fb = fhi;

        for (iter = 0; iter < 200; ++iter) {
            if ((fb > 0.0 && fc > 0.0) || (fb < 0.0 && fc < 0.0)) {
                c = aa;
                fc = fa;
                d = bb - aa;
                e = d;
            }
            if (fabs(fc) < fabs(fb)) {
                aa = bb;
                bb = c;
                c = aa;
                fa = fb;
                fb = fc;
                fc = fa;
            }
            tol1 = 2.0 * 2.220446049250313e-16 * fabs(bb) + 0.5e-14;
            xm = 0.5 * (c - bb);
            if (fabs(xm) <= tol1 || fb == 0.0) {
                return need(P, mat_scalar(bb));
            }
            if (fabs(e) >= tol1 && fabs(fa) > fabs(fb)) {
                s = fb / fa;
                if (aa == c) {
                    p = 2.0 * xm * s;
                    q = 1.0 - s;
                } else {
                    q = fa / fc;
                    r = fb / fc;
                    p = s * (2.0 * xm * q * (q - r) - (bb - aa) * (r - 1.0));
                    q = (q - 1.0) * (r - 1.0) * (s - 1.0);
                }
                if (p > 0.0) {
                    q = -q;
                }
                p = fabs(p);
                {
                    double min1 = 3.0 * xm * q - fabs(tol1 * q), min2 = fabs(e * q);

                    if (2.0 * p < (min1 < min2 ? min1 : min2)) {
                        e = d;
                        d = p / q;
                    } else {
                        d = xm;
                        e = d;
                    }
                }
            } else {
                d = xm;
                e = d;
            }
            aa = bb;
            fa = fb;
            bb += fabs(d) > tol1 ? d : (xm >= 0.0 ? tol1 : -tol1);
            if (!eval_finite(P, f, bb, &fb)) {
                return NULL;
            }
        }
    }
    set_err(P->s, "fzero did not converge");
    return NULL;
}

/* fminbnd: Brent's minimiser (golden section with parabolic steps). */
static Mat *t_fminbnd(Parser *P, const UserFn *f, Mat **a, int n)
{
    double lo, hi, x, v, w, fx, fv, fw, fu, u, d = 0.0, e = 0.0, xm, tol1, tol2, p, q, r;
    const double cgold = 0.3819660112501051, tol = 1e-10;
    int iter;

    (void)n;
    if (!scalar_value(P, a[0], "the lower bound", &lo) || !scalar_value(P, a[1], "the upper bound", &hi)) {
        return NULL;
    }
    if (!(lo < hi)) {
        set_err(P->s, "fminbnd needs a < b");
        return NULL;
    }
    x = v = w = lo + cgold * (hi - lo);
    if (!eval_finite(P, f, x, &fx)) {
        return NULL;
    }
    fv = fw = fx;
    for (iter = 0; iter < 200; ++iter) {
        int golden = 1;

        xm = 0.5 * (lo + hi);
        tol1 = 1.4901161193847656e-08 * fabs(x) + tol / 3.0;
        tol2 = 2.0 * tol1;
        if (fabs(x - xm) <= tol2 - 0.5 * (hi - lo)) {
            return need(P, mat_scalar(x));
        }
        if (fabs(e) > tol1) {
            r = (x - w) * (fx - fv);
            q = (x - v) * (fx - fw);
            p = (x - v) * q - (x - w) * r;
            q = 2.0 * (q - r);
            if (q > 0.0) {
                p = -p;
            }
            q = fabs(q);
            r = e;
            e = d;
            if (!(fabs(p) >= fabs(0.5 * q * r) || p <= q * (lo - x) || p >= q * (hi - x))) {
                d = p / q;
                u = x + d;
                if (u - lo < tol2 || hi - u < tol2) {
                    d = xm >= x ? tol1 : -tol1;
                }
                golden = 0;
            }
        }
        if (golden) {
            e = x >= xm ? lo - x : hi - x;
            d = cgold * e;
        }
        u = fabs(d) >= tol1 ? x + d : x + (d > 0.0 ? tol1 : -tol1);
        if (!eval_finite(P, f, u, &fu)) {
            return NULL;
        }
        if (fu <= fx) {
            if (u >= x) {
                lo = x;
            } else {
                hi = x;
            }
            v = w;
            fv = fw;
            w = x;
            fw = fx;
            x = u;
            fx = fu;
        } else {
            if (u < x) {
                lo = u;
            } else {
                hi = u;
            }
            if (fu <= fw || w == x) {
                v = w;
                fv = fw;
                w = u;
                fw = fu;
            } else if (fu <= fv || v == x || v == w) {
                v = u;
                fv = fu;
            }
        }
    }
    return need(P, mat_scalar(x));
}

/* deriv(f, x [, order]): central differences with Richardson extrapolation (three step sizes, error of order h^6). */
static int derivative_at(Parser *P, const UserFn *f, double x, int order, double *out)
{
    double h = (order == 1 ? 0.02 : 0.05) * (fabs(x) > 1.0 ? fabs(x) : 1.0), r[3][3], f0 = 0.0, fp, fm;
    int i, j;

    if (order == 2 && !eval_finite(P, f, x, &f0)) {
        return 0;
    }
    for (i = 0; i < 3; ++i) {
        if (!eval_finite(P, f, x + h, &fp) || !eval_finite(P, f, x - h, &fm)) {
            return 0;
        }
        r[i][0] = order == 1 ? (fp - fm) / (2.0 * h) : (fp - 2.0 * f0 + fm) / (h * h);
        h *= 0.5;
    }
    for (j = 1; j < 3; ++j) {
        double factor = j == 1 ? 4.0 : 16.0;

        for (i = j; i < 3; ++i) {
            r[i][j] = (factor * r[i][j - 1] - r[i - 1][j - 1]) / (factor - 1.0);
        }
    }
    *out = r[2][2];
    return 1;
}

static Mat *t_deriv(Parser *P, const UserFn *f, Mat **a, int n)
{
    double order = 1.0, v;
    Mat *m;
    long i;

    if (n > 1 && !scalar_value(P, a[1], "the order", &order)) {
        return NULL;
    }
    if (order != 1.0 && order != 2.0) {
        set_err(P->s, "deriv: the order must be 1 or 2");
        return NULL;
    }
    m = need(P, mat_new(a[0]->r, a[0]->c));
    for (i = 0; m != NULL && i < MAT_N(a[0]); ++i) {
        if (!derivative_at(P, f, a[0]->d[i], (int)order, &v)) {
            mat_free(m);
            return NULL;
        }
        m->d[i] = v;
    }
    return m;
}

static Mat *t_arrayfun(Parser *P, const UserFn *f, Mat **a, int n)
{
    Mat *m = need(P, mat_new(a[0]->r, a[0]->c));
    long i;
    double v;

    (void)n;
    for (i = 0; m != NULL && i < MAT_N(a[0]); ++i) {
        if (!eval_scalar(P, f, a[0]->d[i], &v)) {
            mat_free(m);
            return NULL;
        }
        m->d[i] = v;
    }
    return m;
}

typedef Mat *(*ToolFn)(Parser *P, const UserFn *f, Mat **a, int n);

static const struct {
    const char *name;
    int min, max;                               /* arguments after the function */
    ToolFn fn;
} tools[] = {
    {"integral", 2, 2, t_integral}, {"quad", 2, 2, t_integral}, {"fzero", 1, 1, t_fzero}, {"fminbnd", 2, 2, t_fminbnd},
    {"deriv", 1, 2, t_deriv}, {"arrayfun", 1, 1, t_arrayfun}, {NULL, 0, 0, NULL}
};

static int tool_index(const char *name)
{
    int i;

    for (i = 0; tools[i].name != NULL; ++i) {
        if (tools[i].name[0] == name[0] && strcmp(tools[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

/* --- the fast path: expressions of plain numbers --- */

/* Most of what a program does is arithmetic on single numbers: x = x + k, if mod(k, 3) == 0, v(i) = a * b. This evaluates such an
   expression directly on doubles, without a matrix for every step. It gives up (bad) at anything else, a matrix, a string, a range, a
   transpose, a function of your own, and then the general parser does the whole expression again from its start; so it can never
   change a result, only how fast it is found. */

/* Switch for the tests: 0 sends everything through the general parser. */
int ml_fastpath = 1;

typedef struct {
    MlSession *s;
    const char *p;
    int bad;
    int last_bool;                              /* the last operation made a true/false */
    int endr, endc, endk, endn, have_end;       /* for "end" inside an index */
} Fx;

static double fx_oror(Fx *x);
static double fx_bin(Fx *x, int minprec);
static const char *skip_expr(const char *p, int stop_andand);

#define FXWS(x) while (*(x)->p == ' ' || *(x)->p == '\t') { ++(x)->p; }
#define fx_ws(x) FXWS(x)

static const struct {
    const char *name;
    double (*f)(double);
} fast1[] = {
    {"sqrt", f_sqrt}, {"abs", f_abs}, {"sin", f_sin}, {"cos", f_cos}, {"tan", f_tan}, {"atan", f_atan}, {"exp", f_exp}, {"log", f_log},
    {"log10", f_log10}, {"floor", f_floor}, {"ceil", f_ceil}, {"round", f_round}, {"fix", f_fix}, {"sign", f_sign}, {"asin", f_asin},
    {"acos", f_acos}, {NULL, NULL}
};

static double fx_number(Fx *x)
{
    const char *p = x->p;
    char buf[48];
    int n;

    while (isdigit((unsigned char)*p)) {
        ++p;
    }
    if (*p == '.' && !(p[1] == '*' || p[1] == '/' || p[1] == '^' || p[1] == '\\' || p[1] == '\'')) {
        ++p;
        while (isdigit((unsigned char)*p)) {
            ++p;
        }
    }
    if (*p == 'e' || *p == 'E') {
        const char *q = p + 1;

        if (*q == '+' || *q == '-') {
            ++q;
        }
        if (isdigit((unsigned char)*q)) {
            while (isdigit((unsigned char)*q)) {
                ++q;
            }
            p = q;
        }
    }
    n = (int)(p - x->p);
    if (n > 0 && n <= 9) {
        long v = 0;
        int k;

        for (k = 0; k < n && isdigit((unsigned char)x->p[k]); ++k) {
            v = v * 10 + (x->p[k] - '0');
        }
        if (k == n) {
            x->p = p;
            return (double)v;
        }
    }
    if (n <= 0 || n >= (int)sizeof(buf)) {
        x->bad = 1;
        return 0.0;
    }
    memcpy(buf, x->p, (size_t)n);
    buf[n] = '\0';
    x->p = p;
    return atof(buf);
}

/* The element of m at one or two subscripts, read from the text at x->p (after the "("). */
static double fx_index(Fx *x, const Mat *m)
{
    double a, b = 0.0;
    int saved_have = x->have_end, sr = x->endr, sc = x->endc, sk = x->endk, sn = x->endn, nargs = 1;
    long i, j;

    x->have_end = 1;
    x->endr = m->r;
    x->endc = m->c;
    x->endn = 1;
    x->endk = 0;
    {
        const char *q = x->p;
        int depth = 0;

        for (; *q != '\0'; ++q) {                  /* two subscripts or one? */
            if (*q == '(' || *q == '[') {
                ++depth;
            } else if (*q == ')' || *q == ']') {
                if (depth == 0) {
                    break;
                }
                --depth;
            } else if (*q == ',' && depth == 0) {
                nargs = 2;
            }
        }
    }
    x->endn = nargs;
    a = fx_oror(x);
    if (x->bad) {
        return 0.0;
    }
    fx_ws(x);
    if (nargs == 2) {
        if (*x->p != ',') {
            x->bad = 1;
            return 0.0;
        }
        ++x->p;
        x->endk = 1;
        b = fx_oror(x);
        fx_ws(x);
    }
    if (x->bad || *x->p != ')') {
        x->bad = 1;
        return 0.0;
    }
    ++x->p;
    x->have_end = saved_have;
    x->endr = sr;
    x->endc = sc;
    x->endk = sk;
    x->endn = sn;
    if (m->kind == MAT_CHAR || a != floor(a) || a < 1.0 || (nargs == 2 && (b != floor(b) || b < 1.0))) {
        x->bad = 1;
        return 0.0;
    }
    if (nargs == 1) {
        if (a > (double)MAT_N(m)) {
            x->bad = 1;
            return 0.0;
        }
        i = (long)a - 1;
        return m->d[(i % m->r) * m->c + i / m->r];       /* down the columns */
    }
    if (a > (double)m->r || b > (double)m->c) {
        x->bad = 1;
        return 0.0;
    }
    i = (long)a - 1;
    j = (long)b - 1;
    return m->d[i * m->c + j];
}

static double fx_atom(Fx *x)
{
    char name[NAMELEN];
    int len = 0;
    double v;
    const Mat *vm;

    fx_ws(x);
    if (isdigit((unsigned char)*x->p) || (*x->p == '.' && isdigit((unsigned char)x->p[1]))) {
        return fx_number(x);
    }
    if (*x->p == '(') {
        ++x->p;
        v = fx_oror(x);
        fx_ws(x);
        if (x->bad || *x->p != ')') {
            x->bad = 1;
            return 0.0;
        }
        ++x->p;
        return v;
    }
    if (!(isalpha((unsigned char)*x->p) || *x->p == '_')) {
        x->bad = 1;
        return 0.0;
    }
    while (isalnum((unsigned char)x->p[len]) || x->p[len] == '_') {
        if (len >= NAMELEN - 1) {
            x->bad = 1;
            return 0.0;
        }
        name[len] = x->p[len];
        ++len;
    }
    name[len] = '\0';
    x->p += len;
    if (name[0] == 'e' && strcmp(name, "end") == 0 && x->have_end) {
        if (x->endn == 1) {
            return (double)x->endr * x->endc;
        }
        return x->endk == 0 ? x->endr : x->endc;
    }
    vm = lookup_value_s(x->s, name);
    {
        const char *q = x->p;

        while (*q == ' ' || *q == '\t') {
            ++q;
        }
        if (*q == '(' && vm != NULL) {                      /* v(i) or A(i,j) with plain numbers for subscripts */
            if (q != x->p) {
                x->bad = 1;                                 /* "v (i)": leave the odd forms to the general parser */
                return 0.0;
            }
            ++x->p;
            return fx_index(x, vm);
        }
        if (vm != NULL) {
            if (MAT_N(vm) != 1 || vm->kind == MAT_CHAR) {
                x->bad = 1;
                return 0.0;
            }
            return vm->d[0];
        }
        if (*q == '(' && q == x->p) {                       /* a built-in function of plain numbers */
            int fi, a1 = 0;
            double arg1, arg2 = 0.0;

            if (x->s->nfn > 0 && find_func(x->s, name) >= 0) {
                x->bad = 1;
                return 0.0;
            }
            if (x->s->nffn > 0 && find_ffn(x->s, name) >= 0) {
                x->bad = 1;
                return 0.0;
            }
            for (fi = 0; fast1[fi].name != NULL; ++fi) {
                if (fast1[fi].name[0] == name[0] && strcmp(fast1[fi].name, name) == 0) {
                    break;
                }
            }
            a1 = fast1[fi].name != NULL ? 1 : (strcmp(name, "mod") == 0 || strcmp(name, "rem") == 0 || strcmp(name, "max") == 0 ||
                                                strcmp(name, "min") == 0 || strcmp(name, "atan2") == 0) ? 2 : 0;
            if (a1 == 0) {
                x->bad = 1;
                return 0.0;
            }
            ++x->p;
            arg1 = fx_oror(x);
            fx_ws(x);
            if (a1 == 2) {
                if (x->bad || *x->p != ',') {
                    x->bad = 1;
                    return 0.0;
                }
                ++x->p;
                arg2 = fx_oror(x);
                fx_ws(x);
            }
            if (x->bad || *x->p != ')') {
                x->bad = 1;
                return 0.0;
            }
            ++x->p;
            x->last_bool = 0;
            if (a1 == 1) {
                return fast1[fi].f(arg1);
            }
            if (strcmp(name, "atan2") == 0) {
                return atan2(arg1, arg2);
            }
            return apply(name[1] == 'o' ? 'm' : (name[1] == 'e' ? 'r' : (name[1] == 'a' ? 'M' : 'N')), arg1, arg2);
        }
    }
    if (strcmp(name, "pi") == 0) {
        return PI;
    }
    if (strcmp(name, "eps") == 0) {
        return 2.220446049250313e-16;
    }
    x->bad = 1;
    return 0.0;
}

static double fx_power(Fx *x)
{
    double base = fx_atom(x);

    while (!x->bad) {
        double ex;
        int neg = 0;
        const char *save = x->p;

        fx_ws(x);
        if (x->p[0] == '\'') {
            x->bad = 1;                                      /* a transpose: not for this path */
            return 0.0;
        }
        if (x->p[0] == '^') {
            ++x->p;
        } else if (x->p[0] == '.' && x->p[1] == '^') {
            x->p += 2;
        } else {
            x->p = save;
            break;
        }
        fx_ws(x);
        if (*x->p == '-') {
            neg = 1;
            ++x->p;
        } else if (*x->p == '+') {
            ++x->p;
        }
        ex = fx_atom(x);
        if (x->bad) {
            return 0.0;
        }
        base = apply('^', base, neg ? -ex : ex);
        x->last_bool = 0;
    }
    return base;
}

static double fx_unary(Fx *x)
{
    double v;

    fx_ws(x);
    if (*x->p == '-') {
        ++x->p;
        v = fx_unary(x);
        return -v;
    }
    if (*x->p == '+') {
        ++x->p;
        return fx_unary(x);
    }
    if (*x->p == '~' && x->p[1] != '=') {
        ++x->p;
        v = fx_unary(x);
        x->last_bool = 1;
        return v == 0.0 ? 1.0 : 0.0;
    }
    return fx_power(x);
}

/* The binary operators by precedence (a larger number binds tighter): || 1, && 2, | 3, & 4, comparisons 5, + - 6, * / .* ./ 7. */
static int fx_peek(Fx *x, int *len, int *prec)
{
    const char *p = x->p;

    *len = 1;
    switch (p[0]) {
    case '+': *prec = 6; return '+';
    case '-': *prec = 6; return '-';
    case '*': *prec = 7; return '*';
    case '/': *prec = 7; return '/';
    case '.':
        if (p[1] == '*' || p[1] == '/') {
            *len = 2;
            *prec = 7;
            return p[1];
        }
        return 0;
    case '<':
        if (p[1] == '=') { *len = 2; *prec = 5; return 'l'; }
        *prec = 5;
        return '<';
    case '>':
        if (p[1] == '=') { *len = 2; *prec = 5; return 'g'; }
        *prec = 5;
        return '>';
    case '=':
        if (p[1] == '=') { *len = 2; *prec = 5; return '='; }
        return 0;
    case '~':
        if (p[1] == '=') { *len = 2; *prec = 5; return '!'; }
        return 0;
    case '&':
        if (p[1] == '&') { *len = 2; *prec = 2; return 'A'; }
        *prec = 4;
        return '&';
    case '|':
        if (p[1] == '|') { *len = 2; *prec = 1; return 'O'; }
        *prec = 3;
        return '|';
    case ':':
    case '\\':
        x->bad = 1;                                      /* a range, or a solve: not for this path */
        return 0;
    }
    return 0;
}

static double fx_bin(Fx *x, int minprec)
{
    double left = fx_unary(x), right;

    for (;;) {
        int len, prec, op;

        if (x->bad) {
            return 0.0;
        }
        FXWS(x);
        op = fx_peek(x, &len, &prec);
        if (op == 0 || prec < minprec) {
            return left;
        }
        x->p += len;
        if (op == 'O') {
            if (left != 0.0) {
                x->p = skip_expr(x->p, 0);                /* the right side is not evaluated */
                left = 1.0;
            } else {
                right = fx_bin(x, 2);
                left = right != 0.0 ? 1.0 : 0.0;
            }
            x->last_bool = 1;
            continue;
        }
        if (op == 'A') {
            if (left == 0.0) {
                x->p = skip_expr(x->p, 1);
                left = 0.0;
            } else {
                right = fx_bin(x, 3);
                left = right != 0.0 ? 1.0 : 0.0;
            }
            x->last_bool = 1;
            continue;
        }
        right = fx_bin(x, prec + 1);
        if (x->bad) {
            return 0.0;
        }
        if (op == '&') {
            left = (left != 0.0 && right != 0.0) ? 1.0 : 0.0;
        } else if (op == '|') {
            left = (left != 0.0 || right != 0.0) ? 1.0 : 0.0;
        } else {
            left = apply(op, left, right);
        }
        x->last_bool = prec >= 3 && prec <= 5;
    }
}

static double fx_oror(Fx *x)
{
    return fx_bin(x, 1);
}

/* mask: 1 the expression must run to the end of the text; 2 it may stop at a , or ) (an argument). Returns the number as a matrix, or
   NULL when the general parser has to do it (nothing has been changed then). */
static Mat *fast_expr(Parser *P, int mask)
{
    Fx x;
    double v;
    Mat *m;

    if (!ml_fastpath) {
        return NULL;
    }
    x.s = P->s;
    x.p = P->p;
    x.bad = 0;
    x.last_bool = 0;
    x.have_end = 0;
    x.endr = x.endc = x.endk = x.endn = 0;
    if (P->endtop > 0) {                                     /* inside an index of the general parser: "end" means its matrix */
        x.have_end = 1;
        x.endr = P->endctx[P->endtop - 1].r;
        x.endc = P->endctx[P->endtop - 1].c;
        x.endk = P->endctx[P->endtop - 1].k;
        x.endn = P->endctx[P->endtop - 1].n;
    }
    v = fx_oror(&x);
    if (x.bad) {
        return NULL;
    }
    fx_ws(&x);
    if (!(*x.p == '\0' || ((mask & 2) && (*x.p == ',' || *x.p == ')')))) {
        return NULL;
    }
    m = mat_scalar(v);
    if (m == NULL) {
        return NULL;
    }
    if (x.last_bool) {
        m->kind = MAT_BOOL;
    }
    P->p = x.p;
    return m;
}


/* v(i) = number and A(i,j) = number, in place, when the subscripts are plain numbers inside the matrix (anything else, growing or
   an error included, is left to the general code). Returns 1 if it did the store. */
static int fast_store(Parser *P, Mat *tm, const char *paren, const char *eq)
{
    Fx x;
    double a, b = 0.0;
    int nargs = 1, depth = 0;
    const char *q;
    Parser R;
    Mat *val;
    long i, j;

    if (!ml_fastpath) {
        return 0;
    }
    x.s = P->s;
    x.p = paren + 1;
    x.bad = 0;
    x.last_bool = 0;
    x.have_end = 1;
    x.endr = tm->r;
    x.endc = tm->c;
    x.endk = 0;
    for (q = x.p; *q != '\0'; ++q) {
        if (*q == '(' || *q == '[') {
            ++depth;
        } else if (*q == ')' || *q == ']') {
            if (depth == 0) {
                break;
            }
            --depth;
        } else if (*q == ',' && depth == 0) {
            nargs = 2;
        }
    }
    x.endn = nargs;
    a = fx_oror(&x);
    fx_ws(&x);
    if (nargs == 2 && !x.bad) {
        if (*x.p != ',') {
            return 0;
        }
        ++x.p;
        x.endk = 1;
        b = fx_oror(&x);
        fx_ws(&x);
    }
    if (x.bad || *x.p != ')') {
        return 0;
    }
    ++x.p;
    fx_ws(&x);
    if (x.p != eq) {
        return 0;
    }
    if (a != floor(a) || a < 1.0 || (nargs == 2 && (b != floor(b) || b < 1.0))) {
        return 0;
    }
    if (nargs == 1) {
        if (a > (double)MAT_N(tm)) {
            return 0;
        }
        i = (long)a - 1;
        j = (i % tm->r) * tm->c + i / tm->r;                /* down the columns */
    } else {
        if (a > (double)tm->r || b > (double)tm->c) {
            return 0;
        }
        j = ((long)a - 1) * tm->c + ((long)b - 1);
    }
    R.s = P->s;
    R.p = eq + 1;
    R.brack = 0;
    R.endtop = 0;
    val = fast_expr(&R, 1);
    if (val == NULL) {
        return 0;
    }
    tm->d[j] = val->d[0];
    if (tm->kind != MAT_NUM && val->kind != tm->kind) {
        tm->kind = MAT_NUM;
    }
    mat_free(val);
    P->p = R.p;
    return 1;
}

/* How many results [a,b,c] = name(...) can take. */
static int builtin_most_results(const char *name)
{
    if (strcmp(name, "svd") == 0) {
        return 3;
    }
    if (strcmp(name, "eig") == 0 || strcmp(name, "max") == 0 || strcmp(name, "min") == 0 || strcmp(name, "sort") == 0 || strcmp(name, "size") == 0) {
        return 2;
    }
    return 1;
}

/* --- the expression parser --- */

static Mat *parse_number(Parser *P)
{
    const char *p = P->p;
    char buf[48];
    int n;

    while (isdigit((unsigned char)*p)) {
        ++p;
    }
    if (*p == '.' && !(p[1] == '*' || p[1] == '/' || p[1] == '^' || p[1] == '\\' || p[1] == '\'')) {
        ++p;
        while (isdigit((unsigned char)*p)) {
            ++p;
        }
    }
    if (*p == 'e' || *p == 'E') {
        const char *q = p + 1;

        if (*q == '+' || *q == '-') {
            ++q;
        }
        if (isdigit((unsigned char)*q)) {
            while (isdigit((unsigned char)*q)) {
                ++q;
            }
            p = q;
        }
    }
    n = (int)(p - P->p);
    if (n > 0 && n <= 9) {                                   /* only digits and short: no need for atof */
        long v = 0;
        int k;

        for (k = 0; k < n && isdigit((unsigned char)P->p[k]); ++k) {
            v = v * 10 + (P->p[k] - '0');
        }
        if (k == n) {
            P->p = p;
            return need(P, mat_scalar((double)v));
        }
    }
    if (n <= 0 || n >= (int)sizeof(buf)) {
        set_err(P->s, "bad number");
        return NULL;
    }
    memcpy(buf, P->p, (size_t)n);
    buf[n] = '\0';
    P->p = p;
    return need(P, mat_scalar(atof(buf)));
}

static Mat *parse_matrix(Parser *P)
{
    Mat *all = need(P, mat_new(0, 0)), *row = need(P, mat_new(0, 0)), *e;
    int saved = P->brack;

    if (all == NULL || row == NULL) {
        mat_free(all);
        mat_free(row);
        return NULL;
    }
    P->brack = 1;
    for (;;) {
        SKIPWS(P);
        if (*P->p == ']') {
            ++P->p;
            break;
        }
        if (*P->p == '\0') {
            set_err(P->s, "missing ]");
            goto fail;
        }
        if (*P->p == ';') {
            ++P->p;
            all = vcat(P, all, row);
            row = all != NULL ? need(P, mat_new(0, 0)) : NULL;
            if (all == NULL || row == NULL) {
                goto fail;
            }
            continue;
        }
        if (*P->p == ',') {
            ++P->p;
            continue;
        }
        e = parse_expr(P);
        if (e == NULL) {
            goto fail;
        }
        row = hcat(P, row, e);
        if (row == NULL) {
            mat_free(all);
            P->brack = saved;
            return NULL;
        }
    }
    all = vcat(P, all, row);
    P->brack = saved;
    return all;
fail:
    mat_free(all);
    mat_free(row);
    P->brack = saved;
    return NULL;
}

static Mat *call_function(Parser *P, const char *name, int bi, Arg *args, int n)
{
    Mat *av[MAXARGS], *result;
    int i;

    for (i = 0; i < n; ++i) {
        if (args[i].all) {
            set_err(P->s, "a lone : is only valid as an index");
            return NULL;
        }
        av[i] = args[i].v;
    }
    if (n < builtins[bi].min || n > builtins[bi].max) {
        set_err(P->s, "wrong number of arguments for %s (%d given)", name, n);
        return NULL;
    }
    result = builtins[bi].fn(P, av, n);
    return result;
}

static Mat *parse_primary(Parser *P)
{
    char name[NAMELEN + 1];
    int len, bi, fi, ti, want;
    const Mat *vm;
    Mat *m;

    SKIPWS(P);
    if (isdigit((unsigned char)*P->p) || (*P->p == '.' && isdigit((unsigned char)P->p[1]))) {
        return parse_number(P);
    }
    if (*P->p == '\'' || *P->p == '"') {                       /* a string; '' or "" inside stands for the quote itself */
        char q = *P->p;
        const char *p = P->p + 1;
        int count = 0, k = 0;

        for (;; ++p) {
            if (*p == '\0') {
                set_err(P->s, "the string is not closed");
                return NULL;
            }
            if (*p == q) {
                if (p[1] == q) {
                    ++p;
                } else {
                    break;
                }
            }
            ++count;
        }
        m = need(P, mat_new(count > 0 ? 1 : 0, count));
        if (m == NULL) {
            return NULL;
        }
        m->kind = MAT_CHAR;
        for (p = P->p + 1; k < count; ++p, ++k) {
            if (*p == q) {
                ++p;
            }
            m->d[k] = (double)(unsigned char)*p;
        }
        P->p = p + 1;                                           /* past the closing quote */
        return m;
    }
    if (*P->p == '(') {
        int saved = P->brack;

        ++P->p;
        P->brack = 0;
        m = parse_expr(P);
        P->brack = saved;
        if (m == NULL) {
            return NULL;
        }
        SKIPWS(P);
        if (*P->p != ')') {
            set_err(P->s, "missing )");
            mat_free(m);
            return NULL;
        }
        ++P->p;
        return m;
    }
    if (*P->p == '[') {
        ++P->p;
        return parse_matrix(P);
    }
    if (isalpha((unsigned char)*P->p) || *P->p == '_') {
        const char *idstart = P->p;

        len = 0;
        while (isalnum((unsigned char)P->p[len]) || P->p[len] == '_') {
            if (len < NAMELEN) {
                name[len] = P->p[len];
            }
            ++len;
        }
        if (len > NAMELEN - 1) {
            set_err(P->s, "name too long");
            return NULL;
        }
        name[len] = '\0';
        P->p += len;
        want = P->s->want_out;                              /* [a,b] = f(...): only the call itself is asked for several results */
        P->s->want_out = 1;
        if (strcmp(name, "end") == 0 && P->endtop > 0) {
            int t = P->endtop - 1;
            double v;

            if (P->endctx[t].n == 1) {
                v = (double)P->endctx[t].r * P->endctx[t].c;
            } else {
                v = P->endctx[t].k == 0 ? P->endctx[t].r : P->endctx[t].c;
            }
            return need(P, mat_scalar(v));
        }
        vm = lookup_value(P, name);
        if (*P->p == '(' && vm != NULL) {                       /* indexing a variable or a parameter */
            Arg args[MAXARGS];
            int n;

            ++P->p;
            n = parse_arglist(P, args, vm);
            if (n < 0) {
                return NULL;
            }
            if (n == 0 || n > 2) {
                set_err(P->s, "indexing needs one or two subscripts");
                free_args(args, n);
                return NULL;
            }
            m = index_value(P, vm, args, n);
            free_args(args, n);
            return m;
        }
        if (vm != NULL) {
            return need(P, mat_copy(vm));
        }
        fi = find_func(P->s, name);
        if (fi >= 0) {
            Arg args[MAXARGS];
            Mat *av[MAXARGS];
            int n, k;

            SKIPWS(P);
            if (*P->p != '(') {
                set_err(P->s, "'%s' is a function: call it as %s(...)", name, name);
                return NULL;
            }
            ++P->p;
            n = parse_arglist(P, args, NULL);
            if (n < 0) {
                return NULL;
            }
            for (k = 0; k < n; ++k) {
                av[k] = args[k].v;
                if (args[k].all) {
                    set_err(P->s, "a lone : is only valid as an index");
                    free_args(args, n);
                    return NULL;
                }
            }
            m = call_fn(P, &P->s->fn[fi], av, n);
            free_args(args, n);
            return m;
        }
        fi = find_ffn(P->s, name);
        if (fi >= 0) {
            Arg args[MAXARGS];
            Mat *av[MAXARGS];
            int n = 0, k;

            SKIPWS(P);
            if (*P->p == '(') {
                ++P->p;
                n = parse_arglist(P, args, NULL);
                if (n < 0) {
                    return NULL;
                }
            }
            for (k = 0; k < n; ++k) {
                av[k] = args[k].v;
                if (args[k].all) {
                    set_err(P->s, "a lone : is only valid as an index");
                    free_args(args, n);
                    return NULL;
                }
            }
            if (++P->s->evals > MAX_EVALS) {
                set_err(P->s, "too many function evaluations (%ld)", MAX_EVALS);
                free_args(args, n);
                return NULL;
            }
            m = call_ffn(P, fi, av, n);
            free_args(args, n);
            return m;
        }
        ti = tool_index(name);
        if (ti >= 0) {
            UserFn f;
            Arg args[MAXARGS];
            Mat *av[MAXARGS];
            int n = 0, k;

            SKIPWS(P);
            if (*P->p != '(') {
                set_err(P->s, "%s needs arguments: %s(f, ...)", name, name);
                return NULL;
            }
            ++P->p;
            if (!parse_fnref(P, &f)) {
                return NULL;
            }
            SKIPWS(P);
            if (*P->p == ',') {
                ++P->p;
                n = parse_arglist(P, args, NULL);
                if (n < 0) {
                    return NULL;
                }
            } else if (*P->p == ')') {
                ++P->p;
            } else {
                set_err(P->s, "expected , or ) after the function");
                return NULL;
            }
            if (n < tools[ti].min || n > tools[ti].max) {
                set_err(P->s, "wrong number of arguments for %s", name);
                free_args(args, n);
                return NULL;
            }
            for (k = 0; k < n; ++k) {
                av[k] = args[k].v;
                if (args[k].all) {
                    set_err(P->s, "a lone : is only valid as an index");
                    free_args(args, n);
                    return NULL;
                }
            }
            m = tools[ti].fn(P, &f, av, n);
            free_args(args, n);
            return m;
        }
        bi = builtin_index(name);
        if (bi >= 0) {
            Arg args[MAXARGS];
            int n = 0;

            SKIPWS(P);
            if (*P->p == '(') {
                ++P->p;
                n = parse_arglist(P, args, NULL);
                if (n < 0) {
                    return NULL;
                }
            }
            if (want > 1) {
                int most = builtin_most_results(name);

                if (want > most) {
                    set_err(P->s, "%s gives %s", name, most == 1 ? "one result" : (most == 2 ? "at most two results" : "at most three results"));
                    free_args(args, n);
                    return NULL;
                }
            }
            P->s->want_out = want;
            P->s->n_extra = 0;
            m = call_function(P, name, bi, args, n);
            P->s->want_out = 1;
            free_args(args, n);
            return m;
        }
        if (strcmp(name, "pi") == 0) {
            return need(P, mat_scalar(PI));
        }
        if (strcmp(name, "eps") == 0) {
            return need(P, mat_scalar(2.220446049250313e-16));
        }
        if (strcmp(name, "Inf") == 0 || strcmp(name, "inf") == 0) {
            return need(P, mat_scalar(make_inf()));
        }
        if (strcmp(name, "NaN") == 0 || strcmp(name, "nan") == 0) {
            return need(P, mat_scalar(make_nan()));
        }
        if (autoload_function(P->s, name) && find_ffn(P->s, name) >= 0) {
            P->p = idstart;                                     /* a function file was read: look the name up again */
            return parse_primary(P);
        }
        set_err(P->s, "undefined variable or function '%s'", name);
        return NULL;
    }
    if (*P->p == '\0') {
        set_err(P->s, "the expression is incomplete");
    } else {
        set_err(P->s, "unexpected '%c'", *P->p);
    }
    return NULL;
}

static Mat *parse_postfix(Parser *P)
{
    Mat *m = parse_primary(P), *t;

    while (m != NULL) {
        const char *save = P->p;

        if (P->brack == 0) {
            SKIPWS(P);
        }
        if (*P->p == '\'') {
            ++P->p;
        } else if (P->p[0] == '.' && P->p[1] == '\'') {
            P->p += 2;
        } else {
            P->p = save;
            break;
        }
        t = need(P, mat_transpose(m));
        mat_free(m);
        m = t;
    }
    return m;
}

static Mat *parse_power(Parser *P)
{
    Mat *base = parse_postfix(P);

    while (base != NULL) {
        const char *save = P->p;
        int elem = 0;
        Mat *ex, *r;
        int neg = 0, pos = 0;

        SKIPWS(P);
        if (P->p[0] == '^') {
            ++P->p;
        } else if (P->p[0] == '.' && P->p[1] == '^') {
            P->p += 2;
            elem = 1;
        } else {
            P->p = save;
            break;
        }
        SKIPWS(P);
        if (*P->p == '-') {
            neg = 1;
            ++P->p;
        } else if (*P->p == '+') {
            pos = 1;
            ++P->p;
        }
        (void)pos;
        ex = parse_postfix(P);
        if (ex == NULL) {
            mat_free(base);
            return NULL;
        }
        if (neg) {
            long i;

            for (i = 0; i < MAT_N(ex); ++i) {
                ex->d[i] = -ex->d[i];
            }
        }
        r = elem ? elementwise(P, '^', base, ex) : do_power(P, base, ex);
        mat_free(base);
        mat_free(ex);
        base = r;
    }
    return base;
}

static Mat *parse_unary(Parser *P)
{
    Mat *m;

    SKIPWS(P);
    if (*P->p == '~' && P->p[1] != '=') {
        long i;

        ++P->p;
        m = parse_unary(P);
        for (i = 0; m != NULL && i < MAT_N(m); ++i) {
            m->d[i] = m->d[i] == 0.0 ? 1.0 : 0.0;
        }
        if (m != NULL) {
            m->kind = MAT_BOOL;
        }
        return m;
    }
    if (*P->p == '-') {
        long i;

        ++P->p;
        m = parse_unary(P);
        for (i = 0; m != NULL && i < MAT_N(m); ++i) {
            m->d[i] = -m->d[i];
        }
        if (m != NULL) {
            m->kind = MAT_NUM;
        }
        return m;
    }
    if (*P->p == '+') {
        ++P->p;
        return parse_unary(P);
    }
    return parse_power(P);
}

static Mat *parse_term(Parser *P)
{
    Mat *left = parse_unary(P);

    while (left != NULL) {
        const char *save = P->p;
        Mat *right, *r;
        int op, elem = 0;

        SKIPWS(P);
        if (P->p[0] == '.' && (P->p[1] == '*' || P->p[1] == '/')) {
            elem = 1;
            op = P->p[1];
            P->p += 2;
        } else if (P->p[0] == '*' || P->p[0] == '/' || P->p[0] == '\\') {
            op = P->p[0];
            ++P->p;
        } else {
            P->p = save;
            break;
        }
        right = parse_unary(P);
        if (right == NULL) {
            mat_free(left);
            return NULL;
        }
        if (elem) {
            r = elementwise(P, op, left, right);
        } else if (op == '*') {
            r = do_multiply(P, left, right);
        } else if (op == '/') {
            r = do_divide(P, left, right);
        } else {
            r = do_solve(P, left, right);
        }
        mat_free(left);
        mat_free(right);
        left = r;
    }
    return left;
}

static Mat *parse_additive(Parser *P)
{
    Mat *left = parse_term(P);

    while (left != NULL) {
        const char *save = P->p;
        Mat *right, *r;
        int op, had;

        had = skip_ws(P);
        if (*P->p != '+' && *P->p != '-') {
            P->p = save;
            break;
        }
        if (P->brack > 0 && had && P->p[1] != ' ' && P->p[1] != '\t') {
            P->p = save;                              /* [1 -2]: the minus starts a new element */
            break;
        }
        op = *P->p;
        ++P->p;
        right = parse_term(P);
        if (right == NULL) {
            mat_free(left);
            return NULL;
        }
        r = elementwise(P, op, left, right);
        mat_free(left);
        mat_free(right);
        left = r;
    }
    return left;
}

static Mat *parse_range(Parser *P)
{
    Mat *a = parse_additive(P), *b, *c = NULL, *m;
    const char *save;
    double lo, step = 1.0, hi;
    long count, i;

    if (a == NULL) {
        return NULL;
    }
    save = P->p;
    SKIPWS(P);
    if (*P->p != ':') {
        P->p = save;
        return a;
    }
    ++P->p;
    b = parse_additive(P);
    if (b == NULL) {
        mat_free(a);
        return NULL;
    }
    SKIPWS(P);
    if (*P->p == ':') {
        ++P->p;
        c = parse_additive(P);
        if (c == NULL) {
            mat_free(a);
            mat_free(b);
            return NULL;
        }
    }
    if (MAT_N(a) < 1 || MAT_N(b) < 1 || (c != NULL && MAT_N(c) < 1)) {
        set_err(P->s, "a range needs scalar limits");
        mat_free(a);
        mat_free(b);
        mat_free(c);
        return NULL;
    }
    lo = a->d[0];
    if (c != NULL) {
        step = b->d[0];
        hi = c->d[0];
    } else {
        hi = b->d[0];
    }
    mat_free(a);
    mat_free(b);
    mat_free(c);
    if (step == 0.0 || is_nan(lo) || is_nan(hi) || is_nan(step) || is_inf(lo) || is_inf(hi)) {
        return need(P, mat_new(1, 0));
    }
    if ((step > 0.0 && lo > hi) || (step < 0.0 && lo < hi)) {
        return need(P, mat_new(1, 0));
    }
    count = (long)floor((hi - lo) / step + 1e-10) + 1;
    if (count > MAT_MAX_ELEMS) {
        set_err(P->s, "a range of %ld elements is larger than the %d element limit", count, MAT_MAX_ELEMS);
        return NULL;
    }
    m = need(P, mat_new(1, (int)count));
    for (i = 0; m != NULL && i < count; ++i) {
        m->d[i] = lo + step * (double)i;
    }
    return m;
}

/* The end of an operand that is not evaluated (the right side of && or || after the left side decided): at a ; or , or an unmatched ) or ]
   outside brackets, or at a || (and at a && when stop_andand). Strings are skipped; a quote after a letter, digit, ) ] ' . is a transpose. */
static const char *skip_expr(const char *p, int stop_andand)
{
    int depth = 0;
    char prev = ' ';

    for (; *p != '\0'; ++p) {
        char c = *p;

        if (c == '\'' || c == '"') {
            int is_str = c == '"' || !(isalnum((unsigned char)prev) || prev == ')' || prev == ']' || prev == '\'' || prev == '.' || prev == '_');

            if (is_str) {
                for (++p; *p != '\0'; ++p) {
                    if (*p == c) {
                        if (p[1] == c) {
                            ++p;
                        } else {
                            break;
                        }
                    }
                }
                if (*p == '\0') {
                    return p;
                }
                prev = c;
                continue;
            }
        }
        if (c == '(' || c == '[') {
            ++depth;
        } else if (c == ')' || c == ']') {
            if (depth == 0) {
                return p;
            }
            --depth;
        } else if (depth == 0) {
            if (c == ',' || c == ';') {
                return p;
            }
            if (c == '|' && p[1] == '|') {
                return p;
            }
            if (stop_andand && c == '&' && p[1] == '&') {
                return p;
            }
        }
        if (c != ' ' && c != '\t') {
            prev = c;
        }
    }
    return p;
}

static Mat *parse_cmp(Parser *P)
{
    Mat *left = parse_range(P);

    while (left != NULL) {
        const char *save = P->p;
        Mat *right, *r;
        int op;

        SKIPWS(P);
        if (P->p[0] == '=' && P->p[1] == '=') {
            op = '=';
            P->p += 2;
        } else if (P->p[0] == '~' && P->p[1] == '=') {
            op = '!';
            P->p += 2;
        } else if (P->p[0] == '<' && P->p[1] == '=') {
            op = 'l';
            P->p += 2;
        } else if (P->p[0] == '>' && P->p[1] == '=') {
            op = 'g';
            P->p += 2;
        } else if (P->p[0] == '<') {
            op = '<';
            ++P->p;
        } else if (P->p[0] == '>') {
            op = '>';
            ++P->p;
        } else {
            P->p = save;
            break;
        }
        right = parse_range(P);
        if (right == NULL) {
            mat_free(left);
            return NULL;
        }
        r = elementwise(P, op, left, right);
        mat_free(left);
        mat_free(right);
        left = r;
    }
    return left;
}

static Mat *parse_and(Parser *P)
{
    Mat *left = parse_cmp(P);

    while (left != NULL) {
        const char *save = P->p;
        Mat *right, *r;

        SKIPWS(P);
        if (!(P->p[0] == '&' && P->p[1] != '&')) {
            P->p = save;
            break;
        }
        ++P->p;
        right = parse_cmp(P);
        if (right == NULL) {
            mat_free(left);
            return NULL;
        }
        r = elementwise(P, '&', left, right);
        mat_free(left);
        mat_free(right);
        left = r;
    }
    return left;
}

static Mat *parse_or(Parser *P)
{
    Mat *left = parse_and(P);

    while (left != NULL) {
        const char *save = P->p;
        Mat *right, *r;

        SKIPWS(P);
        if (!(P->p[0] == '|' && P->p[1] != '|')) {
            P->p = save;
            break;
        }
        ++P->p;
        right = parse_and(P);
        if (right == NULL) {
            mat_free(left);
            return NULL;
        }
        r = elementwise(P, '|', left, right);
        mat_free(left);
        mat_free(right);
        left = r;
    }
    return left;
}

static Mat *parse_andand(Parser *P)
{
    Mat *left = parse_or(P);

    while (left != NULL) {
        const char *save = P->p;
        Mat *right;
        int value;

        SKIPWS(P);
        if (!(P->p[0] == '&' && P->p[1] == '&')) {
            P->p = save;
            break;
        }
        P->p += 2;
        value = is_true(left);
        mat_free(left);
        if (!value) {
            P->p = skip_expr(P->p, 1);                  /* the right side is not evaluated */
        } else {
            right = parse_or(P);
            if (right == NULL) {
                return NULL;
            }
            value = is_true(right);
            mat_free(right);
        }
        left = bool_result(P, value);
    }
    return left;
}

static Mat *parse_oror(Parser *P)
{
    Mat *left = parse_andand(P);

    while (left != NULL) {
        const char *save = P->p;
        Mat *right;
        int value;

        SKIPWS(P);
        if (!(P->p[0] == '|' && P->p[1] == '|')) {
            P->p = save;
            break;
        }
        P->p += 2;
        value = is_true(left);
        mat_free(left);
        if (value) {
            P->p = skip_expr(P->p, 0);
        } else {
            right = parse_andand(P);
            if (right == NULL) {
                return NULL;
            }
            value = is_true(right);
            mat_free(right);
        }
        left = bool_result(P, value);
    }
    return left;
}

static Mat *parse_expr(Parser *P)
{
    return parse_oror(P);
}

/* --- statements --- */

static int word_is(const char *p, const char *word)
{
    size_t n = strlen(word);

    return strncmp(p, word, n) == 0 && !(isalnum((unsigned char)p[n]) || p[n] == '_');
}

#ifndef MS_PRODUCT_NAME
#define MS_PRODUCT_NAME "MathSmith"
#endif

static const char *help_text =
#ifdef MS_LINUX
    MS_PRODUCT_NAME " help\n"
#else
    MS_PRODUCT_NAME " help          arrows, PgUp, PgDn move   F3 find   Esc closes\n"
#endif
    "\n"
    "THE BASICS\n"
    "  Matrices    [1 2; 3 4]          Ranges     1:5   0:0.5:2\n"
    "  Indexing    A(2,3)  A(:,1)  A(end)  A(2:3,[1 3])  (1-based; one index\n"
    "              runs down the columns)\n"
    "  Assigning   x = 3      A(2,3) = 7 (the matrix grows)\n"
    "  ; at the end of a line hides the result.   % starts a comment.\n"
    "  Operators   + - * / \\ ^ '   and element-wise  .* ./ .^\n"
    "              A\\b solves A*x = b      ^ takes an integer power of a matrix\n"
    "  Constants   pi eps Inf NaN ans\n"
    "  Commands    who  clear [name]  format short|long  help  quit\n"
#ifdef MS_486
    "              dir (or ls) lists the .m files of this folder\n"
#endif
#ifdef MS_LINUX
    "  Ctrl-C stops a long calculation.\n"
#else
    "  Esc stops a long calculation.\n"
#endif
#ifdef MS_486
    "  Limits: matrices of up to 250000 elements (500x500), 200 variables, 255 characters a line.\n"
#else
    "  Limits: 64x64 matrices (4096 elements), 40 variables, 255 characters a line.\n"
#endif
    "\n"
    "FUNCTIONS\n"
    "  inv det trace rank size length numel zeros ones eye rand linspace diag\n"
    "  reshape sum prod mean max min cumsum sqrt abs exp log log10 sin cos tan\n"
    "  asin acos atan atan2 floor ceil round fix sign mod rem norm dot transpose\n"
    "  Numbers:  median std var diff cumprod trapz sum(A,dim) mean(A,dim)\n"
    "\n"
    "YOUR OWN FUNCTIONS AND CALCULUS\n"
    "  f = @(x) x^2 - 2        then   f(3)\n"
    "  integral(f,a,b)    fzero(f,x0)  or  fzero(f,[a b])    fminbnd(f,a,b)\n"
    "  deriv(f,x)  deriv(f,x,2)    arrayfun(f,A)\n"
    "  Give f as @(x) expr, @sin, or the name of a function.\n"
    "\n"
    "PROGRAMS\n"
    "  if / elseif / else / end      for k = 1:n      while      break  continue\n"
    "  return.  Type a block over several lines (the prompt becomes three\n"
    "  spaces); the word  abort  drops an unfinished block.\n"
    "  Strings 'text'    disp  fprintf('%d\\n', x)  sprintf  num2str  error\n"
    "  strcmp.   Tests  == ~= < > <= >=   & | ~   && ||      A(A>2)  any  all\n"
    "  find  sort   tic  toc\n"
    "  Files: myscript.m runs when you type  myscript  or  run myscript.  A file\n"
    "  that starts with  function y = f(x)  defines functions, read when called.\n"
    "\n"
    "PLOTS\n"
    "  plot(x,y)   plot(y)   plot(x,y,'r--')   plot(x1,y1,x2,y2)\n"
    "  title('..')  xlabel('..')  ylabel('..')  grid on  axis([x1 x2 y1 y2])\n"
    "  hold on / off      clf      shg\n"
#ifdef MS_LINUX
    "  A plot is saved when the line or script ends, as plot-1.svg, plot-2.svg ... in\n"
    "  this folder (the MATHSMITH_PLOTS variable names another one); shg saves the\n"
    "  last plot again.  Curve colours: 'r' 'g' 'b' 'c' 'm' 'y' 'k' 'w' in the style.\n"
#else
    "  A plot appears when the line or script ends; any key comes back.\n"
#endif
#ifdef MS_LINUX
#elif defined(MS_XT)
    "  F5 shows the last plot again.  This screen has no graphics: a plot is\n"
    "  drawn with characters (half blocks and dots).\n"
#else
    "  F5 shows the last plot again.\n"
#endif
    "\n"
    "LINEAR ALGEBRA AND NUMBERS\n"
    "  eig(A)   [V,D] = eig(A)   eigc(A) (complex eigenvalues as [re im])\n"
    "  svd(A)   [U,S,V] = svd(A)   norm(A,p)   cond   pinv   rank   det   inv\n"
    "  Several results:  [m,i] = max(v)   [s,k] = sort(v)   [r,c] = size(A)\n"
    "                    [~,i] = ...\n"
    "  Polynomials:  polyfit(x,y,n)  polyval(p,x)  roots(p)  rootsc(p)\n"
#ifdef MS_FFT
    "  Fourier:  F = fft(x)  x = ifft(F)  cabs(F)  cangle(F).  No complex numbers:\n"
    "            F has two columns [real imaginary] (as eigc, rootsc).  fft and\n"
    "            ifft take a real vector or such a pair of columns; cabs gives the\n"
#ifdef FFT_POW2_ONLY
    "            magnitude, cangle the phase.  The length must be a power of 2.\n"
#else
    "            magnitude, cangle the phase.  Any length; powers of 2 are fastest.\n"
#endif
#endif
    "\n"
    "THE EDITOR\n"
#ifdef MS_LINUX
    "  edit name opens name.m in your editor ($EDITOR, else nano or vi; a new file if\n"
    "  none); when you leave it the script is read again.  run name runs it.\n"
#else
    "  edit name opens name.m on the whole screen (a new file if none).\n"
    "  F2 save   F7 save under another name   F3 find   F4 go to line\n"
#ifdef MS_486
    "  F8 open another script, chosen from a list\n"
#endif
    "  F5 run the script and come back at the line of the error\n"
#endif
#ifdef MS_NO_AI
    "  Esc or F10 leave\n"
    "\n"
#else
#ifndef MS_LINUX
    "  F6 assistant (explain, fix, change, write)   Alt+Z undo   Esc or F10 leave\n"
    "\n"
#endif
#ifdef MS_LINUX
    "THE ASSISTANT (optional: needs an Ollama on this computer)\n"
    "  ask <question>      code <what to write>      explain name\n"
    "  fix name [\"what is wrong\"]      change name \"what to change\"\n"
    "  why: the last error\n"
    "  model  (a list of the installed models)    model name   uses that model\n"
    "  model default   the first installed one, or $MATHSMITH_MODEL\n"
    "  Ollama is looked for at 127.0.0.1:11434 ($OLLAMA_HOST names another host:port); if\n"
    "  there is none these commands say so and nothing else changes.\n"
    "  Code is shown and saved in ask.m; nothing is run by itself (run ask runs\n"
    "  it).  Ctrl-C stops the wait.\n"
    "\n"
#else
    "THE ASSISTANT (needs MSASK.EXE and the PC server)\n"
    "  ask <question>      code <what to write>      explain name\n"
    "  fix name [\"what is wrong\"]      change name \"what to change\"\n"
    "  why  (or F6): the last error\n"
#ifdef MS_486
    "  model  (a list of the models the PC has; kept in MSMODEL.CFG)\n"
    "  model name   uses that model        model default   the PC's own\n"
    "  edit, run, explain, fix, change with no name: choose the script from a list\n"
#endif
    "  Code is shown and saved in ask.m; nothing is run by itself (run ask runs\n"
    "  it).  Esc stops the wait.\n"
    "\n"
#endif
#endif
    "KEYS AT THE PROMPT\n"
#ifdef MS_LINUX
    "  Up and Down recall earlier lines (kept in ~/.mathsmith_history)\n"
    "  Left, Right, Home, End, Backspace, Del edit the line;  Ctrl-A, Ctrl-E start and\n"
    "  end of line, Ctrl-K, Ctrl-U cut, Ctrl-L clears the screen;  Ctrl-C stops a\n"
    "  long calculation;  Ctrl-D (or quit) leaves.\n";
#else
#ifdef MS_486
    "  F1 help   F2 variables   F3 run a file (a list)   F4 clear the screen\n"
#else
    "  F1 help   F2 variables   F3 run a file   F4 clear the screen\n"
#endif
#ifdef MS_NO_AI
    "  F5 show the last plot   F10 quit\n"
#else
    "  F5 show the last plot   F6 explain the last error   F10 quit\n"
#endif
    "  Up and Down recall earlier lines.   PgUp and PgDn scroll back.\n";
#endif

/* Runs one command if the statement is one; returns -1 if it is not, else ML_OK / ML_QUIT. `p` is trimmed of leading spaces. */
/* Does the word at p name a variable (then it is not a command)? */
static int name_is_variable(MlSession *s, const char *p)
{
    char name[NAMELEN];
    int k = 0;

    while ((isalnum((unsigned char)p[k]) || p[k] == '_') && k < NAMELEN - 1) {
        name[k] = p[k];
        ++k;
    }
    name[k] = '\0';
    return find_var(s, name) >= 0;
}

#ifdef MS_486
static void list_scripts(MlSession *s);
#endif

static int try_command(MlSession *s, Parser *P, const char *p, const char **after)
{
    char rest[NAMELEN + 1];
    int i;
    const char *q;

    if (((*p == 'h' && word_is(p, "hold")) || (*p == 'g' && word_is(p, "grid")) || (*p == 'a' && word_is(p, "axis")) || (*p == 'c' && word_is(p, "clf")) ||
         (*p == 's' && word_is(p, "shg"))) && !name_is_variable(s, p)) {                  /* hold on, grid on, axis tight, clf, shg: commands without parentheses */
        char name[8], word[16];
        int k = 0;

        q = p;
        while (isalpha((unsigned char)*q) && k < 7) {
            name[k++] = *q++;
        }
        name[k] = '\0';
        while (*q == ' ' || *q == '\t') {
            ++q;
        }
        if (*q != '(' && *q != '=') {                       /* axis([...]) and a variable named grid go the usual way */
            k = 0;
            while (isalpha((unsigned char)*q) && k < 15) {
                word[k++] = *q++;
            }
            word[k] = '\0';
            if (*q == '\0' || *q == ' ' || *q == '\t') {
                Parser PP;

                PP.s = s;
                PP.p = q;
                PP.brack = 0;
                PP.endtop = 0;
                if (!plot_command(&PP, name, word)) {
                    if (s->err[0] == '\0') {
                        set_err(s, "%s: unknown option '%s'", name, word);
                    }
                    *after = q;
                    return ML_ERROR;
                }
                *after = q;
                return ML_OK;
            }
        }
    }
#ifdef MS_486
    if (strchr("qehwcfrdl", *p) == NULL || *p == '\0') {               /* the first letter of every command: most statements stop here */
#else
    if (strchr("qehwcfr", *p) == NULL || *p == '\0') {                /* the first letter of every command: most statements stop here */
#endif
        return -1;
    }
    if (word_is(p, "quit") || word_is(p, "exit")) {
        *after = p + 4;
        return ML_QUIT;
    }
    if (word_is(p, "help")) {
        emit(s, help_text);
        *after = p + 4;
        return ML_OK;
    }
#ifdef MS_486
    if ((word_is(p, "dir") || word_is(p, "ls")) && !name_is_variable(s, p)) {          /* the scripts of this folder (XD88) */
        q = p + (p[0] == 'd' ? 3 : 2);
        while (*q == ' ' || *q == '\t') {
            ++q;
        }
        if (*q == '\0' || *q == ';' || *q == ',') {                  /* dir = 5 and dir + 1 are statements about a variable */
            list_scripts(s);
            *after = q;
            return s->err[0] != '\0' ? ML_ERROR : ML_OK;
        }
    }
#endif
    if (word_is(p, "clc")) {
        emit(s, "\f");                                      /* the screen program clears its history; a plain terminal sees a form feed */
        *after = p + 3;
        return ML_OK;
    }
    if (word_is(p, "run")) {
        char file[40];
        int r, k = 0;

        q = p + 3;
        while (*q == ' ' || *q == '\t') {
            ++q;
        }
        while ((isalnum((unsigned char)*q) || *q == '_' || *q == '.') && k < 36) {
            file[k++] = *q++;
        }
        file[k] = '\0';
        *after = q;
        if (k == 0) {
            set_err(s, "run needs a file name: run myscript");
            return ML_ERROR;
        }
        r = run_file(s, file, 0);
        if (r == -1) {
            set_err(s, "cannot find the file %s.m in this folder", file);
            return ML_ERROR;
        }
        if (r == -2) {
            return ML_QUIT;
        }
        return r == 0 ? ML_ERROR : ML_OK;
    }
    if (word_is(p, "who") || word_is(p, "whos")) {
        char line[80];

        q = p + (p[3] == 's' ? 4 : 3);
        for (i = 0; i < s->cur->nvar; ++i) {
            sprintf(line, "  %-12s %d x %d\n", s->cur->var[i].name, s->cur->var[i].m->r, s->cur->var[i].m->c);
            emit(s, line);
        }
        for (i = 0; i < s->nfn; ++i) {
            char text[BODYLEN + 40];

            fn_text(&s->fn[i], text);
            sprintf(line, "  %-12s function ", s->fn[i].name);
            emit(s, line);
            emit(s, text);
            emit(s, "\n");
        }
        for (i = 0; i < s->nffn; ++i) {
            int k;

            sprintf(line, "  %-12s function %s(", s->ffn[i].name, s->ffn[i].name);
            for (k = 0; k < s->ffn[i].np; ++k) {
                strcat(line, k > 0 ? "," : "");
                strcat(line, s->ffn[i].param[k]);
            }
            strcat(line, ")\n");
            emit(s, line);
        }
        if (s->cur->nvar == 0 && s->nfn == 0 && s->nffn == 0) {
            emit(s, "  (no variables)\n");
        }
        *after = q;
        return ML_OK;
    }
    if (word_is(p, "clear")) {
        q = p + 5;
        while (*q == ' ') {
            ++q;
        }
        if (*q == '\0' || *q == ';' || *q == ',' || word_is(q, "all")) {
            while (s->cur->nvar > 0) {
                clear_var(s, s->cur->var[0].name);
            }
            s->nfn = 0;
            while (s->nffn > 0) {
                ffn_remove(s, s->ffn[0].name);
            }
            *after = (*q == '\0' || *q == ';' || *q == ',') ? q : q + 3;
        } else {
            for (i = 0; (isalnum((unsigned char)*q) || *q == '_') && i < NAMELEN; ++i, ++q) {
                rest[i] = *q;
            }
            rest[i] = '\0';
            clear_var(s, rest);
            ffn_remove(s, rest);
            *after = q;
        }
        return ML_OK;
    }
    if (word_is(p, "format")) {
        q = p + 6;
        while (*q == ' ') {
            ++q;
        }
        if (word_is(q, "long")) {
            s->digits = 8;
            *after = q + 4;
        } else if (word_is(q, "short")) {
            s->digits = 4;
            *after = q + 5;
        } else {
            set_err(s, "format: use format short or format long");
            *after = q;
            return ML_ERROR;
        }
        return ML_OK;
    }
    (void)P;
    return -1;
}

/* If the statement at p is "name = ..." or "name(...) = ...", returns 1 (2 with an index) and the name; *eq points at the '='. */
static int find_assignment(const char *p, char *name, const char **paren, const char **eq)
{
    int len = 0, depth;
    const char *q;

    if (!(isalpha((unsigned char)*p) || *p == '_')) {
        return 0;
    }
    while (isalnum((unsigned char)p[len]) || p[len] == '_') {
        if (len < NAMELEN - 1) {
            name[len] = p[len];
        }
        ++len;
    }
    if (len > NAMELEN - 1) {
        return 0;
    }
    name[len] = '\0';
    q = p + len;
    while (*q == ' ' || *q == '\t') {
        ++q;
    }
    if (*q == '=' && q[1] != '=') {
        *eq = q;
        *paren = NULL;
        return 1;
    }
    if (*q == '(') {
        *paren = q;
        depth = 0;
        for (; *q != '\0'; ++q) {
            if (*q == '(' || *q == '[') {
                ++depth;
            } else if (*q == ')' || *q == ']') {
                --depth;
                if (depth == 0) {
                    ++q;
                    break;
                }
            }
        }
        while (*q == ' ' || *q == '\t') {
            ++q;
        }
        if (depth == 0 && *q == '=' && q[1] != '=') {
            *eq = q;
            return 2;
        }
    }
    return 0;
}

/* [a, b] = ... or [a, ~, c] = ...: the names (at most 3) and where the = is; 0 if the text is not that. */
static int find_multi(const char *p, char names[3][NAMELEN], const char **eq)
{
    int k = 0, len;

    ++p;
    for (;;) {
        while (*p == ' ' || *p == '\t' || *p == ',') {
            ++p;
        }
        if (*p == ']') {
            ++p;
            break;
        }
        if (k >= 3) {
            return 0;
        }
        if (*p == '~') {
            strcpy(names[k++], "~");
            ++p;
            continue;
        }
        if (!(isalpha((unsigned char)*p) || *p == '_')) {
            return 0;
        }
        len = 0;
        while (isalnum((unsigned char)p[len]) || p[len] == '_') {
            if (len >= NAMELEN - 1) {
                return 0;
            }
            names[k][len] = p[len];
            ++len;
        }
        names[k][len] = '\0';
        ++k;
        p += len;
    }
    while (*p == ' ' || *p == '\t') {
        ++p;
    }
    if (k == 0 || *p != '=' || p[1] == '=') {
        return 0;
    }
    *eq = p;
    return k;
}

static int word_len_of_name(const char *t)
{
    int n = 0;

    if (!(isalpha((unsigned char)t[0]) || t[0] == '_')) {
        return 0;
    }
    while (isalnum((unsigned char)t[n]) || t[n] == '_') {
        ++n;
    }
    return n;
}

static int name_is_known(MlSession *s, const char *name)
{
    return find_var(s, name) >= 0 || find_func(s, name) >= 0 || find_ffn(s, name) >= 0 || builtin_index(name) >= 0 || tool_index(name) >= 0 ||
           strcmp(name, "pi") == 0 || strcmp(name, "eps") == 0 || strcmp(name, "Inf") == 0 || strcmp(name, "inf") == 0 ||
           strcmp(name, "NaN") == 0 || strcmp(name, "nan") == 0 || strcmp(name, "ans") == 0;
}

typedef struct {
    char name[NAMELEN];
    const Mat *val;                             /* what to show when the statement has no ; (NULL: nothing) */
    char text[BODYLEN + 40];                    /* or this text after "name = " (a function) */
    int nx;                                     /* [a,b,c] = ...: the second and third results to show after the first */
    char xname[3][NAMELEN];
    const Mat *xval[3];
} Shown;

static Mat empty_matrix = {0, 0, NULL, MAT_NUM};

/* Runs one statement; leaves P->p after it (at its ; or , or the end). */
static int run_statement(MlSession *s, Parser *P, Shown *sh)
{
    char name[NAMELEN];
    const char *paren = NULL, *eq = NULL, *after = NULL, *start;
    int kind, rc, i;
    Mat *value;

    sh->val = NULL;
    sh->text[0] = '\0';
    sh->nx = 0;
    s->novalue = 0;
    SKIPWS(P);
    rc = try_command(s, P, P->p, &after);
    if (rc >= 0) {
        P->p = after;
        return rc;
    }
    if (*P->p == '[') {
        char names[3][NAMELEN];
        const char *eqm = NULL;
        int k = find_multi(P->p, names, &eqm), q;

        if (k > 0) {
            Mat *first;

            s->want_out = k;
            s->n_extra = 0;
            for (q = 0; q < 3; ++q) {
                s->extra[q] = NULL;
            }
            P->p = eqm + 1;
            first = parse_expr(P);
            s->want_out = 1;
            if (first != NULL && s->n_extra < k - 1) {
                set_err(s, "the right side gives %d result%s, not %d", s->n_extra + 1, s->n_extra == 0 ? "" : "s", k);
                mat_free(first);
                first = NULL;
            }
            if (first == NULL) {
                for (q = 0; q < s->n_extra; ++q) {
                    mat_free(s->extra[q]);
                }
                s->n_extra = 0;
                return ML_ERROR;
            }
            for (q = 0; q < k; ++q) {
                Mat *v = q == 0 ? first : s->extra[q - 1];
                int vi;

                if (q > 0) {
                    s->extra[q - 1] = NULL;                             /* the variable (or the bin) has it now */
                }
                if (strcmp(names[q], "~") == 0) {                     /* ~ throws that result away */
                    mat_free(v);
                    continue;
                }
                if (!set_var(s, names[q], v)) {                      /* too many variables: set_var freed v; free the rest */
                    int r;

                    for (r = q; r < k - 1; ++r) {
                        mat_free(s->extra[r]);
                        s->extra[r] = NULL;
                    }
                    s->n_extra = 0;
                    return ML_ERROR;
                }
                vi = find_var(s, names[q]);
                if (q == 0) {
                    strcpy(sh->name, names[0]);
                    sh->val = s->cur->var[vi].m;
                } else {
                    strcpy(sh->xname[q - 1], names[q]);
                    sh->xval[q - 1] = s->cur->var[vi].m;
                }
            }
            sh->nx = k - 1;
            s->n_extra = 0;
            return ML_OK;
        }
    }
    kind = find_assignment(P->p, name, &paren, &eq);
    if (kind == 1) {
        P->p = eq + 1;
        SKIPWS(P);
        if (*P->p == '@') {                                   /* f = @(x) expression */
            UserFn f;
            int fi;

            if (!define_function(P, &f)) {
                return ML_ERROR;
            }
            strcpy(f.name, name);
            fi = find_func(s, name);
            if (fi < 0) {
                if (s->nfn >= MAXFUNCS) {
                    set_err(s, "too many functions (%d)", MAXFUNCS);
                    return ML_ERROR;
                }
                fi = s->nfn++;
            }
            s->fn[fi] = f;
            if (find_var(s, name) >= 0) {                     /* a function replaces a variable of the same name */
                Mat *old = s->cur->var[find_var(s, name)].m;
                int vi = find_var(s, name);

                for (; vi + 1 < s->cur->nvar; ++vi) {
                    s->cur->var[vi] = s->cur->var[vi + 1];
                }
                --s->cur->nvar;
                mat_free(old);
            }
            strcpy(sh->name, name);
            fn_text(&f, sh->text);
            return ML_OK;
        }
        value = fast_expr(P, 1);
        if (value == NULL) {
            value = parse_expr(P);
        }
        if (value == NULL) {
            return ML_ERROR;
        }
        if (!set_var(s, name, value)) {
            return ML_ERROR;
        }
        i = find_var(s, name);
        strcpy(sh->name, name);
        sh->val = s->cur->var[i].m;
        return ML_OK;
    }
    if (kind == 2) {
        Arg args[MAXARGS];
        Mat *cur = NULL, *result;
        int n;

        i = find_var(s, name);
        if (i >= 0 && s->cur->var[i].m->kind != MAT_CHAR && MAT_N(s->cur->var[i].m) > 0 && fast_store(P, s->cur->var[i].m, paren, eq)) {
            strcpy(sh->name, name);
            sh->val = s->cur->var[i].m;
            return ML_OK;
        }
        P->p = paren + 1;
        n = parse_arglist(P, args, i >= 0 ? s->cur->var[i].m : &empty_matrix);
        if (n < 0) {
            return ML_ERROR;
        }
        if (n == 0 || n > 2) {
            set_err(s, "indexing needs one or two subscripts");
            free_args(args, n);
            return ML_ERROR;
        }
        P->p = eq + 1;
        value = parse_expr(P);
        if (value == NULL) {
            free_args(args, n);
            return ML_ERROR;
        }
        if (i >= 0) {
            cur = need(P, mat_copy(s->cur->var[i].m));            /* the variable stays as it was if the assignment fails */
            if (cur == NULL) {
                free_args(args, n);
                mat_free(value);
                return ML_ERROR;
            }
        }
        if (value->r == 0 && value->c == 0 && value->kind == MAT_NUM && cur != NULL) {
            result = delete_elements(P, cur, args, n);                      /* A(idx) = [] */
        } else {
            result = assign_index(P, cur, args, n, value);
        }
        free_args(args, n);
        mat_free(value);
        if (result == NULL || !set_var(s, name, result)) {
            return ML_ERROR;
        }
        i = find_var(s, name);
        strcpy(sh->name, name);
        sh->val = s->cur->var[i].m;
        return ML_OK;
    }
    start = P->p;
    {
        /* a bare name that is nothing known may be a script file: myscript */
        char word[NAMELEN];
        int wl = word_len_of_name(start);

        if (wl > 0 && wl < NAMELEN && (start[wl] == '\0' || start[wl] == ' ' || start[wl] == '\t')) {
            const char *rest = start + wl;
            int r;

            while (*rest == ' ' || *rest == '\t') {
                ++rest;
            }
            memcpy(word, start, (size_t)wl);
            word[wl] = '\0';
            if (*rest == '\0' && !name_is_known(s, word)) {
                r = run_file(s, word, 0);
                if (r == 1) {
                    P->p = rest;
                    return ML_OK;
                }
                if (r == 0) {
                    return ML_ERROR;
                }
                if (r == -2) {
                    return ML_QUIT;
                }
            }
        }
    }
    value = fast_expr(P, 1);
    if (value == NULL) {
        value = parse_expr(P);
    }
    if (value == NULL) {
        return ML_ERROR;
    }
    if (s->novalue) {                                  /* disp, fprintf, tic: nothing to keep or show */
        s->novalue = 0;
        mat_free(value);
        return ML_OK;
    }
    {
        /* A bare variable name shows as itself and does not change ans. */
        char word[NAMELEN];
        int len = 0;

        while ((isalnum((unsigned char)start[len]) || start[len] == '_') && len < NAMELEN - 1) {
            word[len] = start[len];
            ++len;
        }
        word[len] = '\0';
        if (len > 0 && start + len == P->p && find_var(s, word) >= 0) {
            mat_free(value);
            strcpy(sh->name, word);
            sh->val = s->cur->var[find_var(s, word)].m;
            return ML_OK;
        }
    }
    if (!set_var(s, "ans", value)) {
        return ML_ERROR;
    }
    strcpy(sh->name, "ans");
    sh->val = s->cur->var[find_var(s, "ans")].m;
    return ML_OK;
}

#ifdef __WATCOMC__
#pragma code_seg("ENG_EXEC", "CODE")
#endif

/* --- statements, blocks, scripts and function files --- */

#define KW_NONE 0
#define KW_IF 1
#define KW_ELSEIF 2
#define KW_ELSE 3
#define KW_END 4
#define KW_FOR 5
#define KW_WHILE 6
#define KW_BREAK 7
#define KW_CONTINUE 8
#define KW_RETURN 9
#define KW_FUNCTION 10

#define EX_BREAK 10
#define EX_CONTINUE 11
#define EX_RETURN 12

#ifdef MS_486
#define MAX_STMTS 20000
#else
#define MAX_STMTS 3000
#endif
#define SCRIPT_DEPTH 8

static char *dup_text(const char *p, size_t n)
{
    char *q = (char *)malloc(n + 1);

    if (q != NULL) {
        memcpy(q, p, n);
        q[n] = '\0';
    }
    return q;
}

typedef struct {
    Stmt *a;
    int n, cap;
} StmtList;

static int list_add(StmtList *l, const char *text, size_t len, int quiet, int line)
{
    if (l->n >= MAX_STMTS) {
        return 0;
    }
    if (l->n >= l->cap) {
        int cap = l->cap == 0 ? 16 : l->cap * 2;
        Stmt *a = (Stmt *)realloc(l->a, (size_t)cap * sizeof(Stmt));

        if (a == NULL) {
            return 0;
        }
        l->a = a;
        l->cap = cap;
    }
    l->a[l->n].text = dup_text(text, len);
    if (l->a[l->n].text == NULL) {
        return 0;
    }
    l->a[l->n].quiet = quiet;
    l->a[l->n].line = line;
    ++l->n;
    return 1;
}

static void list_free(StmtList *l)
{
    int i;

    for (i = 0; i < l->n; ++i) {
        free(l->a[i].text);
    }
    free(l->a);
    l->a = NULL;
    l->n = l->cap = 0;
}

/* Cuts a line into statements at ; and , outside brackets, parentheses and strings. Empty ones are dropped. */
static int split_statements(MlSession *s, const char *line, StmtList *out, int lineno)
{
    const char *start = line, *p = line;
    int depth = 0;
    char prev = ' ';

    for (;; ++p) {
        char c = *p;
        int end = c == '\0';

        if (!end && (c == '\'' || c == '"')) {
            int is_str = c == '"' || !(isalnum((unsigned char)prev) || prev == ')' || prev == ']' || prev == '\'' || prev == '.' || prev == '_');

            if (is_str) {
                for (++p; *p != '\0'; ++p) {
                    if (*p == c) {
                        if (p[1] == c) {
                            ++p;
                        } else {
                            break;
                        }
                    }
                }
                if (*p == '\0') {
                    set_err(s, "the string is not closed");
                    return 0;
                }
                prev = c;
                continue;
            }
        }
        if (!end) {
            if (c == '(' || c == '[') {
                ++depth;
            } else if (c == ')' || c == ']') {
                --depth;
            }
        }
        if (end || (depth <= 0 && (c == ';' || c == ','))) {
            const char *a = start, *b = p;

            while (a < b && (*a == ' ' || *a == '\t' || *a == '\r' || *a == '\n')) {
                ++a;
            }
            while (b > a && (b[-1] == ' ' || b[-1] == '\t' || b[-1] == '\r' || b[-1] == '\n')) {
                --b;
            }
            if (b > a && !list_add(out, a, (size_t)(b - a), c == ';', lineno)) {
                set_err(s, "out of memory, or more than %d statements", MAX_STMTS);
                return 0;
            }
            start = p + 1;
            if (end) {
                break;
            }
            if (depth < 0) {
                depth = 0;
            }
        }
        if (c != ' ' && c != '\t') {
            prev = c;
        }
    }
    return 1;
}

static int word_len_of(const char *t)
{
    int n = 0;

    while (isalnum((unsigned char)t[n]) || t[n] == '_') {
        ++n;
    }
    return n;
}

static int keyword_of(const char *t)
{
    int n = word_len_of(t);
    const char *rest = t + n;

    while (*rest == ' ' || *rest == '\t') {
        ++rest;
    }
#define KW_IS(w) (n == (int)sizeof(w) - 1 && strncmp(t, w, (size_t)n) == 0)
    if (*rest != '=' || rest[1] == '=') {
        if (KW_IS("if")) { return KW_IF; }
        if (KW_IS("elseif")) { return KW_ELSEIF; }
        if (KW_IS("for")) { return KW_FOR; }
        if (KW_IS("while")) { return KW_WHILE; }
        if (KW_IS("function")) { return KW_FUNCTION; }
        if (*rest == '\0') {
            if (KW_IS("else")) { return KW_ELSE; }
            if (KW_IS("end")) { return KW_END; }
            if (KW_IS("break")) { return KW_BREAK; }
            if (KW_IS("continue")) { return KW_CONTINUE; }
            if (KW_IS("return")) { return KW_RETURN; }
        }
    }
#undef KW_IS
    return KW_NONE;
}

static void prog_free(Prog *p)
{
    free(p->endof);
    free(p->next);
    free(p->kw);
    p->endof = p->next = NULL;
    p->kw = NULL;
}

/* Finds the end of every block and the next clause of every if. functions_have_end: whether "function" opens a block closed by end. */
static int prog_link(MlSession *s, Stmt *st, int n, int functions_have_end, Prog *p)
{
    struct {
        int idx, last;
    } stack[40];
    int top = 0, i, open_fn = -1;

    p->st = st;
    p->n = n;
    p->endof = (int *)calloc((size_t)(n > 0 ? n : 1), sizeof(int));
    p->next = (int *)calloc((size_t)(n > 0 ? n : 1), sizeof(int));
    p->kw = (unsigned char *)calloc((size_t)(n > 0 ? n : 1), 1);
    if (p->endof == NULL || p->next == NULL || p->kw == NULL) {
        prog_free(p);
        set_err(s, "out of memory");
        return 0;
    }
    for (i = 0; i < n; ++i) {
        int kw = keyword_of(st[i].text);

        p->kw[i] = (unsigned char)kw;
        p->endof[i] = -1;
        p->next[i] = -1;
        if (kw == KW_IF || kw == KW_FOR || kw == KW_WHILE || (kw == KW_FUNCTION && functions_have_end)) {
            if (top >= 40) {
                set_err(s, "blocks are nested too deep");
                prog_free(p);
                return 0;
            }
            stack[top].idx = i;
            stack[top].last = i;
            ++top;
        } else if (kw == KW_FUNCTION) {
            if (open_fn >= 0) {
                p->endof[open_fn] = i;
            }
            open_fn = i;
        } else if (kw == KW_ELSEIF || kw == KW_ELSE) {
            if (top == 0 || p->kw[stack[top - 1].idx] != KW_IF) {
                set_err(s, "%s without a matching if%s", kw == KW_ELSE ? "else" : "elseif", st[i].line > 0 ? "" : "");
                prog_free(p);
                return 0;
            }
            p->next[stack[top - 1].last] = i;
            stack[top - 1].last = i;
        } else if (kw == KW_END) {
            if (top == 0) {
                set_err(s, "end without a matching if, for, while or function");
                prog_free(p);
                return 0;
            }
            --top;
            p->endof[stack[top].idx] = i;
            if (p->kw[stack[top].idx] == KW_IF) {
                p->next[stack[top].last] = i;
            }
        }
    }
    if (top > 0) {
        set_err(s, "missing end for the %s on line %d", p->kw[stack[top - 1].idx] == KW_IF ? "if" : (p->kw[stack[top - 1].idx] == KW_FOR ? "for" :
                (p->kw[stack[top - 1].idx] == KW_WHILE ? "while" : "function")), st[stack[top - 1].idx].line);
        prog_free(p);
        return 0;
    }
    if (open_fn >= 0) {
        p->endof[open_fn] = n;
    }
    return 1;
}

/* Does the program use end for its functions? (Compare the openers with the ends.) */
static int functions_use_end(const Stmt *st, int n)
{
    int openers = 0, ends = 0, funcs = 0, i;

    for (i = 0; i < n; ++i) {
        int kw = keyword_of(st[i].text);

        if (kw == KW_IF || kw == KW_FOR || kw == KW_WHILE) {
            ++openers;
        } else if (kw == KW_FUNCTION) {
            ++openers;
            ++funcs;
        } else if (kw == KW_END) {
            ++ends;
        }
    }
    return funcs > 0 && ends >= openers;
}

/* --- file functions --- */

static int find_ffn(const MlSession *s, const char *name)
{
    int i;

    for (i = 0; i < s->nffn; ++i) {
        if (s->ffn[i].name[0] == name[0] && strcmp(s->ffn[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

static void ffn_release(FileFn *f)
{
    int i;

    for (i = 0; i < f->body.n; ++i) {
        free(f->own[i].text);
    }
    free(f->own);
    prog_free(&f->body);
    f->own = NULL;
    f->body.n = 0;
}

static void ffn_remove(MlSession *s, const char *name)
{
    int i = find_ffn(s, name);

    if (i >= 0) {
        ffn_release(&s->ffn[i]);
        for (; i + 1 < s->nffn; ++i) {
            s->ffn[i] = s->ffn[i + 1];
        }
        --s->nffn;
    }
}

/* "function y = name(a, b)", "function [y] = name(a)", "function name(a)". */
static int parse_header(MlSession *s, const char *text, FileFn *f)
{
    const char *p = text + 8, *eq = NULL, *q;
    int depth = 0, len;

    memset(f, 0, sizeof(*f));
    while (*p == ' ' || *p == '\t') {
        ++p;
    }
    for (q = p; *q != '\0'; ++q) {
        if (*q == '(' || *q == '[') {
            ++depth;
        } else if (*q == ')' || *q == ']') {
            --depth;
        } else if (*q == '=' && depth == 0) {
            eq = q;
            break;
        }
    }
    if (eq != NULL) {
        const char *a = p, *b = eq;
        int names = 0;

        while (a < b && (*a == ' ' || *a == '\t' || *a == '[')) {
            ++a;
        }
        while (b > a && (b[-1] == ' ' || b[-1] == '\t' || b[-1] == ']')) {
            --b;
        }
        len = (int)(b - a);
        for (q = a; q < b; ++q) {
            if (*q == ',' || *q == ' ') {
                names = 2;
            }
        }
        if (len <= 0 || len >= NAMELEN || names) {
            set_err(s, "a function has one output name (function y = name(args))");
            return 0;
        }
        memcpy(f->out, a, (size_t)len);
        f->out[len] = '\0';
        p = eq + 1;
        while (*p == ' ' || *p == '\t') {
            ++p;
        }
    }
    len = word_len_of(p);
    if (len == 0 || len >= NAMELEN) {
        set_err(s, "the function needs a name");
        return 0;
    }
    memcpy(f->name, p, (size_t)len);
    f->name[len] = '\0';
    p += len;
    while (*p == ' ' || *p == '\t') {
        ++p;
    }
    if (*p == '(') {
        ++p;
        for (;;) {
            while (*p == ' ' || *p == '\t') {
                ++p;
            }
            if (*p == ')') {
                break;
            }
            len = word_len_of(p);
            if (len == 0 || len >= NAMELEN || f->np >= MAXFPARAMS) {
                set_err(s, "bad parameter list (at most %d parameters)", MAXFPARAMS);
                return 0;
            }
            memcpy(f->param[f->np], p, (size_t)len);
            f->param[f->np][len] = '\0';
            ++f->np;
            p += len;
            while (*p == ' ' || *p == '\t') {
                ++p;
            }
            if (*p == ',') {
                ++p;
            } else if (*p != ')') {
                set_err(s, "bad parameter list");
                return 0;
            }
        }
    }
    return 1;
}

/* Registers the function whose header is statement `h` of the program and whose body ends before statement `e`. */
static int define_ffn(MlSession *s, const Prog *prog, int h, int e)
{
    FileFn f;
    int i, n = e - h - 1, fi;

    if (!parse_header(s, prog->st[h].text, &f)) {
        return 0;
    }
    f.own = (Stmt *)calloc((size_t)(n > 0 ? n : 1), sizeof(Stmt));
    if (f.own == NULL) {
        set_err(s, "out of memory");
        return 0;
    }
    for (i = 0; i < n; ++i) {
        f.own[i].text = dup_text(prog->st[h + 1 + i].text, strlen(prog->st[h + 1 + i].text));
        f.own[i].quiet = prog->st[h + 1 + i].quiet;
        f.own[i].line = prog->st[h + 1 + i].line;
        if (f.own[i].text == NULL) {
            while (i-- > 0) {
                free(f.own[i].text);
            }
            free(f.own);
            set_err(s, "out of memory");
            return 0;
        }
    }
    f.body.n = n;
    if (!prog_link(s, f.own, n, 1, &f.body)) {
        for (i = 0; i < n; ++i) {
            free(f.own[i].text);
        }
        free(f.own);
        return 0;
    }
    fi = find_ffn(s, f.name);
    if (fi >= 0) {
        ffn_release(&s->ffn[fi]);
    } else {
        if (s->nffn >= MAXFFN) {
            set_err(s, "too many functions (%d)", MAXFFN);
            for (i = 0; i < n; ++i) {
                free(f.own[i].text);
            }
            free(f.own);
            prog_free(&f.body);
            return 0;
        }
        fi = s->nffn++;
    }
    s->ffn[fi] = f;
    s->ffn[fi].body.st = s->ffn[fi].own;
    clear_func(s, f.name);
    return 1;
}

static int exec_range(MlSession *s, const Prog *p, int from, int to);

static Mat *call_ffn(Parser *P, int ffi, Mat **argv, int n)
{
    MlSession *s = P->s;
    FileFn *f = &s->ffn[ffi];
    Frame *fr, *saved;
    int i, rc, vi, savedbase;
    Mat *result = NULL;

    if (n > f->np) {
        set_err(s, "%s takes %d argument%s (%d given)", f->name, f->np, f->np == 1 ? "" : "s", n);
        return NULL;
    }
    if (s->fdepth >= MAX_FDEPTH) {
        set_err(s, "functions are nested too deep (does %s call itself without end?)", f->name);
        return NULL;
    }
    if (s->frames[s->fdepth] == NULL) {
        s->frames[s->fdepth] = (Frame *)calloc(1, sizeof(Frame));
    }
    fr = s->frames[s->fdepth];
    if (fr == NULL) {
        need(P, NULL);
        return NULL;
    }
    fr->nvar = 0;
    saved = s->cur;
    savedbase = s->bindbase;
    s->cur = fr;
    s->bindbase = s->nbind;
    ++s->fdepth;
    rc = ML_OK;
    for (i = 0; i < n && rc == ML_OK; ++i) {
        Mat *copy = need(P, mat_copy(argv[i]));

        if (copy == NULL || !put_var(s, f->param[i], copy)) {
            rc = ML_ERROR;
        }
    }
    if (rc == ML_OK && !put_var(s, "nargin", need(P, mat_scalar((double)n)))) {
        rc = ML_ERROR;
    }
    if (rc == ML_OK) {
        rc = exec_range(s, &f->body, 0, f->body.n);
        if (rc == EX_RETURN || rc == EX_BREAK || rc == EX_CONTINUE) {
            rc = ML_OK;
        }
        if (rc == ML_ERROR && s->errline > 0 && s->errfile[0] == '\0') {
            sprintf(s->errfile, "function %s", f->name);
        }
    }
    if (rc == ML_OK) {
        if (f->out[0] != '\0') {
            vi = find_var(s, f->out);
            if (vi < 0) {
                set_err(s, "%s did not set its output '%s'", f->name, f->out);
            } else {
                result = need(P, mat_copy(fr->var[vi].m));
            }
        } else {
            result = need(P, mat_new(0, 0));
        }
    } else if (rc == ML_QUIT) {
        set_err(s, "quit inside a function");
    }
    for (i = 0; i < fr->nvar; ++i) {
        mat_free(fr->var[i].m);
    }
    fr->nvar = 0;
    s->cur = saved;
    s->bindbase = savedbase;
    --s->fdepth;
    s->novalue = f->out[0] == '\0' && result != NULL;
    return result;
}

/* --- running statements --- */

static int exec_simple(MlSession *s, const Stmt *st)
{
    Parser P;
    Shown sh;
    int rc;
    char msg[BODYLEN + 80];

    P.s = s;
    P.p = st->text;
    P.brack = 0;
    P.endtop = 0;
    rc = run_statement(s, &P, &sh);
    if (rc == ML_OK) {
        SKIPWS(&P);
        if (*P.p != '\0') {
            set_err(s, "unexpected '%c'", *P.p);
            rc = ML_ERROR;
        }
    }
    if (rc == ML_OK && !st->quiet) {
        int k;

        if (sh.text[0] != '\0') {
            sprintf(msg, "%s = %s\n", sh.name, sh.text);
            emit(s, msg);
        } else if (sh.val != NULL) {
            show_value(s, sh.name, sh.val);
        }
        for (k = 0; k < sh.nx; ++k) {
            if (sh.xval[k] != NULL) {
                show_value(s, sh.xname[k], sh.xval[k]);
            }
        }
    }
    return rc;
}

static int eval_cond(MlSession *s, const char *text, int *truth)
{
    Parser P;
    Mat *m;

    P.s = s;
    P.p = text;
    P.brack = 0;
    P.endtop = 0;
    m = fast_expr(&P, 1);
    if (m == NULL) {
        m = parse_expr(&P);
    }
    if (m == NULL) {
        if (s->err[0] == '\0') {
            set_err(s, "the condition is missing or wrong");
        }
        return ML_ERROR;
    }
    SKIPWS(&P);
    if (*P.p != '\0') {
        set_err(s, "unexpected '%c' in the condition", *P.p);
        mat_free(m);
        return ML_ERROR;
    }
    *truth = is_true(m);
    mat_free(m);
    return ML_OK;
}

/* Evaluates the text as one number. */
static int eval_number(MlSession *s, const char *text, double *out)
{
    Parser P;
    Mat *m;

    P.s = s;
    P.p = text;
    P.brack = 0;
    P.endtop = 0;
    m = fast_expr(&P, 1);
    if (m == NULL) {
        m = parse_expr(&P);
    }
    if (m == NULL) {
        return 0;
    }
    SKIPWS(&P);
    if (*P.p != '\0' || MAT_N(m) != 1) {
        if (s->err[0] == '\0') {
            set_err(s, "the loop limits must be single numbers");
        }
        mat_free(m);
        return 0;
    }
    *out = m->d[0];
    mat_free(m);
    return 1;
}

/* The top-level colons of text (outside brackets, parentheses and strings): positions in colon[], count returned. */
static int top_colons(const char *text, const char **colon, int max)
{
    int depth = 0, n = 0;
    char prev = ' ';
    const char *p;

    for (p = text; *p != '\0'; ++p) {
        char c = *p;

        if (c == '\'' || c == '"') {
            int is_str = c == '"' || !(isalnum((unsigned char)prev) || prev == ')' || prev == ']' || prev == '\'' || prev == '.' || prev == '_');

            if (is_str) {
                for (++p; *p != '\0'; ++p) {
                    if (*p == c) {
                        if (p[1] == c) {
                            ++p;
                        } else {
                            break;
                        }
                    }
                }
                if (*p == '\0') {
                    return n;
                }
                prev = c;
                continue;
            }
        }
        if (c == '(' || c == '[') {
            ++depth;
        } else if (c == ')' || c == ']') {
            --depth;
        } else if (c == ':' && depth == 0 && n < max) {
            colon[n++] = p;
        }
        if (c != ' ' && c != '\t') {
            prev = c;
        }
    }
    return n;
}

static int run_body(MlSession *s, const Prog *p, int from, int to, int *stop)
{
    int rc = exec_range(s, p, from, to);

    *stop = 0;
    if (rc == EX_BREAK) {
        *stop = 1;
        return ML_OK;
    }
    if (rc == EX_CONTINUE) {
        return ML_OK;
    }
    return rc;
}

static int do_for(MlSession *s, const Prog *p, int i)
{
    const char *t = p->st[i].text + 3, *eqp, *rest, *colon[2];
    char var[NAMELEN], part[3][BODYLEN];
    int e = p->endof[i], len, ncol, stop = 0, rc;
    long k, count;
    double lo, hi, step = 1.0;

    while (*t == ' ' || *t == '\t') {
        ++t;
    }
    len = word_len_of(t);
    if (len == 0 || len >= NAMELEN) {
        set_err(s, "for needs a loop variable: for k = 1:10");
        return ML_ERROR;
    }
    memcpy(var, t, (size_t)len);
    var[len] = '\0';
    eqp = t + len;
    while (*eqp == ' ' || *eqp == '\t') {
        ++eqp;
    }
    if (*eqp != '=') {
        set_err(s, "for needs = : for k = 1:10");
        return ML_ERROR;
    }
    rest = eqp + 1;
    ncol = top_colons(rest, colon, 2);
    if (ncol >= 1) {                                      /* a range: counted without making the vector */
        const char *seg[4];
        int j;

        seg[0] = rest;
        for (j = 0; j < ncol; ++j) {
            seg[j + 1] = colon[j] + 1;
        }
        for (j = 0; j <= ncol; ++j) {
            const char *a = seg[j], *b = j < ncol ? colon[j] : rest + strlen(rest);
            size_t n = (size_t)(b - a);

            if (n >= BODYLEN) {
                set_err(s, "the loop range is too long");
                return ML_ERROR;
            }
            memcpy(part[j], a, n);
            part[j][n] = '\0';
        }
        if (!eval_number(s, part[0], &lo) || !eval_number(s, part[ncol == 2 ? 2 : 1], &hi) || (ncol == 2 && !eval_number(s, part[1], &step))) {
            return ML_ERROR;
        }
        if (step == 0.0 || (step > 0.0 && lo > hi) || (step < 0.0 && lo < hi)) {
            return ML_OK;
        }
        count = (long)floor((hi - lo) / step + 1e-10) + 1;
        for (k = 0; k < count; ++k) {
            if (mat_check_abort()) {
                set_err(s, "interrupted");
                return ML_ERROR;
            }
            if (!set_var(s, var, mat_scalar(lo + step * (double)k))) {
                return ML_ERROR;
            }
            rc = run_body(s, p, i + 1, e, &stop);
            if (rc != ML_OK) {
                return rc;
            }
            if (stop) {
                break;
            }
        }
        return ML_OK;
    }
    {                                                      /* any other value: its columns, one at a time */
        Parser P;
        Mat *v;
        int j, r;

        P.s = s;
        P.p = rest;
        P.brack = 0;
        P.endtop = 0;
        v = parse_expr(&P);
        if (v == NULL) {
            return ML_ERROR;
        }
        SKIPWS(&P);
        if (*P.p != '\0') {
            set_err(s, "unexpected '%c' in the for line", *P.p);
            mat_free(v);
            return ML_ERROR;
        }
        for (j = 0; j < v->c; ++j) {
            Mat *col = mat_new(v->r, 1);

            if (col == NULL) {
                mat_free(v);
                set_err(s, "out of memory");
                return ML_ERROR;
            }
            col->kind = v->kind;
            for (r = 0; r < v->r; ++r) {
                col->d[r] = MAT_AT(v, r, j);
            }
            if (mat_check_abort()) {
                mat_free(col);
                mat_free(v);
                set_err(s, "interrupted");
                return ML_ERROR;
            }
            if (!set_var(s, var, col)) {
                mat_free(v);
                return ML_ERROR;
            }
            rc = run_body(s, p, i + 1, e, &stop);
            if (rc != ML_OK) {
                mat_free(v);
                return rc;
            }
            if (stop) {
                break;
            }
        }
        mat_free(v);
    }
    return ML_OK;
}

/* Runs statements from..to-1 of the program. Returns ML_OK, ML_ERROR, ML_QUIT, or EX_BREAK / EX_CONTINUE / EX_RETURN for the caller to act on. */
static int exec_range(MlSession *s, const Prog *p, int from, int to)
{
    int i = from, rc, truth, c, e, stop;

    while (i < to) {
        const Stmt *st = &p->st[i];

        switch (p->kw[i]) {
        case KW_IF:
            c = i;
            e = p->endof[i];
            for (;;) {
                if (p->kw[c] == KW_ELSE) {
                    rc = exec_range(s, p, c + 1, p->next[c]);
                    break;
                }
                rc = eval_cond(s, st[c - i].text + (p->kw[c] == KW_IF ? 2 : 6), &truth);
                if (rc != ML_OK) {
                    break;
                }
                if (truth) {
                    rc = exec_range(s, p, c + 1, p->next[c]);
                    break;
                }
                c = p->next[c];
                if (c == e) {
                    rc = ML_OK;
                    break;
                }
            }
            if (rc != ML_OK) {
                if (rc == ML_ERROR && s->errline == 0) {
                    s->errline = p->st[c].line;
                }
                return rc;
            }
            i = e + 1;
            break;
        case KW_FOR:
            rc = do_for(s, p, i);
            if (rc != ML_OK) {
                if (rc == ML_ERROR && s->errline == 0) {
                    s->errline = st->line;
                }
                return rc;
            }
            i = p->endof[i] + 1;
            break;
        case KW_WHILE:
            e = p->endof[i];
            for (;;) {
                if (mat_check_abort()) {
                    set_err(s, "interrupted");
                    rc = ML_ERROR;
                    break;
                }
                rc = eval_cond(s, st->text + 5, &truth);
                if (rc != ML_OK || !truth) {
                    break;
                }
                rc = run_body(s, p, i + 1, e, &stop);
                if (rc != ML_OK || stop) {
                    break;
                }
            }
            if (rc != ML_OK) {
                if (rc == ML_ERROR && s->errline == 0) {
                    s->errline = st->line;
                }
                return rc;
            }
            i = e + 1;
            break;
        case KW_BREAK:
            return EX_BREAK;
        case KW_CONTINUE:
            return EX_CONTINUE;
        case KW_RETURN:
            return EX_RETURN;
        case KW_FUNCTION:
            e = p->endof[i] < 0 ? p->n : p->endof[i];
            if (!define_ffn(s, p, i, e)) {
                if (s->errline == 0) {
                    s->errline = st->line;
                }
                return ML_ERROR;
            }
            if (s->sdepth == 0 && s->fdepth == 0) {                  /* typed at the prompt: say what was stored */
                char msg[120];
                sprintf(msg, "(function defined with %d statement%s)\n", e - i - 1, e - i - 1 == 1 ? "" : "s");
                emit(s, msg);
                if (e - i - 1 == 0) {
                    emit(s, "warning: the function has no statements; type the body between the function line and end\n");
                }
            }
            i = (e < p->n && p->kw[e] == KW_END) ? e + 1 : e;      /* with end: past it; without: the next function or the end */
            break;
        case KW_ELSE:
        case KW_ELSEIF:
        case KW_END:
            set_err(s, "unexpected %s", p->kw[i] == KW_END ? "end" : "else");
            s->errline = st->line;
            return ML_ERROR;
        default:
            rc = exec_simple(s, st);
            if (rc != ML_OK) {
                if (rc == ML_ERROR && s->errline == 0) {
                    s->errline = st->line;
                }
                return rc;
            }
            ++i;
            break;
        }
    }
    return ML_OK;
}

/* --- script and function files --- */

static int read_file_statements(MlSession *s, const char *name, StmtList *out)
{
    FILE *f = fopen(name, "r");
    char line[320];
    int lineno = 0;

    if (f == NULL) {
        return -1;
    }
    while (fgets(line, sizeof(line), f) != NULL) {
        char *pc;

        ++lineno;
        if (strchr(line, '\n') == NULL && !feof(f)) {
            set_err(s, "line %d of %s is longer than %d characters", lineno, name, (int)sizeof(line) - 2);
            fclose(f);
            return 0;
        }
        pc = find_comment(line);
        if (pc != NULL) {
            *pc = '\0';
        }
        if (!split_statements(s, line, out, lineno)) {
            fclose(f);
            return 0;
        }
    }
    fclose(f);
    return 1;
}

#ifdef MS_486
#ifdef __WATCOMC__
#include <dos.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#endif

static int script_order(const void *a, const void *b)
{
    return strcmp(((const MlScript *)a)->name, ((const MlScript *)b)->name);
}

int ml_list_scripts(MlScript *items, int max)
{
    int n = 0;
#ifdef __WATCOMC__
    struct find_t fb;
    unsigned rc;

    for (rc = _dos_findfirst("*.m", _A_NORMAL, &fb); rc == 0 && n < max; rc = _dos_findnext(&fb)) {
        const char *dot = strchr(fb.name, '.');
        size_t len = dot != NULL ? (size_t)(dot - fb.name) : 0, k;

        if (len == 0 || len > 8 || (dot[1] != 'M' && dot[1] != 'm') || dot[2] != '\0') {
            continue;
        }
        for (k = 0; k < len; ++k) {
            items[n].name[k] = (char)tolower((unsigned char)fb.name[k]);
        }
        items[n].name[len] = '\0';
        items[n].size = (long)fb.size;
        items[n].year = 1980 + (fb.wr_date >> 9);
        items[n].month = (fb.wr_date >> 5) & 15;
        items[n].day = fb.wr_date & 31;
        ++n;
    }
    _dos_findclose(&fb);
#else
    DIR *d = opendir(".");
    struct dirent *e;

    if (d == NULL) {
        return 0;
    }
    while ((e = readdir(d)) != NULL && n < max) {
        size_t len = strlen(e->d_name);
        struct stat st;
        struct tm *tm;

        if (len < 3 || len > 10 || strcmp(e->d_name + len - 2, ".m") != 0 || stat(e->d_name, &st) != 0 || !S_ISREG(st.st_mode)) {
            continue;
        }
        memcpy(items[n].name, e->d_name, len - 2);
        items[n].name[len - 2] = '\0';
        items[n].size = (long)st.st_size;
        tm = localtime(&st.st_mtime);
        items[n].year = tm->tm_year + 1900;
        items[n].month = tm->tm_mon + 1;
        items[n].day = tm->tm_mday;
        ++n;
    }
    closedir(d);
#endif
    qsort(items, (size_t)n, sizeof(MlScript), script_order);
    return n;
}

#define DIR_MAX 300

/* dir / ls: the scripts of this folder, one a line. */
static void list_scripts(MlSession *s)
{
    MlScript *items = (MlScript *)malloc(DIR_MAX * sizeof(MlScript));
    char line[80];
    int n, i;

    if (items == NULL) {
        set_err(s, "out of memory");
        return;
    }
    n = ml_list_scripts(items, DIR_MAX);
    if (n == 0) {
        emit(s, "  (no .m files in this folder)\n");
    }
    for (i = 0; i < n; ++i) {
        sprintf(line, "  %-9s %7ld  %04d-%02d-%02d\n", items[i].name, items[i].size, items[i].year, items[i].month, items[i].day);
        emit(s, line);
    }
    if (n > 0) {
        sprintf(line, "  %d script%s%s\n", n, n == 1 ? "" : "s", n == DIR_MAX ? " (the list stops here)" : "");
        emit(s, line);
    }
    free(items);
}
#endif

static void file_name_for(const char *name, char *path)
{
    size_t n = strlen(name), i;

    if (n > 2 && name[n - 2] == '.' && (name[n - 1] == 'm' || name[n - 1] == 'M')) {
        strcpy(path, name);
    } else {
        strcpy(path, name);
        strcat(path, ".m");
    }
#ifndef MS_LINUX                                           /* DOS file names have no case; Linux's do */
    for (i = 0; path[i] != '\0'; ++i) {
        path[i] = (char)tolower((unsigned char)path[i]);
    }
#else
    (void)i;
#endif
}

/* Reads name.m and runs it (a script) or defines its functions. kind_wanted: 0 either, 1 only a function file. Returns -1 if there is no
   such file, 0 on an error, 1 if it ran as a script, 2 if it defined functions. */
static int run_file(MlSession *s, const char *name, int functions_only)
{
    char path[64];
    StmtList l;
    Prog prog;
    int rc, got, isfn;
    const char *savedfile;
    int savedline;

    memset(&l, 0, sizeof(l));
    file_name_for(name, path);
    got = read_file_statements(s, path, &l);
    if (got < 0) {
        return -1;
    }
    if (got == 0) {
        list_free(&l);
        return 0;
    }
    isfn = l.n > 0 && keyword_of(l.a[0].text) == KW_FUNCTION;
    if (!isfn && functions_only) {
        list_free(&l);
        return -1;
    }
    if (s->sdepth >= SCRIPT_DEPTH) {
        list_free(&l);
        set_err(s, "scripts are nested too deep (does %s run itself?)", path);
        return 0;
    }
    if (!prog_link(s, l.a, l.n, isfn ? functions_use_end(l.a, l.n) : 1, &prog)) {
        list_free(&l);
        return 0;
    }
    savedfile = s->curfile;
    savedline = s->errline;
    s->curfile = path;
    ++s->sdepth;
    rc = exec_range(s, &prog, 0, prog.n);
    --s->sdepth;
    if (rc == ML_ERROR && s->errline > 0 && s->errfile[0] == '\0') {
        strncpy(s->errfile, path, sizeof(s->errfile) - 1);
        s->errfile[sizeof(s->errfile) - 1] = '\0';
    }
    s->curfile = savedfile;
    if (rc == ML_OK || rc == EX_RETURN || rc == EX_BREAK || rc == EX_CONTINUE) {
        s->errline = savedline;
    }
    prog_free(&prog);
    list_free(&l);
    if (rc == ML_QUIT) {
        return -2;
    }
    return (rc == ML_OK || rc == EX_RETURN || rc == EX_BREAK || rc == EX_CONTINUE) ? (isfn ? 2 : 1) : 0;
}

static int autoload_function(MlSession *s, const char *name)
{
    int r = run_file(s, name, 1);

    return r == 2;
}

#ifdef __WATCOMC__
#pragma code_seg("ENG_MAIN", "CODE")
#endif

MlSession *ml_open(ml_sink sink, void *ctx)
{
    MlSession *s = (MlSession *)calloc(1, sizeof(MlSession));

    if (s != NULL) {
        s->sink = sink;
        s->ctx = ctx;
        s->cur = &s->glob;
        s->digits = 4;
        s->seed = 12345UL;
    }
    return s;
}

static void pending_clear(MlSession *s)
{
    int i;

    for (i = 0; i < s->npend; ++i) {
        free(s->pend[i].text);
    }
    free(s->pend);
    s->pend = NULL;
    s->npend = s->cappend = s->pdepth = 0;
}

void ml_close(MlSession *s)
{
    if (s != NULL) {
        pending_clear(s);
        plot_reset(s, 0);
        {
            int k;

            for (k = 0; k <= MAX_FDEPTH; ++k) {
                free(s->frames[k]);
            }
        }
        while (s->nffn > 0) {
            ffn_remove(s, s->ffn[0].name);
        }
        while (s->glob.nvar > 0) {
            clear_var(s, s->glob.var[0].name);
        }
        free(s);
    }
}

const char *ml_error(const MlSession *s)
{
    return s->err;
}

void ml_info(const MlSession *s, int *nvar, int *nfunc, int *digits)
{
    *nvar = s->glob.nvar;
    *nfunc = s->nfn + s->nffn;
    *digits = s->digits;
}

/* The % that starts a comment: not one inside a string. A quote after a letter, digit, ) ] ' . _ is a transpose, otherwise it opens a string. */
static char *find_comment(char *p)
{
    char prev = ' ';

    for (; *p != '\0'; ++p) {
        char c = *p;

        if (c == '%') {
            return p;
        }
        if (c == '\'' || c == '"') {
            int is_str = c == '"' || !(isalnum((unsigned char)prev) || prev == ')' || prev == ']' || prev == '\'' || prev == '.' || prev == '_');

            if (is_str) {
                for (++p; *p != '\0'; ++p) {
                    if (*p == c) {
                        if (p[1] == c) {
                            ++p;
                        } else {
                            break;
                        }
                    }
                }
                if (*p == '\0') {
                    return NULL;
                }
                prev = c;
                continue;
            }
        }
        if (c != ' ' && c != '\t') {
            prev = c;
        }
    }
    return NULL;
}


/* How much deeper a statement list goes: if/for/while/function open a block, end closes one. */
static int depth_change(const Stmt *st, int n)
{
    int d = 0, i;

    for (i = 0; i < n; ++i) {
        int kw = keyword_of(st[i].text);

        if (kw == KW_IF || kw == KW_FOR || kw == KW_WHILE || kw == KW_FUNCTION) {
            ++d;
        } else if (kw == KW_END) {
            --d;
        }
    }
    return d;
}

static void report_error(MlSession *s)
{
    char msg[300];

    if (s->errline > 0 && s->errfile[0] != '\0') {
        sprintf(msg, "error: %s (line %d of %s)\n", s->err[0] != '\0' ? s->err : "something went wrong", s->errline, s->errfile);
    } else if (s->errline > 0) {
        sprintf(msg, "error: %s (line %d of the block)\n", s->err[0] != '\0' ? s->err : "something went wrong", s->errline);
    } else {
        sprintf(msg, "error: %s\n", s->err[0] != '\0' ? s->err : "something went wrong");
    }
    emit(s, msg);
    if (!s->assisting) {                                    /* what the assistant's why explains */
        size_t n = strlen(msg);

        if (n > 0 && msg[n - 1] == '\n') {
            msg[n - 1] = '\0';
        }
        msg[sizeof(s->why_msg) - 1] = '\0';
        strcpy(s->why_msg, msg);
        strcpy(s->why_file, s->errline > 0 ? s->errfile : "");
        s->why_line = s->errline;
        strcpy(s->why_text, s->err[0] != '\0' ? s->err : "something went wrong");
    }
}


/* --- the assistant (M07, decision XD75): the engine builds the request, the front end's hook runs MSASK.EXE and returns the answer --- */

#ifdef MS_486
#define ASK_CONTEXT 3984                                    /* MSASK.EXE sends 4000 characters at most and the request starts with the #machine= line (XD87) */
#else
#define ASK_CONTEXT 4000                                    /* the most context sent: the serial link carries about 2 KB/s at 19200 baud */
#endif
#define ASK_ANSWER 6000
#define ASK_ILEN 240                                        /* the server's limit for the instruction */
#define ASK_FILE "ask.m"
#ifdef MS_LINUX
#define STOP_KEY "Ctrl-C"
#else
#define STOP_KEY "Esc"
#endif
#define ASK_IN "ASKIN.TXT"                                   /* the request: the mode, the instruction, the context */
#define ASK_OUT "ASKOUT.TXT"                                 /* the answer, or the reason it failed */
enum { A_ASK = 1, A_CODE, A_EXPLAIN, A_FIX, A_WHY, A_EDIT, A_CHANGE, A_MODEL };

void ml_set_ask_hook(MlSession *s, ml_ask_fn fn, void *ctx)
{
    s->ask_hook = fn;
    s->ask_ctx = ctx;
}

const char *ml_help_text(void)
{
    return help_text;
}

void ml_set_edit_hook(MlSession *s, ml_edit_fn fn, void *ctx)
{
    s->edit_hook = fn;
    s->edit_ctx = ctx;
}

/* The last error that was not an assistant command's: its text, and where it happened (a script's file name and the line; 0 and "" if it was typed). */
int ml_last_error(const MlSession *s, char *text, int textmax, char *file, int filemax)
{
    int line = s->why_msg[0] != '\0' && s->why_file[0] != '\0' ? s->why_line : 0;

    strncpy(text, s->why_text, (size_t)textmax - 1);
    text[textmax - 1] = '\0';
    strncpy(file, line > 0 ? s->why_file : "", (size_t)filemax - 1);
    file[filemax - 1] = '\0';
    return line;
}

/* A script was changed on disk: a function that was read from it earlier must be read again (functions are cached under the file's name). */
void ml_file_changed(MlSession *s, const char *name)
{
    char path[64];
    size_t n;

    file_name_for(name, path);
    n = strlen(path);
    if (n > 2 && path[n - 2] == '.' && path[n - 1] == 'm') {
        path[n - 2] = '\0';
    }
    ffn_remove(s, path);
}

/* Is the line an assistant command (ask, code, explain, fix, why)? Returns its kind and sets *args to what follows, else 0. A variable of
   that name, "code = 5", "code(2)" and the function fix(x) are not commands. */
static int assist_kind(MlSession *s, const char *line, const char **args)
{
    static const struct { const char *word; int kind; } cmds[] = {
        {"ask", A_ASK}, {"code", A_CODE}, {"explain", A_EXPLAIN}, {"fix", A_FIX}, {"why", A_WHY}, {"edit", A_EDIT}, {"change", A_CHANGE}
#ifdef MS_486
        , {"model", A_MODEL}
#endif
    };
    const char *p = line, *q;
    int i;

    while (*p == ' ' || *p == '\t') {
        ++p;
    }
#ifdef MS_486
    if (strchr("acefwm", *p) == NULL || *p == '\0') {
#else
    if (strchr("acefw", *p) == NULL || *p == '\0') {
#endif
        return 0;
    }
    for (i = 0; i < (int)(sizeof(cmds) / sizeof(cmds[0])); ++i) {
        if (!word_is(p, cmds[i].word)) {
            continue;
        }
        q = p + strlen(cmds[i].word);
        while (*q == ' ' || *q == '\t') {
            ++q;
        }
        if (*q == '(' || *q == '=' || name_is_variable(s, p)) {
            return 0;
        }
        if (cmds[i].kind == A_FIX && !(isalpha((unsigned char)*q) || *q == '_')) {
            return 0;                                       /* fix(2.5) and fix 2.5 stay the rounding function */
        }
        *args = q;
        return cmds[i].kind;
    }
    return 0;
}

/* Copies text to out without surrounding spaces and without one pair of quotes around it. */
static void ask_text(const char *text, char *out, size_t max)
{
    size_t n = strlen(text);

    while (n > 0 && (text[n - 1] == ' ' || text[n - 1] == '\t' || text[n - 1] == '\r' || text[n - 1] == '\n')) {
        --n;
    }
    if (n >= 2 && (text[0] == '"' || text[0] == '\'') && text[n - 1] == text[0]) {
        ++text;
        n -= 2;
    }
    if (n >= max) {
        n = max - 1;
    }
    memcpy(out, text, n);
    out[n] = '\0';
}

/* Reads a script into out (at most max-1 characters, cut after a whole line); returns 0 if there is no such file, else 1 + 1 if it was cut. */
static int ask_read_file(const char *name, char *out, unsigned max)
{
    char path[64];
    FILE *f;
    unsigned n = 0, last_line_end = 0;
    int c, cut = 0;

    file_name_for(name, path);
    f = fopen(path, "r");
    if (f == NULL) {
        return 0;
    }
    while ((c = fgetc(f)) != EOF) {
        if (n + 1 >= max) {
            cut = 1;
            n = last_line_end;
            break;
        }
        out[n++] = (char)c;
        if (c == '\n') {
            last_line_end = n;
        }
    }
    fclose(f);
    out[n] = '\0';
    return 1 + cut;
}

/* The lines of the script around the error, numbered, else the line typed. */
static void why_context(MlSession *s, char *out, unsigned max)
{
    char path[64], line[320];
    FILE *f;
    int no = 0;
    unsigned n = 0;

    out[0] = '\0';
    if (s->why_file[0] != '\0' && s->why_line > 0) {
        file_name_for(s->why_file, path);
        f = fopen(path, "r");
        if (f != NULL) {
            while (fgets(line, sizeof(line), f) != NULL) {
                ++no;
                if (no >= s->why_line - 8 && no <= s->why_line + 4 && n + strlen(line) + 8 < max) {
                    n += (unsigned)sprintf(out + n, "%d: %s", no, line);
                    if (strchr(line, '\n') == NULL) {
                        out[n++] = '\n';
                        out[n] = '\0';
                    }
                }
            }
            fclose(f);
        }
    }
    if (n == 0) {
        strcpy(out, s->last_line);
        strcat(out, "\n");
    }
}

#ifdef MS_486
#define MODEL_FILE "msmodel.cfg"

/* Where the choice is kept: the file the MODELCFG variable names (the PS/1's MENU sets it to one file for all the programs, XD91), else MSMODEL.CFG here. */
static const char *model_path(void)
{
    const char *p = getenv("MODELCFG");

    return p != NULL && p[0] != '\0' ? p : MODEL_FILE;
}
#define MODEL_MAX 60

void ml_set_pick_hook(MlSession *s, ml_pick_fn fn, void *ctx)
{
    s->pick_hook = fn;
    s->pick_ctx = ctx;
}

/* The model the user chose, remembered in MSMODEL.CFG ("model=name") in the current folder; read the first time it is needed. */
static const char *model_name(MlSession *s)
{
    if (!s->model_loaded) {
        FILE *f = fopen(model_path(), "r");
        char line[100];

        s->model_loaded = 1;
        s->model[0] = '\0';
        if (f != NULL) {
            if (fgets(line, sizeof(line), f) != NULL && strncmp(line, "model=", 6) == 0) {
                size_t n = strcspn(line + 6, "\r\n ");

                if (n > 0 && n < sizeof(s->model)) {
                    memcpy(s->model, line + 6, n);
                    s->model[n] = '\0';
                }
            }
            fclose(f);
        }
    }
    return s->model;
}

static void model_set(MlSession *s, const char *name)
{
    FILE *f;

    model_name(s);
    strcpy(s->model, name);
    if (name[0] == '\0') {
        remove(model_path());
        return;
    }
    f = fopen(model_path(), "w");
    if (f != NULL) {
        fprintf(f, "model=%s\n", name);
        fclose(f);
    } else {
        emit(s, "(could not save the choice in the model file)\n");
    }
}

/* Asks the PC for the installed models (the server answers a request with the line #list=models): fills `text` with the answer (the server's own model first, then
   every installed one, a name to a line). Returns 0 on success; else the error is set (or 1 if the user stopped it). */
static int model_list(MlSession *s, char *text, size_t max)
{
    FILE *f;
    size_t n = 0;
    int rc, c;

    if (s->ask_hook == NULL) {
        set_err(s, "the assistant is not available in this program");
        return -1;
    }
    remove(ASK_OUT);
    f = fopen(ASK_IN, "w");
    if (f == NULL) {
        set_err(s, "cannot write " ASK_IN " (disk full or write-protected?)");
        return -1;
    }
    fputs("13\nmodels\n#machine=486\n#list=models\n", f);
    if (fclose(f) != 0) {
        remove(ASK_IN);
        set_err(s, "cannot write " ASK_IN " (disk full?)");
        return -1;
    }
    emit(s, "(asking the PC which models it has: this takes a while; " STOP_KEY " stops)\n");
    rc = s->ask_hook(s->ask_ctx, 13);
    remove(ASK_IN);
    f = fopen(ASK_OUT, "r");
    if (f != NULL) {
        while (n + 1 < max && (c = fgetc(f)) != EOF) {
            if (c != '\r') {
                text[n++] = (char)c;
            }
        }
        fclose(f);
        remove(ASK_OUT);
    }
    text[n] = '\0';
    if (rc == 2) {
        emit(s, "(stopped)\n");
        return 1;
    }
    if (rc != 0 || text[0] == '\0') {
        while (n > 0 && (text[n - 1] == '\n' || text[n - 1] == ' ')) {
            text[--n] = '\0';
        }
        set_err(s, "%s", text[0] != '\0' ? text : "the assistant did not answer");
        return -1;
    }
    return 0;
}

/* `model` with no name: the list of the models the PC has, to choose from (a front end with a picker) or to read (line mode). */
static int model_choose(MlSession *s)
{
    char *text = (char *)malloc(3000);
    const char *names[MODEL_MAX + 1];
    char shown[MODEL_MAX + 1][80];
    const char *items[MODEL_MAX + 1];
    char *p;
    int n = 0, i, current = -1, rc, pc_default = -1;
    char line[160];

    if (text == NULL) {
        set_err(s, "out of memory");
        return ML_ERROR;
    }
    rc = model_list(s, text, 3000);
    if (rc != 0) {
        free(text);
        return rc < 0 ? ML_ERROR : ML_OK;
    }
    p = text;
    while (*p != '\0' && n < MODEL_MAX + 1) {
        char *eol = strchr(p, '\n');

        if (eol != NULL) {
            *eol = '\0';
        }
        if (*p != '\0' && strlen(p) < 64) {
            names[n++] = p;
        }
        p = eol != NULL ? eol + 1 : p + strlen(p);
    }
    if (n < 2) {
        free(text);
        set_err(s, "the PC has no models to choose from");
        return ML_ERROR;
    }
    /* names[0] is the PC's own choice; names[1..] the installed models */
    for (i = 1; i < n; ++i) {
        if (strcmp(names[i], names[0]) == 0) {
            pc_default = i - 1;
        }
        if (strcmp(names[i], model_name(s)) == 0) {
            current = i - 1;
        }
        sprintf(shown[i - 1], "%s%s", names[i], i - 1 == pc_default ? "   (the PC's own)" : "");
        items[i - 1] = shown[i - 1];
    }
    if (model_name(s)[0] == '\0') {
        current = pc_default;
    }
    if (s->pick_hook != NULL) {
        i = s->pick_hook(s->pick_ctx, "Choose the model for the assistant", items, n - 1, current);
        if (i >= 0) {
            model_set(s, names[i + 1]);
            sprintf(line, "model: %s\n", names[i + 1]);
            emit(s, line);
        } else {
            emit(s, "(the model is unchanged)\n");
        }
    } else {
        sprintf(line, "models on the PC (* is the one in use; the PC's own is %s):\n", names[0]);
        emit(s, line);
        for (i = 1; i < n; ++i) {
            sprintf(line, "  %c %s\n", i - 1 == current ? '*' : ' ', names[i]);
            emit(s, line);
        }
        emit(s, "choose one with: model <name>     the PC's own: model default\n");
    }
    free(text);
    return ML_OK;
}

/* model            choose from the PC's list      model name   use that model      model default   the PC's own */
static int run_model(MlSession *s, const char *args)
{
    char arg[80], line[200];
    size_t i;

    ask_text(args, arg, sizeof(arg));
    if (arg[0] == '\0') {
        return model_choose(s);
    }
    if (strcmp(arg, "default") == 0 || strcmp(arg, "-") == 0) {
        model_set(s, "");
        emit(s, "model: the PC's own\n");
        return ML_OK;
    }
    for (i = 0; arg[i] != '\0'; ++i) {
        if ((unsigned char)arg[i] <= ' ' || (unsigned char)arg[i] >= 0x7F) {
            set_err(s, "a model name has no spaces: model qwen3:14b");
            return ML_ERROR;
        }
    }
    if (i >= sizeof(s->model)) {
        set_err(s, "that model name is too long");
        return ML_ERROR;
    }
    model_set(s, arg);
    sprintf(line, "model: %s (if the PC does not have it, the next question says so)\n", arg);
    emit(s, line);
    return ML_OK;
}
#endif

#ifdef MS_486
/* "x:100x1,A:100x2": the size of each variable of the workspace, in the order they were made, as far as `max` allows. */
static void variable_sizes(MlSession *s, char *out, size_t max)
{
    int i;
    size_t n = 0;
    char one[NAMELEN + 24];

    out[0] = '\0';
    for (i = 0; i < s->glob.nvar; ++i) {
        sprintf(one, "%s%s:%dx%d", n > 0 ? "," : "", s->glob.var[i].name, s->glob.var[i].m->r, s->glob.var[i].m->c);
        if (n + strlen(one) + 1 > max) {
            break;
        }
        strcpy(out + n, one);
        n += strlen(one);
    }
}
#endif

static int run_assist(MlSession *s, int kind, const char *args)
{
    static const int modes[] = {0, 13, 14, 15, 16, 17, 0, 18};
    char instr[ASK_ILEN + 2], name[40];
#ifdef MS_486
    int instr_is_last_error = 0;
#endif
    char *context, *answer;
    size_t n;
    int rc, got;
    const char *q;
    FILE *f;

#ifdef MS_486
    if (kind == A_MODEL) {
        return run_model(s, args);
    }
#endif
    if (kind == A_EDIT) {
        q = args;
        n = 0;
        while ((isalnum((unsigned char)*q) || *q == '_' || *q == '.') && n < sizeof(name) - 1) {
            name[n++] = *q++;
        }
        name[n] = '\0';
        if (n == 0) {
            set_err(s, "edit needs a script name: edit myscript");
            return ML_ERROR;
        }
        if (s->edit_hook == NULL) {
            set_err(s, "the editor needs the full screen (it is not available in line mode)");
            return ML_ERROR;
        }
        s->edit_hook(s->edit_ctx, name);
        return ML_OK;
    }
    if (s->ask_hook == NULL) {
        set_err(s, "the assistant is not available in this program");
        return ML_ERROR;
    }
    instr[0] = '\0';
    name[0] = '\0';
    if (kind == A_EXPLAIN || kind == A_FIX || kind == A_CHANGE) {
        q = args;
        n = 0;
        while ((isalnum((unsigned char)*q) || *q == '_' || *q == '.') && n < sizeof(name) - 1) {
            name[n++] = *q++;
        }
        name[n] = '\0';
        if (n == 0) {
            set_err(s, kind == A_FIX ? "fix needs a script name: fix myscript \"what is wrong\"" :
                       kind == A_CHANGE ? "change needs a script name: change myscript \"what to change\"" : "explain needs a script name: explain myscript");
            return ML_ERROR;
        }
        while (*q == ' ' || *q == '\t') {
            ++q;
        }
        args = q;
    }
    if (kind != A_WHY) {
        if (strlen(args) > ASK_ILEN + 4) {
            set_err(s, "that is too long: %d characters at most", ASK_ILEN);
            return ML_ERROR;
        }
        ask_text(args, instr, sizeof(instr));
        if (instr[0] == '\0' && (kind == A_ASK || kind == A_CODE || kind == A_CHANGE)) {
            set_err(s, kind == A_ASK ? "ask what? Example: ask how do I plot a function" :
                       kind == A_CHANGE ? "change how? Example: change myscript make it print the result" : "code what? Example: code a function that finds primes");
            return ML_ERROR;
        }
        if (strlen(instr) > ASK_ILEN) {
            set_err(s, "that is too long: %d characters at most", ASK_ILEN);
            return ML_ERROR;
        }
#ifdef MS_486
        if (kind == A_FIX && instr[0] == '\0' && s->why_msg[0] != '\0' && s->why_file[0] != '\0') {      /* fix with no words: MathSmith's last error in this script is the message (XD87) */
            char here[64], there[64];

            file_name_for(s->why_file, here);
            file_name_for(name, there);
            if (strcmp(here, there) == 0) {
                strncpy(instr, s->why_msg, ASK_ILEN);
                instr[ASK_ILEN] = '\0';
                instr_is_last_error = 1;
            }
        }
#endif
    } else {
        if (s->why_msg[0] == '\0') {
            set_err(s, "there is no error to explain yet");
            return ML_ERROR;
        }
        strncpy(instr, s->why_msg, ASK_ILEN);
        instr[ASK_ILEN] = '\0';
    }
    context = (char *)malloc(ASK_CONTEXT + 1);
    if (context == NULL) {
        set_err(s, "out of memory");
        return ML_ERROR;
    }
    context[0] = '\0';
    if (kind == A_EXPLAIN || kind == A_FIX || kind == A_CHANGE) {
        got = ask_read_file(name, context, ASK_CONTEXT);
        if (got == 0) {
            free(context);
            set_err(s, "cannot find the file %s", name);
            return ML_ERROR;
        }
        if (got == 2) {
            emit(s, "(the script is long: only its first lines are sent)\n");
        }
    } else if (kind == A_WHY) {
        why_context(s, context, ASK_CONTEXT);
    }
    /* The request goes to a file and its buffer is freed before the helper runs: on the 8086 the helper needs nearly all the memory
       there is (about 230 KB), so MathSmith holds nothing it can do without while the helper works. The answer comes back in a file too. */
    remove(ASK_OUT);
    f = fopen(ASK_IN, "w");
    if (f == NULL) {
        free(context);
        set_err(s, "cannot write " ASK_IN " (disk full or write-protected?)");
        return ML_ERROR;
    }
    fprintf(f, "%d\n%s\n", modes[kind], instr);
#ifdef MS_486
    fputs("#machine=486\n", f);                              /* the server describes this machine to the model (XD87) */
    if (kind == A_FIX && instr_is_last_error) {                   /* the sizes of the variables at the error: a size error cannot be mended without them (XD90) */
        char sizes[420];

        variable_sizes(s, sizes, sizeof(sizes));
        if (sizes[0] != '\0') {
            fprintf(f, "#sizes=%s\n", sizes);
        }
    }
    if (model_name(s)[0] != '\0') {
        fprintf(f, "#model=%s\n", model_name(s));            /* the model the user chose (XD89) */
    }
#endif
    fputs(context, f);
    got = fclose(f) != 0;
    free(context);
    if (got) {
        remove(ASK_IN);
        set_err(s, "cannot write " ASK_IN " (disk full?)");
        return ML_ERROR;
    }
    emit(s, "(asking the assistant: this takes a while; " STOP_KEY " stops)\n");
    rc = s->ask_hook(s->ask_ctx, modes[kind]);
    remove(ASK_IN);
    answer = (char *)malloc(ASK_ANSWER + 1);
    if (answer == NULL) {
        remove(ASK_OUT);
        set_err(s, "out of memory");
        return ML_ERROR;
    }
    n = 0;
    f = fopen(ASK_OUT, "r");
    if (f != NULL) {
        int c;

        while (n < ASK_ANSWER && (c = fgetc(f)) != EOF) {
            answer[n++] = (char)c;
        }
        fclose(f);
        remove(ASK_OUT);
    }
    answer[n] = '\0';
    if (rc == 2) {
        emit(s, "(stopped)\n");
        free(answer);
        return ML_OK;
    }
    if (rc != 0 || answer[0] == '\0') {
        set_err(s, "%s", answer[0] != '\0' ? answer : "the assistant did not answer");
        free(answer);
        return ML_ERROR;
    }
    n = strlen(answer);
    while (n > 0 && (answer[n - 1] == '\n' || answer[n - 1] == ' ')) {
        answer[--n] = '\0';
    }
    emit(s, answer);
    emit(s, "\n");
    if (kind == A_CODE || kind == A_FIX || kind == A_CHANGE) {
        char note[120];

        f = fopen(ASK_FILE, "w");
        if (f == NULL) {
            emit(s, "(could not save the code to " ASK_FILE ")\n");
        } else {
            fputs(answer, f);
            fputs("\n", f);
            fclose(f);
            sprintf(note, "(saved in " ASK_FILE "; nothing was run%s. To run it: run ask)\n",
                    kind != A_CODE ? ", and the original is unchanged" : "");
            emit(s, note);
        }
    }
    free(answer);
    return ML_OK;
}

static int ml_eval_inner(MlSession *s, const char *line)
{
    char buf[400];
    char *pc;
    StmtList l;
    Prog prog;
    int rc, d, wasblock, kind;
    const char *args = line;

    s->err[0] = '\0';
    s->errline = 0;
    s->errfile[0] = '\0';
    s->evals = 0;
    s->nbind = 0;
    s->bindbase = 0;
    s->depth = 0;
    s->fdepth = 0;
    s->sdepth = 0;
    s->cur = &s->glob;
    mat_aborted = 0;
    kind = s->npend == 0 ? assist_kind(s, line, &args) : 0;
    s->assisting = kind != 0;
    if (kind != 0) {
        rc = run_assist(s, kind, args);
        if (rc == ML_ERROR) {
            report_error(s);
        }
        return rc;
    }
    strncpy(s->last_line, line, sizeof(s->last_line) - 1);
    if (strlen(line) >= sizeof(buf)) {
        set_err(s, "the line is too long");
        report_error(s);
        return ML_ERROR;
    }
    strcpy(buf, line);
    pc = find_comment(buf);
    if (pc != NULL) {
        *pc = '\0';
    }
    wasblock = s->npend > 0;
    if (wasblock) {
        char *t = buf;

        while (*t == ' ' || *t == '\t') {
            ++t;
        }
        if (strcmp(t, "abort") == 0) {
            pending_clear(s);
            emit(s, "(the unfinished block was dropped)\n");
            return ML_OK;
        }
    }
    memset(&l, 0, sizeof(l));
    if (!split_statements(s, buf, &l, wasblock ? s->npend + 1 : 0)) {
        list_free(&l);
        report_error(s);
        return ML_ERROR;
    }
    /* put the pending statements in front */
    if (s->npend > 0) {
        Stmt *all = (Stmt *)malloc((size_t)(s->npend + l.n + 1) * sizeof(Stmt));

        if (all == NULL) {
            list_free(&l);
            pending_clear(s);
            set_err(s, "out of memory");
            report_error(s);
            return ML_ERROR;
        }
        memcpy(all, s->pend, (size_t)s->npend * sizeof(Stmt));
        memcpy(all + s->npend, l.a, (size_t)l.n * sizeof(Stmt));
        free(s->pend);
        free(l.a);
        l.a = all;
        l.n += s->npend;
        l.cap = l.n;
        s->pend = NULL;
        s->npend = s->cappend = 0;
    }
    d = depth_change(l.a, l.n);
    if (d > 0) {                                           /* a block is still open: wait for more lines */
        s->pend = l.a;
        s->npend = l.n;
        s->cappend = l.cap;
        s->pdepth = d;
        return ML_MORE;
    }
    s->pdepth = 0;
    if (d < 0) {
        list_free(&l);
        set_err(s, "end without a matching if, for, while or function");
        report_error(s);
        return ML_ERROR;
    }
    if (l.n == 0) {
        list_free(&l);
        return ML_OK;
    }
    if (!prog_link(s, l.a, l.n, 1, &prog)) {
        list_free(&l);
        report_error(s);
        return ML_ERROR;
    }
    rc = exec_range(s, &prog, 0, prog.n);
    prog_free(&prog);
    list_free(&l);
    if (rc == ML_QUIT) {
        return ML_QUIT;
    }
    if (rc == ML_ERROR) {
        report_error(s);
        return ML_ERROR;
    }
    return ML_OK;
}

/* Runs a line, and shows the plot if the line (or a script it ran) made or changed one. */
int ml_eval(MlSession *s, const char *line)
{
    int rc = ml_eval_inner(s, line);

    if (s->plot.dirty && rc != ML_MORE) {
        if (s->plot.nc == 0) {
            s->plot.dirty = 0;
        } else if (s->plot_hook != NULL) {
            ml_show_plot(s);
        } else {
            char note[100];

            s->plot.dirty = 0;
            sprintf(note, "(plot: %d curve%s, %ld points; this program has no screen for it)\n", s->plot.nc, s->plot.nc == 1 ? "" : "s", s->plot.total);
            emit(s, note);
        }
    }
    return rc;
}
