#include "roma/model/RomaMatcher.h"

#include <cmath>
#include <stdexcept>

namespace roma {
namespace {

std::vector<float> to_input(const MatchImage& m, int size) {
    if (!m.rgb || m.width <= 0 || m.height <= 0)
        throw std::runtime_error("roma: empty image '" + m.name + "'");
    // At the same size torch's antialiased bicubic is the identity.
    if (m.width == size && m.height == size) {
        std::vector<float> v((size_t)size * size * 3);
        for (size_t i = 0; i < v.size(); ++i) v[i] = m.rgb[i] / 255.0f;
        return v;
    }
    return resize_rgb(m.rgb, m.width, m.height, size, size);
}

}  // namespace

RomaMatcher::RomaMatcher(const std::string& checkpoint, int size) : size_(size) {
    if (size <= 0 || size % 16 != 0)
        throw std::runtime_error("roma: match size " + std::to_string(size) +
                                 " is not a positive multiple of 16");
    model_.load(checkpoint);
}

Warp RomaMatcher::match(const MatchImage& a, const MatchImage& b) {
    const std::vector<float> ia = to_input(a, size_), ib = to_input(b, size_);
    const CoarseMatch c = model_.coarse(ia.data(), ib.data(), size_, size_);
    Warp w;
    w.width = c.w;
    w.height = c.h;
    const size_t n = (size_t)c.w * c.h;
    w.warp.resize(n * 2);
    w.certainty.resize(n);
    for (size_t i = 0; i < n; ++i) {
        w.warp[i * 2] = c.data[i * 3];
        w.warp[i * 2 + 1] = c.data[i * 3 + 1];
        w.certainty[i] = 1.0f / (1.0f + std::exp(-c.data[i * 3 + 2]));
    }
    return w;
}

std::string RomaMatcher::describe() const {
    return "RoMa v2 coarse (stride 4, no refiners) at " + std::to_string(size_) + "px";
}

}  // namespace roma
