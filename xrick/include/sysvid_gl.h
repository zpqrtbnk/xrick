/*
 * xrick/include/sysvid_gl.h
 *
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
extern void sysvid_gl_shutdown(void);

#endif /* ENABLE_SHADERS */

#endif /* _SYSVID_GL_H */

/* eof */
