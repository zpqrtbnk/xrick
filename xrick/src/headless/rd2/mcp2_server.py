#!/usr/bin/env python3
"""
xrick/src/headless/rd2/mcp2_server.py

xrick2-core only (branch `solver`, never shipped): an MCP server (stdio, JSON-RPC,
no dependencies) that lets an LLM drive the RD2 demo solver -- PLAN.md T47 phase 7,
kb2/demo-solver.md §5. The RD1 server (../mcp_server.py) is the model.

Every tool works on STATES: snapshot files of the game at a frame boundary, kept in a
work directory with their parent and the joystick bytes that led there. A state's
timeline (every frame from a new game on map 1) exports as src/rd2/dat_rd2_script.c
(joy2script.py) and as a .joy file (RD2_JOYSEQ, kb2/hatari_rd2_trace.py). The game
runs in xrick2-core (`make core2`), one process per call. Snapshots belong to the build
that wrote them; `validate` replays timelines from the new game, so a rebuild only
costs the stored snapshots, not the inputs.

Run (WSL):  python3 mcp2_server.py        env: XRICK2_CORE, XRICK2_MCP_WORK
"""

import base64
import json
import os
import struct
import subprocess
import sys
import tempfile
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
BUILD = os.path.join(HERE, "..", "..", "..", "..", "build")
CORE = os.environ.get("XRICK2_CORE", os.path.join(BUILD, "core2", "xrick2-core"))
WORK = os.environ.get("XRICK2_MCP_WORK", os.path.join(BUILD, "mcp2"))
JOBS = int(os.environ.get("XRICK2_JOBS", "8"))   # worker processes per search (-jobs)
os.makedirs(WORK, exist_ok=True)

# the joystick byte [$1a4fb] (algo-player.md §1)
CONTROLS = {"UP": 0x01, "DOWN": 0x02, "LEFT": 0x04, "RIGHT": 0x08, "FIRE": 0x80}


# --------------------------------------------------------------------------
# states

def _meta_path(sid):
    return os.path.join(WORK, sid + ".json")


def _snap_path(sid):
    return os.path.join(WORK, sid + ".snap")


def _inputs_path(sid):
    return os.path.join(WORK, sid + ".inputs")


def _new_id():
    n = 0
    for name in os.listdir(WORK):
        if name.startswith("s") and name.endswith(".json") and name[1:-5].isdigit():
            n = max(n, int(name[1:-5]))
    return "s%d" % (n + 1)


def _meta(sid):
    try:
        with open(_meta_path(sid)) as f:
            return json.load(f)
    except OSError:
        raise ValueError("no state '%s' (see list_states)" % sid)


def _save_meta(sid, meta):
    with open(_meta_path(sid), "w") as f:
        json.dump(meta, f)


def _drop(sid):
    for p in (_snap_path(sid), _inputs_path(sid), _meta_path(sid)):
        if os.path.exists(p):
            os.remove(p)


def _chain(sid, stop=None):
    """the states from the root (or after <stop>) to <sid>, oldest first"""
    out = []
    while sid is not None and sid != stop:
        out.append(sid)
        sid = _meta(sid).get("parent")
    if stop is not None and sid != stop:
        raise ValueError("'%s' is not an ancestor" % stop)
    return list(reversed(out))


def _timeline(sid):
    """every input byte from the root's new game to <sid>, and the root's map"""
    chain = _chain(sid)
    data = b""
    for s in chain:
        if os.path.exists(_inputs_path(s)):
            with open(_inputs_path(s), "rb") as f:
                data += f.read()
    return data, _meta(chain[0]).get("map", 1)


# --------------------------------------------------------------------------
# xrick2-core

def _run(args, timeout=3600):
    p = subprocess.run([CORE] + args, capture_output=True, text=True, timeout=timeout)
    return p.returncode, p.stdout, p.stderr


