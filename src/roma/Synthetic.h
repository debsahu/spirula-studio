// A scene with known surfaces, for `spirula densify --check` and the tests:
// textured rectangles, cameras that see them, renders of those cameras, and
// a matcher that answers from the geometry instead of from the pixels.
#pragma once

#include <map>
#include <string>
#include <vector>

#include "roma/DensifyRun.h"
#include "roma/Matcher.h"
#include "sfm/core/Model.h"

namespace roma {

struct Quad {
    sfm::Vec3 o, u, v;    // o + a u + b v, a and b in [0, 1]
    bool riser = false;
};

struct Scene {
    std::vector<Quad> quads;
    // Nearest hit along a unit ray, its distance (inf for none) and the quad.
    double hit(const sfm::Vec3& origin, const sfm::Vec3& dir, int* quad = nullptr) const;
    // Distance from a point to the nearest surface.
    double distance(const sfm::Vec3& p) const;
    // Procedural albedo in world space, so every view of a point agrees.
    void albedo(const sfm::Vec3& p, int quad, uint8_t rgb[3]) const;
};

// Three non-axis-aligned planes (two walls, a floor) and a staircase of six
// 0.2 m risers and 0.3 m treads, metres, +Z up.
Scene stairScene();

// Eight pinhole and four equirectangular cameras on the stairs, rendered, with
// masks of where each sees a surface (the empty background is what a sky mask
// removes) and a sparse model: images/, masks/, sparse/0/ under `dir`.
void writeStairDataset(const Scene& scene, const std::string& dir, int pinhole_w,
                       int equirect_w, int sparse_points);

// Its depth maps (depths/, 16-bit, linear in depth), each with its own
// disparity-affine error and `noise`; `copies` images see the risers
// `copy_scale` too deep, which no per-image fit undoes: the doubled stairs.
struct SyntheticDepth {
    double noise = 0.002, copy_scale = 1.05;
    int copies = 3;
    uint64_t seed = 5;
    bool normals = false;             // normals/ too, the first `tilted` images' all off by `tilt_deg`
    int tilted = 0;
    double tilt_deg = 70;
};
void writeStairDepths(const Scene& scene, const std::string& dir, const SyntheticDepth& sd);

// Matches from the geometry where B sees what A's pixel ray hits. `noise_px`
// (match px) jitters each warp and is its precision, 1 / noise_px^2 (none at 0);
// a share `outliers` is sent somewhere random, certainty 1, same precision.
class OracleMatcher : public Matcher {
public:
    OracleMatcher(const Scene* scene, std::vector<View> views, int size, double noise_px,
                  double outliers, uint64_t seed);
    // Views rebuilt from the source images by its own arithmetic, not
    // imageViews(): a defect there must not cancel against the oracle.
    static std::vector<View> independentViews(const std::vector<SourceImage>& images);
    int inputSize() const override { return size_; }
    Warp match(const MatchImage& a, const MatchImage& b) override;
    std::string describe() const override { return "geometric oracle"; }

private:
    const Scene* scene_;
    std::map<std::string, View> views_;
    int size_;
    double noise_, outliers_;
    uint64_t seed_;
};

// Fraction of points within `tol` of a surface, the count farther than
// `beyond_tol`, and the share of riser area (sampled every `step`) with a point
// within `cover`.
struct CloudScore {
    int64_t points = 0, beyond = 0;
    double within = 0, riser_cover = 0;
};
CloudScore scoreCloud(const Scene& scene, const std::vector<DensePoint>& cloud, double tol,
                      double beyond_tol, double step, double cover);

}  // namespace roma
