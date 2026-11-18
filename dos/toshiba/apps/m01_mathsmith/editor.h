/* M08: MathSmith's script editor (decision XD78), DOS full screen only. It edits one .m file with WordSmith's text engine. The assistant
   and the running of scripts are done by the caller (ui.c): the editor saves the file, returns what the user asked for, and is entered again
   afterwards with the cursor where it was, so that no editor buffer is in memory while MSASK.EXE runs. */
#ifndef MS_EDITOR_H
#define MS_EDITOR_H

#define ED_QUIT 0       /* the user left (saved or discarded) */
#define ED_RUN 1        /* F5: the file is saved, run it */
#define ED_EXPLAIN 2    /* F6 E: the file is saved, explain it */
#define ED_FIX 3        /* F6 F: the file is saved, ask for a fix (instruction = what is wrong, may be empty) */
#define ED_WRITE 4      /* F6 W: ask for code described in instruction, to be put at the cursor */
#define ED_CHANGE 5     /* F6 C: the file is saved, ask for it to be changed as instruction says (the answer replaces the text, if the user agrees) */
#define ED_OPEN 6       /* F8 (the 486 build): open the script named in instruction instead (the file is saved or its changes were discarded) */
#define ED_FAILED (-1)  /* could not open or load the file: message says why */

#define EDA_NONE 0
#define EDA_REPLACE 1   /* on entry: offer to replace the text with ASK.M (the assistant's fix) */
#define EDA_INSERT 2    /* on entry: put ASK.M in at the cursor's line (the assistant's new code) */

typedef struct {
    char path[64];          /* in: the file, such as myscript.m (a missing file is a new one); out: the name it was last saved under (F7 changes it) */
    unsigned line;          /* in: the line for the cursor (1 = first); out: the line it was on */
    char message[80];       /* in: shown on the bottom row at first (an error to fix); out: why ED_FAILED */
    char error[80];         /* in: the last error, offered as the default of "what is wrong" */
    char instruction[200];  /* out: for ED_FIX, ED_CHANGE and ED_WRITE */
    int apply;              /* in: EDA_* */
} EdRequest;

int editor_run(EdRequest *r);

/* Shows a text to read, not to change (MathSmith's help), until Esc, F10 or F1. */
void editor_view(const char *title, const char *content);

/* "name" or "name.m" in any case to the file name name.m in lower case. */
void editor_path(const char *name, char *out);

#endif
