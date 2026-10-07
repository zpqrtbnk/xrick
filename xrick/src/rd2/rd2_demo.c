/*
 * xrick/src/rd2/rd2_demo.c
 *
 * Rick Dangerous 2 -- adapter to the shared demo core (include/demo.h). Host code, not
 * a transliteration. One segment per map (1..4 -> 0..3), entered at level start
 * ($10a4e); one tick per game_main frame ($10a54). Only real games are played or
 * recorded, never the game's own attract demo ([$3efb6] != 0), which keeps reading its
 * (count, state) stream through $141cc.
 *
 * Playback writes the joystick byte [$1a4fb] at the frame head, the point where
 * RD2_JOYSEQ and kb2/hatari_rd2_trace.py write it; within a frame the byte is read by
 * $141cc (inside $13096), before any IKBD byte can arrive (the host only feeds the IKBD
 * in rd2_sys_pump, from the end-of-frame waits). The keyboard stays live, so P (pause)
 * and fire to resume still work. Recording reads the same byte at the same point, so it
 * captures what the game saw, including the loader's clr.b [$1a4fb] ($11f7a).
 *
 * -record <file> writes <file> (a ready to build dat_rd2_script.c) and, for each map
 * recorded, <file>.map<N>.joy: one joystick byte per frame from level start, the
 * RD2_JOYSEQ / kb2/hatari_rd2_trace.py input format.
 */

#include <stdio.h>
#include <string.h>

#include "system.h"
#include "control.h"
#include "sysarg.h"

#include "rd2_mem.h"
#include "rd2_sys.h"
#include "rd2_game.h"

#ifdef ENABLE_DEMO

#include "demo.h"

#define RD2_DEMO_SEGS 4

/* joystick-1 byte (bit 0 up, 1 down, 2 left, 3 right, 7 fire) <-> CONTROL_* bits */
static U8
to_joy(U8 c)
{
	U8 j = 0;
	if (c & CONTROL_UP)    j |= 0x01;
	if (c & CONTROL_DOWN)  j |= 0x02;
	if (c & CONTROL_LEFT)  j |= 0x04;
	if (c & CONTROL_RIGHT) j |= 0x08;
	if (c & CONTROL_FIRE)  j |= 0x80;
	return j;
}

static U8
from_joy(U8 j)
{
	U8 c = 0;
	if (j & 0x01) c |= CONTROL_UP;
	if (j & 0x02) c |= CONTROL_DOWN;
	if (j & 0x04) c |= CONTROL_LEFT;
	if (j & 0x08) c |= CONTROL_RIGHT;
	if (j & 0x80) c |= CONTROL_FIRE;
	return c;
}

/* <record>.map<N>.joy for every map recorded */
static void
save_joy(void)
{
	static U8 buf[0x10000];
	char path[1024];
	FILE *f;
	U16 s, n, i;

	for (s = 0; s < RD2_DEMO_SEGS; s++) {
		n = demo_ticks(s, buf, 0xffff);
		if (n == 0)
			continue;
		for (i = 0; i < n; i++)
			buf[i] = to_joy(buf[i]);
		snprintf(path, sizeof path, "%s.map%u.joy", sysarg_args_record, (unsigned int)(s + 1));
		f = fopen(path, "wb");
		if (!f) {
			sys_printf("xrick/demo: could not write '%s'\n", path);
			continue;
		}
		fwrite(buf, 1, n, f);
		fclose(f);
		sys_printf("xrick/demo: wrote '%s' (%u frames)\n", path, (unsigned int)n);
	}
}

static const demoset_t demoset = {
	rd2_demo_scripts, RD2_DEMO_SEGS, "xrick/src/rd2/dat_rd2_script.c", "map", "map - 1",
	"", "rd2_demo_scripts", "4", save_joy, NULL
};

void
rd2_demo_init(void)
{
	demo_init(&demoset);
}

/* level start ($10a4e): a real game enters map [$1239c] */
void
rd2_demo_level(void)
{
	if (rd2_rw(RD2_DEMO) != 0)
		return;
	if (demo_recording())
		demo_enterSegment((U16)(rd2_rw(RD2_MAP_PLAYING) - 1));
	else if (demo_active) {
		demo_enterSegment((U16)(rd2_rw(RD2_MAP_PLAYING) - 1));
		if (!demo_active)                       /* no script for this map: demo over */
			rd2_sys_joyresync();
	}
}

/* frame head ($10a54) */
void
rd2_demo_frame(void)
{
	if (rd2_rw(RD2_DEMO) != 0)
		return;
	if (demo_recording())
		demo_record(from_joy(rd2_rb(RD2_JOY)));
	else if (demo_playing())
		rd2_wb(RD2_JOY, to_joy(demo_play()));
}

/* after the title sequence ($178dc), once: with -demo and a script for map 1, the
   solved game (PLAN.md T47) starts by itself as a real game on map 1, whether the
   title timed out or fire was pressed */
U8
rd2_demo_autostart(void)
{
	static U8 done;
	if (done || !demo_active || demo_recording() || rd2_demo_scripts[0].nbr == 0)
		return 0;
	done = 1;
	return 1;
}

/* the run is over (END_OF_RUN, or ESC back to the title): the keyboard takes over for
   game over, name entry and the title */
void
rd2_demo_stop(void)
{
	if (!demo_playing())
		return;
	demo_end();
	rd2_sys_joyresync();
}

#else

void rd2_demo_init(void) {}
void rd2_demo_level(void) {}
void rd2_demo_frame(void) {}
void rd2_demo_stop(void) {}
U8 rd2_demo_autostart(void) { return 0; }

#endif /* ENABLE_DEMO */

/* eof */
