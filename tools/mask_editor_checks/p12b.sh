#!/bin/bash
# First pass of P12, part 2 of 3, unasserted: one click, the pool, ps and
# footprint loaded, then Done and the same readings closed.
# usage: p12b.sh [gui pid]

source "$(dirname "$0")/env.sh"
PID=${1:-$(cat "$MEC_WORK/gui.pid")}
fp() { footprint -p $PID 2>/dev/null | command grep -E "Footprint:|unmapped\) \(graphics\)" | tr -s " " | tr "\n" "|"; }
N=$(st sam_results)
gc click --at $PRINTER >/dev/null
wait_results $N || echo "NO RESULT"
echo "loaded: pool $(st sam_pool_mib)  session $(st sam_vram_mib)  job $(st sam_last_job_ms) ms area $(st sam_last_area) err '$(st sam_error)'"
sleep 3
echo "loaded ps rss KiB (does NOT measure device memory): $(ps -o rss= -p $PID)"
echo "loaded $(fp)"
gc click done >/dev/null
gc wait --frames 10 >/dev/null
echo "closed: open=$(st mask_editor_open) pool $(st sam_pool_mib)  session $(st sam_vram_mib)  close_ms $(st sam_close_ms) clicks $(st mask_editor_clicks) model '$(st mask_editor_model)'"
sleep 3
echo "closed +3s: pool $(st sam_pool_mib) ps rss $(ps -o rss= -p $PID)"
echo "closed $(fp)"
