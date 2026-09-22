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
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace gui {
namespace mask {

struct FrameRef {
    std::string file;      // the image, absolute
    std::string key;       // frame_key under the image root
    std::string camera;    // the camera folder, "" for the image root
};

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

    // `workspace` is where mask_edits/ goes. False with `error` (a sentence)
    // when it sits inside `image_dir` or no frames are found.
    bool open(const std::string& workspace, const std::string& image_dir,
              const std::string& mask_dir, std::string& error);
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
    float brush_radius() const { return _brush; }
    void set_brush_radius(float r) { _brush = r; }
    View& view() { return _view; }
    WindowSource window_source() const;

    // ---- actions ----
    void go_to(int i);
    void pump();
    // A finished stroke in pane pixels under `m`: mapped, rasterised, painted.
    // Returns the DISPLAYED rectangle that changed; empty when nothing did.
    Rect commit_stroke(const ShapeStroke& pane_stroke, Paint mode, const Mapping& m);
    // EditSession::combine_now's grammar over the layers: the mode a stroke
    // commits with, from the modifiers on the frame it completes.
    static Paint paint_for(bool shift, bool ctrl);
    // `[`/`]`'s brush-radius step, clamped to [1, 4096] mask pixels: `grow`
    // true widens by 1.18x, false narrows by 0.85x.
    static float step_brush(float r, bool grow);
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
        std::string warning;
    };
    void enqueue(std::function<void()> job);
    void worker_main();
    void load_frame(int i);
    void post_status(const std::string& s, bool error);
    void set_corrected(int n);
    Rect shown_rect(const Rect& stored) const;
    // MaskPanel.cpp
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
    std::vector<uint8_t> _rgb;
    int _fw = 0, _fh = 0;
    sfm::ExifTransform _turn;
    int _dw = 0, _dh = 0;
    View _view;
    EditTool _tool;
    // The tool is only ever touched from MaskPanel.cpp (EditTool.cpp needs
    // imgui); a frame change asks it to reset through this flag.
    bool _tool_reset = false;
    float _brush = 24.0f;            // mask pixels
    bool _panning = false;
    double _last_commit_ms = 0.0;

    PathTool _path;
    std::unique_ptr<Livewire> _livewire;   // the open frame's edge map, built on first use
    bool _path_mode = false;
    double _livewire_ms = 0.0;
    // paint_for(shift, ctrl) at the first anchor -- a pen has no drag to read
    // a held modifier off at release, so it is captured once, at the click
    // that starts the path, and used whichever way the path later closes.
    Paint _path_paint = Paint::ForceDrop;

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
    int _corrected = 0;              // guarded by _mu
};

}  // namespace mask
}  // namespace gui
