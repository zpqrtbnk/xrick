/*
 * xrick/src/e_box.c
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

#include "game.h"
#include "ents.h"
#include "sounds.h"
#include "e_box.h"
#include "e_bullet.h"
#include "e_bomb.h"
#include "e_rick.h"
#include "maps.h"
#include "util.h"

/*
 * FIXME this is because the same structure is used
 * for all entities. Need to replace this w/ an inheritance
 * solution.
 */
#define cnt c1

/*
 * Constants
 */
#define SEQ_INIT 0x0A

/*
 * Prototypes
 */
static void explode(U8);

/*
 * Entity action
 *
 * ASM 245A
 */
void
e_box_action(U8 e)
{
	/*
	 * review-log.md R3.9: SIX entries, not five -- the fifth index IS reached.
	 * explode() sets cnt = SEQ_INIT = 0x0A, and the next call does sp[cnt >> 1] = sp[5]
	 * BEFORE the decrement, so a 5-element array is read one past its end. The PC does
	 * exactly the same indexing and its table must therefore hold six:
	 *   25AB  MOV BL,[SI+0x26]        ; cnt
	 *   25AE  SHR BL,1                ; cnt >> 1   (= 5 on the first frame)
	 *   25B2  ADD BX,0x8138 / MOV AL,[BX] / MOV [SI+8],AL
	 *   25BB  DEC byte[SI+0x26] / JNZ ; decrement AFTER using the index
	 * Indices walked: 5,4,4,3,3,2,2,1,1,0 -- ten ticks, six distinct sprites.
	 *
	 * ⚠️ The sixth value 0x29 is INFERRED from the ascending run 0x24..0x28, not read:
	 * the PC's sprite table at 0x8138 lives in its DATA segment, which we do not hold
	 * (review-plan.md §10). An inferred sprite is still strictly better than the
	 * out-of-bounds read it replaces, which returned whatever followed the array.
	 */
	static U8 sp[] = {0x24, 0x25, 0x26, 0x27, 0x28, 0x29};  /* explosion sprites sequence */

	if (ent_ents[e].n & ENT_LETHAL) {
		/*
		 * box is lethal i.e. exploding
		 * play sprites sequence then stop
		 */
		ent_ents[e].sprite = sp[ent_ents[e].cnt >> 1];
		if (--ent_ents[e].cnt == 0) {
			ent_ents[e].n = 0;
			map_marks[ent_ents[e].mark].ent |= MAP_MARK_NACT;
		}
	} else {
		/*
		 * not lethal: check to see if triggered
		 */
		if (e_rick_boxtest(e)) {
			/* rick: collect bombs or bullets and stop */
#ifdef ENABLE_SOUND
			syssnd_play(WAV_BOX, 1);
#endif
			if (ent_ents[e].n == 0x10)
				env_bombs = GAME_BOMBS_INIT;
			else  /* 0x11 */
				env_bullets = GAME_BULLETS_INIT;
			ent_ents[e].n = 0;
			map_marks[ent_ents[e].mark].ent |= MAP_MARK_NACT;
		}
		else if (E_RICK_STTST(E_RICK_STSTOP) &&
				u_fboxtest(e, e_rick_stop_x, e_rick_stop_y)) {
			/* rick's stick: explode */
			explode(e);
		}
		else if (E_BULLET_ENT.n && u_fboxtest(e, e_bullet_xc, e_bullet_yc)) {
			/* bullet: explode (and stop bullet) */
			E_BULLET_ENT.n = 0;
			explode(e);
		}
		else if (e_bomb_lethal && e_bomb_hit(e)) {
			/* bomb: explode */
			explode(e);
		}
	}
}


/*
 * Explode when
 */
static void explode(U8 e)
{
	ent_ents[e].cnt = SEQ_INIT;
	ent_ents[e].n |= ENT_LETHAL;
#ifdef ENABLE_SOUND
	syssnd_play(WAV_EXPLODE, 1);
#endif
}

/* eof */


