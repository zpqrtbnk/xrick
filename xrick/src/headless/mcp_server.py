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
    _save_meta(sid, {"parent": src, "steps": steps, "note": a.get("note", "solve")})
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
