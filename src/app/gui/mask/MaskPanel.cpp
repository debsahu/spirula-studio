// MaskPanel.cpp -- the mask editor's window: tool strip, canvas, status
// strip, navigation, and the window texture with its dirty-rect upload.
// ImGui is permitted here and in PathOverlay.cpp (the pen tool's overlay,
// carved out on purpose so mask_doc_test stays imgui-free); GL is called
// only here. Every other file in this directory has neither.

#include "app/gui/mask/MaskSession.h"

#include "app/gui/mask/PathOverlay.h"

#include "app/gui/DatasetPrep.h"
#include "app/gui/Layout.h"
#include "app/gui/MaskPrompt.h"
#include "app/gui/MaskSettings.h"
#include "app/gui/Ui.h"
#include "i18n/catalog/Dataset.h"
#include "i18n/catalog/MaskEdit.h"

#include "imgui.h"

#include <algorithm>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>

namespace msg = spirula::i18n::msg::maskedit;
namespace dmsg = spirula::i18n::msg::dataset;

namespace gui {
namespace mask {

namespace {

double now_ms() {
    using namespace std::chrono;
    return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
}

std::string one_decimal(double v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.1f", v);
    return buf;
}

}  // namespace

void MaskSession::destroy_gl() {
    if (_tex) glDeleteTextures(1, &_tex);
    _tex = 0;
    _win = Window{};
    _win_dirty = true;
}

void MaskSession::ensure_window(const Mapping& m, float pane_w, float pane_h) {
    const Window want = window_for(m, _dw, _dh, pane_w, pane_h);
    if (!_win_dirty && same_window(want, _win)) return;
    _win = want;
    _win_dirty = false;
    if (_win.r.empty()) return;
    derive_window(_win, _win.r, window_source(), _rgba);
    if (!_tex) glGenTextures(1, &_tex);
    glBindTexture(GL_TEXTURE_2D, _tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, _win.tw, _win.th, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 _rgba.data());
}

void MaskSession::upload_rect(const Rect& shown) {
    if (!_tex || _win.r.empty() || shown.empty()) return;
    const Rect t = derive_window(_win, shown, window_source(), _rgba);
    if (t.empty()) return;
    glBindTexture(GL_TEXTURE_2D, _tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, _win.tw);
    glTexSubImage2D(GL_TEXTURE_2D, 0, t.x0, t.y0, t.w(), t.h(), GL_RGBA, GL_UNSIGNED_BYTE,
                    _rgba.data() + ((size_t)t.y0 * _win.tw + (size_t)t.x0) * 4);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
}

void MaskSession::draw() {
    if (!_open) return;
    // close() joins the worker and, on a dirty frame, that means waiting out
    // an 8K MaskDoc::save() (~700 ms). Deferring to the NEXT call means the
    // frame that requested it still reaches the screen before the block.
    if (_close_requested) { close(); return; }
    _popup_at_start = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopup);
    pump();
    {
        const double t0 = now_ms();
        const int before[3] = {_sam_results, _sam_reapplies, _sam_margin_starts};
        upload_rect(sam_pump());
        note_sam_ui(before, now_ms() - t0);
    }
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos, ImGuiCond_Appearing);
    ImGui::SetNextWindowSize(vp->WorkSize, ImGuiCond_Appearing);
    // No narrower than the tool row measured last frame: Revert has no key.
    ImGui::SetNextWindowSizeConstraints(ImVec2(_toolbar_w, 0.0f), ImVec2(FLT_MAX, FLT_MAX));
    bool open = true;
    if (ImGui::Begin(ui::detail::label(msg::window_title), &open,
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings)) {
        draw_toolbar();
        draw_canvas();
        const float status_y = ImGui::GetCursorPosY();
        draw_status();
        _status_h = ImGui::GetCursorPosY() - status_y;
        _strip.update(_status_h, (int)_mode, ImGui::GetWindowWidth());
    }
    ImGui::End();
    if (!open) _close_requested = true;
    // The one thing that can still reach the screen once the deferred close
    // above starts blocking next frame (GuiMain.cpp applies it at the top of
    // the loop, ahead of that call).
    if (_close_requested && _doc && _doc->dirty())
        ImGui::SetMouseCursor(ImGuiMouseCursor_Wait);
}

