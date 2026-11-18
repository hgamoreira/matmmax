#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cfg.h"

void cfg_defaults(Config *c)
{
    memset(c, 0, sizeof *c);
    c->port = 8064;
}

int cfg_ip_is_set(const unsigned char *ip)
{
    return (ip[0] | ip[1] | ip[2] | ip[3]) != 0;
}

int cfg_parse_ip(const char *text, unsigned char *ip)
{
    int part;
    unsigned char out[4];

    for (part = 0; part < 4; ++part) {
        long v = 0;
        int digits = 0;
        while (isdigit((unsigned char)*text)) {
            v = v * 10 + (*text - '0');
            if (++digits > 3 || v > 255) {
                return 0;
            }
            ++text;
        }
        if (digits == 0) {
            return 0;
        }
        out[part] = (unsigned char)v;
        if (part < 3) {
            if (*text != '.') {
                return 0;
            }
            ++text;
        }
    }
    if (*text != '\0') {
        return 0;
    }
    memcpy(ip, out, 4);
    return 1;
}

void cfg_format_ip(const unsigned char *ip, char *out)
{
    sprintf(out, "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
}

static void trim(char *s)
{
    char *start = s;
    size_t n;

    while (isspace((unsigned char)*start)) {
        ++start;
    }
    memmove(s, start, strlen(start) + 1);
    n = strlen(s);
    while (n > 0 && isspace((unsigned char)s[n - 1])) {
        s[--n] = '\0';
    }
}

int cfg_parse_line(Config *c, const char *line)
{
    char buf[100];
    char *eq;
    char *value;

    strncpy(buf, line, sizeof buf - 1);
    buf[sizeof buf - 1] = '\0';
    trim(buf);
    if (buf[0] == '\0' || buf[0] == ';' || buf[0] == '#') {
        return 1;
    }
    eq = strchr(buf, '=');
    if (eq == NULL) {
        return 0;
    }
    *eq = '\0';
    value = eq + 1;
    trim(buf);
    trim(value);
    if (strcmp(buf, "server") == 0) {
        return cfg_parse_ip(value, c->server);
    }
    if (strcmp(buf, "gateway") == 0) {
        return cfg_parse_ip(value, c->gateway);
    }
    if (strcmp(buf, "ip") == 0) {
        return cfg_parse_ip(value, c->ip);
    }
    if (strcmp(buf, "netmask") == 0) {
        return cfg_parse_ip(value, c->netmask);
    }
    if (strcmp(buf, "mss") == 0) {
        char *end;
        long m = strtol(value, &end, 10);
        if (*value == '\0' || *end != '\0' || m < 64 || m > 1460) {
            return 0;
        }
        c->mss = (unsigned)m;
        return 1;
    }
    if (strcmp(buf, "rwin") == 0) {
        char *end;
        long w = strtol(value, &end, 10);
        if (*value == '\0' || *end != '\0' || w < 128 || w > 2048) {
            return 0;
        }
        c->rwin = (unsigned)w;
        return 1;
    }
    if (strcmp(buf, "wait") == 0) {
        char *end;
        long w = strtol(value, &end, 10);
        if (*value == '\0' || *end != '\0' || w < 5 || w > 60) {
            return 0;
        }
        c->wait = (unsigned)w;
        return 1;
    }
    if (strcmp(buf, "piece") == 0) {
        char *end;
        long p = strtol(value, &end, 10);
        if (*value == '\0' || *end != '\0' || p < 256 || p > 4096) {
            return 0;
        }
        c->piece = (unsigned)p;
        return 1;
    }
    if (strcmp(buf, "port") == 0) {
        char *end;
        long p = strtol(value, &end, 10);
        if (*value == '\0' || *end != '\0' || p < 1 || p > 65535) {
            return 0;
        }
        c->port = (unsigned)p;
        return 1;
    }
    return 0;
}

int cfg_load(Config *c, const char *path)
{
    FILE *f = fopen(path, "r");
    char line[100];
    int bad = 0;

    if (f == NULL) {
        return -1;
    }
    while (fgets(line, sizeof line, f) != NULL) {
        if (!cfg_parse_line(c, line)) {
            ++bad;
        }
    }
    fclose(f);
    return bad;
}

int cfg_load_mtcp(Config *c, const char *path)
{
    FILE *f = fopen(path, "r");
    char line[100];
    int filled = 0;

    if (f == NULL) {
        return -1;
    }
    while (fgets(line, sizeof line, f) != NULL) {
        char *p = line;
        char *word;
        unsigned char *field = NULL;
        unsigned char ip[4];

        while (isspace((unsigned char)*p)) {
            ++p;
        }
        word = p;
        while (*p != '\0' && !isspace((unsigned char)*p) && *p != '=') {
            *p = (char)toupper((unsigned char)*p);
            ++p;
        }
        if (*p != '\0') {
            *p++ = '\0';
        }
        if (strcmp(word, "IPADDR") == 0) {
            field = c->ip;
        } else if (strcmp(word, "NETMASK") == 0) {
            field = c->netmask;
        } else if (strcmp(word, "GATEWAY") == 0) {
            field = c->gateway;
        }
        if (field == NULL || cfg_ip_is_set(field)) {
            continue;
        }
        trim(p);
        if (cfg_parse_ip(p, ip)) {
            memcpy(field, ip, 4);
            ++filled;
        }
    }
    fclose(f);
    return filled;
}

void cfg_finish(Config *c)
{
    if (!cfg_ip_is_set(c->gateway)) {
        c->gateway[0] = 10; c->gateway[1] = 42; c->gateway[2] = 0; c->gateway[3] = 1;
        c->gateway_is_default = 1;
    }
}
