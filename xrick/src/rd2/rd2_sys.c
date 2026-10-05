/*
 * xrick/src/rd2/rd2_sys.c
 *
 * Rick Dangerous 2 -- host bridge (rd2_sys.h). Transliterations of the ISRs at their
 * addresses: $1902e (VBL) and $1a546 (ACIA / IKBD). The rest is host code.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#include "system.h"
#include "sysevt.h"
#include "sysvid.h"
#include "control.h"
#include "syskbd.h"
#include "rects.h"
#include "fb.h"

#include "rd2_mem.h"
#include "rd2_sys.h"
#include "rd2_game.h"
#include "sysarg.h"

U16 rd2_hw_pal[16];

static U8 base_hi, base_mid;        /* $ff8201 / $ff8203 */
static U32 vbl_next;                /* host ms of the next VBL */
static U8 joy_sent;                 /* last joystick-1 byte sent by the "IKBD" */
static U8 joy_stale;                /* resend at the next pump even if unchanged */
static U8 replay_fast;              /* RD2_START replay running: no waits, no drawing */
static U8 host_r[36], host_g[36], host_b[36];   /* the fb palette present() sets, also for dbg_shot */
static rect_t full = { 0, 0, FB_WIDTH, FB_HEIGHT, NULL };

#define VBL_MS 20   /* 50 Hz */

/* ---- debug hooks (env vars, inert when unset): RD2_SHOT=<dir> with RD2_SHOT_AT=v1,v2,..
   dumps the presented frame at those VBL counts as <dir>/rd2_<v>.ppm; RD2_EXIT_AT=v exits;
   RD2_INPUT=v:joy,v:joy,.. sets the host joystick byte (hex) from VBL v on, overriding the
   keyboard (0 hands control back). Test aids only, not part of the game. */
static U32 vbl_total;
static int dbg_joy = -1;

static int
dbg_listed(const char *list, U32 v)
{
	char buf[16];
	if (!list) return 0;
	snprintf(buf, sizeof buf, "%u", v);
	while (*list) {
		size_t n = strcspn(list, ",");
		if (n == strlen(buf) && !strncmp(list, buf, n)) return 1;
		list += n;
		if (*list == ',') list++;
	}
	return 0;
}

static void
dbg_input(U32 v)
{
	const char *p = getenv("RD2_INPUT");
	while (p && *p) {
		unsigned long at = strtoul(p, (char **)&p, 10);
		unsigned long j = 0;
		if (*p == ':') j = strtoul(p + 1, (char **)&p, 16);
		if (at == v) dbg_joy = j ? (int)j : -1;
		if (*p == ',') p++; else break;
	}
}

static void
dbg_shot(U32 v)
{
	const char *dir = getenv("RD2_SHOT");
	char path[512];
	FILE *f;
	int x, y;
	if (!dir || !dbg_listed(getenv("RD2_SHOT_AT"), v)) return;
	snprintf(path, sizeof path, "%s/rd2_%u.ppm", dir, v);
	f = fopen(path, "wb");
	if (!f) return;
	fprintf(f, "P6\n320 200\n255\n");
	for (y = 0; y < 200; y++)
		for (x = 0; x < 320; x++) {
			U8 c = fb[y][x] < 36 ? fb[y][x] : 0;
			fputc(host_r[c], f);
			fputc(host_g[c], f);
			fputc(host_b[c], f);
		}
	fclose(f);
}

/* ---- host key -> IKBD code, only the codes the game tests (algo-flow.md §1/§3:
   1 ESC, $19 P, $1f S, $39 space). Name entry ($17f22) uses the joystick, not keys.
   ESC is NOT sent: as in rd1, the host's ESC (CONTROL_EXIT) quits the program
   (rd2_sys_pump), and rd1's E (CONTROL_END) ends the game (rd2_sys_endreq); user
   decision 2026-09-24, not the ST behaviour (ESC $01 = back to the title, $10b90). */
extern void (*sysevt_rawkey)(U16 scancode, U8 down, U8 repeat);
extern U8 sysevt_quit;

/* The host bindings (syskbd.c, -keys) decide, as in rd1: a key bound to a direction or fire
   is only that (sysevt.c tests those first), P is the pause binding, and the ST's S ($1f, debug
   sound-id remap [$1a5ce]) is syskbd_sndset (F10), since S is a direction key (user decision 2026-10-04).
   The fire key (Space) is therefore not also sent as the keyboard's $39: see port-rd2.md §7. */
