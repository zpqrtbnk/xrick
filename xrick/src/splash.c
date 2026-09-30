/*
 * xrick/src/splash.c
 *
 * The xrick splash screen, shown once at startup before either game (xrick.c): a
 * picture, no sound, on screen for SPLASH_MS (splash.h), not skippable.
 *
 * The picture is src/splash/splash.png, compiled in (src/splash/embed.sh) and decoded by
 * SDL. It does not go through the game's 8-bit frame buffer: sysvid_showImage scales it
 * to the letterboxed window area, so its detail follows -zoom and fullscreen.
 *
 * Desktop: splash_run waits here. Web: the browser only draws when control returns
 * to it, so xrick.c calls splash_start and the game's web frame waits for splash_done.
 */

#include <stdlib.h> /* exit */

#include <SDL3/SDL.h>

#include "system.h"
#include "sysvid.h"
#include "splash.h"



static const unsigned char splash_png[] = {
#include "splash/splash.png.inc"
};

static SDL_Surface *image; /* RGBA32, while the splash is up */
static Uint64 start;
static int done;



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
		sysvid_showImage(image);
	else
		sys_printf("xrick/splash: can not load the splash image (%s)\n", SDL_GetError());

	start = SDL_GetTicks();
	done = 0;
#else
	/* SDL_LoadPNG_IO is SDL 3.4.0+ (e.g. Debian's SDL 3.2 in WSL): no splash */
	(void)io;
	(void)s;
	(void)splash_png;
	sys_printf("xrick/splash: skipped, SDL %d.%d.%d has no PNG loader (3.4.0+)\n",
		SDL_MAJOR_VERSION, SDL_MINOR_VERSION, SDL_MICRO_VERSION);
	done = 1;
#endif
}



/*
 * splash_done
 *
 * see splash.h
 */
int splash_done(void)
{
	if (done)
		return 1;
	if (SDL_GetTicks() - start < SPLASH_MS)
		return 0;

	SDL_DestroySurface(image);
	image = NULL;
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
				sysvid_showImage(image);
		}
		SDL_Delay(10);
	}
}

/* eof */
