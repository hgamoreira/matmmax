#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "httpparse.h"

int http_build_get_agent(char *out, const char *path, const char *host, const char *agent)
{
    return sprintf(out,
                   "GET %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: %s\r\nConnection: close\r\n\r\n",
                   path, host, agent);
}

int http_build_post_agent(char *out, const char *path, const char *host, const char *agent, unsigned length)
{
    return sprintf(out,
                   "POST %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: %s\r\n"
                   "Content-Type: application/octet-stream\r\nContent-Length: %u\r\nConnection: close\r\n\r\n",
                   path, host, agent, length);
}

int http_build_get(char *out, const char *path, const char *host)
{
    return http_build_get_agent(out, path, host, "XT-XDIAG/1");
}

/* Case-insensitive compare of the first n characters. */
static int prefix_nocase(const char *s, const char *prefix)
{
    while (*prefix != '\0') {
        if (tolower((unsigned char)*s) != tolower((unsigned char)*prefix)) {
            return 0;
        }
        ++s;
        ++prefix;
    }
    return 1;
}

int http_parse_head(const char *buf, int len, HttpHead *h)
{
    int end = -1;
    int i, line_start;
    char num[12];

    /* the head ends at the first blank line */
    for (i = 0; i + 3 < len; ++i) {
        if (buf[i] == '\r' && buf[i + 1] == '\n' && buf[i + 2] == '\r' && buf[i + 3] == '\n') {
            end = i + 4;
            break;
        }
    }
    if (len >= 5 && memcmp(buf, "HTTP/", 5) != 0) {
        return -1;
    }
    if (end < 0) {
        return 0;
    }
    /* status line: "HTTP/1.x NNN reason" */
    if (end < 12 || buf[8] != ' ' || !isdigit((unsigned char)buf[9]) ||
        !isdigit((unsigned char)buf[10]) || !isdigit((unsigned char)buf[11])) {
        return -1;
    }
    h->status = (buf[9] - '0') * 100 + (buf[10] - '0') * 10 + (buf[11] - '0');
    h->content_length = -1;
    h->header_len = end;

    line_start = 0;
    for (i = 0; i < end; ++i) {
        if (buf[i] == '\n') {
            if (line_start > 0 && prefix_nocase(buf + line_start, "content-length:")) {
                const char *p = buf + line_start + 15;
                int n = 0;
                while (*p == ' ' || *p == '\t') {
                    ++p;
                }
                while (isdigit((unsigned char)*p) && n < (int)sizeof num - 1) {
                    num[n++] = *p++;
                }
                num[n] = '\0';
                if (n > 0) {
                    h->content_length = atol(num);
                }
            }
            line_start = i + 1;
        }
    }
    return 1;
}
