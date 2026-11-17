"""E03 in QEMU, using a disposable flash copy and the existing S3 model."""
from pathlib import Path
import shutil
import subprocess
import tempfile
from run_console import ROOT, QEMU, qemu_command

BUILD = ROOT / "build/esp32/e03_external"


def main():
    if not QEMU.is_file(): raise SystemExit("Run tools/setup_esp_qemu.sh first")
    if not (BUILD/"flash.bin").is_file():
        raise SystemExit("Build first: esp32/scripts/external.sh build")
    with tempfile.TemporaryDirectory(prefix="qemu-", dir=BUILD) as directory:
        flash = Path(directory)/"flash.bin"
        shutil.copyfile(BUILD/"flash.bin", flash)
        print("MatmMax E03 in QEMU. Try :app sample.", flush=True)
        return subprocess.call(qemu_command(flash, serial="mon:stdio"))


if __name__ == "__main__": raise SystemExit(main())
