#!/bin/bash
# First pass of P7, unasserted: Esc during a first encode on f1, from f0 with
# the editor open in SAM mode. battery.sh is the asserted version.

source "$(dirname "$0")/env.sh"
gc click --at $NEXT >/dev/null; for i in $(seq 1 100); do [ "$(st mask_editor_key)" = "f1" ] && break; gc wait --frames 5 >/dev/null; done
gc wait --frames 10 >/dev/null
echo "on $(st mask_editor_key) mode $(st mask_editor_mode)"
K=$(st mask_editor_kept); H=$(st mask_editor_history); N=$(st sam_results); D=$(st sam_dropped)
gc click --at $PRINTER >/dev/null
gc wait --frames 2 >/dev/null
echo "busy after click: $(st sam_busy) status '$(st sam_status)'"
T0=$(python3 -c 'import time; print(time.time())')
gc key Escape >/dev/null
while [ "$(st sam_busy)" = "True" ]; do gc wait --frames 1 >/dev/null; done
python3 -c "import time; print('P7 wait %.0f ms (includes ~0.1 s of polling per round)' % ((time.time() - $T0) * 1000))"
gc wait --frames 10 >/dev/null
echo "kept $K->$(st mask_editor_kept) history $H->$(st mask_editor_history) results $N->$(st sam_results) dropped $D->$(st sam_dropped) err '$(st sam_error)'"
[ "$(st mask_editor_kept)" = "$K" ] && [ "$(st mask_editor_history)" = "$H" ] && [ "$(st sam_results)" = "$N" ] && echo "P7 state unchanged" || echo "P7 state MOVED"
