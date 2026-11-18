"""Generate a bounded build profile; never edit the shared validated sources."""
from pathlib import Path
import hashlib
import json
import os

ROOT = Path(__file__).resolve().parents[2]
APP_NAME = os.environ.get('MS_ESP8266_APP', 'l00_mathsmith')
if APP_NAME not in ('l00_mathsmith', 'l01_webrepl'):
    raise SystemExit('Unknown ESP8266 app: '+APP_NAME)
APP = ROOT / 'apps/esp8266' / APP_NAME
BASE = ROOT / 'apps/esp8266/l00_mathsmith'
BUILD = ROOT / 'build/esp8266' / APP_NAME
SOURCE = ROOT / 'apps/m01_mathsmith'
LIMITS = {
    'engine.c': {'#define MAXVARS 40': '#define MAXVARS 16',
                 '#define MAXFUNCS 12': '#define MAXFUNCS 4',
                 '#define MAXARGS 12': '#define MAXARGS 6',
                 '#define MAX_DEPTH 8': '#define MAX_DEPTH 4',
                 '#define MAXFFN 16': '#define MAXFFN 4',
                 '#define MAX_FDEPTH 12': '#define MAX_FDEPTH 4',
                 '#define MAX_EVALS 20000L': '#define MAX_EVALS 2000L',
                 '#define FMT_MAX 590': '#define FMT_MAX 128',
                 '#define OUT_MAX 2000': '#define OUT_MAX 256',
                 '#define MAX_STMTS 3000': '#define MAX_STMTS 128'},
    'mat.h': {'#define MAT_MAX_ELEMS 4096': '#define MAT_MAX_ELEMS 64'},
    'mat.c': {'#define POOL_MAX 48': '#define POOL_MAX 8'},
}

def inputs_fingerprint():
    files=list(SOURCE.glob('*.h'))+[SOURCE/(x+'.c') for x in ('engine','mat','linalg','plot')]
    for directory in set((BASE, APP)):
        for pattern in ('*.c', '*.cpp', '*.h', '*.html'):
            files += list(directory.glob(pattern))
    files += [Path(__file__),ROOT/'esp8266/scripts/build.py',ROOT/'esp8266/scripts/check_image.py']
    return {str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(set(files))}


def generate():
    dest = BUILD/'engine'
    dest.mkdir(parents=True, exist_ok=True)
    hashes = {}
    for name in ('engine.c','engine.h','mat.c','mat.h','linalg.c','linalg.h','plot.c','plot.h','version.h'):
        original = (SOURCE/name).read_bytes()
        text = original.decode()
        for old,new in LIMITS.get(name,{}).items():
            if text.count(old) != 1:
                raise SystemExit(f'Profile anchor changed in {name}: {old}; review limits before building')
            text = text.replace(old,new)
        if name=='engine.c':
            guards={
                'static Mat *parse_unary(Parser *P)\n{\n    Mat *m;':
                'static Mat *parse_unary(Parser *P)\n{\n    Mat *m;\n    if (!ms_stack_ok()) { set_err(P->s, "ESP8266 stack reserve reached"); return NULL; }',
                'static double fx_unary(Fx *x)\n{\n    double v;':
                'static double fx_unary(Fx *x)\n{\n    double v;\n    if (!ms_stack_ok()) { x->bad=1; return 0.0; }',
            }
            for old,new in guards.items():
                if text.count(old)!=1: raise SystemExit('Stack guard anchor changed; review parser')
                text=text.replace(old,new)
        if name=='engine.c' and APP_NAME=='l01_webrepl':
            definitions='static const struct {\n    const char *name;\n    int min, max;\n    BuiltinFn fn;\n} builtins[] = {'
            if text.count(definitions)!=1: raise SystemExit('Review GPIO builtin table anchor')
            text=text.replace(definitions,'#include "gpio_engine.h"\n\n'+definitions)
            entries='    {NULL, 0, 0, NULL}\n};\n\nstatic int builtin_index'
            if text.count(entries)!=1: raise SystemExit('Review GPIO builtin entries anchor')
            extra='    {"pinmode", 2, 3, b_pinmode}, {"digitalwrite", 2, 2, b_digitalwrite},\n'
            extra+='    {"digitalread", 1, 1, b_digitalread}, {"analogread", 0, 1, b_analogread}, {"delay", 1, 1, b_delay},\n'
            text=text.replace(entries,extra+entries)
            # XD118 program store: scripts are read from flash through a
            # read-only stream; the console asks whether a word is a variable.
            text=('#include "store.h"\n#undef fopen\n#define fopen ms_store_fopen\n'+text+
                  '\nint ms_engine_is_variable(void *s, const char *name)\n'
                  '{\n    return name_is_variable((MlSession *)s, name);\n}\n')
            if text.count('static int name_is_variable(MlSession *s, const char *p)\n')!=1:
                raise SystemExit('Review name_is_variable anchor for the program store')
        (dest/name).write_text(text)
        hashes[name] = hashlib.sha256(original).hexdigest()
    (BUILD/'engine_sources.json').write_text(json.dumps(hashes,indent=2)+'\n')
    return dest


if __name__ == '__main__': print(generate())
