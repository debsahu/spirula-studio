// Ported from Lichtfeld-Densification-Plugin (GPL-3.0-or-later), Copyright (c) 2025 Shady Gmira and contributors; core/selection.py@ab0b04e.
//
// Which images are references and which images each is matched against, and
// the cube faces a panorama is matched as.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "roma/Densify.h"
#include "sfm/core/Model.h"

namespace roma {

// A registered image of the source model, in the order the selectors index.
struct SourceImage {
    uint32_t id = 0;
    std::string name;
    sfm::Camera cam;
    sfm::Pose pose;                      // world -> camera
    sfm::Vec3 centre;
    std::vector<uint64_t> points;        // sparse points it observes, sorted, unique
};

std::vector<SourceImage> sourceImages(const sfm::Reconstruction& rec);

// The plugin's flattened 4x4 world -> camera pose.
std::vector<double> flatPose(const sfm::Pose& p);

// Greedy maximum new sparse-point coverage; ties to the lower index. Sorted.
// Lazy evaluation, which is exact here because a score can only fall.
std::vector<int> refsByVisibility(const std::vector<SourceImage>& imgs,
                                  const std::vector<int>& allowed, int k);
// Farthest-first over z-scored flat poses; the fallback with no sparse points.
std::vector<int> refsKCenters(const std::vector<SourceImage>& imgs,
                              const std::vector<int>& allowed, int k);

// k nearest by Euclidean distance between RAW flat poses (the plugin's rule,
// blind to viewing direction).
std::vector<int> neighboursByPose(const std::vector<SourceImage>& imgs,
                                  const std::vector<int>& allowed, int ref, int k);
// k most covisible (shared sparse points) whose camera centres subtend at
// least `min_angle_deg` at the shared points' centroid, and are no farther
// apart than `max_baseline` when that is positive.
std::vector<int> neighboursByCovis(const std::vector<SourceImage>& imgs,
                                   const sfm::Reconstruction& rec,
                                   const std::vector<int>& allowed, int ref, int k,
                                   double min_angle_deg, double max_baseline);

// The views of one image: six 90-degree faces of a panorama, each
// `face_size` pixels square, or the image itself.
std::vector<View> imageViews(const SourceImage& img, int image_index, bool split,
                             int face_size);
extern const char* const kFaceNames[6];

// For reference view `a`, the neighbour views whose optical axes lie within
// `max_deg` of a's (all of them when `all`). Two whole images always pair.
std::vector<int> pairFaces(const std::vector<View>& views, int a,
                           const std::vector<int>& nbr_views, double max_deg, bool all);

}  // namespace roma
