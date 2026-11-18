/* X02: the file protocol's requests and replies (docs/NET_PROTOCOL.md, /c64/v1/files/)
   for the XT: building the URLs, checking and decoding the `list` and `text`
   replies, the text viewer's line cache, and the panel row format. Pure C (no
   DOS, no network): built for DOS and, for the unit tests, for Linux. */
#ifndef XT_NFPROTO_H
#define XT_NFPROTO_H

#define NF_PATH_MAX     128    /* a folder path, including its trailing "/" */
#define NF_NAME_MAX     32
#ifndef NF_LIST_MAX
#define NF_LIST_MAX     18     /* entries per list reply (NetFiles/VGA builds with 23 and -DNF_BIG_PAGES: the requests then say big=1) */
#endif
#ifndef NF_TEXT_MAX
#define NF_TEXT_MAX     22     /* lines per text reply (27 with -DNF_BIG_PAGES) */
#endif
#ifdef NF_BIG_PAGES
#define NF_PAGES_TAIL   "&big=1"
#else
#define NF_PAGES_TAIL   ""
#endif
#define NF_COLS         80     /* the XT's screen; text is asked for at this width */
#define NF_WIDTH_HEX    "50"   /* 80, in hex, for the `width` parameter */
#define NF_LIST_REPLY_MAX (9 + NF_LIST_MAX * (6 + NF_NAME_MAX))
#define NF_TEXT_REPLY_MAX (7 + NF_TEXT_MAX * (1 + NF_COLS))

#define NF_TYPE_DIR 0
#define NF_TYPE_TEXT 1
#define NF_TYPE_PRG 2
#define NF_TYPE_BIN 3

/* Result codes of the parsers and of a fetch: 0 is success, 1..17 is the
   server's own status (the body status codes of NET_PROTOCOL.md), 100.. is a
   transport problem, and negative means the reply itself was malformed. */
#define NF_OK            0
#define NF_NET_NOROUTE   100
#define NF_NET_NOCONNECT 101
#define NF_NET_TIMEOUT   102
#define NF_NET_BADHTTP   103   /* not HTTP, or the head is too big */
#define NF_NET_HTTPSTATUS 104  /* an HTTP status other than 200 */
#define NF_NET_TOOBIG    105   /* the body is larger than the protocol allows */
#define NF_NET_NOSTACK   106   /* the network stack did not start */
#define NF_NET_CLOSED    107   /* connected, but the server closed the connection without answering */
#define NF_BAD_SIZE      (-1)  /* too short, too long, or not the size it claims */
#define NF_BAD_VERSION   (-2)
#define NF_BAD_ECHO      (-3)  /* start/line is not what was asked */
#define NF_BAD_COUNT     (-4)
#define NF_BAD_BOUNDS    (-5)  /* start + count beyond total */
#define NF_BAD_ENTRY     (-6)  /* unknown type */
#define NF_BAD_NAME      (-7)
#define NF_BAD_LINE      (-8)
#define NF_BAD_TRAILING  (-9)
#define NF_BAD_STATUS    (-10) /* an error status with more than the header, or an unknown status */

typedef struct {
    unsigned char type;
    unsigned long size;
    char name[NF_NAME_MAX + 1];
} NfEntry;

typedef struct {
    unsigned total, start, count, hidden;
    NfEntry e[NF_LIST_MAX];
} NfList;

typedef struct {
    unsigned total, first, count;
    char line[NF_TEXT_MAX][NF_COLS + 1];
} NfText;

/* --- requests --- */

/* Percent-encodes s (everything but A-Z a-z 0-9 - . _ ~) into out; returns
   the length, or -1 if out (of size max, including the NUL) is too small. */
int nf_encode(char *out, int max, const char *s);
/* "/c64/v1/files/list?path=..&start=..&count=.." in out (at least 400 bytes). */
int nf_url_list(char *out, const char *path, unsigned start, unsigned count);
/* "/c64/v1/files/text?path=..&line=..&count=..&width=50"; path is a file. */
int nf_url_text(char *out, const char *path, unsigned line, unsigned count);

/* --- replies (checked completely before anything is used) --- */

/* Return NF_OK, a server status (1..17), or a negative NF_BAD_* code; out is
   only to be used on NF_OK. `start`/`line` and `count` are what was asked. */
int nf_parse_list(const unsigned char *buf, int len, unsigned start, unsigned count, NfList *out);
int nf_parse_text(const unsigned char *buf, int len, unsigned line, unsigned count, NfText *out);

/* A short message for a result code, e.g. "NOT FOUND ON THE SERVER" (never NULL). */
const char *nf_message(int code);
/* The seconds named in the time-out message (net_start sets it from wait=; 5 by default). */
extern unsigned nf_wait_seconds;

