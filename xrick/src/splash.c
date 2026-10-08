/*
 * Copyright (C) 1998-NOW bigorno (bigorno@bigorno.net). All rights reserved.
 *
 * The use and distribution terms for this software are contained in the file
 * named README, which can be found in the root of this distribution. By
 * using this software in any fashion, you are agreeing to be bound by the
 * terms of this license.
 *
 * You must not remove this notice, or any other, from this software.
 */

/*
 * The xrick splash screen, shown once at startup before either game (xrick.c): a
 * picture, no sound, on screen for SPLASH_MS (splash.h) including its fade in and fade
 * out, not skippable.
 *
 * The picture is src/splash/splash.png, compiled in (src/splash/embed.sh) and decoded by
 * SDL. It does not go through the game's 8-bit frame buffer: sysvid_showImage scales it
 * to the letterboxed window area, so its detail follows -zoom and fullscreen. VERSION
 * (config.h) is written onto it at load time, with SDL's built-in debug font.
 *
 * The fades are RD2's (src/rd2/rd2_render.c rd2_19134 / rd2_1919e): 8 steps, one per
 * game frame, each moving every colour channel by one ST level (1/7 of full scale) --
 * fade in: darkened by 7, 6 .. 0 levels; fade out: by 1, 2 .. 7 (black), 8. RD2 runs a
 * game frame every 2 VBLs at 50 Hz (kb2/algo-flow.md, [$18ed8] = 2): 40 ms a step.
 *
 * Desktop: splash_run waits here. Web: the browser only draws when control returns
 * to it, so xrick.c calls splash_start and the game's web frame waits for splash_done.
 */

#include <stdlib.h> /* exit */

#include <SDL3/SDL.h>

#include "system.h"
#include "sysvid.h"
#include "splash.h"



#define FADE_STEPS 8    /* RD2: rd2_19134 / rd2_1919e */
#define FADE_STEP_MS 40 /* RD2: one game frame, 2 VBLs at 50 Hz */
#define FADE_MS (FADE_STEPS * FADE_STEP_MS)
#define ST_LEVELS 7     /* an ST colour channel is 0..7 */

/* the version line: SDL's built-in 8x8 debug font, scaled up, centred, near the top */
#define VERSION_TEXT "#" VERSION
#define VERSION_SCALE 2
#define VERSION_Y 20    /* image pixels from the top */

static const unsigned char splash_png[] = {
#include "splash/splash.png.inc"
};

static SDL_Surface *image; /* RGBA32, while the splash is up */
static SDL_Surface *shown; /* image darkened by <level>, what is on screen */
static int level;          /* ST levels taken off every channel, 0 = full picture */
static Uint64 start;
static int done;



/*
 * the fade level at <t> ms into the splash
 */
static int fade_level(Uint64 t)
{
	int k;

	if (t < FADE_MS)  /* fade in: 7 .. 0 */
		return ST_LEVELS - (int)(t / FADE_STEP_MS);
	if (t + FADE_MS >= SPLASH_MS)  /* fade out: 1 .. 8, 7 and up is black */
	{
		k = (int)((t + FADE_MS - SPLASH_MS) / FADE_STEP_MS) + 1;
		return k > ST_LEVELS ? ST_LEVELS : k;
	}
	return 0;
}

/*
 * write VERSION onto <img>, in the picture's text brown (sampled from splash.png)
 */
static void draw_version(SDL_Surface *img)
{
	SDL_Renderer *r = SDL_CreateSoftwareRenderer(img);
	float w = (float)(sizeof(VERSION_TEXT) - 1) * SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE;

	if (!r)
		return;
	SDL_SetRenderScale(r, VERSION_SCALE, VERSION_SCALE);
	SDL_SetRenderDrawColor(r, 0x65, 0x32, 0x01, 0xff);
	SDL_RenderDebugText(r, ((float)img->w / VERSION_SCALE - w) / 2,
		(float)VERSION_Y / VERSION_SCALE, VERSION_TEXT);
	SDL_FlushRenderer(r);
	SDL_DestroyRenderer(r);
}

/*
 * show the picture darkened by <lvl> ST levels
 */
static void show(int lvl)
{
	int sub = lvl * 255 / ST_LEVELS, x, y;

	level = lvl;
	if (lvl == 0)
	{
		sysvid_showImage(image);
		return;
	}
	for (y = 0; y < image->h; y++)
	{
		const Uint8 *s = (const Uint8 *)image->pixels + y * image->pitch;
		Uint8 *d = (Uint8 *)shown->pixels + y * shown->pitch;

		for (x = 0; x < image->w; x++, s += 4, d += 4)
		{
			d[0] = (Uint8)(s[0] > sub ? s[0] - sub : 0);
			d[1] = (Uint8)(s[1] > sub ? s[1] - sub : 0);
			d[2] = (Uint8)(s[2] > sub ? s[2] - sub : 0);
			d[3] = s[3];
		}
	}
	sysvid_showImage(shown);
}



/*
 * splash_start
 *
 * see splash.h
 */
void splash_start(void)
{
	SDL_IOStream *io;
	SDL_Surface *s = NULL;

#if SDL_VERSION_ATLEAST(3, 4, 0)
	io = SDL_IOFromConstMem(splash_png, sizeof(splash_png));
	if (io)
		s = SDL_LoadPNG_IO(io, true);
	if (s)
	{
		image = SDL_ConvertSurface(s, SDL_PIXELFORMAT_RGBA32);
		SDL_DestroySurface(s);
	}
	if (image)
		draw_version(image);
	if (image)
		shown = SDL_CreateSurface(image->w, image->h, SDL_PIXELFORMAT_RGBA32);
	if (!image || !shown)
	{
		sys_printf("xrick/splash: can not load the splash image (%s)\n", SDL_GetError());
		SDL_DestroySurface(image);
		image = NULL;
	}

	start = SDL_GetTicks();
	done = 0;
	if (image)
		show(fade_level(0));
#else
	/* SDL_LoadPNG_IO is SDL 3.4.0+ (e.g. Debian's SDL 3.2 in WSL): no splash */
	(void)io;
	(void)s;
	(void)splash_png;
	(void)show;
	sys_printf("xrick/splash: skipped, SDL %d.%d.%d has no PNG loader (3.4.0+)\n",
		SDL_MAJOR_VERSION, SDL_MINOR_VERSION, SDL_MICRO_VERSION);
	done = 1;
#endif
}



/*
 * splash_done
 *
 * see splash.h. also steps the fades, so it is called every frame
 */
int splash_done(void)
{
	Uint64 t;
	int lvl;

	if (done)
		return 1;
	t = SDL_GetTicks() - start;
	if (t < SPLASH_MS)
	{
		if (image && (lvl = fade_level(t)) != level)
			show(lvl);
		return 0;
	}

	SDL_DestroySurface(image);
	SDL_DestroySurface(shown);
	image = shown = NULL;
	/* not skippable, and keys pressed meanwhile do not reach the game either */
	SDL_FlushEvents(SDL_EVENT_KEY_DOWN, SDL_EVENT_KEY_UP);
	done = 1;
	return 1;
}



/*
 * splash_run
 *
 * see splash.h
 */
void splash_run(void)
{
	SDL_Event e;

	splash_start();
	while (!splash_done())
	{
		while (SDL_PollEvent(&e))
		{
			if (e.type == SDL_EVENT_QUIT)
				exit(0); /* closing the window still quits (sys_shutdown runs atexit) */
			if (e.type == SDL_EVENT_WINDOW_EXPOSED && image)
				show(level);
		}
		SDL_Delay(5);
	}
}

/* eof */
