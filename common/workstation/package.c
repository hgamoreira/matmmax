#include <string.h>
#include "package.h"
#include "app_api.h"

static uint32_t u32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
           (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

uint32_t wk_crc32(uint32_t previous, const void *data, size_t length)
{
    uint32_t crc = ~previous;
    const uint8_t *p = (const uint8_t *)data;
    size_t i;
    unsigned bit;
    for (i = 0; i < length; ++i) {
        crc ^= p[i];
        for (bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320U : 0);
    }
    return ~crc;
}

int wk_package_check(const uint8_t *data, size_t size, WkPackage *result)
{
    uint32_t arena, length;
    if (!data || !result || size < WK_PACKAGE_HEADER || size > WK_PACKAGE_MAX)
        return -1;
    if (memcmp(data, "MSAP\1\0\40\0", 8) ||
        u32(data+8) != WK_MACHINE_XTENSA || u32(data+12) != WK_APP_ABI_V1 ||
        u32(data+28) != WK_TARGET_ESP32S3) return -1;
    arena = u32(data+16);
    length = u32(data+20);
    if (!arena || arena > WK_ARENA_MAX || length < 52 ||
        length != size-WK_PACKAGE_HEADER) return -1;
    if (wk_crc32(0, data+WK_PACKAGE_HEADER, length) != u32(data+24)) return -1;
    result->elf = data+WK_PACKAGE_HEADER;
    result->elf_size = length;
    result->arena_size = arena;
    return 0;
}
