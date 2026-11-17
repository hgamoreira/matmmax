/* Matrices for MathSmith (M01): see mat.h. */

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "mat.h"

int (*mat_poll_hook)(void) = NULL;
int mat_aborted = 0;

int mat_check_abort(void)
{
    if (mat_poll_hook != NULL && mat_poll_hook()) {
        mat_aborted = 1;
        return 1;
    }
    return 0;
}

/* A matrix is one block: the header, then the numbers. Blocks for single numbers are kept in a small pool, so that the many 1x1
   temporaries of a calculation cost no allocation (an allocation is the slowest thing the 8086 does here). */
#define HEAD ((sizeof(Mat) + 7) / 8 * 8)
#define POOL_MAX 48

static Mat *pool[POOL_MAX];
static int npool;

Mat *mat_new(int r, int c)
{
    Mat *m;
    unsigned long n;

    if (r < 0 || c < 0 || (long)r * c > MAT_MAX_ELEMS) {
        return NULL;
    }
    n = (unsigned long)r * (unsigned long)c;
    if (n == 1 && npool > 0) {
        m = pool[--npool];
        m->r = r;
        m->c = c;
        m->kind = MAT_NUM;
        m->d[0] = 0.0;
        return m;
    }
    m = (Mat *)malloc((size_t)(HEAD + n * sizeof(double)));
    if (m == NULL) {
        return NULL;
    }
    m->r = r;
    m->c = c;
    m->kind = MAT_NUM;
    m->d = NULL;
    if (n > 0) {
        m->d = (double *)((char *)m + HEAD);
        memset(m->d, 0, (size_t)(n * sizeof(double)));
    }
    return m;
}

void mat_free(Mat *m)
{
    if (m == NULL) {
        return;
    }
    if (m->r == 1 && m->c == 1 && npool < POOL_MAX) {
        pool[npool++] = m;
        return;
    }
    free(m);
}

Mat *mat_copy(const Mat *a)
{
    Mat *m = mat_new(a->r, a->c);

    if (m != NULL) {
        m->kind = a->kind;
        if (m->d != NULL) {
            memcpy(m->d, a->d, (size_t)((unsigned long)a->r * (unsigned long)a->c * sizeof(double)));
        }
    }
    return m;
}

Mat *mat_scalar(double x)
{
    Mat *m = mat_new(1, 1);

    if (m != NULL) {
        m->d[0] = x;
    }
    return m;
}

Mat *mat_mul(const Mat *a, const Mat *b)
{
    Mat *m;
    int i, j, k;

    if (a->c != b->r) {
        return NULL;
    }
    m = mat_new(a->r, b->c);
    if (m == NULL) {
        return NULL;
    }
    for (i = 0; i < a->r; ++i) {
        if (mat_check_abort()) {
            mat_free(m);
            return NULL;
        }
        for (k = 0; k < a->c; ++k) {
            double x = MAT_AT(a, i, k);

            if (x != 0.0) {
                for (j = 0; j < b->c; ++j) {
                    MAT_AT(m, i, j) += x * MAT_AT(b, k, j);
                }
            }
        }
    }
    return m;
}

Mat *mat_transpose(const Mat *a)
{
    Mat *m = mat_new(a->c, a->r);
    int i, j;

    if (m == NULL) {
        return NULL;
    }
    m->kind = a->kind;
    for (i = 0; i < a->r; ++i) {
        for (j = 0; j < a->c; ++j) {
            MAT_AT(m, j, i) = MAT_AT(a, i, j);
        }
    }
    return m;
}

static double max_abs(const Mat *a)
{
    long i, n = MAT_N(a);
    double big = 0.0;

    for (i = 0; i < n; ++i) {
        if (fabs(a->d[i]) > big) {
            big = fabs(a->d[i]);
        }
    }
    return big;
}

