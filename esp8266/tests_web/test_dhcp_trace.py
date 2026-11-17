"""Actual bounded DHCP metadata parser/counters against constructed wire frames."""
import ctypes as C
import os
from pathlib import Path
import socket
import struct
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[2]
APP=ROOT/'apps/esp8266/l01_webrepl'
OUT=ROOT/'build/esp8266/l01_webrepl/native'
MAC=bytes.fromhex('345f4540573a')
OTHER=bytes.fromhex('020000000002')
SERVER=bytes.fromhex('020000000001')
class Packet(C.Structure):
    _fields_=[('xid',C.c_uint32),('type',C.c_ubyte),('broadcast',C.c_ubyte),
        ('client_id',C.c_ubyte),('hostname',C.c_ubyte),('checksum_ok',C.c_ubyte),
        ('source',C.c_ubyte*4),('offered',C.c_ubyte*4),('server',C.c_ubyte*4),
        ('cookie',C.c_uint32),('frame_bytes',C.c_uint16),('ip_bytes',C.c_uint16),
        ('udp_bytes',C.c_uint16),('option_offset',C.c_uint16),('option_remaining',C.c_uint16),
        ('reject_reason',C.c_ubyte),('boot_op',C.c_ubyte),('option_code',C.c_ubyte),('option_len',C.c_ubyte)]
class Trace(C.Structure):
    _fields_=[('tx',C.c_uint32*9),('rx',C.c_uint32*9),('tx_failed',C.c_uint32),
        ('rx_alloc_failed',C.c_uint32),('bad_checksum',C.c_uint32),('malformed',C.c_uint32),
        ('wrong_xid',C.c_uint32),('last_tx',Packet),('last_rx',Packet),
        ('tx_rejected',C.c_uint32),('rx_rejected',C.c_uint32),('last_rejected',Packet),
        ('rejected_out',C.c_ubyte),('rejected_success',C.c_ubyte)]
