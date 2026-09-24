/*
 * xrick/src/rd2/rd2_flow.c
 *
 * Rick Dangerous 2 -- title/attract, picker, game over, hall of fame, name entry, the
 * scene runner, transliterated from the disassembly ($178dc-$1798c, $17a46-$17bb4,
 * $17bda-$17d0c, $17e40-$17e9c, $17f22-$1817e, $18186-$18514 (the runner's first bytes
 * and $183fe read raw), $18d00; read 2026-09-24; kb2/algo-flow.md §3, §4, §8, §11,
 * algo-render.md §10).
 */

#include "rd2_mem.h"
#include "rd2_game.h"
#include "rd2_sys.h"
#include "rd2_snd.h"

#define W(v)   ((S16)(v))
#define FIRE() (rd2_rb(RD2_JOY) & 0x80)

void rd2_19316(U16 d0, U16 d1, U32 a0);

/* $1795c tree-coded depacker: a0 = packed data (long count, $3fc-byte tree, bit stream), a1 = dest */
static void
rd2_1795c(U32 a0, U32 a1)
{
	U32 d0 = rd2_rl(a0), a2, a3;
	S16 d1 = 0;
	U16 d2 = 0;
	a0 += 4;
	a2 = a0 + 0x3fc;
	do {
		a3 = a0;
		for (;;) {
			S16 d3;
			if (--d1 < 0) {
				d1 = 0x0f;
				d2 = rd2_rw(a2);
				a2 += 2;
			}
			if (d2 & 0x8000)                                 /* add.w d2,d2 ; bcc */
				a3 += 2;
			d2 = (U16)(d2 << 1);
			d3 = rd2_rws(a3);
			if (d3 < 0) {
				rd2_wb(a1++, (U8)d3);
				break;
			}
			a3 += (U32)(S32)d3;
		}
	} while (--d0 != 0);
}

/* $1793a title picture */
static void
rd2_1793a(void)
{
	rd2_1919e();
	rd2_1795c(0x31eb0u, rd2_rl(0x18eda));
	rd2_19134();
}

/* $17cd8 space bar: toggle the grey palette */
static void
rd2_17cd8(void)
{
	if (rd2_rb(RD2_KEY) != 0x39)
		return;
	rd2_1919e();
	if (rd2_rl(0x18ee2) == 0x18f06u)
		rd2_wl(0x18ee2, 0x18ee6u);
	else
		rd2_wl(0x18ee2, 0x18f06u);
	rd2_19134();
}

/* $17c86 wait up to 176 frames or a new fire press (carry) */
static int
rd2_17c86(void)
{
	S16 d7;
	for (d7 = 0xaf; ; ) {
		rd2_17cd8();
		rd2_191e6();
		if (rd2_snd_rw(0x1aa08) == 0)
			rd2_1a6aa(0, 1);
		if (FIRE()) {
			if (rd2_rw(0x17938) == 0)
				return 1;
		} else
			rd2_ww(0x17938, 0);
		if (--d7 == -1) break;
	}
	return 0;
}

/* $17e40 hall of fame view */
static void
rd2_17e40(void)
{
	U32 a0;
	S16 d0, d1;
	rd2_1919e();
	rd2_19388();
	rd2_194ce(1);
	for (a0 = 0x17d20u, d0 = 7; ; a0 += 0x1e) {
		U32 a1 = a0;
		for (d1 = 9; ; ) {
			rd2_wb(a1, (U8)(rd2_rb(a1) - 1));
			a1++;
			if (--d1 == -1) break;
		}
		if (--d0 == -1) break;
	}
	rd2_1925a(0x17dfeu);
	rd2_19134();
	for (a0 = 0x17d20u, d0 = 7; ; a0 += 0x1e) {
		U32 a1 = a0;
		for (d1 = 9; ; ) {
			rd2_wb(a1, (U8)(rd2_rb(a1) + 1));
			a1++;
			if (--d1 == -1) break;
		}
		if (--d0 == -1) break;
	}
}

/* $178dc title / attract */
void
rd2_178dc(void)
{
	rd2_ww(0x17938, 0);
	if (FIRE())
		rd2_ww(0x17938, 0xffff);
	if (rd2_snd_rw(0x1aa08) == 0)
		rd2_1a6aa(3, 0);
	rd2_1793a();
	if (rd2_17c86()) return;
	rd2_17e40();
	if (rd2_17c86()) return;
	rd2_1793a();
	if (rd2_17c86()) return;
	rd2_17e40();
	if (rd2_17c86()) return;
	rd2_ww(RD2_DEMO, 0xffff);
}

