/* Serial MatmMax front end. Platform commands use ':' to avoid variable names. */
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "driver/uart.h"
#include "driver/uart_vfs.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_psram.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "engine.h"
#include "mat.h"
#include "platform.h"
#ifdef MS_WORKSTATION_LAUNCHER
#include "app_console.h"
#endif

static MlSession *session;
static bool block_open, cancelled, queue_overflow;
static unsigned char queued[2048];
static size_t head, tail;
static int64_t last_yield;
#ifdef MS_WORKSTATION_LAUNCHER
static bool reset_line_ending;
#endif

static int poll_cancel(void)
{
    uint8_t ch;
    while (uart_read_bytes(UART_NUM_0, &ch, 1, 0) == 1) {
        if (ch == 0x1b || ch == 3) {
            cancelled = true;
            head = tail = 0;
            queue_overflow = false;
            uart_flush_input(UART_NUM_0);
            break;
        }
        size_t next = (tail + 1) % sizeof(queued);
        if (next == head) queue_overflow = true;
        else {
            queued[tail] = ch;
            tail = next;
        }
    }
    if (esp_timer_get_time() - last_yield >= 10000) {
        vTaskDelay(1);
        last_yield = esp_timer_get_time();
    }
    return cancelled;
}

static uint8_t read_key(void)
{
    uint8_t ch;
    if (head != tail) {
        ch = queued[head];
        head = (head + 1) % sizeof(queued);
        return ch;
    }
    while (uart_read_bytes(UART_NUM_0, &ch, 1, portMAX_DELAY) != 1) {}
    return ch;
}

#ifdef MS_WORKSTATION_LAUNCHER
static void reset_console_input(void)
{
    head = tail = 0;
    queue_overflow = false;
    uart_flush_input(UART_NUM_0);
    reset_line_ending = true;
}
#endif

static void sink(void *ctx, const char *text)
{
    (void)ctx;
    fputs(text, stdout);
    if (esp_timer_get_time() - last_yield >= 10000) {
        vTaskDelay(1);
        last_yield = esp_timer_get_time();
    }
}

static int no_display(void *ctx, const PlotSpec *spec)
{
    (void)ctx;
    (void)spec;
#ifdef MS_WORKSTATION_LAUNCHER
    puts("E02: plot data computed; display/output is not implemented yet.");
#else
    puts("E01: plot data computed; display/output is not implemented yet.");
#endif
    return 0;
}

static void reset_session(void)
{
    ml_close(session);
    session = ml_open(sink, NULL);
    block_open = false;
    if (session) {
        ml_set_plot_hook(session, no_display, NULL);
        puts("New MatmMax workspace.");
    } else {
        puts("MatmMax unavailable: cannot allocate workspace in PSRAM.");
    }
}