void MaskSession::pick_tool(ToolId t) {
    _tool.set_id(t);
    set_mode(CanvasMode::Shape);
    _path.cancel();
}

// The eraser is the brush shape under a different paint mode, not a shape of
// its own: ToolId is the 3D editor's SELECTION-shape table and a row there
// would put an "Eraser" button in a panel where it means nothing.
void MaskSession::pick_eraser() {
    _tool.set_id(ToolId::Brush);
    set_mode(CanvasMode::Eraser);
    _path.cancel();
}

void MaskSession::pick_path() {
    set_mode(CanvasMode::Path);
    _tool.cancel();
}

// A click here is a prompt, not a stroke: nothing half drawn may survive into it.
void MaskSession::pick_sam() {
    set_mode(CanvasMode::Sam);
    _tool.cancel();
    _path.cancel();
}

void MaskSession::draw_toolbar() {
    const float w = px(96.0f);
    for (int i = (int)ToolId::Box; i <= (int)ToolId::Brush; i++) {
        const ToolRow& row = tool_table()[i];
        if (i != (int)ToolId::Box) ImGui::SameLine();
        if (ui::KeyButton(tool_label(row.id), w, row.key,
                          mode() == CanvasMode::Shape && _tool.id() == row.id))
            pick_tool(row.id);
    }
    ImGui::SameLine();
    // X, not E: E is the Ellipse in the shared key table, and rebinding it
    // would move a shortcut the editor already documents.
    if (ui::KeyButton(msg::tool_eraser, w, "X", erasing())) pick_eraser();
    ImGui::SameLine();
    if (ui::KeyButton(msg::tool_path, w, "I", path_mode())) pick_path();
    ImGui::SameLine();
    // Armable with no checkpoint: the SAM strip is where one is picked and fetched.
    ImGui::BeginDisabled(!sam_available());
    if (ui::KeyButton(msg::tool_sam, w, "G", sam_mode())) pick_sam();
    ImGui::EndDisabled();
    if (!sam_available())
        ui::help_on_hover_raw(backends().masking_reason.c_str(),
                              ImGuiHoveredFlags_AllowWhenDisabled);
    ImGui::SameLine();
    ImGui::BeginDisabled(!_doc || !_doc->can_undo());
    if (ui::Button(msg::undo)) upload_rect(undo());
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!_doc || !_doc->can_redo());
    if (ui::Button(msg::redo)) upload_rect(redo());
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!_doc || !_doc->dirty());
    if (ui::Button(msg::save)) save();
    ImGui::EndDisabled();
    ui::help_on_hover(msg::save_help);
    ImGui::SameLine();
    ImGui::BeginDisabled(!_doc || !idle());
    if (ui::Button(msg::revert_frame)) revert_open_frame();
    ImGui::EndDisabled();
    ui::help_on_hover(msg::revert_frame_help);
    ImGui::SameLine();
    // Only when there is something to lose, and never in one click: it
    // deletes every hand correction in the dataset.
    ImGui::BeginDisabled(!idle() || (corrected_count() == 0 && !(_doc && _doc->dirty())));
    if (ui::Button(msg::revert_all)) _revert_all_ask = true;
    ImGui::EndDisabled();
    ui::help_on_hover(msg::revert_all_help);
    draw_revert_all_modal();
    ImGui::SameLine();
    if (ui::Button(msg::done)) _close_requested = true;
    _toolbar_w = ImGui::GetItemRectMax().x - ImGui::GetWindowPos().x +
                 ImGui::GetStyle().WindowPadding.x;

    ImGui::BeginDisabled(!idle());
    if (ui::ButtonRaw("<")) go_to(_idx - 1);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(px(260.0f));
    ui::SliderIntRaw("##maskframe", &_slider_idx, 0, std::max(0, frame_count() - 1), "%d");
    if (ImGui::IsItemDeactivatedAfterEdit() && _slider_idx != _idx) go_to(_slider_idx);
    ImGui::SameLine();
    if (ui::ButtonRaw(">")) go_to(_idx + 1);
    ImGui::EndDisabled();

    // On this row rather than a third one: a third row comes out of what
    // draw_canvas has to share between canvas and strip, raising the window
    // height at which the strip clips (docs/notes/mask-editor.md).
    if (erasing() || (mode() == CanvasMode::Shape && _tool.id() == ToolId::Brush)) {
        ImGui::SameLine();
        // 340 rather than the frame slider's 260 so the corner hint below
        // clears the centred value at its widest ("Eraser: 4096 px").
        ImGui::SetNextItemWidth(px(340.0f));
        // Logarithmic because the steps are multiplicative over twelve
        // octaves: linear travel would put every usable size in the first 2%.
        const std::string fmt =
            spirula::i18n::format(erasing() ? msg::eraser_radius : msg::brush_radius, {"%.0f"});
        float r = radius();
        if (ui::SliderFloatRaw("##maskbrush", &r, kMinBrush, kMaxBrush, fmt.c_str(),
                               ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp))
            set_radius(r);
        // The same corner the tool buttons carry their key in: the keys work
        // and always did, and the strip's `hint_view` was the only place
        // saying so, four lines down at the bottom of the window.
        ui::corner_key(msg::radius_keys.get());
        ui::help_on_hover(msg::radius_help);
    }
}

