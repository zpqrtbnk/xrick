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

#ifndef _DRAW_H
#define _DRAW_H

#include "system.h"
#include "rects.h"

/* map coordinates of the screen */
#define DRAW_XYMAP_SCRLEFT (-0x0020)
#define DRAW_XYMAP_SCRTOP (0x0040)
/* map coordinates of the top of the hidden bottom of the map */
#define DRAW_XYMAP_HBTOP (0x0100)

/* x-position of the fb, expressed in map-coordinates */
#define MAPS_FBX (-0x0020)
/* y-position of the fb, expressed in map-coordinates */
#define MAPS_FBY (0x0040)
/* y-position of the top of the hidden botton of the map, expressed in FIXME */
#define MAPS_FBB (0x0100)

/* */
extern rect_t draw_STATUSRECT;

/* */
extern rect_t draw_SCREENRECT;

#endif /* _DRAW_H */

/* eof */
