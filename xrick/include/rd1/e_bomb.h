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

#ifndef _E_BOMB_H
#define _E_BOMB_H

#include "system.h"

#define E_BOMB_NO 3
#define E_BOMB_ENT ent_ents[E_BOMB_NO]
/*
 * Fuse length -- review-log.md A1.
 * PC: 45 ticks total (0x2D), fuse while ticker >= 0x0A, explosion 9..1.
 * ST: the fuse is a 17-entry POINTER table at 0x46BF2 driven by
 *     0x4CAC2 `move.w (0x4A810),D0 / bclr #0,D0 / add.w D0,D0` -- entry k for ticks 2k
 *     and 2k+1, so 17 x 2 = 34 fuse ticks, then the sentinel at 0x4CB06 sets the lethal
 *     flag and applies x -= 4 / y -= 5. 34 + 9 = 43 = 0x2B.
 */
#ifdef PLATFORM_ST
#define E_BOMB_TICKER (0x37)   /* 34 fuse (17 frames x2) + 20 explosion (10 frames x2) */
#define E_BOMB_BOOM   (0x14)   /* ticker value at detonation: 20 explosion ticks follow */
#else
#define E_BOMB_TICKER (0x2D)
#define E_BOMB_BOOM   (0x09)   /* PC: detonation tick, 9 explosion ticks follow */
#endif

extern U8 e_bomb_lethal;
extern U8 e_bomb_ticker;
extern U8 e_bomb_xc;
extern U16 e_bomb_yc;

extern U8 e_bomb_hit(U8);
extern void e_bomb_init(U16, U16);
extern void e_bomb_action(U8);

#endif

/* eof */
