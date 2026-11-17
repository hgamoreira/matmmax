#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <cont.h>
#include <coredecls.h>
#include "port.h"
static_assert(sizeof(double)==8,"MatmMax requires 64-bit doubles");
static unsigned char queued[256];
static unsigned head, tail;
static bool lost;
extern "C" int ms_stack_ok(void)
{
    uintptr_t sp;
    __asm__ volatile("mov %0, a1" : "=r"(sp));
    return sp >= (uintptr_t)g_pcont->stack+MS_STACK_RESERVE;
}
extern "C" void ms_write(const char *text) { Serial.print(text); optimistic_yield(1000); }
extern "C" clock_t ms_clock(void) { return (clock_t)((uint64_t)micros()*CLOCKS_PER_SEC/1000000); }
extern "C" int ms_poll(void)
{
    bool stop=false;
    while (Serial.available()) {
        int c=Serial.read();
        if (c==27 || c==3) stop=true;
        else {
            unsigned next=(head+1)%sizeof(queued);
            if (next==tail) { lost=true; stop=true; }
            else { queued[head]=(unsigned char)c; head=next; }
        }
    }
    optimistic_yield(1000);
    if (stop) { head=tail=0; }
    return stop;
}
extern "C" void ms_diagnostics(void)
{
    Serial.printf("Core: %s; CPU: %u MHz; flash real: %u bytes; Wi-Fi mode: %u (0=off)\n",
        ESP.getCoreVersion().c_str(),ESP.getCpuFreqMHz(),ESP.getFlashChipRealSize(),(unsigned)WiFi.getMode());
    Serial.printf("Heap free: %u; largest block: %u; fragmentation: %u%%\n",
        ESP.getFreeHeap(),ESP.getMaxFreeBlockSize(),ESP.getHeapFragmentation());
    Serial.printf("Engine payload: used=%u peak=%u limit=%u bytes\n",
        (unsigned)ms_used(),(unsigned)ms_peak(),(unsigned)ms_limit());
    Serial.printf("Stack: allocated=%u minimum free=%u bytes\n",(unsigned)CONT_STACKSIZE,(unsigned)cont_get_free_stack(g_pcont));
}
extern "C" void ms_restart(void) { Serial.println("Rebooting..."); Serial.flush(); ESP.restart(); }
void setup()
{
    // Force the core's dedicated BSS continuation stack. Its default borrows
    // only about 4 KiB from SYS and cannot safely hold our larger stack.
    disable_extra4k_at_link_time();
    Serial.begin(115200);
    Serial.setDebugOutput(false);
    delay(100);
    Serial.println(F("\nL00 boot: UART ready (115200)"));
    Serial.flush();
    WiFi.persistent(false);
    WiFi.mode(WIFI_OFF);
    Serial.println(F("L00 boot: starting MatmMax"));
    Serial.flush();
    ms_console_start();
}
void loop()
{
    if (lost) {
        lost=false;
        Serial.println("\nTypeahead overflow; calculation cancelled, queued input discarded.");
        ms_console_key(27);
    }
    while (head!=tail) { int c=queued[tail]; tail=(tail+1)%sizeof(queued); ms_console_key(c); }
    if (Serial.available()) ms_console_key(Serial.read());
    delay(0);
}
