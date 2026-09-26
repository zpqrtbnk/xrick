/*
 * xrick/src/headless/hl_state.c
 *
 * xrick-core only (branch `solver`, never shipped): snapshot, restore and hash of
 * the RD1 game state. The region list is kb/demo-solver.md §3 (class L); keep the
 * two in step. Checked by `xrick-core -fuzz`.
 */

#include <stdio.h>
#include <string.h>

#include "hl_state.h"
#include "sysarg.h"
#include "control.h"
#include "env.h"
#include "ents.h"
#include "maps.h"
#include "game.h"
#include "e_rick.h"
#include "e_them.h"
#include "e_bomb.h"
#include "e_bullet.h"
#include "e_sbonus.h"

/*
 * class L: the snapshot. extern variables here, file statics via the
 * <file>_hlRegions functions.
 */
static void
regions(hl_region_f f)
{
  f(ent_ents, sizeof(ent_ents), "ent_ents");
  f(map_marks, sizeof(map_marks), "map_marks");
  f(map_map, sizeof(map_map), "map_map");
  f(map_eflg, sizeof(map_eflg), "map_eflg");
  f(&map_frow, sizeof(map_frow), "map_frow");
  f(&env_lives, sizeof(env_lives), "env_lives");
  f(&env_bombs, sizeof(env_bombs), "env_bombs");
  f(&env_bullets, sizeof(env_bullets), "env_bullets");
  f(&env_score, sizeof(env_score), "env_score");
  f(&env_map, sizeof(env_map), "env_map");
  f(&env_submap, sizeof(env_submap), "env_submap");
  f(&env_trainer, sizeof(env_trainer), "env_trainer");
  f(&env_invicible, sizeof(env_invicible), "env_invicible");
  f(&env_changeSubmap, sizeof(env_changeSubmap), "env_changeSubmap");
  f(&e_rick_state, sizeof(e_rick_state), "e_rick_state");
  f(&e_rick_atExit, sizeof(e_rick_atExit), "e_rick_atExit");
  f(&e_rick_exitDir, sizeof(e_rick_exitDir), "e_rick_exitDir");
  f(&e_rick_stop_x, sizeof(e_rick_stop_x), "e_rick_stop_x");
  f(&e_rick_stop_y, sizeof(e_rick_stop_y), "e_rick_stop_y");
  f(&game_dir, sizeof(game_dir), "game_dir");
  f(&e_bomb_lethal, sizeof(e_bomb_lethal), "e_bomb_lethal");
  f(&e_bomb_ticker, sizeof(e_bomb_ticker), "e_bomb_ticker");
  f(&e_bomb_xc, sizeof(e_bomb_xc), "e_bomb_xc");
  f(&e_bomb_yc, sizeof(e_bomb_yc), "e_bomb_yc");
  f(&e_bullet_offsx, sizeof(e_bullet_offsx), "e_bullet_offsx");
  f(&e_bullet_xc, sizeof(e_bullet_xc), "e_bullet_xc");
  f(&e_bullet_yc, sizeof(e_bullet_yc), "e_bullet_yc");
  f(&e_sbonus_bonus, sizeof(e_sbonus_bonus), "e_sbonus_bonus");
  f(&e_sbonus_counter, sizeof(e_sbonus_counter), "e_sbonus_counter");
  f(&e_sbonus_counting, sizeof(e_sbonus_counting), "e_sbonus_counting");
  f(&e_them_rndseed, sizeof(e_them_rndseed), "e_them_rndseed");
  f(&control_status, sizeof(control_status), "control_status");
  f(&control_last, sizeof(control_last), "control_last");
  f(&control_active, sizeof(control_active), "control_active");
  f(&sysarg_args_map, sizeof(sysarg_args_map), "sysarg_args_map");
  f(&sysarg_args_submap, sizeof(sysarg_args_submap), "sysarg_args_submap");
  game_hlRegions(f);
  e_rick_hlRegions(f);
  e_them_hlRegions(f);
}

