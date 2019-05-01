/*
 * xrick/include/sysvid.h
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

#ifndef _SYSVID_H
#define _SYSVID_H

#include "rects.h"
#include "img.h"

#define SYSVID_ZOOM 2
#define SYSVID_WIDTH 320
#define SYSVID_HEIGHT 200

extern void sysvid_init(U16 width, U16 height);
extern void sysvid_shutdown(void);
extern void sysvid_update(rect_t*);
extern void sysvid_zoom(S8);
extern void sysvid_toggleFullscreen(void);
extern void sysvid_setDisplayPalette(void);
extern void sysvid_setPalette(img_color_t*, U16);

extern U8 *sysvid_fb;  /* frame buffer */
extern void sysvid_setGamma(U8 g);
extern U8 sysvid_fadeIn();
extern U8 sysvid_fadeOut();

#endif /* _SYSVID_H */

/* eof */


