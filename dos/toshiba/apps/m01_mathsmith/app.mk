# Build settings for apps/m01_mathsmith (included by the top-level Makefile): MathSmith's engine (M01) with a line-by-line front end.

EXE_NAME := MATHSMTH

# Large model: the matrices and variables live in the far heap. Software floating point by library calls (the Toshiba has no 8087, and
# -fpc measured about 3 times faster than the emulator in the emulated machine; to be confirmed on the Toshiba with PROBEC).
MODEL := l
EXTRA_SRC_DIRS := apps/03_wordproc
EXTRA_WCC := -DMS_FFT -DFFT_POW2_ONLY -fpc -s -zc -Iapps/03_wordproc
STACK := 32000

SOURCES := repl.c engine.c mat.c linalg.c fft.c plot.c plotdos.c askdos.c ui.c editor.c wstext.c wsio.c
COMMON_SOURCES := screen.c

# Hardware-free sources also built natively for the Linux unit tests.
NATIVE_SOURCES := engine.c mat.c linalg.c fft.c plot.c
NATIVE_EXTRA_FLAGS := -DMS_FFT -DFFT_POW2_ONLY

RELEASE_DIR := build/release
