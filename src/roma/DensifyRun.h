// `spirula densify` without its command line: a COLMAP model and its images
// in, a sibling model with the dense cloud out. The source model is never
// written: cameras.bin and images.bin are copied byte for byte and verified
// (sfm/core/FixedPoses.h), and only points3D.bin is new. docs/notes/densify.md.
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "roma/Densify.h"
#include "roma/DepthSource.h"
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
    DepthSource* depth = nullptr;
    DensifyOptions opt;
    // Each image's depth fit as it is made, refused or not.
    std::function<void(const SourceImage&, const DepthField&)> on_depth_fit;
    // Called once if the warps come back coarser than the matcher's input.
    std::function<void(int warp_size, int input_size)> on_warp_scale;
    // The cloud as it grows, after each reference view (`filtered` false), and
    // once more as written (true). Called on the run's own thread: keep it short.
    std::function<void(const std::vector<DensePoint>& cloud, bool filtered)> on_cloud;
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
    enum class FarState { Off, PluginExact, TooFewPoints, On };
    FarState far_state = FarState::Off;          // the far-isolated filter, and why it is not On
    FarFilter far_filter;                        // resolved when far_state is On
    double max_baseline = 0;
    int min_track = 0;
    double voxel = 0;
    int64_t max_points = 0;
    int64_t max_fill = -1;                       // hybrid, auto cap: the fill's own budget; -1 shared
    DensifySource source = DensifySource::Roma;
    double median_pair_angle_deg = 0;            // ref-neighbour, at their shared points
    double match_focal = 0;                      // median view focal, match pixels
    double mask_keep = -1;                       // mean keep fraction of sampled masks, -1 none
    int masks_sampled = 0;
    DensifyOptions opt;                          // with every auto resolved
};

DensifyPlan planDensify(const DensifyJob& job);

struct DensifyResult {
    DensifyStats stats;
    int64_t points = 0;
    double seconds_match = 0, seconds_total = 0;
    double depth_tol = 0;                        // the depth agreement tolerance used
    double depth_share = -1;                     // usable maps among the matched images, -1 unused
    std::string out_dir;
    std::vector<DensePoint> cloud;               // what points3D.bin holds
};

// <model>-roma beside the model. It must sort after the model's own name, or
// the trainer's automatic pick would take it (densify_autopick_test).
std::string siblingDir(const std::string& model_dir);

// Per source image, on a coarse grid, the nearest distance at which it saw a
// point of `pts` with >= `min_images` images. A point more than `margin` in
// front of that (3x3 cells), in an image outside its track, is seen through.
class FreeSpace {
public:
    FreeSpace(const DensifyPlan& pl, const std::vector<DensePoint>& pts, int min_images,
              double margin, int grid = 512);
    bool seesThrough(const DensePoint& p) const;

private:
    struct Map {
        int w = 0, h = 0;
        double sx = 1, sy = 1;
        std::vector<float> d;
    };
    bool cell(int image, const sfm::Vec3& X, size_t* at, double* dist) const;
    const DensifyPlan& pl_;
    double margin_;
    std::vector<Map> maps_;
};

// progress(done_refs, total_refs, points so far)
DensifyResult runDensify(const DensifyJob& job, const DensifyPlan& plan,
                         const std::function<void(int, int, int64_t)>& progress);

// cameras.bin and the gauge and rig files copied as bytes; images.bin copied
// with every point3D_id invalid (detachPoints); points3D.bin with empty
// tracks, the tracks in points3D_tracks.bin. Throws, writing nothing, unless verified.
std::string detachPoints(const std::string& images_bin, std::vector<size_t>* id_offsets);

// Why `out_dir` must not be written for `model_dir`, or "": the source itself,
// a folder holding it or inside it, or (with `dataset_dir`) a model folder
// that sorts first and so would become the trainer's automatic pick.
std::string outDirProblem(const std::string& dataset_dir, const std::string& model_dir,
                          const std::string& out_dir);

// points3D.bin has no normal field: when any point has one, the sibling also
// holds kNormalsPly, binary little-endian x y z nx ny nz (float) and the
// point3D_id (uint), one vertex per point, 0 0 0 for none.
inline constexpr const char* kNormalsPly = "points3D_normals.ply";
void writeNormalsPly(const std::string& path, const std::vector<DensePoint>& cloud);

// Each written point through its image's own camera and pose at the pixels
// points3D_tracks.bin holds, read back from `model_dir`: independent of the
// triangulation and of the face-to-panorama mapping that wrote them.
struct ReprojStats {
    int64_t observations = 0, invalid = 0;   // invalid: behind, outside, unknown image, non-finite
    double mean_px = NAN, p95_px = NAN;
};
ReprojStats reprojectWritten(const std::string& model_dir);

// Throws, writing nothing, for an empty cloud: a sibling sorts after its
// source, so a trainer could pick it. densify.json gains "reprojection" and
// the cloud's SHA-256s. Holds the writer lock and publishes by publishDir.
ReprojStats writeSibling(const std::string& model_dir, const std::string& out_dir,
                         const DensifyPlan& plan, const std::vector<DensePoint>& cloud,
                         const std::string& settings_json);

// Hybrid: the fill at sample `s` of a G x G grid agrees with the matched and
// sparse points within +-8 cells (`local`: their relative residual, NaN none)
// when the median is within `tol`; with fewer than 3 there, it is not refused.
bool localResidualOk(const std::vector<float>& local, int G, int64_t s, double tol);

// Depth maps in place before a depth run: those present are kept, `compute`
// (when given) runs once if any are missing, and must leave every present map
// untouched (size and time), or this throws.
struct DepthInventory { int reused = 0, computed = 0, missing = 0; };
DepthInventory ensureDepths(const std::vector<std::string>& names,
                            const std::function<std::string(const std::string&)>& find,
                            const std::function<void()>& compute);

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
