#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "port.h"
typedef union { size_t size; double alignment; } Header;
static size_t used, peak, limit = 16384;
size_t ms_used(void) { return used; }
size_t ms_peak(void) { return peak; }
size_t ms_limit(void) { return limit; }
void ms_set_limit(size_t value) { limit = value; }
void *ms_malloc(size_t size)
{
    Header *h;
    if (!size) size = 1;
    if (used > limit || size > limit-used || size > SIZE_MAX-sizeof(Header)) return NULL;
    h = malloc(sizeof(Header)+size);
    if (!h) return NULL;
    h->size = size; used += size;
    if (used > peak) peak = used;
    return h+1;
}
void ms_free(void *ptr)
{
    if (ptr) { Header *h = (Header *)ptr-1; used -= h->size; free(h); }
}
void *ms_calloc(size_t count, size_t size)
{
    void *ptr;
    if (size && count > SIZE_MAX/size) return NULL;
    size *= count;
    ptr = ms_malloc(size);
    if (ptr) memset(ptr,0,size);
    return ptr;
}
void *ms_realloc(void *ptr, size_t size)
{
    Header *h, *next;
    size_t other;
    if (!ptr) return ms_malloc(size);
    if (!size) { ms_free(ptr); return NULL; }
    h = (Header *)ptr-1; other = used-h->size;
    if (other > limit || size > limit-other || size > SIZE_MAX-sizeof(Header)) return NULL;
    next = realloc(h,sizeof(Header)+size);
    if (!next) return NULL;
    next->size = size; used = other+size;
    if (used > peak) peak = used;
    return next+1;
}
FILE *ms_no_file(const char *name, const char *mode)
{ (void)name; (void)mode; return NULL; }
int ms_no_remove(const char *name) { (void)name; return -1; }
