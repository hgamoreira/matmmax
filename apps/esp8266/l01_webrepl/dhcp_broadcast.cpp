/* Wrap only the pinned options-construction hook. Keep its original client-ID
 * addition and the caller's hostname addition; no changes at capture time. */
#ifndef MS_WEB_NATIVE
extern "C" {
#include <lwip/netif.h>
#include <lwip/dhcp.h>
#include <lwip/prot/dhcp.h>
}
#include "dhcp_broadcast.h"
extern "C" void __real_lwip_hook_dhcp_amend_options(struct netif *,struct dhcp *,int,
    struct dhcp_msg *,int,u16_t *);
extern "C" void __wrap_lwip_hook_dhcp_amend_options(struct netif *interface,struct dhcp *client,
    int state,struct dhcp_msg *message,int type,u16_t *options_length)
{
    __real_lwip_hook_dhcp_amend_options(interface,client,state,message,type,options_length);
    if (interface && interface->num==0 && interface->name[0]=='s' && interface->name[1]=='t')
        ms_dhcp_request_broadcast((unsigned char *)message,sizeof(*message),type);
}
#endif
