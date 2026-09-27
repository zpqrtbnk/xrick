/*
 * xrick/src/e_rick.c
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

#include "e_rick.h"

#include "game.h"
#include "ents.h"
#include "sounds.h"
#include "e_bullet.h"
#include "e_bomb.h"
#include "control.h"
#include "maps.h"
#include "util.h"


/*
 * public vars
 */
U16 e_rick_stop_x = 0;
U16 e_rick_stop_y = 0;
U8 e_rick_state = 0;
U8 e_rick_atExit = FALSE; // TRUE when rick is exiting the submap
/*
 * The edge rick left by, LEFT or RIGHT -- what maps.c map_chain matches connectors
 * against. kb/demo-solver.md F7. PC: [0x7D77], written only by the two exit stubs
 * (0x19B4 = 0 left, 0x19C4 = 1 right) and read only by the connector search
 * (0x0D99 `mov bl,[0x7d77] / cmp bl,[si]`). ST: the side is taken from X in
 * process_level_transition_point (0x49A3E-0x49A50). The port matched game_dir,
 * rick's FACING, which the climbing moves never update: climb out right while
 * facing left and no connector matched.
 */
U8 e_rick_exitDir = RIGHT;

/*
 * local vars
 */
static U8 scrawl;

static U8 trigger = FALSE;

static S8 offsx;
static U8 ylow;
static S16 offsy;

static U8 seq;
#ifdef PLATFORM_ST
static U8 tumble_seq;   /* ST only: the player's nAnimFrameIdx (0x4A778), wraps at 4 */
#endif

static U8 save_crawl;
static U16 save_x, save_y;


/*
 * Box test
 *
 * ASM 113E (based on)
 *
 * e: entity to test against (corresponds to SI in asm code -- here DI
 *    is assumed to point to rick).
 * ret: TRUE/intersect, FALSE/not.
 */
U8
e_rick_boxtest(U8 e)
{
	/*
	 * rick: x+0x05 to x+0x11, y+[0x08 if rick's crawling] to y+0x14
	 * entity: x to x+w, y to y+h
	 */

	/*
	 * review-log.md R3.6. TWO differences here, both verified at instruction level.
	 *
	 * PC  u_boxtest @ 0x12BC:
	 *     MOV AL,[DI+2] / ADD AL,0x11 / CMP AL,AH / JC   -> FALSE if x+0x11 <  e.x
	 *     SUB AL,0x0C   (= x+5) / ADD AH,[SI+0xE] / CMP AH,AL / JC
	 *                                               -> FALSE if e.x+e.w <  x+5
	 * ST  entity_overlaps_player @ 0x4D9AA:
	 *     px+5-w vs e.x, BGE  -> FALSE if x+5     >= e.x+e.w   (>= , not >)
	 *     px+5+0xD (= px+0x12) vs e.x, BLT -> FALSE if x+0x12 <  e.x
	 *
	 * So the ST's box reaches one pixel further right (0x12 vs 0x11) and is one pixel
	 * tighter on the left (>= vs >). The two vertical tests are equivalent on both
	 * sides -- note the ST adds 8 for crawling and then subtracts it again before the
	 * bottom compare, which is why the port's bottom test correctly has no crawl term.
	 */
#ifdef PLATFORM_ST
	if (E_RICK_ENT.x + 0x12 < ent_ents[e].x ||
		E_RICK_ENT.x + 0x05 >= ent_ents[e].x + ent_ents[e].w ||
		E_RICK_ENT.y + 0x14 < ent_ents[e].y ||
		E_RICK_ENT.y + (E_RICK_STTST(E_RICK_STCRAWL) ? 0x08 : 0x00) > ent_ents[e].y + ent_ents[e].h - 1)
		return FALSE;
#else /* PLATFORM_PC */
	if (E_RICK_ENT.x + 0x11 < ent_ents[e].x ||
		E_RICK_ENT.x + 0x05 > ent_ents[e].x + ent_ents[e].w ||
		E_RICK_ENT.y + 0x14 < ent_ents[e].y ||
		E_RICK_ENT.y + (E_RICK_STTST(E_RICK_STCRAWL) ? 0x08 : 0x00) > ent_ents[e].y + ent_ents[e].h - 1)
		return FALSE;
#endif
	else
		return TRUE;
}




/*
 * Go zombie
 *
 * ASM 1851
 */
