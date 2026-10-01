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

typedef struct {
	int beam;       /* states kept per frame count */
	int maxsteps;   /* give up past this many frames */
	int exit;       /* index in the submap's exit list (hl2_exits); -1: hl2_solveRoute's choice */
	int verbose;    /* progress on stderr */
	int minbombs;   /* bombs to hold at the exit */
	int minlaser;   /* laser shots to hold at the exit */
	int wp_row, wp_col;  /* >= 0: a waypoint (feet row, column) instead of the exit */
	const char *stuck;   /* on failure, write the frames to the closest state there */
} hl2_solveopt_t;

extern void hl2_solveDefaults(hl2_solveopt_t *);

/*
 * All of these start from the current state, a frame boundary, and put it back
 * when they return -- except hl2_solveReplay, which leaves the game where the
 * replay ended.
 */
extern int hl2_solveRoute(void);  /* the exit (index) on the shortest submap path to the map's end, or -1 */
extern int hl2_solve(const hl2_solveopt_t *, U8 *seq, int max);   /* frames, or -1 */
extern int hl2_solvePolish(const hl2_solveopt_t *, U8 *seq, int n);  /* new length */
extern int hl2_solveReplay(const hl2_solveopt_t *, const U8 *seq, int n);  /* frames to the goal, or -1 */
extern int hl2_solveDistance(const hl2_solveopt_t *);  /* Rick's tile distance to the goal now */

#endif

/* eof */
