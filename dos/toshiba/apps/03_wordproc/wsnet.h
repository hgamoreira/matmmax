/* X03 test B: WordSmith/XT's dealings with the PC server: documents (load, save, directory) and the AI key.
   Pure C on top of an injected transport, so that it is built natively and tested on Linux against the real
   host/ code; on the XT the transport is common/netget.c (Watt-32). Protocol: docs/NET_PROTOCOL.md (files/doc-*,
   upload-*, list, ai-*) and its XT additions 3 and 4 (big=1: a 60 KB text, pieces of 4096 bytes). */
#ifndef XT_WSNET_H
#define XT_WSNET_H

#include "nfproto.h"
#include "wstext.h"

/* The transport (netget.h's net_get and net_post): NF_OK or an NF_NET_* code; the reply body is put in buf. */
typedef int (*WnGet)(const char *path, unsigned char *buf, int bufmax, int *len);
typedef int (*WnPost)(const char *path, const unsigned char *body, int bodylen, unsigned char *buf, int bufmax, int *len);
void wn_set_transport(WnGet get, WnPost post);

/* Allocates the buffers (far heap). 1, or 0 if there is no memory. Call once. */
int wn_init(void);

/* Results, on top of nfproto.h's (0, the server's status 1..17, NF_NET_*, NF_BAD_*). */
#define WN_CANCELLED     200   /* Esc */
#define WN_E_CRC         201   /* what arrived does not match the checksum */
#define WN_UNCLEAR       202   /* save: no clear answer to the last request, the PC may have the file */
#define WN_NEEDS_REPLACE 203   /* save: the name exists on the PC and replace was not asked */
#define WN_FULL          204   /* AI: the text is full */
#define WN_NO_ANSWER     205   /* AI: the model did not answer for too long */
#define WN_E_BIG         206   /* the document does not fit in the buffer given */
#define WN_E_MEMORY      207

/* A message for any result code (never NULL). */
const char *wn_message(int code);

typedef struct {
    void (*progress)(const char *text);        /* a status line such as "Received 3000 of 13543 bytes (Esc cancels)" */
    int (*cancelled)(void);                    /* 1 if the user pressed Esc */
    int (*put)(const unsigned char *data, unsigned n);   /* AI: the answer's next piece (ASCII and LF); 0 = no room */
    void (*pause)(void);                       /* AI: wait a moment (about a quarter of a second) */
} WnHooks;

#define WN_FOLDER "documents/"                 /* the PC folder for documents */

/* Loads the PC document `name` into out (outmax bytes); *n is its length. The text is checked against the
   server's size and CRC-16 before it is handed over. */
int wn_load(const char *name, char *out, unsigned outmax, unsigned *n, const WnHooks *h);

/* Saves the whole text to the PC as `name`, as ASCII with LF. Returns WN_NEEDS_REPLACE (nothing sent) if the
   name exists and `replace` is 0. On any failure the PC is as it was, except WN_UNCLEAR. */
int wn_save(const WsText *t, const char *name, int replace, const WnHooks *h);

#ifdef WS_PROJECT
/* CodeSmith (XD44): makes sure code/<project>/<name> on the PC holds this text; *sent is 1 if it had to be uploaded. */
int wn_sync(const char *project, const char *name, const WsText *t, int *sent, const WnHooks *h);
#endif

typedef struct {
    char name[NF_NAME_MAX + 1];
    unsigned long size;
} WnEntry;
/* The files in the PC's documents folder, sorted as the server sorts them (folders left out); up to `max`
   entries. *n is how many; *more is 1 if there were more than fit. A missing folder is status 1. */
int wn_dir(WnEntry *list, int max, int *n, int *more);

/* The AI (modes NF_AI_WRITE, ASK, REWRITE, CONTINUE). The instruction (0-240 characters) and then the text
   t[from .. to) are sent; the answer is handed to h->put piece by piece (put returns 0 when the text is full:
   WN_FULL). *got is the number of answer bytes received, also after an error or Esc. The job is stopped on the PC
   on Esc and on any error. */
int wn_ai(int mode, const char *instruction, const WsText *t, unsigned from, unsigned to, const WnHooks *h,
          unsigned long *got);

#endif
