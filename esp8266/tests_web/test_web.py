"""Real socket tests of the firmware loop through POSIX HAL, plus target fit."""
import base64
import ctypes
import hashlib
import json
import os
from pathlib import Path
import re
import socket
import struct
import subprocess
import sys
import tempfile
import time
import unittest

ROOT=Path(__file__).resolve().parents[2]
os.environ['MS_ESP8266_APP']='l01_webrepl'
sys.path.insert(0,str(ROOT/'esp8266/scripts'))
from native import build
from light_profile import BUILD, inputs_fingerprint
from check_image import check, TOOL

def setUpModule(): build()

def masked(data,opcode=1,fin=True):
    mask=b'\x12\x34\x56\x78'
    header=bytes([(128 if fin else 0)|opcode])
    header+=bytes([128|len(data)]) if len(data)<126 else b'\xfe'+struct.pack('!H',len(data))
    return header+mask+bytes(c^mask[i%4] for i,c in enumerate(data))

class Frame(ctypes.Structure):
    _fields_=[('stage',ctypes.c_uint),('opcode',ctypes.c_uint),('length',ctypes.c_uint),
              ('count',ctypes.c_uint),('mask',ctypes.c_ubyte*4),('data',ctypes.c_ubyte*193)]

class Protocol(unittest.TestCase):
    def setUp(self):
        self.lib=ctypes.CDLL(str(BUILD/'native/protocol.so'))
        self.lib.ms_frame_byte.argtypes=[ctypes.POINTER(Frame),ctypes.c_ubyte]
        self.lib.ms_web_accept.argtypes=[ctypes.c_char_p,ctypes.c_char_p]
        self.lib.ms_http_request.argtypes=[ctypes.c_char_p]*4

    def test_handshake_matches_rfc_and_python_sha1(self):
        for key in [b'dGhlIHNhbXBsZSBub25jZQ==']+[base64.b64encode(os.urandom(16)) for _ in range(30)]:
            out=ctypes.create_string_buffer(29)
            self.lib.ms_web_accept(key,out)
            self.assertEqual(out.value,base64.b64encode(hashlib.sha1(key+b'258EAFA5-E914-47DA-95CA-C5AB0DC85B11').digest()))

    def test_masked_frames_at_boundaries_and_back_to_back(self):
        p=Frame()
        for n in (0,1,125,126,192):
            data=b'C'+b'x'*(n-1) if n else b''
            results=[self.lib.ms_frame_byte(ctypes.byref(p),c) for c in masked(data)]
            self.assertEqual(results[-1],1)
            self.assertNotIn(-1,results)
            self.assertEqual(bytes(p.data)[:n],data)

    def test_malformed_frames_rejected_before_overflow(self):
        for data in (b'\x81\x01X',masked(b'C'*193),masked(b'x',fin=False),
                     masked(b'x',opcode=2),b'\x89\xfe',b'\xc1\x80',b'\x81\xff'):
            p=Frame()
            self.assertIn(-1,[self.lib.ms_frame_byte(ctypes.byref(p),c) for c in data],data)

    def test_http_header_validation(self):
        token=b'a'*32
        request=(b'GET /ws HTTP/1.1\r\nHost: 127.0.0.1:80\r\nOrigin: http://127.0.0.1:80\r\n'
                 b'Upgrade: websocket\r\nConnection: keep-alive, Upgrade\r\nSec-WebSocket-Version: 13\r\n'
                 b'Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Protocol: mathsmith.'+token+b'\r\n\r\n')
        def status(data):
            return self.lib.ms_http_request(ctypes.create_string_buffer(data),b'127.0.0.1:80',token,ctypes.create_string_buffer(29))
        self.assertEqual(status(request),101)
        named=request.replace(b'127.0.0.1:80',b'mathsmith.local:80')
        self.assertEqual(status(named),101)
        self.assertEqual(status(named.replace(b'Origin: http://mathsmith.local:80',b'Origin: http://evil.test')),403)
        self.assertEqual(status(named.replace(b'mathsmith.local:80',b'mathsmith.local.evil.test:80')),403)
        self.assertEqual(status(request.replace(b'websocket',b'WebSocket')),101)
        for old,new in [(b'Host: 127.0.0.1:80',b'Host: evil.test'),
                        (b'Origin: http://127.0.0.1:80',b'Origin: http://evil.test'),
                        (token,b'b'*32)]: self.assertEqual(status(request.replace(old,new)),403)
        for data in (request.replace(b'13\r\n',b'12\r\n'),request.replace(b'Host:',b'Host: invalid\r\nHost:'),
                     request.replace(b'\r\n\r\n',b'\r\nContent-Length: 2\r\n\r\n')):
            self.assertEqual(status(data),400)

