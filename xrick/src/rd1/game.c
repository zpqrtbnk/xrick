/*
 * xrick/src/game.c
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

#include <stdlib.h>
#include <stdio.h>

#include "system.h"
#include "sysarg.h"
#include "sysvid.h"
#include "sysevt.h"
#include "env.h"

#include "game.h"

#include "draw.h"
#include "maps.h"
#include "ents.h"
#include "sounds.h"
#include "e_rick.h"
#include "e_sbonus.h"
#include "e_them.h"
#include "screens.h"
#include "rects.h"
#include "scroller.h"
#include "control.h"
#include "fb.h"
#include "tiles.h"
#include "draw.h"

#ifdef EMSCRIPTEN
#include "emscripten.h"
#endif

#ifdef ENABLE_DEVTOOLS
#include "devtools.h"
#endif

#ifdef ENABLE_DEMO
#include "demo.h"

/* the RD1 demo scripts: one per submap, written back as src/rd1/dat_demo.c */
static const demoset_t demoset = {
  demo_scripts, MAP_NBR_SUBMAPS, "xrick/src/rd1/dat_demo.c", "submap", "env_submap",
  "#include \"maps.h\"\n", "demo_scripts", "MAP_NBR_SUBMAPS", NULL, e_them_rndreset
};
#endif


/*
 * local typedefs
 */
typedef enum {
#ifdef ENABLE_DEVTOOLS
  DEVTOOLS,
#endif
  XRICK, XRICK_CLR,
  MAIN_INTRO, MAP_INTRO,
  INIT,
  INIT_MAP, INIT_SUBMAP,
  FADEIN__CTRL_ACTION, FADEOUT__MAP_INTRO, FADEOUT__GAMEOVER,
  PAUSE_PRESSED1, PAUSE_PRESSED1B, PAUSED, PAUSE_PRESSED2,
  CTRL_ACTION, CTRL_PAUSE, CTRL_RICK, PAINT, CTRL_SCROLL,
  NEXT_SUBMAP, NEXT_MAP,
  SCROLL_UP, SCROLL_DOWN,
  RESTART, GAMEOVER, GETNAME, EXIT
} game_state_t;


/*
 * global vars
 */
U8 game_period = 0;
U8 game_waitevt = FALSE;
rect_t *game_rects = NULL;

U8 game_dir = 0;

#ifdef GFXST
hscore_t game_hscores[8] = {
  { 8000, "SIMES@@@@@" },
  { 7000, "JAYNE@@@@@" },
  { 6000, "DANGERSTU@" },
  { 5000, "KEN@@@@@@@" },
  { 4000, "ROB@N@BOB@" },
  { 3000, "TELLY@@@@@" },
  { 2000, "NOBBY@@@@@" },
  { 1000, "JEZEBEL@@@" }
};
#endif
#ifdef GFXPC
hscore_t game_hscores[8] = {
  { 8000, "DANGERSTU@" },
  { 7000, "SIMES@@@@@" },
  { 6000, "KEN@T@ZEN@" },
  { 5000, "BOBBLE@@@@" },
  { 4000, "GREG@LAA@@" },
  { 3000, "TELLY@@@@@" },
  { 2000, "CHIGLET@@@" },
  { 1000, "ANDYSPLEEN" }
};
#endif


/*
 * local vars
 */
static U8 save_map_row;
static game_state_t game_state;
static U32 tm, tmx;


/*
 * prototypes
 */
static void game_cycle(void);
static void init(void);
static void restart(void);
static void loadData(void);
static void freeData(void);
static void game_paintEntities();
static void game_save(void);
#ifdef HEADLESS
#include "headless/hl_state.h"
static U32 hl_steps = 0;  /* CTRL_ACTION passes since game_hlStart */
#endif
#ifdef ENABLE_DEMO
static void game_enterSegment(void);
static void trace_open(void);
static void trace_tick(void);
#endif


/*
 * game_toggleCheat
 *
 * toggles one of the three cheat options
 * FIXME weird dependencies here! + _state exclusion is not complete
 */
