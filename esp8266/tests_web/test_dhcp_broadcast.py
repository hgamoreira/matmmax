"""Actual outgoing BOOTP policy; only the pre-address reply flag may change."""
import ctypes as C
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[2]
APP=ROOT/'apps/esp8266/l01_webrepl'

class DhcpBroadcast(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.folder=tempfile.TemporaryDirectory()
        library=Path(cls.folder.name)/'broadcast.so'
        subprocess.run(['gcc','-std=c99','-O2','-Wall','-Wextra','-Werror','-shared','-fPIC',
            str(APP/'dhcp_broadcast.c'),'-o',str(library)],check=True)
        cls.lib=C.CDLL(str(library))
        cls.lib.ms_dhcp_request_broadcast.argtypes=[C.c_void_p,C.c_size_t,C.c_int]
    @classmethod
    def tearDownClass(cls):cls.folder.cleanup()
    def apply(self,wire,kind,length=None):
        buffer=C.create_string_buffer(bytes(wire),len(wire))
        self.lib.ms_dhcp_request_broadcast(buffer,len(wire) if length is None else length,kind)
        return buffer.raw

    def test_discover_and_initial_request_change_only_broadcast_bit(self):
        wire=bytearray((i*31)%256 for i in range(308));wire[0]=1;wire[12:16]=b'\0'*4
        for kind in (1,3):
            for flags in (0,0x01,0x80,0xff):
                wire[10]=flags;expected=bytearray(wire);expected[10]|=0x80
                self.assertEqual(self.apply(wire,kind),bytes(expected))
                self.assertEqual(self.apply(expected,kind),bytes(expected))

    def test_renewal_other_types_replies_and_short_buffers_are_unchanged(self):
        wire=bytearray(308);wire[0]=1
        for kind in (0,2,4,5,6,7,8,9):self.assertEqual(self.apply(wire,kind),bytes(wire))
        for i in range(12,16):
            wire[i]=1
            for kind in (1,3):self.assertEqual(self.apply(wire,kind),bytes(wire))
            wire[i]=0
        wire[0]=2;self.assertEqual(self.apply(wire,1),bytes(wire));wire[0]=1
        for length in range(16):self.assertEqual(self.apply(wire,1,length),bytes(wire))
        self.lib.ms_dhcp_request_broadcast(None,308,1)

    def test_flagged_wire_is_classified_with_valid_recomputed_checksums(self):
        from test_dhcp_trace import frame,Packet,MAC,setUpModule,OUT
        setUpModule()
        parser=C.CDLL(str(OUT/'dhcp_trace.so'))
        parser.ms_dhcp_packet_read.argtypes=[C.c_void_p,C.c_size_t,C.c_void_p,C.POINTER(Packet)]
        from test_dhcp_trace import checksum
        import struct
        for kind in (1,3):
            wire=bytearray(frame(kind));wire[42+10]=0;wire[42:]=self.apply(wire[42:],kind)
            # UDP pseudo-header checksum after construction, as the stack does.
            wire[40:42]=b'\0\0'
            value=checksum(bytes(wire[26:34])+b'\0\x11'+bytes(wire[38:40])+bytes(wire[34:])) or 65535
            struct.pack_into('!H',wire,40,value)
            packet=Packet()
            self.assertEqual(parser.ms_dhcp_packet_read(bytes(wire),len(wire),MAC,C.byref(packet)),1)
            self.assertEqual((packet.type,packet.broadcast,packet.client_id,packet.hostname,packet.checksum_ok),(kind,1,1,1,1))