/* LU with partial pivoting, in place. perm gets the row order; *sign is +1 or -1. Returns 0, or 1 if a pivot is (nearly) zero. */
static int lu_decompose(Mat *a, int *perm, double *sign)
{
    int n = a->r, i, j, k, p;
    double tol = 1e-13 * max_abs(a);
    int singular = 0;

    *sign = 1.0;
    for (i = 0; i < n; ++i) {
        perm[i] = i;
    }
    for (k = 0; k < n; ++k) {
        double best = fabs(MAT_AT(a, k, k));

        if (mat_check_abort()) {
            return 2;
        }
        p = k;
        for (i = k + 1; i < n; ++i) {
            if (fabs(MAT_AT(a, i, k)) > best) {
                best = fabs(MAT_AT(a, i, k));
                p = i;
            }
        }
        if (best <= tol) {
            singular = 1;
        }
        if (p != k) {
            for (j = 0; j < n; ++j) {
                double t = MAT_AT(a, k, j);

                MAT_AT(a, k, j) = MAT_AT(a, p, j);
                MAT_AT(a, p, j) = t;
            }
            i = perm[k];
            perm[k] = perm[p];
            perm[p] = i;
            *sign = -*sign;
        }
        if (MAT_AT(a, k, k) != 0.0) {
            for (i = k + 1; i < n; ++i) {
                double f = MAT_AT(a, i, k) / MAT_AT(a, k, k);

                MAT_AT(a, i, k) = f;
                for (j = k + 1; j < n; ++j) {
                    MAT_AT(a, i, j) -= f * MAT_AT(a, k, j);
                }
            }
        }
    }
    return singular;
}

double mat_det(const Mat *a, int *status)
{
    Mat *w;
    int *perm;
    double sign, det;
    int i;

    *status = MAT_OK;
    if (a->r != a->c) {
        *status = MAT_DIMS;
        return 0.0;
    }
    if (a->r == 0) {
        return 1.0;
    }
    w = mat_copy(a);
    perm = (int *)malloc((size_t)a->r * sizeof(int));
    if (w == NULL || perm == NULL) {
        mat_free(w);
        free(perm);
        *status = MAT_MEMORY;
        return 0.0;
    }
    if (lu_decompose(w, perm, &sign) == 2) {
        mat_free(w);
        free(perm);
        *status = MAT_MEMORY;
        return 0.0;
    }
    det = sign;
    for (i = 0; i < a->r; ++i) {
        det *= MAT_AT(w, i, i);
    }
    mat_free(w);
    free(perm);
    return det;
}

/* Solves a*x = b for square a by LU. */
static Mat *solve_square(const Mat *a, const Mat *b, int *status)
{
    Mat *w = mat_copy(a), *x = mat_new(a->r, b->c);
    int *perm = (int *)malloc((size_t)a->r * sizeof(int));
    int n = a->r, i, j, k;
    double sign;

    if (w == NULL || x == NULL || perm == NULL) {
        mat_free(w);
        mat_free(x);
        free(perm);
        *status = MAT_MEMORY;
        return NULL;
    }
    if (lu_decompose(w, perm, &sign)) {
        mat_free(w);
        mat_free(x);
        free(perm);
        *status = mat_aborted ? MAT_MEMORY : MAT_SINGULAR;
        return NULL;
    }
    for (j = 0; j < b->c; ++j) {
        if (mat_check_abort()) {
            mat_free(w);
            mat_free(x);
            free(perm);
            *status = MAT_MEMORY;
            return NULL;
        }
        for (i = 0; i < n; ++i) {                     /* forward: L*y = P*b */
            double s = MAT_AT(b, perm[i], j);

            for (k = 0; k < i; ++k) {
                s -= MAT_AT(w, i, k) * MAT_AT(x, k, j);
            }
            MAT_AT(x, i, j) = s;
        }
        for (i = n - 1; i >= 0; --i) {                /* backward: U*x = y */
            double s = MAT_AT(x, i, j);

            for (k = i + 1; k < n; ++k) {
                s -= MAT_AT(w, i, k) * MAT_AT(x, k, j);
            }
            MAT_AT(x, i, j) = s / MAT_AT(w, i, i);
        }
    }
    mat_free(w);
    free(perm);
    *status = MAT_OK;
    return x;
}

