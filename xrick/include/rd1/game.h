/*
 * xrick/include/game.h
 *
 * Copyright (C) 1998-2019 bigorno (bigorno@bigorno.net). All rights reserved.
 *
 * The use and distribution terms for this software are contained in the file
 * named README, which can be found in the root of this distribution. By
 * using this software in any fashion, you are agreeing to be bound by the
 * terms of this license.
 *
 * You must not remove this notice, or any other, from this software.
 */

#ifndef _GAME_H
#define _GAME_H

#include <stddef.h> /* NULL */

#include "system.h"
#include "syssnd.h"

#include "rects.h"

#define LEFT 1
#define RIGHT 0

#define GAME_PERIOD 75

#define GAME_BOMBS_INIT 6
#define GAME_BULLETS_INIT 6

typedef struct {
  U32 score;
  U8 name[10];
} hscore_t;

extern hscore_t game_hscores[8];  /* highest scores (hall of fame) */

extern U8 game_dir;        /* direction (LEFT, RIGHT) */

extern U8 game_waitevt;    /* wait for events (TRUE, FALSE) */
extern U8 game_period;     /* time between each frame, in millisecond */

extern rect_t *game_rects; /* rectangles to redraw at each frame */

extern void game_run(void);

extern void game_toggleCheat(U8);

#ifdef HEADLESS
/*
 * headless core (PLAN.md T43, kb/demo-solver.md): the game logic with no video,
 * sound or timing, advanced one logic step (one CTRL_ACTION pass) at a time.
 */
#define GAME_HL_STEP 0  /* one step done, the next one is pending */
#define GAME_HL_OVER 1  /* game over: no more steps */
#define GAME_HL_END 2   /* game completed: no more steps */

extern void game_hlStart(void);  /* new game, as set by sysarg_args_map/submap */
extern U8 game_hlStep(U8);       /* one step with these CONTROL_* bits held */
extern U32 game_hlSteps(void);   /* steps run since game_hlStart */
#endif

#endif

/* eof */


