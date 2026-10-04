/*
 * xrick/src/rd2/rd2_render.c
 *
 * Rick Dangerous 2 -- rendering, transliterated from the disassembly (read 2026-09-24):
 * palette/fades $19106-$191e4, text $19272-$19386, clear $19388-$194cc, banner $194ce,
 * sprite walkers $17086-$171a2, background window $16474-$16656, scrolling $16658-$167a0,
 * animated tiles $175c6-$17692 and $18dac, row renderer $185a6 (raw bytes: Ghidra misdecodes
 * $185fe on), bitmap to screen $18782/$18818, $1884c/$1888e, new picture $188d0, submap
 * transitions $18abe/$18bb2 with the slides $18b5c/$18c50, scene picture copy $18caa.
 * kb2: algo-render.md, graphics.md.
 *
 * The two sprite blitters ($19e96 plain, $1952c masked; 521 and 769 unrolled instructions)
 * are implemented per pixel from algo-render.md §3, which was written from reading both in
 * full; their common entry (flicker, fine scroll, rejects, top/bottom clip, silhouette clear)
 * was re-read here ($19e96-$19f5e).
 */

#include "rd2_mem.h"
#include "rd2_cpu.h"
#include "rd2_game.h"
#include "rd2_sys.h"

#define W(v)  ((S16)(v))

/* ---- palette ($ff8240 = rd2_hw_pal) ---- */

/* $19116: [$18ee2] = a0 ; copy 16 words to the palette registers */
void
rd2_19116(U32 a0)
{
	U16 i;
	rd2_wl(0x18ee2, a0);
	for (i = 0; i < 16; i++)
		rd2_sys_setcolor(i, rd2_rw(a0 + 2u * i));
}

/* $19106 */
void
rd2_19106(void)
{
	rd2_19116(0x18ee6u);
}

/* $19134 fade in (8 steps) */
void
rd2_19134(void)
{
	U16 d2 = 0x700, d3 = 0x70;
	S16 d4;
	for (d4 = 7; ; ) {
		U32 a1;
		U16 c;
		rd2_191e6();
		a1 = rd2_rl(0x18ee2);
		for (c = 0; c < 16; c++, a1 += 2) {
			U16 t = rd2_rw(a1);
			if ((S16)(t & 0x700) > (S16)d2) rd2_sys_setcolor(c, (U16)(rd2_hw_pal[c] + 0x100));
			if ((S16)(t & 0x070) > (S16)d3) rd2_sys_setcolor(c, (U16)(rd2_hw_pal[c] + 0x10));
			if ((S16)(t & 0x007) > d4)      rd2_sys_setcolor(c, (U16)(rd2_hw_pal[c] + 1));
		}
		d2 = (U16)(d2 - 0x100);
		d3 = (U16)(d3 - 0x10);
		if (--d4 == -1) break;
	}
}

/* $1919e fade out (8 steps) */
void
rd2_1919e(void)
{
	S16 d2;
	for (d2 = 7; ; ) {
		U16 c;
		rd2_191e6();
		for (c = 0; c < 16; c++) {
			if (rd2_hw_pal[c] & 0x700) rd2_sys_setcolor(c, (U16)(rd2_hw_pal[c] - 0x100));
			if (rd2_hw_pal[c] & 0x070) rd2_sys_setcolor(c, (U16)(rd2_hw_pal[c] - 0x10));
			if (rd2_hw_pal[c] & 0x007) rd2_sys_setcolor(c, (U16)(rd2_hw_pal[c] - 1));
		}
		if (--d2 == -1) break;
	}
}

/* ---- text ---- */

/* movep.l d,(off,a): the 4 bytes of d to a+off, +2, +4, +6 */
static void
movep(U32 a, U32 d)
{
	rd2_wb(a + 0, (U8)(d >> 24));
	rd2_wb(a + 2, (U8)(d >> 16));
	rd2_wb(a + 4, (U8)(d >> 8));
	rd2_wb(a + 6, (U8)d);
}

