/*
 * xrick/include/rd2/rd2_game.h
 *
 * Rick Dangerous 2 -- the game_main state machine (algo-flow.md S1-S2) and the
 * global variables it and its callees share (algo-flow.md S1). This header declares
 * the state machine's entry point plus every subsystem entry point game_main calls,
 * so rd2_game.c's control flow (P3a) can be written and syntax-checked now even
 * though most of those subsystems (P3b-h) are not implemented yet -- each such
 * declaration is commented "TODO Pnn" and rd2_game.c will not LINK until they are
 * all defined (expected at this stage: port-rd2.md's own P7 is explicitly gated on
 * P3-P6 all being present. Do not add a stub body anywhere just to make it link
 * early -- an empty function is not a documented fact and would misrepresent
 * progress in port-rd2.md).
 */

#ifndef _RD2_GAME_H
#define _RD2_GAME_H

#include "system.h"

/* ---- variables (algo-flow.md S1; addresses are the original's RAM numbering, given
   only as plate comments -- the port's own storage is just C globals) */
extern S16 rd2_map_playing;      /* [$1239c] map being played, 1-4 (word; the original's range
                                     is 1-5 but map 5 is dead code that always hangs the loader
                                     if reached -- PLAN.md T28 -- so the port's own range is 1-4) */
extern S16 rd2_map_loaded;       /* [$1239e] map whose data is currently loaded */
extern S16 rd2_picker_choice;    /* [$17994] map chosen on the picker */
extern U16 rd2_picker_rows;      /* [$17992] number of unlocked picker rows, initial 4 */
extern U16 rd2_cheat_flag;       /* [$1798e] POOKY cheat flag, initial 0 */
extern U16 rd2_long_game;        /* [$17990] "long/short game" cheat-only switch, initial 0 */
extern S16 rd2_demo_flag;        /* [$3efb6] attract-mode flag, -1 while a demo plays */
extern U8  rd2_demo_ended;       /* [$3efb8] demo stream finished */
extern U8  rd2_input_byte;       /* [$1a4fb] current input byte (bit7 fire, 0 up,1 down,2 left,3 right) */
extern U8  rd2_last_scancode;    /* [$1a4fc] last key scancode */
extern S16 rd2_map_done_flag;    /* [$115e0] map-complete flag, set by the trigger scan */
extern S16 rd2_shot_hit_flag;    /* [$115dc] "laser shot hit something this frame" */
extern S16 rd2_rick_dead;        /* [$12e2a] Rick dead flag */
extern S16 rd2_actor_group_flag; /* [$144c2] armed 5-actor group flag, algo-flow.md S10 */
extern S16 rd2_lives;            /* [$17710] */
extern U16 rd2_lives_dirty;      /* [$1770e] */
extern S16 rd2_ammo;             /* [$176f4] laser ammo */
extern U16 rd2_ammo_dirty;       /* [$176f2] */
extern S16 rd2_bombs;            /* [$17702] bomb count */
extern U16 rd2_bombs_dirty;      /* [$176f0] score dirty (sic -- algo-flow.md S1 lists this next to
                                     bombs' dirty flag; kept as a separate field, not merged, since
                                     the doc names it distinctly) */
extern U32 rd2_score_bcd;        /* $176e4..6, 3 bytes packed BCD (6 digits); represented here as
                                     one U32 holding the packed BCD bytes in the low 3 bytes, matching
                                     what $17810's abcd chain operates on -- P3c decides the exact
                                     in-memory form when it implements $17810 */
extern U16 rd2_vblanks_per_frame; /* [$18ed8] 2 normally, 1 during the 16-step submap slide */
extern U16 rd2_vblank_counter;    /* [$19232] incremented by the vblank ISR */
extern S16 rd2_id_remap_flag;     /* [$1a5ce] S-key sound id-remap toggle */
extern U16 rd2_music_playing;     /* [$1aa08] 0 = no music running */

/* ---- subsystem entry points game_main calls, in algo-flow.md S2's own order */
void rd2_title_attract(void);        /* TODO P3b: $178dc, algo-flow.md S3 */
void rd2_level_picker(void);         /* TODO P3b: $17a46, algo-flow.md S4 */
void rd2_new_game_reset(void);       /* TODO P3c: $1771c (score:=0, lives:=6), algo-flow.md S7 */
void rd2_select_map_from_picker(void); /* TODO P3c: $123a0, algo-flow.md S2 */
void rd2_refill_ammo_bombs(void);    /* TODO P3c: $17760, algo-flow.md S7 */
void rd2_clear_screens(void);        /* TODO P4c: $19388 */
void rd2_load_map_if_changed(void);  /* TODO P3c: $123b0 loader, algo-flow.md S2/S5 */
void rd2_level_start(void);          /* TODO P3c: $142a0, algo-flow.md S5 */
void rd2_scan_spawn_table(void);     /* TODO P3f: $14594, level-tables.md S3 */
void rd2_update_actor_group(void);   /* TODO P3g: $15bc0, algo-flow.md S10 */
void rd2_update_actors(void);        /* TODO P3f: $14d48, algo-actors.md S3 */
void rd2_update_objects(void);       /* TODO P3e: $150a2, algo-objects.md */
void rd2_update_bonus_timer(void);   /* TODO P3c: $15826, algo-flow.md S7 */
void rd2_update_player_rick(void);   /* TODO P3d: $13096, algo-player.md */
void rd2_update_laser_shot(void);    /* TODO P3d: $13e14, algo-player.md */
void rd2_update_bomb(void);          /* TODO P3d: $13e98, algo-player.md S11 */
void rd2_scroll_edge_trigger(void);  /* TODO P4a: $16658, graphics.md S5 */
void rd2_animate_background_tiles(void); /* TODO P4a: $18dac, graphics.md S3b */
void rd2_draw_background(void);      /* TODO P4c: $18782, graphics.md S5 */
void rd2_draw_sprites(void);         /* TODO P4c: $170b6, graphics.md S5 */
void rd2_draw_hud(void);             /* TODO P4d: $177a8, graphics.md S4a */
void rd2_vblank_wait_and_flip(void); /* TODO P4c: $19216, graphics.md S1/algo-flow.md S2 */
void rd2_vblank_wait(void);          /* TODO P4c: $191e6 */
void rd2_scan_exit_triggers(void);   /* TODO P3c: $14362, algo-flow.md S9 */
void rd2_stop_all_sound(void);       /* TODO P5: $1a5d0, sound-ref.md / algo-flow.md S6 */
void rd2_kill_all_objects_and_actors(void); /* TODO P3e/P3f: $149c2 */
void rd2_restore_checkpoint(void);   /* TODO P3c: $13060, algo-flow.md S5 */
void rd2_map_done_sequence(void);    /* TODO P3c: $10ad2 MAPDONE, algo-flow.md S8 */
void rd2_pause_until_fire(void);     /* TODO P3b: the $19 (P) pause loop, algo-flow.md S6 */
void rd2_game_over_screen(void);     /* TODO P3b: $17c06, algo-flow.md S8 */
void rd2_hall_of_fame_entry(void);   /* TODO P3b: $17f22, algo-flow.md S11 */

/* ---- entry point */
void rd2_game_run(void);   /* game_main $10992, replaces rd1's game_run() when -rd 2 (P6) */

#endif /* _RD2_GAME_H */

/* eof */