/* $17b86 cheat text */
static void
rd2_17b86(void)
{
	if (rd2_rw(0x1798e) == 0)
		return;
	rd2_19272(0x0c, 0x15, rd2_rw(0x17990) != 0 ? 0x17bc8u : 0x17bb6u);
}

/* $17a46 level picker */
void
rd2_17a46(void)
{
	S16 d1, d2;

	rd2_ww(RD2_PICKER_CHOICE, 1);
	rd2_ww(0x17990, 0);
	if (rd2_rw(RD2_DEMO) != 0) {
		rd2_ww(RD2_PICKER_CHOICE, rd2_rw(RD2_MAP_LOADED));
		return;
	}
	if (rd2_rw(RD2_PICKER_ROWS) == 0)
		return;
	rd2_1a5d0();
	rd2_ww(0x17b84, 0);
	if (FIRE())
		rd2_ww(0x17b84, 0xffff);
	rd2_1919e();
	rd2_19388();
	rd2_194ce(2);
	rd2_1925a(0x17996u + (U32)(S32)(S16)(U16)((5 - rd2_rw(RD2_PICKER_ROWS)) << 3));
	rd2_17b86();
	rd2_19134();
	d2 = 1;
	d1 = 7;
	for (;;) {
		rd2_19272(5, (U16)d1, 0x17e9eu);
		rd2_191e6();
		rd2_191e6();
		rd2_191e6();
		rd2_191e6();
		if (FIRE()) {
			if (rd2_rw(0x17b84) != 0)
				continue;
			rd2_ww(RD2_PICKER_CHOICE, (U16)d2);
			return;
		}
		if (rd2_rw(0x1798e) != 0) {
			if (rd2_rb(RD2_JOY) & 0x04)
				rd2_ww(0x17990, 0);
			else if (rd2_rb(RD2_JOY) & 0x08)
				rd2_ww(0x17990, 0xffff);
			rd2_17b86();
		}
		rd2_ww(0x17b84, 0);
		rd2_19272(5, (U16)d1, 0x17ea0u);
		if (rd2_rb(RD2_JOY) & 0x01) {
			d2--;
			d1 = W(d1 - 3);
			if (!(d2 >= 1)) {
				d2 = 1;
				d1 = 7;
			}
		} else if (rd2_rb(RD2_JOY) & 0x02) {
			d2++;
			d1 = W(d1 + 3);
			if (!(d2 <= rd2_rws(RD2_PICKER_ROWS))) {
				d2--;
				d1 = W(d1 - 3);
			}
		}
	}
}

/* $17bda ending: sound 9 and scene 2, unless demo */
void
rd2_17bda(void)
{
	if (rd2_rw(RD2_DEMO) != 0)
		return;
	rd2_1a6aa(9, 0);
	rd2_18186(2);
}

/* $17bf4 scene 1, unless demo */
void
rd2_17bf4(void)
{
	if (rd2_rw(RD2_DEMO) != 0)
		return;
	rd2_18186(1);
}

/* $17c06 game over */
void
rd2_17c06(void)
{
	S16 d0;
	rd2_1919e();
	rd2_19388();
	rd2_1a6aa(2, 0);
	rd2_ww(0x176f0, 0xffff);
	rd2_ww(0x176f2, 0xffff);
	rd2_ww(0x17700, 0xffff);
	rd2_ww(0x1770e, 0xffff);
	rd2_177a8();
	rd2_19272(0x0f, 0x0c, 0x17c7cu);
	rd2_19134();
	do
		rd2_191e6();
	while (rd2_snd_rw(0x1aa08) != 0);
	for (d0 = 0x3c; ; ) {
		rd2_191e6();
		if (FIRE()) break;
		if (--d0 == -1) break;
	}
	rd2_1a5d0();
}

/* $1813e name entry redraw: grid cursor, name, name cursor */
static void
rd2_1813e(S16 d2, S16 d3, S16 d7)
{
	rd2_19272((U16)((d2 << 1) + 0x0e), (U16)((d3 << 1) + 9), 0x17e9eu);
	rd2_19272(0x0e, 0x14, 0x17f16u);
	rd2_19272((U16)(d7 + 0x0e), 0x15, 0x17e9eu);
}