static U32
text_offset(U16 d0, U16 d1)
{
	U16 d7 = (U16)(d0 & 1);
	d0 &= 0xfffe;
	d7 = (U16)(d7 + (U16)(d0 << 2));
	d1 = (U16)(d1 << 8);
	d7 = (U16)(d7 + d1);
	d1 = (U16)(d1 * 4);
	d7 = (U16)(d7 + d1);
	return (U32)(S32)(S16)d7;                                /* adda.w */
}

/* $19272: glyph string a0 at column d0, row d1, into both screens $70000/$78000 */
void
rd2_19272(U16 d0, U16 d1, U32 a0)
{
	U32 a1 = 0x70000u + text_offset(d0, d1), a2 = a1 + 0x8000u;
	for (;;) {
		U8 g = rd2_rb(a0++);
		U32 a3, r;
		if (g == 0xff)
			return;
		a3 = 0x3ce54u + (U32)(S32)(S16)(U16)(g << 5);
		for (r = 0; r < 8; r++) {
			U32 d = rd2_rl(a3 + 4 * r);
			movep(a1 + 0xa0 * r, d);
			movep(a2 + 0xa0 * r, d);
		}
		a1 ^= 1;                                             /* bchg #0 ; beq ; addq.w #8 */
		a2 ^= 1;
		if (!(a2 & 1)) {
			a1 = (a1 & 0xffff0000u) | (U16)(a1 + 8);
			a2 = (a2 & 0xffff0000u) | (U16)(a2 + 8);
		}
	}
}

/* $19316: the same into the off-screen picture $68000 */
void
rd2_19316(U16 d0, U16 d1, U32 a0)
{
	U32 a1 = 0x68000u + text_offset(d0, d1);
	for (;;) {
		U8 g = rd2_rb(a0++);
		U32 a3, r;
		if (g == 0xff)
			return;
		a3 = 0x3ce54u + (U32)(S32)(S16)(U16)(g << 5);
		for (r = 0; r < 8; r++)
			movep(a1 + 0xa0 * r, rd2_rl(a3 + 4 * r));
		a1 ^= 1;
		if (!(a1 & 1))
			a1 = (a1 & 0xffff0000u) | (U16)(a1 + 8);
	}
}

/* $1925a: (word col, word row, long string) records until a negative col */
void
rd2_1925a(U32 a0)
{
	for (;;) {
		S16 d0 = rd2_rws(a0);
		if (d0 < 0)
			return;
		rd2_19272((U16)d0, rd2_rw(a0 + 2), rd2_rl(a0 + 4));
		a0 += 8;
	}
}

/* ---- clear / banner ---- */

static void
rd2_1939e(U32 a0)
{
	U32 i;
	for (i = 0; i < 32000; i++)
		rd2_wb(a0 + i, 0);
}

/* $19388: clear both screens */
void
rd2_19388(void)
{
	rd2_1939e(rd2_rl(0x18ede));
	rd2_1939e(rd2_rl(0x18eda));
}

/* $194ce: banner d0 (planes 0-1) into both screens */
void
rd2_194ce(U16 d0)
{
	U32 a0, a1 = rd2_rl(0x18eda), a2 = rd2_rl(0x18ede);
	S16 l, g;
	if (d0 == 3) {
		a1 += 0x3200;
		a2 += 0x3200;
	}
	a0 = 0x35a74u + (U32)(S32)(S16)(U16)(d0 * 0x600);
	for (l = 0x17; ; ) {
		a1 += 0x10;
		a2 += 0x10;
		for (g = 0x0f; ; ) {
			rd2_wl(a1, rd2_rl(a0));
			rd2_wl(a2, rd2_rl(a0));
			a0 += 4;
			a1 += 8;
			a2 += 8;
			if (--g == -1) break;
		}
		a1 += 0x10;
		a2 += 0x10;
		if (--l == -1) break;
	}
}

/* ---- sprites ---- */

