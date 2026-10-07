#!/bin/bash
# runs ON the M4 Max: serialised build of spirula + roma_match_pairs in ~/ws6-eval
cd ~/ws6-eval || exit 1
export PATH=~/ws6-eval/venv/bin:$PATH
until mkdir /tmp/spirula-build.lock 2>/dev/null; do sleep 20; done
trap 'rmdir /tmp/spirula-build.lock' EXIT
{
( [ -f b/build.ninja ] || cmake -S src -B b -G Ninja -DSS_BACKEND=vulkan -DSS_BUILD_GUI=OFF -DSS_BUILD_SFM=ON -DSS_BUILD_SAM=ON -DSS_CHECK_COMMENTS=OFF -DOpenMP_ROOT=/opt/homebrew/opt/libomp ) || exit 1
caffeinate -i ninja -C b -j12 spirula roma_match_pairs
echo "BUILD_RC=$?"
} > ~/ws6-eval/build.log 2>&1
