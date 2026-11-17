/* The fast Fourier transform (XD100): see fft.h. */

#include <math.h>
#include <stdlib.h>
#include "fft.h"

#define PI 3.14159265358979323846
#define FFT_DIRECT_MAX 400                        /* up to here a length that is not a power of two uses the definition (160000 multiplications at most) */

static int is_pow2(long n)
{
    return n > 0 && (n & (n - 1)) == 0;
}

/* The two stores in a function of their own: Watcom's 16-bit compiler with -fpc (the software floating point of the Toshiba and the XT) loses the second of two stores of
   floating-point results next to each other in some surroundings (XD72 found it in hqr; XD102 here: the sine of the table came out 0). */
static void put_pair(double *c, double *s, double cc, double ss)
{
    *c = cc;
    *s = ss;
}

/* cos and sin of 2 pi k / n (k >= 0, any size), exact at the multiples of a quarter turn: the angle is cut into whole quarter turns, the rest is in [0, pi/2). Plain
   cos(pi/2) is 6e-17, and noise like that would show up as "1.0000e+00" in a printed result. */
static void unit_root(long k, long n, double *c, double *s)
{
    long q, r;
    double a, cc, ss;

    k %= n;
    q = (4 * k) / n;
    r = 4 * k - q * n;
    if (r == 0) {
        cc = 1.0;
        ss = 0.0;
    } else {
        a = 0.5 * PI * (double)r / (double)n;
        cc = cos(a);
        ss = sin(a);
    }
    switch (q) {
    case 0: put_pair(c, s, cc, ss); break;
    case 1: put_pair(c, s, -ss, cc); break;
    case 2: put_pair(c, s, -cc, -ss); break;
    default: put_pair(c, s, ss, -cc); break;
    }
}

/* Radix 2, in place, forward (sign -1) or with the conjugate twiddles (sign +1), no scaling. n is a power of two. The twiddle factors are not built by repeated
   multiplication (the error would grow with n) but from cos and sin of true angles - and only of the angles in the first eighth of the circle: the rest of the table is
   the same numbers mirrored (cos(pi/2 - a) = sin a, and a quarter turn more swaps and negates). On a machine with software floating point (the Toshiba, the XT) cos and
   sin are what takes the time, and this makes four times fewer of them. Returns 0 or 1 (memory). */
static int radix2(double *re, double *im, long n, int sign)
{
    double *wr, *wi;
    long i, j, k, half, step, m, q, eighth, w, a, b;

    if (n < 2) {
        return 0;
    }
    half = n / 2;
    q = n / 4;
    eighth = n / 8;
    wr = (double *)malloc((size_t)half * sizeof(double));
    wi = (double *)malloc((size_t)half * sizeof(double));
    if (wr == NULL || wi == NULL) {
        free(wr);
        free(wi);
        return 1;
    }
    for (k = 0; k <= eighth && k < half; ++k) {              /* the angles 0 ... pi/4, computed (exact at 0) */
        unit_root(k, n, &wr[k], &wi[k]);
    }
    for (k = eighth + 1; k < q; ++k) {                       /* pi/4 ... pi/2: cos and sin of the angle that is left to the quarter turn, swapped */
        wr[k] = wi[q - k];
        wi[k] = wr[q - k];
    }
    for (k = q; q > 0 && k < half; ++k) {                    /* pi/2 ... pi: a quarter turn on from the table's first quarter */
        wr[k] = k == q ? 0.0 : -wi[k - q];
        wi[k] = k == q ? 1.0 : wr[k - q];
    }
    if (sign < 0) {                                          /* the table holds exp(+i a); the forward transform needs exp(-i a) */
        for (k = 0; k < half; ++k) {
            wi[k] = -wi[k];
        }
    }
    for (i = 1, j = 0; i < n; ++i) {                      /* bit-reversal permutation */
        long bit = n >> 1;
        double t;

        for (; j & bit; bit >>= 1) {
            j ^= bit;
        }
        j ^= bit;
        if (i < j) {
            t = re[i]; re[i] = re[j]; re[j] = t;
            t = im[i]; im[i] = im[j]; im[j] = t;
        }
    }
    for (m = 2; m <= n; m <<= 1) {
        half = m >> 1;
        step = n / m;
        for (i = 0; i < n; i += m) {
            for (k = 0, w = 0; k < half; ++k, w += step) {
                double tr, ti;

                a = i + k;
                b = a + half;
                tr = wr[w] * re[b] - wi[w] * im[b];
                ti = wr[w] * im[b] + wi[w] * re[b];
                re[b] = re[a] - tr;
                im[b] = im[a] - ti;
                re[a] += tr;
                im[a] += ti;
            }
        }
    }
    free(wr);
    free(wi);
    return 0;
}

#ifndef FFT_POW2_ONLY                                      /* the 16-bit machines (XD102): radix 2 only, 3 KB less code and no big arrays */
/* Bluestein: X[k] = w[k] * sum_j (x[j] w[j]) conj(w[k-j]),  w[k] = exp(-i pi k^2 / n), the sum being a convolution done with power-of-two transforms of length
   m >= 2n - 1. k^2 is reduced modulo 2n step by step (k^2 = (k-1)^2 + 2k - 1), so no number gets larger than 2n and the angle stays exact. */
