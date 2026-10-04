/*
 * xrick/src/headless/rd2/hl2_solve.h
 *
 * xrick2-core only (branch `solver`, never shipped): search the joystick bytes that
 * take Rick from the current state through one exit of the current submap without
 * dying (PLAN.md T47 phase 6, kb2/demo-solver.md).
 */

#ifndef _HL2_SOLVE_H
#define _HL2_SOLVE_H

#include "system.h"

/* what a search got to: its closest distance and where Rick was then (the frontier) */
typedef struct {
	int best_h;
	int row, col;
} hl2_solveres_t;

typedef struct {
	int beam;       /* states kept per frame count */
	int jobs;       /* worker processes expanding the beam (1: none, the search runs alone) */
	int stall;      /* give up after this many frames with no new closest distance */
	hl2_solveres_t *res;  /* if not NULL: filled in by hl2_solve */
	int maxsteps;   /* give up past this many frames */
	int exit;       /* index in the submap's exit list (hl2_exits); -1: hl2_solveRoute's choice */
	int verbose;    /* progress on stderr */
	int minbombs;   /* bombs to hold at the exit */
	int minlaser;   /* laser shots to hold at the exit */
	int wp_row, wp_col;  /* >= 0: a waypoint (feet row, column) instead of the exit */
	int noreserve;       /* 1: no ammo reserve (a map with bomb pickups ahead: map 4) */
	int survive;         /* > 0: also a goal: this many frames played and no bomb in play,
	                        Rick alive (escaping a bomb dropped at a switch) */
	const char *stuck;   /* on failure, write the frames to the closest state there */
	U8 *stage;           /* on failure, if not NULL: the frames to the closest safe state */
	int *stage_n;        /* (on the ground, alive STAGE_IDLE frames later), or -1 */
} hl2_solveopt_t;

extern void hl2_solveDefaults(hl2_solveopt_t *);

/*
 * All of these start from the current state, a frame boundary, and put it back
 * when they return -- except hl2_solveReplay, which leaves the game where the
 * replay ended.
 */
extern int hl2_solveRoute(void);  /* the exit (index) on the shortest submap path to the map's end, or -1 */
extern void hl2_solveBlock(int sub, int exit, int row);
extern void hl2_solveArrived(int sub, int row);  /* the chain entered submap sub, feet on row */  /* the route leaves this exit out, from near row */
extern void hl2_solveReserve(int *bombs, int *laser);  /* ammo the search leaves for switches */
extern int hl2_solveRouteCost(void);  /* that path's length (field steps) at the last hl2_solveRoute, -1: none */
extern int hl2_solve(const hl2_solveopt_t *, U8 *seq, int max);   /* frames, or -1 */
extern int hl2_solvePolish(const hl2_solveopt_t *, U8 *seq, int n);  /* new length */
extern int hl2_solveReplay(const hl2_solveopt_t *, const U8 *seq, int n);  /* frames to the goal, or -1 */
extern int hl2_solveDistance(const hl2_solveopt_t *);  /* Rick's tile distance to the goal now */

#endif

/* eof */
