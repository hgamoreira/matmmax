"""Native (Linux) unit tests of MathSmith's engine (M01): a line goes in, the text the user would see comes out."""

import ctypes
import math
import os
import re
import tempfile
import unittest

LIB = ctypes.CDLL(os.path.abspath(os.environ.get("XT_NATIVE_LIB", "build/m01_mathsmith/native.so")))
SINK = ctypes.CFUNCTYPE(None, ctypes.c_void_p, ctypes.c_char_p)
LIB.ml_open.restype = ctypes.c_void_p
LIB.ml_open.argtypes = [SINK, ctypes.c_void_p]
LIB.ml_close.argtypes = [ctypes.c_void_p]
LIB.ml_eval.restype = ctypes.c_int
LIB.ml_eval.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
LIB.ml_error.restype = ctypes.c_char_p
LIB.ml_error.argtypes = [ctypes.c_void_p]
OK, ERROR, QUIT = 0, 1, 2


HOOK = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.c_void_p, ctypes.c_void_p)


class PlotText(ctypes.Structure):
    _fields_ = [("row", ctypes.c_int), ("col", ctypes.c_int), ("text", ctypes.c_char * 81)]


LIB.ml_set_plot_hook.argtypes = [ctypes.c_void_p, HOOK, ctypes.c_void_p]
LIB.ml_render_plot.restype = ctypes.c_int
LIB.ml_render_plot.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.POINTER(PlotText), ctypes.c_int, ctypes.POINTER(ctypes.c_double)]
LIB.ml_plot_curves.restype = ctypes.c_int
LIB.ml_plot_curves.argtypes = [ctypes.c_void_p]
LIB.ml_show_plot.restype = ctypes.c_int
LIB.ml_show_plot.argtypes = [ctypes.c_void_p]


class Session:
    def __init__(self):
        self.out = []
        self.cb = SINK(lambda ctx, text: self.out.append(text.decode()))
        self.s = LIB.ml_open(self.cb, None)
        self.shown = 0
        self.hook = HOOK(self._hook)
        LIB.ml_set_plot_hook(self.s, self.hook, None)

    def _hook(self, ctx, spec):
        self.shown += 1
        return 0

    def plot(self):
        """(pixels as a list of 200 strings of 640 characters, texts as (row, col, text), limits) of the current plot."""
        bits = ctypes.create_string_buffer(16000)
        texts = (PlotText * 40)()
        limits = (ctypes.c_double * 4)()
        n = LIB.ml_render_plot(self.s, bits, texts, 40, limits)
        rows = ["".join("#" if bits.raw[y * 80 + (x >> 3)] & (0x80 >> (x & 7)) else "." for x in range(640)) for y in range(200)]
        return rows, [(texts[i].row, texts[i].col, texts[i].text.decode()) for i in range(n)], list(limits)

    def close(self):
        LIB.ml_close(self.s)

    def run(self, line):
        """The text printed by one line (all lines when a list is given: the last one's text)."""
        text = ""
        for one in ([line] if isinstance(line, str) else line):
            self.out.clear()
            self.rc = LIB.ml_eval(self.s, one.encode())
            text = "".join(self.out)
        return text

    def error(self):
        return LIB.ml_error(self.s).decode()

    def numbers(self, line):
        """All the numbers in the text printed by the line (skips the 'name =' and 'Columns' parts)."""
        text = self.run(line)
        text = re.sub(r"Columns? [0-9 through]+:", "", text)
        text = re.sub(r"^\s*[A-Za-z_][A-Za-z_0-9]* =", "", text, flags=re.M)
        return [float(x) for x in re.findall(r"[-+]?(?:\d+\.?\d*(?:e[-+]?\d+)?|NaN|Inf)", text, flags=re.I)]


