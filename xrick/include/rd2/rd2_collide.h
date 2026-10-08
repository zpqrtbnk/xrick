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

/*
 * Rick Dangerous 2 -- collision probes and hit tests (rd2_collide.c, kb2/algo-collision.md).
 * Probe cells are RAM (inputs x/y/vy/actors-flag, outputs result/platform y/dx/dy).
 */

#ifndef _RD2_COLLIDE_H
#define _RD2_COLLIDE_H

#include "system.h"

#define RD2_PX    0x15f0eu  /* word, probe x */
#define RD2_PY    0x15f10u  /* word, probe y */
#define RD2_PVY   0x15f12u  /* word, vertical velocity 8.8 (high byte used by the actor part) */
#define RD2_PRES  0x15f14u  /* byte, result flags */
#define RD2_PACT  0x15f16u  /* word, != 0: also test actors */
#define RD2_PPLY  0x15f18u  /* word, platform actor y ($7fff on entry) */
#define RD2_PPDX  0x15f1au  /* word, platform dx */
#define RD2_PPDY  0x15f1cu  /* word, platform dy */
#define RD2_BOXDY 0x161cau  /* word, actor-test box y offset */
#define RD2_BOXH  0x161ccu  /* word, actor-test box height */

void rd2_15fba(void);   /* standard probe */
void rd2_15f1e(void);   /* crouch probe, sets [$12e16] */
void rd2_16278(void);   /* 8-px probe (bomb) */
void rd2_161fe(void);   /* point probe */

/* tests: return the carry flag */
int rd2_161ce(S16 d0, S16 d1, S16 d2, S16 d3, S16 d4, S16 d5, S16 d6, S16 d7);  /* aabb_overlap_test */
int rd2_14c8c(S16 d0, S16 d1, S16 d2, S16 d3, S16 d6, S16 d7);                  /* point_in_box */
int rd2_14b7a(S16 d0, S16 d1, S16 d2, S16 d3);          /* box vs Rick */
int rd2_14bee(S16 d0, S16 d1, S16 d2, S16 d3);          /* box vs laser front */
int rd2_14c20(S16 d0, S16 d1, S16 d2, S16 d3);          /* box vs bomb explosion */
int rd2_14c5a(S16 d0, S16 d1, S16 d2, S16 d3);          /* box vs melee point */
int rd2_14cb4(S16 d0, S16 d1, S16 d2, S16 d3, U32 a2);  /* box vs object record */
int rd2_14d04(S16 d0, S16 d1, S16 d2, S16 d3, U32 a2);  /* box vs actor record */

#endif /* _RD2_COLLIDE_H */

/* eof */
