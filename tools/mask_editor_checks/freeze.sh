#!/bin/bash
# First pass of the close freeze, unasserted: Done while a job runs, on a frame
# not yet encoded, with the editor open in SAM mode. usage: freeze.sh <label>

source "$(dirname "$0")/env.sh"
N=$(st sam_results)
gc click --at $PRINTER >/dev/null
gc wait --frames 2 >/dev/null
echo "$1: busy $(st sam_busy) status '$(st sam_status)'"
F0=$(st frame 2>/dev/null)
T0=$(python3 -c 'import time; print(time.time())')
gc click done >/dev/null
gc wait --frames 1 >/dev/null
T1=$(python3 -c 'import time; print(time.time())')
python3 -c "print('$1: done click -> one frame later %.0f ms wall' % (($T1 - $T0) * 1000))"
echo "$1: open=$(st mask_editor_open) sam_close_ms $(st sam_close_ms) pool $(st sam_pool_mib) results $(st sam_results)"