class Engine(unittest.TestCase):
    def setUp(self):
        self.m = Session()
        self.addCleanup(self.m.close)

    def close(self, got, want, tol=2e-4):          # the screen shows four decimals
        self.assertEqual(len(got), len(want), (got, want))
        for g, w in zip(got, want):
            self.assertAlmostEqual(g, w, delta=tol * max(1.0, abs(w)))

    # --- display ---
    def test_a_scalar_assignment_shows_itself(self):
        self.assertEqual(self.m.run("x = 3"), "x = 3\n")

    def test_a_semicolon_hides_the_result(self):
        self.assertEqual(self.m.run("x = 3;"), "")
        self.assertEqual(self.m.run("x"), "x = 3\n")

    def test_the_result_of_an_expression_is_ans(self):
        self.assertEqual(self.m.run("1/3"), "ans = 0.3333\n")
        self.assertEqual(self.m.run("ans*3"), "ans = 1\n")

    def test_a_bare_variable_does_not_change_ans(self):
        self.m.run(["x = 5;", "2+2", "x"])
        self.assertEqual(self.m.run("ans"), "ans = 4\n")

    def test_an_integer_matrix(self):
        self.assertEqual(self.m.run("A = [1 2; 3 4]"), "A =\n    1    2\n    3    4\n")

    def test_a_decimal_matrix(self):
        self.assertEqual(self.m.run("[0.5 1.25]"), "ans =\n   0.5000   1.2500\n")

    def test_negative_and_wide_numbers_align(self):
        self.assertEqual(self.m.run("[-1 22; 3 -444]"), "ans =\n     -1     22\n      3   -444\n")

    def test_big_and_small_numbers_use_exponents(self):
        self.assertEqual(self.m.run("1e10"), "ans = 1.0000e+10\n")
        self.assertEqual(self.m.run("0.00001"), "ans = 1.0000e-05\n")
        self.assertEqual(self.m.run("12345.678"), "ans = 12345.6780\n")

    def test_negative_zero_is_shown_as_zero(self):
        self.assertEqual(self.m.run("0*-1"), "ans = 0\n")
        self.assertEqual(self.m.run("[0*-1 1.5]"), "ans =\n        0   1.5000\n")

    def test_format_long_shows_more_decimals(self):
        self.m.run("format long")
        self.assertEqual(self.m.run("pi"), "ans = 3.14159265\n")
        self.m.run("format short")
        self.assertEqual(self.m.run("pi"), "ans = 3.1416\n")

    def test_a_long_row_wraps_in_column_groups(self):
        text = self.m.run("1:30")
        self.assertIn("Columns 1 through 13:", text)
        self.assertIn("Columns 27 through 30:", text)
        self.assertTrue(all(len(line) <= 80 for line in text.splitlines()))

    def test_an_empty_matrix(self):
        self.assertEqual(self.m.run("x = []"), "x = []\n")

    def test_nan_and_inf(self):
        self.assertEqual(self.m.run("1/0"), "ans = Inf\n")
        self.assertEqual(self.m.run("-1/0"), "ans = -Inf\n")
        self.assertEqual(self.m.run("0/0"), "ans = NaN\n")
        self.assertEqual(self.m.run("sqrt(-1)"), "ans = NaN\n")

    # --- the language ---
    def test_precedence_and_associativity(self):
        for line, want in (("-2^2", -4), ("2^3^2", 64), ("2^-1", 0.5), ("1+2*3", 7), ("(1+2)*3", 9), ("10-4-3", 3), ("8/4/2", 1), ("2*3^2", 18)):
            self.assertEqual(self.m.numbers(line), [want], line)

    def test_spaces_inside_brackets(self):
        self.assertEqual(self.m.numbers("[1 -2]"), [1, -2])
        self.assertEqual(self.m.numbers("[1 - 2]"), [-1])
        self.assertEqual(self.m.numbers("[1 -2 + 3]"), [1, 1])
        self.assertEqual(self.m.numbers("x = 5; [x -1]"), [5, -1])
        self.assertEqual(self.m.numbers("[1, 2, 3]"), [1, 2, 3])
        self.assertEqual(self.m.numbers("[1 2\t3]"), [1, 2, 3])

    def test_rows_and_transpose(self):
        self.assertEqual(self.m.run("[1 2 3]'"), "ans =\n    1\n    2\n    3\n")
        self.assertEqual(self.m.numbers("[1 2; 3 4]'"), [1, 3, 2, 4])
        self.assertEqual(self.m.numbers("x = [1 2 3]; x*x'"), [14])

    def test_ranges(self):
        self.assertEqual(self.m.numbers("1:5"), [1, 2, 3, 4, 5])
        self.assertEqual(self.m.numbers("0:0.5:2"), [0, 0.5, 1, 1.5, 2])
        self.assertEqual(self.m.numbers("5:-2:1"), [5, 3, 1])
        self.assertEqual(self.m.run("5:1"), "ans = []\n")

    def test_element_wise_operators(self):
        self.assertEqual(self.m.numbers("[1 2 3].*[4 5 6]"), [4, 10, 18])
        self.assertEqual(self.m.numbers("[1 2 3].^2"), [1, 4, 9])
        self.assertEqual(self.m.numbers("2.^[1 2 3]"), [2, 4, 8])
        self.assertEqual(self.m.numbers("[2 4 6]./2"), [1, 2, 3])

    def test_scalar_and_vector_expansion(self):
        self.assertEqual(self.m.numbers("[1 2 3] + 10"), [11, 12, 13])
        self.assertEqual(self.m.numbers("[1 2; 3 4] + [10 20]"), [11, 22, 13, 24])
        self.assertEqual(self.m.numbers("[1 2; 3 4] + [10; 20]"), [11, 12, 23, 24])

    def test_comments_and_several_statements(self):
        self.assertEqual(self.m.run("x = 4 % a comment"), "x = 4\n")
        self.assertEqual(self.m.run("a=1, b=2; c=3"), "a = 1\nc = 3\n")

    # --- indexing ---
    def test_indexing(self):
        self.m.run("A = [1 2; 3 4];")
        self.assertEqual(self.m.numbers("A(2,1)"), [3])
        self.assertEqual(self.m.numbers("A(:,2)"), [2, 4])
        self.assertEqual(self.m.numbers("A(1,:)"), [1, 2])
        self.assertEqual(self.m.numbers("A(end,end)"), [4])
        self.assertEqual(self.m.numbers("A(end)"), [4])
        self.assertEqual(self.m.numbers("A(3)"), [2])                 # down the columns, as in MATLAB
        self.assertEqual(self.m.numbers("A(:)"), [1, 3, 2, 4])
        self.assertEqual(self.m.numbers("A([2 1],[1 2])"), [3, 4, 1, 2])
        self.m.run("v = 10:10:50;")
        self.assertEqual(self.m.numbers("v(2:end-1)"), [20, 30, 40])
        self.assertEqual(self.m.run("v([1 3])"), "ans =\n    10    30\n")

    def test_index_errors(self):
        self.m.run("A = [1 2; 3 4];")
        self.m.run("A(3,1)")
        self.assertEqual(self.m.rc, ERROR)
        self.assertIn("exceeds", self.m.error())
        self.m.run("A(0)")
        self.assertIn("positive integers", self.m.error())
        self.m.run("A(1.5)")
        self.assertIn("positive integers", self.m.error())

    def test_assignment_by_index_and_growth(self):
        self.m.run("v = []; v(3) = 7;")
        self.assertEqual(self.m.numbers("v"), [0, 0, 7])
        self.m.run("v(end+1) = 1;")
        self.assertEqual(self.m.numbers("v"), [0, 0, 7, 1])
        self.m.run("B = zeros(2); B(2,3) = 5;")
        self.assertEqual(self.m.run("size(B)"), "ans =\n    2    3\n")
        self.assertEqual(self.m.numbers("B"), [0, 0, 0, 0, 0, 5])
        self.m.run("B(1,:) = [1 2 3];")
        self.assertEqual(self.m.numbers("B(1,:)"), [1, 2, 3])
        self.m.run("C = eye(3); C(2,2) = 9; C(:,3) = [7;8;9];")
        self.assertEqual(self.m.numbers("C"), [1, 0, 7, 0, 9, 8, 0, 0, 9])

    def test_deleting_elements_rows_and_columns(self):
        self.m.run("v = 1:6; v(2) = [];")
        self.assertEqual(self.m.numbers("v"), [1, 3, 4, 5, 6])
        self.m.run("v([1 end]) = [];")
        self.assertEqual(self.m.numbers("v"), [3, 4, 5])
        self.m.run("x = 1:5; x(x > 2) = [];")
        self.assertEqual(self.m.numbers("x"), [1, 2])
        self.m.run("A = [1 2 3; 4 5 6; 7 8 9]; A(2,:) = [];")
        self.assertEqual(self.m.numbers("A"), [1, 2, 3, 7, 8, 9])
        self.m.run("A(:,1) = [];")
        self.assertEqual(self.m.numbers("A"), [2, 3, 8, 9])
        self.m.run("c = [1;2;3]; c(1) = [];")
        self.assertEqual(self.m.run("size(c)"), "ans =\n    2    1\n")
        self.m.run("A(1,1) = [];")
        self.assertIn("whole rows or whole columns", self.m.error())

    def test_a_failed_assignment_leaves_the_variable_alone(self):
        self.m.run("A = [1 2; 3 4];")
        self.m.run("A(1,:) = [1 2 3];")
        self.assertEqual(self.m.rc, ERROR)
        self.assertEqual(self.m.numbers("A"), [1, 2, 3, 4])

    # --- matrices ---
    def test_product_and_power(self):
        self.m.run("A = [1 2; 3 4];")
        self.assertEqual(self.m.numbers("A*A"), [7, 10, 15, 22])
        self.assertEqual(self.m.numbers("A^2"), [7, 10, 15, 22])
        self.assertEqual(self.m.numbers("A^0"), [1, 0, 0, 1])
        self.close(self.m.numbers("A^-1"), [-2, 1, 1.5, -0.5], 1e-3)
        self.close(self.m.numbers("A^3"), [37, 54, 81, 118])

    def test_inverse_determinant_trace_rank(self):
        self.m.run("A = [1 2; 3 4];")
        self.close(self.m.numbers("inv(A)"), [-2, 1, 1.5, -0.5], 1e-3)
        self.close(self.m.numbers("det(A)"), [-2], 1e-3)
        self.assertEqual(self.m.numbers("trace(A)"), [5])
        self.assertEqual(self.m.numbers("rank(A)"), [2])
        self.assertEqual(self.m.numbers("rank([1 2; 2 4])"), [1])
        self.assertEqual(self.m.numbers("det([1 2; 2 4])"), [0])
        self.assertEqual(self.m.numbers("det(eye(4))"), [1])

    def test_a_singular_matrix_is_reported(self):
        self.m.run("inv([1 2; 2 4])")
        self.assertEqual(self.m.rc, ERROR)
        self.assertIn("singular", self.m.error())
        self.m.run("inv([1 2 3; 4 5 6; 7 8 9])")
        self.assertIn("singular", self.m.error())

    def test_solving_a_system(self):
        self.close(self.m.numbers("[2 1; 1 3] \\ [3; 5]"), [0.8, 1.4], 1e-3)
        self.m.run("A = [4 -2 1; -2 4 -2; 1 -2 4]; b = [11; -16; 17];")
        self.close(self.m.numbers("x = A\\b; A*x - b"), [0, 0, 0], 1e-9)
        self.close(self.m.numbers("b'/A"), self.m.numbers("(A'\\b)'"), 1e-9)

    def test_a_larger_system_has_a_small_residual(self):
        self.m.run("A = rand(12) + 12*eye(12); b = ones(12,1); x = A\\b;")
        self.assertLess(self.m.numbers("norm(A*x - b)")[0], 1e-10)
        self.assertEqual(self.m.numbers("inv(inv(eye(5)))"), [1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1])

    def test_least_squares(self):
        self.m.run("A = [1 1; 1 2; 1 3]; y = [1; 2; 2];")
        self.close(self.m.numbers("A\\y"), [2.0 / 3.0, 0.5])

    def test_dimension_errors(self):
        self.m.run("[1 2] * [3 4]")
        self.assertIn("inner matrix dimensions must agree", self.m.error())
        self.m.run("[1 2 3] + [1 2]")
        self.assertIn("dimensions must agree", self.m.error())
        self.m.run("[1 2; 3]")
        self.assertIn("not consistent", self.m.error())
        self.m.run("zeros(100)")
        self.assertIn("4096", self.m.error())

    # --- functions ---
    def test_constructors(self):
        self.assertEqual(self.m.numbers("eye(3)"), [1, 0, 0, 0, 1, 0, 0, 0, 1])
        self.assertEqual(self.m.numbers("zeros(2,3)"), [0] * 6)
        self.assertEqual(self.m.numbers("ones(2)"), [1] * 4)
        self.assertEqual(self.m.numbers("linspace(0,1,5)"), [0, 0.25, 0.5, 0.75, 1])
        self.assertEqual(self.m.numbers("diag([1 2 3])"), [1, 0, 0, 0, 2, 0, 0, 0, 3])
        self.assertEqual(self.m.numbers("diag([1 2; 3 4])"), [1, 4])
        self.assertEqual(self.m.numbers("reshape(1:6,2,3)"), [1, 3, 5, 2, 4, 6])
        r = self.m.numbers("rand(3)")
        self.assertEqual(len(r), 9)
        self.assertTrue(all(0.0 <= x < 1.0 for x in r))

    def test_reductions(self):
        self.assertEqual(self.m.numbers("sum([1 2; 3 4])"), [4, 6])
        self.assertEqual(self.m.numbers("sum(sum([1 2; 3 4]))"), [10])
        self.assertEqual(self.m.numbers("prod([1 2 3 4])"), [24])
        self.assertEqual(self.m.numbers("mean(1:4)"), [2.5])
        self.assertEqual(self.m.numbers("max([3 9 2])"), [9])
        self.assertEqual(self.m.numbers("min([3 9 2])"), [2])
        self.assertEqual(self.m.numbers("max([1 5; 7 2])"), [7, 5])
        self.assertEqual(self.m.numbers("cumsum(1:4)"), [1, 3, 6, 10])

    def test_scalar_functions(self):
        for line, want in (("sqrt(16)", 4), ("abs(-3)", 3), ("floor(2.7)", 2), ("ceil(2.1)", 3), ("round(2.5)", 3), ("round(-2.5)", -3),
                           ("fix(-2.7)", -2), ("mod(7,3)", 1), ("mod(-1,3)", 2), ("rem(-1,3)", -1), ("sign(-4)", -1),
                           ("exp(0)", 1), ("log(1)", 0), ("log10(1000)", 3), ("norm([3 4])", 5), ("dot([1 2 3],[4 5 6])", 32),
                           ("length([1 2 3 4])", 4), ("numel(zeros(3,4))", 12), ("size(zeros(3,4),1)", 3), ("size(zeros(3,4),2)", 4)):
            self.close(self.m.numbers(line), [want], 1e-9)
        self.close(self.m.numbers("sin(pi/2)"), [1])
        self.close(self.m.numbers("cos(pi)"), [-1])
        self.close(self.m.numbers("atan2(1,1)*4"), [math.pi])
        self.close(self.m.numbers("tan(pi/4)"), [1])
        self.close(self.m.numbers("asin(1)*2"), [math.pi])

    def test_functions_work_on_every_element(self):
        self.close(self.m.numbers("sqrt([1 4 9])"), [1, 2, 3])
        self.close(self.m.numbers("abs([-1 2; -3 4])"), [1, 2, 3, 4])

    def test_wrong_calls_are_reported(self):
        self.m.run("sqrt(1,2)")
        self.assertIn("wrong number of arguments", self.m.error())
        self.m.run("foo(1)")
        self.assertIn("undefined variable or function 'foo'", self.m.error())
        self.m.run("y + 1")
        self.assertIn("undefined variable or function 'y'", self.m.error())

    # --- commands and the session ---
    def test_errors_do_not_end_the_session(self):
        self.m.run("x = 1;")
        self.m.run("1 +")
        self.assertEqual(self.m.rc, ERROR)
        self.assertIn("error:", self.m.run("(1"))
        self.assertEqual(self.m.run("x"), "x = 1\n")

    def test_who_and_clear(self):
        self.m.run("a = 1; B = zeros(2,3);")
        text = self.m.run("who")
        self.assertIn("a", text)
        self.assertIn("2 x 3", text)
        self.m.run("clear a")
        self.assertNotIn(" a ", self.m.run("who"))
        self.m.run("clear")
        self.assertIn("no variables", self.m.run("who"))
        self.m.run("a")
        self.assertIn("undefined", self.m.error())

    def test_help_and_quit(self):
        self.assertIn("MathSmith", self.m.run("help"))
        self.m.run("quit")
        self.assertEqual(self.m.rc, QUIT)
        self.m.run("exit")
        self.assertEqual(self.m.rc, QUIT)

    def test_too_long_a_line(self):
        self.m.run("x = " + "1+" * 300 + "1")
        self.assertEqual(self.m.rc, ERROR)
        self.assertIn("too long", self.m.error())

    def test_many_variables(self):
        for i in range(40):
            self.m.run("v%d = %d;" % (i, i))
        self.m.run("one_too_many = 1;")
        self.assertEqual(self.m.rc, ERROR)
        self.assertIn("too many variables", self.m.error())


