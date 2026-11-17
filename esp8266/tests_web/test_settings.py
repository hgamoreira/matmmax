"""Exercise actual flash-record code with byte-level interrupted erase/write."""
import ctypes as C
from pathlib import Path
import struct
import subprocess
import unittest
import zlib

ROOT=Path(__file__).resolve().parents[2]
APP=ROOT/'apps/esp8266/l01_webrepl'
OUT=ROOT/'build/esp8266/l01_webrepl/native/settings.so'
class Settings(C.Structure):
    _fields_=[('ssid',C.c_char*33),('password',C.c_char*64),('key',C.c_char*33),('generation',C.c_uint32)]
READ=C.CFUNCTYPE(C.c_int,C.c_uint,C.c_void_p,C.c_size_t)
ERASE=C.CFUNCTYPE(C.c_int,C.c_uint)
WRITE=C.CFUNCTYPE(C.c_int,C.c_uint,C.c_size_t,C.c_void_p,C.c_size_t)

def setUpModule():
    OUT.parent.mkdir(parents=True,exist_ok=True)
    subprocess.run(['gcc','-std=c99','-shared','-fPIC','-O2','-I'+str(APP),str(APP/'settings.c'),
        str(ROOT/'esp8266/tests_web/settings_fixture.c'),'-o',str(OUT)],check=True)

class FlashRecords(unittest.TestCase):
    def setUp(self):
        self.lib=C.CDLL(str(OUT)); self.flash=[bytearray(b'\xff'*160) for _ in range(2)]
        self.cut=None; self.erase_cut=None; self.erase_count=0; self.write_count=0
        self.read_fail=False; self.erase_fail_on=None
        def rd(slot,p,n):
            if self.read_fail:return 0
            C.memmove(p,bytes(self.flash[slot][:n]),n);return 1
        def er(slot):
            self.erase_count+=1
            if self.erase_count==self.erase_fail_on:return 0
            count=160 if self.erase_cut is None else self.erase_cut
            self.flash[slot][:count]=b'\xff'*count
            return int(count==160)
        def wr(slot,offset,p,n):
            self.write_count+=1;data=C.string_at(p,n)
            for i,b in enumerate(data):
                if self.cut is not None:
                    if not self.cut:return 0
                    self.cut-=1
                if self.flash[slot][offset+i]&b!=b:return 0
                self.flash[slot][offset+i]=b
            return 1
        self.callbacks=(READ(rd),ERASE(er),WRITE(wr))
        self.lib.ms_test_settings_hooks.argtypes=[READ,ERASE,WRITE]
        self.lib.ms_test_settings_hooks(*self.callbacks)
        self.lib.ms_settings_load.argtypes=[C.POINTER(Settings)]
        self.lib.ms_settings_save.argtypes=[C.POINTER(Settings)]

    def value(self,name=b'network',password=b'private-pass'):
        return Settings(name,password,b'0123456789abcdef'*2,0)
    def load(self):
        out=Settings();result=self.lib.ms_settings_load(C.byref(out));return result,out
    def save(self,value):return self.lib.ms_settings_save(C.byref(value))

    def test_crc_reference_and_unchanged_save_does_not_write(self):
        v=self.value();self.assertEqual(self.save(v),1)
        self.assertEqual(struct.unpack_from('<I',self.flash[0],152)[0],zlib.crc32(self.flash[0][:152]))
        before=(self.erase_count,self.write_count)
        self.assertEqual(self.save(v),2);self.assertEqual(before,(self.erase_count,self.write_count))
        result,out=self.load();self.assertEqual(result,1);self.assertEqual(out.ssid,v.ssid)
        self.assertEqual(out.password,v.password);self.assertEqual(out.key,v.key);self.assertEqual(out.generation,1)

    def test_every_interrupted_write_preserves_previous_record(self):
        self.save(self.value(b'old')); original=[x[:] for x in self.flash]
        for cut in range(160):
            with self.subTest(cut=cut):
                self.flash=[x[:] for x in original];self.cut=cut
                self.assertLess(self.save(self.value(b'new')),0)
                result,out=self.load();self.assertEqual(result,1);self.assertEqual(out.ssid,b'old')
        self.flash=[x[:] for x in original];self.cut=160
        self.assertEqual(self.save(self.value(b'new')),1);self.assertEqual(self.load()[1].ssid,b'new')

    def test_interrupted_erase_never_erases_current_record(self):
        self.save(self.value(b'old')); original=[x[:] for x in self.flash]
        for cut in (0,1,40,80,159):
            self.flash=[x[:] for x in original];self.erase_cut=cut
            self.assertLess(self.save(self.value(b'new')),0)
            self.assertEqual(self.load()[1].ssid,b'old')

    def test_corrupt_new_record_falls_back_and_corrupt_all_is_unconfigured(self):
        self.save(self.value(b'old'));self.save(self.value(b'new'))
        self.flash[1][50]^=1;self.assertEqual(self.load()[1].ssid,b'old')
        self.flash[0][50]^=1
        result,out=self.load();self.assertEqual(result,0);self.assertEqual(bytes(out),b'\0'*C.sizeof(out))

    def test_read_error_prevents_overwriting_unknown_data(self):
        self.save(self.value());self.read_fail=True;before=self.erase_count
        self.assertEqual(self.load()[0],-1)
        self.assertEqual(self.save(self.value(b'new')),-1);self.assertEqual(before,self.erase_count)

    def test_forget_tombstone_survives_cleanup_failure(self):
        self.save(self.value(b'private-network'));self.erase_fail_on=self.erase_count+2
        self.assertEqual(self.lib.ms_settings_forget(),-3)
        self.assertEqual(self.load()[0],0)
        self.assertIn(b'private-network',b''.join(self.flash))
        self.erase_fail_on=None;self.assertEqual(self.lib.ms_settings_forget(),1)
        self.assertNotIn(b'private-network',b''.join(self.flash))
        self.assertNotIn(b'private-pass',b''.join(self.flash))

    def test_forget_interrupted_before_commit_keeps_old_settings(self):
        self.save(self.value(b'old'));original=[x[:] for x in self.flash]
        for cut in (0,1,100,152,156,159):
            self.flash=[x[:] for x in original];self.cut=cut
            self.assertLess(self.lib.ms_settings_forget(),0)
            self.assertEqual(self.load()[1].ssid,b'old')

    def test_generation_wrap_and_invalid_input(self):
        self.save(self.value(b'old'))
        struct.pack_into('<I',self.flash[0],8,0xffffffff)
        struct.pack_into('<I',self.flash[0],152,zlib.crc32(self.flash[0][:152]))
        self.assertEqual(self.save(self.value(b'new')),1)
        result,out=self.load();self.assertEqual(result,1);self.assertEqual(out.ssid,b'new')
        self.assertEqual(out.generation,0)
        before=self.erase_count
        for value in (self.value(b''),self.value(b'x'*33),self.value(password=b'x'*64),
                      Settings(b'ok',b'pw',b'g'*32,0)):
            self.assertEqual(self.save(value),-2)
        self.assertEqual(before,self.erase_count)

if __name__=='__main__':unittest.main()
