/*
 * xrick/src/headless/hl_solve.h
 *
 * xrick-core only (branch `solver`, never shipped): search the inputs that take
 * rick from the current state to the next submap without dying (PLAN.md T43
 * phase 6, kb/demo-solver.md §11).
 */

#ifndef _HL_SOLVE_H
#define _HL_SOLVE_H

#include "system.h"

#define HL_SOLVE_NEXTMAP (-1)  /* target: the exit to the next map */
#define HL_SOLVE_AUTO (-2)     /* target: the forward exit, see hl_solveTarget */

typedef struct {
  int beam;      /* states kept per search depth */
  int maxsteps;  /* give up past this many steps */
  int target;    /* submap to reach, HL_SOLVE_NEXTMAP or HL_SOLVE_AUTO */
  int verbose;   /* progress on stderr */
  int closures;  /* restart with the deadliest tiles closed (hl_solve), 0 = off */
  const char *stuck;  /* on failure, write the best state there (xrick-core -load) */
} hl_solveopt_t;

/*
 * All of these start from the current state, which must be a step boundary with
 * the segment's next logic step pending, and put it back when they return --
 * except hl_solveReplay, which leaves the game where the replay ended.
 */
extern int hl_solveTarget(void);  /* the forward exit's target, or HL_SOLVE_NEXTMAP */
extern int hl_solve(const hl_solveopt_t *, U8 *seq, int max);  /* steps, or -1 */
extern int hl_solvePolish(U8 *seq, int n, int target);        /* new length */
extern int hl_solveReplay(const U8 *seq, int n, int target);  /* steps to the goal, or -1 */

#endif

/* eof */
