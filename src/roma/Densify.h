// Ported from Lichtfeld-Densification-Plugin (GPL-3.0-or-later), Copyright (c) 2025 Shady Gmira and contributors; core/pipeline.py@ab0b04e.
//
// Dense points from dense matches between views with known poses: certainty
// sampling, two-view DLT per neighbour, filters, fusion, a multi-view support
// check, voxel dedup. docs/notes/densify.md has the port table and where the
// default mode departs from the plugin (`plugin_exact` reproduces it).
#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "roma/Matcher.h"
#include "sfm/core/Camera.h"
#include "sfm/geometry/LinAlg.h"

namespace roma {

enum class NeighbourRule { Covis, Pose };
// Where points come from: dense matches, fitted monocular depth, or matches
// with depth filling only what the matches do not cover.
enum class DensifySource { Auto, Roma, Depth, Hybrid };

struct DensifyOptions {
    bool plugin_exact = false;
    DensifySource source = DensifySource::Auto;
    double depth_tol = 0;           // relative depth agreement; 0: auto from the fits
    int depth_min_agree = 2;        // other images a depth point must agree with
    bool depth_agreement = true;    // false: every fitted depth sample is a point (no vote)
    bool depth_align = true;
    bool hybrid_local_check = true;
    bool depth_normal_files = true;     // false: normals always from the fitted depth
    double min_depth_share = 0.5;
    int depth_fit_holdout = 0;          // sparse points id % n == 0 kept out of the fits, to score against       // usable maps among the matched images, or no depth source
    double depth_normal_min_cos = 0.9;  // a normal map's median cosine against its own depth
    double depth_normal_deg = 55;       // normal agreement, degrees: measured (docs/notes/densify.md)
    bool depth_normal_check = true;     // against the images that agree on the depth
    bool hybrid_normal_check = true;    // a hybrid fill against the plane of its neighbours

    double refs = 0.8;              // <= 1: a fraction of the images, else a count
    int neighbours = 3;
    NeighbourRule neighbour_rule = NeighbourRule::Covis;
    int holdout_every = 0;          // every Nth image (by name) never enters
    double max_baseline = 0;        // 0: auto (metric models only), < 0: off
    bool all_face_pairs = false;
    double face_pair_deg = 60.0;
    int split = -1;                 // equirect into cube faces: -1 auto, 0 no, 1 yes

    int matches_per_ref = 0;        // per reference VIEW; 0: auto
    float sample_cap = 0.9f;
    float certainty_floor = 0.2f;   // plugin: clamp(min=), never removes anything
    float min_certainty = 0.2f;     // default mode: a real filter, per neighbour too

    // Default mode: pixels of the MATCH resolution. plugin_exact: the
    // camera's own pixels, as the plugin measures them.
    double reproj_px = 1.0;
    double sampson_px2 = 5.0;
    double min_parallax_deg = 1.5;  // the per-point filter
    double covis_min_angle_deg = 1.5; // the neighbour prior: angle at the shared sparse points
    // 0: auto -- 3 images, or 2 with an error no worse than the median of the
    // run's 3-image points (docs/notes/densify.md, "Minimum track").
    int min_track = 0;
    bool visibility_check = true;   // auto min-track: drop two-image points another image saw through
    double max_depth_error = 0;     // per match pixel, a share of the depth; 0 auto, < 0 off
    bool far_isolated = true;       // drop isolated points far outside the sparse box (default mode only)
    // A -> B -> A round trip, match pixels of A: < 0 off, 0 auto, inf measures without rejecting.
    double cycle_px = 0;
    // Each fused point moved along the reference ray to RoMa's precision-weighted
    // Huber fit: the threshold in sigmas of the view's own residuals; < 0 off, 0 auto.
    double refine_huber = 0;
    bool no_filter = false;

