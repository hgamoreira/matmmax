"""Actual ESP32-S3 engine firmware, compared with independent Python references."""
import cmath
import math
from pathlib import Path
import re
import subprocess
import sys
import time
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "esp32/scripts"))
from run_mathsmith import BUILD
from test_console import Console


class MathConsole(Console):
    def __init__(self, name, psram=True, *, build=BUILD):
        super().__init__(name, psram, build=build)

    def read_until(self, marker=b">> ", timeout=30):
        return super().read_until(marker, timeout)


class MatmMaxFirmware(unittest.TestCase):
    build_dir = BUILD
    banner = "MatmMax E01"

    def start(self, psram=True):
        self.assertTrue((self.build_dir / "flash.bin").is_file(),
                        "Run esp32/scripts/mathsmith.sh build first")
        c = MathConsole(self._testMethodName, psram, build=self.build_dir)
        self.addCleanup(c.close)
        boot = c.read_until()
        return c, boot

    def numbers(self, c, expression):
        # fprintf exposes double precision, independently of display rounding.
        output = c.command("fprintf('%.17g\\n'," + expression + ")")
        self.assertNotIn("error:", output)
        body = output.split("\n", 1)[1].rsplit(">> ", 1)[0].strip()
        return [float(word) for word in body.split()]

    def near(self, got, expected, tolerance=3e-13):
        self.assertEqual(len(got), len(expected), (got, expected))
        for a, b in zip(got, expected):
            self.assertAlmostEqual(a, b, delta=tolerance * max(1.0, abs(b)))

    def test_boot_and_scalar_double_precision(self):
        c, boot = self.start()
        self.assertIn(self.banner, boot)
        self.assertIn("double=8", boot)
        self.assertIn("PSRAM: ready bytes=8388608", boot)
        cases = [("sin(1)", math.sin(1)), ("cos(0.37)", math.cos(.37)),
                 ("exp(1)", math.e), ("log(2)", math.log(2)),
                 ("sqrt(2)", math.sqrt(2)), ("pi", math.pi),
                 ("(1+2^-40)-1", 2**-40), ("-2^2", -4), ("2^3^2", 64)]
        for expr, reference in cases:
            with self.subTest(expr=expr):
                self.near(self.numbers(c, expr), [reference], 3e-15)

    def test_matrix_operations_limits_and_error_recovery(self):
        c, _ = self.start()
        c.command("A=[4 -2;-2 4];")
        self.near(self.numbers(c, "A\\[2;8]"), [2, 3])
        self.near(self.numbers(c, "det(A)"), [12])
        self.near(self.numbers(c, "inv(A)"), [1/3, 1/6, 1/6, 1/3])
        self.near(self.numbers(c, "A*inv(A)"), [1, 0, 0, 1])
        c.command("M=eye(64);")
        self.near(self.numbers(c, "sum(sum(M*M))"), [64])
        self.assertIn("error:", c.command("M=ones(65);"))
        self.near(self.numbers(c, "sum(sum(M))"), [64])
        self.assertIn("error:", c.command("inv([1 2;2 4])"))
        self.near(self.numbers(c, "A(1,1)"), [4])

    def test_existing_golden_script_matches_native_engine(self):
        source = ROOT / "apps/m01_mathsmith"
        native = self.build_dir / "golden-native"
        subprocess.run(["gcc", "-std=c89", "-O1", "-I"+str(ROOT/"common"),
                        "-o", str(native), *[str(source/(name+".c")) for name in
                        ("repl", "engine", "mat", "linalg", "plot")], "-lm"],
                       check=True, capture_output=True, text=True)
        script = (source/"tests/golden.txt").read_text()
        reference = subprocess.run([str(native)], input=script, capture_output=True,
                                   text=True, check=True).stdout.split("\n", 1)[1]
        c, _ = self.start()
        output = []
        for line in script.splitlines():
            if line.strip() == "quit": break
            response = c.command(line)
            output.append(response.split("\n", 1)[1].rsplit(">> ", 1)[0])
        pattern = r"[-+]?(?:\d+\.?\d*(?:e[-+]?\d+)?|nan|inf)"
        got = [float(s) for s in re.findall(pattern, "".join(output), re.I)]
        want = [float(s) for s in re.findall(pattern, reference, re.I)]
        self.assertGreater(len(want), 150)
        self.assertEqual(len(got), len(want))
        for a, b in zip(got, want):
            if math.isnan(b): self.assertTrue(math.isnan(a))
            elif math.isinf(b): self.assertEqual(a, b)
            else: self.assertAlmostEqual(a, b, delta=1e-6*max(1,abs(b)))
        memory = c.command(":mem")
        stack_free = int(re.search(r"Main stack minimum free: (\d+)", memory).group(1))
        self.assertGreater(stack_free, 4096, memory)

    def test_calculus_and_statistics(self):
        c, _ = self.start()
        for expr, want, tol in (
            ("integral(@sin,0,pi)", 2, 5e-8),
            ("integral(@exp,0,1)", math.e-1, 5e-8),
            ("deriv(@sin,1)", math.cos(1), 5e-9),
            ("fzero(@(x) x^2-2,1)", math.sqrt(2), 5e-8),
            ("mean([1 2 3 4])", 2.5, 1e-14),
            ("std([1 2 3 4])", math.sqrt(5/3), 1e-14),
        ):
            self.near(self.numbers(c, expr), [want], tol)
        c.command("tic")
        time.sleep(.02)
        elapsed = self.numbers(c, "toc")[0]
        self.assertGreater(elapsed, 0)
        self.assertLess(elapsed, 10)

    def test_fft_against_definition_and_roundtrip(self):
        c, _ = self.start()
        for n in (1, 2, 3, 8, 12, 100, 401, 512):
            c.command(f"k=1:{n}; x=sin(0.37*k)+0.1*k; F=fft(x);")
            output = c.command(
                "for j=1:size(F,1), fprintf('%.17g %.17g\\n',F(j,1),F(j,2)); end")
            self.assertNotIn("error:", output)
            body = output.split("\n", 1)[1].rsplit(">> ", 1)[0]
            values = [float(v) for v in body.split()]
            got = [complex(*values[i:i+2]) for i in range(0, len(values), 2)]
            x = [math.sin(.37*j)+.1*j for j in range(1, n+1)]
            want = [sum(x[j]*cmath.exp(-2j*math.pi*((j*k)%n)/n)
                        for j in range(n)) for k in range(n)]
            self.assertEqual(len(got), n)
            self.assertLess(max(abs(a-b) for a,b in zip(got,want)),
                            2e-12*max(1, max(abs(z) for z in want)), n)
            # Existing MatmMax treats a 1x2 row as two real input samples;
            # only multirow [real imaginary] matrices round-trip as complex.
            if n > 1:
                c.command("y=ifft(F);")
                self.near(self.numbers(c, "max(abs(y(:,1)-x'))"), [0], 2e-11)
                self.near(self.numbers(c, "max(abs(y(:,2)))"), [0], 0)
        c.command("F=fft(ones(1,2048));")
        self.near(self.numbers(c, "F(1,1)"), [2048])
        self.assertIn("error:", c.command("fft(ones(1,2049))"))

    def test_multiline_functions_cancel_and_workspace(self):
        c, _ = self.start()
        c.command("x=41;")
        c.send(b"function y=square(t)\r")
        self.assertIn("..> ", c.read_until(b"..> "))
        c.send(b"y=t*t;\r")
        c.read_until(b"..> ")
        c.command("end")
        self.near(self.numbers(c, "square(7)"), [49])
        c.send(b"while 1\r")
        c.read_until(b"..> ")
        c.send(b"end\r")
        c.read_until(b"end\r\n")
        time.sleep(.1)
        c.send(b"\x1b")
        self.assertIn("interrupted", c.read_until().lower())
        self.near(self.numbers(c, "x+1"), [42])
        c.send(b"for k=1:10\r")
        c.read_until(b"..> ")
        c.send(b"\x1b")
        self.assertIn("Cancelled", c.read_until())
        self.near(self.numbers(c, "x"), [41])

    def test_memory_reuse_reset_and_input_limits(self):
        c, _ = self.start()
        c.command("x=42; A=ones(64);")
        c.command("clear all")
        c.command("A=ones(64); B=A*A; clear all")
        stats = r"Engine payload: used=(\d+)"
        before = int(re.search(stats, c.command(":mem")).group(1))
        for _ in range(5):
            c.command("A=ones(64); B=A*A; clear all")
        after = int(re.search(stats, c.command(":mem")).group(1))
        self.assertEqual(before, after)
        c.command("x=42;")
        self.assertIn("New MatmMax workspace", c.command(":reset"))
        self.assertIn("error:", c.command("x"))
        self.assertIn("Line too long", c.command("x=12;"+" "*400))
        self.assertIn("error:", c.command("x"))
        self.near(self.numbers(c, "2+2"), [4])
        self.assertIn(self.banner, c.command(":reboot"))

    def test_unavailable_services_and_missing_psram(self):
        c, _ = self.start()
        self.assertIn("not implemented", c.command("plot([1 2 3])"))
        self.assertIn("error:", c.command("run missing"))
        self.assertIn("error:", c.command("ask hello"))
        self.assertIn("workspace kept", c.command("quit").lower())
        self.near(self.numbers(c, "1+1"), [2])

    def test_missing_psram_diagnostic_console(self):
        c, boot = self.start(psram=False)
        self.assertIn("PSRAM: unavailable", boot)
        self.assertIn("MatmMax unavailable", c.command("2+2"))
        self.assertIn("SDK:", c.command(":info"))


if __name__ == "__main__":
    unittest.main()