/* $17f22 hall-of-fame entry */
void
rd2_17f22(void)
{
	U16 d0 = rd2_rw(0x176e8);
	U32 d1 = rd2_rl(0x176ea), a1 = 0x17d0eu, a2;
	S16 d2, d3, d7;

	for (d2 = 7; ; ) {
		S16 e = rd2_rws(a1 + 2);
		if ((S16)d0 > e) break;
		if (!((S16)d0 < e) && !((S32)d1 < rd2_rls(a1 + 4))) break;
		a1 += 0x1e;
		if (--d2 == -1) return;
	}
	if (d2 != 0) {                                           /* shift the lower entries down */
		a2 = 0x17de0u;
		for (d3 = W(d2 - 1); ; ) {
			rd2_wl(a2, rd2_rl(a2 - 0x1e));
			rd2_wl(a2 + 4, rd2_rl(a2 - 0x1a));
			rd2_wl(a2 + 0x12, rd2_rl(a2 - 0x0c));
			rd2_wl(a2 + 0x16, rd2_rl(a2 - 0x08));
			rd2_ww(a2 + 0x1a, rd2_rw(a2 - 0x04));
			a2 -= 0x1e;
			if (--d3 == -1) break;
		}
	}
	rd2_ww(a1 + 2, d0);
	rd2_wl(a1 + 4, d1);
	rd2_1919e();
	rd2_19388();
	rd2_194ce(0);
	rd2_19272(9, 5, 0x17ea2u);
	{
		U32 a0 = 0x17ebau;
		U16 row = 8;
		S16 n;
		for (n = 4; ; ) {
			rd2_19272(0x0e, row, a0);
			row = (U16)(row + 2);
			a0 += 0x0c;
			if (--n == -1) break;
		}
	}
	rd2_wl(0x17f16, 0x0e0e0e0eu);
	rd2_wl(0x17f1a, 0x0e0e0e0eu);
	rd2_ww(0x17f1e, 0x0e0e);
	d2 = 5;
	d3 = 4;
	d7 = 0;
	rd2_19134();
	for (;;) {
		U8 d4;
		rd2_1813e(d2, d3, d7);
		rd2_191e6();
		rd2_191e6();
		rd2_191e6();
		rd2_191e6();
		d4 = rd2_rb(RD2_JOY);
		if (d4 & 0x80) {
			U8 d5 = rd2_rb(0x17ef7u + (U32)(U16)(d3 * 6 + d2));
			if (d5 == 0x11)
				break;                                       /* END */
			rd2_19272((U16)(d7 + 0x0e), 0x15, 0x17ea0u);
			a2 = 0x17f16u + (U32)(S32)d7;
			if (d5 == 0x10) {                                /* delete */
				rd2_wb(a2, 0x0e);
				if (d7 != 0)
					d7--;
			} else {
				rd2_wb(a2, d5);
				if (d7 != 9)
					d7++;
			}
			do {                                             /* $18080: spins on the fire bit, which the */
				rd2_1813e(d2, d3, d7);                       /* IKBD interrupt updates on the ST; in the */
				rd2_sys_pump();                              /* port input arrives in the pump */
			} while (FIRE());
			continue;
		}
		{
			S16 d5 = d2, d6 = d3;
			if (d4 & 0x04) {
				if (d2 != 0) d2--;
			} else if (d4 & 0x08) {
				if (d2 != 5) d2++;
			}
			if (d4 & 0x01) {
				if (d3 != 0) d3--;
			} else if (d4 & 0x02) {
				if (d3 != 4) d3++;
			}
			rd2_19272((U16)((d5 << 1) + 0x0e), (U16)((d6 << 1) + 9), 0x17ea0u);
		}
	}
	/* $180f8: store the name (glyph + 1, blanks as $21) */
	a1 += 0x12;
	a2 = 0x17f16u;
	if (rd2_rl(0x17f16) == 0x504f4f4bu && rd2_rl(0x17f1a) == 0x590e0e0eu)
		rd2_ww(0x1798e, 0xff);                               /* "POOKY" */
	for (d2 = 9; ; ) {
		U8 d3b = rd2_rb(a2++);
		if (d3b == 0x0e)
			d3b = 0x20;
		rd2_wb(a1++, (U8)(d3b + 1));
		if (--d2 == -1) break;
	}
}

/* ---- scenes ---- */

/* $18508: scene record d2 */
static U32 scene_rec(U16 d2) { return 0x16d7cu + (U32)(S32)(S16)(U16)(d2 * 0x58); }