void
e_rick_gozombie(void)
{
	if (env_invicible) return;

	/* already zombie? */
	if E_RICK_STTST(E_RICK_STZOMBIE) return;

#ifdef ENABLE_SOUND
	syssnd_play(WAV_DIE);
#endif

	E_RICK_STSET(E_RICK_STZOMBIE);
#ifdef PLATFORM_ST
	tumble_seq = 0;
#endif
	/*
	 * Death launch velocity -- review-log.md R4.11. Genuine ST/PC difference:
	 *   ST 0x4C822  move.w #-0x300,(0x0004a756).l    <- nVelY
	 *   PC 0x19E5   mov word[0x7d70],0xfc00          <- -0x400
	 */
#ifdef PLATFORM_ST
	offsy = -0x0300;
#else
	offsy = -0x0400;
#endif
	/*
	 * PORT DEFECT, fixed -- review-log.md R4.11. Was `x > 0x80`. BOTH originals use
	 * >=, so at exactly x == 0x80 the port drifted the corpse the wrong way:
	 *   ST 0x4C832  cmpi.w #0x80,(0x0004a752).l / bge -> neg.w (0x0004a750).l
	 *   PC 0x19EE   cmp al,0x80 / jnc -> mov byte[0x7d7a],0xfd
	 * (the ST carries the drift in nDirection, the PC and the port in offsx).
	 */
	offsx = (E_RICK_ENT.x >= 0x80 ? -3 : +3);
	ylow = 0;
	E_RICK_ENT.front = TRUE;
}


/*
 * Action sub-function for e_rick when zombie
 *
 * ASM 17DC
 */
static void
e_rick_z_action(void)
{
	U32 i;

#ifdef PLATFORM_ST
	/*
	 * ST death tumble -- review-log.md A5/A6. Two differences from the PC, both real.
	 *
	 * (a) SPRITE is tick-driven, not x-driven. 0x4C8CA increments the player's
	 *     nAnimFrameIdx (0x4A778), wraps it at 4, then indexes the frame table at
	 *     0x46BE6 with `bclr #0,D4 / add.w D4,D4` -- entry D4>>1, so the counter runs
	 *     0,1,2,3 and the frames come out 0,0,1,1: each held TWO ticks. Those two
	 *     pointers, 0x2DF6E and 0x2E0BE, resolve through the A6 mapping
	 *     ((p - 0x2BE9E) / 0x150) to sprites 0x19 and 0x1A -- exactly the pair the port
	 *     already uses, which is what anchored the mapping in the first place.
	 *
	 * (b) The corpse BOUNCES off both edges instead of drifting through them:
	 *     0x4C86E  cmpi.w #0,(0x0004a750).l / bgt -> right check
	 *     0x4C878  (left)  cmp.w #0,D2    / bgt store / neg.w (drift)
	 *     0x4C886  (right) cmp.w #0xE8,D2 / blt store / neg.w (drift)
	 *     On a bounce the drift is negated and the x store is SKIPPED that frame.
	 */
	tumble_seq++;
	if (tumble_seq >= 4)
		tumble_seq = 0;
	E_RICK_ENT.sprite = (tumble_seq & 0x02) ? 0x1A : 0x19;

	{
		S16 nx = E_RICK_ENT.x + offsx;
		if ((offsx > 0) ? (nx >= 0xE8) : (nx <= 0))
			offsx = (S8)-offsx;
		else
			E_RICK_ENT.x = nx;
	}
#else
	/* sprite */
	E_RICK_ENT.sprite = (E_RICK_ENT.x & 0x04) ? 0x1A : 0x19;

	/* x */
	E_RICK_ENT.x += offsx;
#endif

	/* y */
	i = (E_RICK_ENT.y << 8) + offsy + ylow;
	E_RICK_ENT.y = i >> 8;
	offsy += 0x80;
	ylow = i;

	/*
	 * Death-tumble end -- review-log.md R4.12.
	 *
	 * PC 0x19A1: `cmp bl,0x1 / jnz ret` on y's HIGH BYTE, then
	 *            0x19A9 `mov byte[0x7d92],0xff`.
	 * [0x7d92] is zeroed at 0x00C7 and polled by the main loop at 0x0129, and this is
	 * its only setter -- so it is STDEAD. The PC therefore ends the tumble as soon as
	 * y reaches 0x100, some 0x40 px earlier than the port did, and never ends it when
	 * the corpse flies off the TOP (a negative y has high byte 0xff, not 1).
	 *
	 * ST branch VERIFIED -- review-log.md I2 (was marked UNVERIFIED). The ST really
	 * does not end the tumble inside the player update: 0x4C846-0x4C8A8 integrates y and
	 * applies gravity with no bound test at all, and 0x4CA54 is just `movem.l (SP)+ /
	 * rts`. The corpse leaves the world through render_sprites instead:
	 *   4B0B4  moveq #0,D2 / move.w (0x6,A0),D2
	 *   4B0BA  cmp.w #0,D2 / bge   -> else bsr 0x4AC3E (despawn)
	 *   4B0C8  cmp.w #0x142,D2 / ble -> else bsr 0x4AC3E (despawn)
	 * i.e. exactly `y < 0 || y > 0x142`, which is what this branch already expresses
	 * with ENT_YMAX = 0x142. Same condition, different home.
	 */
#ifdef PLATFORM_ST
	if (E_RICK_ENT.y < 0 || E_RICK_ENT.y > ENT_YMAX)
		E_RICK_STSET(E_RICK_STDEAD);
#else
	if ((E_RICK_ENT.y >> 8) == 1)
		E_RICK_STSET(E_RICK_STDEAD);
#endif
}


