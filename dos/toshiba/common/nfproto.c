#include <stdio.h>
#include <string.h>
#include "nfproto.h"

#define LIST_HEADER 9
#define TEXT_HEADER 7
#define VERSION 1
#define LAST_STATUS 17         /* the highest body status in NET_PROTOCOL.md */
#define STATUS_CHANGED 7

/* --- requests --- */

int nf_encode(char *out, int max, const char *s)
{
    static const char hex[] = "0123456789ABCDEF";
    int n = 0;

    for (; *s != '\0'; ++s) {
        unsigned char c = (unsigned char)*s;
        int plain = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                    c == '-' || c == '.' || c == '_' || c == '~';

        if (n + (plain ? 1 : 3) + 1 > max) {
            return -1;
        }
        if (plain) {
            out[n++] = (char)c;
        } else {
            out[n++] = '%';
            out[n++] = hex[c >> 4];
            out[n++] = hex[c & 15];
        }
    }
    out[n] = '\0';
    return n;
}

#define URL_PATH_LIMIT (NF_PATH_MAX + NF_NAME_MAX)

static int url(char *out, const char *verb, const char *path, const char *first_key, unsigned first,
               unsigned count, const char *tail)
{
    char enc[3 * URL_PATH_LIMIT + 1];

    if (strlen(path) > URL_PATH_LIMIT || nf_encode(enc, (int)sizeof enc, path) < 0) {
        return -1;
    }
    return sprintf(out, "/c64/v1/files/%s?path=%s&%s=%X&count=%X%s", verb, enc, first_key, first, count, tail);
}

int nf_url_list(char *out, const char *path, unsigned start, unsigned count)
{
    return url(out, "list", path, "start", start, count, NF_PAGES_TAIL);
}

int nf_url_text(char *out, const char *path, unsigned line, unsigned count)
{
    return url(out, "text", path, "line", line, count, "&width=" NF_WIDTH_HEX NF_PAGES_TAIL);
}

/* --- replies --- */

static unsigned le16(const unsigned char *p)
{
    return (unsigned)p[0] | ((unsigned)p[1] << 8);
}

static unsigned long le32(const unsigned char *p)
{
    return (unsigned long)p[0] | ((unsigned long)p[1] << 8) | ((unsigned long)p[2] << 16) |
           ((unsigned long)p[3] << 24);
}

static int printable(const unsigned char *p, int n)
{
    int i;

    for (i = 0; i < n; ++i) {
        if (p[i] < 0x20 || p[i] > 0x7E) {
            return 0;
        }
    }
    return 1;
}

/* The common start of a reply: the status byte's verdict. Returns NF_OK to go on, or
   the code to return as it is (an error status must be the whole header and nothing else). */
static int check_head(const unsigned char *buf, int len, int header, int max)
{
    int i;

    if (len < header || len > max) {
        return NF_BAD_SIZE;
    }
    if (buf[0] != VERSION) {
        return NF_BAD_VERSION;
    }
    if (buf[1] > LAST_STATUS) {
        return NF_BAD_STATUS;
    }
    if (buf[1] != 0) {
        if (len != header) {
            return NF_BAD_STATUS;
        }
        for (i = 2; i < header; ++i) {      /* after an error the rest of the header is zero,
                                               except the echoed start/line */
            if (i != 4 && i != 5 && buf[i] != 0) {
                return NF_BAD_STATUS;
            }
        }
        return buf[1];
    }
    return NF_OK;
}