/* the two blitters, per pixel (algo-render.md §3) */
static void
blit(U32 a6, S16 sx, S16 sy, U32 a0, U32 a1, int masked)
{
	S16 rows, skip = 0, r;
	int sil;

	if (rd2_rws(a6 + 0x10) < 0) {
		rd2_ww(a6 + 0x10, 0);
		if (rd2_rb(0x18edc) & 0x80)
			return;
	}
	sy = W(sy - (rd2_rw(0x16462) & 7));
	if (sy <= -13 || sy >= 0xc8 || sx < 0 || sx >= 0x120)
		return;
	if (sy < 8) {
		skip = W(8 - sy);
		rows = W(13 + sy);
		sy = 8;
	} else if (sy > 0xb3)
		rows = W(0xc8 - sy);
	else
		rows = 21;
	sil = rd2_rw(a6 + 0x10) != 0;
	if (sil)
		rd2_ww(a6 + 0x10, 0);
	for (r = 0; r < rows; r++) {
		U32 src = a0 + 16u * (U32)(skip + r);
		U32 p0 = rd2_rl(src), p1 = rd2_rl(src + 4), p2 = rd2_rl(src + 8), p3 = rd2_rl(src + 12);
		U32 line = a1 + 160u * (U32)(sy + r);
		U32 mline = 0x65800u + 40u * (U32)((rd2_rw(0x1662e) - 8 + sy + r) & 0xff);
		int i;
		for (i = 0; i < 32; i++) {
			U32 bit = 0x80000000u >> i;
			S16 x = W(sx + i);
			U32 ga;
			U16 m, w;
			int pl;
			if (x < 32 || x >= 288)
				continue;
			if (!((p0 | p1 | p2 | p3) & bit))
				continue;
			if (masked && !rd2_cheat_highlight &&               /* host: highlight cheat, as rd1 */
			    !(rd2_rb(mline + (U32)(x >> 3)) & (0x80 >> (x & 7))))
				continue;
			rd2_sys_hlpixel(a1, x, W(sy + r));
			ga = line + (U32)(x >> 4) * 8u;
			m = (U16)(0x8000 >> (x & 15));
			for (pl = 0; pl < 4; pl++) {
				U32 pv = pl == 0 ? p0 : pl == 1 ? p1 : pl == 2 ? p2 : p3;
				w = rd2_rw(ga + 2u * pl);
				if (sil || (pv & bit))
					w |= m;
				else
					w &= (U16)~m;
				rd2_ww(ga + 2u * pl, w);
			}
		}
	}
}

/* $17116 draw one record into the screen a1 */
static void
rd2_17116(U32 a6, U32 a1)
{
	U32 a0 = 0x37274u;
	U16 d7 = rd2_rw(a6 + 0x0e);
	S16 d0, d1;

	if (d7 == 0xfe)
		return;
	if ((S16)d7 >= 0xc0) {
		a0 = 0x5fe00u;
		d7 = (U16)(d7 - 0xc0);
	} else if ((S16)d7 >= 0x80) {
		a0 = 0x3a844u;
		d7 = (U16)(d7 - 0x80);
	} else if ((S16)d7 >= 0x40) {
		a0 = 0x5aa00u;
		d7 = (U16)(d7 - 0x40);
	}
	d7 = (U16)(d7 << 4);                                     /* ×16 +×64 +×256 = ×336 */
	a0 += (U32)(S32)(S16)d7;
	d7 = (U16)(d7 * 4);
	a0 += (U32)(S32)(S16)d7;
	d7 = (U16)(d7 * 4);
	a0 += (U32)(S32)(S16)d7;
	d0 = W(rd2_rws(a6 + 0x02) + 0x20);
	d1 = W(rd2_rws(a6 + 0x06) - 0x38);
	blit(a6, d0, d1, a0, a1, rd2_rw(a6 + 0x12) != 0);
}

/* $170d4: records with +14 == 0 */
static void
rd2_170d4(U32 a6, U32 a1)
{
	for (;; a6 += 0x58) {
		S16 st = rd2_rws(a6);
		if (st < 0) return;
		if (st != 0 && rd2_rw(a6 + 0x14) == 0)
			rd2_17116(a6, a1);
	}
}

/* $170f2: records with +14 != 0 (cleared) */
static void
rd2_170f2(U32 a6, U32 a1)
{
	for (;; a6 += 0x58) {
		S16 st = rd2_rws(a6);
		if (st < 0) return;
		if (st != 0 && rd2_rw(a6 + 0x14) != 0) {
			rd2_ww(a6 + 0x14, 0);
			rd2_17116(a6, a1);
		}
	}
}

static void
rd2_170ce(U32 a6, U32 a1)
{
	rd2_170d4(a6, a1);
	rd2_170f2(a6, a1);
}

