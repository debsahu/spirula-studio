#!/bin/bash
# Structural tripwire over MaskPanel.cpp: each earlier task's code is still there,
# and the known wrong replacements are not. Text only, never behaviour; the exit
# status is the number of lines that missed. Anchors are whole statements where a
# shorter one also matches elsewhere (the pen commit vs handle_keys' Enter).

cd "$(dirname "$0")/../.." || exit 1
F=src/app/gui/mask/MaskPanel.cpp
FAILS=0
has() {
    local n
    n=$(command grep -cF -- "$2" "$F")
    if [ "$n" -ge "$1" ]; then echo "ok   $n >= $1: $2"; else echo "FAIL $n >= $1: $2"; FAILS=$((FAILS + 1)); fi
}
none() {
    local n
    n=$(command grep -cF -- "$1" "$F")
    if [ "$n" -eq 0 ]; then echo "ok   0 == 0: $1"; else echo "FAIL $n == 0: $1"; FAILS=$((FAILS + 1)); fi
}
has 1 '_strip.h > 0.0f'
has 1 '_canvas_h = size.y'
has 1 '_shown_valid = false'
has 1 'set_radius(wheel_brush(radius(), io.MouseWheel))'
has 1 'sam_prompt_point(fx, fy, sam_click_mode(io.KeyShift, io.KeyCtrl), in.clicked)'
has 1 'draw_sam_clicks(dl, m, ox, origin.y)'
has 1 'upload_rect(commit_stroke(stroke, paint_now(in.shift, in.ctrl), m));'
has 1 'upload_rect(commit_stroke(stroke, paint_now(_path.mode_shift(), _path.mode_ctrl()), m));'
has 1 'const Paint mode = path_mode() ? paint_now(_path.mode_shift(), _path.mode_ctrl())'
has 1 'note_shown(m, origin.x, origin.y, npanes, pane_w, gap)'
has 1 'SetItemKeyOwner(ImGuiKey_Tab)'
has 1 'const bool typing = ImGui::GetIO().WantTextInput;'
has 1 '} else if (path_mode()) {'
has 1 'const int active = bind_pane(hover_pane,'
has 2 'switch_view(_view_mode,'
has 2 'in_each_pane('
# Plan 3 Task 7: the propagate row. The 1-based UI range becomes 0-based here
# only, and only Propagate (never Undo propagate) waits for a SAM job.
has 1 'propagate(scope, _prop_from - 1, _prop_to - 1);'
has 1 'ImGui::BeginDisabled(_prop_scope != 1);'
has 1 'ImGui::BeginDisabled(!can_undo_propagate());'
has 1 'ui::TextDisabled(msg::prop_warn_moves);'
has 3 'note_row_width();'
gate=$(awk '/BeginDisabled\(sam_work_pending\(\)\);/{on=1} on{print} on&&/EndDisabled\(\);/{exit}' "$F")
if printf '%s' "$gate" | command grep -qF 'msg::prop_go)' &&
   ! printf '%s' "$gate" | command grep -qF 'msg::prop_undo)'; then
    echo "ok   the SAM gate holds Propagate and not Undo propagate"
else echo "FAIL the SAM gate holds Propagate and not Undo propagate"; FAILS=$((FAILS + 1)); fi
# Fix round 1 (task-7-review.md I1/I2/I3): the row gate, the warning's
# placement, and the disabled-aware tooltip were each defeatable without
# tripping any check above; anchored past the identical revert-frame gate.
row_gate=$(awk '
/\/\/ Row B: propagate\. Row C: its warning, drawn whether or not B is enabled\./ { getline; print; exit }
' "$F")
if printf '%s' "$row_gate" | command grep -qF 'ImGui::BeginDisabled(!_doc || !idle());'; then
    echo "ok   row B/C gated by !_doc || !idle() on the line right after the Row B/C comment"
else
    echo "FAIL row B/C gated by !_doc || !idle() on the line right after the Row B/C comment"
    FAILS=$((FAILS + 1))
fi
warn_order=$(awk '
{ line=$0; gsub(/^[ \t]+|[ \t]+$/, "", line)
  if (line == "ui::TextDisabled(msg::prop_warn_moves);") { print p2; print p1; exit }
  p2=p1; p1=line }
' "$F")
if [ "$warn_order" = "$(printf 'ImGui::EndDisabled();\nnote_row_width();')" ]; then
    echo "ok   prop_warn_moves immediately follows the row's EndDisabled(); note_row_width(): warning stays outside the row gate"
else
    echo "FAIL prop_warn_moves immediately follows the row's EndDisabled(); note_row_width(): warning stays outside the row gate"
    FAILS=$((FAILS + 1))
fi
has 1 'ui::help_on_hover_disabled(msg::prop_go_help);'
has 1 'ui::help_on_hover_disabled(msg::prop_undo_help);'
none '_path_mode'
none 'paint_for('
none '_status_h > 0.0f'
none 'const ImVec2 far('
none '_stroke_right'
none '_stroke_pane'
exit "$FAILS"
