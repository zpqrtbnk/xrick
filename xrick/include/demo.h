/*
 * xrick/include/demo.h
 *
 * Demo mode, shared by both games: -demo plays a recorded control script, -record
 * writes one. A script is split in segments -- RD1: one per submap visit, RD2: one per map
 * -- and a game adapter tells the core when a segment starts and when a tick passes.
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

#ifdef ENABLE_DEMO

/*
 * RD1: number of animation loops the map introduction runs for before it
 * auto-advances in demo mode (it otherwise waits for FIRE forever). one loop is
 * three frames -- scr_imap.c seq 10 -> 12 -> 13 -> 10 -- so 0x18 is about five
 * seconds at the default game period.
 */
#define DEMO_INTRO_LOOPS 0x18

/*
 * one control event: "at <tick>, <ctrl> was pressed / released".
 *
 * <tick> counts game logic steps since the segment was entered -- RD1: CTRL_ACTION
 * passes, RD2: game_main frames ($10a54) -- NOT milliseconds and NOT rendered
 * frames. The game logic is deterministic and advances only there, so a tick-timed
 * script replays identically at any speed and at any frame rate.
 */
typedef struct {
  U16 tick;   /* logic steps since segment entry */
  U8 ctrl;    /* one CONTROL_* bit (control.h) */
  U8 down;    /* TRUE = pressed, FALSE = released */
} demoevt_t;

/*
 * one segment's script. no segment id: the index into the script array is the id.
 */
typedef struct {
  U16 nbr;          /* number of events; 0 = no demo for this segment */
  demoevt_t *evts;  /* sorted by ascending tick; NULL when nbr == 0 */
} demoscript_t;

/*
 * what a game hands the core: its built-in scripts and how to write a recording
 * back as the C file that holds them.
 */
typedef struct {
  demoscript_t *scripts;  /* built-in scripts, <nbr> entries */
  U16 nbr;                /* number of segments, at most DEMO_MAXSEG */
  const char *file;       /* generated file, as named in its header comment */
  const char *segname;    /* "submap" / "map", for messages and comments */
  const char *index;      /* what indexes the array, for the header comment */
  const char *includes;   /* extra #include lines after demo.h and control.h */
  const char *array;      /* name of the script array */
  const char *size;       /* its size, as written in the generated file */
  void (*saved)(void);    /* extra game-specific export after the C file, or NULL */
  void (*enter)(void);    /* on every segment entry while a script plays or records, or
                             NULL. RD1: reseed the random generator, so every segment
                             is self-contained (PLAN.md T43 D1) */
} demoset_t;

#define DEMO_MAXSEG 0x40

extern demoscript_t demo_scripts[];      /* RD1, src/rd1/dat_demo.c, indexed by submap visit */
extern demoscript_t rd2_demo_scripts[];  /* RD2, src/rd2/dat_rd2_script.c, indexed by map - 1 */

extern U8 demo_active;  /* TRUE while -demo playback is on */

extern void demo_init(const demoset_t *);
extern void demo_enterSegment(U16);
extern void demo_cycle(void);
extern U8 demo_play(void);
extern void demo_record(U8);
extern U8 demo_playing(void);
extern U8 demo_recording(void);
extern U16 demo_ticks(U16, U8 *, U16);
extern void demo_end(void);
extern void demo_save(void);

#endif /* ENABLE_DEMO */

#endif /* _DEMO_H */

/* eof */
