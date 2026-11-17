"""Run unmodified upstream tests with their source-checkout paths rebound."""
import importlib.util
import os
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]
COMPONENT = ROOT / "third_party/elf-loader-v2.1.0/elf_loader"
FIXTURES = ROOT / "third_party/elf-loader-v2.1.0-fixtures"


def main():
    tool_bin = ROOT / "third_party/esp-idf-tools-v5.5.6/tools/xtensa-esp-elf/esp-14.2.0_20260121/xtensa-esp-elf/bin"
    os.environ['PATH'] = str(tool_bin)+os.pathsep+os.environ['PATH']
    spec = importlib.util.spec_from_file_location("upstream_elf_tests", COMPONENT/"tests/host/test_elf_validate.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    module.ROOT = FIXTURES
    module.COMPONENT = COMPONENT
    module.FIXTURES = {arch: FIXTURES/f"examples/elf_loader/elf_loader_example/main/test_{arch}.elf"
                       for arch in ("riscv", "xtensa")}
    suite = unittest.defaultTestLoader.loadTestsFromModule(module)
    return 0 if unittest.TextTestRunner(verbosity=2).run(suite).wasSuccessful() else 1


if __name__ == "__main__": raise SystemExit(main())
