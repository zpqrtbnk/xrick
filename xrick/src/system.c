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

#include <SDL3/SDL.h>

#include <stdarg.h>   /* args for sys_panic */
#include <fcntl.h>    /* fcntl in sys_panic */
#include <stdio.h>    /* printf */
#include <stdlib.h>

#include "system.h"

/*
 * Panic
 */
void
sys_panic(char *err, ...)
{
	va_list argptr;
	char s[1024];

	/* FIXME what is this? */
	/* change stdin to non blocking */
	/*fcntl(0, F_SETFL, fcntl (0, F_GETFL, 0) & ~FNDELAY);*/
	/* NOTE HPUX: use ... is it OK on Linux ? */
	/* fcntl(0, F_SETFL, fcntl (0, F_GETFL, 0) & ~O_NDELAY); */

	/* prepare message */
	va_start(argptr, err);
	vsprintf(s, err, argptr);
	va_end(argptr);

	/* print message and die */
	printf("%s\npanic!\n", s);
	exit(1);
}


/*
 * Print a message
 */
void
sys_printf(char *msg, ...)
{
#ifdef ENABLE_LOG
	va_list argptr;
	/*
	 * 4096, and vsnprintf rather than vsprintf -- see ../../demo.md §5.
	 *
	 * The longest caller is sysarg_fail's usage text: 1108 characters of literal
	 * before its %d/%s are expanded, into what used to be a 1024 byte buffer.
	 * `xrick -h` was already writing past the end of the stack frame and merely
	 * getting away with it; adding the -demo / -record lines pushed it far
	 * enough to crash with no output at all. vsnprintf cannot overrun whatever
	 * the size is, and 4096 is enough for the usage text to be printed in full.
	 */
	char s[4096];

	/* FIXME what is this? */
	/* change stdin to non blocking */
	/* fcntl(0, F_SETFL, fcntl (0, F_GETFL, 0) & ~FNDELAY); */
	/* NOTE HPUX: use ... is it OK on Linux ? */
	/* fcntl(0, F_SETFL, fcntl (0, F_GETFL, 0) & ~O_NDELAY); */

	/* prepare message */
	va_start(argptr, msg);
	vsnprintf(s, sizeof s, msg, argptr);
	va_end(argptr);
	/* fputs, not printf(s): s is data, and may legitimately contain a '%' --
	   a data path name, for instance. */
	fputs(s, stdout);
#endif
}

/*
 * Return number of milliseconds elapsed since first call
 */
U32
sys_gettime(void)
{
	static U32 ticks_base = 0;
	U32 ticks;

	/* SDL3 widened SDL_GetTicks() to 64-bit; truncate explicitly -- this is a
	   monotonic ms counter used only relative to ticks_base, so the wraparound
	   point moving out to ~49 days of Uint32 range is inconsequential here. */
	ticks = (U32)SDL_GetTicks();

	if (!ticks_base)
		ticks_base = ticks;

	return ticks - ticks_base;
}

/*
 * Sleep a number of milliseconds
 */
void
sys_sleep(int s)
{
	SDL_Delay(s);
}

/* eof */
