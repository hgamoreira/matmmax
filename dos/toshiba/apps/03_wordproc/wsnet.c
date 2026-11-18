#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "wsio.h"
#include "wsnet.h"
#include "wsmarks.h"

#define PIECE NF_CHUNK_BIG                    /* data bytes per request */
#define REPLY_MAX (12 + PIECE + 16)
#define AI_IDLE_POLLS 240                     /* empty ai-reads (about a quarter of a second apart) before giving up */

static WnGet do_get;
static WnPost do_post;
static unsigned char *reply, *chunk;          /* far heap: the reply, and a piece of text on its way */
static char url[640];

void wn_set_transport(WnGet get, WnPost post)
{
    do_get = get;
    do_post = post;
}

int wn_init(void)
{
    if (reply == NULL) {
        reply = (unsigned char *)malloc(REPLY_MAX);
        chunk = (unsigned char *)malloc(PIECE);
    }
    return reply != NULL && chunk != NULL;
}

const char *wn_message(int code)
{
    switch (code) {
    case WN_CANCELLED:     return "Cancelled";
    case WN_E_CRC:         return "The data arrived damaged (checksum differs): nothing was kept";
    case WN_UNCLEAR:       return "No clear answer from the PC: the document may or may not be saved (F4 shows)";
    case WN_NEEDS_REPLACE: return "A document with that name already exists on the PC";
    case WN_FULL:          return "The text is full";
    case WN_NO_ANSWER:     return "The AI did not answer for too long";
    case WN_E_BIG:         return "That document is too big for the text (60 KB)";
    case WN_E_MEMORY:      return "Not enough memory";
    case 1:                return "Not found on the PC";
    case 5:                return "That PC file is not a text file";
    case 6:                return "That document is too big for the text (60 KB)";
    default:               return nf_message(code);
    }
}

/* GET a url made by one of the nf_url_ functions (n is its length or -1). */
static int get(int n, int *len)
{
    if (n < 0) {
        return 3;                              /* a name over the protocol's limits */
    }
    *len = 0;
    return do_get(url, reply, REPLY_MAX, len);
}

static void say(const WnHooks *h, const char *text)
{
    if (h != NULL && h->progress != NULL) {
        h->progress(text);
    }
}

static int cancelled(const WnHooks *h)
{
    return h != NULL && h->cancelled != NULL && h->cancelled();
}

/* --- load --- */

int wn_load(const char *name, char *out, unsigned outmax, unsigned *n, const WnHooks *h)
{
    unsigned char type;
    unsigned long size, offset = 0, total;
    unsigned crc, run = 0xFFFFu, got, want;
    const unsigned char *data;
    char text[64];
    int rc, len;

    *n = 0;
    say(h, "Asking the PC...");
    rc = get(nf_url_doc_info(url, WN_FOLDER, name), &len);
    if (rc == NF_OK) {
        rc = nf_parse_info(reply, len, &type, &size, &crc);
    }
    if (rc != NF_OK) {
        return rc;
    }
    if (size > NF_DOC_MAX || size > outmax) {
        return WN_E_BIG;
    }
    while (offset < size) {
        if (cancelled(h)) {
            return WN_CANCELLED;
        }
        sprintf(text, "Received %lu of %lu bytes (Esc cancels)", offset, size);
        say(h, text);
        want = size - offset > PIECE ? PIECE : (unsigned)(size - offset);
        rc = get(nf_url_doc_read(url, WN_FOLDER, name, offset, want), &len);
        if (rc == NF_OK) {
            rc = nf_parse_read(reply, len, offset, want, &total, &got, &data);
        }
        if (rc == NF_OK && total != size) {
            rc = 7;                            /* the document changed on the PC while we read it */
        }
        if (rc == NF_OK && got == 0) {
            rc = NF_BAD_COUNT;
        }
        if (rc != NF_OK) {
            return rc;
        }
        memcpy(out + (unsigned)offset, data, got);
        run = nf_crc16(run, data, got);
        offset += got;
    }
    if (run != crc) {
        return WN_E_CRC;
    }
    *n = (unsigned)size;
    return NF_OK;
}

