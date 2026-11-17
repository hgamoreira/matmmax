"""Stage a hardware candidate; never open a serial port or flash a board."""
import hashlib
from pathlib import Path
import shutil

ROOT = Path(__file__).resolve().parents[2]
BUILD = ROOT / "build/esp32/e00_console"
DEST = ROOT / "releases/esp32/e00_console"


def main():
    sources = {
        "flash.bin": BUILD / "flash.bin",
        "ms_console.elf": BUILD / "ms_console.elf",
        "ms_console.map": BUILD / "ms_console.map",
        "sdkconfig": BUILD / "sdkconfig",
        "README.md": ROOT / "apps/esp32/e00_console/README.md",
        "MEMORY_MAP.md": ROOT / "esp32/docs/MEMORY_MAP_E00.md",
    }
    for source in sources.values():
        if not source.is_file():
            raise SystemExit(f"Missing {source}; build the firmware first.")
    DEST.mkdir(parents=True, exist_ok=True)
    checksums = []
    for name, source in sources.items():
        target = DEST / name
        shutil.copyfile(source, target)
        checksums.append(f"{hashlib.sha256(target.read_bytes()).hexdigest()}  {name}\n")
    (DEST / "SHA256SUMS").write_text("".join(checksums))
    print(f"Staged {DEST}; not flashed or hardware-validated.")


if __name__ == "__main__":
    main()
