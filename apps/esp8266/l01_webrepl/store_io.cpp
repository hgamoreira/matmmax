/* Program store backend (XD118). Board: the core's LittleFS on its standard
 * 4M1M range, physical 0x300000..0x3F9FFF, below the two settings sectors.
 * Native: a folder named by MS_NATIVE_STORE (missing folder = erased flash;
 * a plain file in its place = an unreadable store). Not a flash emulator. */
#include "store.h"
#ifdef MS_WEB_NATIVE
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
static bool mounted;
static long written_total;
static const char *root() { return getenv("MS_NATIVE_STORE"); }
static std::string full(const char *path) { return std::string(root())+path; }
extern "C" int ms_fs_layout_ok(void) { return !getenv("MS_TEST_STORE_LAYOUT_BAD"); }
extern "C" int ms_fs_mount(void)
{
    struct stat st;
    mounted=root() && !stat(root(),&st) && S_ISDIR(st.st_mode);
    return mounted;
}
extern "C" int ms_fs_mounted(void) { return mounted; }
extern "C" int ms_fs_blank(void)
{
    struct stat st;
    return root() && stat(root(),&st) && errno==ENOENT;
}
extern "C" int ms_fs_format(void)
{
    struct stat st;
    mounted=false;
    if (!root() || getenv("MS_TEST_STORE_FORMAT_FAIL")) return 0;
    if (!stat(root(),&st) && !S_ISDIR(st.st_mode) && unlink(root())) return 0;
    if (DIR *d=opendir(root())) {
        while (dirent *e=readdir(d)) if (e->d_name[0]!='.') unlink((std::string(root())+"/"+e->d_name).c_str());
        closedir(d);
    } else if (mkdir(root(),0700)) return 0;
    return ms_fs_mount();
}
extern "C" void *ms_fs_open(const char *path,int write)
{
    if (!mounted) return nullptr;
    return fopen(full(path).c_str(),write ? "wb" : "rb");
}
extern "C" long ms_fs_read(void *file,void *buffer,size_t n)
{
    size_t got=fread(buffer,1,n,(FILE *)file);
    return ferror((FILE *)file) ? -1 : (long)got;
}
extern "C" long ms_fs_write(void *file,const void *buffer,size_t n)
{
    /* MS_TEST_STORE_FULL=N: writes beyond N bytes in this process fall short. */
    if (const char *limit=getenv("MS_TEST_STORE_FULL")) {
        long room=atol(limit)-written_total;
        if (room<(long)n) n=room>0 ? (size_t)room : 0;
    }
    size_t put=fwrite(buffer,1,n,(FILE *)file);
    written_total+=(long)put;
    return (long)put;
}
extern "C" void ms_fs_close(void *file) { fclose((FILE *)file); }
extern "C" long ms_fs_size(const char *path)
{
    struct stat st;
    return mounted && !stat(full(path).c_str(),&st) && S_ISREG(st.st_mode) ? (long)st.st_size : -1;
}
extern "C" int ms_fs_remove(const char *path) { return mounted && !unlink(full(path).c_str()); }
extern "C" int ms_fs_rename(const char *from,const char *to)
{
    return mounted && !getenv("MS_TEST_STORE_RENAME_FAIL") && !rename(full(from).c_str(),full(to).c_str());
}
extern "C" int ms_fs_each(int (*fn)(void *,const char *,long),void *ctx)
{
    if (!mounted) return 0;
    DIR *d=opendir(root()); if (!d) return 0;
    while (dirent *e=readdir(d)) {
        struct stat st;
        std::string name=std::string("/")+e->d_name;
        if (stat(full(name.c_str()).c_str(),&st) || !S_ISREG(st.st_mode)) continue;
        if (!fn(ctx,e->d_name,(long)st.st_size)) break;
    }
    closedir(d); return 1;
}
extern "C" int ms_fs_usage(unsigned long *used,unsigned long *total)
{
    struct Sum { unsigned long bytes; } sum{0};
    if (!ms_fs_each([](void *ctx,const char *,long size) { ((Sum *)ctx)->bytes+=(unsigned long)size; return 1; },&sum)) return 0;
    *used=sum.bytes; *total=1024000UL; return 1;
}
#else
#include <Arduino.h>
#include <LittleFS.h>
#include <new>
#include <flash_hal.h>   /* FS_start, FS_end, FS_page, FS_block, EEPROM_start */
static bool mounted;
extern "C" int ms_fs_layout_ok(void)
{
    return ESP.getFlashChipRealSize()==4194304 && FS_start==0x40500000U && FS_end==0x405fa000U &&
        FS_page==0x100U && FS_block==0x2000U && EEPROM_start==0x405fb000U;
}
extern "C" int ms_fs_mount(void)
{
    if (!ms_fs_layout_ok()) return 0;
    LittleFSConfig config(false);             /* never format without being asked */
    LittleFS.setConfig(config);
    mounted=LittleFS.begin();
    return mounted;
}
extern "C" int ms_fs_mounted(void) { return mounted; }
/* Both superblock candidates (blocks 0 and 1) entirely erased. */
extern "C" int ms_fs_blank(void)
{
    uint32_t words[64];
    if (!ms_fs_layout_ok()) return 0;
    const uint32_t start=FS_PHYS_ADDR;
    for (uint32_t offset=0;offset<2*0x2000U;offset+=sizeof(words)) {
        if (!ESP.flashRead(start+offset,words,sizeof(words))) return 0;
        for (uint32_t w : words) if (w!=0xffffffffU) return 0;
    }
    return 1;
}
extern "C" int ms_fs_format(void)
{
    if (!ms_fs_layout_ok()) return 0;
    LittleFS.end(); mounted=false;
    if (!LittleFS.format()) return 0;
    return ms_fs_mount();
}
extern "C" void *ms_fs_open(const char *path,int write)
{
    if (!mounted) return nullptr;
    if (!write && !LittleFS.exists(path)) return nullptr;
    File f=LittleFS.open(path,write ? "w" : "r");
    if (!f) return nullptr;
    File *held=new (std::nothrow) File(f);
    if (!held) f.close();
    return held;
}
extern "C" long ms_fs_read(void *file,void *buffer,size_t n)
{
    File *f=(File *)file;
    if (!f->available()) return 0;
    int got=f->read((uint8_t *)buffer,n);
    return got<0 ? -1 : (long)got;
}
extern "C" long ms_fs_write(void *file,const void *buffer,size_t n)
{
    return (long)((File *)file)->write((const uint8_t *)buffer,n);
}
extern "C" void ms_fs_close(void *file) { File *f=(File *)file; f->close(); delete f; }
extern "C" long ms_fs_size(const char *path)
{
    if (!mounted || !LittleFS.exists(path)) return -1;
    File f=LittleFS.open(path,"r");
    if (!f || !f.isFile()) return -1;
    long size=(long)f.size(); f.close(); return size;
}
extern "C" int ms_fs_remove(const char *path) { return mounted && LittleFS.remove(path); }
extern "C" int ms_fs_rename(const char *from,const char *to) { return mounted && LittleFS.rename(from,to); }
extern "C" int ms_fs_each(int (*fn)(void *,const char *,long),void *ctx)
{
    if (!mounted) return 0;
    Dir d=LittleFS.openDir("/");
    while (d.next()) {
        if (!d.isFile()) continue;
        if (!fn(ctx,d.fileName().c_str(),(long)d.fileSize())) break;
    }
    return 1;
}
extern "C" int ms_fs_usage(unsigned long *used,unsigned long *total)
{
    FSInfo info;
    if (!mounted || !LittleFS.info(info)) return 0;
    *used=(unsigned long)info.usedBytes; *total=(unsigned long)info.totalBytes; return 1;
}
#endif