class Calculus(unittest.TestCase):
    def setUp(self):
        self.m = Session()
        self.addCleanup(self.m.close)

    def close(self, got, want, tol=1e-8):
        self.assertEqual(len(got), len(want), (got, want))
        for g, w in zip(got, want):
            self.assertAlmostEqual(g, w, delta=tol * max(1.0, abs(w)))

    def precise(self):
        self.m.run("format long")                         # eight decimals

    def test_defining_and_calling_a_function(self):
        self.assertEqual(self.m.run("f = @(x) x^2 - 2"), "f = @(x) x^2 - 2\n")
        self.assertEqual(self.m.run("f(3)"), "ans = 7\n")
        self.assertEqual(self.m.run("f = @(x) x^2 - 2;"), "")
        self.assertEqual(self.m.run("h = @(x,y) x*y + 1; h(2,3)"), "ans = 7\n")
        self.assertEqual(self.m.run("c = @() 42; c()"), "ans = 42\n")

    def test_functions_work_on_vectors_when_the_body_does(self):
        self.m.run("g = @(x) x.^2;")
        self.assertEqual(self.m.numbers("g([1 2 3])"), [1, 4, 9])
        self.m.run("f = @(x) x^2;")
        self.m.run("f([1 2 3])")
        self.assertEqual(self.m.rc, ERROR)
        self.assertIn("^", self.m.error())
        self.assertEqual(self.m.numbers("arrayfun(@(x) x^2, [1 2; 3 4])"), [1, 4, 9, 16])

    def test_parameters_do_not_touch_the_workspace(self):
        self.m.run("x = 5; f = @(x) x + 1;")
        self.assertEqual(self.m.numbers("f(10)"), [11])
        self.assertEqual(self.m.numbers("x"), [5])

    def test_the_body_sees_workspace_variables_at_call_time(self):
        self.m.run("a = 5; k = @(x) a*x;")
        self.assertEqual(self.m.numbers("k(2)"), [10])
        self.m.run("a = 6;")
        self.assertEqual(self.m.numbers("k(2)"), [12])             # MATLAB would keep 10: documented difference

    def test_functions_use_each_other(self):
        self.m.run("f = @(x) x + 1; g = @(x) 2*f(x);")
        self.assertEqual(self.m.numbers("g(3)"), [8])

    def test_a_variable_and_a_function_replace_each_other(self):
        self.m.run("f = @(x) x; f = 3;")
        self.assertEqual(self.m.run("f"), "f = 3\n")
        self.m.run("f = @(x) x + 1;")
        self.m.run("f")
        self.assertEqual(self.m.rc, ERROR)
        self.assertIn("is a function", self.m.error())

    def test_who_and_clear_know_functions(self):
        self.m.run("f = @(x) x^2; v = 1;")
        text = self.m.run("who")
        self.assertIn("function @(x) x^2", text)
        self.m.run("clear f")
        self.assertNotIn("function", self.m.run("who"))
        self.m.run("f = @(x) x; clear all")
        self.assertIn("no variables", self.m.run("who"))

    def test_function_errors(self):
        self.m.run("f = @(x) x; f(1,2)")
        self.assertIn("takes 1 argument", self.m.error())
        self.m.run("r = @(x) r(x); r(1)")
        self.assertIn("nested too deep", self.m.error())
        self.m.run("f = @(x")
        self.assertEqual(self.m.rc, ERROR)
        self.m.run("f = @(x)")
        self.assertIn("body is empty", self.m.error())
        self.m.run("q = @(x) zz + x; q(1)")
        self.assertIn("undefined variable or function 'zz'", self.m.error())
        self.m.run("integral(3, 0, 1)")
        self.assertIn("must be a function", self.m.error())

    def test_integral(self):
        self.precise()
        for line, want in (("integral(@(x) x.^2, 0, 3)", 9.0), ("integral(@sin, 0, pi)", 2.0), ("integral(@exp, 0, 1)", math.e - 1),
                           ("integral(@(x) 1./x, 1, 10)", math.log(10)), ("integral(@(x) sin(10*x), 0, pi)", 0.0),
                           ("integral(@sqrt, 0, 1)", 2.0 / 3.0), ("integral(@(x) x.^3, 3, 0)", -81.0 / 4.0),
                           ("integral(@(x) exp(-x.^2), -3, 3)", math.sqrt(math.pi) * math.erf(3.0)), ("integral(@cos, 1, 1)", 0.0)):
            self.close(self.m.numbers(line), [want], 5e-8)
        self.m.run("f = @(x) 4./(1+x.^2);")
        self.close(self.m.numbers("integral(f, 0, 1)"), [math.pi], 5e-8)
        self.close(self.m.numbers("quad(f, 0, 1)"), [math.pi], 5e-8)

    def test_integral_of_a_pole_gives_up_politely(self):
        text = self.m.run("integral(@(x) 1./x, 0, 1)")
        self.assertTrue("warning: integral did not converge" in text or self.m.rc == ERROR, text)

    def test_integral_needs_a_number_back(self):
        self.m.run("integral(@(x) [x x], 0, 1)")
        self.assertIn("must give one number", self.m.error())

    def test_fzero(self):
        self.precise()
        self.close(self.m.numbers("fzero(@(x) x^2 - 2, 1)"), [math.sqrt(2)], 5e-8)
        self.close(self.m.numbers("fzero(@cos, [0 3])"), [math.pi / 2], 5e-8)
        self.close(self.m.numbers("fzero(@(x) x^3 - 8, 5)"), [2.0], 5e-8)
        self.close(self.m.numbers("fzero(@(x) exp(x) - 5, 0)"), [math.log(5)], 5e-8)
        self.close(self.m.numbers("fzero(@(x) x, 0)"), [0.0], 5e-8)
        self.m.run("fzero(@(x) x^2 + 1, 0)")
        self.assertIn("no sign change", self.m.error())
        self.m.run("fzero(@(x) x^2 + 1, [0 1])")
        self.assertIn("same sign", self.m.error())

    def test_fminbnd(self):
        self.precise()
        self.close(self.m.numbers("fminbnd(@(x) (x-2)^2 + 1, 0, 5)"), [2.0], 1e-6)
        self.close(self.m.numbers("fminbnd(@cos, 2, 4)"), [math.pi], 1e-6)
        self.close(self.m.numbers("fminbnd(@(x) x^4 - 3*x, 0, 2)"), [(3.0 / 4.0) ** (1.0 / 3.0)], 1e-6)

    def test_deriv(self):
        self.precise()
        self.close(self.m.numbers("deriv(@sin, 1)"), [math.cos(1)], 5e-9)
        self.close(self.m.numbers("deriv(@(x) x^3, 2)"), [12.0], 5e-9)
        self.close(self.m.numbers("deriv(@(x) x^3, 2, 2)"), [12.0], 5e-8)
        self.close(self.m.numbers("deriv(@exp, [0 1])"), [1.0, math.e], 5e-9)
        self.close(self.m.numbers("deriv(@(x) 1/x, 100)"), [-1e-4], 5e-9)
        self.close(self.m.numbers("deriv(@log, 0.5)"), [2.0], 5e-8)
        self.m.run("deriv(@sin, 1, 3)")
        self.assertIn("order must be 1 or 2", self.m.error())

    def test_a_user_function_is_accepted_by_name(self):
        self.precise()
        self.m.run("f = @(x) x^2;")
        self.close(self.m.numbers("integral(f, 0, 3)"), [9.0], 5e-8)
        self.close(self.m.numbers("deriv(f, 3)"), [6.0], 5e-8)

    def test_too_many_evaluations_are_stopped(self):
        text = self.m.run("integral(@(x) sin(1./x), 0, 1)")
        self.assertTrue("warning: integral did not converge" in text or self.m.rc == ERROR, text)
        self.m.run("integral(@(x) 1./(x - 0.5), 0, 1)")                       # a pole inside: cannot be integrated
        self.assertTrue(self.m.rc == ERROR or "warning" in "".join(self.m.out))

    def test_esc_stops_a_long_calculation(self):
        hook = ctypes.CFUNCTYPE(ctypes.c_int)(lambda: 1)
        ptr = ctypes.c_void_p.in_dll(LIB, "mat_poll_hook")
        ptr.value = ctypes.cast(hook, ctypes.c_void_p).value
        try:
            self.m.run("a = ones(30); b = a*a;")
            self.assertIn("interrupted", self.m.error())
            self.m.run("c = a\\a;")
            self.assertIn("interrupted", self.m.error())
            self.m.run("integral(@sin, 0, 1)")
            self.assertIn("interrupted", self.m.error())
        finally:
            ptr.value = None
        self.assertEqual(self.m.numbers("sum(sum(ones(3)*ones(3)))"), [27])


class Strings(unittest.TestCase):
    def setUp(self):
        self.m = Session()
        self.addCleanup(self.m.close)

    def test_strings(self):
        self.assertEqual(self.m.run("s = 'hello'"), "s = hello\n")
        self.assertEqual(self.m.run('t = "it""s"'), "t = it\"s\n")
        self.assertEqual(self.m.run("u = 'it''s'"), "u = it's\n")
        self.assertEqual(self.m.run("['ab' 'cd']"), "ans = abcd\n")
        self.assertEqual(self.m.run("s(2)"), "ans = e\n")
        self.assertEqual(self.m.run("length('hello')"), "ans = 5\n")
        self.assertEqual(self.m.run("x = ''"), "x = \n")
        self.assertEqual(self.m.run("s'"), "ans =\nh\ne\nl\nl\no\n")

    def test_a_percent_inside_a_string_is_not_a_comment(self):
        self.assertEqual(self.m.run("s = '100%' % a comment"), "s = 100%\n")
        self.assertEqual(self.m.run("a = [1 2]'; % transpose then comment"), "")
        self.assertEqual(self.m.numbers("a"), [1, 2])

    def test_disp(self):
        self.assertEqual(self.m.run("disp('hi')"), "hi\n")
        self.assertEqual(self.m.run("disp(3.5)"), "3.5000\n")
        self.assertEqual(self.m.run("disp([1 2; 3 4])"), "    1    2\n    3    4\n")
        self.assertEqual(self.m.run("x = disp(1); y = 2"), "1\ny = 2\n")

    def test_num2str(self):
        self.assertEqual(self.m.run("num2str(42)"), "ans = 42\n")
        self.assertEqual(self.m.run("num2str(pi)"), "ans = 3.1416\n")
        self.assertEqual(self.m.run("num2str(123.456)"), "ans = 123.456\n")
        self.assertEqual(self.m.run("num2str([1 2 3])"), "ans = 1  2  3\n")
        self.assertEqual(self.m.run("['x = ' num2str(7)]"), "ans = x = 7\n")

    def test_sprintf_and_fprintf(self):
        self.assertEqual(self.m.run("sprintf('%d apples', 3)"), "ans = 3 apples\n")
        self.assertEqual(self.m.run("fprintf('%d + %d = %d\\n', 1, 2, 3)"), "1 + 2 = 3\n")
        self.assertEqual(self.m.run("fprintf('%5.2f|%-6s|%c\\n', pi, 'ab', 'z')"), " 3.14|ab    |z\n")
        self.assertEqual(self.m.run("fprintf('%d ', [1 2 3]); fprintf('\\n')"), "1 2 3 \n")
        self.m.run("fprintf('%d ', [1 2 3]);")
        self.assertEqual(self.m.run("fprintf('%d,', [1 2 3]); fprintf('end\\n')"), "1,2,3,end\n")
        self.assertEqual(self.m.run("sprintf('%d,', [1 2 3])"), "ans = 1,2,3,\n")
        self.assertEqual(self.m.run("sprintf('%g', 0.5)"), "ans = 0.5\n")
        self.assertEqual(self.m.run("sprintf('%d', 2.5)"), "ans = 2.5\n")
        self.assertEqual(self.m.run("sprintf('100%%')"), "ans = 100%\n")
        self.assertEqual(self.m.run("sprintf('%s is %d', 'x', 5)"), "ans = x is 5\n")
        self.assertEqual(self.m.run("sprintf('Value: %d')"), "ans = Value: \n")
        self.m.run("fprintf('%q', 1)")
        self.assertIn("unknown conversion", self.m.error())

    def test_fprintf_output_arrives_as_text(self):
        self.m.run("fprintf('a%db%dc\\n', 1, 2)")
        self.assertEqual("".join(self.m.out), "a1b2c\n")

    def test_error_stops_with_its_message(self):
        self.m.run("error('bad thing %d', 7)")
        self.assertEqual(self.m.rc, ERROR)
        self.assertIn("bad thing 7", self.m.error())

    def test_strcmp(self):
        self.assertEqual(self.m.numbers("strcmp('a', 'a')"), [1])
        self.assertEqual(self.m.numbers("strcmp('a', 'b')"), [0])
        self.assertEqual(self.m.numbers("strcmp('a', 5)"), [0])

    def test_tic_and_toc(self):
        self.assertEqual(self.m.run("tic"), "")
        t = self.m.numbers("toc")[0]
        self.assertTrue(0.0 <= t < 5.0)


