/*
 * Copyright (C) 1998-NOW bigorno (bigorno@bigorno.net). All rights reserved.
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
#include "e_bonus.h"
#include "e_rick.h"
#include "maps.h"


/*
 * Entity action
 *
 * ASM 242C
 */
void
e_bonus_action(U8 e)
{
#define seq c1

  /*
   * review-log.md R3.11. The two originals differ fundamentally here.
   *
   * PC (@0x2575): CALL 0x12AE (box test) / JZ skip / MOV byte[SI],0 / score / sound /
   *   OR byte[BX],0x80 (mark NACT).  The bonus is deactivated IMMEDIATELY -- there is no
   *   rise animation of any kind.
   * ST treasure_pickup_update (@0x4D102):
   *   collect: entity_overlaps_player -> add_score(0x500) / mark_placement_dead /
   *            move.w #0xC,(0x2c,A0) / play_music(0x11)
   *   each later frame: subi.w #1,(0x2c,A0) ; bne -> gfx = sparkle, subi.w #2,(0x6,A0)
   *                                          ; beq -> despawn
   *   Counter 12 therefore yields ELEVEN animated frames of y -= 2, a 22px rise.
   *
   * The port had a rise of its own -- seq 1..9 with an initial y -= 8, i.e. 26px over
   * ten frames, and an ST sprite number (0xad) used unconditionally. That matched
   * neither original. Each platform now gets its own.
   */
#ifdef PLATFORM_ST
  if (ent_ents[e].seq == 0) {
    if (e_rick_boxtest(e)) {
      env_addscore(500);
#ifdef ENABLE_SOUND
      syssnd_play(WAV_BONUS);
#endif
      map_marks[ent_ents[e].mark].ent |= MAP_MARK_NACT;
      ent_ents[e].seq = 12;            /* move.w #0xC,(0x2c,A0) */
      ent_ents[e].sprite = 0xad;
      ent_ents[e].front = TRUE;
    }
  }
  else if (--ent_ents[e].seq == 0) {   /* subi.w #1 ; beq -> despawn */
    ent_ents[e].n = 0;
  }
  else {
    ent_ents[e].y -= 2;                /* subi.w #2,(0x6,A0) */
  }
#else /* PLATFORM_PC */
  if (e_rick_boxtest(e)) {
    ent_ents[e].n = 0;                 /* MOV byte[SI],0 -- instant, no animation */
    env_addscore(500);
#ifdef ENABLE_SOUND
    syssnd_play(WAV_BONUS);
#endif
    map_marks[ent_ents[e].mark].ent |= MAP_MARK_NACT;
  }
#endif
}


/* eof */


