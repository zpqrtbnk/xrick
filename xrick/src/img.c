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

#include "img.h"
#include "fb.h"



img_t *IMG_SPLASH;



/*
 * paints an image of size <width>,<height> with data in <pic> at
 * position <x>,<y> (fb/px).
 */
#ifdef GFXST
void img_paintPic(U16 x, U16 y, U16 width, U16 height, U32 *pic)
{
	U8 *f, *fb;
	U16 i, j, k, pp;
	U32 v;

	fb = fb_at(x, y);
	pp = 0;

	for (i = 0; i < height; i++) /* rows */
	{
		f = fb;
		for (j = 0; j < width; j += 8) /* cols */
		{
			v = pic[pp++];
			for (k = 8; k--; v >>=4)
				f[k] = v & 0x0F;
			f += 8;
		}
		fb += FB_WIDTH;
	}
}
#endif



/*
 * paints image <img> onto the frame buffer.
 * the image must have the appropriate size.
 * also manages palettes.
 */
void img_paintImg(img_t *img)
{
	U16 k;
	U8 *fb;

	fb = fb_at(0, 0);

	fb_setPaletteFromImg(img);
	for (k = 0; k < FB_WIDTH * FB_HEIGHT; k++)
		fb[k] = img->pixels[k];
}



/* eof */
