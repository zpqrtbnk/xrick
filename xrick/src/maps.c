/*
 * xrick/src/maps.c
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
 * NOTES
 *
 * A map is composed of submaps, which in turn are composed of rows of
 * 0x20 tiles. map_map contains the tiles for the current portion of the
 * current submap, i.e. a little bit more than what appear on the screen,
 * but not the whole submap.
 *
 * map_frow is map_map top row within the submap.
 *
 * Submaps are stored as arrays of blocks, each block being a 4x4 tile
 * array. map_submaps[].bnum points to the first block of the array.
 *
 * Before a submap can be played, it needs to be expanded from blocks
 * to map_map.
 */

#include "system.h"
#include "config.h"
#include "env.h"

#include "maps.h"

#include "game.h"
#include "debug.h"
#include "ents.h"
#include "draw.h"
#include "screens.h"
#include "e_sbonus.h"
#include "tiles.h"
#include "fb.h"

/*
 * global vars
 */
U8 map_map[0x2C][0x20];
U8 map_eflg[0x100];
U8 map_frow;
U8 map_tilesBank;


/*
 * prototypes
 */
static void map_eflg_expand(U8);


/*
 * Fill in map_map with tile numbers by expanding blocks.
 *
 * add map_submaps[].bnum to map_frow to find out where to start from.
 * We need to /4 map_frow to convert from tile rows to block rows, then
 * we need to *8 to convert from block rows to block numbers (there
 * are 8 blocks per block row). This is achieved by *2 then &0xfff8.
 */
void
map_expand(void)
{
  U8 i, j, k, l;
  U8 row, col;
  U16 pbnum;

  pbnum = map_submaps[env_submap].bnum + ((2 * map_frow) & 0xfff8);
  row = col = 0;

  for (i = 0; i < 0x0b; i++) {  /* 0x0b rows of blocks */
    for (j = 0; j < 0x08; j++) {  /* 0x08 blocks per row */
      for (k = 0, l = 0; k < 0x04; k++) {  /* expand one block */
	map_map[row][col++] = map_blocks[map_bnums[pbnum]][l++];
	map_map[row][col++] = map_blocks[map_bnums[pbnum]][l++];
	map_map[row][col++] = map_blocks[map_bnums[pbnum]][l++];
	map_map[row][col]   = map_blocks[map_bnums[pbnum]][l++];
	row += 1; col -= 3;
      }
      row -= 4; col += 4;
      pbnum++;
    }
    row += 4; col = 0;
  }
}


/*
 * Initialize a new submap
 *
 * ASM 0cc3
 */
void
map_init(void)
{
	/*sys_printf("xrick/map_init: map=%#04x submap=%#04x\n", g_map, env_submap);*/
#ifdef GFXPC
	tiles_setFilter(0xffff);
	map_tilesBank = map_submaps[env_submap].page == 1 ? 3 : 2;
#endif
#ifdef GFXST
	map_tilesBank = map_submaps[env_submap].page == 1 ? 2 : 1;
#endif
	map_eflg_expand((map_submaps[env_submap].page == 1) ? 0x10 : 0x00);
	map_expand();
	ent_reset();

	/* entities that are in the visible part of the map */
	ent_actvis(
		map_frow + MAPS_TOPHEIGHT_TL,
		map_frow + MAPS_TOPHEIGHT_TL+MAPS_VISHEIGHT_TL-1);

	/* entities that are in the hidden top of the map */
	ent_actvis(
		map_frow + 0,
		map_frow + MAPS_TOPHEIGHT_TL-1);

	/* entities that are in the hidden bottom of the map */
	ent_actvis(
		map_frow + MAPS_TOPHEIGHT_TL+MAPS_VISHEIGHT_TL,
		map_frow + MAPS_TOPHEIGHT_TL+MAPS_VISHEIGHT_TL+MAPS_BOTHEIGHT_TL-1);
}


/*
 * Expand entity flags for this map
 *
 * ASM 1117
 */
void
map_eflg_expand(U8 offs)
{
  U8 i, j, k;

  for (i = 0, k = 0; i < 0x10; i++) {
    j = map_eflg_c[offs + i++];
    while (j--) map_eflg[k++] = map_eflg_c[offs + i];
  }
}


/*
 * Chain (sub)maps
 *
 * ASM 0c08
 * return: TRUE/next submap OK, FALSE/map finished
 FIXME should return next submap number, or 0.
 */
