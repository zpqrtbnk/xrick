/*
 * xrick/src/system.c
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

#include <SDL.h>

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
	char s[1024];

	/* FIXME what is this? */
	/* change stdin to non blocking */
	/* fcntl(0, F_SETFL, fcntl (0, F_GETFL, 0) & ~FNDELAY); */
	/* NOTE HPUX: use ... is it OK on Linux ? */
	/* fcntl(0, F_SETFL, fcntl (0, F_GETFL, 0) & ~O_NDELAY); */

	/* prepare message */
	va_start(argptr, msg);
	vsprintf(s, msg, argptr);
	va_end(argptr);
	printf(s);
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

	ticks = SDL_GetTicks();

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
