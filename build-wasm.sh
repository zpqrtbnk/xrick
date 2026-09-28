#!/bin/bash
# xrick web build (emscripten) -- wasm.md phase W1, RD1 in the browser.
#
# Mirrors xrick/Makefile: same sources, same include paths, same PLATFORM switch,
# SDL3 from emscripten's own port (-sUSE_SDL=3) instead of pkg-config.
#
# Run from a plain Git Bash (Windows emsdk, wasm.md §0); the script registers the
# emsdk environment itself, nothing has to be sourced beforehand:
#   ./build-wasm.sh            incremental build into build.web/
#   ./build-wasm.sh clean      remove build.web/ first (do this after a header change:
#                              the incremental check only compares .c/.cpp dates)
#   ./build-wasm.sh gz         also write gzip-compressed copies into build.web/gz/
#
# Environment (all optional):
#   EMSDK_DIR     emsdk location, default /d/d/EmSdk
#   EMSDK_PYTHON  python for emsdk, default the one bundled in EMSDK_DIR (plain `python`
#                 can be the Windows Store alias, which breaks emsdk_env.sh)
#   PLATFORM      ST (default) or PC, as in the Makefile
#
# The page itself (index.html, player.js) lives in build/emsdk/ and is copied into the
# output. Output: build.web/index.html, player.js, xrick.js, xrick.wasm. Serve the
# directory over http (e.g. `emrun build.web/index.html`); .wasm must be served as
# application/wasm.

# --- 1. register the emsdk environment (first, and before `set -e`: emsdk_env.sh is
#        sourced into this shell, and a non-zero status from it must not end the script
#        silently) ------------------------------------------------------------------------
EMSDK_DIR="${EMSDK_DIR:-/d/d/EmSdk}"
if [ ! -f "$EMSDK_DIR/emsdk_env.sh" ]; then
  echo "error: no emsdk_env.sh in EMSDK_DIR=$EMSDK_DIR (set EMSDK_DIR to the emsdk folder)" >&2
  exit 1
fi
if [ -z "$EMSDK_PYTHON" ]; then
  # the python bundled with emsdk: Windows layout first, then Linux/macOS
  EMSDK_PYTHON="$(ls -d "$EMSDK_DIR"/python/3*/python.exe 2>/dev/null | sort | tail -1)"
  [ -z "$EMSDK_PYTHON" ] && EMSDK_PYTHON="$(ls -d "$EMSDK_DIR"/python/3*/bin/python3 2>/dev/null | sort | tail -1)"
fi
[ -n "$EMSDK_PYTHON" ] && export EMSDK_PYTHON
echo "emsdk: registering $EMSDK_DIR (python: ${EMSDK_PYTHON:-from PATH})"
EMSDK_LOG="$(mktemp)"
pushd "$EMSDK_DIR" >/dev/null
# in this shell, not through a pipe: a pipe would run it in a subshell and lose the variables
source ./emsdk_env.sh >"$EMSDK_LOG" 2>&1
popd >/dev/null
if ! command -v emcc >/dev/null 2>&1; then
  echo "error: emcc not found after sourcing $EMSDK_DIR/emsdk_env.sh; its output:" >&2
  cat "$EMSDK_LOG" >&2
  rm -f "$EMSDK_LOG"
  exit 1
fi
rm -f "$EMSDK_LOG"
echo "emcc: $(command -v emcc)"
echo "      $(emcc --version 2>/dev/null | head -1)"

set -e

# --- 2. paths and arguments -----------------------------------------------------------------
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRCDIR="$ROOT/xrick"
OUT="$ROOT/build.web"
PAGE="$ROOT/build/emsdk"
PLATFORM="${PLATFORM:-ST}"

for arg in "$@"; do
  case "$arg" in
    clean) rm -rf "$OUT" ;;
    gz) GZ=1 ;;
    *) echo "unknown argument: $arg" >&2; exit 2 ;;
  esac
done

# --- 3. flags, as in xrick/Makefile ------------------------------------------------------------
INC="-Iinclude -Iinclude/rd1 -Iinclude/rd2 -Isrc -Isrc/rd1 -Isrc/rd2"
WARN="-Wall -Wextra -Wconversion -Wsign-conversion -Wtype-limits"
CFLAGS="$INC $WARN -fcommon -O2 -DPLATFORM_$PLATFORM -sUSE_SDL=3"
CXXFLAGS="$INC -O2 -std=c++17 -sUSE_SDL=3"
# INVOKE_RUN=0 + callMain: the page starts the game on a click (audio unlock, wasm.md W1.5)
# EXIT_RUNTIME: exit() and emscripten_force_exit() really end the runtime, flush stdio and
# call Module.onExit (the page shows why the game stopped). _fflush: the page flushes the
# -trace file before reading it from MEMFS while the game runs (wasm.md W1.7).
LDFLAGS="-O2 -sUSE_SDL=3 -sINVOKE_RUN=0 -sEXIT_RUNTIME=1 -sEXPORTED_RUNTIME_METHODS=callMain,FS -sEXPORTED_FUNCTIONS=_main,_fflush -sALLOW_MEMORY_GROWTH=1"

# --- 4. compile (incremental) and link -------------------------------------------------------
cd "$SRCDIR"
CSRC=$(ls src/*.c src/rd1/*.c src/rd2/*.c | grep -v -E 'src/rd1/dat_(pics|sprites|tiles)PC\.c')
CXXSRC=$(ls src/audio_engine/*.cpp src/audio_engine/external/Musashi/*.cpp)

mkdir -p "$OUT/obj"
: > "$OUT/warn.log"
OBJS=""
n=0
for f in $CSRC $CXXSRC; do
  o="$OUT/obj/$(echo "$f" | tr '/' '_').o"
  OBJS="$OBJS $o"
  if [ ! -f "$o" ] || [ "$f" -nt "$o" ]; then
    case "$f" in
      *.c)   cc=emcc; flags="$CFLAGS" ;;
      *.cpp) cc=em++; flags="$CXXFLAGS" ;;
    esac
    # diagnostics go to warn.log; on an error, show them rather than stop silently
    if ! $cc -c "$f" -o "$o" $flags 2>>"$OUT/warn.log"; then
      echo "error: $cc failed on $f:" >&2
      grep -A3 "error:" "$OUT/warn.log" | tail -20 >&2
      exit 1
    fi
    n=$((n+1))
  fi
done
echo "compiled: $n file(s), warnings this run: $(grep -c 'warning:' "$OUT/warn.log")"

echo "linking..."
em++ $OBJS -o "$OUT/xrick.js" $LDFLAGS

for p in index.html player.js; do
  if [ -f "$PAGE/$p" ]; then cp "$PAGE/$p" "$OUT/"; fi
done

if [ -n "$GZ" ]; then
  rm -rf "$OUT/gz" && mkdir -p "$OUT/gz"
  for f in index.html player.js xrick.js xrick.wasm; do gzip -c -9 "$OUT/$f" > "$OUT/gz/$f"; done
  echo "gz/: serve with Content-Encoding: gzip (and application/wasm for xrick.wasm)"
fi

ls -la "$OUT/xrick.js" "$OUT/xrick.wasm"
echo "done: $OUT"
