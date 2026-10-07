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

    uint64_t plannedBytes() const;   // the largest arena plan so far
    uint64_t peakBytes() const;      // the arena's high water
    uint64_t weightBytes() const;

private:
    struct Impl;
    Impl* impl_;
};

// F.interpolate(mode="bicubic", antialias=True, align_corners=False) of
// 8-bit RGB, the resize RoMaV2.match() applies: [oh, ow, 3] in [0, 1].
std::vector<float> resize_rgb(const uint8_t* rgb, int w, int h, int ow, int oh);

}  // namespace roma
