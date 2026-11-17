"""Install pinned ESP8266 core/compiler locally; optional offline archives."""
import argparse
import hashlib
from pathlib import Path
import shutil
import tarfile
import urllib.request
import zipfile

ROOT=Path(__file__).resolve().parents[1]
DEST=ROOT/'third_party/esp8266-3.1.2'
PACKAGES=(
 ('esp8266-3.1.2.zip','https://github.com/esp8266/Arduino/releases/download/3.1.2/esp8266-3.1.2.zip',
  'b3f47686d7783c120c2f10bf82788f921c53db8642cc87012599abb6e335b182','esp8266-3.1.2'),
 ('esp8266-gcc.tar.gz','https://github.com/earlephilhower/esp-quick-toolchain/releases/download/3.1.0-gcc10.3/x86_64-linux-gnu.xtensa-lx106-elf-e5f9fec.220621.tar.gz',
  '89d21f38a516e34e0bb44df7e30ef6b1bc2f7f125aae3b7986e5cae545a3ed83','xtensa-lx106-elf'))

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archives-dir',type=Path)
    args=parser.parse_args()
    cache=ROOT/'third_party/downloads'
    cache.mkdir(parents=True,exist_ok=True)
    DEST.mkdir(parents=True,exist_ok=True)
    for name,url,digest,directory in PACKAGES:
        archive=(args.archives_dir or cache)/name
        if not archive.exists():
            if args.archives_dir: raise SystemExit('Missing offline archive: '+str(archive))
            print('Downloading '+url,flush=True)
            with urllib.request.urlopen(url,timeout=60) as src, archive.open('wb') as dst:
                shutil.copyfileobj(src,dst)
        if hashlib.sha256(archive.read_bytes()).hexdigest()!=digest:
            raise SystemExit('Archive checksum mismatch: '+str(archive))
        if (DEST/directory).exists():
            print('Verified archive; keeping existing '+str(DEST/directory))
            continue
        if name.endswith('.zip'):
            with zipfile.ZipFile(archive) as z:
                for member in z.namelist():
                    if not (DEST/member).resolve().is_relative_to(DEST.resolve()):
                        raise SystemExit('Unsafe archive path')
                z.extractall(DEST)
        else:
            with tarfile.open(archive) as t: t.extractall(DEST,filter='data')
    print('ESP8266 Arduino core 3.1.2 / GCC 10.3 installed locally; no board opened.')

if __name__=='__main__': main()
