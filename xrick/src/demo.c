/*
 * xrick/src/demo.c
 *
 * Demo mode, shared by both games -- see demo.h
 *
 * Replays a scripted sequence of control events into the game engine, timed per
 * segment (RD1: submap, RD2: map), and records one. The game adapters are
 * src/rd1/game.c (demo_cycle drives control_status) and src/rd2/rd2_demo.c
 * (demo_play / demo_record drive the joystick byte).
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
static const demoset_t *set = NULL;  /* the running game's scripts */
static U8 recording = FALSE;
static demoscript_t *script = NULL;  /* current segment's script, NULL when none */
static U16 tick = 0;                 /* logic steps since segment entry */
static U16 cursor = 0;               /* next event to apply within <script> */
static U8 mask = 0;                  /* control bits the demo currently holds */
static U16 segment = 0;              /* segment being played back or recorded */
static U8 last = 0;                  /* ctrl of the last event applied by demo_play, 0 if none */
static U8 drove = FALSE;             /* demo_cycle wrote control_status */

/*
 * recorder storage: one flat pool, one block per segment. re-entering a segment
 * (RD1: after a death, or on a revisit; RD2: a new game on that map) opens a new
 * block, so the last take wins and the earlier one is simply orphaned.
 */
#define DEMO_POOL 0x8000
static demoevt_t pool[DEMO_POOL];
static U16 pool_used = 0;
static U8 pool_full = FALSE;
static U8 rec_open = FALSE;          /* a segment is being recorded */
static U16 rec_first[DEMO_MAXSEG];
static U16 rec_nbr[DEMO_MAXSEG];
static U16 rec_len[DEMO_MAXSEG];     /* ticks recorded in the segment's last take */

/*
 * prototypes
 */
static void emit(U8, U8);
static char *ctrlName(U8);


/*
 * demo_init
 *
 * process the -demo / -record arguments for the game described by <s>. must run
 * after sysarg_init.
 */
void
demo_init(const demoset_t *s)
{
  U16 i;

  set = s;
  if (set->nbr > DEMO_MAXSEG)  /* cannot happen -- both games are below it */
  {
    sys_printf("xrick/demo: %u segments, more than %u, demo disabled\n",
	       (unsigned int)set->nbr, (unsigned int)DEMO_MAXSEG);
    set = NULL;
    return;
  }

  if (sysarg_args_record)
  {
    recording = TRUE;
    for (i = 0; i < DEMO_MAXSEG; i++)
    {
      rec_first[i] = 0;
      rec_nbr[i] = 0;
      rec_len[i] = 0;
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
 * demo_enterSegment
 *
 * called wherever the game (re)enters a segment -- RD1: next to every map_init,
 * RD2: at level start. resets the clock and selects the script. entering a segment
 * that has no script ends the demo and hands the controls back to the keyboard.
 */
void
demo_enterSegment(U16 sm)
{
  if (!set)
    return;

  if (recording && rec_open)
    rec_len[segment] = tick;  /* close the previous take */

  tick = 0;
  cursor = 0;
  mask = 0;
  script = NULL;

  if (sm >= set->nbr)  /* RD1: cannot happen -- map_submaps is indexed the same way */
  {
    rec_open = FALSE;
    demo_end();
    return;
  }

  segment = sm;

  if (recording)
  {
    rec_first[sm] = pool_used;  /* last take wins */
    rec_nbr[sm] = 0;
    rec_len[sm] = 0;
    rec_open = TRUE;
    return;
  }

  if (!demo_active)
    return;

  if (set->scripts[sm].nbr == 0)  /* past the last authored segment */
  {
    demo_end();
    return;
  }

  script = &set->scripts[sm];
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

  if (drove)
    control_status &= (U8)~mask;  /* drop whatever key the script was holding */
  mask = 0;
  script = NULL;
  demo_active = FALSE;

  sys_printf("xrick/demo: end of demo at %s %#04x, keyboard control restored\n",
	     set ? set->segname : "segment", (unsigned int)segment);
}


/*
 * demo_playing
 *
 * TRUE while a script is driving the controls.
 */
U8
demo_playing(void)
{
  return (U8)(demo_active && script);
}


/*
 * demo_recording
 */
U8
demo_recording(void)
{
  return recording;
}


/*
 * demo_cycle
 *
 * RD1: one entity logic step's worth of input. called at the top of CTRL_ACTION,
 * so the injected mask is what ent_action sees this step.
 */
void
demo_cycle(void)
{
  if (recording)
    demo_record((U8)(control_status & DEMO_CTRLBITS));
  else if (demo_active)
  {
    demo_play();
    drove = TRUE;

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
  }
}


/*
 * demo_play
 *
 * apply every event due at this tick and return the control bits the script
 * holds. running out of events holds the mask.
 */
U8
demo_play(void)
{
  demoevt_t *e;

  last = 0;
  while (script && cursor < script->nbr && script->evts[cursor].tick <= tick)
  {
    e = &script->evts[cursor++];
    if (e->down)
      mask = (U8)(mask | e->ctrl);
    else
      mask = (U8)(mask & ~e->ctrl);
    last = e->ctrl;
  }

  tick++;
  return mask;
}


/*
 * demo_record
 *
 * diff the controls <now> (CONTROL_* bits) against the previous tick and log what
 * changed.
 */
void
demo_record(U8 now)
{
  U8 diff;
  U16 b;

  if (!rec_open)
    return;

  now = (U8)(now & DEMO_CTRLBITS);
  diff = (U8)(now ^ mask);
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
 * demo_ticks
 *
 * expand the last take of segment <sm> into one CONTROL_* mask per tick, at most
 * <max> of them into <buf>. returns the number of ticks written.
 */
U16
demo_ticks(U16 sm, U8 *buf, U16 max)
{
  U16 n, t, i;
  U8 m = 0;
  demoevt_t *e;

  if (!set || sm >= set->nbr)
    return 0;
  n = (recording && rec_open && sm == segment) ? tick : rec_len[sm];
  if (n > max)
    n = max;
  i = 0;
  for (t = 0; t < n; t++)
  {
    while (i < rec_nbr[sm] && pool[rec_first[sm] + i].tick <= t)
    {
      e = &pool[rec_first[sm] + i++];
      m = e->down ? (U8)(m | e->ctrl) : (U8)(m & ~e->ctrl);
    }
    buf[t] = m;
  }
  return n;
}


/*
 * emit
 *
 * append one event to the current segment's block.
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
  rec_nbr[segment]++;
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
 * write the whole generated script file -- every segment row, recorded or empty
 * -- so the result drops straight into src/ with no splicing, then run the game's
 * extra export. registered with atexit by demo_init.
 */
void
demo_save(void)
{
  FILE *f;
  U16 s, i;
  demoevt_t *e;

  if (!recording || !set)
    return;

  f = fopen(sysarg_args_record, "w");
  if (!f)
  {
    sys_printf("xrick/demo: could not write '%s'\n", sysarg_args_record);
    return;
  }

  fprintf(f,
	  "/*\n"
	  " * %s\n"
	  " *\n"
	  " * Demo mode scripts -- see ../../include/demo.h\n"
	  " *\n"
	  " * GENERATED by `xrick -record`. One entry per %s, indexed by %s;\n"
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
	  "%s"
	  "\n",
	  set->file, set->segname, set->index, set->includes);

  for (s = 0; s < set->nbr; s++)
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

  fprintf(f, "demoscript_t %s[%s] = {\n", set->array, set->size);
  for (s = 0; s < set->nbr; s++)
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

  if (set->saved)
    set->saved();
}

#endif /* ENABLE_DEMO */

/* eof */
