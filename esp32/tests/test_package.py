"""Native checks of bounded MSAP framing and CRC against independent Python code."""
import ctypes as C
from pathlib import Path
import struct
import subprocess
import unittest
import zlib

ROOT = Path(__file__).resolve().parents[2]


class Package(C.Structure):
    _fields_ = [("elf", C.c_void_p), ("elf_size", C.c_size_t), ("arena_size", C.c_size_t)]


def package(payload, *, machine=94, abi=1, arena=4096, target=1):
    return struct.pack('<4sHH6I', b'MSAP', 1, 32, machine, abi, arena,
                       len(payload), zlib.crc32(payload), target)+payload


class PackageTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        build = ROOT/'build/esp32/e03_native'
        build.mkdir(parents=True, exist_ok=True)
        common = ROOT/'common/workstation'
        target = build/'package.so'
        subprocess.run(['gcc','-std=c89','-O1','-Wall','-Wextra','-Werror','-shared','-fPIC',
                        '-I'+str(common),str(common/'package.c'),'-o',str(target)], check=True)
        cls.lib = C.CDLL(str(target))
        cls.lib.wk_package_check.argtypes = [C.c_void_p,C.c_size_t,C.POINTER(Package)]
        cls.lib.wk_crc32.argtypes = [C.c_uint32,C.c_void_p,C.c_size_t]
        cls.lib.wk_crc32.restype = C.c_uint32

    def check(self, data):
        buffer = C.create_string_buffer(data)
        result = Package(None,123,456)
        status = self.lib.wk_package_check(buffer,len(data),C.byref(result))
        if status:
            self.assertEqual((result.elf,result.elf_size,result.arena_size),(None,123,456))
        return status,result,buffer

    def test_crc_matches_zlib_and_incremental_updates(self):
        data = bytes(range(256))*4
        for n in (0,1,13,256,1024):
            self.assertEqual(self.lib.wk_crc32(0,data,n),zlib.crc32(data[:n]))
        crc = 0
        for offset in range(0,len(data),17):
            piece = data[offset:offset+17]
            crc = self.lib.wk_crc32(crc,piece,len(piece))
        self.assertEqual(crc,zlib.crc32(data))

    def test_valid_package_and_exact_output_view(self):
        payload = bytes(range(256))
        status,result,buffer = self.check(package(payload))
        self.assertEqual(status,0)
        self.assertEqual(result.elf_size,len(payload))
        self.assertEqual(result.arena_size,4096)
        self.assertEqual(C.string_at(result.elf,result.elf_size),payload)

    def test_truncated_extra_and_bad_crc_rejected(self):
        data = package(b'x'*100)
        for n in list(range(33))+[len(data)-1]:
            self.assertNotEqual(self.check(data[:n])[0],0)
        self.assertNotEqual(self.check(data+b'x')[0],0)
        broken = bytearray(data)
        broken[-1] ^= 1
        self.assertNotEqual(self.check(bytes(broken))[0],0)

    def test_target_abi_and_arena_limits(self):
        for options in ({'machine':243},{'abi':2},{'target':0},{'target':2},{'arena':0},{'arena':1048577}):
            self.assertNotEqual(self.check(package(b'x'*100,**options))[0],0)
        self.assertEqual(self.check(package(b'x'*100,arena=1048576))[0],0)

    def test_format_and_size_boundaries(self):
        data = package(b'x'*100)
        for offset in (0,4,6):
            broken = bytearray(data)
            broken[offset] ^= 1
            self.assertNotEqual(self.check(bytes(broken))[0],0)
        self.assertEqual(self.check(package(b'x'*(262144-32)))[0],0)
        self.assertNotEqual(self.check(package(b'x'*(262144-31)))[0],0)
        self.assertNotEqual(self.check(package(b'x'*51))[0],0)


if __name__ == '__main__': unittest.main()
