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

#include "config.h"

#ifdef ENABLE_JOYSTICK

#include "system.h"
#include "debug.h"

static SDL_Joystick *j = NULL;

void
sysjoy_init(void)
{
  int i, jcount;
  SDL_JoystickID *jids;

  if (!SDL_InitSubSystem(SDL_INIT_JOYSTICK)) {
    IFDEBUG_JOYSTICK(
      sys_printf("xrick/joystick: can not initialize joystick subsystem\n");
      );
    return;
  }

  /* SDL3 replaced SDL_NumJoysticks()/SDL_JoystickOpen(index) with an explicit
     instance-id array from SDL_GetJoysticks(). */
  jids = SDL_GetJoysticks(&jcount);
  if (!jids || !jcount) {  /* no joystick on this system */
    IFDEBUG_JOYSTICK(sys_printf("xrick/joystick: no joystick available\n"););
    SDL_free(jids);
    return;
  }

  /* use the first joystick that we can open */
  for (i = 0; i < jcount; i++) {
    j = SDL_OpenJoystick(jids[i]);
    if (j)
      break;
  }
  SDL_free(jids);

  /* enable events -- SDL3 takes a plain bool, no separate SDL_ENABLE constant */
  SDL_SetJoystickEventsEnabled(true);
}

void
sysjoy_shutdown(void)
{
  if (j)
    SDL_CloseJoystick(j);
}

#endif /* ENABLE_JOYSTICK */

/* eof */

