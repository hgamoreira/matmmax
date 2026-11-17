/* E00: board/console/memory smoke test. No MatmMax engine or app launcher. */
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "driver/uart.h"
#include "driver/uart_vfs.h"
#include "esp_chip_info.h"
#include "esp_err.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_idf_version.h"
#include "esp_memory_utils.h"
#include "esp_psram.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define INTERNAL_CAPS (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)
#define EXTERNAL_CAPS (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
#define LINE_CAPACITY 128

static void show_info(void)
{
    esp_chip_info_t chip;
    uint32_t flash_size = 0;
    esp_chip_info(&chip);
    puts("MatmMax workstation E00 - console smoke test");
    printf("SDK: %s\n", esp_get_idf_version());
    printf("Chip: ESP32-S3 cores=%u revision=%u\n", chip.cores, chip.revision);
    if (esp_flash_get_size(NULL, &flash_size) == ESP_OK) {
        printf("Flash: %" PRIu32 " bytes\n", flash_size);
    } else {
        puts("Flash: query failed");
    }
    printf("PSRAM: %s bytes=%zu\n",
           esp_psram_is_initialized() ? "ready" : "unavailable",
           esp_psram_is_initialized() ? esp_psram_get_size() : 0);
    puts("Console: UART0 115200 8N1, no flow control");
    printf("double: %zu bytes\n", sizeof(double));
}

static void show_memory(void)
{
    printf("Internal: total=%zu free=%zu largest=%zu\n",
           heap_caps_get_total_size(INTERNAL_CAPS),
           heap_caps_get_free_size(INTERNAL_CAPS),
           heap_caps_get_largest_free_block(INTERNAL_CAPS));
    printf("External: total=%zu free=%zu largest=%zu\n",
           heap_caps_get_total_size(EXTERNAL_CAPS),
           heap_caps_get_free_size(EXTERNAL_CAPS),
           heap_caps_get_largest_free_block(EXTERNAL_CAPS));
}

static uint8_t pattern(size_t offset, unsigned pass)
{
    return (uint8_t)((offset * 37U) ^ (offset >> 8) ^
                     (offset >> 16) ^ (pass ? 0xa5U : 0x5aU));
}

static bool check_buffer(const char *name, size_t size, uint32_t caps,
                         bool external)
{
    volatile uint8_t *buffer = heap_caps_malloc(size, caps);
    if (!buffer) {
        printf("%s: FAIL allocation (%zu bytes)\n", name, size);
        return false;
    }
    bool ok = external ? esp_ptr_external_ram((const void *)buffer)
                       : esp_ptr_internal((const void *)buffer);
    for (unsigned pass = 0; ok && pass < 2; ++pass) {
        for (size_t i = 0; i < size; ++i) {
            buffer[i] = pattern(i, pass);
        }
        for (size_t i = 0; i < size; ++i) {
            if (buffer[i] != pattern(i, pass)) {
                printf("%s: mismatch at %zu\n", name, i);
                ok = false;
                break;
            }
        }
        /* Release the CPU between passes on the real board. */
        vTaskDelay(1);
    }
    heap_caps_free((void *)buffer);
    printf("%s: %s (%zu bytes, two patterns)\n", name, ok ? "PASS" : "FAIL", size);
    return ok;
}

static void self_test(void)
{
    bool ok = check_buffer("Internal RAM", 4096, INTERNAL_CAPS, false);
    bool has_psram = esp_psram_is_initialized();
    if (!has_psram) {
        puts("PSRAM: FAIL unavailable (console remains usable)");
        ok = false;
    } else {
        if (!check_buffer("PSRAM", 1024 * 1024, EXTERNAL_CAPS, true)) {
            ok = false;
        }
        /* More than the physical capacity must fail without exhausting RAM. */
        void *too_big = heap_caps_malloc(esp_psram_get_size() + 1, EXTERNAL_CAPS);
        bool rejected = (too_big == NULL);
        heap_caps_free(too_big);
        printf("Oversize allocation: %s\n", rejected ? "PASS rejected" : "FAIL accepted");
        if (!rejected) {
            ok = false;
        }
        if (!check_buffer("Recovery allocation", 4096, EXTERNAL_CAPS, true)) {
            ok = false;
        }
    }
    bool intact = heap_caps_check_integrity_all(true);
    printf("Heap integrity: %s\n", intact ? "PASS" : "FAIL");
    printf("SELFTEST: %s\n", ok && intact ? "PASS" : "FAIL");
}

static void dispatch(const char *line)
{
    if (!strcmp(line, "help")) {
        puts("help | info | mem | test | echo TEXT | reboot");
    } else if (!strcmp(line, "info")) {
        show_info();
    } else if (!strcmp(line, "mem")) {
        show_memory();
    } else if (!strcmp(line, "test")) {
        self_test();
    } else if (!strncmp(line, "echo ", 5)) {
        puts(line + 5);
    } else if (!strcmp(line, "reboot")) {
        puts("Rebooting...");
        fflush(stdout);
        uart_wait_tx_done(UART_NUM_0, pdMS_TO_TICKS(1000));
        esp_restart();
    } else if (*line) {
        puts("Unknown command; type help.");
    }
}

void app_main(void)
{
    const uart_config_t config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_param_config(UART_NUM_0, &config));
    /* Keep the boot console's default UART0 pins; CP2102 wiring is board-specific. */
    ESP_ERROR_CHECK(uart_driver_install(UART_NUM_0, 1024, 0, 0, NULL, 0));
    uart_vfs_dev_use_driver(UART_NUM_0);
    setvbuf(stdout, NULL, _IONBF, 0);
    show_info();
    show_memory();
    puts("Type help for commands. MatmMax engine is not loaded.");
    printf("e00> ");

    char line[LINE_CAPACITY];
    size_t used = 0;
    bool overflow = false;
    bool previous_cr = false;
    for (;;) {
        uint8_t ch;
        if (uart_read_bytes(UART_NUM_0, &ch, 1, portMAX_DELAY) != 1) {
            continue;
        }
        if (ch == '\n' && previous_cr) {
            previous_cr = false;
            continue;
        }
        previous_cr = (ch == '\r');
        if (ch == '\r' || ch == '\n') {
            putchar('\n');
            if (overflow) {
                puts("Line too long (127 bytes maximum); discarded.");
            } else {
                line[used] = '\0';
                dispatch(line);
            }
            used = 0;
            overflow = false;
            printf("e00> ");
        } else if (ch == 0x1b) {
            /* Cancel the whole pending line, including overflow. */
            used = 0;
            overflow = false;
            puts("\nCancelled.");
            printf("e00> ");
        } else if ((ch == '\b' || ch == 0x7f) && !overflow) {
            if (used) {
                --used;
                printf("\b \b");
            }
        } else if (ch >= 32 && ch <= 126 && !overflow) {
            if (used < sizeof(line) - 1) {
                line[used++] = (char)ch;
                putchar(ch);
            } else {
                overflow = true;
            }
        }
    }
}
