// The growing dense cloud as model.bin snapshots in the run's --progress-dir,
// the same file the reconstruction writes and the dataset screen already draws.
// Design after spirula-studio#154 (D1odeKing, GPL-3.0): the child publishes,
// the GUI polls. docs/notes/densify.md.
#pragma once

#include <chrono>
#include <cstddef>
#include <string>
#include <vector>

#include "roma/Densify.h"
#include "sfm/core/Model.h"

namespace roma {

class CloudPreview {
public:
    // Cameras come from `model_dir`; its points are not drawn. A model that
    // cannot be read turns the preview off, never the run.
    explicit CloudPreview(const std::string& model_dir, double interval_s = 1.5,
                          size_t max_points = 50000);

    // Returns at once unless the interval has passed (or `final`). A write is a
    // strided slice of at most max_points; the next waits eight times as long
    // as this one took. Never throws.
    void update(const std::vector<DensePoint>& cloud, bool final = false);

    bool active() const { return active_; }
    int writes() const { return writes_; }
    // Points in the last snapshot.
    size_t lastPoints() const { return last_points_; }

private:
    sfm::Reconstruction rec_;
    double interval_;
    size_t max_points_;
    bool active_ = false;
    int writes_ = 0;
    size_t last_points_ = 0;
    std::chrono::steady_clock::time_point next_;
};

}  // namespace roma
