/* Eigenvalues and singular values for MathSmith (M05). Plain C89, no I/O. All matrices are Mat (row-major). */
#ifndef MS_LINALG_H
#define MS_LINALG_H

#include "mat.h"

#define LA_OK 0
#define LA_MEMORY 1
#define LA_NOCONV 2                              /* the iteration did not converge */

/* A symmetric n x n matrix: eigenvalues in w (n numbers, ascending); if v is not NULL (n x n) its columns get the orthonormal eigenvectors. */
int la_symmetric(const Mat *a, double *w, Mat *v);
/* A general real n x n matrix: the eigenvalues wr + i wi, sorted by real part (then imaginary part). */
int la_eigenvalues(const Mat *a, double *wr, double *wi);
/* An eigenvector (unit length) of a general matrix for a real eigenvalue, by inverse iteration. x has n numbers. */
int la_eigenvector(const Mat *a, double lambda, double *x);
/* Singular value decomposition of an m x n matrix with m >= n: u (m x n), s (n, descending), v (n x n); a = u*diag(s)*v'. u and v may be NULL. */
int la_svd(const Mat *a, Mat *u, double *s, Mat *v);
/* Non-zero if a is symmetric to within 1e-12 of its largest entry. */
int la_is_symmetric(const Mat *a);

#endif
