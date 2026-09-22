// MaskSession.cpp -- see MaskSession.h.

#include "app/gui/mask/MaskSession.h"
#include "app/gui/mask/MaskSam.h"

#include "app/FrameLook.h"
#include "app/FrameMask.h"
#include "i18n/catalog/MaskEdit.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>

namespace fs = std::filesystem;
namespace msg = spirula::i18n::msg::maskedit;

namespace gui {
namespace mask {

namespace {

// Lexical, as DatasetPrep's own test is: does `p` sit inside `root`?
bool inside(const fs::path& p, const fs::path& root) {
    const fs::path rel = p.lexically_relative(root);
    return !rel.empty() && *rel.begin() != "..";
}

// Undoes read_layers' own ", "-joined `warning` (MaskLayer.h) so each
// mismatched file can be reported with ITS OWN size, not the first one's.
std::vector<std::string> split_paths(const std::string& joined) {
    std::vector<std::string> out;
    size_t start = 0;
    for (;;) {
        const size_t sep = joined.find(", ", start);
        out.push_back(joined.substr(start, sep - start));
        if (sep == std::string::npos) return out;
        start = sep + 2;
    }
}

// One sentence per mismatched file, each with its own size: a joined path list
// with one file's dimensions used to name every file it is not is worse than
// either a single file or an honest omission.
std::string size_mismatch_text(const std::string& joined, const MaskDoc& doc) {
    std::string text;
    for (const std::string& path : split_paths(joined)) {
        int lw = 0, lh = 0;
        app::image_size(path, lw, lh);
        if (!text.empty()) text += " ";
        text += spirula::i18n::format(msg::err_size_mismatch,
                                      {path, lw, lh, doc.width(), doc.height()});
    }
    return text;
}

}  // namespace

MaskSession::MaskSession() = default;
MaskSession::~MaskSession() { close(); }

bool MaskSession::open(const std::string& workspace, const std::string& image_dir,
                       const std::string& mask_dir, bool mask_flipped,
                       std::string& error) {
    close();
    std::error_code ec;
    _workspace = normalize_dir(fs::absolute(workspace, ec).string());
    _image_root = normalize_dir(fs::absolute(image_dir, ec).string());
    _mask_root = normalize_dir(fs::absolute(mask_dir, ec).string());
    _layer_root = (fs::path(_workspace) / kLayerDirName).string();
    if (_workspace == _image_root || inside(fs::path(_workspace), fs::path(_image_root))) {
        error = msg::err_workspace_inside_images.get();
        return false;
    }
    _frames.clear();
    const auto groups = app::group_frames_by_camera(_image_root, _mask_root);
    for (const auto& [camera, files] : groups)
        for (const std::string& f : files)
            _frames.push_back({f, frame_key(_image_root, f), camera});
    if (_frames.empty()) {
        error = spirula::i18n::format(msg::err_no_frames, {_image_root});
        return false;
    }
    // On this thread, before the worker: a corrupt index must refuse the
    // session, and the recorded root and convention are what every .base.png
    // means -- adopting different ones reinterprets all of them.
    LayerIndex idx;
    if (!idx.load(_layer_root, error)) {
        error = spirula::i18n::format(msg::err_read, {error});
        return false;
    }
    if (!idx.frames.empty()) {
        if (!idx.mask_root.empty() && idx.mask_root != _mask_root) {
            error = spirula::i18n::format(msg::err_other_mask_root, {idx.mask_root});
            return false;
        }
        if (idx.mask_flipped != mask_flipped) {
            error = msg::err_other_mask_polarity.get();
            return false;
        }
    }
    idx.mask_root = _mask_root;
    idx.mask_flipped = mask_flipped;
    _index = std::move(idx);
    _idx = -1;
    _doc.reset();
    _rgb.reset();
    _win_dirty = true;
    _close_requested = false;
    _tool_reset = true;
    {
        std::lock_guard<std::mutex> lk(_mu);
        _loaded = Loaded{};
        _loaded_ready = false;
        _saved_ready = false;
        _status = msg::working.get();
        _error.clear();
        _error_sticky = false;
        _corrected = (int)_index.frames.size();
    }
    _quit = false;
    _worker = std::thread([this] { worker_main(); });
    _open = true;
    load_frame(0);
    return true;
}

void MaskSession::close() {
    if (!_open && !_worker.joinable()) return;
    if (_doc && _doc->dirty()) save();
    {
        std::lock_guard<std::mutex> lk(_qmu);
        _quit = true;
    }
    _qcv.notify_all();
    if (_worker.joinable()) _worker.join();
    // The status strip is gone by now, so a write that failed on the way out
    // has nowhere else to be seen.
    if (_log) {
        std::string lost;
        {
            std::lock_guard<std::mutex> lk(_mu);
            if (_error_sticky) lost = _error;
        }
        if (!lost.empty()) _log(lost);
    }
    _quit = false;
    _open = false;
    _doc.reset();
    _rgb.reset();
    _frames.clear();
    _idx = -1;
    _workspace.clear();
    _image_root.clear();
    _mask_root.clear();
    _layer_root.clear();
    std::lock_guard<std::mutex> lk(_mu);
    _loaded = Loaded{};
    _loaded_ready = false;
    _saved_ready = false;
}

// ---------------------------------------------------------------------------
// The worker
// ---------------------------------------------------------------------------

void MaskSession::enqueue(std::function<void()> job) {
    _pending++;
    {
        std::lock_guard<std::mutex> lk(_qmu);
        _queue.push_back(std::move(job));
    }
    _qcv.notify_one();
}

// Drains the queue before quitting, so a save queued by close() lands.
void MaskSession::worker_main() {
    for (;;) {
        std::function<void()> job;
        {
            std::unique_lock<std::mutex> lk(_qmu);
            _qcv.wait(lk, [&] { return _quit || !_queue.empty(); });
            if (_queue.empty()) return;
            job = std::move(_queue.front());
            _queue.pop_front();
        }
        job();
        _pending--;
    }
}

void MaskSession::post_status(const std::string& s, bool error) {
    std::lock_guard<std::mutex> lk(_mu);
    if (error) _error = s;
    else _status = s;
}

void MaskSession::post_error(const std::string& s, bool sticky) {
    std::lock_guard<std::mutex> lk(_mu);
    _error = s;
    _error_sticky = _error_sticky || sticky;
}

void MaskSession::set_corrected(int n) {
    std::lock_guard<std::mutex> lk(_mu);
    _corrected = n;
}

int MaskSession::corrected_count() const {
    std::lock_guard<std::mutex> lk(_mu);
    return _corrected;
}

std::string MaskSession::status() const {
    std::lock_guard<std::mutex> lk(_mu);
    return _status;
}

std::string MaskSession::error() const {
    std::lock_guard<std::mutex> lk(_mu);
    return _error;
}

void MaskSession::load_frame(int i) {
    if (i < 0 || i >= frame_count()) return;
    const FrameRef f = _frames[(size_t)i];
    post_status(msg::working.get(), false);
    enqueue([this, f, i] {
        Loaded l;
        l.index = i;
        if (!app::load_rgb(f.file, l.fw, l.fh, l.rgb)) {
            post_error(spirula::i18n::format(msg::err_read, {f.file}), false);
            return;
        }
        l.turn = app::photo_turn(f.file);
        l.doc = std::make_unique<MaskDoc>();
        std::string err, warning;
        if (!l.doc->load(_layer_root, _mask_root, f.key, l.fw, l.fh, _index, err, warning)) {
            post_error(warning.empty() ? spirula::i18n::format(msg::err_read, {err})
                                       : size_mismatch_text(warning, *l.doc),
                       false);
            return;
        }
        set_corrected((int)_index.frames.size());
        std::lock_guard<std::mutex> lk(_mu);
        _loaded = std::move(l);
        _loaded_ready = true;
        _status.clear();
        if (!_error_sticky) _error.clear();
    });
}

void MaskSession::pump() {
    Loaded l;
    bool have_loaded = false, have_saved = false;
    std::string saved_key;
    uint64_t saved_rev = 0;
    bool saved_comp = false;
    {
        std::lock_guard<std::mutex> lk(_mu);
        if (_loaded_ready) {
            l = std::move(_loaded);
            _loaded_ready = false;
            have_loaded = true;
        }
        if (_saved_ready) {
            saved_key = _saved_key;
            saved_rev = _saved_rev;
            saved_comp = _saved_comp;
            _saved_ready = false;
            have_saved = true;
        }
    }
    if (have_saved && _doc && _doc->key() == saved_key) _doc->mark_saved(saved_rev, saved_comp);
    if (!have_loaded) return;
    _doc = std::move(l.doc);
    _rgb = std::make_shared<const std::vector<uint8_t>>(std::move(l.rgb));
    _fw = l.fw;
    _fh = l.fh;
    _turn = l.turn;
    _idx = l.index;
    _slider_idx = _idx;
    _dw = _doc->width();
    _dh = _doc->height();
    spirula::oriented_size(_turn.turns_cw, _dw, _dh);
    _view = View{1.0f, 0.5f * (float)_dw, 0.5f * (float)_dh};
    _tool_reset = true;
    _livewire.reset();
    _path.set_livewire(nullptr);
    _path.cancel();
    _win_dirty = true;
}

// ---------------------------------------------------------------------------
// Actions
// ---------------------------------------------------------------------------

void MaskSession::go_to(int i) {
    if (i < 0 || i >= frame_count() || i == _idx) return;
    if (_doc && _doc->dirty()) save();
    _doc.reset();
    _rgb.reset();
    load_frame(i);
}

// A snapshot of the planes goes to the worker, so painting can go on while
// an 8K frame encodes; the revision decides whether the document is still
// dirty when the save lands.
void MaskSession::save() {
    if (!_doc) return;
    struct Snap {
        std::string key;
        int w = 0, h = 0;
        std::vector<uint8_t> base, drop, keep;
        bool comp = false;
        uint64_t rev = 0;
    };
    auto s = std::make_shared<Snap>();
    s->key = _doc->key();
    s->w = _doc->width();
    s->h = _doc->height();
    s->base = _doc->base();
    s->drop = _doc->drop();
    s->keep = _doc->keep();
    s->comp = _doc->base_state() != BaseState::Missing;
    s->rev = _doc->revision();
    enqueue([this, s] {
        std::string err;
        if (!save_frame(_layer_root, _mask_root, s->key, s->w, s->h, s->base.data(),
                        s->drop.data(), s->keep.data(), s->comp, _index, err)) {
            post_error(spirula::i18n::format(msg::err_write, {err}), true);
            return;
        }
        set_corrected((int)_index.frames.size());
        std::lock_guard<std::mutex> lk(_mu);
        _saved_key = s->key;
        _saved_rev = s->rev;
        _saved_comp = s->comp;
        _saved_ready = true;
        _status = msg::status_saved.get();
        _error.clear();
        _error_sticky = false;
    });
}

void MaskSession::revert_open_frame() {
    if (!_doc) return;
    const std::string key = _doc->key();
    const int i = _idx;
    _doc.reset();
    _rgb.reset();
    enqueue([this, key] {
        std::string err;
        if (!mask::revert_frame(_layer_root, _mask_root, key, _index, err))
            post_error(spirula::i18n::format(msg::err_write, {err}), true);
        set_corrected((int)_index.frames.size());
    });
    _idx = -1;
    load_frame(i);
}

void MaskSession::revert_every_frame() {
    const int i = _idx;
    _doc.reset();
    _rgb.reset();
    enqueue([this] {
        std::string err;
        const bool flipped = _index.mask_flipped;
        if (mask::revert_all(_layer_root, err) < 0)
            post_error(spirula::i18n::format(msg::err_write, {err}), true);
        if (!_index.load(_layer_root, err)) _index = LayerIndex{};
        _index.mask_root = _mask_root;
        _index.mask_flipped = flipped;
        set_corrected((int)_index.frames.size());
    });
    _idx = -1;
    if (i >= 0) load_frame(i);
}

Rect MaskSession::shown_rect(const Rect& stored) const {
    if (!_doc) return {};
    return rect_to_displayed(stored, _turn, _doc->width(), _doc->height());
}

Rect MaskSession::commit_stroke(const ShapeStroke& pane_stroke, Paint mode, const Mapping& m) {
    if (!_doc) return {};
    ShapeStroke shown = pane_stroke;
    for (size_t i = 0; i + 1 < shown.pts.size(); i += 2) {
        shown.pts[i] = m.to_mask_x(pane_stroke.pts[i]);
        shown.pts[i + 1] = m.to_mask_y(pane_stroke.pts[i + 1]);
    }
    shown.brush_radius = pane_stroke.brush_radius / m.scale;
    const int W = _doc->width(), H = _doc->height();
    const ShapeStroke stored = stroke_to_stored(shown, _turn, W, H);
    const Rect r = stroke_bounds(stored, W, H);
    if (r.empty()) return {};
    Stencil st;
    rasterize_shape(stored, W, H, st);
    _doc->paint(mode, std::move(st), r);
    return shown_rect(_doc->last_change());
}

// Plain and Shift drop, Ctrl keeps, both held clears back to the base
// (Intersect in the 3D editor, meaningless on a layer). The eraser starts
// from keep instead, so Ctrl still means "the other one".
Paint MaskSession::paint_for(bool shift, bool ctrl, bool erasing) {
    if (shift && ctrl) return Paint::Clear;
    const bool keep = ctrl != erasing;
    return keep ? Paint::ForceKeep : Paint::ForceDrop;
}

// Written as a rejection test rather than std::clamp so that a NaN lands on
// the minimum: std::clamp returns it, and a NaN radius rasterizes nothing
// while the slider and the status strip still read a number.
float MaskSession::clamp_brush(float r) {
    if (!(r > kMinBrush)) return kMinBrush;
    return r < kMaxBrush ? r : kMaxBrush;
}

float MaskSession::scale_brush(float r, float factor) { return clamp_brush(r * factor); }

float MaskSession::step_brush(float r, bool grow) {
    return scale_brush(r, grow ? 1.18f : 0.85f);
}

float MaskSession::wheel_brush(float r, float wheel) {
    return scale_brush(r, std::pow(1.18f, wheel));
}

Rect MaskSession::undo() {
    if (!_doc || !_doc->can_undo()) return {};
    _doc->undo();
    return shown_rect(_doc->last_change());
}

Rect MaskSession::redo() {
    if (!_doc || !_doc->can_redo()) return {};
    _doc->redo();
    return shown_rect(_doc->last_change());
}

WindowSource MaskSession::window_source() const {
    WindowSource s;
    if (!_doc) return s;
    s.rgb = _rgb ? _rgb->data() : nullptr;
    s.fw = _fw;
    s.fh = _fh;
    s.composite = _doc->composite().data();
    s.drop = _doc->drop().data();
    s.keep = _doc->keep().data();
    s.W = _doc->width();
    s.H = _doc->height();
    s.turn = _turn;
    return s;
}

// Derived by inverting to_stored's switch, not through inverse_turn: the
// mirror is applied before the turn in one and after it in the other.
void to_displayed(const sfm::ExifTransform& t, int W, int H, float sx, float sy,
                  float& dx, float& dy) {
    int dw = W, dh = H;
    spirula::oriented_size(t.turns_cw, dw, dh);
    float mx;
    switch (t.turns_cw & 3) {
        case 1:  dy = sx;            mx = (float)H - sy; break;
        case 2:  mx = (float)W - sx; dy = (float)H - sy; break;
        case 3:  dy = (float)W - sx; mx = sy;            break;
        default: mx = sx;            dy = sy;            break;
    }
    dx = t.mirror ? (float)dw - mx : mx;
}

void MaskSession::ensure_livewire() {
    if (_livewire || !_doc || !_rgb || _rgb->empty()) return;
    const auto t0 = std::chrono::steady_clock::now();
    auto lw = std::make_unique<Livewire>();
    lw->build(_rgb->data(), _fw, _fh);
    _livewire_ms = std::chrono::duration<double, std::milli>(
                       std::chrono::steady_clock::now() - t0).count();
    _livewire = std::move(lw);
    _path.set_livewire(_livewire.get());
    char ms[32];
    std::snprintf(ms, sizeof ms, "%.0f", _livewire_ms);
    post_status(spirula::i18n::format(msg::path_edge_map,
                                      {_livewire->width(), _livewire->height(),
                                       _livewire->step(), std::string(ms)}),
                false);
}

// Pane px -> displayed mask px (the mapping) -> stored mask px (the EXIF
// turn) -> stored frame px (the mask-to-frame scale), and back.
PathSpace MaskSession::path_space(const Mapping& m) const {
    PathSpace s;
    const int W = _doc ? _doc->width() : 1, H = _doc ? _doc->height() : 1;
    const float kx = (float)_fw / (float)std::max(1, W), ky = (float)_fh / (float)std::max(1, H);
    const sfm::ExifTransform turn = _turn;
    s.to_frame = [m, turn, W, H, kx, ky](float x, float y, float& fx, float& fy) {
        float sx, sy;
        to_stored(turn, W, H, m.to_mask_x(x), m.to_mask_y(y), sx, sy);
        fx = sx * kx;
        fy = sy * ky;
    };
    s.from_frame = [m, turn, W, H, kx, ky](float fx, float fy, float& x, float& y) {
        float dx, dy;
        to_displayed(turn, W, H, fx / kx, fy / ky, dx, dy);
        x = m.to_screen_x(dx);
        y = m.to_screen_y(dy);
    };
    return s;
}

}  // namespace mask
}  // namespace gui
