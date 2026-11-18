/* X02: one HTTP GET at a time to the host server, over Watt-32 (DOS only). */
#ifndef XT_NETGET_H
#define XT_NETGET_H

#include "cfg.h"

/* Starts the network stack for this program: the XT's address, netmask and gateway
   and the server's address from cfg. Returns NF_OK or NF_NET_NOSTACK. */
int net_start(const Config *cfg);
/* After net_start returned NF_NET_NOSTACK: the reason in a few words (Watt-32's own text), else "". */
const char *net_start_error(void);
/* Releases the packet driver. Safe to call when net_start failed or was not called. */
void net_stop(void);
/* GET `path` (with its query) and put the body in buf. Only a 200 reply whose
   body fits in bufmax is accepted. Returns NF_OK (*len is the body length) or an
   NF_NET_* code from nfproto.h. Waits at most 5 s to connect and 5 s for data (wait= in the .CFG: longer). */
int net_get(const char *path, unsigned char *buf, int bufmax, int *len);
/* POST `body` (bodylen up to 4096 bytes) as raw bytes to `path`; the reply as for net_get. */
int net_post(const char *path, const unsigned char *body, int bodylen, unsigned char *buf, int bufmax, int *len);

#endif