def checksum(data):
    if len(data)%2:data+=b'\0'
    total=sum(struct.unpack('!%dH'%(len(data)//2),data))
    while total>>16:total=(total&65535)+(total>>16)
    return (~total)&65535
def frame(kind=1,mac=MAC,xid=0x40573a10,options=None,vlan=False,ip_options=False,udp_check=True):
    reply=kind in (2,5,6)
    options=options if options is not None else bytes((53,1,kind,61,7,1))+mac+b'\x0c\x03esp\x36\x04\xc0\xa8\x0a\x01\xff'
    body=struct.pack('!BBBBIHHIIII16s64s128s',2 if reply else 1,1,6,0,xid,0,0x8000,
        0,0xc0a80a9d if reply else 0,0,0,mac+b'\0'*10,b'\0'*64,b'\0'*128)
    body+=bytes.fromhex('63825363')+options
    source=socket.inet_aton('192.168.10.1' if reply else '0.0.0.0'); dest=b'\xff'*4
    udp=struct.pack('!HHHH',67 if reply else 68,68 if reply else 67,len(body)+8,0)+body
    if udp_check:
        value=checksum(source+dest+b'\0\x11'+struct.pack('!H',len(udp))+udp) or 65535
        udp=udp[:6]+struct.pack('!H',value)+udp[8:]
    extra=b'\x01'*4 if ip_options else b''
    ip=struct.pack('!BBHHHBBH4s4s',0x46 if extra else 0x45,0,len(udp)+20+len(extra),1,0,64,17,0,source,dest)+extra
    ip=ip[:10]+struct.pack('!H',checksum(ip))+ip[12:]
    eth=b'\xff'*6+(SERVER if reply else mac)+(b'\x81\x00\x00\x01\x08\x00' if vlan else b'\x08\x00')
    return eth+ip+udp

def setUpModule():
    OUT.mkdir(parents=True,exist_ok=True)
    subprocess.run(['gcc','-std=c99','-O2','-shared','-fPIC','-Wall','-Wextra','-Werror',
        '-I'+str(APP),str(APP/'dhcp_trace.c'),'-o',str(OUT/'dhcp_trace.so')],check=True)

class DhcpTrace(unittest.TestCase):
    def setUp(self):
        self.lib=C.CDLL(str(OUT/'dhcp_trace.so'))
        self.lib.ms_dhcp_packet_read.argtypes=[C.c_void_p,C.c_size_t,C.c_void_p,C.POINTER(Packet)]
        self.lib.ms_dhcp_trace_reset.argtypes=[C.c_void_p]
        self.lib.ms_dhcp_trace_observe.argtypes=[C.c_int,C.c_void_p,C.c_size_t,C.c_int,C.c_int]
        self.lib.ms_dhcp_trace_snapshot.argtypes=[C.POINTER(Trace)]
        self.lib.ms_dhcp_trace_reset(MAC)
    def parse(self,data):
        packet=Packet();result=self.lib.ms_dhcp_packet_read(data,len(data),MAC,C.byref(packet));return result,packet
    def observe(self,data,out,success=1,interface=0):
        self.lib.ms_dhcp_trace_observe(interface,data,len(data),out,success)
        trace=Trace();self.lib.ms_dhcp_trace_snapshot(C.byref(trace));return trace

    def test_wire_types_options_checksum_vlan_and_ip_options(self):
        for kind in range(1,9):
            for tagged in (False,True):
                with self.subTest(kind=kind,tagged=tagged):
                    result,p=self.parse(frame(kind,vlan=tagged,ip_options=True))
                    self.assertEqual(result,1);self.assertEqual(p.type,kind)
                    self.assertEqual(p.xid,0x40573a10);self.assertEqual(p.broadcast,1)
                    self.assertEqual(p.client_id,1);self.assertEqual(p.hostname,1);self.assertEqual(p.checksum_ok,1)
                    self.assertEqual(bytes(p.server),b'\xc0\xa8\x0a\x01')

    def test_all_truncations_and_invalid_option_lengths_are_bounded(self):
        data=frame()
        for n in range(len(data)):
            with self.subTest(n=n):self.assertLessEqual(self.parse(data[:n])[0],0)
        for options in (b'\x35',b'\x35\x02\x01',b'\x35\x00\xff',b'\x35\x01\x01\x35\x01\x02\xff',b'\x35\x01\x09\xff'):
            self.assertEqual(self.parse(frame(options=options))[0],-1)

    def test_other_clients_non_dhcp_and_fragments_are_not_retained(self):
        self.assertEqual(self.parse(frame(mac=OTHER))[0],0)
        data=bytearray(frame());data[14+6]=0x20
        self.assertEqual(self.parse(bytes(data))[0],0)
        data=bytearray(frame());struct.pack_into('!H',data,14+20+2,69)
        self.assertEqual(self.parse(bytes(data))[0],0)
        self.assertEqual(self.parse(b'GET /ws HTTP/1.1 secret-key')[0],0)
        self.assertEqual(sum(self.observe(frame(mac=OTHER),1).tx),0)
        self.assertEqual(sum(self.observe(frame(),1,interface=1).tx),0)

    def test_bad_checksum_optional_udp_checksum_and_failure_counters(self):
        result,p=self.parse(frame(udp_check=False));self.assertEqual(result,1);self.assertEqual(p.checksum_ok,1)
        bad=bytearray(frame());bad[14+10]^=1
        result,p=self.parse(bytes(bad));self.assertEqual(result,1);self.assertEqual(p.checksum_ok,0)
        trace=self.observe(bytes(bad),1,success=0)
        self.assertEqual(trace.tx_failed,1);self.assertEqual(trace.bad_checksum,1)
        trace=self.observe(frame(2),0,success=0)
        self.assertEqual(trace.rx[2],1);self.assertEqual(trace.rx_alloc_failed,1)

    def test_board_reply_xid_reset_and_input_immutability(self):
        secret=b'private-host'
        data=frame(options=b'\x35\x01\x01\x0c'+bytes([len(secret)])+secret+b'\xff')
        original=bytes(bytearray(data));trace=self.observe(data,1)
        self.assertEqual(data,original);self.assertEqual(trace.tx[1],1)
        trace=self.observe(frame(2,xid=3),0)
        self.assertEqual(trace.wrong_xid,1);self.assertEqual(trace.last_rx.xid,3)
        self.assertNotIn(secret,bytes(trace))
        self.lib.ms_dhcp_trace_reset(MAC)
        trace=self.observe(frame(5),0);self.assertEqual(sum(trace.tx),0);self.assertEqual(trace.wrong_xid,0)

    def test_rejected_header_and_option_reasons_are_preserved(self):
        data=frame(2)
        mutations=[]
        for offset,value,reason in [(14+2,len(data),1),(14+20+4,247,2)]:
            bad=bytearray(data);struct.pack_into('!H',bad,offset,value);mutations.append((bytes(bad),reason))
        bad=bytearray(data);bad[42]=1;mutations.append((bytes(bad),3))
        bad=bytearray(data);bad[42+236]^=1;mutations.append((bytes(bad),4))
        for options,reason in [(b'\x35',5),(b'\x35\x02\x01',6),
            (b'\x35\x00\xff',7),(b'\x35\x01\x02\x35\x01\x02\xff',8),
            (b'\x35\x01\x09\xff',9),(b'\x00\xff',10)]:
            mutations.append((frame(2,options=options),reason))
        for data,reason in mutations:
            with self.subTest(reason=reason):
                result,p=self.parse(data)
                self.assertEqual(result,-1);self.assertEqual(p.reject_reason,reason)
                self.assertEqual(p.frame_bytes,len(data));self.assertEqual(p.xid,0x40573a10)
                self.assertEqual(bytes(p.source),socket.inet_aton('192.168.10.1'))
                self.assertEqual(bytes(p.offered),socket.inet_aton('192.168.10.157'))
                if reason>=5:self.assertEqual(p.cookie,0x63825363)
        result,p=self.parse(frame(2,options=b'\x35\x01\x02\x0c\xffx'))
        self.assertEqual((result,p.reject_reason,p.type,p.option_offset,p.option_code,p.option_len,p.option_remaining),
                         (-1,6,2,3,12,255,1))

    def test_rejected_rx_and_tx_have_separate_counts_and_do_not_erase_valid_frames(self):
        self.observe(frame(),1)
        data=frame(2,options=b'\x35\x01\x02\x0c\xffprivate-host')
        original=bytes(bytearray(data));trace=self.observe(data,0,success=0)
        self.assertEqual(data,original);self.assertNotIn(b'private-host',bytes(trace))
        self.assertEqual((trace.malformed,trace.rx_rejected,trace.tx_rejected),(1,1,0))
        self.assertEqual((trace.rejected_out,trace.rejected_success,trace.last_rejected.type),(0,0,2))
        self.assertEqual((trace.last_tx.type,trace.last_rx.type),(1,0))
        trace=self.observe(frame(options=b'\x35\x01\x09\xff'),1)
        self.assertEqual((trace.malformed,trace.rx_rejected,trace.tx_rejected),(2,1,1))
        self.assertEqual(trace.rejected_out,1)
        self.lib.ms_dhcp_trace_reset(MAC);trace=self.observe(frame(2),0)
        self.assertEqual((trace.rx_rejected,trace.tx_rejected,trace.last_rejected.reject_reason),(0,0,0))

    def test_rejected_packet_output_exposes_reason_without_payload_or_credentials(self):
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder);wire=path/'frame.bin'
            wire.write_bytes(frame(2,options=b'\x35\x01\x02\x0c\xffprivate-host'))
            executable=path/'output'
            subprocess.run(['g++','-std=c++17','-O2','-DMS_WEB_NATIVE',
                '-I'+str(APP),'-I'+str(ROOT/'apps/esp8266/l00_mathsmith'),
                '-I'+str(ROOT/'esp8266/tests_web'),str(APP/'dhcp_trace_io.cpp'),
                str(APP/'dhcp_trace.c'),str(ROOT/'esp8266/tests_web/dhcp_trace_output.cpp'),
                '-o',str(executable)],check=True)
            output=subprocess.check_output([str(executable),str(wire)],text=True)
            self.assertIn('DHCP parser rejected TX=0 RX=1',output)
            self.assertIn('direction=RX boundary-success=1 reason=option-truncated',output)
            self.assertIn('op=2 xid=40573a10 cookie=63825363 type=2 checksum=ok',output)
            self.assertIn('offset=3 code=12 len=255 remaining=12',output)
            self.assertNotIn('private-host',output)

    def test_sanitized_c_parser_survives_wire_mutations_and_all_lengths(self):
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder);(path/'frame.bin').write_bytes(frame())
            executable=path/'bounds'
            subprocess.run(['gcc','-std=c99','-g','-O1','-fsanitize=address,undefined','-fno-omit-frame-pointer',
                '-I'+str(APP),str(APP/'dhcp_trace.c'),str(ROOT/'esp8266/tests_web/dhcp_trace_bounds.c'),
                '-o',str(executable)],check=True)
            run=subprocess.run([str(executable),str(path/'frame.bin')],capture_output=True,text=True,timeout=15,
                env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0'))
            self.assertEqual(run.returncode,0,run.stdout+run.stderr)

    def test_reported_r11_short_reply_preserves_header_but_is_not_an_offer(self):
        options=b'\x35\x01\x02\xff'+b'\0'*56
        complete=frame(2,xid=0x25184131,options=options)
        self.assertEqual(len(complete),342)
        short=complete[:174]
        result,p=self.parse(short)
        self.assertEqual((result,p.reject_reason,p.frame_bytes,p.ip_bytes,p.udp_bytes),(-1,1,174,328,308))
        self.assertEqual((p.boot_op,p.xid,p.cookie,p.type),(2,0x25184131,0,0))
        self.assertEqual(bytes(p.offered),socket.inet_aton('192.168.10.157'))
        trace=self.observe(short,0)
        self.assertEqual((trace.rx_rejected,trace.rejected_success,sum(trace.rx)),(1,1,0))
        self.assertEqual(self.parse(complete)[0],1)
