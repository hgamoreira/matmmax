/* X03 WordSmith/XT: converting text to and from DOS files, DOS names, and print formatting. Pure C. */
#ifndef XT_WSIO_H
#define XT_WSIO_H

#include "wstext.h"

#define WS_TAB 8                 /* tab stops when a file is loaded */
#define WS_PRINT_WIDTH 70
#define WS_PRINT_MARGIN 8        /* spaces before every printed line */
#define WS_PRINT_PAGE 60         /* lines per page */

/* A DOS file's bytes to the text: CR LF, a lone CR and a lone LF become one LF (a paragraph end); a tab
   becomes spaces up to the next tab stop (counted from the paragraph start); Ctrl+Z (the DOS end-of-file
   mark) ends the text; every other control character becomes "?". Returns the length written to out; *toobig
   is set to 1 if outmax would be exceeded (the result is then incomplete). */
unsigned ws_import(const unsigned char *raw, unsigned n, char *out, unsigned outmax, int *toobig);

/* The text to a DOS file's bytes: every LF becomes CR LF. dst holds at least 2 * n bytes. Returns the length. */
unsigned ws_export(const char *src, unsigned n, char *dst);

/* 1 if `path` is something DOS can open as a document: at most 66 characters, no wildcards or other characters
   DOS refuses, and a last component with a base of 1-8 characters and an extension of 0-3, not a device name. */
int ws_path_ok(const char *path);
/* Adds ".TXT" when the last component of `name` has no dot (and there is room: name holds `max` bytes). */
void ws_add_extension(char *name, unsigned max);

/* The BIOS printer status byte (INT 17h): bit 7 not busy, 4 selected (on line), 5 out of paper, 3 I/O error,
   0 time-out. ws_prn_ready is 1 for a printer that can take a character; ws_prn_failed is 1 if a character
   was not taken (time-out, I/O error or out of paper); ws_prn_words says what is wrong in a few words. */
int ws_prn_ready(unsigned char status);
int ws_prn_failed(unsigned char status);
const char *ws_prn_words(unsigned char status);

/* A character as it is sent to the printer: the code page 437 letters (and everything else above 0x7F) as plain
   ASCII, control characters as a space; the ASCII characters unchanged. */
char ws_print_char(unsigned char c);

/* One line to print, from the line start *pos, at WS_PRINT_WIDTH columns: a form feed first when a new page
   begins, then the margin, the text (through ws_print_char), CR LF (an empty line is only CR LF). Advances *pos; *line is the line number on the page (0 at the top).
   Returns the length written to out (at least WS_PRINT_MARGIN + WS_PRINT_WIDTH + 4 bytes); *last is 1 when the
   text ends with this line. */
unsigned ws_print_line(const WsText *t, unsigned *pos, unsigned *line, char *out, int *last);

#endif
