/* Text screen for the XT: MDA at B000h (or CGA text at B800h), 80x25,
   written directly. Shared by the programs; see apps/00_smoke for the
   first version of these ideas. */
#ifndef XT_SCREEN_H
#define XT_SCREEN_H

#ifndef SCR_ROWS
#define SCR_ROWS 25      /* the VGA build (common/vgascreen.c) sets 30 */
#endif
#define SCR_COLS 80

/* MDA attributes (they also render sensibly on CGA). */
#define ATTR_NORMAL    0x07
#define ATTR_BRIGHT    0x0F
#define ATTR_UNDERLINE 0x01
#define ATTR_REVERSE   0x70

#define KEY_ESC   0x011B
#define KEY_ENTER 0x1C0D

/* Finds the screen, saves the video state, installs a Ctrl+Break
   handler, drops keys typed while loading. Returns 0, or 1 if the
   video mode is not 80-column text. */
int scr_init(void);
/* 1 if scr_init has been called and found a text screen (a program that is sometimes a line-mode filter asks before drawing). */
int scr_ready(void);
/* Restores the video mode and cursor shape (this clears the screen). */
void scr_done(void);

/* Drawing in frames, for a screen that is redrawn often (a phosphor display flashes when it is
   cleared and rewritten): between scr_begin_frame and scr_end_frame the calls below draw into a
   copy in memory, and scr_end_frame writes to the screen only the rows that differ from what it
   shows. Outside a frame the calls draw straight onto the screen, as always. */
void scr_begin_frame(void);
void scr_end_frame(void);
/* How many character cells have been written to the screen so far (a test and tuning aid). */
extern unsigned long scr_cells_written;

/* Copies `count` rows from row `from` to row `to` (they may overlap): a fast scroll. */
void scr_move_rows(int from, int to, int count);

void scr_clear(void);
void scr_clear_row(int row, unsigned char attr);
void scr_put(int row, int col, const char *text, unsigned char attr);
void scr_put_centred(int row, const char *text, unsigned char attr);
void scr_cursor(int row, int col);
void scr_hide_cursor(void);

/* Drops any keys already typed. */
void scr_flush_keys(void);
/* Blocking key read through the BIOS: (scan << 8) | ascii. */
unsigned scr_getkey(void);
/* A key if one is waiting, else 0 (never blocks). */
unsigned scr_poll_key(void);
/* Edits a line in buf (at most max characters, buf holds the default)
   at row/col with the cursor shown. Returns 1 on Enter, 0 on Esc. */
int scr_input(int row, int col, char *buf, int max, unsigned char attr);
/* The same, and the key `stop_key` (a BIOS key code, e.g. Tab = 0x0F09) ends it too, returning 2. */
int scr_input_stop(int row, int col, char *buf, int max, unsigned char attr, unsigned stop_key);

/* Test hook: when the environment variable XTDUMP names a file, writes
   the 4000-byte text screen there. Call just before exiting. */
void scr_dump_if_requested(void);
/* The same for any other variable (a program may offer a second dump, e.g.
   NETFILES' XTDUMPVIEW when the text viewer is left). */
void scr_dump_to_env(const char *variable);

#endif
