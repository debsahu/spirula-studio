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

struct DensifyOptions {
    bool plugin_exact = false;

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
    double min_parallax_deg = 1.5;
    int min_track = 0;              // 0: auto (3 with >= 3 neighbours, else 2)
    // A short track survives alone in its voxel past this; off by default, as
    // on the S-1 staircase it kept every floater the multi-view check removed.
    double lone_parallax_deg = INFINITY;
    double max_depth_error = 0.02;  // per match pixel, as a share of the depth; <= 0 off
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
    std::vector<Observation> track;
};

// Per-filter counts, for the log and densify.json.
struct DensifyStats {
    int64_t samples = 0, below_certainty = 0, outside = 0, sampson = 0, nonfinite = 0,
            reproj = 0, cheirality = 0, parallax = 0, candidates = 0, ref_reproj = 0, inconsistent = 0, uncertain = 0,
            fused = 0, short_track = 0, lone_kept = 0, voxel_merged = 0, capped = 0;
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

// The points one reference view yields from the given samples (flat indices
// into its w x h grid). `source_image_of` maps a view to its source image,
// which is what distinct_images and the support check count.
std::vector<DensePoint> triangulateRef(const RefMatches& m, const std::vector<View>& views,
                                       const std::vector<int64_t>& samples,
                                       const DensifyOptions& opt, DensifyStats& stats);

// Short-track filter (with the lone-voxel exception in the default mode),
// track-preserving voxel select, then a seeded cap. `voxel` <= 0 is off.
std::vector<DensePoint> finalizePoints(std::vector<DensePoint> pts, int min_track,
                                       double voxel, int64_t max_points,
                                       const DensifyOptions& opt, DensifyStats& stats);

// The plugin's per-voxel choice: longest track, then lower error; surviving
// points keep their input order. Track length counts views (plugin) or
// distinct source images.
std::vector<size_t> voxelSelect(const std::vector<DensePoint>& pts, double voxel,
                                bool by_images);

}  // namespace roma
