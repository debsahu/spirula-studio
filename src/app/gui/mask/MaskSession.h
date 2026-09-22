#pragma once

// The mask editor's session: the frames of a prepared dataset, the frame
// that is open with its document and picture, a worker that loads, saves and
// reverts, the view, and the stroke commit. Drawing and every GL call live in
// MaskPanel.cpp so this file links into mask_doc_test.
// Design: docs/notes/mask-editor.md.

#include "app/gui/GlLoader.h"
#include "app/gui/edit/EditTool.h"
#include "app/gui/mask/MaskDoc.h"
#include "app/gui/mask/MaskWindow.h"
#include "app/gui/mask/Livewire.h"
#include "app/gui/mask/PathTool.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace gui {

struct MaskSettings;

namespace mask {

struct FrameRef {
    std::string file;      // the image, absolute
    std::string key;       // frame_key under the image root
    std::string camera;    // the camera folder, "" for the image root
};

class MaskSam;

// The continuous inverse of to_stored (MaskDoc.h): a stored point to the
// displayed frame. Plan 1 has the pixel form only.
void to_displayed(const sfm::ExifTransform& t, int W, int H, float sx, float sy,
                  float& dx, float& dy);

class MaskSession {
public:
    MaskSession();
    ~MaskSession();
    MaskSession(const MaskSession&) = delete;
    MaskSession& operator=(const MaskSession&) = delete;

    // `workspace` is where mask_edits/ goes; `mask_flipped` is the convention
    // `mask_dir` is in (TrainConfig::flip_mask). False with `error` (a
    // sentence) on any of the five refusals in open().
    bool open(const std::string& workspace, const std::string& image_dir,
              const std::string& mask_dir, bool mask_flipped, std::string& error);
    // Where a failure that lands after the editor is gone goes -- a save that
    // fails while closing has no status strip left to reach.
    void set_log(std::function<void(const std::string&)> log) { _log = std::move(log); }
    bool is_open() const { return _open; }
    // Saves a dirty frame, then joins the worker.
    void close();
    void destroy_gl();
    void draw();

    // ---- read by the panel and the tests ----
    int frame_count() const { return (int)_frames.size(); }
    int frame_index() const { return _idx; }
    const std::vector<FrameRef>& frames() const { return _frames; }
    const std::string& layer_root() const { return _layer_root; }
    const std::string& mask_root() const { return _mask_root; }
    MaskDoc* doc() { return _doc.get(); }
    const MaskDoc* doc() const { return _doc.get(); }
    int shown_width() const { return _dw; }
    int shown_height() const { return _dh; }
    const sfm::ExifTransform& turn() const { return _turn; }
    bool idle() const { return _pending.load() == 0; }
    int corrected_count() const;
    std::string status() const;
    std::string error() const;
    double last_commit_ms() const { return _last_commit_ms; }
    // ONE radius, shared by the brush and the eraser: the operator wants the
    // size to carry when they switch tools mid-correction. A second copy is
    // the defect to avoid here, not a feature to add.
    bool erasing() const { return _erase; }
    void set_erasing(bool on) { _erase = on; }
    float radius() const { return _brush; }
    void set_radius(float r) { _brush = clamp_brush(r); }
    View& view() { return _view; }
    WindowSource window_source() const;
    // The open frame's pixels, co-owned: a holder keeps them past a frame change.
    std::shared_ptr<const std::vector<uint8_t>> frame_pixels() const { return _rgb; }

