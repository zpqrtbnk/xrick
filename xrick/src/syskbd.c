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

#include "system.h"

/*
 * Using the SDL_SCANCODE_xxx keysyms, which map to a QWERTY keyboard.
 * We get them via SDL_KEYDOWN.
 * We do *not* use SDL_TEXTINPUT nor SDLK_ to get true key mappings, so
 * for instance a binding to 'q' is the key labelled 'a' on an AZERTY keyboard.
 */

U8 syskbd_up = SDL_SCANCODE_O;
U8 syskbd_down = SDL_SCANCODE_K;
U8 syskbd_left = SDL_SCANCODE_S;
U8 syskbd_right = SDL_SCANCODE_D;
U8 syskbd_pause = SDL_SCANCODE_P;
U8 syskbd_end = SDL_SCANCODE_E;
U8 syskbd_xtra = SDL_SCANCODE_ESCAPE;
U8 syskbd_fire = SDL_SCANCODE_SPACE;
U8 syskbd_sndset = SDL_SCANCODE_F10;  /* rd2 only: the ST's S key ($1f), debug sound-id remap [$1a5ce] */

/* eof */


