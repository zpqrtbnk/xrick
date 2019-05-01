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

#ifndef _FB_H
#define _FB_H

#include "system.h"
#include "img.h"

/* framebuffer width and height */
#define FB_WIDTH 320
#define FB_HEIGHT 200

// FIXME?
extern U8 fb[FB_HEIGHT][FB_WIDTH];

/*
 * returns the fb pointer at <x>, <y>.
 * <x>, <y> are fb-coordinates.
 */
extern U8 *fb_at(U16, U16);

/*
 * clears the frame buffer
 */
extern void fb_clear();

/*
 * ramp the fb from black to visible.
 * returns TRUE when done, FALSE when ongoing.
 */
extern U8 fb_fadeIn();

/*
 * ramp the fb from visible to black.
 * returns TRUE when done, FALSE when ongoing.
 */
extern U8 fb_fadeOut();

/*
 * sets fb visibility to black (FALSE) or full (TRUE).
 */
extern void fb_setVisible(U8);

/*
 * initializes the video layer with the game palette
 */
void fb_initPalette();

/*
 * sets the palette from the image
 */
void fb_setPaletteFromImg(img_t* img);

#endif

/* eof */