int nf_parse_list(const unsigned char *buf, int len, unsigned start, unsigned count, NfList *out)
{
    int rc = check_head(buf, len, LIST_HEADER, NF_LIST_REPLY_MAX);
    unsigned n, i;
    int pos;
    NfList tmp;

    if (rc != NF_OK) {
        return rc;
    }
    tmp.total = le16(buf + 2);
    tmp.start = le16(buf + 4);
    n = buf[6];
    tmp.hidden = le16(buf + 7);
    tmp.count = n;
    if (tmp.start != start) {
        return NF_BAD_ECHO;
    }
    if (count < 1 || count > NF_LIST_MAX || n > count) {
        return NF_BAD_COUNT;
    }
    if (n > 0 && (unsigned long)start + n > tmp.total) {
        return NF_BAD_BOUNDS;
    }
    pos = LIST_HEADER;
    for (i = 0; i < n; ++i) {
        unsigned nlen;

        if (len - pos < 6) {
            return NF_BAD_SIZE;
        }
        tmp.e[i].type = buf[pos];
        tmp.e[i].size = le32(buf + pos + 1);
        nlen = buf[pos + 5];
        if (tmp.e[i].type > NF_TYPE_BIN) {
            return NF_BAD_ENTRY;
        }
        if (nlen < 1 || nlen > NF_NAME_MAX) {
            return NF_BAD_NAME;
        }
        if (len - pos - 6 < (int)nlen) {
            return NF_BAD_SIZE;
        }
        if (!printable(buf + pos + 6, (int)nlen) || memchr(buf + pos + 6, '/', nlen) != NULL ||
            memchr(buf + pos + 6, '\\', nlen) != NULL) {
            return NF_BAD_NAME;
        }
        memcpy(tmp.e[i].name, buf + pos + 6, nlen);
        tmp.e[i].name[nlen] = '\0';
        pos += 6 + (int)nlen;
    }
    if (pos != len) {
        return NF_BAD_TRAILING;
    }
    memcpy(out, &tmp, sizeof tmp);
    return NF_OK;
}

int nf_parse_text(const unsigned char *buf, int len, unsigned line, unsigned count, NfText *out)
{
    int rc = check_head(buf, len, TEXT_HEADER, NF_TEXT_REPLY_MAX);
    unsigned n, i;
    int pos;

    if (rc != NF_OK) {
        return rc;
    }
    out->total = le16(buf + 2);
    out->first = le16(buf + 4);
    n = buf[6];
    out->count = n;
    if (out->first != line) {
        return NF_BAD_ECHO;
    }
    if (count < 1 || count > NF_TEXT_MAX || n > count) {
        return NF_BAD_COUNT;
    }
    if ((unsigned long)line + n > out->total) {
        return NF_BAD_BOUNDS;
    }
    pos = TEXT_HEADER;
    for (i = 0; i < n; ++i) {
        unsigned llen;

        if (len - pos < 1) {
            return NF_BAD_SIZE;
        }
        llen = buf[pos];
        if (llen > NF_COLS) {
            return NF_BAD_LINE;
        }
        if (len - pos - 1 < (int)llen) {
            return NF_BAD_SIZE;
        }
        if (!printable(buf + pos + 1, (int)llen)) {
            return NF_BAD_LINE;
        }
        memcpy(out->line[i], buf + pos + 1, llen);
        out->line[i][llen] = '\0';
        pos += 1 + (int)llen;
    }
    if (pos != len) {
        return NF_BAD_TRAILING;
    }
    return NF_OK;
}

unsigned nf_wait_seconds = 5;

const char *nf_message(int code)
{
    static char other[64];

    switch (code) {
    case 1:   return "Not found on the server (R: reload)";
    case 2:   return "Wrong type (a file where a folder is needed, or the reverse)";
    case 3:   return "The server refused the request (bad parameters or path)";
    case 4:   return "Access denied by the server";
    case 5:   return "Not a text file: cannot view";
    case 6:   return "File too large to view (over 1 MB)";
    case STATUS_CHANGED: return "The file changed on the server (R: reload)";
    case 8:   return "That name already exists on the PC";
    case 13:  return "The folder is not empty: nothing was deleted";
    case NF_STATUS_AI_UNKNOWN: return "The PC no longer knows this AI job (idle too long?)";
    case NF_STATUS_AI_BUSY:    return "The PC is busy with other AI jobs: try again";
    case NF_STATUS_AI_NOMODEL: return "The AI model is not available on the PC (is Ollama running?)";
    case NF_STATUS_AI_FAILED:  return "The AI model failed before it wrote anything";
    case 9:   return "The upload got out of step: nothing was saved";
    case NF_UPLOAD_CHECK: return "The PC found the data damaged: nothing was saved";
    case 11:  return "The PC no longer knows this upload (idle too long?): nothing was saved";
    case 12:  return "The server is busy with other uploads: try again";
    case NF_NET_NOROUTE:    return "No route to the server (ARP, gateway)";
    case NF_NET_NOCONNECT:  return "Could not connect (is the server running? firewall?)";
    case NF_NET_TIMEOUT:
        if (nf_wait_seconds != 5) {
            static char text[40];

            sprintf(text, "No data from the server for %u s", nf_wait_seconds);
            return text;
        }
        return "No data from the server for 5 s";
    case NF_NET_BADHTTP:    return "Not an HTTP reply from the server";
    case NF_NET_HTTPSTATUS: return "The server answered with an HTTP error";
    case NF_NET_TOOBIG:     return "The reply is larger than the protocol allows";
    case NF_NET_NOSTACK:    return "The network did not start (packet driver? IP address?)";
    case NF_NET_CLOSED:     return "The server closed the connection without answering";
    default:
        break;
    }
    if (code < 0) {
        sprintf(other, "Damaged reply from the server (code %d)", code);
    } else {
        sprintf(other, "The server reports status %d", code);
    }
    return other;
}

