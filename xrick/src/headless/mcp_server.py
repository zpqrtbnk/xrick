#!/usr/bin/env python3
"""
xrick/src/headless/mcp_server.py

xrick-core only (branch `solver`, never shipped): an MCP server (stdio, JSON-RPC,
no dependencies) that lets an LLM drive the RD1 demo solver -- PLAN.md T43 phase 7,
kb/demo-solver.md §12.

Every tool works on STATES: snapshot files of the game at a step boundary, kept in
a work directory with their parent and the inputs that led there. A state's
timeline (every input from the new game) can be exported as src/rd1/dat_demo.c.
The game itself runs in xrick-core (built by `make core`), one process per call.

Run (WSL):  python3 mcp_server.py        env: XRICK_CORE, XRICK_MCP_WORK
"""

import json
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
CORE = os.environ.get("XRICK_CORE", os.path.join(HERE, "..", "..", "xrick-core"))
WORK = os.environ.get("XRICK_MCP_WORK", os.path.join(HERE, "..", "..", "build.mcp"))
os.makedirs(WORK, exist_ok=True)

CONTROLS = {"UP": 0x08, "DOWN": 0x04, "LEFT": 0x02, "RIGHT": 0x01, "FIRE": 0x10}


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


def _timeline(sid):
    """every input byte from the new game to <sid>, and the root's -submap"""
    parts, meta = [], _meta(sid)
    while True:
        if os.path.exists(_inputs_path(sid)):
            with open(_inputs_path(sid), "rb") as f:
                parts.append(f.read())
        if meta.get("parent") is None:
            break
        sid = meta["parent"]
        meta = _meta(sid)
    return b"".join(reversed(parts)), meta.get("submap_arg")


# --------------------------------------------------------------------------
# xrick-core

def _run(args, timeout=3600):
    p = subprocess.run([CORE] + args, capture_output=True, text=True, timeout=timeout)
    return p.returncode, p.stdout, p.stderr


def _observe(sid, full):
    """the state as JSON, from xrick-core -dump, plus -distance"""
    rc, out, err = _run(["-reseed", "-load", _snap_path(sid), "-steps", "0",
                         "-distance", "-dump"])
    if rc != 0:
        raise RuntimeError("xrick-core failed: " + err.strip())
    first, _, js = out.partition("\n")
    d = json.loads(js[js.index("{"):])
    d["distance"] = first.strip()
    if not full:
        for k in ("tiles", "tiles_legend", "marks"):
            d.pop(k, None)
    d["state"] = sid
    return d


def _mask(m):
    if isinstance(m, int):
        return m & 0x1f
    v = 0
    for part in str(m).replace("+", "|").split("|"):
        part = part.strip().upper()
        if part in ("", "NONE", "0"):
            continue
        if part not in CONTROLS:
            raise ValueError("unknown control '%s' (UP DOWN LEFT RIGHT FIRE)" % part)
        v |= CONTROLS[part]
    return v


# --------------------------------------------------------------------------
# tools

def t_new_game(a):
    sid = _new_id()
    args = ["-chain", "0", "-save", _snap_path(sid)]
    if a.get("submap"):
        args = ["-submap", str(int(a["submap"]))] + args
    rc, out, err = _run(args)
    if rc != 0:
        raise RuntimeError(err.strip() or out.strip())
    _save_meta(sid, {"parent": None, "submap_arg": a.get("submap"), "steps": 0,
                     "note": "new game"})
    return _observe(sid, a.get("full", True))


def t_observe(a):
    return _observe(a["state"], a.get("full", True))


def t_step(a):
    src = a["state"]
    _meta(src)
    data = bytearray()
    for item in a["inputs"]:
        mask, count = (item if isinstance(item, (list, tuple)) else (item, 1))
        data += bytes([_mask(mask)]) * int(count)
    sid = _new_id()
    with open(_inputs_path(sid), "wb") as f:
        f.write(bytes(data))
    rc, out, err = _run(["-reseed", "-load", _snap_path(src), "-inputs", _inputs_path(sid),
                         "-save", _snap_path(sid)])
    if rc != 0:
        raise RuntimeError(err.strip() or out.strip())
    _save_meta(sid, {"parent": src, "steps": len(data), "note": a.get("note", "step")})
    return _observe(sid, a.get("full", False))


