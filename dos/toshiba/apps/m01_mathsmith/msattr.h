/* The text attributes of MathSmith's two full screens (ui.c, editor.c). The mono builds (the Toshiba's, the XT's) use screen.h's; the 486's VGA build (-DMS_486,
   decision XD86) uses colours (attribute: background << 4 | foreground). ATTR_ECHO is what was typed, ATTR_ERRLINE an error or a warning, ATTR_KEYS the row of keys. */
#ifndef MS_ATTR_H
#define MS_ATTR_H

#include "screen.h"

#ifdef MS_486
#undef ATTR_NORMAL
#undef ATTR_BRIGHT
#undef ATTR_REVERSE
#undef ATTR_UNDERLINE
#define ATTR_NORMAL    0x17            /* light grey on blue */
#define ATTR_BRIGHT    0x1F            /* white on blue */
#define ATTR_REVERSE   0x30            /* black on cyan: the status line, the titles */
#define ATTR_UNDERLINE 0x1F            /* an input field */
#define ATTR_ECHO      0x1E            /* yellow on blue: the lines that were typed */
#define ATTR_ERRLINE   0x4F            /* white on red */
#define ATTR_KEYS      0x70            /* black on light grey: the row of keys */
#else
#define ATTR_ECHO      ATTR_BRIGHT
#define ATTR_ERRLINE   ATTR_BRIGHT
#define ATTR_KEYS      ATTR_NORMAL
#endif

#endif
