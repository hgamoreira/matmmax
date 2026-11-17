"""E02 in QEMU, with a disposable flash copy and the shared S3 configuration."""
from pathlib import Path
import shutil
import subprocess
import tempfile
from run_console import ROOT, QEMU, qemu_command

BUILD = ROOT / "build/esp32/e02_launcher"


def main():
    if not QEMU.is_file():
        raise SystemExit("Install first: tools/setup_esp_qemu.sh")
    if not (BUILD / "flash.bin").is_file():
        raise SystemExit("Build first: esp32/scripts/workstation.sh build")
    with tempfile.TemporaryDirectory(prefix="qemu-", dir=BUILD) as temporary:
        flash = Path(temporary) / "flash.bin"
        shutil.copyfile(BUILD / "flash.bin", flash)
        print("MathSmith E02 in QEMU (no physical serial port).", flush=True)
        return subprocess.call(qemu_command(flash, serial="mon:stdio"))


if __name__ == "__main__":
    raise SystemExit(main())
