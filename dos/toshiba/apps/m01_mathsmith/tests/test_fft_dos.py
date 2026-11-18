"""fft, ifft, cabs and cangle in the 16-bit MathSmith (the Toshiba's MATHSMTH.EXE; the XT's MATHXT.EXE through test_fft_xt.py), XD102: radix 2 only, the length must be
a power of 2. One run in DOSBox-X prints everything; the checks read the numbers and compare them with the definition of the transform written in pure Python. The software
floating point of the 16-bit compiler (-fpc) once lost the imaginary parts of eigenvalues (XD72), which is why the numbers of every build are checked, not assumed."""

import cmath
import math
import os
import re
import unittest

from dosbox import run_program

EXE = os.environ.get("XT_EXE", "build/m01_mathsmith/MATHSMTH.EXE")
LENGTHS = (1, 2, 4, 8, 64, 256)


def signal(n):
    return [math.sin(0.37 * k) + 0.5 * math.cos(0.011 * k * k) + (k % 7) / 7.0 for k in range(1, n + 1)]


def dft(x, inverse=False):
    n = len(x)
    sign = 1 if inverse else -1
    out = [sum(x[j] * cmath.exp(sign * 2j * math.pi * ((j * k) % n) / n) for j in range(n)) for k in range(n)]
    return [v / n for v in out] if inverse else out


def script():
    lines = []
    for n in LENGTHS:
        lines.append("k = 1:%d; x = sin(0.37*k) + 0.5*cos(0.011*k.^2) + mod(k,7)/7; F = fft(x);" % n)
        lines.append("for j = 1:%d, fprintf('S%d %%.15g %%.15g\\n', F(j,1), F(j,2)), end" % (n, n))
        lines.append("y = ifft(F); fprintf('B%d %%.3e %%.3e\\n', max(abs(y(:,1) - x')), max(abs(y(:,2))))" % n)
    lines += ["k = (1:32)'; X = [sin(k) cos(2*k)]; G = fft(X);",
              "for j = 1:32, fprintf('C %.15g %.15g\\n', G(j,1), G(j,2)), end",
              "P = [3 4; 0 0; -1 0; 0 -2]; m = cabs(P); a = cangle(P);",
              "for j = 1:4, fprintf('P %.15g %.15g\\n', m(j), a(j)), end",
              "w = ifft(fft([1 2 3 4])); fprintf('E %g %g %g %g | %g %g %g %g\\n', w(1,1), w(2,1), w(3,1), w(4,1), w(1,2), w(2,2), w(3,2), w(4,2))",
              "fft([1 2 3 4])", "fft(ones(1,12))", "ifft(ones(5,2))", "fft(ones(3,3))", "cabs([1 2 3])",
              "x = ones(1, 12); x = [x zeros(1, 128 - length(x))]; z = fft(x); fprintf('Z %d %d %g\\n', size(z, 1), size(z, 2), z(1,1))",
              "help"]
    return "\r\n".join(lines).encode() + b"\r\n"


class FftDos(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        r = run_program(EXE, [], args="/L < IN.TXT > OUT.TXT", files={"IN.TXT": script()}, read_files=["OUT.TXT"], time_limit=600)
        cls.out = (r.files.get("OUT.TXT") or b"").decode("latin1").replace("\r\n", "\n")

    def tagged(self, tag):
        # the first line a command prints follows the prompt ">> " on the same line
        return [[float(w) for w in m.split()] for m in re.findall(r"(?:^|>> )%s ([^\n]*)$" % tag, self.out, re.M)]

    def test_the_spectrum_equals_the_definition(self):
        for n in LENGTHS:
            got = [complex(a, b) for a, b in self.tagged("S%d" % n)]
            want = dft(signal(n))
            self.assertEqual(len(got), n, n)
            self.assertLess(max(abs(a - b) for a, b in zip(got, want)), 1e-9 * max(max(abs(v) for v in want), 1.0), n)

    def test_ifft_undoes_fft_and_the_result_is_exactly_real(self):
        for n in LENGTHS:
            if n == 1:
                continue                                            # one value is a 1x2 row, which is two real samples (documented)
            (err, imag), = self.tagged("B%d" % n)
            self.assertLess(err, 1e-11, n)
            self.assertEqual(imag, 0.0, n)

    def test_complex_input_and_output(self):
        want = dft([complex(math.sin(k), math.cos(2 * k)) for k in range(1, 33)])
        got = [complex(a, b) for a, b in self.tagged("C")]
        self.assertEqual(len(got), 32)
        self.assertLess(max(abs(a - b) for a, b in zip(got, want)), 1e-9)

    def test_magnitude_and_phase(self):
        for (re_, im_), (mag, phase) in zip([(3.0, 4.0), (0.0, 0.0), (-1.0, 0.0), (0.0, -2.0)], self.tagged("P")):
            self.assertAlmostEqual(mag, math.hypot(re_, im_), places=12)
            self.assertAlmostEqual(phase, math.atan2(im_, re_), places=12)

    def test_small_cases_print_as_whole_numbers(self):
        self.assertIn("E 1 2 3 4 | 0 0 0 0", self.out)
        self.assertIn("ans =\n    10     0\n    -2     2\n    -2     0\n    -2    -2\n", self.out)

    def test_a_length_that_is_not_a_power_of_two_is_refused_with_advice(self):
        self.assertIn("error: fft needs a length that is a power of 2 (got 12): pad the list with zeros, for example to 16", self.out)
        self.assertIn("error: ifft needs a length that is a power of 2 (got 5): pad the list with zeros, for example to 8", self.out)
        self.assertEqual(self.tagged("Z"), [[128.0, 2.0, 12.0]])             # the advice works: padded to 128, the DC term is the sum

    def test_the_other_errors_and_the_help(self):
        self.assertIn("error: fft needs a vector, or two columns [real imaginary] (got 3x3)", self.out)
        self.assertIn("error: cabs needs two columns [real imaginary] (got 1x3)", self.out)
        self.assertIn("Fourier:  F = fft(x)", self.out)
        self.assertIn("The length must be a power of 2.", self.out)


if __name__ == "__main__":
    unittest.main()