def _tmp(data, suffix=".inputs"):
    with tempfile.NamedTemporaryFile(dir=WORK, suffix=suffix, delete=False) as f:
        f.write(data)
        return f.name


def _dump(snap, extra=()):
    rc, out, err = _run(["-load", snap, "-steps", "0"] + list(extra) + ["-dump"])
    if rc != 0:
        raise RuntimeError("xrick2-core failed: " + (err.strip() or out.strip()))
    head, js = out[:out.index("{")], out[out.index("{"):]
    d = json.loads(js)
    return d, head.strip()


def _observe(sid, full):
    d, head = _dump(_snap_path(sid), ["-distance"])
    d["distance"] = head.splitlines()[0] if head else None
    if not full:
        for k in ("tiles", "legend", "spawns", "headers"):
            d.pop(k, None)
    d["state"] = sid
    return d


def _mask(m):
    if isinstance(m, int):
        return m & 0x8f
    v = 0
    for part in str(m).replace("+", "|").split("|"):
        part = part.strip().upper()
        if part in ("", "NONE", "0"):
            continue
        if part not in CONTROLS:
            raise ValueError("unknown control '%s' (UP DOWN LEFT RIGHT FIRE)" % part)
        v |= CONTROLS[part]
    return v


def _inputs_bytes(items):
    data = bytearray()
    for item in items:
        mask, count = (item if isinstance(item, (list, tuple)) else (item, 1))
        data += bytes([_mask(mask)]) * int(count)
    return bytes(data)


def _play(src_snap, data, save=None, steplog=False):
    """play <data> from a snapshot; the -steplog lines when asked"""
    tin = _tmp(data)
    args = ["-load", src_snap, "-inputs", tin]
    if save:
        args += ["-save", save]
    if steplog:
        args += ["-steplog"]
    try:
        rc, out, err = _run(args)
        if rc != 0:
            raise RuntimeError(err.strip() or out.strip())
        lines = []
        for l in out.splitlines():
            f = l.split()
            if len(f) > 20 and f[1] == "joy":   # see xrick2_core.c -steplog
                lines.append({"frame": int(f[0]), "joy": f[2], "map": int(f[4]), "sub": int(f[6]),
                              "scroll": int(f[8]), "x": int(f[10]), "y": int(f[12]),
                              "dead": int(f[14]), "lives": int(f[16]), "laser": int(f[18]),
                              "bombs": int(f[20]), "bomb": int(f[22]), "shot": int(f[24])})
        return lines, out.strip().splitlines()[-1:] if out.strip() else []
    finally:
        os.remove(tin)


def _summary(snap):
    d, _ = _dump(snap)
    r = d["rick"]
    return {"map": d["map"], "submap": d["submap"], "lives": d["lives"], "laser": d["laser"],
            "bombs": d["bombs"], "score": d["score"], "x": r["x"], "y": r["y"],
            "row": r["row"], "col": r["col"]}


def _same(a, b):
    return all(a[k] == b[k] for k in ("map", "submap", "lives", "laser", "bombs", "x", "y"))


# --------------------------------------------------------------------------
# tools

def t_new_game(a):
    m = int(a.get("map", 1))
    sid = _new_id()
    rc, out, err = _run(["-map", str(m), "-steps", "0", "-save", _snap_path(sid)])
    if rc != 0:
        raise RuntimeError(err.strip() or out.strip())
    _save_meta(sid, {"parent": None, "map": m, "steps": 0, "note": "new game, map %d" % m})
    return _observe(sid, a.get("full", True))


def t_load_joy(a):
    """a state from a .joy file played from a new game"""
    with open(a["file"], "rb") as f:
        data = f.read()
    root = t_new_game({"map": a.get("map", 1), "full": False})["state"]
    sid = _new_id()
    with open(_inputs_path(sid), "wb") as f:
        f.write(data)
    _play(_snap_path(root), data, save=_snap_path(sid))
    _save_meta(sid, {"parent": root, "steps": len(data), "note": a.get("note", "from " + a["file"])})
    return _observe(sid, a.get("full", False))