/* --- save --- */

/* The text from..from+n as bytes for the PC: ASCII with LF (what the PC converts to anyway). */
#ifdef WS_STYLE_MARKS
static int keep_marks;                         /* a document saved on the PC keeps its style marks; the AI gets spaces for them */
#endif

static void ascii_piece(const WsText *t, unsigned from, unsigned n, unsigned char *out)
{
    unsigned i;
    char c;

    for (i = 0; i < n; ++i) {
        c = ws_at(t, from + i);
#ifdef WS_STYLE_MARKS
        if (keep_marks && WS_IS_MARK(c)) {
            out[i] = (unsigned char)c;
            continue;
        }
#endif
        if (c != '\n' && ((unsigned char)c < 0x20 || (unsigned char)c > 0x7E)) {
            c = ws_print_char((unsigned char)c);
        }
        out[i] = (unsigned char)c;
    }
}

static int post(int n, const unsigned char *body, int body_len, int *len)
{
    if (n < 0) {
        return 3;
    }
    *len = 0;
    return do_post(url, body, body_len, reply, REPLY_MAX, len);
}

static void upload_abort(unsigned long id)
{
    int len;

    get(nf_url_upload_abort(url, id), &len);   /* best effort: the server also expires it */
}

int wn_save(const WsText *t, const char *name, int replace, const WnHooks *h)
{
    unsigned long size = ws_len(t), offset = 0, id, received, reply_id, reply_received;
    unsigned crc = 0xFFFFu, n;
    char text[64];
    int rc, len;

#ifdef WS_STYLE_MARKS
    keep_marks = 1;
#endif
    say(h, "Asking the PC...");
    rc = get(nf_url_upload_begin(url, WN_FOLDER, name, replace), &len);
    if (rc == NF_OK) {
        rc = nf_parse_upload(reply, len, &id, &received);
    }
    if (rc == NF_UPLOAD_EXISTS && !replace) {
        rc = WN_NEEDS_REPLACE;
    }
    if (rc != NF_OK) {
        return rc;
    }
    if (received != 0) {
        upload_abort(id);
        return NF_BAD_COUNT;
    }
    while (offset < size) {
        if (cancelled(h)) {
            rc = WN_CANCELLED;
            goto fail;
        }
        sprintf(text, "Sent %lu of %lu bytes (Esc cancels)", offset, size);
        say(h, text);
        n = size - offset > PIECE ? PIECE : (unsigned)(size - offset);
        ascii_piece(t, (unsigned)offset, n, chunk);
        rc = post(nf_url_upload_data(url, id, offset), chunk, (int)n, &len);
        if (rc == NF_OK) {
            rc = nf_parse_upload(reply, len, &reply_id, &reply_received);
            if (rc == NF_OK && (reply_id != id || reply_received != offset + n)) {
                rc = NF_BAD_ECHO;
            }
        }
        if (rc != NF_OK) {
            goto fail;
        }
        crc = nf_crc16(crc, chunk, n);
        offset += n;
    }
    say(h, "The PC is checking and saving the document...");
    rc = get(nf_url_upload_finish(url, id, size, crc), &len);
    if (rc == NF_OK) {
        rc = nf_parse_upload(reply, len, &reply_id, &reply_received);
        if (rc == NF_OK && (reply_id != id || reply_received != size)) {
            rc = WN_UNCLEAR;
        }
    }
    if (rc == NF_OK) {
        return NF_OK;
    }
    if (rc >= NF_NET_NOROUTE || rc < 0) {
        return WN_UNCLEAR;                     /* no reply, or a damaged one: the document may be in place */
    }
    return rc;                                 /* the PC answered with a status: it kept nothing */

fail:
    upload_abort(id);
    return rc;
}

