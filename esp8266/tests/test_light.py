"""Native profile/front-end tests and checks on the actual linked L106 image."""
import ctypes
import hashlib
import json
import math
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import shutil
import struct
import unittest

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'esp8266/scripts'))
from native import build
from light_profile import BUILD,SOURCE,inputs_fingerprint
from build import TOOL
from check_image import check,crc8266

def setUpModule(): build()

class LightEngine(unittest.TestCase):
    def run_console(self,lines,**env):
        data=lines if isinstance(lines,bytes) else ('\n'.join(lines)+'\n').encode()
        result=subprocess.run([str(BUILD/'native/mathsmith_light')],input=data,
            capture_output=True,timeout=15,env=dict(os.environ,**env))
        self.assertEqual(result.returncode,0,result.stderr.decode())
        return result.stdout.decode()

    def scalar(self,expression,expected,tolerance=1e-7):
        output=self.run_console(['format long',expression])
        values=re.findall(r'ans = ([^\n]+)',output)
        self.assertTrue(values,output)
        self.assertAlmostEqual(float(values[-1]),expected,delta=tolerance)

    def test_double_and_scientific_functions(self):
        for expression,expected in [('(2^53+1)-2^53',0),('(2^53-1)-2^53',-1),
            ('sin(pi/6)',.5),('sqrt(2)',math.sqrt(2)),('exp(1)',math.e),
            ('log(exp(2))',2),('2^3^2',64)]:
            with self.subTest(expression=expression): self.scalar(expression,expected)

    def test_matrices_and_solver_against_reference(self):
        self.scalar('det([4 -2;-2 4])',12)
        output=self.run_console(['A=[4 -2;-2 4]; b=A\\[2;8];','b(1)','b(2)',
                                 'B=ones(8,8); sum(sum(B))','zeros(9,9)','2+2'])
        self.assertIn('ans = 2',output); self.assertIn('ans = 3',output)
        self.assertIn('ans = 64',output); self.assertIn('larger than 64 elements',output)
        self.assertIn('ans = 4',output)

    def test_bounded_text_formatting_and_transpose(self):
        output=self.run_console(["fprintf('value=%d\\n',42)","A=[1 2;3 4]; B=A'; B(1,2)",
            "sprintf('%d',42)","fprintf('%300d',1)",'2+2'])
        self.assertIn('value=42',output)
        self.assertIn('ans = 3',output)
        self.assertIn('42',output)
        self.assertIn('error:',output)
        self.assertIn('ans = 4',output)

    def test_typed_functions_loops_and_variable_limit(self):
        output=self.run_console(['x=41; f=@(t) t+x;','f(1)',
            's=0;','for k=1:8','s=s+k;','end','s',
            *[f'v{i}={i};' for i in range(20)],'x+1'])
        self.assertIn('ans = 42',output); self.assertIn('s = 36',output)
        self.assertRegex(output,r'(?i)(too many variables|variable.*full|16 variables)')
        self.assertTrue(output.endswith('>> '))

    def test_editing_crlf_and_overlong_line(self):
        output=self.run_console(b'x=41\r\nx+2\b1\r\n'+b'a'*192+b'\r\nx+1\r\n')
        self.assertEqual(output.count('ans = 42'),2,output)
        self.assertIn('Input too long; line discarded',output)
        self.assertNotIn('error:',output)

    def test_cancel_pending_block_and_preserve_workspace(self):
        output=self.run_console(b'x=41;\nfor k=1:8\n\x1bx+1\n')
        self.assertIn('Cancelled.',output); self.assertIn('ans = 42',output)

    def test_cancel_running_calculation(self):
        output=self.run_console(['x=41;','s=0;','for k=1:1000','s=s+k;','end','x+1'],MS_TEST_CANCEL_AFTER='30')
        self.assertIn('interrupted',output.lower()); self.assertIn('ans = 42',output)

    def test_nesting_and_unary_limits_recover(self):
        output=self.run_console(['x=41;', '('*9+'1'+')'*9,'-'*9+'1','x+1'])
        self.assertEqual(output.count('Expression nesting/unary limit is 8'),2)
        self.assertIn('ans = 42',output)

    def test_stack_guard_returns_error_and_diagnostics(self):
        output=self.run_console(['A=[1 2;3 4]','2+2',':mem'],MS_TEST_STACK_LOW='1')
        self.assertIn('ESP8266 stack reserve reached',output)
        self.assertIn('Engine payload:',output)

    def test_startup_allocation_failure_keeps_console(self):
        output=self.run_console([':mem','2+2',':reset'],MS_TEST_LIMIT='1')
        self.assertIn('Engine allocation failed',output)
        self.assertIn('Engine unavailable',output)
        self.assertIn('Not enough memory',output)

    def test_runtime_allocation_failure_preserves_values_and_recovers(self):
        output=self.run_console(['x=41;','A=ones(8,8);','x+1','2+2'],MS_TEST_LIMIT='5600')
        self.assertIn('out of memory',output)
        self.assertIn('ans = 42',output); self.assertIn('ans = 4',output)

    def test_repeated_matrices_and_reset_restore_live_payload(self):
        cycle='A=eye(8); B=inv(A); clear A; clear B;'
        warm=['x=41;','x+1',cycle,cycle,':mem']
        cycles=[cycle]*20
        output=self.run_console(warm+cycles+[':mem','x+1',':reset','2+2'])
        values=re.findall(r'Engine payload: used=(\d+)',output)
        self.assertEqual(values[1],values[2],output)
        self.assertNotIn('error:',output)
        self.assertIn('ans = 42',output); self.assertIn('ans = 4',output)

    def test_files_fft_and_launcher_are_unavailable(self):
        output=self.run_console(['run absent','fft([1 2])',':app demo','2+2'])
        self.assertIn('error:',output)
        self.assertIn('Unknown platform command',output)
        self.assertIn('ans = 4',output)

