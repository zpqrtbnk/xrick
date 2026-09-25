/*
 * xrick/src/headless/hl_state.h
 *
 * xrick-core only (branch `solver`, never shipped): snapshot, restore and hash of
 * the RD1 game state, over the class L variables of kb/demo-solver.md §3, taken
 * between two game_hlStep calls. Class K tables are hashed too, as a guard
 * against writes nobody knows about (§4).
 */

#ifndef _HL_STATE_H
#define _HL_STATE_H

#include <stddef.h>

#include "system.h"

/* one memory region: address, size, name (for diagnostics) */
typedef void (*hl_region_f)(void *, size_t, const char *);

/* the file-static part of the state, defined under #ifdef HEADLESS in each file */
extern void game_hlRegions(hl_region_f);
extern void e_rick_hlRegions(hl_region_f);
extern void e_them_hlRegions(hl_region_f);

extern size_t hl_stateSize(void);          /* bytes a snapshot takes */
extern void hl_stateSave(U8 *);            /* copy the state out, hl_stateSize bytes */
extern void hl_stateLoad(const U8 *);      /* and back in */
extern unsigned long long hl_stateHash(void);  /* FNV-1a 64 over state + guard tables */
extern void hl_stateList(void);            /* print the regions to stdout */
extern void hl_stateDiff(const U8 *, const U8 *);  /* print where two snapshots differ */

#endif

/* eof */
