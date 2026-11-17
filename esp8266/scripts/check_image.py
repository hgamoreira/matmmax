"""Check core checksum addresses and that image conversion preserves ELF bytes."""
import argparse
from pathlib import Path
import re
import struct
import subprocess
import tempfile
from light_profile import ROOT,BUILD

TOOL=ROOT/'third_party/esp8266-3.1.2/xtensa-lx106-elf/bin'

def crc8266(data):
    table=[]
    for value in range(256):
        crc=value<<24
        for _ in range(8):
            crc=((crc<<1)^(0x04c11db7 if crc&0x80000000 else 0))&0xffffffff
        table.append(crc)
    crc=0xffffffff
    for value in data:crc=((crc<<8)&0xffffffff)^table[((crc>>24)^value)&255]
    return crc

def check(directory=BUILD):
    elf=directory/'mathsmith_light.elf'
    raw=(directory/'mathsmith_light.bin').read_bytes()
    layout=subprocess.check_output([str(TOOL/'xtensa-lx106-elf-size'),'-A',str(elf)],text=True)
    sections={m.group(1):(int(m.group(2)),int(m.group(3))) for m in
        re.finditer(r'^(\.\S+)\s+(\d+)\s+(\d+)$',layout,re.M)}
    symbols=subprocess.check_output([str(TOOL/'xtensa-lx106-elf-nm'),str(elf)],text=True)
    # The default core initializes its heap before instruction cache is on.
    # Preserve the linker rule excluding umm_malloc.cpp.o from generic flash
    # placement, including all startup helpers in that object.
    for name in ('app_entry','app_entry_redefinable','umm_init','umm_init_heap'):
        match=re.search(r'(?m)^([0-9a-f]+) \w '+name+'$',symbols)
        if not match or not 0x40100000<=int(match.group(1),16)<0x40108000:
            raise ValueError(f'{name} must be in IRAM: startup runs before flash cache is enabled')
    for name,address in (('__crc_len',0x40201010),('__crc_val',0x40201014)):
        match=re.search(r'(?m)^([0-9a-f]+) \w '+name+'$',symbols)
        if not match or int(match.group(1),16)!=address:
            raise ValueError(f'{name} must be at {address:#x}; image converter uses a fixed offset')
    if struct.unpack_from('<I',raw,0x1010)[0]!=len(raw):
        raise ValueError('Image length metadata does not match binary')
    normalized=bytearray(raw)
    normalized[0x1010:0x1018]=b'\0'*8
    if crc8266(normalized)!=struct.unpack_from('<I',raw,0x1014)[0]:
        raise ValueError('Whole image CRC does not match core algorithm')
    # Parse the app container after the 4096-byte eboot region. Compare all
    # five load segments with ELF sections, masking only intended CRC writes.
    if raw[0x1000:0x1002]!=b'\xe9\x05':raise ValueError('Invalid app image header')
    offset=0x1008
    with tempfile.TemporaryDirectory(dir=directory) as temp:
        for section in ('.irom0.text','.text','.text1','.data','.rodata'):
            if offset+8>len(raw):raise ValueError('Truncated segment header')
            address,size=struct.unpack_from('<II',raw,offset)
            if (size,address)!=sections[section]:
                raise ValueError(f'{section}: load address/length differs from ELF')
            offset+=8
            payload=bytearray(raw[offset:offset+size])
            section_file=Path(temp)/'section.bin'
            subprocess.run([str(TOOL/'xtensa-lx106-elf-objcopy'),'-O','binary',
                '--only-section='+section,str(elf),str(section_file)],check=True)
            expected=section_file.read_bytes()
            if section=='.irom0.text':payload[:8]=b'\0'*8
            if payload!=expected:raise ValueError(f'{section}: converter changed linked payload bytes')
            offset+=size
    return len(raw)

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory',nargs='?',type=Path,default=BUILD)
    args=parser.parse_args()
    try:print(f'Image metadata, CRC and all ELF payload segments OK ({check(args.directory)} bytes)')
    except (ValueError,OSError) as error:raise SystemExit(str(error))
