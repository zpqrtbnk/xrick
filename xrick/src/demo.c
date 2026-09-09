/*
 * xrick/src/demo.c
 *
 * Demo (attract) mode -- see ../../demo.md
 *
 * Replays a scripted sequence of control events into the game engine, timed per
 * submap, and records one.
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

#ifdef ENABLE_DEMO

#include <stdio.h>
#include <stdlib.h>  /* atexit */

#include "demo.h"
#include "control.h"
#include "sysarg.h"
#include "maps.h"

/*
 * global vars
 */
U8 demo_active = FALSE;

/*
 * bits a script may carry. the meta keys (pause, exit, end) are left to the
 * human, so a running demo can always be paused or quit.
 */
#define DEMO_CTRLBITS (CONTROL_UP|CONTROL_DOWN|CONTROL_LEFT|CONTROL_RIGHT|CONTROL_FIRE)

/*
 * local vars
 */
static U8 recording = FALSE;
static demoscript_t *script = NULL;  /* current submap's script, NULL when none */
static U16 tick = 0;                 /* CTRL_ACTION passes since submap entry */
static U16 cursor = 0;               /* next event to apply within <script> */
static U8 mask = 0;                  /* control bits the demo currently holds */
static U16 submap = 0;               /* submap being played back or recorded */

/*
 * recorder storage: one flat pool, one block per submap. re-entering a submap
 * (after a death, or on a revisit) opens a new block, so the last take wins and
 * the earlier one is simply orphaned.
 */
#define DEMO_POOL 0x2000
static demoevt_t pool[DEMO_POOL];
static U16 pool_used = 0;
static U8 pool_full = FALSE;
static U16 rec_first[MAP_NBR_SUBMAPS];
static U16 rec_nbr[MAP_NBR_SUBMAPS];

/*
 * prototypes
 */
static void play(void);
static void record(void);
static void emit(U8, U8);
static char *ctrlName(U8);


/*
 * demo_init
 *
 * process the -demo / -record arguments. must run after sysarg_init.
 */
void
demo_init(void)
{
  U16 i;

  if (sysarg_args_record)
  {
    recording = TRUE;
    for (i = 0; i < MAP_NBR_SUBMAPS; i++)
    {
      rec_first[i] = 0;
      rec_nbr[i] = 0;
    }
    /*
     * on exit, not from game_exit: xrick.c routes SIGINT and SIGTERM to exit(),
     * and a take is worth keeping whichever way the game was left.
     */
    atexit(demo_save);

    sys_printf("xrick/demo: recording to '%s'\n", sysarg_args_record);
    if (sysarg_args_demo)
      sys_printf("xrick/demo: -record overrides -demo, playback disabled\n");
  }
  else if (sysarg_args_demo)
  {
    demo_active = TRUE;
  }
}


/*
 * demo_enterSubmap
 *
 * called wherever the engine (re)enters a submap, i.e. next to every map_init.
 * resets the clock and selects the script. entering a submap that has no script
 * ends the demo and hands the controls back to the keyboard.
 */
void
demo_enterSubmap(U16 sm)
{
  tick = 0;
  cursor = 0;
  mask = 0;
  script = NULL;

  if (sm >= MAP_NBR_SUBMAPS)  /* cannot happen -- map_submaps is indexed the same way */
  {
    demo_end();
    return;
  }

  submap = sm;

  if (recording)
  {
    rec_first[sm] = pool_used;  /* last take wins */
    rec_nbr[sm] = 0;
    return;
  }

  if (!demo_active)
    return;

  if (demo_scripts[sm].nbr == 0)  /* past the last authored submap */
  {
    demo_end();
    return;
  }

  script = &demo_scripts[sm];
}


/*
 * demo_end
 *
 * stop driving the controls and hand them back to the keyboard, in place.
 * idempotent, so it can be called from a state that runs on every frame.
 */
void
demo_end(void)
{
  if (!demo_active)
    return;

  control_status &= (U8)~mask;  /* drop whatever key the script was holding */
  mask = 0;
  script = NULL;
  demo_active = FALSE;

  sys_printf("xrick/demo: end of demo at submap %#04x, keyboard control restored\n",
	     (unsigned int)submap);
}


/*
 * demo_cycle
 *
 * one entity logic step's worth of input. called at the top of CTRL_ACTION, so
 * the injected mask is what ent_action sees this step.
 */
void
demo_cycle(void)
{
  if (recording)
    record();
  else if (demo_active)
    play();
}


/*
 * play
 *
 * apply every event due at this tick, then publish the mask.
 */