void rd2_17086(void) { rd2_170ce(0x16d7cu, rd2_rl(0x18ede)); }  /* scene sprites */
void rd2_1709e(void) { rd2_170ce(0x167a2u, 0x68000u); }          /* into the off-screen picture */
void rd2_170b6(void) { rd2_170ce(0x167a2u, rd2_rl(0x18ede)); }

/* ---- background window ---- */

/* $165a6 partial: d0 = first tile row, d1 = rows; advances *a0 by 8, *a1 by 32 * rows */
static void
rd2_165a6(U32 *a0, U32 *a1, U16 d0, U16 d1)
{
	U32 a4 = 0x56d00u + (U32)((d0 & 3) << 2);
	U32 dst = *a1;
	U16 n = (U16)((d1 - 1) & 3);
	S16 d2;
	for (d2 = 7; ; ) {
		U32 a2 = a4 + (U32)(rd2_rb((*a0)++) << 4);
		U32 a3 = dst;
		S16 d4 = (S16)n;
		for (;;) {
			rd2_wl(a3, rd2_rl(a2));
			a2 += 4;
			a3 += 0x20;
			if (--d4 == -1) break;
		}
		dst += 4;
		if (--d2 == -1) break;
	}
	*a1 += (U32)((((d1 - 1) & 3) + 1) << 5);
}

/* $165fc full block row */
static void
rd2_165fc(U32 *a0, U32 *a1)
{
	S16 d2;
	for (d2 = 7; ; ) {
		U32 a2 = 0x56d00u + (U32)(rd2_rb((*a0)++) << 4);
		rd2_wl(*a1, rd2_rl(a2));
		*a1 += 4;
		rd2_wl(*a1 + 0x1c, rd2_rl(a2 + 4));
		rd2_wl(*a1 + 0x3c, rd2_rl(a2 + 8));
		rd2_wl(*a1 + 0x5c, rd2_rl(a2 + 12));
		if (--d2 == -1) break;
	}
	*a1 += 0x60;
}

/* $16474 window generation */
void
rd2_16474(void)
{
	U32 a0 = rd2_rl(0x1646a), a1 = 0x65300u;
	U16 d1, d0;
	S16 d2 = 9;

	d1 = (U16)((rd2_rw(0x16462) & 0xfff8) >> 3);
	d0 = d1;
	d1 = (U16)(((d1 & 0xfffc) >> 2) << 3);
	a0 += (U32)(S32)(S16)d1;
	rd2_wl(0x1646e, a0);
	d0 &= 3;
	rd2_ww(0x16472, d0);
	if (d0 != 0) {
		rd2_165a6(&a0, &a1, d0, (U16)(4 - d0));
		while (--d2 != -1)
			rd2_165fc(&a0, &a1);
	} else {
		do
			rd2_165fc(&a0, &a1);
		while (--d2 != -1);
	}
	if (rd2_rw(0x16472) != 0)
		rd2_165a6(&a0, &a1, 0, rd2_rw(0x16472));
}

/* $164de window down one row */
static void
rd2_164de(void)
{
	U32 a0, a1;
	U32 i;
	for (i = 0; i < 39 * 32; i++)
		rd2_wb(0x65300u + i, rd2_rb(0x65320u + i));
	a0 = rd2_rl(0x1646e) + 0x50;
	a1 = 0x657e0u;
	rd2_165a6(&a0, &a1, rd2_rw(0x16472), 1);
	rd2_ww(0x16472, (U16)((rd2_rw(0x16472) + 1) & 3));
	if (rd2_rw(0x16472) == 0)
		rd2_wl(0x1646e, rd2_rl(0x1646e) + 8);
}

/* $1653e window up one row */
static void
rd2_1653e(void)
{
	U32 a0, a1;
	S32 i;
	for (i = 38 * 32 + 31; i >= 0; i--)
		rd2_wb(0x65320u + (U32)i, rd2_rb(0x65300u + (U32)i));
	a0 = rd2_rl(0x1646e);
	rd2_ww(0x16472, (U16)((rd2_rw(0x16472) - 1) & 3));
	if (rd2_rw(0x16472) == 3) {
		a0 -= 8;
		rd2_wl(0x1646e, a0);
	}
	a1 = 0x65300u;
	rd2_165a6(&a0, &a1, rd2_rw(0x16472), 1);
}