static U8
st_code(U16 sdl)
{
	if (sdl == syskbd_up || sdl == syskbd_down || sdl == syskbd_left ||
	    sdl == syskbd_right || sdl == syskbd_fire)
		return 0;
	if (sdl == syskbd_pause)
		return 0x19;
	if (sdl == syskbd_sndset)
		return 0x1f;
	return 0;
}

static void
rawkey(U16 sdl, U8 down, U8 repeat)
{
	U8 c;
	if (repeat)
		return;             /* the IKBD does not auto-repeat */
	c = st_code(sdl);
	if (c)
		rd2_1a546(down ? c : (U8)(c | 0x80));
}

void
rd2_sys_init(void)
{
	U16 i;
	for (i = 0; i < 16; i++)
		rd2_hw_pal[i] = 0;
	base_hi = base_mid = 0;
	joy_sent = 0;
	sysevt_rawkey = rawkey;
	/* the host display scales every colour by its gamma, which starts at 0 and only rd1's
	   fades set; RD2 fades in the ST palette itself, so the host gamma stays at full */
	sysvid_setGamma(255);
	vbl_next = sys_gettime() + VBL_MS;
}

void
rd2_sys_setcolor(U16 i, U16 v)
{
	rd2_hw_pal[i & 15] = v;
}

void
rd2_sys_setbase(U8 hi, U8 mid)
{
	base_hi = hi;
	base_mid = mid;
}

/* ---- $1a546: one IKBD byte (flags at $1a5ca = "joystick 0 byte next", $1a5cc = "joystick 1 byte next") */
void
rd2_1a546(U8 b)
{
	if (rd2_rb(0x1a5ca)) {                 /* $1a592 */
		rd2_wb(0x1a4fa, b);
		rd2_wb(0x1a5ca, 0);
		return;
	}
	if (rd2_rb(0x1a5cc)) {                 /* $1a5ae */
		rd2_wb(0x1a4fb, b);
		rd2_wb(0x1a5cc, 0);
		return;
	}
	if (b == 0xfe)
		rd2_wb(0x1a5ca, 1);
	else if (b == 0xff)
		rd2_wb(0x1a5cc, 1);
	else
		rd2_wb(0x1a4fc, b);
}

/* joystick 1 from the host controls: bit0 up, 1 down, 2 left, 3 right, 7 fire */
static void
joystick(void)
{
	U8 j = 0;
	if (control_status & CONTROL_UP)    j |= 0x01;
	if (control_status & CONTROL_DOWN)  j |= 0x02;
	if (control_status & CONTROL_LEFT)  j |= 0x04;
	if (control_status & CONTROL_RIGHT) j |= 0x08;
	if (control_status & CONTROL_FIRE)  j |= 0x80;
	if (dbg_joy >= 0) j = (U8)dbg_joy;
	if (j != joy_sent || joy_stale) {
		joy_sent = j;
		joy_stale = 0;
		rd2_1a546(0xff);
		rd2_1a546(j);
	}
}

/* ---- host overlays (rd1 features, not in the original; drawn into fb only) ---- */
static U8 ovl_paused, ovl_info;

void rd2_sys_paused(U8 on) { ovl_paused = on; }
void rd2_sys_info(U8 on)   { ovl_info = on; }
U8 rd2_sys_endreq(void)    { return (U8)((control_status & CONTROL_END) != 0); }
U8 rd2_sys_pausekey(void)  { return (U8)((control_status & CONTROL_PAUSE) != 0); }

/* ---- cheats (rd1 game_toggleCheat; host additions, rd2_sys.h) ---- */
U8 rd2_cheat_trainer, rd2_cheat_invincible, rd2_cheat_highlight;

