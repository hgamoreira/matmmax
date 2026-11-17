"""Run E00 in QEMU using a disposable copy of the merged flash image."""
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
BUILD = ROOT / "build/esp32/e00_console"
QEMU = ROOT / "third_party/esp-qemu/esp_develop_9.2.2_20260417/bin/qemu-system-xtensa"


def qemu_command(flash, *, psram=True, serial="stdio"):
    command = [
        str(QEMU), "-machine", "esp32s3", "-display", "none",
        "-monitor", "none", "-serial", serial, "-nic", "none",
        "-drive", f"file={str(flash).replace(',', ',,')},if=mtd,format=raw",
    ]
    if psram:
        command += ["-m", "8M", "-global",
                    "driver=ssi_psram,property=is_octal,value=true"]
    return command


def main():
    if not QEMU.is_file():
        raise SystemExit("Install first: tools/setup_esp_qemu.sh")
    image = BUILD / "flash.bin"
    if not image.is_file():
        raise SystemExit("Build first: esp32/scripts/console.sh build")
    with tempfile.TemporaryDirectory(prefix="qemu-", dir=BUILD) as temporary:
        flash = Path(temporary) / "flash.bin"
        shutil.copyfile(image, flash)
        print("E00 in QEMU: Ctrl+A then X exits. No physical board is used.", flush=True)
        return subprocess.call(qemu_command(flash, serial="mon:stdio"))


if __name__ == "__main__":
    raise SystemExit(main())
