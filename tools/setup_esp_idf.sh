#!/bin/sh
# E00's SDK and build tools, kept separate from the host server's .venv.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
VERSION=v5.5.6
COMMIT=880a203ae603d08a94d06aa01e0d6761958e5e9e
SDK="$ROOT/third_party/esp-idf-$VERSION"
export IDF_TOOLS_PATH="$ROOT/third_party/esp-idf-tools-$VERSION"
export PIP_CACHE_DIR="$IDF_TOOLS_PATH/pip-cache"
if [ "$(uname -s)/$(uname -m)" != Linux/x86_64 ]; then
    echo 'This setup is tested on x86_64 Linux only.' >&2
    exit 1
fi
mkdir -p "$ROOT/third_party"
if [ ! -d "$SDK" ]; then
    git clone --depth 1 --branch "$VERSION" https://github.com/espressif/esp-idf.git "$SDK"
fi
if [ "$(git -C "$SDK" rev-parse HEAD)" != "$COMMIT" ]; then
    echo "Unexpected SDK commit in $SDK; expected $COMMIT" >&2
    exit 1
fi
git -C "$SDK" submodule update --init --recursive --depth 1
python3 "$SDK/tools/idf_tools.py" install --targets esp32s3
python3 "$SDK/tools/idf_tools.py" install cmake ninja
python3 "$SDK/tools/idf_tools.py" install-python-env
echo "SDK ready: $SDK ($COMMIT)"
