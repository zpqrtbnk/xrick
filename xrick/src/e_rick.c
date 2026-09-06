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
 * local vars
 */
static U8 scrawl;

static U8 trigger = FALSE;

static S8 offsx;
static U8 ylow;
static S16 offsy;

static U8 seq;

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
	syssnd_play(WAV_DIE, 1);
#endif

	E_RICK_STSET(E_RICK_STZOMBIE);
	offsy = -0x0400;
	offsx = (E_RICK_ENT.x > 0x80 ? -3 : +3);
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

	/* sprite */
	E_RICK_ENT.sprite = (E_RICK_ENT.x & 0x04) ? 0x1A : 0x19;

	/* x */
	E_RICK_ENT.x += offsx;

	/* y */
	i = (E_RICK_ENT.y << 8) + offsy + ylow;
	E_RICK_ENT.y = i >> 8;
	offsy += 0x80;
	ylow = i;

	/* dead when out of screen */
	if (E_RICK_ENT.y < 0 || E_RICK_ENT.y > ENT_YMAX)
		E_RICK_STSET(E_RICK_STDEAD);
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
	if (offsy > 0x0800) {
		offsy = 0x0800;
		ylow = 0;
	}

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
    E_RICK_ENT.y &= 0xF8;
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
  E_RICK_ENT.y &= 0xF8;
  E_RICK_ENT.y |= 0x03;
  ylow = 0;

  /* standing on a super pad? */
  if ((env1 & MAP_EFLG_SPAD) && offsy >= 0X0200) {
    offsy = (control_status & CONTROL_UP) ? 0xf800 : 0x00fe - offsy;
#ifdef ENABLE_SOUND
	syssnd_play(WAV_PAD, 1);
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

  if (control_status == (CONTROL_FIRE|CONTROL_UP)) {  /* bullet */
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
    if (!env_bullets)
      return;
    if (!env_trainer)
      env_bullets--;

    /* initialize bullet */
    e_bullet_init(E_RICK_ENT.x, E_RICK_ENT.y);
    return;
  }

  trigger = FALSE; /* not shooting means trigger is released */
  seq = 0; /* reset */

  if (control_status == (CONTROL_FIRE|CONTROL_DOWN)) {  /* bomb */
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
    if (env1 & MAP_EFLG_CLIMB) {  /* climb */
      E_RICK_STSET(E_RICK_STCLIMB);
      return;
    }
    offsy = -0x0580;  /* jump */
    ylow = 0;
#ifdef ENABLE_SOUND
    syssnd_play(WAV_JUMP, 1);
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
			u_envtest(E_RICK_ENT.x, y, E_RICK_STTST(E_RICK_STCRAWL), &env0, &env1);
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
						syssnd_play(WAV_JUMP, 1);
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
	/*6dbd = 0x00;*/
	E_RICK_ENT.x = SUBMAP_REENTRY_RIGHT;
	return;
      }
    }
    else {
      x = E_RICK_ENT.x + 0x02;
      if (x >= 0xe8) {  /* next submap */
	e_rick_atExit = TRUE;
	/*6dbd = 0x01;*/
	E_RICK_ENT.x = SUBMAP_REENTRY_LEFT;
	return;
      }
    }
    u_envtest(x, E_RICK_ENT.y, E_RICK_STTST(E_RICK_STCRAWL), &env0, &env1);
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
			syssnd_play(WAV_STICK, 1);
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
		if (seq == 0) syssnd_play(WAV_WALK, 1);
#endif
		return;
	}

	if E_RICK_STTST(E_RICK_STCRAWL)
	{
		E_RICK_ENT.sprite = (game_dir ? 0x13 : 0x07);
		if (E_RICK_ENT.x & 0x04) E_RICK_ENT.sprite++;
#ifdef ENABLE_SOUND
		seq = (seq + 1) & 0x03;
		if (seq == 0) syssnd_play(WAV_CRAWL, 1);
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
		syssnd_play(WAV_WALK, 1);
#endif
		seq = 0x04;
	}
#ifdef ENABLE_SOUND
  else
  if (seq == 0x0C)
    syssnd_play(WAV_WALK, 1);
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
