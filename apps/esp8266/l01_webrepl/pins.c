/* One policy/state registry shared by prompt commands and MatmMax builtins. */
#include <string.h>
#include "web.h"
#include "pins.h"
static const unsigned char gpio[9]={16,5,4,0,2,14,12,13,15};
static signed char modes[9];
static int initialised;
static int valid(int pin) { return pin>=0 && pin<9; }
static void initialise(void)
{ if (!initialised) { memset(modes,-1,sizeof(modes)); initialised=1; } }
int ms_pin_index(const char *label)
{ return label && label[0]=='D' && label[1]>='0' && label[1]<='8' && !label[2] ? label[1]-'0' : -1; }
int ms_pin_gpio(int pin) { return valid(pin) ? gpio[pin] : -1; }
int ms_pin_configure(int pin,int mode,int initial)
{
    if (!valid(pin)) return MS_PIN_LABEL;
    if (mode<0 || mode>2) return MS_PIN_MODE;
    if (initial!=0 && initial!=1) return MS_PIN_LEVEL;
    if (pin==0 && mode==2) return MS_PIN_PULLUP;
    initialise(); ms_gpio_mode(gpio[pin],mode,initial); modes[pin]=(signed char)mode;
    return MS_PIN_OK;
}
int ms_pin_write(int pin,int value)
{
    if (!valid(pin)) return MS_PIN_LABEL;
    if (value!=0 && value!=1) return MS_PIN_LEVEL;
    initialise(); if (modes[pin]!=1) return MS_PIN_OUTPUT;
    ms_gpio_write(gpio[pin],value); return MS_PIN_OK;
}
int ms_pin_read(int pin,int *value)
{
    if (!valid(pin)) return MS_PIN_LABEL;
    *value=ms_gpio_read(gpio[pin]); return MS_PIN_OK;
}
int ms_pin_analog(const char *label,int *value)
{
    if (!label || strcmp(label,"A0")) return MS_PIN_ADC;
    *value=ms_adc_read(); return MS_PIN_OK;
}
const char *ms_pin_error(int error)
{
    switch (error) {
    case MS_PIN_LABEL:return "use D0..D8; UART/flash pins are reserved";
    case MS_PIN_MODE:return "pin mode must be input, output or pullup";
    case MS_PIN_LEVEL:return "digital level must be 0 or 1";
    case MS_PIN_PULLUP:return "D0 has no pullup";
    case MS_PIN_OUTPUT:return "configure this pin as output first";
    case MS_PIN_ADC:return "analog input must be A0";
    case MS_PIN_INTERRUPTED:return "interrupted";
    default:return "pin operation failed";
    }
}