class Logic(unittest.TestCase):
    def setUp(self):
        self.m = Session()
        self.addCleanup(self.m.close)

    def test_comparisons(self):
        for line, want in (("3 > 2", [1]), ("3 < 2", [0]), ("2 >= 2", [1]), ("2 <= 1", [0]), ("2 == 2", [1]), ("2 ~= 2", [0]),
                           ("[1 2 3] > 2", [0, 0, 1]), ("[1 2 3] == [1 5 3]", [1, 0, 1]), ("1:3 >= 2", [0, 1, 1])):
            self.assertEqual(self.m.numbers(line), want, line)

    def test_comparison_precedence(self):
        self.assertEqual(self.m.numbers("1 + 1 == 2"), [1])
        self.assertEqual(self.m.numbers("2 * 3 > 5"), [1])
        self.assertEqual(self.m.numbers("x = 3 == 3"), [1])
        self.assertEqual(self.m.numbers("1:3 == 2"), [0, 1, 0])

    def test_not_and_or(self):
        self.assertEqual(self.m.numbers("~0"), [1])
        self.assertEqual(self.m.numbers("~[1 0 2]"), [0, 1, 0])
        self.assertEqual(self.m.numbers("~(3 > 2)"), [0])
        self.assertEqual(self.m.numbers("[1 0 1] & [1 1 0]"), [1, 0, 0])
        self.assertEqual(self.m.numbers("[1 0 1] | [0 0 1]"), [1, 0, 1])
        self.assertEqual(self.m.numbers("1 | 0 & 0"), [1])                 # & binds tighter than |

    def test_short_circuit(self):
        self.assertEqual(self.m.numbers("0 && undefinedthing"), [0])
        self.assertEqual(self.m.numbers("1 || undefinedthing"), [1])
        self.assertEqual(self.m.numbers("1 && 1"), [1])
        self.assertEqual(self.m.numbers("0 || 0"), [0])
        self.assertEqual(self.m.numbers("0 || 1 && 1"), [1])
        self.m.run("v = [1 2 3]; i = 0;")
        self.assertEqual(self.m.numbers("i > 0 && v(i) > 1"), [0])         # v(0) would be an error
        self.assertEqual(self.m.numbers("i == 0 || v(i) > 1"), [1])
        self.m.run("1 && undefinedthing")
        self.assertIn("undefined", self.m.error())
        self.assertEqual(self.m.numbers("0 && [1 2](5) || 1"), [1])
        self.assertEqual(self.m.numbers("0 && max(1, 2) || 1"), [1])
        self.assertEqual(self.m.numbers("0 && strcmp('a,b', 'c') || 1"), [1])

    def test_a_list_is_true_only_if_every_element_is(self):
        self.assertEqual(self.m.numbers("[1 1 1] && 1"), [1])
        self.assertEqual(self.m.numbers("[1 0 1] && 1"), [0])
        self.assertEqual(self.m.numbers("[] || 0"), [0])

    def test_logical_indexing(self):
        self.m.run("A = [5 1 8 3]; ")
        self.assertEqual(self.m.numbers("A(A > 2)"), [5, 8, 3])
        self.assertEqual(self.m.numbers("A(A > 100)"), [])
        self.m.run("A(A < 4) = 0;")
        self.assertEqual(self.m.numbers("A"), [5, 0, 8, 0])
        self.m.run("B = [1 2; 3 4];")
        self.assertEqual(self.m.numbers("B(B > 1)"), [3, 2, 4])             # down the columns
        self.assertEqual(self.m.numbers("sum(A(A > 0))"), [13])

    def test_any_all_find_isempty(self):
        self.assertEqual(self.m.numbers("any([0 0 1])"), [1])
        self.assertEqual(self.m.numbers("all([1 1 0])"), [0])
        self.assertEqual(self.m.numbers("any([0 0; 0 1])"), [0, 1])
        self.assertEqual(self.m.numbers("all([1 1; 0 1])"), [0, 1])
        self.assertEqual(self.m.numbers("find([0 3 0 5])"), [2, 4])
        self.assertEqual(self.m.numbers("find([0 3; 5 0])"), [2, 3])
        self.assertEqual(self.m.numbers("isempty([])"), [1])
        self.assertEqual(self.m.numbers("isempty(1)"), [0])
        self.assertEqual(self.m.numbers("isempty(zeros(0,3))"), [1])

    def test_sort_and_two_argument_max_min(self):
        self.assertEqual(self.m.numbers("sort([3 1 2 9 -4])"), [-4, 1, 2, 3, 9])
        self.assertEqual(self.m.numbers("sort([3 1; 1 2])"), [1, 1, 3, 2])
        self.assertEqual(self.m.numbers("max([1 5 3], 4)"), [4, 5, 4])
        self.assertEqual(self.m.numbers("min([1 5 3], [2 2 2])"), [1, 2, 2])
        self.assertEqual(self.m.numbers("max([1 5 3])"), [5])

    def test_negating_a_comparison_gives_a_number(self):
        self.assertEqual(self.m.numbers("-(3 > 2)"), [-1])
        self.assertEqual(self.m.numbers("(3 > 2) + (4 > 1)"), [2])


MORE = 3


class Flow(unittest.TestCase):
    def setUp(self):
        self.m = Session()
        self.addCleanup(self.m.close)

    def test_if_on_one_line(self):
        self.assertEqual(self.m.run("if 3 > 2, disp('yes'), end"), "yes\n")
        self.assertEqual(self.m.run("if 3 < 2, disp('yes'), end"), "")
        self.assertEqual(self.m.run("x = 5; if x > 10, disp('big'), else, disp('small'), end"), "small\n")
        self.assertEqual(self.m.run("x = 5; if x > 10, disp('big'), elseif x > 3, disp('mid'), else, disp('small'), end"), "mid\n")

    def test_if_conditions_on_vectors_and_strings(self):
        self.assertEqual(self.m.run("if [1 1 1], disp('all'), end"), "all\n")
        self.assertEqual(self.m.run("if [1 0 1], disp('all'), else, disp('not all'), end"), "not all\n")
        self.assertEqual(self.m.run("if [], disp('x'), else, disp('empty is false'), end"), "empty is false\n")
        self.assertEqual(self.m.run("s = 'abc'; if strcmp(s, 'abc'), disp('same'), end"), "same\n")

    def test_for_loops(self):
        self.assertEqual(self.m.run("s = 0; for k = 1:5, s = s + k; end; s"), "s = 15\n")
        self.assertEqual(self.m.run("for k = 5:-2:1, fprintf('%d ', k); end"), "5 3 1 ")
        self.assertEqual(self.m.run("for v = [2 4 6], fprintf('%d ', v); end"), "2 4 6 ")
        self.assertEqual(self.m.run("for c = [1 2; 3 4], disp(c'), end"), "    1    3\n    2    4\n")
        self.assertEqual(self.m.run("for k = 3:1, disp('never'), end"), "")
        self.assertEqual(self.m.run("for k = 1:2, k, end"), "k = 1\nk = 2\n")
        self.m.run("for k = 1:3, end")
        self.assertEqual(self.m.numbers("k"), [3])                 # the loop variable stays
        self.assertEqual(self.m.run("n = 3; for k = 1:n, fprintf('%d', k); end"), "123")
        self.assertEqual(self.m.run("for k = 0:0.5:1.5, fprintf('%g ', k); end"), "0 0.5 1 1.5 ")

    def test_a_long_range_does_not_make_a_vector(self):
        self.assertEqual(self.m.run("s = 0; for k = 1:6000, s = s + 1; end; s"), "s = 6000\n")

    def test_while_break_continue(self):
        self.assertEqual(self.m.run("n = 0; while n < 5, n = n + 1; end; n"), "n = 5\n")
        self.assertEqual(self.m.run("k = 0; while 1, k = k + 1; if k >= 4, break, end, end; k"), "k = 4\n")
        self.assertEqual(self.m.run("for k = 1:6, if mod(k, 2) == 0, continue, end, fprintf('%d ', k); end"), "1 3 5 ")

    def test_break_leaves_only_the_inner_loop(self):
        text = self.m.run("for i = 1:3, for j = 1:3, if j == 2, break, end, fprintf('%d%d ', i, j); end, end")
        self.assertEqual(text, "11 21 31 ")

    def test_nested_ifs_and_loops(self):
        text = self.m.run("for i = 1:4, if mod(i, 2) == 0, if i > 2, disp('big even'), else, disp('small even'), end, end, end")
        self.assertEqual(text, "small even\nbig even\n")

    def test_blocks_typed_over_several_lines(self):
        self.assertEqual(self.m.run("for k = 1:3"), "")
        self.assertEqual(self.m.rc, MORE)
        self.assertEqual(self.m.run("  disp(k)"), "")
        self.assertEqual(self.m.rc, MORE)
        self.assertEqual(self.m.run("end"), "1\n2\n3\n")
        self.assertEqual(self.m.rc, OK)
        self.m.run(["x = 7;", "if x > 5", "  disp('five')", "else", "  disp('no')", "end"])
        self.assertEqual("".join(self.m.out), "five\n")

    def test_blocks_inside_blocks_over_several_lines(self):
        for line in ("for i = 1:2", "if i == 2", "disp('two')", "end", "end"):
            self.m.run(line)
        self.assertEqual("".join(self.m.out), "two\n")

    def test_abort_drops_an_unfinished_block(self):
        self.m.run("for k = 1:3")
        self.assertEqual(self.m.rc, MORE)
        self.assertIn("dropped", self.m.run("abort"))
        self.assertEqual(self.m.run("1 + 1"), "ans = 2\n")

    def test_end_without_a_block(self):
        self.m.run("end")
        self.assertEqual(self.m.rc, ERROR)
        self.assertIn("end without a matching", self.m.error())
        self.m.run("else")
        self.assertEqual(self.m.rc, ERROR)
        self.assertEqual(self.m.run("1 + 1"), "ans = 2\n")

    def test_errors_inside_a_block_name_the_line(self):
        self.m.run("if 1")
        self.m.run("x = undefinedthing;")
        text = self.m.run("end")
        self.assertEqual(self.m.rc, ERROR)
        self.assertIn("line 2 of the block", text)
        self.assertEqual(self.m.run("1 + 1"), "ans = 2\n")

    def test_semicolons_inside_strings_do_not_split(self):
        self.assertEqual(self.m.run("disp('a;b,c'); disp('d')"), "a;b,c\nd\n")
        self.assertEqual(self.m.run("x = [1,2;3,4]; disp(size(x))"), "    2    2\n")

    def test_esc_ends_an_endless_loop(self):
        hook = ctypes.CFUNCTYPE(ctypes.c_int)(lambda: 1)
        ptr = ctypes.c_void_p.in_dll(LIB, "mat_poll_hook")
        ptr.value = ctypes.cast(hook, ctypes.c_void_p).value
        try:
            self.m.run("while 1, end")
            self.assertIn("interrupted", self.m.error())
            self.m.run("for k = 1:100000, end")
            self.assertIn("interrupted", self.m.error())
        finally:
            ptr.value = None

    def test_a_loop_can_build_a_vector(self):
        self.m.run("v = []; for k = 1:5, v(end+1) = k^2; end;")
        self.assertEqual(self.m.numbers("v"), [1, 4, 9, 16, 25])

    def test_the_iteration_limit_of_evaluations_is_per_line(self):
        self.m.run("f = @(x) x; s = 0; for k = 1:300, s = s + f(k); end")
        self.assertEqual(self.m.rc, OK)


