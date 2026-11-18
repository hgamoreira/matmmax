"""XD118 program store through the real firmware loop (POSIX HAL; a folder
stands in for LittleFS, so flash wear, timing and power loss are not tested)."""
import os
from pathlib import Path
import shutil
import tempfile
import time
import unittest

import test_web as base
from test_web import masked

def setUpModule(): base.setUpModule()

PROGRAM='s=0;\nfor k=1:8\n    s=s+k;\nend\ns\n'

class Store(unittest.TestCase):
    start=base.Device.start; socket=base.Device.socket; connect=base.Device.connect
    exact=base.Device.exact; frame=base.Device.frame; ready=base.Device.ready; open=base.Device.open
    command=base.Device.command

    def setUp(self):
        self.dir=tempfile.mkdtemp(prefix='store-',dir=base.BUILD)
        self.store=Path(self.dir)/'flash'
        self.start(MS_NATIVE_STORE=str(self.store))

    def tearDown(self):
        base.Device.tearDown(self)
        shutil.rmtree(self.dir,ignore_errors=True)

    def restart(self,**env):
        base.Device.tearDown(self); self.start(MS_NATIVE_STORE=str(self.store),**env)

    def output(self):
        self.log.seek(0); return self.log.read().decode()

    def request(self,s,payload):
        """Send one store request; return (console text, file data, result)."""
        s.sendall(masked(payload))
        text,data='',b''
        while True:
            opcode,frame=self.frame(s)
            if opcode==8: raise EOFError('server sent close')
            if frame[:1]==b'R': return text,data.decode(),frame[2:3]
            if frame[:1]==b'O':
                self.assertEqual(data,b'','console output inside a file reply'); text+=frame[1:].decode()
            else:
                self.assertEqual(frame[:1],b'F'); self.assertLessEqual(len(frame),128); data+=frame[1:]

    def save(self,s,name,source,piece=190):
        text,_,result=self.request(s,b'B'+name.encode())
        for i in range(0,len(source),piece):
            if result!=b'0': break
            more,_,result=self.request(s,b'D'+source[i:i+piece].encode()); text+=more
        if result==b'0':
            more,_,result=self.request(s,b'E'); text+=more
        return text,result

    def test_erased_flash_is_formatted_once_and_an_unreadable_store_is_kept(self):
        self.assertIn('erased flash found; formatted an empty store',self.output())
        self.assertTrue(self.store.is_dir())
        self.restart()
        self.assertIn('Program store ready',self.output())
        shutil.rmtree(self.store); self.store.write_text('not a store')
        self.restart()
        self.assertIn('Program store: not readable. :store format confirm',self.output())
        self.assertTrue(self.store.is_file(),'an unreadable store must not be formatted automatically')
        s=self.open()
        self.assertIn('not available',self.command(s,':store')); self.assertEqual(self.last_result,b'1')
        self.assertIn('error: the program store is not available',self.command(s,'ls'))
        self.assertIn('type :store format confirm',self.command(s,':store format').lower())
        self.assertTrue(self.store.is_file())
        self.assertIn('formatted; it is empty',self.command(s,':store format confirm'))
        self.assertTrue(self.store.is_dir()); self.assertEqual(self.last_result,b'0')
        self.assertIn('0 programs, 0 of 1000 KiB used',self.command(s,':store'))

    def test_save_list_open_run_and_bare_name(self):
        s=self.open()
        text,result=self.save(s,'SumUp',PROGRAM)
        self.assertEqual(result,b'0',text); self.assertIn('Saved sumup.m (32 bytes).',text)
        self.assertEqual((self.store/'sumup.m').read_text(),PROGRAM)
        self.assertFalse((self.store/'save.tmp').exists())
        self.assertEqual(self.request(s,b'L')[1:],('sumup 32\n',b'0'))
        self.assertEqual(self.request(s,b'Gsumup')[1:],(PROGRAM,b'0'))
        self.assertIn('s = 36',self.command(s,'run sumup')); self.assertEqual(self.last_result,b'0')
        self.assertIn('s = 36',self.command(s,'sumup')); self.assertEqual(self.last_result,b'0')
        self.assertIn('s = 36',self.command(s,'run SUMUP'))

    def test_large_program_round_trips_through_many_frames(self):
        s=self.open()
        source=''.join(f'v{k}={k};\n' for k in range(1500))[:9000]
        source=source[:source.rindex('\n')+1]
        text,result=self.save(s,'big',source); self.assertEqual(result,b'0',text)
        self.assertEqual((self.store/'big.m').read_text(),source)
        self.assertIn('ans = 4',self.command(s,'2+2'))      # console output first, then file frames
        text,data,result=self.request(s,b'Gbig')
        self.assertEqual((text,data,result),('',source,b'0'))

    def test_replacing_a_function_file_takes_effect_and_cache_is_dropped(self):
        s=self.open()
        self.assertEqual(self.save(s,'sq','function y=sq(x)\n  y=x^2;\nend\n')[1],b'0')
        self.assertIn('ans = 49',self.command(s,'sq(7)'))
        self.assertEqual(self.save(s,'sq','function y=sq(x)\n  y=x^3;\nend\n')[1],b'0')
        self.assertIn('ans = 343',self.command(s,'sq(7)'))
        self.assertIn('Deleted sq.m.',self.command(s,'rm sq'))
        self.assertIn('undefined',self.command(s,'sq(7)')); self.assertEqual(self.last_result,b'1')

    def test_console_ls_type_rm_respect_variables_and_names(self):
        for name,body in (('zeta','z=1\n'),('alpha','a=2\n'),('mid_1','m=3'),('Upper','u=4\n')):
            (self.store/f'{name}.m').write_text(body)
        (self.store/'notes.txt').write_text('ignored')
        s=self.open()
        listing=self.command(s,'ls')
        self.assertEqual([line.split()[0] for line in listing.splitlines() if line.startswith('  ')][:3],
                         ['alpha','mid_1','zeta'])
        self.assertNotIn('notes',listing); self.assertNotIn('Upper',listing)
        self.assertIn('3 programs;',listing)
        self.assertEqual(self.command(s,'dir').split('\n',1)[1],listing.split('\n',1)[1])
        self.assertEqual(self.command(s,'type mid_1'),'type mid_1\nm=3\n>> ')
        self.assertIn('error: no saved program nothing.m',self.command(s,'type nothing'))
        self.assertEqual(self.last_result,b'1')
        self.assertIn('error: type NAME',self.command(s,'type')); self.assertEqual(self.last_result,b'1')
        self.assertIn('names are a letter',self.command(s,'rm 1abc'))
        self.assertIn('Deleted zeta.m.',self.command(s,'delete zeta'))
        self.assertFalse((self.store/'zeta.m').exists())
        self.command(s,'ls=3;'); self.assertIn('ls = 3',self.command(s,'ls'))
        self.command(s,'for k=1:2')
        self.assertNotIn('alpha',self.command(s,'dir'))       # inside a block it is block text
        self.command(s,'end')
        self.assertIn('cannot find the file absent.m',self.command(s,'run absent'))

    def test_rejected_saves_keep_the_previous_program(self):
        s=self.open()
        self.assertEqual(self.save(s,'keep',PROGRAM)[1],b'0')
        text,_,result=self.request(s,b'B1bad'); self.assertEqual(result,b'1'); self.assertIn('names are a letter',text)
        text,result=self.save(s,'keep','x=1;\n'*3300)
        self.assertEqual(result,b'1'); self.assertIn('limited to 16384 bytes; nothing was saved',text)
        text,_,result=self.request(s,b'E'); self.assertEqual(result,b'1'); self.assertIn('no save in progress',text)
        self.assertEqual((self.store/'keep.m').read_text(),PROGRAM)
        self.assertFalse((self.store/'save.tmp').exists())
        self.assertEqual(self.request(s,b'L')[1],'keep 32\n')

    def test_store_full_and_rename_failure_report_and_clean_up(self):
        self.restart(MS_TEST_STORE_FULL='40')
        s=self.open()
        self.assertEqual(self.save(s,'keep',PROGRAM)[1],b'0')
        text,result=self.save(s,'more',PROGRAM)
        self.assertEqual(result,b'1'); self.assertIn('flash write failed (is the store full?)',text)
        self.assertFalse((self.store/'more.m').exists()); self.assertFalse((self.store/'save.tmp').exists())
        self.restart(MS_TEST_STORE_RENAME_FAIL='1')
        s=self.open()
        text,result=self.save(s,'keep','x=1;\n')
        self.assertEqual(result,b'1'); self.assertIn('could not replace the saved program',text)
        self.assertEqual((self.store/'keep.m').read_text(),PROGRAM)
        self.assertFalse((self.store/'save.tmp').exists())

    def test_disconnect_stop_or_command_abandon_an_unfinished_save(self):
        s=self.open()
        self.assertEqual(self.request(s,b'Bhalf')[2],b'0'); self.assertEqual(self.request(s,b'Dx=1;\n')[2],b'0')
        s.close(); time.sleep(.3)   # let the loop process the disconnect's own cancel/reset first
        s=self.open()
        self.assertFalse((self.store/'save.tmp').exists()); self.assertFalse((self.store/'half.m').exists())
        self.request(s,b'Bhalf'); self.request(s,b'Dx=1;\n')
        s.sendall(masked(b'S')); self.ready(s); self.assertEqual(self.last_result,b'2')
        self.assertIn('no save in progress',self.request(s,b'E')[0])
        self.request(s,b'Bhalf'); self.request(s,b'Dx=1;\n'); self.command(s,'2+2')
        self.assertEqual(self.request(s,b'E')[2],b'1')
        self.assertFalse((self.store/'half.m').exists()); self.assertFalse((self.store/'save.tmp').exists())

    def test_frames_are_validated(self):
        s=self.open()
        for bad in (b'Dtab\there',b'Cline\nbreak',b'Lextra',b'Eextra',b'X'):
            s2=self.open() if bad!=b'Dtab\there' else s
            s2.sendall(masked(bad))
            opcode,data=self.frame(s2)
            self.assertEqual((opcode,data),(8,b'\x03\xf0'),bad)
        s=self.open()
        text,data,result=self.request(s,b'Gmissing')
        self.assertEqual((data,result),('',b'1')); self.assertIn('no saved program with that name',text)
        self.assertEqual(self.request(s,b'L')[1:],('',b'0'))

    def test_store_disabled_on_layout_mismatch(self):
        self.restart(MS_TEST_STORE_LAYOUT_BAD='1')
        self.assertIn('Program store: disabled (flash layout differs',self.output())
        s=self.open()
        self.assertIn('disabled',self.command(s,':store'))
        self.assertIn('not available',self.request(s,b'Bx')[0])
        self.assertEqual(self.request(s,b'L')[1:],('',b'0'))
        self.assertIn('cannot find the file',self.command(s,'run x'))

    def test_shipped_ledtest_runs_from_the_store(self):
        self.restart(MS_TEST_GPIO_TRACE='1')
        s=self.open()
        source=(base.ROOT/'apps/esp8266/l01_webrepl/examples/ledtest.m').read_text()
        self.assertEqual(self.save(s,'ledtest',source)[1],b'0')
        output=self.command(s,'ledtest'); self.assertEqual(self.last_result,b'0',output)
        self.assertIn('blink 5 of 5',output); self.assertIn('ans = 1',output)
        levels=[int(v) for v in __import__('re').findall(r'Native GPIO write GPIO2=([01])',self.output())]
        self.assertEqual(levels,[1]+[0,1]*5)

    def test_page_offers_board_controls_and_build_label(self):
        page=(base.ROOT/'apps/esp8266/l01_webrepl/index.html').read_text()
        for needed in ('id="boardfiles"','id="boardopen"','id="boardsave"','id="boardrun"','L02-r2',
                       "boardOp('L')","boardOp('B'+name)","boardOp('E')","sendCommand('run '+name)"):
            self.assertIn(needed,page)
        self.assertIn('Build: L02-r2',self.output())

if __name__=='__main__': unittest.main()