/* --- download and upload --- */

static const unsigned crc_nibble[16] = {
    0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50A5, 0x60C6, 0x70E7,
    0x8108, 0x9129, 0xA14A, 0xB16B, 0xC18C, 0xD1AD, 0xE1CE, 0xF1EF
};

unsigned nf_crc16(unsigned crc, const unsigned char *data, unsigned len)
{
    crc &= 0xFFFFu;
    while (len-- > 0) {
        unsigned char b = *data++;

        crc = ((crc << 4) & 0xFFFFu) ^ crc_nibble[((crc >> 12) ^ (b >> 4)) & 15];
        crc = ((crc << 4) & 0xFFFFu) ^ crc_nibble[((crc >> 12) ^ b) & 15];
    }
    return crc;
}

#define FILES "/c64/v1/files/"

static int url_path(char *out, const char *verb, const char *path, const char *tail_fmt_unused)
{
    char enc[3 * URL_PATH_LIMIT + 1];

    (void)tail_fmt_unused;
    if (strlen(path) > URL_PATH_LIMIT || nf_encode(enc, (int)sizeof enc, path) < 0) {
        return -1;
    }
    return sprintf(out, FILES "%s?path=%s", verb, enc);
}

int nf_url_info(char *out, const char *path)
{
    return url_path(out, "info", path, "");
}

int nf_url_read(char *out, const char *path, unsigned long offset, unsigned count)
{
    int n = url_path(out, "read", path, "");

    if (n < 0) {
        return -1;
    }
    return n + sprintf(out + n, "&offset=%lX&count=%X%s", offset, count, count > NF_CHUNK_MAX ? "&big=1" : "");
}

int nf_url_upload_begin(char *out, const char *folder, const char *name, int replace)
{
    char enc_folder[3 * NF_PATH_MAX + 1], enc_name[3 * NF_NAME_MAX + 1];

    if (strlen(folder) > NF_PATH_MAX || strlen(name) > NF_NAME_MAX ||
        nf_encode(enc_folder, (int)sizeof enc_folder, folder) < 0 ||
        nf_encode(enc_name, (int)sizeof enc_name, name) < 0) {
        return -1;
    }
    return sprintf(out, FILES "upload-begin?path=%s&name=%s&replace=%d", enc_folder, enc_name, replace ? 1 : 0);
}

int nf_url_upload_data(char *out, unsigned long id, unsigned long offset)
{
    return sprintf(out, FILES "upload-data?id=%08lX&offset=%08lX", id, offset);
}

int nf_url_upload_finish(char *out, unsigned long id, unsigned long size, unsigned crc)
{
    return sprintf(out, FILES "upload-finish?id=%08lX&size=%08lX&crc=%04X", id, size, crc & 0xFFFFu);
}

int nf_url_upload_abort(char *out, unsigned long id)
{
    return sprintf(out, FILES "upload-abort?id=%08lX", id);
}

int nf_url_rename(char *out, const char *folder, const char *name, const char *new_name)
{
    char enc_folder[3 * NF_PATH_MAX + 1], enc_name[3 * NF_NAME_MAX + 1], enc_new[3 * NF_NAME_MAX + 1];

    if (strlen(folder) > NF_PATH_MAX || strlen(name) > NF_NAME_MAX || strlen(new_name) > NF_NAME_MAX ||
        nf_encode(enc_folder, (int)sizeof enc_folder, folder) < 0 ||
        nf_encode(enc_name, (int)sizeof enc_name, name) < 0 ||
        nf_encode(enc_new, (int)sizeof enc_new, new_name) < 0) {
        return -1;
    }
    return sprintf(out, FILES "rename?path=%s&name=%s&new=%s", enc_folder, enc_name, enc_new);
}