class Device(unittest.TestCase):
    def setUp(self):
        self.start()

    def start(self,**env):
        self.log=tempfile.TemporaryFile()
        child_env=dict(os.environ,MS_NATIVE_WIFI='1'); child_env.update(env)
        self.proc=subprocess.Popen([str(BUILD/'native/mathsmith_light')],stdin=subprocess.PIPE,
            stdout=self.log,stderr=subprocess.STDOUT,env=child_env)
        self.sockets=[]
        deadline=time.monotonic()+5
        while time.monotonic()<deadline:
            self.log.seek(0); output=self.log.read().decode()
            match=re.search(r'Browser URL: http://127.0.0.1:(\d+)/#([a-f0-9]{32})',output)
            if match:
                self.port=int(match[1]); self.token=match[2]; return
            time.sleep(.01)
        self.fail('Native firmware server did not start: '+output)

    def tearDown(self):
        for s in self.sockets: s.close()
        self.proc.terminate(); self.proc.wait(timeout=3)
        self.proc.stdin.close(); self.log.close()

    def socket(self):
        s=socket.create_connection(('127.0.0.1',self.port),timeout=3)
        self.sockets.append(s); return s

    def connect(self,token=None,origin=None):
        s=self.socket(); host=f'127.0.0.1:{self.port}'
        s.sendall((f'GET /ws HTTP/1.1\r\nHost: {host}\r\nOrigin: {origin or "http://"+host}\r\n'
            'Upgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Version: 13\r\n'
            'Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n'
            f'Sec-WebSocket-Protocol: mathsmith.{token or self.token}\r\n\r\n').encode())
        response=b''
        while not response.endswith(b'\r\n\r\n'):
            part=s.recv(1)
            if not part: raise EOFError('Closed during HTTP reply: '+repr(response))
            response+=part
        return s,response

    def exact(self,s,n):
        data=b''
        while len(data)<n:
            part=s.recv(n-len(data))
            if not part: raise EOFError('socket closed')
            data+=part
        return data

    def frame(self,s):
        first,second=self.exact(s,2); n=second&127
        if n==126: n=struct.unpack('!H',self.exact(s,2))[0]
        self.assertFalse(second&128)
        return first&15,self.exact(s,n)

    def ready(self,s):
        text=''
        for _ in range(10000):
            opcode,data=self.frame(s)
            if opcode==8: raise EOFError('server sent close')
            self.assertEqual(opcode,2)
            if data[:1]==b'R':
                self.assertEqual(len(data),3)
                self.assertIn(data[2:3],(b'0',b'1',b'2'))
                self.last_result=data[2:3]
                return text,data[1:2]
            self.assertEqual(data[:1],b'O'); text+=data[1:].decode()
        self.fail('No readiness frame')

    def open(self):
        s,response=self.connect(); self.assertIn(b'101 Switching',response); self.ready(s); return s

    def command(self,s,line):
        s.sendall(masked(('C'+line).encode())); return self.ready(s)[0]

    def test_engine_results_are_explicit_and_output_text_is_not_an_error_status(self):
        s=self.open()
        self.command(s,'x=41;');self.assertEqual(self.last_result,b'0')
        self.assertIn('error: example',self.command(s,"disp('error: example')"))
        self.assertEqual(self.last_result,b'0')
        self.assertIn('undefined',self.command(s,'unknown_editor_variable'))
        self.assertEqual(self.last_result,b'1')
        self.command(s,'zeros(9,9)');self.assertEqual(self.last_result,b'1')
        self.assertIn('ans = 42',self.command(s,'x+1'));self.assertEqual(self.last_result,b'0')

    def test_result_status_covers_open_block_validation_and_abort(self):
        s=self.open();self.command(s,'for k=1:8');self.assertEqual(self.last_result,b'0')
        self.command(s,'('*9+'1'+')'*9);self.assertEqual(self.last_result,b'1')
        s.sendall(masked(b'S'));self.ready(s);self.assertEqual(self.last_result,b'2')
        self.assertIn('ans = 4',self.command(s,'2+2'))
        self.command(s,':unknown');self.assertEqual(self.last_result,b'1')
        self.command(s,'quit');self.assertEqual(self.last_result,b'1')

    def test_shipped_editor_examples_execute_with_existing_engine_limits(self):
        s=self.open()
        outputs={}
        for name in ('sum','solve','square','blink'):
            output=''
            for line in (ROOT/'apps/esp8266/l01_webrepl/examples'/f'{name}.ms').read_text().splitlines():
                output+=self.command(s,line)
                self.assertEqual(self.last_result,b'0',(name,line,output))
            outputs[name]=output
        self.assertIn('s = 36',outputs['sum'])
        self.assertRegex(outputs['solve'],r'x =\s+2\s+3\s')
        self.assertIn('ans = 49',outputs['square'])
        self.assertIn('ans = 1',outputs['blink'])

    def test_exact_blink_example_issues_five_low_high_pairs(self):
        self.tearDown();self.start(MS_TEST_GPIO_TRACE='1')
        s=self.open()
        for count,name in enumerate(('blink','blink_count'),1):
            output=''
            for line in (ROOT/'apps/esp8266/l01_webrepl/examples'/f'{name}.ms').read_text().splitlines():
                output+=self.command(s,line);self.assertEqual(self.last_result,b'0')
            self.log.seek(0);log=self.log.read().decode()
            levels=[int(value) for value in re.findall(r'Native GPIO write GPIO2=([01])',log)]
            self.assertEqual(levels,([1]+[0,1]*5)*count)
            self.assertIn('k = 5',self.command(s,'k'))
            if name=='blink_count':self.assertRegex(output,r'1\s+2\s+3\s+4\s+5\s')

    def test_page_self_contained_and_correct_length(self):
        s=self.socket(); s.sendall(f'GET / HTTP/1.1\r\nHost: 127.0.0.1:{self.port}\r\n\r\n'.encode())
        data=b''
        while True:
            part=s.recv(4096)
            if not part: break
            data+=part
        header,body=data.split(b'\r\n\r\n',1)
        self.assertIn(b'200 OK',header)
        self.assertEqual(int(re.search(rb'Content-Length: (\d+)',header)[1]),len(body))
        self.assertEqual(body,(ROOT/'apps/esp8266/l01_webrepl/index.html').read_bytes())
        self.assertNotIn(b'<script src=',body)

    def test_auth_and_same_origin_before_any_command(self):
        for options in ({'token':'0'*32},{'origin':'http://evil.test'}):
            s,response=self.connect(**options); self.assertIn(b'403',response); s.close(); time.sleep(.02)
        s=self.open(); self.assertIn('ans = 4',self.command(s,'2+2'))

    def test_math_workspace_limits_and_reconnect(self):
        s=self.open()
        self.command(s,'x=41; A=[4 -2;-2 4]; b=A\\[2;8];')
        self.assertIn('ans = 42',self.command(s,'x+1'))
        self.assertIn('ans = 3',self.command(s,'b(2)'))
        self.assertIn('larger than 64 elements',self.command(s,'zeros(9,9)'))
        s.close(); time.sleep(.03); s=self.open()
        self.assertIn('ans = 42',self.command(s,'x+1'))

    def test_program_prompt_and_stop_during_running_math(self):
        s=self.open(); self.command(s,'x=41; s=0;')
        s.sendall(masked(b'Cfor k=1:100000000')); self.assertEqual(self.ready(s)[1],b'1')
        self.command(s,'s=s+k;')
        s.sendall(masked(b'Cend')); time.sleep(.025); s.sendall(masked(b'S'))
        output,pending=self.ready(s)
        self.assertIn('interrupted',output.lower()); self.assertEqual(pending,b'0')
        self.assertEqual(self.last_result,b'2')
        self.assertIn('ans = 42',self.command(s,'x+1'))

    def test_stop_pending_block_and_disconnect_during_calculation(self):
        s=self.open(); self.command(s,'x=41;')
        self.command(s,'while 1'); s.sendall(masked(b'S'))
        self.assertIn('Cancelled',self.ready(s)[0])
        self.command(s,'while 1'); self.command(s,'x=x;')
        s.sendall(masked(b'Cend')); time.sleep(.02); s.close(); time.sleep(.05)
        s=self.open(); self.assertIn('ans = 42',self.command(s,'x+1'))

    def test_gpio_modes_aliases_and_invalid_commands(self):
        s=self.open()
        self.assertIn('output first',self.command(s,':write D1 1'))
        self.assertIn('D1 GPIO5: output',self.command(s,':pin D1 output 0'))
        self.assertIn('D1 = 1',self.command(s,':write D1 1'))
        self.assertIn('D1 = 1',self.command(s,':read D1'))

        self.assertIn('A0 = 321',self.command(s,':analog A0')) # Native stub, not ADC accuracy.
        for line in (':pin D0 pullup',':pin D1 output 2',':write D1 1 trailing',':read GPIO6',':read 1',':read D9'):
            self.assertIn('error:',self.command(s,line))
        self.assertIn('D1 = 1',self.command(s,':read D1'))

    def test_gpio_builtins_share_configuration_with_console_and_return_scalars(self):
        s=self.open()
        self.command(s,"pinmode('D4','output',1);")
        self.assertEqual(self.last_result,b'0')
        self.assertIn('D4 = 1',self.command(s,':read D4'))
        self.command(s,':write D4 0')
        self.assertIn('ans = 0',self.command(s,"digitalread('D4')"))
        self.command(s,':pin D1 output 0')
        self.assertIn('ans = 0',self.command(s,"digitalwrite('D1',1)"))
        self.assertIn('D1 = 1',self.command(s,':read D1'))
        self.assertIn('ans = 321',self.command(s,'analogread()'))
        self.assertIn('ans = 321',self.command(s,"analogread('A0')"))
        self.command(s,"pinmode('D2','pullup');")
        self.assertIn('ans = 1',self.command(s,"digitalread('D2')"))
        self.command(s,':reset')
        self.assertIn('ans = 1',self.command(s,"digitalread('D1')"))

    def test_gpio_builtin_restrictions_types_and_arity_leave_pin_unchanged(self):
        s=self.open();self.command(s,"pinmode('D4','output',1);")
        cases=["digitalwrite('D1',1)","digitalwrite('D4',2)","digitalwrite('D4',NaN)",
            "digitalwrite('D4',Inf)","digitalwrite('D4',0.5)","digitalwrite('D4','0')",
            "digitalwrite('D4',[0 1])","digitalwrite('D4',-1)","digitalwrite('TX',0)",
            "digitalread('GPIO6')","digitalread('D9')","digitalread(4)","digitalread('')",
            "pinmode('D0','pullup')","pinmode('D4','unknown')","pinmode('D4','input',1)",
            "pinmode('D4','output',2)","analogread('D1')","delay(-1)","delay(60001)",
            "delay(0.1)","delay(NaN)","delay(Inf)","delay('10')","delay([1 2])",
            "pinmode()","digitalwrite('D4')","digitalread('D4',0)","analogread('A0',1)","delay()"]
        for line in cases:
            with self.subTest(line=line):
                self.assertIn('error:',self.command(s,line));self.assertEqual(self.last_result,b'1')
                self.assertIn('D4 = 1',self.command(s,':read D4'))
        self.assertIn('ans = 4',self.command(s,'2+2'))

    def test_gpio_functions_work_in_loops_functions_and_matrix_values(self):
        s=self.open();self.command(s,"pinmode('D4','output',1);")
        for line in ['for k=1:4',"digitalwrite('D4',mod(k,2));",'delay(1);','end']:
            self.command(s,line);self.assertEqual(self.last_result,b'0')
        self.assertIn('ans = 0',self.command(s,"digitalread('D4')"))
        for line in ['function y=sample()',"y=analogread('A0');",'end','samples=[sample() digitalread(\'D4\')];']:
            self.command(s,line);self.assertEqual(self.last_result,b'0')
        self.assertIn('ans = 321',self.command(s,'samples(1)'))
        self.assertIn('ans = 0',self.command(s,'samples(2)'))

    def test_delay_elapsed_and_browser_stop_preserve_completed_gpio_changes(self):
        s=self.open();self.command(s,"pinmode('D4','output',0);")
        started=time.monotonic();self.command(s,'delay(30);')
        self.assertGreaterEqual(time.monotonic()-started,.025)
        started=time.monotonic();s.sendall(masked(b'Cdelay(60000);'))
        time.sleep(.025);s.sendall(masked(b'S'));output,pending=self.ready(s)
        self.assertLess(time.monotonic()-started,1.0)
        self.assertIn('interrupted',output.lower());self.assertEqual(self.last_result,b'2')
        self.assertEqual(pending,b'0');self.assertIn('D4 = 0',self.command(s,':read D4'))
        self.assertIn('ans = 4',self.command(s,'2+2'))
        self.command(s,'delay(0)');self.assertEqual(self.last_result,b'0')

    def test_disconnect_during_delay_cancels_and_reconnect_preserves_pin_state(self):
        s=self.open();self.command(s,"pinmode('D4','output',0);")
        s.sendall(masked(b'Cdelay(60000);'));time.sleep(.025);s.close();time.sleep(.05)
        s=self.open();self.assertIn('ans = 0',self.command(s,"digitalread('D4')"))
        self.assertIn('ans = 4',self.command(s,'2+2'))

    def test_serial_recovery_interrupts_gpio_delay_and_preserves_workspace(self):
        s=self.open();self.command(s,'x=41;')
        s.sendall(masked(b'Cdelay(60000);'));time.sleep(.025)
        self.proc.stdin.write(b'\x03');self.proc.stdin.flush();time.sleep(.05)
        s.close();s=self.open()
        self.assertIn('ans = 42',self.command(s,'x+1'))

    def test_repeated_gpio_success_and_error_free_temporary_engine_values(self):
        s=self.open();self.command(s,"pinmode('D4','output',1);")
        cycle=["digitalwrite('D4',0);",'delay(0);',"digitalread('D4');",'analogread();',
               "digitalwrite('D4',1);","digitalwrite('D4',2);","pinmode('D0','pullup');","analogread('D1');"]
        for line in cycle:self.command(s,line)
        before=int(re.search(r'Engine payload: used=(\d+)',self.command(s,':mem'))[1])
        for _ in range(10):
            for line in cycle:self.command(s,line)
        after=int(re.search(r'Engine payload: used=(\d+)',self.command(s,':mem'))[1])
        self.assertEqual(after,before)
        self.assertIn('D4 = 1',self.command(s,':read D4'))

    def test_serial_ownership_recovery_and_wifi_restriction(self):
        s=self.open(); self.command(s,'x=41;')
        self.proc.stdin.write(b'x=99;\n'); self.proc.stdin.flush(); time.sleep(.03)
        self.assertIn('ans = 42',self.command(s,'x+1'))
        self.assertIn('serial-only',self.command(s,':wifi join "ssid" "secret"'))
        self.proc.stdin.write(b'\x03'); self.proc.stdin.flush(); time.sleep(.03)
        s.close(); s=self.open(); self.assertIn('ans = 42',self.command(s,'x+1'))

    def test_ping_and_invalid_or_oversize_messages(self):
        s=self.open(); s.sendall(masked(b'hello',opcode=9))
        self.assertEqual(self.frame(s),(10,b'hello'))
        self.assertIn('ans = 4',self.command(s,'2+2'))
        for data in (b'C'+b'x'*192,b'C:write D1 1\n',b'C\x00',b'junk'):
            s.sendall(masked(data))
            try: self.assertEqual(self.frame(s)[0],8)
            except (EOFError,ConnectionResetError): pass
            s.close(); time.sleep(.02); s=self.open()
        self.assertIn('ans = 4',self.command(s,'2+2'))

    def test_second_client_cannot_replace_owner(self):
        s=self.open(); self.command(s,'x=41;')
        other=self.socket(); other.sendall(b'GET / HTTP/1.1\r\n\r\n')
        try: self.assertEqual(other.recv(10),b'')
        except ConnectionResetError: pass
        self.assertIn('ans = 42',self.command(s,'x+1'))

    def test_got_ip_event_starts_browser_even_when_sdk_status_is_connecting(self):
        self.tearDown(); self.start(MS_TEST_WIFI_SDK_STATE='1',MS_TEST_WIFI_HAS_IP='1')
        s=self.open()
        output=self.command(s,':wifi')
        self.assertIn('Wi-Fi status=7',output)
        self.assertIn('SDK=1 (connecting)',output)
        self.assertIn('IP=127.0.0.1',output)
        self.assertIn('got-ip-events=1',output)
        self.assertIn('ready=1',output)
        self.assertIn('SDK status is stale',output)
        self.assertIn('ans = 4',self.command(s,'2+2'))

    def test_output_backpressure_disconnects_and_serial_recovers(self):
        self.tearDown(); self.start(MS_TEST_SOCKET_STALL_AFTER='700')
        s=self.open(); self.command(s,'x=41;')
        time.sleep(.75)
        s.sendall(masked(b'Cfor k=1:2000, disp(k), end'))
        try:
            while s.recv(4096): pass
        except ConnectionResetError: pass
        time.sleep(.03) # Let cooperative cancellation unwind before serial input.
        self.proc.stdin.write(b'x+1\n'); self.proc.stdin.flush()
        deadline=time.monotonic()+2
        while time.monotonic()<deadline:
            self.log.seek(0); output=self.log.read().decode()
            if 'ans = 42' in output: break
            time.sleep(.01)
        self.assertIn('ans = 42',output)

    def test_partial_http_times_out_and_next_client_works(self):
        s=self.socket(); s.settimeout(7); s.sendall(b'GET / HTTP/1.1\r\nHost:')
        self.assertEqual(s.recv(20),b''); s.close()
        s=self.open(); self.assertIn('ans = 4',self.command(s,'2+2'))

    def test_serial_credentials_masked_and_platform_reset_keeps_output(self):
        self.proc.stdin.write(b':wifi join "native" "do-not-print-me"\n'); self.proc.stdin.flush()
        time.sleep(.12); self.log.seek(0); output=self.log.read().decode()
        self.assertNotIn('do-not-print-me',output)
        self.assertIn(':wifi join "native" ',output)
        self.port=int(re.findall(r'Browser URL: http://127.0.0.1:(\d+)',output)[-1])
        s=self.open(); self.command(s,':pin D1 output 1'); self.command(s,':reset')
        self.assertIn('D1 = 1',self.command(s,':read D1'))

    def serial_until(self,expected,count=1):
        deadline=time.monotonic()+3
        while time.monotonic()<deadline:
            self.log.seek(0); output=self.log.read().decode()
            if output.count(expected)>=count: return output
            time.sleep(.01)
        self.fail(f'Missing serial output {expected!r}: {output}')

    def serial(self,data):
        self.proc.stdin.write(data); self.proc.stdin.flush()

    def test_guided_wifi_setup_with_visible_ssid_hidden_password_and_arrows(self):
        self.serial(b':wifi join\r\n')
        self.serial_until('Wi-Fi SSID (visible): ')
        # Split CSI/SS3 sequences across USB reads, as a real terminal can.
        self.serial(b'native\x1b'); time.sleep(.015); self.serial(b'[A\x1b[B\x1b[C\x1b[D')
        self.serial(b'\x1b[3~\x1bOH net\r\n')
        self.serial_until('Wi-Fi password (hidden; Enter for open network): ')
        s,response=self.connect(); self.assertIn(b'409',response); s.close()
        self.serial(b'sensitive-passworX\bd\r\n')
        output=self.serial_until('Wi-Fi SSID (visible): native net\n')
        self.assertNotIn('[A',output); self.assertNotIn('[B',output)
        self.assertNotIn('[C',output); self.assertNotIn('[D',output)
        self.assertNotIn('Cancelled.',output)
        self.assertNotIn('sensitive-passwor',output)
        deadline=time.monotonic()+2
        while time.monotonic()<deadline:
            self.log.seek(0); output=self.log.read().decode()
            urls=re.findall(r'Browser URL: http://127.0.0.1:(\d+)',output)
            if len(urls)>=2: break
            time.sleep(.01)
        self.assertGreaterEqual(len(urls),2,output)
        self.port=int(urls[-1]); s=self.open()
        self.assertIn('ans = 4',self.command(s,'2+2'))

    def test_guided_wifi_validation_open_network_and_cancel(self):
        self.serial(b':wifi join\n\n'+b'x'*33+b'\n')
        self.serial_until('SSID must contain 1..32 characters.')
        self.serial(b'native\n'+b'x'*64+b'\n')
        self.serial_until('Password must contain at most 63 characters.')
        self.serial(b'\x1b')
        self.serial_until('Cancelled.')
        self.serial(b'2+2\n'); self.serial_until('ans = 4')
        self.serial(b':wifi join\nnative\n\n')
        deadline=time.monotonic()+2
        while time.monotonic()<deadline:
            self.log.seek(0); output=self.log.read().decode()
            if len(re.findall('Browser URL:',output))>=2: break
            time.sleep(.01)
        self.assertGreaterEqual(output.count('Browser URL:'),2,output)

    def test_serial_arrows_do_not_cancel_calculation_and_ctrl_c_still_does(self):
        self.serial(b'x=41; s=0;\nfor k=1:100000000\ns=s+k;\nend\n')
        self.serial_until('..> end\n')
        self.serial(b'\x1b[A\x1b[B\x1b[C\x1b[D')
        time.sleep(.05); self.log.seek(0); output=self.log.read().decode()
        self.assertNotIn('interrupted',output.lower())
        self.serial(b'\x03'); self.serial_until('error: interrupted')
        self.serial(b'x+1\n'); self.serial_until('ans = 42')

    def test_saved_wifi_and_key_survive_restart_without_serial_setup(self):
        with tempfile.TemporaryDirectory() as folder:
            path=str(Path(folder)/'settings.bin')
            self.tearDown(); self.start(MS_SETTINGS_FILE=path)
            first_key=self.token
            stored=Path(path).read_bytes(); timestamp=Path(path).stat().st_mtime_ns
            s=self.open(); self.command(s,'x=41;')
            self.tearDown()
            self.start(MS_SETTINGS_FILE=path,MS_NATIVE_WIFI='0',MS_TEST_EXPECT_SSID='native',MS_TEST_EXPECT_PASSWORD='')
            self.assertEqual(self.token,first_key)
            self.assertEqual(Path(path).read_bytes(),stored)
            self.assertEqual(Path(path).stat().st_mtime_ns,timestamp)
            self.serial_until('Saved Wi-Fi found; reconnecting automatically.')
            s=self.open();self.assertIn('Saved network=yes',self.command(s,':wifi'))
            self.assertIn('undefined',self.command(s,'x').lower())
            self.assertIn('ans = 4',self.command(s,'2+2'))

    def test_forget_is_serial_only_and_disables_next_boot_join(self):
        with tempfile.TemporaryDirectory() as folder:
            path=str(Path(folder)/'settings.bin')
            self.tearDown();self.start(MS_SETTINGS_FILE=path)
            s=self.open();self.assertIn('serial-only',self.command(s,':wifi forget'))
            self.serial(b'\x03');time.sleep(.03);self.serial(b':wifi forget\n')
            self.serial_until('Saved Wi-Fi and browser key forgotten.')
            self.tearDown()
            r=subprocess.run([str(BUILD/'native/mathsmith_light')],input=b':wifi\n2+2\n',capture_output=True,
                timeout=5,env=dict(os.environ,MS_SETTINGS_FILE=path,MS_NATIVE_WIFI='0'))
            self.assertEqual(r.returncode,0,r.stderr.decode())
            self.assertIn(b'No saved network',r.stdout)
            self.assertIn(b'Saved network=no',r.stdout)
            self.assertNotIn(b'Browser URL:',r.stdout)
            self.assertIn(b'ans = 4',r.stdout)
            self.start() # Restore a live fixture for tearDown.

    def test_failed_new_network_does_not_replace_previous_settings(self):
        with tempfile.TemporaryDirectory() as folder:
            path=str(Path(folder)/'settings.bin')
            self.tearDown();self.start(MS_SETTINGS_FILE=path)
            previous=Path(path).read_bytes();self.tearDown()
            r=subprocess.run([str(BUILD/'native/mathsmith_light')],input=b':wifi join\nnew net\nprivate-pass\n:wifi\n',
                capture_output=True,timeout=5,env=dict(os.environ,MS_NATIVE_WIFI='0',MS_SETTINGS_FILE=path,MS_TEST_WIFI_SDK_STATE='1'))
            self.assertEqual(r.returncode,0,r.stderr.decode())
            self.assertIn(b'save-pending=yes',r.stdout);self.assertNotIn(b'private-pass',r.stdout)
            self.assertEqual(Path(path).read_bytes(),previous)
            self.start(MS_SETTINGS_FILE=path,MS_NATIVE_WIFI='0',MS_TEST_EXPECT_SSID='native')

    def test_successful_network_replacement_is_used_after_restart_with_same_key(self):
        with tempfile.TemporaryDirectory() as folder:
            path=str(Path(folder)/'settings.bin')
            self.tearDown();self.start(MS_SETTINGS_FILE=path)
            key=self.token;self.tearDown()
            r=subprocess.run([str(BUILD/'native/mathsmith_light')],
                input=b':wifi join\nnext network\nnext-password\n:wifi\n',capture_output=True,timeout=5,
                env=dict(os.environ,MS_NATIVE_WIFI='0',MS_SETTINGS_FILE=path))
            self.assertEqual(r.returncode,0,r.stderr.decode());self.assertNotIn(b'next-password',r.stdout)
            self.assertIn(b'Wi-Fi and browser key saved',r.stdout)
            self.start(MS_SETTINGS_FILE=path,MS_NATIVE_WIFI='0',
                       MS_TEST_EXPECT_SSID='next network',MS_TEST_EXPECT_PASSWORD='next-password')
            self.assertEqual(self.token,key)
            self.assertIn('ans = 4',self.command(self.open(),'2+2'))

    def test_corrupt_settings_boot_to_serial_and_failed_save_is_reported(self):
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder)/'settings.bin';path.write_bytes(b'\0'*320)
            r=subprocess.run([str(BUILD/'native/mathsmith_light')],input=b'2+2\n',capture_output=True,
                timeout=5,env=dict(os.environ,MS_SETTINGS_FILE=str(path),MS_NATIVE_WIFI='0'))
            self.assertEqual(r.returncode,0,r.stderr.decode())
            self.assertIn(b'No saved network',r.stdout);self.assertIn(b'ans = 4',r.stdout)
            self.tearDown();self.start(MS_SETTINGS_FILE=str(path),MS_TEST_SETTINGS_FAIL='1')
            self.serial_until('Settings could not be saved')
            s=self.open();self.assertIn('Saved network=no',self.command(s,':wifi'))

    def test_repeated_join_gets_fresh_events_without_rewriting_unchanged_settings(self):
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder)/'settings.bin'
            self.tearDown();self.start(MS_SETTINGS_FILE=str(path),MS_TEST_WIFI_REPEAT_BEGIN_NO_EVENTS='1')
            before=path.read_bytes(); timestamp=path.stat().st_mtime_ns
            self.serial(b':wifi join "native" ""\n')
            self.serial_until('Wi-Fi and browser key saved.',count=2)
            self.assertEqual(path.read_bytes(),before)
            self.assertEqual(path.stat().st_mtime_ns,timestamp)

    def test_mdns_failure_keeps_numeric_browser_access(self):
        self.tearDown();self.start(MS_TEST_MDNS_FAIL='1')
        s=self.open();self.assertIn('discovery=numeric IP only',self.command(s,':wifi'))
        self.assertIn('ans = 4',self.command(s,'2+2'))

    def test_wifi_diag_keeps_workspace_saved_record_and_auth_key_private(self):
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder)/'settings.bin'
            self.tearDown();self.start(MS_SETTINGS_FILE=str(path))
            before=path.read_bytes();timestamp=path.stat().st_mtime_ns
            s=self.open();self.command(s,'x=41;')
            output=self.command(s,':wifi diag')
            self.assertIn('state=10 (BOUND)',output)
            self.assertNotIn(self.token,output)
            self.assertNotIn('Browser URL:',output)
            self.assertIn('ans = 42',self.command(s,'x+1'))
            self.assertEqual(path.read_bytes(),before)
            self.assertEqual(path.stat().st_mtime_ns,timestamp)

