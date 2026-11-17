#include <string.h>
#include "launcher.h"

int wk_init(WkLauncher *l, const WkApp *apps, size_t count,
            const WkServices *s)
{
    size_t i, j;
    if (!l || (!apps && count) || !s || !s->allocate || !s->release ||
        !s->read_key || !s->write || !s->reset_input) return WK_INVALID;
    for (i = 0; i < count; ++i) {
        if (!apps[i].name || !*apps[i].name || !apps[i].description || !apps[i].run)
            return WK_INVALID;
        for (j = 0; j < i; ++j) {
            if (!strcmp(apps[i].name, apps[j].name)) return WK_INVALID;
        }
    }
    l->apps = apps;
    l->count = count;
    l->services = *s;
    l->active = NULL;
    return WK_OK;
}

const WkApp *wk_find(const WkLauncher *l, const char *name)
{
    size_t i;
    if (!l || !name) return NULL;
    for (i = 0; i < l->count; ++i) {
        if (!strcmp(l->apps[i].name, name)) return &l->apps[i];
    }
    return NULL;
}

int wk_run(WkLauncher *l, const char *name, const char *arguments)
{
    const WkApp *app;
    WkAppContext context;
    void *arena;
    int result;
    if (!l) return WK_INVALID;
    if (l->active) return WK_BUSY;
    app = wk_find(l, name);
    if (!app) return WK_UNKNOWN;
    l->active = app;
    context.memory = NULL;
    if (app->memory_required) {
        context.memory = l->services.allocate(l->services.ctx, app->memory_required);
        if (!context.memory) {
            l->active = NULL;
            return WK_NO_MEMORY;
        }
        memset(context.memory, 0, app->memory_required);
    }
    context.memory_size = app->memory_required;
    arena = context.memory;
    context.arguments = arguments ? arguments : "";
    context.io = l->services;
    l->services.reset_input(l->services.ctx);
    result = app->run(&context);
    /* The same cleanup path serves normal, cancelled and error returns. */
    l->services.reset_input(l->services.ctx);
    if (arena) l->services.release(l->services.ctx, arena);
    l->active = NULL;
    if (result == WK_APP_OK) return WK_OK;
    if (result == WK_APP_CANCEL) return WK_CANCELLED;
    return WK_APP_FAILED;
}