int nf_url_delete(char *out, const char *folder, const char *name)
{
    char enc_folder[3 * NF_PATH_MAX + 1], enc_name[3 * NF_NAME_MAX + 1];

    if (strlen(folder) > NF_PATH_MAX || strlen(name) > NF_NAME_MAX ||
        nf_encode(enc_folder, (int)sizeof enc_folder, folder) < 0 ||
        nf_encode(enc_name, (int)sizeof enc_name, name) < 0) {
        return -1;
    }
    return sprintf(out, FILES "delete?path=%s&name=%s", enc_folder, enc_name);
}

int nf_parse_change(const unsigned char *buf, int len)
{
    if (len != 2) {
        return NF_BAD_SIZE;
    }
    if (buf[0] != VERSION) {
        return NF_BAD_VERSION;
    }
    if (buf[1] > LAST_STATUS) {
        return NF_BAD_STATUS;
    }
    return buf[1];
}

#define INFO_REPLY 9
#define READ_HEADER 12
#define UPLOAD_REPLY 10

int nf_parse_info(const unsigned char *buf, int len, unsigned char *type, unsigned long *size, unsigned *crc)
{
    int rc = check_head(buf, len, INFO_REPLY, INFO_REPLY);

    if (rc != NF_OK) {
        return rc;
    }
    if (len != INFO_REPLY) {
        return NF_BAD_SIZE;
    }
    if (buf[2] > NF_TYPE_BIN) {
        return NF_BAD_ENTRY;
    }
    *type = buf[2];
    *size = le32(buf + 3);
    *crc = le16(buf + 7);
    return NF_OK;
}

int nf_parse_read(const unsigned char *buf, int len, unsigned long offset, unsigned count,
                  unsigned long *size, unsigned *got, const unsigned char **data)
{
    int rc;
    unsigned n;

    if (len == READ_HEADER && buf[0] == VERSION && buf[1] != 0 && buf[1] <= LAST_STATUS) {
        return buf[1];                  /* an error status: the offset field is echoed, not zero */
    }
    rc = check_head(buf, len, READ_HEADER, READ_HEADER + NF_CHUNK_BIG);
    if (rc != NF_OK) {
        return rc;
    }
    n = le16(buf + 10);
    if (le32(buf + 6) != offset) {
        return NF_BAD_ECHO;
    }
    if (count < 1 || count > NF_CHUNK_BIG || n > count) {
        return NF_BAD_COUNT;
    }
    if ((unsigned long)len != (unsigned long)READ_HEADER + n) {
        return NF_BAD_SIZE;
    }
    if (offset + n > le32(buf + 2)) {
        return NF_BAD_BOUNDS;
    }
    *size = le32(buf + 2);
    *got = n;
    *data = buf + READ_HEADER;
    return NF_OK;
}

int nf_parse_upload(const unsigned char *buf, int len, unsigned long *id, unsigned long *received)
{
    if (len != UPLOAD_REPLY) {
        return NF_BAD_SIZE;
    }
    if (buf[0] != VERSION) {
        return NF_BAD_VERSION;
    }
    if (buf[1] > LAST_STATUS) {
        return NF_BAD_STATUS;
    }
    *id = le32(buf + 2);
    *received = le32(buf + 6);
    return buf[1];
}

static int dos_char_ok(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || strchr("!#$%&'()-@^_`{}~", c) != NULL;
}

void nf_dos_name(char *out, const char *pc_name)
{
    const char *dot = strrchr(pc_name, '.');
    const char *end = pc_name + strlen(pc_name);
    char base[9], ext[4];
    int nb = 0, ne = 0;
    const char *p;

    if (dot != NULL && dot[1] == '\0') {
        end = dot;                              /* a trailing dot is no extension */
        dot = NULL;
    }
    for (p = pc_name; p < end; ++p) {
        char c = *p;
        int in_ext = dot != NULL && p > dot;

        if (p == dot) {
            continue;
        }
        if (c >= 'a' && c <= 'z') {
            c = (char)(c - 'a' + 'A');
        }
        if (c == ' ') {
            continue;
        }
        if (!dos_char_ok(c)) {
            c = '_';
        }
        if (in_ext) {
            if (ne < 3) {
                ext[ne++] = c;
            }
        } else if (nb < 8) {
            base[nb++] = c;
        }
    }
    base[nb] = '\0';
    ext[ne] = '\0';
    if (nb == 0) {
        strcpy(base, "FILE");
    }
    strcpy(out, base);
    if (ne > 0) {
        strcat(out, ".");
        strcat(out, ext);
    }
}