def t_observe(a):
    return _observe(a["state"], a.get("full", True))


def t_step(a):
    src = a["state"]
    _meta(src)
    data = _inputs_bytes(a["inputs"])
    sid = _new_id()
    with open(_inputs_path(sid), "wb") as f:
        f.write(data)
    _play(_snap_path(src), data, save=_snap_path(sid))
    _save_meta(sid, {"parent": src, "steps": len(data), "note": a.get("note", "step")})
    return _observe(sid, a.get("full", False))


def t_trace(a):
    src = a["state"]
    _meta(src)
    data = _inputs_bytes(a["inputs"])
    every = max(1, int(a.get("every", 1)))
    limit = int(a.get("max_lines", 300))
    while len(data) // every > limit:
        every *= 2
    sid = None
    if a.get("save"):
        sid = _new_id()
        with open(_inputs_path(sid), "wb") as f:
            f.write(data)
    lines, _ = _play(_snap_path(src), data, save=_snap_path(sid) if sid else None, steplog=True)
    if sid:
        _save_meta(sid, {"parent": src, "steps": len(data), "note": a.get("note", "trace")})
    keep = [l for i, l in enumerate(lines, 1) if i % every == 0 or i == len(lines)]
    return {"every": every, "lines": keep, "state": sid}


def _solve_args(a):
    args = ["-jobs", str(int(a.get("jobs", JOBS)))]
    if "stall" in a:
        args += ["-stall", str(int(a["stall"]))]
    for k, flag in (("beam", "-beam"), ("max_steps", "-maxsteps"), ("exit", "-exit"),
                    ("min_bombs", "-minbombs"), ("min_laser", "-minlaser")):
        if k in a:
            args += [flag, str(int(a[k]))]
    if a.get("waypoint"):
        args += ["-waypoint", "%d,%d" % tuple(int(v) for v in a["waypoint"])]
    if a.get("switches") is False:
        args += ["-noswitches"]
    return args


def t_solve(a):
    """xrick2-core -chain n: per leg search, else stages and switches (xrick2_core.c
       attempt). What it commits is kept even when the leg fails (partial)."""
    src = a["state"]
    _meta(src)
    sid = _new_id()
    args = ["-load", _snap_path(src), "-chain", str(int(a.get("legs", 1))),
            "-save", _snap_path(sid), "-out", _inputs_path(sid), "-v"] + _solve_args(a)
    rc, out, err = _run(args, timeout=int(a.get("timeout", 3600)))
    lines = [l for l in out.splitlines() if l.startswith("solve")]
    found = rc == 0 and not any("NOT FOUND" in l for l in lines)
    steps = os.path.getsize(_inputs_path(sid)) if os.path.exists(_inputs_path(sid)) else 0
    if steps == 0:
        _drop(sid)
        return {"found": False, "solver": lines,
                "progress": [l for l in err.splitlines() if "stage" in l or "switch" in l][-8:]}
    hints = {k: a[k] for k in ("beam", "max_steps", "exit", "min_bombs", "min_laser",
                               "waypoint", "switches", "legs") if k in a}
    _save_meta(sid, {"parent": src, "steps": steps, "solve": hints, "partial": not found,
                     "note": a.get("note", "solve" if found else "solve (partial: stages/switches only)")})
    res = _observe(sid, a.get("full", False))
    res.update({"found": found, "partial": not found, "solver": lines, "steps": steps})
    return res


