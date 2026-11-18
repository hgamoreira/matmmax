# Build settings for apps/m07_msask (included by the top-level Makefile): MSASK.EXE, the network helper of MathSmith's AI assistant (M07,
# decision XD75). MathSmith writes ASKIN.TXT and runs this; the answer comes back in ASKOUT.TXT. The PC-server client is WordSmith's.

EXE_NAME := MSASK
MODEL := l

EXTRA_SRC_DIRS := apps/03_wordproc
EXTRA_WCC := -fpc -Ithird_party/Watt-32/inc -Iapps/03_wordproc
EXTRA_LIBS := third_party/Watt-32/lib/wattcpwl.lib
STACK := 8192

SOURCES := msask.c wstext.c wsio.c wsnet.c
COMMON_SOURCES := ticks.c cfg.c httpparse.c nfproto.c netget.c

# Not an XT program: make release puts the EXE in build/release; toshiba/scripts/floppy.sh stages it.
RELEASE_DIR := build/release
