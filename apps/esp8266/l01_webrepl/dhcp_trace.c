/* Observe bounded Ethernet/IPv4/UDP/BOOTP metadata. Never retain packet bodies,
 * allocate, print, yield, alter buffers or affect the networking result. */
#include <string.h>
#include "dhcp_trace.h"
static unsigned char station_mac[6];
static int enabled;
static MsDhcpTrace trace;
static unsigned word(const unsigned char *p) { return ((unsigned)p[0]<<8)|p[1]; }
static uint32_t dword(const unsigned char *p)
{ return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3]; }
static uint32_t sum_words(const unsigned char *p,size_t n)
{
    uint32_t sum=0;
    while (n>=2) { sum+=word(p); p+=2; n-=2; }
    if (n) sum+=(unsigned)p[0]<<8;
    return sum;
}
static int sum_ok(uint32_t sum)
{ while (sum>>16) sum=(sum&65535)+(sum>>16); return sum==65535; }
static int rejected(MsDhcpPacket *out,unsigned char reason)
{ out->reject_reason=reason; return -1; }

int ms_dhcp_packet_read(const unsigned char *p,size_t n,const unsigned char mac[6],MsDhcpPacket *out)
{
    const unsigned char *ip,*udp,*boot,*opt;
    size_t l2=14,ihl,iplen,udplen,remaining;
    unsigned type,tags=0,src,dst,seen_type=0;
    memset(out,0,sizeof(*out));
    if (n<14) return 0;
    type=word(p+12);
    while ((type==0x8100 || type==0x88a8) && tags++<2) {
        if (n<l2+4) return 0;
        type=word(p+l2+2); l2+=4;
    }
    if (type!=0x0800 || n<l2+20) return 0;
    ip=p+l2; ihl=(ip[0]&15)*4;
    if ((ip[0]>>4)!=4 || ihl<20 || n<l2+ihl+8 || ip[9]!=17 || (word(ip+6)&0x3fff)) return 0;
    udp=ip+ihl; src=word(udp); dst=word(udp+2);
    if (!((src==68 && dst==67) || (src==67 && dst==68))) return 0;
    // Check client address before counting malformed traffic, excluding peers.
    if (n<l2+ihl+8+34) return 0;
    boot=udp+8;
    if (boot[1]!=1 || boot[2]!=6 || memcmp(boot+28,mac,6)) return 0;
    iplen=word(ip+2); udplen=word(udp+4);
    out->frame_bytes=n>65535 ? 65535 : (uint16_t)n;
    out->ip_bytes=(uint16_t)iplen; out->udp_bytes=(uint16_t)udplen;
    out->boot_op=boot[0];
    out->xid=dword(boot+4); out->broadcast=(word(boot+10)&0x8000)!=0;
    memcpy(out->source,ip+12,4); memcpy(out->offered,boot+16,4);
    if (n-l2-ihl-8>=240) out->cookie=dword(boot+236);
    if (iplen<ihl+8 || iplen>n-l2) return rejected(out,MS_DHCP_IP_LENGTH);
    if (udplen<248 || udplen>iplen-ihl) return rejected(out,MS_DHCP_UDP_LENGTH);
    out->checksum_ok=sum_ok(sum_words(ip,ihl)) &&
        (!word(udp+6) || sum_ok(sum_words(ip+12,8)+17+udplen+sum_words(udp,udplen)));
    if ((src==68 && boot[0]!=1) || (src==67 && boot[0]!=2)) return rejected(out,MS_DHCP_OPCODE);
    if (out->cookie!=0x63825363U) return rejected(out,MS_DHCP_COOKIE);
    opt=boot+240; remaining=udplen-8-240;
    while (remaining) {
        unsigned code=*opt++,length; --remaining;
        if (code==255) break;
        if (!code) continue;
        out->option_offset=(uint16_t)(opt-1-(boot+240));
        out->option_code=(unsigned char)code; out->option_len=0;
        out->option_remaining=(uint16_t)remaining;
        if (!remaining) return rejected(out,MS_DHCP_OPTION_LENGTH);
        length=*opt++; --remaining;
        out->option_len=(unsigned char)length; out->option_remaining=(uint16_t)remaining;
        if (length>remaining) return rejected(out,MS_DHCP_OPTION_TRUNCATED);
        if (code==53) {
            if (length!=1) return rejected(out,MS_DHCP_TYPE_LENGTH);
            if (seen_type++) return rejected(out,MS_DHCP_TYPE_DUPLICATE);
            if (!opt[0] || opt[0]>8) return rejected(out,MS_DHCP_TYPE_VALUE);
            out->type=opt[0];
        } else if (code==61 && length==7 && opt[0]==1 && !memcmp(opt+1,mac,6)) out->client_id=1;
        else if (code==12 && length) out->hostname=1;
        else if (code==54 && length==4) memcpy(out->server,opt,4);
        opt+=length; remaining-=length;
    }
    return seen_type ? 1 : rejected(out,MS_DHCP_TYPE_MISSING);
}
void ms_dhcp_trace_reset(const unsigned char mac[6])
{ enabled=0; memset(&trace,0,sizeof(trace)); memcpy(station_mac,mac,6); enabled=1; }
void ms_dhcp_trace_observe(int netif_idx,const char *data,size_t len,int out,int success)
{
    MsDhcpPacket packet; int result;
    if (!enabled || netif_idx!=0) return;
    result=ms_dhcp_packet_read((const unsigned char *)data,len,station_mac,&packet);
    if (result<0) {
        ++trace.malformed;
        if (out) ++trace.tx_rejected; else ++trace.rx_rejected;
        trace.last_rejected=packet;
        trace.rejected_out=!!out; trace.rejected_success=!!success;
        return;
    }
    if (!result) return;
    if (!packet.checksum_ok) ++trace.bad_checksum;
    if (out) {
        ++trace.tx[packet.type]; if (!success) ++trace.tx_failed;
        trace.last_tx=packet;
    } else {
        ++trace.rx[packet.type]; if (!success) ++trace.rx_alloc_failed;
        if (trace.last_tx.type && trace.last_tx.xid!=packet.xid) ++trace.wrong_xid;
        trace.last_rx=packet;
    }
}
void ms_dhcp_trace_snapshot(MsDhcpTrace *out) { *out=trace; }