void game_toggleCheat(U8 nbr)
{
#ifdef ENABLE_CHEATS
	if (game_state != MAIN_INTRO && game_state != MAP_INTRO &&
		game_state != GAMEOVER && game_state != GETNAME &&
#ifdef ENABLE_DEVTOOLS
		game_state != DEVTOOLS &&
#endif
		game_state != XRICK && game_state != EXIT)
	{
		switch (nbr)
		{
			case 1:
				env_trainer = ~env_trainer;
				env_lives = 6;
				env_bombs = 6;
				env_bullets = 6;
				break;

			case 2:
				env_invicible = ~env_invicible;
				break;

			case 3:
				env_highlight = ~env_highlight;
			break;
		}

		env_paintXtra(); /* fixme -- shouldn't this be done elswhere? */

		/* FIXME this should probably only raise a flag ... */
		/* plus we only need to update INFORECT not the whole screen */
		sysvid_update(&draw_SCREENRECT);
	}
#endif
}

/* prototype */
static void game_loop(void);
static void game_exit(void);


/*
 * game_run
 *
 * main loop.
 */
void
game_run(void)
{
	loadData(); /* load cached data */

	game_period = sysarg_args_period ? sysarg_args_period : GAME_PERIOD;
	tm = sys_gettime();
	game_state = XRICK;

#ifdef ENABLE_DEMO
	demo_init(&demoset);
	trace_open();
#endif

	/* main loop */
#ifdef EMSCRIPTEN
	// callback, fps, simulate_infinite_loop
	//
	// "If called on the main browser thread, setting 0 or a negative value as the fps will
	// use the browser�s requestAnimationFrame mechanism to call the main loop function."
	// "This is HIGHLY recommended if you are doing rendering, as the browser�s
	// requestAnimationFrame will make sure you render at a proper smooth rate that lines
	// up properly with the browser and monitor."
	//
	// if fps == -1 then it uses the browser requestAnimatedFrame() period - what if I want
	// to be slower? is it better to pass a fps here, or to just do nothing (NOT wait!) in
	// game_loop?
	// 
	int fps = (24 * GAME_PERIOD) / game_period;
	emscripten_set_main_loop(game_loop, fps, 1);
#else
	while (game_state != EXIT)
	{
		game_loop();
	}
#endif

	game_exit();
}

static void game_exit(void)
{
	freeData(); /* free cached data */
}

static void game_loop(void)
{
	/* timer */
#ifdef EMSCRIPTEN
	// nothing - emscripten should invoke the loop every game_period
	// and we should not sys_sleep in emscripten apps
	// (see game_run above)
#else
	// sys_gettime() and sys_sleep() use milliseconds
	tmx = tm; tm = sys_gettime(); tmx = tm - tmx;
	if (tmx < game_period) sys_sleep(game_period - tmx);
#endif

	/* video */
	/*DEBUG*//*game_rects=&draw_SCREENRECT;*//*DEBUG*/
	// FIXME:??
	//sysvid_update(fb_updatedRects);
	sysvid_update(game_rects);
	draw_STATUSRECT.next = NULL;  /* FIXME freerects should handle this */

	/* sound: nothing to do here, everything is managed via callbacks */

	/* events */
	if (game_waitevt)
		sysevt_wait();  /* wait for an event, stop doing anything */
	else
		sysevt_poll();  /* process events (non-blocking) */

	/*
	 * game_cycle: depending on the game state
	 * - process events
	 * - run the game logic, AI, ...
	 * - paints a new frame onto the frame buffer
	 * - updates fb_updatedRects
	 */
	game_cycle();

#ifdef EMSCRIPTEN
	if (game_state == EXIT)
	{
		game_exit();
		sys_shutdown();
		emscripten_cancel_main_loop();
	}
#endif
}


//static game_state_t game_state2;

/*
 * game_cycle
 *
 * This function loops forever: use 'return' when a frame is ready.
 * When returning, game_rects must contain every parts of the buffer
 * that have been modified.
 */