def t_fire_switch(a):
    src = a["state"]
    _meta(src)
    sid = _new_id()
    args = ["-load", _snap_path(src), "-steps", "0", "-switch", str(int(a["index"])),
            "-save", _snap_path(sid), "-out", _inputs_path(sid)] + _solve_args(a)
    rc, out, err = _run(args, timeout=int(a.get("timeout", 3600)))
    steps = os.path.getsize(_inputs_path(sid)) if os.path.exists(_inputs_path(sid)) else 0
    if rc != 0 or steps == 0:
        _drop(sid)
        return {"fired": False, "core": out.strip().splitlines()[-3:]}
    _save_meta(sid, {"parent": src, "steps": steps, "note": a.get("note", "switch %s" % a["index"])})
    res = _observe(sid, a.get("full", False))
    res.update({"fired": True, "steps": steps})
    return res


def _state_map(sid):
    return _summary(_snap_path(sid))["map"]


def t_view(a):
    """-view: the tiles around Rick with what moves drawn in"""
    sid = a["state"]
    _meta(sid)
    up, down = int(a.get("up", 16)), int(a.get("down", 8))
    rc, out, err = _run(["-load", _snap_path(sid), "-steps", "0", "-view", "%d,%d" % (up, down)])
    if rc != 0:
        raise RuntimeError(err.strip() or out.strip())
    return {"text": "\n".join(l for l in out.splitlines() if not l.startswith("xrick2-core:"))}


def _png(ppm):
    """a binary PPM (P6) as PNG bytes"""
    head, rest = ppm.split(b"\n", 1)
    dims, rest = rest.split(b"\n", 1)
    _, px = rest.split(b"\n", 1)
    w, h = (int(v) for v in dims.split())
    raw = b"".join(b"\x00" + px[y * w * 3:(y + 1) * w * 3] for y in range(h))

    def chunk(t, b):
        return struct.pack(">I", len(b)) + t + b + struct.pack(">I", zlib.crc32(t + b) & 0xffffffff)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))


def t_shot(a):
    """-shot: the screen last shown, as an image (and a PNG file)"""
    sid = a["state"]
    _meta(sid)
    zoom = int(a.get("zoom", 2))
    ppm = os.path.join(WORK, sid + ".ppm")
    # the palette is the hardware registers, not in the snapshot: start on the state's map
    rc, out, err = _run(["-map", str(_state_map(sid)), "-load", _snap_path(sid), "-steps", "0",
                         "-shot", ppm, "-zoom", str(zoom)])
    if rc != 0 or not os.path.exists(ppm):
        raise RuntimeError(err.strip() or out.strip())
    with open(ppm, "rb") as f:
        png = _png(f.read())
    os.remove(ppm)
    path = a.get("file") or os.path.join(WORK, sid + ".png")
    with open(path, "wb") as f:
        f.write(png)
    return {"_content": [{"type": "image", "data": base64.b64encode(png).decode(), "mimeType": "image/png"},
                         {"type": "text", "text": json.dumps({"state": sid, "file": path})}]}


def t_list_states(a):
    rows = []
    for name in sorted(os.listdir(WORK), key=lambda n: (len(n), n)):
        if name.endswith(".json") and name.startswith("s") and name[1:-5].isdigit():
            sid = name[:-5]
            m = _meta(sid)
            rows.append({"state": sid, "parent": m.get("parent"), "steps": m.get("steps"),
                         "note": m.get("note")})
    return {"states": rows}


def t_validate(a):
    """legs: each leg replayed from its parent's stored snapshot; timeline: the whole
       chain replayed from the new game, compared at every state"""
    chain = _chain(a["state"])
    mode = a.get("mode", "timeline")
    rows, first = [], None
    tmp = os.path.join(WORK, "validate.snap")
    if mode == "legs":
        for p, s in zip(chain, chain[1:]):
            with open(_inputs_path(s), "rb") as f:
                data = f.read()
            _play(_snap_path(p), data, save=tmp)
            new, old = _summary(tmp), _summary(_snap_path(s))
            ok = _same(new, old)
            rows.append({"leg": "%s->%s" % (p, s), "ok": ok, **({} if ok else {"now": new, "was": old})})
            if not ok and first is None:
                first = s
    else:
        root = _new_root(chain[0])
        data = b""
        for s in chain[1:]:
            with open(_inputs_path(s), "rb") as f:
                data += f.read()
            _play(root, data, save=tmp)
            new, old = _summary(tmp), _summary(_snap_path(s))
            ok = _same(new, old)
            rows.append({"state": s, "ok": ok, **({} if ok else {"now": new, "was": old})})
            if not ok:
                first = s
                break  # everything after drifts too
        os.remove(root)
    if os.path.exists(tmp):
        os.remove(tmp)
    return {"mode": mode, "first_broken": first, "checked": len(rows),
            "broken": [r for r in rows if not r["ok"]], "ok": sum(r["ok"] for r in rows)}


