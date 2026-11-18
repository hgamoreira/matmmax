/* Eigenvalues and singular values for MathSmith (M05): see linalg.h. The reductions and iterations are the classical ones (EISPACK, as in
   Numerical Recipes): Householder + QL for symmetric matrices, Hessenberg + shifted QR for general ones, one-sided Jacobi for the SVD. */

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "linalg.h"

#define ITER_MAX 60

static double sq(double x) { return x * x; }

static double pythag(double a, double b)
{
    double aa = fabs(a), bb = fabs(b);

    if (aa > bb) {
        return aa * sqrt(1.0 + sq(bb / aa));
    }
    return bb == 0.0 ? 0.0 : bb * sqrt(1.0 + sq(aa / bb));
}

static double sign_of(double a, double b)
{
    return b >= 0.0 ? fabs(a) : -fabs(a);
}

int la_is_symmetric(const Mat *a)
{
    int i, j;
    double big = 0.0, diff = 0.0;

    if (a->r != a->c) {
        return 0;
    }
    for (i = 0; i < a->r; ++i) {
        for (j = 0; j < a->c; ++j) {
            if (fabs(MAT_AT(a, i, j)) > big) {
                big = fabs(MAT_AT(a, i, j));
            }
            if (fabs(MAT_AT(a, i, j) - MAT_AT(a, j, i)) > diff) {
                diff = fabs(MAT_AT(a, i, j) - MAT_AT(a, j, i));
            }
        }
    }
    return diff <= 1e-12 * big;
}

/* --- symmetric matrices: Householder reduction to tridiagonal form, then QL iteration with implicit shifts --- */

#define Z(i, j) z[((i) - 1) * n + ((j) - 1)]           /* 1-based, as in the book */

static void tred2(double *z, int n, double *d, double *e)
{
    int l, k, j, i;
    double scale, hh, h, g, f;

    for (i = n; i >= 2; i--) {
        l = i - 1;
        h = scale = 0.0;
        if (l > 1) {
            for (k = 1; k <= l; k++) {
                scale += fabs(Z(i, k));
            }
            if (scale == 0.0) {
                e[i] = Z(i, l);
            } else {
                for (k = 1; k <= l; k++) {
                    Z(i, k) /= scale;
                    h += Z(i, k) * Z(i, k);
                }
                f = Z(i, l);
                g = f >= 0.0 ? -sqrt(h) : sqrt(h);
                e[i] = scale * g;
                h -= f * g;
                Z(i, l) = f - g;
                f = 0.0;
                for (j = 1; j <= l; j++) {
                    Z(j, i) = Z(i, j) / h;
                    g = 0.0;
                    for (k = 1; k <= j; k++) {
                        g += Z(j, k) * Z(i, k);
                    }
                    for (k = j + 1; k <= l; k++) {
                        g += Z(k, j) * Z(i, k);
                    }
                    e[j] = g / h;
                    f += e[j] * Z(i, j);
                }
                hh = f / (h + h);
                for (j = 1; j <= l; j++) {
                    f = Z(i, j);
                    e[j] = g = e[j] - hh * f;
                    for (k = 1; k <= j; k++) {
                        Z(j, k) -= f * e[k] + g * Z(i, k);
                    }
                }
            }
        } else {
            e[i] = Z(i, l);
        }
        d[i] = h;
    }
    d[1] = 0.0;
    e[1] = 0.0;
    for (i = 1; i <= n; i++) {
        l = i - 1;
        if (d[i] != 0.0) {
            for (j = 1; j <= l; j++) {
                g = 0.0;
                for (k = 1; k <= l; k++) {
                    g += Z(i, k) * Z(k, j);
                }
                for (k = 1; k <= l; k++) {
                    Z(k, j) -= g * Z(k, i);
                }
            }
        }
        d[i] = Z(i, i);
        Z(i, i) = 1.0;
        for (j = 1; j <= l; j++) {
            Z(j, i) = Z(i, j) = 0.0;
        }
    }
}

