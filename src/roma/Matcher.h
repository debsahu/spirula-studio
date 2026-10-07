// The seam between the densify host stage and whatever produces dense
// matches: RoMa v2 on src/nn (src/roma/model/), or warps another
// implementation dumped to disk (DumpMatcher.h). docs/notes/densify.md.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace roma {

// One image as the matcher sees it: interleaved 8-bit sRGB, already masked
// (masked pixels are black, as the matcher was fed in RoMa's own pipelines).
// `name` keys the pair for implementations that look matches up.
struct MatchImage {
    std::string name;
    int width = 0, height = 0;
    const uint8_t* rgb = nullptr;
};

// RoMa's warp_AB and overlap_AB at its output size, A -> B: warp[(y*w+x)*2+k]
// is where A's pixel lands in B in [-1, 1], align_corners=false, so B's
// continuous coordinate is (u + 1) / 2 * B.width. certainty in [0, 1].
struct Warp {
    int width = 0, height = 0;
    std::vector<float> warp;
    std::vector<float> certainty;
};

class Matcher {
public:
    virtual ~Matcher() = default;
    // The square side an input should be resampled to before match(): RoMa's
    // H_lr for single-scale presets, H_hr when it refines at a second scale.
    virtual int inputSize() const = 0;
    // Throws std::runtime_error when the pair cannot be matched.
    virtual Warp match(const MatchImage& a, const MatchImage& b) = 0;
    virtual std::string describe() const = 0;
};

}  // namespace roma