def _new_root(root):
    """a fresh new-game snapshot for <root>'s map (written by this build, not stored)"""
    p = os.path.join(WORK, "root.snap")
    rc, out, err = _run(["-map", str(_meta(root).get("map", 1)), "-steps", "0", "-save", p])
    if rc != 0:
        raise RuntimeError(err.strip() or out.strip())
    return p


def _goal_reached(new, old, parent):
    """a replayed leg still does its job: same map and submap and lives, no less ammo
       (a leg that spends more leaves the next ones short), and inside a submap Rick
       within a tile of where the old leg ended"""
    if (new["map"], new["submap"]) != (old["map"], old["submap"]) or new["lives"] < old["lives"]:
        return False
    if new["bombs"] < old["bombs"] or new["laser"] < old["laser"]:
        return False
    if (old["map"], old["submap"]) != (parent["map"], parent["submap"]):
        return True
    return abs(new["x"] - old["x"]) <= 8 and abs(new["y"] - old["y"]) <= 8


def t_repair(a):
    """port the legs of an old chain (after <from>, up to <old>) onto a new parent:
       replay each; if it no longer does its job, re-solve it -- from <backoff> frames
       before where it went wrong, then from its start -- to the same goal (the next
       submap, or Rick's old end cell as a waypoint), keeping the old leg's ammo"""
    old_chain = _chain(a["old"], stop=a["from"])
    cur = a.get("onto", a["from"])
    backoff = int(a.get("backoff", 50))
    report = []
    for s in old_chain[:int(a.get("max_legs", len(old_chain)))]:
        meta = _meta(s)
        with open(_inputs_path(s), "rb") as f:
            data = f.read()
        old, start = _summary(_snap_path(s)), _summary(_snap_path(cur))
        sid = _new_id()
        lines, _ = _play(_snap_path(cur), data, save=_snap_path(sid), steplog=True)
        new = _summary(_snap_path(sid))
        if _goal_reached(new, old, start):
            with open(_inputs_path(sid), "wb") as f:
                f.write(data)
            _save_meta(sid, {"parent": cur, "steps": len(data),
                             "note": "repair of %s (replayed): %s" % (s, meta.get("note"))})
            report.append({"leg": s, "how": "replayed", "state": sid})
            cur = sid
            continue
        if os.path.exists(_snap_path(sid)):
            os.remove(_snap_path(sid))
        hints = dict(meta.get("solve", {}))
        hints.pop("legs", None)
        if (old["map"], old["submap"]) == (start["map"], start["submap"]):
            hints["waypoint"] = [old["row"], old["col"]]
        hints.setdefault("min_bombs", old["bombs"])
        hints.setdefault("min_laser", old["laser"])
        for k in ("beam", "timeout"):
            if k in a:
                hints[k] = a[k]
        loss = next((i for i, l in enumerate(lines, 1) if l["dead"] or l["lives"] < start["lives"]), None)
        cut = max(0, (loss if loss else len(data)) - backoff)
        tries = ([("prefix %d of %d frames, then solve" % (cut, len(data)), cut)] if cut else []) + \
                [("solve from the new parent", 0)]
        done = None
        for how, k in tries:
            base = cur
            if k:
                base = _new_id()
                with open(_inputs_path(base), "wb") as f:
                    f.write(data[:k])
                _play(_snap_path(cur), data[:k], save=_snap_path(base))
                if _summary(_snap_path(base))["lives"] < start["lives"]:
                    _drop(base)
                    continue
                _save_meta(base, {"parent": cur, "steps": k, "note": "repair of %s: first %d frames" % (s, k)})
            res = t_solve(dict(hints, state=base, note="repair of %s: %s" % (s, meta.get("note"))))
            if res.get("found"):
                done = (how, res["state"])
                break
            if res.get("state"):
                _drop(res["state"])
            if k:
                _drop(base)
        if not done:
            report.append({"leg": s, "how": "FAILED", "was": old, "replay_ended": new,
                           "first_loss_frame": loss, "hints": hints})
            return {"done": False, "last_good": cur, "report": report}
        report.append({"leg": s, "how": done[0], "state": done[1]})
        cur = done[1]
    return {"done": True, "last_good": cur, "report": report}


