#!/bin/bash
# First pass of P8b, unasserted: from f1, go back to f0, click, and leave for f1
# while the job runs. battery.sh is the asserted version.

source "$(dirname "$0")/env.sh"
KNEXT=$(st mask_editor_kept); echo "on $(st mask_editor_key): next frame untouched kept $KNEXT history $(st mask_editor_history)"
gc click --at $PREV >/dev/null; for i in $(seq 1 100); do [ "$(st mask_editor_key)" = "f0" ] && break; gc wait --frames 5 >/dev/null; done
gc wait --frames 10 >/dev/null
echo "on $(st mask_editor_key) mode $(st mask_editor_mode)"
D=$(st sam_dropped); N=$(st sam_results)
gc click --at $CHAIR >/dev/null
gc click --at $NEXT >/dev/null
echo "busy right after leaving: $(st sam_busy)"
while [ "$(st sam_busy)" = "True" ]; do gc wait --frames 2 >/dev/null; done
gc wait --frames 10 >/dev/null
echo "dropped $D -> $(st sam_dropped), results $N -> $(st sam_results)"
echo "on $(st mask_editor_key): history $(st mask_editor_history), kept $(st mask_editor_kept) vs untouched $KNEXT"
gc state >/dev/null && echo "state still answers"