/* ---- animated background tiles ---- */

/* $175c6 reset the slots */
static void
rd2_175c6(void)
{
	U32 a6;
	rd2_ww(0x17392, 0);
	rd2_wl(0x18da8, 0x17394u);
	for (a6 = 0x17394u; !(rd2_rb(a6) & 0x80); a6 += 0x0e)
		rd2_wb(a6, 0);
}

/* $175f2 register a slot for tile d0 (a0 window byte, a3 bitmap, a4 mask) */
static void
rd2_175f2(U8 d0, U32 a0, U32 a3, U32 a4)
{
	U32 a6;
	if (!(rd2_rws(0x17392) < 0x28))
		return;
	for (a6 = 0x17394u; ; a6 += 0x0e) {
		U8 f = rd2_rb(a6);
		if (f & 0x80) return;
		if (f != 0) continue;
		rd2_wb(a6, 1);
		d0 = (U8)(d0 + 8);                                   /* subi.b #-8 */
		d0 = (U8)(d0 << 2);
		rd2_18538();
		d0 = (U8)(d0 + (rd2_rb(0x18568) & 3));
		rd2_wb(a6 + 1, d0);
		rd2_wl(a6 + 2, a0);
		rd2_wl(a6 + 6, a3);
		rd2_wl(a6 + 10, a4);
		rd2_ww(0x17392, (U16)(rd2_rw(0x17392) + 1));
		return;
	}
}

/* $17644 (scroll up) / $17694 (scroll down): move the slots' window pointers */
static void
anim_shift(S32 delta)
{
	U32 a6;
	if (rd2_rw(0x17392) == 0)
		return;
	for (a6 = 0x17394u; ; a6 += 0x0e) {
		U8 f = rd2_rb(a6);
		S32 d0;
		if (f == 0) continue;
		if (f & 0x80) return;
		d0 = (S32)rd2_rl(a6 + 2) + delta;
		if (delta > 0 ? d0 >= 0x65740 : d0 < 0x65400) {
			rd2_wb(a6, 0);
			rd2_ww(0x17392, (U16)(rd2_rw(0x17392) - 1));
		} else
			rd2_wl(a6 + 2, (U32)d0);
	}
}

/* $18dac every frame: 8 slots from the cursor */
void
rd2_18dac(void)
{
	U32 a6;
	S16 d0;
	if (rd2_rw(0x17392) == 0)
		return;
	a6 = rd2_rl(0x18da8);
	for (d0 = 7; ; ) {
		U8 f = rd2_rb(a6);
		if (f & 0x80) {
			a6 = 0x17394u;
		} else {
			if (f != 0) {
				U8 d1 = rd2_rb(a6 + 1);
				U32 a0, a1 = rd2_rl(a6 + 6), a2 = rd2_rl(a6 + 10);
				int r;
				d1 = (U8)((d1 & 0xfc) | ((d1 + 1) & 3));
				rd2_wb(a6 + 1, d1);
				a0 = 0x5a500u + 40u * d1;
				for (r = 0; r < 8; r++) {
					rd2_wb(a1, rd2_rb(a0++));
					rd2_wb(a1 + 2, rd2_rb(a0++));
					rd2_wb(a1 + 4, rd2_rb(a0++));
					rd2_wb(a1 + 6, rd2_rb(a0++));
					rd2_wb(a2, rd2_rb(a0++));
					a1 += 0x80;
					a2 += 0x28;
				}
			}
			a6 += 0x0e;
		}
		if (--d0 == -1) break;
	}
	rd2_wl(0x18da8, a6);
}

/* ---- bitmap ---- */