void MaskSession::draw_canvas() {
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    // What the strip actually took last frame. Its height is however many
    // lines the tool and the wrapped hints produce, which no constant can
    // know; frame one has no measurement, so seed the pen tool's eight.
    const float status_h = _strip.h > 0.0f ? _strip.h
                                           : 8.0f * ImGui::GetTextLineHeightWithSpacing();
    const ImVec2 size(std::max(avail.x, px(64.0f)), std::max(avail.y - status_h, px(64.0f)));
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 far(origin.x + size.x, origin.y + size.y);
    _canvas_h = size.y;
    ui::InvisibleButtonRaw("##maskcanvas", size,
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle);
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(origin, far, IM_COL32(24, 24, 24, 255));
    if (!_doc) {
        _shown_valid = false;
        dl->AddText(ImVec2(origin.x + px(8.0f), origin.y + px(8.0f)),
                    IM_COL32(200, 200, 200, 255), msg::working.get());
        return;
    }

    if (_tool_reset) {
        _tool.cancel();
        if (_tool.id() == ToolId::Navigate) _tool.set_id(ToolId::Brush);
        _tool_reset = false;
    }
    const ImGuiIO& io = ImGui::GetIO();
    const bool space = ImGui::IsKeyDown(ImGuiKey_Space);
    // The view. Never while a stroke is in progress: its points are pane pixels.
    if (!_tool.in_progress()) {
        const Mapping m0 = mapping(_view, _dw, _dh, size.x, size.y);
        // Alt, because the bare wheel is the zoom and Shift/Ctrl are the
        // paint modes. Guarded by in_progress() with the view: a stroke
        // carries one radius, so changing it mid-stroke would resize all of it.
        if (hovered && io.MouseWheel != 0.0f) {
            if (io.KeyAlt) set_radius(wheel_brush(radius(), io.MouseWheel));
            else
                zoom_about(_view, std::pow(1.2f, io.MouseWheel), io.MousePos.x - origin.x,
                           io.MousePos.y - origin.y, _dw, _dh, size.x, size.y);
        }
        const bool pan_down = ImGui::IsMouseDown(ImGuiMouseButton_Middle) ||
                              (space && ImGui::IsMouseDown(ImGuiMouseButton_Left));
        if (hovered && (ImGui::IsMouseClicked(ImGuiMouseButton_Middle) ||
                        (space && ImGui::IsMouseClicked(ImGuiMouseButton_Left))))
            _panning = true;
        if (_panning) {
            if (!pan_down) _panning = false;
            else pan(_view, io.MouseDelta.x, io.MouseDelta.y, m0, _dw, _dh);
        }
    }
    const Mapping m = mapping(_view, _dw, _dh, size.x, size.y);
    ensure_window(m, size.x, size.y);

    dl->PushClipRect(origin, far, true);
    if (_tex && !_win.r.empty()) {
        const ImVec2 a(origin.x + m.to_screen_x((float)_win.r.x0),
                       origin.y + m.to_screen_y((float)_win.r.y0));
        const ImVec2 b(origin.x + m.to_screen_x((float)(_win.r.x0 + _win.tw * _win.step)),
                       origin.y + m.to_screen_y((float)(_win.r.y0 + _win.th * _win.step)));
        dl->AddImage((ImTextureID)(intptr_t)_tex, a, b);
    }

    // The tool, fed pane pixels, the left button only. The modifiers are
    // read from the frame the stroke completes, as EditSession does.
    const bool can_stroke = hovered && !space && !_panning;
    _tool.set_brush_radius(radius() * m.scale);
    ViewportInput in;
    in.hovered = can_stroke;
    in.x = io.MousePos.x - origin.x;
    in.y = io.MousePos.y - origin.y;
    in.W = (int)size.x;
    in.H = (int)size.y;
    in.down = ImGui::IsMouseDown(ImGuiMouseButton_Left);
    in.clicked = can_stroke && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    in.released = ImGui::IsMouseReleased(ImGuiMouseButton_Left);
    in.right_clicked = can_stroke && ImGui::IsMouseClicked(ImGuiMouseButton_Right);
    in.double_clicked = can_stroke && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
    in.shift = io.KeyShift;
    in.ctrl = io.KeyCtrl;
    in.alt = io.KeyAlt;
    if (sam_mode()) {
        // Left is "this", right "not this": the dataset screen's grammar.
        float fx = 0.0f, fy = 0.0f;
        if ((in.clicked || in.right_clicked) && !sam_busy() && sam_has_model() &&
            shown_to_frame(io.MousePos.x, io.MousePos.y, fx, fy)) {
            sam_prompt_point(fx, fy, sam_click_mode(io.KeyShift, io.KeyCtrl), in.clicked);
        }
        draw_sam_clicks(dl, m, origin.x, origin.y);
    } else if (path_mode()) {
        ensure_livewire();
        _path.set_space(path_space(m));
        _path.note_modifiers(io.KeyShift, io.KeyCtrl);
        std::vector<float> poly;
        bool consumed = false;
        if (_path.update(in, poly, consumed)) {
            const double t0 = now_ms();
            ShapeStroke stroke;
            stroke.kind = ShapeKind::Polygon;
            stroke.pts = std::move(poly);
            upload_rect(commit_stroke(stroke, paint_now(_path.mode_shift(), _path.mode_ctrl()), m));
            _last_commit_ms = now_ms() - t0;
        }
        draw_path_overlay(dl, origin, _path);
    } else {
        ShapeStroke stroke;
        bool consumed = false;
        if (_tool.update(in, stroke, consumed)) {
            const double t0 = now_ms();
            upload_rect(commit_stroke(stroke, paint_now(in.shift, in.ctrl), m));
            _last_commit_ms = now_ms() - t0;
        }
        _tool.draw_overlay(dl, origin);
    }
    dl->PopClipRect();
    note_shown(m, origin.x, origin.y);
    handle_keys(m);
}

