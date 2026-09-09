/*
 * xrick/include/demo.h
 *
 * Demo (attract) mode -- see ../../demo.md
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

#ifndef _DEMO_H
#define _DEMO_H

#include "system.h"
#include "maps.h"  /* MAP_NBR_SUBMAPS */

#ifdef ENABLE_DEMO

/*
 * number of animation loops the map introduction runs for before it
 * auto-advances in demo mode (it otherwise waits for FIRE forever). one loop is
 * three frames -- scr_imap.c seq 10 -> 12 -> 13 -> 10 -- so 0x18 is about five
 * seconds at the default game period.
 */
#define DEMO_INTRO_LOOPS 0x18

/*
 * one control event: "at <tick>, <ctrl> was pressed / released".
 *
 * <tick> counts CTRL_ACTION passes -- i.e. entity logic steps -- since the
 * submap was entered, NOT milliseconds and NOT rendered frames. The game logic
 * is deterministic and advances only there, so a tick-timed script replays
 * identically at any -speed and at any frame rate.
 */
typedef struct {
  U16 tick;   /* CTRL_ACTION passes since submap entry */
  U8 ctrl;    /* one CONTROL_* bit (control.h) */
  U8 down;    /* TRUE = pressed, FALSE = released */
} demoevt_t;

/*
 * one submap's script. no submap id: the index into demo_scripts is the id,
 * the same way env_submap indexes map_submaps.
 */
typedef struct {
  U16 nbr;          /* number of events; 0 = no demo for this submap */
  demoevt_t *evts;  /* sorted by ascending tick; NULL when nbr == 0 */
} demoscript_t;

extern demoscript_t demo_scripts[MAP_NBR_SUBMAPS];  /* indexed by env_submap */

extern U8 demo_active;  /* TRUE while the demo is driving the controls */

extern void demo_init(void);
extern void demo_enterSubmap(U16);
extern void demo_cycle(void);
extern void demo_end(void);
extern void demo_save(void);

#endif /* ENABLE_DEMO */

#endif /* _DEMO_H */

/* eof */
