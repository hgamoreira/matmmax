"""Stage E03 and its separate app packages; never flash a physical board."""
import hashlib
from pathlib import Path
import shutil

ROOT = Path(__file__).resolve().parents[2]
BUILD = ROOT / "build/esp32/e03_external"
DEST = ROOT / "releases/esp32/e03_external"


def main():
    sources = {
        "flash.bin": BUILD / "flash.bin",
        "mathsmith_load.elf": BUILD / "mathsmith_load.elf",
        "mathsmith_load.map": BUILD / "mathsmith_load.map",
        "sdkconfig": BUILD / "sdkconfig",
        "sample.msp": BUILD / "sample.msp",
        "demo_b.msp": BUILD / "demo_b.msp",
        "README.md": ROOT / "apps/esp32/e03_external/README.md",
        "MEMORY_MAP.md": ROOT / "esp32/docs/MEMORY_MAP_E03.md",
        "WORKSTATION_APP_ABI.md": ROOT / "docs/WORKSTATION_APP_ABI.md",
        "SERIAL_UPLOAD.md": ROOT / "esp32/docs/SERIAL_UPLOAD.md",
        "ELF_LOADER_LICENSE.txt": ROOT / "third_party/elf-loader-v2.1.0/elf_loader/license.txt",
    }
    for source in sources.values():
        if not source.is_file():
            raise SystemExit(f"Missing {source}; build E03 and install its pinned loader first.")
    DEST.mkdir(parents=True, exist_ok=True)
    hashes = []
    for name, source in sources.items():
        shutil.copyfile(source, DEST / name)
        hashes.append(f"{hashlib.sha256((DEST / name).read_bytes()).hexdigest()}  {name}\n")
    (DEST / "SHA256SUMS").write_text("".join(hashes))
    print(f"Staged {DEST}; not flashed or hardware-validated.")


if __name__ == "__main__":
    main()
