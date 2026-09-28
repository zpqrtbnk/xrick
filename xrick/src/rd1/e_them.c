/*
 * xrick/src/e_them.c
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
#include "e_them.h"
#include "e_rick.h"
#include "e_bomb.h"
#include "e_bullet.h"
#include "maps.h"
#include "util.h"

#define TYPE_1A (0x00)
#define TYPE_1B (0xff)

/*
 * public vars
 */
U32 e_them_rndseed = 0;

/*
 * local vars
 */
static U16 e_them_rndnbr = 0;
#ifdef PLATFORM_ST
/* ST PRNG state, 0x495C0 / 0x495C4; seeds are the values held in atari_ram.bin */
static U32 st_rnd_a = 0x121901F9u;
static U32 st_rnd_b = 0x160566F9u;

/*
 * ST generator step, update_prng 0x49596:
 *
 *   move.l (0x495C0),D6 / move.l (0x495C4),D7 / exg D6,D7
 *   rol.l #3,D7 / subq.w #7,D7 / eor.w D6,D7
 *   move.l D6,(0x495C0) / move.l D7,(0x495C4)
 *
 * output byte = 0x495C7 = D7 & 0xFF. Two callers in the original, both `bsr`
 * to 0x49596 (read in atari_ram.bin): 0x4D79E in enemy_ai_update (per decision,
 * e_them_t2_action below) and 0x4DD3A in the main loop's RENDER path, once per
 * frame after render_sprites has run the entity handlers (game.c CTRL_ACTION).
 * The per-frame call was missing from the port -- kb/demo-solver.md F1.
 */
U8
e_them_rndstep(void)
{
	U32 d6 = st_rnd_a, d7 = st_rnd_b, t;
	t = d6; d6 = d7; d7 = t;                                 /* exg d6,d7 */
	d7 = ((d7 << 3) | (d7 >> 29)) & 0xFFFFFFFFu;             /* rol.l #3 */
	d7 = (d7 & 0xFFFF0000u) | ((d7 - 7) & 0xFFFFu);          /* subq.w #7 */
	d7 = (d7 & 0xFFFF0000u) | ((d7 ^ d6) & 0xFFFFu);         /* eor.w d6,d7 */
	st_rnd_a = d6; st_rnd_b = d7;
	return (U8)(d7 & 0xFF);
}
#endif

/*
 * Put the random generator back to its power-on state.
 *
 * ST: the values seed_prng_state (0x49574) computes, which are also what
 * atari_ram.bin holds at 0x495C0 and the initialisers above. PC: 0 / 0, the
 * initialisers. Demo mode only -- the originals seed once, at boot. Called on
 * every demo segment entry so each submap's script is self-contained
 * (PLAN.md T43 D1/D5).
 */
void
e_them_rndreset(void)
{
#ifdef PLATFORM_ST
	st_rnd_a = 0x121901F9u;
	st_rnd_b = 0x160566F9u;
#endif
	e_them_rndseed = 0;
	e_them_rndnbr = 0;
}

/*
 * Read the random generator state, for the trace (game.c). ST: st_rnd_a/b.
 * PC: e_them_rndseed/rndnbr.
 */
void
e_them_rndstate(U32 *a, U32 *b)
{
#ifdef PLATFORM_ST
	*a = st_rnd_a;
	*b = st_rnd_b;
#else
	*a = e_them_rndseed;
	*b = e_them_rndnbr;
#endif
}

/*
 * Check if entity boxtests with a lethal e_them i.e. something lethal
 * in slot 0 and 4 to 8.
 *
 * ASM 122E
 *
 * e: entity slot number.
 * ret: TRUE/boxtests, FALSE/not
 */
U8
u_themtest(U8 e)
{
  U8 i;

  if ((ent_ents[0].n & ENT_LETHAL) && u_boxtest(e, 0))
    return TRUE;

  for (i = 4; i < 9; i++)
    if ((ent_ents[i].n & ENT_LETHAL) && u_boxtest(e, i))
      return TRUE;

  return FALSE;
}


/*
 * Go zombie
 *
 * ASM 237B
 */
void
e_them_gozombie(U8 e)
{
#define offsx c1
  ent_ents[e].n = 0x47;  /* zombie entity */
  ent_ents[e].front = TRUE;
  /* review-log.md R3.12 / xref.md: death launch. PC -0x400; ST kill_enemy @0x4D886
     does move.w #-0x300,(0x8,A0). */
#ifdef PLATFORM_ST
  ent_ents[e].offsy = -0x0300;
#else
  ent_ents[e].offsy = -0x0400;
#endif
#ifdef ENABLE_SOUND
  syssnd_play(WAV_DIE);
#endif
  env_addscore(50);
  if (ent_ents[e].flags & ENT_FLG_ONCE) {
    /* make sure entity won't be activated again */
    map_marks[ent_ents[e].mark].ent |= MAP_MARK_NACT;
  }
  ent_ents[e].offsx = (ent_ents[e].x >= 0x80 ? -0x02 : 0x02);
#undef offsx
}


