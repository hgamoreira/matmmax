/* Bounded outer-loop fallback for SDK-enabled DHCP whose real client is OFF.
 * Never stop/restart an active client or force an interface/link/address. */
#ifdef MS_WEB_NATIVE
#include "native_hal.h"
#else
#include <Arduino.h>
#include <ESP8266WiFi.h>
extern "C" {
#include <user_interface.h>
#include <lwip/netif.h>
#include <lwip/dhcp.h>
#include <lwip/prot/dhcp.h>
}
#endif
#include <string.h>
#include "web.h"

extern "C" int ms_wifi_start_stopped_dhcp(MsDhcpStart *result)
{
    memset(result,0,sizeof(*result));
    if (!(WiFi.getMode()&WIFI_STA) || wifi_station_dhcpc_status()!=1 || (uint32_t)WiFi.localIP()) return 1;
#ifdef MS_WEB_NATIVE
    if (getenv("MS_TEST_DIAG_NO_INTERFACE") ||
        (getenv("MS_TEST_DIAG_UP") && !atoi(getenv("MS_TEST_DIAG_UP"))) ||
        (getenv("MS_TEST_DIAG_LINK_UP") && !atoi(getenv("MS_TEST_DIAG_LINK_UP")))) return 1;
    result->before_state=WiFi.lwip_state; result->before_pcb=WiFi.lwip_pcb;
    if (WiFi.lwip_state || WiFi.lwip_pcb) return 1;
    int error=WiFi.startLwipDhcp();
    result->state=WiFi.lwip_state; result->pcb=WiFi.lwip_pcb; result->tries=WiFi.lwip_tries;
    return error;
#else
    unsigned char mac[6]; WiFi.macAddress(mac);
    unsigned seen=0;
    for (netif *n=netif_list;n && seen++<8;n=n->next) {
        if (n->hwaddr_len!=6 || memcmp(n->hwaddr,mac,6)) continue;
        if (!netif_is_up(n) || !netif_is_link_up(n) || !ip4_addr_isany(netif_ip4_addr(n))) return 1;
        const dhcp *client=netif_dhcp_data(n);
        if (client) {
            result->before_state=client->state; result->before_pcb=client->pcb_allocated;
            if (client->state!=DHCP_STATE_OFF || client->pcb_allocated) return 1;
        }
        // arch/cc.h maps this to dhcp_start_LWIP2, not the SDK lwIP1 wrapper.
        int error=(int)dhcp_start(n);
        client=netif_dhcp_data(n);
        if (client) { result->state=client->state; result->pcb=client->pcb_allocated; result->tries=client->tries; }
        return error;
    }
    return 1;
#endif
}