def t_export(a):
    """the timeline as a .joy (RD2_JOYSEQ / Hatari) and as src/rd2/dat_rd2_script.c"""
    sid = a["state"]
    data, m = _timeline(sid)
    if m != 1 and a.get("script", True):
        raise ValueError("this timeline starts on map %d; the script starts from a new game on map 1 "
                         "(script=false writes the .joy, replayed with xrick2-core -map %d)" % (m, m))
    joy = a.get("joy") or os.path.join(WORK, sid + ".joy")
    with open(joy, "wb") as f:
        f.write(data)
    res = {"joy": joy, "frames": len(data)}
    if a.get("script", True):
        out = a.get("file") or os.path.join(WORK, sid + "_dat_rd2_script.c")
        p = subprocess.run([sys.executable, os.path.join(HERE, "joy2script.py"), joy, out, CORE],
                           capture_output=True, text=True)
        if p.returncode != 0:
            raise RuntimeError(p.stderr.strip() or p.stdout.strip())
        res.update({"file": out, "maps": p.stdout.strip().splitlines()})
    return res


TOOLS = [
    ("new_game", t_new_game,
     "Start a new RD2 game on 'map' 1..4 (as xrick -game 2 -map N; the demo starts on map 1). "
     "Returns the first state, at the map's level start, as JSON (see observe).",
     {"map": {"type": "integer"}, "full": {"type": "boolean"}}, []),
    ("load_joy", t_load_joy,
     "Make a state by playing a .joy file (one joystick byte per frame) from a new game on "
     "'map' (default 1), e.g. build/rd2solve/map1_to_submap1.joy.",
     {"file": {"type": "string"}, "map": {"type": "integer"}, "note": {"type": "string"},
      "full": {"type": "boolean"}}, ["file"]),
    ("observe", t_observe,
     "The game at a state: map, submap, tick, lives/laser/bombs/score, scroll, rick (x, y, "
     "row = feet row and col = (x+4)>>3 in submap tiles -- the coordinates of waypoints --, vy, "
     "dead, ladder, crouch, ground, tunnel, facing), headers, exits (side, feet row, target, "
     "done = completes the map), records (objects 0-3, laser 4, debris 6-9, bomb 10, actors "
     "11-16 with their spawn record and hit class), the submap's spawns with trigger boxes, "
     "switches (boxes fired by shot 2 / bomb 4 / melee 8; index = what fire_switch takes), "
     "route_exit (the exit index the solver's route picks), distance, and the submap tiles "
     "(legend inside). full=false drops tiles, spawns, headers.",
     {"state": {"type": "string"}, "full": {"type": "boolean"}}, ["state"]),
    ("step", t_step,
     "Play inputs from a state: a list of [controls, frames], controls like 'RIGHT', "
     "'UP|LEFT' (jump left), 'FIRE|UP' (laser), 'FIRE|LEFT' (punch), 'FIRE|DOWN' (drop a "
     "bomb), 'FIRE|DOWN|RIGHT' (throw it: it slides ~54 px), 'NONE', or a joystick byte. "
     "One frame = one game_main frame. Makes a new state; returns it (full=false).",
     {"state": {"type": "string"}, "inputs": {"type": "array"}, "note": {"type": "string"},
      "full": {"type": "boolean"}}, ["state", "inputs"]),
    ("trace", t_trace,
     "Play inputs from a state (as step) and return one line per 'every' frames (doubled until "
     "at most max_lines, 300): frame, joy, map, sub, scroll, x, y, dead, lives, laser, bombs, "
     "bomb state, shot. save=true keeps the end as a new state.",
     {"state": {"type": "string"}, "inputs": {"type": "array"}, "every": {"type": "integer"},
      "max_lines": {"type": "integer"}, "save": {"type": "boolean"}, "note": {"type": "string"}},
     ["state", "inputs"]),
    ("solve", t_solve,
     "Run the solver from a state through 'legs' (1) exits: the route's exit, or 'exit' "
     "(index in observe's exits), or to 'waypoint' [row, col] (feet row, column; Rick on the "
     "ground there, within a column). When a search fails it commits its closest safe state "
     "(stages) and fires switches (switches=false: not); what it committed is kept as the new "
     "state even if the exit is still not reached (partial=true). Hints: beam (128), max_steps "
     "(3000), min_bombs, min_laser (ammo to still hold), jobs (worker processes, default 8 or env "
     "XRICK2_JOBS; same result for any number), stall (frames with no new closest distance before "
     "giving up, 600), timeout (s). Returns found, partial, "
     "the solver's lines, the new state.",
     {"state": {"type": "string"}, "legs": {"type": "integer"}, "exit": {"type": "integer"},
      "waypoint": {"type": "array"}, "beam": {"type": "integer"}, "max_steps": {"type": "integer"},
      "min_bombs": {"type": "integer"}, "min_laser": {"type": "integer"},
      "switches": {"type": "boolean"}, "jobs": {"type": "integer"}, "stall": {"type": "integer"}, "timeout": {"type": "integer"}, "note": {"type": "string"},
      "full": {"type": "boolean"}}, ["state"]),
    ("fire_switch", t_fire_switch,
     "Fire switch 'index' (observe's switches): solve to a spot next to its box, then punch, "
     "shoot, drop or throw a bomb until the box fires, Rick alive. Hints: beam, max_steps, jobs.",
     {"state": {"type": "string"}, "index": {"type": "integer"}, "beam": {"type": "integer"},
      "max_steps": {"type": "integer"}, "jobs": {"type": "integer"}, "stall": {"type": "integer"}, "timeout": {"type": "integer"}, "note": {"type": "string"},
      "full": {"type": "boolean"}}, ["state", "index"]),
    ("view", t_view,
     "The tiles around Rick ('up' rows above his feet row, 16; 'down' below, 8) with what moves "
     "drawn in: R Rick, o objects, * shot, B bomb, A-F actors (slots 11-16) over their boxes, "
     "a-z switch boxes (fire_switch index order), : touch boxes (traps, spawners Rick sets off), "
     "< > exits; a legend with every actor, switch and touch box. Tiles: ^ lethal, # solid, "
     "H ladder, T ladder top, = floor, ~ surface, . empty.",
     {"state": {"type": "string"}, "up": {"type": "integer"}, "down": {"type": "integer"}}, ["state"]),
    ("shot", t_shot,
     "The game screen at a state, as an image (320x200 times 'zoom', 2), also saved as a PNG "
     "('file', default <work>/<state>.png).",
     {"state": {"type": "string"}, "zoom": {"type": "integer"}, "file": {"type": "string"}}, ["state"]),
    ("list_states", t_list_states, "All states: id, parent, frames from the parent, note.", {}, []),
    ("validate", t_validate,
     "Check a state's chain under the current xrick2-core. mode 'timeline' (default): the "
     "whole chain from a new game, compared at every state (map, submap, lives, ammo, rick "
     "x/y); 'legs': each leg from its parent's stored snapshot. Returns first_broken.",
     {"state": {"type": "string"}, "mode": {"type": "string"}}, ["state"]),
    ("repair", t_repair,
     "Port the legs of an old chain -- after 'from', up to 'old' -- onto 'onto' (default "
     "'from'): each leg replayed, kept if it still does its job (same map/submap/lives, no "
     "less ammo; inside a submap Rick within a tile of the old end), else re-solved to the same "
     "goal from 'backoff' (50) frames before where it went wrong, then from its start. Stops "
     "at the first leg it cannot repair. Options: max_legs, beam, timeout.",
     {"old": {"type": "string"}, "from": {"type": "string"}, "onto": {"type": "string"},
      "backoff": {"type": "integer"}, "max_legs": {"type": "integer"},
      "beam": {"type": "integer"}, "timeout": {"type": "integer"}}, ["old", "from"]),
    ("export", t_export,
     "Write a state's timeline (a new game on its root's map to the state) as a .joy (default "
     "<work>/<state>.joy, RD2_JOYSEQ / Hatari input) and, unless script=false, as "
     "dat_rd2_script.c (joy2script.py; default <work>/<state>_dat_rd2_script.c; map-1 roots only).",
     {"state": {"type": "string"}, "joy": {"type": "string"}, "file": {"type": "string"},
      "script": {"type": "boolean"}}, ["state"]),
]


