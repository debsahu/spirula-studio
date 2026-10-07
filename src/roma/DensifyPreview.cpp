#include "roma/DensifyPreview.h"

#include <algorithm>
#include <cmath>

#include "sfm/core/Progress.h"

namespace roma {

CloudPreview::CloudPreview(const std::string& model_dir, double interval_s, size_t max_points)
    : interval_(interval_s), max_points_(std::max<size_t>(1, max_points)),
      next_(std::chrono::steady_clock::now()) {
    if (!sfm::progress::enabled()) return;
    try {
        rec_ = sfm::Reconstruction::readBinary(model_dir);
        rec_.points3D.clear();
        active_ = true;
    } catch (const std::exception&) {
    }
}

void CloudPreview::update(const std::vector<DensePoint>& cloud, bool final) {
    using clock = std::chrono::steady_clock;
    if (!active_) return;
    const auto t0 = clock::now();
    if (!final && t0 < next_) return;
    try {
        const size_t stride = cloud.size() > max_points_ ? (cloud.size() + max_points_ - 1) / max_points_ : 1;
        rec_.points3D.clear();
        uint64_t id = 1;
        for (size_t i = 0; i < cloud.size(); i += stride) {
            sfm::Point3D p;
            p.xyz = cloud[i].xyz;
            for (int c = 0; c < 3; c++)
                p.rgb[c] = (uint8_t)std::lround(std::clamp(cloud[i].rgb[c], 0.0f, 1.0f) * 255.0f);
            rec_.points3D.emplace_hint(rec_.points3D.end(), id++, std::move(p));
        }
        last_points_ = rec_.points3D.size();
        sfm::progress::model(rec_, /*force=*/true);
        writes_++;
    } catch (const std::exception&) {
    }
    const double took = std::chrono::duration<double>(clock::now() - t0).count();
    next_ = clock::now() + std::chrono::duration_cast<clock::duration>(
                               std::chrono::duration<double>(std::max(interval_, 8.0 * took)));
}

}  // namespace roma
