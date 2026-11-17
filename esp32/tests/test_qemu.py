"""Installed emulator checks only; no MatmMax firmware is involved.

Run: python3 -m unittest discover -s esp32/tests -v
"""
import json
from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
QEMU = ROOT / "third_party/esp-qemu/esp_develop_9.2.2_20260417/bin/qemu-system-xtensa"


class QemuSmokeTest(unittest.TestCase):
    def setUp(self):
        self.assertTrue(QEMU.is_file(), "Install first: tools/setup_esp_qemu.sh")
        output = ROOT / "build/esp32/emulator"
        output.mkdir(parents=True, exist_ok=True)
        self.temp = tempfile.TemporaryDirectory(dir=output)
        self.addCleanup(self.temp.cleanup)
        self.work = Path(self.temp.name)
        self.flash = self.work / "blank.bin"
        self.flash.write_bytes(b"\xff" * (16 * 1024 * 1024))
        # Each test owns a disposable flash image; never open a real device.
        self.command = [
            str(QEMU), "-machine", "esp32s3", "-m", "8M",
            "-global", "driver=ssi_psram,property=is_octal,value=true",
            "-drive", f"file={str(self.flash).replace(',', ',,')},if=mtd,format=raw",
            "-display", "none", "-monitor", "none", "-nic", "none",
        ]

    def test_flash_and_psram_map(self):
        commands = [
            {"execute": "qmp_capabilities"},
            {"execute": "human-monitor-command",
             "arguments": {"command-line": "info mtree"}},
            {"execute": "quit"},
        ]
        result = subprocess.run(
            self.command + ["-S", "-serial", "null", "-qmp", "stdio"],
            input="".join(json.dumps(c) + "\n" for c in commands),
            capture_output=True, text=True, timeout=10,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        replies = [json.loads(line) for line in result.stdout.splitlines()]
        self.assertFalse(any("error" in reply for reply in replies), replies)
        memory_map = "\n".join(r["return"] for r in replies
                               if isinstance(r.get("return"), str))
        self.assertIn("0000000000000000-00000000007fffff (prio 0, ram): psram.memory_region", memory_map)
        self.assertIn("0000000000000000-0000000000ffffff (prio 0, ram): esp32s3.cache.flash_mr", memory_map)

    def test_rom_download_console(self):
        serial = self.work / "uart.log"
        command = self.command + [
            "-global", "driver=esp32s3.gpio,property=strap_mode,value=0x00",
            "-serial", f"file:{serial}",
        ]
        # The ROM waits forever for an upload. The timeout kills and reaps QEMU.
        # Use a file UART: stdin is closed in automated runs.
        with self.assertRaises(subprocess.TimeoutExpired):
            subprocess.run(command, stdin=subprocess.DEVNULL,
                           capture_output=True, timeout=3, check=True)
        console = serial.read_text()
        self.assertIn("ESP-ROM:esp32s3-", console)
        self.assertIn("DOWNLOAD(USB/UART0)", console)
        self.assertIn("waiting for download", console)


if __name__ == "__main__":
    unittest.main()
