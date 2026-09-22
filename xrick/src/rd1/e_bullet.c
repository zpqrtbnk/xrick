/*
 * xrick/src/e_bullet.c
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
#include "game.h"
#include "ents.h"
#include "sounds.h"
#include "e_bullet.h"
#include "sysvid.h"

#include "maps.h"

/*
 * public vars (for performance reasons)
 */
S8 e_bullet_offsx;
U16 e_bullet_xc, e_bullet_yc;

/*
 * Initialize bullet
 */
void
e_bullet_init(U16 x, U16 y)
{
  E_BULLET_ENT.n = 0x02;
  E_BULLET_ENT.x = x;
  E_BULLET_ENT.y = y + 0x0006;
  if (game_dir == LEFT) {
    e_bullet_offsx = -0x08;
    E_BULLET_ENT.sprite = 0x21;
  }
  else {
    e_bullet_offsx = 0x08;
    E_BULLET_ENT.sprite = 0x20;
  }
#ifdef ENABLE_SOUND
  syssnd_play(WAV_BULLET);
#endif
}


/*
 * Entity action
 *
 * ASM 1883, 0F97
 */
void
e_bullet_action(UNUSED(U8 e))
{
  /* move bullet */
  E_BULLET_ENT.x += e_bullet_offsx;

  /*
   * review-log.md R3.7. Bounds corrected against the PC, which is unambiguous
   * (e_bullet_action @ 0x1A01):
   *   left  : ADD AL,[SI+2] / JNC 0x1A44   -> byte underflow, i.e. deactivate when x < 0
   *   right : CMP AL,0xE8   / JNC 0x1A44   -> deactivate when x >= 0xE8
   * The port had `x <= -0x10 || x > 0xe8`: sixteen pixels late on the left and one late
   * on the right. (While `x` was U16 the left test was dead and the wrap happened to
   * reproduce `x < 0` via the right-hand arm; making x signed exposed it.)
   *
   * The ST has NO explicit bounds test at all -- player_bullet_update @ 0x4CA5A just
   * calls the terrain test and deactivates on carry, relying on the map border being
   * SOLID. The PC's bounds are kept on both platforms as a guard, because without them
   * a negative x would index map_map out of range; the ST reaches the same outcome by
   * its data rather than by a test.
   */
  if (E_BULLET_ENT.x < 0 || E_BULLET_ENT.x >= 0xe8) {
    /* out: deactivate */
    E_BULLET_ENT.n = 0;
  }
  else {
    /* update bullet center coordinates */
    e_bullet_xc = E_BULLET_ENT.x + 0x0c;
    e_bullet_yc = E_BULLET_ENT.y + 0x05;
    if (map_eflg[map_map[e_bullet_yc >> 3][e_bullet_xc >> 3]] &
	MAP_EFLG_SOLID) {
      /* hit something: deactivate */
      E_BULLET_ENT.n = 0;
    }
#ifdef PLATFORM_ST
    /*
     * The ST also stops the bullet on the slot-0 block entity; the PC does not.
     * ST 0x4CD2A-0x4CD5A: tst.w (0x4A702) / cmp against (0x4A706)+(0x4A712) and
     * (0x4A708)+(0x4A714), blocked iff bx <= px < bx+w and by <= py < by+h.
     * The PC's terrain test (0x1115) is `AND AL,0x40` on the tile only -- no entity
     * test of any kind. review-log.md R3.7.
     */
    else if (ent_ents[0].n != 0 &&
	     e_bullet_xc >= ent_ents[0].x &&
	     e_bullet_xc <  ent_ents[0].x + ent_ents[0].w &&
	     e_bullet_yc >= ent_ents[0].y &&
	     e_bullet_yc <  ent_ents[0].y + ent_ents[0].h) {
      E_BULLET_ENT.n = 0;
    }
#endif
  }
}


/* eof */


