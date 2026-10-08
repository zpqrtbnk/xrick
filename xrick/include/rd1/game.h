/*
 * Copyright (C) 1998-NOW bigorno (bigorno@bigorno.net). All rights reserved.
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

#endif

/* eof */


