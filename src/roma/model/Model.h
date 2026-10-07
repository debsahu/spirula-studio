#pragma once
// The three networks behind roma/Roma.h, each a straight-line forward pass on
// nn:: ops into caller-owned outputs, with the arena bytes it needs beside it
// (the arena refuses to grow mid-pass, so a plan that under-counts fails).
//
// Shapes: an image is [H, W, 3] ImageNet-normalized; a token map is
// [h*w, C] row-major with h = H/16; the coarse output is [4h, 4w, 3] holding
// the normalized warp (x, y) and the overlap logit.

#include "roma/Roma.h"
#include "roma/model/Weights.h"
#include "nn/Tensor.h"
#include "nn/vk/Memory.h"

#include <algorithm>
#include <vector>

namespace roma {

// Each stage's arena peak above the head it started at, against its plan, so
// a term that never binds the overall maximum is still checked.
class StageLog {
public:
    explicit StageLog(vk::Arena& a) : arena_(a) {}
    template <class F>
    void run(const char* name, uint64_t plan, F&& f) {
        const uint64_t base = arena_.mark();
        overall_ = std::max<uint64_t>(overall_, arena_.highWater());
        arena_.resetHighWater();
        f();
        stages_.push_back({name, arena_.highWater() - base, plan});
        overall_ = std::max<uint64_t>(overall_, arena_.highWater());
    }
    void clear() { stages_.clear(); }
    const std::vector<Model::Stage>& stages() const { return stages_; }
    uint64_t overall() const { return std::max<uint64_t>(overall_, arena_.highWater()); }

private:
    vk::Arena& arena_;
    std::vector<Model::Stage> stages_;
    uint64_t overall_ = 0;
};

// A device RoPE table cached per token grid.
class RopeTable {
public:
    ~RopeTable() { release(); }
    void release();
    const nn::Tensor& get(const std::vector<float>& periods, int64_t h, int64_t w,
                          bool bf16);

private:
    nn::DevicePtr blob_ = 0;
    nn::Tensor    t_;
    int64_t       h_ = 0, w_ = 0;
};

// DINOv3 ViT-L/16, run to its last tap. Writes the patch rows of the final
// LayerNorm after blocks taps[0] and taps[1].
class Backbone {
public:
    void run(const Weights& w, vk::Arena& arena, const nn::Tensor& image,
             const nn::Tensor& tap0, const nn::Tensor& tap1, int64_t h, int64_t wd);
    static uint64_t planBytes(const Weights& w, int64_t h, int64_t wd);

private:
    RopeTable rope_;
};

// VGG19-BN features[:27]: the maps before each MaxPool, at strides 1, 2, 4.
class FineFeatures {
public:
    static void run(const Weights& w, vk::Arena& arena, const nn::Tensor& image,
                    const nn::Tensor taps[3], int64_t H, int64_t W);
    static uint64_t planBytes(const Weights& w, int64_t H, int64_t W);
};

// The multi-view transformer, the similarity softmax and match embedding, and
// the DPT head: A's coarse warp into B.
class CoarseMatcher {
public:
    ~CoarseMatcher();
    void run(const Weights& w, vk::Arena& arena, const nn::Tensor taps_a[2],
             const nn::Tensor taps_b[2], int64_t h, int64_t wd, const nn::Tensor& out,
             StageLog& log);
    struct Plan {
        uint64_t transformer = 0, similarity = 0, head = 0;
        uint64_t total() const { return std::max({transformer, similarity, head}); }
    };
    static Plan plan(const Weights& w, int64_t h, int64_t wd);

private:
    void ensurePosEmbed(const Weights& w, int64_t h, int64_t wd);

    RopeTable rope_;
    nn::DevicePtr pos_blob_ = 0;   // [out, h*w]: the match embedding's table, transposed
    nn::Tensor pos_t_;
    int64_t pos_h_ = 0, pos_w_ = 0;
};

// out = the DPT head over [tap0, tap0, x, x], where x = tap1 + mv + match_emb.
void dpt_head(const Weights& w, vk::Arena& arena, const nn::Tensor& tap0,
              const nn::Tensor& x, int64_t h, int64_t wd, const nn::Tensor& out);
uint64_t dpt_plan_bytes(const Weights& w, int64_t h, int64_t wd);

// False under SS_ROMA_ROPE_F32=1, which runs the matcher's RoPE in fp32 to
// match compare_torch.py --ref rope32: a diagnostic, not a model.
bool matcher_rope_rounds();

// The multi-view transformer's bf16 RoPE (shaders/roma.slang).
void rope_half_bf16(const nn::Tensor& x, const nn::Tensor& cs, int n_heads, int head_dim,
                    int64_t n, int batch, int64_t row_stride);

}  // namespace roma