/* --- download and upload (step 2) --- */

#define NF_CHUNK_MAX 1000          /* data bytes per read reply or upload POST (the protocol's own limit) */
#define NF_CHUNK_BIG 4096          /* ... with the XT's optional larger piece (big=1; raw POST) */
#define NF_FILE_MAX  (16UL * 1024UL * 1024UL)   /* the server's limit, both ways */
#define NF_UPLOAD_EXISTS 8         /* status: the name exists and replace was not asked */
#define NF_UPLOAD_CHECK  10        /* status: size or CRC-16 did not match, nothing saved */

/* CRC-16/CCITT-FALSE (polynomial 1021h, start FFFFh, no reflection, no final XOR), as
   the server computes it; pass the previous result (0xFFFF to start) to continue. */
unsigned nf_crc16(unsigned crc, const unsigned char *data, unsigned len);

/* URLs (out at least 640 bytes). Names and paths are percent-encoded; they return the
   length, or -1 if a path or name is too long. */
int nf_url_info(char *out, const char *path);
int nf_url_read(char *out, const char *path, unsigned long offset, unsigned count);   /* count over 1000 adds &big=1 */
int nf_url_upload_begin(char *out, const char *folder, const char *name, int replace);
int nf_url_upload_data(char *out, unsigned long id, unsigned long offset);
int nf_url_upload_finish(char *out, unsigned long id, unsigned long size, unsigned crc);
int nf_url_upload_abort(char *out, unsigned long id);

/* `info`: type, size and CRC-16 of a file (9 bytes). Return NF_OK, a server status or a
   negative NF_BAD_* code; the outputs are only to be used on NF_OK. */
int nf_parse_info(const unsigned char *buf, int len, unsigned char *type, unsigned long *size, unsigned *crc);
/* `read` at `offset` for `count` bytes asked: *size is the file's size now, *got the data
   bytes in this reply (at most `count`), *data points into buf. The data length must be
   exactly what the header says and the offset the one asked for. */
int nf_parse_read(const unsigned char *buf, int len, unsigned long offset, unsigned count,
                  unsigned long *size, unsigned *got, const unsigned char **data);
/* The 10-byte reply of the four upload requests. Returns NF_OK, a server status (8 exists, 9 out
   of order, 10 check failed, 11 unknown upload, 12 busy, ...) or a negative NF_BAD_* code. */
int nf_parse_upload(const unsigned char *buf, int len, unsigned long *id, unsigned long *received);

/* rename and delete on the PC (step 3). The reply of both is 2 bytes: version and status. */
int nf_url_rename(char *out, const char *folder, const char *name, const char *new_name);
int nf_url_delete(char *out, const char *folder, const char *name);
/* Returns NF_OK, a server status (1 not found, 3 bad request, 4 denied, 8 the new name exists,
   13 the folder is not empty, ...) or a negative NF_BAD_* code. */
int nf_parse_change(const unsigned char *buf, int len);

/* A DOS 8.3 name for a PC file name: upper case, spaces dropped, characters DOS does not
   allow become "_", base cut to 8 and extension to 3 ("FILE" if nothing is left). out holds 13. */
void nf_dos_name(char *out, const char *pc_name);
/* 1 if `name` (already upper case) is a valid 8.3 name that is not a DOS device (CON, NUL...). */
int nf_dos_name_valid(const char *name);
/* 1 if the server will accept `name` for an upload: 1-32 printable ASCII characters, no "/" or
   "\\", not starting with ".". */
int nf_pc_name_valid(const char *name);

/* --- paths on the server --- */

/* Appends "name/" to the folder path (which ends in "/" or is empty); returns
   0 and leaves path unchanged if the result would be over NF_PATH_MAX. */
int nf_path_push(char *path, const char *name);
/* Removes the last folder; copies its name (without "/") to leaf (at least
   NF_NAME_MAX + 1) when leaf is not NULL. Returns 0 at the root. */
int nf_path_pop(char *path, char *leaf);
/* Number of folders in the path ("" is 0). */
int nf_path_depth(const char *path);

/* --- the local (DOS) side --- */

#define NF_DOS_PATH_MAX 66   /* "C:" and DOS's 64 */

typedef struct {
    char name[13];           /* 8.3 */
    unsigned long size;
    unsigned char is_dir;
} NfLocal;

/* Folders first, then by name (qsort order). */
int nf_local_cmp(const void *a, const void *b);
/* "C:\XT" + "SUB" -> "C:\XT\SUB" ("C:\" + "XT" -> "C:\XT"); 0 and unchanged if over 66. */
int nf_dos_push(char *dir, const char *name);
/* "C:\XT\SUB" -> "C:\XT" and leaf "SUB"; "C:\XT" -> "C:\"; returns 0 at a drive's root. */
int nf_dos_pop(char *dir, char *leaf);

