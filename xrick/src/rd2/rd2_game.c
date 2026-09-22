/*
 * xrick/src/rd2/rd2_game.c
 *
 * Rick Dangerous 2 -- game_main, transcribed from algo-flow.md SS1-2, S6, S8.
 * Control-flow labels (TITLE/PICK/LOAD/FRAME/MAPDONE/NEXT/ENDING/END_OF_RUN) match
 * the doc's own labels, each with its address, so this stays checkable against the
 * source section directly. Kept as ONE function with real `goto`s, mirroring the
 * original's own flat label structure (rd1's port does the same for its equivalent
 * loop, `game.c`'s `game_run()`) -- splitting the labels into separate C functions
 * was tried first and abandoned: C has no way for a callee to jump back into a
 * caller's label, and papering over that with an extra recursive call was a made-up
 * workaround, not a translation of anything documented, so it was removed rather
 * than shipped.
 *
 * Every called subsystem is a stub declared in rd2_game.h and implemented by a
 * later P3/P4/P5 phase (see that header's TODO comments) -- this file implements
 * ONLY the state machine itself, so it will not LINK until those exist (expected:
 * P7 is explicitly gated on P3-P6 all being present).
 */

#include "rd2_game.h"

S16 rd2_map_playing;
S16 rd2_map_loaded;
S16 rd2_picker_choice;
U16 rd2_picker_rows;
U16 rd2_cheat_flag;
U16 rd2_long_game;
S16 rd2_demo_flag;
U8  rd2_demo_ended;
U8  rd2_input_byte;
U8  rd2_last_scancode;
S16 rd2_map_done_flag;
S16 rd2_shot_hit_flag;
S16 rd2_rick_dead;
S16 rd2_actor_group_flag;
S16 rd2_lives;
U16 rd2_lives_dirty;
S16 rd2_ammo;
U16 rd2_ammo_dirty;
S16 rd2_bombs;
U16 rd2_bombs_dirty;
U32 rd2_score_bcd;
U16 rd2_vblanks_per_frame;
U16 rd2_vblank_counter;
S16 rd2_id_remap_flag;
U16 rd2_music_playing;

/* $10b5c -- "S key already handled" latch (algo-flow.md S1/S6). Local to this file:
   nothing outside the frame-end checks reads it. */
static S16 rd2_s_key_latch;

/* [$16960] Rick's y, read by this file's death check (algo-flow.md S6: "Rick dead
   and fallen out of the playfield, y >= $100"). Owned by P3d (rd2_player.c), which
   does not exist yet -- declared here as an extern the same way every rd2_game.h
   stub is, so this file states exactly what it depends on rather than hiding it
   inside a stub call. */
extern S16 rd2_rick_y;   /* TODO P3d: [$16960] */

