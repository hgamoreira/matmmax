/* Force-included only for E01's build of the shared engine/kernel sources. */
#ifndef MS_S3_ENGINE_PORT_H
#define MS_S3_ENGINE_PORT_H
#include <stdlib.h>
#include <time.h>
#include <math.h>
#include "platform.h"
#define malloc ms_malloc
#define calloc ms_calloc
#define realloc ms_realloc
#define free ms_free
#define clock ms_clock
/* Newlib exports finite(); the portable plot source has its own static helper.
 * Include math.h first, then rename only the source's helper in this build. */
#define finite ms_plot_finite
#endif
