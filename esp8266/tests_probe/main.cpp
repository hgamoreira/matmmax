#include "native_hal.h"
void setup();
void loop();
int main()
{
    setup(); unsigned eof=0;
    for (;;) { loop(); if (Serial.eof && ++eof>50) break; usleep(1000); }
}