void
rd2_game_run(void)
{
	/* $10992: install rte at $19044, load MFP regs/vectors -- platform init, not
	   game logic; the common platform layer's own sys_init() already brings up an
	   equivalent modern audio/video/input stack (P6 wires -rd 2 into it), so
	   nothing further is done here for that step. */
	rd2_input_byte = 0;   /* clr.b [$1a4fb] */

TITLE:
	rd2_title_attract();          /* $10a18 -> $178dc */

PICK:
	rd2_level_picker();           /* $10a1e -> $17a46 */
	rd2_new_game_reset();         /* $10a24 -> $1771c: score := 0, lives := 6 */
	rd2_select_map_from_picker(); /* $10a2a -> $123a0: [$1239c] := [$17994]; $17760 */

LOAD:
	rd2_clear_screens();          /* $10a30 -> $19388 */
	rd2_load_map_if_changed();    /* $10a36 -> $123b0 */
	rd2_refill_ammo_bombs();      /* $10a3c -> $17760 */
	rd2_map_done_flag = 0;        /* $10a42 */
	rd2_clear_screens();          /* $10a42 -> $19388 (again, per the doc) */
	rd2_level_start();            /* $10a42 -> $142a0 */

FRAME:
	rd2_shot_hit_flag = 0;        /* $10a54 */
	rd2_scan_spawn_table();       /* $14594 */
	rd2_update_actor_group();     /* $15bc0 */
	rd2_update_actors();          /* $14d48 */
	rd2_update_objects();         /* $150a2 */
	rd2_update_bonus_timer();     /* $15826 */
	rd2_update_player_rick();     /* $13096 */
	rd2_update_laser_shot();      /* $13e14 */
	rd2_update_bomb();            /* $13e98 */
	if (rd2_shot_hit_flag != 0) {
		/* TODO P3d/P3f: [$16902] := 0 -- clear the laser shot record's active
		   word. The record itself (rd2_record.h's 88-byte type) is owned by P3d
		   (rd2_player.c, which allocates Rick's/the shot's/the bomb's/debris'
		   instances); this file only implements the *condition* until that
		   ownership exists. */
	}
	rd2_scroll_edge_trigger();       /* $16658 */
	rd2_animate_background_tiles();  /* $18dac */
	rd2_draw_background();           /* $18782 */
	rd2_draw_sprites();              /* $170b6 */
	rd2_draw_hud();                  /* $177a8 */
	rd2_vblank_wait_and_flip();      /* $19216 */
	rd2_vblank_wait();               /* $191e6 */
	rd2_scan_exit_triggers();        /* $14362 */

	/* ---- algo-flow.md S6, "Checks at the end of every frame" (`$10acc..`) ---- */

	if (rd2_map_done_flag != 0)
		goto MAPDONE;                 /* $10ad0 */

	if (rd2_demo_flag == 0) {
		/* TODO P3b: if key == 0x1f (S): if (rd2_s_key_latch == 0) {
		   rd2_s_key_latch = -1; rd2_stop_all_sound(); rd2_id_remap_flag ^= -1; }
		   else rd2_s_key_latch = 0. Needs the scancode test P3b's key-reading
		   code owns. */
	} else {
		rd2_s_key_latch = 0;
	}

	if (rd2_demo_flag != 0) {
		if (rd2_demo_ended != 0)
			goto END_OF_RUN;
		/* TODO P3b: if fire: rd2_demo_flag = 0; goto PICK. Needs the fire-bit
		   test on rd2_input_byte, which P3b's input code owns. */
	} else {
		/* TODO P3b: if key == 1 (ESC): rd2_stop_all_sound(); goto TITLE.
		   if key == 0x19 (P): rd2_pause_until_fire(). Same dependency. */
	}

	if (rd2_rick_dead != 0 && rd2_rick_y >= 0x100) {
		if (rd2_lives == 0)
			goto END_OF_RUN;
		rd2_kill_all_objects_and_actors();  /* $149c2 */
		rd2_restore_checkpoint();           /* $142fc = $13060, then S5 from $14300 */
		goto FRAME;
	}

	goto FRAME;

	/*
	 * MAPDONE (algo-flow.md S8), with the map-5 tease removed -- settled decision,
	 * not improvised here: PLAN.md T28 / hnk-system.md S7, "Port ships 4 maps, no
	 * more, no unlock code, no 5th picker row that goes anywhere." The literal
	 * original tests `[$1239c]==4` (always -> NEXT, plus the picker-row-5 unlock
	 * side effect this port omits) and separately `[$1239c]==5` (-> ENDING if
	 * `[$17994]==1`, else a scene + END_OF_RUN) -- but `[$1239c]` can now never
	 * reach 5 (NEXT no longer runs after map 4), so the "==5" case's two outcomes
	 * are re-triggered directly off "map 4 finished", one map earlier than the
	 * original, which is exactly what not having a map 5 to advance into implies.
	 * Every OTHER branch, and every other subsystem in this file, is the literal
	 * original.
	 */
MAPDONE:
	if (rd2_map_playing == 4) {
		if (rd2_picker_choice == 1) {
			goto ENDING;
		} else {
			/* TODO P3b: run_scene(1) ($18186, D0=1), only if !demo -- $17bf4 */
			goto END_OF_RUN;
		}
	}
	/* NEXT ($10b1a) */
	rd2_map_playing++;
	/* TODO P3c: rd2_extra_life(); -- $17782, algo-flow.md S7 */
	goto LOAD;

ENDING:
	/* $10bf2: bsr $10c28 is a traced no-op trampoline (algo-flow.md S8), not
	   needed here. */
	/* TODO P3c: rd2_tally_end_bonus(); -- $1789a, algo-flow.md S7 */
	/* TODO P3b: nothing when demo, else play_sound(9,0) and run_scene(2) -- $17bda */

END_OF_RUN:
	if (rd2_demo_flag != 0) {
		rd2_demo_flag = 0;
	} else {
		rd2_game_over_screen();     /* $17c06 */
		rd2_hall_of_fame_entry();   /* $17f22 */
	}
	goto TITLE;
}

/* eof */
