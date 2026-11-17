#ifdef MS_WEB_NATIVE
#include "native_hal.h"
#else
#include <Arduino.h>
#include <ESP8266WiFi.h>
extern "C" {
#include <gluedebug.h>
}
#endif
#include <stdio.h>
#include "web.h"
#include "dhcp_trace.h"
static const char *reject_reason(unsigned reason)
{
    switch (reason) {
    case MS_DHCP_IP_LENGTH: return "ip-length";
    case MS_DHCP_UDP_LENGTH: return "udp-length";
    case MS_DHCP_OPCODE: return "bootp-opcode";
    case MS_DHCP_COOKIE: return "cookie";
    case MS_DHCP_OPTION_LENGTH: return "option-length-missing";
    case MS_DHCP_OPTION_TRUNCATED: return "option-truncated";
    case MS_DHCP_TYPE_LENGTH: return "message-type-length";
    case MS_DHCP_TYPE_DUPLICATE: return "message-type-duplicate";
    case MS_DHCP_TYPE_VALUE: return "message-type-value";
    case MS_DHCP_TYPE_MISSING: return "message-type-missing";
    default: return "unknown";
    }
}
extern "C" void ms_dhcp_trace_start(void)
{
    unsigned char mac[6];
#ifdef MS_WEB_NATIVE
    const unsigned char fixture[6]={0x34,0x5f,0x45,0x40,0x57,0x3a};
    memcpy(mac,fixture,6);
#else
    WiFi.macAddress(mac);
#endif
    ms_dhcp_trace_reset(mac);
#ifndef MS_WEB_NATIVE
    phy_capture=ms_dhcp_trace_observe;
#endif
}
extern "C" void ms_dhcp_trace_diagnostics(void)
{
    MsDhcpTrace s; ms_dhcp_trace_snapshot(&s);
    char text[224]; uint32_t tx=0,rx=0;
    for (unsigned i=1;i<9;++i) { tx+=s.tx[i]; rx+=s.rx[i]; }
    snprintf(text,sizeof(text),"DHCP packets TX=%u discover=%u request=%u driver-failed=%u; RX=%u offer=%u ack=%u nak=%u alloc-failed=%u\n",
        (unsigned)tx,(unsigned)s.tx[1],(unsigned)s.tx[3],(unsigned)s.tx_failed,(unsigned)rx,
        (unsigned)s.rx[2],(unsigned)s.rx[5],(unsigned)s.rx[6],(unsigned)s.rx_alloc_failed); ms_write(text);
    if (s.last_tx.type) {
        snprintf(text,sizeof(text),"Last DHCP TX: type=%u xid=%08x broadcast=%u client-id=%u hostname=%u\n",
            s.last_tx.type,(unsigned)s.last_tx.xid,s.last_tx.broadcast,s.last_tx.client_id,s.last_tx.hostname); ms_write(text);
    }
    if (s.last_rx.type) {
        const MsDhcpPacket& p=s.last_rx;
        snprintf(text,sizeof(text),"Last DHCP RX: type=%u xid=%08x source=%u.%u.%u.%u offered=%u.%u.%u.%u server=%u.%u.%u.%u checksum-ok=%u\n",
            p.type,(unsigned)p.xid,p.source[0],p.source[1],p.source[2],p.source[3],
            p.offered[0],p.offered[1],p.offered[2],p.offered[3],
            p.server[0],p.server[1],p.server[2],p.server[3],p.checksum_ok); ms_write(text);
    }
    snprintf(text,sizeof(text),"DHCP observed: bad-checksum=%u parser-rejected=%u wrong-xid=%u (driver acceptance is not server receipt)\n",
        (unsigned)s.bad_checksum,(unsigned)s.malformed,(unsigned)s.wrong_xid); ms_write(text);
    snprintf(text,sizeof(text),"DHCP parser rejected TX=%u RX=%u (observer verdict, not lwIP verdict)\n",
        (unsigned)s.tx_rejected,(unsigned)s.rx_rejected); ms_write(text);
    if (s.last_rejected.reject_reason) {
        const MsDhcpPacket& p=s.last_rejected;
        snprintf(text,sizeof(text),"Last DHCP rejected: direction=%s boundary-success=%u reason=%s frame=%u ip=%u udp=%u\n",
            s.rejected_out ? "TX" : "RX",s.rejected_success,reject_reason(p.reject_reason),
            p.frame_bytes,p.ip_bytes,p.udp_bytes); ms_write(text);
        snprintf(text,sizeof(text),"Rejected DHCP header: op=%u xid=%08x cookie=%08x type=%u checksum=%s source=%u.%u.%u.%u offered=%u.%u.%u.%u\n",
            p.boot_op,(unsigned)p.xid,(unsigned)p.cookie,p.type,
            p.reject_reason<=MS_DHCP_UDP_LENGTH ? "unchecked" : p.checksum_ok ? "ok" : "bad",
            p.source[0],p.source[1],p.source[2],p.source[3],
            p.offered[0],p.offered[1],p.offered[2],p.offered[3]); ms_write(text);
        if (p.reject_reason>=MS_DHCP_OPTION_LENGTH) {
            snprintf(text,sizeof(text),"Rejected DHCP option: offset=%u code=%u len=%u remaining=%u\n",
                p.option_offset,p.option_code,p.option_len,p.option_remaining); ms_write(text);
        }
    }
}
