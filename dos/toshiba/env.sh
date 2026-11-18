# Use the installed Open Watcom cross-compiler on Linux.
export WATCOM=${WATCOM:-/opt/open-watcom}
export PATH="$WATCOM/binl64:$WATCOM/binl:$PATH"
export INCLUDE="$WATCOM/h"
