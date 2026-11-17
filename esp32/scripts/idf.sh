#!/bin/sh
# Run the pinned SDK without changing the calling shell or host Python venv.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
export IDF_PATH="$ROOT/third_party/esp-idf-v5.5.6"
export IDF_TOOLS_PATH="$ROOT/third_party/esp-idf-tools-v5.5.6"
export IDF_TARGET=esp32s3
export IDF_COMPONENT_MANAGER=0
if [ ! -f "$IDF_PATH/tools/idf_tools.py" ]; then
    echo 'Run tools/setup_esp_idf.sh first.' >&2
    exit 1
fi
if [ "$(git -C "$IDF_PATH" rev-parse HEAD)" != 880a203ae603d08a94d06aa01e0d6761958e5e9e ]; then
    echo 'SDK commit does not match E00; rerun tools/setup_esp_idf.sh.' >&2
    exit 1
fi
# The shell statements come from the pinned, trusted SDK, not user input.
IDF_EXPORTS=$(python3 "$IDF_PATH/tools/idf_tools.py" export --format shell)
eval "$IDF_EXPORTS"
exec "$IDF_PYTHON_ENV_PATH/bin/python" "$IDF_PATH/tools/idf.py" "$@"
