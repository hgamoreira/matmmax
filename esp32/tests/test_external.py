"""E03 inherits the complete workstation suite and tests real ELF UART installs."""
import hashlib
import os
from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile
import unittest
import zlib
import test_workstation as workstation_tests
from run_external import BUILD
from run_console import qemu_command
from upload_external import Stream, UploadError, upload


class ExternalFirmware(workstation_tests.WorkstationFirmware):
    build_dir = BUILD
    banner = 'MathSmith E03'

    def transfer(self,c,data):
        stream = Stream(c.process.stdout.fileno(),c.process.stdin.fileno())
        stream.pending,c.pending = c.pending,b''
        try:
            result = upload(stream,data,sync=False)
        finally:
            c.pending += stream.pending
            c.transcript += stream.received
        c.read_until()
        return result

    def fixture(self,name='demo_b.msp'):
        return (BUILD/name).read_bytes()

    def open_external(self,c,name='uploaded',expect='B'):
        c.send(f':app {name} hello\r'.encode())
        output = c.read_until(b'demo> ')
        self.assertIn('External demo '+expect,output)
        self.assertNotIn('External state was not reset',output)
        return output

    def check_old_app(self,c):
        self.open_external(c)
        c.send(b'exit\r')
        self.assertIn('Returned to MathSmith',c.read_until())
        self.near(self.numbers(c,'x+1'),[42])

    def test_separately_compiled_sample_loads_and_returns(self):
        c,_ = self.start()
        c.command('x=41; A=[1 2;3 4]; f=@(t) t+x;')
        self.open_external(c,'sample','A')
        c.send(b'x=999\r')
        self.assertIn('Demo echo: x=999',c.read_until(b'demo> '))
        c.send(b'exit\r')
        c.read_until()
        self.near(self.numbers(c,'x+1'),[42])
        self.near(self.numbers(c,'A(2,1)'),[3])
        self.near(self.numbers(c,'f(1)'),[42])

    def test_host_upload_runs_new_code_without_firmware_change(self):
        c,_ = self.start()
        image = (BUILD/'flash.bin').read_bytes()
        digest = hashlib.sha256(image).digest()
        self.assertNotIn(b'External demo B:',image)
        c.command('x=41;')
        self.assertIn('INSTALLED uploaded',self.transfer(c,self.fixture()))
        self.assertIn('uploaded - Serial-installed app',c.command(':apps'))
        self.check_old_app(c)
        self.assertEqual(hashlib.sha256((BUILD/'flash.bin').read_bytes()).digest(),digest)

    def test_replace_program_without_reboot(self):
        c,_ = self.start()
        self.transfer(c,self.fixture('sample.msp'))
        self.open_external(c,expect='A')
        c.send(b'exit\r'); c.read_until()
        self.transfer(c,self.fixture())
        self.open_external(c)
        c.send(b'exit\r'); c.read_until()

    def test_corrupt_and_incompatible_uploads_keep_old_program(self):
        c,_ = self.start()
        c.command('x=41;')
        self.transfer(c,self.fixture())
        original = self.fixture()
        variants = []
        bad = bytearray(original); struct.pack_into('<I',bad,12,99); variants.append(bytes(bad))
        bad = bytearray(original); struct.pack_into('<I',bad,28,2); variants.append(bytes(bad))
        for transform in ('magic','machine','symbol','section'):
            bad = bytearray(original)
            if transform == 'magic': bad[32] = 0
            elif transform == 'machine': struct.pack_into('<H',bad,32+18,243)
            elif transform == 'symbol': bad = bad.replace(b'wk_app_api_v1',b'wk_app_api_v9')
            else: struct.pack_into('<I',bad,32+32,0xfffffff0)
            struct.pack_into('<I',bad,24,zlib.crc32(bad[32:]))
            variants.append(bytes(bad))
        for data in variants:
            with self.assertRaises(UploadError): self.transfer(c,data)
            c.read_until()
            self.check_old_app(c)

    def test_wire_crc_failure_keeps_old_program(self):
        c,_ = self.start()
        c.command('x=41;')
        data = self.fixture()
        self.transfer(c,data)
        c.send(f':upload {len(data)} 00000000\r'.encode())
        c.read_until(b'READY 256\r\n')
        for off in range(0,len(data),256):
            piece = data[off:off+256]
            c.send(piece)
            c.read_until(f'ACK {off+len(piece)}\r\n'.encode())
        self.assertIn('CRC32 mismatch; previous app kept',c.read_until())
        self.check_old_app(c)

    def test_partial_transfer_times_out_and_recovers(self):
        c,_ = self.start()
        c.command('x=41;')
        data = self.fixture()
        self.transfer(c,data)
        c.send(f':upload {len(data)} {zlib.crc32(data):08x}\r'.encode())
        c.read_until(b'READY 256\r\n')
        c.send(data[:17])
        self.assertIn('timeout; previous app kept',c.read_until(timeout=15))
        self.check_old_app(c)
        self.transfer(c,data)

    def test_unload_and_repeated_loads_release_memory(self):
        c,_ = self.start()
        c.command('x=41;')
        self.open_external(c,'sample','A')
        c.send(b'exit\r'); c.read_until()
        self.near(self.numbers(c,'x'),[41])
        before = c.command(':mem')
        for keys in (b'exit\r',b'\x1b',b'fail\r')*3:
            self.transfer(c,self.fixture())
            self.open_external(c)
            c.send(keys)
            output = c.read_until()
            self.assertIn('workspace kept',output.lower())
            c.command(':unload')
        after = c.command(':mem')
        for pattern in (r'Engine payload: used=(\d+)',r'external=(\d+) bytes'):
            self.assertEqual(re.search(pattern,before).group(1),re.search(pattern,after).group(1))
        self.assertIn('No uploaded app',c.command(':app uploaded'))

    def test_upload_header_bounds_and_pending_block(self):
        c,_ = self.start()
        for header in (':upload',':upload 262145 00000000',':upload -1 00000000',
                       ':upload 100 xyz',':upload 100 00000000 extra'):
            self.assertIn('ERROR upload header',c.command(header))
        c.send(b'for k=1:3\r'); c.read_until(b'..> ')
        c.send(b':upload 100 00000000\r')
        self.assertIn('Finish or abort',c.read_until(b'..> '))
        c.send(b':app sample\r')
        self.assertIn('Finish or abort',c.read_until(b'..> '))
        c.command('abort')
        self.near(self.numbers(c,'2+2'),[4])

    def test_reboot_discards_only_ram_install(self):
        c,_ = self.start()
        self.transfer(c,self.fixture())
        c.command(':reboot')
        self.assertIn('No uploaded app',c.command(':app uploaded'))
        self.open_external(c,'sample','A')
        c.send(b'exit\r'); c.read_until()

    def test_external_programs_without_psram_are_refused(self):
        c,_ = self.start(psram=False)
        self.assertIn('Not enough memory',c.command(':app sample'))
        with self.assertRaises(UploadError): self.transfer(c,self.fixture())
        c.read_until()
        self.assertIn('SDK:',c.command(':info'))


