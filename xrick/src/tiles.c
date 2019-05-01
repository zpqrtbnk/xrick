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



#include "tiles.h"
#include "fb.h"



static tile_t *tiles_bank;
static U16 tiles_filter;



/*
 * tiles_setBank
 *
 * sets current tiles bank to <bank>.
 */
void tiles_setBank(U8 bank)
{
	if (bank >= TILES_BANKS_COUNT)
		sys_panic("xrick/tiles: invalid bank number %d\n", bank);
	tiles_bank = tiles_banks[bank];
}



/*
 * tiles_setFilter
 *
 * sets current tiles display filter to <filter>
 */
void tiles_setFilter(U16 filter)
{
	tiles_filter = filter;
}



/*
 * tiles_paint
 *
 * paints tile <tileNumber> at the position indicated by <fb>.
 * returns next <fb> value.
 */
U8 *tiles_paint(U8 tileNumber, U8 *fb)
{
	U8 i, k, *f;
#ifdef GFXPC
	U16 x;
#endif
#ifdef GFXST
	U32 x;
#endif

	f = fb;

	for (i = 0; i < 8; i++) /* 8 pixel lines */
	{
#ifdef GFXPC
		/* map CGA 2 bits per pixel to frame buffer 8 bits per pixels */
		x = tiles_bank[tileNumber][i] & tiles_filter;
		for (k = 8; k--; x >>= 2)
			f[k] = x & 3;
#endif
#ifdef GFXST
		/* map ST 4 bits per pixel to frame buffer 8 bits per pixels */
		x = tiles_bank[tileNumber][i];
		for (k = 8; k--; x >>= 4)
			f[k] = x & 0x0f;
#endif
		f += FB_WIDTH; /* next line */
	}

	return fb + 8;
}



/*
 * tiles_paintAt
 *
 * paints tile <tileNumber> at the position indicated by <x>, <y>.
 * <x>, <y> are fb-coordinates.
 */
void tiles_paintAt(U8 tileNumber, U16 x, U16 y)
{
	tiles_paint(tileNumber, fb_at(x, y));
}



/*
 * tiles_paintList
 *
 * paints list of tiles <tilesList> at the position indicated by <fb>. the
 * list must be TILES_NULL terminated and can contain TILES_CRLF elements
 * to produce crlf.
 *
 * returns next <fb> value.
 */
U8 *tiles_paintList(U8 *tilesList, U8 *fb)
{
	U8 *f;

	f = fb;

	while (1)
	{
		if (*tilesList == TILES_NULL) /* end of list */
			return f;

		if (*tilesList == TILES_CRLF) /* crlf */
		{
			fb += 8 * FB_WIDTH;
			f = fb;
			tilesList++;
			continue;
		}

		/* else paint */
		tiles_paint(*tilesList, f);
		f += 8;
		tilesList++;
	}
}



/*
 * tiles_paintListAt
 *
 * paints list of tiles <tilesList> at the position indicated by <x>, <y>. the
 * list must be TILES_NULL terminated and can contain TILES_CRLF elements to
 * produce crlf.
 * <x>, <y> are fb-coordinates.
 */
void tiles_paintListAt(U8 *tilesList, U16 x, U16 y)
{
	tiles_paintList(tilesList, fb_at(x, y));
}



/* eof */