void MaskSession::handle_keys(const Mapping& m) {
    const ImGuiIO& io = ImGui::GetIO();
    // RootAndChildWindows counts a popup opened from this window as focused, so
    // a modal (or one closed earlier this frame by the same Esc) masks the keys.
    if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) || io.WantTextInput ||
        _popup_at_start || ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopup))
        return;
    if (!io.KeyCtrl) {
        for (int i = (int)ToolId::Box; i <= (int)ToolId::Brush; i++) {
            const ToolRow& row = tool_table()[i];
            if (ImGui::IsKeyPressed((ImGuiKey)row.imgui_key, false)) pick_tool(row.id);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_I, false)) pick_path();
        if (ImGui::IsKeyPressed(ImGuiKey_X, false)) pick_eraser();
        if (ImGui::IsKeyPressed(ImGuiKey_G, false) && sam_available()) pick_sam();
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        if (sam_mode()) sam_cancel();
        else if (path_mode()) _path.cancel();
        else _tool.cancel();
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Enter, false) ||
        ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false)) {
        const Paint mode = path_mode() ? paint_now(_path.mode_shift(), _path.mode_ctrl())
                                      : paint_now(io.KeyShift, io.KeyCtrl);
        ShapeStroke s;
        bool pending = false;
        if (path_mode()) {
            std::vector<float> poly;
            pending = _path.commit_pending(poly);
            s.kind = ShapeKind::Polygon;
            s.pts = std::move(poly);
        } else {
            pending = _tool.commit_pending(s);
        }
        if (pending) {
            const double t0 = now_ms();
            upload_rect(commit_stroke(s, mode, m));
            _last_commit_ms = now_ms() - t0;
        }
    }
    if (ImGui::IsKeyPressed(ImGuiKey_LeftBracket, true)) set_radius(step_brush(radius(), false));
    if (ImGui::IsKeyPressed(ImGuiKey_RightBracket, true)) set_radius(step_brush(radius(), true));
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z, false)) {
        if (!io.KeyShift && path_mode() && _path.in_progress() && _path.pop_anchor()) return;
        if (!io.KeyShift && !path_mode() && _tool.id() == ToolId::Polygon && _tool.in_progress() &&
            _tool.pop_point())
            return;
        upload_rect(io.KeyShift ? redo() : undo());
    }
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false)) save();
}