/* $185a6 row renderer: a0 = 32 tile ids, a1 = bitmap line (128 pitch), a2 = mask line (40 pitch) */
static void
rd2_185a6(U32 a0, U32 a1, U32 a2)
{
	S16 d1;
	a2 += 4;
	for (d1 = 0x0f; ; ) {
		int half;
		for (half = 0; half < 2; half++) {
			U32 a3 = a1 + (U32)half, a4 = a2 + (U32)half, a5;
			U8 id = rd2_rb(a0);
			int r;
			if (id >= 0xf8)
				rd2_175f2(id, a0, a3, a4);
			a0++;
			a5 = 0x57d00u + 40u * id;
			for (r = 0; r < 8; r++) {
				rd2_wb(a3, rd2_rb(a5++));
				rd2_wb(a3 + 2, rd2_rb(a5++));
				rd2_wb(a3 + 4, rd2_rb(a5++));
				rd2_wb(a3 + 6, rd2_rb(a5++));
				rd2_wb(a4, rd2_rb(a5++));
				a3 += 0x80;
				a4 += 0x28;
			}
		}
		a1 += 8;
		a2 += 2;
		if (--d1 == -1) break;
	}
}

/* $1856a full render */
static void
rd2_1856a(void)
{
	S16 d2;
	U32 a0 = 0x65400u, a1 = 0x68000u, a2 = 0x65800u;
	rd2_185a6(0x653e0u, 0x6fc00u, 0x67ec0u);
	for (d2 = 0x19; ; ) {
		rd2_185a6(a0, a1, a2);
		a0 += 32;
		a1 += 0x400;
		a2 += 0x140;
		if (--d2 == -1) break;
	}
}

/* $16630 after a level start / transition */
void
rd2_16630(void)
{
	rd2_ww(0x1662c, 0);
	rd2_ww(0x1662e, (U16)(rd2_rw(0x16462) & 7));
	rd2_175c6();
	rd2_1856a();
}

/* $1884c (up) / $1888e (down): render one window row into the bitmap */
static void
rd2_1884c(void)
{
	U16 d0 = (U16)(((rd2_rw(0x1662e) & 0xf8) - 8) & 0xff);
	rd2_185a6(0x65400u, 0x68000u + 128u * d0, 0x65800u + 40u * d0);
}

static void
rd2_1888e(void)
{
	U16 d0 = (U16)(((rd2_rw(0x1662e) & 0xf8) + 0xd0) & 0xff);
	rd2_185a6(0x65720u, 0x68000u + 128u * d0, 0x65800u + 40u * d0);
}

/* $18818: d0 lines of 128 bytes from a0 (128 pitch) to a1 (160 pitch) */
static void
rd2_18818(U16 d0, U32 a0, U32 a1)
{
	S16 n = (S16)d0;
	while (--n != -1) {
		U32 i;
		for (i = 0; i < 128; i++)
			rd2_wb(a1 + i, rd2_rb(a0 + i));
		a0 += 128;
		a1 += 160;
	}
}

/* $18782 background to the draw screen */
void
rd2_18782(void)
{
	U16 s = rd2_rw(0x1662e);
	U16 e = (U16)((s + 0xc0) & 0xff);
	U32 dst = rd2_rl(0x18ede) + 0x510;
	if ((S16)e > (S16)s) {
		rd2_18818(0xc0, 0x68000u + (U32)(s << 7), dst);
		return;
	}
	rd2_18818(e, 0x68000u, dst + (U32)(S32)(S16)(U16)((0x100 - s) * 0xa0));
	rd2_18818((U16)(0x100 - s), 0x68000u + (U32)(S32)(S16)(U16)(s << 7), dst);
}

/* $16786 */
static void
rd2_16786(void)
{
	rd2_ww(0x1662e, (U16)((rd2_rw(0x1662e) + rd2_rw(0x1662c)) & 0xff));
}

/* $166ae scroll up */
static void
rd2_166ae(void)
{
	S16 d0 = rd2_rws(0x16462), d1 = d0;
	d0 = W(d0 + rd2_rws(0x1662c));
	if (d0 < rd2_rws(0x16466)) {
		rd2_ww(0x1662c, (U16)(rd2_rw(0x16466) - rd2_rw(0x16462)));
		d0 = rd2_rws(0x16466);
	}
	rd2_ww(0x16462, (U16)d0);
	if (d1 == d0)
		return;
	d1 = W((d1 & 7) + rd2_rws(0x1662c));
	if (d1 >= 0) {
		rd2_16786();
		return;
	}
	anim_shift(0x20);                                        /* $17644 */
	rd2_1653e();
	rd2_1884c();
	rd2_16786();
	rd2_171a4(8);
	rd2_14542();
}

