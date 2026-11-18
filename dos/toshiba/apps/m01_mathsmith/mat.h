/* Matrices of doubles for MathSmith (M01): row-major, at most MAT_MAX_ELEMS elements (so one matrix stays below 64 KB for the 16-bit large
   model). Plain C89, no I/O: the same file is built for DOS and for the Linux unit tests. */
#ifndef MS_MAT_H
#define MS_MAT_H

#ifdef MS_486
#define MAT_MAX_ELEMS 250000    /* the 486 (M11): 32-bit, 32 MB: a matrix of up to 500 x 500 */
#else
#define MAT_MAX_ELEMS 4096
#endif

#define MAT_NUM 0
#define MAT_CHAR 1             /* a string: the character codes as doubles */
#define MAT_BOOL 2             /* the result of a comparison: 0 or 1; used as an index it is a mask */

typedef struct {
    int r, c;
    double *d;                 /* r*c values, row by row; NULL when r*c is 0 */
    int kind;                  /* MAT_NUM, MAT_CHAR or MAT_BOOL */
} Mat;

#define MAT_AT(m, i, j) ((m)->d[(long)(i) * (m)->c + (j)])
#define MAT_N(m) ((long)(m)->r * (m)->c)

/* status codes of the solvers */
#define MAT_OK 0
#define MAT_DIMS 1             /* dimensions do not fit */
#define MAT_SINGULAR 2
#define MAT_MEMORY 3
#define MAT_UNDER 4            /* fewer equations than unknowns: not supported yet */

/* A long calculation calls this hook now and then; if it returns non-zero the calculation is abandoned (the function returns NULL and
   mat_aborted is set). The DOS front end uses it for the Esc key. */
extern int (*mat_poll_hook)(void);
extern int mat_aborted;
int mat_check_abort(void);                     /* 1 if the hook asked to stop */

Mat *mat_new(int r, int c);                    /* zeros; NULL if too big or out of memory */
void mat_free(Mat *m);
Mat *mat_copy(const Mat *m);
Mat *mat_scalar(double x);
Mat *mat_mul(const Mat *a, const Mat *b);      /* NULL: inner dimensions differ, or memory */
Mat *mat_transpose(const Mat *a);
Mat *mat_inverse(const Mat *a, int *status);
double mat_det(const Mat *a, int *status);
Mat *mat_solve(const Mat *a, const Mat *b, int *status);    /* a\b: square (LU) or more equations than unknowns (least squares, QR) */
int mat_rank(const Mat *a);

#endif
