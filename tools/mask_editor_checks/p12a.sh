#!/bin/bash
# First pass of P12, part 1 of 3, unasserted: the fresh pool, then open the
# editor and arm SAM. battery.sh is the asserted version.

source "$(dirname "$0")/env.sh"
echo "fresh:  pool $(st sam_pool_mib)  session $(st sam_vram_mib)"
gc click correct_masks >/dev/null
for i in $(seq 1 200); do [ -n "$(st mask_editor_key)" ] && break; gc wait --frames 5 >/dev/null; done
echo "editor open: key=$(st mask_editor_key) kept=$(st mask_editor_kept) pool $(st sam_pool_mib)"
gc key g >/dev/null; gc wait --frames 10 >/dev/null
echo "mode $(st mask_editor_mode) model $(st mask_editor_model)"
gc shot $G/editor.png >/dev/null
