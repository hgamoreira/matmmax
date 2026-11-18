/* XDIAG.CFG, NETFILES.CFG, WORDSMTH.CFG: "key=value" lines. Keys: server, port, gateway, ip, netmask, piece, mss, rwin, wait (pure C). */
#ifndef XT_CFG_H
#define XT_CFG_H

typedef struct {
    unsigned char server[4];    /* the PC running host/server.py; 0.0.0.0 = not set */
    unsigned port;
    unsigned char gateway[4];   /* for the ARP test */
    unsigned char ip[4];        /* the XT's own address; 0.0.0.0 = take it from the mTCP file */
    unsigned char netmask[4];
    int mtcp_fields;            /* how many values came from the mTCP file (-1: no file) */
    int gateway_is_default;     /* 1 if the gateway is cfg_finish's built-in guess */
    unsigned piece;             /* piece= (256-4096): data bytes per request in NETFILES' copies; 0 = not set */
    unsigned mss;               /* mss= (64-1460): the TCP segment size we ask the server to use; 0 = not set (Watt-32's 1460) */
    unsigned rwin;              /* rwin= (128-2048): our TCP receive window, i.e. how much the server may send before we acknowledge; 0 = not set (2048) */
    unsigned wait;              /* wait= (5-60): seconds a transfer waits for the server (to connect, and between bytes); 0 = not set (5) */
} Config;

void cfg_defaults(Config *c);
/* "a.b.c.d" -> 4 bytes; 1 if valid (four decimal numbers 0-255). */
int cfg_parse_ip(const char *text, unsigned char *ip);
void cfg_format_ip(const unsigned char *ip, char *out);
/* One line; blank lines, "; ..." and "# ..." comments are accepted (return 1).
   Returns 0 for an unknown key or a bad value. */
int cfg_parse_line(Config *c, const char *line);
/* Reads path; returns the number of lines that were not understood, or -1
   if the file cannot be opened (then c keeps its defaults). */
int cfg_load(Config *c, const char *path);
int cfg_ip_is_set(const unsigned char *ip);
/* mTCP's config file (named by the MTCPCFG environment variable; DHCP.EXE
   records the lease there): lines "IPADDR a.b.c.d", "NETMASK ...",
   "GATEWAY ...", keywords in any case, "#" comments. Fills only the
   fields of c that are still 0.0.0.0. Returns the number of fields it
   filled, or -1 if the file cannot be opened. */
int cfg_load_mtcp(Config *c, const char *path);
/* Last step after the files: a still-unset gateway becomes 10.42.0.1, the
   gateway the XT got from the Pi's hotspot (PROGRESS.md), and is flagged. */
void cfg_finish(Config *c);

#endif