/* $18d00 op 5 image: a0 = op record, a1 = image record */
static void
rd2_18d00(U32 a0, U32 a1)
{
	U16 c = rd2_rw(a0 + 4), r = rd2_rw(a0 + 6);
	U16 d7 = (U16)(c & 1);
	U32 a2;
	S16 d0, d1;
	c &= 0xfffe;
	d7 = (U16)(d7 + (U16)(c << 2));
	r = (U16)(r << 8);
	d7 = (U16)(d7 + r);
	r = (U16)(r * 4);
	d7 = (U16)(d7 + r);
	a2 = 0x68000u + (U32)(S32)(S16)d7;
	a1 += 8;
	a1 += (U32)(S32)rd2_rws(a0 + 8);
	a1 += (U32)(S32)(S16)(U16)(rd2_rw(a0 + 0x0a) << 5);
	for (d1 = W(rd2_rw(a0 + 0x0e) - 1); ; ) {
		U32 a3 = a1, a5 = a2;
		for (d0 = W(rd2_rw(a0 + 0x0c) - 1); ; ) {
			U32 a4 = 0x3ce54u + (U32)(S32)(S16)(U16)(rd2_rb(a3++) << 5);
			int k;
			for (k = 0; k < 8; k++) {
				U32 d = rd2_rl(a4 + 4u * k);
				rd2_wb(a5 + 0xa0u * k + 0, (U8)(d >> 24));
				rd2_wb(a5 + 0xa0u * k + 2, (U8)(d >> 16));
				rd2_wb(a5 + 0xa0u * k + 4, (U8)(d >> 8));
				rd2_wb(a5 + 0xa0u * k + 6, (U8)d);
			}
			a5 ^= 1;
			if (!(a5 & 1))
				a5 = (a5 & 0xffff0000u) | (U16)(a5 + 8);
			if (--d0 == -1) break;
		}
		a1 += 0x20;
		a2 += 0x500;
		if (--d1 == -1) break;
	}
}

/* $183ac op 9: run n frames */
static void
rd2_183ac(U32 a0)
{
	S16 d2 = W(rd2_rw(a0 + 2) - 1);
	for (;;) {
		U32 a6;
		for (a6 = 0x16d7cu; ; a6 += 0x58) {
			S16 st = rd2_rws(a6);
			if (st < 0) break;
			if (st == 0) continue;
			if (rd2_rl(a6 + 0x16) != 0) {
				S16 dx, dy;
				rd2_1726e(a6, &dx, &dy);
				rd2_ww(a6 + 2, (U16)(rd2_rw(a6 + 2) + (U16)dx));
				rd2_ww(a6 + 6, (U16)(rd2_rw(a6 + 6) + (U16)dy));
			} else {
				rd2_ww(a6 + 2, (U16)(rd2_rw(a6 + 2) + rd2_rw(a6 + 0x0a)));
				rd2_ww(a6 + 6, (U16)(rd2_rw(a6 + 6) + rd2_rw(a6 + 0x0c)));
			}
			rd2_171bc(a6);
		}
		if (rd2_rw(0x18180) == 2)
			goto DRAW;
		if (rd2_rw(0x18180) == 1) {
			if (!FIRE()) {
				rd2_ww(0x18182, 0);
				goto DRAW;
			}
			if (rd2_rw(0x18182) != 0)
				goto DRAW;
			rd2_ww(0x18184, 0xffff);
			return;
		}
		rd2_ww(0x184a8, (U16)(rd2_rw(0x184a8) + 1));
		if (rd2_rw(0x184a8) == 7) {
			rd2_ww(0x184a8, 0);
			goto DRAW;
		}
		if (FIRE())
			goto NEXT;
	DRAW:
		rd2_18caa();
		rd2_17086();
		rd2_19216();
		rd2_191e6();
		if (rd2_rw(0x18180) == 0 && rd2_rb(RD2_KEY) == 0x19) {
			do
				rd2_191e6();
			while (!FIRE());
			rd2_191e6();
		}
	NEXT:
		if (--d2 == -1)
			return;
	}
}

