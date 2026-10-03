#!/usr/bin/env python3
"""
xrick/src/headless/rd2/bombprobe.py

xrick2-core only (branch `solver`, never shipped): brute-force a bomb at a switch from
the end of a .joy (PLAN.md T47 6b) -- drop or throw, after 0..12 frames walking either
way, then an escape (walk, jump, crouch, stand) of 6..30 frames, then 70 idle frames.
Prints every combination where the switch's record fired (its actor gone or reacted,
or the record spawned) and Rick never died. The .joy part to append is printed as
[mask, frames] pairs, as the MCP step tool takes them.

  python3 bombprobe.py <map> <file.joy> <spawn record> [--first]
"""

import subprocess
import sys

CORE = "/mnt/d/d/reverse/xrick/xrick/build/core2/xrick2-core"


def run(m, data, args):
    open("/tmp/bombprobe.joy", "wb").write(data)
    return subprocess.run([CORE, "-map", str(m), "-inputs", "/tmp/bombprobe.joy"] + args,
                          capture_output=True, text=True).stdout


def state(m, data, rec):
    v = run(m, data, ["-view", "0,0"])
    sw = [l for l in v.splitlines() if "switch" in l and "record %d" % rec in l]
    return ("spawn %d" % rec in v), (bool(sw) and "FIRED" in sw[0]), sw


def main():
    m, joy, rec = int(sys.argv[1]), sys.argv[2], int(sys.argv[3])
    first = "--first" in sys.argv
    base = open(joy, "rb").read()
    out0 = run(m, base, ["-steplog"])
    lives0 = [l.split() for l in out0.splitlines() if len(l.split()) > 20 and l.split()[1] == "joy"][-1][16]
    alive0, fired0, sw0 = state(m, base, rec)
    print("start: actor out %s, fired %s, %s" % (alive0, fired0, sw0))
    acts = {"drop": 0x82, "throw R": 0x8a, "throw L": 0x86}
    escs = {"stand": 0, "left": 4, "right": 8, "jump L": 5, "jump R": 9, "crouch": 2, "crawl L": 6, "crawl R": 10}
    for an, a in acts.items():
        for pre in range(-12, 13, 4):
            for en, e in escs.items():
                for n in ((6,) if e == 0 else (6, 12, 20, 30)):
                    tail = [[4 if pre < 0 else 8, abs(pre)], [0, 2], [a, 2], [e, n], [0, 70]]
                    data = base + b"".join(bytes([mm]) * k for mm, k in tail)
                    log = run(m, data, ["-steplog"])
                    rows = [l.split() for l in log.splitlines() if len(l.split()) > 20 and l.split()[1] == "joy"]
                    if any(r[14] != "0" or r[16] != lives0 for r in rows[len(base):]):
                        continue
                    alive, fired, sw = state(m, data, rec)
                    if (alive0 and not alive) or (fired and not fired0):
                        print("%s, walk %+d, %s %d: alive; actor gone %s, fired %s -> %s" %
                              (an, pre, en, n, alive0 and not alive, fired, [t for t in tail if t[1]]))
                        if first:
                            return


if __name__ == "__main__":
    main()
