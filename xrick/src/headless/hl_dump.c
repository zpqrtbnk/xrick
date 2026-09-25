/*
 * xrick/src/headless/hl_dump.c
 *
 * xrick-core only (branch `solver`, never shipped): the RD1 game state as one JSON
 * object (PLAN.md T43 phase 5). Format and meaning: kb/demo-solver.md §9.
 *
 * Coordinates are the game's own: pixels, x 0..0xE7 across the play area, y from
 * the top of map_map (row 0 = first hidden-top row; the screen is rows 0x08..0x1F,
 * y 0x40..0xFF). A tile is 8x8, so tile (col, row) = (x >> 3, y >> 3).
 */

#include <stdio.h>

#include "hl_dump.h"
#include "control.h"
#include "env.h"
#include "ents.h"
#include "maps.h"
#include "game.h"
#include "e_rick.h"
#include "e_sbonus.h"

/* tile classes, first match wins -- kb/data-structures.md "Tile attribute bits" */
static char
tile_class(U8 f)
{
  if (f & MAP_EFLG_LETHAL) return 'x';
  if (f & MAP_EFLG_SPAD) return 'S';
  if (f & MAP_EFLG_SOLID) return '#';
  if (f & MAP_EFLG_WAYUP) return '=';
  if ((f & MAP_EFLG_CLIMB) && (f & MAP_EFLG_VERT)) return 'T';
  if (f & MAP_EFLG_CLIMB) return 'H';
  if (f & MAP_EFLG_VERT) return '|';
  if (f & MAP_EFLG_FGND) return ':';
  return '.';
}

/* ent_ents[].n & 0x7f -> kind, as ents.c ent_action dispatches it */
static const char *
kind(U8 k)
{
  if (k == 0x00) return "free";
  if (k == 0x01) return "rick";
  if (k == 0x02) return "bullet";
  if (k == 0x03) return "bomb";
  if (k == 0x47) return "zombie";
  if (k >= 0x18) return "trap";      /* e_them type 3, scripted */
  if (k == 0x10) return "crate_bombs";
  if (k == 0x11) return "crate_bullets";
  if (k >= 0x12 && k <= 0x15) return "bonus";
  if (k == 0x16) return "timer_start";
  if (k == 0x17) return "timer_stop";
  switch ((k - 0x04) % 3)
  {
    case 0: return "enemy_1a";
    case 1: return "enemy_1b";
    default: return "enemy_2";
  }
}

static void
flag_list(FILE *f, U8 v)
{
  static const char *names[8] = {
    "once", "stoprick", "lethal_restart", "lethal_init",
    "trig_bomb", "trig_bullet", "trig_stop", "trig_rick"
  };
  int i, first = 1;

  fputc('[', f);
  for (i = 0; i < 8; i++)
    if (v & (1 << i))
    {
      fprintf(f, "%s\"%s\"", first ? "" : ",", names[i]);
      first = 0;
    }
  fputc(']', f);
}

static void
rick_state(FILE *f, U8 s)
{
  static const char *names[7] = {
    "stop", "shoot", "climb", "jump", "zombie", "dead", "crawl"
  };
  int i, first = 1;

  fputc('[', f);
  for (i = 0; i < 7; i++)
    if (s & (1 << i))
    {
      fprintf(f, "%s\"%s\"", first ? "" : ",", names[i]);
      first = 0;
    }
  fputc(']', f);
}

