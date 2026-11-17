/* Engine allocations: explicit external RAM, 8-byte alignment and a 4 MiB cap.
 * Native tests exercise the same accounting/failure code with libc as backend.
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "platform.h"
#ifndef MS_ALLOC_NATIVE_TEST
#include "esp_heap_caps.h"
#include "esp_timer.h"
#endif

typedef union {
    size_t size;
    double alignment;
} Header;
static size_t used, peak;
static size_t limit = 4U * 1024U * 1024U;

static void *backend_realloc(void *ptr, size_t size)
{
#ifdef MS_ALLOC_NATIVE_TEST
    return realloc(ptr, size);
#else
    return heap_caps_realloc(ptr, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#endif
}

void *ms_malloc(size_t size)
{
    Header *h;
    if (!size) size = 1;
    if (used > limit || size > limit - used || size > SIZE_MAX - sizeof(Header))
        return NULL;
    h = backend_realloc(NULL, sizeof(Header) + size);
    if (!h) return NULL;
    h->size = size;
    used += size;
    if (used > peak) peak = used;
    return h + 1;
}

void ms_free(void *ptr)
{
    if (ptr) {
        Header *h = (Header *)ptr - 1;
        used -= h->size;
#ifdef MS_ALLOC_NATIVE_TEST
        free(h);
#else
        heap_caps_free(h);
#endif
    }
}

void *ms_calloc(size_t count, size_t size)
{
    if (size && count > SIZE_MAX / size) return NULL;
    size_t bytes = count * size;
    void *ptr = ms_malloc(bytes);
    if (ptr) memset(ptr, 0, bytes);
    return ptr;
}

void *ms_realloc(void *ptr, size_t size)
{
    if (!ptr) return ms_malloc(size);
    if (!size) {
        ms_free(ptr);
        return NULL;
    }
    Header *h = (Header *)ptr - 1;
    size_t old = h->size;
    size_t other = used - old;
    if (other > limit || size > limit - other || size > SIZE_MAX - sizeof(Header))
        return NULL;
    Header *next = backend_realloc(h, sizeof(Header) + size);
    if (!next) return NULL; /* Original allocation/data/accounting survive. */
    next->size = size;
    used = other + size;
    if (used > peak) peak = used;
    return next + 1;
}

size_t ms_memory_used(void) { return used; }
size_t ms_memory_peak(void) { return peak; }
size_t ms_memory_limit(void) { return limit; }
void ms_set_memory_limit(size_t bytes) { limit = bytes; }

clock_t ms_clock(void)
{
#ifdef MS_ALLOC_NATIVE_TEST
    return clock();
#else
    /* Scale through seconds to avoid overflowing the intermediate product. */
    int64_t us = esp_timer_get_time();
    return (clock_t)((us / 1000000) * CLOCKS_PER_SEC +
                     (us % 1000000) * CLOCKS_PER_SEC / 1000000);
#endif
}