static int bluestein(double *re, double *im, long n, int sign)
{
    long m = 1, k, sq;
    double *cr, *ci, *ar, *ai, *br, *bi;
    int bad = 0;

    while (m < 2 * n - 1) {
        m <<= 1;
    }
    cr = (double *)malloc((size_t)n * sizeof(double));
    ci = (double *)malloc((size_t)n * sizeof(double));
    ar = (double *)calloc((size_t)m, sizeof(double));
    ai = (double *)calloc((size_t)m, sizeof(double));
    br = (double *)calloc((size_t)m, sizeof(double));
    bi = (double *)calloc((size_t)m, sizeof(double));
    if (cr == NULL || ci == NULL || ar == NULL || ai == NULL || br == NULL || bi == NULL) {
        bad = 1;
    } else {
        for (k = 0, sq = 0; k < n; ++k) {                 /* the chirp: exp(sign * i pi k^2 / n) */
            double c, sn;

            if (k > 0) {
                sq = (sq + 2 * k - 1) % (2 * n);
            }
            unit_root(sq, 2 * n, &c, &sn);                 /* pi k^2 / n = 2 pi (k^2) / (2n) */
            cr[k] = c;
            ci[k] = sign * sn;
        }
        for (k = 0; k < n; ++k) {
            ar[k] = re[k] * cr[k] - im[k] * ci[k];
            ai[k] = re[k] * ci[k] + im[k] * cr[k];
        }
        br[0] = cr[0];
        bi[0] = -ci[0];
        for (k = 1; k < n; ++k) {
            br[k] = br[m - k] = cr[k];
            bi[k] = bi[m - k] = -ci[k];
        }
        if (radix2(ar, ai, m, -1) || radix2(br, bi, m, -1)) {
            bad = 1;
        } else {
            for (k = 0; k < m; ++k) {
                double t = ar[k] * br[k] - ai[k] * bi[k];

                ai[k] = ar[k] * bi[k] + ai[k] * br[k];
                ar[k] = t;
            }
            if (radix2(ar, ai, m, 1)) {
                bad = 1;
            } else {
                for (k = 0; k < n; ++k) {
                    double xr = ar[k] / (double)m, xi = ai[k] / (double)m;

                    re[k] = xr * cr[k] - xi * ci[k];
                    im[k] = xr * ci[k] + xi * cr[k];
                }
            }
        }
    }
    free(cr);
    free(ci);
    free(ar);
    free(ai);
    free(br);
    free(bi);
    return bad;
}

/* The definition, O(n^2), for the small lengths that are not powers of two: exact at the easy points (the sum of a real list has an imaginary part of exactly 0) and
   faster than Bluestein's three big transforms. */
static int direct(double *re, double *im, long n, int sign)
{
    double *wr = (double *)malloc((size_t)n * sizeof(double)), *wi = (double *)malloc((size_t)n * sizeof(double));
    double *xr = (double *)malloc((size_t)n * sizeof(double)), *xi = (double *)malloc((size_t)n * sizeof(double));
    long j, k;

    if (wr == NULL || wi == NULL || xr == NULL || xi == NULL) {
        free(wr);
        free(wi);
        free(xr);
        free(xi);
        return 1;
    }
    for (k = 0; k < n; ++k) {
        double c, sn;

        unit_root(k, n, &c, &sn);
        wr[k] = c;
        wi[k] = sign * sn;
    }
    for (k = 0; k < n; ++k) {
        double sr = 0.0, si = 0.0;

        for (j = 0; j < n; ++j) {
            long w = (j * k) % n;

            sr += re[j] * wr[w] - im[j] * wi[w];
            si += re[j] * wi[w] + im[j] * wr[w];
        }
        xr[k] = sr;
        xi[k] = si;
    }
    for (k = 0; k < n; ++k) {
        re[k] = xr[k];
        im[k] = xi[k];
    }
    free(wr);
    free(wi);
    free(xr);
    free(xi);
    return 0;
}

#endif

int fft_transform(double *re, double *im, long n, int inverse)
{
    long i;
    int rc;

    if (n < 2) {
        return 0;
    }
    /* the inverse is the conjugate of the forward transform of the conjugate, divided by n; the sign of the twiddles does the conjugation, so no copy is needed */
#ifdef FFT_POW2_ONLY
    if (!is_pow2(n)) {
        return 2;
    }
    rc = radix2(re, im, n, inverse ? 1 : -1);
#else
    rc = is_pow2(n) ? radix2(re, im, n, inverse ? 1 : -1) : n <= FFT_DIRECT_MAX ? direct(re, im, n, inverse ? 1 : -1) : bluestein(re, im, n, inverse ? 1 : -1);
#endif
    if (rc == 0 && inverse) {
        for (i = 0; i < n; ++i) {
            re[i] /= (double)n;
            im[i] /= (double)n;
        }
    }
    return rc;
}