def t_solve(a):
    src = a["state"]
    _meta(src)
    sid = _new_id()
    args = ["-chain", "1", "-load", _snap_path(src), "-save", _snap_path(sid),
            "-out", _inputs_path(sid), "-v"]
    if "beam" in a:
        args += ["-beam", str(int(a["beam"]))]
    if "max_steps" in a:
        args += ["-maxsteps", str(int(a["max_steps"]))]
    if "min_bombs" in a:
        args += ["-minbombs", str(int(a["min_bombs"]))]
    if a.get("waypoint"):
        args += ["-waypoint", "%d,%d" % tuple(int(v) for v in a["waypoint"])]
    for r in a.get("forbid", []):
        args += ["-forbid", "%d,%d,%d,%d" % tuple(int(v) for v in r)]
    if a.get("no_closures"):
        args += ["-noclosures"]
    rc, out, err = _run(args, timeout=int(a.get("timeout", 3600)))
    lines = [l for l in out.splitlines() if l.startswith("solve:")]
    progress = [l for l in err.splitlines()
                if "stuck" in l or "failed" in l or "no state" in l][-6:]
    found = any("NOT FOUND" not in l for l in lines[:1]) and rc == 0 and \
        os.path.exists(_inputs_path(sid)) and os.path.getsize(_inputs_path(sid)) > 0
    if not found:
        for p in (_snap_path(sid), _inputs_path(sid)):
            if os.path.exists(p):
                os.remove(p)
        return {"found": False, "solver": lines, "progress": progress}
    steps = os.path.getsize(_inputs_path(sid))
    hints = {k: a[k] for k in ("waypoint", "forbid", "min_bombs", "beam", "max_steps",
                               "no_closures") if k in a}
    _save_meta(sid, {"parent": src, "steps": steps, "note": a.get("note", "solve"),
                     "solve": hints})
    res = _observe(sid, a.get("full", False))
    res.update({"found": True, "solver": lines, "steps": steps})
    return res


def t_list_states(a):
    rows = []
    for name in sorted(os.listdir(WORK), key=lambda n: (len(n), n)):
        if name.endswith(".json") and name.startswith("s"):
            sid = name[:-5]
            m = _meta(sid)
            rows.append({"state": sid, "parent": m.get("parent"), "steps": m.get("steps"),
                         "note": m.get("note")})
    return {"states": rows}


def t_export(a):
    sid = a["state"]
    data, submap_arg = _timeline(sid)
    if submap_arg:
        raise ValueError("this timeline starts at -submap %s; a demo starts from a new game"
                         % submap_arg)
    out = a.get("file") or os.path.join(WORK, sid + "_dat_demo.c")
    with tempfile.NamedTemporaryFile(dir=WORK, suffix=".inputs", delete=False) as f:
        f.write(data)
        tmp = f.name
    try:
        rc, o, err = _run(["-reseed", "-inputs", tmp, "-record", out])
    finally:
        os.remove(tmp)
    if rc != 0:
        raise RuntimeError(err.strip())
    return {"file": out, "steps": len(data), "result": o.strip().splitlines()[-1:]}


# --------------------------------------------------------------------------
# trace, validate, repair

def _inputs_bytes(items):
    data = bytearray()
    for item in items:
        mask, count = (item if isinstance(item, (list, tuple)) else (item, 1))
        data += bytes([_mask(mask)]) * int(count)
    return bytes(data)


def _play(src_snap, data, save=None, every=0):
    """play <data> from a snapshot; the -steplog lines (every <every> steps, 0 = none)"""
    with tempfile.NamedTemporaryFile(dir=WORK, suffix=".inputs", delete=False) as f:
        f.write(data)
        tin = f.name
    tlog = tin[:-7] + ".steplog"
    args = ["-reseed", "-load", src_snap, "-inputs", tin]
    if save:
        args += ["-save", save]
    if every:
        args += ["-steplog", tlog, "-every", str(every)]
    try:
        rc, out, err = _run(args)
        if rc != 0:
            raise RuntimeError(err.strip() or out.strip())
        lines = []
        if every:
            with open(tlog) as f:
                lines = [json.loads(l) for l in f if l.strip()]
        return lines
    finally:
        for p in (tin, tlog):
            if os.path.exists(p):
                os.remove(p)