static void game_cycle(void)
{
	while (1) {

		//if (game_state != game_state2)
		//{
		//	sys_printf("xrick/game: state = %d", (U8) game_state);
		//	game_state2 = game_state;
		//}

		switch (game_state) {



#ifdef ENABLE_DEVTOOLS
		case DEVTOOLS:

			switch (devtools_run()) {
			case SCREEN_RUNNING:
				return;
			case SCREEN_DONE:
				game_state = INIT_GAME;
				break;
			case SCREEN_EXIT:
				game_state = EXIT;
				return;
			}
		break;
#endif


		case XRICK:

			switch(screen_xrick())
			{
				case SCREEN_RUNNING:
					return;
				case SCREEN_DONE:
					game_state = XRICK_CLR;
					return;
				case SCREEN_EXIT:
					game_state = EXIT;
					return;
			}
		break;



		case XRICK_CLR:

			/* this step is required to force a screen update (clear) before changing the palette */
			fb_initPalette();
#ifdef ENABLE_DEVTOOLS
			game_state = DEVTOOLS;
#else
			game_state = MAIN_INTRO;
#endif
			break;



		case MAIN_INTRO:

			switch (screen_introMain())
			{
				case SCREEN_RUNNING:
					return;
				case SCREEN_DONE:
					game_state = INIT;
					break;
				case SCREEN_EXIT:
					game_state = EXIT;
					return;
			}
			break;



		case INIT:

			init();
			if (env_submap == map_maps[env_map].submap)
			{
				game_state = MAP_INTRO;
			}
			else
			{
				game_state = INIT_MAP; /* no intro if not first submap */
			}
			break;



		case MAP_INTRO:

#ifdef HEADLESS
			/* the map intro only draws and waits for FIRE; it writes no game state */
			game_state = INIT_MAP;
			break;
#endif
			switch (screen_introMap())
			{
				case SCREEN_RUNNING:
					return;
				case SCREEN_DONE:
					game_waitevt = FALSE;
					game_state = INIT_MAP;
					break;
				case SCREEN_EXIT:
					game_state = EXIT;
					return;
			}
			break;



		case INIT_MAP:

			if (env_map >= 0x04) /* reached end of game */
			{
				/*
				 * End-of-game bonus for unspent lives. Both originals award it at
				 * exactly this point -- once the map counter has reached 4, before the
				 * completion transition:
				 *
				 *   ST 0x49AD2  cmpi.w #4,level_index / blt / moveq #0,d1 /
				 *               move.b lives,d1 / L: move.l #$00100000,d0 /
				 *               bsr add_score / dbra d1,L
				 *   PC 0x0DD8   inc [0x7D90] / cmp al,4 / jnz / mov al,[0x7E46] /
				 *               inc al / mov [0x4916],al / mov si,0x4916 / call 0x0292
				 *
				 * Same mechanism, tenfold different value. `dbra` runs the ST body
				 * lives+1 times at 100000 a turn (its delta byte 0x10 is packed BCD
				 * landing on the 10^5 nibble). The PC instead writes lives+1 into ONE
				 * digit of the 5-digit delta buffer at 0x4916; its adder does
				 * `add si,4` then walks down against score digits 0x4913..0x490F, so
				 * buffer[0] pairs with 0x490F -- the 10^4 place, not 10^5. The rest of
				 * that buffer (0x4917..0x491A) is referenced by no instruction in the
				 * segment, so it is static zero. Hence the PC awards a tenth.
				 *
				 * env_lives never exceeds 6 (set at game.c:144/749, only ever
				 * decremented at game.c:524 -- neither the port nor either original
				 * awards extra lives), so lives+1 <= 7 and the PC's single digit
				 * cannot overflow.
				 *
				 * Written as one add rather than the ST's loop: env_addscore() wraps
				 * mod 1000000 on every call, and repeated addition mod m equals the
				 * summed product mod m, so the two are exactly equivalent.
				 */
#ifdef PLATFORM_ST
				env_addscore((U32)(env_lives + 1) * 100000);
#else
				env_addscore((U32)(env_lives + 1) * 10000);
#endif
				sysarg_args_map = 0; // FIXME game completed, start all over. fine, but... ack...
				sysarg_args_submap = 0;
				game_state = FADEOUT__GAMEOVER;
			}
			else
			{
				map_init();
#ifdef ENABLE_DEMO
				game_enterSegment();
#endif
				game_save();
				fb_clear();                 /* clear buffer */
				//ent_clprev();
				maps_paint();                     /* draw the map onto the buffer */
				//ents_paintAll();
				env_paintGame();              /* draw the status bar onto the buffer */
				env_paintXtra();                   /* draw the info bar onto the buffer */
				game_rects = &draw_SCREENRECT;  /* request full buffer refresh */
				game_state = FADEIN__CTRL_ACTION;
			}
			break;



		case FADEIN__CTRL_ACTION:

#ifdef HEADLESS
			/*
			 * fades only set the gamma, but their counters are statics inside
			 * fb.c that a snapshot cannot hold: skip them (kb/demo-solver.md §8)
			 */
			game_state = CTRL_ACTION;
			break;
#endif
			if (fb_fadeIn())
			{
				game_state = CTRL_ACTION;
			}
			return;



		case PAUSE_PRESSED1:

			screen_pause(TRUE);
			game_state = PAUSE_PRESSED1B;
			break;



		case PAUSE_PRESSED1B:

			if (control_status & CONTROL_PAUSE)
				return;
			game_state = PAUSED;
			break;



		case PAUSED:

			if (control_status & CONTROL_PAUSE)
			{
				game_state = PAUSE_PRESSED2;
			}
			if (control_status & CONTROL_EXIT)
			{
				game_state = EXIT;
			}
			return;



		case PAUSE_PRESSED2:

			if (!(control_status & CONTROL_PAUSE)) 
			{
				game_waitevt = FALSE;
				screen_pause(FALSE);
#ifdef ENABLE_SOUND
				syssnd_pause(FALSE, FALSE);
#endif
				game_state = CTRL_RICK;
			}
		return;



		case CTRL_ACTION:

#ifdef ENABLE_DEMO
			demo_cycle(); /* feed (or capture) the controls for this logic step */
#endif
			if (control_status & CONTROL_END) /* request to end the game */
			{
				game_state = FADEOUT__GAMEOVER;
			}
			else
			if (control_last == CONTROL_EXIT) /* request to exit the game */
			{
				game_state = EXIT;
			}
			else
			{
				ent_action();      /* run entities */
#ifdef HEADLESS
				hl_steps++;
#endif
				e_them_rndseed++;  /* (0270) */
#ifdef PLATFORM_ST
				/*
				 * ST: the main loop's once-per-frame update_prng, `bsr` at 0x4DD3A
				 * in RENDER, after render_sprites ran the entity handlers. RENDER is
				 * skipped on scroll frames and on the respawn frame, as CTRL_ACTION is
				 * here. Was missing -- kb/demo-solver.md F1.
				 */
				e_them_rndstep();
#endif
#ifdef ENABLE_DEMO
				trace_tick();
#endif
				game_state = CTRL_PAUSE;
			}
			break;



		case CTRL_PAUSE:

			if (control_status & CONTROL_PAUSE)
			{
#ifdef ENABLE_SOUND
				syssnd_pause(TRUE, FALSE);
#endif
				game_waitevt = TRUE;
				game_state = PAUSE_PRESSED1;
			}
			else
			if (control_active == FALSE)
			{
#ifdef ENABLE_SOUND
				syssnd_pause(TRUE, FALSE);
#endif
				game_waitevt = TRUE;
				screen_pause(TRUE);
				game_state = PAUSED;
			}
			else
			{
				game_state = CTRL_RICK;
			}
			break;



		case CTRL_RICK:

			// FIXME if (e_rick_isDead)
			if E_RICK_STTST(E_RICK_STDEAD) /* rick is dead */
			{
				if (env_trainer || --env_lives)
				{
					game_state = RESTART;
				}
				else
				{
					game_state = FADEOUT__GAMEOVER;
				}
			}
			else 
			if (e_rick_atExit) /* rick is exiting the submap, must chain to next submap */
			{
				//	e_rick_enterMap(); // akn
				e_rick_atExit = FALSE;
				game_state = NEXT_SUBMAP;
			}
			else
			{
				game_state = PAINT;
			}
			break;



		case PAINT:

			game_paintEntities();
			game_state = CTRL_SCROLL;
			return;



		case CTRL_SCROLL:
			if (!E_RICK_STTST(E_RICK_STZOMBIE))
			{
				if (ent_ents[1].y >= 0xcc)
				{
					game_state = SCROLL_UP;
				}
				else
				/*
				 * PORT DEFECT, fixed -- review-log.md G1 / defect #19. Was `<= 0x60`
				 * on the PC branch, and platform-switched. It is NOT a platform
				 * difference: the two originals express the same test differently.
				 *   PC 0x018B  cmp al,0x60 / jnc skip   -> scrolls when y <  0x60
				 *   ST 0x4DD0E cmpi.w #0x5f / bgt skip  -> scrolls when y <= 0x5F
				 * Those are the same condition. The port's `<= 0x60` scrolled one row
				 * early, at exactly y == 0x60, where neither original does.
				 * The high threshold needs no switch either: PC 0x017E `cmp al,0xcc /
				 * jc` and ST 0x4DD1E `cmpi.w #0xcc / blt` both scroll up at y >= 0xCC.
				 */
				if (ent_ents[1].y <= 0x5f)
				{
					game_state = SCROLL_DOWN;
				}
				else
				{
					game_state = CTRL_ACTION;
				}
			}
			else
			{
				game_state = CTRL_ACTION;
			}
			break;



		case NEXT_SUBMAP:

			if (map_chain())
			{
				/* next submap, now initialize */
				game_state = INIT_SUBMAP;
			}
			else
			{
				/* end of submap, chain to next map */

				env_bullets = 0x06;
				env_bombs = 0x06;
				env_map++;

				if (env_map == 0x04)
				{
					/* reached end of game */
					/* FIXME @292?*/
				}

				game_state = NEXT_MAP;
			}
			break;



		case NEXT_MAP:

			ent_ents[1].x = map_maps[env_map].x;
			ent_ents[1].y = map_maps[env_map].y;
			map_frow = (U8)map_maps[env_map].row;
			env_submap = map_maps[env_map].submap;
			game_state = FADEOUT__MAP_INTRO;
			break;



		case FADEOUT__MAP_INTRO:

#ifdef HEADLESS
			game_state = MAP_INTRO;  /* no fade, see FADEIN__CTRL_ACTION */
			break;
#endif
			if (fb_fadeOut())
			{
				game_state = MAP_INTRO;
			}
			return;



		case INIT_SUBMAP:

			map_init();                     /* initialize the map */
#ifdef ENABLE_DEMO
			game_enterSegment();
#endif
			game_save();                        /* save data in case of a restart */
			fb_clear();
			ent_clprev();                   /* cleanup entities */
			maps_paint();                     /* draw the map onto the buffer */
			ents_paintAll();
			env_paintGame();              /* draw the status bar onto the buffer */
			env_paintXtra();
			game_rects = &draw_SCREENRECT;  /* request full screen refresh */
			game_state = CTRL_ACTION;
			return;



		case SCROLL_UP:

			switch (scroll_up())
			{
				case SCROLL_RUNNING:
					return;
				case SCROLL_DONE:
					game_state = CTRL_ACTION;
					break;
			}
			break;



		case SCROLL_DOWN:

			switch (scroll_down())
			{
				case SCROLL_RUNNING:
					return;
				case SCROLL_DONE:
					game_state = CTRL_ACTION;
					break;
			}
			break;



		case RESTART:

			restart();
			game_state = CTRL_ACTION;
			return;



		case FADEOUT__GAMEOVER:

#ifdef ENABLE_DEMO
			/* the demo is over -- the hall of fame is entered by hand. idempotent. */
			demo_end();
#endif
#ifdef HEADLESS
			return;  /* the game is over: stay here, game_hlStep reports it */
#endif
			if (fb_fadeOut())
				game_state = GAMEOVER;
			return;



		case GAMEOVER:

			switch (screen_gameover())
			{
				case SCREEN_RUNNING:
					return;
				case SCREEN_DONE:
					game_state = GETNAME;
					break;
				case SCREEN_EXIT:
					game_state = EXIT;
					break;
			}
			break;



		case GETNAME:

			switch (screen_getname())
			{
				case SCREEN_RUNNING:
					return;
				case SCREEN_DONE:
					game_state = XRICK_CLR;
					return;
				case SCREEN_EXIT:
					game_state = EXIT;
					break;
			}
			break;



		case EXIT:
			return;
    }
  }
}



