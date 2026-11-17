#!/bin/sh
# E00 commands. Physical-board flashing is deliberately an explicit idf command.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
APP="$ROOT/apps/esp32/e00_console"
BUILD="$ROOT/build/esp32/e00_console"
ACTION=${1:-build}
if [ "$#" -gt 1 ]; then
    echo "Usage: $0 [build|size|qemu|test|release]" >&2
    exit 1
fi
case "$ACTION" in
    build)
        mkdir -p "$BUILD"
        exec "$ROOT/esp32/scripts/idf.sh" -C "$APP" -B "$BUILD" \
            -D "SDKCONFIG=$BUILD/sdkconfig" build merge-bin \
            --fill-flash-size 16MB -o "$BUILD/flash.bin"
        ;;
    size)
        mkdir -p "$BUILD"
        exec "$ROOT/esp32/scripts/idf.sh" -C "$APP" -B "$BUILD" \
            -D "SDKCONFIG=$BUILD/sdkconfig" "$ACTION"
        ;;
    qemu)
        exec python3 "$ROOT/esp32/scripts/run_console.py"
        ;;
    test)
        cd "$ROOT"
        exec python3 -m unittest discover -s esp32/tests -v
        ;;
    release)
        exec python3 "$ROOT/esp32/scripts/release_console.py"
        ;;
    *)
        echo "Usage: $0 [build|size|qemu|test|release]" >&2
        exit 1
        ;;
esac
