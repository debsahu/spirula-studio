#pragma once

// The training screen's two "new points for the cameras a dataset already has"
// rows, which stay byte for byte as they are. Points: `spirula sfm auto
// --poses`, whose points replace the model's own, kept as *_original
// (docs/notes/fixed-poses.md). Dense: `spirula densify`, which writes a sibling
// model and replaces nothing (docs/notes/densify.md).

#include "app/gui/SfmProgress.h"

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace gui {

class RecomputePanel {
public:
    enum class Kind { Points, Dense };
    explicit RecomputePanel(Kind kind = Kind::Points) : _kind(kind) {}

    struct Source {
        std::string dataset;
        std::string image_dir, mask_dir;   // as the trainer has them, may be relative
        bool mask_flipped = false;
        // Dense: the model the trainer is set to, "" for its own pick.
        std::string recon_dir;
        // Dense: draws the checkpoint's state for the chosen source (a kDensifySources
        // index) and returns whether a run can start.
        std::function<bool(int)> draw_checkpoint;
    };
    ~RecomputePanel();

    // Above the region row. `busy`: training owns the dataset. `start` asks
    // the app for the device to run on, "" for automatic, or false to refuse.
    void draw(const Source& src, bool busy, const std::function<bool(std::string&)>& start);
    bool running() const { return _running.load(); }
    void cancel();
    // Lines for the log since the last call: (text, detail).
    std::vector<std::pair<std::string, bool>> drain_log();
    // True once after the model's points changed, recomputed or restored.
    bool take_changed();

private:
    void draw_points(const Source& src, bool busy, const std::function<bool(std::string&)>& start);
    void draw_dense(const Source& src, bool busy, const std::function<bool(std::string&)>& start);
    void draw_progress();
    void run(std::vector<std::string> argv, std::string out_dir, std::string model);
    void run_dense(std::vector<std::string> argv, std::string dataset, std::string out_dir);
    void log(const std::string& s, bool detail = false);

    Kind _kind;
    bool _open = false;
    std::string _model_for, _model;   // the dataset, and the model it would keep
    std::atomic<bool> _restorable{false};   // _model holds a recompute and its originals
    int _quality = 2;   // kSfmQuality
    int _features = 0;  // kSfmFeatures
    int _max_features = 0, _max_image_size = 0;
    bool _use_masks = true;

    // Dense: the preset, overrides and what the run last said.
    int _source = 0, _preset = 0, _refs = 0, _neighbours = 0, _rule = 0, _matches = 0, _max_points = 0,
        _min_track = 0;
    bool _replace = false;
    std::atomic<bool> _out_exists{false};
    std::string _recon_for;
    std::atomic<int64_t> _dense_done{0}, _dense_total{0};

    std::thread _worker;
    std::atomic<bool> _running{false}, _cancel{false}, _changed{false};
    std::mutex _mu;
    std::vector<std::pair<std::string, bool>> _log;
    std::string _progress_dir;
    int64_t _status_mtime = 0;
    RunStatus _status;
};

}  // namespace gui