/*
 * init
 *
 * FIXME some dirty hacks here, plus we should not manage sysargs_ this way
 */
static void
init(void)
{
  U8 i;

  E_RICK_STRST(0xff);

  env_lives = 6;
  env_bombs = 6;
  env_bullets = 6;
  env_score = 0;

  env_map = sysarg_args_map;

  if (sysarg_args_submap == 0) {
    env_submap = map_maps[env_map].submap;
    map_frow = (U8)map_maps[env_map].row;
  }
  else {
    /* dirty hack to determine frow by chaining submaps...*/
    env_submap = sysarg_args_submap;
    i = 0;
    while (i < 4 && map_maps[i++].submap <= env_submap);
    env_map = i - 1;
    i = 0;
    while (i < MAP_NBR_CONNECT &&
	   (map_connect[i].submap != env_submap ||
	    map_connect[i].dir != RIGHT))
      i++;
    map_frow = map_connect[i].rowin - 0x10; // WHY 0x10??
    ent_ents[1].y = 0x10 << 3; // FIXME?
  }

  ent_ents[1].x = map_maps[env_map].x;
  ent_ents[1].y = map_maps[env_map].y;
  ent_ents[1].w = 0x18;
  ent_ents[1].h = 0x15;
  ent_ents[1].n = 0x01;
  ent_ents[1].sprite = 0x01;
  ent_ents[1].front = FALSE;
  ent_ents[ENT_ENTSNUM].n = 0xFF;

  map_resetMarks();
}