    // ---- SAM assist (MaskSam.h); every call is safe with no checkpoint ----
    // The SAM half, created on first use.
    MaskSam& sam();
    bool sam_available() const;
    // The process-wide inference pool, MiB -- readable with the editor closed.
    static double sam_pool_mib();
    // GuiApp's checkpoint, every frame; "" = not cached. A NEW path drops the
    // warm session (released once any job stops) and keeps the clicks.
    void set_sam_model(const std::string& path, bool text_prompts);
    const std::string& sam_model_path() const;
    int sam_model_changes() const { return _sam_model_changes; }
    // Draws GuiApp's model picker into the SAM strip; GuiApp owns the state.
    void set_model_picker(std::function<void()> draw) { _model_picker = std::move(draw); }
    bool sam_has_model() const { return !sam_model_path().empty(); }
    bool sam_text_supported() const;
    bool sam_busy() const;
    void sam_cancel();
    // The editor's own prompt state, created on first use; never the dataset's.
    MaskSettings& sam_prompt();
    int sam_click_count() const;
    int sam_object_count() const;
    // Frame pixels of the open frame. False when nothing started.
    bool sam_prompt_point(float frame_x, float frame_y, bool keep, bool positive = true);
    bool sam_prompt_text(const std::string& phrases);
    // Paints a finished result onto the open frame; the DISPLAYED rect changed.
    Rect sam_pump();
    std::string sam_status() const;
    std::string sam_error() const;
    double sam_vram_mib() const;
    // Why another inference user bars SAM here, "" when none does: every
    // sam::Session shares the pool's slots and one unsynchronised stream.
    static std::string sam_blocker(bool mask_preview, bool depth_preview, bool run_active);
    // GuiApp's sam_blocker() answer, every frame before draw(); a blocked
    // prompt is refused with it in sam_error().
    void set_sam_blocker(const std::string& reason);
    // Cancels, joins and unloads, before another inference user starts. Keeps
    // the clicks; the next prompt reloads. Returns the milliseconds joined.
    double sam_yield();
    // What a job is stamped with and a result must still match: the frame's
    // key AND the document generation, since a revert reopens the same key.
    std::string sam_frame_stamp() const;
    // `last_ms` is prompt -> painted, stamped on this thread; `last_job_ms` is
    // the job's own time. Both cover a result that painted nothing.
    int sam_results() const { return _sam_results; }
    int sam_dropped() const { return _sam_dropped; }
    double sam_last_ms() const { return _sam_last_ms; }
    double sam_last_job_ms() const { return _sam_last_job_ms; }
    float sam_last_score() const { return _sam_last_score; }
    // The stencil's pixel count, NOT the pixels that changed: a paint over
    // pixels already dropped reports its full area and changes nothing.
    int64_t sam_last_area() const { return _sam_last_area; }
    int sam_last_detections() const { return _sam_last_detections; }

    // ---- actions ----
    void go_to(int i);
    void pump();
    // A finished stroke in pane pixels under `m`: mapped, rasterised, painted.
    // Returns the DISPLAYED rectangle that changed; empty when nothing did.
    Rect commit_stroke(const ShapeStroke& pane_stroke, Paint mode, const Mapping& m);
    // The mode a stroke commits with, from the modifiers on the frame it
    // completes and the tool it was drawn with.
    static Paint paint_for(bool shift, bool ctrl, bool erasing);
    Paint paint_now(bool shift, bool ctrl) const { return paint_for(shift, ctrl, _erase); }
    // The radius arithmetic, all of it, in mask pixels. clamp_brush is the
    // one place [kMinBrush, kMaxBrush] is enforced -- and it folds NaN to the
    // minimum, which std::clamp would propagate instead.
    static constexpr float kMinBrush = 1.0f;
    static constexpr float kMaxBrush = 4096.0f;
    static float clamp_brush(float r);
    static float scale_brush(float r, float factor);
    // `[`/`]`: `grow` true widens by 1.18x, false narrows by 0.85x.
    static float step_brush(float r, bool grow);
    // Alt+wheel, one notch per `]`, reciprocal so a notch back undoes it.
    static float wheel_brush(float r, float wheel);
    Rect undo();
    Rect redo();
    void save();
    void revert_open_frame();
    void revert_every_frame();

private:
    struct Loaded {
        std::unique_ptr<MaskDoc> doc;
        std::vector<uint8_t> rgb;
        int fw = 0, fh = 0;
        sfm::ExifTransform turn;
        int index = -1;
    };
    void enqueue(std::function<void()> job);
    void worker_main();
    void load_frame(int i);
    void post_status(const std::string& s, bool error);
    // `sticky` marks a failed write, which a later successful load must not
    // clear: the work it lost is still lost.
    void post_error(const std::string& s, bool sticky);
    void set_corrected(int n);
    Rect shown_rect(const Rect& stored) const;
    // MaskPanel.cpp
    // The three ways a tool is chosen, so the toolbar and the key handler
    // cannot drift apart over which flags a switch clears.
    void pick_tool(ToolId t);
    void pick_eraser();
    void pick_path();
    void draw_toolbar();
    void draw_canvas();
    void draw_status();
    void handle_keys(const Mapping& m);
    void ensure_window(const Mapping& m, float pane_w, float pane_h);
    void upload_rect(const Rect& shown);
    // The pen tool (MaskPanel.cpp drives it; these two have no ImGui).
    void ensure_livewire();
    PathSpace path_space(const Mapping& m) const;