/* $18186 run_scene(d0) */
void
rd2_18186(U16 d0)
{
	U32 a0, a2, a3;

	if (rd2_rw(RD2_DEMO) != 0)
		return;
	rd2_ww(0x18180, d0);
	rd2_ww(0x18184, 0);
	rd2_ww(0x18182, 0);
	if (FIRE())
		rd2_ww(0x18182, 0xffff);
	rd2_ww(0x16462, 0);
	rd2_ww(0x176f0, 0xffff);
	rd2_ww(0x176f2, 0xffff);
	rd2_ww(0x17700, 0xffff);
	rd2_ww(0x1770e, 0xffff);
	rd2_177a8();
	rd2_1704c(0x16d7cu);
	a0 = 0x55400u + (U32)rd2_extw(rd2_rw(0x55402u + (U32)(S32)(S16)(U16)(d0 * 2)));
	for (;;) {
		U16 op = rd2_rw(a0);
		if (op == 0)
			return;
		switch (op) {
		case 1:                                              /* $1826a sprite on */
			a2 = scene_rec(rd2_rw(a0 + 2));
			rd2_ww(a2, 1);
			rd2_ww(a2 + 2, rd2_rw(a0 + 4));
			rd2_ww(a2 + 6, rd2_rw(a0 + 6));
			rd2_wl(a2 + 0x16, 0);
			rd2_ww(a2 + 0x1a, 0);
			rd2_ww(a2 + 0x0a, 0);
			rd2_ww(a2 + 0x0c, 0);
			rd2_ww(a2 + 0x0e, 0);
			rd2_wl(a2 + 0x1e, 0);
			rd2_ww(a2 + 0x22, 0);
			a0 += 8;
			break;
		case 2:                                              /* $182b6 sprite off */
			rd2_ww(scene_rec(rd2_rw(a0 + 2)), 0);
			a0 += 4;
			break;
		case 3:                                              /* $182c8 velocity */
			a2 = scene_rec(rd2_rw(a0 + 2));
			rd2_wl(a2 + 0x16, 0);
			rd2_ww(a2 + 0x0a, rd2_rw(a0 + 4));
			rd2_ww(a2 + 0x0c, rd2_rw(a0 + 6));
			a0 += 8;
			break;
		case 4:                                              /* $182ea animation */
			a2 = scene_rec(rd2_rw(a0 + 2));
			rd2_wl(a2 + 0x1e, 0x55400u + (U32)rd2_extw(rd2_rw(a0 + 4)));
			rd2_ww(a2 + 0x22, 0);
			a0 += 6;
			break;
		case 5:                                              /* $1830c image */
			if (rd2_rw(a0 + 2) == 0) {
				U32 i;
				for (i = 0; i < 32000; i++)
					rd2_wb(0x68000u + i, 0);
				a0 += 4;
				break;
			}
			a3 = 0x55400u;
			a3 += (U32)rd2_extw(rd2_rw(a3));
			a3 += 2u * (U32)(S32)(S16)(U16)(rd2_rw(a0 + 2) - 1);
			rd2_18d00(a0, 0x55400u + (U32)rd2_extw(rd2_rw(a3)));
			a0 += 0x10;
			break;
		case 6:                                              /* $1834e text */
			rd2_19316(rd2_rw(a0 + 2), rd2_rw(a0 + 4), a0 + 6);
			a3 = a0 + 6;
			while (rd2_rb(a3) != 0xff)
				a3++;
			a3++;
			if (a3 & 1)
				a3++;
			a0 = a3;
			break;
		case 7:                                              /* $1838a */
		case 12:                                             /* $18502 */
			a0 += 4;
			break;
		case 8:                                              /* $18390 sound */
			rd2_1a6aa(rd2_rw(a0 + 2), 0);
			a0 += 4;
			break;
		case 9:                                              /* $183ac frames */
			rd2_183ac(a0);
			a0 += 4;
			break;
		case 10:                                             /* $184aa movement script */
			a2 = scene_rec(rd2_rw(a0 + 2));
			rd2_wl(a2 + 0x16, 0x55400u + (U32)rd2_extw(rd2_rw(a0 + 4)));
			rd2_ww(a2 + 0x1a, 0);
			rd2_ww(a2 + 0x1c, 0);
			a0 += 6;
			break;
		case 11:                                             /* $184d2 */
			a0 += 0x10;
			break;
		case 13:                                             /* $184dc call */
			rd2_wl(0x184d8, a0 + 4);
			a0 = 0x55400u + (U32)rd2_extw(rd2_rw(a0 + 2));
			break;
		case 14:                                             /* $184fa return */
			a0 = rd2_rl(0x184d8);
			break;
		default:
			/* the original jumps through long[$18232 + 4(op-1)] for any op. The decoded
			   scene data (kb2/assets/levels/scenes.json, all 4 maps) uses only ops 0-6,
			   9 and 10, so this is unreachable with the shipped data; an op outside the
			   14-entry table would jump into unknown code, so the port stops instead. */
			sys_printf("xrick/rd2: scene opcode %u outside the table at $%x\n", op, a0);
			return;
		}
		if (rd2_rw(0x18184) != 0)
			return;
	}
}

/* eof */