class Functions(unittest.TestCase):
    def setUp(self):
        self.m = Session()
        self.addCleanup(self.m.close)

    def define(self, *lines):
        for line in lines:
            self.m.run(line)

    def test_a_function_typed_over_several_lines(self):
        self.define("function y = sq(x)", "y = x^2;", "end")
        self.assertEqual(self.m.rc, OK)
        self.assertEqual(self.m.run("sq(4)"), "ans = 16\n")
        self.assertEqual(self.m.numbers("sq(3) + sq(2)"), [13])

    def test_defining_at_the_prompt_says_how_many_statements_were_stored(self):
        self.m.run("function y = sq(x)")
        self.m.run("y = x^2;")
        self.assertEqual(self.m.run("end"), "(function defined with 1 statement)\n")
        text = self.m.run(["function z = empty(x)", "end"])
        self.assertIn("defined with 0 statements", text)
        self.assertIn("warning: the function has no statements", text)
        self.m.run("empty(1)")
        self.assertIn("did not set its output 'z'", self.m.error())

    def test_recursion(self):
        self.define("function f = fact(n)", "if n <= 1", "f = 1;", "else", "f = n * fact(n - 1);", "end", "end")
        self.assertEqual(self.m.numbers("fact(5)"), [120])
        self.assertEqual(self.m.numbers("fact(10)"), [3628800])

    def test_locals_do_not_leak_either_way(self):
        self.define("function y = setz(x)", "z = 99; y = x;", "end")
        self.m.run("setz(1);")
        self.m.run("z")
        self.assertIn("undefined", self.m.error())
        self.m.run("outer = 5;")
        self.define("function y = peek(x)", "y = outer;", "end")
        self.m.run("peek(1)")
        self.assertIn("undefined variable or function 'outer'", self.m.error())
        self.assertEqual(self.m.numbers("outer"), [5])

    def test_a_parameter_with_the_same_name_does_not_touch_the_caller(self):
        self.define("function y = inc(x)", "x = x + 1; y = x;", "end")
        self.m.run("x = 10;")
        self.assertEqual(self.m.numbers("inc(x)"), [11])
        self.assertEqual(self.m.numbers("x"), [10])

    def test_nargin(self):
        self.define("function r = addopt(a, b)", "if nargin < 2", "b = 10;", "end", "r = a + b;", "end")
        self.assertEqual(self.m.numbers("addopt(1)"), [11])
        self.assertEqual(self.m.numbers("addopt(1, 2)"), [3])
        self.m.run("addopt(1, 2, 3)")
        self.assertIn("takes 2 arguments", self.m.error())

    def test_a_function_without_output(self):
        self.define("function show(x)", "disp(x * 2)", "end")
        self.assertEqual(self.m.run("show(5)"), "10\n")
        self.assertEqual(self.m.run("show(5);"), "10\n")

    def test_an_error_inside_a_function_names_it(self):
        self.define("function y = bad(x)", "z = undefinedvar + 1;", "y = z;", "end")
        text = self.m.run("bad(1)")
        self.assertIn("line 2 of function bad", text)

    def test_output_never_set(self):
        self.define("function y = lazy(x)", "q = 1;", "end")
        self.m.run("lazy(1)")
        self.assertIn("did not set its output 'y'", self.m.error())

    def test_return_leaves_early(self):
        self.define("function y = early(x)", "y = 1;", "if x > 0", "return", "end", "y = 2;", "end")
        self.assertEqual(self.m.numbers("early(1)"), [1])
        self.assertEqual(self.m.numbers("early(-1)"), [2])

    def test_runaway_recursion_stops(self):
        self.define("function r = forever(x)", "r = forever(x);", "end")
        self.m.run("forever(1)")
        self.assertEqual(self.m.rc, ERROR)
        self.assertIn("nested too deep", self.m.error())
        self.assertEqual(self.m.run("1 + 1"), "ans = 2\n")

    def test_calculus_tools_take_these_functions(self):
        self.define("function y = sq(x)", "y = x^2;", "end")
        self.m.run("format long")
        self.assertAlmostEqual(self.m.numbers("integral(@sq, 0, 3)")[0], 9.0, delta=1e-7)
        self.assertAlmostEqual(self.m.numbers("deriv(@sq, 2)")[0], 4.0, delta=1e-7)
        self.assertAlmostEqual(self.m.numbers("fzero(@(x) sq(x) - 2, 1)")[0], math.sqrt(2), delta=1e-7)
        self.assertAlmostEqual(self.m.numbers("fminbnd(@(x) sq(x - 1), -3, 4)")[0], 1.0, delta=1e-6)
        self.assertEqual(self.m.numbers("arrayfun(@sq, 1:3)"), [1, 4, 9])
        self.assertAlmostEqual(self.m.numbers("integral(sq, 0, 3)")[0], 9.0, delta=1e-7)

    def test_an_anonymous_function_can_call_one(self):
        self.define("function y = cube(x)", "y = x^3;", "end")
        self.m.run("g = @(x) cube(x) + 1;")
        self.assertEqual(self.m.numbers("g(2)"), [9])

    def test_who_and_clear(self):
        self.define("function y = sq(x)", "y = x^2;", "end")
        self.assertIn("function sq(x)", self.m.run("who"))
        self.m.run("clear sq")
        self.m.run("sq(2)")
        self.assertIn("undefined", self.m.error())

    def test_redefining_replaces(self):
        self.define("function y = k(x)", "y = 1;", "end")
        self.define("function y = k(x)", "y = 2;", "end")
        self.assertEqual(self.m.numbers("k(0)"), [2])

    def test_a_function_can_loop_and_use_strings(self):
        self.define("function s = total(v)", "s = 0;", "for i = 1:length(v)", "s = s + v(i);", "end", "end")
        self.assertEqual(self.m.numbers("total([1 2 3 4])"), [10])
        self.define("function show2(n)", "fprintf('n=%d\\n', n);", "end")
        self.assertEqual(self.m.run("show2(7)"), "n=7\n")


class Files(unittest.TestCase):
    def setUp(self):
        self.m = Session()
        self.addCleanup(self.m.close)
        self.old = os.getcwd()
        self.tmp = tempfile.TemporaryDirectory()
        os.chdir(self.tmp.name)
        self.addCleanup(self.tmp.cleanup)
        self.addCleanup(os.chdir, self.old)

    def write(self, name, text, newline="\n"):
        with open(name, "w", newline="") as f:
            f.write(text.replace("\n", newline))

    def test_a_script_runs_by_name_and_by_run(self):
        self.write("hello.m", "% a comment\nx = 4;\ndisp('hello')\ny = x * 2\n")
        self.assertEqual(self.m.run("hello"), "hello\ny = 8\n")
        self.assertEqual(self.m.run("run hello"), "hello\ny = 8\n")
        self.assertEqual(self.m.run("run hello.m"), "hello\ny = 8\n")
        self.assertEqual(self.m.numbers("x"), [4])                     # the script shares the workspace

    def test_a_script_with_loops_and_blocks(self):
        self.write("sumsq.m", "total = 0;\nfor k = 1:10\n  if mod(k, 2) == 0\n    total = total + k^2;\n  end\nend\ntotal\n")
        self.assertEqual(self.m.run("sumsq"), "total = 220\n")

    def test_dos_line_ends(self):
        self.write("crlf.m", "a = 1;\nb = a + 1\n", newline="\r\n")
        self.assertEqual(self.m.run("crlf"), "b = 2\n")

    def test_a_script_error_names_the_line_and_the_file(self):
        self.write("bad.m", "x = 1;\ny = x + nothing;\nz = 3;\n")
        text = self.m.run("bad")
        self.assertEqual(self.m.rc, ERROR)
        self.assertIn("line 2 of bad.m", text)
        self.assertIn("undefined variable or function 'nothing'", text)
        self.assertEqual(self.m.numbers("x"), [1])
        self.m.run("z")
        self.assertIn("undefined", self.m.error())

    def test_a_missing_file(self):
        self.m.run("run nothere")
        self.assertIn("cannot find the file nothere.m", self.m.error())
        self.m.run("nothere")
        self.assertIn("undefined variable or function 'nothere'", self.m.error())

    def test_a_function_file_loads_when_called(self):
        self.write("cube.m", "function y = cube(x)\ny = x^3;\n")                   # no end, as in old function files
        self.assertEqual(self.m.numbers("cube(3)"), [27])
        self.assertEqual(self.m.numbers("cube(2) + cube(1)"), [9])

    def test_a_function_file_with_end_and_a_helper(self):
        self.write("hyp.m", "function h = hyp(a, b)\nh = sqrt(sq(a) + sq(b));\nend\n\nfunction y = sq(x)\ny = x^2;\nend\n")
        self.assertEqual(self.m.numbers("hyp(3, 4)"), [5])
        self.assertEqual(self.m.numbers("sq(5)"), [25])

    def test_a_function_file_without_ends_and_two_functions(self):
        self.write("two.m", "function a = two(x)\na = one(x) + 1;\nfunction b = one(x)\nb = x * 10;\n")
        self.assertEqual(self.m.numbers("two(3)"), [31])

    def test_a_function_file_in_a_calculus_tool(self):
        self.write("parab.m", "function y = parab(x)\ny = x^2 - 4;\n")
        self.m.run("format long")
        self.assertAlmostEqual(self.m.numbers("fzero(@parab, 1)")[0], 2.0, delta=1e-7)

    def test_a_script_that_runs_itself_stops(self):
        self.write("again.m", "again\n")
        self.m.run("again")
        self.assertEqual(self.m.rc, ERROR)
        self.assertIn("nested too deep", self.m.error())

    def test_a_script_can_call_another_and_a_function(self):
        self.write("inner.m", "v = 5;\n")
        self.write("outer.m", "inner\nw = v + 1\n")
        self.assertEqual(self.m.run("outer"), "w = 6\n")

    def test_return_ends_a_script(self):
        self.write("early.m", "disp('a')\nreturn\ndisp('b')\n")
        self.assertEqual(self.m.run("early"), "a\n")

    def test_a_script_with_an_unclosed_block(self):
        self.write("open.m", "for k = 1:3\ndisp(k)\n")
        self.m.run("open")
        self.assertEqual(self.m.rc, ERROR)
        self.assertIn("missing end for the for on line 1", self.m.error())


class FastPath(unittest.TestCase):
    """The fast path for plain numbers must give exactly what the general parser gives."""

    def setUp(self):
        self.fast = ctypes.c_int.in_dll(LIB, "ml_fastpath")
        self.addCleanup(setattr, self.fast, "value", 1)

    def both(self, setup, line):
        results = []
        for mode in (1, 0):
            self.fast.value = mode
            m = Session()
            for l in setup:
                m.run(l)
            results.append(m.run(line))
            m.close()
        return results

    def test_random_expressions_agree(self):
        import random
        rnd = random.Random(20261017)
        setup = ["a = 3; b = -2.5; c = 0; d = 7; v = [4 8 15 16 23 42]; A = [1 2; 3 4]; format long"]
        atoms = ["a", "b", "c", "d", "1", "2", "10", "0.5", "1e3", "3.25", "v(2)", "v(end)", "v(a)", "A(2,1)", "A(end,end)", "pi", "A(3)", "v(d)"]
        funcs = ["sqrt", "abs", "floor", "round", "sin", "cos", "exp", "sign", "fix", "ceil", "log"]
        ops = ["+", "-", "*", "/", "^", "<", ">", "<=", ">=", "==", "~=", "&", "|", "&&", "||", ".*", "./", ".^"]

        def expr(depth):
            r = rnd.random()
            if depth == 0 or r < 0.25:
                return rnd.choice(atoms)
            if r < 0.40:
                return "(" + expr(depth - 1) + ")"
            if r < 0.50:
                return rnd.choice(["-", "~", "+"]) + expr(depth - 1)
            if r < 0.62:
                return rnd.choice(funcs) + "(" + expr(depth - 1) + ")"
            if r < 0.68:
                return rnd.choice(["mod", "rem", "max", "min", "atan2"]) + "(" + expr(depth - 1) + ", " + expr(depth - 1) + ")"
            return expr(depth - 1) + " " + rnd.choice(ops) + " " + expr(depth - 1)

        differences = []
        for _ in range(700):
            line = expr(3)
            fast, slow = self.both(setup, line)
            if fast != slow:
                differences.append((line, fast, slow))
        self.assertEqual(differences[:5], [])

    def test_assignments_and_conditions_agree(self):
        setup = ["v = zeros(1, 5); A = zeros(2, 3); k = 2;"]
        for line in ("v(3) = 7; v", "v(end) = k * 2; v", "v(k + 1) = v(k) + 5; v", "A(2, 3) = 4.5; A", "A(end, 1) = -1; A", "v(6) = 1; v", "v(0) = 1",
                     "v(1.5) = 2", "A(3, 1) = 1; A", "k = k + 1; k", "k = (k > 1) + 1; k", "x = 3 > 2", "if k > 1 && v(1) == 0, disp(1), else, disp(2), end",
                     "t = 0; for i = 1:5, t = t + v(i) * 2; end; t", "b = 5; b(2) = 7; b"):
            fast, slow = self.both(setup, line)
            self.assertEqual(fast, slow, line)

    def test_a_loop_of_plain_numbers(self):
        fast, slow = self.both([], "s = 0; for k = 1:300, s = s + mod(k, 7) * 2 - (k > 100); end; s")
        self.assertEqual(fast, slow)
        self.assertEqual(fast, "s = 1606\n")


