// Monocular depth as a densify source, beside matches (roma/Matcher.h): a
// per-image depth map, fitted to that image's own sparse points, then
// unprojected and kept only where other images' fitted maps agree.
// docs/notes/densify.md, "Depth source".
#pragma once

#include <array>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "app/DepthManifest.h"
#include "roma/Densify.h"
#include "roma/Select.h"
#include "sfm/core/Model.h"

namespace roma {

// A depth map as stored: w x h values proportional to depth (0 = no data),
// either ray distance from the camera centre or z; and the image's normal
// map when it has one (camera frame, OpenCV axes, all zero = none).
struct RawDepth {
    int width = 0, height = 0;
    std::vector<float> value;
    bool ray = true;
    int normal_width = 0, normal_height = 0;
    std::vector<float> normal;        // [nh*nw*3], empty = no normal map
    std::string refused;              // a map that is there but must not be used, and why
    bool recorded = false;            // checked against geometry's record of it
};

class DepthSource {
public:
    virtual ~DepthSource() = default;
    // False when the image has no map.
    virtual bool load(const SourceImage& img, RawDepth& out) = 0;
    virtual std::string describe() const = 0;
};

// Spirula `geometry` output: 16-bit depth, relative or mm (the fit absorbs which);
// normals/ byte / 127.5 - 1, black = none. A map geometry recorded
// (app/DepthManifest.h) must match its record, which also says ray or z depth.
class DepthFiles : public DepthSource {
public:
    DepthFiles(std::function<std::string(const std::string& name)> path,
               std::function<bool(const SourceImage&)> ray, std::string dir,
               std::function<std::string(const std::string& name)> normal_path = {},
               std::function<std::string(const std::string& name)> image_path = {});
    bool load(const SourceImage& img, RawDepth& out) override;
    std::string describe() const override { return "depth maps in " + dir_; }
    int recorded() const { return (int)records_.size(); }

private:
    std::function<std::string(const std::string&)> path_, normal_path_, image_path_;
    std::function<bool(const SourceImage&)> ray_;
    std::string dir_;
    std::map<std::string, app::DepthMapRecord> records_;
};

// The view ray of map pixel (x, y) of a w x h map of `img`: unit for ray
// depth, scaled to z = 1 for z depth (zero where z <= 0).
sfm::Vec3 mapRay(const SourceImage& img, int w, int h, int x, int y, bool ray);

// Normals from a depth map with the engine's stencil (app/ScanDepth.cpp,
// shaders/pixel_wise.slang points_to_normal): (x+ - x-) x (y- - y+), then
// faced to each pixel's OWN ray. Never by n.z: off axis that inverts them.
void normalsFromDepth(const SourceImage& img, int w, int h, bool ray,
                      const std::vector<float>& dist, std::vector<float>& normal);
// Flips every normal with dot(n, ray) > 0 to face its pixel's ray; the count.
int64_t faceCamera(const SourceImage& img, int w, int h, std::vector<float>& normal);

enum class NormalFrom { None, File, Depth };

// A map fitted to the image's sparse points: disparity_true = a disparity_raw
// + b, by 2-point RANSAC scored on relative depth error, then Huber IRLS.
struct DepthField {
    int width = 0, height = 0;
    bool ray = true;
    std::vector<float> dist;          // fitted ray distance or z; 0 = none
    bool ok = false;
    std::string refused;              // why not, when !ok
    int anchors = 0, inliers = 0;
    double a = 0, b = 0, spearman = 0, rel_residual = 0;   // median |rel. error| of inliers
    // Unit normals, camera frame, facing the camera, x 127; 0 0 0 = none.
    std::vector<int8_t> normal;
    NormalFrom normal_from = NormalFrom::None;
    double normal_file_cos = NAN;     // median cos(file normal, normal from depth)
    int64_t normal_flipped = 0;       // file normals that faced away, flipped

    // The camera-frame normal at map pixel (x, y); false for none.
    bool normalAtPixel(int x, int y, sfm::Vec3* n) const;
    // The world normal at X's projection; false outside the map or for none.
    bool normalAt(const SourceImage& img, const sfm::Vec3& X, sfm::Vec3* n) const;

    // The fitted distance from the camera centre along X's ray (or z),
    // and X's own, at X's projection; false outside the map or on no data.
    bool at(const SourceImage& img, const sfm::Vec3& X, double* field, double* own) const;
};

struct DepthFitOptions {
    double inlier_rel = 0.10;
    int min_anchors = 30, min_inliers = 20;
    double min_inlier_frac = 0.5, min_spearman = 0.5, max_lost = 0.05;
    uint64_t seed = 1;
    bool align = true;                // false: the raw values are distances already
    bool normal_files = true;         // false: always normals from the fitted depth
    // A normal map below this median cosine against the depth's own normals
    // is in another convention and is not used (docs/notes/densify.md).
    double normal_min_cos = 0.9;
    int holdout_mod = 0;              // > 0: sparse points with id % mod == 0 are not anchors (an evaluation)
};

DepthField fitDepth(const SourceImage& img, const sfm::Reconstruction& rec, const RawDepth& raw,
                    const DepthFitOptions& opt);

// Candidates from view `ref`'s fitted map at `samples` of a W x H grid. Other
// images vote (agree within `tol`, see through past `through`, hide a copy up to
// `front`); kept with >= `min_agree` agreeing and more than either other vote.
struct DepthAgreeOptions {
    int min_agree = 2;
    double tol = 0.03, through = 0.06, front = 0.25;
    bool vote = true;
    bool normal_check = true;         // drop past `normal_deg` from the agreeing images' median
    double normal_deg = 55;
    // A voter this close in angle at the point shares the reference's monocular
    // error instead of checking it (adjacent frames): it does not vote.
    double min_parallax_deg = 1.5;
};
std::vector<DensePoint> depthPointsForView(
    const std::vector<View>& views, const std::vector<SourceImage>& images,
    const std::vector<DepthField>& fields, const std::vector<int>& others, int ref, int W,
    int H, const std::vector<int64_t>& samples, const DepthAgreeOptions& o,
    const std::function<std::array<float, 3>(double, double)>& colour_at,
    const std::function<bool(int64_t, const sfm::Vec3& X, const sfm::Vec3* normal)>& local_ok,
    DensifyStats& st);

// The normal of a plane through `pts` (6 or more), false unless they are
// flat (least spread under a tenth of the next) and not on a line.
bool planeNormal(const std::vector<sfm::Vec3>& pts, sfm::Vec3* n);

// Angle between two unit normals, degrees.
double normalAngleDeg(const sfm::Vec3& a, const sfm::Vec3& b);

}  // namespace roma
