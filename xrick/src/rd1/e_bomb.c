/*
 * xrick/src/e_bomb.c
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



#include "system.h"
#include "config.h"
#include "env.h"

#include "ents.h"
#include "sounds.h"
#include "sprites.h"
#include "e_bomb.h"
#include "e_rick.h"

/* fixme is this for sounds only? */
#include "game.h"



/*
 * public vars (for performance reasons)
 */
U8 e_bomb_lethal;
U8 e_bomb_xc;
U16 e_bomb_yc;

/*
 * private vars
 */
U8 e_bomb_ticker;

/*
 * Bomb hit test
 *
 * ASM 11CD
 * returns: TRUE/hit, FALSE/not
 */
U8 e_bomb_hit(U8 e)
{
	/*
	 * review-log.md R3.8. ST explosion_overlaps_entity @ 0x4CC92 works from the blast
	 * CENTRE (explosion_x/y at 0x4BF2A/0x4BF2C = bomb.x+0x0C, bomb.y+0x0A):
	 *   D0 = ex-0x10-w ; cmp.w (0x4,A0),D0w ; bge  -> FALSE if ex-0x10   >= e.x+w
	 *   D0 += 0x1F + w (= ex+0x0F) ;         blt  -> FALSE if ex+0x0F   <  e.x
	 *   D1 = ey-0x0E-h ;                     bge  -> FALSE if ey-0x0E   >= e.y+h
	 *   D1 += 0x1C + h (= ey+0x0E) ;         blt  -> FALSE if ey+0x0E   <  e.y
	 * In bomb-entity coordinates that is x in [bx-4, bx+0x1B], y in [by-4, by+0x18];
	 * the PC's box is x in [bx-4, bx+0x20], y in [by-4, by+0x1D] -- five wider on both
	 * upper edges. The ST also has NO 0xFF clamp and no clamp-at-zero: it works in
	 * signed words throughout, where the PC's byte arithmetic needs both.
	 * The lower edges also differ in sense: ST `>=` (excludes equality), PC `<`.
	 */
#ifdef PLATFORM_ST
	if (ent_ents[e].x > E_BOMB_ENT.x + 0x1B)
			return FALSE;
	if (ent_ents[e].x + ent_ents[e].w <= E_BOMB_ENT.x - 0x04)
			return FALSE;
	if (ent_ents[e].y > E_BOMB_ENT.y + 0x18)
			return FALSE;
	if (ent_ents[e].y + ent_ents[e].h <= E_BOMB_ENT.y - 0x04)
			return FALSE;
	return TRUE;
#else /* PLATFORM_PC */
	if (ent_ents[e].x > (E_BOMB_ENT.x >= 0xE0 ? 0xFF : E_BOMB_ENT.x + 0x20))
			return FALSE;
	if (ent_ents[e].x + ent_ents[e].w < (E_BOMB_ENT.x > 0x04 ? E_BOMB_ENT.x - 0x04 : 0))
			return FALSE;
	if (ent_ents[e].y > (E_BOMB_ENT.y + 0x1D))
			return FALSE;
	if (ent_ents[e].y + ent_ents[e].h < (E_BOMB_ENT.y > 0x0004 ? E_BOMB_ENT.y - 0x0004 : 0))
			return FALSE;
	return TRUE;
#endif
}

/*
 * Initialize bomb
 */
void e_bomb_init(U16 x, U16 y)
{
    E_BOMB_ENT.n = 0x03;
    E_BOMB_ENT.x = x;
    E_BOMB_ENT.y = y;
    e_bomb_ticker = E_BOMB_TICKER;
    e_bomb_lethal = FALSE;

    /*
     * Atari ST dynamite sprites are not centered the
     * way IBM PC sprites were ... need to adjust things a little bit
     */
#ifdef GFXST
    E_BOMB_ENT.x += 4;
    E_BOMB_ENT.y += 5;
#endif

}


/*
 * Entity action
 *
 * ASM 18CA
 */
#ifdef PLATFORM_ST
/*
 * ST explosion frames -- review-log.md R4.1. The bomb reuses the BOX's ten-frame table:
 *   4CB82  lea (0x46c3e).l,A0        <- the very table e_box.c uses
 *   4CB8E  cmp.w #7,D0 / blt         <- index >= 7 CLEARS the lethal flag (0x4BF28)
 *   4CB9A  bclr #0,D0 / add.w D0,D0  <- entry = index >> 1, two ticks per frame
 * So: 20 explosion ticks, ten frames, and the blast stops being lethal after seven --
 * where the port had five frames of its own (0xa8..0xac), lethal throughout.
 */
static void
e_bomb_setExplosionSprite(void)
{
	/* ST-native sprite-slot numbers -- see sprites.h's sprites_stnum_to_index
	   comment: numbers >= 0x37 need translating, dat_spritesST.c's array does
	   not store them at that same position. */
	static const U8 expl[10] = {0x90, 0x24, 0x91, 0x25, 0x92, 0x26, 0x93, 0x27, 0x94, 0x28};
	U8 idx = (U8)(E_BOMB_BOOM - e_bomb_ticker);   /* counts UP, 0..19 */

	E_BOMB_ENT.sprite = sprites_stnum_to_index[expl[idx >> 1]];
	e_bomb_lethal = (idx < 7) ? TRUE : FALSE;
}
#endif


