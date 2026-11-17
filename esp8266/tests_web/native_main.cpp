#include "native_hal.h"
#include "web.h"
void setup();
void loop();
int main()
{
    setup();
    const char *auto_wifi=getenv("MS_NATIVE_WIFI");
    if (auto_wifi && !strcmp(auto_wifi,"1")) ms_wifi_command(":wifi join \"native\" \"\"");
    unsigned eof_ticks=0;
    for (;;) {
        loop();
        if (Serial.eof && ++eof_ticks>50) break;
        usleep(1000);
    }
}
