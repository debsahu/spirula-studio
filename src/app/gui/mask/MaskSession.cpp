// MaskSession.cpp -- see MaskSession.h.

#include "app/gui/mask/MaskSession.h"
#include "app/gui/mask/MaskSam.h"
#include "app/gui/MaskSettings.h"

#include "app/FrameLook.h"
#include "app/FrameMask.h"
#include "i18n/catalog/Dataset.h"
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

std::vector<int> propagate_targets(const std::vector<FrameRef>& frames, int src,
                                   PropagateScope scope, int lo, int hi) {
    std::vector<int> out;
    const int n = (int)frames.size();
    if (src < 0 || src >= n) return out;
    const std::string& cam = frames[(size_t)src].camera;
    int a = 0, b = n - 1;
    if (scope == PropagateScope::Next) a = b = src + 1;
    if (scope == PropagateScope::Range) {
        a = std::max(lo, 0);
        b = std::min(hi, n - 1);
    }
    for (int i = a; i <= b && i < n; i++)
        if (i != src && i >= 0 && frames[(size_t)i].camera == cam) out.push_back(i);
    return out;
}

MaskSession::MaskSession()
    : _sam_ops{[](MaskSam& s) { return s.busy(); }, [](MaskSam& s) { return s.release(); }} {}

MaskSession::~MaskSession() {
    close();
    sam_drain_retiring();
}