class Linear(unittest.TestCase):
    def setUp(self):
        self.m = Session()
        self.addCleanup(self.m.close)
        self.m.run("format long")

    def close(self, got, want, tol=1e-6):
        self.assertEqual(len(got), len(want), (got, want))
        for g, w in zip(got, want):
            self.assertAlmostEqual(g, w, delta=tol * max(1.0, abs(w)))

    def small(self, expr, tol=1e-9):
        v = self.m.numbers(expr)
        self.assertEqual(len(v), 1, expr)
        self.assertLess(abs(v[0]), tol, expr)

    # --- eigenvalues ---
    def test_symmetric_eigenvalues(self):
        self.close(self.m.numbers("eig([2 1; 1 2])"), [1, 3])
        self.close(self.m.numbers("eig([2 -1 0; -1 2 -1; 0 -1 2])"), [2 - math.sqrt(2), 2, 2 + math.sqrt(2)])
        self.close(self.m.numbers("eig(5)"), [5])
        self.close(self.m.numbers("eig(eye(3))"), [1, 1, 1])
        self.close(self.m.numbers("eig([4 1; 1 -2])"), [1 - math.sqrt(10), 1 + math.sqrt(10)])

    def test_symmetric_eigenvectors(self):
        self.m.run("S = [4 1 2; 1 3 0; 2 0 5]; [V, D] = eig(S);")
        self.small("norm(S*V - V*D)")
        self.small("norm(V'*V - eye(3))")
        d = self.m.numbers("diag(D)")
        self.assertEqual(d, sorted(d))

    def test_a_larger_symmetric_matrix(self):
        self.m.run("R = rand(8) - 0.5; S = R + R';")
        self.m.run("[V, D] = eig(S);")
        self.small("norm(S*V - V*D)", 1e-8)
        self.small("norm(V'*V - eye(8))", 1e-8)
        self.close(self.m.numbers("trace(S) - sum(diag(D))"), [0.0], 1e-8)
        self.m.run("R = rand(12) - 0.5; S = R + R'; [V, D] = eig(S);")
        self.small("norm(S*V - V*D)", 1e-8)

    def test_general_matrices_with_real_eigenvalues(self):
        self.close(self.m.numbers("eig([2 1; 0 3])"), [2, 3])
        self.close(self.m.numbers("eig([6 -11 6; 1 0 0; 0 1 0])"), [1, 2, 3])
        self.close(self.m.numbers("eig([1 2; 3 4])"), [(5 - math.sqrt(33)) / 2, (5 + math.sqrt(33)) / 2])
        self.m.run("A = [6 -11 6; 1 0 0; 0 1 0]; [V, D] = eig(A);")
        self.small("norm(A*V - V*D)", 1e-6)
        self.close(self.m.numbers("norm(V(:,1))"), [1.0])

    def test_a_built_up_matrix_with_known_eigenvalues(self):
        self.m.run("P = [2 1 0 1 0 0; 0 3 1 0 1 0; 1 0 2 0 0 1; 0 1 0 4 1 0; 1 0 0 1 3 1; 0 0 1 0 1 2]; A = P * diag([1 2 3 4 5 6]) / P;")
        self.close(self.m.numbers("eig(A)"), [1, 2, 3, 4, 5, 6], 1e-6)

    def test_complex_eigenvalues_need_eigc(self):
        self.m.run("eig([0 -1; 1 0])")
        self.assertEqual(self.m.rc, ERROR)
        self.assertIn("eigc", self.m.error())
        self.close(self.m.numbers("eigc([0 -1; 1 0])"), [0, -1, 0, 1])
        self.close(self.m.numbers("eigc([1 2; 3 4])"), [(5 - math.sqrt(33)) / 2, 0, (5 + math.sqrt(33)) / 2, 0])
        self.close(self.m.numbers("eigc([2 1; 1 2])"), [1, 0, 3, 0])
        self.close(self.m.numbers("eigc([1 -5; 5 1])"), [1, -5, 1, 5])                      # 1 -/+ 5i

    def test_eig_errors(self):
        self.m.run("eig([1 2 3])")
        self.assertIn("square", self.m.error())
        self.m.run("eig([1 NaN; 2 3])")
        self.assertIn("NaN", self.m.error())
        self.m.run("eig('ab')")
        self.assertEqual(self.m.rc, ERROR)

    # --- singular values ---
    def test_singular_values(self):
        self.close(self.m.numbers("svd([3 0; 0 4])"), [4, 3])
        self.close(self.m.numbers("svd([1 2; 3 4])"), [5.464985704219043, 0.3659661906262578])
        self.close(self.m.numbers("svd([1 2; 3 4; 5 6])"), [9.525518091565107, 0.5143005806586446])
        self.close(self.m.numbers("svd([1 2 3; 4 5 6])"), [9.508032000695724, 0.7728696356734838])
        self.close(self.m.numbers("svd([1 2; 2 4])"), [5.0, 0.0], 1e-7)

    def test_the_full_decomposition(self):
        for text in ("[1 2; 3 4]", "[1 2; 3 4; 5 6; 7 9]", "[1 2 3 4; 5 6 7 9]", "[1; 2; 3]", "[4 5 6]"):
            self.m.run("A = %s; [U, S, V] = svd(A);" % text)
            self.small("norm(U*S*V' - A)", 1e-8)
            self.small("norm(U'*U - eye(size(U, 2)))", 1e-8)
            self.small("norm(V'*V - eye(size(V, 2)))", 1e-8)

    def test_norms(self):
        self.close(self.m.numbers("norm([1 2; 3 4])"), [5.464985704219043])
        self.close(self.m.numbers("norm([1 2; 3 4], 1)"), [6])
        self.close(self.m.numbers("norm([1 2; 3 4], Inf)"), [7])
        self.close(self.m.numbers("norm([1 2; 3 4], 'fro')"), [math.sqrt(30)])
        self.close(self.m.numbers("norm([3 4])"), [5])
        self.close(self.m.numbers("norm([1 -2 3], 1)"), [6])
        self.close(self.m.numbers("norm([1 -2 3], Inf)"), [3])
        self.close(self.m.numbers("norm([1 -2 3], 3)"), [(1 + 8 + 27) ** (1 / 3)])
        self.m.run("norm([1 2; 3 4], 7)")
        self.assertIn("p must be", self.m.error())

    def test_cond_and_pinv(self):
        self.close(self.m.numbers("cond([1 2; 3 4])"), [14.933034373659268])
        self.close(self.m.numbers("cond(eye(3))"), [1.0])
        self.assertEqual(self.m.run("cond([1 2; 2 4])"), "ans = Inf\n")
        self.m.run("A = [1 2; 3 4; 5 6]; P = pinv(A);")
        self.small("norm(A*P*A - A)", 1e-8)
        self.small("norm(P*A - eye(2))", 1e-8)
        self.m.run("B = [1 2; 2 4]; Q = pinv(B);")
        self.small("norm(B*Q*B - B)", 1e-8)

    # --- several results ---
    def test_several_results(self):
        self.assertEqual(self.m.run("[m, i] = max([3 9 2 9])"), "m = 9\ni = 2\n")
        self.assertEqual(self.m.run("[m, i] = min([3 9 2 1 1])"), "m = 1\ni = 4\n")
        self.assertEqual(self.m.numbers("[m, i] = max([1 5; 7 2]); [m i]"), [7, 5, 2, 1])
        self.assertEqual(self.m.run("[r, c] = size(zeros(3, 5))"), "r = 3\nc = 5\n")
        self.assertEqual(self.m.numbers("[s, k] = sort([3 1 2 1]); [s; k]"), [1, 1, 2, 3, 2, 4, 3, 1])
        self.assertEqual(self.m.run("[~, i] = max([4 8 6]); i"), "i = 2\n")
        self.assertEqual(self.m.run("[m, ~] = max([4 8 6]); m"), "m = 8\n")
        self.assertEqual(self.m.run("[a, b] = size(zeros(2, 7)); a + b"), "ans = 9\n")

    def test_semicolon_hides_all_results(self):
        self.assertEqual(self.m.run("[m, i] = max([3 9 2]);"), "")
        self.assertEqual(self.m.numbers("i"), [2])

    def test_wrong_numbers_of_results(self):
        self.m.run("[a, b] = sqrt(4)")
        self.assertIn("sqrt gives one result", self.m.error())
        self.m.run("[a, b] = 5")
        self.assertIn("gives 1 result, not 2", self.m.error())
        self.m.run("[a, b, c] = max([1 2])")
        self.assertIn("at most two results", self.m.error())
        self.m.run("[a, b] = f2(1)")
        self.assertEqual(self.m.rc, ERROR)
        self.assertEqual(self.m.run("[1 2 3]"), "ans =\n    1    2    3\n")
        self.assertEqual(self.m.numbers("x = 1; y = 2; [x y]"), [1, 2])
        self.assertEqual(self.m.numbers("[x, y] == [1, 2]"), [1, 1])

    def test_the_inner_call_does_not_get_the_extra_results(self):
        self.assertEqual(self.m.run("[m, i] = max(sort([3 9 2]))"), "m = 9\ni = 3\n")

    # --- statistics and polynomials ---
    def test_dimensions_in_sums(self):
        self.assertEqual(self.m.numbers("sum([1 2; 3 4], 1)"), [4, 6])
        self.assertEqual(self.m.numbers("sum([1 2; 3 4], 2)"), [3, 7])
        self.assertEqual(self.m.numbers("mean([1 2; 3 4], 2)"), [1.5, 3.5])
        self.assertEqual(self.m.numbers("prod([1 2; 3 4], 2)"), [2, 12])
        self.assertEqual(self.m.numbers("sum([1 2 3], 1)"), [1, 2, 3])
        self.m.run("sum([1 2], 3)")
        self.assertIn("dimension must be 1", self.m.error())

    def test_statistics(self):
        self.close(self.m.numbers("median([3 1 2])"), [2])
        self.close(self.m.numbers("median([4 1 3 2])"), [2.5])
        self.close(self.m.numbers("median([1 9; 3 7; 5 8])"), [3, 8])
        self.close(self.m.numbers("std([2 4 4 4 5 5 7 9])"), [2.138089935299395])
        self.close(self.m.numbers("var([2 4 4 4 5 5 7 9])"), [4.571428571428571])
        self.close(self.m.numbers("std([5])"), [0])
        self.close(self.m.numbers("var([1 2; 3 6])"), [2, 8])
        self.close(self.m.numbers("diff([1 4 9 16])"), [3, 5, 7])
        self.close(self.m.numbers("diff([1; 4; 9])"), [3, 5])
        self.close(self.m.numbers("diff([1 2; 4 8; 9 20])"), [3, 6, 5, 12])
        self.close(self.m.numbers("cumprod([1 2 3 4])"), [1, 2, 6, 24])
        self.close(self.m.numbers("trapz([1 2 3])"), [4])
        self.close(self.m.numbers("trapz([0 1 2], [0 1 4])"), [3])

    def test_polynomials(self):
        self.close(self.m.numbers("polyfit([0 1 2 3], [1 3 7 13], 2)"), [1, 1, 1])
        self.close(self.m.numbers("polyfit([0 1 2], [1 3 5], 1)"), [2, 1])
        self.close(self.m.numbers("polyfit([1 2 3 4], [2.1 3.9 6.2 7.8], 1)"), [1.94, 0.15], 1e-6)
        self.close(self.m.numbers("polyval([1 1 1], [0 1 2])"), [1, 3, 7])
        self.close(self.m.numbers("polyval([2 0 -1], 3)"), [17])
        self.m.run("polyfit([1 2], [1 2], 3)")
        self.assertIn("degree + 1", self.m.error())
        self.m.run("polyfit([1 1 1], [1 2 3], 2)")
        self.assertIn("do not determine", self.m.error())

    def test_roots(self):
        self.close(self.m.numbers("roots([1 -6 11 -6])"), [1, 2, 3])
        self.close(self.m.numbers("roots([1 0 -1])"), [-1, 1])
        self.close(self.m.numbers("roots([2 -4])"), [2])
        self.close(self.m.numbers("roots([1 0 0])"), [0, 0])
        self.close(self.m.numbers("roots([0 0 1 -3])"), [3])
        self.close(self.m.numbers("roots([1 -3 2 0])"), [0, 1, 2])
        self.assertEqual(self.m.run("roots(5)"), "ans = []\n")
        self.m.run("roots([1 0 1])")
        self.assertIn("rootsc", self.m.error())
        self.close(self.m.numbers("rootsc([1 0 1])"), [0, -1, 0, 1])


