#!/bin/bash
# usage: build_ws6.sh <logfile> [targets...]   (worktree: spirula-ws6, this Mac)
LOG=$1; shift
until mkdir /tmp/spirula-build.lock 2>/dev/null; do sleep 20; done
trap 'rmdir /tmp/spirula-build.lock' EXIT
cd /Users/debsahu/Workspace/slam/compute/spirula-ws6
{
if [ ! -f build/build.ninja ]; then
  python3 tools/codegen/generate_headers.py && python3 tools/codegen/generate_kernel_instantiation.py
  cmake -G Ninja -B build -DSS_BACKEND=vulkan -DSS_BUILD_GUI=OFF -DSS_BUILD_SFM=ON -DSS_BUILD_SAM=ON -DSS_CHECK_COMMENTS=OFF -DOpenMP_ROOT=/opt/homebrew/opt/libomp || exit 1
fi
caffeinate -i cmake --build build -j14 --target "$@"
echo "BUILD_RC=$?"
} > "$LOG" 2>&1