/*
 * game_paintEntities
 *
 * paints the entities.
 */
static void game_paintEntities()
{
	static rect_t *r;

	env_clearGame();  /* clear the status bar */
	ents_paintAll();  /* draw all entities onto the buffer */
	env_paintGame();  /* draw the status bar onto the buffer*/

	/* fixme: rectangle management!!*/
	// should just do: fb_touchRect(env_GameRect)
	r = &draw_STATUSRECT; r->next = ent_rects;  /* refresh status bar too */
	game_rects = r;   /* take care to cleanup draw_STATUSRECT->next later! */
}



/*
 * restart
 *
 * restarts the game after rick died. just come back to the beginning
 * of the current submap, restore positions and flags and...
 */
static void restart(void)
{
	E_RICK_STRST(E_RICK_STDEAD|E_RICK_STZOMBIE); // should be part of e_rick

	env_bullets = 6;
	env_bombs = 6;

	ent_ents[1].n = 1; // FIXMEwhy??

	e_rick_restore(); // FIXME that should restore the state ?!!?
	map_frow = save_map_row;

	map_init(); // see INIT_MAP check that everything is OK here
#ifdef ENABLE_DEMO
	game_enterSegment(); /* a death replays the submap script from tick 0 */
#endif
	game_save();
	ent_clprev();
	maps_paint();
	env_paintGame(); // and Xtra???
	game_rects = &draw_SCREENRECT; //fb_touchFb();
}