static int tqli(double *d, double *e, int n, double *z)
{
    int m, l, iter, i, k;
    double s, r, p, g, f, dd, c, b;

    for (i = 2; i <= n; i++) {
        e[i - 1] = e[i];
    }
    e[n] = 0.0;
    for (l = 1; l <= n; l++) {
        iter = 0;
        do {
            for (m = l; m <= n - 1; m++) {
                dd = fabs(d[m]) + fabs(d[m + 1]);
                if (fabs(e[m]) + dd == dd) {
                    break;
                }
            }
            if (m != l) {
                if (iter++ == ITER_MAX) {
                    return LA_NOCONV;
                }
                g = (d[l + 1] - d[l]) / (2.0 * e[l]);
                r = pythag(g, 1.0);
                g = d[m] - d[l] + e[l] / (g + sign_of(r, g));
                s = c = 1.0;
                p = 0.0;
                for (i = m - 1; i >= l; i--) {
                    f = s * e[i];
                    b = c * e[i];
                    e[i + 1] = r = pythag(f, g);
                    if (r == 0.0) {
                        d[i + 1] -= p;
                        e[m] = 0.0;
                        break;
                    }
                    s = f / r;
                    c = g / r;
                    g = d[i + 1] - p;
                    r = (d[i] - g) * s + 2.0 * c * b;
                    d[i + 1] = g + (p = s * r);
                    g = c * r - b;
                    for (k = 1; k <= n; k++) {
                        f = Z(k, i + 1);
                        Z(k, i + 1) = s * Z(k, i) + c * f;
                        Z(k, i) = c * Z(k, i) - s * f;
                    }
                }
                if (r == 0.0 && i >= l) {
                    continue;
                }
                d[l] -= p;
                e[l] = g;
                e[m] = 0.0;
            }
        } while (m != l);
    }
    return LA_OK;
}

int la_symmetric(const Mat *a, double *w, Mat *v)
{
    int n = a->r, i, j, k, rc;
    double *z, *d, *e;

    z = (double *)malloc((size_t)n * n * sizeof(double));
    d = (double *)malloc((size_t)(n + 2) * sizeof(double));
    e = (double *)malloc((size_t)(n + 2) * sizeof(double));
    if (z == NULL || d == NULL || e == NULL) {
        free(z);
        free(d);
        free(e);
        return LA_MEMORY;
    }
    for (i = 0; i < n; ++i) {                           /* the lower triangle is used: symmetrise */
        for (j = 0; j < n; ++j) {
            z[i * n + j] = 0.5 * (MAT_AT(a, i, j) + MAT_AT(a, j, i));
        }
    }
    if (n == 1) {
        d[1] = z[0];
        z[0] = 1.0;
        rc = LA_OK;
    } else {
        tred2(z, n, d, e);
        rc = tqli(d, e, n, z);
    }
    if (rc == LA_OK) {                                  /* ascending order, with the vectors */
        for (i = 1; i <= n; ++i) {
            k = i;
            for (j = i + 1; j <= n; ++j) {
                if (d[j] < d[k]) {
                    k = j;
                }
            }
            if (k != i) {
                double t = d[i];

                d[i] = d[k];
                d[k] = t;
                for (j = 1; j <= n; ++j) {
                    t = Z(j, i);
                    Z(j, i) = Z(j, k);
                    Z(j, k) = t;
                }
            }
        }
        for (i = 0; i < n; ++i) {
            w[i] = d[i + 1];
        }
        if (v != NULL) {
            for (i = 0; i < n; ++i) {
                for (j = 0; j < n; ++j) {
                    MAT_AT(v, i, j) = z[i * n + j];
                }
            }
        }
    }
    free(z);
    free(d);
    free(e);
    return rc;
}

#undef Z

/* --- general matrices: reduction to Hessenberg form, then the shifted QR algorithm (eigenvalues) --- */

#define H(i, j) h[((i) - 1) * n + ((j) - 1)]

static void elmhes(double *h, int n)
{
    int m, j, i;
    double y, x, t;

    for (m = 2; m < n; m++) {
        x = 0.0;
        i = m;
        for (j = m; j <= n; j++) {
            if (fabs(H(j, m - 1)) > fabs(x)) {
                x = H(j, m - 1);
                i = j;
            }
        }
        if (i != m) {
            for (j = m - 1; j <= n; j++) {
                t = H(i, j);
                H(i, j) = H(m, j);
                H(m, j) = t;
            }
            for (j = 1; j <= n; j++) {
                t = H(j, i);
                H(j, i) = H(j, m);
                H(j, m) = t;
            }
        }
        if (x != 0.0) {
            for (i = m + 1; i <= n; i++) {
                if ((y = H(i, m - 1)) != 0.0) {
                    y /= x;
                    H(i, m - 1) = y;
                    for (j = m; j <= n; j++) {
                        H(i, j) -= y * H(m, j);
                    }
                    for (j = 1; j <= n; j++) {
                        H(j, m) += y * H(j, i);
                    }
                }
            }
        }
    }
    for (i = 3; i <= n; i++) {                           /* the multipliers are not part of the Hessenberg matrix */
        for (j = 1; j <= i - 2; j++) {
            H(i, j) = 0.0;
        }
    }
}