static void
play(void)
{
  U8 last = 0;
  demoevt_t *e;

  while (script && cursor < script->nbr && script->evts[cursor].tick <= tick)
  {
    e = &script->evts[cursor++];
    if (e->down)
      mask = (U8)(mask | e->ctrl);
    else
      mask = (U8)(mask & ~e->ctrl);
    last = e->ctrl;
  }

  /*
   * running out of events is normal -- a script usually stops changing keys a
   * few ticks before rick reaches the exit -- so the mask is held, not cleared.
   */
  control_status = (U8)(mask |
			(control_status & (CONTROL_EXIT|CONTROL_END|CONTROL_PAUSE)));

  /*
   * control_last only moves on an actual event, as in sysevt.c -- and never
   * over a pending exit, which game.c tests through control_last.
   */
  if (last && !(control_status & CONTROL_EXIT))
    control_last = last;

  tick++;
}


/*
 * record
 *
 * diff the controls against the previous tick and log what changed.
 */
static void
record(void)
{
  U8 now = (U8)(control_status & DEMO_CTRLBITS);
  U8 diff = (U8)(now ^ mask);
  U16 b;

  if (diff)
  {
    for (b = 0x01; b <= 0x80; b = (U16)(b << 1))
      if (diff & b)
	emit((U8)b, (U8)((now & b) ? TRUE : FALSE));
    mask = now;
  }

  tick++;
}


/*
 * emit
 *
 * append one event to the current submap's block.
 */
static void
emit(U8 ctrl, U8 down)
{
  if (pool_used >= DEMO_POOL)
  {
    if (!pool_full)
    {
      sys_printf("xrick/demo: recorder pool full, recording truncated\n");
      pool_full = TRUE;
    }
    return;
  }

  pool[pool_used].tick = tick;
  pool[pool_used].ctrl = ctrl;
  pool[pool_used].down = down;
  pool_used++;
  rec_nbr[submap]++;
}


/*
 * ctrlName
 */
static char *
ctrlName(U8 ctrl)
{
  switch (ctrl)
  {
    case CONTROL_UP: return "CONTROL_UP";
    case CONTROL_DOWN: return "CONTROL_DOWN";
    case CONTROL_LEFT: return "CONTROL_LEFT";
    case CONTROL_RIGHT: return "CONTROL_RIGHT";
    case CONTROL_FIRE: return "CONTROL_FIRE";
    default: return "0";
  }
}


/*
 * demo_save
 *
 * write the whole of dat_demo.c -- every submap row, recorded or empty -- so
 * the result drops straight into src/ with no splicing. registered with atexit
 * by demo_init.
 */
void
demo_save(void)
{
  FILE *f;
  U16 s, i;
  demoevt_t *e;

  if (!recording)
    return;

  f = fopen(sysarg_args_record, "w");
  if (!f)
  {
    sys_printf("xrick/demo: could not write '%s'\n", sysarg_args_record);
    return;
  }

  fprintf(f,
	  "/*\n"
	  " * xrick/src/dat_demo.c\n"
	  " *\n"
	  " * Demo (attract) mode scripts -- see ../../demo.md\n"
	  " *\n"
	  " * GENERATED by `xrick -record`. One entry per submap, indexed by env_submap;\n"
	  " * { 0, NULL } means \"no demo here\", which ends the demo and hands the\n"
	  " * controls back to the keyboard.\n"
	  " */\n"
	  "\n"
	  "#include \"system.h\"\n"
	  "\n"
	  "#ifdef ENABLE_DEMO\n"
	  "\n"
	  "#include \"demo.h\"\n"
	  "#include \"control.h\"\n"
	  "#include \"maps.h\"\n"
	  "\n");

  for (s = 0; s < MAP_NBR_SUBMAPS; s++)
  {
    if (rec_nbr[s] == 0)
      continue;

    fprintf(f, "static demoevt_t demo_evts_%02x[] = {\n", (unsigned int)s);
    for (i = 0; i < rec_nbr[s]; i++)
    {
      e = &pool[rec_first[s] + i];
      fprintf(f, "  { %5u, %-13s, %-5s },\n",
	      (unsigned int)e->tick, ctrlName(e->ctrl), e->down ? "TRUE" : "FALSE");
    }
    fprintf(f, "};\n\n");
  }

  fprintf(f, "demoscript_t demo_scripts[MAP_NBR_SUBMAPS] = {\n");
  for (s = 0; s < MAP_NBR_SUBMAPS; s++)
  {
    if (rec_nbr[s])
      fprintf(f, "  /* 0x%02x */ { %u, demo_evts_%02x },\n",
	      (unsigned int)s, (unsigned int)rec_nbr[s], (unsigned int)s);
    else
      fprintf(f, "  /* 0x%02x */ { 0, NULL },\n", (unsigned int)s);
  }
  fprintf(f, "};\n\n#endif /* ENABLE_DEMO */\n\n/* eof */\n");

  fclose(f);
  sys_printf("xrick/demo: wrote '%s'\n", sysarg_args_record);
}

#endif /* ENABLE_DEMO */

/* eof */
