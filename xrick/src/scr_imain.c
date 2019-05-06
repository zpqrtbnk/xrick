/*
 * xrick/src/scr_imain.c
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

#include <stdio.h>  /* sprintf */

#include "system.h"
#include "game.h"
#include "screens.h"
#include "sysvid.h"
#include "sounds.h"

#include "draw.h"
#include "pics.h"
#include "control.h"
#include "fb.h"

#include "tiles.h"

#define IMAIN_PERIOD 50;

/*
 * Main introduction
 *
 * return: SCREEN_RUNNING, SCREEN_DONE, SCREEN_EXIT
 */
U8
screen_introMain(void)
{
	static U8 seq = 0;
	static U8 seen = 0;
	static U8 first = TRUE;
	static U8 period = 0;
	static U32 tm = 0;
	U8 i, s[32];

	if (seq == 0)
	{
		tiles_setBank(0);
		if (first == TRUE)
			seq = 1;
		else
			seq = 10;
		period = game_period;
		game_period = IMAIN_PERIOD;
		game_rects = &draw_SCREENRECT;
#ifdef ENABLE_SOUND
		sounds_setMusic("sounds/tune5.wav", -1);
#endif
	}

	switch (seq)
	{
		case 1:  /* dispay hall of fame */
			fb_clear();
			sysvid_setGamma(0);
			tm = sys_gettime();

#ifdef GFXPC
			/* Rick Dangerous title */
			tiles_setFilter(0xaaaa);
			tiles_paintList(screen_imainrdt, fb_at(32, 16));

			/* Core Design copyright + press space to start */
			tiles_setFilter(0x5555);
			tiles_paintList(screen_imaincdc, fb_at(64, 80));
#endif

#ifdef GFXST
			img_paintPic(0, 0, 0x140, 0xc8, pic_splash);
#endif

			game_period = period/2;
			seq = 2;
			break;

		case 2: /* fade-in */
			if (fb_fadeIn())
			{
				game_period = IMAIN_PERIOD;
				seq = 3;
			}
			break;

		case 3:  /* wait for key pressed or timeout */
			if (control_status & CONTROL_FIRE)
				seq = 4;
			else if (sys_gettime() - tm > SCREEN_TIMEOUT)
			{
				seen++;
				game_period = period/2;
				seq = 8;
			}
			break;

		case 4:  /* wait for key released */
			if (!(control_status & CONTROL_FIRE))
			{
				if (seen++ == 0)
					seq = 8;
				else
				{
					game_period = period/2;
					seq = 28;
				}
			}
			break;

		case 8: /* fade-out */
			if (fb_fadeOut())
			{
				game_period = IMAIN_PERIOD;
				seq = 10;
			}
			break;

		case 10:  /* display Rick Dangerous title and Core Design copyright */
			fb_clear();
			tm = sys_gettime();

			/* hall of fame title */
#ifdef GFXPC
			tiles_setFilter(0xaaaa);
			tiles_paintListAt(screen_imainhoft, 32, 0);
#endif
#ifdef GFXST
			img_paintPic(0, 0, 0x140, 0x20, pic_haf);
#endif

			/* hall of fame content */
#ifdef GFXPC
			tiles_setFilter(0x5555);
#endif
			for (i = 0; i < 8; i++)
			{
				sprintf((char *)s, "%06d@@@....@@@%s",
					game_hscores[i].score, game_hscores[i].name);
				s[26] = TILES_NULL;
				tiles_paintListAt(s, 56, 40 + i*2*8);
			}

			game_period = period/2;
			seq = 11;
			break;

		case 11: /* fade-in */
			if (fb_fadeIn())
			{
				game_period = IMAIN_PERIOD;
				seq = 12;
			}
			break;

		case 12:  /* wait for key pressed or timeout */
			if (control_status & CONTROL_FIRE)
				seq = 13;
			else if (sys_gettime() - tm > SCREEN_TIMEOUT)
			{
				seen++;
				seq = 18;
			}
			break;

		case 13:  /* wait for key released */
			if (!(control_status & CONTROL_FIRE))
			{
				if (seen++ == 0)
					seq = 18;
				else
				{
					game_period = period/2;
					seq = 28;
				}
			}
			break;

		case 18: /* fade-out */
			if (fb_fadeOut())
			{
				game_period = IMAIN_PERIOD;
				seq = 1;
			}
			break;

		case 28: /* fade-out */
			if (fb_fadeOut())
			{
				game_period = IMAIN_PERIOD;
				seq = 30;
			}
			break;

	}

	if (control_status & CONTROL_EXIT)  /* check for exit request */
		return SCREEN_EXIT;

	if (seq == 30) /* we're done */
	{
		fb_clear();
		seq = 0;
		seen = 0;
		first = FALSE;
		game_period = period;
		sysvid_setGamma(255);
		return SCREEN_DONE;
	}
	else
		return SCREEN_RUNNING;
}

/* eof */


