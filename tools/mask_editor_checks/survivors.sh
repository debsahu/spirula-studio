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
# Plan 3 Task 12: Play waits for SAM, the worker, a stroke and a pen path, and
# says why when greyed; playing greys Revert all (both clauses kept), the
# navigation row and the SAM strip; the slideshow branch returns before !_doc.
play_gate=$(awk '/Play waits for anything that would land on the document it releases/{on=1} on{print} on&&/EndDisabled\(\);/{exit}' "$F")
for clause in 'frame_count() < 2 ||' '(!_slide_playing && (!_doc || !idle() || sam_work_pending() ||' \
              '_tool.in_progress() || _path.in_progress())));' 'else start_slideshow();'; do
    if printf '%s' "$play_gate" | command grep -qF -- "$clause"; then echo "ok   Play's gate holds: $clause"
    else echo "FAIL Play's gate holds: $clause"; FAILS=$((FAILS + 1)); fi
done
has 1 'ui::help_on_hover_disabled(msg::slide_help);'
has 1 'ImGui::BeginDisabled(!idle() || _slide_playing ||'
has 1 '(corrected_count() == 0 && !(_doc && _doc->dirty())));'
nav=$(awk '/if \(ui::ButtonRaw\("<"\)\) go_to\(_idx - 1\);/{print p; exit} {p=$0}' "$F")
if [ "$(printf '%s' "$nav" | sed 's/^ *//')" = 'ImGui::BeginDisabled(!idle() || _slide_playing);' ]; then
    echo "ok   the navigation row is greyed while playing"
else echo "FAIL the navigation row is greyed while playing"; FAILS=$((FAILS + 1)); fi
sam=$(awk '/^ *draw_sam_status\(\);$/{print p; exit} {p=$0}' "$F")
if [ "$(printf '%s' "$sam" | sed 's/^ *//')" = 'ImGui::BeginDisabled(_slide_playing);' ]; then
    echo "ok   the SAM strip is drawn greyed while playing"
else echo "FAIL the SAM strip is drawn greyed while playing"; FAILS=$((FAILS + 1)); fi
order=$(awk '/dl->AddRectFilled\(origin, far_corner,/{a=NR} /draw_slideshow\(dl, origin.x, origin.y, size.x, size.y\);/{b=NR} /^    if \(!_doc\) \{$/{c=NR} END{print (a && b && c && a < b && b < c) ? "yes" : "no"}' "$F")
if [ "$order" = yes ]; then echo "ok   the slideshow branch sits between the canvas fill and !_doc"
else echo "FAIL the slideshow branch sits between the canvas fill and !_doc"; FAILS=$((FAILS + 1)); fi
has 1 'if (!_slide_first && input) {'
if command grep -qF '_path.in_progress())' src/app/gui/mask/MaskSession.cpp &&
   command grep -qF 'bool animating() const { return _compare.animating() || _mask_editor.animating(); }' src/app/gui/GuiApp.h; then
    echo "ok   start_slideshow refuses a pen path; GuiApp::animating() asks the editor"
else echo "FAIL start_slideshow refuses a pen path; GuiApp::animating() asks the editor"; FAILS=$((FAILS + 1)); fi
none '_path_mode'
none 'paint_for('
none '_status_h > 0.0f'
none 'const ImVec2 far('
none '_stroke_right'
none '_stroke_pane'
exit "$FAILS"