#ifdef WS_PROJECT
/* CodeSmith's mirror of a program (XD44). This is wn_save with the folder as a parameter, kept apart so that WordSmith's own code
   is exactly what it was. */
static int save_in(const char *folder, const WsText *t, const char *name, int replace, const WnHooks *h)
{
    unsigned long size = ws_len(t), offset = 0, id, received, reply_id, reply_received;
    unsigned crc = 0xFFFFu, n;
    char text[64];
    int rc, len;

#ifdef WS_STYLE_MARKS
    keep_marks = 0;
#endif
    say(h, "Asking the PC...");
    rc = get(nf_url_upload_begin(url, folder, name, replace), &len);
    if (rc == NF_OK) {
        rc = nf_parse_upload(reply, len, &id, &received);
    }
    if (rc == NF_UPLOAD_EXISTS && !replace) {
        rc = WN_NEEDS_REPLACE;
    }
    if (rc != NF_OK) {
        return rc;
    }
    if (received != 0) {
        upload_abort(id);
        return NF_BAD_COUNT;
    }
    while (offset < size) {
        if (cancelled(h)) {
            rc = WN_CANCELLED;
            goto fail;
        }
        sprintf(text, "Sent %lu of %lu bytes (Esc cancels)", offset, size);
        say(h, text);
        n = size - offset > PIECE ? PIECE : (unsigned)(size - offset);
        ascii_piece(t, (unsigned)offset, n, chunk);
        rc = post(nf_url_upload_data(url, id, offset), chunk, (int)n, &len);
        if (rc == NF_OK) {
            rc = nf_parse_upload(reply, len, &reply_id, &reply_received);
            if (rc == NF_OK && (reply_id != id || reply_received != offset + n)) {
                rc = NF_BAD_ECHO;
            }
        }
        if (rc != NF_OK) {
            goto fail;
        }
        crc = nf_crc16(crc, chunk, n);
        offset += n;
    }
    say(h, "The PC is checking and saving the document...");
    rc = get(nf_url_upload_finish(url, id, size, crc), &len);
    if (rc == NF_OK) {
        rc = nf_parse_upload(reply, len, &reply_id, &reply_received);
        if (rc == NF_OK && (reply_id != id || reply_received != size)) {
            rc = WN_UNCLEAR;
        }
    }
    if (rc == NF_OK) {
        return NF_OK;
    }
    if (rc >= NF_NET_NOROUTE || rc < 0) {
        return WN_UNCLEAR;                     /* no reply, or a damaged one: the document may be in place */
    }
    return rc;                                 /* the PC answered with a status: it kept nothing */

fail:
    upload_abort(id);
    return rc;
}


/* code/<project>/<name> on the PC is made to hold this text. The PC is asked for the size and CRC-16 of the file it has; if they
   are those of this text (as ASCII with LF) nothing is sent (*sent = 0), else the text is uploaded, replacing the file (*sent = 1). */
int wn_sync(const char *project, const char *name, const WsText *t, int *sent, const WnHooks *h)
{
    unsigned long size = ws_len(t), offset = 0, pc_size;
    unsigned crc = 0xFFFFu, pc_crc, n;
    unsigned char type;
    char path[NF_PATH_MAX + NF_NAME_MAX + 8], folder[NF_PATH_MAX];
    int rc, len;

    *sent = 0;
#ifdef WS_STYLE_MARKS
    keep_marks = 0;
#endif
    sprintf(folder, "code/%.40s/", project);
    sprintf(path, "%s%.40s", folder, name);
    while (offset < size) {
        n = size - offset > PIECE ? PIECE : (unsigned)(size - offset);
        ascii_piece(t, (unsigned)offset, n, chunk);
        crc = nf_crc16(crc, chunk, n);
        offset += n;
    }
    rc = get(nf_url_info(url, path), &len);
    if (rc == NF_OK) {
        rc = nf_parse_info(reply, len, &type, &pc_size, &pc_crc);
    }
    if (rc == NF_OK && pc_size == size && pc_crc == crc) {
        return NF_OK;                          /* the PC has this very text already */
    }
    if (rc >= NF_NET_NOROUTE) {
        return rc;                             /* no answer: do not try to send */
    }
    *sent = 1;
    return save_in(folder, t, name, 1, h);
}
#endif

