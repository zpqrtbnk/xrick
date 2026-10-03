#!/usr/bin/env python3
"""
xrick/src/headless/rd2/rd2solve.py

xrick2-core only (branch `solver`, never shipped): solve RD2 maps side by side, one
xrick2-core chain per map, resumable (PLAN.md T47 phase 6b, kb2/demo-solver.md §6).

Maps are independent: a map played from a new `-map N` game plays as it does after
the maps before it (checked 2026-10-03 on map 2: steplog identical, only the score
differs). So each map has its own work dir <root>/mapN/:

  best.joy      the longest prefix found so far, from a new game on map N: checked by
                replay (Rick never dead, no life lost). Edit by hand to give a hint
                (MCP: load_joy best.joy map N, play, export script=false over it).
  status.json   where best.joy ends (submap, Rick, route cost to the map's end), the
                beam level, attempts, state: running / stuck / done
  logs/NNN.log  one per attempt

An attempt replays best.joy and runs `-chain 64 -onemap` from its end; what the chain
commits (legs, stages, switches) goes to run.joy, which becomes best.joy if it is
valid and gets closer to the map's end (smaller route cost, or the map done). The
search is deterministic, so an attempt without progress is retried with a wider
beam (BEAMS); after the widest the map is "stuck" and waits for a hint.

  python3 rd2solve.py [--maps 2,3,4] [--jobs 7] [--budget 110] [--root DIR]
  python3 rd2solve.py --status [--root DIR]

--budget: minutes of wall time, then the attempts in flight are stopped (their
committed frames still count) -- background jobs are limited to 2 h.
"""

import argparse
import json
import os
import shutil
import signal
import subprocess
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
BUILD = os.path.normpath(os.path.join(HERE, "..", "..", "..", "..", "build"))
CORE = os.environ.get("XRICK2_CORE", os.path.join(BUILD, "core2", "xrick2-core"))
BEAMS = (192, 384, 768)


def check(m, joy):
    """replay <joy> on a new game on map m: frames, valid (never dead, no life lost),
       where it ends, the route cost to the map's end (0 when the map is done)"""
    args = [CORE, "-map", str(m), "-steplog", "-distance"]
    if os.path.getsize(joy):
        args += ["-inputs", joy]
    else:
        args += ["-steps", "0"]
    out = subprocess.run(args, capture_output=True, text=True).stdout.splitlines()
    rows = [l.split() for l in out if len(l.split()) > 20 and l.split()[1] == "joy"]
    dist = next((l for l in out if l.startswith("distance:")), "distance: -1 (exit -1), route -1").split()
    r = {"frames": len(rows), "valid": True, "map": m, "submap": 0, "lives": None,
         "distance": int(dist[1]), "route": int(dist[-1])}
    if rows:
        lives0 = int(rows[0][16])
        r["valid"] = all(int(f[14]) == 0 and int(f[16]) >= lives0 for f in rows)
        last = rows[-1]
        r.update({"map": int(last[4]), "submap": int(last[6]), "x": int(last[10]),
                  "lives": int(last[16]), "laser": int(last[18]), "bombs": int(last[20])})
    r["done"] = r["map"] != m
    if r["done"]:
        r["route"] = 0
    return r


def better(new, old, log):
    """valid, longer, and closer to the map's end -- or as close after a switch the
       chain kept (it opened the way: the search after it got closer)"""
    if not new["valid"] or new["frames"] <= old["frames"]:
        return False
    if new["done"] or old["route"] < 0:
        return True
    if 0 <= new["route"] < old["route"]:
        return True
    with open(log) as f:
        return new["route"] == old["route"] and "switch kept" in f.read()


