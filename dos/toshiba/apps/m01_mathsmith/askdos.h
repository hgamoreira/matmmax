/* M07: the assistant's hook for the DOS front ends. The engine has written ASKIN.TXT; this runs MSASK.EXE (the network helper), which writes ASKOUT.TXT
   (the answer, or the reason it failed). Same signature as the engine's ml_ask_fn. Returns 0 (answered), 1 (failed) or 2 (Esc). */
#ifndef MS_ASKDOS_H
#define MS_ASKDOS_H

int askdos_run(void *ctx, int mode);

#endif
