/*
 * xrick/src/util.c
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

#include <stdlib.h>  /* NULL */

#include "system.h"
#include "config.h"
#include "env.h"

#include "util.h"

#include "game.h"
#include "ents.h"
#include "e_rick.h"
#include "maps.h"

/*
 * Full box test.
 *
 * ASM 1199
 *
 * e: entity to test against.
 * x,y: coordinates to test.
 * ret: TRUE/(x,y) is within e's space, FALSE/not.
 */
U8
u_fboxtest(U8 e, U16 x, U16 y)
{
  /*
   * review-log.md R3.6c. Verified against both originals instruction by instruction.
   *
   * PC u_fboxtest @ 0x1317          ST entity_contains_point @ 0x4CC4C
   *   MOV AL,[SI+2]/CMP AL,BL/JNC     move.w (0x4,A0),D0w / cmp.w D1w,D0w / bgt
   *     -> FALSE if ent.x >= x          -> FALSE if ent.x >  x     (ST includes x==ent.x)
   *   ADD AL,[SI+0xE]/CMP AL,BL/JC    subi.w #1 / add.w (0x10,A0) / cmp.w D1w,D0w / blt
   *     -> FALSE if ent.x+w   <  x      -> FALSE if ent.x+w-1 <  x  (ST upper is one less)
   *   MOV AX,[SI+4]/CMP DX,AX/JC      move.w (0x6,A0),D0w / cmp.w D2w,D0w / bgt
   *     -> FALSE if y < ent.y           -> FALSE if ent.y > y   -- BOTH INCLUSIVE
   *   ADD AX,CX/CMP AX,DX/JC          subi.w #1 / add.w (0x12,A0) / blt
   *     -> FALSE if ent.y+h   <  y      -> FALSE if ent.y+h-1 <  y
   *
   * NOTE the vertical lower bound: BOTH originals accept y == ent.y. The port wrote
   * `ent.y >= y`, which rejects it -- a one-pixel PORT DEFECT, corrected below on both
   * platforms. The horizontal lower bound and both upper bounds are genuine PC-vs-ST
   * divergences and are switched.
   */
#ifdef PLATFORM_ST
  if (ent_ents[e].x > x ||
      ent_ents[e].x + ent_ents[e].w - 1 < x ||
      ent_ents[e].y > y ||
      ent_ents[e].y + ent_ents[e].h - 1 < y)
    return FALSE;
  else
    return TRUE;
#else /* PLATFORM_PC */
  if (ent_ents[e].x >= x ||
      ent_ents[e].x + ent_ents[e].w < x ||
      ent_ents[e].y > y ||
      ent_ents[e].y + ent_ents[e].h < y)
    return FALSE;
  else
    return TRUE;
#endif
}




/*
 * Box test (then whole e2 is checked agains the center of e1).
 *
 * ASM 113E
 *
 * e1: entity to test against (corresponds to DI in asm code).
 * e2: entity to test (corresponds to SI in asm code).
 * ret: TRUE/intersect, FALSE/not.
 */
U8
u_boxtest(U8 e1, U8 e2)
{
  /* rick is special (may be crawling) */
  if (e1 == E_RICK_NO)
    return e_rick_boxtest(e2);

  /*
   * entity 1: x+0x05 to x+0x011, y to y+0x14
   * entity 2: x to x+ .w, y to y+ .h
   */
  if (ent_ents[e1].x + 0x11 < ent_ents[e2].x ||
      ent_ents[e1].x + 0x05 > ent_ents[e2].x + ent_ents[e2].w ||
      ent_ents[e1].y + 0x14 < ent_ents[e2].y ||
      ent_ents[e1].y > ent_ents[e2].y + ent_ents[e2].h - 1)
    return FALSE;
  else
    return TRUE;
}


/*
 * Compute the environment flag.
 *
 * ASM 0FBC if !crawl, else 103E
 *
 * x, y: coordinates where to compute the environment flag
 * crawl: is rick crawling?
 * rc0: anything CHANGED to the environment flag for crawling (6DBA)
 * rc1: anything CHANGED to the environment flag (6DAD)
 */