/* $16718 scroll down */
static void
rd2_16718(void)
{
	S16 d0 = rd2_rws(0x16462), d1 = d0;
	d0 = W(d0 + rd2_rws(0x1662c));
	if (d0 > rd2_rws(0x16468)) {
		rd2_ww(0x1662c, (U16)(rd2_rw(0x16468) - rd2_rw(0x16462)));
		d0 = rd2_rws(0x16468);
	}
	rd2_ww(0x16462, (U16)d0);
	if (d1 == d0)
		return;
	d1 = W((d1 & 7) + rd2_rws(0x1662c));
	if (d1 <= 7) {
		rd2_16786();
		return;
	}
	anim_shift(-0x20);                                       /* $17694 */
	rd2_164de();
	rd2_1888e();
	rd2_16786();
	rd2_171a4(-8);
	rd2_14542();
}

/* $16658 every frame */
void
rd2_16658(void)
{
	S16 d1;
	if (rd2_rw(0x12e2a) != 0)
		return;
	d1 = W(rd2_rws(0x16960) - (rd2_rw(0x16462) & 7));
	if (d1 < 0x9b) {
		if (rd2_rws(0x1662c) < 0)
			rd2_166ae();
		return;
	}
	if (d1 < 0x86)
		return;
	if (rd2_rws(0x1662c) > 0)
		rd2_16718();
}

/* ---- transitions ---- */

/* $188d0 new picture ($68510, 160 pitch) and mask ($65804) */
static void
rd2_188d0(void)
{
	U32 a0 = 0x65400u, d3 = 0x68510u, a4 = 0x65804u, a2, a3, a5;
	S16 d2 = 0x17, d1, d6;
	U16 d4, d5;

	rd2_ww(0x1662e, 0);
	d4 = (U16)(rd2_rw(0x16462) & 7);
	if (d4 != 0) {                                           /* first partial row */
		d5 = (U16)(d4 * 5);
		d4 ^= 7;
		for (d1 = 0x1f; ; ) {
			a2 = 0x57d00u + 40u * rd2_rb(a0++) + d5;
			a3 = d3;
			a5 = a4;
			for (d6 = (S16)d4; ; ) {
				rd2_wb(a3, rd2_rb(a2++));
				rd2_wb(a3 + 2, rd2_rb(a2++));
				rd2_wb(a3 + 4, rd2_rb(a2++));
				rd2_wb(a3 + 6, rd2_rb(a2++));
				rd2_wb(a5, rd2_rb(a2++));
				a3 += 0xa0;
				a5 += 0x28;
				if (--d6 == -1) break;
			}
			d3 ^= 1;
			if (!(d3 & 1))
				SETW(d3, RW(d3) + 8);
			a4 += 1;
			if (--d1 == -1) break;
		}
		a4 += 8;
		SETW(d3, RW(d3) + 0x20);
		a4 += 40u * d4;
		SETW(d3, RW(d3) + 160u * d4);
		goto L18a5e;
	}
L18974:
	for (d1 = 0x1f; ; ) {                                    /* full rows */
		int r;
		a2 = 0x57d00u + 40u * rd2_rb(a0++);
		a3 = d3;
		a5 = a4;
		for (r = 0; r < 8; r++) {
			rd2_wb(a3, rd2_rb(a2++));
			rd2_wb(a3 + 2, rd2_rb(a2++));
			rd2_wb(a3 + 4, rd2_rb(a2++));
			rd2_wb(a3 + 6, rd2_rb(a2++));
			rd2_wb(a5, rd2_rb(a2++));
			a3 += 0xa0;
			a5 += 0x28;
		}
		d3 ^= 1;
		if (!(d3 & 1))
			SETW(d3, RW(d3) + 8);
		a4 += 1;
		if (--d1 == -1) break;
	}
	SETW(d3, RW(d3) + 0x480);
	a4 += 0x120;
L18a5e:
	if (--d2 != -1)
		goto L18974;
	d4 = (U16)(rd2_rw(0x16462) & 7);
	if (d4 == 0)
		return;
	for (d1 = 0x1f; ; ) {                                    /* last partial row: f+1 lines */
		a2 = 0x57d00u + 40u * rd2_rb(a0++);
		a3 = d3;
		a5 = a4;
		for (d6 = (S16)d4; ; ) {
			rd2_wb(a3, rd2_rb(a2++));
			rd2_wb(a3 + 2, rd2_rb(a2++));
			rd2_wb(a3 + 4, rd2_rb(a2++));
			rd2_wb(a3 + 6, rd2_rb(a2++));
			rd2_wb(a5, rd2_rb(a2++));
			a3 += 0xa0;
			a5 += 0x28;
			if (--d6 == -1) break;
		}
		d3 ^= 1;
		if (!(d3 & 1))
			SETW(d3, RW(d3) + 8);
		a4 += 1;
		if (--d1 == -1) break;
	}
}

