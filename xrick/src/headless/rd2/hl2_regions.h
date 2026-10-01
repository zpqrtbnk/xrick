/*
 * xrick/src/headless/rd2/hl2_regions.h
 *
 * xrick2-core only: the RD2 snapshot regions, { lo, hi, render, name }, [lo, hi).
 * From the T47 phase 1 write audit (xrick2-audit -audit 200000, all four maps; every
 * byte written after the boot, kb2/demo-solver.md §2), widened where the audit's
 * random play could not reach every path: the whole variable block $12e00-$17800
 * and every map's trigger/spawn tables. render = 1: drawing buffers only, left out
 * of the search snapshots and of the search key (hl2_stateRender).
 */

{ 0x10b5cu, 0x10b5eu, 0, "S key latch" },
{ 0x115dcu, 0x115e2u, 0, "shot hit, map done" },
{ 0x1239cu, 0x123a0u, 0, "map playing, map loaded" },
{ 0x12e00u, 0x17800u, 0, "game variables and the 17-record chain" },
{ 0x17896u, 0x1789au, 0, "score add" },
{ 0x17990u, 0x17996u, 0, "game length switch, picker rows and choice" },
{ 0x18180u, 0x18186u, 0, "scene runner" },
{ 0x184a8u, 0x184aau, 0, "$184a8" },
{ 0x18562u, 0x1856au, 0, "PRNG" },
{ 0x18da8u, 0x18dacu, 0, "animated tiles" },
{ 0x18ed8u, 0x18ee6u, 0, "frame rate, screen and palette pointers" },
{ 0x19232u, 0x19234u, 0, "VBL counter" },
{ 0x1a4fau, 0x1a4fdu, 0, "joysticks, last key" },
{ 0x1a5cau, 0x1a5d0u, 0, "IKBD flags, sound id remap" },
{ 0x54c00u, 0x56400u, 0, "submap headers, trigger and spawn tables" },
{ 0x65300u, 0x65800u, 0, "tile window" },
{ 0x65800u, 0x80000u, 1, "tile mask, background bitmap, screens" },
