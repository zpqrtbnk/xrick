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

#include "system.h"
#include "sysevt.h"
#include "sysvid.h"
#include "control.h"
#include "rects.h"
#include "fb.h"

#include "rd2_mem.h"
#include "rd2_sys.h"
#include "rd2_game.h"

U16 rd2_hw_pal[16];

static U8 base_hi, base_mid;        /* $ff8201 / $ff8203 */
static U32 vbl_next;                /* host ms of the next VBL */
static U8 joy_sent;                 /* last joystick-1 byte sent by the "IKBD" */
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
			U16 c = rd2_hw_pal[fb[y][x]];
			fputc(((c >> 8) & 7) * 255 / 7, f);
			fputc(((c >> 4) & 7) * 255 / 7, f);
			fputc((c & 7) * 255 / 7, f);
		}
	fclose(f);
}

/* ---- host key -> IKBD code, only the codes the game tests (algo-flow.md §1/§3:
   1 ESC, $19 P, $1f S, $39 space). Name entry ($17f22) uses the joystick, not keys. */
extern void (*sysevt_rawkey)(U16 scancode, U8 down, U8 repeat);
extern U8 sysevt_quit;

static U8
st_code(U16 sdl)
{
	switch (sdl) {
	case SDL_SCANCODE_ESCAPE: return 0x01;
	case SDL_SCANCODE_P:      return 0x19;
	case SDL_SCANCODE_S:      return 0x1f;
	default: return 0;
	}
	/* SDL_SCANCODE_SPACE is the joystick fire key (syskbd_fire) and is therefore not
	   also sent as the keyboard's $39: see port-rd2.md §7 (host binding). */
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
	if (j != joy_sent) {
		joy_sent = j;
		rd2_1a546(0xff);
		rd2_1a546(j);
	}
}

/* show the ST low-res screen at the video base with the hardware palette */
static void
present(void)
{
	U8 r[16], g[16], b[16];
	U32 base = ((U32)base_hi << 16) | ((U32)base_mid << 8);
	U16 i, x, y;

	for (i = 0; i < 16; i++) {
		r[i] = (U8)(((rd2_hw_pal[i] >> 8) & 7) * 255 / 7);
		g[i] = (U8)(((rd2_hw_pal[i] >> 4) & 7) * 255 / 7);
		b[i] = (U8)((rd2_hw_pal[i] & 7) * 255 / 7);
	}
	sysvid_setPaletteFromRGB(r, g, b, 16);

	for (y = 0; y < 200; y++)
		for (x = 0; x < 320; x += 16) {
			U32 a = base + y * 160u + (x >> 4) * 8u;
			U16 p0 = rd2_rw(a), p1 = rd2_rw(a + 2), p2 = rd2_rw(a + 4), p3 = rd2_rw(a + 6);
			for (i = 0; i < 16; i++) {
				U16 m = (U16)(0x8000 >> i);
				fb[y][x + i] = (U8)(((p0 & m) ? 1 : 0) | ((p1 & m) ? 2 : 0) |
				                    ((p2 & m) ? 4 : 0) | ((p3 & m) ? 8 : 0));
			}
		}
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
	if (sysevt_quit)
		exit(0);                                             /* sys_shutdown runs at exit (xrick.c) */
	joystick();

	now = sys_gettime();
	if ((S32)(vbl_next - now) > 0) {
		sys_sleep((int)(vbl_next - now));
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

void
rd2_sys_hang_red(void)
{
	rd2_sys_setcolor(0, 0x700);
	for (;;)
		rd2_sys_pump();
}

/* eof */