class ReadOnlyWiFiDiagnostics(unittest.TestCase):
    def run_diag(self,**flags):
        r=subprocess.run([str(BUILD/'native/mathsmith_light')],
            input=b':wifi join "native" "private-pass"\n:wifi diag\n2+2\n',capture_output=True,
            timeout=5,env=dict(os.environ,MS_NATIVE_WIFI='0',**flags))
        self.assertEqual(r.returncode,0,r.stderr.decode())
        output=r.stdout.decode();self.assertNotIn('private-pass',output)
        self.assertIn('ans = 4',output)
        self.assertIn('Native diagnostic fixture',output)
        return output

    def test_waiting_offer_is_reported_before_any_ip(self):
        output=self.run_diag(MS_TEST_WIFI_SDK_STATE='1',MS_TEST_WIFI_ASSOCIATED='1',
            MS_TEST_DIAG_STATE='6',MS_TEST_DIAG_TRIES='7',MS_TEST_DIAG_LINK_UP='0')
        self.assertIn('Station MAC=34:5f:45:40:57:3a',output)
        self.assertIn('AP BSSID=02:00:00:00:00:01; channel=6; RSSI=-56 dBm',output)
        self.assertIn('Station interface=st0; up=1; link-up=0',output)
        self.assertIn('state=6 (SELECTING); tries=7',output)
        self.assertIn('Interface IP=0.0.0.0',output)
        self.assertNotIn('Browser URL:',output)

    def test_unknown_state_and_missing_client_or_interface_are_bounded(self):
        for flags,expected in [({'MS_TEST_DIAG_STATE':'255'},'state=255 (unknown)'),
            ({'MS_TEST_DIAG_NO_CLIENT':'1'},'lwIP DHCP client absent.'),
            ({'MS_TEST_DIAG_NO_INTERFACE':'1'},'Station lwIP interface not found by MAC.')]:
            with self.subTest(flags=flags):self.assertIn(expected,self.run_diag(**flags))

    def test_ssid_is_bounded_and_cannot_inject_terminal_control_lines(self):
        output=self.run_diag(MS_TEST_DIAG_SSID='network\nINJECTED\x1b'+('x'*200))
        self.assertIn('SSID=network?INJECTED?',output)
        self.assertNotIn('\nINJECTED',output)
        self.assertNotIn('x'*33,output)

