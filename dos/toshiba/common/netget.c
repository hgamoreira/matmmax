#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tcp.h>
#include "httpparse.h"
#include "nfproto.h"
#include "ticks.h"
#include "netget.h"

#define DEFAULT_TICKS 91      /* 5 s to open the connection, and 5 s without a byte; wait= in the .CFG changes both */
#define HEAD_ROOM     512     /* a reply head must fit in this */
#define CHUNK         512

static tcp_Socket sock;
static char cfg_ip[20], cfg_mask[20], cfg_gw[20], cfg_mss[8], host[20];
static unsigned cfg_rwin;                 /* 0: the default 2048-byte window */
static unsigned long wait_ticks = DEFAULT_TICKS;   /* a serial link (the Toshiba, the PS/1) waits longer: a lost packet's resend */
static byte rx_window[2048 + 16];         /* the receive buffer when rwin= is set */
static longword server_ip;
static unsigned server_port;
static int started;
static char start_error[48];            /* why net_start failed, for the user */
static char *req;                       /* the request head and, for a POST, its body (far heap) */
#define REQ_MAX (900 + NF_CHUNK_BIG)

static long W32_CALL inject_settings(int pass, const struct config_table *cfg)
{
    /* Both passes (1: packet driver set-up, 2: the rest): with pass 1 alone Watt-32
       reports "Failed to get an IP-address". */
    (void)pass;
    tcp_inject_config(cfg, "MY_IP", cfg_ip);
    tcp_inject_config(cfg, "NETMASK", cfg_mask);
    tcp_inject_config(cfg, "GATEWAY", cfg_gw);
    if (cfg_mss[0] != '\0') {
        tcp_inject_config(cfg, "MSS", cfg_mss);     /* a slow serial link: small segments (see docs/DECISIONS.md XD31) */
    }
    return 1;       /* 0 would mean "config file not found" to Watt-32 */
}

static void remember(const char *why)
{
    strncpy(start_error, why, sizeof start_error - 1);
    start_error[sizeof start_error - 1] = '\0';
}

const char *net_start_error(void)
{
    return start_error;
}

int net_start(const Config *cfg)
{
    int rc;

    start_error[0] = '\0';
    if (req == NULL) {
        req = (char *)malloc(REQ_MAX);
        if (req == NULL) {
            remember("not enough memory");
            return NF_NET_NOSTACK;
        }
    }
    cfg_format_ip(cfg->server, host);
    cfg_format_ip(cfg->ip, cfg_ip);
    cfg_format_ip(cfg->netmask, cfg_mask);
    cfg_format_ip(cfg->gateway, cfg_gw);
    cfg_mss[0] = '\0';
    if (cfg->mss != 0) {
        sprintf(cfg_mss, "%u", cfg->mss);
    }
    cfg_rwin = cfg->rwin;
    server_port = cfg->port;
    wait_ticks = cfg->wait != 0 ? (unsigned long)cfg->wait * 182UL / 10UL : DEFAULT_TICKS;
    nf_wait_seconds = cfg->wait != 0 ? cfg->wait : 5;

    _outch = NULL;                      /* Watt-32 must not print on our screen */
    _watt_no_config = 1;
    _watt_do_exit = 0;                  /* a failed sock_init must return, not exit() */
    _watt_user_config(inject_settings);
    rc = sock_init();
    if (rc != 0) {
        remember(sock_init_err(rc));
        return NF_NET_NOSTACK;
    }
    server_ip = resolve(host);
    if (server_ip == 0) {
        remember("the server address could not be used");
        sock_exit();
        return NF_NET_NOSTACK;
    }
    started = 1;
    return NF_OK;
}

void net_stop(void)
{
    if (started) {
        sock_exit();                    /* releases the packet driver handle */
        started = 0;
    }
}

/* Sends the first reqlen bytes of req and reads the reply as net_get describes. */
static int request(int reqlen, unsigned char *buf, int bufmax, int *len)
{
    static char head[HEAD_ROOM + CHUNK + 1];
    static byte chunk[CHUNK];
    HttpHead h;
    int head_len = 0, head_done = 0, body = 0, outcome = NF_OK;
    unsigned long start, last_data;
    sock_type *s = (sock_type *)&sock;

    *len = 0;
    if (!started) {
        return NF_NET_NOSTACK;
    }
    h.content_length = -1;
    start = bios_ticks();
    if (!tcp_open(&sock, 0, server_ip, server_port, NULL)) {
        return NF_NET_NOROUTE;
    }
    if (cfg_rwin != 0) {                  /* a slow link: the server may send only this much before we acknowledge */
        sock_setbuf((sock_type *)&sock, rx_window, (size_t)cfg_rwin + 9);
    }
    while (tcp_tick(s) && !sock_established(s)) {
        if (bios_ticks() - start > wait_ticks) {
            break;
        }
    }
    if (!sock_established(s)) {
        sock_abort(s);
        return NF_NET_NOCONNECT;
    }
    sock_write(s, (const byte *)req, reqlen);

    last_data = bios_ticks();
    for (;;) {
        int alive = tcp_tick(s);
        int n = sock_fastread(s, chunk, CHUNK);

        if (n > 0) {
            last_data = bios_ticks();
            if (head_done) {
                if (body + n > bufmax) {
                    outcome = NF_NET_TOOBIG;
                    break;
                }
                memcpy(buf + body, chunk, (size_t)n);
                body += n;
            } else {
                int rc;

                memcpy(head + head_len, chunk, (size_t)n);
                head_len += n;
                rc = http_parse_head(head, head_len, &h);
                if (rc < 0 || (rc == 0 && head_len >= HEAD_ROOM)) {
                    outcome = NF_NET_BADHTTP;
                    break;
                }
                if (rc == 1) {
                    int extra = head_len - h.header_len;

                    head_done = 1;
                    if (h.status != 200) {
                        outcome = NF_NET_HTTPSTATUS;
                        break;
                    }
                    if (h.content_length > bufmax || extra > bufmax) {
                        outcome = NF_NET_TOOBIG;
                        break;
                    }
                    memcpy(buf, head + h.header_len, (size_t)extra);
                    body = extra;
                }
            }
        } else if (n < 0) {
            break;                                  /* the server closed and its data is read */
        }
        if (head_done && h.content_length >= 0 && body >= h.content_length) {
            break;
        }
        if (!alive && n <= 0) {
            break;
        }
        if (bios_ticks() - last_data > wait_ticks) {
            outcome = NF_NET_TIMEOUT;
            break;
        }
    }
    sock_abort(s);
    if (outcome != NF_OK) {
        return outcome;
    }
    if (!head_done) {
        return head_len == 0 ? NF_NET_CLOSED : NF_NET_BADHTTP;
    }
    if (h.content_length >= 0 && body != h.content_length) {
        return NF_NET_TIMEOUT;                      /* closed before the whole body arrived */
    }
    *len = body;
    return NF_OK;
}

#define AGENT "XT-NETFILES/1"

int net_get(const char *path, unsigned char *buf, int bufmax, int *len)
{
    return request(http_build_get_agent(req, path, host, AGENT), buf, bufmax, len);
}

int net_post(const char *path, const unsigned char *body, int bodylen, unsigned char *buf, int bufmax, int *len)
{
    int n;

    if (!started || bodylen < 0 || bodylen > NF_CHUNK_BIG || strlen(path) > 200) {
        *len = 0;
        return NF_NET_TOOBIG;
    }
    n = http_build_post_agent(req, path, host, AGENT, (unsigned)bodylen);
    memcpy(req + n, body, (size_t)bodylen);
    return request(n + bodylen, buf, bufmax, len);
}
