#include <string.h>
#include "wsio.h"
#include "wsmarks.h"

unsigned ws_import(const unsigned char *raw, unsigned n, char *out, unsigned outmax, int *toobig)
{
    unsigned i, o = 0, col = 0;

    *toobig = 0;
    for (i = 0; i < n; ++i) {
        unsigned char c = raw[i];
        unsigned need = 1;

        if (c == 0x1A) {
            break;
        }
        if (c == '\r') {
            if (i + 1 < n && raw[i + 1] == '\n') {
                ++i;
            }
            c = '\n';
        }
        if (c == '\t') {
            need = WS_TAB - col % WS_TAB;
        }
        if (o + need > outmax) {
            *toobig = 1;
            return o;
        }
        if (c == '\n') {
            out[o++] = '\n';
            col = 0;
        } else if (c == '\t') {
            while (need-- > 0) {
                out[o++] = ' ';
                ++col;
            }
        } else if (c < 0x20 && !WS_IS_MARK(c)) {
            out[o++] = '?';
            ++col;
        } else {
            out[o++] = (char)c;
            ++col;
        }
    }
    return o;
}

unsigned ws_export(const char *src, unsigned n, char *dst)
{
    unsigned i, o = 0;

    for (i = 0; i < n; ++i) {
        if (src[i] == '\n') {
            dst[o++] = '\r';
        }
        dst[o++] = src[i];
    }
    return o;
}

static int name_char_ok(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
           strchr("!#$%&'()-@^_`{}~", c) != NULL;
}

int ws_path_ok(const char *path)
{
    static const char *const devices[] = {
        "CON", "PRN", "AUX", "NUL", "COM1", "COM2", "COM3", "COM4", "LPT1", "LPT2", "LPT3", "CLOCK$"
    };
    size_t n = strlen(path), i, start = 0, base = 0, ext = 0;
    int dot = 0;
    char name[9];

    if (n == 0 || n > 66) {
        return 0;
    }
    for (i = 0; i < n; ++i) {
        if (path[i] == '\\' || path[i] == '/' || path[i] == ':') {
            start = i + 1;
        }
        if (strchr("*?\"<>|,;=[] +", path[i]) != NULL) {
            return 0;
        }
    }
    if (start >= n) {
        return 0;
    }
    for (i = start; i < n; ++i) {
        if (path[i] == '.') {
            if (dot || i == start) {
                return 0;
            }
            dot = 1;
        } else if (!name_char_ok(path[i])) {
            return 0;
        } else if (dot) {
            ++ext;
        } else {
            ++base;
        }
    }
    if (base < 1 || base > 8 || ext > 3 || (dot && ext == 0)) {
        return 0;
    }
    memcpy(name, path + start, base);
    name[base] = '\0';
    for (i = 0; i < base; ++i) {
        if (name[i] >= 'a' && name[i] <= 'z') {
            name[i] = (char)(name[i] - 'a' + 'A');
        }
    }
    for (i = 0; i < sizeof devices / sizeof devices[0]; ++i) {
        if (strcmp(name, devices[i]) == 0) {
            return 0;
        }
    }
    return 1;
}

void ws_add_extension(char *name, unsigned max)
{
    size_t n = strlen(name), i, start = 0;

    for (i = 0; i < n; ++i) {
        if (name[i] == '\\' || name[i] == '/' || name[i] == ':') {
            start = i + 1;
        }
    }
    if (n == 0 || start >= n || memchr(name + start, '.', n - start) != NULL || n + 4 >= max) {
        return;
    }
    strcpy(name + n, ".TXT");
}

/* The characters of code page 437 above 0x7F as plain ASCII: sent as they are, an Epson-type printer takes
   0x80-0x9F for the control codes 0x00-0x1F (an A-ring, 0x8F, would switch on condensed print). */
static const char ascii_of_cp437[128] = {
    'C', 'u', 'e', 'a', 'a', 'a', 'a', 'c', 'e', 'e', 'e', 'i', 'i', 'i', 'A', 'A',
    'E', 'a', 'A', 'o', 'o', 'o', 'u', 'u', 'y', 'O', 'U', 'c', 'L', 'Y', 'P', 'f',
    'a', 'i', 'o', 'u', 'n', 'N', 'a', 'o', '?', '-', '-', '/', '/', '!', '<', '>',
    '.', ':', '#', '|', '|', '|', '|', '+', '+', '|', '|', '+', '+', '+', '+', '+',
    '+', '+', '+', '|', '-', '+', '|', '|', '+', '+', '+', '+', '|', '-', '+', '+',
    '+', '+', '+', '+', '+', '+', '+', '+', '+', '+', '+', '#', '#', '#', '#', '#',
    'a', 'B', 'G', 'p', 'S', 's', 'u', 't', 'F', 'T', 'O', 'd', '8', 'f', 'e', 'n',
    '=', '+', '>', '<', ' ', ' ', '/', '~', 'o', '.', '.', 'v', 'n', '2', '#', ' ',
};

char ws_print_char(unsigned char c)
{
    if (c < 0x20 || c == 0x7F) {
        return ' ';
    }
    return c < 0x80 ? (char)c : ascii_of_cp437[c - 0x80];
}

unsigned ws_print_line(const WsText *t, unsigned *pos, unsigned *line, char *out, int *last)
{
    char text[WS_PRINT_WIDTH + 1];
    unsigned o = 0, len, i;

    if (*line >= WS_PRINT_PAGE) {
        out[o++] = '\f';
        *line = 0;
    }
    *pos = ws_next_line(t, *pos, WS_PRINT_WIDTH, text, last);
#ifdef WS_STYLE_MARKS
    {   /* the style marks are not printed */
        unsigned from, to = 0;

        for (from = 0; text[from] != '\0'; ++from) {
            if (!WS_IS_MARK(text[from])) {
                text[to++] = text[from];
            }
        }
        text[to] = '\0';
    }
#endif
    len = (unsigned)strlen(text);
    if (len > 0) {                          /* an empty line is a bare CR LF: no spaces for a printer to skip over */
        memset(out + o, ' ', WS_PRINT_MARGIN);
        o += WS_PRINT_MARGIN;
    }
    for (i = 0; i < len; ++i) {
        out[o++] = ws_print_char((unsigned char)text[i]);
    }
    out[o++] = '\r';
    out[o++] = '\n';
    ++*line;
    return o;
}

int ws_prn_ready(unsigned char status)
{
    return (status & 0x80) != 0 && (status & 0x10) != 0 && (status & 0x29) == 0;
}

int ws_prn_failed(unsigned char status)
{
    return (status & 0x29) != 0;
}

const char *ws_prn_words(unsigned char status)
{
    if (status & 0x20) {
        return "out of paper";
    }
    if (status & 0x08) {
        return "I/O error";
    }
    if (status & 0x01) {
        return "does not answer";
    }
    if (!(status & 0x10)) {
        return "not on line";
    }
    if (!(status & 0x80)) {
        return "busy";
    }
    return "ready";
}
