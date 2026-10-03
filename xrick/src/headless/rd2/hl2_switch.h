/*
 * xrick/src/headless/rd2/hl2_switch.h
 *
 * xrick2-core only (branch `solver`, never shipped): switches = trigger boxes fired
 * by the laser, a bomb or melee, and how to fire one (PLAN.md T47 phase 6).
 */

#ifndef _HL2_SWITCH_H
#define _HL2_SWITCH_H

#include "system.h"
#include "hl2_solve.h"

typedef struct {
	U32 rec, box;    /* spawn record and box addresses */
	int x, row;      /* box x (px), top row */
	int w, h;        /* px */
	int mask;        /* d3 & $7f: 2 shot, 4 bomb, 8 melee (algo-actors.md §4) */
	int actor;       /* the record spawns an actor (b2 bit 7): its boxes are tested only while it lives */
	int spawned;     /* the record's b0 bit 7 */
} hl2_switch_t;

extern int hl2_switches(hl2_switch_t *, int max);   /* the current submap's */
extern void hl2_switchesSort(hl2_switch_t *, int n, int row, int col);   /* nearest to (row, col) first */
extern int hl2_switchFired(const hl2_switch_t *);   /* nothing left to fire: its actor gone / record spawned */
extern int hl2_switchFire(const hl2_solveopt_t *, const hl2_switch_t *, U8 *seq, int max);

#endif

/* eof */
