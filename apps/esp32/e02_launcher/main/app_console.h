#ifndef MS_S3_APP_CONSOLE_H
#define MS_S3_APP_CONSOLE_H
#include <stdbool.h>
#include <stdint.h>
void app_console_init(uint8_t (*read)(void), void (*reset)(void));
bool app_console_dispatch(const char *line, bool block_open);
#endif
