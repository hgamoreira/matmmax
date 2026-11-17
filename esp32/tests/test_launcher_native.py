"""Portable app lifecycle and the actual demo program, built with native gcc."""
import ctypes as C
from pathlib import Path
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[2]
ALLOC = C.CFUNCTYPE(C.c_void_p, C.c_void_p, C.c_size_t)
FREE = C.CFUNCTYPE(None, C.c_void_p, C.c_void_p)
READ = C.CFUNCTYPE(C.c_int, C.c_void_p)
WRITE = C.CFUNCTYPE(None, C.c_void_p, C.c_char_p)
RESET = C.CFUNCTYPE(None, C.c_void_p)


class Services(C.Structure):
    _fields_ = [("ctx", C.c_void_p), ("allocate", ALLOC), ("release", FREE),
                ("read_key", READ), ("write", WRITE), ("reset_input", RESET)]


class Context(C.Structure):
    _fields_ = [("io", Services), ("memory", C.c_void_p), ("memory_size", C.c_size_t),
                ("arguments", C.c_char_p)]


RUN = C.CFUNCTYPE(C.c_int, C.POINTER(Context))


class App(C.Structure):
    _fields_ = [("name", C.c_char_p), ("description", C.c_char_p),
                ("memory_required", C.c_size_t), ("run", RUN)]


class Launcher(C.Structure):
    _fields_ = [("apps", C.POINTER(App)), ("count", C.c_size_t),
                ("services", Services), ("active", C.POINTER(App))]


