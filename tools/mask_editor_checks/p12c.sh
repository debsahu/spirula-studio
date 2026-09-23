#!/bin/bash
# First pass of P12, part 3 of 3, unasserted: reopen and click; the strip's
# status during the reload is the witness. usage: p12c.sh <tag>

source "$(dirname "$0")/env.sh"
TAG=$1
gc click correct_masks >/dev/null
for i in $(seq 1 200); do [ -n "$(st mask_editor_key)" ] && break; gc wait --frames 5 >/dev/null; done
gc key g >/dev/null; gc wait --frames 5 >/dev/null
echo "reopened: pool $(st sam_pool_mib) session $(st sam_vram_mib) results $(st sam_results) clicks $(st mask_editor_clicks) model_set=$([ -n "$(st mask_editor_model)" ] && echo yes)"
N=$(st sam_results)
gc click --at $PRINTER >/dev/null
gc wait --frames 3 >/dev/null
echo "during: status '$(st sam_status)' busy $(st sam_busy)"
gc shot $G/${TAG}_reload.png >/dev/null
wait_results $N || echo "NO RESULT"
echo "reload job $(st sam_last_job_ms) ms pool $(st sam_pool_mib) session $(st sam_vram_mib)"
