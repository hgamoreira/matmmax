"""Build/test/stage a separate, engine-free Wi-Fi comparison image; never auto-flash."""
import argparse
import hashlib
import json
import shutil
import subprocess
import sys
from pathlib import Path
from build import ROOT, TOOL, main as build_target, probe_inputs
from check_image import check

APP=ROOT/'apps/esp8266/wifi_probe'
BUILD=ROOT/'build/esp8266/wifi_probe'
ESP_PY=ROOT/'third_party/esp-idf-tools-v5.5.6/python_env/idf5.5_py3.14_env/bin/python'

def fresh():
    check(BUILD)
    config=json.loads((BUILD/'build_config.json').read_text())
    if config['inputs']!=probe_inputs(APP): raise SystemExit('Probe build is stale; rebuild/test first')

def native():
    out=BUILD/'native'; out.mkdir(parents=True,exist_ok=True)
    subprocess.run(['g++','-std=c++17','-O2','-DMS_WEB_NATIVE','-I'+str(ROOT/'esp8266/tests_web'),
        str(APP/'firmware.cpp'),str(ROOT/'esp8266/tests_probe/main.cpp'),'-o',str(out/'wifi_probe')],check=True)
    return out/'wifi_probe'

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('action',choices=['build','test','release','flash','monitor'])
    p.add_argument('port',nargs='?')
    a=p.parse_args()
    if (a.action in ('flash','monitor')) != bool(a.port): p.error('Supply a port only for flash/monitor')
    if a.action=='build': build_target(app=APP,output=BUILD,engine_enabled=False,stack_bytes=4096)
    elif a.action=='test':
        subprocess.run([sys.executable,'-m','unittest','discover','-s','esp8266/tests_probe','-v'],cwd=ROOT,check=True)
    elif a.action=='release':
        fresh(); dest=ROOT/'releases/esp8266/wifi_probe'; dest.mkdir(parents=True,exist_ok=True)
        files={name:BUILD/name for name in ('mathsmith_light.bin','mathsmith_light.elf','mathsmith_light.map','build_config.json')}
        files['README.md']=APP/'README.md'
        hashes=[]
        for name,source in files.items():
            shutil.copyfile(source,dest/name)
            hashes.append(hashlib.sha256((dest/name).read_bytes()).hexdigest()+'  '+name+'\n')
        (dest/'SHA256SUMS').write_text(''.join(hashes))
        print('Staged engine-free probe at '+str(dest)+'; no board opened.')
    elif a.action=='flash':
        fresh()
        subprocess.run([str(ESP_PY),'-m','esptool','--chip','esp8266','--port',a.port,'--baud','115200',
            'write_flash','--flash_size','4MB','--flash_mode','dio','--flash_freq','40m','0x0',str(BUILD/'mathsmith_light.bin')],check=True)
    else:
        subprocess.run([str(ESP_PY),'-m','serial.tools.miniterm','--raw','--dtr','0','--rts','0',a.port,'115200'],check=True)

if __name__=='__main__': main()
