/*
 * xrick/include/sprites.h
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

/*
 * NOTES -- PC version
 *
 * A sprite consists in 4 columns and 0x15 rows of (U16 mask, U16 pict),
 * each pair representing 8 pixels (cga encoding, two bits per pixels).
 * Sprites are stored in 'sprites.bin' and are loaded by spr_init. Memory
 * is freed by spr_shutdown.
 *
 * There are four sprites planes. Plane 0 is the raw content of 'sprites.bin',
 * and planes 1, 2 and 3 contain copies of plane 0 with all sprites shifted
 * 2, 4 and 6 pixels to the right.
 */


#ifndef _SPRITES_H_
#define _SPRITES_H_

#include "system.h"

/*
 * methods
 */
void sprites_setDepth(U8);
void sprites_paint(U8, U16, U16);
void sprites_paint2(U8, U16, U16, U8);
void sprites_clear(U16, U16);

#ifdef GFXPC

#define SPRITES_NBR_SPRITES (0x9b)

typedef struct {
  U16 mask;
  U16 pict;
} spriteX_t;

typedef spriteX_t sprite_t[4][0x15];   /* one sprite */

extern sprite_t sprites_data[SPRITES_NBR_SPRITES];

#endif

#ifdef GFXST

#define SPRITES_NBR_SPRITES (0xD5)

typedef U32 sprite_t[0x54];  /* 0x15 per 0x04 */

extern sprite_t sprites_data[SPRITES_NBR_SPRITES];

/*
 * ST-native sprite-slot number -> sprites_data[] array index.
 *
 * review-log.md A1/A6 derived several animation tables' sprite numbers directly
 * from the ST's own object pointers via sprite = (pointer - 0x2BE9E) / 0x150 --
 * verified bit-exact against re/atari_ram.bin. That number is the ST's *own*
 * numbering of its sprite slots, and dat_spritesST.c's array happens to store
 * sprites 0-0x36 at that same position -- but from there on the array is
 * permuted (extraction pulled in a different bank before circling back), so
 * ST slot number and sprites_data[] index diverge for anything at or past
 * 0x37. A1's dynamite-fuse table and A6's box/bomb explosion table both used
 * ST slot numbers >= 0x37 directly as sprites_data[] indices, which is why the
 * bomb fuse animation showed unrelated sprites (2026-09-10 bug report).
 *
 * This table is the fix, and it applies to *any* ST-slot-derived sprite number,
 * not just those two tables -- index by the ST slot number, get back the real
 * sprites_data[] position. Mechanically generated from dat_spritesST.c's own
 * per-entry hex-value comments (each entry already records which ST slot its
 * data came from, e.g. entry 153 is commented 0x0081) -- not hand-typed, not
 * guessed: paired each entry's array position with its comment value, sorted
 * by that value. Below 0x37 it is the identity, exactly as expected.
 */
extern const U8 sprites_stnum_to_index[SPRITES_NBR_SPRITES];

#endif

#endif

/* eof */

