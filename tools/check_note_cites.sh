#!/bin/bash
# Fail when a file:line in docs/notes/mask-editor.md no longer says what it claims.
#
# Each row names one citation as the note writes it and greps its lines (or a
# pinned sub-range) for a token the claim needs. Line numbers are HARDCODED:
# a commit that moves cited lines fails the build until the note and the rows
# are updated together. That is the point -- 11 of 32 citations were once
# wrong, 17 another time. Also fails on a note citation no row checks and on
# a row no citation makes. _deps/ rows SKIP, loudly, with no build tree.
# Usage:  bash tools/check_note_cites.sh      (SS_BUILD_DIR picks the tree)

cd "$(dirname "$0")/.." || exit 1
NOTE=docs/notes/mask-editor.md
BUILD=${SS_BUILD_DIR:-}
if [ -z "$BUILD" ]; then
    for d in build build_vulkan build_cuda; do
        [ -d "$d/_deps" ] && { BUILD=$d; break; }
    done
fi
fail=0
ok=0
skip=0
ROWS=$(mktemp)
trap 'rm -f "$ROWS"' EXIT

row() {
    local p=$1 ln=$2 want=$3 at=${4:-$2} f=$1
    local lo=${at%%-*} hi=${at##*-}
    echo "$p ${ln%%-*} ${ln##*-}" >> "$ROWS"
    case $p in
        _deps/*)
            f="$BUILD/$p"
            if [ -z "$BUILD" ] || [ ! -f "$f" ]; then
                echo "SKIP $p:$ln  (no configured build tree holds it)"
                skip=$((skip + 1))
                return
            fi ;;
    esac
    if [ ! -f "$f" ]; then
        echo "FAIL $p:$ln  no such file"
        fail=1
    elif [ "$lo" -lt "${ln%%-*}" ] || [ "$hi" -gt "${ln##*-}" ]; then
        echo "FAIL $p:$ln  row pins $at, outside the cited range"
        fail=1
    elif sed -n "${lo},${hi}p" "$f" | command grep -qF -- "$want"; then
        ok=$((ok + 1))
    else
        echo "FAIL $p:$ln  wanted at $at: $want"
        sed -n "${lo},${hi}p" "$f" | head -3 | sed 's/^/     | /'
        fail=1
    fi
}

row src/app/gui/mask/Livewire.h 3 "Mortensen and Barrett 1995"
row src/app/gui/mask/Livewire.h 84 "std::vector<uint8_t> _parent;"
row _deps/imgui-src/imgui.cpp 1850-1856 "ImGuiKey_LeftCtrl)  { key = ImGuiKey_LeftSuper; }"
row _deps/imgui-src/imgui.cpp 1957-1967 "Convert Ctrl(Super)+Left click into Right-click"
row src/app/gui/mask/MaskPanel.cpp 177 "ui::Button(msg::undo)"
row src/app/gui/mask/MaskPanel.cpp 408 "io.KeyShift ? redo() : undo()"
row src/app/gui/mask/MaskSession.cpp 397-408 "MaskSession::commit_stroke" 397
row src/app/gui/mask/MaskSession.cpp 397-408 "if (r.empty()) return {};" 408
row src/app/gui/mask/MaskPanel.cpp 76 "shown.empty()) return;"
row src/app/gui/mask/MaskPanel.cpp 455 "msg::status_kept"
row src/app/gui/mask/MaskLayer.cpp 341-375 "bool recomposite_frame" 341
row src/app/gui/mask/MaskLayer.cpp 341-375 "}" 375
row src/app/gui/mask/MaskPanel.cpp 319-320 "in.shift = io.KeyShift;"
row src/app/gui/mask/MaskPanel.cpp 319-320 "in.ctrl = io.KeyCtrl;"
row src/app/gui/mask/MaskPanel.cpp 350 "paint_now(in.shift, in.ctrl)"
row src/app/gui/edit/EditTool.cpp 205 "ToolId::Box || _id == ToolId::Ellipse"
row src/app/gui/edit/EditTool.cpp 160 "ToolId::Lasso || _id == ToolId::Brush"
row src/app/gui/mask/MaskPanel.cpp 207 'go_to(_idx - 1)'
row src/app/gui/mask/MaskPanel.cpp 211 'go_to(_slider_idx)'
row src/app/gui/mask/MaskPanel.cpp 213 'go_to(_idx + 1)'
row cmake/SsApps.cmake 382-384 "align_fit_test.cpp"
row cmake/SsApps.cmake 382 "add_executable(align_fit_test" 382
row src/app/FrameMask.h 52-53 "255 = keep"
row src/app/gui/mask/Livewire.cpp 76 "_step = std::max(1,"
row src/core/MaskMargin.h 19-28 "inline int radius_px"
row src/app/gui/GlLoader.h 3-6 "namespaced under glx::"
row src/app/gui/mask/MaskSession.cpp 238 "enqueue([this, f, i]"
row src/app/gui/mask/MaskSession.cpp 258 "_status.clear();"
row src/app/gui/mask/MaskSession.cpp 300 "_livewire.reset();"
row src/app/gui/mask/Livewire.h 80-90 "_acos_abs[256][8]"
row src/app/gui/mask/Livewire.h 80-90 "std::vector<uint8_t> _fg;"
row src/app/gui/mask/Livewire.cpp 83-131 "std::vector<uint8_t> luma(n);"
row src/app/gui/mask/Livewire.cpp 225-226 "_parent.assign(n, kUnseen);"
row src/app/gui/mask/MaskPanel.cpp 441-480 "void MaskSession::draw_status()" 441
row src/app/gui/mask/MaskPanel.cpp 441-480 "}" 480
row src/app/gui/mask/MaskPanel.cpp 461-464 "status_base_regenerated"
row src/app/gui/mask/MaskPanel.cpp 461-464 "status_base_missing"
row src/app/gui/mask/MaskPanel.cpp 1-5 "ImGui is permitted here"
row src/app/gui/Fonts.cpp 33 "kBaseSize = 16.0f"
row src/app/gui/Layout.cpp 33 "FramePadding = ImVec2(6, 4)"
row src/app/gui/Layout.cpp 34 "ItemSpacing = ImVec2(8, 6)"
row src/app/gui/Layout.h 26 "unscaled * ui_scale()"
row src/app/gui/Layout.cpp 49-50 "ScaleAllSizes(scale)"
row src/app/gui/Layout.cpp 49-50 "FontScaleMain = scale"
row cmake/SsApps.cmake 412-427 "add_executable(mask_doc_test" 412
row cmake/SsApps.cmake 412-427 "ss_configure_app(mask_doc_test)" 427
# The slider and the eraser.
row src/app/gui/mask/MaskPanel.cpp 219-233 "ImGuiSliderFlags_Logarithmic"
row src/app/gui/mask/MaskPanel.cpp 219-233 "erasing() || (mode() == CanvasMode::Shape && _tool.id() == ToolId::Brush)" 219
row src/app/gui/mask/MaskPanel.cpp 273-281 "io.KeyAlt) set_radius(wheel_brush(radius(), io.MouseWheel))"
row src/app/gui/mask/MaskSession.cpp 415-440 "// Plain and Shift drop, Ctrl keeps" 415
row src/app/gui/mask/MaskSession.cpp 415-440 "const bool keep = ctrl != erasing;"
row src/app/gui/mask/MaskSession.cpp 415-440 "if (!(r > kMinBrush)) return kMinBrush;"
row src/app/gui/mask/MaskSession.cpp 415-440 "std::pow(1.18f, wheel)"
row src/app/gui/mask/MaskSession.cpp 415-440 "}" 440
row src/app/gui/mask/MaskSession.h 107-108 "float radius() const { return _brush; }"
row src/app/gui/mask/MaskSession.h 107-108 "set_radius(float r) { _brush = clamp_brush(r); }"
row src/app/gui/mask/MaskDoc.cpp 176-180 "case Paint::ForceKeep: _keep[i] = 255; _drop[i] = 0; break;"
row src/app/gui/mask/MaskLayer.h 47-48 "final = keep ? 255 : drop ? 0 : base"
row src/app/gui/edit/EditTool.h 27-32 "enum class ToolId"
row src/app/gui/Automation.cpp 509-532 'r.get_bool("alt", false)) mod.keys.push_back((int)ImGuiKey_LeftAlt)'
# The radius keys surfaced on the slider.
row src/app/gui/mask/MaskPanel.cpp 234-240 "ui::corner_key(msg::radius_keys.get())"
row src/app/gui/mask/MaskPanel.cpp 234-240 "ui::help_on_hover(msg::radius_help)"
row src/app/gui/Ui.h 219-229 "inline void corner_key(const char* key)"
row src/app/gui/Ui.h 219-229 "ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax()"
# The SAM half.
row src/sam/Masking.cpp 85-87 "(int)((int64_t)y * sh / dh)"
row src/sam/Masking.cpp 85-87 "(int)((int64_t)x * sw / dw)"
row src/sam/pipeline/Session.cpp 565-585 "std::string Session::vramReport() const" 565
row src/sam/pipeline/Session.cpp 565-585 "total += impl_->arena.capacity();"
row src/nn/vk/Memory.cpp 243-266 "void Allocator::free(DevicePtr ptr)" 243
row src/nn/vk/Memory.cpp 243-266 "vkFreeMemory(ctx.device(), a.memory, nullptr);"
row src/nn/vk/Memory.cpp 362-395 "void VramPool::release(" 362
row src/nn/vk/Memory.cpp 362-395 "void VramPool::releaseAll()"

# Coverage, both ways. A bare `:N` continues the paragraph's last cited file;
# `.md` citations point outside the repo (session briefs) and are not checked.
cover=$(awk -v rows="$ROWS" '
    BEGIN {
        while ((getline l < rows) > 0) { n++; split(l, a, " "); rp[n] = a[1]; rl[n] = a[2]; rh[n] = a[3] }
    }
    function cite(file, rng, at,   lo, hi, i, hit) {
        if (file ~ /\.md$/ || file == "") return
        lo = rng; hi = rng
        if (index(rng, "-")) { lo = substr(rng, 1, index(rng, "-") - 1); hi = substr(rng, index(rng, "-") + 1) }
        hit = 0
        for (i = 1; i <= n; i++)
            if ((rp[i] == file || substr(rp[i], length(rp[i]) - length(file)) == "/" file) &&
                rl[i] + 0 == lo + 0 && rh[i] + 0 == hi + 0) { hit = 1; used[i] = 1 }
        if (!hit) printf "FAIL %s:%d cites %s:%s and no row checks it\n", FILENAME, at, file, rng
    }
    /^[ \t]*$/ { last = ""; next }
    {
        s = $0
        while (match(s, /[A-Za-z0-9_.\/-]+\.(cpp|h|cmake|md):[0-9]+(-[0-9]+)?|`:[0-9]+(-[0-9]+)?/)) {
            m = substr(s, RSTART, RLENGTH); s = substr(s, RSTART + RLENGTH)
            if (substr(m, 1, 1) == "`") { f = last; r = substr(m, 3) }
            else { c = match(m, /:[0-9]/); f = substr(m, 1, c - 1); r = substr(m, c + 1); last = f }
            cite(f, r, NR)
            while (match(s, /^,[0-9]+(-[0-9]+)?/)) { cite(last, substr(s, 2, RLENGTH - 1), NR); s = substr(s, RLENGTH + 1) }
        }
    }
    END {
        for (i = 1; i <= n; i++)
            if (!used[i]) printf "FAIL row %s:%s-%s matches no citation in the note\n", rp[i], rl[i], rh[i]
    }' "$NOTE")
if [ -n "$cover" ]; then
    echo "$cover"
    fail=1
fi

if [ "$fail" -ne 0 ]; then
    echo ""
    echo "$NOTE cites lines that moved. Re-find each by the token shown, then"
    echo "update the note's number AND the row above in the same commit."
    exit 1
fi
echo "check_note_cites: $ok citation checks pass ($skip skipped for want of a build tree)"