void
hl_dump(FILE *f)
{
  static const char *status[3] = { "running", "game_over", "game_completed" };
  U16 c, m;
  U8 i, k, row, col;
  int lo;

  fprintf(f, "{\n");
  fprintf(f, "  \"step\": %lu, \"status\": \"%s\",\n",
	  (unsigned long)game_hlSteps(), status[game_hlStatus()]);
  fprintf(f, "  \"map\": %u, \"submap\": %u, \"frow\": %u,\n",
	  (unsigned int)env_map, (unsigned int)env_submap, (unsigned int)map_frow);
  fprintf(f, "  \"lives\": %u, \"bombs\": %u, \"bullets\": %u, \"score\": %lu,\n",
	  (unsigned int)env_lives, (unsigned int)env_bombs,
	  (unsigned int)env_bullets, (unsigned long)env_score);
  fprintf(f, "  \"timer\": {\"counting\": %s, \"counter\": %u, \"bonus\": %u},\n",
	  e_sbonus_counting ? "true" : "false", (unsigned int)e_sbonus_counter,
	  (unsigned int)e_sbonus_bonus);

  /* rick: slot 1 */
  fprintf(f, "  \"rick\": {\"x\": %d, \"y\": %d, \"w\": %u, \"h\": %u, \"dir\": \"%s\", "
	  "\"state\": ", (int)ent_ents[1].x, (int)ent_ents[1].y,
	  (unsigned int)ent_ents[1].w, (unsigned int)ent_ents[1].h,
	  game_dir == LEFT ? "left" : "right");
  rick_state(f, e_rick_state);
  fprintf(f, ", \"at_exit\": %s},\n", e_rick_atExit ? "true" : "false");

  /* live entities, rick excepted */
  fprintf(f, "  \"entities\": [");
  for (i = 0, k = 0; i < ENT_ENTSNUM; i++)
  {
    ent_t *e = &ent_ents[i];
    U8 t = (U8)(e->n & 0x7f);

    if (i == 1 || !e->n)
      continue;
    fprintf(f, "%s\n    {\"slot\": %u, \"n\": %u, \"kind\": \"%s\", \"lethal\": %s, "
	    "\"x\": %d, \"y\": %d, \"w\": %u, \"h\": %u, \"sprite\": %u, \"mark\": %u, "
	    "\"flags\": ",
	    k++ ? "," : "", (unsigned int)i, (unsigned int)e->n, kind(t),
	    (e->n & ENT_LETHAL) ? "true" : "false", (int)e->x, (int)e->y,
	    (unsigned int)e->w, (unsigned int)e->h, (unsigned int)e->sprite,
	    (unsigned int)e->mark);
    flag_list(f, e->flags);
    if (t < ENT_NBR_ENTDATA && (e->flags & (ENT_FLG_TRIGBOMB|ENT_FLG_TRIGBULLET|
					   ENT_FLG_TRIGSTOP|ENT_FLG_TRIGRICK)))
      /* u_trigbox: [x0, x1) x [y0, y1) */
      fprintf(f, ", \"trigger\": [%u, %u, %u, %u]",
	      (unsigned int)e->trig_x, (unsigned int)e->trig_y,
	      (unsigned int)(e->trig_x + (ent_entdata[t].trig_w << 3)),
	      (unsigned int)(e->trig_y + (ent_entdata[t].trig_h << 3)));
    fputc('}', f);
  }
  fprintf(f, "\n  ],\n");

  /* this submap's placements: what can spawn here, and what is already gone */
  fprintf(f, "  \"marks\": [");
  for (m = map_submaps[env_submap].mark, k = 0; map_marks[m].row != 0xff; m++)
  {
    fprintf(f, "%s\n    {\"mark\": %u, \"n\": %u, \"kind\": \"%s\", \"row\": %u, "
	    "\"x\": %u, \"done\": %s, \"flags\": ",
	    k++ ? "," : "", (unsigned int)m, (unsigned int)(map_marks[m].ent & 0x7f),
	    kind((U8)(map_marks[m].ent & 0x7f)),
	    (unsigned int)((map_marks[m].row & 0xf8) + (map_marks[m].xy & 0x07)),
	    (unsigned int)(map_marks[m].xy & 0xf8),
	    (map_marks[m].ent & MAP_MARK_NACT) ? "true" : "false");
    flag_list(f, map_marks[m].flags);
    fputc('}', f);
  }
  fprintf(f, "\n  ],\n");

  /*
   * exits: maps.c map_chain. rick leaves by the left (x < 0) or right (x >= 0xE8)
   * edge; the connector used is the first with dir == game_dir and
   * (y >> 3) + frow - rowout in 0..2, i.e. rick's tile row in [lo, lo + 2].
   */
  fprintf(f, "  \"exits\": [");
  for (c = map_submaps[env_submap].connect, k = 0; map_connect[c].dir != 0xff; c++)
  {
    lo = (int)map_connect[c].rowout - (int)map_frow;
    fprintf(f, "%s\n    {\"side\": \"%s\", \"rows\": [%d, %d], \"to\": ",
	    k++ ? "," : "", map_connect[c].dir == LEFT ? "left" : "right", lo, lo + 2);
    if (map_connect[c].submap == 0xff)
      fprintf(f, "\"next_map\"}");
    else
      fprintf(f, "%u}", (unsigned int)map_connect[c].submap);
  }
  fprintf(f, "\n  ],\n");

  /* tiles: map_map rows 0x00..0x27, one class char per tile */
  fprintf(f, "  \"tiles_legend\": {\".\": \"empty\", \":\": \"foreground\", "
	  "\"#\": \"solid\", \"=\": \"one-way up\", \"S\": \"bounce\", \"H\": \"climb\", "
	  "\"T\": \"climb top\", \"|\": \"vertical only\", \"x\": \"lethal\"},\n");
  fprintf(f, "  \"screen_rows\": [%u, %u],\n", MAP_ROW_SCRTOP, MAP_ROW_SCRBOT);
  fprintf(f, "  \"tiles\": [");
  for (row = 0; row <= MAP_ROW_HBBOT; row++)
  {
    fprintf(f, "%s\n    \"", row ? "," : "");
    for (col = 0; col < 0x20; col++)
      fputc(tile_class(map_eflg[map_map[row][col]]), f);
    fputc('"', f);
  }
  fprintf(f, "\n  ]\n}\n");
}

/* eof */
