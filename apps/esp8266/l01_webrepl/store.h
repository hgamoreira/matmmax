/* L02 program store (XD118): MatmMax programs as name.m on a flash filesystem.
 * store.c holds names, limits, commands and the engine's read stream;
 * store_io.cpp is the backend (LittleFS on the board, a folder natively). */
#ifndef MS_STORE_H
#define MS_STORE_H
#include <stddef.h>
#include <stdio.h>
#ifdef __cplusplus
extern "C" {
#endif
#define MS_STORE_NAME_MAX 24      /* letters, digits, _; starts with a letter */
#define MS_STORE_FILE_MAX 16384   /* bytes per program, the engine payload cap */
#define MS_STORE_TEMP "/save.tmp"

/* Console: 0 not a store command, 1 done, 2 done with an error. */
int ms_store_command(void *session, const char *line);
/* Boot: mount, formatting only a completely erased region. Prints one line. */
void ms_store_start(void);
/* Engine stream (fopen is mapped here in the L02 engine profile): "r" only. */
FILE *ms_store_fopen(const char *name, const char *mode);
/* name -> "/name.m" after validation and lower-casing; 0 if invalid. */
int ms_store_path(const char *name, char path[MS_STORE_NAME_MAX+4]);

/* Browser save: begin, data pieces, end (atomic replace) or abort. Each call
 * returns 1 on success; failures print one "error: ..." line and abort. */
int ms_store_save_begin(const char *name);
int ms_store_save_data(const char *data, size_t n);
int ms_store_save_end(void *session);
void ms_store_save_abort(void);
int ms_store_saving(void);
/* Browser read/list: the sink receives raw text pieces. Returns 1 or 0. */
int ms_store_read(const char *name, void (*sink)(const char *, size_t));
int ms_store_list(void (*sink)(const char *, size_t));

/* Backend (store_io.cpp). Paths are "/name.m" or MS_STORE_TEMP. */
int ms_fs_layout_ok(void);
int ms_fs_mount(void);
int ms_fs_blank(void);
int ms_fs_format(void);
int ms_fs_mounted(void);
void *ms_fs_open(const char *path, int write);
long ms_fs_read(void *file, void *buffer, size_t n);
long ms_fs_write(void *file, const void *buffer, size_t n);
void ms_fs_close(void *file);
long ms_fs_size(const char *path);
int ms_fs_remove(const char *path);
int ms_fs_rename(const char *from, const char *to);
/* Calls fn for each regular file in "/" (name without "/"); stops if fn returns 0. */
int ms_fs_each(int (*fn)(void *ctx, const char *name, long size), void *ctx);
int ms_fs_usage(unsigned long *used, unsigned long *total);
/* Engine: is this name a workspace variable (generated L02 profile only). */
int ms_engine_is_variable(void *session, const char *name);
#ifdef __cplusplus
}
#endif
#endif
