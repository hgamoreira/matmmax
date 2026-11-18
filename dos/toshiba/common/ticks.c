#include <dos.h>
#include <i86.h>
#include "ticks.h"

unsigned long bios_ticks(void)
{
    volatile unsigned long __far *counter = (volatile unsigned long __far *)MK_FP(0x40, 0x6C);
    unsigned long a, b;

    /* A dword read is two bus cycles: read until two reads agree. */
    do {
        a = *counter;
        b = *counter;
    } while (a != b);
    return a;
}

/* 65536 ticks of the 1.193182 MHz clock per BIOS tick: 54.9254 ms. */
unsigned long ticks_to_ms(unsigned long ticks)
{
    return (ticks * 549254UL + 5000UL) / 10000UL;
}
