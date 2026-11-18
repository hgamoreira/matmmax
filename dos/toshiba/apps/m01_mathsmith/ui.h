/* MathSmith's full-screen front end (DOS only). */
#ifndef MS_UI_H
#define MS_UI_H

/* Runs until the user quits. Returns 0, or -1 at once if the screen is not 80-column text (nothing was done). */
int ui_run(void);


#ifdef MS_486
/* MATHVGA /MODEL: only the model choice (shared with the other programs through the MODELCFG file), then returns like ui_run. */
int ui_run_model(void);
#endif

#endif
