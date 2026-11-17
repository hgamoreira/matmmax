#!/bin/sh
# Repository-local Espressif QEMU, independent of an ESP-IDF SDK install.
# Optional argument: a previously downloaded archive (still checksum-checked).
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
VERSION=esp_develop_9.2.2_20260417
SHA256=0eecb2a34a5586c0e59110f77b9343b7b336e82fdb0e1a30e1dc1bab8a547e35
DEST="$ROOT/third_party/esp-qemu/$VERSION"
ARCHIVE="qemu-xtensa-softmmu-$VERSION-x86_64-linux-gnu.tar.xz"
URL="https://github.com/espressif/qemu/releases/download/esp-develop-9.2.2-20260417/$ARCHIVE"
# Pin and checksum from Espressif's tools/tools.json (checked 2026-11-14):
# https://github.com/espressif/esp-idf/blob/master/tools/tools.json
if [ "$(uname -s)/$(uname -m)" != Linux/x86_64 ]; then
    echo 'This package is for x86_64 Linux only.' >&2
    exit 1
fi
if [ -x "$DEST/bin/qemu-system-xtensa" ]; then
    "$DEST/bin/qemu-system-xtensa" --version
    exit 0
fi
mkdir -p "$ROOT/third_party/esp-qemu"
TMP=$(mktemp -d "$ROOT/third_party/esp-qemu/.install.XXXXXX")
trap 'rm -rf "$TMP"' EXIT HUP INT TERM
if [ "$#" -gt 1 ]; then
    echo "Usage: $0 [downloaded-archive.tar.xz]" >&2
    exit 1
fi
if [ "$#" -eq 1 ]; then
    cp -- "$1" "$TMP/package.tar.xz"
else
    curl -fL --retry 3 --connect-timeout 15 --max-time 300 \
        "$URL" -o "$TMP/package.tar.xz"
fi
echo "$SHA256  $TMP/package.tar.xz" | sha256sum -c -
tar -xJf "$TMP/package.tar.xz" -C "$TMP"
# Exercise the binary before making the install visible; reports missing libs.
"$TMP/qemu/bin/qemu-system-xtensa" --version
mv -- "$TMP/qemu" "$DEST"
echo "Installed in $DEST"
