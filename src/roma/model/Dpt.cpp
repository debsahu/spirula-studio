// The DPT head RoMa v2 puts on its matcher, composed from the architecture
// (Ranftl et al., "Vision Transformers for Dense Prediction"): four token maps
// reassembled to strides 4, 8, 16 and 32, refined coarse to fine through
// fusion blocks, and a small conv head at stride 4.
//
// Two conventions that are not guessable: every bilinear resize here is
// align_corners=True, and the residual units' ReLU is in place, so the skip
// adds relu(x) rather than x.

#include "roma/model/Model.h"

#include "roma/model/Dump.h"
#include "nn/Ops.h"

#include <string>

namespace roma {
namespace {

using nn::Act;
using nn::DType;
using nn::Tensor;

constexpr float kNormEps = 1e-5f;   // nn.LayerNorm's default
constexpr uint64_t kSlack = 33ull << 20;   // the conv column chunk (nn/OpConv.cpp)
const std::string kHead = "matcher.head.";

void conv3x3(vk::Arena& arena, const Weights& w, const Tensor& out, const Tensor& in,
             const std::string& name, bool bias, Act act = Act::None, int stride = 1) {
    nn::ConvOpts co;
    co.pad_y = co.pad_x = 1;
    co.stride_y = co.stride_x = stride;
    co.act = act;
    if (bias) co.bias = w.get(name + ".bias");
    nn::conv2d(arena, out, in, w.get(name + ".weight"), 3, 3, co);
}

void conv1x1(const Weights& w, const Tensor& out, const Tensor& in, const std::string& name,
             int64_t rows) {
    nn::LinearOpts lo;
    lo.bias = w.get(name + ".bias");
    const Tensor wt = w.get(name + ".weight");
    nn::linear(out.view(rows, out.numel() / rows), in.view(rows, in.numel() / rows),
               wt.asMatrix(), lo);
}

// out = conv2(relu(conv1(relu(x)))) + relu(x). `x` is overwritten with
// relu(x), which is what the in-place ReLU does to the reference's input.
void residual_unit(vk::Arena& arena, const Weights& w, const Tensor& out, const Tensor& x,
                   const std::string& name) {
    vk::ArenaScope scope(arena);
    nn::unary(x, x, Act::Relu);
    Tensor t = nn::arena_tensor(arena, DType::F32, x.shape[0], x.shape[1], x.shape[2]);
    conv3x3(arena, w, t, x, name + ".conv1", true, Act::Relu);
    conv3x3(arena, w, out, t, name + ".conv2", true);
    nn::add(out, out, x);
}

// One fusion block: (prev + RCU1(skip)) -> RCU2 -> resize -> 1x1. `skip` may
// be empty (refinenet4); the result is [ho, wo, F].
void fusion(vk::Arena& arena, const Weights& w, int r, const Tensor& prev, const Tensor& skip,
            int64_t ho, int64_t wo, const Tensor& out) {
    const std::string n = kHead + "scratch.refinenet" + std::to_string(r) + ".";
    vk::ArenaScope scope(arena);
    const int64_t h = prev.shape[0], wd = prev.shape[1], F = prev.shape[2];
    Tensor sum = nn::arena_tensor(arena, DType::F32, h, wd, F);
    if (skip.valid()) {
        residual_unit(arena, w, sum, skip, n + "resConfUnit1");
        nn::add(sum, sum, prev);
    } else {
        nn::copy(sum, prev);
    }
    Tensor y = nn::arena_tensor(arena, DType::F32, h, wd, F);
    residual_unit(arena, w, y, sum, n + "resConfUnit2");
    Tensor up = nn::arena_tensor(arena, DType::F32, ho, wo, F);
    nn::resize_bilinear(up, y, true);
    conv1x1(w, out, up, n + "out_conv", ho * wo);
}

}  // namespace

uint64_t dpt_plan_bytes(const Weights& w, int64_t h, int64_t wd) {
    const MatcherHparams& mp = w.matcher();
    const std::vector<int64_t>& c = mp.dpt_channels;
    const int64_t F = mp.dpt_features, n = h * wd;
    const int64_t h4 = (h + 1) / 2, w4 = (wd + 1) / 2;
    // The four 256-wide projections stay live until the fusion reaches them.
    const int64_t rn = F * (16 * n + 4 * n + n + h4 * w4);
    // Reassembly: rn, the four resized maps, the normalized tokens, one
    // projection and the transposed conv's scatter buffer.
    const int64_t layers = 16 * n * c[0] + 4 * n * c[1] + n * c[2] + h4 * w4 * c[3];
    const int64_t one = std::max({n * c[0] + 16 * n * c[0], n * c[1] + 4 * n * c[1], n * c[3]});
    const int64_t reassemble = rn + layers + n * mp.out + one;
    // Fusion: rn, the outputs at strides 16, 8 and 4, the stride-2 output, and
    // the finest block's sum, y, unit scratch and upsample.
    const int64_t live = rn + 21 * n * F + 64 * n * F;
    const int64_t fuse = live + 3 * 16 * n * F + 64 * n * F;
    const int64_t head = live + 32 * n * F + 8 * n * F + 16 * n * 32;
    return (uint64_t)std::max({reassemble, fuse, head}) * 4 + kSlack;
}

void dpt_head(const Weights& w, vk::Arena& arena, const Tensor& tap0, const Tensor& x,
              int64_t h, int64_t wd, const Tensor& out) {
    const MatcherHparams& mp = w.matcher();
    const std::vector<int64_t>& c = mp.dpt_channels;
    const int64_t F = mp.dpt_features, n = h * wd, D = mp.out;
    const int64_t h4 = (h + 1) / 2, w4 = (wd + 1) / 2;

    vk::ArenaScope scope(arena);
    const Tensor rn[4] = {nn::arena_tensor(arena, DType::F32, 4 * h, 4 * wd, F),
                          nn::arena_tensor(arena, DType::F32, 2 * h, 2 * wd, F),
                          nn::arena_tensor(arena, DType::F32, h, wd, F),
                          nn::arena_tensor(arena, DType::F32, h4, w4, F)};
    {
        vk::ArenaScope inner(arena);
        const Tensor layer[4] = {nn::arena_tensor(arena, DType::F32, 4 * h, 4 * wd, c[0]),
                                 nn::arena_tensor(arena, DType::F32, 2 * h, 2 * wd, c[1]),
                                 nn::arena_tensor(arena, DType::F32, h, wd, c[2]),
                                 nn::arena_tensor(arena, DType::F32, h4, w4, c[3])};
        for (int i = 0; i < 4; ++i) {
            vk::ArenaScope one(arena);
            // The head reads [tap0, tap0, x, x]; LayerNorm is the same for each pair.
            Tensor tn = nn::arena_tensor(arena, DType::F32, n, D);
            nn::layer_norm(tn, i < 2 ? tap0.view(n, D) : x.view(n, D),
                           w.get(kHead + "norm.weight"), w.get(kHead + "norm.bias"),
                           kNormEps);
            const std::string pj = kHead + "projects." + std::to_string(i);
            const std::string rs = kHead + "resize_layers." + std::to_string(i);
            const Tensor proj = i == 2 ? layer[2]
                                       : nn::arena_tensor(arena, DType::F32, h, wd, c[i]);
            conv1x1(w, proj, tn, pj, n);
            if (i == 0)
                nn::conv_transpose4x4(arena, layer[0], proj, w.get(rs + ".weight"),
                                      w.get(rs + ".bias"));
            else if (i == 1)
                nn::conv_transpose2x2(arena, layer[1], proj, w.get(rs + ".weight"),
                                      w.get(rs + ".bias"));
            else if (i == 3)
                conv3x3(arena, w, layer[3], proj, rs, true, Act::None, 2);
        }
        for (int i = 0; i < 4; ++i) {
            static const char* kDump[4] = {"dpt_l1", "dpt_l2", "dpt_l3", "dpt_l4"};
            dump_tensor(kDump[i], layer[i], {layer[i].shape[0], layer[i].shape[1],
                                             layer[i].shape[2]});
            conv3x3(arena, w, rn[i], layer[i],
                    kHead + "scratch.layer" + std::to_string(i + 1) + "_rn", false);
        }
    }

    Tensor f4 = nn::arena_tensor(arena, DType::F32, h, wd, F);
    fusion(arena, w, 4, rn[3], Tensor{}, h, wd, f4);
    dump_tensor("dpt_rn4", f4, {h, wd, F});
    Tensor f3 = nn::arena_tensor(arena, DType::F32, 2 * h, 2 * wd, F);
    fusion(arena, w, 3, f4, rn[2], 2 * h, 2 * wd, f3);
    dump_tensor("dpt_rn3", f3, {2 * h, 2 * wd, F});
    Tensor f2 = nn::arena_tensor(arena, DType::F32, 4 * h, 4 * wd, F);
    fusion(arena, w, 2, f3, rn[1], 4 * h, 4 * wd, f2);
    dump_tensor("dpt_rn2", f2, {4 * h, 4 * wd, F});
    Tensor f1 = nn::arena_tensor(arena, DType::F32, 8 * h, 8 * wd, F);
    fusion(arena, w, 1, f2, rn[0], 8 * h, 8 * wd, f1);
    dump_tensor("dpt_rn1", f1, {8 * h, 8 * wd, F});

    Tensor c1 = nn::arena_tensor(arena, DType::F32, 8 * h, 8 * wd, F / 2);
    conv3x3(arena, w, c1, f1, kHead + "scratch.output_conv1", true);
    dump_tensor("dpt_conv1", c1, {8 * h, 8 * wd, F / 2});
    // patch 16 / down ratio 4: the head's output is at stride 4.
    Tensor s4 = nn::arena_tensor(arena, DType::F32, 4 * h, 4 * wd, F / 2);
    nn::resize_bilinear(s4, c1, true);
    Tensor c2 = nn::arena_tensor(arena, DType::F32, 4 * h, 4 * wd, 32);
    conv3x3(arena, w, c2, s4, kHead + "scratch.output_conv2.0", true, Act::Relu);
    conv1x1(w, out, c2, kHead + "scratch.output_conv2.2", 16 * n);
}

}  // namespace roma