void
rd2_sys_toggleCheat(U8 n)
{
#ifndef ENABLE_CHEATS
	(void)n;
	return;
#endif
	if (!ovl_info || rd2_rw(RD2_DEMO) != 0)                  /* in a level, played by hand */
		return;
#ifdef ENABLE_DEMO
	if (sysarg_args_record)
		return;
#endif
	switch (n) {
	case 1:                                                  /* rd1: lives, bombs, bullets := 6 */
		rd2_cheat_trainer = !rd2_cheat_trainer;
		rd2_ww(0x17710, 6);                                  /* lives, HUD slot $1770e */
		rd2_ww(0x1770e, 0xffff);
		rd2_ww(0x176f4, 6);                                  /* laser, HUD slot $176f2 */
		rd2_ww(0x176f2, 0xffff);
		rd2_ww(0x17702, 6);                                  /* bombs, HUD slot $17700 */
		rd2_ww(0x17700, 0xffff);
		break;
	case 2:
		rd2_cheat_invincible = !rd2_cheat_invincible;
		break;
	case 3:
		rd2_cheat_highlight = !rd2_cheat_highlight;
		break;
	}
}

/* highlight. Boxes: collected during a frame's logic, shown from the flip that presents that
   frame. Tint: per screen ($70000 / $78000), the pixels the sprite blitters drew; a screen's
   map is cleared when it becomes the draw screen again, since the game then redraws its whole
   playfield ($18782, graphics.md §5). */
#define BOX_RICK   2
#define BOX_ACTOR  3
#define BOX_MAX    64
typedef struct { U8 kind; S16 x, y, w, h; } box_t;
static box_t box_pend[BOX_MAX], box_shown[BOX_MAX];
static U16 box_npend, box_nshown;
static U8 hl[2][200][320];

static U8 *
hlmap(U32 screen)
{
	if ((screen & ~0x8000u) != 0x70000u)
		return NULL;
	return &hl[(screen >> 15) & 1][0][0];
}

static void
box_add(box_t *l, U16 *n, U8 kind, S16 x, S16 y, S16 w, S16 h)
{
	if (*n < BOX_MAX) {
		l[*n].kind = kind;
		l[*n].x = x; l[*n].y = y; l[*n].w = w; l[*n].h = h;
		(*n)++;
	}
}

void
rd2_sys_box(U8 kind, S16 x, S16 y, S16 w, S16 h)
{
	U16 i;
	if (!rd2_cheat_highlight)
		return;
	for (i = 0; i < box_npend; i++)                          /* $14a3c's own call to $14b7a */
		if (box_pend[i].x == x && box_pend[i].y == y && box_pend[i].w == w && box_pend[i].h == h)
			return;
	box_add(box_pend, &box_npend, kind, x, y, w, h);
}

void
rd2_sys_hlpixel(U32 screen, S16 x, S16 y)
{
	U8 *m = hlmap(screen);
	if (m && rd2_cheat_highlight && x >= 0 && x < 320 && y >= 0 && y < 200)
		m[y * 320 + x] = 1;
}

static void
cheat_flip(void)
{
	U8 *m = hlmap(rd2_rl(0x18ede));
	U32 a;
	U16 i;
	if (m)
		memset(m, 0, 200 * 320);
	box_nshown = 0;
	if (rd2_cheat_highlight) {
		for (i = 0; i < box_npend; i++)
			box_shown[box_nshown++] = box_pend[i];
		/* Rick's own box, as $14b7a tests it: x [x+4, x+$14), y [y, y+$15), crouched from y+5 */
		if (rd2_rw(0x12e2a) == 0) {
			S16 c = rd2_rw(0x12e18) != 0 ? 5 : 0;
			box_add(box_shown, &box_nshown, BOX_RICK, (S16)(rd2_rws(0x1695c) + 4),
			        (S16)(rd2_rws(0x16960) + c), 0x10, (S16)(0x15 - c));
		}
		/* live actors, with their own size (+26, +28) as $14d04 and $15fba test it */
		for (a = 0x16b6au, i = 0; i < 6; i++, a += 0x58)
			if (rd2_rw(a) != 0)
				box_add(box_shown, &box_nshown, BOX_ACTOR, rd2_rws(a + 2), rd2_rws(a + 6),
				        rd2_rws(a + 0x26), rd2_rws(a + 0x28));
	}
	box_npend = 0;
}

/* box outline in fb, game coords -> screen as a sprite ($17116: x + $20, y - $38, fine scroll),
   clipped to the playfield (32..287, 8..199) */
