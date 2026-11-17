/* Native workstation app ABI v1. Build separately for each target CPU/toolchain. */
#ifndef WORKSTATION_APP_API_H
#define WORKSTATION_APP_API_H
#include <stdint.h>
#include "launcher.h"
#define WK_APP_ABI_V1 1U
typedef struct {
    uint32_t abi_version;
    uint32_t struct_size;
    WkAppContext *context; /* borrowed; valid only during this app's entry call */
    void *host;
    void (*finish)(void *host, int result);
} WkAppApiV1;
/* Only public bootstrap import; returns NULL outside a loaded app call. */
const WkAppApiV1 *wk_app_api_v1(void);
#endif
