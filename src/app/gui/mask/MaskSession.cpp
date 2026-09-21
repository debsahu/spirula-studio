// MaskSession.cpp -- see MaskSession.h.

#include "app/gui/mask/MaskSession.h"

#include "app/FrameLook.h"
#include "app/FrameMask.h"
#include "i18n/catalog/MaskEdit.h"

#include <algorithm>
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

}  // namespace

MaskSession::MaskSession() = default;
MaskSession::~MaskSession() { close(); }

bool MaskSession::open(const std::string& workspace, const std::string& image_dir,
                       const std::string& mask_dir, std::string& error) {
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
    _idx = -1;
    _doc.reset();
    _rgb.clear();
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
        _corrected = 0;
    }
    _quit = false;
    _worker = std::thread([this] { worker_main(); });
    _open = true;
    enqueue([this] {
        std::string err;
        if (!_index.load(_layer_root, err)) {
            post_status(spirula::i18n::format(msg::err_read, {err}), true);
            _index = LayerIndex{};
        }
        _index.mask_root = _mask_root;
        set_corrected((int)_index.frames.size());
    });
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
    _quit = false;
    _open = false;
    _doc.reset();
    _rgb.clear();
    _frames.clear();
    _idx = -1;
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
            post_status(spirula::i18n::format(msg::err_read, {f.file}), true);
            return;
        }
        l.turn = app::photo_turn(f.file);
        l.doc = std::make_unique<MaskDoc>();
        std::string err;
        if (!l.doc->load(_layer_root, _mask_root, f.key, l.fw, l.fh, _index, err, l.warning)) {
            post_status(spirula::i18n::format(msg::err_read, {err}), true);
            return;
        }
        set_corrected((int)_index.frames.size());
        std::lock_guard<std::mutex> lk(_mu);
        _loaded = std::move(l);
        _loaded_ready = true;
        _status.clear();
        _error.clear();
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
    _rgb = std::move(l.rgb);
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
    _win_dirty = true;
    if (!l.warning.empty()) {
        int lw = 0, lh = 0;
        app::image_size(l.warning.substr(0, l.warning.find(", ")), lw, lh);
        post_status(spirula::i18n::format(msg::err_size_mismatch,
                                          {l.warning, lw, lh, _doc->width(), _doc->height()}),
                    true);
    }
}

// ---------------------------------------------------------------------------
// Actions
// ---------------------------------------------------------------------------

void MaskSession::go_to(int i) {
    if (i < 0 || i >= frame_count() || i == _idx) return;
    if (_doc && _doc->dirty()) save();
    _doc.reset();
    _rgb.clear();
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
            post_status(spirula::i18n::format(msg::err_write, {err}), true);
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
    });
}

void MaskSession::revert_open_frame() {
    if (!_doc) return;
    const std::string key = _doc->key();
    const int i = _idx;
    _doc.reset();
    _rgb.clear();
    enqueue([this, key] {
        std::string err;
        if (!mask::revert_frame(_layer_root, _mask_root, key, _index, err))
            post_status(spirula::i18n::format(msg::err_write, {err}), true);
        set_corrected((int)_index.frames.size());
    });
    _idx = -1;
    load_frame(i);
}

void MaskSession::revert_every_frame() {
    const int i = _idx;
    _doc.reset();
    _rgb.clear();
    enqueue([this] {
        std::string err;
        if (mask::revert_all(_layer_root, err) < 0)
            post_status(spirula::i18n::format(msg::err_write, {err}), true);
        if (!_index.load(_layer_root, err)) _index = LayerIndex{};
        _index.mask_root = _mask_root;
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

// Plain and Shift add to the dropped set, Ctrl subtracts from it by forcing
// keep, and both held (Intersect in the 3D editor, meaningless on a layer)
// clears the correction. Mirrors EditSession::combine_now.
Paint MaskSession::paint_for(bool shift, bool ctrl) {
    if (shift && ctrl) return Paint::Clear;
    if (ctrl) return Paint::ForceKeep;
    return Paint::ForceDrop;
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
    s.rgb = _rgb.data();
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

}  // namespace mask
}  // namespace gui