def _summary(snap):
    """what a leg is judged by: submap, counters, rick"""
    rc, out, err = _run(["-load", snap, "-steps", "0", "-dump"])
    if rc != 0:
        raise RuntimeError("xrick-core failed: " + err.strip())
    d = json.loads(out[out.index("{"):])
    r = d["rick"]
    return {"submap": d["submap"], "lives": d["lives"], "bombs": d["bombs"],
            "bullets": d["bullets"], "score": d["score"], "x": r["x"], "y": r["y"],
            "anchor": r["anchor"], "rick_state": r["state"]}


def _same(a, b):
    return all(a[k] == b[k] for k in ("submap", "lives", "bombs", "bullets", "x", "y"))


def _chain(sid, stop=None):
    """the states from the root (or after <stop>) to <sid>, oldest first"""
    out = []
    while sid is not None and sid != stop:
        out.append(sid)
        sid = _meta(sid).get("parent")
    if stop is not None and sid != stop:
        raise ValueError("'%s' is not an ancestor" % stop)
    return list(reversed(out))


def _first_loss(lines, lives0):
    """index (steps played) of the first line where rick is dying or a life is gone"""
    for n, l in enumerate(lines, 1):
        if "zombie" in l["rick"][2] or l["lives"] < lives0:
            return n
    return None


def t_trace(a):
    src = a["state"]
    _meta(src)
    data = _inputs_bytes(a["inputs"])
    every = max(1, int(a.get("every", 1)))
    limit = int(a.get("max_lines", 300))
    while len(data) // every > limit:  # keep the answer small
        every *= 2
    sid = None
    if a.get("save"):
        sid = _new_id()
        with open(_inputs_path(sid), "wb") as f:
            f.write(data)
    lines = _play(_snap_path(src), data, save=_snap_path(sid) if sid else None, every=every)
    if sid:
        _save_meta(sid, {"parent": src, "steps": len(data), "note": a.get("note", "trace")})
    return {"every": every, "lines": lines, "state": sid}


def t_validate(a):
    """legs: each leg replayed from its parent's STORED snapshot; timeline: the whole
       chain replayed from the new game, compared at every state"""
    chain = _chain(a["state"])
    mode = a.get("mode", "legs")
    rows, first = [], None
    tmp = os.path.join(WORK, "validate.snap")
    if mode == "legs":
        for p, s in zip(chain, chain[1:]):
            with open(_inputs_path(s), "rb") as f:
                data = f.read()
            _play(_snap_path(p), data, save=tmp)
            new, old = _summary(tmp), _summary(_snap_path(s))
            ok = _same(new, old)
            rows.append({"leg": "%s->%s" % (p, s), "ok": ok, "note": _meta(s).get("note"),
                         **({} if ok else {"now": new, "was": old})})
            if not ok and first is None:
                first = s
    else:
        root = chain[0]
        if _meta(root).get("submap_arg"):
            raise ValueError("timeline mode needs a chain that starts with a new game")
        data = b""
        for s in chain[1:]:
            with open(_inputs_path(s), "rb") as f:
                data += f.read()
            _play(_snap_path(root), data, save=tmp)
            new, old = _summary(tmp), _summary(_snap_path(s))
            ok = _same(new, old)
            rows.append({"state": s, "ok": ok, **({} if ok else {"now": new, "was": old})})
            if not ok:
                first = s
                break  # everything after drifts too
    if os.path.exists(tmp):
        os.remove(tmp)
    return {"mode": mode, "first_broken": first, "legs": len(rows),
            "broken": [r for r in rows if not r["ok"]], "ok": sum(r["ok"] for r in rows)}


def _goal_reached(new, old, parent_submap):
    """a replayed leg still does its job: same submap and lives, and inside a submap
       rick within one tile of where the old leg ended"""
    if new["submap"] != old["submap"] or new["lives"] < old["lives"]:
        return False
    if old["submap"] != parent_submap:
        return True  # an exit leg: reaching the next submap is the job
    return abs(new["x"] - old["x"]) <= 8 and abs(new["y"] - old["y"]) <= 8