U8
map_chain(void)
{
  U16 c, t;

  env_changeSubmap = 0; /* FIXME but not used?! */
  e_sbonus_counting = FALSE; /* FIXME what? move this out of here!! */

  /* find connection */
  c = map_submaps[env_submap].connect;
  t = 3;

  IFDEBUG_MAPS(
    sys_printf("xrick/maps: chain submap=%#04x frow=%#04x .connect=%#04x %s\n",
	       env_submap, map_frow, c,
	       (game_dir == LEFT ? "-> left" : "-> right"));
  );

  /*
   * look for the first connector with compatible row number. if none
   * found, then panic
   */
  for (c = map_submaps[env_submap].connect; ; c++) {
    if (map_connect[c].dir == 0xff)
      sys_panic("(map_chain) can not find connector\n");
    if (map_connect[c].dir != game_dir) continue;
    t = (ent_ents[1].y >> 3) + map_frow - map_connect[c].rowout;
    if (t < 3) break;
  }

  /* got it */
  IFDEBUG_MAPS(
    sys_printf("xrick/maps: chain frow=%#04x y=%#06x\n",
	       map_frow, ent_ents[1].y);
    sys_printf("xrick/maps: chain connect=%#04x rowout=%#04x - ",
	       c, map_connect[c].rowout);
    );

  if (map_connect[c].submap == 0xff) {
    /* no next submap - request next map */
    IFDEBUG_MAPS(
      sys_printf("chain to next map\n");
      );
    return FALSE;
  }
  else  {
    /* next submap */
    IFDEBUG_MAPS(
      sys_printf("chain to submap=%#04x rowin=%#04x\n",
		 map_connect[c].submap, map_connect[c].rowin);
      );
    map_frow = map_frow - map_connect[c].rowout + map_connect[c].rowin;
    env_submap = map_connect[c].submap;
    IFDEBUG_MAPS(
      sys_printf("xrick/maps: chain frow=%#04x\n",
		 map_frow);
      );
    return TRUE;
  }
}


/*
 * Reset all marks, i.e. make them all active again.
 *
 * ASM 0025
 *
 */
void
map_resetMarks(void)
{
  U16 i;
  for (i = 0; i < MAP_NBR_MARKS; i++)
    map_marks[i].ent &= ~MAP_MARK_NACT;
}


/* CLEAN BELOW THIS LINE */

/*
 * maps_paint
 *
 * paints the current map to the frame buffer.
 */
void maps_paint(void)
{
	U8 i, j;
	U8 *f;

	tiles_setBank(map_tilesBank);

	for (i = 0; i < 0x18; i++) /* 0x18 rows */
	{
#ifdef GFXPC
		f = fb_at(0x20, i * 8);
#endif
#ifdef GFXST
		f = fb_at(0x20, (i + 1) * 8);
#endif
		for (j = 0; j < 0x20; j++) /* 0x20 tiles per row */
			f = tiles_paint(map_map[i + 8][j], f);
	}
}



/*
 * maps_paintRect
 *
 * paints a portion of the map at <x>, <y> of size <width>, <height>.
 * <x>, <y> expressed in map/px.
 */
void maps_paintRect(U16 x, U16 y, U16 width, U16 height)
{
	U16 x_fb, y_fb;
	U8 *fb;
	U8 r, c;

	/* align to tiles */
	maps_alignRect(&x, &y, &width, &height);

	/* clip */
	if (maps_clip(&x, &y, &width, &height))  /* return if not visible */
		return;

	/* convert to fb/px */
	x_fb = x - MAPS_FB_X;
	y_fb = y - MAPS_FB_Y;

	/* convert map/px to map/tl */
	x >>= 3;
	y >>= 3;
	width >>= 3;
	height >>= 3;

	/* draw */
	for (r = 0; r < height; r++) /* for each tile row */
	{
#ifdef GFXPC
		fb = fb_at(x_fb, y_fb + r * 8);
#endif
#ifdef GFXST
		fb = fb_at(x_fb, 8 + y_fb + r * 8); /* FIXME +8? */
#endif
		for (c = 0; c < width; c++) /* for each tile column */
		{
			fb = tiles_paint(map_map[y + r][x + c], fb);
		}
	}
}



/*
 * maps_alignRect
 *
 * aligns a rectangle at <x>, <y> of size <width>, <height> to tiles.
 * coordinates expressed in map/px.
 * resulting rectangle might be bigger.
 */
void maps_alignRect(U16 *x, U16 *y, U16 *width, U16 *height)
{
	U16 xa, ya;
	U16 wa, ha;

  	/* align to column and row */
	xa = *x & 0xfff8;
	ya = *y & 0xfff8;

	/* grow width and height to cover tiles */
	*width += *x - xa;
	*height += *y - ya;
	wa = *width + 8 - (*width % 8);
	ha = *height + 8 - (*height % 8);

	*x = xa;
	*y = ya;
	*width = wa;
	*height = ha;
}



/*
 * maps_clip
 *
 * clips a rectangle at <x>, <y> of size <width>, <height>.
 * <x>, <y> expressed in map/px.
 * returns TRUE if fully clipped, FALSE if still (at least partly) visible.
 */
U8 maps_clip(U16 *x, U16 *y, U16 *width, U16 *height)
{
	if (*x < 0)
	{
		if (*x + *width < 0)
			return TRUE;
		else
		{
			*width += *x;
			*x = 0;
		}
	}
	else
	{
		if (*x > MAPS_WIDTH_PX)
			return TRUE;
		else
		if (*x + *width > MAPS_WIDTH_PX)
		{
			*width = MAPS_WIDTH_PX - *x;
		}
	}

	if (*y < MAPS_TOPHEIGHT_PX)
	{
		if ((*y + *height) < MAPS_TOPHEIGHT_PX)
			return TRUE;
		else
		{
			*height += *y - MAPS_TOPHEIGHT_PX;
			*y = MAPS_TOPHEIGHT_PX;
		}
	}
	else
	{
		if (*y >= MAPS_TOPHEIGHT_PX+MAPS_VISHEIGHT_PX)
			return TRUE;
		else
		if (*y + *height > MAPS_TOPHEIGHT_PX+MAPS_VISHEIGHT_PX)
			*height = MAPS_TOPHEIGHT_PX+MAPS_VISHEIGHT_PX - *y;
	}

	return FALSE;
}




/* eof */