/* The stores of eigenvalues are kept out of hqr's body: compiled by the 16-bit Watcom compiler with -fpc they were lost there, depending on
   the code around them (found in the emulator: the imaginary parts came out 0; see XD72). In a function of their own they are safe. */
static void put_eig(double *wr, double *wi, int k, double re, double im)
{
    wr[k] = re;
    wi[k] = im;
}

static int hqr(double *h, int n, double *wr, double *wi)
{
    int nn, m, l, k, j, its, i, mmin;
    double z = 0.0, y, x, w, v, u, t, s, r = 0.0, q = 0.0, p = 0.0, anorm = 0.0;

    for (i = 1; i <= n; i++) {
        for (j = (i - 1 > 1 ? i - 1 : 1); j <= n; j++) {
            anorm += fabs(H(i, j));
        }
    }
    nn = n;
    t = 0.0;
    while (nn >= 1) {
        its = 0;
        do {
            for (l = nn; l >= 2; l--) {
                s = fabs(H(l - 1, l - 1)) + fabs(H(l, l));
                if (s == 0.0) {
                    s = anorm;
                }
                if (fabs(H(l, l - 1)) + s == s) {
                    H(l, l - 1) = 0.0;
                    break;
                }
            }
            x = H(nn, nn);
            if (l == nn) {
                put_eig(wr, wi, nn, x + t, 0.0);
                nn--;
            } else {
                y = H(nn - 1, nn - 1);
                w = H(nn, nn - 1) * H(nn - 1, nn);
                if (l == nn - 1) {
                    p = 0.5 * (y - x);
                    q = p * p + w;
                    z = sqrt(fabs(q));
                    x += t;
                    if (q >= 0.0) {
                        z = p + sign_of(z, p);
                        put_eig(wr, wi, nn - 1, x + z, 0.0);
                        put_eig(wr, wi, nn, z != 0.0 ? x - w / z : x + z, 0.0);
                    } else {
                        put_eig(wr, wi, nn - 1, x + p, -z);
                        put_eig(wr, wi, nn, x + p, z);
                    }
                    nn -= 2;
                } else {
                    if (its == 30) {
                        return LA_NOCONV;
                    }
                    if (its == 10 || its == 20) {
                        t += x;
                        for (i = 1; i <= nn; i++) {
                            H(i, i) -= x;
                        }
                        s = fabs(H(nn, nn - 1)) + fabs(H(nn - 1, nn - 2));
                        y = x = 0.75 * s;
                        w = -0.4375 * s * s;
                    }
                    ++its;
                    for (m = nn - 2; m >= l; m--) {
                        z = H(m, m);
                        r = x - z;
                        s = y - z;
                        p = (r * s - w) / H(m + 1, m) + H(m, m + 1);
                        q = H(m + 1, m + 1) - z - r - s;
                        r = H(m + 2, m + 1);
                        s = fabs(p) + fabs(q) + fabs(r);
                        p /= s;
                        q /= s;
                        r /= s;
                        if (m == l) {
                            break;
                        }
                        u = fabs(H(m, m - 1)) * (fabs(q) + fabs(r));
                        v = fabs(p) * (fabs(H(m - 1, m - 1)) + fabs(z) + fabs(H(m + 1, m + 1)));
                        if (u + v == v) {
                            break;
                        }
                    }
                    for (i = m + 2; i <= nn; i++) {
                        H(i, i - 2) = 0.0;
                        if (i != m + 2) {
                            H(i, i - 3) = 0.0;
                        }
                    }
                    for (k = m; k <= nn - 1; k++) {
                        if (k != m) {
                            p = H(k, k - 1);
                            q = H(k + 1, k - 1);
                            r = 0.0;
                            if (k != nn - 1) {
                                r = H(k + 2, k - 1);
                            }
                            if ((x = fabs(p) + fabs(q) + fabs(r)) != 0.0) {
                                p /= x;
                                q /= x;
                                r /= x;
                            }
                        }
                        if ((s = sign_of(sqrt(p * p + q * q + r * r), p)) != 0.0) {
                            if (k == m) {
                                if (l != m) {
                                    H(k, k - 1) = -H(k, k - 1);
                                }
                            } else {
                                H(k, k - 1) = -s * x;
                            }
                            p += s;
                            x = p / s;
                            y = q / s;
                            z = r / s;
                            q /= p;
                            r /= p;
                            for (j = k; j <= nn; j++) {
                                p = H(k, j) + q * H(k + 1, j);
                                if (k != nn - 1) {
                                    p += r * H(k + 2, j);
                                    H(k + 2, j) -= p * z;
                                }
                                H(k + 1, j) -= p * y;
                                H(k, j) -= p * x;
                            }
                            mmin = nn < k + 3 ? nn : k + 3;
                            for (i = l; i <= mmin; i++) {
                                p = x * H(i, k) + y * H(i, k + 1);
                                if (k != nn - 1) {
                                    p += z * H(i, k + 2);
                                    H(i, k + 2) -= p * r;
                                }
                                H(i, k + 1) -= p * q;
                                H(i, k) -= p;
                            }
                        }
                    }
                }
            }
        } while (l < nn - 1);
    }
    return LA_OK;
}

