#ifndef MS_DHCP_TRACE_H
#define MS_DHCP_TRACE_H
#include <stddef.h>
#include <stdint.h>
enum {
    MS_DHCP_OK, MS_DHCP_IP_LENGTH, MS_DHCP_UDP_LENGTH, MS_DHCP_OPCODE,
    MS_DHCP_COOKIE, MS_DHCP_OPTION_LENGTH, MS_DHCP_OPTION_TRUNCATED,
    MS_DHCP_TYPE_LENGTH, MS_DHCP_TYPE_DUPLICATE, MS_DHCP_TYPE_VALUE,
    MS_DHCP_TYPE_MISSING
};
typedef struct {
    uint32_t xid;
    unsigned char type,broadcast,client_id,hostname,checksum_ok;
    unsigned char source[4],offered[4],server[4];
    uint32_t cookie;
    uint16_t frame_bytes,ip_bytes,udp_bytes,option_offset,option_remaining;
    unsigned char reject_reason,boot_op,option_code,option_len;
} MsDhcpPacket;
typedef struct {
    uint32_t tx[9],rx[9],tx_failed,rx_alloc_failed,bad_checksum,malformed,wrong_xid;
    MsDhcpPacket last_tx,last_rx;
    uint32_t tx_rejected,rx_rejected;
    MsDhcpPacket last_rejected;
    unsigned char rejected_out,rejected_success;
} MsDhcpTrace;
#ifdef __cplusplus
extern "C" {
#endif
int ms_dhcp_packet_read(const unsigned char *,size_t,const unsigned char mac[6],MsDhcpPacket *);
void ms_dhcp_trace_reset(const unsigned char mac[6]);
void ms_dhcp_trace_observe(int,const char *,size_t,int,int);
void ms_dhcp_trace_snapshot(MsDhcpTrace *);
void ms_dhcp_trace_start(void);
void ms_dhcp_trace_diagnostics(void);
#ifdef __cplusplus
}
#endif
#endif
