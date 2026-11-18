#ifndef MS_WEB_H
#define MS_WEB_H
#define MS_WEB_BUILD "L02-r2"
#include <stddef.h>
#include <stdint.h>
#include "port.h"
#ifdef __cplusplus
extern "C" {
#endif
int ms_platform_command(const char *);
int ms_console_pending(void);
void *ms_console_session(void);
int ms_console_result(void);
int ms_console_wifi_setup(void);
int ms_console_setup_active(void);
void ms_gpio_mode(int gpio, int mode, int initial);
int ms_gpio_read(int gpio);
void ms_gpio_write(int gpio, int value);
int ms_adc_read(void);
int ms_wifi_command(const char *);
void ms_wifi_diagnostics(const unsigned char *associated_bssid,unsigned associated_channel);
typedef struct { unsigned before_state, before_pcb, state, pcb, tries; } MsDhcpStart;
/* Returns1 when no recovery is needed; otherwise the actual lwIP err_t. */
int ms_wifi_start_stopped_dhcp(MsDhcpStart *result);
void ms_wifi_join_network(const char *, const char *);
/* Protocol parser has no allocation or hardware dependency. */
typedef struct {
    unsigned stage, opcode, length, count;
    unsigned char mask[4], data[193];
} MsFrame;
int ms_frame_byte(MsFrame *, unsigned char);
int ms_http_request(char *, const char *host, const char *token, char accept[29]);
void ms_web_accept(const char key[25], char accept[29]);
#ifdef __cplusplus
}
#endif
#endif