    double voxel = 0;               // 0: auto, < 0: off
    int64_t max_points = 0;         // 0: auto, < 0: off
    uint64_t seed = 0;
    bool keep_masks_polarity_check = true;
};

// One image the matcher sees: a whole image, or one cube face of a panorama.
// Pixel coordinates everywhere are continuous, COLMAP's: (0,0) is the corner
// of the first pixel, its centre is (0.5, 0.5).
struct View {
    std::string name;
    int image = -1;                 // index into the source images
    int face = -1;                  // camhost::equirect_face_axes row, -1 = whole
    sfm::Camera cam;                // native camera of this view
    sfm::Mat3 R = sfm::mat3Identity();
    sfm::Vec3 t;                    // world -> view: x = R X + t
    sfm::Vec3 centre;
    sfm::Mat3 face_R = sfm::mat3Identity();  // source camera frame -> view frame
};

struct Observation {
    int view = 0;
    double x = 0, y = 0;            // native pixels of the view
};

struct DensePoint {
    sfm::Vec3 xyz;
    float rgb[3] = {0, 0, 0};       // 0..1
    double error = 0;               // max support reprojection error, filter units
    double parallax_deg = 0;
    int distinct_images = 1;
    bool from_depth = false;        // a depth-source point; `error` is then relative depth
    float normal[3] = {0, 0, 0};    // world, unit, from the depth source; 0 0 0 = none
    std::vector<Observation> track;
};

// Per-filter counts, for the log and densify.json.
struct DensifyStats {
    int64_t samples = 0, below_certainty = 0, outside = 0, sampson = 0, nonfinite = 0,
            reproj = 0, cheirality = 0, parallax = 0, candidates = 0, ref_reproj = 0, inconsistent = 0, uncertain = 0,
            fused = 0, short_track = 0, two_image_kept = 0, seen_through = 0, voxel_merged = 0,
            capped = 0, depth_samples = 0, depth_nodata = 0, depth_disagree = 0,
            depth_through = 0, depth_local = 0, depth_left_to_matches = 0, depth_kept = 0, depth_edge = 0,
            depth_normal = 0, depth_local_normal = 0, depth_vote_close = 0, fill_near_matches = 0,
            far_beyond = 0, far_isolated = 0, cycle = 0, refined = 0, refine_fallback = 0,
            refine_no_precision = 0;
    // A -> B -> A error, 0.05 match px bins, the last one open: samples that went
    // on to be candidates, and samples a geometric filter rejected.
    std::array<int64_t, 64> cycle_kept_hist{}, cycle_rejected_hist{};
    // RoMa's Mahalanobis residual at the fused mean, half-octave bins from 2^-6.
    std::array<int64_t, 40> refine_residual_hist{};
    std::vector<float> refine_scales;    // per reference view, the sigma the Huber threshold is in
    // 5-degree bins: candidate normal against the agreeing images' (agree), against
    // a random pixel of the same image (null), and a hybrid fill against its neighbourhood.
    std::array<int64_t, 36> normal_agree_hist{}, normal_null_hist{}, local_normal_hist{};
    double two_image_bar = -1;           // the auto min-track error bar, -1 unused
    std::map<int, int64_t> track_hist;   // distinct images per output point
    void add(const DensifyStats& o);
};

// One reference view and what its neighbours' warps say about it, at the
// match resolution w x h. cert is AFTER collect().
struct RefMatches {
    int ref = -1;
    std::vector<int> nbrs;
    int w = 0, h = 0;
    std::vector<std::vector<float>> warp;   // per neighbour [h*w*2], B normalised
    std::vector<std::vector<float>> cert;   // per neighbour [h*w]
    std::vector<std::vector<float>> prec;   // per neighbour [h*w*3] (Warp::precision) or empty
    std::vector<std::vector<float>> rev;    // per neighbour B -> A [h*w*2], when the cycle check runs
    std::vector<uint8_t> rgb_match;         // the reference at w x h, masked (plugin colour)
    // Default-mode colour at a native pixel of the reference view, 0..1.
    std::function<std::array<float, 3>(double x, double y)> colour_at;
    // When set: per sample, the candidate each neighbour (index into nbrs) left.
    struct Candidate { int k; sfm::Vec3 X; double err; };
    std::vector<std::vector<Candidate>>* record_candidates = nullptr;
};

// Certainty (floored in plugin_exact), times the reference mask and the
// neighbour's mask where the warp lands (grid_sample nearest, zeros, no
// align_corners). Masks are [h*w] at the match size, 1 = keep; empty = none.
std::vector<float> collectCertainty(const Warp& raw, const std::vector<uint8_t>& mask_a,
                                    const std::vector<uint8_t>& mask_b,
                                    const DensifyOptions& opt);

// A match-resolution pixel's x as the plugin maps it, (u + 1) / 2 * (w - 1)
// on RoMa's pixel-centre grid, or the continuous coordinate j + 0.5.
double referenceX(int j, int w, bool plugin_exact);
double warpX(float u, int w, bool plugin_exact);

// Where B's warp sends A's pixel (j, i) back to in A, through `rev` bilinear at
// B's continuous coordinate (u, v) in [-1, 1]: the distance in match pixels.
double cycleError(const std::vector<float>& rev, int w, int h, float u, float v, int j, int i);

// The points one reference view yields from the given samples (flat indices
// into its w x h grid). `source_image_of` maps a view to its source image,
// which is what distinct_images and the support check count.
std::vector<DensePoint> triangulateRef(const RefMatches& m, const std::vector<View>& views,
                                       const std::vector<int64_t>& samples,
                                       const DensifyOptions& opt, DensifyStats& stats);

// Far isolated points (docs/notes/densify.md): more than `margin` outside the sparse p0.5-p99.5 box
// `lo`/`hi` on some axis, with at most `max_neighbours` others within `radius`. margin <= 0: off.
struct FarFilter {
    sfm::Vec3 lo, hi;
    double margin = 0, radius = 0;
    int max_neighbours = 2;
};
// The box from the sparse points (numpy's linear percentiles), the margin (2 for a metric
// model, else 0.2 x the box diagonal) and the radius (8 x `spacing`). Fewer than 100 points: off.
FarFilter resolveFarFilter(const std::vector<sfm::Vec3>& sparse, bool metric, double spacing);
// Removes them, keeping the order of the rest; the count. `beyond` gets the number of far points.
int64_t dropFarIsolated(std::vector<DensePoint>& pts, const FarFilter& f, int64_t* beyond = nullptr);

// The minimum-track filter (0 = auto), a track-preserving voxel select, the far filter
// (default mode, after the select), then a seeded cap. `voxel` <= 0 is off.
// `veto`, when set, may drop a two-image point the automatic rule would admit.
std::vector<DensePoint> finalizePoints(std::vector<DensePoint> pts, int min_track,
                                       double voxel, int64_t max_points,
                                       const DensifyOptions& opt, DensifyStats& stats,
                                       const std::function<bool(const DensePoint&)>& veto = {},
                                       int64_t max_fill = -1,    // >= 0: depth points capped apart
                                       const FarFilter* far_filter = nullptr);

// Depth points within `radius` of a matched point (any reference's) removed:
// there the surface is measured, and a fill off it is a second layer. The count.
int64_t dropFillNearMatches(std::vector<DensePoint>& pts, double radius);

// The plugin's per-voxel choice: longest track, then lower error; surviving
// points keep their input order. Track length counts views (plugin) or
// distinct source images.
std::vector<size_t> voxelSelect(const std::vector<DensePoint>& pts, double voxel,
                                bool by_images);

}  // namespace roma