// Names what goes -- the corrected-frame count, and the open frame's unsaved
// strokes -- and needs a second, explicit click. Cancel is the safe default.
void MaskSession::draw_revert_all_modal() {
    if (_revert_all_ask) {
        ui::OpenPopup(msg::revert_all_title);
        _revert_all_ask = false;
    }
    if (!ui::BeginPopupModal(msg::revert_all_title, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        return;
    ImGui::PushTextWrapPos(px(460.0f));
    ui::Text(msg::revert_all_confirm);
    ImGui::Spacing();
    ui::Text(msg::corrected_count, {corrected_count()});
    if (_doc && _doc->dirty()) ui::Text(msg::revert_all_unsaved);
    ImGui::PopTextWrapPos();
    ImGui::Spacing();
    // Esc is Cancel, the convention; handle_keys skips this frame, so the same Esc
    // cannot also cancel a stroke or a path behind the modal.
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) ImGui::CloseCurrentPopup();
    if (ui::Button(msg::revert_all_button, ImVec2(px(220.0f), 0)) && idle()) {
        revert_every_frame();
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ui::Button(dmsg::cancel, ImVec2(px(150.0f), 0))) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

void MaskSession::draw_status() {
    // First, where a short window clips it last: a failed save's error.
    const std::string err = error();
    const std::string st = err.empty() ? status() : std::string();
    if (!err.empty()) ui::TextColoredRaw(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), err);
    else if (!st.empty()) ui::TextDisabledRaw(st);
    else ImGui::Dummy(ImVec2(0.0f, ImGui::GetTextLineHeight()));
    if (_idx >= 0 && _idx < frame_count()) {
        const FrameRef& f = _frames[(size_t)_idx];
        ui::Text(msg::status_frame, {_idx + 1, frame_count(), f.key});
        ImGui::SameLine();
        ui::Text(msg::status_camera, {f.camera.empty() ? std::string("/") : f.camera});
    }
    if (_doc) {
        ui::Text(msg::status_kept, {one_decimal(100.0 * _doc->kept_fraction())});
        ImGui::SameLine();
        ui::TextDisabled(_doc->dirty() ? msg::status_unsaved : msg::status_saved);
        ImGui::SameLine();
        ui::Text(msg::corrected_count, {corrected_count()});
        if (_doc->base_state() == BaseState::Regenerated)
            ui::TextDisabledWrapped(msg::status_base_regenerated);
        if (_doc->base_state() == BaseState::Missing)
            ui::TextDisabledWrapped(msg::status_base_missing);
    }
    if (sam_mode()) {
        draw_sam_status();
    } else {
        ui::Text(erasing() ? msg::eraser_radius : msg::brush_radius,
                 {(int)std::lround(radius())});
        ImGui::SameLine();
        ui::Text(msg::status_commit, {one_decimal(_last_commit_ms)});
        ui::TextDisabledWrapped(erasing() ? msg::hint_eraser : msg::hint_buttons);
    }
    if (path_mode()) {
        ui::TextDisabledWrapped(msg::hint_path);
        ui::Text(msg::path_anchors, {_path.anchor_count()});
        if (!_path.snapping()) ui::TextDisabledWrapped(msg::path_straight);
    }
    ui::TextDisabledWrapped(msg::hint_view);
}

// The editor's clicks on this frame, as SegmentPanel draws them: the object's
// colour, and red with a cross for "not this" so colour is not the only cue.
void MaskSession::draw_sam_clicks(ImDrawList* dl, const Mapping& m, float ox, float oy) {
    if (!_sam || _idx < 0 || _idx >= frame_count()) return;
    const PathSpace ps = path_space(m);
    const std::string& camera = _frames[(size_t)_idx].camera;
    const float r = px(6.0f), k = px(3.0f);
    for (const MaskClick& c : sam_prompt().clicks) {
        if (!c.source.empty() || c.frame != _idx || c.camera != camera) continue;
        float x = 0.0f, y = 0.0f;
        ps.from_frame(c.x, c.y, x, y);
        const ImVec2 p(ox + x, oy + y);
        dl->AddCircleFilled(p, r, c.positive ? (ImU32)mask_object_color(c.object)
                                             : IM_COL32(240, 90, 90, 255));
        dl->AddCircle(p, r, IM_COL32(20, 20, 20, 200), 0, 1.5f);
        if (c.positive) continue;
        dl->AddLine(ImVec2(p.x - k, p.y - k), ImVec2(p.x + k, p.y + k), IM_COL32(255, 255, 255, 255), 1.5f);
        dl->AddLine(ImVec2(p.x - k, p.y + k), ImVec2(p.x + k, p.y - k), IM_COL32(255, 255, 255, 255), 1.5f);
    }
}

// GuiApp's picker over the app's one model, the hint, then a two-line slot the
// busy, error and result lines share: none of them may resize the canvas.
void MaskSession::draw_sam_status() {
    if (_model_picker) _model_picker();
    if (!sam_has_model()) ui::TextDisabled(dmsg::mask_model_first);
    // The editor's own margin, never the dataset's. A drop takes it, and a
    // release re-applies it to the drop just made (sam_margin_changed).
    MaskSettings& p = sam_prompt();
    if (draw_margin_slider(p.dilate_ratio, p.shrink_ratio, /*keep=*/false, px(220.0f),
                           /*inline_label=*/true))
        _sam_margin_moved = true;
    if (_sam_margin_moved && !ImGui::IsAnyItemActive()) {
        _sam_margin_moved = false;
        sam_margin_changed();
    }
    ui::TextDisabledWrapped(msg::sam_hint);
    draw_sam_objects();
    draw_sam_text();
    const float y0 = ImGui::GetCursorPosY();
    const std::string sam_err = sam_error();
    if (sam_busy()) {
        ui::TextDisabledRaw(sam_status());
        ui::TextDisabledWrapped(msg::sam_cancel_slow);
    } else if (!sam_err.empty()) {
        ui::TextColoredRaw(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), sam_err);
    } else if (sam_results() > 0 && sam_last_area() == 0) {
        ui::TextDisabledWrapped(msg::sam_empty);
    } else if (sam_results() > 0) {
        char score[16];
        std::snprintf(score, sizeof score, "%.2f", sam_last_score());
        ui::Text(msg::sam_result, {(long long)sam_last_area(), sam_last_detections(),
                                   std::string(score), one_decimal(sam_last_ms())});
    }
    const float slot = 2.0f * ImGui::GetTextLineHeightWithSpacing();
    const float used = ImGui::GetCursorPosY() - y0;
    const float pad = slot - used - ImGui::GetStyle().ItemSpacing.y;
    if (pad > 0.0f) ImGui::Dummy(ImVec2(0.0f, pad));
}