/* Least squares for m > n by Householder QR. */
static Mat *solve_least_squares(const Mat *a, const Mat *b, int *status)
{
    Mat *r = mat_copy(a), *q = mat_copy(b), *x = mat_new(a->c, b->c);
    int m = a->r, n = a->c, i, j, k;
    double tol;

    if (r == NULL || q == NULL || x == NULL) {
        mat_free(r);
        mat_free(q);
        mat_free(x);
        *status = MAT_MEMORY;
        return NULL;
    }
    tol = 1e-13 * max_abs(a);
    for (k = 0; k < n; ++k) {
        double norm = 0.0, alpha, vnorm2 = 0.0, dot;

        if (mat_check_abort()) {
            mat_free(r);
            mat_free(q);
            mat_free(x);
            *status = MAT_MEMORY;
            return NULL;
        }

        for (i = k; i < m; ++i) {
            norm += MAT_AT(r, i, k) * MAT_AT(r, i, k);
        }
        norm = sqrt(norm);
        if (norm <= tol) {
            mat_free(r);
            mat_free(q);
            mat_free(x);
            *status = MAT_SINGULAR;
            return NULL;
        }
        alpha = MAT_AT(r, k, k) > 0.0 ? -norm : norm;
        MAT_AT(r, k, k) -= alpha;                      /* column k below the diagonal now holds v (v[k] = r[k][k] - alpha) */
        for (i = k; i < m; ++i) {
            vnorm2 += MAT_AT(r, i, k) * MAT_AT(r, i, k);
        }
        if (vnorm2 > 0.0) {
            for (j = k + 1; j < n; ++j) {
                dot = 0.0;
                for (i = k; i < m; ++i) {
                    dot += MAT_AT(r, i, k) * MAT_AT(r, i, j);
                }
                dot = 2.0 * dot / vnorm2;
                for (i = k; i < m; ++i) {
                    MAT_AT(r, i, j) -= dot * MAT_AT(r, i, k);
                }
            }
            for (j = 0; j < b->c; ++j) {
                dot = 0.0;
                for (i = k; i < m; ++i) {
                    dot += MAT_AT(r, i, k) * MAT_AT(q, i, j);
                }
                dot = 2.0 * dot / vnorm2;
                for (i = k; i < m; ++i) {
                    MAT_AT(q, i, j) -= dot * MAT_AT(r, i, k);
                }
            }
        }
        MAT_AT(r, k, k) = alpha;                       /* the diagonal of R */
        for (i = k + 1; i < m; ++i) {
            MAT_AT(r, i, k) = 0.0;
        }
    }
    for (j = 0; j < b->c; ++j) {
        for (i = n - 1; i >= 0; --i) {
            double s = MAT_AT(q, i, j);

            for (k = i + 1; k < n; ++k) {
                s -= MAT_AT(r, i, k) * MAT_AT(x, k, j);
            }
            MAT_AT(x, i, j) = s / MAT_AT(r, i, i);
        }
    }
    mat_free(r);
    mat_free(q);
    *status = MAT_OK;
    return x;
}

Mat *mat_solve(const Mat *a, const Mat *b, int *status)
{
    *status = MAT_OK;
    if (a->r != b->r) {
        *status = MAT_DIMS;
        return NULL;
    }
    if (a->r == a->c) {
        return solve_square(a, b, status);
    }
    if (a->r > a->c) {
        return solve_least_squares(a, b, status);
    }
    *status = MAT_UNDER;
    return NULL;
}

Mat *mat_inverse(const Mat *a, int *status)
{
    Mat *id;
    Mat *x;
    int i;

    if (a->r != a->c) {
        *status = MAT_DIMS;
        return NULL;
    }
    id = mat_new(a->r, a->r);
    if (id == NULL) {
        *status = MAT_MEMORY;
        return NULL;
    }
    for (i = 0; i < a->r; ++i) {
        MAT_AT(id, i, i) = 1.0;
    }
    x = solve_square(a, id, status);
    mat_free(id);
    return x;
}

/* Rank by Gaussian elimination with full pivoting; a pivot below 1e-11 of the largest entry counts as zero. */
int mat_rank(const Mat *a)
{
    Mat *w = mat_copy(a);
    int rank = 0, m = a->r, n = a->c, k, i, j, pi, pj;
    double tol = 1e-11 * max_abs(a);

    if (w == NULL) {
        return -1;
    }
    for (k = 0; k < m && k < n; ++k) {
        double best = 0.0;

        pi = pj = -1;
        for (i = k; i < m; ++i) {
            for (j = k; j < n; ++j) {
                if (fabs(MAT_AT(w, i, j)) > best) {
                    best = fabs(MAT_AT(w, i, j));
                    pi = i;
                    pj = j;
                }
            }
        }
        if (pi < 0 || best <= tol) {
            break;
        }
        for (j = 0; j < n; ++j) {
            double t = MAT_AT(w, k, j);

            MAT_AT(w, k, j) = MAT_AT(w, pi, j);
            MAT_AT(w, pi, j) = t;
        }
        for (i = 0; i < m; ++i) {
            double t = MAT_AT(w, i, k);

            MAT_AT(w, i, k) = MAT_AT(w, i, pj);
            MAT_AT(w, i, pj) = t;
        }
        for (i = k + 1; i < m; ++i) {
            double f = MAT_AT(w, i, k) / MAT_AT(w, k, k);

            for (j = k; j < n; ++j) {
                MAT_AT(w, i, j) -= f * MAT_AT(w, k, j);
            }
        }
        ++rank;
    }
    mat_free(w);
    return rank;
}
