"""Stage the tested L00 build; never open or flash a board."""
import hashlib
import json
from pathlib import Path
import shutil
from light_profile import ROOT,BUILD,APP,APP_NAME,inputs_fingerprint
from check_image import check as check_image

def main():
    if json.loads((BUILD/'build_config.json').read_text())['inputs']!=inputs_fingerprint():
        raise SystemExit('Target build is stale; rebuild and test before release')
    check_image()
    dest=ROOT/'releases/esp8266'/APP_NAME
    sources={name:BUILD/name for name in ('mathsmith_light.bin','mathsmith_light.elf',
             'mathsmith_light.map','build_config.json','engine_sources.json')}
    sources.update({'README.md':APP/'README.md',
        'USER_MANUAL.md':ROOT/('esp8266/docs/USER_MANUAL.md' if APP_NAME=='l00_mathsmith' else 'esp8266/docs/USER_MANUAL_L01.md'),
        'MEMORY_MAP.md':ROOT/('esp8266/docs/MEMORY_MAP_L00.md' if APP_NAME=='l00_mathsmith' else 'esp8266/docs/MEMORY_MAP_L01.md'),
        'CORE_LICENSE.txt':ROOT/'third_party/esp8266-3.1.2/esp8266-3.1.2/LICENSE'})
    if APP_NAME=='l00_mathsmith':
        sources['HARDWARE_REPORT.txt']=APP/'hw/d1_mini_2026-11-15_boot_eig.txt'
    else:
        sources['MATH_LANGUAGE.md']=ROOT/'esp8266/docs/USER_MANUAL.md'
        sources['WEB_PROTOCOL.md']=ROOT/'esp8266/docs/WEB_PROTOCOL_L01.md'
        sources['SETTINGS.md']=ROOT/'esp8266/docs/SETTINGS_L01.md'
        sources['R4_COMPARISON.md']=ROOT/'esp8266/docs/R4_COMPARISON.md'
        sources['HOME_NETWORK_DIAGNOSIS.md']=ROOT/'esp8266/docs/HOME_NETWORK_DIAGNOSIS.md'
        sources['NETWORK_DIAGNOSTICS.md']=ROOT/'esp8266/docs/NETWORK_DIAGNOSTICS_L01.md'
        sources['DHCP_RECOVERY.md']=ROOT/'esp8266/docs/DHCP_RECOVERY_L01.md'
        sources['DHCP_COMPATIBILITY.md']=ROOT/'esp8266/docs/DHCP_COMPATIBILITY_L01.md'
        sources['DHCP_PACKETS.md']=ROOT/'esp8266/docs/DHCP_PACKETS_L01.md'
        sources['DHCP_BROADCAST.md']=ROOT/'esp8266/docs/DHCP_BROADCAST_L01.md'
        sources['BROWSER_EDITOR.md']=ROOT/'esp8266/docs/BROWSER_EDITOR_L01.md'
        sources['SCRIPT_GPIO.md']=ROOT/'esp8266/docs/SCRIPT_GPIO_L01.md'
        sources['PROGRAM_STORE.md']=ROOT/'esp8266/docs/PROGRAM_STORE_L02.md'
        for name in ('sum.ms','solve.ms','square.ms','blink.ms','blink_count.ms','ledtest.m'):
            sources[name]=APP/'examples'/name
    for source in sources.values():
        if not source.is_file(): raise SystemExit('Missing '+str(source))
    dest.mkdir(parents=True,exist_ok=True)
    hashes=[]
    for name,source in sources.items():
        shutil.copyfile(source,dest/name)
        hashes.append(hashlib.sha256((dest/name).read_bytes()).hexdigest()+'  '+name+'\n')
    (dest/'SHA256SUMS').write_text(''.join(hashes))
    print('Staged '+str(dest)+'; no board flashed or hardware validation claimed.')

if __name__=='__main__':main()
