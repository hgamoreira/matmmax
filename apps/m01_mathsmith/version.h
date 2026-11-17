/* Which MathSmith this is: the Toshiba's (M08, with the assistant, graphics plots on CGA) or the IBM XT's (M10, M13: text screen only, plots drawn with characters,
   with the assistant since M13: -DMS_XT) or the 486's (M11: 32-bit protected mode, 80x50 colour text, VGA plots, with the assistant: -DMS_486). */
#ifndef MS_VERSION_H
#define MS_VERSION_H

#ifdef MS_LINUX
#define MS_VERSION "M12 Linux"
#elif defined(MS_XT)
#define MS_VERSION "M13 XT"
#elif defined(MS_486)
#define MS_VERSION "M11 486"
#else
#define MS_VERSION "M08"
#endif

#endif