def t_repair(a):
    """port the legs of an old chain (after <from>, up to <old>) onto a new parent:
       replay each leg; if it no longer does its job, re-solve it -- a solver leg with its
       own hints, any leg from <backoff> steps before where it went wrong, to the same
       goal (the next submap, or rick's old end tile as a waypoint)"""
    old_chain = _chain(a["old"], stop=a["from"])
    cur = a.get("onto", a["from"])
    backoff = int(a.get("backoff", 30))
    max_legs = int(a.get("max_legs", len(old_chain)))
    report = []
    for s in old_chain[:max_legs]:
        meta = _meta(s)
        with open(_inputs_path(s), "rb") as f:
            data = f.read()
        old = _summary(_snap_path(s))
        start = _summary(_snap_path(cur))
        sid = _new_id()
        lines = _play(_snap_path(cur), data, save=_snap_path(sid), every=1)
        new = _summary(_snap_path(sid))
        if _goal_reached(new, old, start["submap"]):
            with open(_inputs_path(sid), "wb") as f:
                f.write(data)
            _save_meta(sid, {"parent": cur, "steps": len(data),
                             "note": "repair of %s (replayed): %s" % (s, meta.get("note")),
                             **({"solve": meta["solve"]} if "solve" in meta else {})})
            report.append({"leg": s, "how": "replayed", "state": sid})
            cur = sid
            continue
        os.remove(_snap_path(sid))
        # re-solve: goal = the old leg's goal
        hints = dict(meta.get("solve", {}))
        exit_leg = old["submap"] != start["submap"]
        if not exit_leg:
            hints["waypoint"] = old["anchor"]
        hints.setdefault("min_bombs", old["bombs"] if exit_leg else 0)
        for k in ("beam", "timeout"):
            if k in a:
                hints[k] = a[k]
        tries = []
        loss = _first_loss(lines, start["lives"])
        cut = max(0, (loss if loss else len(data)) - backoff)
        if cut > 0:
            tries.append(("prefix %d of %d steps, then solve" % (cut, len(data)), cut))
        tries.append(("solve from the new parent", 0))
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
                _save_meta(base, {"parent": cur, "steps": k,
                                  "note": "repair of %s: first %d steps" % (s, k)})
            res = t_solve(dict(hints, state=base, note="repair of %s: %s" % (s, meta.get("note"))))
            if res.get("found"):
                done = (how, res["state"])
                break
            if k:
                _drop(base)
        if not done:
            report.append({"leg": s, "how": "FAILED", "was": old, "replay_ended": new,
                           "first_loss_step": loss, "hints": hints})
            return {"done": False, "last_good": cur, "report": report}
        report.append({"leg": s, "how": done[0], "state": done[1]})
        cur = done[1]
    return {"done": True, "last_good": cur, "report": report}


def _drop(sid):
    for p in (_snap_path(sid), _inputs_path(sid), _meta_path(sid)):
        if os.path.exists(p):
            os.remove(p)


