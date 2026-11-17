#ifndef WORKSTATION_PACKAGE_H
#define WORKSTATION_PACKAGE_H
#include <stddef.h>
#include <stdint.h>
#define WK_PACKAGE_HEADER 32U
#define WK_PACKAGE_MAX 262144U
#define WK_ARENA_MAX 1048576U
#define WK_MACHINE_XTENSA 94U
#define WK_TARGET_ESP32S3 1U
typedef struct {
    const uint8_t *elf;
    size_t elf_size;
    size_t arena_size;
} WkPackage;
uint32_t wk_crc32(uint32_t previous, const void *data, size_t length);
/* Validate complete MSAP v1 framing, target/ABI, bounds and payload CRC.
 * ELF structure/linking validation is performed by the target ELF loader. */
int wk_package_check(const uint8_t *data, size_t size, WkPackage *result);
#endif