/*
 * Action sub-function for e_them _t1a and _t1b
 *
 * Those two types move horizontally, and fall if they have to.
 * Type 1a moves horizontally over a given distance and then
 * u-turns and repeats; type 1b is more subtle as it does u-turns
 * in order to move horizontally towards rick.
 *
 * ASM 2242
 */
void
e_them_t1_action2(U8 e, U8 type)
{
#define offsx c1
#define step_count c2
  U32 i;
  U16 x, y;
  U8 env0, env1;

  /* by default, try vertical move. calculate new y */
  i = (ent_ents[e].y << 8) + ent_ents[e].offsy + ent_ents[e].ylow;
  y = i >> 8;

  /*
   * deactivate if outside vertical boundaries -- review-log.md I3 / defect #22.
   * PC 0x23AD `cmp dx,0x140 / jc` after the 16.16 integration: dead at y >= 0x140.
   * Being an UNSIGNED compare it catches a negative y too, so the port's "no need to
   * test zero" note holds either way. Was `y > ENT_YMAX`, one row too high.
   */
  if (ENT_YDEAD(y)) {
    ent_ents[e].n = 0;
    return;
  }

  /* test environment */
  u_envtest(ent_ents[e].x, y, FALSE, &env0, &env1);

  if (!(env1 & (MAP_EFLG_VERT|MAP_EFLG_SOLID|MAP_EFLG_SPAD|MAP_EFLG_WAYUP))) {
    /* vertical move possible: falling */
    if (env1 & MAP_EFLG_LETHAL) {
      /* lethal entities kill e_them */
      e_them_gozombie(e);
      return;
    }
    /* save, cleanup and return */
    ent_ents[e].y = y;
    ent_ents[e].ylow = i;
    ent_ents[e].offsy += 0x0080;
    if (ent_ents[e].offsy > 0x0800)
      ent_ents[e].offsy = 0x0800;
    return;
  }

  /* vertical move not possible. calculate new sprite */
  ent_ents[e].sprite = ent_ents[e].sprbase
    + ent_sprseq[(ent_ents[e].x & 0x1c) >> 3]
    + (ent_ents[e].offsx < 0 ? 0x03 : 0x00);

  /* reset offsy */
  ent_ents[e].offsy = 0x0080;

  /* align to ground */
  ent_ents[e].y &= ~0x07;   /* R4.16: was 0xfff8, wrong for negative y */
  ent_ents[e].y |= 0x0003;

  /* latency: if not zero then decrease and return */
  if (ent_ents[e].latency > 0) {
    ent_ents[e].latency--;
    return;
  }

  /* horizontal move. calculate new x */
  if (ent_ents[e].offsx == 0)  /* not supposed to move -> don't */
    return;

  x = ent_ents[e].x + ent_ents[e].offsx;
  if (ent_ents[e].x < 0 || ent_ents[e].x > 0xe8) {
    /*  U-turn and return if reaching horizontal boundaries */
    ent_ents[e].step_count = 0;
    ent_ents[e].offsx = -ent_ents[e].offsx;
    return;
  }

  /* test environment */
  u_envtest(x, ent_ents[e].y, FALSE, &env0, &env1);

  if (env1 & (MAP_EFLG_VERT|MAP_EFLG_SOLID|MAP_EFLG_SPAD|MAP_EFLG_WAYUP)) {
    /* horizontal move not possible: u-turn and return */
    ent_ents[e].step_count = 0;
    ent_ents[e].offsx = -ent_ents[e].offsx;
    return;
  }

  /* horizontal move possible */
  if (env1 & MAP_EFLG_LETHAL) {
    /* lethal entities kill e_them */
    e_them_gozombie(e);
    return;
  }

  /* save */
  ent_ents[e].x = x;

  /* depending on type, */
  if (type == TYPE_1B) {
    /* set direction to move horizontally towards rick */
    if ((ent_ents[e].x & 0x1e) != 0x10)  /* prevents too frequent u-turns */
      return;
    ent_ents[e].offsx = (ent_ents[e].x < E_RICK_ENT.x) ? 0x02 : -0x02;
    return;
  }
  else {
    /* set direction according to step counter */
    ent_ents[e].step_count++;
    /* FIXME why trig_x (b16) ?? */
    if ((ent_ents[e].trig_x >> 1) > ent_ents[e].step_count)
      return;
  }

  /* type is 1A and step counter reached its limit: u-turn */
  ent_ents[e].step_count = 0;
  ent_ents[e].offsx = -ent_ents[e].offsx;
#undef offsx
#undef step_count
}


/*
 * ASM 21CF
 */
