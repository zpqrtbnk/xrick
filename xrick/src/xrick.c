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

#include "system.h"
#include "sysarg.h"
#include "sysvid.h"
#include "game.h"
#include "fb.h"

#include <SDL3/SDL.h>
#include <signal.h>
#include <stdlib.h> /* atexit, exit -- SDL2's SDL.h pulled this in transitively, SDL3's doesn't */
#ifdef __WIN32__
#include <windows.h>
#endif


/*
 * Sets a console, if possible
 */
/* review-plan.md R0.1: implicit int return is a hard error in modern C. Windows-only
   body; build portability only. */
static void setConsole(void)
{
	// NOTE: does not handle parent process console being redirected
	// eg "./xrick > stdout.txt" still writes to the actual console

#ifdef __WIN32__
	// try to attach to parent process console
	if (AttachConsole(-1))
	{
		// reopen (SDL2 closed everything)
		freopen("CONIN$", "r", stdin);
		freopen("CONOUT$", "w", stdout);
		freopen("CONOUT$", "w", stderr);
	}
#endif
}



 /*
  * Initialize system
  */
void
sys_init(int argc, char** argv)
{
	setConsole();
	sys_printf("xrick\n");

	sysarg_init(argc, argv);

	// FIXME not writing to stdxxx.txt files anymore?
	// SDL3 dropped SDL_INIT_TIMER (timers are always available) and SDL_INIT_EVENTS
	// (implied by SDL_INIT_VIDEO); SDL_Init now returns bool, true on success.
	if (!SDL_Init(SDL_INIT_VIDEO))
		sys_panic("xrick/video: could not init SDL\n");

	// FIXME logging
	// Solved (embarrassingly simple) :
	// SDL 1.2 did that outputting into files, SDL 2 does not.However most information(toturials and such) on the net is about SDL 1.2 since SDL 2 is new.
	// Furthermore compiling with -mwindows sends all stdout and stderr to null.
	// Compiling without solves my problem.

	//SDL_Log("SDL!");

	sysvid_init(FB_WIDTH, FB_HEIGHT);
#ifdef ENABLE_JOYSTICK
	sysjoy_init();
#endif
#ifdef ENABLE_SOUND
	if (sysarg_args_nosound == 0)
		syssnd_init();
#endif

	atexit(sys_shutdown);
	signal(SIGINT, exit);
	signal(SIGTERM, exit);
}



/*
 * Shutdown system
 */
void
sys_shutdown(void)
{
#ifdef ENABLE_SOUND
	syssnd_shutdown();
#endif
#ifdef ENABLE_JOYSTICK
	sysjoy_shutdown();
#endif
	sysvid_shutdown();

	SDL_Quit();
}



/*
 * main
 */
int
main(int argc, char *argv[])
{
	sys_init(argc, argv);

	game_run();

	sys_shutdown();
	return 0;
}

/* eof */