/* --- directory --- */

int wn_dir(WnEntry *list, int max, int *n, int *more)
{
    static NfList page;
    unsigned start = 0, i;
    int rc, len;

    *n = 0;
    *more = 0;
    for (;;) {
        rc = get(nf_url_list(url, WN_FOLDER, start, NF_LIST_MAX), &len);
        if (rc == NF_OK) {
            rc = nf_parse_list(reply, len, start, NF_LIST_MAX, &page);
        }
        if (rc != NF_OK) {
            return rc;
        }
        for (i = 0; i < page.count; ++i) {
            if (page.e[i].type == NF_TYPE_DIR) {
                continue;
            }
            if (*n >= max) {
                *more = 1;
                return NF_OK;
            }
            strcpy(list[*n].name, page.e[i].name);
            list[*n].size = page.e[i].size;
            ++*n;
        }
        start += page.count;
        if (page.count == 0 || start >= page.total) {
            return NF_OK;
        }
    }
}

/* --- the AI --- */

static void ai_stop(unsigned long id)
{
    int len;

    get(nf_url_ai_stop(url, id), &len);        /* best effort */
}

#ifdef WN_MODEL
/* The model the user chose (XD92): the file the MODELCFG variable names holds "model=name"; the request then starts its context with the line "#model=name",
   which the server takes off and checks against Ollama. No variable or no file: no line, the PC's own model answers (and the bytes sent are as before). */
static char *model_line;                    /* malloc'd: the near data of the 16-bit programs is full */
static unsigned model_len;

static void model_load(void)
{
    const char *path = getenv("MODELCFG");
    FILE *f;
    char line[100];
    size_t n;

    model_len = 0;
    if (path == NULL || path[0] == '\0') {
        return;
    }
    if (model_line == NULL && (model_line = (char *)malloc(80)) == NULL) {
        return;
    }
    if ((f = fopen(path, "r")) == NULL) {
        return;
    }
    if (fgets(line, sizeof(line), f) != NULL && strncmp(line, "model=", 6) == 0) {
        n = strcspn(line + 6, "\r\n ");
        if (n > 0 && n <= 64) {
            memcpy(model_line, "#model=", 7);
            memcpy(model_line + 7, line + 6, n);
            model_line[7 + n] = '\n';
            model_len = (unsigned)(8 + n);
        }
    }
    fclose(f);
}
#define MODEL_LEN model_len
#else
#define MODEL_LEN 0
#endif

/* The next piece of the job's byte stream: the instruction, (the model line,) then the text t[from .. to) as ASCII. */
static unsigned ai_piece(const char *instruction, unsigned ilen, const WsText *t, unsigned from,
                         unsigned long offset, unsigned long total)
{
    unsigned long left = total - offset;
    unsigned n = left > PIECE ? PIECE : (unsigned)left, done = 0, k;

    if (offset < ilen) {
        k = (unsigned)(ilen - offset);
        if (k > n) {
            k = n;
        }
        memcpy(chunk, instruction + (unsigned)offset, k);
        done = k;
    }
#ifdef WN_MODEL
    if (done < n && offset + done < (unsigned long)ilen + model_len) {
        k = (unsigned)(ilen + model_len - (offset + done));
        if (k > n - done) {
            k = n - done;
        }
        memcpy(chunk + done, model_line + (unsigned)(offset + done - ilen), k);
        done += k;
    }
#endif
    if (done < n) {
        unsigned long at = offset + done - ilen - MODEL_LEN;

        ascii_piece(t, from + (unsigned)at, n - done, chunk + done);
    }
    return n;
}