static void
box_draw(const box_t *b)
{
	S16 x0 = (S16)(b->x + 0x20), y0 = (S16)(b->y - 0x38 - (rd2_rw(0x16462) & 7));
	S16 x1 = (S16)(x0 + b->w - 1), y1 = (S16)(y0 + b->h - 1), x, y;
	U8 c = (U8)(32 + b->kind);
	if (b->w <= 0 || b->h <= 0)
		return;
	for (y = y0; y <= y1; y++) {
		if (y < 8 || y >= 200)
			continue;
		for (x = x0; x <= x1; x++) {
			if (x < 32 || x >= 288)
				continue;
			if (y == y0 || y == y1 || x == x0 || x == x1)
				fb[y][x] = c;
		}
	}
}

/* one glyph of the game's own font ($3ce54 + 32n: 8 rows x 4 plane bytes, opaque, the way
   $19272 draws it) at pixel x, y of fb */
static void
glyph(U8 g, U16 x, U16 y)
{
	U32 a = 0x3ce54u + 32u * g;
	U16 r, i;
	for (r = 0; r < 8; r++) {
		U8 p0 = rd2_rb(a + 4 * r), p1 = rd2_rb(a + 4 * r + 1);
		U8 p2 = rd2_rb(a + 4 * r + 2), p3 = rd2_rb(a + 4 * r + 3);
		for (i = 0; i < 8; i++) {
			U8 m = (U8)(0x80 >> i);
			fb[y + r][x + i] = (U8)(((p0 & m) ? 1 : 0) | ((p1 & m) ? 2 : 0) |
			                        ((p2 & m) ? 4 : 0) | ((p3 & m) ? 8 : 0));
		}
	}
}

/* ASCII text: digits map to glyphs 0-9, letters and space are at their ASCII codes */
static void
text(const char *s, U16 x, U16 y)
{
	for (; *s; s++, x += 8)
		glyph((U8)(*s >= '0' && *s <= '9' ? *s - '0' : *s), x, y);
}

static void
overlays(void)
{
	char s[8];
	U16 i;
	for (i = 0; i < box_nshown; i++)
		box_draw(&box_shown[i]);
	if (ovl_info) {                       /* rd1 env_paintXtra: T / I / H at 0,0 8,0 16,0 */
		if (rd2_cheat_trainer)    text("T", 0, 0);
		if (rd2_cheat_invincible) text("I", 8, 0);
		if (rd2_cheat_highlight)  text("H", 16, 0);
	}
	if (ovl_info) {                       /* rd1 env_paintXtra: M<map> / S<submap> at 0,16 */
		snprintf(s, sizeof s, "M%02u", (unsigned int)(rd2_rw(RD2_MAP_PLAYING) % 100));
		text(s, 0, 16);
		snprintf(s, sizeof s, "S%02u", (unsigned int)(rd2_rw(0x16464u) % 100));   /* $14458 */
		text(s, 0, 24);
	}
	if (ovl_paused) {                     /* rd1 screen_pausedtxt at 120,80 */
		text("          ", 120, 80);
		text("  PAUSED  ", 120, 88);
		text("          ", 120, 96);
	}
}

/* show the ST low-res screen at the video base with the hardware palette */
static void
present(void)
{
	/* 0-15 the ST palette; 16-31 the same lightened, for highlighted sprite pixels (as rd1's
	   GFXST highlight colours, fb.c); 32-35 the highlight boxes: trigger yellow, hurt red,
	   Rick white, actor green */
	static const U8 br[4] = { 0xff, 0xff, 0xff, 0x00 }, bg[4] = { 0xff, 0x30, 0xff, 0xff },
	                bb[4] = { 0x00, 0x30, 0xff, 0x00 };
	U8 *r = host_r, *g = host_g, *b = host_b;
	U32 base = ((U32)base_hi << 16) | ((U32)base_mid << 8);
	U8 *m = hlmap(base);
	U16 i, x, y;

	for (i = 0; i < 16; i++) {
		r[i] = (U8)(((rd2_hw_pal[i] >> 8) & 7) * 255 / 7);
		g[i] = (U8)(((rd2_hw_pal[i] >> 4) & 7) * 255 / 7);
		b[i] = (U8)((rd2_hw_pal[i] & 7) * 255 / 7);
		r[16 + i] = (U8)(r[i] + (255 - r[i]) * 5 / 16);
		g[16 + i] = (U8)(g[i] + (255 - g[i]) * 5 / 16);
		b[16 + i] = (U8)(b[i] + (255 - b[i]) * 5 / 16);
	}
	for (i = 0; i < 4; i++) {
		r[32 + i] = br[i];
		g[32 + i] = bg[i];
		b[32 + i] = bb[i];
	}
	sysvid_setPaletteFromRGB(r, g, b, 36);

	for (y = 0; y < 200; y++)
		for (x = 0; x < 320; x += 16) {
			U32 a = base + y * 160u + (x >> 4) * 8u;
			U16 p0 = rd2_rw(a), p1 = rd2_rw(a + 2), p2 = rd2_rw(a + 4), p3 = rd2_rw(a + 6);
			for (i = 0; i < 16; i++) {
				U16 bit = (U16)(0x8000 >> i);
				fb[y][x + i] = (U8)(((p0 & bit) ? 1 : 0) | ((p1 & bit) ? 2 : 0) |
				                    ((p2 & bit) ? 4 : 0) | ((p3 & bit) ? 8 : 0) |
				                    ((m && m[y * 320 + x + i]) ? 0x10 : 0));
			}
		}
	overlays();
	sysvid_update(&full);
}

