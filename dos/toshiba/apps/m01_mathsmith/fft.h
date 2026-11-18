/* The fast Fourier transform for MathSmith (XD100): radix 2 when the length is a power of two, Bluestein's chirp-z method (a convolution done with the radix-2
   transform) for any other length. Plain C89, no I/O, no MathSmith types: the same file is tested natively against Python's numpy/cmath. */
#ifndef MS_FFT_H
#define MS_FFT_H

/* Transforms the n complex numbers (re[i], im[i]) in place. inverse = 0: X[k] = sum x[j] exp(-2 pi i j k / n); inverse = 1: x[j] = (1/n) sum X[k] exp(+2 pi i j k / n),
   so that fft_transform(.., 1) undoes fft_transform(.., 0). n >= 1. Returns 0, or 1 if there is not enough memory (the data are then unchanged). Built with -DFFT_POW2_ONLY (the Toshiba
   and the XT: slow software floating point, little memory, XD102) it does only powers of two and returns 2, with the data unchanged, for any other length. */
int fft_transform(double *re, double *im, long n, int inverse);

#endif
