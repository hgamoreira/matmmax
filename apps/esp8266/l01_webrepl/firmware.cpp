/* Single-owner console, fixed transport buffers and cooperative network polling. */
#ifdef MS_WEB_NATIVE
#include "native_hal.h"
#else
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <cont.h>
#include <coredecls.h>
extern "C" {
#include <user_interface.h>
}
#endif
#include "web.h"
#include "pins.h"
#include "settings.h"
#include "dhcp_trace.h"
#include "web_page.h"
static_assert(sizeof(double)==8,"MatmMax requires doubles");
static WiFiServer server(80);
static WiFiClient client;
static char token[33], request[1536], queued[192];
static unsigned request_len, reply_len, reply_pos, page_pos;
static unsigned char output[1024];
static unsigned out_head, out_tail;
static MsFrame frame;
static unsigned long accepted_at;
static unsigned long last_output;
static bool listening, websocket, upgrading, sending_page, evaluating, remote_command;
static bool queued_command, stop_requested, reset_input, takeover, servicing;
static unsigned ready_message;
static unsigned ready_result;
static bool command_cancelled;
static unsigned serial_escape;
static unsigned long serial_escape_at;
static int serial_held=-1;
static WiFiEventHandler wifi_disconnect_event;
static WiFiEventHandler wifi_associated_event, wifi_ip_event, wifi_dhcp_event;
static unsigned last_wifi_reason, wifi_disconnect_count;
static unsigned wifi_associated_count, wifi_ip_count, wifi_dhcp_count;
static bool wifi_associated, wifi_has_ip, wifi_link_down;
static unsigned char wifi_ap_bssid[6];
static unsigned wifi_ap_channel;
static bool wifi_ap_known;
static unsigned long wifi_attempt_at;
static int wifi_begin_status=-1;
static bool wifi_attempted;
static unsigned long wifi_associated_at;
static bool wifi_dhcp_retried;
static unsigned long wifi_dhcp_checked_at;
static bool settings_saved, settings_pending, mdns_active;
static bool pending_network_matched;
static MsSettings pending_settings;

static void new_access_key()
{
    for (unsigned i=0;i<4;++i) snprintf(token+i*8,9,"%08x",(unsigned)os_random());
}
static void clear_pending_settings()
{
    memset(&pending_settings,0,sizeof(pending_settings)); settings_pending=false; pending_network_matched=false;
}

/* The pinned SDK can leave its status at CONNECTING after a GotIP event.
 * Require an actual address and either that event or the normal SDK status;
 * a recorded disconnect always invalidates readiness, even with a stale IP. */
static bool network_ready()
{
    auto address=WiFi.localIP();
    uint32_t ip=(uint32_t)address;
    return (WiFi.getMode()&WIFI_STA) && ip && ip!=0xffffffffU &&
           !(address[0]==169 && address[1]==254) && !wifi_link_down &&
           (wifi_has_ip || WiFi.status()==WL_CONNECTED);
}

/* Recover once per explicit join/boot, after association has had time to
 * complete DHCP. Only start a stopped client; never interrupt active DHCP. */
static void service_dhcp_recovery()
{
    uint32_t ip=(uint32_t)WiFi.localIP();
    if (!wifi_attempted || wifi_dhcp_retried || !wifi_associated ||
        !(WiFi.getMode()&WIFI_STA) || (ip && ip!=0xffffffffU) ||
        millis()-wifi_associated_at<15000 || millis()-wifi_dhcp_checked_at<1000) return;
    wifi_dhcp_checked_at=millis();
    MsDhcpStart state;
    int error=ms_wifi_start_stopped_dhcp(&state);
    if (error==1) return;
    wifi_dhcp_retried=true;
    char text[160];
    snprintf(text,sizeof(text),"Stopped lwIP DHCP client; start once: result=%d; before=%u/pcb%u; after=%u/pcb%u; tries=%u\n",
        error,state.before_state,state.before_pcb,state.state,state.pcb,state.tries);
    ms_write(text);
}