    bool _open = false;
    // Set once in open() before the worker starts, read by both threads
    // thereafter, cleared only after close()'s join(): safe by ordering,
    // not by exclusivity -- neither field is ever mutable mid-session.
    std::string _workspace, _image_root, _mask_root, _layer_root;
    std::vector<FrameRef> _frames;
    int _idx = -1;

    // The open frame, UI thread.
    std::unique_ptr<MaskDoc> _doc;
    // Co-owned with any SAM job still reading it, so replacing or dropping it
    // here never frees what a job holds. const: nothing may refill it in place.
    std::shared_ptr<const std::vector<uint8_t>> _rgb;
    int _fw = 0, _fh = 0;
    sfm::ExifTransform _turn;
    int _dw = 0, _dh = 0;
    View _view;
    EditTool _tool;
    // The tool is only ever touched from MaskPanel.cpp (EditTool.cpp needs
    // imgui); a frame change asks it to reset through this flag.
    bool _tool_reset = false;
    float _brush = 24.0f;            // mask pixels, the brush's AND the eraser's
    bool _erase = false;
    bool _panning = false;
    double _last_commit_ms = 0.0;

    PathTool _path;
    std::unique_ptr<Livewire> _livewire;   // the open frame's edge map, built on first use
    // Created on first use; its session is released by sam_yield() and a model
    // change, and the object (clicks included) outlives close().
    std::unique_ptr<MaskSam> _sam;
    std::string _sam_model;
    bool _sam_text_hint = false;
    bool _sam_release_pending = false;   // a model change waiting for the job to stop
    int _sam_model_changes = 0;
    std::function<void()> _model_picker;
    std::chrono::steady_clock::time_point _sam_t0{};
    int _sam_results = 0, _sam_dropped = 0, _sam_last_detections = 0;
    double _sam_last_ms = 0.0, _sam_last_job_ms = 0.0;
    float _sam_last_score = 0.0f;
    int64_t _sam_last_area = 0;
    std::string _sam_blocker;
    uint64_t _doc_gen = 0;           // bumped where pump() installs a _doc; never reset
    bool _path_mode = false;
    double _livewire_ms = 0.0;

    // What draw_status() took last frame, so draw_canvas() can reserve it
    // instead of a constant. 0 until the first frame has been drawn.
    float _status_h = 0.0f;

    // MaskPanel.cpp's texture and window.
    GLuint _tex = 0;
    Window _win;
    bool _win_dirty = true;
    std::vector<uint8_t> _rgba;
    int _slider_idx = 0;
    bool _close_requested = false;

    // The worker and what it hands back.
    std::thread _worker;
    std::mutex _qmu;
    std::condition_variable _qcv;
    std::deque<std::function<void()>> _queue;
    bool _quit = false;
    std::atomic<int> _pending{0};
    LayerIndex _index;               // worker thread only, after open()
    mutable std::mutex _mu;
    Loaded _loaded;                  // guarded by _mu
    bool _loaded_ready = false;      // guarded by _mu
    std::string _saved_key;          // guarded by _mu
    uint64_t _saved_rev = 0;         // guarded by _mu
    bool _saved_comp = false;        // guarded by _mu
    bool _saved_ready = false;       // guarded by _mu
    std::string _status, _error;     // guarded by _mu
    bool _error_sticky = false;      // guarded by _mu
    int _corrected = 0;              // guarded by _mu
    std::function<void(const std::string&)> _log;
};

}  // namespace mask
}  // namespace gui