class WiFiDiagnostics(unittest.TestCase):
    def run_serial(self,**env):
        result=subprocess.run([str(BUILD/'native/mathsmith_light')],
            input=b':wifi join\nnative\nprivate-pass\n:wifi\n2+2\n',capture_output=True,
            timeout=5,env=dict(os.environ,**env))
        self.assertEqual(result.returncode,0,result.stderr.decode())
        output=result.stdout.decode()
        self.assertNotIn('private-pass',output)
        self.assertIn('ans = 4',output)
        return output

    def test_status7_is_explained_as_connecting(self):
        output=self.run_serial(MS_TEST_WIFI_SDK_STATE='1')
        self.assertIn('Wi-Fi status=7',output)
        self.assertIn('Mode=1; SDK=1 (connecting); begin=7;',output)
        self.assertIn('Connection in progress; allow up to 30 s',output)
        self.assertIn('IP=0.0.0.0',output)
        self.assertIn('ready=0',output)

    def test_sdk_enabled_but_lwip_off_starts_once_and_saves_after_fresh_ip(self):
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder)/'settings.bin'
            output=self.run_serial(MS_SETTINGS_FILE=str(path),MS_TEST_WIFI_SDK_STATE='1',
                MS_TEST_WIFI_ASSOCIATED='1',MS_TEST_ASSOCIATED_AGE_MS='16000',
                MS_TEST_DHCP_STATE='1',MS_TEST_DIAG_STATE='0',MS_TEST_LWIP_START_GET_IP='1')
            self.assertEqual(output.count('start once:'),1)
            self.assertIn('result=0; before=0/pcb0; after=10/pcb1; tries=1',output)
            self.assertIn('got-ip-events=1; dhcp-timeouts=0; ready=1',output)
            self.assertIn('Wi-Fi and browser key saved.',output)
            self.assertTrue(path.exists())
            self.assertIn('Browser URL:',output)

    def test_start_success_without_ip_or_error_is_bounded_and_does_not_save(self):
        for flags in ({},{'MS_TEST_LWIP_START_ERROR':'-1'},{'MS_TEST_LWIP_START_ERROR':'-12'}):
            with self.subTest(flags=flags),tempfile.TemporaryDirectory() as folder:
                path=Path(folder)/'settings.bin'
                output=self.run_serial(MS_SETTINGS_FILE=str(path),MS_TEST_WIFI_SDK_STATE='1',
                    MS_TEST_WIFI_ASSOCIATED='1',MS_TEST_ASSOCIATED_AGE_MS='98000',MS_TEST_DIAG_STATE='0',**flags)
                self.assertEqual(output.count('start once:'),1)
                self.assertIn('result='+flags.get('MS_TEST_LWIP_START_ERROR','0'),output)
                self.assertIn('recovery-attempted=yes',output)
                self.assertIn('got-ip-events=0; dhcp-timeouts=0; ready=0',output)
                self.assertFalse(path.exists())
                self.assertNotIn('Browser URL:',output)

    def test_dhcp_recovery_waits_for_association_and_never_overrides_disconnect(self):
        for flags in ({},{'MS_TEST_WIFI_ASSOCIATED':'1','MS_TEST_WIFI_REASON':'201'}):
            with self.subTest(flags=flags):
                output=self.run_serial(MS_TEST_WIFI_SDK_STATE='1',MS_TEST_WIFI_ELAPSED_MS='98000',
                    MS_TEST_ASSOCIATED_AGE_MS='16000',MS_TEST_DIAG_STATE='0',**flags)
                self.assertNotIn('start once:',output)
                self.assertIn('recovery-attempted=no',output)

    def test_active_dhcp_stages_and_allocated_pcb_are_left_running(self):
        for stage,pcb in [('6','1'),('1','1'),('5','1'),('10','1'),('0','1')]:
            with self.subTest(stage=stage,pcb=pcb):
                output=self.run_serial(MS_TEST_WIFI_SDK_STATE='1',MS_TEST_WIFI_ASSOCIATED='1',
                    MS_TEST_ASSOCIATED_AGE_MS='98000',MS_TEST_DIAG_STATE=stage,MS_TEST_DIAG_PCB=pcb)
                self.assertNotIn('start once:',output)
                self.assertIn('recovery-attempted=no',output)

    def test_stopped_client_is_not_started_without_interface_link_or_sdk_enable(self):
        for flags in ({'MS_TEST_DIAG_NO_INTERFACE':'1'},{'MS_TEST_DIAG_UP':'0'},
                      {'MS_TEST_DIAG_LINK_UP':'0'},{'MS_TEST_DHCP_STATE':'0'}):
            with self.subTest(flags=flags):
                output=self.run_serial(MS_TEST_WIFI_SDK_STATE='1',MS_TEST_WIFI_ASSOCIATED='1',
                    MS_TEST_ASSOCIATED_AGE_MS='98000',MS_TEST_DIAG_STATE='0',**flags)
                self.assertNotIn('start once:',output)
                self.assertIn('recovery-attempted=no',output)

    def test_stopped_client_gets_initial_fifteen_second_grace(self):
        output=self.run_serial(MS_TEST_WIFI_SDK_STATE='1',MS_TEST_WIFI_ASSOCIATED='1',
            MS_TEST_ASSOCIATED_AGE_MS='14000',MS_TEST_DIAG_STATE='0')
        self.assertNotIn('start once:',output)

    def test_disconnect_invalidates_stale_sdk_status_and_address(self):
        output=self.run_serial(MS_TEST_WIFI_SDK_STATE='5',MS_TEST_WIFI_HAS_IP='1',MS_TEST_WIFI_REASON='201')
        self.assertIn('Wi-Fi status=3',output)
        self.assertIn('IP=127.0.0.1',output)
        self.assertIn('got-ip-events=1',output)
        self.assertIn('Last disconnect=201',output)
        self.assertIn('ready=0',output)
        self.assertNotIn('Browser URL:',output)

    def test_association_without_ip_and_dhcp_timeout_are_distinguished(self):
        output=self.run_serial(MS_TEST_WIFI_SDK_STATE='1',MS_TEST_WIFI_ASSOCIATED='1',MS_TEST_WIFI_DHCP_TIMEOUT='1')
        self.assertIn('IP=0.0.0.0; associated=1',output)
        self.assertIn('got-ip-events=0; dhcp-timeouts=1; ready=0',output)
        self.assertNotIn('Browser URL:',output)

    def test_stalled_attempt_does_not_repeat_the_initial_wait_advice(self):
        output=self.run_serial(MS_TEST_WIFI_SDK_STATE='1',MS_TEST_WIFI_ELAPSED_MS='140000')
        self.assertIn('elapsed=140 s',output)
        self.assertIn('No usable IP after 30 s; capture :wifi and :mem',output)
        self.assertNotIn('Connection in progress; allow up to 30 s',output)

    def test_link_local_autoip_is_not_saved_or_exposed_as_a_lan_connection(self):
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder)/'settings.bin'
            output=self.run_serial(MS_SETTINGS_FILE=str(path),MS_TEST_WIFI_SDK_STATE='5',
                MS_TEST_WIFI_LINK_LOCAL='1')
            self.assertIn('IP=169.254.10.20',output)
            self.assertIn('ready=0',output)
            self.assertIn('Saved network=no; save-pending=yes',output)
            self.assertNotIn('Browser URL:',output)
            self.assertFalse(path.exists())

    def test_stale_ip_or_another_ssid_cannot_persist_unverified_credentials(self):
        for flags in ({'MS_TEST_WIFI_SKIP_IP_EVENT':'1'},{'MS_TEST_WIFI_ASSOCIATED_SSID':'another network'}):
            with self.subTest(flags=flags),tempfile.TemporaryDirectory() as folder:
                path=Path(folder)/'settings.bin'
                output=self.run_serial(MS_SETTINGS_FILE=str(path),**flags)
                self.assertIn('save-pending=yes',output)
                self.assertFalse(path.exists(),'Unverified candidate must not be saved')

    def test_mode_enable_failure_keeps_serial_console(self):
        output=self.run_serial(MS_TEST_WIFI_MODE_FAIL='1')
        self.assertIn('Wi-Fi station mode failed to start',output)
        self.assertIn('Mode=0; SDK=0 (idle); begin=-1;',output)
        self.assertNotIn('Connecting; credentials',output)

    def test_sdk_failure_reasons_are_reported_without_credentials(self):
        for state,reason,expected in [('2','202','wrong-password'),('3','201','no-ap-found'),('4','204','connect-failed')]:
            with self.subTest(state=state):
                output=self.run_serial(MS_TEST_WIFI_SDK_STATE=state,MS_TEST_WIFI_REASON=reason)
                self.assertIn(f'SDK={state} ({expected})',output)
                self.assertIn(f'Last disconnect={reason}',output)
                self.assertIn('events=1',output)

