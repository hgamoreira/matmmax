/* Read-only snapshot of the station and pinned lwIP DHCP client. No yielding,
 * writes, reconnects or flash operations while collecting the snapshot. */
#ifdef MS_WEB_NATIVE
#include "native_hal.h"
#else
#include <Arduino.h>
#include <ESP8266WiFi.h>
extern "C" {
#include <lwip/netif.h>
#include <lwip/dhcp.h>
#include <user_interface.h>
}
#endif
#include <stdio.h>
#include <string.h>
#include "web.h"

struct Snapshot {
    char ssid[33], hostname[33], interface_name[4];
    unsigned char mac[6], bssid[6], local[4], mask[4], gateway[4], server[4], offered[4];
    unsigned channel, interface_found, up, link_up, dhcp_present, state, tries, pcb, ticks;
    int rssi, has_bssid;
    uint32_t xid;
};
#ifndef MS_WEB_NATIVE
static void copy_ip(unsigned char out[4],const ip4_addr_t *ip)
{
    out[0]=ip4_addr1(ip); out[1]=ip4_addr2(ip);
    out[2]=ip4_addr3(ip); out[3]=ip4_addr4(ip);
}
static void snapshot(Snapshot& s)
{
    WiFi.macAddress(s.mac);
    station_config conf{};
    if (wifi_station_get_config(&conf)) memcpy(s.ssid,conf.ssid,32);
    memset(&conf,0,sizeof(conf));
    const char *hostname=WiFi.getHostname();
    if (hostname) strncpy(s.hostname,hostname,sizeof(s.hostname)-1);
    s.channel=WiFi.channel(); s.rssi=WiFi.RSSI();
    unsigned seen=0;
    for (netif *n=netif_list;n && seen++<8;n=n->next) {
        if (n->hwaddr_len!=6 || memcmp(n->hwaddr,s.mac,6)) continue;
        s.interface_found=1; s.interface_name[0]=n->name[0]; s.interface_name[1]=n->name[1];
        s.interface_name[2]=(n->num<10 ? '0'+n->num : '?');
        s.up=netif_is_up(n); s.link_up=netif_is_link_up(n);
        copy_ip(s.local,netif_ip4_addr(n)); copy_ip(s.mask,netif_ip4_netmask(n));
        copy_ip(s.gateway,netif_ip4_gw(n));
        const dhcp *d=netif_dhcp_data(n);
        if (d) {
            s.dhcp_present=1; s.state=d->state; s.tries=d->tries; s.pcb=d->pcb_allocated;
            s.xid=d->xid; s.ticks=d->request_timeout;
            copy_ip(s.server,ip_2_ip4(&d->server_ip_addr)); copy_ip(s.offered,&d->offered_ip_addr);
        }
        break;
    }
}
#else
static unsigned fixture(const char *name,unsigned fallback)
{ const char *value=getenv(name); return value ? (unsigned)strtoul(value,nullptr,0) : fallback; }
static void snapshot(Snapshot& s)
{
    /* Native fixture values test formatting/ownership only, not lwIP/radio. */
    const unsigned char mac[6]={0x34,0x5f,0x45,0x40,0x57,0x3a};
    memcpy(s.mac,mac,6);
    const char *ssid=getenv("MS_TEST_DIAG_SSID");
    strncpy(s.ssid,ssid ? ssid : "native",32); strcpy(s.hostname,"esp-40573A");
    s.channel=6; s.rssi=-56; strcpy(s.interface_name,"st0");
    s.interface_found=!getenv("MS_TEST_DIAG_NO_INTERFACE");
    s.up=fixture("MS_TEST_DIAG_UP",1); s.link_up=fixture("MS_TEST_DIAG_LINK_UP",1);
    s.dhcp_present=!getenv("MS_TEST_DIAG_NO_CLIENT");
    s.state=WiFi.lwip_state; s.pcb=WiFi.lwip_pcb; s.tries=WiFi.lwip_tries;
    s.ticks=8; s.xid=0x40573a10;
    if (WiFi.status()==WL_CONNECTED) {
        s.local[0]=s.offered[0]=s.server[0]=s.gateway[0]=127;
        s.local[3]=s.offered[3]=s.server[3]=s.gateway[3]=1; s.mask[0]=255;
    }
}
#endif
static const char *state_name(unsigned state)
{
    switch (state) {
    case 0:return "OFF"; case 1:return "REQUESTING"; case 2:return "INIT";
    case 3:return "REBOOTING"; case 4:return "REBINDING"; case 5:return "RENEWING";
    case 6:return "SELECTING"; case 7:return "INFORMING"; case 8:return "CHECKING";
    case 9:return "PERMANENT"; case 10:return "BOUND"; case 11:return "RELEASING";
    case 12:return "BACKING_OFF"; default:return "unknown";
    }
}
static void printable(char *s)
{ for (;*s;++s) if ((unsigned char)*s<32 || (unsigned char)*s>126) *s='?'; }
static void print_ip(const char *label,const unsigned char ip[4])
{
    char text[64]; snprintf(text,sizeof(text),"%s=%u.%u.%u.%u\n",label,ip[0],ip[1],ip[2],ip[3]); ms_write(text);
}
extern "C" void ms_wifi_diagnostics(const unsigned char *associated_bssid,unsigned associated_channel)
{
    Snapshot s{}; snapshot(s); // Finish collection before ms_write can yield.
    if (associated_bssid) {
        memcpy(s.bssid,associated_bssid,6); s.has_bssid=1; s.channel=associated_channel;
    }
    printable(s.ssid); printable(s.hostname); printable(s.interface_name);
    char text[192];
#ifdef MS_WEB_NATIVE
    ms_write("Native diagnostic fixture; no physical radio/lwIP measurements.\n");
#endif
    snprintf(text,sizeof(text),"Station MAC=%02x:%02x:%02x:%02x:%02x:%02x; hostname=%s\nSSID=%s\n",
        s.mac[0],s.mac[1],s.mac[2],s.mac[3],s.mac[4],s.mac[5],s.hostname,s.ssid); ms_write(text);
    if (s.has_bssid) {
        snprintf(text,sizeof(text),"AP BSSID=%02x:%02x:%02x:%02x:%02x:%02x; channel=%u; RSSI=%d dBm\n",
            s.bssid[0],s.bssid[1],s.bssid[2],s.bssid[3],s.bssid[4],s.bssid[5],s.channel,s.rssi); ms_write(text);
    } else ms_write("AP BSSID unavailable.\n");
    if (!s.interface_found) { ms_write("Station lwIP interface not found by MAC.\n"); return; }
    snprintf(text,sizeof(text),"Station interface=%s; up=%u; link-up=%u\n",s.interface_name,s.up,s.link_up); ms_write(text);
    print_ip("Interface IP",s.local); print_ip("Netmask",s.mask); print_ip("Gateway",s.gateway);
    if (!s.dhcp_present) { ms_write("lwIP DHCP client absent.\n"); return; }
    snprintf(text,sizeof(text),"lwIP DHCP state=%u (%s); tries=%u; pcb=%u; xid=%08x; request-timeout-ticks=%u\n",
        s.state,state_name(s.state),s.tries,s.pcb,(unsigned)s.xid,s.ticks); ms_write(text);
    print_ip("DHCP server",s.server); print_ip("Offered IP",s.offered);
    ms_write("DHCP counters describe client state, not a packet capture.\n");
}
