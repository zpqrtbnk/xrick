/*
 * xrick/src/scroller.c
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

#include <stdlib.h>

#include "system.h"
#include "game.h"
#include "env.h"

#include "scroller.h"

#include "debug.h"
#include "draw.h"
#include "maps.h"
#include "ents.h"

static U8 period;

/*
 * Scroll up
 *
 */
U8
scroll_up(void)
{
  U8 i, j;
  static U8 n = 0;

  /* last call: restore */
  if (n == 8) {
    n = 0;
    game_period = period;
    return SCROLL_DONE;
  }

  /* first call: prepare */
  if (n == 0) {
    period = game_period;
    game_period = SCROLL_PERIOD;
  }

  /* translate map */
  for (i = MAP_ROW_SCRTOP; i < MAP_ROW_HBBOT; i++)
    for (j = 0x00; j < 0x20; j++)
      map_map[i][j] = map_map[i + 1][j];

  /* translate entities */
  for (i = 0; ent_ents[i].n != 0xFF; i++) {
    if (ent_ents[i].n) {
      ent_ents[i].ysave -= 8;
      ent_ents[i].trig_y -= 8;
      ent_ents[i].y -= 8;
      /*
       * ENT_YDEAD, not `y & 0x8000` -- review-log.md R2.1 / defect #20.
       * The PC translates entities in ONE shared routine (0x10B2) used by both scroll
       * directions, and it applies BOTH bounds every time:
       *   10D5  test ah,0x80 / jz     -> y < 0      -> mov byte[si],0
       *   10E3  cmp ax,0x140 / jc     -> y >= 0x140 -> mov byte[si],0
       * The port split them, keeping only `y < 0` here and only `y > 0x140` in
       * scroll_down -- and `>` where the PC uses `>=`, so an entity sitting exactly on
       * 0x140 survived a scroll the PC would have removed.
       * The ST bound is 0x142, not 0x140 (0x4B0C8 `cmp.w #0x142,D2 / ble`, and 0x4D352),
       * which is what ENT_YDEAD already encodes per platform.
       */
      if (ENT_YDEAD(ent_ents[i].y)) {
	IFDEBUG_SCROLLER(
	  sys_printf("xrick/scroller: entity %#04X is gone\n", i);
	  );
	ent_ents[i].n = 0;
      }
    }
  }

  /* display */
	maps_paint();
  ents_paintAll();
  env_paintGame();
  map_frow++;

  /* loop */
  if (n++ == 7) {
    /* activate visible entities */
    ent_actvis(map_frow + MAP_ROW_HBTOP, map_frow + MAP_ROW_HBBOT);

    /* prepare map */
    map_expand();

    /* display */
	maps_paint();
    ents_paintAll();
    env_paintGame();
  }

  game_rects = &draw_SCREENRECT;

  return SCROLL_RUNNING;
}

/*
 * Scroll down
 *
 */
U8
scroll_down(void)
{
  U8 i, j;
  static U8 n = 0;

  /* last call: restore */
  if (n == 8) {
    n = 0;
    game_period = period;
    return SCROLL_DONE;
  }

  /* first call: prepare */
  if (n == 0) {
    period = game_period;
    game_period = SCROLL_PERIOD;
  }

  /* translate map */
  for (i = MAP_ROW_SCRBOT; i > MAP_ROW_HTTOP; i--)
    for (j = 0x00; j < 0x20; j++)
      map_map[i][j] = map_map[i - 1][j];

  /* translate entities */
  for (i = 0; ent_ents[i].n != 0xFF; i++) {
    if (ent_ents[i].n) {
      ent_ents[i].ysave += 8;
      ent_ents[i].trig_y += 8;
      ent_ents[i].y += 8;
      /* both bounds, per-platform -- see scroll_up above (defect #20). */
      if (ENT_YDEAD(ent_ents[i].y)) {
	IFDEBUG_SCROLLER(
	  sys_printf("xrick/scroller: entity %#04X is gone\n", i);
	  );
	ent_ents[i].n = 0;
      }
    }
  }

  /* display */
	maps_paint();
  ents_paintAll();
  env_paintGame();
  map_frow--;

  /* loop */
  if (n++ == 7) {
    /* activate visible entities */
    ent_actvis(map_frow + MAP_ROW_HTTOP, map_frow + MAP_ROW_HTBOT);

    /* prepare map */
    map_expand();

    /* display */
	maps_paint();
    ents_paintAll();
    env_paintGame();
  }

  game_rects = &draw_SCREENRECT;

  return SCROLL_RUNNING;
}

/* eof */
