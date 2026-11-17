#include <stdio.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "app_console.h"
#include "launcher.h"
#include "demo.h"
#ifdef MS_EXTERNAL_APPS
#include "external_apps.h"
#endif

static WkLauncher launcher;
static uint8_t (*read_character)(void);
static void (*reset_input)(void);
static const WkApp apps[] = {
    {"demo", "Launcher test program", WK_DEMO_MEMORY, wk_demo_run},
};

static void *app_allocate(void *ctx, size_t size)
{
    (void)ctx;
    return heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}
static void app_release(void *ctx, void *memory)
{
    (void)ctx;
    heap_caps_free(memory);
}
static int app_read(void *ctx)
{
    (void)ctx;
    return read_character();
}
static void app_write(void *ctx, const char *text)
{
    (void)ctx;
    fputs(text, stdout);
}
static void app_reset(void *ctx)
{
    (void)ctx;
    reset_input();
}

void app_console_init(uint8_t (*read)(void), void (*reset)(void))
{
    WkServices services = {NULL, app_allocate, app_release, app_read, app_write, app_reset};
    read_character = read;
    reset_input = reset;
#ifdef MS_EXTERNAL_APPS
    external_apps_init(&services);
#endif
    if (wk_init(&launcher, apps, sizeof(apps)/sizeof(apps[0]), &services) != WK_OK)
        puts("Application registry configuration error.");
}

static const char *skip_space(const char *p)
{
    while (*p == ' ' || *p == '\t') ++p;
    return p;
}

bool app_console_dispatch(const char *line, bool block_open)
{
#ifdef MS_EXTERNAL_APPS
    if (external_apps_dispatch(line, block_open)) return true;
#endif
    const char *p;
    char name[32];
    size_t n = 0;
    int result;
    if (!strncmp(line, ":apps", 5) && (!line[5] || line[5] == ' ' || line[5] == '\t')) {
        if (*skip_space(line+5)) puts("Usage: :apps");
        else {
            puts("Applications:");
            for (size_t i = 0; i < launcher.count; ++i)
                printf("  %s - %s\n", launcher.apps[i].name, launcher.apps[i].description);
#ifdef MS_EXTERNAL_APPS
            external_apps_list();
#endif
            puts("Use :app NAME [arguments]; closing it returns to this workspace.");
        }
        return true;
    }
    if (strncmp(line, ":app", 4) || (line[4] && line[4] != ' ' && line[4] != '\t'))
        return false;
    if (block_open) {
        puts("Finish or abort the open MatmMax block before launching a program.");
        return true;
    }
    p = skip_space(line+4);
    while (p[n] && p[n] != ' ' && p[n] != '\t') ++n;
    if (!n) { puts("Usage: :app NAME [arguments]"); return true; }
    if (n >= sizeof(name)) { puts("Application name too long."); return true; }
    memcpy(name, p, n);
    name[n] = '\0';
    p = skip_space(p+n);
    result = wk_run(&launcher, name, p);
    switch (result) {
    case WK_OK: puts("Returned to MatmMax; workspace kept."); break;
    case WK_CANCELLED: puts("Program cancelled; returned to MatmMax; workspace kept."); break;
    case WK_UNKNOWN: puts("Unknown application; use :apps. Workspace kept."); break;
    case WK_NO_MEMORY: puts("Not enough memory to start application. Workspace kept."); break;
    case WK_BUSY: puts("Another application is already active. Workspace kept."); break;
    case WK_APP_FAILED: puts("Program returned an error; MatmMax workspace kept."); break;
    default: puts("Application launcher configuration error."); break;
    }
    return true;
}
