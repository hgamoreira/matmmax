#!/bin/sh
# Pinned upstream C sources; local IDF integration needs no cmake_utilities.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
DEST="$ROOT/third_party/elf-loader-v2.1.0"
SHA256=31a00c76ae203d2f29d257bde2c26fb0d79cb0ee46e3b67e61baa361f01a8e4c
URL=https://components-file.espressif.com/components/espressif/elf_loader/2.1.0/download/99d041a4-1cfd-48c3-b99d-3e412b3e2ff2.zip
mkdir -p "$ROOT/third_party"
TMP=$(mktemp -d "$ROOT/third_party/.elf-loader.XXXXXX")
trap 'rm -rf "$TMP"' EXIT HUP INT TERM
if [ "$#" -gt 1 ]; then
    echo "Usage: $0 [archive.zip]" >&2
    exit 1
fi
if [ "$#" -eq 1 ]; then cp -- "$1" "$TMP/package.zip"
else curl -fL --retry 3 --connect-timeout 15 --max-time 120 "$URL" -o "$TMP/package.zip"
fi
echo "$SHA256  $TMP/package.zip" | sha256sum -c -
python3 - "$TMP/package.zip" "$TMP/unpack" <<'PY'
import sys, zipfile
from pathlib import PurePosixPath
with zipfile.ZipFile(sys.argv[1]) as archive:
    for name in archive.namelist():
        p = PurePosixPath(name)
        if p.is_absolute() or '..' in p.parts or p.parts[0] != 'elf_loader':
            raise SystemExit('Unexpected archive member: '+name)
    archive.extractall(sys.argv[2])
PY
# An existing install must match the checked archive, without changing it.
if [ -d "$DEST" ]; then
    diff -qr -x __pycache__ -x '*.pyc' "$DEST/elf_loader" "$TMP/unpack/elf_loader" >/dev/null
else
    mv -- "$TMP/unpack" "$DEST"
fi
echo "ELF loader 2.1.0 ready: $DEST"
