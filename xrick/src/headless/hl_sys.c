/*
 * xrick/src/headless/hl_sys.c
 *
 * The host layer of xrick-core (PLAN.md T43, kb/demo-solver.md): what the SDL
 * files (sys*.c, xrick.c, rd1/syssnd.c) give the RD1 game logic, minus video,
 * sound, input and timing. The game logic itself is compiled unchanged, with
 * -DHEADLESS for the few lines in game.c that drive it.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

#include "system.h"
#include "sysarg.h"
#include "sysvid.h"
#include "sysevt.h"
#include "syssnd.h"
#include "img.h"

/*
 * sysarg.c: the options the game logic reads. xrick_core.c sets them.
 */
int sysarg_args_period = 0;
int sysarg_args_map = 0;
int sysarg_args_submap = 0;
int sysarg_args_fullscreen = 0;
int sysarg_args_zoom = 0;
int sysarg_args_nosound = 1;
int sysarg_args_vol = 0;
int sysarg_args_rd = 1;
#ifdef ENABLE_DEMO
int sysarg_args_demo = 0;
char *sysarg_args_record = NULL;
char *sysarg_args_trace = NULL;
#endif

/*
 * system.c
 */
void
sys_printf(char *msg, ...)
{
  va_list argptr;

  va_start(argptr, msg);
  vfprintf(stderr, msg, argptr);
  va_end(argptr);
}

void
sys_panic(char *err, ...)
{
  va_list argptr;

  va_start(argptr, err);
  vfprintf(stderr, err, argptr);
  va_end(argptr);
  exit(1);
}

U32
sys_gettime(void)
{
  return 0;
}

void
sys_sleep(int s)
{
  (void)s;
}

/*
 * sysvid.c: nothing is displayed. fb (fb.c) is still drawn into, in memory.
 */
void sysvid_update(rect_t *r) { (void)r; }
void sysvid_setDisplayPalette(void) { }
void sysvid_setPaletteFromImg(img_t *img) { (void)img; }
void sysvid_setPaletteFromRGB(U8 *r, U8 *g, U8 *b, U16 n) { (void)r; (void)g; (void)b; (void)n; }
void sysvid_setGamma(U8 g) { (void)g; }

/*
 * sysevt.c: xrick-core sets control_status itself (game_hlStep).
 */
void sysevt_poll(void) { }
void sysevt_wait(void) { }

/*
 * rd1/syssnd.c: silent.
 */
void syssnd_play(const sound_t *sound) { (void)sound; }
void syssnd_play_track(U8 track, S8 d1) { (void)track; (void)d1; }
void syssnd_pause(U8 pause, U8 clear) { (void)pause; (void)clear; }
void syssnd_vol(S8 d) { (void)d; }
void syssnd_toggleMute(void) { }

/* eof */