/*
 * class K: never written (grep), hashed only
 */
static void
guards(hl_region_f f)
{
  f(ent_entdata, sizeof(ent_entdata), "ent_entdata");
  f(ent_mvstep, sizeof(ent_mvstep), "ent_mvstep");
  f(ent_sprseq, sizeof(ent_sprseq), "ent_sprseq");
  f(map_blocks, sizeof(map_blocks), "map_blocks");
  f(map_bnums, sizeof(map_bnums), "map_bnums");
  f(map_connect, sizeof(map_connect), "map_connect");
  f(map_eflg_c, sizeof(map_eflg_c), "map_eflg_c");
  f(map_maps, sizeof(map_maps), "map_maps");
  f(map_submaps, sizeof(map_submaps), "map_submaps");
}

/*
 * region walkers
 */
static size_t size_n;
static U8 *save_p;
static const U8 *load_p;
static unsigned long long hash_h;

static void size_f(void *p, size_t n, const char *s) { (void)p; (void)s; size_n += n; }
static void save_f(void *p, size_t n, const char *s) { (void)s; memcpy(save_p, p, n); save_p += n; }
static void load_f(void *p, size_t n, const char *s) { (void)s; memcpy(p, load_p, n); load_p += n; }

static void
hash_f(void *p, size_t n, const char *s)
{
  const U8 *b = (const U8 *)p;
  size_t i;

  (void)s;
  for (i = 0; i < n; i++)
  {
    hash_h ^= b[i];
    hash_h *= 0x100000001b3ULL;
  }
}

static void
list_f(void *p, size_t n, const char *s)
{
  (void)p;
  printf("  %-20s %6lu\n", s, (unsigned long)n);
}

size_t
hl_stateSize(void)
{
  size_n = 0;
  regions(size_f);
  return size_n;
}

void
hl_stateSave(U8 *buf)
{
  save_p = buf;
  regions(save_f);
}

void
hl_stateLoad(const U8 *buf)
{
  load_p = buf;
  regions(load_f);
}

unsigned long long
hl_stateHash(void)
{
  hash_h = 0xcbf29ce484222325ULL;
  regions(hash_f);
  guards(hash_f);
  return hash_h;
}

/*
 * the state as a search key: the snapshot minus the counters and the held
 * controls, which do not change what can happen next (control_status is set
 * anew by every game_hlStep, control_last only matters for CONTROL_EXIT)
 */
static void
key_f(void *p, size_t n, const char *s)
{
  if (!strcmp(s, "hl_steps") || !strcmp(s, "hl_segments") ||
      !strcmp(s, "control_status") || !strcmp(s, "control_last"))
    return;
  hash_f(p, n, s);
}

unsigned long long
hl_stateKey(void)
{
  hash_h = 0xcbf29ce484222325ULL;
  regions(key_f);
  return hash_h;
}

void
hl_stateList(void)
{
  printf("snapshot (class L), %lu bytes:\n", (unsigned long)hl_stateSize());
  regions(list_f);
  printf("hash guard (class K):\n");
  guards(list_f);
}


/*
 * name the regions where two snapshots differ, to stdout
 */
static const U8 *diff_a, *diff_b;
static size_t diff_o;

static void
diff_f(void *p, size_t n, const char *s)
{
  size_t i;

  (void)p;
  for (i = 0; i < n; i++)
    if (diff_a[diff_o + i] != diff_b[diff_o + i])
    {
      printf("  differs: %s +%lu (%02x / %02x)\n", s, (unsigned long)i,
	     diff_a[diff_o + i], diff_b[diff_o + i]);
      break;
    }
  diff_o += n;
}

void
hl_stateDiff(const U8 *a, const U8 *b)
{
  diff_a = a; diff_b = b; diff_o = 0;
  regions(diff_f);
}

/* eof */
