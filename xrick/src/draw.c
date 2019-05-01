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

/*
 * NOTES
 *
 * This is the only file which accesses the video. Anything calling d_*
 * function should be video-independant.
 *
 * draw.c draws into a 320x200 or 0x0140x0xc8 8-bits depth frame buffer,
 * using the CGA 2 bits color codes. It is up to the video to figure out
 * how to display the frame buffer. Whatever draw.c does, does not show
 * until the screen is explicitely refreshed.
 *
 * Every object that can be painted, paints itself onto the frame buffer.
 * The underlying video layer is responsible for allocating the fb, and blitting
 * the fb to the actual screen.
 *
 * The frame buffer is 8 bits per pixels (indexed colors).
 *

 *
 * DIMENSIONS AND COORDINATES
 *
 * The frame buffer (fb) is a 320x200 i.e. 0x0140 by 0x00c8 pixles rectangle.
 * Coordinates in "fb/px" (pixels) go from 0x0000,0x0000 to 0x013f,0x00c7 and
 * therefore are U16,U16.
 *
 * The map is a 0x0100 by 0x0140 pixels rectangle that represents the active
 * game area, it is a subset of the entire map, beginning at map_frow pixels
 * from the top.
 *
 * Its width is MAP_WIDTH and it is composed of (in pixels):
 * - a hidden top, of height MAPS_TOPHEIGHT (0x40)
 * - a visible part, of height MAPS_VISHEIGHT (0x40)
 * - a hidden bottom, of height MAPS_BOTHEIGHT (0c40)
 *
 * The visible part should be centered on the frame buffer. Expressed in map/px,
 * the origin of the frame buffer is at MAPS_FB_X, MAPS_FB_Y.
 *
 * Coordinates in "map/px" (pixels) go from 0x0000,0x0000 to 0x0ff,0x013f
 * and therefore are U16,U16.
 *
 * A tile is an 8x8 pixels rectangle, a map is composed of rows of 0x20 tiles.
 *
 * Coordinates in "map/tl" (tiles) go from 0x00,0x00 to 0x1f,0x27 and therefore
 * are U8,U8.
 *
 * Entities are positionned relative to the map, with coordinates in map/px.
 *


                               0x140
  <-------------------------------------------------------------->

      0x20                     0x100                      0x20
  <-----------><------------------------------------><----------->


               +------------------------------------+
               |       ^                            |
               |       |                            |
               |       |                            |
               |       |                            |
               |       |                            |
               |       | map_frow                   |
               |       |                            |
               |       |                            |
               |       |                            |
               |       |                            |
               |       V                            |
               MAP----------------------------------+
               |       ^                            |
               |       | 0x40                       |
               |       |        HIDDEN TOP          |
               |       V                            |
  FB-----------+------------------------------------+------------+ ^
  |            |       ^                            |            | |
  |            |       |                            |            | |
  |            |       |                            |            | |
  |            |       | 0xc0                       |            | | 0xc8
  |            |       |                            |            | |
  |            |       |                            |            | |
  |            |       |     MAP SCREEN             |            | |
  |            |       |                            |            | |
  |            |       |                            |            | |
  |            |       |                            |            | |
  |            |       |                            |            | |
  |            |       |                            |            | |
  |            |       |                            |            | |
  |            |       V                            |            | |
  +------------+------------------------------------+------------+ |
  |            |       ^                            |            | |
  +............|...... | 0x40 ......................|............+ V
               |       |                            |
               |       V        HIDDEN BOTTOM       |
               +------------------------------------+
               |                                    |
               |                                    |
               :                                    :
               |                                    |
               |                                    |
               +------------------------------------+

 */

#include "draw.h"
#include "fb.h"



#ifdef GFXPC
#define DRAW_STATUS_SCORE_X 0x28
#define DRAW_STATUS_LIVES_X 0xE8
#define DRAW_STATUS_Y 0x08
#endif
#define DRAW_STATUS_BULLETS_X 0x68
#define DRAW_STATUS_BOMBS_X 0xA8
#ifdef GFXST
#define DRAW_STATUS_SCORE_X 0x20
#define DRAW_STATUS_LIVES_X 0xF0
#define DRAW_STATUS_Y 0
#endif



rect_t draw_STATUSRECT = {
  DRAW_STATUS_SCORE_X, DRAW_STATUS_Y,
  DRAW_STATUS_LIVES_X + 6 * 8 - DRAW_STATUS_SCORE_X, 8,
  NULL
};
rect_t draw_SCREENRECT = { 0, 0, FB_WIDTH, FB_HEIGHT, NULL };



/* eof */
