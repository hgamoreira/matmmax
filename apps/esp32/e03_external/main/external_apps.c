#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "driver/uart.h"
#include "esp_elf.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "external_apps.h"
#include "app_api.h"
#include "package.h"

extern const unsigned char wk_sample_msp[];
extern const size_t wk_sample_msp_size;
static WkServices services;
static uint8_t *uploaded;
static size_t uploaded_size;
static WkPackage selected;
static WkAppApiV1 api;
static int app_result;
static bool finished;

const WkAppApiV1 *wk_app_api_v1(void)
{
    return api.context ? &api : NULL;
}

static void app_finish(void *host, int result)
{
    (void)host;
    if (api.context && !finished) {
        app_result = result;
        finished = true;
    }
}

static uintptr_t resolve_public(const char *name)
{
    if (!strcmp(name, "wk_app_api_v1")) return (uintptr_t)wk_app_api_v1;
    if (!strcmp(name, "strcmp")) return (uintptr_t)strcmp;
    return 0;
}

static int load_image(const WkPackage *package, esp_elf_t *elf)
{
    int result = esp_elf_init(elf);
    if (result) return result;
    return esp_elf_relocate(elf, package->elf, package->elf_size);
}

static int run_loaded(WkAppContext *context)
{
    esp_elf_t elf = {0};
    int error = load_image(&selected, &elf);
    if (error) {
        printf("ELF load failed: %d\n", error);
        esp_elf_deinit(&elf);
        return WK_APP_ERROR;
    }
    app_result = WK_APP_ERROR;
    finished = false;
    api.context = context;
    error = esp_elf_request(&elf, 0, 0, NULL);
    api.context = NULL;
    esp_elf_deinit(&elf);
    if (error || !finished) return WK_APP_ERROR;
    return app_result;
}

void external_apps_init(const WkServices *s)
{
    services = *s;
    api.abi_version = WK_APP_ABI_V1;
    api.struct_size = sizeof(api);
    api.finish = app_finish;
    /* No general libc/SDK export table: only this explicit bootstrap contract. */
    elf_set_symbol_resolver(resolve_public);
}

void external_apps_list(void)
{
    puts("  sample - Separately built ELF demo A (loader fixture)");
    if (uploaded) printf("  uploaded - Serial-installed app (%zu bytes, RAM only)\n", uploaded_size);
}

static bool run_package(const uint8_t *bytes, size_t size, const char *name, const char *args)
{
    WkLauncher launcher;
    WkApp app = {name, "External native program", 0, run_loaded};
    int result;
    if (wk_package_check(bytes, size, &selected)) {
        puts("Invalid app package; workspace kept.");
        return true;
    }
    app.memory_required = selected.arena_size;
    if (wk_init(&launcher, &app, 1, &services) != WK_OK) {
        puts("External launcher configuration error.");
        return true;
    }
    result = wk_run(&launcher, name, args);
    if (result == WK_OK) puts("Returned to MathSmith; workspace kept.");
    else if (result == WK_CANCELLED) puts("Program cancelled; returned to MathSmith; workspace kept.");
    else if (result == WK_NO_MEMORY) puts("Not enough memory to start application. Workspace kept.");
    else puts("External program returned an error; MathSmith workspace kept.");
    return true;
}

static bool read_exact(uint8_t *buffer, size_t size)
{
    int64_t deadline = esp_timer_get_time()+5000000;
    size_t got = 0;
    while (got < size) {
        int64_t left = deadline-esp_timer_get_time();
        if (left <= 0) return false;
        TickType_t ticks = pdMS_TO_TICKS((unsigned)(left/1000));
        if (!ticks) ticks = 1;
        int n = uart_read_bytes(UART_NUM_0, buffer+got, size-got, ticks);
        if (n < 0) return false;
        got += (size_t)n;
    }
    return true;
}

static void receive_upload(const char *arguments)
{
    char *end;
    errno = 0;
    unsigned long length = strtoul(arguments, &end, 10);
    if (*arguments < '0' || *arguments > '9' || end == arguments || errno ||
        length < WK_PACKAGE_HEADER || length > WK_PACKAGE_MAX ||
        (*end != ' ' && *end != '\t')) {
        puts("ERROR upload header: expected size and 8-digit CRC32");
        return;
    }
    while (*end == ' ' || *end == '\t') ++end;
    const char *crc_text = end;
    if (strlen(crc_text) != 8 || strspn(crc_text, "0123456789abcdefABCDEF") != 8) {
        puts("ERROR upload header: expected size and 8-digit CRC32");
        return;
    }
    uint32_t expected = (uint32_t)strtoul(crc_text, &end, 16);
    uint8_t *candidate = heap_caps_malloc(length, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!candidate) { puts("ERROR upload: no memory; previous app kept"); return; }
    services.reset_input(services.ctx);
    puts("READY 256");
    size_t offset = 0;
    uint32_t crc = 0;
    const char *failure = NULL;
    while (offset < length) {
        size_t count = length-offset;
        if (count > 256) count = 256;
        if (!read_exact(candidate+offset, count)) {
            failure = "timeout";
            break;
        }
        crc = wk_crc32(crc, candidate+offset, count);
        offset += count;
        printf("ACK %zu\n", offset);
    }
    WkPackage package;
    esp_elf_t check = {0};
    if (!failure && crc != expected) failure = "CRC32 mismatch";
    if (!failure && wk_package_check(candidate, length, &package)) failure = "invalid package";
    if (!failure && load_image(&package, &check)) failure = "invalid or incompatible ELF";
    esp_elf_deinit(&check);
    services.reset_input(services.ctx);
    if (failure) {
        heap_caps_free(candidate);
        printf("ERROR upload: %s; previous app kept\n", failure);
    } else {
        heap_caps_free(uploaded);
        uploaded = candidate;
        uploaded_size = length;
        printf("INSTALLED uploaded %zu\n", uploaded_size);
    }
}

static const char *skip_space(const char *p)
{
    while (*p == ' ' || *p == '\t') ++p;
    return p;
}

bool external_apps_dispatch(const char *line, bool block_open)
{
    if (!strncmp(line, ":upload", 7) && (!line[7] || line[7] == ' ' || line[7] == '\t')) {
        if (block_open) puts("Finish or abort the open MathSmith block before uploading.");
        else receive_upload(skip_space(line+7));
        return true;
    }
    if (!strcmp(line, ":unload")) {
        heap_caps_free(uploaded);
        uploaded = NULL;
        uploaded_size = 0;
        puts("Uploaded app removed from RAM; workspace kept.");
        return true;
    }
    if (!strncmp(line, ":app", 4) && (line[4] == ' ' || line[4] == '\t')) {
        const char *p = skip_space(line+4);
        const char *end = p;
        while (*end && *end != ' ' && *end != '\t') ++end;
        size_t n = (size_t)(end-p);
        bool sample = n == 6 && !memcmp(p, "sample", 6);
        bool external = n == 8 && !memcmp(p, "uploaded", 8);
        if (!sample && !external) return false;
        if (block_open) { puts("Finish or abort the open MathSmith block before launching a program."); return true; }
        if (sample) return run_package(wk_sample_msp, wk_sample_msp_size, "sample", skip_space(end));
        if (!uploaded) { puts("No uploaded app; transfer a package first. Workspace kept."); return true; }
        return run_package(uploaded, uploaded_size, "uploaded", skip_space(end));
    }
    return false;
}
