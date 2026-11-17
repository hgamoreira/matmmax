/* Native front-end harness: no chip/heap/stack measurements are simulated. */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "port.h"
static long polls;
void ms_write(const char *text) { fputs(text,stdout); }
clock_t ms_clock(void) { return clock(); }
int ms_stack_ok(void) { return getenv("MS_TEST_STACK_LOW")==NULL; }
int ms_poll(void)
{
    const char *after=getenv("MS_TEST_CANCEL_AFTER");
    return after && ++polls==atol(after);
}
void ms_diagnostics(void)
{
    printf("Native harness; target heap/stack not measured.\nEngine payload: used=%lu peak=%lu limit=%lu bytes\n",
           (unsigned long)ms_used(),(unsigned long)ms_peak(),(unsigned long)ms_limit());
}
void ms_restart(void) { puts("Native restart requested."); exit(0); }
int main(void)
{
    int c;
    const char *limit=getenv("MS_TEST_LIMIT");
    if (limit) ms_set_limit((size_t)atol(limit));
    ms_console_start();
    while ((c=getchar())!=EOF) ms_console_key(c);
    return 0;
}