static void memory_info(void)
{
    printf("Engine payload: used=%zu peak=%zu limit=%zu bytes (PSRAM)\n",
           ms_memory_used(), ms_memory_peak(), ms_memory_limit());
    printf("Heap free: internal=%zu external=%zu bytes\n",
           heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
           heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    printf("Main stack minimum free: %u bytes\n", (unsigned)uxTaskGetStackHighWaterMark(NULL));
}

static void platform_info(void)
{
    uint32_t flash_size = 0;
    esp_flash_get_size(NULL, &flash_size);
#ifdef MS_WORKSTATION_LAUNCHER
#ifdef MS_EXTERNAL_APPS
    puts("MatmMax E03 ESP32-S3 - external program loader");
#else
    puts("MatmMax E02 ESP32-S3 - application launcher");
#endif
#else
    puts("MatmMax E01 ESP32-S3 - serial engine port");
#endif
    printf("SDK: %s\nFlash: %" PRIu32 " bytes\n", esp_get_idf_version(), flash_size);
    printf("PSRAM: %s bytes=%zu\n", esp_psram_is_initialized() ? "ready" : "unavailable",
           esp_psram_is_initialized() ? esp_psram_get_size() : 0);
    printf("Limits: %d elements/matrix, 40 variables, double=%zu bytes\n",
           MAT_MAX_ELEMS, sizeof(double));
#ifdef MS_WORKSTATION_LAUNCHER
    puts("UART0 115200 8N1. Use :apps and :app NAME for built-in programs.");
#else
    puts("UART0 115200 8N1. Files, AI, plot output and app launching are later steps.");
#endif
}

static void help(void)
{
    puts("MatmMax: expressions, variables, matrices, functions and multiline blocks.");
    puts("Examples: x=2^10   A=[4 -2;-2 4]   A\\[2;8]   fft([1 2 3 4])");
    puts("Language: who, clear [name], format short|long; abort drops an open block.");
    puts("Math: sin cos exp log sqrt, det inv eig svd, fzero integral deriv, fft ifft.");
    puts("Platform: :help :info :mem :reset :reboot");
#ifdef MS_WORKSTATION_LAUNCHER
    puts("Programs: :apps lists them; :app NAME [arguments] starts one.");
#ifdef MS_EXTERNAL_APPS
    puts("External apps: :upload SIZE CRC32 (host tool), :unload; RAM installs until reboot.");
#endif
#endif
    puts("Esc cancels input/blocks or stops calculations at engine polling points.");
#ifdef MS_WORKSTATION_LAUNCHER
#ifdef MS_EXTERNAL_APPS
    puts("399 printable characters/line. Files, AI, plot output and SD storage pending.");
#else
    puts("399 printable characters/line. Files, AI, plot output and SD binary loading pending.");
#endif
#else
    puts("399 printable characters/line. Files, AI, plot output and app launcher pending.");
#endif
}

static void evaluate(const char *line)
{
#ifdef MS_WORKSTATION_LAUNCHER
    if (app_console_dispatch(line, block_open)) return;
#endif
    if (!strcmp(line, ":help") || (!block_open && !strcmp(line, "help"))) help();
    else if (!strcmp(line, ":info")) platform_info();
    else if (!strcmp(line, ":mem")) memory_info();
    else if (!strcmp(line, ":reset")) reset_session();
    else if (!strcmp(line, ":reboot")) {
        puts("Rebooting...");
        uart_wait_tx_done(UART_NUM_0, pdMS_TO_TICKS(1000));
        esp_restart();
    } else if (*line == ':') puts("Unknown platform command; use :help.");
    else if (!session) puts("MatmMax unavailable; use :info or :reset.");
    else {
        cancelled = false;
        last_yield = esp_timer_get_time();
        int result = ml_eval(session, line);
        block_open = (result == ML_MORE);
        if (result == ML_QUIT) {
#ifdef MS_WORKSTATION_LAUNCHER
            puts("MatmMax is the workstation prompt; workspace kept. Use :app NAME for programs.");
#else
            puts("E01 has no parent shell yet; workspace kept. Use :reset or :reboot.");
#endif
        }
    }
}

void app_main(void)
{
    const uart_config_t config = {
        .baud_rate = 115200, .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE, .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE, .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_param_config(UART_NUM_0, &config));
    ESP_ERROR_CHECK(uart_driver_install(UART_NUM_0, 4096, 0, 0, NULL, 0));
    uart_vfs_dev_use_driver(UART_NUM_0);
    setvbuf(stdout, NULL, _IONBF, 0);
    mat_poll_hook = poll_cancel;
#ifdef MS_WORKSTATION_LAUNCHER
    app_console_init(read_key, reset_console_input);
#endif
    platform_info();
    reset_session();
    puts("Type help. The prompt runs the shared MatmMax engine.");
    printf(">> ");
    char line[400];
    size_t used = 0;
    bool overflow = false, previous_cr = false;
    for (;;) {
        uint8_t ch = read_key();
        if (queue_overflow) {
            head = tail = 0;
            queue_overflow = false;
            uart_flush_input(UART_NUM_0);
            used = 0;
            overflow = false;
            puts("\nInput queue full; queued input discarded.");
            printf(block_open ? "..> " : ">> ");
            continue;
        }
        if (ch == '\n' && previous_cr) {
            previous_cr = false;
            continue;
        }
        previous_cr = (ch == '\r');
        if (ch == '\r' || ch == '\n') {
            putchar('\n');
            if (overflow) puts("Line too long (399 bytes maximum); discarded.");
            else {
                line[used] = '\0';
                evaluate(line);
#ifdef MS_WORKSTATION_LAUNCHER
                if (reset_line_ending) {
                    previous_cr = false;
                    reset_line_ending = false;
                }
#endif
            }
            used = 0;
            overflow = false;
            printf(block_open ? "..> " : ">> ");
        } else if (ch == 0x1b || ch == 3) {
            used = 0;
            overflow = false;
            if (session && block_open) {
                ml_eval(session, "abort");
                block_open = false;
            }
            puts("\nCancelled.");
            printf(">> ");
        } else if ((ch == '\b' || ch == 127) && !overflow) {
            if (used) { --used; printf("\b \b"); }
        } else if (ch >= 32 && ch <= 126 && !overflow) {
            if (used < sizeof(line) - 1) {
                line[used++] = (char)ch;
                putchar(ch);
            } else overflow = true;
        }
    }
}
