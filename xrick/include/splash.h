/*
 * xrick/include/splash.h
 *
 * The xrick splash screen (src/splash.c), shown once at startup before either game.
 */

#ifndef _SPLASH_H
#define _SPLASH_H

/* how long the splash stays on screen, in ms, fade in and fade out included (320 ms
   each, src/splash.c). it cannot be skipped */
#define SPLASH_MS 2000

/* shows the splash and starts its timer */
extern void splash_start(void);
/* TRUE once SPLASH_MS have passed since splash_start (and the splash is released) */
extern int splash_done(void);
/* desktop: splash_start, then wait until splash_done, handling window events */
extern void splash_run(void);

#endif /* _SPLASH_H */

/* eof */
