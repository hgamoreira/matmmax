"""Exercise the actual E00 target firmware through QEMU's UART."""
import os
from pathlib import Path
import re
import selectors
import shutil
import subprocess
import sys
import tempfile
import time
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "esp32/scripts"))
from run_console import BUILD, QEMU, qemu_command  # noqa: E402


class Console:
    def __init__(self, name, psram=True, *, build=BUILD):
        self.temp = tempfile.TemporaryDirectory(prefix="test-", dir=build)
        self.work = Path(self.temp.name)
        flash = self.work / "flash.bin"
        shutil.copyfile(build / "flash.bin", flash)
        self.process = subprocess.Popen(
            qemu_command(flash, psram=psram), stdin=subprocess.PIPE,
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, bufsize=0,
        )
        self.selector = selectors.DefaultSelector()
        self.selector.register(self.process.stdout, selectors.EVENT_READ)
        self.pending = b""
        self.transcript = b""
        self.log = build / "test-logs" / (name + ".log")

    def close(self):
        self.process.terminate()
        try:
            tail, _ = self.process.communicate(timeout=5)
        except subprocess.TimeoutExpired:
            self.process.kill()
            tail, _ = self.process.communicate(timeout=5)
        self.transcript += tail
        self.selector.close()
        self.log.parent.mkdir(exist_ok=True)
        self.log.write_bytes(self.transcript)
        self.temp.cleanup()

    def read_until(self, marker=b"e00> ", timeout=30):
        deadline = time.monotonic() + timeout
        while marker not in self.pending:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise AssertionError(f"UART timeout waiting for {marker!r}:\n"
                                     + self.transcript[-5000:].decode(errors="replace"))
            if not self.selector.select(remaining):
                continue
            block = os.read(self.process.stdout.fileno(), 65536)
            if not block:
                raise AssertionError("QEMU exited:\n"
                                     + self.transcript[-5000:].decode(errors="replace"))
            self.pending += block
            self.transcript += block
        end = self.pending.index(marker) + len(marker)
        result, self.pending = self.pending[:end], self.pending[end:]
        return result.decode(errors="replace").replace("\r\n", "\n")

    def send(self, data):
        self.process.stdin.write(data)
        self.process.stdin.flush()

    def command(self, text):
        self.send(text.encode("ascii") + b"\r")
        return self.read_until()


class ConsoleFirmwareTest(unittest.TestCase):
    def start(self, psram=True):
        self.assertTrue(QEMU.is_file(), "Run tools/setup_esp_qemu.sh first")
        self.assertTrue((BUILD / "flash.bin").is_file(),
                        "Run esp32/scripts/console.sh build first")
        console = Console(self._testMethodName, psram=psram)
        self.addCleanup(console.close)
        return console, console.read_until()

    def test_boot_and_hardware_report(self):
        console, boot = self.start()
        self.assertIn("MatmMax workstation E00", boot)
        self.assertIn("SDK: v5.5.6", boot)
        self.assertIn("Flash: 16777216 bytes", boot)
        self.assertIn("PSRAM: ready bytes=8388608", boot)
        self.assertIn("double: 8 bytes", boot)
        self.assertIn("UART0 115200 8N1", boot)
        self.assertIn("PSRAM: ready bytes=8388608", console.command("info"))

    def test_memory_patterns_failure_and_recovery(self):
        console, _ = self.start()
        before = console.command("mem")
        for _ in range(3):
            output = console.command("test")
            for expected in ("Internal RAM: PASS", "PSRAM: PASS (1048576 bytes",
                             "Oversize allocation: PASS rejected",
                             "Recovery allocation: PASS", "Heap integrity: PASS",
                             "SELFTEST: PASS"):
                self.assertIn(expected, output)
            self.assertNotIn("FAIL", output)
        after = console.command("mem")
        # External test allocations must all be returned to the heap.
        stats = r"External: total=(\d+) free=(\d+) largest=(\d+)"
        self.assertEqual(re.search(stats, before).groups(),
                         re.search(stats, after).groups())

    def test_line_endings_and_backspace(self):
        console, _ = self.start()
        for ending in (b"\r", b"\n", b"\r\n"):
            console.send(b"echo hello" + ending)
            self.assertIn("\nhello\n", console.read_until())
        console.send(b"echo abc\b\x7fdone\r")
        self.assertIn("\nadone\n", console.read_until())
        self.assertIn("help | info | mem | test | echo TEXT | reboot",
                      console.command("help"))

    def test_unknown_empty_and_cancel(self):
        console, _ = self.start()
        self.assertIn("Unknown command", console.command("wrong"))
        self.assertNotIn("Unknown command", console.command(""))
        console.send(b"reboot\x1b")
        self.assertIn("Cancelled.", console.read_until())
        self.assertIn("\nstill here\n", console.command("echo still here"))

    def test_line_limit_and_overflow_recovery(self):
        console, _ = self.start()
        self.assertIn("\n" + "x" * 122 + "\n", console.command("echo " + "x" * 122))
        output = console.command("echo " + "x" * 123)
        self.assertIn("Line too long", output)
        self.assertNotIn("\n" + "x" * 122 + "\n", output)
        console.send(b"x" * 200 + b"\x1b")
        self.assertIn("Cancelled.", console.read_until())
        self.assertIn("\nrecovered\n", console.command("echo recovered"))

    def test_reboot_returns_to_console(self):
        console, _ = self.start()
        output = console.command("reboot")
        self.assertIn("Rebooting...", output)
        self.assertIn("MatmMax workstation E00", output)
        self.assertIn("\nalive\n", console.command("echo alive"))

    def test_missing_psram_keeps_console_usable(self):
        console, boot = self.start(psram=False)
        self.assertIn("PSRAM: unavailable bytes=0", boot)
        output = console.command("test")
        self.assertIn("PSRAM: FAIL unavailable", output)
        self.assertIn("SELFTEST: FAIL", output)
        self.assertIn("\nconsole works\n", console.command("echo console works"))


if __name__ == "__main__":
    unittest.main()
