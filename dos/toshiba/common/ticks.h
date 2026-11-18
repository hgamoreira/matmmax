/* The BIOS tick counter (18.2 Hz, 0040:006C): the only clock the XT has
   without programming the timer chip. One tick = 54.9 ms. */
#ifndef XT_TICKS_H
#define XT_TICKS_H

unsigned long bios_ticks(void);
/* Milliseconds in the given number of ticks (rounded). */
unsigned long ticks_to_ms(unsigned long ticks);

#endif
