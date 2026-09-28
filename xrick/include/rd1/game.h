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

/*
 * ms per gameplay step. ST: 40 = the original's rate, measured in Hatari -- one
 * main-loop iteration per 2 VBLs at 50 Hz, 25 steps/s (kb/hatari.md 2026-09-28).
 * PC: not measured; 38 keeps the pace the port had (xrick's 75, which its old timer
 * ran at about half period, game.c game_loop).
 */
#ifdef PLATFORM_ST
#define GAME_PERIOD 40
#else
#define GAME_PERIOD 38
#endif

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
extern U8 game_hlStatus(void);   /* GAME_HL_STEP while the game runs, else OVER / END */
extern void game_hlReseed(U8);   /* reseed on every segment entry, as a demo does */
extern U32 game_hlSegments(void); /* segment entries since the process started */
extern void game_hlSettle(void);  /* run frames up to the next pending logic step */
#endif

#endif

/* eof */


