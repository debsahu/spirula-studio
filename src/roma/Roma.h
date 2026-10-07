#pragma once
// RoMa v2 dense matching on the inference layer (src/nn/): no PyTorch, no
// converter, the author's romav2.0.1.pt read in process. README.md has the
// architecture, the parity numbers and the memory plan.

#include <cstdint>
#include <string>
#include <vector>

namespace roma {

// A's coarse warp into B at stride 4: [h, w, 3] holding the warp (x, y),
// normalized to [-1, 1] over B with pixel centres at (2i + 1)/n - 1, and the
// overlap logit (certainty before the sigmoid).
struct CoarseMatch {
    int h = 0, w = 0;
    std::vector<float> data;
};

// A refined warp at the last refiner's resolution, [h, w] pixels of the image
// it was matched from: warp (x, y) as in CoarseMatch, and confidence [h, w, 4]
// = (overlap logit, precision p00, p10, p11). overlap() is sigmoid(logit).
struct DenseMatch {
    int h = 0, w = 0;
    std::vector<float> warp, confidence;
    float overlap(size_t i) const;
};

// RoMaV2.forward's two scales: the coarse match and three refiners at lr, then
// the same refiners again at hr when hr_h > 0. `bidirectional` also returns B
// into A; A into B does not depend on it.
struct MatchSpec {
    int lr_h = 0, lr_w = 0, hr_h = 0, hr_w = 0;
    bool bidirectional = false;
};

struct MatchResult {
    DenseMatch ab, ba;   // ba is empty unless MatchSpec::bidirectional
};

class Model {
public:
    Model();
    ~Model();
    Model(const Model&) = delete;
    Model& operator=(const Model&) = delete;

    void load(const std::string& checkpoint);
    bool loaded() const;

    // `a`, `b`: [H, W, 3] RGB in [0, 1], H and W multiples of 16 -- the
    // tensors RoMaV2.forward() takes, after match()'s resize.
    CoarseMatch coarse(const float* a, const float* b, int H, int W);

    // [lr_h, lr_w, 3] and [hr_h, hr_w, 3] RGB in [0, 1]; the hr pair is ignored
    // without an hr scale. A's backbone taps and VGG maps stay on the device and
    // are reused when the next A has the same sizes and the same bytes.
    MatchResult match(const float* a_lr, const float* b_lr, const float* a_hr,
                      const float* b_hr, const MatchSpec& spec);
    // The reference cache's arena, separate from plannedBytes()/peakBytes().
    uint64_t cacheBytes() const;
    uint64_t cacheHits() const;
    uint64_t cacheMisses() const;

    uint64_t plannedBytes() const;   // the largest arena plan so far
    uint64_t peakBytes() const;      // the arena's high water
    uint64_t weightBytes() const;

    // Each stage of the last coarse() against its own plan, in arena bytes
    // above what was live when it started.
    struct Stage {
        const char* name;
        uint64_t peak, plan;
    };
    const std::vector<Stage>& stages() const;

private:
    struct Impl;
    Impl* impl_;
};

// F.interpolate(mode="bicubic", antialias=True, align_corners=False) of
// 8-bit RGB, the resize RoMaV2.match() applies: [oh, ow, 3] in [0, 1].
std::vector<float> resize_rgb(const uint8_t* rgb, int w, int h, int ow, int oh);

}  // namespace roma
