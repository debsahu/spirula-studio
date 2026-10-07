#!/bin/bash
# usage: build_m4.sh <ws_dir> [target...]          (runs ON the M4 Max; default targets: spirula roma_match_pairs)
# <ws_dir> holds the source checkout in <ws_dir>/src (override: SRC=...) and optionally a python venv in <ws_dir>/venv.
# Build tree <ws_dir>/b, log <ws_dir>/build.log. Takes /tmp/spirula-build.lock first.
if [ $# -lt 1 ] || [ "$1" = "-h" ] || [ "$1" = "--help" ]; then
  sed -n '2,4p' "$0" | sed 's/^# \{0,1\}//' >&2; exit 2
fi
WS=$1; shift
SRC=${SRC:-$WS/src}
[ -f "$SRC/CMakeLists.txt" ] || { echo "error: $SRC/CMakeLists.txt not found; pass the workspace as <ws_dir> or set SRC=" >&2; exit 2; }
[ $# -gt 0 ] || set -- spirula roma_match_pairs
[ -d "$WS/venv/bin" ] && export PATH="$WS/venv/bin:$PATH"
until mkdir /tmp/spirula-build.lock 2>/dev/null; do sleep 20; done
trap 'rmdir /tmp/spirula-build.lock' EXIT
cd "$WS" || exit 1
{
( [ -f b/build.ninja ] || cmake -S "$SRC" -B b -G Ninja -DSS_BACKEND=vulkan -DSS_BUILD_GUI=OFF -DSS_BUILD_SFM=ON -DSS_BUILD_SAM=ON -DSS_CHECK_COMMENTS=OFF -DOpenMP_ROOT=/opt/homebrew/opt/libomp ) || exit 1
caffeinate -i ninja -C b -j12 "$@"
echo "BUILD_RC=$?"
} > "$WS/build.log" 2>&1
