#!/bin/bash
# xrick web build (emscripten) -- wasm.md phase W1, RD1 in the browser.
#
# Mirrors xrick/Makefile: same sources, same include paths, same PLATFORM switch,
# SDL3 from emscripten's own port (-sUSE_SDL=3) instead of pkg-config.
#
# Run from Git Bash (Windows emsdk, wasm.md §0):
#   build/emsdk/build.sh            incremental build into build.web/
#   build/emsdk/build.sh clean      remove build.web/ first (do this after a header change:
#                                   the incremental check only compares .c/.cpp dates)
#   build/emsdk/build.sh gz         also write gzip-compressed copies into build.web/gz/
#
# Environment (all optional):
#   EMSDK_DIR     emsdk location, default /d/d/EmSdk
#   EMSDK_PYTHON  python for emsdk, default the one bundled in EMSDK_DIR (plain `python`
#                 can be the Windows Store alias, which breaks emsdk_env.sh)
#   PLATFORM      ST (default) or PC, as in the Makefile
#
# Output: build.web/index.html, player.js, xrick.js, xrick.wasm. Serve the directory
# over http (e.g. `emrun build.web/index.html`); .wasm must be served as application/wasm.

set -e

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SRCDIR="$ROOT/xrick"
OUT="$ROOT/build.web"
PLATFORM="${PLATFORM:-ST}"

for arg in "$@"; do
  case "$arg" in
    clean) rm -rf "$OUT" ;;
    gz) GZ=1 ;;
    *) echo "unknown argument: $arg" >&2; exit 2 ;;
  esac
done

# --- emsdk ------------------------------------------------------------------------------
EMSDK_DIR="${EMSDK_DIR:-/d/d/EmSdk}"
if ! command -v emcc >/dev/null 2>&1; then
  if [ -z "$EMSDK_PYTHON" ]; then
    EMSDK_PYTHON="$(ls -d "$EMSDK_DIR"/python/3*/python.exe 2>/dev/null | sort | tail -1)"
  fi
  export EMSDK_PYTHON
  # sourced here, in this shell: through a pipe it would run in a subshell and be lost
  pushd "$EMSDK_DIR" >/dev/null
  source ./emsdk_env.sh >/dev/null 2>&1
  popd >/dev/null
fi
command -v emcc >/dev/null 2>&1 || { echo "emcc not found (EMSDK_DIR=$EMSDK_DIR)" >&2; exit 1; }
echo "emcc: $(emcc --version 2>/dev/null | head -1)"

# --- flags, as in xrick/Makefile ----------------------------------------------------------
INC="-Iinclude -Iinclude/rd1 -Iinclude/rd2 -Isrc -Isrc/rd1 -Isrc/rd2"
WARN="-Wall -Wextra -Wconversion -Wsign-conversion -Wtype-limits"
CFLAGS="$INC $WARN -fcommon -O2 -DPLATFORM_$PLATFORM -sUSE_SDL=3"
CXXFLAGS="$INC -O2 -std=c++17 -sUSE_SDL=3"
# INVOKE_RUN=0 + callMain: the page starts the game on a click (audio unlock, wasm.md W1.5)
LDFLAGS="-O2 -sUSE_SDL=3 -sINVOKE_RUN=0 -sEXPORTED_RUNTIME_METHODS=callMain,FS -sALLOW_MEMORY_GROWTH=1"

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
      *.c)   emcc -c "$f" -o "$o" $CFLAGS   2>>"$OUT/warn.log" ;;
      *.cpp) em++ -c "$f" -o "$o" $CXXFLAGS 2>>"$OUT/warn.log" ;;
    esac
    n=$((n+1))
  fi
done
echo "compiled: $n file(s), warnings this run: $(grep -c 'warning:' "$OUT/warn.log")"

echo "linking..."
em++ $OBJS -o "$OUT/xrick.js" $LDFLAGS

for p in index.html player.js; do
  [ -f "$ROOT/build/emsdk/$p" ] && cp "$ROOT/build/emsdk/$p" "$OUT/"
done

if [ -n "$GZ" ]; then
  rm -rf "$OUT/gz" && mkdir -p "$OUT/gz"
  for f in index.html player.js xrick.js xrick.wasm; do gzip -c -9 "$OUT/$f" > "$OUT/gz/$f"; done
  echo "gz/: serve with Content-Encoding: gzip (and application/wasm for xrick.wasm)"
fi

ls -la "$OUT/xrick.js" "$OUT/xrick.wasm"
echo "done: $OUT"
