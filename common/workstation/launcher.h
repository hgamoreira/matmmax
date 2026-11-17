/* Portable foreground app lifecycle; no CPU, SDK, MathSmith or filesystem API. */
#ifndef WORKSTATION_LAUNCHER_H
#define WORKSTATION_LAUNCHER_H
#include <stddef.h>

typedef struct {
    void *ctx;
    void *(*allocate)(void *ctx, size_t bytes);
    void (*release)(void *ctx, void *memory);
    int (*read_key)(void *ctx);
    void (*write)(void *ctx, const char *text);
    void (*reset_input)(void *ctx);
} WkServices;

typedef struct {
    WkServices io;
    void *memory;               /* zeroed arena, borrowed for this run only */
    size_t memory_size;
    const char *arguments;     /* borrowed for this run only */
} WkAppContext;

#define WK_APP_OK 0
#define WK_APP_CANCEL 1
#define WK_APP_ERROR 2

typedef struct {
    const char *name;
    const char *description;
    size_t memory_required;
    int (*run)(WkAppContext *context);
} WkApp;

typedef struct {
    const WkApp *apps;
    size_t count;
    WkServices services;
    const WkApp *active;
} WkLauncher;

#define WK_OK 0
#define WK_UNKNOWN 1
#define WK_BUSY 2
#define WK_NO_MEMORY 3
#define WK_CANCELLED 4
#define WK_APP_FAILED 5
#define WK_INVALID 6

int wk_init(WkLauncher *launcher, const WkApp *apps, size_t count,
            const WkServices *services);
const WkApp *wk_find(const WkLauncher *launcher, const char *name);
int wk_run(WkLauncher *launcher, const char *name, const char *arguments);
#endif