TOOLS = [
    ("new_game", t_new_game,
     "Start a new RD1 game (or at 'submap', 1..47 as xrick -submap: exports need a new "
     "game). Returns the first state, at the first submap's tick 0, as JSON (see observe).",
     {"submap": {"type": "integer"}, "full": {"type": "boolean"}}, []),
    ("observe", t_observe,
     "The game at a state: step, submap, frow, lives/bombs/bullets/score, rick (x, y, "
     "state, 'anchor' = [row, col] in submap tiles, the coordinates waypoints and forbid "
     "use), live entities with trigger boxes, the submap's placements ('marks', done or "
     "not), exits (map_map row windows), the tile classes of map_map rows 0..39 (row i = "
     "submap row frow + i; see tiles_legend), and 'distance' (the solver's tile distance "
     "to the forward exit, and whether a dynamite wall still stands). full=false drops "
     "tiles and marks.",
     {"state": {"type": "string"}, "full": {"type": "boolean"}}, ["state"]),
    ("step", t_step,
     "Play inputs from a state: a list of [controls, steps], controls like 'RIGHT', "
     "'UP|LEFT', 'FIRE|DOWN' (dynamite), 'NONE', or a CONTROL_* mask. One step = one "
     "logic tick. Makes a new state; returns it as JSON (full=false by default).",
     {"state": {"type": "string"}, "inputs": {"type": "array"}, "note": {"type": "string"},
      "full": {"type": "boolean"}}, ["state", "inputs"]),
    ("solve", t_solve,
     "Run the solver from a state to the forward exit (the next submap's tick 0), or to "
     "'waypoint' [row, col] (submap tiles, rick's anchor within one tile) when given. "
     "Hints: min_bombs (bombs to still hold at the exit), forbid (list of [r0, c0, r1, "
     "c1] anchor rectangles to avoid), beam (128), max_steps (3000), no_closures. "
     "Returns found, the solver's lines, and the new state.",
     {"state": {"type": "string"}, "waypoint": {"type": "array"}, "forbid": {"type": "array"},
      "min_bombs": {"type": "integer"}, "beam": {"type": "integer"},
      "max_steps": {"type": "integer"}, "no_closures": {"type": "boolean"},
      "timeout": {"type": "integer"}, "note": {"type": "string"},
      "full": {"type": "boolean"}}, ["state"]),
    ("list_states", t_list_states, "All states: id, parent, steps from the parent, note.",
     {}, []),
    ("trace", t_trace,
     "Play inputs from a state (as step) and return one line per 'every' steps (doubled "
     "until at most max_lines, 300): step, sub, frow, lives, bombs, bullets, rick [x, y, "
     "state, anchor row, anchor col], ents [[slot, kind, x, y, lethal], ...]. save=true "
     "also keeps the end as a new state.",
     {"state": {"type": "string"}, "inputs": {"type": "array"}, "every": {"type": "integer"},
      "max_lines": {"type": "integer"}, "save": {"type": "boolean"},
      "note": {"type": "string"}}, ["state", "inputs"]),
    ("validate", t_validate,
     "Check a state's chain under the current xrick-core. mode 'legs' (default): each leg "
     "replayed from its parent's stored snapshot, compared with its stored end (submap, "
     "lives, bombs, bullets, rick x/y). mode 'timeline': the whole chain from the new game, "
     "stopping at the first state that differs. Returns first_broken and the broken legs "
     "(now vs was).",
     {"state": {"type": "string"}, "mode": {"type": "string"}}, ["state"]),
    ("repair", t_repair,
     "Port the legs of an old chain -- those after state 'from', up to 'old' -- onto "
     "'onto' (default 'from'), one by one. Each leg is replayed; if it still does its job "
     "(same submap and lives; inside a submap, rick within one tile of the old end) it is "
     "kept, else re-solved to the same goal (next submap with min_bombs = the old end's "
     "bombs, or the old end tile as a waypoint), first from 'backoff' (30) steps before "
     "where it went wrong, then from the leg's start; a solver leg keeps its own hints. "
     "Stops at the first leg it cannot repair. Options: max_legs, beam, timeout (per "
     "solve). Returns the new states per leg and last_good.",
     {"old": {"type": "string"}, "from": {"type": "string"}, "onto": {"type": "string"},
      "backoff": {"type": "integer"}, "max_legs": {"type": "integer"},
      "beam": {"type": "integer"}, "timeout": {"type": "integer"}}, ["old", "from"]),
    ("export", t_export,
     "Write the timeline of a state (every input from the new game) as src/rd1/dat_demo.c "
     "through the game's own demo recorder (xrick-core -reseed -inputs ... -record). "
     "Default file: <work>/<state>_dat_demo.c.",
     {"state": {"type": "string"}, "file": {"type": "string"}}, ["state"]),
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
                     "serverInfo": {"name": "xrick-solver", "version": "0.1"}})
    elif method == "tools/list":
        _reply(mid, {"tools": [{"name": n, "description": d,
                                "inputSchema": {"type": "object", "properties": p,
                                                "required": r}}
                               for n, _, d, p, r in TOOLS]})
    elif method == "tools/call":
        p = msg.get("params", {})
        fn = {n: f for n, f, _, _, _ in TOOLS}.get(p.get("name"))
        if not fn:
            _reply(mid, error={"code": -32602, "message": "unknown tool"})
            return
        try:
            res = fn(p.get("arguments") or {})
            _reply(mid, {"content": [{"type": "text", "text": json.dumps(res)}]})
        except Exception as e:  # reported to the model, not a protocol error
            _reply(mid, {"content": [{"type": "text", "text": "error: %s" % e}],
                         "isError": True})
    elif method == "ping":
        _reply(mid, {})
    elif mid is not None:
        _reply(mid, error={"code": -32601, "message": "method not found"})
    # notifications (no id): nothing to answer


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
