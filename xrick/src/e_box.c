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
/*
 * Explosion length -- review-log.md A4/A6.
 * PC: 10 ticks, cnt counting DOWN, sprite = sp[cnt >> 1] (0x25AE `shr bl,1`).
 * ST: TWENTY ticks -- 10 pointer frames held 2 ticks each, index counting UP to a -1
 *     sentinel (0x4D0AC `move.w (0x2a,A0),D0 / bclr #0,D0 / add.w D0,D0`).
 */
#ifdef PLATFORM_ST
#define SEQ_INIT 0x14
#else
#define SEQ_INIT 0x0A
#endif

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
	 * explode() sets cnt = SEQ_INIT = 0x0A and the next call does sp[cnt >> 1] = sp[5]
	 * BEFORE the decrement, so the original 5-element array was read one past its end:
	 *   25AB  MOV BL,[SI+0x26]        ; cnt
	 *   25AE  SHR BL,1                ; cnt >> 1   (= 5 on the first frame)
	 *   25B2  ADD BX,0x8138 / MOV AL,[BX] / MOV [SI+8],AL
	 *   25BB  DEC byte[SI+0x26] / JNZ ; decrement AFTER using the index
	 * Indices walked: 5,4,4,3,3,2,2,1,1,0.
	 *
	 * CORRECTION -- review-log.md R4.10. An earlier pass filled the sixth slot with an
	 * INFERRED 0x29, extrapolating the run 0x24..0x28. The PC data segment is now in hand
	 * and that guess was WRONG. The real table, read at ibmpc_ds1.bin (segment 0x179C)
	 * offset 0x8138, is:
	 *
	 *     24 24 25 25 26 26 27 27 28 28 ff      <- 0xff terminates it
	 *
	 * every sprite DOUBLED in the table while the index is ALSO halved. Two independent
	 * consumers halve it (0x25AE here, 0x1A89 for the bomb), so the doubling is real.
	 * With cnt 10..1 the box only reaches index 5, so the PC's explosion is three sprites
	 * over ten ticks -- 26 26 26 25 25 25 25 24 24 24 -- not the ascending 29..24 the
	 * inferred array produced.
	 */
#ifdef PLATFORM_ST
	/*
	 * ST: NOT equivalent -- structural, unimplemented (review-log.md R4.10, T9).
	 * destructible_pickup_update animates from a POINTER table at 0x46C3E:
	 *   4D0AC  move.w (0x2a,A0),D0 / bclr #0,D0 / add.w D0,D0
	 *   4D0B6  move.l (0,A1,D0.w),D1 / cmp.l #-1,D1 / beq end
	 *   4D0C2  addi.w #1,(0x2a,A0) / move.l D1,(0x22,A0)
	 * Entry k serves ticks 2k and 2k+1: TEN distinct frames x 2 ticks = 20 ticks, counting
	 * UP and ending on the sentinel, against the PC's three sprites over ten ticks counting
	 * DOWN. The port's sprite-number model cannot express that without changing SEQ_INIT
	 * and the frame source, so this keeps the port's five ST sprite numbers and holds each
	 * for two ticks -- the right SHAPE, not the ST's sequence. (cnt-1)>>1 stays in 0..4.
	 */
	/*
	 * The ST's frame POINTERS, resolved to port sprite numbers -- review-log.md A6.
	 * sprite index = (pointer - 0x2BE9E) / 0x150, `sprite_t` being U32[0x54] = 0x150
	 * bytes. Table at 0x46C3E, ten entries then a -1 sentinel:
	 *   0x37B9E 0x2EDDE 0x37CEE 0x2EF2E 0x37E3E 0x2F07E 0x37F8E 0x2F1CE 0x380DE 0x2F31E
	 * every one dividing exactly. The odd entries come out as 0x24..0x28 -- the very
	 * numbers the port already used -- which is what confirms the base.
	 */
	static U8 sp[] = {0x90, 0x24, 0x91, 0x25, 0x92, 0x26, 0x93, 0x27, 0x94, 0x28};
#else
	/* PC: the table as READ from ibmpc_ds1.bin:0x8138. Six entries is all the box's
	 * cnt>>1 index can reach. */
	static U8 sp[] = {0x24, 0x24, 0x25, 0x25, 0x26, 0x26};
#endif

	if (ent_ents[e].n & ENT_LETHAL) {
		/*
		 * box is lethal i.e. exploding
		 * play sprites sequence then stop
		 */
#ifdef PLATFORM_ST
		/* index counts UP on the ST: ticks elapsed = SEQ_INIT - cnt, frame = that >> 1 */
		ent_ents[e].sprite = sp[(SEQ_INIT - ent_ents[e].cnt) >> 1];
		/*
		 * ST only: an exploding box KILLS RICK -- review-log.md R4.21.
		 *   4D0C8  move.l D1,(0x22,A0)          ; store the frame
		 *   4D0CC  bsr 0x4D9AA                  ; entity-overlaps-player probe
		 *   4D0D0  bcc 0x4D0DA                  ; no overlap -> rts
		 *   4D0D2  move.w #0xff,(0x0004bf2e).l  ; the kill flag
		 * (0x4BF2E) is confirmed as "player is being killed": it is tested at 0x4C06A
		 * (`tst.w` / `beq`) and, when set, runs `bsr 0x4C7E4` then `bra 0x4C8CA` -- the
		 * kill routine followed by the death tumble.
		 *
		 * The PC has NO such test: its whole box routine (0x25A3-0x2628) never probes
		 * Rick while the box is exploding. So this is a genuine ST/PC difference, not a
		 * port omission.
		 *
		 * 0x4D9AA is the general entity-overlaps-player probe -- 6 callers, including
		 * the box's own collect path at 0x4D088 -- so e_rick_boxtest() is its analogue.
		 * Both ST paths rts either way, and the animation index was already advanced at
		 * 0x4D0C2, so the kill does not alter control flow: no early return here.
		 */
		if (e_rick_boxtest(e))
			e_rick_gozombie();
#else
		ent_ents[e].sprite = sp[ent_ents[e].cnt >> 1];
#endif
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


