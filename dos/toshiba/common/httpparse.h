/* Just enough HTTP/1.x for the programs: build a GET, read a reply's
   status line and Content-Length (pure C). Shared by X01 and X02. */
#ifndef XT_HTTPPARSE_H
#define XT_HTTPPARSE_H

typedef struct {
    int status;            /* e.g. 200 */
    long content_length;   /* -1 if the reply has no Content-Length */
    int header_len;        /* bytes up to and including the blank line */
} HttpHead;

/* "GET <path> HTTP/1.0" with Host, User-Agent and Connection: close.
   out must hold at least 200 bytes. Returns the length. */
int http_build_get(char *out, const char *path, const char *host);
/* The same with another User-Agent (at most 24 characters, no CR/LF). */
int http_build_get_agent(char *out, const char *path, const char *host, const char *agent);
/* The head of a "POST <path> HTTP/1.0" whose body is `length` raw bytes
   (Content-Type: application/octet-stream, docs/NET_PROTOCOL.md "XT additions"). The
   body follows the head. out must hold at least 300 bytes for a path of 200. */
int http_build_post_agent(char *out, const char *path, const char *host, const char *agent, unsigned length);
/* Returns 1 when the head is complete and valid (h filled in), 0 when more
   bytes are needed, -1 when it is not an HTTP reply. */
int http_parse_head(const char *buf, int len, HttpHead *h);

#endif