// The phrase field on one row with the palette's button, or the reason there is
// none (SAM 2 has no text tower), so the strip never grows. Enter runs it.
void MaskSession::draw_sam_text() {
    MaskSettings& p = sam_prompt();
    const bool no_text = !sam_has_model() || !sam_text_supported();
    ImGui::BeginDisabled(no_text);
    ImGui::SetNextItemWidth(px(420.0f));
    if (ui::InputTextEnglish(msg::sam_text_label, "person; monopod", &p.prompt,
                             ImGuiInputTextFlags_EnterReturnsTrue) &&
        !sam_busy())
        sam_prompt_text(p.prompt);
    ImGui::EndDisabled();
    if (no_text)
        ui::help_on_hover_disabled(sam_has_model() ? msg::sam_text_unsupported
                                                   : dmsg::mask_model_first);
    if (!sam_has_model()) return;
    ImGui::SameLine();
    if (no_text) {
        ui::TextDisabledWrapped(msg::sam_text_unsupported);
        return;
    }
    if (ui::Button(dmsg::mask_subjects)) ImGui::OpenPopup("##samsubjects");
    // A popup, not a section: open, the chips would push the picture up.
    ImGui::SetNextWindowSizeConstraints(ImVec2(px(560.0f), 0.0f), ImVec2(px(560.0f), FLT_MAX));
    if (!ImGui::BeginPopup("##samsubjects")) return;
    if (spirula::i18n::current() != spirula::i18n::Lang::en)
        ui::TextDisabledWrapped(dmsg::mask_english_only);
    ImGui::SetNextItemOpen(true, ImGuiCond_Appearing);
    // `false`: the editor's phrase always names what to drop.
    draw_subject_palette(p.prompt, p.negative_prompt, /*keep_subject=*/false);
    ImGui::EndPopup();
}