bool MaskSession::open(const std::string& workspace, const std::string& image_dir,
                       const std::string& mask_dir, bool mask_flipped,
                       std::string& error) {
    close();
    sam_drain_retiring();
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
    close_sam();   // order vs the worker join is free: the save never touches SAM
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
    sam_forget();
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
    _doc_gen++;   // by construction: every _doc arrives here
    _shown_valid = false;
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
    sam_revert(i);
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
    sam_revert(-1);
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

Style pane_style_for(Peek peek, ViewMode view, int pane) {
    if (view == ViewMode::SideBySide) {
        // Both bare views are on screen already, so the peek turns ONE pane
        // into the overlay and leaves the other as the reference.
        if (peek == Peek::Photo) return pane == 1 ? Style::Overlay : Style::Photo;
        if (peek == Peek::Mask) return pane == 0 ? Style::Overlay : Style::MaskOnly;
        return pane == 0 ? Style::Photo : Style::MaskOnly;
    }
    if (peek == Peek::Photo) return Style::Photo;
    if (peek == Peek::Mask) return Style::MaskOnly;
    if (view == ViewMode::MaskOnly) return Style::MaskOnly;
    return Style::Overlay;
}

PaneDerive plan_derive(bool dirty, const Window& want, ViewMode view, Peek peek, Window& win0,
                       Style& style0, Window& win1, Style& style1) {
    const bool two = view == ViewMode::SideBySide;
    const Style s0 = pane_style_for(peek, view, 0), s1 = pane_style_for(peek, view, 1);
    if (!two) win1 = Window{};
    PaneDerive d;
    d.left = dirty || !same_window(want, win0) || s0 != style0;
    d.right = two && (dirty || !same_window(want, win1) || s1 != style1);
    if (d.left) {
        win0 = want;
        style0 = s0;
    }
    if (d.right) {
        win1 = want;
        style1 = s1;
    }
    return d;
}

int pane_at(float x, int panes, float pane_w, float gap) {
    const float stride = pane_w + gap;
    if (panes < 1 || x < 0.0f || stride <= 0.0f) return -1;
    const int p = (int)std::floor(x / stride);
    if (p >= panes || x - (float)p * stride >= pane_w) return -1;
    return p;
}

ViewMode switch_view(ViewMode cur, ViewMode want, bool shape_in_progress) {
    return shape_in_progress ? cur : want;
}

// Cleared after the release frame is answered: that frame ends the drag.
int MaskSession::bind_pane(int hover, bool pressed, bool down, int panes) {
    if (_held_pane >= panes) _held_pane = -1;
    if (pressed && hover >= 0) _held_pane = hover;
    const int p = _held_pane >= 0 ? _held_pane : std::max(0, hover);
    if (!down) _held_pane = -1;
    return p;
}

// Pane px -> displayed mask px (the mapping) -> stored mask px (the EXIF
// turn) -> stored frame px (the mask-to-frame scale), and back.
void MaskSession::note_shown(const Mapping& m, float origin_x, float origin_y, int panes,
                             float pane_w, float gap) {
    _shown = m;
    _shown_x = origin_x;
    _shown_y = origin_y;
    _shown_panes = std::max(1, panes);
    _shown_pane_w = pane_w;
    _shown_gap = gap;
    _shown_valid = true;
}

bool MaskSession::shown_to_frame(float screen_x, float screen_y, float& fx, float& fy) const {
    if (!_shown_valid || !_doc) return false;
    float x = screen_x - _shown_x;
    if (_shown_panes > 1) {
        const int p = pane_at(x, _shown_panes, _shown_pane_w, _shown_gap);
        if (p < 0) return false;
        x -= pane_left(p, _shown_pane_w, _shown_gap);
    }
    path_space(_shown).to_frame(x, screen_y - _shown_y, fx, fy);
    return true;
}

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

// ---------------------------------------------------------------------------
// SAM assist
// ---------------------------------------------------------------------------

bool MaskSession::sam_available() const { return MaskSam::available(); }

double MaskSession::sam_pool_mib() { return MaskSam::pool_mib(); }

int MaskSession::sam_loads() { return MaskSam::load_count(); }

// Idempotent: GuiApp calls it every frame. A changed path cancels the job and
// defers the release to sam_pump(), so the UI thread never joins an encode.
void MaskSession::set_sam_model(const std::string& path, bool text_prompts) {
    _sam_text_hint = text_prompts;
    if (path != _sam_model) {
        _sam_model = path;
        _sam_model_changes++;
        if (_sam) {
            _sam->cancel();
            _sam_release_pending = true;
        }
    }
    if (_sam) _sam->set_model(path, text_prompts);
}

MaskSam& MaskSession::sam() {
    if (!_sam) {
        _sam = std::make_unique<MaskSam>();
        _sam->set_model(_sam_model, _sam_text_hint);
    }
    return *_sam;
}

bool MaskSession::sam_text_supported() const {
    return _sam ? _sam->text_supported() : MaskSam::available() && _sam_text_hint;
}

bool MaskSession::sam_busy() const { return _sam && _sam->busy(); }

void MaskSession::sam_cancel() {
    if (_sam) _sam->cancel();
}

MaskSettings& MaskSession::sam_prompt() { return sam().prompt(); }

int MaskSession::sam_click_count() const {
    return _sam ? (int)_sam->prompt().clicks.size() : 0;
}

int MaskSession::sam_object_count() const { return _sam ? _sam->prompt().object_count : 0; }

std::string MaskSession::sam_status() const { return _sam ? _sam->status() : std::string(); }

std::string MaskSession::sam_error() const { return _sam ? _sam->error() : std::string(); }

std::string MaskSession::sam_blocker(bool mask_preview, bool depth_preview, bool run_active) {
    if (run_active) return msg::sam_blocked_run.get();
    if (mask_preview || depth_preview) return msg::sam_blocked_preview.get();
    return {};
}

// Records why SAM is paused; never joins or yields (stop_inference_users()
// does that). A lifted pause takes its own message with it and nothing else.
void MaskSession::set_sam_blocker(const std::string& reason) {
    if (reason == _sam_blocker) return;
    if (_sam && !_sam_blocker.empty()) _sam->clear_error_if(_sam_blocker);
    _sam_blocker = reason;
}

// Everything but the picker, which draws GuiApp's own state, and the counter
// of model changes. The clicks go too: their frame indices name this session.
void MaskSession::sam_forget() {
    _sam.reset();
    _sam_model.clear();
    _sam_text_hint = false;
    _sam_release_pending = false;
    _sam_results = _sam_dropped = _sam_last_detections = 0;
    _sam_last_ms = _sam_last_job_ms = 0.0;
    _sam_last_score = 0.0f;
    _sam_last_area = 0;
    _sam_last_vetoed = false;
    _sam_ui_ms = 0.0;
    _sam_click_x = _sam_click_y = -1.0f;
    _sam_job_object = -1;
    _sam_add_key.clear();
    _sam_add_step = 0;
    _sam_add_object = -1;
    _sam_add_mode = Paint::ForceDrop;
    _sam_held.clear();
    _sam_reapply_ms = _sam_reapply_job_ms = _sam_margin_start_ms = 0.0;
    _sam_reapplies = _sam_margin_starts = 0;
    _sam_margin_pending = _sam_margin_moved = false;
    _sam_blocker.clear();
}

// An idle session is released here; a running job is cancelled and parked, as
// joining it froze the UI for the whole stage, 2887 ms mid-encode, 7805 mid-load.
void MaskSession::close_sam() {
    _sam_close_ms = 0.0;
    if (!_sam) return;
    const auto t0 = std::chrono::steady_clock::now();
    if (_sam_ops.busy(*_sam)) {
        _sam->cancel();
        sam_drain_retiring();
        _sam_retiring = std::move(_sam);
    } else {
        sam_yield();
    }
    _sam_close_ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

void MaskSession::sam_poll_retiring() {
    if (!_sam_retiring || _sam_ops.busy(*_sam_retiring)) return;
    const auto t0 = std::chrono::steady_clock::now();
    sam_drain_retiring();
    _sam_retire_ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

void MaskSession::sam_drain_retiring() {
    if (!_sam_retiring) return;
    _sam_retiring->cancel();
    _sam_ops.release(*_sam_retiring);
    _sam_retiring.reset();
}

double MaskSession::sam_yield() {
    const auto t0 = std::chrono::steady_clock::now();
    sam_drain_retiring();
    if (_sam) {
        _sam->cancel();
        if (_sam_ops.release(*_sam)) _sam_dropped++;
    }
    _sam_release_pending = false;
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0)
        .count();
}

std::string MaskSession::sam_frame_stamp() const {
    return _doc ? std::to_string(_doc_gen) + "|" + _doc->key() : std::string();
}

const std::string& MaskSession::sam_model_path() const {
    return _sam ? _sam->model_path() : _sam_model;
}


float MaskSession::sam_margin() const { return _sam ? _sam->prompt().dilate_ratio : -1.0f; }

const spirula::i18n::Msg* MaskSession::sam_empty_note() const {
    if (_sam_results == 0 || _sam_last_area != 0) return nullptr;
    return _sam_last_vetoed ? &msg::sam_vetoed_all : &msg::sam_empty;
}

double MaskSession::sam_vram_mib() const { return _sam ? _sam->vram_mib() : -1.0; }

// A click joins the current object on this frame, and the prompt is every click
// that object has here, "not this" ones included. It is recorded only once its
// job starts. A point off the frame is ignored quietly: SAM answers it with speckle.
bool MaskSession::sam_prompt_point(float frame_x, float frame_y, Paint mode, bool positive) {
    if (!_doc || !_rgb || _idx < 0 || !sam_has_model() || _sam_release_pending) return false;
    if (!(frame_x >= 0.0f && frame_y >= 0.0f && frame_x < (float)_fw && frame_y < (float)_fh))
        return false;
    if (!sam_gate_passes()) return false;
    _sam_click_x = frame_x;
    _sam_click_y = frame_y;
    MaskSam& sam = this->sam();
    const std::string& camera = _frames[(size_t)_idx].camera;
    std::vector<SamPoint> points =
        sam.prompt_points(_idx, camera, SamPoint{frame_x, frame_y, positive});
    if (!sam.start_points(sam_frame_stamp(), _rgb, _fw, _fh, _doc->width(), _doc->height(),
                          std::move(points), mode, sam_prompt().dilate_ratio))
        return false;
    sam_prompt_started(frame_x, frame_y, positive);
    return true;
}

// A refusal lands in sam_error(); the device is frozen only once no blocker
// stands, so a paused prompt never commits the app to a device.
bool MaskSession::sam_gate_passes() {
    if (!_sam_blocker.empty()) {
        sam().refuse(_sam_blocker);
        return false;
    }
    if (!_sam_device_gate) return true;
    std::string device, error;
    if (!_sam_device_gate(device, error)) {
        sam().refuse(error);
        return false;
    }
    sam().set_device(device);
    return true;
}

void MaskSession::sam_prompt_started(float frame_x, float frame_y, bool positive) {
    MaskSam& sam = this->sam();
    sam.add_click(_idx, _frames[(size_t)_idx].camera, frame_x, frame_y, positive);
    _sam_job_object = sam.prompt().current_object;
    _sam_t0 = std::chrono::steady_clock::now();
}

// A list with no phrase in it (sam::split_phrases would drop every entry) is
// refused here: a job would pay a ~1.5 s encode to find nothing.
bool MaskSession::sam_prompt_text(const std::string& phrases) {
    if (!_doc || !_rgb || _idx < 0 || !sam_has_model() || _sam_release_pending) return false;
    if (sam_phrases_blank(phrases)) return false;
    if (!sam_gate_passes()) return false;
    if (!sam().start_text(sam_frame_stamp(), _rgb, _fw, _fh, _doc->width(), _doc->height(),
                          phrases, sam_prompt().dilate_ratio))
        return false;
    _sam_job_object = -1;
    _sam_t0 = std::chrono::steady_clock::now();
    return true;
}

bool MaskSession::sam_phrases_blank(const std::string& phrases) {
    return std::all_of(phrases.begin(), phrases.end(),
                       [](char c) { return c == ';' || c == ' ' || c == '\t'; });
}

bool MaskSession::sam_submit_text() {
    return !sam_busy() && sam_prompt_text(sam_prompt().prompt);
}

std::string MaskSession::sam_text_refusal(bool has_model, bool text, const std::string& blocker,
                                          bool busy, const std::string& phrases) {
    if (!has_model) return spirula::i18n::msg::dataset::mask_model_first.get();
    if (!text) return msg::sam_text_unsupported.get();
    if (!blocker.empty()) return blocker;
    if (busy) return msg::sam_working.get();
    if (sam_phrases_blank(phrases)) return msg::sam_text_empty.get();
    return {};
}

std::string MaskSession::sam_text_refused() const {
    return sam_text_refusal(sam_has_model(), sam_text_supported(), _sam_blocker,
                            sam_busy() || _sam_release_pending || _sam_margin_pending,
                            _sam ? _sam->prompt().prompt : std::string());
}

Rect MaskSession::sam_pump() {
    if (!_sam) return {};
    // Held while the add is on top or a redo away; after that it never returns.
    if (!_sam_held.empty() && !sam_add_redoable()) _sam_held.clear();
    SamResult res;
    // A model change: the old model's answer, if any, is counted and dropped.
    if (_sam_release_pending) {
        if (_sam->busy()) return {};
        if (_sam->take_result(res)) _sam_dropped++;
        _sam->release();
        _sam_release_pending = false;
        return {};
    }
    if (!_doc) return {};
    // Read before the take: a job publishes before it stops, so a job idle here
    // has had its result taken below, and a margin job cannot overwrite it.
    const bool idle = !_sam->busy();
    Rect shown;
    if (_sam->take_result(res)) shown = sam_land(std::move(res));
    if (_sam_margin_pending && idle) sam_start_margin();
    return shown;
}

// A result that outlived its document -- a frame change, a revert, another
// dataset -- is counted and dropped, never painted onto what is open now; so
// is a margin that lands after another edit, rather than stacking on it.
Rect MaskSession::sam_land(SamResult res) {
    if (res.frame_key != sam_frame_stamp() || (res.margin_job && !sam_add_on_top(_sam_add_object))) {
        _sam_dropped++;
        // An undone add a redo can still bring back gets its detections back too.
        if (res.margin_job && res.frame_key == sam_frame_stamp() && sam_add_redoable())
            _sam_held = std::move(res.held);
        return {};
    }
    if (res.margin_job) {
        _sam_reapplies++;
        _sam_reapply_job_ms = res.ms;
        return apply_sam_add(std::move(res), _sam_add_object);
    }
    _sam_results++;
    _sam_last_job_ms = res.ms;
    _sam_last_score = res.score;
    _sam_last_detections = res.detections;
    const Rect shown = apply_sam_add(std::move(res), _sam_job_object);
    _sam_last_ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - _sam_t0)
            .count();
    return shown;
}

void MaskSession::sam_start_margin() {
    _sam_margin_pending = false;
    if (!sam_margin_reapplies()) return;
    if (_sam->start_margin(sam_frame_stamp(), std::move(_sam_held), _doc->width(), _doc->height(),
                           sam_prompt().dilate_ratio))
        _sam_margin_starts++;
}

// A job in flight lands as a plain add: the list's numbers now name other
// objects, so nothing may replace it (and drop its redo tail) later.
void MaskSession::sam_objects_edited() {
    _sam_add_object = -1;
    _sam_job_object = -1;
    _sam_held.clear();
}

// Kept, a reverted frame's clicks would re-prompt its next click with the
// correction just discarded. The phrase and exceptions are not per frame.
void MaskSession::sam_revert(int frame) {
    sam_objects_edited();
    _sam_add_key.clear();
    _sam_margin_pending = false;
    if (!_sam) return;
    MaskSettings& p = _sam->prompt();
    if (frame < 0) {
        const MaskSettings fresh;
        p.clicks.clear();
        p.object_count = fresh.object_count;
        p.current_object = fresh.current_object;
        return;
    }
    p.clicks.erase(std::remove_if(p.clicks.begin(), p.clicks.end(),
                                  [&](const MaskClick& c) { return c.frame == frame; }),
                   p.clicks.end());
}

// The stamp names the document (a reload moves it) and the step names the add
// itself, so "on top" means undo would take back exactly that add next.
bool MaskSession::sam_add_redoable() const {
    return _sam_add_object >= 0 && _doc && sam_frame_stamp() == _sam_add_key &&
           _doc->redo_reaches(_sam_add_step);
}

bool MaskSession::sam_add_on_top(int object) const {
    return object >= 0 && object == _sam_add_object && _doc && _doc->can_undo() &&
           sam_frame_stamp() == _sam_add_key && _doc->top_step() == _sam_add_step;
}

Rect MaskSession::apply_sam_add(SamResult res, int object) {
    _sam_last_area = 0;
    _sam_last_vetoed = res.vetoed_all;
    if (!_doc || !res.landed) return {};
    Rect changed;
    const bool replacing = sam_add_on_top(object);
    if (replacing) {
        _doc->undo();
        changed = _doc->last_change();
    }
    const uint64_t before = _doc->revision();
    _doc->paint(res.mode, std::move(res.stencil), res.bounds);
    // A paint that changed nothing records no step, so nothing of ours is on top,
    // and the add it replaced must not wait on the redo stack either.
    const bool painted = _doc->revision() != before;
    if (painted) changed = join(changed, _doc->last_change());
    else if (replacing) _doc->drop_redo();
    _sam_add_key = painted ? sam_frame_stamp() : std::string();
    _sam_add_step = _doc->top_step();
    _sam_add_object = painted ? object : -1;
    _sam_add_mode = res.mode;
    _sam_held = painted && object >= 0 ? std::move(res.held) : std::vector<HeldRegion>();
    _sam_last_area = res.set_px;
    return shown_rect(changed);
}

Paint MaskSession::sam_refine_mode(Paint fallback) const {
    return _sam && sam_add_on_top(_sam->prompt().current_object) ? _sam_add_mode : fallback;
}

Paint MaskSession::sam_click_mode(bool shift, bool ctrl) const {
    const Paint held = paint_now(shift, ctrl);
    return shift || ctrl ? held : sam_refine_mode(held);
}

bool MaskSession::sam_margin_reapplies() const {
    return !_sam_held.empty() && _sam_add_mode == Paint::ForceDrop &&
           sam_add_on_top(_sam_add_object);
}

size_t MaskSession::sam_held_bytes() const {
    size_t n = 0;
    for (const HeldRegion& h : _sam_held) n += h.mask.size();
    return n;
}

}  // namespace mask
}  // namespace gui