static const char *wifi_state_name(int state)
{
    switch (state) {
    case 0: return "idle";
    case 1: return "connecting";
    case 2: return "wrong-password";
    case 3: return "no-ap-found";
    case 4: return "connect-failed";
    case 5: return "got-ip";
    default: return "unknown";
    }
}
static const char *wifi_reason_name(unsigned reason)
{
    switch (reason) {
    case 0: return "none";
    case 2: return "auth-expired";
    case 8: return "association-leave";
    case 15: return "4-way-handshake-timeout";
    case 200: return "beacon-timeout";
    case 201: return "no-ap-found";
    case 202: return "auth-failed";
    case 203: return "association-failed";
    case 204: return "handshake-timeout";
    default: return "other-sdk-reason";
    }
}
/* Consume terminal CSI/SS3 keys without treating their ESC prefix as Stop.
 * A standalone Esc becomes cancellation after 75 ms; Ctrl+C stays immediate.
 * This console edits at the end of a line, so arrows/Home/Delete are ignored. */
static int serial_key_event()
{
    if (serial_escape && millis()-serial_escape_at>=75) {
        unsigned previous=serial_escape; serial_escape=0;
        if (previous==1) return 27;
    }
    int c;
    if (serial_held>=0) { c=serial_held; serial_held=-1; }
    else { if (!Serial.available()) return -1; c=Serial.read(); }
    if (c<0) return -1;
    if (c==3) { serial_escape=0; return c; }
    if (serial_escape==1) {
        serial_escape_at=millis();
        if (c=='[' || c=='O') { serial_escape=2; return -1; }
        serial_escape=0; serial_held=c; return 27;
    }
    if (serial_escape==2) {
        serial_escape_at=millis();
        if (c==27) { serial_escape=1; return -1; }
        if (c<32) { serial_escape=0; return c; }
        if (c>=0x40 && c<=0x7e) serial_escape=0;
        return -1;
    }
    if (c==27) { serial_escape=1; serial_escape_at=millis(); return -1; }
    return c;
}