void
e_them_t1_action(U8 e, U8 type)
{
  e_them_t1_action2(e, type);

  /* lethal entities kill them */
  if (u_themtest(e)) {
    e_them_gozombie(e);
    return;
  }

  /* bullet kills them */
  if (E_BULLET_ENT.n &&
      u_fboxtest(e, E_BULLET_ENT.x + (e_bullet_offsx < 0 ? 0 : 0x18),
		 E_BULLET_ENT.y)) {
    E_BULLET_ENT.n = 0;
    e_them_gozombie(e);
    return;
  }

  /* bomb kills them */
  if (e_bomb_lethal && e_bomb_hit(e)) {
#ifdef PLATFORM_ST
    /* review-log.md R2.3b: the ST awards 50 HERE, on the explosion path, IN ADDITION to
       the 50 that kill_enemy awards itself -- so a dynamite kill scores 100, other kills
       50. ST: enemy_ai_update 0x4D542 tests explosion_overlaps_entity, adds 0x50 at
       0x4D54E, then calls kill_enemy (0x4D87C) which adds a second 0x50 at 0x4D892.
       The PC has exactly ONE score add on this path -- all four CALL 0x0292 sites in
       ibmpc_cs.bin are accounted for (level bonus 0x0DEE, super bonus 0x22BB, enemy kill
       0x24D6 inside gozombie, pickup 0x2585) and none is a second add here. Genuine
       PC-vs-ST difference; the port was faithful to the PC. */
    env_addscore(50);
#endif
    e_them_gozombie(e);
    return;
  }

  /* rick stops them */
  if (E_RICK_STTST(E_RICK_STSTOP) &&
      u_fboxtest(e, e_rick_stop_x, e_rick_stop_y))
    ent_ents[e].latency = ENT_STUN;

  /* they kill rick */
  if (e_rick_boxtest(e))
    e_rick_gozombie();
}


/*
 * Action function for e_them _t1a type (stays within boundaries)
 *
 * ASM 2452
 */
void
e_them_t1a_action(U8 e)
{
  e_them_t1_action(e, TYPE_1A);
}


/*
 * Action function for e_them _t1b type (runs for rick)
 *
 * ASM 21CA
 */
void
e_them_t1b_action(U8 e)
{
  e_them_t1_action(e, TYPE_1B);
}


/*
 * Action function for e_them _z (zombie) type
 *
 * ASM 23B8
 */
void
e_them_z_action(U8 e)
{
#define offsx c1
  U32 i;

  /* calc new sprite */
  ent_ents[e].sprite = ent_ents[e].sprbase
    + ((ent_ents[e].x & 0x04) ? 0x07 : 0x06);

  /* calc new y */
  i = (ent_ents[e].y << 8) + ent_ents[e].offsy + ent_ents[e].ylow;

  /*
   * deactivate if out of vertical boundaries -- review-log.md I3.
   *
   * NOTE: this test has NO counterpart in the PC. Its entity bound checks number
   * exactly six, all now located and mapped: 0x10E3 (scroll translate), 0x2976 (t2
   * ymove), 0x2A06 (t2 fall), 0x23AD (t1 fall), 0x2742 (restore from xsave/ysave),
   * 0x278B (scripted move). The DYING-enemy update has none -- a dying enemy leaves the
   * world through the general mechanisms instead (the PC's scroll translate; on the ST
   * render_sprites' `y < 0 || y > 0x142` despawn at 0x4B0BE/0x4B0C8).
   *
   * It also tests the PRE-integration y: `i` is computed above from the old y, this
   * reads the old y, and the new one is stored below regardless -- so it fires a frame
   * late. Left in place (deleting a despawn on no evidence is the riskier change) but
   * switched to ENT_YDEAD so its edge matches every other bound in the tree instead of
   * sitting one row higher on the PC side.
   */
  if (ENT_YDEAD(ent_ents[e].y)) {
    ent_ents[e].n = 0;
    return;
  }

  /* save */
  /* review-log.md R3.12: DYING gravity. The ST uses a different constant for a dying
     entity than for a living one -- addi.w #0xC4,(0x8,A0) @ 0x4D532, with NO terminal
     clamp; the living paths use +0x80 clamped at 0x800 (@0x4D684/0x4D68A) and those
     agree with the port already. */
#ifdef PLATFORM_ST
  ent_ents[e].offsy += 0x00C4;
#else
  ent_ents[e].offsy += 0x0080;
#endif
  ent_ents[e].ylow = i;
  ent_ents[e].y = i >> 8;

  /* calc new x */
  /*
   * xref.md 'Enemy corpse drift'. The two builds drift a dying entity differently:
   *   PC/port : x += offsx (+/-2 on X only), then clamp to [0, 0xE8]
   *   ST      : tst.w (0x2,A0) @0x4D4FC -- if nDirection != 0 then addi.w #1,(0x6,A0)
   *             (y += 1), ELSE subi.w #1,(0x4,A0) (x -= 1). One pixel, on ONE axis,
   *             chosen by facing; the clamps do not exist on that path.
   */
#ifdef PLATFORM_ST
  /* The ST tests nDirection (0x02), set to 0xFF when moving left (move.w #0xff,(0x2,A0)
     @0x4D604) and cleared when moving right (clr.w @0x4D618). The port has no separate
     direction field for enemies -- it carries the sign of offsx (c1) instead, so
     offsx < 0 is the same condition. */
  if (ent_ents[e].offsx < 0)
    ent_ents[e].y += 1;
  else
    ent_ents[e].x -= 1;
#else
  ent_ents[e].x += ent_ents[e].offsx;
#endif

  /* must stay within horizontal boundaries */
  if (ent_ents[e].x < 0)
    ent_ents[e].x = 0;
  if (ent_ents[e].x > 0xe8)
    ent_ents[e].x = 0xe8;
#undef offsx
}


