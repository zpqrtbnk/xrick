/*
 * xrick/src/scr_imap.c
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

#include <stdio.h>

#include "system.h"
#include "config.h"
#include "env.h"

#include "screens.h"

#include "game.h"
#include "rects.h"
#include "draw.h"
#include "control.h"
#include "maps.h"
#include "tiles.h"
#include "sprites.h"
#include "fb.h"

/*
 * local vars
 */
static U16 step;              /* current step */
static U16 count;             /* number of loops for current step */
static U16 run;               /* 1 = run, 0 = no more step */
static U8 flipflop;           /* flipflop for top, bottom, left, right */
static U8 spnum;             /* sprite number */
static U16 spx, spdx;         /* sprite x position and delta */
static U16 spy, spdy;         /* sprite y position and delta */
static U16 spbase, spoffs;    /* base, offset for sprite numbers table */
static U8 seq = 0;            /* anim sequence */

static rect_t anim_rect = { 120, 16, 64, 64, NULL }; /* anim rectangle */

/*
 * prototypes
 */
static void drawtb(void);
static void drawlr(void);
static void drawsprite(void);
static void drawcenter(void);
static void nextstep(void);
static void anim(void);
static void init(void);

/*
 * Map introduction
 *
 * return: SCREEN_RUNNING, SCREEN_DONE, SCREEN_EXIT
 */
U8 screen_introMap(void)
{
	switch (seq)
	{
		case 0: /* initialize */
			fb_clear();
			sysvid_setGamma(0);

#ifdef GFXPC
			tiles_setBank(1);
			tiles_setFilter(0xaaaa);
#endif
#ifdef GFXST
			tiles_setBank(0);
#endif
			tiles_paintListAt(maps_intros[env_map].title, 32, 0);

#ifdef GFXPC
			tiles_setFilter(0x5555);
#endif
			tiles_paintListAt(maps_intros[env_map].body, 32, 96);

#ifdef GFXPC
			tiles_setFilter(0xffff);
#endif

			init();
			nextstep();
			drawcenter();
			drawtb();
			drawlr();
			drawsprite();
			control_last = 0;

			//game_rects = &draw_SCREENRECT;

#ifdef ENABLE_SOUND
			sounds_setMusic(map_maps[env_map].tune, 1);
#endif

			seq = 1;
			break;

		case 1: /* fade-in */
			if (fb_fadeIn())
				seq = 10;
			break;

		case 10:  /* top and bottom borders */
			if (control_status & CONTROL_FIRE)
			{
				seq = 20;
			}
			else
			{
				drawtb();
				game_rects = &anim_rect;
				seq = 12;
			}
			break;

		case 12:  /* background and sprite */
			anim();
			drawcenter();
			drawsprite();
			game_rects = &anim_rect;
			seq = 13;
			break;

		case 13:  /* all borders */
			drawtb();
			drawlr();
			game_rects = &anim_rect;
			seq = 10;
			break;

		case 20:  /* wait for key release */
			if (!(control_status & CONTROL_FIRE))
				seq = 21;
			else
				sys_sleep(50);
			break;

		case 21:
			if (fb_fadeOut())
				seq = 30;
			break;
	}

	if (control_status & CONTROL_EXIT)  /* check for exit request */
		return SCREEN_EXIT;

	if (seq == 30)
	{
		fb_clear();
		sysvid_setGamma(255);
		seq = 0;
		return SCREEN_DONE;
	}
	else
		return SCREEN_RUNNING;
}


/*
 * Display top and bottom borders (0x1B1F)
 *
 */
static void
drawtb(void)
{
	U8 i;

	flipflop++;
	if (flipflop & 0x01)
	{
		for (i = 0; i < 6; i++)
			tiles_paintAt(0x40, 128 + i * 8, 16);
		for (i = 0; i < 6; i++)
			tiles_paintAt(0x06, 128 + i * 8, 72);
	}
	else
	{
		for (i = 0; i < 6; i++)
			tiles_paintAt(0x05, 128 + i * 8, 16);
		for (i = 0; i < 6; i++)
			tiles_paintAt(0x40, 128 + i * 8, 72);
	}
}


/*
 * Display left and right borders (0x1B7C)
 *
 */
static void
drawlr(void)
{
	U8 i;

	if (flipflop & 0x02)
	{
		for (i = 0; i < 8; i++)
		{
			tiles_paintAt(0x04, 120, 16 + i * 8);
			tiles_paintAt(0x04, 176, 16 + i * 8);
		}
	}
	else
	{
		for (i = 0; i < 8; i++)
		{
			tiles_paintAt(0x2B, 120, 16 + i * 8);
			tiles_paintAt(0x2B, 176, 16 + i * 8);
		}
	}
}


/*
 * Draw the sprite (0x19C6)
 *
 */
static void
drawsprite(void)
{
	U8 x = 128 + ((spx << 1) & 0x1C);
	U8 y = 24 + (spy << 1);
	sprites_paint(spnum, x, y);
}


/*
 * Draw the background (0x1AF1)
 *
 */
static void
drawcenter(void)
{
	static U8 tn0[] = { 0x07, 0x5B, 0x7F, 0xA3, 0xC7 };
	U8 i, j, tn;

	tn = tn0[env_map];
	for (i = 0; i < 6; i++)
		for (j = 0; j < 6; j++)
			tiles_paintAt(tn++, 128 + 8 * j, 24 + 8 * i);
}


/*
 * Next Step (0x1A74)
 *
 */
static void
nextstep(void)
{
	if (screen_imapsteps[step].count)
	{
		count = screen_imapsteps[step].count;
		spdx = screen_imapsteps[step].dx;
		spdy = screen_imapsteps[step].dy;
		spbase = screen_imapsteps[step].base;
		spoffs = 0;
		step++;
	}
	else
	{
		run = 0;
	}
}


/*
 * Anim (0x1AA8)
 *
 */
static void
anim(void)
{
	U8 i;

	if (run)
	{
		i = screen_imapsl[spbase + spoffs];
		if (i == 0)
		{
			spoffs = 0;
			i = screen_imapsl[spbase];
		}
		spnum = i;
		spoffs++;
		spx += spdx;
		spy += spdy;
		count--;
		if (count == 0)
			nextstep();
	}
}


/*
 * Initialize (0x1A43)
 *
 */
static void
init(void)
{
	run = 0; run--;
	step = screen_imapsofs[env_map];
	spx = screen_imapsteps[step].dx;
	spy = screen_imapsteps[step].dy;
	step++;
	spnum = 0; /* NOTE spnum in [8728] is never initialized ? */
}

/* eof */