void
u_envtest(U16 x, U16 y, U8 crawl, U8 *rc0, U8 *rc1)
{
  U8 i, xx;

  /* prepare for ent #0 test */
  ent_ents[ENT_ENTSNUM].x = x;
  ent_ents[ENT_ENTSNUM].y = y;

  i = 1;
  if (!crawl) i++;
  if (y & 0x0004) i++;

  x += 4;
  xx = (U8)x; /* FIXME? */

  x = x >> 3;  /* from pixels to tiles */
  y = y >> 3;  /* from pixels to tiles */

  *rc0 = *rc1 = 0;

  if (xx & 0x07) {  /* tiles columns alignment */
    if (crawl) {
      *rc0 |= (map_eflg[map_map[y][x]] &
	   (MAP_EFLG_VERT|MAP_EFLG_SOLID|MAP_EFLG_SPAD|MAP_EFLG_WAYUP));
      *rc0 |= (map_eflg[map_map[y][x + 1]] &
	   (MAP_EFLG_VERT|MAP_EFLG_SOLID|MAP_EFLG_SPAD|MAP_EFLG_WAYUP));
      *rc0 |= (map_eflg[map_map[y][x + 2]] &
	   (MAP_EFLG_VERT|MAP_EFLG_SOLID|MAP_EFLG_SPAD|MAP_EFLG_WAYUP));
      y++;
    }
    do {
      *rc1 |= (map_eflg[map_map[y][x]] &
	       (MAP_EFLG_SOLID|MAP_EFLG_SPAD|MAP_EFLG_FGND|
		MAP_EFLG_LETHAL|MAP_EFLG_01));
      *rc1 |= (map_eflg[map_map[y][x + 1]] &
	       (MAP_EFLG_SOLID|MAP_EFLG_SPAD|MAP_EFLG_FGND|
		MAP_EFLG_LETHAL|MAP_EFLG_CLIMB|MAP_EFLG_01));
      *rc1 |= (map_eflg[map_map[y][x + 2]] &
	       (MAP_EFLG_SOLID|MAP_EFLG_SPAD|MAP_EFLG_FGND|
		MAP_EFLG_LETHAL|MAP_EFLG_01));
      y++;
    } while (--i > 0);

    *rc1 |= (map_eflg[map_map[y][x]] &
	     (MAP_EFLG_SOLID|MAP_EFLG_SPAD|MAP_EFLG_WAYUP|MAP_EFLG_FGND|
	      MAP_EFLG_LETHAL|MAP_EFLG_01));
    *rc1 |= (map_eflg[map_map[y][x + 1]]);
    *rc1 |= (map_eflg[map_map[y][x + 2]] &
	     (MAP_EFLG_SOLID|MAP_EFLG_SPAD|MAP_EFLG_WAYUP|MAP_EFLG_FGND|
	      MAP_EFLG_LETHAL|MAP_EFLG_01));
  }
  else {
    if (crawl) {
      *rc0 |= (map_eflg[map_map[y][x]] &
	   (MAP_EFLG_VERT|MAP_EFLG_SOLID|MAP_EFLG_SPAD|MAP_EFLG_WAYUP));
      *rc0 |= (map_eflg[map_map[y][x + 1]] &
	   (MAP_EFLG_VERT|MAP_EFLG_SOLID|MAP_EFLG_SPAD|MAP_EFLG_WAYUP));
      y++;
    }
    do {
      *rc1 |= (map_eflg[map_map[y][x]] &
	       (MAP_EFLG_SOLID|MAP_EFLG_SPAD|MAP_EFLG_FGND|
		MAP_EFLG_LETHAL|MAP_EFLG_CLIMB|MAP_EFLG_01));
      *rc1 |= (map_eflg[map_map[y][x + 1]] &
	       (MAP_EFLG_SOLID|MAP_EFLG_SPAD|MAP_EFLG_FGND|
		MAP_EFLG_LETHAL|MAP_EFLG_CLIMB|MAP_EFLG_01));
      y++;
    } while (--i > 0);

    *rc1 |= (map_eflg[map_map[y][x]]);
    *rc1 |= (map_eflg[map_map[y][x + 1]]);
  }

  /*
   * If not lethal yet, and there's an entity on slot zero, and (x,y)
   * boxtests this entity, then raise SOLID flag. This is how we make
   * sure that no entity can move over the entity that is on slot zero.
   *
   * Beware! When env_invicible is set, this means that a block can
   * move over rick without killing him -- but then rick is trapped
   * because the block is solid.
   */
  if (!(*rc1 & MAP_EFLG_LETHAL)
      && ent_ents[0].n
      && u_boxtest(ENT_ENTSNUM, 0)) {
    *rc1 |= MAP_EFLG_SOLID;
  }

  /* When invicible, the environment can not be lethal. */
  if (env_invicible) *rc1 &= ~MAP_EFLG_LETHAL;
}


/*
 * Check if x,y is within e trigger box.
 *
 * ASM 126F
 * return: FALSE if not in box, TRUE if in box.
 */
U8
u_trigbox(U8 e, U16 x, U16 y)
{
  U16 xmax, ymax;

  xmax = ent_ents[e].trig_x + (ent_entdata[ent_ents[e].n & 0x7F].trig_w << 3);
  ymax = ent_ents[e].trig_y + (ent_entdata[ent_ents[e].n & 0x7F].trig_h << 3);

  /*
   * review-log.md R3.6b. Two differences, both verified at instruction level.
   *
   * PC  u_trigbox @ 0x13ED:
   *   MOV AL,[SI+0x16] / CMP AL,BL / JNC fail   -> FALSE if trig_x >= x  (EXCLUSIVE low)
   *   ... AH = trig_x + trig_w*8 ; JNC / MOV AH,0xFF   -> saturating byte CLAMP
   *   CMP AH,CH / JC fail                        -> FALSE if xmax  <  x  (inclusive high)
   * ST  trigger_box_contains_point @ 0x4D986:
   *   cmp.w (0x3c,A0),D0w / blt fail             -> FALSE if x < XMin    (INCLUSIVE low)
   *   cmp.w (0x40,A0),D0w / bgt fail             -> FALSE if x > XMax    (inclusive high)
   *   no clamp -- XMax is a precomputed word field (0x40/0x42), set at spawn by
   *   init_entity_from_placement without saturation, and it CAN exceed 0xFF
   *   (trig_x up to 0xF8 plus trig_w*8).
   *
   * So the ST box includes its left/top edge where the PC's excludes it, and the ST box
   * is not truncated at 0xFF near the right edge.
   */
#ifdef PLATFORM_ST
  if (x < ent_ents[e].trig_x || x > xmax ||
      y < ent_ents[e].trig_y || y > ymax)
    return FALSE;
  else
    return TRUE;
#else /* PLATFORM_PC */
  if (xmax > 0xFF) xmax = 0xFF;

  if (x <= ent_ents[e].trig_x || x > xmax ||
      y <= ent_ents[e].trig_y || y > ymax)
    return FALSE;
  else
    return TRUE;
#endif
}


/* eof */
