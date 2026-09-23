#!/bin/bash
# Plan 3's bench dataset: the three 8K root frames mask_doc_test's bench writes
# (f0000..f0002), plus cam1/f0002 copied from f0002 so a second camera exists
# for the cross-camera refusal. Idempotent. Prints the frame count per camera;
# callers assert it rather than assume it.
# usage: workflow_bench.sh [dir]     (default /tmp/spirula_mask_bench)

D=${1:-/tmp/spirula_mask_bench}
cd "$(dirname "$0")/../.." || exit 1
if [ ! -f "$D/images/f0002.jpg" ] || [ ! -f "$D/masks/f0002.png" ]; then
    SS_MASK_BENCH="$D" ./build/mask_doc_test > /dev/null || exit 1
fi
mkdir -p "$D/images/cam1" "$D/masks/cam1"
[ -f "$D/images/cam1/f0002.jpg" ] || cp "$D/images/f0002.jpg" "$D/images/cam1/"
[ -f "$D/masks/cam1/f0002.png" ] || cp "$D/masks/f0002.png" "$D/masks/cam1/"
echo "root $(ls "$D"/images/*.jpg | wc -l | tr -d ' ') cam1 $(ls "$D"/images/cam1/*.jpg | wc -l | tr -d ' ')"
