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

#ifndef _IMG_H
#define _IMG_H

#include "system.h"

/* a color */
typedef struct {
  U8 r, g, b, nothing;
} img_color_t;

/* an image */
typedef struct {
  U16 w, h;
  U16 ncolors;
  img_color_t *colors;
  U8 *pixels;
} img_t;

/* the splash image */
extern img_t *IMG_SPLASH;

/*
 * paints image <img> onto the frame buffer.
 * the image must have the appropriate size.
 * also manages palettes.
 */
extern void img_paintImg(img_t *);

#ifdef GFXST
/*
 * paints an image of size <width>,<height> with data in <pic> at
 * position <x>,<y> (fb/px).
 */
extern void img_paintPic(U16, U16, U16, U16, U32 *);
#endif

#endif /* _IMG_H */

/* eof */