# --------------------------------------------------------------------------
# MCP over stdio: newline-delimited JSON-RPC 2.0

def _reply(mid, result=None, error=None):
    msg = {"jsonrpc": "2.0", "id": mid}
    if error is not None:
        msg["error"] = error
    else:
        msg["result"] = result
    sys.stdout.write(json.dumps(msg) + "\n")
    sys.stdout.flush()


def _handle(msg):
    method, mid = msg.get("method"), msg.get("id")
    if method == "initialize":
        v = msg.get("params", {}).get("protocolVersion", "2025-06-18")
        _reply(mid, {"protocolVersion": v, "capabilities": {"tools": {}},
                     "serverInfo": {"name": "xrick2-solver", "version": "0.1"}})
    elif method == "tools/list":
        _reply(mid, {"tools": [{"name": n, "description": d,
                                "inputSchema": {"type": "object", "properties": p, "required": r}}
                               for n, _, d, p, r in TOOLS]})
    elif method == "tools/call":
        p = msg.get("params", {})
        fn = {n: f for n, f, _, _, _ in TOOLS}.get(p.get("name"))
        if not fn:
            _reply(mid, error={"code": -32602, "message": "unknown tool"})
            return
        try:
            res = fn(p.get("arguments") or {})
            if isinstance(res, dict) and "_content" in res:
                _reply(mid, {"content": res["_content"]})
            elif isinstance(res, dict) and set(res) == {"text"}:
                _reply(mid, {"content": [{"type": "text", "text": res["text"]}]})
            else:
                _reply(mid, {"content": [{"type": "text", "text": json.dumps(res)}]})
        except Exception as e:  # reported to the model, not a protocol error
            _reply(mid, {"content": [{"type": "text", "text": "error: %s" % e}], "isError": True})
    elif method == "ping":
        _reply(mid, {})
    elif mid is not None:
        _reply(mid, error={"code": -32601, "message": "method not found"})


def main():
    for line in sys.stdin:
        line = line.strip()
        if line:
            try:
                _handle(json.loads(line))
            except json.JSONDecodeError:
                _reply(None, error={"code": -32700, "message": "parse error"})


if __name__ == "__main__":
    main()
