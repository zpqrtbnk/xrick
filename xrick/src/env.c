/*
 * xrick/src/env.c
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



#include "system.h"
#include "config.h"
#include "env.h"

#include "fb.h"
#include "tiles.h"



U8 env_trainer = FALSE;
U8 env_invicible = FALSE;
U8 env_highlight = FALSE;

U8 env_depth = TRUE;

U8 env_lives = 0;
U8 env_bombs = 0;
U8 env_bullets = 0;
U32 env_score = 0;

U16 env_map = 0;
U16 env_submap = 0;
U8 env_changeSubmap = FALSE;


/*
 * FIXME counters positions in fp/px
 */
#ifdef GFXPC
#define DRAW_STATUS_SCORE_X 0x28
#define DRAW_STATUS_LIVES_X 0xE8
#define DRAW_STATUS_Y 0x08
#endif
#define DRAW_STATUS_BULLETS_X 0x68
#define DRAW_STATUS_BOMBS_X 0xA8
#ifdef GFXST
#define DRAW_STATUS_SCORE_X 0x20
#define DRAW_STATUS_LIVES_X 0xF0
#define DRAW_STATUS_Y 0
#endif


/*
 * env_paintGame
 *
 * paints the game environment (score, lives, bullets, bombs).
 */
void env_paintGame(void)
{
	S8 i;
	U32 sv;
	static U8 s[7] = {0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0xfe};

	tiles_setBank(0);

	for (i = 5, sv = env_score; i >= 0; i--)
	{
		s[i] = 0x30 + (U8)(sv % 10);
		sv /= 10;
	}

	tiles_paintListAt(s, DRAW_STATUS_SCORE_X, DRAW_STATUS_Y);

	for (i = 0; i < env_bullets; i++)
		tiles_paintAt(TILES_BULLET, DRAW_STATUS_BULLETS_X + i * 8, DRAW_STATUS_Y);

	for (i = 0; i < env_bombs; i++)
		tiles_paintAt(TILES_BOMB, DRAW_STATUS_BOMBS_X + i * 8, DRAW_STATUS_Y);

	for (i = 0; i < env_lives; i++)
		tiles_paintAt(TILES_RICK, DRAW_STATUS_LIVES_X + i * 8, DRAW_STATUS_Y);
}


/*
 * env_paintXtra
 *
 * paints the extra environment (cheats, modes...).
 */
void env_paintXtra(void)
{
	S8 i;
	U32 sv;
	static U8 s[8] = {'M', 0x30, 0x30, TILES_CRLF, 'S', 0x30, 0x30, TILES_NULL};
	static U8 c[8] = {'@', '@', '@', TILES_CRLF, '@', '@', '@', TILES_NULL};

	tiles_setBank(0);

#ifdef GFXPC
	tiles_setFilter(0xffff);
#endif

	tiles_paintAt(env_trainer ? 'T' : '@', 0, DRAW_STATUS_Y);
	tiles_paintAt(env_invicible ? 'I' : '@', 8, DRAW_STATUS_Y);
	tiles_paintAt(env_highlight ? 'H' : '@', 16, DRAW_STATUS_Y);

	/* show map number */
	for (i = 2, sv = env_map; i >= 1; i--) {
		s[i] = 0x30 + (U8)(sv % 10);
		sv /= 10;
	}

	/* show submap number */
	for (i = 6, sv = env_submap; i >= 5; i--) {
		s[i] = 0x30 + (U8)(sv % 10);
		sv /= 10;
	}

	tiles_paintListAt(s, 0, DRAW_STATUS_Y + 8*2);
}


/*
 * env_clearGame
 *
 * clears the game environment (repaints the map).
 */
void
env_clearGame(void)
{
	U8 i, *f;

#ifdef GFXPC
	tiles_setBank(map_tilesBank);
#endif
#ifdef GFXST
	tiles_setBank(0);
#endif

  f = fb_at(DRAW_STATUS_SCORE_X, DRAW_STATUS_Y);
#ifdef GFXPC
	for (i = 0; i < DRAW_STATUS_LIVES_X/8 + 6 - DRAW_STATUS_SCORE_X/8; i++)
		f = tile_paint(map_map[MAP_ROW_SCRTOP + (DRAW_STATUS_Y / 8)][i], f);
#endif
#ifdef GFXST
	for (i = 0; i < DRAW_STATUS_LIVES_X/8 + 6 - DRAW_STATUS_SCORE_X/8; i++)
		f = tiles_paint('@', f);
#endif
}






/* eof */