#undef H

int la_eigenvalues(const Mat *a, double *wr, double *wi)
{
    int n = a->r, i, j, rc;
    double *h, *r1, *i1;

    h = (double *)malloc((size_t)n * n * sizeof(double));
    r1 = (double *)malloc((size_t)(n + 1) * sizeof(double));
    i1 = (double *)malloc((size_t)(n + 1) * sizeof(double));
    if (h == NULL || r1 == NULL || i1 == NULL) {
        free(h);
        free(r1);
        free(i1);
        return LA_MEMORY;
    }
    memcpy(h, a->d, (size_t)n * n * sizeof(double));
    if (n > 2) {
        elmhes(h, n);
    }
    rc = hqr(h, n, r1, i1);
    if (rc == LA_OK) {
        for (i = 1; i <= n; ++i) {                      /* sort by real part, then imaginary part (insertion sort) */
            double re = r1[i], im = i1[i];

            for (j = i - 1; j >= 1 && (r1[j] > re || (r1[j] == re && i1[j] > im)); --j) {
                r1[j + 1] = r1[j];
                i1[j + 1] = i1[j];
            }
            r1[j + 1] = re;
            i1[j + 1] = im;
        }
        for (i = 0; i < n; ++i) {
            wr[i] = r1[i + 1];
            wi[i] = i1[i + 1];
        }
    }
    free(h);
    free(r1);
    free(i1);
    return rc;
}

/* An eigenvector by inverse iteration: solve (A - (lambda + shift) I) x = previous x a few times. */
int la_eigenvector(const Mat *a, double lambda, double *x)
{
    int n = a->r, i, it, st;
    Mat *shifted, *b, *sol;
    double big = 0.0, shift, norm;

    for (i = 0; i < n * n; ++i) {
        if (fabs(a->d[i]) > big) {
            big = fabs(a->d[i]);
        }
    }
    shift = 1e-10 * (big > 0.0 ? big : 1.0);              /* off the eigenvalue a little, so that the matrix is not exactly singular */
    shifted = mat_copy(a);
    b = mat_new(n, 1);
    if (shifted == NULL || b == NULL) {
        mat_free(shifted);
        mat_free(b);
        return LA_MEMORY;
    }
    for (i = 0; i < n; ++i) {
        MAT_AT(shifted, i, i) -= lambda + shift;
        b->d[i] = 1.0 / sqrt((double)n) * (1.0 + 0.1 * i);   /* a start that is not an accident of symmetry */
    }
    for (it = 0; it < 4; ++it) {
        sol = mat_solve(shifted, b, &st);
        if (sol == NULL) {                                /* exactly singular: use a bigger shift */
            for (i = 0; i < n; ++i) {
                MAT_AT(shifted, i, i) -= 1e3 * shift;
            }
            shift *= 1e3;
            sol = mat_solve(shifted, b, &st);
            if (sol == NULL) {
                mat_free(shifted);
                mat_free(b);
                return st == MAT_MEMORY ? LA_MEMORY : LA_NOCONV;
            }
        }
        norm = 0.0;
        for (i = 0; i < n; ++i) {
            norm += sol->d[i] * sol->d[i];
        }
        norm = sqrt(norm);
        for (i = 0; i < n; ++i) {
            b->d[i] = sol->d[i] / norm;
        }
        mat_free(sol);
    }
    {                                                      /* a sign that is the same every time: the largest component positive */
        int imax = 0;

        for (i = 1; i < n; ++i) {
            if (fabs(b->d[i]) > fabs(b->d[imax])) {
                imax = i;
            }
        }
        for (i = 0; i < n; ++i) {
            x[i] = b->d[imax] < 0.0 ? -b->d[i] : b->d[i];
        }
    }
    mat_free(shifted);
    mat_free(b);
    return LA_OK;
}