int nf_dos_name_valid(const char *name)
{
    static const char *const devices[] = {
        "CON", "PRN", "AUX", "NUL", "COM1", "COM2", "COM3", "COM4", "LPT1", "LPT2", "LPT3", "CLOCK$"
    };
    const char *dot = strchr(name, '.');
    size_t nbase, next = 0, i;
    char base[9];

    nbase = dot != NULL ? (size_t)(dot - name) : strlen(name);
    if (dot != NULL) {
        next = strlen(dot + 1);
        if (strchr(dot + 1, '.') != NULL || next < 1 || next > 3) {
            return 0;
        }
    }
    if (nbase < 1 || nbase > 8) {
        return 0;
    }
    for (i = 0; name[i] != '\0'; ++i) {
        if (name + i != dot && !dos_char_ok(name[i])) {
            return 0;
        }
    }
    memcpy(base, name, nbase);
    base[nbase] = '\0';
    for (i = 0; i < sizeof devices / sizeof devices[0]; ++i) {
        if (strcmp(base, devices[i]) == 0) {
            return 0;
        }
    }
    return 1;
}

int nf_pc_name_valid(const char *name)
{
    size_t n = strlen(name), i;

    if (n < 1 || n > NF_NAME_MAX || name[0] == '.') {
        return 0;
    }
    for (i = 0; i < n; ++i) {
        if ((unsigned char)name[i] < 0x20 || (unsigned char)name[i] > 0x7E || name[i] == '/' || name[i] == '\\') {
            return 0;
        }
    }
    return 1;
}

/* --- paths --- */

int nf_path_push(char *path, const char *name)
{
    size_t have = strlen(path), add = strlen(name) + 1;

    if (have + add > NF_PATH_MAX) {
        return 0;
    }
    memcpy(path + have, name, add - 1);
    path[have + add - 1] = '/';
    path[have + add] = '\0';
    return 1;
}

int nf_path_pop(char *path, char *leaf)
{
    size_t n = strlen(path), end, start;

    if (n == 0) {
        return 0;
    }
    end = n - 1;                      /* the final "/" */
    start = end;
    while (start > 0 && path[start - 1] != '/') {
        --start;
    }
    if (leaf != NULL) {
        memcpy(leaf, path + start, end - start);
        leaf[end - start] = '\0';
    }
    path[start] = '\0';
    return 1;
}

int nf_path_depth(const char *path)
{
    int d = 0;

    for (; *path != '\0'; ++path) {
        if (*path == '/') {
            ++d;
        }
    }
    return d;
}

/* --- the local side --- */

int nf_local_cmp(const void *a, const void *b)
{
    const NfLocal *x = (const NfLocal *)a, *y = (const NfLocal *)b;

    if (x->is_dir != y->is_dir) {
        return x->is_dir ? -1 : 1;
    }
    return strcmp(x->name, y->name);
}

int nf_dos_push(char *dir, const char *name)
{
    size_t have = strlen(dir), add = strlen(name);
    int slash = have > 0 && dir[have - 1] != '\\';

    if (have + (size_t)slash + add > NF_DOS_PATH_MAX) {
        return 0;
    }
    if (slash) {
        dir[have++] = '\\';
    }
    memcpy(dir + have, name, add + 1);
    return 1;
}

int nf_dos_pop(char *dir, char *leaf)
{
    size_t n = strlen(dir), i;

    for (i = n; i > 0 && dir[i - 1] != '\\'; --i) {
    }
    if (i == 0 || i == n) {                  /* no folder part, or already "C:\" */
        return 0;
    }
    if (leaf != NULL) {
        memcpy(leaf, dir + i, n - i + 1);
    }
    dir[i > 3 ? i - 1 : i] = '\0';          /* keep the "\" of a drive's root */
    return 1;
}

/* --- the viewer's cache --- */

#define ROW_BYTES (NF_COLS + 1)

