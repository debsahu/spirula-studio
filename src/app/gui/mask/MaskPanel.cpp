// MaskPanel.cpp -- the mask editor's window: tool strip, canvas, status
// strip, navigation, and the window texture with its dirty-rect upload. The
// only file in app/gui/mask/ that includes imgui or calls GL.

#include "app/gui/mask/MaskSession.h"

#include "app/gui/mask/PathOverlay.h"

#include "app/gui/Layout.h"
#include "app/gui/Ui.h"
#include "i18n/catalog/MaskEdit.h"

#include "imgui.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>

namespace msg = spirula::i18n::msg::maskedit;

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
    pump();
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos, ImGuiCond_Appearing);
    ImGui::SetNextWindowSize(vp->WorkSize, ImGuiCond_Appearing);
    bool open = true;
    if (ImGui::Begin(ui::detail::label(msg::window_title), &open,
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings)) {
        draw_toolbar();
        draw_canvas();
        draw_status();
    }
    ImGui::End();
    if (!open) _close_requested = true;
    // The one thing that can still reach the screen once the deferred close
    // above starts blocking next frame (GuiMain.cpp applies it at the top of
    // the loop, ahead of that call).
    if (_close_requested && _doc && _doc->dirty())
        ImGui::SetMouseCursor(ImGuiMouseCursor_Wait);
}

void MaskSession::draw_toolbar() {
    const float w = px(96.0f);
    for (int i = (int)ToolId::Box; i <= (int)ToolId::Brush; i++) {
        const ToolRow& row = tool_table()[i];
        if (i != (int)ToolId::Box) ImGui::SameLine();
        if (ui::KeyButton(tool_label(row.id), w, row.key, !_path_mode && _tool.id() == row.id)) {
            _tool.set_id(row.id);
            _path_mode = false;
            _path.cancel();
        }
    }
    ImGui::SameLine();
    if (ui::KeyButton(msg::tool_path, w, "I", _path_mode)) {
        _path_mode = true;
        _tool.cancel();
    }
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
    ImGui::BeginDisabled(!idle());
    if (ui::Button(msg::revert_all)) revert_every_frame();
    ImGui::EndDisabled();
    ui::help_on_hover(msg::revert_all_help);
    ImGui::SameLine();
    if (ui::Button(msg::done)) _close_requested = true;

    ImGui::BeginDisabled(!idle());
    if (ui::ButtonRaw("<")) go_to(_idx - 1);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(px(260.0f));
    ui::SliderIntRaw("##maskframe", &_slider_idx, 0, std::max(0, frame_count() - 1), "%d");
    if (ImGui::IsItemDeactivatedAfterEdit() && _slider_idx != _idx) go_to(_slider_idx);
    ImGui::SameLine();
    if (ui::ButtonRaw(">")) go_to(_idx + 1);
    ImGui::EndDisabled();
}

void MaskSession::draw_canvas() {
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float status_h = px(118.0f);
    const ImVec2 size(std::max(avail.x, px(64.0f)), std::max(avail.y - status_h, px(64.0f)));
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 far(origin.x + size.x, origin.y + size.y);
    ui::InvisibleButtonRaw("##maskcanvas", size,
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle);
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(origin, far, IM_COL32(24, 24, 24, 255));
    if (!_doc) {
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
        if (hovered && io.MouseWheel != 0.0f)
            zoom_about(_view, std::pow(1.2f, io.MouseWheel), io.MousePos.x - origin.x,
                       io.MousePos.y - origin.y, _dw, _dh, size.x, size.y);
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
    _tool.set_brush_radius(_brush * m.scale);
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
    if (_path_mode) {
        ensure_livewire();
        _path.set_space(path_space(m));
        // A pen has no drag to read a held modifier off at release: the mode
        // is fixed by whatever is held on the click that plants the first
        // anchor, and stays that way however the path later closes.
        if (!_path.in_progress()) _path_paint = paint_for(io.KeyShift, io.KeyCtrl);
        std::vector<float> poly;
        bool consumed = false;
        if (_path.update(in, poly, consumed)) {
            const double t0 = now_ms();
            ShapeStroke stroke;
            stroke.kind = ShapeKind::Polygon;
            stroke.pts = std::move(poly);
            upload_rect(commit_stroke(stroke, _path_paint, m));
            _last_commit_ms = now_ms() - t0;
        }
        draw_path_overlay(dl, origin, _path);
    } else {
        ShapeStroke stroke;
        bool consumed = false;
        if (_tool.update(in, stroke, consumed)) {
            const double t0 = now_ms();
            upload_rect(commit_stroke(stroke, paint_for(in.shift, in.ctrl), m));
            _last_commit_ms = now_ms() - t0;
        }
        _tool.draw_overlay(dl, origin);
    }
    dl->PopClipRect();
    handle_keys(m);
}

void MaskSession::handle_keys(const Mapping& m) {
    const ImGuiIO& io = ImGui::GetIO();
    if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) || io.WantTextInput)
        return;
    if (!io.KeyCtrl) {
        for (int i = (int)ToolId::Box; i <= (int)ToolId::Brush; i++) {
            const ToolRow& row = tool_table()[i];
            if (ImGui::IsKeyPressed((ImGuiKey)row.imgui_key, false)) {
                _tool.set_id(row.id);
                _path_mode = false;
                _path.cancel();
            }
        }
        if (ImGui::IsKeyPressed(ImGuiKey_I, false)) {
            _path_mode = true;
            _tool.cancel();
        }
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        if (_path_mode) _path.cancel();
        else _tool.cancel();
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Enter, false) ||
        ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false)) {
        const Paint mode = _path_mode ? _path_paint : paint_for(io.KeyShift, io.KeyCtrl);
        ShapeStroke s;
        bool pending = false;
        if (_path_mode) {
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
    if (ImGui::IsKeyPressed(ImGuiKey_LeftBracket, true)) _brush = std::max(1.0f, _brush * 0.85f);
    if (ImGui::IsKeyPressed(ImGuiKey_RightBracket, true)) _brush = std::min(4096.0f, _brush * 1.18f);
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z, false)) {
        if (!io.KeyShift && _path_mode && _path.in_progress() && _path.pop_anchor()) return;
        if (!io.KeyShift && !_path_mode && _tool.id() == ToolId::Polygon && _tool.in_progress() &&
            _tool.pop_point())
            return;
        upload_rect(io.KeyShift ? redo() : undo());
    }
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false)) save();
}

void MaskSession::draw_status() {
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
    ui::Text(msg::brush_radius, {(int)std::lround(_brush)});
    ImGui::SameLine();
    ui::Text(msg::status_commit, {one_decimal(_last_commit_ms)});
    ui::TextDisabledWrapped(msg::hint_buttons);
    if (_path_mode) {
        ui::TextDisabledWrapped(msg::hint_path);
        ui::Text(msg::path_anchors, {_path.anchor_count()});
        if (!_path.snapping()) ui::TextDisabledWrapped(msg::path_straight);
    }
    ui::TextDisabledWrapped(msg::hint_view);
    const std::string err = error();
    if (!err.empty()) ui::TextColoredRaw(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), err);
    else {
        const std::string st = status();
        if (!st.empty()) ui::TextDisabledRaw(st);
    }
}

}  // namespace mask
}  // namespace gui
