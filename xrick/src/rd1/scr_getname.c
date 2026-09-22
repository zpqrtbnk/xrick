/*
 * xrick/src/scr_getname.c
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

#include "screens.h"

#include "game.h"
#include "draw.h"
#include "control.h"
#include "tiles.h"
#include "fb.h"

/*
 * local vars
 */
static U8 seq = 0;
static U8 x, y, p;
static U8 name[10];

#define TILE_POINTER '\072'
#define TILE_CURSOR '\073'
#define TOPLEFT_X 116
#define TOPLEFT_Y 64
#define NAMEPOS_X 120
#define NAMEPOS_Y 160
#define AUTOREPEAT_TMOUT 100


/*
 * prototypes
 */
static void pointer_show(U8);
static void name_update(void);
static void name_draw(void);


/*
 * Get name
 *
 * return: 0 while running, 1 when finished.
 */
U8
screen_getname(void)
{
  static U32 tm = 0;
  U8 i, j;

  if (seq == 0) {
    /* figure out if this is a high score */
    if (env_score < game_hscores[7].score)
      return SCREEN_DONE;

    /* prepare */
    tiles_setBank(0);
#ifdef GFXPC
    tiles_setFilter(0xffff);
#endif
    for (i = 0; i < 10; i++)
      name[i] = '@';
    x = y = p = 0;
    game_rects = &draw_SCREENRECT;
    seq = 1;
  }

	switch (seq) {
	case 1:  /* prepare screen */
		fb_clear();
#ifdef GFXPC
		tiles_setFilter(0xaaaa); /* red */
		tiles_paintListAt(screen_congrats, 32, 8);
#endif
#ifdef GFXPC
		tiles_setFilter(0xffff); /* yellow */
#endif
		tiles_paintListAt((U8 *)"PLEASE@ENTER@YOUR@NAME\376", 76, 40);
#ifdef GFXPC
		tiles_setFilter(0x5555); /* green */
#endif
	for (i = 0; i < 6; i++)
		for (j = 0; j < 4; j++)
			tiles_paintAt('A' + i + j * 6, TOPLEFT_X + i * 8 * 2, TOPLEFT_Y + j * 8 * 2);
#ifdef GFXST
    tiles_paintListAt((U8 *)"Y@Z@.@@@\074\373\374\375\376", TOPLEFT_X, TOPLEFT_Y + 64);
#endif
#ifdef GFXPC
    tiles_paintListAt((U8 *)"Y@Z@.@@@\074@\075@\376", TOPLEFT_X, TOPLEFT_Y + 64);
#endif
    name_draw();
    pointer_show(TRUE);
    seq = 2;
    break;

  case 2:  /* wait for key pressed */
    if (control_status & CONTROL_FIRE)
      seq = 3;
    if (control_status & CONTROL_UP) {
      if (y > 0) {
	pointer_show(FALSE);
	y--;
	pointer_show(TRUE);
	tm = sys_gettime();
      }
      seq = 4;
    }
    if (control_status & CONTROL_DOWN) {
      if (y < 4) {
	pointer_show(FALSE);
	y++;
	pointer_show(TRUE);
	tm = sys_gettime();
      }
      seq = 5;
    }
    if (control_status & CONTROL_LEFT) {
      if (x > 0) {
	pointer_show(FALSE);
	x--;
	pointer_show(TRUE);
	tm = sys_gettime();
      }
      seq = 6;
    }
    if (control_status & CONTROL_RIGHT) {
      if (x < 5) {
	pointer_show(FALSE);
	x++;
	pointer_show(TRUE);
	tm = sys_gettime();
      }
      seq = 7;
    }
    if (seq == 2)
      sys_sleep(50);
    break;

  case 3:  /* wait for FIRE released */
    if (!(control_status & CONTROL_FIRE)) {
      if (x == 5 && y == 4) {  /* end */
	i = 0;
	while (env_score < game_hscores[i].score)
	  i++;
	j = 7;
	while (j > i) {
	  game_hscores[j].score = game_hscores[j - 1].score;
	  for (x = 0; x < 10; x++)
	    game_hscores[j].name[x] = game_hscores[j - 1].name[x];
	  j--;
	}
	game_hscores[i].score = env_score;
	for (x = 0; x < 10; x++)
	  game_hscores[i].name[x] = name[x];
	seq = 99;
      }
      else {
	name_update();
	name_draw();
	seq = 2;
      }
    }
    else
      sys_sleep(50);
    break;

  case 4:  /* wait for UP released */
    if (!(control_status & CONTROL_UP) ||
	sys_gettime() - tm > AUTOREPEAT_TMOUT)
      seq = 2;
    else
      sys_sleep(50);
    break;

  case 5:  /* wait for DOWN released */
    if (!(control_status & CONTROL_DOWN) ||
	sys_gettime() - tm > AUTOREPEAT_TMOUT)
      seq = 2;
    else
      sys_sleep(50);
    break;

  case 6:  /* wait for LEFT released */
    if (!(control_status & CONTROL_LEFT) ||
	sys_gettime() - tm > AUTOREPEAT_TMOUT)
      seq = 2;
    else
      sys_sleep(50);
    break;

  case 7:  /* wait for RIGHT released */
    if (!(control_status & CONTROL_RIGHT) ||
	sys_gettime() - tm > AUTOREPEAT_TMOUT)
      seq = 2;
    else
      sys_sleep(50);
    break;

  }

  if (control_status & CONTROL_EXIT)  /* check for exit request */
    return SCREEN_EXIT;

  if (seq == 99) {  /* seq 99, we're done */
    fb_clear();
    seq = 0;
    return SCREEN_DONE;
  }
  else
    return SCREEN_RUNNING;
}


static void
pointer_show(U8 show)
{
#ifdef GFXPC
	tiles_setFilter(0xaaaa); /* red */
#endif
	tiles_paintAt(show == TRUE ? TILE_POINTER : '@',
		TOPLEFT_X + x * 8 * 2, TOPLEFT_Y + y * 8 * 2 + 8);
}

static void
name_update(void)
{
  U8 i;

  i = x + y * 6;
  if (i < 26 && p < 10)
    name[p++] = 'A' + i;
  if (i == 26 && p < 10)
    name[p++] = '.';
  if (i == 27 && p < 10)
    name[p++] = '@';
  if (i == 28 && p > 0) {
    p--;
  }
}

/* FIXME WHAT IS P??? */

static void
name_draw(void)
{
	U8 i;

#ifdef GFXPC
	tiles_setFilter(0xaaaa); /* red */
#endif
	for (i = 0; i < p; i++)
		tiles_paintAt(name[i], NAMEPOS_X + i * 8, NAMEPOS_Y);
	for (i = p; i < 10; i++)
		tiles_paintAt(TILE_CURSOR, NAMEPOS_X + i * 8, NAMEPOS_Y);

#ifdef GFXST
	for (i = 0; i < 10; i++)
		tiles_paintAt('@', NAMEPOS_X + i * 8, NAMEPOS_Y + 8);
	tiles_paintAt(TILE_POINTER, NAMEPOS_X + 8 * (p < 9 ? p : 9), NAMEPOS_Y + 8);
#endif
}


/* eof */