/* --- the text viewer's cache of lines --- */

#define NF_CACHE_LINES (3 * NF_TEXT_MAX)

typedef struct {
    char rows[NF_CACHE_LINES][NF_COLS + 1];
    unsigned first;      /* the file line number of rows[0] */
    unsigned n;          /* lines held */
    unsigned total;      /* lines in the file, as of the last fetch */
    int valid;           /* 0 until the first fetch */
    NfText scratch;      /* the reply being merged */
} NfCache;

/* Fetches `count` lines from `first` (the cache's callback): returns NF_OK or a code. */
typedef int (*NfFetch)(void *ctx, unsigned first, unsigned count, NfText *out);

void nf_cache_reset(NfCache *c);
/* Makes lines top..top+21 (as far as the file has them) available, fetching
   only what is missing, next to what is held. On a failure the cache is as it
   was and the code is returned. If the file has changed length, the cache is
   rebuilt from `top`. */
int nf_cache_ensure(NfCache *c, NfFetch fetch, void *ctx, unsigned top);
/* The text of file line n, or NULL if it is not held. */
const char *nf_cache_line(const NfCache *c, unsigned n);

/* --- panel rows --- */

#define NF_ROW_WIDTH 38      /* the inside of a 40-column panel */
/* Writes exactly NF_ROW_WIDTH characters and a NUL: " name  size ", the name
   cut with "+" if it is over 27 characters, "<DIR>" for a folder, the size in
   bytes (in K from 10,000,000). out holds NF_ROW_WIDTH + 1 bytes. */
void nf_format_row(char *out, const char *name, int is_dir, unsigned long size);
/* "Items 19-36 of 40 (2 hidden)" into out (at least 48 bytes); hidden < 0 omits it.
   `first` is zero-based; with total 0 it reads "Empty". */
void nf_format_range(char *out, unsigned first, unsigned count, unsigned total, int hidden);

/* --- documents and AI writing for WordSmith/XT (X03 test B; docs/NET_PROTOCOL.md, XT additions 4) --- */

#define NF_DOC_MAX      61440      /* the biggest document: WordSmith/XT's text buffer */
#define NF_AI_ILEN_MAX  240        /* the instruction */
#define NF_AI_READ_MAX  512        /* answer bytes per ai-read */
#define NF_AI_WRITE 1
#define NF_AI_ASK 2
#define NF_AI_REWRITE 3
#define NF_AI_CONTINUE 4
#define NF_STATUS_AI_UNKNOWN 14    /* the job is unknown (idle too long) */
#define NF_STATUS_AI_BUSY    15
#define NF_STATUS_AI_NOMODEL 16    /* Ollama or the model is not available */
#define NF_STATUS_AI_FAILED  17    /* the model failed before it wrote anything */

/* doc-info and doc-read always ask for big=1 (up to 61,440 bytes; pieces up to 4096). `folder` ends in "/".
   out is at least 640 bytes; they return the length, or -1 if a folder or name is too long. */
int nf_url_doc_info(char *out, const char *folder, const char *name);
int nf_url_doc_read(char *out, const char *folder, const char *name, unsigned long offset, unsigned count);
/* The replies are those of `info` (nf_parse_info) and `read` (nf_parse_read). */

/* ai-begin (big=1: a 60 KB text and 4096-byte pieces), ai-data (POST; the raw piece), ai-go, ai-read, ai-stop. */
int nf_url_ai_begin(char *out, int mode, unsigned ilen);
int nf_url_ai_data(char *out, unsigned long id, unsigned long offset);
int nf_url_ai_go(char *out, unsigned long id, unsigned long size, unsigned crc);
int nf_url_ai_read(char *out, unsigned long id, unsigned long offset, unsigned count);
int nf_url_ai_stop(char *out, unsigned long id);
/* The replies of ai-begin, ai-data, ai-go and ai-stop are the upload reply (nf_parse_upload). */

/* The reply of ai-read at `offset` for `count` bytes asked. On NF_OK: *total is the answer's length so far,
   *done 1 if this reply reaches the end of the finished answer, *got the bytes in this reply, *crc the
   server's CRC-16 of the answer's first offset + got bytes (the caller checks it against its own running
   CRC before using the data), *data points into buf. An error status must be the 11-byte header and nothing
   else. */
int nf_parse_ai_read(const unsigned char *buf, int len, unsigned long offset, unsigned count,
                     unsigned *total, int *done, unsigned *got, unsigned *crc, const unsigned char **data);

#endif