static void
copy_bytes(U32 dst, U32 src, U32 n)
{
	U32 i;
	for (i = 0; i < n; i++)
		rd2_wb(dst + i, rd2_rb(src + i));
}

/* $18b5c left-exit slide step: new group from a0 into dest group 2, displayed groups 2..16 -> 3..17 */
static void
rd2_18b5c(U32 a0)
{
	U32 a1 = rd2_rl(0x18eda) + 0x510, a2 = rd2_rl(0x18ede) + 0x510;
	S16 d7;
	for (d7 = 0xbf; ; ) {
		copy_bytes(a2, a0, 8);
		copy_bytes(a2 + 8, a1, 120);
		a1 += 160;
		a0 += 160;
		a2 += 160;
		if (--d7 == -1) break;
	}
}

/* $18c50 right-exit slide step: displayed groups 3..17 -> 2..16, new group from a0 into dest group 17 */
static void
rd2_18c50(U32 a0)
{
	U32 a1 = rd2_rl(0x18eda) + 0x518, a2 = rd2_rl(0x18ede) + 0x510;
	S16 d7;
	for (d7 = 0xbf; ; ) {
		copy_bytes(a2, a1, 120);
		copy_bytes(a2 + 120, a0, 8);
		a1 += 160;
		a0 += 160;
		a2 += 160;
		if (--d7 == -1) break;
	}
}

static void
transition_common(U16 d0, U16 d1)
{
	rd2_18782();
	rd2_ww(0x1695a, 0);
	rd2_ww(0x16902, 0);
	rd2_170b6();
	rd2_ww(0x1695a, 1);
	rd2_ww(0x16b12, 0);
	rd2_14458(d0, d1);
	rd2_16474();
	rd2_188d0();
	rd2_157b4();
	rd2_149c2();
	rd2_14542();
	rd2_ww(0x14592, 0xffff);
	rd2_14594();
	rd2_ww(0x14592, 0);
	rd2_1709e();
	rd2_19216();
	rd2_191e6();
}

/* $18abe left exit */
void
rd2_18abe(U16 d0, U16 d1)
{
	U32 a0 = 0x68588u;
	S16 n;
	transition_common(d0, d1);
	rd2_ww(0x18ed8, 1);
	for (n = 0x0f; ; ) {
		rd2_18b5c(a0);
		a0 -= 8;
		rd2_19216();
		rd2_191e6();
		if (--n == -1) break;
	}
	rd2_ww(0x18ed8, 2);
	rd2_16630();
}

/* $18bb2 right exit */
void
rd2_18bb2(U16 d0, U16 d1)
{
	U32 a0 = 0x68510u;
	S16 n;
	transition_common(d0, d1);
	rd2_ww(0x18ed8, 1);
	for (n = 0x0f; ; ) {
		rd2_18c50(a0);
		a0 += 8;
		rd2_19216();
		rd2_191e6();
		if (--n == -1) break;
	}
	rd2_ww(0x18ed8, 2);
	rd2_16630();
}

/* $18caa copy the off-screen picture (192 lines from line 0, x 32) to the draw screen at (32, 8) */
void
rd2_18caa(void)
{
	U32 a0 = 0x68010u, a1 = rd2_rl(0x18ede) + 0x510;
	S16 d7;
	for (d7 = 0xbf; ; ) {
		copy_bytes(a1, a0, 128);
		a0 += 160;
		a1 += 160;
		if (--d7 == -1) break;
	}
}

/* eof */
