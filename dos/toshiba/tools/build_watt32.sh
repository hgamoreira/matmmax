#!/bin/sh
# Build Watt-32 for OpenWatcom 16-bit LARGE model (TCP enabled) on Linux.
# Result: third_party/Watt-32/lib/wattcpwl.lib, headers in .../inc.
# Needs: third_party/Watt-32 (git clone https://github.com/gvanem/Watt-32,
# tested at commit c727114), OpenWatcom (env.sh), dosbox-x.
# The cross-build cannot make src/build/watcom/syserr.c and
# inc/sys/watcom.err (it needs a DOS tool, util/wc_err.exe, run under
# dosemu in upstream's script); DOSBox-X does that step here.
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
W=$ROOT/third_party/Watt-32
. "$ROOT/env.sh"
export WATT_ROOT=$W
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT

cd "$W/util"
wcl -zq -bcl=dos -ml -wx -I"../inc" -I"$WATCOM/h" -fe=wc_err.exe errnos.c
rm -f errnos.obj
cd "$W/src"
sh configur.sh watcom >/dev/null 2>&1 || true   # its dosemu step fails: expected
{ cat "$ROOT/dosbox-x.conf"
  printf '\n[autoexec]\nmount c %s\nc:\nutil\\wc_err -s > SYSERR.TXT\nutil\\wc_err -e > WATCOM.TXT\nexit\n' "$W"
} > "$T/gen.conf"
# DOSBox-X ignores SIGTERM; -time-limit bounds it. Redirecting straight
# into a subdirectory does not work under the mount, hence the root files.
timeout 60 dosbox-x -conf "$T/gen.conf" -fastlaunch -nogui -silent -time-limit 30 >/dev/null 2>&1 || true
mv "$W/SYSERR.TXT" build/watcom/syserr.c
mv "$W/WATCOM.TXT" ../inc/sys/watcom.err
sed -i 's/\r$//' build/watcom/syserr.c ../inc/sys/watcom.err
wmake -h -f watcom_l.mak
ls -l ../lib/wattcpwl.lib
