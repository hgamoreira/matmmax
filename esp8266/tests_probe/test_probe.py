"""Independent radio comparison: native input tests and real linked target checks."""
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import unittest

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'esp8266/scripts'))
from wifi_probe import APP, BUILD, TOOL, native, fresh

def setUpModule(): native()

class Probe(unittest.TestCase):
    def run_input(self,data,**env):
        r=subprocess.run([str(BUILD/'native/wifi_probe')],input=data,capture_output=True,
                         timeout=5,env=dict(os.environ,**env))
        self.assertEqual(r.returncode,0,r.stderr.decode()); return r.stdout.decode()

    def test_guided_credentials_masking_and_ip(self):
        out=self.run_input(b':wifi join\r\nnative net\r\nsecret-pass\r\n:wifi\r\n')
        self.assertIn('SSID (visible): native net',out)
        self.assertNotIn('secret-pass',out)
        self.assertIn('SDK=5 (got-ip); begin=3',out)
        self.assertIn('IP address: 127.0.0.1 (probe has no web server)',out)
        self.assertIn('Native fixture: physical radio/heap/stack are not measured',out)

    def test_bounds_arrows_and_cancellation(self):
        out=self.run_input(b':wifi join\n'+b'x'*33+b'\nnative\x1b[A\x1b[B\n'+b'x'*64+b'\n\x03:wifi\n')
        self.assertIn('SSID must contain 1..32 characters',out)
        self.assertIn('Password must contain at most 63 characters',out)
        self.assertNotIn('[A',out); self.assertNotIn('[B',out)
        self.assertIn('Cancelled.',out)
        self.assertIn('SDK=0 (idle); begin=-1',out)

    def test_connecting_and_failures_are_observable(self):
        data=b':wifi join\nnative\nprivate-pass\n:wifi\n'
        out=self.run_input(data,MS_TEST_WIFI_SDK_STATE='1')
        self.assertIn('status=7; SDK=1 (connecting); begin=7',out)
        self.assertNotIn('private-pass',out)
        out=self.run_input(data,MS_TEST_WIFI_SDK_STATE='3',MS_TEST_WIFI_REASON='201')
        self.assertIn('status=1; SDK=3 (no-ap-found); begin=1',out)
        self.assertIn('Last disconnect=201; events=1',out)
        out=self.run_input(data,MS_TEST_WIFI_MODE_FAIL='1')
        self.assertIn('Station mode failed to start',out)
        self.assertIn('mode=0; status=0; SDK=0 (idle); begin=-1',out)

    def test_ip_address_is_reported_even_when_sdk_status_is_stale(self):
        out=self.run_input(b':wifi join\nnative\nprivate-pass\n:wifi\n',
                           MS_TEST_WIFI_SDK_STATE='1',MS_TEST_WIFI_HAS_IP='1')
        self.assertIn('status=7; SDK=1 (connecting)',out)
        self.assertIn('IP address: 127.0.0.1',out)

    def test_image_freshness_and_no_math_engine(self):
        fresh()
        config=json.loads((BUILD/'build_config.json').read_text())
        self.assertEqual(config['engine_payload_cap'],0)
        self.assertEqual(config['stack_bytes'],4096)
        symbols=subprocess.check_output([str(TOOL/'xtensa-lx106-elf-nm'),'-S',str(BUILD/'mathsmith_light.elf')],text=True)
        for name in ('ml_open','ml_eval','mat_zeros','ms_console_start'):
            self.assertNotRegex(symbols,r'(?m)\b'+name+r'$')
        self.assertRegex(symbols,r'(?m)^3ff[0-9a-f]+ 00001028 b _ZL6g_cont$')

    def test_linked_memory_budget(self):
        layout=subprocess.check_output([str(TOOL/'xtensa-lx106-elf-size'),'-A',str(BUILD/'mathsmith_light.elf')],text=True)
        sections={m[1]:int(m[2]) for m in re.finditer(r'^(\.\S+)\s+(\d+)\s+\d+$',layout,re.M)}
        other=ROOT/'build/esp8266/l01_webrepl/mathsmith_light.elf'
        l01_layout=subprocess.check_output([str(TOOL/'xtensa-lx106-elf-size'),'-A',str(other)],text=True)
        l01={m[1]:int(m[2]) for m in re.finditer(r'^(\.\S+)\s+(\d+)\s+\d+$',l01_layout,re.M)}
        saving=sum(l01[n]-sections[n] for n in ('.data','.rodata','.bss'))
        self.assertGreater(saving,16384,'Comparison image must leave materially more RAM than L01')
        self.assertLess(sections['.text']+sections['.text1'],32768)
        self.assertLess((BUILD/'mathsmith_light.bin').stat().st_size,1044464)

if __name__=='__main__': unittest.main()
