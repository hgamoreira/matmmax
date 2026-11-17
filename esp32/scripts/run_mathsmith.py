"""Interactive E01 using the existing S3 QEMU configuration."""
from pathlib import Path
import shutil
import subprocess
import tempfile
from run_console import ROOT, QEMU, qemu_command

BUILD = ROOT / "build/esp32/e01_mathsmith"


def main():
    if not QEMU.is_file():
        raise SystemExit("Install first: tools/setup_esp_qemu.sh")
    if not (BUILD / "flash.bin").is_file():
        raise SystemExit("Build first: esp32/scripts/mathsmith.sh build")
    with tempfile.TemporaryDirectory(prefix="qemu-", dir=BUILD) as directory:
        flash = Path(directory) / "flash.bin"
        shutil.copyfile(BUILD / "flash.bin", flash)
        print("MatmMax E01 in QEMU (no physical serial port).", flush=True)
        return subprocess.call(qemu_command(flash, serial="mon:stdio"))


if __name__ == "__main__":
    raise SystemExit(main())