class NativeLauncher(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        build = ROOT / "build/esp32/e02_native"
        build.mkdir(parents=True, exist_ok=True)
        common = ROOT / "common/workstation"
        demo = ROOT / "apps/workstation/demo"
        library = build / "launcher.so"
        subprocess.run(["gcc", "-std=c89", "-Wall", "-Wextra", "-Werror", "-O1",
                        "-shared", "-fPIC", "-I"+str(common), "-I"+str(demo),
                        str(common/"launcher.c"), str(demo/"demo.c"), "-o", str(library)],
                       check=True, capture_output=True, text=True)
        cls.lib = lib = C.CDLL(str(library))
        lib.wk_init.argtypes = [C.POINTER(Launcher), C.POINTER(App), C.c_size_t,
                               C.POINTER(Services)]
        lib.wk_run.argtypes = [C.POINTER(Launcher), C.c_char_p, C.c_char_p]
        lib.wk_find.argtypes = [C.POINTER(Launcher), C.c_char_p]
        lib.wk_find.restype = C.POINTER(App)

    def setUp(self):
        self.events, self.output, self.input = [], [], []
        self.allocations = {}
        self.refuse, self.reenter, self.change_memory = False, False, False
        self.app_result = 0
        self.observed = None
        self.nested_result = None
        self.allocate = ALLOC(self._allocate)
        self.release = FREE(self._release)
        self.read = READ(lambda ctx: self.input.pop(0) if self.input else -1)
        self.write = WRITE(lambda ctx, text: self.output.append(text.decode()))
        self.reset = RESET(lambda ctx: self.events.append("reset"))
        self.run = RUN(self._run)
        self.services = Services(None, self.allocate, self.release, self.read,
                                 self.write, self.reset)
        self.registry = (App*1)(App(b"test", b"Test callback", 64, self.run))
        self.launcher = Launcher()
        self.assertEqual(self.lib.wk_init(C.byref(self.launcher), self.registry,
                                         1, C.byref(self.services)), 0)

    def _allocate(self, ctx, size):
        self.events.append("allocate")
        if self.refuse: return None
        block = C.create_string_buffer(size)
        address = C.addressof(block)
        C.memset(address, 0xa5, size)
        self.allocations[address] = block
        return address

    def _release(self, ctx, memory):
        self.events.append("release")
        self.allocations.pop(memory)

    def _run(self, context):
        self.events.append("run")
        c = context.contents
        self.observed = (C.string_at(c.memory, c.memory_size), c.arguments,
                         bool(self.launcher.active))
        if self.reenter:
            self.nested_result = self.lib.wk_run(C.byref(self.launcher), b"test", b"")
        if self.change_memory: c.memory = None
        return self.app_result

    def launch(self, name=b"test", args=b""):
        return self.lib.wk_run(C.byref(self.launcher), name, args)

    def test_success_cancel_error_share_cleanup(self):
        for code, expected in ((0, 0), (1, 4), (2, 5)):
            self.events.clear()
            self.app_result = code
            self.assertEqual(self.launch(args=b"hello world"), expected)
            self.assertEqual(self.events, ["allocate", "reset", "run", "reset", "release"])
            self.assertEqual(self.observed, (b"\0"*64, b"hello world", True))
            self.assertFalse(self.allocations)
            self.assertFalse(self.launcher.active)

    def test_unknown_application_has_no_side_effects(self):
        self.assertEqual(self.launch(b"missing"), 1)
        self.assertFalse(self.lib.wk_find(C.byref(self.launcher), None))
        self.assertEqual(self.events, [])
        self.assertFalse(self.launcher.active)

    def test_allocation_failure_then_success(self):
        self.refuse = True
        self.assertEqual(self.launch(), 3)
        self.assertEqual(self.events, ["allocate"])
        self.assertFalse(self.launcher.active)
        self.refuse = False
        self.assertEqual(self.launch(), 0)
        self.assertFalse(self.allocations)

    def test_nested_launch_is_refused(self):
        self.reenter = True
        self.assertEqual(self.launch(), 0)
        self.assertEqual(self.nested_result, 2)
        self.assertEqual(self.events.count("allocate"), 1)

    def test_cleanup_owns_original_arena(self):
        self.change_memory = True
        self.assertEqual(self.launch(), 0)
        self.assertFalse(self.allocations)

    def test_invalid_duplicate_and_empty_registries(self):
        duplicate = (App*2)(self.registry[0], self.registry[0])
        self.assertEqual(self.lib.wk_init(C.byref(self.launcher), duplicate, 2,
                                         C.byref(self.services)), 6)
        self.assertEqual(self.lib.wk_init(None, self.registry, 1, C.byref(self.services)), 6)
        self.assertEqual(self.lib.wk_init(C.byref(self.launcher), None, 0,
                                         C.byref(self.services)), 0)
        self.assertEqual(self.launch(), 1)

    def use_demo(self, text):
        self.demo_fn = RUN(("wk_demo_run", self.lib))
        self.registry = (App*1)(App(b"demo", b"Demo", 4096, self.demo_fn))
        self.assertEqual(self.lib.wk_init(C.byref(self.launcher), self.registry, 1,
                                         C.byref(self.services)), 0)
        self.input = list(text)
        return self.launch(b"demo", b"sample arguments")

    def test_demo_echo_editing_arguments_and_normal_exit(self):
        self.assertEqual(self.use_demo(b"hellp\bo\r\nexit\r"), 0)
        output = "".join(self.output)
        self.assertIn("Arguments: sample arguments", output)
        self.assertIn("Demo echo: hello", output)
        self.assertFalse(self.allocations)

    def test_demo_cancel_error_and_input_exhaustion(self):
        for text, expected in ((b"\x1b", 4), (b"fail\r", 5), (b"", 5)):
            self.assertEqual(self.use_demo(text), expected)
            self.assertFalse(self.allocations)
            self.assertFalse(self.launcher.active)

    def test_demo_overflow_recovers(self):
        self.assertEqual(self.use_demo(b"a"*128+b"\rnext\rexit\r"), 0)
        self.assertIn("Demo line too long; discarded.", "".join(self.output))
        self.assertIn("Demo echo: next", "".join(self.output))


if __name__ == "__main__":
    unittest.main()
