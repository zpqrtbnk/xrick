/*
 * xrick/src/headless/xrick_core.c
 *
 * xrick-core: the RD1 game logic with no video, sound or timing, one logic step
 * at a time (PLAN.md T43 phase 3, kb/demo-solver.md). Built by `make core`.
 *
 *   xrick-core [-demo] [-trace <file>] [-steps <n>] [-scramble <n>]
 *
 * -demo      play the built-in scripts (src/rd1/dat_demo.c) from a new game;
 *            stops when the demo hands control back. without it, Rick stands
 *            still (no control held) until -steps runs out or the game ends.
 * -trace     same trace as `xrick -trace`, for diffing against the SDL build.
 * -steps     stop after <n> logic steps (default 100000).
 * -scramble  step the random generator <n> times before the game starts. With
 *            -demo, every segment entry reseeds it (T43 D1), so the trace must
 *            not change.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "system.h"
#include "sysarg.h"
#include "game.h"
#include "env.h"
#include "e_them.h"
#include "demo.h"

static void
usage(void)
{
  fprintf(stderr, "usage: xrick-core [-demo] [-trace <file>] [-steps <n>] [-scramble <n>]\n");
  exit(2);
}

int
main(int argc, char *argv[])
{
  unsigned long steps = 100000, scramble = 0, i;
  U8 r = GAME_HL_STEP;
  int a;
  const char *why;

  for (a = 1; a < argc; a++)
  {
    if (!strcmp(argv[a], "-demo"))
      sysarg_args_demo = 1;
    else if (!strcmp(argv[a], "-trace") && a + 1 < argc)
      sysarg_args_trace = argv[++a];
    else if (!strcmp(argv[a], "-steps") && a + 1 < argc)
      steps = strtoul(argv[++a], NULL, 0);
    else if (!strcmp(argv[a], "-scramble") && a + 1 < argc)
      scramble = strtoul(argv[++a], NULL, 0);
    else
      usage();
  }

  for (i = 0; i < scramble; i++)
  {
#ifdef PLATFORM_ST
    e_them_rndstep();
#else
    e_them_rndseed++;
#endif
  }

  game_hlStart();
  why = "step limit";
  while (game_hlSteps() < steps)
  {
    r = game_hlStep(0);
    if (r == GAME_HL_OVER) { why = "game over"; break; }
    if (r == GAME_HL_END) { why = "game completed"; break; }
    if (sysarg_args_demo && !demo_active) { why = "end of demo"; break; }
  }

  printf("xrick-core: %s after %lu steps, submap %#04x, lives %u, score %lu\n",
	 why, (unsigned long)game_hlSteps(), (unsigned int)env_submap,
	 (unsigned int)env_lives, (unsigned long)env_score);
  return 0;
}

/* eof */
