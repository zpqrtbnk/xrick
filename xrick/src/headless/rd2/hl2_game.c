/*
 * xrick/src/headless/rd2/hl2_game.c
 *
 * xrick2-core only (branch `solver`, never shipped): rd2_game_run (src/rd2/rd2_game.c)
 * from PICKED on, as a state machine one frame at a time. The frame itself is the
 * game's rd2_frame_step; boot, LOAD and MAPDONE are copied from rd2_game_run without
 * its host hooks -- keep them in step with it.
 */

#include <setjmp.h>
#include <string.h>

#include "rd2_mem.h"
#include "rd2_sys.h"
#include "rd2_game.h"
#include "hl2.h"

static int status = HL2_STEP;
static U32 tick;
static jmp_buf hang_jmp;
static int hang_armed;

/* rd2_boot ($10000), minus the host's own init */
void
hl2_boot(void)
{
	rd2_mem_init();
	rd2_ww(0x18ed8, 2);             /* $1901a */
	rd2_19106();                    /* palette $18ee6 */
	rd2_ww(RD2_MAP_PLAYING, 1);
	rd2_123ca();                    /* silent map-1 load */
}

/* LOAD ($10a30-$10a4e) */
static void
load(void)
{
	rd2_19388();                                /* $10a30 */
	rd2_123b0();                                /* $10a36 */
	rd2_17760();                                /* $10a3c */
	rd2_ww(RD2_MAPDONE, 0);                     /* $10a42 */
	rd2_19388();                                /* $10a48 */
	rd2_142a0();                                /* $10a4e */
	tick = 0;
}

void
hl2_hang(void)
{
	if (hang_armed)
		longjmp(hang_jmp, 1);
	for (;;) ;
}

void
hl2_start(int map)
{
	hl2_boot();
	hl2_newgame(map);
}

/* hl2_start after the boot (the write audit starts between the two) */
void
hl2_newgame(int map)
{
	rd2_wb(RD2_JOY, 0);                         /* $10a12 */
	rd2_ww(RD2_PICKER_CHOICE, (U16)map);        /* -map N: as if N were picked */
	rd2_1771c();                                /* $10a24 */
	rd2_123a0();                                /* $10a2a */
	load();
	status = HL2_STEP;
}

/* MAPDONE ($10ad4): HL2_MAP after NEXT's load, else where the run ends */
static int
mapdone(void)
{
	if (rd2_rw(RD2_MAP_PLAYING) == 4) {
		if (rd2_rw(RD2_PICKER_ROWS) != 5) {
			if (rd2_rw(RD2_PICKER_CHOICE) != 1) {
				rd2_17bf4();
				return HL2_END;
			}
			rd2_ww(RD2_PICKER_ROWS, 5);         /* $10af2 */
		}
	} else if (rd2_rw(RD2_MAP_PLAYING) == 5)
		return HL2_END;                         /* never reached: map 5 hangs */
	rd2_ww(RD2_MAP_PLAYING, (U16)(rd2_rw(RD2_MAP_PLAYING) + 1));   /* NEXT $10b1a */
	rd2_17782();
	if (setjmp(hang_jmp)) {
		hang_armed = 0;
		return HL2_HANG;
	}
	hang_armed = 1;
	load();
	hang_armed = 0;
	return HL2_MAP;
}

/*
 * watchdog: a frame that never ends. Map 4 submap 6 (2026-10-04): an actor's movement
 * script loop in update_actor_ai ($14da4, rd2_actors.c rd2_14d70) never left, so the
 * search's workers spun for an hour. A periodic SIGALRM (WD_MS) sees whether a frame
 * finished since the last tick; when one has not, it jumps out of hl2_step, which
 * reports HL2_STUCK. Whether the ST hangs the same way is not known.
 */
#include <signal.h>
#include <sys/time.h>

#define WD_MS 250

static sigjmp_buf stuck_jmp;
static volatile sig_atomic_t in_frame, frames_done, last_seen = -1;

static void
on_alarm(int sig)
{
	(void)sig;
	if (in_frame && frames_done == last_seen) {
		in_frame = 0;
		siglongjmp(stuck_jmp, 1);
	}
	last_seen = frames_done;
}

void
hl2_watchdog(void)
{
	struct sigaction sa;
	struct itimerval it;

	memset(&sa, 0, sizeof sa);
	sa.sa_handler = on_alarm;
	sa.sa_flags = SA_NODEFER | SA_RESTART;   /* not blocked after the jump (no mask saved) */
	sigaction(SIGALRM, &sa, NULL);
	it.it_interval.tv_sec = 0;
	it.it_interval.tv_usec = WD_MS * 1000;
	it.it_value = it.it_interval;
	setitimer(ITIMER_REAL, &it, NULL);
}

int
hl2_step(U8 joy)
{
	if (status != HL2_STEP && status != HL2_MAP)
		return status;
	rd2_wb(RD2_JOY, joy);                       /* as rd2_demo_frame plays it */
	tick++;
	if (sigsetjmp(stuck_jmp, 0)) {
		status = HL2_STUCK;
		return status;
	}
	in_frame = 1;
	switch (rd2_frame_step()) {
	case RD2_STEP_FRAME:
		status = HL2_STEP;
		break;
	case RD2_STEP_MAPDONE:
		status = mapdone();
		break;
	default:                                    /* END_OF_RUN; TITLE/PICK need keys */
		status = HL2_OVER;
		break;
	}
	in_frame = 0;
	frames_done++;
	return status;
}

int hl2_status(void) { return status; }

/* hl2_state.c: the driver's own part of a snapshot */
void hl2_gameGet(int *s, U32 *t) { *s = status; *t = tick; }
void hl2_gameSet(int s, U32 t) { status = s; tick = t; }
U32 hl2_tick(void) { return tick; }

/* eof */