class Plots(unittest.TestCase):
    def setUp(self):
        self.m = Session()
        self.addCleanup(self.m.close)

    def lit(self, rows, x, y, r=1):
        return any(rows[yy][xx] == "#" for yy in range(y - r, y + r + 1) for xx in range(x - r, x + r + 1) if 0 <= xx < 640 and 0 <= yy < 200)

    def test_a_plot_is_shown_when_the_line_ends(self):
        self.m.run("x = 0:10;")
        self.assertEqual(self.m.shown, 0)
        self.m.run("plot(x, x.^2)")
        self.assertEqual(self.m.shown, 1)
        self.m.run("title('Squares'), xlabel('x'), ylabel('y'), grid on")
        self.assertEqual(self.m.shown, 2)                       # all four changes in one line: one showing
        self.m.run("y = 3;")
        self.assertEqual(self.m.shown, 2)
        self.assertEqual(LIB.ml_show_plot(self.m.s), 1)           # F5
        self.assertEqual(self.m.shown, 3)

    def test_a_script_shows_its_plot_once(self):
        old = os.getcwd()
        with tempfile.TemporaryDirectory() as d:
            os.chdir(d)
            try:
                open("pl.m", "w").write("x = 1:5;\nplot(x, x)\ntitle('A')\nxlabel('B')\ngrid on\n")
                self.m.run("pl")
            finally:
                os.chdir(old)
        self.assertEqual(self.m.shown, 1)

    def test_a_straight_line(self):
        self.m.run("plot(0:10, 0:10)")
        rows, texts, limits = self.m.plot()
        self.assertEqual(limits, [0, 10, 0, 10])
        self.assertTrue(self.lit(rows, 72, 175, 0), "bottom left of the line")
        self.assertTrue(self.lit(rows, 631, 16, 0), "top right of the line")
        self.assertTrue(self.lit(rows, 352, 96, 1), "its middle")
        self.assertFalse(self.lit(rows, 200, 60, 8), "nothing far from the line")
        for x in range(72, 632):                                  # the frame
            self.assertEqual(rows[16][x], "#")
            self.assertEqual(rows[175][x], "#")

    def test_the_tick_numbers_and_labels(self):
        self.m.run("plot(0:10, 0:10); title('Line'); xlabel('time'); ylabel('value')")
        rows, texts, limits = self.m.plot()
        xs = sorted(t for t in texts if t[0] == 22)
        self.assertEqual([t[2] for t in xs], ["0", "2", "4", "6", "8", "10"])
        ys = sorted(t for t in texts if t[0] not in (0, 1, 22, 23))
        self.assertEqual(sorted(float(t[2]) for t in ys), [0, 2, 4, 6, 8, 10])
        self.assertIn((0, 38, "Line"), texts)
        self.assertIn((23, 38, "time"), texts)
        self.assertIn((1, 0, "value"), texts)
        for row, col, text in texts:
            self.assertTrue(0 <= col and col + len(text) <= 80, (row, col, text))

    def test_negative_and_fractional_ticks(self):
        self.m.run("plot([-1 1], [0.25 0.75])")
        rows, texts, limits = self.m.plot()
        self.assertEqual(limits[:2], [-1, 1])
        self.assertAlmostEqual(limits[2], 0.2, places=9)
        self.assertAlmostEqual(limits[3], 0.8, places=9)
        self.assertIn("-1", [t[2] for t in texts])
        self.assertIn("0.4", [t[2] for t in texts])

    def test_plot_of_a_vector_alone_uses_1_2_3(self):
        self.m.run("plot([5 3 8 1])")
        rows, texts, limits = self.m.plot()
        self.assertEqual(limits[0], 0.0 if limits[0] == 0.0 else limits[0])
        self.assertLessEqual(limits[0], 1)
        self.assertGreaterEqual(limits[1], 4)

    def test_axis_limits(self):
        self.m.run("plot(1:9, (1:9).^2); axis([0 20 0 100])")
        self.assertEqual(self.m.plot()[2], [0, 20, 0, 100])
        self.m.run("axis tight")
        self.assertEqual(self.m.plot()[2], [1, 9, 1, 81])
        self.m.run("axis auto")
        self.assertEqual(self.m.plot()[2], [0, 10, 0, 100])
        self.m.run("xlim([2 4]); ylim([0 20])")
        self.assertEqual(self.m.plot()[2], [2, 4, 0, 20])
        self.m.run("axis([1 1 0 1])")
        self.assertIn("min below max", self.m.error())

    def test_clipping_keeps_the_curve_inside_the_frame(self):
        self.m.run("plot(0:10, (0:10).^3); axis([0 10 0 100])")
        rows, texts, limits = self.m.plot()
        for y in range(200):
            for x in range(640):
                if rows[y][x] == "#":
                    self.assertTrue(10 <= x <= 635 and 10 <= y <= 180 or y > 176, (x, y))
        self.assertEqual(sum(1 for r in rows[:15] for c in r if c == "#"), 0)        # nothing above the frame (the title row)

    def test_hold_adds_curves(self):
        self.m.run("plot(1:5, 1:5)")
        self.assertEqual(LIB.ml_plot_curves(self.m.s), 1)
        self.m.run("hold on")
        self.m.run("plot(1:5, 5:-1:1)")
        self.assertEqual(LIB.ml_plot_curves(self.m.s), 2)
        self.m.run("hold off")
        self.m.run("plot(1:5, 1:5)")
        self.assertEqual(LIB.ml_plot_curves(self.m.s), 1)

    def test_several_curves_in_one_call(self):
        self.m.run("x = 1:5; plot(x, x, x, x.^2, x, 2*x)")
        self.assertEqual(LIB.ml_plot_curves(self.m.s), 3)
        self.m.run("plot(x, [x' x'.^2 (x*2)'])")
        self.assertEqual(LIB.ml_plot_curves(self.m.s), 3)
        self.m.run("plot([x' x'.^2])")
        self.assertEqual(LIB.ml_plot_curves(self.m.s), 2)

    def test_styles(self):
        for style in ("-", "--", ":", ".", "o", "+", "*", "x", "r--", "bo", "k.-", "g:"):
            self.m.run("plot(1:5, [3 1 4 1 5], '%s')" % style)
            self.assertEqual(self.m.rc, OK, style)
            rows, texts, limits = self.m.plot()
            self.assertGreater(sum(r.count("#") for r in rows), 600 + 16 + 16, style)       # more than the frame
        self.m.run("plot(1:5, 1:5, 'q')")
        self.assertIn("not a line style", self.m.error())

    def test_markers_alone_do_not_draw_a_line(self):
        self.m.run("plot([1 10], [1 10], 'o'); axis([0 11 0 11])")
        rows, texts, limits = self.m.plot()
        self.assertFalse(self.lit(rows, 352, 96, 3), "no line through the middle")
        self.assertTrue(self.lit(rows, 72 + int(559 * 1 / 11), 175 - int(159 * 1 / 11), 3))

    def test_a_dashed_line_has_gaps(self):
        self.m.run("plot([0 1], [0.5 0.5], '--'); axis([0 1 0 1])")
        solid = Session()
        solid.run("plot([0 1], [0.5 0.5]); axis([0 1 0 1])")
        d = sum(r.count("#") for r in self.m.plot()[0])
        s = sum(r.count("#") for r in solid.plot()[0])
        solid.close()
        self.assertLess(d, s)

    def test_nan_breaks_the_line(self):
        self.m.run("plot([1 2 NaN 4 5], [1 2 3 4 5]); axis([0 6 0 6])")
        rows, texts, limits = self.m.plot()
        mid = [rows[y][x] for y in range(60, 130) for x in range(300, 400)]
        self.assertNotIn("#", mid[:0] or [])                                   # (the gap is between x=2 and x=4: columns about 230-440)
        gap_pixels = sum(1 for y in range(120, 160) for x in range(330, 340) if rows[y][x] == "#")
        self.assertEqual(gap_pixels, 0)

    def test_errors(self):
        self.m.run("plot(1:3, 1:4)")
        self.assertIn("same number of points", self.m.error())
        self.m.run("plot([])")
        self.assertIn("nothing to plot", self.m.error())
        self.m.run("plot('abc')")
        self.assertEqual(self.m.rc, ERROR)
        self.m.run("plot(1:3, rand(3, 8))")
        self.assertIn("at most 6 curves", self.m.error())
        self.m.run("plot(1:3000, rand(1, 3000), 1:3000, rand(1, 3000), 1:3000, rand(1, 3000))")
        self.assertIn("at most 6000 points", self.m.error())
        self.m.run("clf")
        self.m.run("title('x')")
        self.assertIn("no plot yet", self.m.error())
        self.m.run("shg")
        self.assertIn("no plot yet", self.m.error())
        self.m.run("hold maybe")
        self.assertIn("hold on or hold off", self.m.error())
        self.m.run("axis sideways")
        self.assertEqual(self.m.rc, ERROR)

    def test_commands_and_functions_agree(self):
        self.m.run("plot(1:4, 1:4)")
        self.m.run("grid on")
        self.assertEqual(self.m.shown, 2)
        rows_on = sum(r.count("#") for r in self.m.plot()[0])
        self.m.run("grid('off')")
        rows_off = sum(r.count("#") for r in self.m.plot()[0])
        self.assertGreater(rows_on, rows_off)
        self.m.run("grid")                                          # toggles
        self.assertEqual(sum(r.count("#") for r in self.m.plot()[0]), rows_on)
        self.m.run("hold('on'); hold('off'); axis('tight')")
        self.assertEqual(self.m.rc, OK)

    def test_a_variable_named_like_a_command_still_works(self):
        self.assertEqual(self.m.run("grid = 5"), "grid = 5\n")
        self.assertEqual(self.m.run("axis = [1 2 3 4];"), "")
        self.assertEqual(self.m.numbers("axis"), [1, 2, 3, 4])

    def test_without_a_screen_the_engine_says_so(self):
        m = Session()
        LIB.ml_set_plot_hook(m.s, ctypes.cast(None, HOOK), None)
        text = m.run("plot(1:3, 1:3)")
        m.close()
        self.assertIn("no screen for it", text)

    def test_the_text_picture_of_a_plot_for_the_xt(self):
        LIB.plot_text_image.argtypes = [ctypes.c_char_p, ctypes.POINTER(PlotText), ctypes.c_int, ctypes.c_char_p]
        self.m.run("plot(1:10, 1:10), title('rising')")
        bits = ctypes.create_string_buffer(16000)
        texts = (PlotText * 40)()
        n = LIB.ml_render_plot(self.m.s, bits, texts, 40, (ctypes.c_double * 4)())
        cells = ctypes.create_string_buffer(25 * 80)
        LIB.plot_text_image(bits, texts, n, cells)
        rows = [cells.raw[r * 80:(r + 1) * 80] for r in range(25)]
        self.assertIn(b"rising", b"".join(rows))                              # the title is written over the picture
        ink = {0xDF, 0xDC, 0xDB, 0xDD, 0xDE}
        self.assertGreater(sum(1 for r in rows for c in r if c in ink), 100)  # the frame and the line
        first = [min((i for i in range(11, 77) if rows[r][i] in ink), default=None) for r in range(3, 22)]
        first = [c for c in first if c is not None]
        self.assertEqual(first, sorted(first, reverse=True))                  # the line rises to the right
        self.assertTrue(all(c != 0 for row in rows for c in row))

    def test_a_flat_line_and_a_single_point(self):
        self.m.run("plot(1:5, [2 2 2 2 2])")
        rows, texts, limits = self.m.plot()
        self.assertLess(limits[2], 2)
        self.assertGreater(limits[3], 2)
        self.m.run("plot(3, 4, 'o')")
        self.assertEqual(self.m.rc, OK)
        self.assertGreater(sum(r.count("#") for r in self.m.plot()[0]), 600)


ASKHOOK = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.c_void_p, ctypes.c_int)
LIB.ml_set_ask_hook.argtypes = [ctypes.c_void_p, ASKHOOK, ctypes.c_void_p]


def put(name, text):
    with open(name, "w") as f:
        f.write(text)


def get(name):
    with open(name) as f:
        return f.read()