static void service();
static void disconnect_client(bool cancel)
{
    bool was_web=websocket;
    client.abort(); websocket=upgrading=sending_page=false;
    request_len=reply_len=reply_pos=page_pos=out_head=out_tail=ready_message=0;
    memset(&frame,0,sizeof(frame));
    queued_command=false;
    if (was_web) { reset_input=true; if (cancel) stop_requested=true; }
}
static bool send_frame(unsigned opcode,const unsigned char *data,unsigned n)
{
    unsigned char packet[132];
    if (n>128 || !client.connected()) return false;
    unsigned h=n<126 ? 2 : 4;
    if (client.availableForWrite()<(int)(n+h)) return false;
    packet[0]=0x80|opcode; packet[1]=n<126 ? n : 126;
    if (h==4) { packet[2]=0; packet[3]=n; }
    memcpy(packet+h,data,n);
    if (client.write(packet,n+h)!=n+h) { disconnect_client(true); return false; }
    return true;
}
static void close_frame(unsigned code)
{
    unsigned char data[2]={(unsigned char)(code>>8),(unsigned char)code};
    send_frame(8,data,2); disconnect_client(true);
}
static void flush_output()
{
    if (!websocket) return;
    unsigned char data[128]; unsigned n=1, tail=out_tail;
    data[0]='O';
    while (tail!=out_head && n<sizeof(data)) { data[n++]=output[tail]; tail=(tail+1)%sizeof(output); }
    if (n>1 && (n==sizeof(data) || ready_message || millis()-last_output>=8) && send_frame(2,data,n)) {
        out_tail=tail; last_output=millis();
    }
    if (out_head==out_tail && ready_message) {
        data[0]='R'; data[1]=ready_message==2 ? '1' : '0';
        data[2]=(unsigned char)('0'+ready_result);
        if (send_frame(2,data,3)) ready_message=0;
    }
}
static bool valid_command(const unsigned char *s,unsigned n)
{
    for (unsigned i=0;i<n;++i) if (s[i]<32 || s[i]>126) return false;
    return true;
}
static void received_frame()
{
    if (frame.opcode==8) { close_frame(1000); return; }
    if (frame.opcode==9) {
        if (!send_frame(10,frame.data,frame.length)) disconnect_client(true);
        return;
    }
    if (frame.opcode==10) return;
    if (frame.length==1 && frame.data[0]=='S') {
        stop_requested=true; queued_command=false; return;
    }
    if (!frame.length || frame.data[0]!='C' || !valid_command(frame.data+1,frame.length-1) ||
        queued_command || evaluating) { close_frame(1008); return; }
    memcpy(queued,frame.data+1,frame.length-1); queued[frame.length-1]=0;
    queued_command=true;
}
static void prepare_reply(int status,const char *accept)
{
    reply_pos=page_pos=0;
    if (status==101) {
        reply_len=snprintf(request,sizeof(request),"HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: %s\r\nSec-WebSocket-Protocol: mathsmith.%s\r\n\r\n",accept,token);
        upgrading=true;
    } else if (status==200) {
        reply_len=snprintf(request,sizeof(request),"HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: %u\r\nConnection: close\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\nContent-Security-Policy: default-src 'none'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; connect-src 'self'; frame-ancestors 'none'; base-uri 'none'\r\n\r\n",(unsigned)(sizeof(web_page)-1));
        sending_page=true;
    } else {
        reply_len=snprintf(request,sizeof(request),"HTTP/1.1 %d Rejected\r\nContent-Length: 0\r\nConnection: close\r\n\r\n",status);
    }
}
static void serve_reply()
{
    unsigned char buffer[128]; unsigned n;
    if (reply_pos<reply_len) {
        n=reply_len-reply_pos; if (n>sizeof(buffer)) n=sizeof(buffer);
        if (client.availableForWrite()<(int)n) return;
        if (client.write((const unsigned char *)request+reply_pos,n)!=n) { disconnect_client(false); return; }
        reply_pos+=n; return;
    }
    if (upgrading) {
        upgrading=false; websocket=true; takeover=true;
        memset(&frame,0,sizeof(frame));
        Serial.println("\nBrowser owns console. Serial Ctrl+C reclaims it.");
        return;
    }
    if (sending_page && page_pos<sizeof(web_page)-1) {
        n=sizeof(web_page)-1-page_pos; if (n>sizeof(buffer)) n=sizeof(buffer);
        if (client.availableForWrite()<(int)n) return;
        memcpy_P(buffer,web_page+page_pos,n);
        if (client.write(buffer,n)!=n) { disconnect_client(false); return; }
        page_pos+=n; return;
    }
    client.stop(); disconnect_client(false);
}
static void service()
{
    if (servicing || !listening) return;
    servicing=true;
    if (mdns_active) MDNS.update();
    if (client && !client.connected()) disconnect_client(true);
    if (!client && server.hasClient()) {
        client=server.accept(); client.setNoDelay(true); client.setTimeout(20);
        accepted_at=millis(); request_len=reply_len=reply_pos=page_pos=0;
    } else if (client && server.hasClient()) { WiFiClient extra=server.accept(); extra.abort(); }
    if (client) {
        if (!websocket && millis()-accepted_at>5000) disconnect_client(false);
        else if (!websocket && reply_len) serve_reply();
        else {
            unsigned budget=256;
            while (client && client.available() && budget--) {
                int c=client.read(); if (c<0) break;
                if (websocket) {
                    int result=ms_frame_byte(&frame,(unsigned char)c);
                    if (result<0) { close_frame(1002); break; }
                    if (result) received_frame();
                } else {
                    if (!c || request_len+1>=sizeof(request)) { prepare_reply(400,""); break; }
                    request[request_len++]=(char)c; request[request_len]=0;
                    if (request_len>=4 && !strcmp(request+request_len-4,"\r\n\r\n")) {
                        char accept[29],host[48];
#ifdef MS_WEB_NATIVE
                        snprintf(host,sizeof(host),"127.0.0.1:%u",server.port());
#else
                        snprintf(host,sizeof(host),"%s",WiFi.localIP().toString().c_str());
#endif
                        int status=ms_http_request(request,host,token,accept);
                        if (status==101 && (evaluating || ms_console_setup_active())) status=409;
                        prepare_reply(status,accept); break;
                    }
                }
            }
            flush_output();
        }
    }
    servicing=false;
}
extern "C" int ms_stack_ok(void)
{
#ifdef MS_WEB_NATIVE
    return true;
#else
    uintptr_t sp; __asm__ volatile("mov %0, a1" : "=r"(sp));
    return sp >= (uintptr_t)g_pcont->stack+MS_STACK_RESERVE;
#endif
}
extern "C" void ms_write(const char *text)
{
    Serial.print(text);
    unsigned batch=0;
    while (*text && websocket) {
        if (!batch) service();
        if (!websocket) break;
        unsigned next=(out_head+1)%sizeof(output);
        if (next==out_tail) { disconnect_client(true); break; }
        output[out_head]=(unsigned char)*text++; out_head=next;
        batch=(batch+1)%127;
    }
    service(); optimistic_yield(1000);
}
extern "C" clock_t ms_clock(void) { return (clock_t)((uint64_t)micros()*CLOCKS_PER_SEC/1000000); }
extern "C" int ms_poll(void)
{
    service();
    /* During evaluation only cancellation is accepted; no nested evaluation. */
    for (unsigned n=0;n<64;++n) {
        int c=serial_key_event();
        if (c==3 || c==27) { if (websocket) disconnect_client(true); stop_requested=true; }
        if (c<0 && !Serial.available() && serial_held<0) break;
    }
    optimistic_yield(1000);
    bool stop=stop_requested; stop_requested=false;
    if (stop) command_cancelled=true;
    return stop;
}
extern "C" int ms_pin_delay(unsigned long milliseconds)
{
    unsigned long started=millis();
    for (;;) {
        if (ms_poll()) return MS_PIN_INTERRUPTED;
        unsigned long elapsed=millis()-started;
        if (elapsed>=milliseconds) return MS_PIN_OK;
        unsigned long remaining=milliseconds-elapsed;
        unsigned pause=(unsigned)(remaining>5 ? 5 : remaining);
#ifdef MS_WEB_NATIVE
        usleep(pause*1000);
#else
        delay(pause);
#endif
    }
}
extern "C" void ms_diagnostics(void)
{
    ms_write("Build: " MS_WEB_BUILD "\n");
#ifdef MS_WEB_NATIVE
    ms_write("Native transport harness; no ESP8266 heap, stack or GPIO measurements.\n");
#else
    char text[256];
    snprintf(text,sizeof(text),"Core: %s; CPU: %u MHz; flash real: %u bytes; Wi-Fi mode: %u\nHeap free: %u; largest block: %u; fragmentation: %u%%\n",ESP.getCoreVersion().c_str(),ESP.getCpuFreqMHz(),ESP.getFlashChipRealSize(),(unsigned)WiFi.getMode(),ESP.getFreeHeap(),ESP.getMaxFreeBlockSize(),ESP.getHeapFragmentation());
    ms_write(text);
    snprintf(text,sizeof(text),"Stack: allocated=%u minimum free=%u bytes\n",(unsigned)CONT_STACKSIZE,(unsigned)cont_get_free_stack(g_pcont)); ms_write(text);
#endif
    char text2[128];
    snprintf(text2,sizeof(text2),"Engine payload: used=%u peak=%u limit=%u bytes\n",(unsigned)ms_used(),(unsigned)ms_peak(),(unsigned)ms_limit()); ms_write(text2);
}
extern "C" void ms_restart(void) { Serial.flush(); ESP.restart(); }
extern "C" void ms_gpio_mode(int gpio,int mode,int initial)
{
    if (mode==1) digitalWrite(gpio,initial);
    pinMode(gpio,mode==1 ? OUTPUT : mode==2 ? INPUT_PULLUP : INPUT);
}
extern "C" int ms_gpio_read(int gpio) { return digitalRead(gpio); }
extern "C" void ms_gpio_write(int gpio,int value) { digitalWrite(gpio,value); }
extern "C" int ms_adc_read(void) { return analogRead(A0); }