/*
 * game_save
 *
 * save game state so it can be restored when rick dies, by <restart>.
 * it is NOT a "save game" option!
 */
static void game_save(void)
{
  e_rick_save();
  save_map_row = map_frow;
}



#ifdef ENABLE_DEMO
/*
 * trace (-trace <file>): one line per logic step and one per segment entry,
 * so two runs -- e.g. the SDL build and the headless core, PLAN.md T43 -- can
 * be diffed tick by tick.
 *
 *   E <step> <submap> <rng a> <rng b>
 *   T <step> <submap> <ctrl> <rng a> <rng b> <lives> <bombs> <bullets> <score>
 *     <rick state> then n:x,y,sprite for every entity slot
 *
 * <step> counts CTRL_ACTION passes since the game started. rng is st_rnd_a/b on
 * the ST, e_them_rndseed/rndnbr on the PC.
 */
static FILE *trace_f = NULL;
static U32 trace_n = 0;

static void
trace_open(void)
{
  if (!sysarg_args_trace)
    return;
  trace_f = fopen(sysarg_args_trace, "w");
  if (!trace_f)
    sys_printf("xrick/trace: could not write '%s'\n", sysarg_args_trace);
}

static void
trace_tick(void)
{
  U32 a, b;
  U8 i;

  if (!trace_f)
    return;
  e_them_rndstate(&a, &b);
  fprintf(trace_f, "T %lu %02x %02x %08lx %08lx %u %u %u %lu %02x",
	  (unsigned long)trace_n, (unsigned int)env_submap,
	  (unsigned int)(control_status & (CONTROL_UP|CONTROL_DOWN|CONTROL_LEFT|
					   CONTROL_RIGHT|CONTROL_FIRE)),
	  (unsigned long)a, (unsigned long)b,
	  (unsigned int)env_lives, (unsigned int)env_bombs,
	  (unsigned int)env_bullets, (unsigned long)env_score,
	  (unsigned int)e_rick_state);
  for (i = 0; i < ENT_ENTSNUM; i++)
    fprintf(trace_f, " %02x:%d,%d,%02x", (unsigned int)ent_ents[i].n,
	    (int)ent_ents[i].x, (int)ent_ents[i].y, (unsigned int)ent_ents[i].sprite);
  fputc('\n', trace_f);
  trace_n++;
}

