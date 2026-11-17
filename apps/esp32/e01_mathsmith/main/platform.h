#ifndef MS_S3_PLATFORM_H
#define MS_S3_PLATFORM_H
#include <stddef.h>
#include <time.h>
void *ms_malloc(size_t size);
void *ms_calloc(size_t count, size_t size);
void *ms_realloc(void *ptr, size_t size);
void ms_free(void *ptr);
size_t ms_memory_used(void);
size_t ms_memory_peak(void);
size_t ms_memory_limit(void);
void ms_set_memory_limit(size_t size); /* also used by native failure tests */
clock_t ms_clock(void);
#endif
