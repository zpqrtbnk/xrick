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
 * OpenGL shader chain between the game's frame and the screen (src/sysvid_gl.c).
 * Only built with ENABLE_SHADERS (config.h); sysvid.c falls back to its SDL_Renderer
 * path when this is off or when sysvid_gl_init fails.
 */

#ifndef _SYSVID_GL_H
#define _SYSVID_GL_H

#include "config.h"

#ifdef ENABLE_SHADERS

#include <SDL3/SDL.h>

/* creates a win_w x win_h window with a GL context and builds the shader chain for
   a fb_w x fb_h frame; NULL (everything released, error logged) on failure */
extern SDL_Window *sysvid_gl_init(const char *title, int win_w, int win_h, SDL_WindowFlags flags,
	int fb_w, int fb_h);
/* runs the chain over one full frame of RGBA8 bytes and presents it */
extern void sysvid_gl_present(const Uint8 *rgba);
/* presents a w x h RGBA8 picture (rows <pitch> bytes apart) scaled to the letterboxed
   window area, linear filtering, without the chain */
extern void sysvid_gl_showImage(const Uint8 *rgba, int w, int h, int pitch);
extern void sysvid_gl_shutdown(void);

#endif /* ENABLE_SHADERS */

#endif /* _SYSVID_GL_H */

/* eof */
