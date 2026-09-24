/*
 * xrick/src/rd2/rd2_objects.c
 *
 * Rick Dangerous 2 -- the 4-slot object table, transliterated at register level from the
 * disassembly ($150a2-$156ee, $1570e-$1573e; read 2026-09-24; kb2/algo-objects.md).
 * d1 carries y as a 16.16 value and is swapped as in the original; labels are the
 * original addresses.
 */

#include "rd2_mem.h"
#include "rd2_cpu.h"
#include "rd2_game.h"
#include "rd2_collide.h"

#define KIND(a6)  rd2_rw((a6) + 0x00)

/* $1570e: at x & $f == 4, 1 chance in 4 (PRNG) -- carry */
static int
rd2_1570e(U32 a6)
{
	if ((rd2_rb(a6 + 0x03) & 0x0f) != 4)
		return 0;
	rd2_18538();
	return (rd2_rb(0x18569) & 3) == 0;
}

/* $150c0 update one object */
static void
rd2_150c0(U32 a6)
{
	U32 d0, d1, d2 = 0, d3, d5, d6 = 0;
	U8 d7;

	if (rd2_rw(a6 + 0x4c) != 0)
		goto L155e6;
	rd2_ww(a6 + 0x40, 0);
	rd2_ww(a6 + 0x4a, 0);
	if (rd2_rw(a6 + 0x56) != 0)
		rd2_ww(a6 + 0x56, (U16)(rd2_rw(a6 + 0x56) - 1));
	if (rd2_rw(a6 + 0x4e) != 0)
		goto L1568e;
	if (rd2_rw(a6 + 0x50) != 0)
		goto L15590;
	rd2_ww(a6 + 0x42, 0);
	rd2_ww(a6 + 0x44, 0);
	rd2_ww(a6 + 0x46, 0);
	rd2_ww(a6 + 0x48, 0);
	d1 = (U32)(U16)(rd2_rw(a6 + 0x0c) + 0x80);                /* upper bits cleared by ext.l below */
	if (RWS(d1) > 0x800)
		SETW(d1, 0x800);
	rd2_ww(a6 + 0x0c, RW(d1));
	rd2_ww(RD2_PVY, RW(d1));
	EXTL(d1);
	d1 <<= 8;                                                /* asl.l #8 */
	d1 += rd2_rl(a6 + 0x06);
	SWAP(d1);
	if (RWS(d1) < 0 || RWS(d1) > 0x12b)
		goto L156ea;
	rd2_ww(RD2_PY, RW(d1));
	rd2_ww(RD2_PX, rd2_rw(a6 + 0x02));
	rd2_ww(RD2_PACT, 0xffff);
	rd2_15fba();

L15156:
	d7 = rd2_rb(RD2_PRES);
	if (d7 & 0x20)
		goto L155e6;
	if (!(d7 & 0x40))
		goto L151da;
	d1 = 0;
	SETW(d1, rd2_rw(RD2_PPLY) - 0x15);
	rd2_ww(RD2_PY, RW(d1));
	rd2_ww(a6 + 0x42, 1);
	rd2_ww(a6 + 0x4a, rd2_rw(RD2_PPDX));
	rd2_ww(RD2_PACT, 0);
	rd2_15fba();
	d7 = rd2_rb(RD2_PRES);
	if (d7 & 0x20)
		goto L155e6;
	if ((d7 & 0x06) != 0) {
		d7 = rd2_rb(RD2_PRES);
		goto L151ee;
	}
	if ((rd2_rw(RD2_PPDX) | rd2_rw(RD2_PPDY)) != 0)
		rd2_ww(a6 + 0x40, 1);
	d7 = rd2_rb(RD2_PRES);
	rd2_ww(a6 + 0x0c, 0x100);
	goto L152a0;

L151da:
	if (d7 & 0x04)
		goto L15206;
	if (!(d7 & 0x02))
		goto L152c8;
	if (rd2_rws(a6 + 0x0c) >= 0)
		goto L15206;
L151ee:                                                      /* head hit: y to the next tile row */
	SETB(d1, RB(d1) | 7);
	SETW(d1, RW(d1) + 1);
	SWAP(d1);
	SETW(d1, 0);
	SWAP(d1);
	rd2_ww(a6 + 0x0c, 0x100);
	goto L152c8;

L15206:                                                      /* floor */
	if (KIND(a6) == 3 && (d7 & 0x10) && !(RWS(d1) > rd2_rws(0x16960)) &&
	    (rd2_rb(a6 + 0x03) & 0x1f) == 4) {
		rd2_ww(a6 + 0x50, 1);
		rd2_ww(a6 + 0x0c, 0x100);
		goto L152c8;
	}
	rd2_ww(a6 + 0x50, 0);
	rd2_ww(a6 + 0x42, 1);
	SETW(d1, RW(d1) + 0x0c);
	SETB(d1, RB(d1) | 7);
	SETW(d1, RW(d1) - 0x14);
	SWAP(d1);
	SETW(d1, 0);
	SWAP(d1);
	rd2_ww(a6 + 0x0c, 0x100);
	if ((d7 & 0x01) && rd2_rw(RD2_MAP_PLAYING) == 3) {
		SETW(d6, rd2_rw(a6 + 0x0c));
		if (RWS(d6) >= 0x200) {
			SETW(d6, (U16)(-RWS(d6)));
			SETW(d6, RW(d6) + 0x60);
			rd2_ww(a6 + 0x0c, RW(d6));
			rd2_1a6aa(0x19, 0);
			goto L152c8;
		}
		rd2_ww(a6 + 0x0c, 0x100);
		goto L152c8;
	}
L152a0:
	if ((d7 & 0x01) && rd2_rw(RD2_MAP_PLAYING) == 5) {
		rd2_1a6aa(0x1a, 0);
		rd2_ww(a6 + 0x0c, 0xf781);                           /* -$87f */
	}
L152c8:
	SWAP(d1);
	rd2_wl(a6 + 0x06, d1);
	SWAP(d1);
	if (KIND(a6) == 3 && (d7 & 0x08) && !(d7 & 0x10) && RWS(d1) > rd2_rws(0x16960) &&
	    (rd2_rb(a6 + 0x03) & 0x1f) == 4)
		rd2_ww(a6 + 0x50, 1);

	/* $15300: horizontal step */
	if (rd2_rw(a6 + 0x56) != 0 || rd2_rw(a6 + 0x40) != 0)
		goto L15328;
	if (rd2_rws(a6 + 0x0c) >= 0 && rd2_rw(a6 + 0x42) == 0)
		goto L15328;
	if (KIND(a6) == 3 && rd2_rw(a6 + 0x50) != 0)
		goto L15328;
	goto L15332;
L15328:
	d6 = 0;
	SETW(d2, rd2_rw(a6 + 0x02));
	goto L153b4;
L15332:
	SETW(d6, rd2_rw(a6 + 0x0a));
	if (d7 & 0x01) {
		if (rd2_rw(RD2_MAP_PLAYING) == 2)
			goto L15360;
		if (rd2_rw(RD2_MAP_PLAYING) == 4) {
			rd2_ww(a6 + 0x46, 1);
			d6 = 1;
			if (rd2_rws(a6 + 0x0a) < 0) {                    /* bpl $15366 skips the neg and the +$48 */
				SETW(d6, (U16)(-RWS(d6)));
			L15360:
				rd2_ww(a6 + 0x48, 1);
			}
		}
	}
	if (KIND(a6) == 1) {
		SETW(d5, rd2_rw(a6 + 0x52) + 1);
		if (RW(d5) == rd2_rw(a6 + 0x54)) {
			rd2_ww(a6 + 0x52, 0);
			SETW(d6, (U16)(-RWS(d6)));
		} else
			rd2_ww(a6 + 0x52, RW(d5));
	} else if (KIND(a6) == 2) {
		if (rd2_1570e(a6)) {
			d6 = 2;
			if (!(rd2_rws(a6 + 0x02) < rd2_rws(0x1695c)))
				SETW(d6, 0xfffe);
		}
	} else if (rd2_1570e(a6))
		SETW(d6, (U16)(-RWS(d6)));
L153b4:
	SETW(d6, RW(d6) + rd2_rw(a6 + 0x4a));
	SETW(d2, rd2_rw(a6 + 0x02));
	SETW(d2, RW(d2) + RW(d6));
	if (RWS(d6) != 0) {
		if (RWS(d6) < 0) {
			if (RWS(d2) < 0) {
				d2 = 0;
				SETW(d6, (U16)(-RWS(d6)));
				rd2_ww(a6 + 0x52, 0);
			}
		} else if (RWS(d2) >= 0xe8) {
			SETW(d2, 0xe8);
			SETW(d6, (U16)(-RWS(d6)));
			rd2_ww(a6 + 0x52, 0);
		}
	}
	rd2_ww(RD2_PY, rd2_rw(a6 + 0x06));
	rd2_ww(RD2_PVY, 0xffff);
	rd2_ww(RD2_PX, RW(d2));
	rd2_ww(RD2_PACT, 0xffff);
	rd2_15fba();
	d7 = rd2_rb(RD2_PRES);
	if (d7 & 0x20)
		goto L155e6;
	if (d7 & 0x02) {                                         /* wall: back off to the tile edge, turn round */
		if (RWS(d6) < 0)
			SETW(d2, RW(d2) + 0x0c);
		else
			SETW(d2, RW(d2) + 0x03);
		SETB(d2, RB(d2) & 0xf8);
		SETW(d2, RW(d2) - 4);
		SETW(d6, (U16)(-RWS(d6)));
		rd2_ww(a6 + 0x52, 0);
	}
	rd2_ww(a6 + 0x02, RW(d2));
	if (RW(d6) != 0)
		rd2_ww(a6 + 0x0a, RW(d6));
	if (KIND(a6) == 3 && !(d7 & 0x08))
		rd2_ww(a6 + 0x50, 0);

	/* $1546e: frame */
	if (rd2_rw(a6 + 0x40) != 0)
		goto L154be;
	if (rd2_rw(a6 + 0x50) != 0) {
		rd2_ww(a6 + 0x0e, 8);
		if ((rd2_rw(a6 + 0x06) ^ rd2_rw(a6 + 0x02)) & 4)
			rd2_ww(a6 + 0x0e, 9);
		goto L15510;
	}
	if (rd2_rw(a6 + 0x42) == 0) {
		rd2_ww(a6 + 0x0e, 2);
		rd2_ww(a6 + 0x22, 0);
		goto L15504;
	}
	if (rd2_rw(a6 + 0x56) == 0 && rd2_rw(a6 + 0x44) == 0) {
		U32 a0 = 0x1485au;
		d1 = 1;
		if (rd2_rw(a6 + 0x46) == 0) {
			d1 = 4;
			if (rd2_rw(a6 + 0x48) == 0)
				d1 = 2;
		}
		SETW(d1, RW(d1) + rd2_rw(a6 + 0x22));
		if (RWS(d1) >= 0x20)
			SETW(d1, RW(d1) - 0x20);
		rd2_ww(a6 + 0x22, RW(d1));
		SETW(d1, RW(d1) >> 2);                               /* lsr.w #2 */
		a0 += (U32)(S32)RWS(d1);
		d1 = rd2_rb(a0);
		rd2_ww(a6 + 0x0e, RW(d1));
		goto L15504;
	}
L154be:
	rd2_ww(a6 + 0x0e, 1);
	rd2_ww(a6 + 0x22, 0);
L15504:
	if (rd2_rws(a6 + 0x0a) < 0)
		rd2_ww(a6 + 0x0e, (U16)(rd2_rw(a6 + 0x0e) + 3));
L15510:
	rd2_ww(a6 + 0x0e, (U16)(rd2_rw(a6 + 0x0e) + rd2_rw(a6 + 0x3e)));

	/* hits */
	d0 = (U16)(rd2_rw(a6 + 0x02) + 4);
	d1 = rd2_rw(a6 + 0x06);
	d2 = 0x10;
	d3 = 0x15;
	if (rd2_14c5a(RWS(d0), RWS(d1), RWS(d2), RWS(d3))) {    /* melee: stunned */
		rd2_ww(a6 + 0x56, 0x28);
		rd2_ww(a6 + 0x10, 1);
		rd2_1a6aa(0x18, 0);
	} else if (rd2_14b7a(RWS(d0), RWS(d1), RWS(d2), RWS(d3)))
		rd2_ww(0x12e2c, 0xffff);
	if (rd2_14bee(RWS(d0), RWS(d1), RWS(d2), RWS(d3))) {
		rd2_ww(0x16902, 0);
		goto L155e6;
	}
	if (!rd2_14c20((S16)(RWS(d0) - 0x10), (S16)(RWS(d1) - 0x0e), (S16)(RWS(d2) + 0x20), (S16)(RWS(d3) + 0x1d)))
		return;
	rd2_17810(0x50);
	goto L155e6;

L15590:                                                      /* climbing */
	rd2_wb(a6 + 0x03, rd2_rb(a6 + 0x03) & 0xfe);
	rd2_wb(a6 + 0x07, rd2_rb(a6 + 0x07) & 0xfe);
	d1 = rd2_rw(a6 + 0x06);
	d6 = 0;
	if (rd2_rw(a6 + 0x56) == 0) {
		d5 = (U16)((rd2_rw(0x16960) + 1) & 0xfffe);
		if (RW(d1) != RW(d5)) {
			if (RWS(d1) > RWS(d5))
				SETW(d6, 0xfffe);
			else
				d6 = 2;
		}
	}
	rd2_ww(a6 + 0x0c, RW(d6));
	SETW(d1, RW(d1) + RW(d6));
	rd2_ww(RD2_PVY, RW(d6));
	rd2_ww(RD2_PY, RW(d1));
	rd2_ww(RD2_PX, rd2_rw(a6 + 0x02));
	rd2_15fba();
	goto L15156;

L155e6:                                                      /* destroyed */
	if (rd2_rw(a6 + 0x4c) != 0) {
		rd2_17810(0x100);
		rd2_ww(a6 + 0x4c, 0);
	}
	rd2_ww(a6 + 0x4e, 0xffff);
	rd2_ww(a6 + 0x12, 0);
	rd2_ww(a6 + 0x22, 0);
	rd2_ww(a6 + 0x08, 0);
	rd2_ww(a6 + 0x0c, 0xfb00);                               /* -$500 */
	rd2_ww(a6 + 0x0a, 2);
	rd2_17810(0x50);
	if (rd2_rws(a6 + 0x06) >= 0x23 && rd2_rws(a6 + 0x06) <= 0x148) {
		U32 a0 = 0x156f0u;
		U16 m = (U16)((rd2_rw(RD2_MAP_PLAYING) - 1) * 2);
		a0 += (U32)(S32)(S16)(U16)(m + m * 2);               /* ×6 */
		if (rd2_rb(a6 + 0x3f) & 0x80)
			d0 = rd2_rw(a0 + 4);
		else if (rd2_rw(a6 + 0x3e) == 0x41)
			d0 = rd2_rw(a0);
		else
			d0 = rd2_rw(a0 + 2);
		rd2_1a6aa(RW(d0), 0);
	}
	if (rd2_rws(a6 + 0x02) >= 0x74)
		rd2_ww(a6 + 0x0a, 0xfffe);

L1568e:                                                      /* dying: fly off */
	rd2_ww(a6 + 0x02, (U16)(rd2_rw(a6 + 0x02) + rd2_rw(a6 + 0x0a)));
	d1 = rd2_rw(a6 + 0x22);
	SETB(d1, (RB(d1) + 1) & 3);
	rd2_ww(a6 + 0x22, RW(d1));
	SETB(d1, RB(d1) >> 1);
	SETW(d1, RW(d1) + 6);
	SETW(d1, RW(d1) + rd2_rw(a6 + 0x3e));
	rd2_ww(a6 + 0x0e, RW(d1));
	rd2_ww(a6 + 0x14, 0xffff);
	d1 = (U16)(rd2_rw(a6 + 0x0c) + 0x80);
	if (RWS(d1) > 0x800)
		d1 = 0x800;
	rd2_ww(a6 + 0x0c, RW(d1));
	EXTL(d1);
	d1 <<= 8;
	d1 += rd2_rl(a6 + 0x06);
	rd2_wl(a6 + 0x06, d1);
	if (rd2_rws(a6 + 0x06) >= 0x12b)
		rd2_14a12(a6);
	return;

L156ea:
	rd2_149f0(a6);
}

/* $150a2 update objects */
void
rd2_150a2(void)
{
	U32 a6;
	S16 d7;
	for (a6 = 0x167a2u, d7 = 3; ; a6 += 0x58) {
		if (rd2_rw(a6) != 0)
			rd2_150c0(a6);
		if (--d7 == -1) break;
	}
}

/* eof */
