/* Style marks (V02, WordSmith/VGA, decision XD37): three control bytes inside a text that start or end bold, italic or
   underline where they stand. They are only recognised when a program is built with WS_STYLE_MARKS (the VGA editor);
   without it WS_IS_MARK is 0 and WordSmith/XT's code is exactly what it was. */
#ifndef XT_WSMARKS_H
#define XT_WSMARKS_H

#define WS_MARK_BOLD 0x02          /* Ctrl+B */
#define WS_MARK_ITALIC 0x19        /* Ctrl+Y */
#define WS_MARK_UNDERLINE 0x15     /* Ctrl+U */

#ifdef WS_STYLE_MARKS
#define WS_IS_MARK(c) ((unsigned char)(c) == WS_MARK_BOLD || (unsigned char)(c) == WS_MARK_ITALIC || \
                       (unsigned char)(c) == WS_MARK_UNDERLINE)
#else
#define WS_IS_MARK(c) 0
#endif

#endif
