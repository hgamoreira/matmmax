#ifndef MS_SETTINGS_H
#define MS_SETTINGS_H
#include <stddef.h>
#include <stdint.h>
#define MS_SETTINGS_SIZE 160U
#define MS_SETTINGS_FLASH 0x3fa000U
#define MS_SETTINGS_SECTOR 4096U
typedef struct { char ssid[33], password[64], key[33]; uint32_t generation; } MsSettings;
enum { MS_STORE_EMPTY=0, MS_STORE_OK=1, MS_STORE_UNCHANGED=2,
       MS_STORE_IO=-1, MS_STORE_INVALID=-2, MS_STORE_CLEANUP=-3 };
#ifdef __cplusplus
extern "C" {
#endif
int ms_settings_load(MsSettings *);
int ms_settings_save(const MsSettings *);
int ms_settings_forget(void);
/* Target flash adapter; offsets/sizes must be multiples of four. */
int ms_settings_read(unsigned slot, void *, size_t);
int ms_settings_erase(unsigned slot);
int ms_settings_write(unsigned slot, size_t offset, const void *, size_t);
#ifdef __cplusplus
}
#endif
#endif