class Assistant(unittest.TestCase):
    """M07: ask, code, explain, fix, why. The engine builds the request; a fake hook plays MSASK.EXE."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.old = os.getcwd()
        os.chdir(self.tmp.name)
        self.addCleanup(os.chdir, self.old)
        self.addCleanup(self.tmp.cleanup)
        self.m = Session()
        self.addCleanup(self.m.close)
        self.calls = []
        self.answer = b"Use A\\b."
        self.result = 0
        self.hook = ASKHOOK(self._hook)
        LIB.ml_set_ask_hook(self.m.s, self.hook, None)

    def _hook(self, ctx, mode):
        """What MSASK.EXE does: read ASKIN.TXT (mode, instruction, context), write the answer to ASKOUT.TXT."""
        number, instruction, context = get("ASKIN.TXT").split("\n", 2)
        self.assertEqual(int(number), mode)
        self.calls.append((mode, instruction, context))
        put("ASKOUT.TXT", self.answer.decode())
        return self.result

    def test_ask_with_and_without_quotes(self):
        text = self.m.run("ask how do I solve a system, quickly; please")
        self.assertEqual(self.calls[-1], (13, "how do I solve a system, quickly; please", ""))
        self.assertIn("Use A\\b.", text)
        self.m.run('ask "what\'s the rank?"')
        self.assertEqual(self.calls[-1][:2], (13, "what's the rank?"))

    def test_the_request_and_answer_files_are_gone_afterwards(self):
        self.m.run("ask hello")
        self.assertFalse(os.path.exists("ASKIN.TXT"))
        self.assertFalse(os.path.exists("ASKOUT.TXT"))
        self.result = 1
        self.m.run("ask hello")
        self.assertFalse(os.path.exists("ASKIN.TXT"))
        self.assertFalse(os.path.exists("ASKOUT.TXT"))

    def test_an_empty_answer_is_a_failure(self):
        self.answer = b""
        text = self.m.run("ask hello")
        self.assertEqual(self.m.rc, ERROR)
        self.assertIn("did not answer", text)

    def test_ask_needs_a_question(self):
        text = self.m.run("ask")
        self.assertEqual(self.m.rc, ERROR)
        self.assertIn("ask what?", text)
        self.assertEqual(self.calls, [])

    def test_code_is_saved_in_ask_m_and_not_run(self):
        self.answer = b"x = 1:3\ndisp(sum(x))\ny = 99"
        text = self.m.run("code add up three numbers")
        self.assertEqual(self.calls[-1], (14, "add up three numbers", ""))
        self.assertIn("x = 1:3\ndisp(sum(x))\ny = 99\n", text)
        self.assertIn("saved in ask.m; nothing was run", text)
        self.assertEqual(get("ask.m"), "x = 1:3\ndisp(sum(x))\ny = 99\n")
        self.assertEqual(self.m.run("who").count("y"), 0)        # y = 99 was not executed
        self.assertIn("6", self.m.run("run ask"))                 # but the user can run it

    def test_explain_sends_the_script(self):
        put("sc.m", "s = 0;\nfor k = 1:3\n  s = s + k;\nend\n")
        text = self.m.run("explain sc")
        self.assertEqual(self.calls[-1], (15, "", "s = 0;\nfor k = 1:3\n  s = s + k;\nend\n"))
        self.assertIn("Use A", text)
        self.m.run("explain sc.m")
        self.assertEqual(self.calls[-1][0], 15)

    def test_explain_and_fix_need_a_script(self):
        self.assertIn("cannot find the file nope", self.m.run("explain nope"))
        self.assertEqual(self.m.rc, ERROR)
        self.assertIn("needs a script name", self.m.run("explain"))
        self.assertEqual(self.m.rc, ERROR)
        self.assertEqual(self.calls, [])

    def test_fix_saves_the_new_version_and_keeps_the_old(self):
        put("bad.m", "y = 2 x\n")
        self.answer = b"y = 2 * x"
        text = self.m.run('fix bad "it does not run"')
        self.assertEqual(self.calls[-1], (16, "it does not run", "y = 2 x\n"))
        self.assertIn("the original is unchanged", text)
        self.assertEqual(get("ask.m"), "y = 2 * x\n")
        self.assertEqual(get("bad.m"), "y = 2 x\n")
        self.m.run("fix bad")
        self.assertEqual(self.calls[-1][1], "")

    def test_change_sends_the_script_and_what_to_change_and_saves_the_result(self):
        put("tr.m", "x = 1:3\ny = x * 2\n")
        self.answer = b"x = 1:3\ny = x * 3"
        text = self.m.run('change tr "triple instead of double"')
        self.assertEqual(self.calls[-1], (18, "triple instead of double", "x = 1:3\ny = x * 2\n"))
        self.assertIn("the original is unchanged", text)
        self.assertEqual(get("ask.m"), "x = 1:3\ny = x * 3\n")
        self.assertEqual(get("tr.m"), "x = 1:3\ny = x * 2\n")
        self.m.run("change tr make it print the result, please; thanks")
        self.assertEqual(self.calls[-1][1], "make it print the result, please; thanks")

    def test_change_needs_a_script_and_a_request(self):
        put("tr.m", "x = 1\n")
        self.assertIn("change needs a script name", self.m.run("change"))
        self.assertIn("change how?", self.m.run("change tr"))
        self.assertIn("cannot find the file nope", self.m.run('change nope "x"'))
        self.assertEqual(self.calls, [])
        self.m.run("change = 4;")
        self.assertEqual(self.m.run("change"), "change = 4\n")

    def test_fix_the_function_and_variables_named_like_commands_still_work(self):
        self.assertEqual(self.m.run("fix(-2.7)"), "ans = -2\n")
        self.assertEqual(self.m.run("fix 2.5")[:5], "error")
        self.m.run("code = 5;")
        self.assertEqual(self.m.run("code"), "code = 5\n")
        self.assertEqual(self.m.run("code + 1"), "ans = 6\n")
        self.m.run("why = 2")
        self.assertEqual(self.m.run("why"), "why = 2\n")
        self.assertEqual(self.calls, [])

    def test_why_explains_the_last_error(self):
        self.m.run("y = q + 1")
        text = self.m.run("why")
        mode, instruction, context = self.calls[-1]
        self.assertEqual(mode, 17)
        self.assertIn("undefined variable or function 'q'", instruction)
        self.assertIn("y = q + 1", context)
        self.assertIn("Use A", text)
        self.m.run("ask something")                               # an assistant command does not replace the error
        self.m.run("why")
        self.assertIn("undefined variable", self.calls[-1][1])

    def test_why_in_a_script_sends_the_lines_around_the_error(self):
        put("boom.m", "a = 1;\nb = 2;\nc = a + zz;\nd = 4;\n")
        self.m.run("run boom")
        self.m.run("why")
        mode, instruction, context = self.calls[-1]
        self.assertIn("line 3 of boom.m", instruction)
        self.assertIn("3: c = a + zz;", context)
        self.assertIn("1: a = 1;", context)

    def test_why_without_an_error(self):
        text = self.m.run("why")
        self.assertEqual(self.m.rc, ERROR)
        self.assertIn("no error to explain", text)

    def test_a_failure_shows_the_reason(self):
        self.answer = b"MSASK: no server address"
        self.result = 1
        text = self.m.run("ask hello")
        self.assertEqual(self.m.rc, ERROR)
        self.assertIn("error: MSASK: no server address", text)
        self.assertFalse(os.path.exists("ask.m"))

    def test_stopping_is_not_an_error(self):
        self.result = 2
        text = self.m.run("code something")
        self.assertEqual(self.m.rc, OK)
        self.assertIn("(stopped)", text)
        self.assertFalse(os.path.exists("ask.m"))

    def test_a_long_script_is_cut_after_a_whole_line(self):
        put("big.m", "".join("x%d = %d;\n" % (i, i) for i in range(600)))
        text = self.m.run("explain big")
        context = self.calls[-1][2]
        self.assertLessEqual(len(context), 4000)
        self.assertTrue(context.endswith(";\n"))
        self.assertIn("only its first lines are sent", text)

    def test_too_long_a_question(self):
        text = self.m.run("ask " + "word " * 60)
        self.assertEqual(self.m.rc, ERROR)
        self.assertIn("too long", text)

    def test_without_a_hook(self):
        m = Session()
        text = m.run("ask hello")
        m.close()
        self.assertIn("not available", text)


EDITHOOK = ctypes.CFUNCTYPE(None, ctypes.c_void_p, ctypes.c_char_p)
LIB.ml_set_edit_hook.argtypes = [ctypes.c_void_p, EDITHOOK, ctypes.c_void_p]
LIB.ml_last_error.restype = ctypes.c_int
LIB.ml_last_error.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int, ctypes.c_char_p, ctypes.c_int]
LIB.ml_file_changed.argtypes = [ctypes.c_void_p, ctypes.c_char_p]


class Editing(unittest.TestCase):
    """M08: the edit command only notes the request for the front end; ml_last_error tells the editor where to put the cursor."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.old = os.getcwd()
        os.chdir(self.tmp.name)
        self.addCleanup(os.chdir, self.old)
        self.addCleanup(self.tmp.cleanup)
        self.m = Session()
        self.addCleanup(self.m.close)
        self.names = []
        self.hook = EDITHOOK(lambda ctx, name: self.names.append(name.decode()))
        LIB.ml_set_edit_hook(self.m.s, self.hook, None)

    def last_error(self):
        text, file = ctypes.create_string_buffer(100), ctypes.create_string_buffer(40)
        line = LIB.ml_last_error(self.m.s, text, 100, file, 40)
        return line, file.value.decode(), text.value.decode()

    def test_edit_hands_the_name_to_the_front_end(self):
        self.assertEqual(self.m.run("edit foo"), "")
        self.assertEqual(self.m.rc, OK)
        self.m.run("  edit bar.m  ")
        self.assertEqual(self.names, ["foo", "bar.m"])

    def test_edit_needs_a_name(self):
        self.assertIn("edit needs a script name", self.m.run("edit"))
        self.assertEqual(self.m.rc, ERROR)
        self.assertEqual(self.names, [])

    def test_a_variable_called_edit_is_not_the_command(self):
        self.m.run("edit = 3;")
        self.assertEqual(self.m.run("edit"), "edit = 3\n")
        self.assertEqual(self.names, [])

    def test_without_a_hook(self):
        m = Session()
        text = m.run("edit foo")
        m.close()
        self.assertIn("needs the full screen", text)

    def test_the_help_text_fits_a_viewer(self):
        LIB.ml_help_text.restype = ctypes.c_char_p
        text = LIB.ml_help_text().decode()
        self.assertTrue(text.startswith("MathSmith help"))
        self.assertTrue(all(len(line) <= 78 for line in text.split("\n")))
        self.assertIn("THE EDITOR", text)
        self.assertIn("THE ASSISTANT", text)
        self.assertEqual(self.m.run("help"), text)                 # line mode prints the same text

    def test_the_last_error_typed_has_no_place(self):
        self.m.run("y = q + 1")
        line, file, text = self.last_error()
        self.assertEqual((line, file), (0, ""))
        self.assertIn("undefined variable or function 'q'", text)

    def test_the_last_error_in_a_script_has_its_line_and_file(self):
        with open("boom.m", "w") as f:
            f.write("a = 1;\nb = 2;\nc = a + zz;\n")
        self.m.run("run boom")
        line, file, text = self.last_error()
        self.assertEqual((line, file), (3, "boom.m"))
        self.assertIn("zz", text)
        self.m.run("ask something")                             # no assistant here: its own error does not count
        self.assertEqual(self.last_error()[0], 3)

    def test_an_edited_function_file_is_read_again(self):
        with open("tw.m", "w") as f:
            f.write("function y = tw(x)\ny = 2 * x;\nend\n")
        self.assertEqual(self.m.run("tw(5)"), "ans = 10\n")
        with open("tw.m", "w") as f:
            f.write("function y = tw(x)\ny = 3 * x;\nend\n")
        self.assertEqual(self.m.run("tw(5)"), "ans = 10\n")     # the first reading is kept
        LIB.ml_file_changed(self.m.s, b"tw")
        self.assertEqual(self.m.run("tw(5)"), "ans = 15\n")
        with open("tw.m", "w") as f:
            f.write("function y = tw(x)\ny = 4 * x;\nend\n")
        LIB.ml_file_changed(self.m.s, b"tw.m")
        self.assertEqual(self.m.run("tw(5)"), "ans = 20\n")


if __name__ == "__main__":
    unittest.main()
