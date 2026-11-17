"""Build a freestanding target ELF and a bounded MSAP v1 package; no OS link."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import zlib

ROOT = Path(__file__).resolve().parents[2]


def build(variant="A", output=None, embed=False):
    destination = output or ROOT / "build/esp32/e03_external" / ("sample.msp" if variant == "A" else "demo_b.msp")
    destination = Path(destination).resolve()
    destination.parent.mkdir(parents=True, exist_ok=True)
    work = destination.parent / (destination.stem+"-build")
    work.mkdir(exist_ok=True)
    tool = ROOT / "third_party/esp-idf-tools-v5.5.6/tools/xtensa-esp-elf/esp-14.2.0_20260121/xtensa-esp-elf/bin"
    gcc = tool / "xtensa-esp32s3-elf-gcc"
    if not gcc.is_file(): raise SystemExit("Run tools/setup_esp_idf.sh first")
    common = ROOT / "common/workstation"
    demo = ROOT / "apps/workstation/demo"
    entry = ROOT / "apps/workstation/external_demo/entry.c"
    flags = [str(gcc), "-std=c99", "-Os", "-Wall", "-Wextra", "-Werror", "-fPIC",
             "-mlongcalls", "-fdata-sections", "-ffunction-sections", "-fvisibility=hidden",
             "-I"+str(common), "-I"+str(demo)]
    greeting = f"External demo {variant}: loaded from a separately built ELF.\n"
    objects = []
    for source in (entry, demo/"demo.c"):
        obj = work / (source.stem+".o")
        subprocess.run(flags + ["-DAPP_GREETING="+json.dumps(greeting), "-c", str(source),
                                "-o", str(obj)], check=True)
        objects.append(str(obj))
    elf = work / "demo.app.elf"
    subprocess.run([str(gcc), "-shared", "-fPIC", "-nostartfiles", "-nostdlib",
                    "-Wl,-e,app_main", "-Wl,--gc-sections", *objects, "-o", str(elf)], check=True)
    subprocess.run([str(tool/"xtensa-esp32s3-elf-strip"), "--strip-unneeded",
                    "--remove-section=.comment", "--remove-section=.dynamic",
                    "--remove-section=.xt.lit", "--remove-section=.xt.prop",
                    "--remove-section=.xtensa.info", str(elf)], check=True)
    data = elf.read_bytes()
    header = struct.pack("<4sHH6I", b"MSAP", 1, 32, 94, 1, 4096,
                         len(data), zlib.crc32(data), 1)
    package = header+data
    if len(package) > 262144: raise SystemExit("App package exceeds 256 KiB")
    destination.write_bytes(package)
    if embed:
        code = "#include <stddef.h>\nconst unsigned char wk_sample_msp[] __attribute__((aligned(4))) = {\n"
        code += "\n".join(",".join(str(b) for b in package[i:i+24])+"," for i in range(0,len(package),24))
        code += "\n};\nconst size_t wk_sample_msp_size = sizeof(wk_sample_msp);\n"
        (destination.parent/"sample_blob.c").write_text(code)
    print(f"{destination}: {len(package)} bytes, SHA256 {hashlib.sha256(package).hexdigest()}")
    return destination


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--variant", choices=["A", "B"], default="A")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--embed", action="store_true")
    args = parser.parse_args()
    build(args.variant, args.output, args.embed)
