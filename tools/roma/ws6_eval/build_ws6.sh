#!/bin/bash
# usage: build_ws6.sh <worktree> <logfile> <target> [target...]
# Serialised build of the given targets in <worktree>/build (Vulkan, no GUI). Takes /tmp/spirula-build.lock first.
if [ $# -lt 3 ] || [ "$1" = "-h" ] || [ "$1" = "--help" ]; then
  sed -n '2,3p' "$0" | sed 's/^# \{0,1\}//' >&2; exit 2
fi
WT=$1; LOG=$2; shift 2
[ -f "$WT/CMakeLists.txt" ] || { echo "error: $WT/CMakeLists.txt not found; first argument must be the worktree root" >&2; exit 2; }
until mkdir /tmp/spirula-build.lock 2>/dev/null; do sleep 20; done
trap 'rmdir /tmp/spirula-build.lock' EXIT
cd "$WT" || exit 1
{
if [ ! -f build/build.ninja ]; then
  python3 tools/codegen/generate_headers.py && python3 tools/codegen/generate_kernel_instantiation.py
  cmake -G Ninja -B build -DSS_BACKEND=vulkan -DSS_BUILD_GUI=OFF -DSS_BUILD_SFM=ON -DSS_BUILD_SAM=ON -DSS_CHECK_COMMENTS=OFF -DOpenMP_ROOT=/opt/homebrew/opt/libomp || exit 1
fi
caffeinate -i cmake --build build -j14 --target "$@"
echo "BUILD_RC=$?"
} > "$LOG" 2>&1