/* VBL ISR $1902e */
static void
vbl(void)
{
	rd2_ww(0x19232, (U16)(rd2_rw(0x19232) + 1));
	rd2_1a866();
	vbl_total++;
	dbg_input(vbl_total);
}

void
rd2_sys_pump(void)
{
	U32 now;
	U8 shown = 0;

	sysevt_poll();
	if (sysevt_quit || (control_status & CONTROL_EXIT))
		exit(0);                                             /* window closed or ESC (rd1); sys_shutdown runs at exit */
	if (replay_fast) {                                       /* RD2_START: one VBL per pump, no */
		vbl();                                               /* wait, no drawing, the host joystick */
		vbl_next = sys_gettime() + VBL_MS;                   /* left alone (rd2_dbg_frame feeds it) */
		return;
	}
	joystick();

	now = sys_gettime();
	if ((S32)(vbl_next - now) > 0) {
#ifdef __EMSCRIPTEN__
		/* web (wasm.md W2): every RD2 wait spins on this pump, so sleeping here is where
		   the browser gets control back (drawing, input, audio callback). emscripten_sleep
		   unwinds the wasm stack and resumes it later (link flag -sASYNCIFY, build.sh) */
		emscripten_sleep((unsigned int)(vbl_next - now));
#else
		sys_sleep((int)(vbl_next - now));
#endif
		now = sys_gettime();
	}
	while ((S32)(now - vbl_next) >= 0) {
		vbl();
		vbl_next += VBL_MS;
		shown = 1;
	}
	if (shown) {
		const char *e = getenv("RD2_EXIT_AT");
		present();
		dbg_shot(vbl_total);
		if (e && vbl_total >= (U32)strtoul(e, NULL, 10))
			exit(0);
	}
}

/* $19216: wait until [$19232] >= [$18ed8]-1, then flip */
void
rd2_19216(void)
{
	S16 d0 = (S16)(rd2_rws(0x18ed8) - 1);
	while (d0 > rd2_rws(0x19232))
		rd2_sys_pump();
	rd2_19234();
}

/* $19234: toggle bit 7 of $18edc / $18ee0 (the two screen pointers), video base := [$18eda] */
void
rd2_19234(void)
{
	rd2_wb(0x18edc, rd2_rb(0x18edc) ^ 0x80);
	rd2_wb(0x18ee0, rd2_rb(0x18ee0) ^ 0x80);
	rd2_sys_setbase(rd2_rb(0x18edb), rd2_rb(0x18edc));
	cheat_flip();                                            /* host: highlight boxes and tint */
}

/* $191e6: wait until [$19232] >= [$18ed8]-1 and has changed since entry, then clear it */
void
rd2_191e6(void)
{
	U16 d0 = rd2_rw(0x19232);
	S16 d1 = (S16)(rd2_rws(0x18ed8) - 1);
	while (d1 > rd2_rws(0x19232) || rd2_rw(0x19232) == d0)
		rd2_sys_pump();
	rd2_ww(0x19232, 0);
}

/* debug: RD2_FORCE_MAP=N writes [$1239c] = N at the first loader call ($10a36), as
   kb2/hatari_rd2.py does in Hatari; the attract demo then replays map N */
