#include "settings.h"
#ifdef MS_WEB_NATIVE
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
static unsigned char memory[2][MS_SETTINGS_SIZE];
static bool initialised;
static bool read_all(unsigned char out[2][MS_SETTINGS_SIZE])
{
    if (!initialised) { memset(memory,0xff,sizeof(memory)); initialised=true; }
    memcpy(out,memory,sizeof(memory));
    const char *path=getenv("MS_SETTINGS_FILE");
    if (!path) return true;
    FILE *f=fopen(path,"rb"); if (!f) return true;
    bool ok=fread(out,1,sizeof(memory),f)==sizeof(memory); fclose(f); return ok;
}
static bool write_all(const unsigned char in[2][MS_SETTINGS_SIZE])
{
    const char *path=getenv("MS_SETTINGS_FILE");
    if (path) {
        FILE *f=fopen(path,"wb"); if (!f) return false;
        bool ok=fwrite(in,1,sizeof(memory),f)==sizeof(memory) && !fflush(f) && !fsync(fileno(f));
        if (fclose(f)) ok=false;
        if (!ok) return false;
    }
    memcpy(memory,in,sizeof(memory)); return true;
}
extern "C" int ms_settings_read(unsigned slot,void *out,size_t n)
{
    unsigned char all[2][MS_SETTINGS_SIZE];
    if (slot>1 || n>MS_SETTINGS_SIZE || !read_all(all)) return 0;
    memcpy(out,all[slot],n); return 1;
}
extern "C" int ms_settings_erase(unsigned slot)
{
    unsigned char all[2][MS_SETTINGS_SIZE];
    if (getenv("MS_TEST_SETTINGS_FAIL") || slot>1 || !read_all(all)) return 0;
    memset(all[slot],0xff,MS_SETTINGS_SIZE); return write_all(all);
}
extern "C" int ms_settings_write(unsigned slot,size_t offset,const void *in,size_t n)
{
    unsigned char all[2][MS_SETTINGS_SIZE]; const auto *bytes=(const unsigned char *)in;
    if (getenv("MS_TEST_SETTINGS_FAIL") || slot>1 || offset+n>MS_SETTINGS_SIZE || !read_all(all)) return 0;
    for (size_t i=0;i<n;++i) { if ((all[slot][offset+i]&bytes[i])!=bytes[i]) return 0; all[slot][offset+i]=bytes[i]; }
    return write_all(all);
}
#else
#include <Arduino.h>
extern "C" { extern const char _FS_start, _FS_end, _EEPROM_start; }
static bool layout_ok()
{
    return ESP.getFlashChipRealSize()==4194304 && (uintptr_t)&_FS_start==0x405fa000U &&
        (uintptr_t)&_FS_end==0x405fa000U && (uintptr_t)&_EEPROM_start==0x405fb000U;
}
extern "C" int ms_settings_read(unsigned slot,void *out,size_t n)
{
    return slot<2 && n<=MS_SETTINGS_SIZE && !(n%4) && layout_ok() &&
        ESP.flashRead(MS_SETTINGS_FLASH+slot*MS_SETTINGS_SECTOR,(uint32_t *)out,n);
}
extern "C" int ms_settings_erase(unsigned slot)
{
    return slot<2 && layout_ok() && ESP.flashEraseSector(MS_SETTINGS_FLASH/MS_SETTINGS_SECTOR+slot);
}
extern "C" int ms_settings_write(unsigned slot,size_t offset,const void *in,size_t n)
{
    return slot<2 && offset+n<=MS_SETTINGS_SIZE && !(offset%4) && !(n%4) && layout_ok() &&
        ESP.flashWrite(MS_SETTINGS_FLASH+slot*MS_SETTINGS_SECTOR+offset,(const uint32_t *)in,n);
}
#endif
