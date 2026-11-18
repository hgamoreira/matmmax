/* Showing a plot on a DOS machine with a CGA-compatible display (M06): DOS only. */
#ifndef MS_PLOTDOS_H
#define MS_PLOTDOS_H

#include "plot.h"

/* Switches to the 640x200 two-colour mode, draws the plot, waits for a key and goes back to the text mode it found. Returns 0, or -1 (nothing changed) if
   the display does not have that mode. When the environment variable XTPLOTDUMP names a file, the 16 KB of video memory is written there before the wait. */
int plotdos_show(const PlotSpec *spec);

#endif