class Target(unittest.TestCase):
    def test_gpio_profile_is_local_and_delay_services_poll_and_sdk(self):
        generated=(BUILD/'engine/engine.c').read_text()
        self.assertIn('#include "gpio_engine.h"',generated)
        shared=ROOT/'apps/m01_mathsmith/engine.c'
        self.assertNotIn('gpio_engine.h',shared.read_text())
        self.assertEqual(hashlib.sha256(shared.read_bytes()).hexdigest(),
                         json.loads((BUILD/'engine_sources.json').read_text())['engine.c'])
        delay_code=subprocess.check_output([str(TOOL/'xtensa-lx106-elf-objdump'),'-d',
            '--disassemble=ms_pin_delay',str(BUILD/'mathsmith_light.elf')],text=True)
        self.assertIn('<ms_poll>',delay_code)
        self.assertIn('<__delay>',delay_code)
        symbols=subprocess.check_output([str(TOOL/'xtensa-lx106-elf-nm'),str(BUILD/'mathsmith_light.elf')],text=True)
        address=lambda name:re.search(r'(?m)^([0-9a-f]+) \w '+name+'$',symbols)[1]
        self.assertEqual(address('delay'),address('__delay')) # Pinned weak alias.

    def test_broadcast_reply_hook_preserves_stock_options_before_checksum(self):
        relocations=subprocess.check_output([str(TOOL/'xtensa-lx106-elf-objdump'),'-r',
            str(BUILD/'dhcp_broadcast.cpp.o')],text=True)
        self.assertIn('__real_lwip_hook_dhcp_amend_options',relocations)
        self.assertIn('ms_dhcp_request_broadcast',relocations)
        discover=subprocess.check_output([str(TOOL/'xtensa-lx106-elf-objdump'),'-d',
            '--disassemble=dhcp_discover',str(BUILD/'mathsmith_light.elf')],text=True)
        self.assertIn('<__wrap_lwip_hook_dhcp_amend_options>',discover)
        self.assertLess(discover.index('<__wrap_lwip_hook_dhcp_amend_options>'),discover.index('<udp_sendto_if_src>'))
    def test_packet_trace_wires_the_pinned_bidirectional_phy_hook(self):
        relocations=subprocess.check_output([str(TOOL/'xtensa-lx106-elf-objdump'),'-r',
            str(BUILD/'dhcp_trace_io.cpp.o')],text=True)
        self.assertIn('phy_capture',relocations)
        self.assertIn('ms_dhcp_trace_observe',relocations)
    def test_matching_standard_low_memory_feature_stack_and_dhcp_hooks(self):
        config=json.loads((BUILD/'build_config.json').read_text())
        self.assertIn('-DLWIP_FEATURES=1',config['defines'])
        self.assertIn('-DTCP_MSS=536',config['defines'])
        self.assertIn('-DLWIP_IPV6=0',config['defines'])
        self.assertIn('liblwip2-536-feat.a',(BUILD/'mathsmith_light.map').read_text())
        symbols=subprocess.check_output([str(TOOL/'xtensa-lx106-elf-nm'),str(BUILD/'mathsmith_light.elf')],text=True)
        self.assertIn('lwip_hook_dhcp_amend_options',symbols)
    def test_recovery_links_real_lwip2_start_not_sdk_restart_wrapper(self):
        relocations=subprocess.check_output([str(TOOL/'xtensa-lx106-elf-objdump'),'-r',
            str(BUILD/'dhcp_recovery.cpp.o')],text=True)
        self.assertIn('dhcp_start_LWIP2',relocations)
        self.assertNotIn('wifi_station_dhcpc_stop',relocations)
    def test_settings_sectors_are_reserved_before_sdk_flash(self):
        symbols=subprocess.check_output([str(TOOL/'xtensa-lx106-elf-nm'),str(BUILD/'mathsmith_light.elf')],text=True)
        for name,address in [('_FS_start',0x405fa000),('_FS_end',0x405fa000),('_EEPROM_start',0x405fb000)]:
            with self.subTest(name=name):
                m=re.search(r'(?m)^([0-9a-f]+) \w '+name+'$',symbols)
                self.assertIsNotNone(m);self.assertEqual(int(m[1],16),address)
    def test_image_and_input_freshness(self):
        self.assertGreater(check(),0)
        self.assertEqual(json.loads((BUILD/'build_config.json').read_text())['inputs'],inputs_fingerprint())

    def test_linked_resources_and_dedicated_stack(self):
        layout=subprocess.check_output([str(TOOL/'xtensa-lx106-elf-size'),'-A',str(BUILD/'mathsmith_light.elf')],text=True)
        sizes={m[1]:int(m[2]) for m in re.finditer(r'^(\.\S+)\s+(\d+)\s+\d+$',layout,re.M)}
        self.assertLess(sizes['.text']+sizes['.text1'],32768)
        self.assertLess(sum(sizes[n] for n in ('.data','.rodata','.bss')),58000)
        self.assertLess((BUILD/'mathsmith_light.bin').stat().st_size,1044464)
        symbols=subprocess.check_output([str(TOOL/'xtensa-lx106-elf-nm'),'-S',str(BUILD/'mathsmith_light.elf')],text=True)
        self.assertRegex(symbols,r'(?m)^3ff[0-9a-f]+ 00004028 b _ZL6g_cont$')

if __name__=='__main__': unittest.main()
