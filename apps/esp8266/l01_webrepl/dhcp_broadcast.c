/* RFC2131 section4.1: request broadcast replies before acquiring an address.
 * Called while constructing BOOTP, BEFORE UDP/IP checksums are generated.
 * Receive validation and the read-only PHY observer stay unchanged. */
#include "dhcp_broadcast.h"
void ms_dhcp_request_broadcast(unsigned char *message,size_t length,int type)
{
    if (!message || length<16 || message[0]!=1 || (type!=1 && type!=3)) return;
    if (message[12] || message[13] || message[14] || message[15]) return;
    message[10]|=0x80;
}
