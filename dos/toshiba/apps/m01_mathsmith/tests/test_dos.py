"""MATHSMTH.EXE in DOSBox-X: lines go in through a redirected file, the results come out in another. The 8086 build with software floating point must
agree with the native engine, and must not stop on 1/0 or sqrt(-1)."""

import os
import unittest

from dosbox import run_program

EXE = os.environ.get("XT_EXE", "build/m01_mathsmith/MATHSMTH.EXE")


def session(lines, limit=60):
    r = run_program(EXE, [], args="< IN.TXT > OUT.TXT", files={"IN.TXT": ("\r\n".join(lines) + "\r\n").encode()}, read_files=["OUT.TXT"],
                    time_limit=limit)
    return (r.files.get("OUT.TXT") or b"").decode("latin1").replace("\r\n", "\n")


class Dos(unittest.TestCase):
    def test_a_session(self):
        out = session(["A = [1 2; 3 4]", "A*A", "x = A\\[5; 6]", "det(A)", "inv(A)", "quit"])
        self.assertIn("MathSmith M08 (line mode)", out)
        self.assertIn("A =\n    1    2\n    3    4\n", out)
        self.assertIn("ans =\n     7    10\n    15    22\n", out)
        self.assertIn("x =\n  -4.0000\n   4.5000\n", out)
        self.assertIn("ans = -2.0000\n", out)
        self.assertIn("ans =\n  -2.0000   1.0000\n   1.5000  -0.5000\n", out)

    def test_infinity_and_not_a_number_do_not_stop_the_program(self):
        out = session(["1/0", "-1/0", "0/0", "sqrt(-1)", "log(0)", "x = 5", "quit"])
        self.assertIn("ans = Inf\n", out)
        self.assertIn("ans = -Inf\n", out)
        self.assertIn("ans = NaN\n", out)
        self.assertIn("x = 5\n", out)                 # still running after all of them

    def test_functions_and_indexing(self):
        out = session(["v = 10:10:50;", "v(2:end-1)", "sum(1:100)", "sin(pi/2)", "B = zeros(2); B(2,3) = 5", "mod(-1,3)", "quit"])
        self.assertIn("ans =\n    20    30    40\n", out)
        self.assertIn("ans = 5050\n", out)
        self.assertIn("ans = 1\n", out)
        self.assertIn("B =\n    0    0    0\n    0    0    5\n", out)
        self.assertIn("ans = 2\n", out)

    def test_errors_are_reported_and_the_session_goes_on(self):
        out = session(["1 +", "inv([1 2; 2 4])", "x = 3", "quit"])
        self.assertIn("error: the expression is incomplete", out)
        self.assertIn("error: matrix is singular to working precision", out)
        self.assertIn("x = 3\n", out)

    def test_calculus_on_the_8086_build(self):
        out = session(["f = @(x) x.^2 - 2", "fzero(f, 1)", "integral(@sin, 0, pi)", "deriv(@sin, 1)", "format long", "integral(@(x) 4./(1+x.^2), 0, 1)", "quit"],
                      limit=120)
        self.assertIn("f = @(x) x.^2 - 2\n", out)
        self.assertIn("ans = 1.4142\n", out)
        self.assertIn("ans = 2\n", out)
        self.assertIn("ans = 0.5403", out)
        self.assertIn("ans = 3.14159265\n", out)

    def test_programs_scripts_and_function_files_on_the_8086_build(self):
        files = {
            "IN.TXT": ("fibm(10)\r\nscr\r\nfunction y = sq(x)\r\ny = x^2;\r\nend\r\nfor k = 1:3\r\n  disp(sq(k))\r\nend\r\nfact(11)\r\nquit\r\n").encode(),
            "FIBM.M": b"function f = fibm(n)\r\nif n < 2\r\n  f = n;\r\nelse\r\n  f = fibm(n-1) + fibm(n-2);\r\nend\r\nend\r\n",
            "FACT.M": b"function f = fact(n)\r\nif n <= 1\r\n  f = 1;\r\nelse\r\n  f = n * fact(n - 1);\r\nend\r\nend\r\n",
            "SCR.M": b"total = 0;\r\nfor k = 1:20\r\n  total = total + k;\r\nend\r\nfprintf('total=%d\\n', total);\r\n",
        }
        r = run_program(EXE, [], args="< IN.TXT > OUT.TXT", files=files, read_files=["OUT.TXT"], time_limit=120)
        out = (r.files.get("OUT.TXT") or b"").decode("latin1").replace("\r\n", "\n")
        self.assertIn("ans = 55\n", out)
        self.assertIn("total=210\n", out)
        self.assertIn("ans = 39916800\n", out)                                   # 11 levels of recursion fit the stack

    def test_eigenvalues_and_singular_values_on_the_8086_build(self):
        """The 8086 build's -fpc code once lost the imaginary parts of the eigenvalues (XD72): checked here by values and by invariants."""
        import re
        lines = [
            "format long",
            "eigc([0 -1; 1 0])",
            "eigc([4 1 2 0; 1 3 0 1; 2 7 5 1; 0 1 1 2])",
            "eig([2 -1 0; -1 2 -1; 0 -1 2])",
            "R = rand(6) - 0.5; e = eigc(R); [abs(sum(e(:,1)) - trace(R)) < 1e-9, abs(sum(e(:,1).^2 - e(:,2).^2) - trace(R*R)) < 1e-9, max(abs(e(:,2))) > 0]",
            "R = rand(7) - 0.5; e = eigc(R); [abs(sum(e(:,1)) - trace(R)) < 1e-9, abs(sum(e(:,1).^2 - e(:,2).^2) - trace(R*R)) < 1e-9]",
            "S = rand(5) - 0.5; S = S + S'; [V, D] = eig(S); [norm(S*V - V*D) < 1e-9, norm(V'*V - eye(5)) < 1e-9]",
            "G = [6 -11 6; 1 0 0; 0 1 0]; [V, D] = eig(G); [norm(G*V - V*D) < 1e-6, diag(D)']",
            "A = rand(5, 3); [U, S, W] = svd(A); [norm(U*S*W' - A) < 1e-9, norm(U'*U - eye(3)) < 1e-9, norm(W'*W - eye(3)) < 1e-9]",
            "svd([1 2; 3 4])",
            "roots([1 -6 11 -6])'",
            "rootsc([1 0 1])",
            "quit",
        ]
        out = session(lines, limit=300)
        out = re.sub(r"(?m)^\(\d+\.\d s\)\n", "", out)
        chunks = out.split(">> ")[1:]                     # one per line typed (the first is "format long")

        def nums(chunk):
            return [float(x) for x in re.findall(r"[-+]?\d+\.?\d*(?:e[-+]?\d+)?", chunk.replace("ans =", ""), flags=re.I)]

        self.assertEqual([round(x) for x in nums(chunks[1])], [0, -1, 0, 1])
        pair = nums(chunks[2])
        for got, want in zip(pair, [1.88270148, -1.42803397, 1.88270148, 1.42803397, 2.60573088, 0, 7.62886616, 0]):
            self.assertAlmostEqual(got, want, places=6)
        for got, want in zip(nums(chunks[3]), [2 - 2 ** 0.5, 2.0, 2 + 2 ** 0.5]):
            self.assertAlmostEqual(got, want, places=7)
        self.assertEqual(nums(chunks[4]), [1, 1, 1])
        self.assertEqual(nums(chunks[5]), [1, 1])
        self.assertEqual(nums(chunks[6]), [1, 1])
        got = nums(chunks[7])
        self.assertEqual(got[0], 1)
        for g, w in zip(got[1:], [1, 2, 3]):
            self.assertAlmostEqual(g, w, places=6)
        self.assertEqual(nums(chunks[8]), [1, 1, 1])
        for g, w in zip(nums(chunks[9]), [5.46498570, 0.36596619]):
            self.assertAlmostEqual(g, w, places=6)
        for g, w in zip(nums(chunks[10]), [1, 2, 3]):
            self.assertAlmostEqual(g, w, places=6)
        self.assertEqual([round(x) for x in nums(chunks[11])], [0, -1, 0, 1])


if __name__ == "__main__":
    unittest.main()
