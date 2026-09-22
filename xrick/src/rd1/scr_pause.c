/*
 * xrick/src/scr_pause.c
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

#include <stddef.h> /* NULL */

#include "system.h"
#include "config.h"
#include "env.h"

#include "game.h"
#include "screens.h"

#include "draw.h"
#include "control.h"
#include "ents.h"

#include "tiles.h"
#include "maps.h"


/*
 * Display the pause indicator
 */
void
screen_pause(U8 pause)
{
	if (pause == TRUE)
	{
		tiles_setBank(0);
#ifdef GFXPC
		tiles_setFilter(0xaaaa);
#endif
		tiles_paintListAt(screen_pausedtxt, 120, 80);
	}
	else
	{
#ifdef GFXPC
		tiles_setFilter(0xffff);
#endif
		maps_paint();
		ents_paintAll();
		env_paintGame();
	}
	game_rects = &draw_SCREENRECT;
}


/* eof */