void
e_bomb_action(UNUSED(U8 e))
{
	/* tick */
	e_bomb_ticker--;

	if (e_bomb_ticker == 0)
	{
		/*
		 * end: deactivate
		 */
		E_BOMB_ENT.n = 0;
		e_bomb_lethal = FALSE;
	}
	else if (e_bomb_ticker > E_BOMB_BOOM)
	{
		/*
		 * ticking
		 */
#ifdef ENABLE_SOUND
		if ((e_bomb_ticker & 0x03) == 0x02)
			syssnd_play(WAV_BOMBSHHT);
#endif
#ifdef PLATFORM_ST
		/*
		 * ST fuse -- review-log.md A1/A6. The 17 frame pointers at 0x46BF2 resolve
		 * through (p - 0x2BE9E) / 0x150 to:
		 *   0x22 0x23 0x81 0x82 0x83 0x84 0x85 0x86 0x87 0x88 0x89 0x8A 0x8B 0x8C
		 *   0x8D 0x8E 0x8F
		 * each held TWO ticks (0x4CAC8 `bclr #0,D0 / add.w D0,D0`). These are the
		 * ST's own sprite-slot numbers (verified bit-exact against re/atari_ram.bin),
		 * not indices into dat_spritesST.c's array.
		 *
		 * Correction, 2026-09-10 (bomb-fuse bug report): A1 replaced the port's
		 * original 0x99..0xA7 with this 0x81..0x8F range, on the assumption that
		 * an ST-derived sprite-slot number can be used directly as a
		 * sprites_data[] index. It can't, past slot 0x36 -- dat_spritesST.c's
		 * array is permuted there (see sprites.h's sprites_stnum_to_index
		 * comment), and 0x99..0xA7 was actually already the *correct array
		 * position* for these exact frames -- A1's "wrong range" was the port
		 * author's own correct answer, expressed in the other numbering. Fixed by
		 * translating through sprites_stnum_to_index instead of picking either
		 * numbering by hand; the same mapping reproduces the port's own numbers
		 * for the climb (0x0C/0x18), crawl (0x07/0x08), walk (0x02..0x06) and
		 * tumble (0x19/0x1A) tables too, all of which stayed correct only because
		 * they happen to sit below the permutation's 0x37 threshold.
		 */
		{
			static const U8 fuse[17] = {
				0x22, 0x23, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
				0x88, 0x89, 0x8A, 0x8B, 0x8C, 0x8D, 0x8E, 0x8F
			};
			U8 elapsed = (U8)(E_BOMB_TICKER - 1 - e_bomb_ticker);   /* counts UP, 0..33 */
			E_BOMB_ENT.sprite = sprites_stnum_to_index[fuse[elapsed >> 1]];
		}
#else
#ifdef GFXST
		/* ST bomb sprites sequence is longer */
		if (e_bomb_ticker < 40)
			E_BOMB_ENT.sprite = 0x99 + 19 - (e_bomb_ticker >> 1);
		else
#endif
		E_BOMB_ENT.sprite = (e_bomb_ticker & 0x01) ? 0x23 : 0x22;
#endif
	}
	else if (e_bomb_ticker == E_BOMB_BOOM)
	{
		/*
		 * explode
		 */
#ifdef ENABLE_SOUND
		syssnd_play(WAV_EXPLODE);
#endif
#ifdef PLATFORM_ST
		/*
		 * ST detonation -- review-log.md R4.1. 0x4CB06 onward: set the lethal flag,
		 * x -= 4 / y -= 5, clr.w (0x4A810) to restart the animation, then track 10
		 * twice. The blast centre at 0x4CB60/0x4CB70 is x + 0x0C, y + 0x0A -- exactly
		 * what the port already had.
		 */
		E_BOMB_ENT.x -= 4;
		E_BOMB_ENT.y -= 5;
		e_bomb_setExplosionSprite();
#else
		E_BOMB_ENT.sprite = 0x24 + 4 - (e_bomb_ticker >> 1);
#endif
		e_bomb_xc = E_BOMB_ENT.x + 0x0C;
		e_bomb_yc = E_BOMB_ENT.y + 0x000A;
		e_bomb_lethal = TRUE;
		if (e_bomb_hit(E_RICK_NO))
			e_rick_gozombie();
	}
	else
	{
		/*
		 * exploding
		 */
#ifdef PLATFORM_ST
		e_bomb_setExplosionSprite();
#else
		E_BOMB_ENT.sprite = 0x24 + 4 - (e_bomb_ticker >> 1);
#endif
		/* exploding, hence lethal (ST: only while the index is below 7 -- see above) */
		if (e_bomb_hit(E_RICK_NO))
			e_rick_gozombie();
	}
}

/* eof */


