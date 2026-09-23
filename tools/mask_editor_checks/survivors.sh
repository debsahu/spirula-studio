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
none '_path_mode'
none 'paint_for('
none '_status_h > 0.0f'
none 'const ImVec2 far('
none '_stroke_right'
none '_stroke_pane'
exit "$FAILS"
