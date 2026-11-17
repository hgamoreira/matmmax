#!/bin/sh
set -eu
MS_ESP8266_APP=l01_webrepl
export MS_ESP8266_APP
if [ "${1:-}" = flash-r4 ]; then
    if [ "$#" -ne 2 ]; then echo "Usage: $0 flash-r4 SERIAL_PORT" >&2; exit 1; fi
    TASK_ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
    R4_HISTORY="$TASK_ROOT/build/esp8266/history/l01-r4-before-persistence"
    python3 "$TASK_ROOT/esp8266/scripts/check_image.py" "$R4_HISTORY"
    python3 - "$R4_HISTORY/mathsmith_light.bin" <<'PY'
import hashlib,sys
from pathlib import Path
expected='42d3600f8e243e53747c7fa3d9d2ef87c1c793356ce3229b96c4f09d9d48216c'
if hashlib.sha256(Path(sys.argv[1]).read_bytes()).hexdigest()!=expected:
    raise SystemExit('Archived R4 hash mismatch; refusing to flash')
PY
    exec "$TASK_ROOT/third_party/esp-idf-tools-v5.5.6/python_env/idf5.5_py3.14_env/bin/python" \
        -m esptool --chip esp8266 --port "$2" --baud 115200 \
        write_flash --flash_size 4MB --flash_mode dio --flash_freq 40m 0x0 \
        "$R4_HISTORY/mathsmith_light.bin"
fi
exec "$(dirname -- "$0")/mathsmith.sh" "$@"