// A frame's pump-and-upload time, credited to what it did: a prompt landing,
// a margin landing, or a margin job starting.
void MaskSession::note_sam_ui(const int before[3], double ms) {
    if (_sam_results != before[0]) _sam_ui_ms = ms;
    if (_sam_reapplies != before[1]) _sam_reapply_ms = ms;
    if (_sam_margin_starts != before[2]) _sam_margin_start_ms = ms;
}

// The dataset screen's object list over the editor's clicks, in a fixed-height
// box (two objects, then it scrolls, to the end on a new one) so the picture
// never moves. Disabled with no checkpoint: the line above says why.
void MaskSession::draw_sam_objects() {
    if (_idx < 0 || _idx >= frame_count()) return;
    const float h = ImGui::GetTextLineHeightWithSpacing() + 3.0f * ImGui::GetFrameHeightWithSpacing();
    ImGui::BeginDisabled(!sam_has_model());
    if (ImGui::BeginChild("##samobjects", ImVec2(0.0f, h))) {
        MaskSettings& p = sam_prompt();
        bool edited = false;
        draw_mask_objects(p, (long long)_idx, _frames[(size_t)_idx].camera, std::string(), edited);
        if (edited) sam_objects_edited();
        // Twice: the first frame clamps to the content size before the new row.
        if (p.object_count != _sam_objects_drawn) _sam_scroll_frames = 2;
        if (_sam_scroll_frames > 0 && _sam_scroll_frames--) ImGui::SetScrollHereY(1.0f);
        _sam_objects_drawn = p.object_count;
    }
    ImGui::EndChild();
    ImGui::EndDisabled();
}

}  // namespace mask
}  // namespace gui