/*
 * every (re)entry of a submap: the demo clock and, while a script plays or
 * records, the random generator restart here (demo.c, T43 D1).
 */
static void
game_enterSegment(void)
{
  U32 a, b;

  demo_enterSegment(env_submap);
  if (trace_f)
  {
    e_them_rndstate(&a, &b);
    fprintf(trace_f, "E %lu %02x %08lx %08lx\n", (unsigned long)trace_n,
	    (unsigned int)env_submap, (unsigned long)a, (unsigned long)b);
  }
}
#endif



/*
 * loadData
 *
 * loads data into cache.
 */
static void loadData()
{
#ifdef ENABLE_SOUND
	sounds_load();
#endif
}



/*
 * freeData
 *
 * free cached data
 */
static void freeData()
{
#ifdef ENABLE_SOUND
	sounds_free();
#endif
}



#ifdef HEADLESS
/*
 * game_hlStart
 *
 * start a new game from sysarg_args_map/submap, straight into INIT: the title
 * and hall of fame screens write no game state. -demo / -trace apply as in
 * game_run.
 */
void
game_hlStart(void)
{
#ifdef ENABLE_DEMO
	demo_init(&demoset);
	trace_open();
#endif
	hl_steps = 0;
	game_state = INIT;
}

/*
 * game_hlStep
 *
 * hold <ctrl> (CONTROL_* bits) and run frames until one CTRL_ACTION pass has
 * run and its frame is painted. scroll, fade and restart frames in between are
 * run too: they are not logic steps (the demo clock does not count them either).
 * under -demo the script overrides <ctrl>, as it overrides the keyboard.
 */
U8
game_hlStep(U8 ctrl)
{
	U32 n = hl_steps;

	control_status = ctrl;
	while (hl_steps == n)
	{
		if (game_state == FADEOUT__GAMEOVER || game_state == GAMEOVER ||
		    game_state == GETNAME || game_state == EXIT)
			return env_map >= 0x04 ? GAME_HL_END : GAME_HL_OVER;
		game_cycle();
	}
	return GAME_HL_STEP;
}

U32
game_hlSteps(void)
{
	return hl_steps;
}

/*
 * this file's statics that are game state, for snapshots -- kb/demo-solver.md §3.
 * tm/tmx and game_period are timing only.
 */
void
game_hlRegions(hl_region_f f)
{
	f(&game_state, sizeof(game_state), "game_state");
	f(&save_map_row, sizeof(save_map_row), "save_map_row");
	f(&hl_steps, sizeof(hl_steps), "hl_steps");
}
#endif



/* eof */
