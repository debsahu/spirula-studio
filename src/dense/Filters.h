#pragma once

// Point filters that run after a reference's points are refined: two-image
// admission with a free-space test, a depth-precision bar, far isolated points,
// and the written-cloud reprojection check. Measurements: docs/dense.md.

#include "dense/Geometry.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <vector>

namespace spirula::dense {

struct TrackObservation {
    uint32_t view = 0;
    float pixel[2]{};
};

// Per view, on a coarse grid, the nearest distance at which a point with three or
// more distinct images was seen. 512 cells span 360 degrees of one source image.
class FreeSpaceGrid {
public:
    FreeSpaceGrid(const std::vector<View>& views, double margin = 0.05, int cells_per_turn = 512);
    // Records `point` in every view of each source image in `sources`.
    void observe(const sfm::Vec3& point, const std::vector<int64_t>& sources);
    // True when a view of a source image outside `sources` saw a surface more
    // than `margin` behind the point (nearest of the 3 x 3 cells around it).
    bool sees_through(const sfm::Vec3& point, const std::vector<int64_t>& sources) const;
    uint64_t bytes() const;
    int cells_x(uint32_t view) const { return maps_[view].w; }

private:
    struct Map { int w = 0, h = 0; bool periodic = false; std::vector<float> nearest; };
    bool locate(uint32_t view, const sfm::Vec3& point, int& x, int& y, double& distance) const;
    const std::vector<View>& views_;
    double margin_;
    std::vector<Map> maps_;
    std::vector<std::vector<uint32_t>> by_source_;
    std::vector<int64_t> sources_;
};

// The upper median of `count` doubles in `values`, sorted on disk within `budget`; -1 when
// empty. The two-image bar is this over the run's three-or-more-image errors.
double external_median(const std::filesystem::path& values, uint64_t count, uint64_t budget,
                       const std::function<void()>& check = {});

// Focal length in matcher cells per radian, the mean of the two axes.
double cell_focal(const View& view, int grid_width, int grid_height);
// Relative depth change per matcher cell for the best pair of the track: the
// smallest 1 / (f sin(parallax)), f the coarser view's cell focal.
double best_depth_per_cell(const sfm::Vec3& point, const View& reference, const std::vector<const View*>& others,
                           int grid_width, int grid_height);
// max(2 %, 1 / (f sin(theta / 2))): a pair at half the median parallax survives one cell.
double automatic_depth_precision(double median_pair_angle_radians, double median_cell_focal);

struct FarFilter {
    sfm::Vec3 low, high;
    double margin = 0, radius = 0;
    int max_neighbours = 2;
    bool active() const { return margin > 0 && radius > 0; }
};
// Box: per-axis p0.5 and p99.5 of the sparse points. Margin: 2 units when metric,
// else 0.2 x the box diagonal. Radius: 8 x the median nearest-neighbour spacing.
// Inactive under 100 sparse points.
FarFilter resolve_far_filter(const std::vector<double>& xyz, bool metric);
FarFilter resolve_far_filter(const std::vector<double>& xyz, bool metric, double spacing);
double median_neighbour_spacing(const std::vector<double>& xyz);
// Max-norm distance outside the box, 0 inside.
double box_excess(const FarFilter& filter, const sfm::Vec3& point);
// Indices of far points (beyond the margin) with at most `max_neighbours` other
// points within the radius; `beyond` gets the far count.
std::vector<uint64_t> far_isolated(const std::vector<sfm::Vec3>& points, const FarFilter& filter, uint64_t* beyond = nullptr);

// Three times the median baseline of the planned pairs; 0 with no pairs.
double automatic_max_baseline(std::vector<double> baselines);

struct PairImage;
// Resolved before matching. A negative precision is off, which is also what
// Reconstruction does when no plan is set.
struct FilterPlan {
    double depth_precision = -1, median_pair_angle_degrees = 0, median_cell_focal = 0;
};
// The depth-precision bar from the planned image pairs: each pair's angle at the
// centroid of the sparse points both see, and the views' cell focals. `setting`
// > 0 is used as given, < 0 is off; 0 is automatic, and off with no shared points.
FilterPlan plan_depth_precision(const std::vector<PairImage>& images, const std::vector<std::pair<uint32_t,uint32_t>>& pairs,
                                const std::vector<double>& sparse_xyz, const std::vector<View>& views,
                                int grid_width, int grid_height, double setting);

struct ReprojectionSummary {
    uint64_t observations = 0, invalid = 0;
    double mean_pixels = 0, p50_pixels = 0, p95_pixels = 0, p95_cells = 0;
};
ReprojectionSummary summarize_reprojection(std::vector<double> pixels, std::vector<double> cells, uint64_t invalid);

}  // namespace spirula::dense