/*
 * Action sub-function for e_rick.
 *
 * ASM 13BE
 */
void
e_rick_action2(void)
{
	U8 env0, env1;
	/* review-log.md R3.1: x and y must be SIGNED. Both the PC and the ST detect the
	   left-edge submap exit by the position going negative -- the PC as a byte underflow
	   (ADD AL,0xFE / JC @ 0x1906), the ST as a signed word test in the main loop
	   (player.nPosX <= 0 @ 0x4DD2E, nPosX being signed: cmp.w #-0x8 / bge). Declared U16,
	   `if (x < 0)` at :217 and :411 is dead and Rick can never leave a submap leftward.
	   Both sites return before the value is used as an index, so S16 is safe here. */
	S16 x, y;
	U32 i;

	E_RICK_STRST(E_RICK_STSTOP|E_RICK_STSHOOT);

	/* if zombie, run dedicated function and return */
	if E_RICK_STTST(E_RICK_STZOMBIE) {
		e_rick_z_action();
		return;
	}

	/* climbing? */
	if E_RICK_STTST(E_RICK_STCLIMB)
		goto climbing;

	/*
	* NOT CLIMBING
	*/
	E_RICK_STRST(E_RICK_STJUMP);
	/* calc y */
	i = (E_RICK_ENT.y << 8) + offsy + ylow;
	y = i >> 8;
	/* test environment */
	u_envtest(E_RICK_ENT.x, y, E_RICK_STTST(E_RICK_STCRAWL), &env0, &env1);
	/* stand up, if possible */
	if (E_RICK_STTST(E_RICK_STCRAWL) && !env0)
		E_RICK_STRST(E_RICK_STCRAWL);
	/* can move vertically? */
	if (env1 & (offsy < 0 ?
					MAP_EFLG_VERT|MAP_EFLG_SOLID|MAP_EFLG_SPAD :
					MAP_EFLG_VERT|MAP_EFLG_SOLID|MAP_EFLG_SPAD|MAP_EFLG_WAYUP))
		goto vert_not;

	/*
	* VERTICAL MOVE
	*/
	E_RICK_STSET(E_RICK_STJUMP);
	/* killed? */
	if (env1 & MAP_EFLG_LETHAL) {
		e_rick_gozombie();
		return;
	}
	/* save */
	E_RICK_ENT.y = y;
	ylow = i;
	/* climb? */
	if ((env1 & MAP_EFLG_CLIMB) &&
			(control_status & (CONTROL_UP|CONTROL_DOWN))) {
		offsy = 0x0100;
		E_RICK_STSET(E_RICK_STCLIMB);
		return;
	}
	/* fall */
	offsy += 0x0080;
	/*
	 * Terminal-velocity clamp -- review-log.md R4.15. The port held a MIX of the two
	 * originals: the ST's edge with the PC's ylow reset, matching NEITHER.
	 *
	 *   ST 0x4C150  cmpi.w #0x800,(0x0004a756).l / ble  -> clamp only when offsy > 0x800,
	 *               and the clamp branch (0x4C15C) sets offsy ALONE. ylow was already
	 *               stored at 0x4C142 and is left as computed.
	 *   PC 0x160A   cmp dh,0x8 / jc                     -> clamp when offsy >= 0x800,
	 *               and the clamp branch ZEROES ylow (0x1612 mov byte[0x7d72],0) before
	 *               setting offsy.
	 *
	 * The edge is not academic: offsy steps 0x100, 0x180, ... so it lands exactly on
	 * 0x800 (0x100 + 0x80*14) on every long fall. On that frame the PC zeroes ylow and
	 * the ST does not.
	 */
#ifdef PLATFORM_ST
	if (offsy > 0x0800)
		offsy = 0x0800;
#else
	if (offsy >= 0x0800) {
		ylow = 0;
		offsy = 0x0800;
	}
#endif

	/*
	* HORIZONTAL MOVE
	*/
	horiz:
	/* should move? */
	if (!(control_status & (CONTROL_LEFT|CONTROL_RIGHT))) {
		seq = 2; /* no: reset seq and return */
		return;
	}
	if (control_status & CONTROL_LEFT) {  /* move left */
		x = E_RICK_ENT.x - 2;
		game_dir = LEFT;
		if (x < 0) {  /* prev submap */
			e_rick_atExit = TRUE;
			e_rick_exitDir = LEFT;
			/* xref.md 'Submap re-entry X'. PC 0xE2 / 0x04 -- MOV word[SI+2],0x00E2 @0x19B9 and
			   MOV word[SI+2],0x0004 @0x19C9; ST 0xE6 / 0x02 -- algo-level.md reposition. */
			E_RICK_ENT.x = SUBMAP_REENTRY_RIGHT;
			return;
		}
	} else {  /* move right */
		x = E_RICK_ENT.x + 2;
		game_dir = RIGHT;
		if (x >= 0xe8) {  /* next submap */
			e_rick_atExit = TRUE;
			e_rick_exitDir = RIGHT;
			E_RICK_ENT.x = SUBMAP_REENTRY_LEFT;
			return;
		}
	}

	/* still within this map: test environment */
	u_envtest(x, E_RICK_ENT.y, E_RICK_STTST(E_RICK_STCRAWL), &env0, &env1);

	/* save x-position if it is possible to move */
	if (!(env1 & (MAP_EFLG_SOLID|MAP_EFLG_SPAD|MAP_EFLG_WAYUP))) {
		E_RICK_ENT.x = x;
		if (env1 & MAP_EFLG_LETHAL) e_rick_gozombie();
	}

	/* end */
	return;

  /*
   * NO VERTICAL MOVE
   */
 vert_not:
  if (offsy < 0) {
    /* not climbing + trying to go _up_ not possible -> hit the roof */
    E_RICK_STSET(E_RICK_STJUMP);  /* fall back to the ground */
    /*
	 * Tile-grid snap. `& ~0x07`, NOT `& 0xf8` -- review-log.md R4.16.
	 * Both originals mask the LOW BYTE ONLY and keep the high byte:
	 *   PC 0x16A2 / 0x16B8 / 0x2A5B  `and al,0xf8` (+ `or al,0x3`) on AL
	 *   ST 0x4D6F0                   `move.b (0x7,A0),D7 / andi.b #-8 / ori.b #3`
	 * y is S16 and reaches 0x142, so a 16-bit `& 0xf8` also cleared bits 8-15 and
	 * teleported the entity to the top of the world (0x108 -> 0x08).
	 */
    E_RICK_ENT.y &= ~0x07;
    /* xref.md 'Ceiling-bonk velocity' (the difference found during T8 that nobody had
       noticed). PC zeroes it -- MOV word[0x7D70],0 @0x16AC; ST sets 0x80, so Rick begins
       falling a frame sooner. */
#ifdef PLATFORM_ST
    offsy = 0x80;
#else
    offsy = 0;
#endif
    ylow = 0;
    goto horiz;
  }
  /* else: not climbing + trying to go _down_ not possible -> standing */
  /* align to ground */
  E_RICK_ENT.y &= ~0x07;   /* R4.16 */
  E_RICK_ENT.y |= 0x03;
  ylow = 0;

  /* standing on a super pad? */
  if ((env1 & MAP_EFLG_SPAD) && offsy >= 0X0200) {
    offsy = (control_status & CONTROL_UP) ? 0xf800 : 0x00fe - offsy;
#ifdef ENABLE_SOUND
	syssnd_play(WAV_PAD);
#endif
    goto horiz;
  }

  offsy = 0x0100;  /* reset*/

  /* standing. firing ? */
  if (scrawl || !(control_status & CONTROL_FIRE))
    goto firing_not;

  /*
   * FIRING
   */
	if (control_status & (CONTROL_LEFT|CONTROL_RIGHT)) {  /* stop */
		if (control_status & CONTROL_RIGHT)
		{
			game_dir = RIGHT;
			e_rick_stop_x = E_RICK_ENT.x + 0x17;
		} else {
			game_dir = LEFT;
			e_rick_stop_x = E_RICK_ENT.x;
		}
		e_rick_stop_y = E_RICK_ENT.y + 0x000E;
		E_RICK_STSET(E_RICK_STSTOP);
		return;
	}

  /*
   * Masked, not an equality -- review-log.md B5 / defect #16.
   * PC 0x174B: `mov al,dh / and al,0xc / cmp al,0x8 / jnz` -- UP set and DOWN clear,
   * every other bit IGNORED. FIRE was already required at 0x1713 and LEFT|RIGHT
   * consumed by the stop path at 0x171B, which is exactly this function's flow, so the
   * mask is the whole condition. The port's `== (FIRE|UP)` additionally demanded
   * PAUSE/END/EXIT (0x80/0x40/0x20) be clear, and refused to fire on UP+DOWN where the
   * PC bombs.
   */
  if ((control_status & (CONTROL_UP|CONTROL_DOWN)) == CONTROL_UP) {  /* bullet */
    E_RICK_STSET(E_RICK_STSHOOT);
    /* not an automatic gun: shoot once only */
    if (trigger)
      return;
    else
      trigger = TRUE;
    /* already a bullet in the air ... that's enough */
    if (E_BULLET_ENT.n)
      return;
    /* else use a bullet, if any available */
    if (!env_bullets) {
#if defined(PLATFORM_ST) && defined(ENABLE_SOUND)
      /*
       * ST only: firing with an empty gun CLICKS -- review-log.md R3.1 / defect #21.
       *   4C524  tst.b (0x0004b32a).l      ; bBullets
       *   4C52A  bne  -> fire normally
       *   4C530  move.w #0x9,D0 / moveq #1,D1 / jsr play_music(0x44CCE) / return
       * Track 9 is the same track the dynamite fuse uses (assets-manifest.md track map:
       * "player_controller (fire path when bBullets == 0); player_dynamite_update fuse"),
       * so it is WAV_BOMBSHHT here as well.
       * The PC is silent on this path -- 0x1776 `mov al,[0x7e45] / and al,al / jnz / ret`
       * with no speaker call -- so this is ST-only, not a port omission on both sides.
       */
      syssnd_play(WAV_BOMBSHHT);
#endif
      return;
    }
    if (!env_trainer)
      env_bullets--;

    /* initialize bullet */
    e_bullet_init(E_RICK_ENT.x, E_RICK_ENT.y);
    return;
  }

  trigger = FALSE; /* not shooting means trigger is released */
  seq = 0; /* reset */

  /* PC 0x17CD: `test dh,0x4 / jnz` -- DOWN alone; UP+DOWN reaches here and bombs. */
  if (control_status & CONTROL_DOWN) {  /* bomb */
    /* already a bomb ticking ... that's enough */
    if (E_BOMB_ENT.n)
      return;
    /* else use a bomb, if any available */
    if (!env_bombs)
      return;
    if (!env_trainer)
      env_bombs--;

    /* initialize bomb */
    e_bomb_init(E_RICK_ENT.x, E_RICK_ENT.y);
    return;
  }

  return;

  /*
   * NOT FIRING
   */
 firing_not:
  if (control_status & CONTROL_UP) {  /* jump or climb */
    /*
     * ST: a rick crouching on a ladder JUMPS -- 0x4C318 `tst.b crouching / bne 0x4C370`
     * skips START_CLIMB, then ACTION (0x4C38C) uncrouches and sets nVelY = -0x580.
     * scrawl is last frame's crawl, i.e. the ST's crouching flag at 0x4C318 (the
     * stand-up above has already cleared STCRAWL). The PC has no such path: its UP
     * test (0x1808 test dh,8 / test cl,2) climbs after the stand-up at 0x1596.
     * Needed to leave the ladder top on submap 0x09 (kb/demo-solver.md F10).
     */
#ifdef PLATFORM_ST
    if ((env1 & MAP_EFLG_CLIMB) && !scrawl) {  /* climb */
#else
    if (env1 & MAP_EFLG_CLIMB) {  /* climb */
#endif
      E_RICK_STSET(E_RICK_STCLIMB);
      return;
    }
    offsy = -0x0580;  /* jump */
    ylow = 0;
#ifdef ENABLE_SOUND
    syssnd_play(WAV_JUMP);
#endif
    goto horiz;
  }
  if (control_status & CONTROL_DOWN) {  /* crawl or climb */
    if ((env1 & MAP_EFLG_VERT) &&  /* can go down */
	!(control_status & (CONTROL_LEFT|CONTROL_RIGHT)) &&  /* + not moving horizontaly */
	(E_RICK_ENT.x & 0x1f) < 0x0a) {  /* + aligned -> climb */
      E_RICK_ENT.x &= 0xf0;
      E_RICK_ENT.x |= 0x04;
      E_RICK_STSET(E_RICK_STCLIMB);
    }
    else {  /* crawl */
      E_RICK_STSET(E_RICK_STCRAWL);
      goto horiz;
    }

  }
  goto horiz;

	/*
	* CLIMBING
	*/
	climbing:
		/* should move? */
		if (!(control_status & (CONTROL_UP|CONTROL_DOWN|CONTROL_LEFT|CONTROL_RIGHT))) {
			seq = 0; /* no: reset seq and return */
			return;
		}

		if (control_status & (CONTROL_UP|CONTROL_DOWN)) {
			/* up-down: calc new y and test environment */
			y = E_RICK_ENT.y + ((control_status & CONTROL_UP) ? -0x02 : 0x02);
			/*
			 * FALSE, not STCRAWL -- review-log.md B4 / defect #15.
			 * The PC calls the PLAIN probe unconditionally in BOTH climbing
			 * env tests (0x18A2 and 0x191E). Its crawl-dependent pairs are
			 * only the non-climbing vertical (0x1596 crawl / 0x15AB plain)
			 * and horizontal (0x166D / 0x1673) moves. The ST has no crawl
			 * parameter to this probe at all: (0x4DC28) is a result MASK
			 * (`and.b (0x4DC28).l,D0` @ 0x4DBFC) written only from
			 * enemy_ai_update. Both originals agree; the port alone passed
			 * the crawl flag, shortening the probe by one row whenever
			 * STCRAWL and STCLIMB were both set.
			 */
			u_envtest(E_RICK_ENT.x, y, FALSE, &env0, &env1);
			if (env1 & (MAP_EFLG_SOLID|MAP_EFLG_SPAD|MAP_EFLG_WAYUP) &&
					!(control_status & CONTROL_UP)) {
				/* FIXME what? */
				E_RICK_STRST(E_RICK_STCLIMB);
				return;
			}
			if (!(env1 & (MAP_EFLG_SOLID|MAP_EFLG_SPAD|MAP_EFLG_WAYUP)) ||
					(env1 & MAP_EFLG_WAYUP)) {
				/* ok to move, save */
				E_RICK_ENT.y = y;
				if (env1 & MAP_EFLG_LETHAL) {
					e_rick_gozombie();
					return;
				}
				if (!(env1 & (MAP_EFLG_VERT|MAP_EFLG_CLIMB))) {
					/* reached end of climb zone */
					offsy = (control_status & CONTROL_UP) ? -0x0300 : 0x0100;
#ifdef ENABLE_SOUND
					if (control_status & CONTROL_UP)
						syssnd_play(WAV_JUMP);
#endif
					E_RICK_STRST(E_RICK_STCLIMB);
					return;
				}
			}
		}
  if (control_status & (CONTROL_LEFT|CONTROL_RIGHT)) {
    /* left-right: calc new x and test environment */
    if (control_status & CONTROL_LEFT) {
      x = E_RICK_ENT.x - 0x02;
      if (x < 0) {  /* (i.e. negative) prev submap */
	e_rick_atExit = TRUE;
	e_rick_exitDir = LEFT;  /* was the commented-out 6dbd = 0x00, PC 0x19B4 */
	E_RICK_ENT.x = SUBMAP_REENTRY_RIGHT;
	return;
      }
    }
    else {
      x = E_RICK_ENT.x + 0x02;
      if (x >= 0xe8) {  /* next submap */
	e_rick_atExit = TRUE;
	e_rick_exitDir = RIGHT;  /* was the commented-out 6dbd = 0x01, PC 0x19C4 */
	E_RICK_ENT.x = SUBMAP_REENTRY_LEFT;
	return;
      }
    }
    /* FALSE, not STCRAWL -- the PC's second climbing probe, 0x191E, is also plain. See above. */
    u_envtest(x, E_RICK_ENT.y, FALSE, &env0, &env1);
    if (env1 & (MAP_EFLG_SOLID|MAP_EFLG_SPAD)) return;
    E_RICK_ENT.x = x;
    if (env1 & MAP_EFLG_LETHAL) {
      e_rick_gozombie();
      return;
    }

    if (env1 & (MAP_EFLG_VERT|MAP_EFLG_CLIMB)) return;
    E_RICK_STRST(E_RICK_STCLIMB);
    if (control_status & CONTROL_UP)
      /* xref.md 'Ladder-exit upward velocity'. PC -0x300 (MOV word[0x7D70],0xFD00
         @0x1953); ST -0x200. */
#ifdef PLATFORM_ST
      offsy = -0x0200;
#else
      offsy = -0x0300;
#endif
  }
}


/*
 * Action function for e_rick
 *
 * ASM 12CA
 */
void e_rick_action(UNUSED(U8 e))
{
	static U8 stopped = FALSE; /* is this the most elegant way? */

	e_rick_action2();

	scrawl = E_RICK_STTST(E_RICK_STCRAWL);

	if E_RICK_STTST(E_RICK_STZOMBIE)
		return;

	/*
	 * set sprite
	 */

	if E_RICK_STTST(E_RICK_STSTOP) {
		E_RICK_ENT.sprite = (game_dir ? 0x17 : 0x0B);
#ifdef ENABLE_SOUND
		if (!stopped)
		{
			syssnd_play(WAV_STICK);
			stopped = TRUE;
		}
#endif
		return;
	}

	stopped = FALSE;

	if E_RICK_STTST(E_RICK_STSHOOT) {
		E_RICK_ENT.sprite = (game_dir ? 0x16 : 0x0A);
		return;
	}

	if E_RICK_STTST(E_RICK_STCLIMB) {
		E_RICK_ENT.sprite = (((E_RICK_ENT.x ^ E_RICK_ENT.y) & 0x04) ? 0x18 : 0x0c);
#ifdef ENABLE_SOUND
		seq = (seq + 1) & 0x03;
		if (seq == 0) syssnd_play(WAV_WALK);
#endif
		return;
	}

	if E_RICK_STTST(E_RICK_STCRAWL)
	{
		E_RICK_ENT.sprite = (game_dir ? 0x13 : 0x07);
		if (E_RICK_ENT.x & 0x04) E_RICK_ENT.sprite++;
#ifdef ENABLE_SOUND
		seq = (seq + 1) & 0x03;
		if (seq == 0) syssnd_play(WAV_CRAWL);
#endif
		return;
	}

	if E_RICK_STTST(E_RICK_STJUMP)
	{
		E_RICK_ENT.sprite = (game_dir ? 0x15 : 0x06);
		return;
	}

	seq++;

	if (seq >= 0x14)
	{
#ifdef ENABLE_SOUND
		syssnd_play(WAV_WALK);
#endif
		seq = 0x04;
	}
#ifdef ENABLE_SOUND
  else
  if (seq == 0x0C)
    syssnd_play(WAV_WALK);
#endif

  E_RICK_ENT.sprite = (seq >> 2) + 1 + (game_dir ? 0x0c : 0x00);
}


/*
 * Save status
 *
 * ASM part of 0x0BBB
 */
void e_rick_save(void)
{
	save_x = E_RICK_ENT.x;
	save_y = E_RICK_ENT.y;
	save_crawl = E_RICK_STTST(E_RICK_STCRAWL);
	/* FIXME
	 * save_C0 = E_RICK_ENT.b0C;
	 * plus some 6DBC stuff?
	 */
}


/*
 * Restore status
 *
 * ASM part of 0x0BDC
 */
void e_rick_restore(void)
{
	E_RICK_ENT.x = save_x;
	E_RICK_ENT.y = save_y;
	E_RICK_ENT.front = FALSE;
	if (save_crawl)
		E_RICK_STSET(E_RICK_STCRAWL);
	else
		E_RICK_STRST(E_RICK_STCRAWL);
	/* FIXME
	 * E_RICK_ENT.b0C = save_C0;
	 * plus some 6DBC stuff?
	 */
}




/* eof */
