// MaskDoc.cpp -- see MaskDoc.h.

#include "app/gui/mask/MaskDoc.h"

#include "app/FrameMask.h"
#include "app/gui/edit/Selection.h"

#include <algorithm>
#include <cmath>
#include <filesystem>

namespace fs = std::filesystem;

namespace gui {
namespace mask {

Rect clip(const Rect& r, int W, int H) {
    Rect c;
    c.x0 = std::max(0, r.x0);
    c.y0 = std::max(0, r.y0);
    c.x1 = std::min(W, r.x1);
    c.y1 = std::min(H, r.y1);
    return c;
}

Rect join(const Rect& a, const Rect& b) {
    if (a.empty()) return b;
    if (b.empty()) return a;
    return {std::min(a.x0, b.x0), std::min(a.y0, b.y0),
            std::max(a.x1, b.x1), std::max(a.y1, b.y1)};
}

// ---- Task 7 replaces these four with the real mapping ----
void to_stored(const sfm::ExifTransform&, int, int, float dx, float dy, float& sx, float& sy) {
    sx = dx;
    sy = dy;
}
void to_stored(const sfm::ExifTransform&, int, int, int dx, int dy, int& sx, int& sy) {
    sx = dx;
    sy = dy;
}
void to_displayed(const sfm::ExifTransform&, int, int, int sx, int sy, int& dx, int& dy) {
    dx = sx;
    dy = sy;
}
ShapeStroke stroke_to_stored(const ShapeStroke& s, const sfm::ExifTransform&, int, int) {
    return s;
}
Rect rect_to_displayed(const Rect& stored, const sfm::ExifTransform&, int, int) {
    return stored;
}

Rect stroke_bounds(const ShapeStroke& s, int W, int H) {
    if (s.pts.size() < 2) return {};
    float x0 = s.pts[0], y0 = s.pts[1], x1 = x0, y1 = y0;
    for (size_t i = 0; i + 1 < s.pts.size(); i += 2) {
        x0 = std::min(x0, s.pts[i]);
        x1 = std::max(x1, s.pts[i]);
        y0 = std::min(y0, s.pts[i + 1]);
        y1 = std::max(y1, s.pts[i + 1]);
    }
    const float r = s.kind == ShapeKind::Brush ? std::max(s.brush_radius, 1.0f) : 0.0f;
    const Rect b{(int)std::floor(x0 - r) - 1, (int)std::floor(y0 - r) - 1,
                 (int)std::ceil(x1 + r) + 2, (int)std::ceil(y1 + r) + 2};
    return clip(b, W, H);
}

// ---------------------------------------------------------------------------
// MaskDoc
// ---------------------------------------------------------------------------

bool MaskDoc::load(const std::string& layer_root, const std::string& mask_root,
                   const std::string& key, int w, int h, LayerIndex& idx,
                   std::string& error, std::string& warning) {
    _key = key;
    _ops.clear();
    _head = 0;
    _bytes = 0;
    _revision = _saved = 0;
    _last = Rect{};
    if (!recomposite_frame(layer_root, mask_root, key, idx, _state, error)) return false;
    const std::string base_path = layer_file(layer_root, key, Layer::Base);
    const std::string mask_path = mask_file(mask_root, key);
    std::error_code ec;
    const std::string src = fs::exists(base_path, ec) ? base_path
                          : fs::exists(mask_path, ec) ? mask_path : std::string();
    if (src.empty()) {
        _w = w;
        _h = h;
        _base.assign((size_t)w * h, 255);
    } else if (!app::load_stencil(src, _w, _h, _base)) {
        error = src;
        return false;
    }
    FrameLayers layers;
    read_layers(layer_root, key, _w, _h, layers, warning);
    _drop.swap(layers.drop);
    _keep.swap(layers.keep);
    _composite.resize(_base.size());
    mask::composite(_base.data(), _drop.data(), _keep.data(), _base.size(), _composite.data());
    _kept = 0;
    for (uint8_t v : _composite) _kept += v ? 1 : 0;
    return true;
}

float MaskDoc::kept_fraction() const {
    const size_t n = _composite.size();
    return n ? (float)_kept / (float)n : 0.0f;
}

void MaskDoc::recomposite(const Rect& r) {
    for (int y = r.y0; y < r.y1; y++) {
        const size_t row = (size_t)y * _w;
        for (int x = r.x0; x < r.x1; x++) {
            const size_t i = row + (size_t)x;
            const uint8_t was = _composite[i];
            const uint8_t now = _keep[i] ? 255 : _drop[i] ? 0 : _base[i];
            _composite[i] = now;
            _kept += (now ? 1 : 0) - (was ? 1 : 0);
        }
    }
}

void MaskDoc::paint_rect(Paint mode, const Stencil& st, const Rect& r) {
    for (int y = r.y0; y < r.y1; y++) {
        const size_t row = (size_t)y * _w;
        for (int x = r.x0; x < r.x1; x++) {
            const size_t i = row + (size_t)x;
            if (!st.in[i]) continue;
            switch (mode) {
                case Paint::ForceDrop: _drop[i] = 255; _keep[i] = 0; break;
                case Paint::ForceKeep: _keep[i] = 255; _drop[i] = 0; break;
                default:               _drop[i] = 0;   _keep[i] = 0; break;
            }
        }
    }
    recomposite(r);
}

void MaskDoc::read_rect(const Rect& r, std::vector<uint8_t>& drop_r,
                        std::vector<uint8_t>& keep_r) const {
    const size_t n = (size_t)std::max(0, r.w()) * (size_t)std::max(0, r.h());
    drop_r.resize(n);
    keep_r.resize(n);
    for (int y = r.y0; y < r.y1; y++) {
        const size_t src = (size_t)y * _w + (size_t)r.x0;
        const size_t dst = (size_t)(y - r.y0) * (size_t)r.w();
        std::copy(_drop.begin() + (ptrdiff_t)src, _drop.begin() + (ptrdiff_t)(src + r.w()),
                  drop_r.begin() + (ptrdiff_t)dst);
        std::copy(_keep.begin() + (ptrdiff_t)src, _keep.begin() + (ptrdiff_t)(src + r.w()),
                  keep_r.begin() + (ptrdiff_t)dst);
    }
}

void MaskDoc::write_rect(const Rect& r, const uint8_t* drop_r, const uint8_t* keep_r) {
    for (int y = r.y0; y < r.y1; y++) {
        const size_t dst = (size_t)y * _w + (size_t)r.x0;
        const size_t src = (size_t)(y - r.y0) * (size_t)r.w();
        std::copy(drop_r + src, drop_r + src + r.w(), _drop.begin() + (ptrdiff_t)dst);
        std::copy(keep_r + src, keep_r + src + r.w(), _keep.begin() + (ptrdiff_t)dst);
    }
    recomposite(r);
}

// ---- Task 6 replaces this block with the StrokeOp history ----
void MaskDoc::paint(Paint mode, Stencil st, const Rect& bounds) {
    const Rect r = clip(bounds, _w, _h);
    if (r.empty() || st.W != _w || st.H != _h) return;
    paint_rect(mode, st, r);
    _last = r;
    _revision++;
}
void MaskDoc::run(std::unique_ptr<MaskOp>) {}
void MaskDoc::undo() {}
void MaskDoc::redo() {}
const spirula::i18n::Msg* MaskDoc::last_label() const { return nullptr; }

bool MaskDoc::save(const std::string& layer_root, const std::string& mask_root,
                   LayerIndex& idx, std::string& error) {
    const bool write_comp = _state != BaseState::Missing;
    if (!save_frame(layer_root, mask_root, _key, _w, _h, _base.data(), _drop.data(),
                    _keep.data(), write_comp, idx, error))
        return false;
    _saved = _revision;
    if (write_comp) _state = BaseState::Unchanged;
    return true;
}

}  // namespace mask
}  // namespace gui
