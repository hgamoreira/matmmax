/* Independent radio/SDK comparison: no MathSmith, WebSocket, GPIO or file service. */
#ifdef MS_WEB_NATIVE
#include "native_hal.h"
#else
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <cont.h>
#include <coredecls.h>
extern "C" {
#include <user_interface.h>
}
#endif
static WiFiEventHandler disconnected;
static unsigned reason, events, step, length, escape;
static unsigned long escape_at, joined_at;
static int held=-1, begin_status=-1;
static bool overflow, after_cr, attempted;
static char line[96], ssid[33];
#ifndef MS_WEB_NATIVE
static_assert(CONT_STACKSIZE==4096,"Wi-Fi comparison uses a dedicated 4 KiB stack");
#endif
static const char *state_name(int s)
{
    switch(s) {
    case 0:return "idle"; case 1:return "connecting"; case 2:return "wrong-password";
    case 3:return "no-ap-found"; case 4:return "connect-failed"; case 5:return "got-ip";
    default:return "unknown";
    }
}
static void prompt()
{
    Serial.print(step==1 ? "SSID (visible): " : step==2 ? "Password (hidden; Enter for open network): " : "probe> ");
}
static void diagnostics()
{
    int raw=wifi_station_get_connect_status();
    Serial.printf("WIFI-PROBE-r1: mode=%u; status=%d; SDK=%d (%s); begin=%d; elapsed=%lu s\n",
        (unsigned)WiFi.getMode(),(int)WiFi.status(),raw,state_name(raw),begin_status,attempted ? (millis()-joined_at)/1000 : 0);
    Serial.printf("Last disconnect=%u; events=%u\n",reason,events);
#ifdef MS_WEB_NATIVE
    Serial.println("Native fixture: physical radio/heap/stack are not measured.");
#else
    Serial.printf("Heap free=%u; largest=%u; fragmentation=%u%%; stack minimum free=%u/%u\n",
        ESP.getFreeHeap(),ESP.getMaxFreeBlockSize(),ESP.getHeapFragmentation(),
        (unsigned)cont_get_free_stack(g_pcont),(unsigned)CONT_STACKSIZE);
#endif
    Serial.printf("IP address: %s (probe has no web server)\n",WiFi.localIP().toString().c_str());
}
static void clear_line() { memset(line,0,sizeof(line)); length=0; overflow=false; }
static void submit()
{
    if (overflow) Serial.println("Input too long; line discarded.");
    else if (step==1) {
        if (!length || length>32) Serial.println("SSID must contain 1..32 characters.");
        else { memcpy(ssid,line,length+1); step=2; }
    } else if (step==2) {
        if (length>63) Serial.println("Password must contain at most 63 characters.");
        else {
            reason=events=0; attempted=true; joined_at=millis(); begin_status=-1;
            WiFi.persistent(false);
            if (!WiFi.mode(WIFI_STA)) Serial.println("Station mode failed to start.");
            else {
                begin_status=(int)WiFi.begin(ssid,line);
                Serial.println("Join requested. Wait 30 s, then type :wifi. Credentials are RAM only.");
            }
            memset(ssid,0,sizeof(ssid)); step=0;
        }
    } else if (!strcmp(line,":wifi join") || !strcmp(line,"join")) step=1;
    else if (!strcmp(line,":wifi") || !strcmp(line,":mem") || !strcmp(line,":info")) diagnostics();
    else if (!strcmp(line,":wifi off")) { WiFi.mode(WIFI_OFF); Serial.println("Wi-Fi off."); }
    else if (!strcmp(line,":reboot")) { Serial.flush(); ESP.restart(); }
    else if (length) Serial.println("Commands: :wifi join, :wifi, :mem, :wifi off, :reboot");
    clear_line(); prompt();
}
static int serial_key()
{
    if (escape && millis()-escape_at>=75) { unsigned old=escape; escape=0; if (old==1) return 27; }
    int c;
    if (held>=0) { c=held; held=-1; }
    else { if (!Serial.available()) return -1; c=Serial.read(); }
    if (c==3) { escape=0; return c; }
    if (escape==1) {
        escape_at=millis();
        if (c=='[' || c=='O') { escape=2; return -1; }
        escape=0; held=c; return 27;
    }
    if (escape==2) {
        escape_at=millis();
        if (c==27) { escape=1; return -1; }
        if (c<32) { escape=0; return c; }
        if (c>=0x40 && c<=0x7e) escape=0;
        return -1;
    }
    if (c==27) { escape=1; escape_at=millis(); return -1; }
    return c;
}
void setup()
{
    disable_extra4k_at_link_time(); Serial.begin(115200); Serial.setDebugOutput(false);
    WiFi.persistent(false); WiFi.mode(WIFI_OFF);
    disconnected=WiFi.onStationModeDisconnected([](const WiFiEventStationModeDisconnected& e) {
        reason=(unsigned)e.reason; ++events;
    });
    Serial.println("\nWIFI-PROBE-r1: Wi-Fi only; MathSmith/browser not linked. Dedicated 4 KiB stack.");
    diagnostics(); Serial.println("Type :wifi join for separate SSID/password prompts."); prompt();
}
void loop()
{
    int c=serial_key();
    if (c>=0) {
        if (c=='\n' && after_cr) { after_cr=false; return; }
        after_cr=c=='\r';
        if (c==27 || c==3) {
            clear_line(); memset(ssid,0,sizeof(ssid)); step=0;
            Serial.println("\nCancelled."); prompt();
        } else if (c=='\r' || c=='\n') { Serial.println(""); submit(); }
        else if (c==8 || c==127) {
            if (length && !overflow) { line[--length]=0; Serial.print("\b \b"); }
        } else if (c>=32 && c<127 && !overflow) {
            if (length+1<sizeof(line)) {
                line[length++]=(char)c; line[length]=0;
                char echo[2]={(char)(step==2 ? '*' : c),0}; Serial.print(echo);
            } else overflow=true;
        }
    }
    delay(1);
}