/* --- singular values: one-sided Jacobi (Hestenes). Rotate pairs of columns until they are all orthogonal; the lengths are the values. --- */

int la_svd(const Mat *a, Mat *u, double *s, Mat *v)
{
    int m = a->r, n = a->c, i, j, p, q, sweep, rotated, k;
    double tiny;
    Mat *w = mat_copy(a), *vv = mat_new(n, n);

    if (w == NULL || vv == NULL) {
        mat_free(w);
        mat_free(vv);
        return LA_MEMORY;
    }
    for (i = 0; i < n; ++i) {
        MAT_AT(vv, i, i) = 1.0;
    }
    {
        double biggest = 0.0;

        for (j = 0; j < n; ++j) {                          /* columns far smaller than the largest are rounding noise: leave them alone */
            double len2 = 0.0;

            for (i = 0; i < m; ++i) {
                len2 += sq(MAT_AT(w, i, j));
            }
            if (len2 > biggest) {
                biggest = len2;
            }
        }
        tiny = 1e-28 * biggest;
    }
    for (sweep = 0; sweep < 60; ++sweep) {
        rotated = 0;
        for (p = 0; p < n - 1; ++p) {
            for (q = p + 1; q < n; ++q) {
                double alpha = 0.0, beta = 0.0, gamma = 0.0;

                for (i = 0; i < m; ++i) {
                    alpha += sq(MAT_AT(w, i, p));
                    beta += sq(MAT_AT(w, i, q));
                    gamma += MAT_AT(w, i, p) * MAT_AT(w, i, q);
                }
                if (alpha > tiny && beta > tiny && gamma != 0.0 && fabs(gamma) > 1e-12 * sqrt(alpha * beta)) {
                    double zeta = (beta - alpha) / (2.0 * gamma), t, c, sn;

                    t = sign_of(1.0, zeta) / (fabs(zeta) + sqrt(1.0 + zeta * zeta));
                    c = 1.0 / sqrt(1.0 + t * t);
                    sn = c * t;
                    for (i = 0; i < m; ++i) {
                        double up = MAT_AT(w, i, p), uq = MAT_AT(w, i, q);

                        MAT_AT(w, i, p) = c * up - sn * uq;
                        MAT_AT(w, i, q) = sn * up + c * uq;
                    }
                    for (i = 0; i < n; ++i) {
                        double vp = MAT_AT(vv, i, p), vq = MAT_AT(vv, i, q);

                        MAT_AT(vv, i, p) = c * vp - sn * vq;
                        MAT_AT(vv, i, q) = sn * vp + c * vq;
                    }
                    rotated = 1;
                }
            }
            if (mat_check_abort()) {
                mat_free(w);
                mat_free(vv);
                return LA_MEMORY;
            }
        }
        if (!rotated) {
            break;
        }
    }
    if (sweep == 60) {
        mat_free(w);
        mat_free(vv);
        return LA_NOCONV;
    }
    for (j = 0; j < n; ++j) {
        double len = 0.0;

        for (i = 0; i < m; ++i) {
            len += sq(MAT_AT(w, i, j));
        }
        s[j] = sqrt(len);
    }
    /* sort the values down, and their vectors with them */
    for (j = 0; j < n - 1; ++j) {
        k = j;
        for (i = j + 1; i < n; ++i) {
            if (s[i] > s[k]) {
                k = i;
            }
        }
        if (k != j) {
            double t = s[j];

            s[j] = s[k];
            s[k] = t;
            for (i = 0; i < m; ++i) {
                t = MAT_AT(w, i, j);
                MAT_AT(w, i, j) = MAT_AT(w, i, k);
                MAT_AT(w, i, k) = t;
            }
            for (i = 0; i < n; ++i) {
                t = MAT_AT(vv, i, j);
                MAT_AT(vv, i, j) = MAT_AT(vv, i, k);
                MAT_AT(vv, i, k) = t;
            }
        }
    }
    if (u != NULL) {
        for (j = 0; j < n; ++j) {
            for (i = 0; i < m; ++i) {
                MAT_AT(u, i, j) = s[j] > 0.0 ? MAT_AT(w, i, j) / s[j] : 0.0;
            }
        }
    }
    if (v != NULL) {
        for (i = 0; i < n; ++i) {
            for (j = 0; j < n; ++j) {
                MAT_AT(v, i, j) = MAT_AT(vv, i, j);
            }
        }
    }
    mat_free(w);
    mat_free(vv);
    return LA_OK;
}
