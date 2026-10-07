#pragma once
// The two axial RoPE tables, built on the host the way the reference builds
// them: coords (arange(0.5, n) / n) * 2 - 1 per axis, angle 2*pi*coord/period,
// the first half of each head's pairs on the row axis and the second on the
// column axis. Both are [H*W, head_dim/2, 2] holding (cos, sin) of pair k.

#include <cstdint>
#include <vector>

namespace roma {

// torch's float -> bfloat16, round to nearest even; roma.slang has the twin.
float to_bf16(float f);

// The rows of a fused [q; k; v] projection (or its bias, cols = 1) with
// q's and k's moved from rotate-half pairing to adjacent pairing.
std::vector<float> permute_qk_rows(const std::vector<float>& src, int64_t width, int heads,
                                   int64_t cols);

// DINOv3's qkv bias as the device wants it: times its K-bias mask (the
// reference's LinearKMaskedBias), then permuted as the weight rows are.
std::vector<float> fold_qkv_bias(const std::vector<float>& bias,
                                 const std::vector<float>& mask, int64_t width, int heads);

// The backbone's, in fp32 (pos_embed_rope_dtype="fp32").
std::vector<float> backbone_rope(const std::vector<float>& periods, int64_t H, int64_t W);

// The multi-view transformer's, with every eager op rounded to bf16.
std::vector<float> matcher_rope_bf16(const std::vector<float>& periods, int64_t H,
                                     int64_t W);

}  // namespace roma