void nf_cache_reset(NfCache *c)
{
    c->first = 0;
    c->n = 0;
    c->total = 0;
    c->valid = 0;
}

const char *nf_cache_line(const NfCache *c, unsigned n)
{
    if (!c->valid || n < c->first || n - c->first >= c->n) {
        return NULL;
    }
    return c->rows[n - c->first];
}

int nf_cache_ensure(NfCache *c, NfFetch fetch, void *ctx, unsigned top)
{
    int pass, rc;

    for (pass = 0; pass < 4; ++pass) {
        unsigned long end = (unsigned long)top + NF_TEXT_MAX;   /* exclusive */
        unsigned start = top, count = NF_TEXT_MAX;
        int mode = 0;                                           /* 0 rebuild, 1 append, 2 prepend */

        if (c->valid) {
            if (c->total == 0 || top >= c->total) {
                return NF_OK;                                   /* nothing to show there */
            }
            if (end > c->total) {
                end = c->total;
            }
            if (top >= c->first && end <= (unsigned long)c->first + c->n) {
                return NF_OK;
            }
            if (top >= c->first && top < c->first + c->n) {
                mode = 1;
                start = c->first + c->n;
            } else if (top < c->first && end >= c->first) {
                mode = 2;
                start = c->first >= NF_TEXT_MAX ? c->first - NF_TEXT_MAX : 0;
                count = c->first - start;
            }
        }
        rc = fetch(ctx, start, count, &c->scratch);
        if (rc != NF_OK) {
            return rc;                                          /* the cache is untouched */
        }
        if (c->scratch.first != start) {
            return NF_BAD_ECHO;
        }
        if (mode != 0 && c->scratch.total != c->total) {        /* the file changed length: start over */
            c->valid = 0;
            c->n = 0;
            continue;
        }
        if (mode == 0) {
            memcpy(c->rows, c->scratch.line, (size_t)c->scratch.count * ROW_BYTES);
            c->first = start;
            c->n = c->scratch.count;
            c->total = c->scratch.total;
            c->valid = 1;
        } else if (mode == 1) {
            if (c->n + c->scratch.count > NF_CACHE_LINES) {     /* drop the oldest lines */
                unsigned drop = c->n + c->scratch.count - NF_CACHE_LINES;

                memmove(c->rows, c->rows[drop], (size_t)(c->n - drop) * ROW_BYTES);
                c->first += drop;
                c->n -= drop;
            }
            memcpy(c->rows[c->n], c->scratch.line, (size_t)c->scratch.count * ROW_BYTES);
            c->n += c->scratch.count;
        } else {
            if (c->n + c->scratch.count > NF_CACHE_LINES) {     /* drop the newest lines */
                c->n = NF_CACHE_LINES - c->scratch.count;
            }
            memmove(c->rows[c->scratch.count], c->rows, (size_t)c->n * ROW_BYTES);
            memcpy(c->rows, c->scratch.line, (size_t)c->scratch.count * ROW_BYTES);
            c->first = start;
            c->n += c->scratch.count;
        }
    }
    return NF_BAD_COUNT;
}

/* --- panel rows --- */

void nf_format_row(char *out, const char *name, int is_dir, unsigned long size)
{
    char shown[28], num[24];
    size_t n = strlen(name);

    if (n > 27) {
        memcpy(shown, name, 26);
        shown[26] = '+';
        shown[27] = '\0';
    } else {
        memcpy(shown, name, n + 1);
    }
    if (is_dir) {
        strcpy(num, "<DIR>");
    } else if (size >= 10000000UL) {
        sprintf(num, "%luK", size / 1024UL);
    } else {
        sprintf(num, "%lu", size);
    }
    sprintf(out, " %-27s %8s ", shown, num);
}

void nf_format_range(char *out, unsigned first, unsigned count, unsigned total, int hidden)
{
    if (total == 0) {
        strcpy(out, "Empty");
    } else {
        sprintf(out, "Items %u-%u of %u", first + 1, first + count, total);
    }
    if (hidden > 0) {
        sprintf(out + strlen(out), " (%d hidden)", hidden);
    }
}

/* --- documents and AI writing --- */

