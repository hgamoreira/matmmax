#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
APP=${MS_ESP8266_APP:-l00_mathsmith}
case "$APP" in l00_mathsmith|l01_webrepl) ;; *) echo "Unknown ESP8266 app" >&2; exit 1 ;; esac
BUILD="$ROOT/build/esp8266/$APP"
ESP_PY="$ROOT/third_party/esp-idf-tools-v5.5.6/python_env/idf5.5_py3.14_env/bin/python"
ACTION=${1:-build}
case "$ACTION" in
    build|test|native|size|release)
        if [ "$#" -gt 1 ]; then echo "Unexpected argument" >&2; exit 1; fi
        ;;
    backup|flash)
        if [ "$#" -ne 2 ]; then echo "Usage: $0 $ACTION SERIAL_PORT" >&2; exit 1; fi
        ;;
    monitor)
        if [ "$#" -lt 2 ] || [ "$#" -gt 3 ]; then echo "Usage: $0 monitor SERIAL_PORT [BAUD]" >&2; exit 1; fi
        ;;
    *) echo "Usage: $0 [build|test|native|size|release|backup PORT|flash PORT|monitor PORT [BAUD]]" >&2; exit 1 ;;
esac
case "$ACTION" in
    build) exec python3 "$ROOT/esp8266/scripts/build.py" ;;
    test)
        cd "$ROOT"
        if [ "$APP" = l01_webrepl ]; then exec python3 -m unittest discover -s esp8266/tests_web -v; fi
        exec python3 -m unittest discover -s esp8266/tests -v ;;
    native)
        python3 "$ROOT/esp8266/scripts/native.py"
        exec "$BUILD/native/mathsmith_light"
        ;;
    size) exec "$ROOT/third_party/esp8266-3.1.2/xtensa-lx106-elf/bin/xtensa-lx106-elf-size" -A "$BUILD/mathsmith_light.elf" ;;
    release) exec python3 "$ROOT/esp8266/scripts/release.py" ;;
    backup)
        mkdir -p "$BUILD/backups"
        BACKUP="$BUILD/backups/d1-mini-$(date +%Y%m%dT%H%M%S%N).bin"
        "$ESP_PY" -m esptool --chip esp8266 --port "$2" --baud 115200 read_flash 0x0 0x400000 "$BACKUP"
        echo "Saved complete original flash: $BACKUP"
        ;;
    flash)
        test -f "$BUILD/mathsmith_light.bin"
        python3 "$ROOT/esp8266/scripts/check_image.py"
        exec "$ESP_PY" -m esptool --chip esp8266 --port "$2" --baud 115200 \
            write_flash --flash_size 4MB --flash_mode dio --flash_freq 40m 0x0 "$BUILD/mathsmith_light.bin"
        ;;
    monitor)
        exec "$ESP_PY" -m serial.tools.miniterm --raw --dtr 0 --rts 0 "$2" "${3:-115200}"
        ;;
esac
