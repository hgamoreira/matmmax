"""Archived-image command gates/routing, with fake checkers and serial tools."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[2]

class R4RecoveryCommand(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory();self.addCleanup(self.tmp.cleanup)
        self.root=Path(self.tmp.name);self.log=self.root/'calls.jsonl'
        scripts=self.root/'esp8266/scripts';scripts.mkdir(parents=True)
        self.script=scripts/'webrepl.sh';shutil.copyfile(ROOT/'esp8266/scripts/webrepl.sh',self.script)
        self.script.chmod(0o755)
        spy='#!'+shutil.which('python3')+'''\nimport json,os,sys
with open(os.environ['RECOVERY_TEST_LOG'],'a') as f:f.write(json.dumps(sys.argv)+'\\n')
kind='hash' if len(sys.argv)>1 and sys.argv[1]=='-' else 'image'
if os.environ.get('RECOVERY_TEST_FAIL')==kind:sys.exit(3)
'''
        fake=self.root/'bin/python3';fake.parent.mkdir();fake.write_text(spy);fake.chmod(0o755)
        self.serial=self.root/'third_party/esp-idf-tools-v5.5.6/python_env/idf5.5_py3.14_env/bin/python'
        self.serial.parent.mkdir(parents=True);self.serial.write_text(spy);self.serial.chmod(0o755)
        delegate=scripts/'mathsmith.sh';delegate.write_text(spy);delegate.chmod(0o755)
        self.env=dict(os.environ,PATH=str(fake.parent)+':'+os.environ['PATH'],RECOVERY_TEST_LOG=str(self.log))

    def run_script(self,*args,fail=''):
        r=subprocess.run([str(self.script),*args],capture_output=True,text=True,
            env=dict(self.env,RECOVERY_TEST_FAIL=fail),timeout=5)
        calls=[json.loads(s) for s in self.log.read_text().splitlines()] if self.log.exists() else []
        return r,calls

    def test_wrong_argument_count_never_runs_checks_or_serial(self):
        r,calls=self.run_script('flash-r4');self.assertNotEqual(r.returncode,0);self.assertEqual(calls,[])

    def test_failed_image_or_hash_check_never_opens_serial(self):
        for kind in ('image','hash'):
            with self.subTest(kind=kind):
                self.log.unlink(missing_ok=True)
                r,calls=self.run_script('flash-r4','/dev/test-board',fail=kind)
                self.assertNotEqual(r.returncode,0)
                self.assertFalse(any(c[0]==str(self.serial) for c in calls))

    def test_validated_recovery_uses_archived_image_and_4mb_profile(self):
        r,calls=self.run_script('flash-r4','/dev/test-board');self.assertEqual(r.returncode,0,r.stderr)
        self.assertEqual(len(calls),3)
        self.assertEqual(calls[-1],[str(self.serial),'-m','esptool','--chip','esp8266','--port',
            '/dev/test-board','--baud','115200','write_flash','--flash_size','4MB','--flash_mode',
            'dio','--flash_freq','40m','0x0',
            str(self.root/'build/esp8266/history/l01-r4-before-persistence/mathsmith_light.bin')])

    def test_normal_flash_still_delegates_to_current_candidate(self):
        r,calls=self.run_script('flash','/dev/test-board');self.assertEqual(r.returncode,0,r.stderr)
        self.assertEqual(calls,[[str(self.root/'esp8266/scripts/mathsmith.sh'),'flash','/dev/test-board']])
