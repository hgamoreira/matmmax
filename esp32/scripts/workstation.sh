#!/bin/sh
# E02: MathSmith workstation with a foreground app launcher.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
APP="$ROOT/apps/esp32/e02_launcher"
BUILD="$ROOT/build/esp32/e02_launcher"
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
        exec "$ROOT/esp32/scripts/idf.sh" -C "$APP" -B "$BUILD" \
            -D "SDKCONFIG=$BUILD/sdkconfig" size
        ;;
    qemu)
        exec python3 "$ROOT/esp32/scripts/run_workstation.py"
        ;;
    test)
        cd "$ROOT"
        exec python3 -m unittest discover -s esp32/tests -v
        ;;
    release)
        exec python3 "$ROOT/esp32/scripts/release_workstation.py"
        ;;
    *)
        echo "Usage: $0 [build|size|qemu|test|release]" >&2
        exit 1
        ;;
esac