class Allocator(unittest.TestCase):
    def test_failed_realloc_retains_data_and_budget(self):
        lib=ctypes.CDLL(str(BUILD/'native/allocator.so'))
        lib.ms_malloc.argtypes=[ctypes.c_size_t];lib.ms_malloc.restype=ctypes.c_void_p
        lib.ms_realloc.argtypes=[ctypes.c_void_p,ctypes.c_size_t];lib.ms_realloc.restype=ctypes.c_void_p
        lib.ms_calloc.argtypes=[ctypes.c_size_t,ctypes.c_size_t];lib.ms_calloc.restype=ctypes.c_void_p
        lib.ms_free.argtypes=[ctypes.c_void_p];lib.ms_used.restype=ctypes.c_size_t
        lib.ms_set_limit.argtypes=[ctypes.c_size_t]
        lib.ms_set_limit(64)
        p=lib.ms_malloc(32);self.assertTrue(p)
        try:
            ctypes.memset(p,0x5a,32)
            self.assertFalse(lib.ms_realloc(p,65))
            self.assertEqual(ctypes.string_at(p,32),b'Z'*32)
            self.assertEqual(lib.ms_used(),32)
            self.assertFalse(lib.ms_calloc(ctypes.c_size_t(-1).value,2))
        finally:lib.ms_free(p)
        self.assertEqual(lib.ms_used(),0)
        p=lib.ms_calloc(8,8);self.assertTrue(p)
        try:self.assertEqual(ctypes.string_at(p,64),b'\0'*64)
        finally:lib.ms_free(p)