static bool argument(const char *&p,char *dest,size_t capacity)
{
    while (*p==' ') ++p;
    if (!*p) return false;
    char quote=(*p=='\'' || *p=='"') ? *p++ : 0;
    size_t n=0;
    while (*p && (quote ? *p!=quote : *p!=' ')) {
        char c=*p++;
        if (c=='\\' && quote && (*p==quote || *p=='\\')) c=*p++;
        if (n+1>=capacity) return false;
        dest[n++]=c;
    }
    if (quote) { if (*p!=quote) return false; ++p; }
    dest[n]=0; return true;
}
extern "C" int ms_wifi_command(const char *line)
{
    if (!strcmp(line,":wifi diag")) {
        ms_wifi_diagnostics(wifi_associated && wifi_ap_known ? wifi_ap_bssid : nullptr,wifi_ap_channel);
        ms_dhcp_trace_diagnostics();
        return 1;
    }
    if (!strcmp(line,":wifi") || !strcmp(line,":web")) {
        char text[256];
        int sdk=(int)wifi_station_get_connect_status();
        snprintf(text,sizeof(text),MS_WEB_BUILD ": Wi-Fi status=%d; browser=%s\nMode=%u; SDK=%d (%s); begin=%d; elapsed=%lu s\nLast disconnect=%u (%s); events=%u\n",(int)WiFi.status(),websocket ? "connected" : "disconnected",(unsigned)WiFi.getMode(),sdk,wifi_state_name(sdk),wifi_begin_status,wifi_attempted ? (millis()-wifi_attempt_at)/1000 : 0,last_wifi_reason,wifi_reason_name(last_wifi_reason),wifi_disconnect_count);
        ms_write(text);
        snprintf(text,sizeof(text),"Saved network=%s; save-pending=%s; discovery=%s\n",
            settings_saved ? "yes" : "no",settings_pending ? "yes" : "no",mdns_active ? "mathsmith.local" : "numeric IP only");
        ms_write(text);
        snprintf(text,sizeof(text),"IP=%s; associated=%u; associated-events=%u; got-ip-events=%u; dhcp-timeouts=%u; ready=%u\n",
            WiFi.localIP().toString().c_str(),wifi_associated ? 1U : 0U,wifi_associated_count,wifi_ip_count,wifi_dhcp_count,network_ready() ? 1U : 0U);
        ms_write(text);
        snprintf(text,sizeof(text),"DHCP=%u (0=stopped,1=started); recovery-attempted=%s\n",
            (unsigned)wifi_station_dhcpc_status(),wifi_dhcp_retried ? "yes" : "no");
        ms_write(text);
        if (sdk==1 && network_ready()) ms_write("SDK status is stale; an assigned IP is available.\n");
        else if (sdk==1 && millis()-wifi_attempt_at<30000) ms_write("Connection in progress; allow up to 30 s, then use :wifi again.\n");
        else if (sdk==1) ms_write("No usable IP after 30 s; capture :wifi and :mem. Further waiting alone may not help.\n");
        if (!wifi_attempted) ms_write("No join attempted. Use :wifi join via serial for guided setup.\n");
        if (network_ready()) {
#ifdef MS_WEB_NATIVE
            Serial.printf("Browser URL: http://127.0.0.1:%u/#%s\n",server.port(),token);
#else
            Serial.printf("Browser URL: http://%s/#%s\n",WiFi.localIP().toString().c_str(),token);
#endif
            if (mdns_active) Serial.printf("Named URL: http://mathsmith.local/#%s\n",token);
        }
        return 1;
    }
    if (remote_command) { ms_write("error: Wi-Fi setup is serial-only\n"); return 1; }
    if (!strcmp(line,":wifi join")) { ms_console_wifi_setup(); return 1; }
    if (!strcmp(line,":wifi forget")) {
        int result=ms_settings_forget();
        if (result==MS_STORE_IO) { ms_write("Could not forget settings; existing saved network may remain.\n"); return 1; }
        disconnect_client(true); server.stop(); listening=false;
        if (mdns_active) { MDNS.close(); mdns_active=false; }
        WiFi.mode(WIFI_OFF); wifi_has_ip=wifi_associated=false; wifi_link_down=true;
        clear_pending_settings(); settings_saved=false; new_access_key();
        ms_write(result==MS_STORE_CLEANUP ? "Auto-connect disabled; old flash cleanup failed. Retry :wifi forget.\n" :
                 "Saved Wi-Fi and browser key forgotten. Wi-Fi off; use :wifi join to configure again.\n");
        return 1;
    }
    if (!strcmp(line,":wifi off")) {
        disconnect_client(true); server.stop(); listening=false; WiFi.mode(WIFI_OFF);
        if (mdns_active) { MDNS.close(); mdns_active=false; }
        clear_pending_settings();
        wifi_has_ip=wifi_associated=false; wifi_link_down=true;
        ms_write("Wi-Fi off; serial console active.\n"); return 1;
    }
    if (!strncmp(line,":wifi join ",11)) {
        char ssid[33],password[64]; const char *p=line+11;
        bool ok=argument(p,ssid,sizeof(ssid)) && argument(p,password,sizeof(password));
        while (*p==' ') ++p;
        if (!ok || *p || !ssid[0]) { ms_write("error: :wifi join \"SSID\" \"PASSWORD\"\n"); return 1; }
        ms_wifi_join_network(ssid,password);
        memset(password,0,sizeof(password));
        return 1;
    }
    ms_write("error: :wifi, :wifi diag, :wifi join, :wifi off, :wifi forget, :web\n"); return 1;
}
extern "C" void ms_wifi_join_network(const char *ssid,const char *password)
{
    disconnect_client(true); server.stop(); listening=false;
    if (mdns_active) { MDNS.close(); mdns_active=false; }
    clear_pending_settings();
    strncpy(pending_settings.ssid,ssid,sizeof(pending_settings.ssid)-1);
    strncpy(pending_settings.password,password,sizeof(pending_settings.password)-1);
    memcpy(pending_settings.key,token,sizeof(token)); settings_pending=true;
    wifi_attempted=true; wifi_attempt_at=millis(); wifi_begin_status=-1;
    last_wifi_reason=wifi_disconnect_count=0;
    wifi_associated_count=wifi_ip_count=wifi_dhcp_count=0;
    wifi_ap_known=false;
    wifi_dhcp_retried=false;
    wifi_dhcp_checked_at=millis();
    wifi_has_ip=wifi_associated=wifi_link_down=false;
    WiFi.persistent(false);
    /* Cold start follows the working R4 mode(STA)/begin sequence. For a new
     * attempt on an existing station, reset the interface before enabling it;
     * do not disconnect a newly enabled station immediately before begin. */
    if (WiFi.getMode()&WIFI_STA) {
        if (!WiFi.mode(WIFI_OFF)) {
            clear_pending_settings(); ms_write("Wi-Fi interface reset failed; use :wifi and :mem.\n"); return;
        }
        delay(1);
    }
    ms_dhcp_trace_start();
    if (!WiFi.mode(WIFI_STA)) {
        clear_pending_settings();
        ms_write("Wi-Fi station mode failed to start; use :wifi and :mem for diagnostics.\n");
        return;
    }
    wifi_has_ip=wifi_associated=wifi_link_down=false;
    pending_network_matched=false;
    last_wifi_reason=wifi_disconnect_count=0;
    wifi_begin_status=(int)WiFi.begin(ssid,password);
    ms_write("Join requested; allow up to 30 s. Settings will be saved after a usable IP is assigned.\n");
}
void setup()
{
    disable_extra4k_at_link_time(); Serial.begin(115200); Serial.setDebugOutput(false);
    WiFi.persistent(false); WiFi.mode(WIFI_OFF);
    wifi_disconnect_event=WiFi.onStationModeDisconnected([](const WiFiEventStationModeDisconnected& e) {
        last_wifi_reason=(unsigned)e.reason; ++wifi_disconnect_count;
        wifi_has_ip=wifi_associated=false; wifi_link_down=true;
        pending_network_matched=false;
        wifi_ap_known=false;
    });
    wifi_associated_event=WiFi.onStationModeConnected([](const WiFiEventStationModeConnected& e) {
        wifi_associated=true; wifi_link_down=false; ++wifi_associated_count;
        memcpy(wifi_ap_bssid,e.bssid,6); wifi_ap_channel=e.channel; wifi_ap_known=true;
        wifi_associated_at=millis();
        if (settings_pending) pending_network_matched=!strcmp(e.ssid.c_str(),pending_settings.ssid);
    });
    wifi_ip_event=WiFi.onStationModeGotIP([](const WiFiEventStationModeGotIP&) {
        wifi_has_ip=wifi_associated=true; wifi_link_down=false; ++wifi_ip_count;
    });
    wifi_dhcp_event=WiFi.onStationModeDHCPTimeout([]() {
        wifi_has_ip=false; wifi_link_down=true; ++wifi_dhcp_count;
    });
    MsSettings stored;
    int loaded=ms_settings_load(&stored); settings_saved=loaded==MS_STORE_OK;
    if (settings_saved) memcpy(token,stored.key,sizeof(token)); else new_access_key();
    Serial.printf("\nL01 access key: %s\n",token);
    ms_console_start();
    if (settings_saved) {
        ms_write("Saved Wi-Fi found; reconnecting automatically. Browser key retained.\n");
        ms_wifi_join_network(stored.ssid,stored.password); clear_pending_settings();
    } else ms_write(loaded==MS_STORE_IO ? "Settings could not be read. Serial :wifi join remains available.\n" :
                   "No saved network. Serial :wifi join sets it up once.\n");
    memset(&stored,0,sizeof(stored));
}
void loop()
{
    service_dhcp_recovery();
    if (settings_pending && pending_network_matched && wifi_ip_count && network_ready()) {
        int result=ms_settings_save(&pending_settings);
        clear_pending_settings();
        if (result>0) { settings_saved=true; ms_write("Wi-Fi and browser key saved. Future boots reconnect automatically.\n"); }
        else ms_write("Settings could not be saved; retry :wifi join before relying on automatic boot.\n");
    }
    if (!listening && network_ready()) {
        if (!WiFi.setAutoReconnect(true)) ms_write("SDK automatic retry could not be enabled.\n");
        server.begin(80,1); server.setNoDelay(true); listening=true;
        mdns_active=MDNS.begin("mathsmith");
        if (mdns_active) MDNS.addService("http","tcp",80);
        ms_wifi_command(":web");
    }
    if (listening && !network_ready()) {
        disconnect_client(true); server.stop(); listening=false;
        if (mdns_active) { MDNS.close(); mdns_active=false; }
    }
    service();
    if (takeover) { takeover=false; reset_input=true; }
    if (reset_input) { reset_input=false; ms_console_key(27); ready_result=2; ready_message=websocket ? 1 : 0; }
    if (stop_requested) {
        stop_requested=false; ms_console_key(27); ready_result=2; ready_message=websocket ? 1 : 0;
    }
    if (queued_command && websocket) {
        queued_command=false; evaluating=remote_command=true;
        command_cancelled=false;
        for (unsigned i=0;queued[i];++i) ms_console_key((unsigned char)queued[i]);
        ms_console_key('\n');
        evaluating=remote_command=false;
        ready_result=command_cancelled ? 2 : (unsigned)ms_console_result();
        ready_message=websocket ? (ms_console_pending() ? 2 : 1) : 0;
    }
    int c=serial_key_event();
    if (c>=0) {
        if (websocket) {
            if (c==3 || c==27) { disconnect_client(true); ms_console_key(27); stop_requested=reset_input=false; }
        } else {
            evaluating=true; ms_console_key(c); evaluating=false;
        }
    }
    service(); delay(0);
}
