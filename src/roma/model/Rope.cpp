// RoPE tables and the qkv row permutation, written from the reference's
// behaviour (its coordinate recipe, its dtype settings, one bf16 rounding per
// eager op) and checked bit for bit against torch's own tables. They are not a
// translation of the reference's RoPE module, which is Meta's under the DINOv3
// License.

#include "roma/model/Rope.h"

#include <cmath>
#include <cstring>

namespace roma {
namespace {

// The Python literal 2 * math.pi, which torch casts to the tensor's opmath
// (float) before multiplying.
constexpr float kTwoPi = (float)(2.0 * 3.14159265358979323846);

template <class Round>
std::vector<float> build(const std::vector<float>& periods, int64_t H, int64_t W,
                         Round r) {
    const int64_t q = (int64_t)periods.size(), pairs = 2 * q;
    std::vector<float> out((size_t)(H * W * pairs * 2));
    auto coord = [&](int64_t i, int64_t n) {
        const float c = r(r((float)i + 0.5f) / (float)n);
        return r(r(2.0f * c) - 1.0f);
    };
    for (int64_t y = 0; y < H; ++y)
        for (int64_t x = 0; x < W; ++x) {
            const float cy = coord(y, H), cx = coord(x, W);
            float* o = &out[(size_t)((y * W + x) * pairs * 2)];
            for (int64_t k = 0; k < pairs; ++k) {
                const float c = k < q ? cy : cx;
                const float a = r(r(kTwoPi * c) / periods[(size_t)(k % q)]);
                o[2 * k] = r(std::cos(a));
                o[2 * k + 1] = r(std::sin(a));
            }
        }
    return out;
}

}  // namespace

// Rotate-half pairs element k of a head with k + D/2; nn::rope pairs 2k with
// 2k+1. Moving row k to 2k and row k + D/2 to 2k+1 in q AND k makes the two
// the same rotation, and q.k is invariant under one permutation of both.
std::vector<float> permute_qk_rows(const std::vector<float>& src, int64_t width,
                                   int heads, int64_t cols) {
    const int64_t hd = width / heads, half = hd / 2;
    std::vector<float> dst(src);
    for (int part = 0; part < 2; ++part)
        for (int h = 0; h < heads; ++h)
            for (int64_t k = 0; k < half; ++k)
                for (int j = 0; j < 2; ++j) {
                    const int64_t from = part * width + h * hd + k + j * half;
                    const int64_t to = part * width + h * hd + 2 * k + j;
                    std::memcpy(&dst[(size_t)(to * cols)], &src[(size_t)(from * cols)],
                                (size_t)cols * sizeof(float));
                }
    return dst;
}

std::vector<float> fold_qkv_bias(const std::vector<float>& bias,
                                 const std::vector<float>& mask, int64_t width, int heads) {
    std::vector<float> v(bias);
    for (size_t i = 0; i < v.size() && i < mask.size(); ++i) v[i] *= mask[i];
    return permute_qk_rows(v, width, heads, 1);
}

float to_bf16(float f) {
    uint32_t u;
    std::memcpy(&u, &f, 4);
    u = (u + 0x7FFFu + ((u >> 16) & 1u)) & 0xFFFF0000u;
    std::memcpy(&f, &u, 4);
    return f;
}

std::vector<float> backbone_rope(const std::vector<float>& periods, int64_t H, int64_t W) {
    return build(periods, H, W, [](float v) { return v; });
}

std::vector<float> matcher_rope_bf16(const std::vector<float>& periods, int64_t H,
                                     int64_t W) {
    return build(periods, H, W, to_bf16);
}

}  // namespace roma