class Map:
    def __init__(self, root, m, jobs, deadline):
        self.m, self.jobs, self.deadline = m, jobs, deadline
        self.dir = os.path.join(root, "map%d" % m)
        os.makedirs(os.path.join(self.dir, "logs"), exist_ok=True)
        self.best = os.path.join(self.dir, "best.joy")
        if not os.path.exists(self.best):
            open(self.best, "wb").close()
        self.st = self.load()
        self.proc = None

    def load(self):
        try:
            with open(os.path.join(self.dir, "status.json")) as f:
                return json.load(f)
        except (OSError, ValueError):
            return {"map": self.m, "level": 0, "attempts": 0}

    def save(self, **kw):
        self.st.update(kw, updated=time.strftime("%Y-%m-%d %H:%M:%S"))
        tmp = os.path.join(self.dir, "status.json.tmp")
        with open(tmp, "w") as f:
            json.dump(self.st, f, indent=1)
        os.replace(tmp, os.path.join(self.dir, "status.json"))

    def run(self):
        cur = check(self.m, self.best)
        if not cur["valid"]:
            self.save(state="error", reason="best.joy loses a life", best=cur)
            return
        # an attempt cut short (this script stopped): what it committed may be progress
        run = os.path.join(self.dir, "run.joy")
        log = self.st.get("log")
        if os.path.exists(run) and log and os.path.exists(log):
            new = check(self.m, run)
            if better(new, cur, log):
                shutil.copyfile(self.best, os.path.join(self.dir, "best.prev.joy"))
                os.replace(run, self.best)
                cur = new
                self.save(level=0, best=cur, last_progress=time.strftime("%Y-%m-%d %H:%M:%S"))
        while time.time() < self.deadline - 60:
            if cur["done"]:
                self.save(state="done", best=cur)
                return
            level = self.st.get("level", 0)
            if level >= len(BEAMS):
                self.save(state="stuck", best=cur)
                return
            n = self.st.get("attempts", 0) + 1
            log = os.path.join(self.dir, "logs", "%03d.log" % n)
            run = os.path.join(self.dir, "run.joy")
            shutil.copyfile(self.best, run)
            self.save(state="running", attempts=n, beam=BEAMS[level], log=log, best=cur)
            args = [CORE, "-map", str(self.m), "-chain", "64", "-onemap", "-beam", str(BEAMS[level]),
                    "-maxsteps", "4000", "-jobs", str(self.jobs), "-v", "-out", run]
            args[3:3] = ["-inputs", self.best] if cur["frames"] else []
            with open(log, "w") as lf:
                self.proc = subprocess.Popen(args, stdout=lf, stderr=subprocess.STDOUT,
                                             start_new_session=True)
                try:
                    self.proc.wait(timeout=max(1, self.deadline - time.time()))
                except subprocess.TimeoutExpired:
                    os.killpg(self.proc.pid, signal.SIGKILL)
                    self.proc.wait()
                    lf.write("\nrd2solve: stopped at the time budget\n")
            new = check(self.m, run)
            with open(log, "a") as lf:
                lf.write("rd2solve: run.joy %s\nrd2solve: best.joy %s\n" % (json.dumps(new), json.dumps(cur)))
            if better(new, cur, log):
                shutil.copyfile(self.best, os.path.join(self.dir, "best.prev.joy"))
                os.replace(run, self.best)
                cur = new
                self.save(level=0, best=cur, last_progress=time.strftime("%Y-%m-%d %H:%M:%S"))
            elif time.time() < self.deadline - 60:     # not cut short: the beam did not do it
                self.save(level=level + 1, best=cur)
        self.save(state="paused", best=cur)


def status(root):
    for name in sorted(os.listdir(root)):
        p = os.path.join(root, name, "status.json")
        if name.startswith("map") and os.path.exists(p):
            with open(p) as f:
                s = json.load(f)
            b = s.get("best", {})
            print("map %s: %-7s %6s frames, submap %s x %s, route %s, distance %s, lives %s laser %s "
                  "bombs %s; beam %s, attempt %s, %s" % (
                      s.get("map"), s.get("state"), b.get("frames"), b.get("submap"), b.get("x"),
                      b.get("route"), b.get("distance"), b.get("lives"), b.get("laser"), b.get("bombs"),
                      s.get("beam"), s.get("attempts"), s.get("updated")))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--maps", default="2,3,4")
    ap.add_argument("--jobs", type=int, default=7)
    ap.add_argument("--budget", type=float, default=110, help="minutes")
    ap.add_argument("--root", default=os.path.join(BUILD, "rd2solve"))
    ap.add_argument("--status", action="store_true")
    ap.add_argument("--retry", action="store_true",
                    help="every map starts again at the first beam (after a solver change)")
    a = ap.parse_args()
    if a.status:
        status(a.root)
        return
    deadline = time.time() + a.budget * 60
    maps = [Map(a.root, int(m), a.jobs, deadline) for m in a.maps.split(",")]
    for mp in maps:
        if a.retry:
            mp.st["level"] = 0
    th = [threading.Thread(target=mp.run) for mp in maps]
    for t in th:
        t.start()
    for t in th:
        t.join()
    status(a.root)


if __name__ == "__main__":
    main()
