#ifndef MS_PINS_H
#define MS_PINS_H
#ifdef __cplusplus
extern "C" {
#endif
enum { MS_PIN_OK,MS_PIN_LABEL,MS_PIN_MODE,MS_PIN_LEVEL,MS_PIN_PULLUP,
       MS_PIN_OUTPUT,MS_PIN_ADC,MS_PIN_INTERRUPTED };
int ms_pin_index(const char *);
int ms_pin_gpio(int);
int ms_pin_configure(int,int,int);
int ms_pin_write(int,int);
int ms_pin_read(int,int *);
int ms_pin_analog(const char *,int *);
int ms_pin_delay(unsigned long);
const char *ms_pin_error(int);
#ifdef __cplusplus
}
#endif
#endif