class HostUploaderCli(unittest.TestCase):
    def test_missing_and_oversized_files_fail_before_connecting(self):
        with tempfile.TemporaryDirectory(dir=BUILD) as directory:
            large = Path(directory)/'large.msp'
            large.write_bytes(b'x'*262145)
            for path in (large,Path(directory)/'missing.msp'):
                result = subprocess.run([sys.executable,'esp32/scripts/upload_external.py',
                                         '--qemu','--no-console',str(path)],capture_output=True,
                                        text=True,timeout=5)
                self.assertEqual(result.returncode,1)
                self.assertIn('Upload failed:',result.stderr)
                self.assertNotIn('Traceback',result.stderr)

    def test_qemu_cli_upload(self):
        result = subprocess.run([sys.executable,'esp32/scripts/upload_external.py','--qemu',
                                 '--no-console',str(BUILD/'demo_b.msp')],capture_output=True,
                                text=True,timeout=20)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        self.assertIn('INSTALLED uploaded',result.stdout)

    def test_virtual_serial_cli_upload(self):
        import shutil
        with tempfile.TemporaryDirectory(prefix='pty-',dir=BUILD) as directory:
            image = BUILD/'flash.bin'
            copy = Path(directory)/'flash.bin'
            shutil.copyfile(image,copy)
            process = subprocess.Popen(qemu_command(copy,serial='pty'),stdin=subprocess.DEVNULL,
                                       stdout=subprocess.PIPE,stderr=subprocess.STDOUT,bufsize=0)
            fd = None
            try:
                logs = Stream(process.stdout.fileno(),process.stdout.fileno())
                message = logs.until(b'(label serial0)\n')
                port = re.search(rb'char device redirected to (\S+)',message).group(1).decode()
                # QEMU's PTY is already raw. Preserve its startup bytes and wait
                # for the ROM/SDK to boot before testing UART synchronisation.
                fd = os.open(port,os.O_RDWR|os.O_NOCTTY|os.O_NONBLOCK)
                uart = Stream(fd,fd)
                uart.until(b'>> ')
                uart.write(b'x=41;\r')
                uart.until(b'>> ')
                os.close(fd); fd = None
                result = subprocess.run([sys.executable,'esp32/scripts/upload_external.py',
                                         '--port',port,'--no-console',str(BUILD/'demo_b.msp')],
                                        capture_output=True,text=True,timeout=20)
                self.assertEqual(result.returncode,0,result.stdout+result.stderr)
                fd = os.open(port,os.O_RDWR|os.O_NOCTTY|os.O_NONBLOCK)
                uart = Stream(fd,fd)
                uart.write(b':app uploaded\r')
                self.assertIn(b'External demo B',uart.until(b'demo> '))
                uart.write(b'exit\r'); uart.until(b'>> ')
                uart.write(b'x+1\r')
                self.assertIn(b'ans = 42',uart.until(b'>> '))
            finally:
                if fd is not None: os.close(fd)
                process.terminate()
                try: process.communicate(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill(); process.communicate(timeout=5)


if __name__ == '__main__': unittest.main()