class TargetBuild(unittest.TestCase):
    def test_pre_cache_heap_startup_is_in_iram(self):
        symbols=subprocess.check_output([str(TOOL/'xtensa-lx106-elf-nm'),
            str(BUILD/'mathsmith_light.elf')],text=True)
        for name in ('app_entry','app_entry_redefinable','umm_init','umm_init_heap'):
            with self.subTest(symbol=name):
                match=re.search(r'(?m)^([0-9a-f]+) \w '+name+'$',symbols)
                self.assertIsNotNone(match)
                self.assertGreaterEqual(int(match.group(1),16),0x40100000)
                self.assertLess(int(match.group(1),16),0x40108000)
        members=subprocess.check_output([str(TOOL/'xtensa-lx106-elf-ar'),
            't',str(BUILD/'core.a')],text=True).splitlines()
        self.assertIn('umm_malloc.cpp.o',members)
        self.assertNotIn('umm_malloc_umm_malloc.cpp.o',members)
    def test_image_converter_preserves_payload_and_core_metadata(self):
        self.assertEqual(crc8266(b'123456789'),0x0376e6e7)
        self.assertGreater(check(),0)

    def test_image_validation_rejects_crc_and_payload_corruption(self):
        original=(BUILD/'mathsmith_light.bin').read_bytes()
        for mode in ('crc','payload-with-valid-crc','load-address-with-valid-crc'):
            with self.subTest(mode=mode),tempfile.TemporaryDirectory(dir=BUILD) as directory:
                directory=Path(directory)
                shutil.copyfile(BUILD/'mathsmith_light.elf',directory/'mathsmith_light.elf')
                image=bytearray(original)
                image[0x1008 if mode.startswith('load-address') else 0x1020]^=1
                if mode!='crc':
                    image[0x1010:0x1018]=b'\0'*8
                    checksum=crc8266(image)
                    struct.pack_into('<II',image,0x1010,len(image),checksum)
                (directory/'mathsmith_light.bin').write_bytes(image)
                with self.assertRaises(ValueError):check(directory)

    def test_generated_profile_tracks_shared_sources_without_editing_them(self):
        hashes=json.loads((BUILD/'engine_sources.json').read_text())
        for name,digest in hashes.items():
            self.assertEqual(hashlib.sha256((SOURCE/name).read_bytes()).hexdigest(),digest)
        self.assertIn('#define MAT_MAX_ELEMS 4096',(SOURCE/'mat.h').read_text())
        self.assertIn('#define MAT_MAX_ELEMS 64',(BUILD/'engine/mat.h').read_text())
        self.assertEqual(json.loads((BUILD/'build_config.json').read_text())['inputs'],inputs_fingerprint(),
                         'Target build is stale; rebuild before testing/releasing')

    def test_linked_flash_ram_and_stack_fit(self):
        elf=BUILD/'mathsmith_light.elf'
        self.assertTrue(elf.exists(),'Build the target firmware before testing')
        output=subprocess.check_output([str(TOOL/'xtensa-lx106-elf-size'),'-A',str(elf)],text=True)
        sections={m.group(1):(int(m.group(2)),int(m.group(3))) for m in
                  re.finditer(r'^(\.\S+)\s+(\d+)\s+(\d+)$',output,re.M)}
        ram=sum(sections[x][0] for x in ('.data','.rodata','.bss'))
        self.assertLess(ram,56000) # Includes the dedicated 16 KiB continuation stack.
        self.assertLess(sections['.irom0.text'][0],1044464)
        self.assertLess(sections['.rodata'][0],4096)
        symbols=subprocess.check_output([str(TOOL/'xtensa-lx106-elf-nm'),'-S',str(elf)],text=True)
        self.assertRegex(symbols,r'(?m)^401[0-9a-f]+ .* non32xfer_exception_handler$')
        self.assertRegex(symbols,r'(?m)^402[0-9a-f]+ .* builtins$')
        stack=re.search(r'(?m)^3ff[0-9a-f]+ ([0-9a-f]+) b _ZL6g_cont$',symbols)
        self.assertIsNotNone(stack,'Dedicated BSS stack must be linked; do not borrow SYS stack')
        self.assertEqual(int(stack.group(1),16),16424)
        self.assertEqual(json.loads((BUILD/'build_config.json').read_text())['stack_bytes'],16384)
        image=(BUILD/'mathsmith_light.bin').read_bytes()
        self.assertEqual(image[0],0xe9)
        self.assertLess(len(image),1044464)

if __name__=='__main__':unittest.main()
