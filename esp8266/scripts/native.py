"""Compile exactly the generated light engine and serial front end on Linux."""
from pathlib import Path
import subprocess
from light_profile import ROOT,BUILD,APP,BASE,APP_NAME,generate


def build():
    engine=generate()
    out=BUILD/'native'
    out.mkdir(exist_ok=True)
    objects=[]
    extra=[APP/'console.c',BASE/'allocator.c',ROOT/'esp8266/tests/native.c']
    if APP_NAME=='l01_webrepl':
        html=(APP/'index.html').read_text()
        (BUILD/'web_page.h').write_text('static const char web_page[] PROGMEM = R"MSPAGE('+html+')MSPAGE";\n')
        extra=list(APP.glob('*.c'))+list(APP.glob('*.cpp'))+[BASE/'allocator.c',ROOT/'esp8266/tests_web/native_main.cpp']
    for src in [engine/(x+'.c') for x in ('engine','mat','linalg','plot')]+extra:
        obj=out/(src.name+'.o')
        cpp=src.suffix=='.cpp'
        cmd=['g++' if cpp else 'gcc','-std=c++17' if cpp else '-std=c99','-O2','-g','-fPIC',
             '-I'+str(engine),'-I'+str(APP),'-I'+str(BASE),'-I'+str(BUILD),'-I'+str(ROOT/'esp8266/tests_web')]
        if APP_NAME=='l01_webrepl': cmd+=['-DMS_WEB_NATIVE']
        if src.parent==engine: cmd+=['-DMS_ENGINE_BUILD','-include',str(BASE/'port.h')]
        subprocess.run(cmd+['-c',str(src),'-o',str(obj)],check=True)
        objects.append(str(obj))
    subprocess.run(['g++' if APP_NAME=='l01_webrepl' else 'gcc',*objects,'-lm','-o',str(out/'mathsmith_light')],check=True)
    if APP_NAME=='l01_webrepl':
        subprocess.run(['gcc','-shared',str(out/'protocol.c.o'),'-o',str(out/'protocol.so')],check=True)
    subprocess.run(['gcc','-shared',str(out/'allocator.c.o'),'-o',str(out/'allocator.so')],check=True)
    return out/'mathsmith_light'

if __name__=='__main__': print(build())
