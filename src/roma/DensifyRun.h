// `spirula densify` without its command line: a COLMAP model and its images
// in, a sibling model with the dense cloud out. The source model is never
// written: cameras.bin and images.bin are copied byte for byte and verified
// (sfm/core/FixedPoses.h), and only points3D.bin is new. docs/notes/densify.md.
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "roma/Densify.h"
#include "roma/Matcher.h"
#include "roma/Select.h"

namespace roma {

struct DensifyJob {
    std::string model_dir;              // the source model, e.g. <dataset>/sparse/0
    std::string out_dir;                // "" = <model_dir>-roma
    // Where an image of the model and its mask live; "" = none. The mask is
    // white where the image is kept unless flip_mask; with no mask file an
    // image's own alpha is the mask.
    std::function<std::string(const std::string& name)> image_path, mask_path;
    bool flip_mask = false;
    bool force = false;                 // past the mask-polarity refusal
    bool overwrite = false;
    std::string export_dir;             // write the views and pairs.txt, match nothing
    Matcher* matcher = nullptr;
    DensifyOptions opt;
};

// What the run will do, every automatic choice resolved, and why.
struct DensifyPlan {
    std::vector<SourceImage> images;
    std::vector<View> views;
    std::vector<std::vector<int>> views_of;      // per image
    std::vector<int> held_out;                   // image indices
    std::vector<int> refs;                       // image indices
    std::vector<std::vector<int>> nbrs;          // per ref, image indices
    struct RefView { int view; std::vector<int> nbr_views; };
    std::vector<RefView> ref_views;
    int64_t pairs = 0;
    bool split = false;
    int face_size = 0, match_size = 0;
    int64_t sparse_points = 0;
    double sparse_spacing = 0;                   // median nearest-neighbour distance
    bool metric = false;
    double max_baseline = 0;
    int min_track = 0;
    double voxel = 0;
    int64_t max_points = 0;
    double mask_keep = -1;                       // mean keep fraction of sampled masks, -1 none
    int masks_sampled = 0;
    DensifyOptions opt;                          // with every auto resolved
};

DensifyPlan planDensify(const DensifyJob& job);

struct DensifyResult {
    DensifyStats stats;
    int64_t points = 0;
    double seconds_match = 0, seconds_total = 0;
    std::string out_dir;
    std::vector<DensePoint> cloud;               // what points3D.bin holds
};

// <model>-roma beside the model. It must sort after the model's own name, or
// the trainer's automatic pick would take it (densify_autopick_test).
std::string siblingDir(const std::string& model_dir);

// progress(done_refs, total_refs, points so far)
DensifyResult runDensify(const DensifyJob& job, const DensifyPlan& plan,
                         const std::function<void(int, int, int64_t)>& progress);

// The source's cameras/images.bin and its gauge and rig files copied as bytes;
// points3D.bin with empty tracks (images.bin is untouched), the tracks in
// points3D_tracks.bin, densify.json. Throws, writing nothing, unless verified.
void writeSibling(const std::string& model_dir, const std::string& out_dir,
                  const DensifyPlan& plan, const std::vector<DensePoint>& cloud,
                  const std::string& settings_json);

// A panorama face (or a whole image) resampled to `size` square, supersampled
// so a large downscale does not alias; and its keep-mask by nearest.
struct ImageData {
    int width = 0, height = 0;
    std::vector<uint8_t> rgb;       // 3 per pixel
    std::vector<uint8_t> keep;      // 1 per pixel, 1 = keep; empty = no mask
};
ImageData loadImage(const std::string& path, const std::string& mask_path, bool flip_mask);
void cutView(const ImageData& img, const SourceImage& src, const View& v, int size,
             std::vector<uint8_t>& rgb, std::vector<uint8_t>& keep);
// A native pixel of `v` -> the source image's pixel, and its colour there.
sfm::Vec2 viewToSource(const SourceImage& src, const View& v, double x, double y);
std::array<float, 3> sampleRgb(const ImageData& img, double x, double y);

}  // namespace roma
