/*
 * XRICK
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

#ifndef _TILES_H
#define _TILES_H

#include "system.h"

/*
 * methods
 */
void tiles_setBank(U8);
void tiles_setFilter(U16);
U8 *tiles_paint(U8, U8 *);
void tiles_paintAt(U8, U16, U16);
U8 *tiles_paintList(U8 *, U8 *);
void tiles_paintListAt(U8 *, U16, U16);

/*
 * one single tile
 *
 * a tile is 8x8 pixels.
 * PC: CGA encoding = 2 bits per pixel, one U16 per line.
 * ST: encoding = 4 bits per pixel, one U32 per line.
 */
#ifdef GFXPC
typedef U16 tile_t[8];
#endif
#ifdef GFXST
typedef U32 tile_t[8];
#endif

/*
 * banks (each bank contains 256 (0x100) tiles)
 *
 * FIXME is this true?
 * bank 0: tiles for main intro
 * bank 1: tiles for map intro
 * bank 2: unused
 * bank 3: game tiles, page 0
 * bank 4: game tiles, page 1
 */
#ifdef GFXPC
#define TILES_BANKS_COUNT 4
#endif
#ifdef GFXST
#define TILES_BANKS_COUNT 3
#endif

extern tile_t tiles_banks[TILES_BANKS_COUNT][256];

/*
 * special tile numbers
 */
#define TILES_BULLET 0x01
#define TILES_BOMB 0x02
#define TILES_RICK 0x03

#define TILES_NULL 0xfe
#define TILES_NULLCHAR "\376"
#define TILES_CRLF 0xff
#define TILES_CRLFCHAR "\377"

#endif

/* eof */
