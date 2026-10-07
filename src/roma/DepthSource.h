// Monocular depth as a densify source, beside matches (roma/Matcher.h): a
// per-image depth map, fitted to that image's own sparse points, then
// unprojected and kept only where other images' fitted maps agree.
// docs/notes/densify.md, "Depth source".
#pragma once

#include <array>
#include <functional>
#include <string>
#include <vector>

#include "roma/Densify.h"
#include "roma/Select.h"
#include "sfm/core/Model.h"

namespace roma {

// A depth map as stored: w x h values proportional to depth (0 = no data),
// either ray distance from the camera centre or z.
struct RawDepth {
    int width = 0, height = 0;
    std::vector<float> value;
    bool ray = true;
};

class DepthSource {
public:
    virtual ~DepthSource() = default;
    // False when the image has no map.
    virtual bool load(const SourceImage& img, RawDepth& out) = 0;
    virtual std::string describe() const = 0;
};

// Spirula `geometry` output: 16-bit PNGs, relative or millimetres (both are
// linear in depth, so the fit absorbs which), ray depth where `ray(img)` says.
class DepthFiles : public DepthSource {
public:
    DepthFiles(std::function<std::string(const std::string& name)> path,
               std::function<bool(const SourceImage&)> ray, std::string dir);
    bool load(const SourceImage& img, RawDepth& out) override;
    std::string describe() const override { return "depth maps in " + dir_; }

private:
    std::function<std::string(const std::string&)> path_;
    std::function<bool(const SourceImage&)> ray_;
    std::string dir_;
};

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
};
std::vector<DensePoint> depthPointsForView(
    const std::vector<View>& views, const std::vector<SourceImage>& images,
    const std::vector<DepthField>& fields, const std::vector<int>& others, int ref, int W,
    int H, const std::vector<int64_t>& samples, const DepthAgreeOptions& o,
    const std::function<std::array<float, 3>(double, double)>& colour_at,
    const std::function<bool(int64_t, double)>& local_ok, DensifyStats& st);

}  // namespace roma