/*
 * Action sub-function for e_them _t2.
 *
 * Must document what it does.
 *
 * ASM 2792
 */
void
e_them_t2_action2(U8 e)
{
#define flgclmb c1
#define offsx c2
  U32 i;
  /* review-log.md R3.1: signed, same reason as e_rick_action2 -- `y = ent.y + yd`
     with yd = -2 goes negative at the top edge, and `if (y < 0 ...)` at :403 is dead
     while y is U16. The guard returns before y is used, so S16 is safe. */
  S16 x, y;
  S16 yd;
  U8 env0, env1;

#ifndef PLATFORM_ST   /* the PC generator only; the ST uses st_rnd_a/b (A3) */
  /*
   * vars required by the Black Magic (tm) performance at the
   * end of this function.
   */
  static U16 bx;
  static U8 *bl = (U8 *)&bx;
  static U8 *bh = (U8 *)&bx + 1;
  static U16 cx;
  static U8 *cl = (U8 *)&cx;
  static U8 *ch = (U8 *)&cx + 1;
  static U16 *sl = (U16 *)&e_them_rndseed;
  /*
   * PORT DEFECT, fixed -- review-log.md R4.8.  Was `+ 2`.
   *
   * e_them_rndseed is a U32, so pointer arithmetic on U16* means `+ 2` lands FOUR bytes
   * in -- one U16 past the end of the variable.  Every read of *sh was out of bounds.
   * The PC's two operands are 2 bytes apart:
   *
   *   0x0250  add bx,[0x7e4a]      <- *sl, the low half
   *   0x0257  mov cx,[0x7e4c]      <- *sh, the high half
   *
   * and 0x7e4e (what `+ 2` would correspond to) is an unrelated variable with 10
   * references elsewhere in the segment.  `+ 1` is the high half.
   */
  static U16 *sh = (U16 *)&e_them_rndseed + 1;
#endif

  /*sys_printf("e_them_t2 ------------------------------\n");*/

  /* latency: if not zero then decrease */
  if (ent_ents[e].latency > 0) ent_ents[e].latency--;

  /* climbing? */
  if (ent_ents[e].flgclmb != TRUE) goto climbing_not;

  /* CLIMBING */

  /*sys_printf("e_them_t2 climbing\n");*/

  /* latency: if not zero then return */
  if (ent_ents[e].latency > 0) return;

  /* calc new sprite */
  ent_ents[e].sprite = ent_ents[e].sprbase + 0x08 +
    (((ent_ents[e].x ^ ent_ents[e].y) & 0x04) ? 1 : 0);

  /* reached rick's level? */
  /*
   * Reached Rick's level?  PORT DEFECT, fixed -- review-log.md R4.4.
   *
   * The port masked with 0x00fe, which drops bits 8-15; `y` reaches 0x142, so an enemy
   * 256 px from Rick compared equal and the enemy switched to xmove as if it had
   * arrived. BOTH originals compare the full 16-bit y with only bit 0 cleared:
   *
   *   ST 0x4D5AE  bclr #0 on WORDS
   *   PC 0x2913   mov ax,[0x7e82] / and al,0xfe / mov bx,[si+4] / and bl,0xfe /
   *               cmp ax,bx      -- 16-bit loads, 16-bit compare; `and al/bl` touches
   *                                 only the low byte, leaving ah/bh intact, so this
   *                                 is y & 0xfffe, not y & 0x00fe.
   *
   * Not platform-switched: the two originals agree, the port alone was wrong.
   */
  if ((ent_ents[e].y & ~1) != (E_RICK_ENT.y & ~1)) goto ymove;

  xmove:
    /* calc new x and test environment */
    ent_ents[e].offsx = (ent_ents[e].x < E_RICK_ENT.x) ? 0x02 : -0x02;
    x = ent_ents[e].x + ent_ents[e].offsx;
    u_envtest(x, ent_ents[e].y, FALSE, &env0, &env1);
    if (env1 & (MAP_EFLG_SOLID|MAP_EFLG_SPAD|MAP_EFLG_WAYUP))
      return;
    if (env1 & MAP_EFLG_LETHAL) {
      e_them_gozombie(e);
      return;
    }
    ent_ents[e].x = x;
    if (env1 & (MAP_EFLG_VERT|MAP_EFLG_CLIMB))  /* still climbing */
      return;
    goto climbing_not;  /* not climbing anymore */

  ymove:
    /* calc new y and test environment */
    yd = ent_ents[e].y < E_RICK_ENT.y ? 0x02 : -0x02;
    y = ent_ents[e].y + yd;
    if (ENT_YDEAD(y)) {          /* ST 0x4D34A / PC 0x2976 -- R4.7 */
      ent_ents[e].n = 0;
      return;
    }
    u_envtest(ent_ents[e].x, y, FALSE, &env0, &env1);
    if (env1 & (MAP_EFLG_SOLID|MAP_EFLG_SPAD|MAP_EFLG_WAYUP)) {
      if (yd < 0)
	goto xmove;  /* can't go up */
      else
	goto climbing_not;  /* can't go down */
    }
    /* can move */
    ent_ents[e].y = y;
#ifdef PLATFORM_ST
    /*
     * ST only: climbing UP resets the vertical velocity (review-log.md R4.5).
     *   0x4D5C4  subi.w #2,D7 / bsr envtest / bcs -> xmove
     *   0x4D5CE  move.w #-0x200,(0x8,A0)      <- nVelY, only on the up path
     *   0x4D5D6  addi.w #2,D7 ...             <- down path, no write
     * The PC's ymove (0x2968-0x29B5) writes [SI+0x2c] nowhere, so it leaves offsy
     * untouched in both directions.
     */
    if (yd < 0)
      ent_ents[e].offsy = -0x0200;
#endif
    if (env1 & (MAP_EFLG_VERT|MAP_EFLG_CLIMB))  /* still climbing */
      return;

    /* NOT CLIMBING */

 climbing_not:
    /*sys_printf("e_them_t2 climbing NOT\n");*/

    ent_ents[e].flgclmb = FALSE;  /* not climbing */

    /* calc new y (falling) and test environment */
    i = (ent_ents[e].y << 8) + ent_ents[e].offsy + ent_ents[e].ylow;
    y = i >> 8;
    u_envtest(ent_ents[e].x, y, FALSE, &env0, &env1);
    if (!(env1 & (MAP_EFLG_SOLID|MAP_EFLG_SPAD|MAP_EFLG_WAYUP))) {
      /*sys_printf("e_them_t2 y move OK\n");*/
      /* can go there */
      if (env1 & MAP_EFLG_LETHAL) {
	e_them_gozombie(e);
	return;
      }
      /*
       * R4.7. PC 0x2A06 is `cmp dh,0 / jz ok / cmp dl,0x40 / jc ok / kill` -- dead at
       * y >= 0x140. (It omits the `dh == 1` arm that 0x2976 has, so a y >= 0x200 would
       * slip through on the PC; unreachable here, and not reproduced.) The ST's
       * equivalent is cmp.w #0x142 / ble, so `>` there and `>=` here.
       */
#ifdef PLATFORM_ST
      if (y > ENT_YMAX) {  /* deactivate if outside */
#else
      if (y >= 0x0140) {   /* deactivate if outside */
#endif
	ent_ents[e].n = 0;
	return;
      }
      if (!(env1 & MAP_EFLG_VERT)) {
	/* save */
	ent_ents[e].y = y;
	ent_ents[e].ylow = i;
	ent_ents[e].offsy += 0x0080;
	if (ent_ents[e].offsy > 0x0800)
	  ent_ents[e].offsy = 0x0800;
	return;
      }
#ifdef PLATFORM_ST
      /*
       * ST climb gate 1 -- review-log.md A2. 0x4D69C onward:
       *   4D6AC  cmp.w (0x0004a754).l,D7 / bgt reject      -> enemy.y <= rick.y (INCLUSIVE)
       *   4D6B4  btst #3,D6 / beq accept                   -> (x & 8) == 0
       *   4D6BA  move.b D6,D5 / andi.b #7,D5 / bne reject  -> or (x & 7) == 0
       *   4D6C2  andi.b #-0x10,D6 / ori.b #4,D6            -> x = (x & ~0x0F) | 4
       *   4D6CE  move.b (0x7,A0),D7 / andi.b #-8 / ori.b #5 -> y = (y & ~7) | 5
       *   4D6DE  clr.w (0xa,A0)  ; 4D6E2 move.b #-1,(0x48,A0)
       * The ST reaches this from its BLOCKED branch, but the flows converge: its carry is
       * `(env & 0xD0) != 0` (0x4DC0E `andi.b #-0x30,D0 / bne`) = VERT|SOLID|WAYUP, so a
       * VERT tile sets it and the explicit VERT test re-selects the case the port reaches
       * via `!(env1 & 0x70)` then `env1 & VERT`.
       */
      if ((((ent_ents[e].x & 0x08) == 0) || ((ent_ents[e].x & 0x07) == 0)) &&
	  (y <= E_RICK_ENT.y)) {
	ent_ents[e].x = (ent_ents[e].x & ~0x0F) | 0x04;
	ent_ents[e].y = (y & ~0x07) | 0x05;
	ent_ents[e].ylow = 0;
	ent_ents[e].flgclmb = TRUE;
	return;
      }
#else
      if (((ent_ents[e].x & 0x07) == 0x04) && (y < E_RICK_ENT.y)) {
	/*sys_printf("e_them_t2 climbing00\n");*/
	ent_ents[e].flgclmb = TRUE;  /* climbing */
	return;
      }
#endif
    }

    /*sys_printf("e_them_t2 ymove nok or ...\n");*/
    /* can't go there, or ... */
    ent_ents[e].y = (ent_ents[e].y & ~0x07) | 0x03;  /* align to ground -- R4.16 */
    ent_ents[e].offsy = 0x0100;
    if (ent_ents[e].latency != 00)
      return;

#ifdef PLATFORM_ST
    /*
     * ST climb gate 2 -- same mask and x snap, no y snap. 0x4D71A onward:
     *   4D71A  btst #1,(0x0004dc29).l / beq   -> CLIMB flag
     *   4D724  cmp.w (0x0004a754).l,D7 / ble  -> enemy.y > rick.y
     *   4D72C  btst #3,D6 / beq / andi.b #7,D5 / bne
     *   4D73A  andi.b #-0x10 / ori.b #4 ; 4D74A clr.w (0xa,A0)
     */
    if ((env1 & MAP_EFLG_CLIMB) &&
	(((ent_ents[e].x & 0x08) == 0) || ((ent_ents[e].x & 0x07) == 0)) &&
	(ent_ents[e].y > E_RICK_ENT.y)) {
      ent_ents[e].x = (ent_ents[e].x & ~0x0F) | 0x04;
      ent_ents[e].ylow = 0;
      ent_ents[e].flgclmb = TRUE;
      return;
    }
#else
    if ((env1 & MAP_EFLG_CLIMB) &&
	((ent_ents[e].x & 0x0e) == 0x04) &&
	(ent_ents[e].y > E_RICK_ENT.y)) {
      /*sys_printf("e_them_t2 climbing01\n");*/
      ent_ents[e].flgclmb = TRUE;  /* climbing */
      return;
    }
#endif

    /* calc new sprite */
    ent_ents[e].sprite = ent_ents[e].sprbase +
      ent_sprseq[(ent_ents[e].offsx < 0 ? 4 : 0) +
		((ent_ents[e].x & 0x0e) >> 3)];
    /*sys_printf("e_them_t2 sprite %02x\n", ent_ents[e].sprite);*/


    /* */
    if (ent_ents[e].offsx == 0)
      ent_ents[e].offsx = 2;
    x = ent_ents[e].x + ent_ents[e].offsx;
    /*sys_printf("e_them_t2 xmove x=%02x\n", x);*/
    /*
     * Left edge -- kb/demo-solver.md F6. `x` became S16 (R3.1), so a negative x
     * passed `x < 0xe8` and the probe read map_map out of bounds (segfault).
     *   PC 0x2AD3  offsx < 0: ADD AL,[SI+2] / JC probe, else blocked -> x >= 0 only;
     *      0x2AE8  offsx > 0: CMP AL,0xE8 / JNC blocked.
     *   ST 0x4D76C no bound either side before the probe (0x4DA40); off the left
     *      edge it reads constant bytes (util.c st_offgrid) and the enemy is
     *      despawned at x < -8 (ents.c). The right bound stays the PC's here.
     */
#ifdef PLATFORM_ST
    if (x < 0xe8) {
#else
    if (x >= 0 && x < 0xe8) {
#endif
      u_envtest(x, ent_ents[e].y, FALSE, &env0, &env1);
      if (!(env1 & (MAP_EFLG_VERT|MAP_EFLG_SOLID|MAP_EFLG_SPAD|MAP_EFLG_WAYUP))) {
	ent_ents[e].x = x;
	if ((x & 0x1e) != 0x08)
	  return;

	/*
	 * Black Magic (tm)
	 *
	 * this is obviously some sort of randomizer to define a direction
	 * for the entity. it is an exact copy of what the assembler code
	 * does but I can't explain.
	 */
#ifdef PLATFORM_ST
	/*
	 * ST random turn -- review-log.md A3. A DIFFERENT generator and a different rule.
	 *
	 *   generator 0x49596: e_them_rndstep() above.
	 *   decision  0x4D7A8: andi.b #3,D5 / bne skip  -> turn on 1 in 4,
	 *     and the turn (0x4D7DE) FLIPS nDirection between 0 and 0xFF; it does not pick
	 *     a direction. The PC instead SETS the direction from one random bit (0x2B13
	 *     `and al,0x1`), i.e. 50/50, so it often re-picks the same way.
	 *
	 * The ST's gate is its frame counter (`cmpi.w #8,(0x2a,A0)` @ 0x4D790) where the
	 * port's is the position (`(x & 0x1e) == 0x08`); both fire every 8 steps, so the
	 * gate is left as the port has it and only the generator and rule are switched.
	 */
	if ((e_them_rndstep() & 0x03) == 0)
		ent_ents[e].offsx = (S16)-ent_ents[e].offsx;
#else
	bx = e_them_rndnbr + *sh + *sl + 0x0d;
	cx = *sh;
	*bl ^= *ch;
	*bl ^= *cl;
	*bl ^= *bh;
	e_them_rndnbr = bx;

	ent_ents[e].offsx = (*bl & 0x01) ? -0x02 : 0x02;
#endif

	/* back to normal */

	return;

      }
    }

    /* U-turn */
    /*sys_printf("e_them_t2 u-turn\n");*/
    if (ent_ents[e].offsx == 0)
      ent_ents[e].offsx = 2;
    else
      ent_ents[e].offsx = -ent_ents[e].offsx;
#undef offsx
}

/*
 * Action function for e_them _t2 type
 *
 * ASM 2718
 */
void
e_them_t2_action(U8 e)
{
  e_them_t2_action2(e);

  /* they kill rick */
  if (e_rick_boxtest(e))
    e_rick_gozombie();

  /* lethal entities kill them */
  if (u_themtest(e)) {
    e_them_gozombie(e);
    return;
  }

  /* bullet kills them */
  if (E_BULLET_ENT.n &&
      u_fboxtest(e, E_BULLET_ENT.x + (e_bullet_offsx < 0 ? 00 : 0x18),
		 E_BULLET_ENT.y)) {
    E_BULLET_ENT.n = 0;
    e_them_gozombie(e);
    return;
  }

  /* bomb kills them */
  if (e_bomb_lethal && e_bomb_hit(e)) {
#ifdef PLATFORM_ST
    /* review-log.md R2.3b: the ST awards 50 HERE, on the explosion path, IN ADDITION to
       the 50 that kill_enemy awards itself -- so a dynamite kill scores 100, other kills
       50. ST: enemy_ai_update 0x4D542 tests explosion_overlaps_entity, adds 0x50 at
       0x4D54E, then calls kill_enemy (0x4D87C) which adds a second 0x50 at 0x4D892.
       The PC has exactly ONE score add on this path -- all four CALL 0x0292 sites in
       ibmpc_cs.bin are accounted for (level bonus 0x0DEE, super bonus 0x22BB, enemy kill
       0x24D6 inside gozombie, pickup 0x2585) and none is a second add here. Genuine
       PC-vs-ST difference; the port was faithful to the PC. */
    env_addscore(50);
#endif
    e_them_gozombie(e);
    return;
  }

  /* rick stops them */
  if (E_RICK_STTST(E_RICK_STSTOP) &&
      u_fboxtest(e, e_rick_stop_x, e_rick_stop_y))
    ent_ents[e].latency = ENT_STUN;
}


/*
 * Action sub-function for e_them _t3
 *
 * FIXME always starts asleep??
 *
 * Waits until triggered by something, then execute move steps from
 * ent_mvstep with sprite from ent_sprseq. When done, either restart
 * or disappear.
 *
 * Not always lethal ... but if lethal, kills rick.
 *
 * ASM: 255A
 */
void
e_them_t3_action2(U8 e)
{
#define sproffs c1
#define step_count c2
  U8 i;
  U16 x, y;

  while (1) {

    /* calc new sprite */
    i = ent_sprseq[ent_ents[e].sprbase + ent_ents[e].sproffs];
    if (i == 0xff)
      i = ent_sprseq[ent_ents[e].sprbase];
    ent_ents[e].sprite = i;

    if (ent_ents[e].sproffs != 0) {  /* awake */

      /* rotate sprseq */
      if (ent_sprseq[ent_ents[e].sprbase + ent_ents[e].sproffs] != 0xff)
	ent_ents[e].sproffs++;
      if (ent_sprseq[ent_ents[e].sprbase + ent_ents[e].sproffs] == 0xff)
	ent_ents[e].sproffs = 1;

      if (ent_ents[e].step_count < ent_mvstep[ent_ents[e].step_no].count) {
	/*
	 * still running this step: try to increment x and y while
	 * checking that they remain within boudaries. if so, return.
	 * else switch to next step.
	 */
	ent_ents[e].step_count++;
	x = ent_ents[e].x + ent_mvstep[ent_ents[e].step_no].dx;

	/* check'n save */
	if (x > 0 && x < 0xe8) {
	  ent_ents[e].x = x;
	  /*FIXME*/
	  /*
	  y = ent_mvstep[ent_ents[e].step_no].dy;
	  if (y < 0)
	    y += 0xff00;
	  y += ent_ents[e].y;
	  */
	  y = ent_ents[e].y + ent_mvstep[ent_ents[e].step_no].dy;
	  /*
	   * review-log.md I3 / defect #23. PC 0x278B:
	   *   add ax,cx / and ah,ah / jz store       ; y high == 0 -> store
	   *   cmp ah,0x1 / jnz reject
	   *   cmp al,0x40 / jc store                 ; y < 0x140   -> store
	   * Store iff y is in range -- the exact complement of the despawn predicate. It
	   * ACCEPTS y == 0, which the port's `y > 0` rejected.
	   */
	  if (!ENT_YDEAD(y)) {
	    ent_ents[e].y = y;
	    return;
	  }
	}
      }

      /*
       * step is done, or x or y is outside boundaries. try to
       * switch to next step
       */
      ent_ents[e].step_no++;
      if (ent_mvstep[ent_ents[e].step_no].count != 0xff) {
	/* there is a next step: init and loop */
	ent_ents[e].step_count = 0;
      }
      else {
	/* there is no next step: restart or deactivate */
	if (!E_RICK_STTST(E_RICK_STZOMBIE) &&
	    !(ent_ents[e].flags & ENT_FLG_ONCE)) {
	  /* loop this entity */
	  ent_ents[e].sproffs = 0;
	  ent_ents[e].n &= ~ENT_LETHAL;
	  if (ent_ents[e].flags & ENT_FLG_LETHALR)
	    ent_ents[e].n |= ENT_LETHAL;
	  ent_ents[e].x = ent_ents[e].xsave;
	  ent_ents[e].y = ent_ents[e].ysave;
	  /*
	   * review-log.md I3 / defect #22. PC 0x2739:
	   *   mov al,[si+0x1c] / mov [si+0x2],al     ; x = xsave  (+0x1C = xsave)
	   *   mov ax,[si+0x1e] / cmp ax,0x140 / jnc  ; y = ysave  (+0x1E = ysave)
	   * dead at y >= 0x140; the port had `> ENT_YMAX`, one row too high.
	   */
	  if (ENT_YDEAD(ent_ents[e].y)) {
	    ent_ents[e].n = 0;
	    return;
	  }
	}
	else {
	  /* deactivate this entity */
	  ent_ents[e].n = 0;
	  return;
	}
      }
    }
    else {  /* ent_ents[e].sprseq1 == 0 -- waiting */

      /* ugly GOTOs */

      if (ent_ents[e].flags & ENT_FLG_TRIGRICK) {  /* reacts to rick */
	/* wake up if triggered by rick */
	/*
	 * RICK_PROBE_DX, not 0x0C -- kb/demo-solver.md F13. ST scripted_trap_update
	 * 0x4D19A: move.w nPosX,D0 / addi.w #0xb,D0 / ... bsr 0x4D986. The PC's
	 * 0x27B1 is ADD AL,0x0C, so the constant was PC-only here.
	 */
	if (u_trigbox(e, E_RICK_ENT.x + RICK_PROBE_DX, E_RICK_ENT.y + 0x0A))
	  goto wakeup;
      }

      if (ent_ents[e].flags & ENT_FLG_TRIGSTOP) {  /* reacts to rick "stop" */
	/* wake up if triggered by rick "stop" */
	if (E_RICK_STTST(E_RICK_STSTOP) &&
	    u_trigbox(e, e_rick_stop_x, e_rick_stop_y))
	  goto wakeup;
      }

      if (ent_ents[e].flags & ENT_FLG_TRIGBULLET) {  /* reacts to bullets */
	/* wake up if triggered by bullet */
	if (E_BULLET_ENT.n && u_trigbox(e, e_bullet_xc, e_bullet_yc)) {
	  E_BULLET_ENT.n = 0;
	  goto wakeup;
	}
      }

      if (ent_ents[e].flags & ENT_FLG_TRIGBOMB) {  /* reacts to bombs */
	/* wake up if triggered by bomb */
	if (e_bomb_lethal && u_trigbox(e, e_bomb_xc, e_bomb_yc))
	  goto wakeup;
      }

      /* not triggered: keep waiting */
      return;

      /* something triggered the entity: wake up */
      /* initialize step counter */
    wakeup:
      if E_RICK_STTST(E_RICK_STZOMBIE)
	return;
#if defined(PLATFORM_ST) && defined(ENABLE_SOUND)
		/*
		 * ST plays wTriggerSound here, ZERO-GUARDED -- pm-baty.md G8:
		 *   4D25E  move.w (0x44,A0),D0     ; wTriggerSound
		 *   4D262  tst.w D0 / beq.s 4D272  ; 0 = silent
		 *   4D266  bclr #7,D0              ; strip the replay bit (the 0x9A entries)
		 *   4D26C  jsr play_music(0x44CCE)
		 * Unguarded, snd == 0 indexed WAV_ENTITY[-0x13] -- a wild sound_t* (Egypt
		 * alone has 26 trigger-flagged placements with snd == 0: the "jewel" freeze).
		 * Base 0x13 per review-log.md R3.12a: the ten real values 0x13..0x1C map onto
		 * WAV_ENTITY[0..9], all ten now populated (T19 / audio-sndh.md S7 -- closes
		 * G8 (c): there is no longer a WAV file to be missing, the engine already
		 * contains track 0x1C like every other track).
		 * The PC plays NOTHING at wakeup (ibmpc_cs.bin 0x2836..0x2860: zombie guard,
		 * lethal bits, step init, ret -- no sound call), so PLATFORM_ST only, like
		 * the empty-gun click (defect #21).
		 * NOT implemented: the ST's second site, replay-at-end-of-animation when
		 * bit 7 is set (0x4D2BC) -- see pm-baty.md G8 (d).
		 */
		if ((ent_ents[e].trigsnd & 0x7F) != 0)
			syssnd_play(WAV_ENTITY[(ent_ents[e].trigsnd & 0x7F) - 0x13]);
#endif
      ent_ents[e].n &= ~ENT_LETHAL;
      if (ent_ents[e].flags & ENT_FLG_LETHALI)
	ent_ents[e].n |= ENT_LETHAL;
      ent_ents[e].sproffs = 1;
      ent_ents[e].step_count = 0;
      ent_ents[e].step_no = ent_ents[e].step_no_i;
      return;
    }
  }
#undef step_count
}


/*
 * Action function for e_them _t3 type
 *
 * ASM 2546
 */
void
e_them_t3_action(U8 e)
{
  e_them_t3_action2(e);

  /* if lethal, can kill rick */
  if ((ent_ents[e].n & ENT_LETHAL) &&
      !E_RICK_STTST(E_RICK_STZOMBIE) && e_rick_boxtest(e)) {  /* CALL 1130 */
    e_rick_gozombie();
  }
}

/* eof */


