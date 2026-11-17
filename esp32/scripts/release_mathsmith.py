"""Stage E01 without flashing or changing E00's candidate."""
import hashlib
from pathlib import Path
import shutil

ROOT = Path(__file__).resolve().parents[2]
BUILD = ROOT / "build/esp32/e01_mathsmith"
DEST = ROOT / "releases/esp32/e01_mathsmith"


def main():
    sources = {
        "flash.bin": BUILD / "flash.bin",
        "mathsmith_s3.elf": BUILD / "mathsmith_s3.elf",
        "mathsmith_s3.map": BUILD / "mathsmith_s3.map",
        "sdkconfig": BUILD / "sdkconfig",
        "README.md": ROOT / "apps/esp32/e01_mathsmith/README.md",
        "MEMORY_MAP.md": ROOT / "esp32/docs/MEMORY_MAP_E01.md",
    }
    for source in sources.values():
        if not source.is_file():
            raise SystemExit(f"Missing {source}; build E01 first.")
    DEST.mkdir(parents=True, exist_ok=True)
    hashes = []
    for name, source in sources.items():
        shutil.copyfile(source, DEST / name)
        hashes.append(f"{hashlib.sha256((DEST / name).read_bytes()).hexdigest()}  {name}\n")
    (DEST / "SHA256SUMS").write_text("".join(hashes))
    print(f"Staged {DEST}; not flashed or hardware-validated.")


if __name__ == "__main__":
    main()
