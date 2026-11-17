#ifndef MS_EXTERNAL_APPS_H
#define MS_EXTERNAL_APPS_H
#include <stdbool.h>
#include "launcher.h"
void external_apps_init(const WkServices *services);
void external_apps_list(void);
bool external_apps_dispatch(const char *line, bool block_open);
#endif