static int doc_url(char *out, const char *verb, const char *folder, const char *name)
{
    char enc_folder[3 * NF_PATH_MAX + 1], enc_name[3 * NF_NAME_MAX + 1];

    if (strlen(folder) > NF_PATH_MAX || strlen(name) > NF_NAME_MAX ||
        nf_encode(enc_folder, (int)sizeof enc_folder, folder) < 0 ||
        nf_encode(enc_name, (int)sizeof enc_name, name) < 0) {
        return -1;
    }
    return sprintf(out, FILES "%s?path=%s&name=%s", verb, enc_folder, enc_name);
}

/* WordSmith/VGA (built with WS_STYLE_MARKS) asks the server to keep the three style marks of a document (docs/NET_PROTOCOL.md, 5). */
#ifdef WS_STYLE_MARKS
#define DOC_MARKS "&marks=1"
#else
#define DOC_MARKS ""
#endif

int nf_url_doc_info(char *out, const char *folder, const char *name)
{
    int n = doc_url(out, "doc-info", folder, name);

    if (n < 0) {
        return -1;
    }
    return n + sprintf(out + n, "&big=1" DOC_MARKS);
}

int nf_url_doc_read(char *out, const char *folder, const char *name, unsigned long offset, unsigned count)
{
    int n = doc_url(out, "doc-read", folder, name);

    if (n < 0) {
        return -1;
    }
    return n + sprintf(out + n, "&offset=%lX&count=%X&big=1" DOC_MARKS, offset, count);
}

#define AI "/c64/v1/ai-"

int nf_url_ai_begin(char *out, int mode, unsigned ilen)
{
    return sprintf(out, AI "begin?mode=%d&ilen=%X&big=1", mode, ilen);
}

int nf_url_ai_data(char *out, unsigned long id, unsigned long offset)
{
    return sprintf(out, AI "data?id=%08lX&offset=%08lX", id, offset);
}

int nf_url_ai_go(char *out, unsigned long id, unsigned long size, unsigned crc)
{
    return sprintf(out, AI "go?id=%08lX&size=%08lX&crc=%04X", id, size, crc & 0xFFFFu);
}

int nf_url_ai_read(char *out, unsigned long id, unsigned long offset, unsigned count)
{
    return sprintf(out, AI "read?id=%08lX&offset=%08lX&count=%X", id, offset, count);
}

int nf_url_ai_stop(char *out, unsigned long id)
{
    return sprintf(out, AI "stop?id=%08lX", id);
}

#define AI_READ_HEADER 11

int nf_parse_ai_read(const unsigned char *buf, int len, unsigned long offset, unsigned count,
                     unsigned *total, int *done, unsigned *got, unsigned *crc, const unsigned char **data)
{
    unsigned n, all;
    int i;

    if (len < AI_READ_HEADER || len > AI_READ_HEADER + NF_AI_READ_MAX) {
        return NF_BAD_SIZE;
    }
    if (buf[0] != VERSION) {
        return NF_BAD_VERSION;
    }
    if (buf[1] > LAST_STATUS) {
        return NF_BAD_STATUS;
    }
    if (buf[1] != 0) {
        /* an error: the whole header, "done" set, the answer's length and count and CRC zero */
        if (len != AI_READ_HEADER || buf[2] != 1) {
            return NF_BAD_STATUS;
        }
        for (i = 3; i < AI_READ_HEADER; ++i) {
            if (i != 5 && i != 6 && buf[i] != 0) {
                return NF_BAD_STATUS;
            }
        }
        return buf[1];
    }
    if (buf[2] > 1) {
        return NF_BAD_STATUS;
    }
    if (le16(buf + 5) != (unsigned)(offset & 0xFFFFu)) {
        return NF_BAD_ECHO;
    }
    n = le16(buf + 7);
    if (count < 1 || count > NF_AI_READ_MAX || n > count) {
        return NF_BAD_COUNT;
    }
    if (len != AI_READ_HEADER + (int)n) {
        return NF_BAD_SIZE;
    }
    all = le16(buf + 3);
    if (offset + n > all) {
        return NF_BAD_BOUNDS;
    }
    if (buf[2] == 1 && offset + n != all) {
        return NF_BAD_BOUNDS;               /* "the end" claimed, but there is more */
    }
    *total = all;
    *done = buf[2];
    *got = n;
    *crc = le16(buf + 9);
    *data = buf + AI_READ_HEADER;
    return NF_OK;
}
