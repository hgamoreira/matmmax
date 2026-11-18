"""Build L00 using the pinned core's platform.txt recipes, without Arduino IDE."""
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
from pathlib import Path
import subprocess
import sys
from light_profile import ROOT, BUILD, APP, BASE, APP_NAME, generate, inputs_fingerprint
from check_image import check as check_image

PLATFORM=ROOT/'third_party/esp8266-3.1.2/esp8266-3.1.2'
TOOL=PLATFORM.parent/'xtensa-lx106-elf/bin'

def run(cmd): subprocess.run([str(x) for x in cmd],check=True)

def probe_inputs(app):
    files=list(app.glob('*.cpp'))+list(app.glob('*.h'))
    files += [Path(__file__),ROOT/'esp8266/scripts/wifi_probe.py',ROOT/'esp8266/scripts/check_image.py']
    return {str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(files)}

def main(app=APP, output=BUILD, engine_enabled=True, stack_bytes=16384):
    APP, BUILD = Path(app), Path(output)
    if engine_enabled:
        engine=generate()
    else:
        engine=BUILD/'engine'
        engine.mkdir(parents=True,exist_ok=True)
        (BUILD/'engine_sources.json').write_text('{}\n')
    if APP.name == 'l01_webrepl':
        html=(APP/'index.html').read_text()
        assert ')MSPAGE"' not in html
        (BUILD/'web_page.h').write_text('static const char web_page[] PROGMEM = R"MSPAGE('+html+')MSPAGE";\n')
    if not (TOOL/'xtensa-lx106-elf-gcc').is_file():
        raise SystemExit('Run python3 tools/setup_esp8266.py first')
    core=PLATFORM/'cores/esp8266'
    sdk=PLATFORM/'tools/sdk'
    includes=[core,PLATFORM/'variants/d1_mini',sdk/'include',sdk/'lwip2/include',
              sdk/'libc/xtensa-lx106-elf/include',BUILD/'core',engine,APP,BASE,BUILD,
              PLATFORM/'libraries/ESP8266WiFi/src']
    if APP.name=='l01_webrepl': includes += [PLATFORM/'libraries/ESP8266mDNS/src',PLATFORM/'libraries/LittleFS/src']
    lwip_features=1 if APP.name=='l01_webrepl' else 0
    (BUILD/'core').mkdir(exist_ok=True)
    (BUILD/'core/Updater_Signing.h').write_text('// No signing key configured.\n')
    defines=['-D__ets__','-DICACHE_FLASH','-U__STRICT_ANSI__','-D_GNU_SOURCE','-DESP8266',
             '-DNONOSDK22x_190703=1','-DF_CPU=80000000L','-DF_CRYSTAL=26000000',
             '-DARDUINO=10819','-DARDUINO_ARCH_ESP8266','-DARDUINO_ESP8266_WEMOS_D1MINI',
             '-DARDUINO_BOARD="ESP8266_WEMOS_D1MINI"','-DARDUINO_BOARD_ID="d1_mini"',
             '-DFLASHMODE_DIO','-DLWIP_OPEN_SRC','-DTCP_MSS=536',f'-DLWIP_FEATURES={lwip_features}',
             '-DLWIP_IPV6=0','-DVTABLES_IN_FLASH','-DFP_IN_IROM','-DNON32XFER_HANDLER',
             '-DMMU_IRAM_SIZE=0x8000','-DMMU_ICACHE_SIZE=0x8000',f'-DCONT_STACKSIZE={stack_bytes}']
    flags=['-Os','-g','-free','-fipa-pta','-mlongcalls','-mtext-section-literals',
           '-falign-functions=4','-ffunction-sections','-fdata-sections','-fno-exceptions',
           '-fstack-usage','-Werror=return-type']+defines+['-I'+str(x) for x in includes]
    sources=list(core.rglob('*.c'))+list(core.rglob('*.cpp'))+list(core.rglob('*.S'))
    sources+=list((PLATFORM/'libraries/ESP8266WiFi/src').glob('*.cpp'))
    if APP.name=='l01_webrepl':
        sources+=list((PLATFORM/'libraries/ESP8266mDNS/src').glob('*.cpp'))
        sources+=[PLATFORM/'libraries/LittleFS/src'/x for x in ('LittleFS.cpp','lfs.c','lfs_util.c')]
    if len({source.name for source in sources})!=len(sources):
        raise SystemExit('Core/library object basenames collide; review archive naming before building')
    jobs=[]
    coreobjs=[]
    appobjs=[]
    application = ([APP/'firmware.cpp',APP/'console.c',APP/'allocator.c'] if APP==BASE
                   else list(APP.glob('*.c'))+list(APP.glob('*.cpp')))
    if APP != BASE and engine_enabled: application += [BASE/'allocator.c']
    engine_files=[engine/(x+'.c') for x in ('engine','mat','linalg','plot')] if engine_enabled else []
    for source in sources+application+engine_files:
        iscore=source in sources
        # The pinned linker script matches umm_malloc.cpp.o by basename to
        # keep its startup allocator in IRAM. Do not prefix nested filenames.
        obj=(BUILD/'core' if iscore else BUILD)/(source.name+'.o')
        compiler='g++' if source.suffix=='.cpp' else 'gcc'
        cmd=[TOOL/('xtensa-lx106-elf-'+compiler),'-c',*flags]
        if source.suffix=='.cpp': cmd+=['-std=gnu++17','-fno-rtti']
        elif source.suffix=='.S': cmd+=['-x','assembler-with-cpp','-I'+str(TOOL.parent/'include')]
        else: cmd+=['-std=gnu17','-fno-inline-functions']
        if source.parent==engine:
            cmd+=['-DMS_ENGINE_BUILD','-include',BASE/'port.h']
        cmd += [source,'-o',obj]
        # Archives/core headers are immutable pinned input; rebuild cache when
        # flags change. Application/profile sources always compile.
        digest=hashlib.sha256((' '.join(map(str,cmd))).encode()+source.read_bytes()).hexdigest()
        stamp=Path(str(obj)+'.sha256')
        if not iscore or not obj.exists() or not stamp.exists() or stamp.read_text()!=digest:
            jobs.append((cmd,stamp,digest))
        (coreobjs if iscore else appobjs).append(obj)
    def compile(job):
        cmd,stamp,digest=job; run(cmd); stamp.write_text(digest)
    print(f'Compiling {len(jobs)} source files',flush=True)
    with ThreadPoolExecutor(max_workers=8) as pool: list(pool.map(compile,jobs))
    archive=BUILD/'core.a'
    if archive.exists(): archive.unlink()
    run([TOOL/'xtensa-lx106-elf-ar','rcs',archive,*coreobjs])
    # Keep engine constants in mapped flash. Byte access is serviced by the
    # pinned core's NON32XFER handler; no C string conversion/engine edit.
    common=(sdk/'ld/eagle.app.v6.common.ld.h').read_text()
    # elf2bin patches fixed offsets 0x1010/0x1014. Keep the core's leading
    # CRC words and version ahead of our constants, at their original addresses.
    anchor='    *(.ver_number)'
    placement='\n    . = ALIGN(4);\n'+''.join(f'    *{x}.c.o(.rodata .rodata.*)\n' for x in ('engine','mat','linalg','plot','console','store'))
    assert common.count(anchor)==1
    (BUILD/'common.ld.h').write_text(common.replace(anchor,anchor+placement))
    flash_script=sdk/'ld/eagle.flash.4m.ld'
    if APP.name=='l01_webrepl':
        # XD118: the core's standard 4M1M layout. LittleFS 0x300000..0x3F9FFF
        # (1000 KiB), settings slots 0x3FA000/0x3FB000 untouched (XD113).
        flash_script=sdk/'ld/eagle.flash.4m1m.ld'
        layout=flash_script.read_text()
        for value in ('_FS_start = 0x40500000','_FS_end = 0x405FA000','_FS_page = 0x100',
                      '_FS_block = 0x2000','_EEPROM_start = 0x405fb000'):
            if layout.count(f'PROVIDE ( {value} );')!=1: raise SystemExit('Review store/settings flash layout: '+value)
    for source,dest in ((flash_script,'local.eagle.flash.ld'),(BUILD/'common.ld.h','local.eagle.app.v6.common.ld')):
        run([TOOL/'xtensa-lx106-elf-gcc','-E','-P','-CC','-x','c',*defines,'-I'+str(sdk/'ld'),source,'-o',BUILD/dest])
    elf=BUILD/'mathsmith_light.elf'
    lwip_library='lwip2-536-feat' if lwip_features else 'lwip2-536'
    libs=['hal','phy','pp','net80211',lwip_library,'wpa','crypto','main','wps','bearssl','espnow','smartconfig','airkiss','wpa2','stdc++','m','c','gcc']
    network_wrap=['-Wl,-wrap,lwip_hook_dhcp_amend_options'] if APP.name=='l01_webrepl' else []
    run([TOOL/'xtensa-lx106-elf-gcc','-nostdlib','-Os','-g','-Wl,--no-check-sections',
         '-u','app_entry','-u','_printf_float','-u','_scanf_float','-Wl,-static',
         '-L'+str(sdk/'lib'),'-L'+str(sdk/'lib/NONOSDK22x_190703'),'-L'+str(BUILD),
         '-L'+str(sdk/'libc/xtensa-lx106-elf/lib'),'-Tlocal.eagle.flash.ld','-Wl,--gc-sections',
         '-Wl,-wrap,system_restart_local','-Wl,-wrap,spi_flash_read',*network_wrap,
         '-Wl,-Map,'+str(BUILD/'mathsmith_light.map'),'-o',elf,'-Wl,--start-group',
         *appobjs,archive,*['-l'+x for x in libs],'-Wl,--end-group'])
    run([sys.executable,PLATFORM/'tools/elf2bin.py','--eboot',PLATFORM/'bootloaders/eboot/eboot.elf',
         '--app',elf,'--flash_mode','dio','--flash_freq','40','--flash_size','4M',
         '--path',TOOL,'--out',BUILD/'mathsmith_light.bin'])
    check_image(BUILD)
    run([sys.executable,PLATFORM/'tools/sizes.py','--elf',elf,'--path',TOOL,'--mmu',
         '-DMMU_IRAM_SIZE=0x8000 -DMMU_ICACHE_SIZE=0x8000'])
    (BUILD/'build_config.json').write_text(json.dumps({'core':'3.1.2','gcc':'10.3.0',
        'cpu_hz':80000000,'crystal_hz':26000000,'flash_bytes':4194304,'flash_mode':'dio',
        'flash_hz':40000000,'stack_bytes':stack_bytes,'engine_payload_cap':16384 if engine_enabled else 0,
        'variables':16 if engine_enabled else 0,'matrix_elements':64 if engine_enabled else 0,
        'stack_reserve_bytes':6144 if engine_enabled else 0,
        'program_store':{'filesystem':'LittleFS','flash_start':0x300000,'flash_end':0x3fa000,
                         'block':0x2000,'page':0x100} if APP.name=='l01_webrepl' else None,
        'defines':defines,'inputs':inputs_fingerprint() if engine_enabled else probe_inputs(APP)},indent=2)+'\n')

if __name__=='__main__': main()
