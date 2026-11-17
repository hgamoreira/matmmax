#ifndef MS_8266_PORT_H
#define MS_8266_PORT_H
#define MS_PRODUCT_NAME "MatmMax"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>
#define MS_STACK_RESERVE 6144U
#ifdef __cplusplus
extern "C" {
#endif
void *ms_malloc(size_t);
void *ms_calloc(size_t, size_t);
void *ms_realloc(void *, size_t);
void ms_free(void *);
size_t ms_used(void);
size_t ms_peak(void);
size_t ms_limit(void);
void ms_set_limit(size_t);
clock_t ms_clock(void);
FILE *ms_no_file(const char *, const char *);
int ms_no_remove(const char *);
int ms_stack_ok(void);
void ms_write(const char *);
int ms_poll(void);
void ms_diagnostics(void);
void ms_restart(void);
void ms_console_start(void);
void ms_console_key(int);
#ifdef __cplusplus
}
#endif
#ifdef MS_ENGINE_BUILD
#define malloc ms_malloc
#define calloc ms_calloc
#define realloc ms_realloc
#define free ms_free
#define clock ms_clock
#define fopen ms_no_file
#define remove ms_no_remove
#define finite ms_plot_finite
#endif
#endif