void
rd2_dbg_load(void)
{
	static int done;
	const char *m = getenv("RD2_FORCE_MAP");
	if (done || !m)
		return;
	done = 1;
	rd2_ww(0x1239cu, (U16)strtoul(m, NULL, 10));
}

/*
 * debug (branch `solver`, PLAN.md T47): RD2_START=<file.joy> replays the file -- one
 * joystick byte per frame, byte k-1 at frame head k, as RD2_JOYSEQ and the demo playback
 * write [$1a4fb] -- as fast as the game runs (rd2_sys_pump), then hands the joystick to the
 * player. With `-game 2 -map N` and a .joy the solver wrote from a new game on map N
 * (build/rd2solve/mapN/best.joy), the game is where the solver left Rick: the SDL build and
 * xrick2-core play the same frames byte for byte (kb2/demo-solver.md section 1). The game
 * is then paused (P plays).
 */
static void
start_frame(void)
{
	static FILE *js;
	static int state;                                        /* 0 not started, 1 replaying, 2 off */
	static U32 k, t0;
	int c;

	if (state == 2)
		return;
	if (state == 0) {
		const char *p = getenv("RD2_START");
		if (!p || !(js = fopen(p, "rb"))) {
			if (p)
				sys_printf("xrick: RD2_START: cannot read '%s'\n", p);
			state = 2;
			return;
		}
		sys_printf("xrick: RD2_START: replaying %s\n", p);
		t0 = sys_gettime();
		state = 1;
		replay_fast = 1;
	}
	if ((c = fgetc(js)) != EOF) {
		rd2_wb(0x1a4fbu, (U8)c);
		k++;
		return;
	}
	fclose(js);
	state = 2;
	replay_fast = 0;
	vbl_next = sys_gettime() + VBL_MS;
	rd2_sys_joyresync();                                     /* the host state from the next pump on */
	rd2_wb(0x1a4fcu, 0x19);                                  /* paused, as if P had been pressed
	                                                            (rd2_game.c $10ba6): P plays */
	sys_printf("xrick: RD2_START: %u frames replayed (VBL %u, %u ms), paused: press P to play\n",
	           k, vbl_total, sys_gettime() - t0);
}

/* debug: RD2_TRACE=<dir> [RD2_TRACE_N=frames] writes RAM $12e00-$17800, $54c00-$56400 and
   $70000-$7ffff at each game_main
   frame head ($10a54) as <dir>/f<k>.bin, k = 1.., for comparison with the original under Hatari */
void
rd2_dbg_frame(void)
{
	static U32 k;
	const char *dir = getenv("RD2_TRACE");
	char path[512];
	FILE *f;
	U32 n;
	start_frame();
	if (!dir) return;
	n = getenv("RD2_TRACE_N") ? (U32)strtoul(getenv("RD2_TRACE_N"), NULL, 10) : 1000;
	if (++k > n) return;
	snprintf(path, sizeof path, "%s/f%u.bin", dir, k);
	f = fopen(path, "wb");
	if (!f) return;
	fwrite(rd2_ram + 0x12e00, 1, 0x17800 - 0x12e00, f);
	fwrite(rd2_ram + 0x54c00, 1, 0x56400 - 0x54c00, f);
	fwrite(rd2_ram + 0x70000, 1, 0x10000, f);
	fclose(f);
	if (k == n)
		exit(0);
	/* RD2_JOYSEQ=<file>: after the dump, frame k writes byte k-1 of the file into the joystick
	   cell [$1a4fb]; frame 1 also clears the demo flag [$3efb6], turning the run into a real
	   game driven by that input (the Hatari script writes the same bytes at the same point) */
	if (getenv("RD2_JOYSEQ")) {
		static FILE *js;
		int c;
		if (k == 1) {
			js = fopen(getenv("RD2_JOYSEQ"), "rb");
			rd2_ww(0x3efb6u, 0);
		}
		if (js && (c = fgetc(js)) != EOF)
			rd2_wb(0x1a4fbu, (U8)c);
	}
}

/* demo playback wrote [$1a4fb] behind the IKBD's back: send the host state again */
void
rd2_sys_joyresync(void)
{
	joy_stale = 1;
}

void
rd2_sys_hang_red(void)
{
	rd2_sys_setcolor(0, 0x700);
	for (;;)
		rd2_sys_pump();
}

/* eof */
