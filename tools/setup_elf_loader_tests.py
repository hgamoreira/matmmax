"""Fetch only the pinned fixtures required by the upstream loader test suite."""
from pathlib import Path
from urllib.request import urlopen
import hashlib

ROOT = Path(__file__).resolve().parents[1]
COMMIT = "cc06c1c383ba78f913596a176a4513c07657ede5"
BASE = f"https://raw.githubusercontent.com/espressif/esp-iot-solution/{COMMIT}/"
DEST = ROOT / "third_party/elf-loader-v2.1.0-fixtures"
HASHES = {
    "riscv/test_app.elf": "84423683222034a70c8c4b470903c73b56d5c88ce1462fadaa67d3c343c951ff",
    "riscv/test_so.elf": "1c46d2200d288bd2c8a2a22193c2b262f3757876289ed6bc167868532c6465e2",
    "riscv/lib.so": "c0b21f4db43fa6c32a805b9aa18142a8190473d1b0da2004efe645d341a07a8b",
    "xtensa/test_app.elf": "abf37203d06e16a4b89609f9afcbd3f417173bf03f1ac1b380b60fcfee899359",
    "xtensa/test_so.elf": "61ff283a94bc15253b603c405ac0cd3ec6d5f55e51ed9c17762d3a3c46c38373",
    "xtensa/lib.so": "caad7c049059f8779cf526815a1a58f06905d7a0975700dc3da7b79f3e8f4983",
}


def main():
    files = {f"examples/elf_loader/elf_console_example/main/fs_image/{name}": digest
             for name, digest in HASHES.items()}
    for arch in ("riscv", "xtensa"):
        files[f"examples/elf_loader/elf_loader_example/main/test_{arch}.elf"] = HASHES[f"{arch}/test_app.elf"]
    for name, expected in files.items():
        target = DEST/name
        data = target.read_bytes() if target.is_file() else urlopen(BASE+name, timeout=30).read()
        if hashlib.sha256(data).hexdigest() != expected:
            raise SystemExit("Fixture checksum mismatch: "+name)
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
    print("Eight pinned upstream ELF fixtures verified.")


if __name__ == "__main__": main()
