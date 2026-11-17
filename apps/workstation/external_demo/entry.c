/* Freestanding ELF entry: no direct SDK calls or libc allocation imports. */
#include "app_api.h"
#include "demo.h"

#ifndef APP_GREETING
#define APP_GREETING "External demo A: loaded from a separately built ELF.\n"
#endif
static unsigned calls;

void app_main(int argc, char **argv)
{
    const WkAppApiV1 *api = wk_app_api_v1();
    (void)argc;
    (void)argv;
    if (!api || api->abi_version != WK_APP_ABI_V1 ||
        api->struct_size < sizeof(*api) || !api->context || !api->finish) return;
    if (++calls != 1) {
        api->context->io.write(api->context->io.ctx, "External state was not reset.\n");
        api->finish(api->host, WK_APP_ERROR);
        return;
    }
    api->context->io.write(api->context->io.ctx, APP_GREETING);
    api->finish(api->host, wk_demo_run(api->context));
}