static int ai_check(int rc, unsigned long id, unsigned long want_received, int len)
{
    unsigned long reply_id, received;

    if (rc == NF_OK) {
        rc = nf_parse_upload(reply, len, &reply_id, &received);
        if (rc == NF_OK && (reply_id != id || received != want_received)) {
            rc = NF_BAD_ECHO;
        }
    }
    return rc;
}

int wn_ai(int mode, const char *instruction, const WsText *t, unsigned from, unsigned to, const WnHooks *h,
          unsigned long *got)
{
    unsigned ilen = (unsigned)strlen(instruction), count, total_now, piece, server_crc, send_crc = 0xFFFFu;
    unsigned long id, received, offset = 0, total, answer = 0;
    unsigned answer_crc = 0xFFFFu, ask;
    const unsigned char *data;
    int rc, len, done, idle = 0, tries;
    char text[64];

    *got = 0;
#ifdef WS_STYLE_MARKS
    keep_marks = 0;
#endif
    if (ilen > NF_AI_ILEN_MAX || to < from) {
        return 3;
    }
#ifdef WN_MODEL
    model_load();
#endif
    total = (unsigned long)ilen + MODEL_LEN + (to - from);
    if (total > NF_AI_ILEN_MAX + (unsigned long)NF_DOC_MAX) {
        return WN_E_BIG;
    }
    say(h, "AI: sending...");
    rc = get(nf_url_ai_begin(url, mode, ilen), &len);
    if (rc == NF_OK) {
        rc = nf_parse_upload(reply, len, &id, &received);
    }
    if (rc != NF_OK) {
        return rc;
    }
    while (offset < total) {
        if (cancelled(h)) {
            rc = WN_CANCELLED;
            goto fail;
        }
        piece = ai_piece(instruction, ilen, t, from, offset, total);
        rc = post(nf_url_ai_data(url, id, offset), chunk, (int)piece, &len);
        rc = ai_check(rc, id, offset + piece, len);
        if (rc != NF_OK) {
            goto fail;
        }
        send_crc = nf_crc16(send_crc, chunk, piece);
        offset += piece;
    }
    rc = get(nf_url_ai_go(url, id, total, send_crc), &len);
    rc = ai_check(rc, id, total, len);
    if (rc != NF_OK) {
        goto fail;
    }

    say(h, "AI: thinking... (Esc stops)");
    for (;;) {
        if (cancelled(h)) {
            rc = WN_CANCELLED;
            goto fail;
        }
        ask = NF_AI_READ_MAX;
        for (tries = 0; ; ++tries) {           /* an ai-read can safely be asked again */
            rc = get(nf_url_ai_read(url, id, answer, ask), &len);
            if (rc == NF_OK) {
                rc = nf_parse_ai_read(reply, len, answer, ask, &total_now, &done, &count, &server_crc, &data);
            }
            if (rc < NF_NET_NOROUTE || tries >= 1) {
                break;
            }
        }
        if (rc != NF_OK) {
            goto fail;
        }
        if (count > 0) {
            unsigned new_crc = nf_crc16(answer_crc, data, count);

            if (new_crc != server_crc) {
                rc = WN_E_CRC;
                goto fail;
            }
            answer_crc = new_crc;
            answer += count;
            *got = answer;
            if (!h->put(data, count)) {
                rc = WN_FULL;
                goto fail;
            }
            idle = 0;
            sprintf(text, "AI: writing... %lu characters (Esc stops)", answer);
            say(h, text);
        }
        if (done) {
            return NF_OK;
        }
        if (count == 0) {
            if (++idle > AI_IDLE_POLLS) {
                rc = WN_NO_ANSWER;
                goto fail;
            }
            if (h->pause != NULL) {
                h->pause();
            }
        }
    }

fail:
    ai_stop(id);
    return rc;
}
