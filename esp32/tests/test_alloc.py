"""Native tests of E01's real allocator adapter, including engine OOM recovery."""
import ctypes
from pathlib import Path
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[2]
PORT = ROOT / "apps/esp32/e01_mathsmith/main"
ENGINE = ROOT / "apps/m01_mathsmith"
SINK = ctypes.CFUNCTYPE(None, ctypes.c_void_p, ctypes.c_char_p)


class Allocator(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        build = ROOT / "build/esp32/e01_native"
        build.mkdir(parents=True, exist_ok=True)
        flags = ["gcc", "-std=c99", "-O1", "-fPIC", "-Wall", "-Wextra", "-Werror",
                 "-DMS_ALLOC_NATIVE_TEST", "-DMS_FFT", "-DMS_NO_AI",
                 "-I"+str(PORT), "-I"+str(ENGINE)]
        objects = []
        for source in [PORT/"platform.c"] + [ENGINE/(s+".c") for s in
                                             ("engine", "mat", "linalg", "fft", "plot")]:
            obj = build / (source.stem+".o")
            command = flags + ["-c", str(source), "-o", str(obj)]
            if source.parent == ENGINE:
                command += ["-include", str(PORT/"engine_port.h")]
            subprocess.run(command, check=True, capture_output=True, text=True)
            objects.append(str(obj))
        library = build / "adapter.so"
        subprocess.run(["gcc", "-shared", *objects, "-lm", "-o", str(library)], check=True)
        cls.lib = lib = ctypes.CDLL(str(library))
        for name in ("ms_memory_used", "ms_memory_peak", "ms_memory_limit"):
            getattr(lib, name).restype = ctypes.c_size_t
        lib.ms_set_memory_limit.argtypes = [ctypes.c_size_t]
        lib.ms_malloc.argtypes = [ctypes.c_size_t]
        lib.ms_malloc.restype = ctypes.c_void_p
        lib.ms_calloc.argtypes = [ctypes.c_size_t, ctypes.c_size_t]
        lib.ms_calloc.restype = ctypes.c_void_p
        lib.ms_realloc.argtypes = [ctypes.c_void_p, ctypes.c_size_t]
        lib.ms_realloc.restype = ctypes.c_void_p
        lib.ms_free.argtypes = [ctypes.c_void_p]
        lib.ml_open.argtypes = [SINK, ctypes.c_void_p]
        lib.ml_open.restype = ctypes.c_void_p
        lib.ml_close.argtypes = [ctypes.c_void_p]
        lib.ml_eval.argtypes = [ctypes.c_void_p, ctypes.c_char_p]

    def setUp(self):
        self.lib.ms_set_memory_limit(4*1024*1024)
        self.addCleanup(self.lib.ms_set_memory_limit, 4*1024*1024)

    def test_alignment_accounting_and_free(self):
        l = self.lib
        before = l.ms_memory_used()
        ptr = l.ms_malloc(29)
        self.assertTrue(ptr)
        self.assertEqual(ptr % 8, 0)
        self.assertEqual(l.ms_memory_used(), before+29)
        l.ms_free(ptr)
        l.ms_free(None)
        self.assertEqual(l.ms_memory_used(), before)

    def test_calloc_zero_and_overflow(self):
        l = self.lib
        ptr = l.ms_calloc(7, 8)
        self.assertEqual(ctypes.string_at(ptr, 56), b"\0"*56)
        l.ms_free(ptr)
        maximum = ctypes.c_size_t(-1).value
        self.assertFalse(l.ms_calloc(maximum, 8))
        self.assertFalse(l.ms_malloc(maximum))

    def test_failed_realloc_retains_data_and_accounting(self):
        l = self.lib
        ptr = l.ms_malloc(16)
        ctypes.memmove(ptr, b"kept unchanged!!", 16)
        used = l.ms_memory_used()
        l.ms_set_memory_limit(used)
        self.assertFalse(l.ms_realloc(ptr, 17))
        self.assertEqual(ctypes.string_at(ptr, 16), b"kept unchanged!!")
        self.assertEqual(l.ms_memory_used(), used)
        l.ms_free(ptr)

    def test_resize_and_zero_size_free(self):
        l = self.lib
        baseline = l.ms_memory_used()
        ptr = l.ms_realloc(None, 8)
        ctypes.memmove(ptr, b"12345678", 8)
        ptr = l.ms_realloc(ptr, 64)
        self.assertEqual(ctypes.string_at(ptr, 8), b"12345678")
        self.assertEqual(l.ms_memory_used(), baseline+64)
        ptr = l.ms_realloc(ptr, 4)
        self.assertEqual(ctypes.string_at(ptr, 4), b"1234")
        self.assertFalse(l.ms_realloc(ptr, 0))
        self.assertEqual(l.ms_memory_used(), baseline)

    def test_engine_allocation_failure_preserves_workspace(self):
        l = self.lib
        output = []
        sink = SINK(lambda ctx, text: output.append(text.decode()))
        session = l.ml_open(sink, None)
        self.assertTrue(session)
        self.addCleanup(l.ml_close, session)
        self.assertEqual(l.ml_eval(session, b"x=41;"), 0)
        for extra in (0, 2048):
            output.clear()
            l.ms_set_memory_limit(l.ms_memory_used()+extra)
            self.assertEqual(l.ml_eval(session, b"x=ones(64);"), 1)
            self.assertIn("out of memory", "".join(output))
            l.ms_set_memory_limit(4*1024*1024)
            output.clear()
            self.assertEqual(l.ml_eval(session, b"x"), 0)
            self.assertEqual("".join(output), "x = 41\n")


if __name__ == "__main__":
    unittest.main()
