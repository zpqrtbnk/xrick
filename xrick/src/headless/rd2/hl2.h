/*
 * xrick/src/headless/rd2/hl2.h
 *
 * xrick2-core only (branch `solver`, never shipped): RD2's game_main driven one frame
 * at a time, with no video, sound or timing (PLAN.md T47 phase 3).
 */

#ifndef _HL2_H
#define _HL2_H

#include "system.h"

/* hl2_step results */
#define HL2_STEP 0  /* a frame played, same map */
#define HL2_MAP  1  /* map done: now at the next map's level start (its tick 0) */
#define HL2_OVER 2  /* END_OF_RUN: last life lost */
#define HL2_HANG 3  /* map 5's load hangs (red border): map 4 done in a game begun on map 1 */
#define HL2_END  4  /* map 4 done in a game begun elsewhere: END_OF_RUN */
#define HL2_STUCK 5 /* the frame never ended (hl2_watchdog): a state to throw away */

extern void hl2_start(int map);   /* boot, then a new game on map 1..4, as `xrick -game 2 -map N` */
extern void hl2_boot(void);       /* hl2_start = hl2_boot + hl2_newgame */
extern void hl2_newgame(int map);
extern int hl2_step(U8 joy);      /* one game_main frame with joystick byte <joy> */
extern void hl2_watchdog(void);   /* arm it in this process (and again in each fork) */
extern int hl2_status(void);      /* the last hl2_step result */
extern U32 hl2_tick(void);        /* frames since the current map's level start */
extern void hl2_hang(void);       /* hl2_sys.c: rd2_sys_hang_red */
extern void hl2_gameGet(int *, U32 *);  /* hl2_state.c: status and tick */
extern void hl2_gameSet(int, U32);

/* RAM cells the tools read (algo-flow.md, algo-player.md) */
#define HL2_MAP_PLAYING 0x1239cu
#define HL2_SUBMAP      0x16464u
#define HL2_SCROLL      0x16462u
#define HL2_RICK_X      0x1695cu
#define HL2_RICK_Y      0x16960u
#define HL2_RICK_DEAD   0x12e2au
#define HL2_RICK_HIT    0x12e2cu
#define HL2_LIVES       0x17710u
#define HL2_LASER       0x176f4u
#define HL2_BOMBS       0x17702u
#define HL2_SCORE       0x176e4u  /* 3 BCD bytes */

#endif

/* eof */
